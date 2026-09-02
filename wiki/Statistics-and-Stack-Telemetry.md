# Statistics and Stack Telemetry

`GetStatistics()` exposes executor activity and worker-stack information so overload and provisioning decisions can be made from measured behaviour.

The statistics include submitted, completed, rejected, and dropped work counts together with configured stack size and the worker's lifetime minimum-free-stack measurement.

## Stack telemetry

The underlying stack measurement comes through `System::Execution`, not a native RTOS API. This keeps Task portable while allowing a target provider to supply the best available stack high-water information.

## Operational use

Use statistics to identify:

- sustained queue saturation;
- an unsuitable saturation policy;
- under-provisioned worker stack;
- unexpectedly low completion throughput;
- excessive rejection/drop rates.

Telemetry should guide configuration rather than encouraging arbitrarily large queues or stacks.