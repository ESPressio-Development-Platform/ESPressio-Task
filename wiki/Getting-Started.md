# Getting Started

Include the umbrella header:

```cpp
#include <ESPressio_Task.hpp>
```

A typical `TaskExecutor<TWorkItem>` uses a trivially-copyable work item and a persistent bounded worker:

```cpp
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

## Platform providers

Task consumes ESPressio System execution, queue, and synchronization capabilities. The application/platform layer must install appropriate System providers before Task starts using them.

## Lifecycle rule

Constructors do not execute user work. `Initialize()` reserves required resources, while `Start()` opens the worker's start gate. This lets an application prepare resources during setup without executing work before dependencies are ready.

## Next steps

- [TaskExecutor](TaskExecutor)
- [Lifecycle](Lifecycle)
- [Queue Saturation](Queue-Saturation)
- [Choosing Task or Threads](Choosing-Task-or-Threads)