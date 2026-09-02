# Work Item Contracts

`TaskExecutor<TWorkItem>` requires `TWorkItem` to be trivially copyable.

This is a deliberate hot-path constraint. The underlying bounded System queue stores fixed-size work-item values directly instead of retaining heap-allocating callable captures or opaque ownership graphs.

## Good work items

Prefer small value types containing identifiers, numeric values, fixed-size metadata, or handles whose lifetime is independently guaranteed.

```cpp
struct SensorSampleWork {
    uint32_t SensorId;
    uint64_t Timestamp;
    float Value;
};
```

## Avoid hidden ownership

Do not smuggle complex lifetime management into queued work items through pointers whose pointees may disappear before execution. If a work item references external storage, its ownership/lifetime contract must be explicit and safe across asynchronous execution.

## Extension principle

If a domain needs richer move-only or variable-sized ownership semantics, solve that at the domain boundary rather than weakening the deterministic fixed-item queue contract for every Task consumer.