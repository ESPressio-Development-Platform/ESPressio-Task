#include <Arduino.h>
#include <ESPressio_Task.hpp>

struct WorkItem {
    uint32_t Sequence;
};

ESPressio::Task::TaskConfiguration configuration;
ESPressio::Task::TaskExecutor<WorkItem>* executor = nullptr;

void setup() {
    Serial.begin(115200);

    configuration.Name = "taskExample";
    configuration.StackSize = 3072;
    configuration.QueueDepth = 8;

    static ESPressio::Task::TaskExecutor<WorkItem> instance(configuration);
    executor = &instance;

    executor->Initialize([](const WorkItem& item) {
        Serial.printf("work item %lu\n", static_cast<unsigned long>(item.Sequence));
    });
    executor->Start();
}

void loop() {
    static uint32_t sequence = 0;
    executor->Submit(WorkItem{++sequence});
    delay(1000);
}
