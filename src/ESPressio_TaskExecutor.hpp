#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <ESPressio_SystemPlatformClock.hpp>
#include "ESPressio_IdleWorkerTask.hpp"

namespace ESPressio::Task {
/// <summary>One explicit fixed FIFO plus a reusable T1 worker; QueueDepth must fit MaximumPending.</summary>
/// <remarks>Queue storage is inline in this object, never allocated or grown. The owner chooses the object's placement.
/// The two executor signals provide blocking-producer capacity and submission quiescence; the T1 worker owns its one
/// separate wake signal. Execution/discard bindings are fixed noexcept thunks. Stop never drains application work.</remarks>
template<class TWorkItem,std::size_t MaximumPending=8> class TaskExecutor final {
    static_assert(MaximumPending>0 && std::is_trivially_copyable_v<TWorkItem> &&
                  std::is_nothrow_default_constructible_v<TWorkItem> && std::is_nothrow_copy_assignable_v<TWorkItem>,
                  "TaskExecutor requires positive fixed capacity and nothrow trivial queue records");
    TaskExecutorConfiguration _configuration;
    std::array<TWorkItem,MaximumPending> _queue{};
    std::size_t _head=0,_count=0,_submitters=0;
    mutable System::Synchronization::Mutex _mutex;
    bool _initialized=false,_started=false,_stopping=false,_joining=false;
    void* _owner=nullptr;
    void (*_handler)(void*,const TWorkItem&) noexcept=nullptr;
    void (*_discard)(void*,const TWorkItem&) noexcept=nullptr;
    std::unique_ptr<System::Synchronization::ISignal> _space,_submissionsDone;
    std::atomic<std::uint64_t> _submitted{0},_completed{0},_rejected{0},_dropped{0};
    /// Owned worker lease also reclaims a Ready assignment discarded by T1 shutdown.
    struct Work {
        TaskExecutor* Owner=nullptr;
        TWorkItem Item{};
        Work(TaskExecutor& owner,const TWorkItem& item) noexcept:Owner(&owner),Item(item) {}
        Work(Work&& other) noexcept:Owner(other.Owner),Item(other.Item) { other.Owner=nullptr; }
        Work(const Work&)=delete;
        ~Work() noexcept { if (Owner) Owner->Discard(Item); }
    };
    IdleWorkerTask<Work> _worker;
    void Discard(const TWorkItem& item) noexcept {
        ++_dropped; if (_discard) _discard(_owner,item);
    }
    void Execute(Work& work) noexcept {
        work.Owner=nullptr; // Execution claims ownership; no discard after a handled record.
        _handler(_owner,work.Item); ++_completed;
    }
    void Refill(IdleWorkerTask<Work>&) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex); Fill();
    }
    void Fill() noexcept {
        if (!_started || _stopping || !_count) return;
        Work next(*this,_queue[_head]);
        const auto assigned=_worker.TryAssign(std::move(next));
        if (!assigned) { next.Owner=nullptr; return; } // The FIFO still owns its unchanged head.
        _queue[_head]=TWorkItem{}; _head=(_head+1)%_configuration.QueueDepth; --_count;
        (void)_space->Give();
    }
    void LeaveSubmission() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        --_submitters;
        if (_stopping) (void)_space->Give(); // Baton wakes every blocked submitter during quiescence.
        if (!_submitters) (void)_submissionsDone->Give();
    }
    struct SubmissionGuard { TaskExecutor& Owner; ~SubmissionGuard() { Owner.LeaveSubmission(); } };
public:
    explicit TaskExecutor(TaskExecutorConfiguration configuration={}) noexcept:_configuration(configuration) {}
    TaskExecutor(const TaskExecutor&)=delete;
    TaskExecutor& operator=(const TaskExecutor&)=delete;
    ~TaskExecutor() { if (Stop()!=TaskExecutionStatus::Success) std::terminate(); }
    /// <summary>Returns the immutable execution/backlog configuration.</summary>
    const TaskExecutorConfiguration& GetConfiguration() const noexcept { return _configuration; }
    /// <summary>Creates all signals and the T1 context, binding a fixed owner/member handler and optional discard thunk.</summary>
    template<class TOwner,void (TOwner::*THandler)(const TWorkItem&) noexcept,
             void (TOwner::*TDiscard)(const TWorkItem&) noexcept=nullptr>
    TaskExecutionStatus Initialize(TOwner& owner) {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if (_initialized) return TaskExecutionStatus::AlreadyInitialized;
        if (!_configuration.QueueDepth || _configuration.QueueDepth>MaximumPending || THandler==nullptr ||
            _configuration.OverflowPolicy>TaskQueueOverflowPolicy::Block)
            return TaskExecutionStatus::InvalidConfiguration;
        // This implementation has inline backing; it never silently changes the requested memory policy.
        if (_configuration.QueueMemoryPolicy!=TaskMemoryPolicy::Internal) return TaskExecutionStatus::UnsupportedMemoryPolicy;
        auto* provider=System::Synchronization::Provider();
        if (!provider) return TaskExecutionStatus::SignalUnavailable;
        try { _space=provider->CreateBinarySignal(false); _submissionsDone=provider->CreateBinarySignal(false); }
        catch (...) { _space.reset(); _submissionsDone.reset(); return TaskExecutionStatus::SignalUnavailable; }
        if (!_space || !_submissionsDone) { _space.reset(); _submissionsDone.reset(); return TaskExecutionStatus::SignalUnavailable; }
        _owner=&owner;
        _handler=[](void* context,const TWorkItem& item) noexcept { (static_cast<TOwner*>(context)->*THandler)(item); };
        if constexpr (TDiscard!=nullptr) _discard=[](void* context,const TWorkItem& item) noexcept { (static_cast<TOwner*>(context)->*TDiscard)(item); };
        const auto status=_worker.template Initialize<TaskExecutor,&TaskExecutor::Execute,&TaskExecutor::Refill>(*this,_configuration.Execution);
        if (status!=TaskExecutionStatus::Success) { _space.reset(); _submissionsDone.reset(); _owner=nullptr; _handler=nullptr; _discard=nullptr; return status; }
        _head=0; _count=0; _submitters=0; _started=false; _stopping=false; _joining=false;
        _submitted=0; _completed=0; _rejected=0; _dropped=0; _initialized=true;
        return TaskExecutionStatus::Success;
    }
    /// <summary>Opens the executor admission/dispatch gate; the underlying worker already exists.</summary>
    TaskExecutionStatus Start() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if (!_initialized || _stopping) return TaskExecutionStatus::NotInitialized;
        if (_started) return TaskExecutionStatus::AlreadyStarted;
        _started=true; Fill(); return TaskExecutionStatus::Success;
    }
    /// <summary>Copies into the explicit FIFO under the selected overflow policy; newer work never bypasses an older head.</summary>
    /// <remarks>Block uses one finite monotonic budget and a capacity signal, without a polling task or retry sleeps.
    /// DropNewest rejects the caller-owned input. DropOldest calls the fixed discard thunk for the evicted accepted item.
    /// A caller in this executor's handler may submit non-blocking work but cannot wait for its own queue capacity.</remarks>
    TaskExecutionStatus Submit(const TWorkItem& item,std::uint32_t blockMilliseconds=0) noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if (!_initialized || _stopping) return TaskExecutionStatus::NotInitialized;
            if (!_started) return TaskExecutionStatus::NotStarted;
            if (blockMilliseconds==UINT32_MAX) return TaskExecutionStatus::InvalidConfiguration;
            ++_submitters;
        }
        SubmissionGuard guard{*this};
        const auto start=System::Clock::Monotonic().NowNanoseconds();
        const auto budget=std::uint64_t(blockMilliseconds)*1000000;
        for (;;) {
            std::uint32_t wait=0;
            {
                std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
                if (_stopping) return TaskExecutionStatus::Stopping;
                Fill();
                if (_count==_configuration.QueueDepth) {
                    switch (_configuration.OverflowPolicy) {
                        case TaskQueueOverflowPolicy::DropOldest:
                            Discard(_queue[_head]); _head=(_head+1)%_configuration.QueueDepth; --_count; break;
                        case TaskQueueOverflowPolicy::DropNewest:
                            ++_dropped; ++_rejected; return TaskExecutionStatus::QueueFull;
                        case TaskQueueOverflowPolicy::Reject:
                            ++_rejected; return TaskExecutionStatus::QueueFull;
                        case TaskQueueOverflowPolicy::Block: {
                            if (_worker.IsCurrentExecution() && blockMilliseconds) return TaskExecutionStatus::SelfJoin;
                            const auto elapsed=System::Clock::Monotonic().NowNanoseconds()-start;
                            if (elapsed>=budget) { ++_rejected; return TaskExecutionStatus::QueueFull; }
                            wait=static_cast<std::uint32_t>((budget-elapsed+999999)/1000000); break;
                        }
                    }
                }
                if (!wait) {
                    _queue[(_head+_count)%_configuration.QueueDepth]=item; ++_count; ++_submitted;
                    Fill();
                    if (_count<_configuration.QueueDepth) (void)_space->Give();
                    return TaskExecutionStatus::Success;
                }
            }
            (void)_space->Wait(wait); // Spurious/stale wakeups consume the same original finite budget.
        }
    }
    /// <summary>Closes admissions, cooperatively joins the worker, then discards bounded backlog and releases signals.</summary>
    TaskExecutionStatus Stop() noexcept {
        if (_worker.IsCurrentExecution()) return TaskExecutionStatus::SelfJoin;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if (!_initialized) return TaskExecutionStatus::Success;
            if (_joining) return TaskExecutionStatus::Busy;
            _joining=true; _stopping=true; _started=false;
            (void)_space->Give();
        }
        const auto status=_worker.Shutdown();
        if (status!=TaskExecutionStatus::Success) {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex); _joining=false; return status;
        }
        for (;;) {
            { std::lock_guard<System::Synchronization::Mutex> lock(_mutex); if (!_submitters) break; }
            (void)_submissionsDone->Wait();
        }
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        while (_count) {
            Discard(_queue[_head]); _queue[_head]=TWorkItem{};
            _head=(_head+1)%_configuration.QueueDepth; --_count;
        }
        _space.reset(); _submissionsDone.reset(); _initialized=false; _joining=false;
        _owner=nullptr; _handler=nullptr; _discard=nullptr;
        return TaskExecutionStatus::Success;
    }
    /// <summary>Returns independent fixed cumulative activity counters and the underlying worker's stack telemetry.</summary>
    TaskExecutionStatistics GetStatistics() const noexcept {
        auto result=_worker.GetStatistics();
        result.Submitted=_submitted; result.Completed=_completed; result.Rejected=_rejected; result.Dropped=_dropped;
        return result;
    }
    /// <summary>Compile-time FIFO storage; the single executing/Ready T1 slot is separately accounted.</summary>
    static constexpr std::size_t MaximumPendingItems=MaximumPending;
};
}
