#include <ESPressio_Task.hpp>
#include "HostRuntime.hpp"
#include <array>
#include <new>
static bool denyHeap=false;
void* operator new(std::size_t n) { if (denyHeap) std::abort(); if (auto* p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
using namespace ESPressio::Task;
template<class F> void Until(F f) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!f()) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
}
struct Owner {
    std::array<int,16> Seen{},Discarded{};
    std::atomic<unsigned> Count{0},Drops{0};
    std::atomic<bool> Hold{false},Entered{false},Release{false};
    TaskExecutor<int,2>* Executor=nullptr;
    std::atomic<TaskExecutionStatus> SelfStop{TaskExecutionStatus::Success};
    void Execute(const int& value) noexcept {
        SelfStop=Executor->Stop();
        Seen[Count.load()]=value; ++Count; Entered=true;
        while (Hold && !Release) std::this_thread::yield();
    }
    void Discard(const int& value) noexcept { Discarded[Drops.load()]=value; ++Drops; }
};
int main() {
    HostRuntime runtime;
    for (auto policy:std::array<TaskQueueOverflowPolicy,3>{TaskQueueOverflowPolicy::Reject,TaskQueueOverflowPolicy::DropNewest,TaskQueueOverflowPolicy::DropOldest}) {
        runtime.Pause();
        TaskExecutorConfiguration configuration; configuration.QueueDepth=2; configuration.OverflowPolicy=policy;
        Owner owner; TaskExecutor<int,2> executor(configuration); owner.Executor=&executor;
        assert((executor.Initialize<Owner,&Owner::Execute,&Owner::Discard>(owner)==TaskExecutionStatus::Success));
        denyHeap=true;
        assert(executor.Submit(0)==TaskExecutionStatus::NotStarted);
        assert(executor.Start()==TaskExecutionStatus::Success && executor.Start()==TaskExecutionStatus::AlreadyStarted);
        assert(executor.Submit(1)==TaskExecutionStatus::Success); // T1 Ready slot
        assert(executor.Submit(2)==TaskExecutionStatus::Success); // FIFO 0
        assert(executor.Submit(3)==TaskExecutionStatus::Success); // FIFO 1
        const auto fourth=executor.Submit(4);
        assert(fourth==(policy==TaskQueueOverflowPolicy::DropOldest ? TaskExecutionStatus::Success : TaskExecutionStatus::QueueFull));
        runtime.ResumeGate(); Until([&]{return executor.GetStatistics().Completed==3;});
        assert(owner.Seen[0]==1);
        assert(owner.Seen[1]==(policy==TaskQueueOverflowPolicy::DropOldest ? 3 : 2));
        assert(owner.Seen[2]==(policy==TaskQueueOverflowPolicy::DropOldest ? 4 : 3));
        assert(owner.SelfStop==TaskExecutionStatus::SelfJoin);
        assert(owner.Drops==(policy==TaskQueueOverflowPolicy::DropOldest ? 1u : 0u));
        if (owner.Drops) assert(owner.Discarded[0]==2);
        assert(executor.Stop()==TaskExecutionStatus::Success);
        denyHeap=false;
    }
    // Stop discards both worker Ready lease and external FIFO records without calling the handler.
    {
        runtime.Pause(); TaskExecutorConfiguration config; config.QueueDepth=2;
        Owner owner; TaskExecutor<int,2> executor(config); owner.Executor=&executor;
        assert((executor.Initialize<Owner,&Owner::Execute,&Owner::Discard>(owner)==TaskExecutionStatus::Success));
        executor.Start(); executor.Submit(1); executor.Submit(2); executor.Submit(3);
        std::thread stop([&]{assert(executor.Stop()==TaskExecutionStatus::Success);});
        Until([&]{return executor.Submit(4)==TaskExecutionStatus::NotInitialized;});
        runtime.ResumeGate(); stop.join();
        assert(owner.Count==0 && owner.Drops==3 && executor.GetStatistics().Dropped==3);
    }
    // Finite blocking policy waits on capacity and shutdown wakes a blocked producer.
    {
        TaskExecutorConfiguration config; config.QueueDepth=2; config.OverflowPolicy=TaskQueueOverflowPolicy::Block;
        Owner owner; TaskExecutor<int,2> executor(config); owner.Executor=&executor; owner.Hold=true;
        assert((executor.Initialize<Owner,&Owner::Execute,&Owner::Discard>(owner)==TaskExecutionStatus::Success));
        executor.Start(); executor.Submit(1); Until([&]{return owner.Entered.load();});
        executor.Submit(2); executor.Submit(3);
        assert(executor.Submit(4,1)==TaskExecutionStatus::QueueFull);
        std::atomic<bool> done{false}; std::atomic<TaskExecutionStatus> status{TaskExecutionStatus::Success};
        const auto waits=runtime.Waits.load();
        std::thread producer([&]{status=executor.Submit(5,10000); done=true;});
        Until([&]{return runtime.Waits>waits;}); assert(!done);
        std::thread stop([&]{assert(executor.Stop()==TaskExecutionStatus::Success);});
        Until([&]{return done.load();}); assert(status==TaskExecutionStatus::Stopping);
        owner.Release=true; producer.join(); stop.join(); assert(owner.Count==1 && owner.Drops==2);
    }
    assert(runtime.Created==5 && runtime.Joined==5 && runtime.Signals==15);
}
