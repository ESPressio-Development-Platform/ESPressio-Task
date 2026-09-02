# Platform Integration

A platform does not implement ESPressio Task directly. It implements the ESPressio System capabilities Task consumes.

A new target therefore needs suitable providers for:

- primitive execution;
- bounded fixed-element queues;
- synchronization signals;
- processor/affinity reporting where applicable;
- stack telemetry where available.

Once installed, Task remains unchanged.

## Startup

Install System providers before Task initialization. This ensures executor resources are created through the intended platform implementation.

## Unsupported features

If a target cannot honour affinity or a requested memory-placement semantic, report that limitation explicitly through the relevant System/Task result rather than silently accepting and ignoring the request.

## Board versus platform support

Board pinouts and hardware peripherals are not Task concerns. Task is concerned only with execution semantics. Keep target integration at the System/provider boundary.