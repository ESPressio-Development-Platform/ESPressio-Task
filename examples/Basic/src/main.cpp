#include <Arduino.h>
#include <ESPressio_Task.hpp>

/**
 * ESPressio Memory Audit
 * Members:
 * - Sequence (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
