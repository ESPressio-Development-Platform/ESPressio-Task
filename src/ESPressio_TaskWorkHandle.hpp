#pragma once
#include <cstdint>
#include "ESPressio_TaskTypes.hpp"
namespace ESPressio::Task {
/// <summary>Non-owning control token scoped to one worker and one non-reused assignment generation.</summary>
struct TaskWorkHandle final {
    const void* Worker = nullptr;
    std::uint64_t Generation = 0;
    constexpr explicit operator bool() const noexcept { return Worker && Generation; }
};
/// <summary>Non-blocking slot admission result. Failure does not consume the supplied descriptor.</summary>
struct TaskWorkSubmission final {
    TaskExecutionStatus Status = TaskExecutionStatus::NotInitialized;
    TaskWorkHandle Handle{};
    constexpr explicit operator bool() const noexcept { return Status==TaskExecutionStatus::Success; }
};
/// <summary>Cancellation never interrupts a claimed handler or targets a different assignment.</summary>
enum class TaskWorkCancelStatus : std::uint8_t { Cancelled, Stale, InProgress, Busy, NotInitialized, Stopping };
}
