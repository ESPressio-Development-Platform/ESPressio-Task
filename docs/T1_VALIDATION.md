# T1 implementation and validation

Starting branch: primitives_redesign at efa4e302b4d32caa6c5258ffb709013bbb4ce959,
revalidated before source changes. There were no conventional native tests in the
baseline. The Basic example and its workflow were read before replacement; its
combined configuration and capturing/dynamic handler expectations were obsolete.

The System prerequisite adds CreateJoinable/Join, because forceful Destroy cannot
provide the locked cooperative shutdown contract. Unsupported providers fail
closed. No System-to-Task dependency was introduced. The ESP32 implementation of
joinable execution remains a platform-tranche requirement, not an assumed runtime
capability of the predecessor provider.

IdleWorkerTask has one inline nothrow-movable/destructible slot, one binary signal
and one joinable context. Fixed handler/refill thunks replace dynamic closures.
Admission and cancellation are non-blocking. Nonzero generations survive explicit
reinitialization and cannot wrap. Ready cancellation and execution claims are
serialized; destruction precedes Idle. Release notifications use one scalar count
bounded by the number of unique generations, without retaining any work item.
Shutdown closes admission and suppresses unclaimed refill, then joins; it never
calls Destroy or Suspend. Provider failure retains owned resources for retry.

TaskExecutor reuses T1 and owns one inline array of MaximumPending trivial records.
QueueDepth is an admission limit no larger than this array. All accepted submissions
enter the FIFO, and refill claims only its oldest record. The worker owns a separate
small RAII Work descriptor to reclaim a Ready item on shutdown. One capacity signal
and one producer-quiescence signal are explicitly added at executor level. This is
three signals total including T1's wake, one task/stack, one worker slot and one
fixed FIFO; there is no hidden queue allocation or runtime growth. Block uses a
finite monotonic budget and capacity notification. Inline backing is the only
supported executor queue placement policy; unsupported policies fail explicitly.

Native gates:

- idle_worker: inert/transactional initialization, one context/signal, move-only
  ownership, Busy source preservation, foreign/stale handles, Ready cancellation,
  executing cancellation rejection, destructor-before-refill, refill progress,
  stack telemetry, self-shutdown misuse, idle/Ready/executing shutdown and explicit
  reinitialization. Heap denial covers hot assignments, cancellation, refill and
  shutdown. One hundred real cancellation/execution races prove exactly one
  execution-or-cancellation and exactly one lease release per admission.
- executor: FIFO/refill, Start gate, all four overflow policies, protected worker
  slot, Ready and queued reclamation, finite blocking timeout, blocked-producer
  shutdown wake, self-stop rejection and stack/counter composition. Heap denial
  covers normal queue/worker execution and overflow paths after initialization.
- configuration: no queue fields on TaskExecutionConfiguration, invalid execution
  setup, queue bounds and unsupported placement rejection before resource creation.
- rejected_contracts: throwing move descriptors and QueueDepth on physical
  execution configuration fail compilation for the intended diagnostic.

All four native CTest gates pass with GCC 13.3 C++17, warnings as errors and RTTI
disabled. Eight public headers compile independently. Three README C++ snippets
compile independently. The updated optional Persistence logging consumer passes
its native compile contract, and all 16 Persistence CTest cases pass against T1.
GitHub native/ESP32 compilation follows publication. No quota failure observed.

Expected downstream migration: remaining family, Thread, Radio/Mesh and platform
consumers of TaskConfiguration/capturing TaskExecutor handlers are migrated in
their dependency-ordered tranches. No alias is retained to conceal those breaks.
One-shot Task::Run remains an explicitly dynamic convenience, not a core hot path.
No version numbers, tags, releases or main integration were changed.

## Early provider entry regression

Event integration exposed the case where CreateJoinable enters the worker before Initialize publishes Idle. The entry now waits on the existing binary signal during that publication gap; it does not enter the admission mutex with an obsolete Uninitialized observation. The test provider can require the entry to have reached Wait before CreateJoinable returns, making the ordering deterministic. First assignment then succeeds immediately. All four Task gates pass with this regression. No slot, queue, signal or public API was added.
