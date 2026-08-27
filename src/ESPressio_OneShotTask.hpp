#pragma once

#include <functional>
#include <memory>
#include <new>
#include <utility>

#include "ESPressio_TaskRuntime.hpp"
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
        TaskRuntime::Delete(System::Execution::InvalidExecutionHandle);
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
