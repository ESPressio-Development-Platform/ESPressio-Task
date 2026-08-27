# Platform Abstractions Audit Trail

This file records changes made during the platform-abstraction tranche tracked by issue #3.

## 2026-08-27

- Removed direct FreeRTOS task types and calls from `TaskRuntime`; execution now delegates to `ESPressio::System::Execution`.
- Replaced native task handles with portable System execution handles.
- Mapped Task stack size, priority and optional core selection to the System execution configuration.
- Replaced the TaskExecutor FreeRTOS queue with the System bounded message-queue abstraction.
- Replaced the TaskExecutor FreeRTOS binary semaphore start gate with a System synchronization signal.
- Replaced direct task stack high-water calls with System execution telemetry.
- Reworked one-shot `Task::Run()` to create work through `TaskRuntime` rather than FreeRTOS directly.
- Changed blocking `TaskExecutor::Submit()` timeout input from native RTOS ticks to portable milliseconds.
- Updated package metadata so ESPressio-Task is platform/framework neutral and depends on ESPressio-System rather than ESP32/FreeRTOS.

## Boundary

ESPressio-Task owns task/executor lifecycle and bounded asynchronous-work semantics. ESPressio-System owns the primitive execution, queue and synchronization capabilities. Concrete ESP32/FreeRTOS behaviour is supplied by ESPressio-ESP32.
