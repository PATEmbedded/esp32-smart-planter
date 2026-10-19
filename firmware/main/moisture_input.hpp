// The soil-moisture sensor on an ADC1 pin, read in millivolts with the chip's ADC calibration.
#pragma once

#include <cstdint>
#include <optional>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

class MoistureInput {
public:
    esp_err_t begin(int gpio);

    // Median of nine readings, in millivolts; nullopt if the ADC fails.
    std::optional<uint32_t> read_mv();

private:
    adc_oneshot_unit_handle_t unit_ = nullptr;
    adc_cali_handle_t cali_ = nullptr;
    adc_channel_t channel_ = ADC_CHANNEL_0;
};
