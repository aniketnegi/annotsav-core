#ifndef SENSOR_H
#define SENSOR_H

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
void adc_calibration_deinit(adc_cali_handle_t handle);
adc_oneshot_unit_handle_t adc_init(adc_unit_t adc_unit, adc_channel_t adc_channel, adc_atten_t adc_atten);
adc_cali_handle_t adc_calibrate();

#endif // SENSOR_H
