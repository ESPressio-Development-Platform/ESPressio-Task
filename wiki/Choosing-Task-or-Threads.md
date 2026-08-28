# Choosing Task or Threads

Use **ESPressio Task** for discrete asynchronous work and **ESPressio Threads** for long-lived autonomous behaviour.

## Use TaskExecutor when

- work arrives as discrete messages/items;
- a bounded queue is desirable;
- one persistent worker can process many items;
- backpressure/drop policy matters;
- you want to avoid creating an execution context per item.

## Use a one-shot Task when

- the work is genuinely infrequent;
- fire-and-forget execution is acceptable;
- repeated stack/control-block allocation is not on a hot path.

## Use Threads when

- the component itself is a long-lived worker;
- it has a continuous lifecycle rather than a stream of independent work items;
- start/stop/iteration semantics are part of the component abstraction.

The distinction is architectural, not merely syntactic: Task models **work**, while Threads models **workers**.