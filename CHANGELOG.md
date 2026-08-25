# Changelog

All notable changes to ESPressio Task will be documented in this file.

## 0.1.0

- Initial bounded asynchronous task execution abstraction.
- Added explicit one-shot `Task::Run()` support.
- Added typed bounded `TaskExecutor<TWorkItem>` with explicit initialize/start lifecycle.
- Added queue overflow policies and stack high-water statistics.
- Added initial ESP32/FreeRTOS backend.
