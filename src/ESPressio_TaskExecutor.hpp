#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

template <typename TWorkItem>
class TaskExecutor {
    static_assert(
        std::is_trivially_copyable<TWorkItem>::value,
        "TaskExecutor work items must be trivially copyable for deterministic bounded queue storage"
    );

public:
    using Handler = std::function<void(const TWorkItem&)>;

private:
    TaskConfiguration _configuration;
    Handler _handler;
    QueueHandle_t _queue = nullptr;
    SemaphoreHandle_t _startGate = nullptr;
    TaskHandle_t _task = nullptr;
    std::atomic<bool> _initialized{false};
    std::atomic<bool> _started{false};
    std::atomic<bool> _stopping{false};
    std::atomic<uint64_t> _submitted{0};
    std::atomic<uint64_t> _completed{0};
    std::atomic<uint64_t> _rejected{0};
    std::atomic<uint64_t> _dropped{0};
    std::atomic<uint32_t> _minimumFreeStack{0};

    static void _entry(void* parameter) {
        auto* executor = static_cast<TaskExecutor*>(parameter);
        if (executor != nullptr) {
            executor->_run();
        }
        vTaskDelete(nullptr);
    }

    void _run() {
        if (_startGate == nullptr) {
            return;
        }

        xSemaphoreTake(_startGate, portMAX_DELAY);
        if (_stopping.load(std::memory_order_acquire)) {
            return;
        }

        TWorkItem item{};
        while (!_stopping.load(std::memory_order_acquire)) {
            if (xQueueReceive(_queue, &item, portMAX_DELAY) != pdTRUE) {
                continue;
            }

            if (_stopping.load(std::memory_order_acquire)) {
                break;
            }

            try {
                if (_handler) {
                    _handler(item);
                }
            } catch (...) {
                // Executor isolation: work-item failures must not kill the worker.
            }

            _completed.fetch_add(1, std::memory_order_relaxed);
            const auto highWater = uxTaskGetStackHighWaterMark(nullptr);
            _minimumFreeStack.store(
                static_cast<uint32_t>(highWater),
                std::memory_order_relaxed
            );
        }
    }

public:
    TaskExecutor() = default;

    explicit TaskExecutor(TaskConfiguration configuration)
        : _configuration(configuration) {
    }

    ~TaskExecutor() {
        Stop();
        if (_queue != nullptr) {
            vQueueDelete(_queue);
            _queue = nullptr;
        }
        if (_startGate != nullptr) {
            vSemaphoreDelete(_startGate);
            _startGate = nullptr;
        }
    }

    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    const TaskConfiguration& GetConfiguration() const {
        return _configuration;
    }

    TaskExecutionStatus Initialize(Handler handler) {
        if (_initialized.load(std::memory_order_acquire)) {
            return TaskExecutionStatus::AlreadyInitialized;
        }
        if (!handler || _configuration.StackSize == 0 || _configuration.QueueDepth == 0) {
            return TaskExecutionStatus::InvalidConfiguration;
        }
        if (_configuration.MemoryPolicy == TaskMemoryPolicy::External) {
            return TaskExecutionStatus::UnsupportedMemoryPolicy;
        }

        _handler = std::move(handler);
        _queue = xQueueCreate(
            static_cast<UBaseType_t>(_configuration.QueueDepth),
            sizeof(TWorkItem)
        );
        if (_queue == nullptr) {
            return TaskExecutionStatus::QueueUnavailable;
        }

        _startGate = xSemaphoreCreateBinary();
        if (_startGate == nullptr) {
            vQueueDelete(_queue);
            _queue = nullptr;
            return TaskExecutionStatus::QueueUnavailable;
        }

        BaseType_t result = pdFAIL;
        if (_configuration.Core >= 0) {
            result = xTaskCreatePinnedToCore(
                _entry,
                _configuration.Name,
                _configuration.StackSize,
                this,
                _configuration.Priority,
                &_task,
                _configuration.Core
            );
        } else {
            result = xTaskCreate(
                _entry,
                _configuration.Name,
                _configuration.StackSize,
                this,
                _configuration.Priority,
                &_task
            );
        }

        if (result != pdPASS || _task == nullptr) {
            vSemaphoreDelete(_startGate);
            _startGate = nullptr;
            vQueueDelete(_queue);
            _queue = nullptr;
            return TaskExecutionStatus::TaskCreationFailed;
        }

        _minimumFreeStack.store(
            static_cast<uint32_t>(uxTaskGetStackHighWaterMark(_task)),
            std::memory_order_relaxed
        );
        _initialized.store(true, std::memory_order_release);
        return TaskExecutionStatus::Success;
    }

    TaskExecutionStatus Start() {
        if (!_initialized.load(std::memory_order_acquire)) {
            return TaskExecutionStatus::NotInitialized;
        }
        bool expected = false;
        if (!_started.compare_exchange_strong(expected, true)) {
            return TaskExecutionStatus::AlreadyStarted;
        }
        xSemaphoreGive(_startGate);
        return TaskExecutionStatus::Success;
    }

    void Stop() {
        if (!_initialized.load(std::memory_order_acquire)) {
            return;
        }

        _stopping.store(true, std::memory_order_release);
        if (_startGate != nullptr) {
            xSemaphoreGive(_startGate);
        }
        if (_task != nullptr) {
            vTaskDelete(_task);
            _task = nullptr;
        }
        _started.store(false, std::memory_order_release);
        _initialized.store(false, std::memory_order_release);
    }

    TaskExecutionStatus Submit(
        const TWorkItem& item,
        TickType_t blockTicks = 0
    ) {
        if (!_initialized.load(std::memory_order_acquire)) {
            return TaskExecutionStatus::NotInitialized;
        }
        if (!_started.load(std::memory_order_acquire)) {
            return TaskExecutionStatus::NotStarted;
        }

        BaseType_t queued = pdFALSE;
        switch (_configuration.OverflowPolicy) {
            case TaskQueueOverflowPolicy::Reject:
                queued = xQueueSend(_queue, &item, 0);
                break;

            case TaskQueueOverflowPolicy::DropNewest:
                queued = xQueueSend(_queue, &item, 0);
                if (queued != pdTRUE) {
                    _dropped.fetch_add(1, std::memory_order_relaxed);
                    return TaskExecutionStatus::QueueFull;
                }
                break;

            case TaskQueueOverflowPolicy::DropOldest: {
                queued = xQueueSend(_queue, &item, 0);
                if (queued != pdTRUE) {
                    TWorkItem discarded{};
                    if (xQueueReceive(_queue, &discarded, 0) == pdTRUE) {
                        _dropped.fetch_add(1, std::memory_order_relaxed);
                    }
                    queued = xQueueSend(_queue, &item, 0);
                }
                break;
            }

            case TaskQueueOverflowPolicy::Block:
                queued = xQueueSend(_queue, &item, blockTicks);
                break;
        }

        if (queued != pdTRUE) {
            _rejected.fetch_add(1, std::memory_order_relaxed);
            return TaskExecutionStatus::QueueFull;
        }

        _submitted.fetch_add(1, std::memory_order_relaxed);
        return TaskExecutionStatus::Success;
    }

    TaskExecutionStatistics GetStatistics() const {
        TaskExecutionStatistics statistics;
        statistics.Submitted = _submitted.load(std::memory_order_relaxed);
        statistics.Completed = _completed.load(std::memory_order_relaxed);
        statistics.Rejected = _rejected.load(std::memory_order_relaxed);
        statistics.Dropped = _dropped.load(std::memory_order_relaxed);
        statistics.ConfiguredStackSize = _configuration.StackSize;
        statistics.MinimumFreeStack = _minimumFreeStack.load(std::memory_order_relaxed);
        return statistics;
    }
};

}
}
