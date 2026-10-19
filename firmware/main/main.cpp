// Smart planter: waters a pot from a small tank when the soil gets dry.
//
// Once a second the control task reads the soil-moisture sensor and the tank switch, works out
// from the clock whether a watering window is open, and lets planter::WateringController decide.
// The pump never runs longer than one pulse even if this task stops: every pulse is timed by
// esp32-gpio-devices, and the task watchdog resets the chip if the task hangs.
#include <gpio_devices.h>

#include <cinttypes>
#include <ctime>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "moisture_input.hpp"
#include "network.hpp"
#include "nvs_flash.h"
#include "planter/moisture.hpp"
#include "planter/schedule.hpp"
#include "planter/watering.hpp"
#include "sdkconfig.h"
#include "time_sync.h"

namespace {

const char* const TAG = "planter";

enum class Command : uint8_t { pulse, clear_fault };

QueueHandle_t s_commands;

#ifdef CONFIG_PLANTER_PUMP_ACTIVE_LOW
constexpr bool kPumpActiveLow = true;
#else
constexpr bool kPumpActiveLow = false;
#endif

gpiodev::Output s_pump([] {
    gpiodev::OutputConfig config;
    config.pin = static_cast<gpio_num_t>(CONFIG_PLANTER_PUMP_GPIO);
    config.active_low = kPumpActiveLow;
    return config;
}());
gpiodev::Output s_led(static_cast<gpio_num_t>(CONFIG_PLANTER_LED_GPIO));
gpiodev::Button s_button([] {
    gpiodev::ButtonConfig config;
    config.pin = static_cast<gpio_num_t>(CONFIG_PLANTER_BUTTON_GPIO);
    config.timing.long_press_ms = 3000;
    config.timing.max_clicks = 1;  // react to every click at once
    return config;
}());
gpiodev::Button* s_tank = nullptr;  // only with a float switch

planter::WateringConfig watering_config() {
    planter::WateringConfig c;
    c.start_below = CONFIG_PLANTER_START_BELOW;
    c.stop_at = CONFIG_PLANTER_STOP_AT;
    c.critical_below = CONFIG_PLANTER_CRITICAL_BELOW;
    c.pulse_ms = CONFIG_PLANTER_PULSE_S * 1000u;
    c.soak_ms = CONFIG_PLANTER_SOAK_S * 1000u;
    c.daily_budget_ms = CONFIG_PLANTER_DAILY_BUDGET_S * 1000u;
    c.max_ineffective_pulses = CONFIG_PLANTER_MAX_INEFFECTIVE_PULSES;
    c.min_rise = CONFIG_PLANTER_MIN_RISE;
    return c;
}

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void on_button(const gpiodev::ButtonEvent& event) {
    Command command;
    if (event.type == gpiodev::ButtonEventType::clicked) {
        command = Command::pulse;
    } else if (event.type == gpiodev::ButtonEventType::long_press) {
        command = Command::clear_fault;
    } else {
        return;
    }
    xQueueSend(s_commands, &command, 0);
}

void show(planter::Phase phase) {
    switch (phase) {
        case planter::Phase::idle:
            s_led.off();
            break;
        case planter::Phase::watering:
            s_led.on();
            break;
        case planter::Phase::soaking:
            s_led.blink(200, 1800);
            break;
        case planter::Phase::fault:
            s_led.blink(100, 100);
            break;
    }
}

// Inside a watering window? Only a clock set from a time server knows the time of day.
std::optional<bool> window_now(const planter::WateringWindows& windows) {
    const ts_status_t status = time_sync_status();
    if (status != TS_STATUS_SYNCED && status != TS_STATUS_STALE) {
        return std::nullopt;
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return planter::in_window(windows, &local);
}

// One line with everything needed to calibrate the sensor and follow the decisions.
void log_state(const planter::Readings& readings, std::optional<uint32_t> mv,
               const planter::WateringController& controller) {
    char clock[TS_ISO8601_SIZE];
    time_sync_format_now(clock, sizeof(clock));
    const bool faulty = controller.fault() != planter::Fault::none;
    ESP_LOGI(TAG, "%s soil %.1f %% (%" PRIu32 " mV), tank %s, window %s, %s%s%s, pump %" PRIu32 " s, clock %s", clock,
             readings.moisture ? static_cast<double>(*readings.moisture) : -1.0, mv.value_or(0),
             readings.tank_empty ? "empty" : "ok",
             readings.in_window ? (*readings.in_window ? "open" : "closed") : "unknown",
             planter::to_string(controller.phase()), faulty ? ": " : "",
             faulty ? planter::to_string(controller.fault()) : "", controller.pump_time_used_ms() / 1000u,
             ts_status_name(time_sync_status()));
}

void control_task(void* /*arg*/) {
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));

    MoistureInput sensor;
    ESP_ERROR_CHECK(sensor.begin(CONFIG_PLANTER_MOISTURE_GPIO));
    const planter::MoistureCalibration calibration{CONFIG_PLANTER_DRY_MV, CONFIG_PLANTER_WET_MV};

    planter::WateringWindows windows(false);
    if (!planter::add_window(windows, CONFIG_PLANTER_WINDOW_1) ||
        !planter::add_window(windows, CONFIG_PLANTER_WINDOW_2)) {
        ESP_LOGE(TAG, "a watering window is not in the form HH:MM-HH:MM; check menuconfig");
    }

    const planter::WateringConfig config = watering_config();
    planter::WateringController controller(config);
    controller.start(now_ms());
    show(controller.phase());
    planter::Phase shown = controller.phase();
    bool pump_was_on = false;
    uint32_t last_log_ms = 0;

    for (;;) {
        const std::optional<uint32_t> mv = sensor.read_mv();
        planter::Readings readings;
        readings.moisture = mv ? planter::moisture_percent(*mv, calibration) : std::nullopt;
        readings.tank_empty = s_tank != nullptr && s_tank->is_pressed();
        readings.in_window = window_now(windows);
        const uint32_t now = now_ms();

        Command command;
        while (xQueueReceive(s_commands, &command, 0) == pdTRUE) {
            if (command == Command::pulse) {
                if (!controller.request_pulse(readings, now)) {
                    ESP_LOGI(TAG, "no pulse now (%s, %s)", planter::to_string(controller.phase()),
                             readings.tank_empty ? "tank empty" : "budget or state");
                }
            } else {
                controller.clear_fault(now);
            }
        }
        controller.update(readings, now);

        // The pump output times every pulse itself, a little longer than the controller, as a
        // second limit; the controller switches it off at the right moment.
        if (controller.pump_on() && !pump_was_on) {
            s_pump.pulse(config.pulse_ms + 1000u);
        } else if (!controller.pump_on() && pump_was_on) {
            s_pump.off();
        }
        pump_was_on = controller.pump_on();

        if (controller.phase() != shown) {
            shown = controller.phase();
            show(shown);
        }
        if (now - last_log_ms >= 60000u || controller.phase() == planter::Phase::watering) {
            last_log_ms = now;
            log_state(readings, mv, controller);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void on_tank(const gpiodev::ButtonEvent& event) {
    if (event.type == gpiodev::ButtonEventType::pressed) {
        ESP_LOGW(TAG, "tank empty");
    } else if (event.type == gpiodev::ButtonEventType::released) {
        ESP_LOGI(TAG, "tank refilled");
    }
}

}  // namespace

extern "C" void app_main() {
    // The pump first, so it is driven off as early as possible.
    ESP_ERROR_CHECK(s_pump.begin());
    ESP_ERROR_CHECK(s_led.begin());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (start_network(CONFIG_PLANTER_WIFI_SSID, CONFIG_PLANTER_WIFI_PASSWORD)) {
        time_sync_config_t time_config = TIME_SYNC_CONFIG_DEFAULT();
        time_config.servers[0] = CONFIG_PLANTER_NTP_SERVER;
        time_config.timezone = CONFIG_PLANTER_TIMEZONE;
        ESP_ERROR_CHECK(time_sync_start(&time_config));
    }

    s_commands = xQueueCreate(4, sizeof(Command));
    ESP_ERROR_CHECK(s_button.begin(on_button));
    if (CONFIG_PLANTER_TANK_GPIO >= 0) {
        static gpiodev::Button tank(static_cast<gpio_num_t>(CONFIG_PLANTER_TANK_GPIO));  // closes to GND when empty
        ESP_ERROR_CHECK(tank.begin(on_tank));
        s_tank = &tank;
    }

    xTaskCreate(control_task, "control", 6144, nullptr, 5, nullptr);
}
