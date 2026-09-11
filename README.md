# ESPressio Task

Task owns discrete work execution below Threads. `IdleWorkerTask<T>` owns one
pre-created joinable physical context, one binary wake signal and one inline
nothrow-movable descriptor. It has no queue, application lifecycle or dependency
on Threads. `TaskExecutor<T, MaximumPending>` adds one explicit fixed FIFO and
reuses that worker. System provides execution, synchronization and monotonic time.

Install concrete System providers before initialization and keep them installed
and alive until shutdown. In particular, a worker requires `CreateJoinable`/`Join`;
an ordinary force-delete execution provider is rejected. The platform migration
must implement this capability before the worker can run on that platform.

## Single-slot work

```cpp
#include <ESPressio_Task.hpp>
using namespace ESPressio::Task;
struct Work { unsigned Index; };
struct Owner {
    IdleWorkerTask<Work> Worker;
    void Execute(Work& work) noexcept { (void)work.Index; }
    void Released(IdleWorkerTask<Work>& worker) noexcept {
        // An owner with an independently bounded FIFO may try its oldest item here.
        // Retain that item if TryAssign reports Busy. No payload is supplied by T1.
        (void)worker;
    }
    TaskExecutionStatus Initialize() {
        TaskExecutionConfiguration execution;
        execution.Name = "dispatchLane";
        execution.StackSize = 4096;
        execution.Priority = 2;
        execution.Core = -1;
        return Worker.Initialize<Owner, &Owner::Execute, &Owner::Released>(*this, execution);
    }
};
void SubmitAndCancel(Owner& owner) {
    Work item{7};
    auto admitted = owner.Worker.TryAssign(std::move(item));
    if (admitted) {
        auto cancelled = owner.Worker.Cancel(admitted.Handle);
        // Cancelled means the exact Ready descriptor was destroyed. InProgress
        // means execution already claimed it. Stale cannot target later work.
        (void)cancelled;
    }
}
```

Construction is inert. Successful `Initialize` already leaves the worker Idle;
there is no Start operation. `TryAssign` never waits for a lock or consumes a
rejected source. Work may be move-only and own a family-pool lease, but move
construction and destruction must be noexcept. Handler/refill bindings contain
only an owner pointer and fixed noexcept function pointers. Application exception
translation belongs inside the domain's thunk.

`TaskWorkSubmission` carries status and a non-owning `TaskWorkHandle`. Handles are
worker-scoped and generation-tagged; generations never reset on reinitialization
or wrap. A handle must not outlive its worker object. `Cancel` may return Busy
under contention and never interrupts Executing work. Descriptor destruction
precedes Idle publication and each configured release hook. Cancellation release
notifications use one fixed counter, not work-item storage. Hooks execute in the
worker context, including cancellation hooks.

`Shutdown()` closes admission, suppresses future release-hook claims, discards
Ready work, waits for the one executing handler/already-claimed hook, and joins
the context. It returns the worker to Uninitialized. Self-shutdown returns
`SelfJoin`; concurrent shutdown returns Busy. Never destroy a worker from its own
handler. A failed provider join retains resources for an external retry.

`IsInitialized`, `IsIdle`, `IsCurrentExecution` and `GetStatistics` are fixed
queries. Statistics report assignments, completions, rejection, cancellation,
shutdown discard and stack headroom; successful Initialize resets counters.
Stack/control resources and the single signal are allocated only at Initialize.
The inline descriptor and state are included in `sizeof(IdleWorkerTask<T>)`.

## Explicit bounded FIFO

```cpp
#include <ESPressio_Task.hpp>
using namespace ESPressio::Task;
struct QueueOwner {
    static TaskExecutorConfiguration Configuration() {
        TaskExecutorConfiguration c;
        c.Execution.Name = "queuedWork";
        c.Execution.StackSize = 3072;
        c.QueueDepth = 8;
        c.OverflowPolicy = TaskQueueOverflowPolicy::Reject;
        return c;
    }
    TaskExecutor<unsigned, 8> Executor{Configuration()};
    void Execute(const unsigned& value) noexcept { (void)value; }
    void Discard(const unsigned& value) noexcept { (void)value; }
    TaskExecutionStatus Initialize() {
        return Executor.Initialize<QueueOwner, &QueueOwner::Execute, &QueueOwner::Discard>(*this);
    }
};
void StartAndSubmit(QueueOwner& owner) {
    if (owner.Initialize() != TaskExecutionStatus::Success) return;
    owner.Executor.Start();
    owner.Executor.Submit(42);
    auto statistics = owner.Executor.GetStatistics();
    (void)statistics;
    owner.Executor.Stop();
}
```

`MaximumPending` defaults to 8 and is compile-time storage capacity; `QueueDepth`
selects a positive admitted limit no larger than it. Queue records must be
trivially copyable, nothrow default constructible and nothrow copy assignable.
The FIFO consumes `sizeof(T) * MaximumPending` inline bytes, plus fixed indices;
the worker owns one separate Work lease/slot and one stack. There is no queue
allocation or growth. `QueueMemoryPolicy` supports Internal for inline backing;
other requests fail explicitly. The composition owner chooses object placement.

Two executor-owned binary signals support capacity waits and producer quiescence,
in addition to the worker's one wake signal. These are created during Initialize.
The executor Start gate admits no submissions until Start; the physical worker
already exists. Every submission joins the FIFO and cannot bypass older work.

Overflow policies:

- Reject returns QueueFull and leaves existing work untouched.
- DropNewest rejects the input, counts it as dropped, and invokes no discard hook
  because the executor never owned it.
- DropOldest reclaims the oldest queued accepted item through the fixed discard
  thunk before admitting the new one; an executing/Ready worker item is protected.
- Block waits on capacity for the finite millisecond budget passed to Submit,
  such as `executor.Submit(item, 25)`. Stale wakes never restart that budget.
  `UINT32_MAX` is rejected. A handler cannot block waiting for its own queue.

Discard thunks are bounded reclamation hooks and must not reenter the executor.
`Stop` closes admission, wakes blocked producers, joins T1, and reclaims Ready and
queued records through discard hooks. It does not run queued application handlers.
Self-stop is a typed misuse; destruction requires a successful external stop.

## Low-level and one-shot execution

`TaskRuntime::Create` accepts queue-free `TaskExecutionConfiguration` and adapts it
to the System execution provider. `CreateJoinable` explicitly uses a supplied
provider whose lifetime spans the context and its join. Runtime diagnostics expose
current handle and stack headroom without native RTOS types.

```cpp
#include <ESPressio_Task.hpp>
void RunInfrequentWork() {
    ESPressio::Task::TaskExecutionConfiguration execution;
    execution.Name = "oneShot";
    auto status = ESPressio::Task::Task::Run([] { /* infrequent work */ }, execution);
    (void)status;
}
```

One-shot `Task::Run` intentionally allocates a new invocation/callable and physical
context for every call. It is an explicit convenience, outside deterministic
family/transport hot paths. No persistent executor stores `std::function`.

This coordinated development branch consumes System `primitives_redesign`.
No family dependency, compatibility configuration alias or version change is added.

Native validation: `cmake -S tests -B build -DESPRESSIO_SYSTEM_SOURCE_DIR=/path/to/ESPressio-System`, then `cmake --build build` and `ctest --test-dir build --output-on-failure`. The source checkout must use the coordinated branch.
