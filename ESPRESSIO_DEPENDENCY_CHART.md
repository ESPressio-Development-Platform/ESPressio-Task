# ESPressio Task dependency chart

```text
Application / ESPressio consumers
             |
             v
      ESPressio-Task
             |
             v
       ESP32 / FreeRTOS
```

The initial Task library has no mandatory ESPressio-library dependencies. This keeps it available as the lowest asynchronous execution primitive and prevents cycles when ESPressio Threads, Event, Command, or communications libraries consume it.
