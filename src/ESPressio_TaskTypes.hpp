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
    UnsupportedMemoryPolicy
};

/// <summary>Configures execution, queueing, affinity, and memory policy for an ESPressio task.</summary>
struct TaskConfiguration {
    /// <summary>Diagnostic name assigned to the underlying execution context.</summary>
    const char* Name = "espressioTask";
    /// <summary>Requested task stack size in bytes.</summary>
    uint32_t StackSize = 4096;
    /// <summary>Requested platform scheduling priority.</summary>
    uint32_t Priority = 1;
    /// <summary>Requested processor index, or a negative value for no fixed affinity.</summary>
    int32_t Core = -1;
    /// <summary>Maximum number of work items retained by an executor queue.</summary>
    size_t QueueDepth = 8;
    /// <summary>Behaviour applied when the executor queue has no free capacity.</summary>
    TaskQueueOverflowPolicy OverflowPolicy = TaskQueueOverflowPolicy::Reject;
    /// <summary>Memory-placement policy requested for task runtime resources.</summary>
    /// <remarks>
    /// The default prefers external memory for allocator-capable ancillary resources such as executor queue backing.
    /// Task stacks remain on the platform-safe execution path unless a platform explicitly implements a safe external
    /// stack policy; the current ESP32 implementation therefore keeps FreeRTOS stacks internal.
    /// </remarks>
    TaskMemoryPolicy MemoryPolicy = TaskMemoryPolicy::PreferExternal;
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
    /// <summary>Configured task stack size in bytes.</summary>
    uint32_t ConfiguredStackSize = 0;
    /// <summary>Minimum observed free stack capacity in bytes.</summary>
    uint32_t MinimumFreeStack = 0;
};

}
}
