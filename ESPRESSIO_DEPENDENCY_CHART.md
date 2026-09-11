# ESPressio Task dependencies

Task depends only on System for execution, synchronization, monotonic deadlines
and the explicitly dynamic one-shot memory policy. It has no Threads, Timing,
Primitive, Event, Command, State, Adapter, Radio or platform-specific dependency.

Consumers provide fixed work descriptors and fixed owner thunks. Concrete platform
providers are installed by the composition application. Joinable execution is
required for persistent workers; platform-specific completion storage belongs to
the execution provider and is allocated during creation.

TaskExecutor's FIFO is inline, fixed capacity and separately accounted from T1's
single worker slot. It does not create a System message queue or a second worker.
