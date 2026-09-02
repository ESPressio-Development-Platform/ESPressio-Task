# Queue Saturation

Task queues are always bounded. When a `TaskExecutor` queue is full, the configured saturation policy defines the result.

| Policy | Behaviour |
| --- | --- |
| `Reject` | Reject the incoming item. |
| `DropOldest` | Remove the oldest queued item to admit the new one. |
| `DropNewest` | Discard the new item when the queue is full. |
| `Block` | Wait for capacity up to the supplied timeout. |

## Blocking timeout

For `Block`, the optional timeout supplied to `Submit()` is expressed in milliseconds:

```cpp
executor.Submit(item, 25); // wait up to 25 ms
```

The API intentionally does not expose native RTOS tick units.

## Choosing a policy

Choose according to domain semantics rather than convenience. Commands may need rejection/backpressure; telemetry may tolerate dropping stale data; control-loop work may require a different policy entirely.

Queue statistics let the application observe saturation rather than hiding it.