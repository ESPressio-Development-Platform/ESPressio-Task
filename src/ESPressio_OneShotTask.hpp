#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <ESPressio_Memory.hpp>

#include "ESPressio_TaskRuntime.hpp"
#include "ESPressio_TaskTypes.hpp"

namespace ESPressio {
namespace Task {

/// <summary>Runs fire-and-forget work on a newly created ESPressio task execution context.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class Task {
    static constexpr auto InvocationMemoryPolicy =
        System::Memory::MemoryPolicy::ExternalPreferred;

/**
 * ESPressio Memory Audit
 * Members:
 * - Provider (System::Memory::IMemoryProvider*): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct Invocation {
        System::Memory::IMemoryProvider* Provider = nullptr;
        std::function<void()> Work;
    };

    using InvocationPointer =
        System::Memory::UniquePtr<Invocation, InvocationMemoryPolicy>;

    static void _entry(void* parameter) {
        auto* rawInvocation = static_cast<Invocation*>(parameter);
        System::Memory::IMemoryProvider& provider =
            rawInvocation != nullptr && rawInvocation->Provider != nullptr
                ? *rawInvocation->Provider
                : System::Memory::GetProvider();
        InvocationPointer invocation(
            rawInvocation,
            System::Memory::ObjectDeleter<Invocation, InvocationMemoryPolicy>(provider)
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
    /// <remarks>
    /// The invocation control object is retained in externally preferred System memory so fire-and-forget bookkeeping
    /// does not consume scarce internal DRAM on platforms with external memory. The platform task stack remains governed
    /// independently by the task runtime provider and is not moved to external memory by this allocation policy.
    /// </remarks>
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

        InvocationPointer invocation;
        try {
            invocation = System::Memory::MakeUnique<Invocation, InvocationMemoryPolicy>(
                Invocation{nullptr, std::move(work)}
            );
        } catch (...) {
            return TaskExecutionStatus::TaskCreationFailed;
        }
        if (!invocation) {
            return TaskExecutionStatus::TaskCreationFailed;
        }
        invocation->Provider = invocation.get_deleter().Provider();

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
