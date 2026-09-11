#include <ESPressio_Task.hpp>
#include "HostRuntime.hpp"
#include <type_traits>
using namespace ESPressio::Task;
template<class T,class=void> struct HasQueue : std::false_type {};
template<class T> struct HasQueue<T,std::void_t<decltype(T{}.QueueDepth)>> : std::true_type {};
static_assert(!HasQueue<TaskExecutionConfiguration>::value && HasQueue<TaskExecutorConfiguration>::value);
struct Owner {
    void Execute(int&) noexcept {}
    void Queued(const int&) noexcept {}
};
int main() {
    HostRuntime runtime; Owner owner;
    IdleWorkerTask<int> worker;
    TaskExecutionConfiguration execution; execution.StackSize=0;
    assert((worker.Initialize<Owner,&Owner::Execute>(owner,execution)==TaskExecutionStatus::InvalidConfiguration));
    execution.StackSize=1024; execution.Core=256;
    assert((worker.Initialize<Owner,&Owner::Execute>(owner,execution)==TaskExecutionStatus::InvalidConfiguration));
    execution.Core=-1; execution.MemoryPolicy=TaskMemoryPolicy::External;
    assert((worker.Initialize<Owner,&Owner::Execute>(owner,execution)==TaskExecutionStatus::UnsupportedMemoryPolicy));
    TaskExecutorConfiguration queue; queue.QueueDepth=9;
    TaskExecutor<int> tooLarge(queue);
    assert((tooLarge.Initialize<Owner,&Owner::Queued>(owner)==TaskExecutionStatus::InvalidConfiguration));
    queue.QueueDepth=8; queue.QueueMemoryPolicy=TaskMemoryPolicy::PreferExternal;
    TaskExecutor<int> wrongPlacement(queue);
    assert((wrongPlacement.Initialize<Owner,&Owner::Queued>(owner)==TaskExecutionStatus::UnsupportedMemoryPolicy));
    assert(runtime.Created==0 && runtime.Signals==0);
}
