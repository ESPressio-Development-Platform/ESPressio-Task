#pragma once

#include <cstdint>

#include <ESPressio_Execution.hpp>

#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

using TaskHandle = System::Execution::ExecutionHandle;
using TaskEntry = System::Execution::ExecutionEntry;

struct TaskCreationResult {
    TaskExecutionStatus Status = TaskExecutionStatus::TaskCreationFailed;
    TaskHandle Handle = System::Execution::InvalidExecutionHandle;

    explicit operator bool() const noexcept {
        return Status == TaskExecutionStatus::Success &&
            Handle != System::Execution::InvalidExecutionHandle;
    }
};

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

    static void Delete(TaskHandle handle) {
        (void)System::Execution::Provider().Destroy(handle);
    }

    static void Suspend(TaskHandle handle) {
        (void)System::Execution::Provider().Suspend(handle);
    }

    static void Resume(TaskHandle handle) {
        (void)System::Execution::Provider().Resume(handle);
    }

    static TaskHandle Current() {
        return System::Execution::Provider().Current();
    }

    static uint32_t MinimumFreeStack(TaskHandle handle) {
        return System::Execution::Provider().MinimumFreeStackBytes(handle);
    }

    static void SleepMilliseconds(uint32_t milliseconds) {
        System::Execution::Provider().SleepMilliseconds(milliseconds);
    }

    static void Yield() {
        System::Execution::Provider().Yield();
    }
};

}
}
