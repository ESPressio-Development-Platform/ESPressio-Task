#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

using NativeTaskHandle = TaskHandle_t;
using NativeTaskEntry = TaskFunction_t;

struct NativeTaskCreationResult {
    TaskExecutionStatus Status = TaskExecutionStatus::TaskCreationFailed;
    NativeTaskHandle Handle = nullptr;

    explicit operator bool() const {
        return Status == TaskExecutionStatus::Success && Handle != nullptr;
    }
};

class TaskRuntime {
public:
    static NativeTaskCreationResult Create(
        NativeTaskEntry entry,
        void* parameter,
        const TaskConfiguration& configuration
    ) {
        NativeTaskCreationResult result;
        if (entry == nullptr || configuration.StackSize == 0) {
            result.Status = TaskExecutionStatus::InvalidConfiguration;
            return result;
        }
        if (configuration.MemoryPolicy == TaskMemoryPolicy::External) {
            result.Status = TaskExecutionStatus::UnsupportedMemoryPolicy;
            return result;
        }

        BaseType_t created = pdFAIL;
        if (configuration.Core >= 0) {
            created = xTaskCreatePinnedToCore(
                entry,
                configuration.Name,
                configuration.StackSize,
                parameter,
                configuration.Priority,
                &result.Handle,
                configuration.Core
            );
        } else {
            created = xTaskCreate(
                entry,
                configuration.Name,
                configuration.StackSize,
                parameter,
                configuration.Priority,
                &result.Handle
            );
        }

        result.Status =
            created == pdPASS && result.Handle != nullptr
                ? TaskExecutionStatus::Success
                : TaskExecutionStatus::TaskCreationFailed;
        return result;
    }

    static void Delete(NativeTaskHandle handle) {
        vTaskDelete(handle);
    }

    static void Suspend(NativeTaskHandle handle) {
        vTaskSuspend(handle);
    }

    static void Resume(NativeTaskHandle handle) {
        vTaskResume(handle);
    }

    static NativeTaskHandle Current() {
        return xTaskGetCurrentTaskHandle();
    }

    static uint32_t MinimumFreeStack(NativeTaskHandle handle) {
        return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(handle));
    }
};

}
}
