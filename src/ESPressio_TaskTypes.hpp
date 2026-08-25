#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio {
namespace Task {

enum class TaskQueueOverflowPolicy : uint8_t {
    Reject,
    DropOldest,
    DropNewest,
    Block
};

enum class TaskMemoryPolicy : uint8_t {
    Internal,
    External,
    PreferExternal
};

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

struct TaskConfiguration {
    const char* Name = "espressioTask";
    uint32_t StackSize = 4096;
    uint32_t Priority = 1;
    int32_t Core = -1;
    size_t QueueDepth = 8;
    TaskQueueOverflowPolicy OverflowPolicy = TaskQueueOverflowPolicy::Reject;
    TaskMemoryPolicy MemoryPolicy = TaskMemoryPolicy::Internal;
};

struct TaskExecutionStatistics {
    uint64_t Submitted = 0;
    uint64_t Completed = 0;
    uint64_t Rejected = 0;
    uint64_t Dropped = 0;
    uint32_t ConfiguredStackSize = 0;
    uint32_t MinimumFreeStack = 0;
};

}
}
