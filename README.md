# ESPressio Task

ESPressio Task provides bounded asynchronous execution primitives for the ESPressio Development Platform.

It is intentionally distinct from ESPressio Threads:

- **Task** represents discrete asynchronous work.
- **TaskExecutor** represents a persistent bounded worker used to execute many discrete work items without creating a FreeRTOS task per message.
- **Threads** remains the higher-level lifecycle abstraction for long-lived autonomous workers.

## Lifecycle

Constructors never execute user work. `TaskExecutor::Initialize()` reserves the queue and worker resources, but the worker remains behind a start gate until `Start()` is called. This makes it safe to reserve stacks early during application setup without allowing work to escape before dependencies are ready.

## Typed bounded executors

`TaskExecutor<TWorkItem>` requires a trivially-copyable work item. The FreeRTOS queue therefore stores the work item directly and deterministically rather than retaining heap-allocating callable captures on the hot path.

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

## Stack instrumentation

`GetStatistics()` reports submitted/completed/rejected/dropped counts together with configured stack size and the worker's lifetime minimum-free stack high-water value.

## One-shot work

`Task::Run()` is available for infrequent fire-and-forget work. High-frequency communications and protocol paths should prefer `TaskExecutor` to avoid repeated task/stack/TCB allocation.

## Memory policy

The initial backend exposes memory policy in `TaskConfiguration`. Internal task stacks are supported now. External-stack support is kept explicit in the API but returns `UnsupportedMemoryPolicy` until a backend implementation can guarantee safe cleanup/lifecycle semantics for the selected ESP-IDF target.

## Current implementation

The initial implementation targets ESP32/FreeRTOS. The public concepts are kept separate enough that the backend can later move to a dedicated FreeRTOS implementation library without changing consumers.
