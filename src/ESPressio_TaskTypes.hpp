#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio {
namespace Task {

/// <summary>Specifies how a task executor behaves when its work queue is full.</summary>

enum class TaskQueueOverflowPolicy : uint8_t {
    Reject,
    DropOldest,
    DropNewest,
    Block
};

/// <summary>Specifies the memory-placement policy requested for a task's runtime resources.</summary>

enum class TaskMemoryPolicy : uint8_t {
    Internal,
    External,
    PreferExternal
};

/// <summary>Identifies the outcome of task creation, lifecycle, or work-submission operations.</summary>

enum class TaskExecutionStatus : uint8_t {
    Success,
    NotInitialized,
    AlreadyInitialized,
    AlreadyStarted,
    NotStarted,
    QueueUnavailable,
    QueueFull,
    TaskCreationFailed,
    InvalidConfiguration,
    UnsupportedMemoryPolicy,
    UnsupportedExecutionProvider,
    SignalUnavailable,
    Busy,
    Stopping,
    SelfJoin,
    GenerationExhausted,
    JoinFailed
};

/// <summary>Configures one physical execution context without queue capacity or overflow policy.</summary>

struct TaskExecutionConfiguration {
    /// <summary>Diagnostic name assigned to the underlying execution context.</summary>
    const char* Name = "espressioTask";
    /// <summary>Requested task stack size in bytes.</summary>
    uint32_t StackSize = 4096;
    /// <summary>Requested platform scheduling priority.</summary>
    uint32_t Priority = 1;
    /// <summary>Requested processor index, or a negative value for no fixed affinity.</summary>
    int32_t Core = -1;
    /// <summary>Memory-placement policy requested for task runtime resources.</summary>
    /// <remarks>Execution policy is separate from queued storage. PreferExternal permits the platform-safe internal
    /// stack path; External is rejected until an execution provider can guarantee safe external-stack lifecycle.</remarks>
    TaskMemoryPolicy MemoryPolicy = TaskMemoryPolicy::PreferExternal;
};

/// <summary>Composes physical execution with one separately bounded executor backlog.</summary>
struct TaskExecutorConfiguration {
    TaskExecutionConfiguration Execution{};
    std::size_t QueueDepth = 8;
    TaskQueueOverflowPolicy OverflowPolicy = TaskQueueOverflowPolicy::Reject;
    /// <summary>Queue allocation policy, applied once during Initialize without a hidden fallback.</summary>
    TaskMemoryPolicy QueueMemoryPolicy = TaskMemoryPolicy::Internal;
};

/// <summary>Captures cumulative executor activity and stack headroom diagnostics.</summary>

struct TaskExecutionStatistics {
    /// <summary>Total number of work items accepted for execution.</summary>
    uint64_t Submitted = 0;
    /// <summary>Total number of work items whose handlers completed.</summary>
    uint64_t Completed = 0;
    /// <summary>Total number of work items rejected by queueing policy.</summary>
    uint64_t Rejected = 0;
    /// <summary>Total number of work items discarded by a drop policy.</summary>
    uint64_t Dropped = 0;
    /// <summary>Ready assignments cancelled before execution.</summary>
    uint64_t Cancelled = 0;
    /// <summary>Configured task stack size in bytes.</summary>
    uint32_t ConfiguredStackSize = 0;
    /// <summary>Minimum observed free stack capacity in bytes.</summary>
    uint32_t MinimumFreeStack = 0;
};

}
}
