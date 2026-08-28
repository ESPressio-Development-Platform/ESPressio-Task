# TaskExecutor

`TaskExecutor<TWorkItem>` is the preferred primitive for repeated asynchronous work.

It owns a persistent worker and a bounded queue, avoiding creation and destruction of a new execution context for every message.

## Work item type

`TWorkItem` must be trivially copyable. The System queue provider stores fixed-size values directly, keeping the hot path deterministic and avoiding heap-allocating callable captures.

## Basic flow

```cpp
TaskExecutor<WorkItem> executor(configuration);

executor.Initialize([](const WorkItem& item) {
    Process(item);
});

executor.Start();
executor.Submit(WorkItem{1, 42});
```

## Why persistent execution matters

High-frequency transport, protocol, event, or command paths should prefer `TaskExecutor` over repeated one-shot execution. A persistent worker amortizes stack/control-block allocation and provides explicit backpressure through a bounded queue.

See [Queue Saturation](Queue-Saturation) for overload behaviour.