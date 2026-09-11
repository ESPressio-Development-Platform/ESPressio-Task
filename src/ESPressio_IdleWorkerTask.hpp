#pragma once
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <exception>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_TaskRuntime.hpp"
#include "ESPressio_TaskWorkHandle.hpp"

namespace ESPressio::Task {
/// <summary>One pre-created joinable context, one binary signal and one inline owned descriptor; no work queue.</summary>
/// <remarks>Initialize/Shutdown are external owner operations. Normal-context TryAssign/Cancel may run concurrently.
/// Provider lifetimes cover the worker. Every execution/refill thunk is noexcept. The descriptor is destroyed before
/// Idle publication. Generation is retained across reinitialization and fails closed at exhaustion.</remarks>
template<class TWorkItem> class IdleWorkerTask final {
    static_assert(std::is_nothrow_move_constructible_v<TWorkItem> && std::is_nothrow_destructible_v<TWorkItem>,
                  "IdleWorkerTask requires nothrow move construction and destruction");
    enum class State : std::uint8_t { Uninitialized, Idle, Ready, Executing };
    alignas(TWorkItem) std::byte _slot[sizeof(TWorkItem)];
    mutable System::Synchronization::Mutex _mutex;
    std::atomic<State> _state{State::Uninitialized};
    std::atomic<bool> _stopping{false};
    bool _joining=false;
    std::uint64_t _generation=0;
    // Only release notifications, never descriptors or additional assignment capacity.
    // At most one notification per unique generation can be pending, so this cannot wrap.
    std::atomic<std::uint64_t> _released{0};
    void* _owner=nullptr;
    void (*_execute)(void*,TWorkItem&) noexcept=nullptr;
    void (*_refill)(void*,IdleWorkerTask&) noexcept=nullptr;
    std::unique_ptr<System::Synchronization::ISignal> _wake;
    System::Execution::IExecutionProvider* _provider=nullptr;
    std::atomic<TaskHandle> _task{System::Execution::InvalidExecutionHandle};
    struct Counters {
        std::atomic<std::uint64_t> Submitted{0},Completed{0},Rejected{0},Dropped{0},Cancelled{0};
        std::atomic<std::uint32_t> ConfiguredStackSize{0},MinimumFreeStack{0};
        void Reset() noexcept { Submitted=0; Completed=0; Rejected=0; Dropped=0; Cancelled=0; ConfiguredStackSize=0; MinimumFreeStack=0; }
        TaskExecutionStatistics Read() const noexcept {
            return {Submitted.load(),Completed.load(),Rejected.load(),Dropped.load(),Cancelled.load(),
                    ConfiguredStackSize.load(),MinimumFreeStack.load()};
        }
    } _statistics;

    TWorkItem& Item() noexcept { return *std::launder(reinterpret_cast<TWorkItem*>(_slot)); }
    static void Entry(void* context) { static_cast<IdleWorkerTask*>(context)->Run(); }
    void Run() noexcept {
        for (;;) {
            // Idle inspection never holds the admission lock. This also prevents
            // a refill attempt from losing progress to a diagnostic/idle read.
            if (_state==State::Idle && !_stopping && _released==0) {
                (void)_wake->Wait();
                continue;
            }
            void (*refill)(void*,IdleWorkerTask&) noexcept=nullptr;
            bool execute=false;
            {
                std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
                if (_stopping) {
                    if (_state==State::Ready) { Item().~TWorkItem(); ++_statistics.Dropped; }
                    _state=State::Idle; _released=0;
                    return; // Provider trampoline owns termination; external Join reclaims it.
                }
                if (_released) { --_released; refill=_refill; }
                else if (_state==State::Ready) { _state=State::Executing; execute=true; }
            }
            if (refill) {
                // Claim was serialized before Shutdown. Shutdown waits for this already-claimed
                // owner hook through Join; no later hook is claimed once stopping is published.
                refill(_owner,*this);
                continue;
            }
            if (execute) {
                _execute(_owner,Item());
                std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
                Item().~TWorkItem();
                ++_statistics.Completed;
                const auto free=_provider->MinimumFreeStackBytes(_task);
                if (free<_statistics.MinimumFreeStack) _statistics.MinimumFreeStack=free;
                _state=State::Idle;
                if (!_stopping && _refill) ++_released;
                continue;
            }
            // Ready/stop publication always precedes Give. A coalesced or stale signal
            // is harmless: the next iteration rechecks the slot and release state.
            (void)_wake->Wait();
        }
    }
public:
    IdleWorkerTask() noexcept = default;
    IdleWorkerTask(const IdleWorkerTask&)=delete;
    IdleWorkerTask& operator=(const IdleWorkerTask&)=delete;
    /// <summary>Externally joins the worker; destroying it from its own handler is fatal misuse.</summary>
    ~IdleWorkerTask() { if (Shutdown()!=TaskExecutionStatus::Success) std::terminate(); }
    /// <summary>Transactionally binds fixed thunks and creates the signal/context; success leaves an accepting Idle worker.</summary>
    template<class TOwner,void (TOwner::*TExecute)(TWorkItem&) noexcept,
             void (TOwner::*TOnSlotReleased)(IdleWorkerTask&) noexcept=nullptr>
    TaskExecutionStatus Initialize(TOwner& owner,TaskExecutionConfiguration configuration={}) {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex); // Resolves the mutex before any hot path.
        if (_state!=State::Uninitialized) return TaskExecutionStatus::AlreadyInitialized;
        if (!configuration.StackSize || configuration.Core>255 || TExecute==nullptr)
            return TaskExecutionStatus::InvalidConfiguration;
        if (configuration.MemoryPolicy==TaskMemoryPolicy::External) return TaskExecutionStatus::UnsupportedMemoryPolicy;
        auto* signals=System::Synchronization::Provider();
        if (!signals) return TaskExecutionStatus::SignalUnavailable;
        try { _wake=signals->CreateBinarySignal(false); }
        catch (...) { return TaskExecutionStatus::SignalUnavailable; }
        if (!_wake) return TaskExecutionStatus::SignalUnavailable;
        _owner=&owner;
        _execute=[](void* context,TWorkItem& item) noexcept { (static_cast<TOwner*>(context)->*TExecute)(item); };
        if constexpr (TOnSlotReleased!=nullptr)
            _refill=[](void* context,IdleWorkerTask& worker) noexcept { (static_cast<TOwner*>(context)->*TOnSlotReleased)(worker); };
        else _refill=nullptr;
        _provider=&System::Execution::Provider();
        TaskCreationResult created;
        try { created=TaskRuntime::CreateJoinable(Entry,this,configuration,*_provider); }
        catch (...) { created.Status=TaskExecutionStatus::TaskCreationFailed; }
        if (!created) { _wake.reset(); _provider=nullptr; _owner=nullptr; _execute=nullptr; _refill=nullptr; return created.Status; }
        _task=created.Handle;
        _statistics.Reset(); _statistics.ConfiguredStackSize=configuration.StackSize;
        _statistics.MinimumFreeStack=_provider->MinimumFreeStackBytes(_task);
        _stopping=false; _joining=false; _released=0; _state=State::Idle;
        return TaskExecutionStatus::Success;
    }
    /// <summary>Attempts one admission without waiting for a lock or consuming a rejected source.</summary>
    TaskWorkSubmission TryAssign(TWorkItem&& item) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if (!lock.owns_lock()) { ++_statistics.Rejected; return {TaskExecutionStatus::Busy,{}}; }
        if (_state==State::Uninitialized) return {TaskExecutionStatus::NotInitialized,{}};
        if (_stopping) return {TaskExecutionStatus::Stopping,{}};
        if (_state!=State::Idle) { ++_statistics.Rejected; return {TaskExecutionStatus::Busy,{}}; }
        if (_generation==std::numeric_limits<std::uint64_t>::max()) return {TaskExecutionStatus::GenerationExhausted,{}};
        new (_slot) TWorkItem(std::move(item));
        ++_generation; ++_statistics.Submitted; _state=State::Ready;
        (void)_wake->Give();
        return {TaskExecutionStatus::Success,{this,_generation}};
    }
    /// <summary>Cancels only this worker's exact Ready generation, destroying its lease before publishing Idle.</summary>
    TaskWorkCancelStatus Cancel(TaskWorkHandle handle) noexcept {
        const auto state=_state.load();
        if (state==State::Uninitialized) return TaskWorkCancelStatus::NotInitialized;
        if (state==State::Idle) return TaskWorkCancelStatus::Stale;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if (!lock.owns_lock()) return TaskWorkCancelStatus::Busy;
        if (_state==State::Uninitialized) return TaskWorkCancelStatus::NotInitialized;
        if (_stopping) return TaskWorkCancelStatus::Stopping;
        if (handle.Worker!=this || !handle.Generation || handle.Generation!=_generation) return TaskWorkCancelStatus::Stale;
        if (_state==State::Executing) return TaskWorkCancelStatus::InProgress;
        if (_state!=State::Ready) return TaskWorkCancelStatus::Stale;
        Item().~TWorkItem(); ++_statistics.Cancelled; _state=State::Idle;
        if (_refill) ++_released;
        (void)_wake->Give();
        return TaskWorkCancelStatus::Cancelled;
    }
    /// <summary>Rejects admissions, suppresses future refill, discards Ready work and cooperatively joins current execution.</summary>
    /// <remarks>Concurrent shutdown callers receive Busy. A failed provider Join retains resources for a later retry.</remarks>
    TaskExecutionStatus Shutdown() noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if (_state==State::Uninitialized) return TaskExecutionStatus::Success;
            if (_provider->Current()==_task) return TaskExecutionStatus::SelfJoin;
            if (_joining) return TaskExecutionStatus::Busy;
            _joining=true; _stopping=true;
            (void)_wake->Give();
        }
        bool joined=false;
        try { joined=bool(_provider->Join(_task)); } catch (...) {}
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _joining=false;
        if (!joined) return TaskExecutionStatus::JoinFailed;
        _state=State::Uninitialized; _task=System::Execution::InvalidExecutionHandle;
        _wake.reset(); _provider=nullptr; _owner=nullptr; _execute=nullptr; _refill=nullptr;
        return TaskExecutionStatus::Success;
    }
    /// <summary>Identifies self-join or blocking-on-self misuse; provider installation remains fixed during ownership.</summary>
    bool IsCurrentExecution() const noexcept { const auto task=_task.load(); return task && TaskRuntime::Current()==task; }
    /// <summary>Reports whether platform resources remain owned, including while stopping.</summary>
    bool IsInitialized() const noexcept { return _state!=State::Uninitialized; }
    /// <summary>Reports current Idle admission state; another producer can win immediately afterward.</summary>
    bool IsIdle() const noexcept { return _state==State::Idle && !_stopping; }
    /// <summary>Returns fixed cumulative counters and platform stack headroom; reset only by successful Initialize.</summary>
    TaskExecutionStatistics GetStatistics() const noexcept { return _statistics.Read(); }
};
}
