#pragma once

#include <functional>
#include <memory>
#include <new>
#include <utility>

#include "ESPressio_TaskRuntime.hpp"
#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

/// <summary>Runs fire-and-forget work on a newly created ESPressio task execution context.</summary>
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
        TaskRuntime::Delete(System::Execution::InvalidExecutionHandle);
    }

public:
    /// <summary>Submits one callable for execution on a dedicated one-shot task.</summary>
    /// <param name="work">Callable executed by the newly created task.</param>
    /// <param name="configuration">Execution configuration used for the one-shot task.</param>
    /// <returns>The task-domain status describing whether the work was successfully submitted.</returns>
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

        const auto created = TaskRuntime::Create(
            _entry,
            invocation.get(),
            configuration
        );
        if (!created) {
            return created.Status;
        }

        invocation.release();
        return TaskExecutionStatus::Success;
    }
};

}
}
