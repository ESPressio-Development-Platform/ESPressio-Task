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
        while (_state.load(std::memory_order_acquire)==State::Uninitialized)
            (void)_wake->Wait();
        for (;;) {
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
                    return;
                }
                if (_released) { --_released; refill=_refill; }
                else if (_state==State::Ready) { _state=State::Executing; execute=true; }
            }
            if (refill) {
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
            (void)_wake->Wait();
        }
    }
public:
    IdleWorkerTask() noexcept = default;
    IdleWorkerTask(const IdleWorkerTask&)=delete;
    IdleWorkerTask& operator=(const IdleWorkerTask&)=delete;
    ~IdleWorkerTask() { if (Shutdown()!=TaskExecutionStatus::Success) std::terminate(); }
    template<class TOwner,void (TOwner::*TExecute)(TWorkItem&) noexcept,
             void (TOwner::*TOnSlotReleased)(IdleWorkerTask&) noexcept=nullptr>
    TaskExecutionStatus Initialize(TOwner& owner,TaskExecutionConfiguration configuration={}) {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
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
        if (TOnSlotReleased!=nullptr)
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
        (void)_wake->Give();
        return TaskExecutionStatus::Success;
    }
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
    bool IsCurrentExecution() const noexcept { const auto task=_task.load(); return task && TaskRuntime::Current()==task; }
    bool IsInitialized() const noexcept { return _state!=State::Uninitialized; }
    bool IsIdle() const noexcept { return _state==State::Idle && !_stopping; }
    TaskExecutionStatistics GetStatistics() const noexcept { return _statistics.Read(); }
};
}
