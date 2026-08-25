#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

class Task {
    struct Invocation {
        std::function<void()> Work;
    };

    static void _entry(void* parameter) {
        std::unique_ptr<Invocation> invocation(
            static_cast<Invocation*>(parameter)
        );
        if (invocation && invocation->Work) {
            try {
                invocation->Work();
            } catch (...) {
                // Fire-and-forget isolation: callers observe submission only.
            }
        }
        vTaskDelete(nullptr);
    }

public:
    static TaskExecutionStatus Run(
        std::function<void()> work,
        TaskConfiguration configuration = {}
    ) {
        if (!work || configuration.StackSize == 0) {
            return TaskExecutionStatus::InvalidConfiguration;
        }
        if (configuration.MemoryPolicy == TaskMemoryPolicy::External) {
            return TaskExecutionStatus::UnsupportedMemoryPolicy;
        }

        std::unique_ptr<Invocation> invocation(
            new (std::nothrow) Invocation{std::move(work)}
        );
        if (!invocation) {
            return TaskExecutionStatus::TaskCreationFailed;
        }

        TaskHandle_t task = nullptr;
        BaseType_t result = pdFAIL;
        if (configuration.Core >= 0) {
            result = xTaskCreatePinnedToCore(
                _entry,
                configuration.Name,
                configuration.StackSize,
                invocation.get(),
                configuration.Priority,
                &task,
                configuration.Core
            );
        } else {
            result = xTaskCreate(
                _entry,
                configuration.Name,
                configuration.StackSize,
                invocation.get(),
                configuration.Priority,
                &task
            );
        }

        if (result != pdPASS || task == nullptr) {
            return TaskExecutionStatus::TaskCreationFailed;
        }

        invocation.release();
        return TaskExecutionStatus::Success;
    }
};

}
}
