# FreeRTOS Mutexes on ESP32-S3 (Dual-Core)

## Does ESP32-S3 Use Both Cores?

Yes. ESP-IDF FreeRTOS schedules tasks on **both** cores by default. Unless you pin a task to a specific core with `xTaskCreatePinnedToCore()`, it can run on either. Two tasks can execute **truly in parallel** — making data races a real concern.

## Do You Need a Mutex in Your Code?

Your current ISR → queue → `gpio_task` pattern is already safe — FreeRTOS queues have built-in synchronization.

**You WILL need a mutex** once you add a shared `hid_report` variable that:
- `gpio_task` **writes** (updating button bits on GPIO events)
- A separate HID send task **reads** (sending the report over USB)

## Mutex in 30 Seconds

A mutex is a lock. Only one task can hold it at a time:

```
Task A                        Task B
  │                             │
  ├─ xSemaphoreTake(mutex) ►   │  (A holds lock)
  │  [write shared data]       ├─ xSemaphoreTake(mutex) ► BLOCKS
  ├─ xSemaphoreGive(mutex) ►   │  (A releases)
  │                             ├─ (B unblocks, acquires lock)
  │                             │  [read shared data]
  │                             ├─ xSemaphoreGive(mutex)
```

## API

```c
#include "freertos/semphr.h"

// Create (once, before tasks start)
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

// Use
xSemaphoreTake(mutex, portMAX_DELAY);   // lock (blocks until available)
// ... access shared data ...
xSemaphoreGive(mutex);                  // unlock
```

## Rules

| Rule | Why |
|------|-----|
| **Never** take a mutex in an ISR | ISRs cannot block — use queues to defer to a task |
| Always give after take | Forgetting = deadlock |
| Keep critical sections short | Copy data out, process outside the lock |
| Create before starting tasks | Avoid race on the handle itself |

## Applied to Your Code

```c
#include "freertos/semphr.h"

static uint8_t hid_report_buttons = 0;
static SemaphoreHandle_t report_mutex = NULL;

void app_main(void)
{
    report_mutex = xSemaphoreCreateMutex();

    // ... existing gpio config, queue, ISR setup ...

    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);
    xTaskCreate(hid_send_task, "hid_send", 2048, NULL, 5, NULL);

    // ... tinyusb init ...
}

static void gpio_task(void *arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int level = gpio_get_level(io_num);

            xSemaphoreTake(report_mutex, portMAX_DELAY);
            if (io_num == GPIO_INPUT_GEAR_UP)
                hid_report_buttons = (hid_report_buttons & ~0x01) | (!level);
            else if (io_num == GPIO_INPUT_GEAR_DOWN)
                hid_report_buttons = (hid_report_buttons & ~0x02) | ((!level) << 1);
            xSemaphoreGive(report_mutex);
        }
    }
}

static void hid_send_task(void *arg)
{
    for (;;) {
        if (tud_hid_ready()) {
            uint8_t report;

            xSemaphoreTake(report_mutex, portMAX_DELAY);
            report = hid_report_buttons;
            xSemaphoreGive(report_mutex);

            tud_hid_report(0, &report, sizeof(report));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

Note: `!level` because your pull-ups mean pressed = LOW (0).

## Alternative: Skip the Mutex for a Single Byte

For a single `uint8_t`, reads/writes are atomic on ESP32-S3. You could skip the mutex here. But once your report grows beyond one byte, you **must** use a mutex to keep fields consistent. Using one from the start is the safe, scalable choice.
