#pragma once
#include <ESPressio_Execution.hpp>
#include <ESPressio_Synchronization.hpp>
#include <array>
#include <cassert>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

/// Test-only deterministic scheduling gate over real host threads; platform allocations occur at Initialize.
class HostRuntime final : public ESPressio::System::Execution::IExecutionProvider,
                          public ESPressio::System::Synchronization::ISynchronizationProvider {
    using Result=ESPressio::System::PlatformResult;
    using Status=ESPressio::System::PlatformStatus;
    using Handle=ESPressio::System::Execution::ExecutionHandle;
    struct Context { std::thread Thread; bool Used=false; };
    std::array<Context,32> _contexts{};
    mutable std::mutex _gateMutex;
    std::condition_variable _gate;
    bool _paused=false;
    inline static thread_local Handle _current=0;
public:
    std::atomic<unsigned> Created{0},Joined{0},Signals{0},Waits{0};
    bool FailSignal=false,FailExecution=false,RequireEntryWaitBeforeCreateReturns=false;
    std::atomic<std::uint32_t> FreeStack{1024};
    class Signal final : public ESPressio::System::Synchronization::ISignal {
        HostRuntime& _runtime;
        std::mutex _mutex; std::condition_variable _ready; bool _set;
    public:
        Signal(HostRuntime& runtime,bool set):_runtime(runtime),_set(set) {}
        Result Give() noexcept override { {std::lock_guard<std::mutex> lock(_mutex); _set=true;} _ready.notify_one(); return Result::Succeeded(); }
        Result GiveFromInterrupt() noexcept override { return Give(); }
        Result Wait(std::uint32_t milliseconds=UINT32_MAX) noexcept override {
            ++_runtime.Waits;
            std::unique_lock<std::mutex> lock(_mutex);
            if (milliseconds==UINT32_MAX) _ready.wait(lock,[&]{return _set;});
            else if (!_ready.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return _set;})) return Result::Failed(Status::Timeout);
            _set=false; lock.unlock(); _runtime.Gate(); return Result::Succeeded();
        }
        Result Reset() noexcept override { std::lock_guard<std::mutex> lock(_mutex); _set=false; return Result::Succeeded(); }
    };
    HostRuntime() {
        ESPressio::System::Execution::SetProvider(this);
        ESPressio::System::Synchronization::SetProvider(this);
    }
    ~HostRuntime() {
        for (const auto& c:_contexts) assert(!c.Used);
        ESPressio::System::Execution::ResetProvider(); ESPressio::System::Synchronization::ResetProvider();
    }
    void Pause() { std::lock_guard<std::mutex> lock(_gateMutex); _paused=true; }
    void ResumeGate() { {std::lock_guard<std::mutex> lock(_gateMutex); _paused=false;} _gate.notify_all(); }
    void Gate() { std::unique_lock<std::mutex> lock(_gateMutex); _gate.wait(lock,[&]{return !_paused;}); }
    ESPressio::System::Execution::ExecutionCreationResult Create(
        ESPressio::System::Execution::ExecutionEntry,void*,const ESPressio::System::Execution::ExecutionConfiguration&) override {
        return {Result::Failed(Status::Unsupported),0};
    }
    ESPressio::System::Execution::ExecutionCreationResult CreateJoinable(
        ESPressio::System::Execution::ExecutionEntry entry,void* context,const ESPressio::System::Execution::ExecutionConfiguration&) override {
        if (FailExecution) return {Result::Failed(Status::Unavailable),0};
        for (std::size_t i=0;i<_contexts.size();++i) if (!_contexts[i].Used) {
            auto& c=_contexts[i]; c.Used=true; ++Created;
            const auto waitsBefore=Waits.load();
            c.Thread=std::thread([=]{_current=i+1; Gate(); entry(context); _current=0;});
            if (RequireEntryWaitBeforeCreateReturns) {
                const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (Waits.load()==waitsBefore) {
                    assert(std::chrono::steady_clock::now()<limit);
                    std::this_thread::yield();
                }
            }
            return {Result::Succeeded(),i+1};
        }
        return {Result::Failed(Status::Unavailable),0};
    }
    Result Join(Handle h) override {
        if (h==_current || !h || h>_contexts.size()) return Result::Failed(Status::InvalidArgument);
        auto& c=_contexts[h-1]; assert(c.Used); c.Thread.join(); c.Used=false; ++Joined; return Result::Succeeded();
    }
    Result Destroy(Handle) override { std::abort(); } // No forced deletion is ever allowed in these tests.
    Result Suspend(Handle) override { std::abort(); }
    Result Resume(Handle) override { std::abort(); }
    Handle Current() const noexcept override { return _current; }
    std::uint32_t MinimumFreeStackBytes(Handle) const noexcept override { return FreeStack; }
    std::uint32_t ProcessorCount() const noexcept override { return 2; }
    void SleepMilliseconds(std::uint32_t n) override { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }
    void Yield() override { std::this_thread::yield(); }
    bool SupportsProcessorAffinity() const noexcept override { return true; }
    std::unique_ptr<ESPressio::System::Synchronization::ISignal> CreateBinarySignal(bool set=false) override {
        if (FailSignal) return {};
        ++Signals; return std::make_unique<Signal>(*this,set);
    }
};
