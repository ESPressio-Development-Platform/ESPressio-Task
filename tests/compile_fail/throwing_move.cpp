#include <ESPressio_IdleWorkerTask.hpp>
struct Work { Work(Work&&) noexcept(false) {} };
ESPressio::Task::IdleWorkerTask<Work> worker;
