# Runtime Abstraction

The Task runtime adapts ESPressio System primitives into the lifecycle required by `TaskExecutor` and one-shot work.

## System dependencies

Task consumes:

- `System::Execution` for worker creation, destruction, stack telemetry, sleep/yield and affinity;
- `System::Queue` for bounded fixed-size work-item storage;
- `System::Synchronization` for lifecycle/start-gate coordination.

## Portability rule

Task runtime code must not include or expose native scheduler/RTOS handles. A new target becomes usable by Task when its System providers satisfy these contracts.

## Lifecycle ordering

The runtime must preserve the distinction between resource reservation and work execution: initializing resources must not allow user callbacks to run before the explicit start transition.

## Failure propagation

Provider failures, unsupported affinity, unsupported memory policy, queue creation failure, and execution creation failure should remain explicit Task outcomes rather than being hidden behind target-specific behaviour.