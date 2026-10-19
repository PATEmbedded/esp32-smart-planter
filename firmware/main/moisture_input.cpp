#include "moisture_input.hpp"

#include <array>

#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "planter/moisture.hpp"

namespace {

const char* const TAG = "moisture";

// The full-range attenuation was renamed from 11 dB to 12 dB in ESP-IDF 5.2.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;
#else
constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_11;
#endif

}  // namespace

esp_err_t MoistureInput::begin(int gpio) {
    adc_unit_t unit = ADC_UNIT_1;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(gpio, &unit, &channel_), TAG, "GPIO %d has no ADC channel", gpio);
    ESP_RETURN_ON_FALSE(unit == ADC_UNIT_1, ESP_ERR_INVALID_ARG, TAG, "GPIO %d is on ADC2; use an ADC1 pin", gpio);

    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &unit_), TAG, "cannot open ADC1");
    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.atten = kAttenuation;
    channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(unit_, channel_, &channel_config), TAG, "cannot set up GPIO %d",
                        gpio);

    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = unit;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    cali_config.chan = channel_;
#endif
    cali_config.atten = kAttenuation;
    cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {};
    cali_config.unit_id = unit;
    cali_config.atten = kAttenuation;
    cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_cali_create_scheme_line_fitting(&cali_config, &cali_);
#endif
    if (err != ESP_OK) {
        cali_ = nullptr;
        ESP_LOGW(TAG, "no ADC calibration data in this chip (%s); millivolts are approximate", esp_err_to_name(err));
    }
    return ESP_OK;
}

std::optional<uint32_t> MoistureInput::read_mv() {
    std::array<uint32_t, 9> samples{};
    for (uint32_t& sample : samples) {
        int raw = 0;
        if (adc_oneshot_read(unit_, channel_, &raw) != ESP_OK) {
            return std::nullopt;
        }
        int mv = 0;
        if (cali_ != nullptr && adc_cali_raw_to_voltage(cali_, raw, &mv) == ESP_OK) {
            sample = static_cast<uint32_t>(mv);
        } else {
            sample = static_cast<uint32_t>(raw) * 3100u / 4095u;  // rough full scale of a 12-bit reading
        }
    }
    return planter::median(samples);
}
