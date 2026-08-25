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
