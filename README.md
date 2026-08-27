# ESPressio Task

ESPressio Task provides bounded asynchronous execution primitives for the ESPressio Development Platform.

It is intentionally distinct from ESPressio Threads:

- **Task** represents discrete asynchronous work.
- **TaskExecutor** represents a persistent bounded worker used to execute many discrete work items without creating an execution context per message.
- **Threads** remains the higher-level lifecycle abstraction for long-lived autonomous workers.

## Platform independence

ESPressio Task no longer depends directly on FreeRTOS or ESP32. Primitive execution, bounded queues and synchronization are supplied by ESPressio-System providers.

On ESP32, the top-level application installs those providers through ESPressio-ESP32:

```cpp
#include <ESPressio_ESP32.hpp>

ESPressio::ESP32Platform::InstallSystemProviders();
```

This keeps Task's lifecycle and asynchronous-work semantics reusable on future hardware/runtime implementations.

## Lifecycle

Constructors never execute user work. `TaskExecutor::Initialize()` reserves the queue and worker resources, but the worker remains behind a start gate until `Start()` is called. This makes it safe to reserve execution resources early during application setup without allowing work to escape before dependencies are ready.

## Typed bounded executors

`TaskExecutor<TWorkItem>` requires a trivially-copyable work item. The installed System queue provider stores fixed-size work items directly and deterministically rather than retaining heap-allocating callable captures on the hot path.

```cpp
#include <ESPressio_Task.hpp>

struct WorkItem {
    uint32_t Id;
    uint8_t Value;
};

ESPressio::Task::TaskConfiguration configuration;
configuration.Name = "exampleExecutor";
configuration.StackSize = 4096;
configuration.QueueDepth = 8;

ESPressio::Task::TaskExecutor<WorkItem> executor(configuration);

void setup() {
    executor.Initialize([](const WorkItem& item) {
        // asynchronous processing
    });
    executor.Start();

    executor.Submit(WorkItem{1, 42});
}
```

## Queue saturation

Available policies are `Reject`, `DropOldest`, `DropNewest`, and `Block`. Queues are always bounded; ESPressio Task never grows an unbounded pending-work collection.

For the `Block` policy, the optional second argument to `Submit()` is now expressed in **milliseconds**, not native RTOS ticks:

```cpp
executor.Submit(item, 25); // block for up to 25 ms
```

## Stack instrumentation

`GetStatistics()` reports submitted/completed/rejected/dropped counts together with configured stack size and the worker's lifetime minimum-free-stack value. The underlying platform measurement is provided through `System::Execution` rather than a native RTOS API.

## One-shot work

`Task::Run()` is available for infrequent fire-and-forget work. High-frequency communications and protocol paths should prefer `TaskExecutor` to avoid repeated execution-stack/control-block allocation.

## Processor affinity

`TaskConfiguration::Core` remains the developer-facing compatibility setting for this generation. A negative value requests any processor; a non-negative value is translated to `System::ProcessorAffinity::Specific(...)`.

Whether a target can honour affinity is a platform capability. ESPressio-ESP32 does so through its FreeRTOS execution provider.

## Memory policy

`TaskConfiguration` continues to expose task memory policy. Internal execution stacks are supported by the current provider path. `External` remains explicit but returns `UnsupportedMemoryPolicy` until a provider contract exists that can guarantee safe external-stack lifecycle semantics across supported targets.

## Architecture

```text
ESPressio-Task
    |
    +-- System::Execution
    +-- System::Queue
    +-- System::Synchronization
             |
             v
       ESPressio-ESP32
             |
             v
          FreeRTOS
```

Task consumers therefore do not include FreeRTOS headers or expose FreeRTOS handles.

## Coordinated development dependency

During this tranche, Task consumes:

```ini
https://github.com/ESPressio-Development-Platform/ESPressio-System.git#feature/1-system-memory-policy
```

The Lab application remains responsible for installing the concrete ESP32 providers.

See `PLATFORM_ABSTRACTIONS.md` for the migration audit trail.
