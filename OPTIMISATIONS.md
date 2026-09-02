# Optimisations

Chronological record of resource and execution optimisations made during active development.

## 2026-08-25 — Issue #1 — Initial bounded executor architecture

- Chose persistent bounded executors for high-frequency work instead of creating/deleting a FreeRTOS task for every message.
- Typed executor queues store trivially-copyable work items directly in the FreeRTOS queue, avoiding hot-path `std::function` capture allocations.
- `Initialize()` reserves resources without executing user work; `Start()` is the sole gate that permits work-item execution.
- Queue growth is always bounded and saturation behavior is explicit.
- Added lifetime minimum-free-stack instrumentation for evidence-based stack sizing.
- One-shot `Task::Run()` remains available for infrequent fire-and-forget operations but is not intended for packet hot paths.
- External task-stack policy is represented explicitly but deliberately unsupported in the first backend until lifecycle-safe cleanup semantics are implemented and validated.

## 2026-08-29 — System-backed queue storage

- Routed executor queue creation through the ESPressio-System policy-aware queue abstraction.
- `PreferExternal` executors now request `ExternalPreferred` queue backing by default, while retaining a portable internal fallback only when a platform cannot provide policy-aware queue storage.
- ESP32 queue item storage can therefore reside in PSRAM without moving execution stacks or RTOS task requirements out of internal-capable memory.
- Drop-oldest saturation now returns the displaced work item to an optional discard handler so pointer/ownership work items can be reclaimed instead of leaked.

## 2026-08-30 — External one-shot invocation ownership

- Moved the one-shot `Task::Run()` invocation control object from ordinary `new` storage to ESPressio-System `ExternalPreferred` ownership.
- The invocation retains the exact memory provider used for allocation so task-context cleanup always returns storage to the originating provider even if the process-wide provider changes later.
- Allocation failure now maps cleanly to `TaskCreationFailed` without leaking partially constructed callable state.
- Task stacks remain governed independently by the execution provider and are deliberately not moved to PSRAM by this change.
