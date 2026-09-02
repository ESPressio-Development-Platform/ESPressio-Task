#pragma once

#include <cstdint>

#include <ESPressio_Execution.hpp>

#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

/// <summary>Opaque handle identifying a task execution context.</summary>
using TaskHandle = System::Execution::ExecutionHandle;

/// <summary>Entry-point signature used to start a task execution context.</summary>
using TaskEntry = System::Execution::ExecutionEntry;

/// <summary>Contains the outcome and handle returned when a task execution context is created.</summary>
struct TaskCreationResult {
    /// <summary>The task-domain creation status.</summary>
    TaskExecutionStatus Status = TaskExecutionStatus::TaskCreationFailed;
    /// <summary>The created task handle, or the invalid execution handle sentinel.</summary>
    TaskHandle Handle = System::Execution::InvalidExecutionHandle;

    /// <summary>Indicates whether task creation succeeded and produced a valid handle.</summary>
    explicit operator bool() const noexcept {
        return Status == TaskExecutionStatus::Success &&
            Handle != System::Execution::InvalidExecutionHandle;
    }
};

/// <summary>Adapts ESPressio task operations onto the active System execution provider.</summary>
class TaskRuntime {
private:
    static TaskExecutionStatus MapCreationStatus(System::PlatformStatus status) noexcept {
        switch (status) {
            case System::PlatformStatus::Success:
                return TaskExecutionStatus::Success;
            case System::PlatformStatus::InvalidArgument:
                return TaskExecutionStatus::InvalidConfiguration;
            default:
                return TaskExecutionStatus::TaskCreationFailed;
        }
    }

public:
    /// <summary>Creates a task execution context using the supplied task configuration.</summary>
    /// <param name="entry">Entry point invoked by the created task.</param>
    /// <param name="parameter">Opaque parameter supplied to the entry point.</param>
    /// <param name="configuration">Task execution configuration.</param>
    /// <returns>The task-domain creation status and resulting handle.</returns>
    static TaskCreationResult Create(
        TaskEntry entry,
        void* parameter,
        const TaskConfiguration& configuration
    ) {
        TaskCreationResult result;
        if (entry == nullptr || configuration.StackSize == 0) {
            result.Status = TaskExecutionStatus::InvalidConfiguration;
            return result;
        }
        if (configuration.MemoryPolicy == TaskMemoryPolicy::External) {
            result.Status = TaskExecutionStatus::UnsupportedMemoryPolicy;
            return result;
        }

        System::Execution::ExecutionConfiguration nativeConfiguration;
        nativeConfiguration.Name = configuration.Name;
        nativeConfiguration.StackSizeBytes = configuration.StackSize;
        nativeConfiguration.Priority = configuration.Priority;
        nativeConfiguration.Affinity = configuration.Core >= 0
            ? System::ProcessorAffinity::Specific(static_cast<uint8_t>(configuration.Core))
            : System::ProcessorAffinity::Any();

        const auto created = System::Execution::Provider().Create(
            entry,
            parameter,
            nativeConfiguration
        );
        result.Status = MapCreationStatus(created.Result.Status);
        result.Handle = created.Handle;
        return result;
    }

    /// <summary>Destroys the specified task execution context.</summary>
    static void Delete(TaskHandle handle) {
        (void)System::Execution::Provider().Destroy(handle);
    }

    /// <summary>Suspends the specified task execution context.</summary>
    static void Suspend(TaskHandle handle) {
        (void)System::Execution::Provider().Suspend(handle);
    }

    /// <summary>Resumes the specified task execution context.</summary>
    static void Resume(TaskHandle handle) {
        (void)System::Execution::Provider().Resume(handle);
    }

    /// <summary>Gets the handle for the currently executing task context.</summary>
    static TaskHandle Current() {
        return System::Execution::Provider().Current();
    }

    /// <summary>Gets the minimum observed free stack capacity for a task.</summary>
    static uint32_t MinimumFreeStack(TaskHandle handle) {
        return System::Execution::Provider().MinimumFreeStackBytes(handle);
    }

    /// <summary>Suspends the current task for the requested number of milliseconds.</summary>
    static void SleepMilliseconds(uint32_t milliseconds) {
        System::Execution::Provider().SleepMilliseconds(milliseconds);
    }

    /// <summary>Yields the current task to the platform scheduler.</summary>
    static void Yield() {
        System::Execution::Provider().Yield();
    }
};

}
}
