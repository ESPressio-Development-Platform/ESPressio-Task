#include <Arduino.h>
#include <ESPressio_Task.hpp>
struct WorkItem { std::uint32_t Sequence; };
struct Application {
    static ESPressio::Task::TaskExecutorConfiguration Configuration() {
        ESPressio::Task::TaskExecutorConfiguration c;
        c.Execution.Name="taskExample"; c.Execution.StackSize=3072; c.QueueDepth=8;
        return c;
    }
    ESPressio::Task::TaskExecutor<WorkItem,8> Executor{Configuration()};
    void Execute(const WorkItem& item) noexcept {
        Serial.printf("work item %lu\n",static_cast<unsigned long>(item.Sequence));
    }
};
Application application;
void setup() {
    Serial.begin(115200);
    // The composition application installs a concrete joinable System execution
    // provider and synchronization provider before this point.
    const auto status=application.Executor.Initialize<Application,&Application::Execute>(application);
    if (status==ESPressio::Task::TaskExecutionStatus::Success) application.Executor.Start();
    else Serial.println("Task providers unavailable or initialization failed");
}
void loop() {
    static std::uint32_t sequence=0;
    application.Executor.Submit(WorkItem{++sequence});
    delay(1000);
}
