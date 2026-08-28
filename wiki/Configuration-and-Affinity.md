# Configuration and Affinity

`TaskConfiguration` controls the executor's worker resources and overload behaviour.

Important settings include worker name, stack size, queue depth, priority, memory policy, saturation policy, and processor/core preference.

## Processor affinity

`TaskConfiguration::Core` is the developer-facing affinity setting in the 1.0.0 baseline.

- a negative value requests any processor;
- a non-negative value is translated to `System::ProcessorAffinity::Specific(...)`.

Whether that request can be honoured is a property of the installed platform provider. Multi-core hardware does not automatically imply enforceable execution affinity.

## Memory policy

Internal execution stacks are supported by the current provider contract. An explicit external-stack request remains unsupported until the platform abstraction can guarantee safe external-stack lifecycle semantics across supported targets.

Consumers should treat unsupported memory policy as an explicit result rather than assuming the request was silently ignored.