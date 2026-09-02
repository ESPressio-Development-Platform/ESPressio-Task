# Testing Task Extensions

Task tests should validate asynchronous semantics without relying on a particular RTOS implementation.

## Core behaviour

Cover initialization/start gating, work submission, handler execution, deterministic teardown, and statistics.

## Saturation

Exercise every queue policy at capacity: `Reject`, `DropOldest`, `DropNewest`, and `Block`, including zero and finite block timeouts.

## Platform capability paths

Test unrestricted affinity, supported affinity, unsupported affinity, stack telemetry, queue/provider creation failure, and unsupported memory policy.

## Work item constraints

Compile-time coverage should preserve the trivially-copyable requirement and prevent accidental regression toward heap-allocating queued callable ownership.

## Stress behaviour

Run sustained producer load against bounded executors and verify that queue depth remains bounded, counters remain coherent, and teardown does not leak worker/queue/synchronization resources.