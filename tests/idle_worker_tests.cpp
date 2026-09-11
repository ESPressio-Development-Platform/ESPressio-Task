#include <ESPressio_IdleWorkerTask.hpp>
#include "HostRuntime.hpp"
#include <cstdlib>
#include <new>
using namespace ESPressio::Task;
static std::atomic<bool> denyHeap{false};
void* operator new(std::size_t n) {
    if (denyHeap) std::abort();
    if (auto* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
template<class F> void Until(F f) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!f()) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
}
struct Lease {
    std::atomic<unsigned>* Releases=nullptr;
    int Value=0;
    Lease(std::atomic<unsigned>& releases,int value) noexcept:Releases(&releases),Value(value) {}
    Lease(Lease&& other) noexcept:Releases(other.Releases),Value(other.Value) { other.Releases=nullptr; }
    Lease(const Lease&)=delete;
    ~Lease() noexcept { if (Releases) ++*Releases; }
};
using Worker=IdleWorkerTask<Lease>;
struct Owner {
    Worker WorkerInstance;
    std::atomic<unsigned> Releases{0},Calls{0},Refills{0};
    std::atomic<bool> Entered{false},Hold{false},Release{false},RefillOnce{false};
    std::atomic<TaskExecutionStatus> SelfStatus{TaskExecutionStatus::Success};
    void Execute(Lease&) noexcept {
        SelfStatus=WorkerInstance.Shutdown();
        ++Calls; Entered=true;
        while (Hold && !Release) std::this_thread::yield();
    }
    void Refill(Worker& worker) noexcept {
        assert(Releases.load()>0); // Slot destructor ran before the callback.
        ++Refills;
        if (RefillOnce.exchange(false)) {
            assert(worker.IsIdle());
            Lease next(Releases,99); assert(worker.TryAssign(std::move(next))); assert(!next.Releases);
        }
    }
};
int main() {
    HostRuntime runtime;
    Owner owner;
    auto& worker=owner.WorkerInstance;
    assert(!worker.IsInitialized());
    Lease uninitialized(owner.Releases,0);
    assert(worker.TryAssign(std::move(uninitialized)).Status==TaskExecutionStatus::NotInitialized && uninitialized.Releases);
    runtime.FailSignal=true;
    assert((worker.Initialize<Owner,&Owner::Execute,&Owner::Refill>(owner)==TaskExecutionStatus::SignalUnavailable));
    runtime.FailSignal=false; runtime.FailExecution=true;
    assert((worker.Initialize<Owner,&Owner::Execute,&Owner::Refill>(owner)==TaskExecutionStatus::TaskCreationFailed));
    assert(!worker.IsInitialized() && runtime.Created==0);
    runtime.FailExecution=false; runtime.Pause();
    assert((worker.Initialize<Owner,&Owner::Execute,&Owner::Refill>(owner)==TaskExecutionStatus::Success));
    assert(worker.IsIdle() && runtime.Created==1 && runtime.Signals==2); // One failed creation reclaimed its signal.
    denyHeap=true;
    Lease first(owner.Releases,1),busy(owner.Releases,2);
    const auto one=worker.TryAssign(std::move(first)); assert(one && !first.Releases);
    assert(worker.TryAssign(std::move(busy)).Status==TaskExecutionStatus::Busy && busy.Releases);
    assert(worker.Cancel(one.Handle)==TaskWorkCancelStatus::Cancelled);
    assert(worker.IsIdle() && owner.Releases==1 && owner.Calls==0);
    const auto two=worker.TryAssign(std::move(busy)); assert(two && !busy.Releases);
    assert(two.Handle.Generation>one.Handle.Generation);
    assert(worker.Cancel(one.Handle)==TaskWorkCancelStatus::Stale);
    assert(worker.Cancel({nullptr,two.Handle.Generation})==TaskWorkCancelStatus::Stale);
    assert(worker.Cancel(two.Handle)==TaskWorkCancelStatus::Cancelled);
    runtime.ResumeGate();
    Until([&]{return owner.Refills==2;});
    owner.Hold=true;
    Lease executing(owner.Releases,3);
    TaskWorkSubmission assigned;
    Until([&]{assigned=worker.TryAssign(std::move(executing)); return bool(assigned);});
    Until([&]{return owner.Entered.load();});
    assert(owner.SelfStatus==TaskExecutionStatus::SelfJoin);
    assert(worker.Cancel(assigned.Handle)==TaskWorkCancelStatus::InProgress);
    Lease rejected(owner.Releases,4);
    assert(worker.TryAssign(std::move(rejected)).Status==TaskExecutionStatus::Busy && rejected.Releases);
    owner.RefillOnce=true; runtime.FreeStack=512; owner.Release=true;
    Until([&]{return worker.GetStatistics().Completed==2 && owner.Refills==4;});
    assert(owner.Calls==2 && owner.Releases==4);
    assert(worker.GetStatistics().MinimumFreeStack==512);
    assert(worker.GetStatistics().Cancelled==2 && worker.GetStatistics().Submitted==4);
    assert(worker.Shutdown()==TaskExecutionStatus::Success && !worker.IsInitialized());
    assert(runtime.Created==1 && runtime.Joined==1);
    denyHeap=false;

    // Explicit reinitialization creates a new context but cannot revive old control tokens.
    runtime.Pause();
    assert((worker.Initialize<Owner,&Owner::Execute,&Owner::Refill>(owner)==TaskExecutionStatus::Success));
    Lease ready(owner.Releases,5);
    const auto next=worker.TryAssign(std::move(ready)); assert(next && next.Handle.Generation>assigned.Handle.Generation);
    assert(worker.Cancel(one.Handle)==TaskWorkCancelStatus::Stale);
    std::atomic<bool> joined{false};
    std::thread stopping([&]{assert(worker.Shutdown()==TaskExecutionStatus::Success); joined=true;});
    Until([&]{return worker.TryAssign(std::move(rejected)).Status==TaskExecutionStatus::Stopping;});
    assert(!joined && rejected.Releases);
    const auto calls=owner.Calls.load(),refills=owner.Refills.load();
    runtime.ResumeGate(); stopping.join();
    assert(owner.Calls==calls && owner.Refills==refills && worker.GetStatistics().Dropped==1);

    // Shutdown waits for an executing handler instead of destroying/suspending it.
    owner.Entered=false; owner.Release=false;
    assert((worker.Initialize<Owner,&Owner::Execute,&Owner::Refill>(owner)==TaskExecutionStatus::Success));
    Lease last(owner.Releases,6);
    Until([&]{return bool(worker.TryAssign(std::move(last)));});
    Until([&]{return owner.Entered.load();});
    joined=false;
    std::thread joinExecuting([&]{assert(worker.Shutdown()==TaskExecutionStatus::Success); joined=true;});
    Until([&]{return worker.TryAssign(std::move(rejected)).Status==TaskExecutionStatus::Stopping;});
    assert(!joined); owner.Release=true; joinExecuting.join();
    assert(joined && owner.Refills==refills && runtime.Created==3 && runtime.Joined==3);
    // Exercise the actual Ready->Executing versus cancellation race, with no
    // reset hook or second descriptor in the worker and no heap on the hot path.
    Owner racing;
    assert((racing.WorkerInstance.Initialize<Owner,&Owner::Execute,&Owner::Refill>(racing)==TaskExecutionStatus::Success));
    denyHeap=true;
    unsigned cancelled=0;
    for (unsigned i=0;i<100;++i) {
        Lease item(racing.Releases,static_cast<int>(i)); TaskWorkSubmission submission;
        Until([&]{submission=racing.WorkerInstance.TryAssign(std::move(item)); return bool(submission);});
        TaskWorkCancelStatus status;
        Until([&]{status=racing.WorkerInstance.Cancel(submission.Handle); return status!=TaskWorkCancelStatus::Busy;});
        if (status==TaskWorkCancelStatus::Cancelled) ++cancelled;
        else assert(status==TaskWorkCancelStatus::InProgress || status==TaskWorkCancelStatus::Stale);
        Until([&]{return racing.Releases==i+1 && racing.Refills==i+1;});
    }
    assert(racing.Calls+cancelled==100 && racing.Releases==100);
    assert(racing.WorkerInstance.Shutdown()==TaskExecutionStatus::Success);
    denyHeap=false;

}
