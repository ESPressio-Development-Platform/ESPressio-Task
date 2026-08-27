#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include <ESPressio_Queue.hpp>
#include <ESPressio_Synchronization.hpp>

#include "ESPressio_TaskRuntime.hpp"
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
    std::unique_ptr<System::Queue::IMessageQueue> _queue;
    std::unique_ptr<System::Synchronization::ISignal> _startGate;
    TaskHandle _task = System::Execution::InvalidExecutionHandle;
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
        TaskRuntime::Delete(System::Execution::InvalidExecutionHandle);
    }

    void _run() {
        if (_startGate == nullptr || _queue == nullptr) return;

        if (!_startGate->Wait()) return;
        if (_stopping.load(std::memory_order_acquire)) return;

        TWorkItem item{};
        while (!_stopping.load(std::memory_order_acquire)) {
            const auto received = _queue->Receive(&item);
            if (!received) continue;
            if (_stopping.load(std::memory_order_acquire)) break;

            try {
                if (_handler) _handler(item);
            } catch (...) {
                // Executor isolation: work-item failures must not kill the worker.
            }

            _completed.fetch_add(1, std::memory_order_relaxed);
            _minimumFreeStack.store(
                TaskRuntime::MinimumFreeStack(System::Execution::InvalidExecutionHandle),
                std::memory_order_relaxed
            );
        }
    }

public:
    TaskExecutor() = default;

    explicit TaskExecutor(TaskConfiguration configuration)
        : _configuration(configuration) {}

    ~TaskExecutor() {
        Stop();
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
        _stopping.store(false, std::memory_order_release);

        _queue = System::Queue::Create<TWorkItem>(_configuration.QueueDepth);
        if (_queue == nullptr) {
            return TaskExecutionStatus::QueueUnavailable;
        }

        _startGate = System::Synchronization::CreateBinarySignal();
        if (_startGate == nullptr) {
            _queue.reset();
            return TaskExecutionStatus::QueueUnavailable;
        }

        const auto created = TaskRuntime::Create(_entry, this, _configuration);
        if (!created) {
            _startGate.reset();
            _queue.reset();
            return created.Status;
        }

        _task = created.Handle;
        _minimumFreeStack.store(
            TaskRuntime::MinimumFreeStack(_task),
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
        if (_startGate == nullptr || !_startGate->Give()) {
            _started.store(false, std::memory_order_release);
            return TaskExecutionStatus::QueueUnavailable;
        }
        return TaskExecutionStatus::Success;
    }

    void Stop() {
        if (!_initialized.load(std::memory_order_acquire)) return;

        _stopping.store(true, std::memory_order_release);
        if (_startGate != nullptr) (void)_startGate->Give();

        if (_task != System::Execution::InvalidExecutionHandle) {
            TaskRuntime::Delete(_task);
            _task = System::Execution::InvalidExecutionHandle;
        }

        _started.store(false, std::memory_order_release);
        _initialized.store(false, std::memory_order_release);
        _startGate.reset();
        _queue.reset();
    }

    TaskExecutionStatus Submit(
        const TWorkItem& item,
        uint32_t blockMilliseconds = 0
    ) {
        if (!_initialized.load(std::memory_order_acquire)) {
            return TaskExecutionStatus::NotInitialized;
        }
        if (!_started.load(std::memory_order_acquire)) {
            return TaskExecutionStatus::NotStarted;
        }
        if (_queue == nullptr) {
            return TaskExecutionStatus::QueueUnavailable;
        }

        System::PlatformResult queued = System::PlatformResult::Failed(
            System::PlatformStatus::Busy
        );

        switch (_configuration.OverflowPolicy) {
            case TaskQueueOverflowPolicy::Reject:
                queued = _queue->Send(&item, 0);
                break;

            case TaskQueueOverflowPolicy::DropNewest:
                queued = _queue->Send(&item, 0);
                if (!queued) {
                    _dropped.fetch_add(1, std::memory_order_relaxed);
                    return TaskExecutionStatus::QueueFull;
                }
                break;

            case TaskQueueOverflowPolicy::DropOldest: {
                queued = _queue->Send(&item, 0);
                if (!queued) {
                    TWorkItem discarded{};
                    if (_queue->Receive(&discarded, 0)) {
                        _dropped.fetch_add(1, std::memory_order_relaxed);
                    }
                    queued = _queue->Send(&item, 0);
                }
                break;
            }

            case TaskQueueOverflowPolicy::Block:
                queued = _queue->Send(&item, blockMilliseconds);
                break;
        }

        if (!queued) {
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
