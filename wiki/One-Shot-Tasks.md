# One-Shot Tasks

`Task::Run()` is intended for infrequent fire-and-forget work where creating a dedicated execution context is acceptable.

Use it when the work is genuinely occasional and does not require a persistent queue-backed worker.

## When not to use it

Do not use one-shot tasks for high-frequency communications, protocol handling, or other paths where repeated execution-stack/control-block allocation would create avoidable memory pressure and scheduling overhead.

For repeated work, use [TaskExecutor](TaskExecutor).