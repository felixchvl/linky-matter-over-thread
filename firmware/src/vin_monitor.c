#include "vin_monitor.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vin, CONFIG_LOG_DEFAULT_LEVEL);

#define VIN_NODE DT_NODELABEL(vin_sense)

static const struct adc_dt_spec adc = ADC_DT_SPEC_GET(VIN_NODE);
static const uint32_t full_ohms = DT_PROP(VIN_NODE, full_ohms);
static const uint32_t output_ohms = DT_PROP(VIN_NODE, output_ohms);

static int last_mv = -1;
static void vin_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(vin_work, vin_work_fn);

static int measure(void)
{
	int16_t raw;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int err = adc_sequence_init_dt(&adc, &seq);

	if (!err) {
		err = adc_read_dt(&adc, &seq);
	}
	if (err) {
		return err;
	}
	int32_t mv = raw < 0 ? 0 : raw;

	err = adc_raw_to_millivolts_dt(&adc, &mv);
	if (err) {
		return err;
	}
	last_mv = (int)((int64_t)mv * full_ohms / output_ohms);
	return 0;
}

static void vin_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (measure() == 0) {
		LOG_INF("VIN %d mV", last_mv);
	}
	k_work_reschedule(&vin_work, K_SECONDS(CONFIG_LINKY_VIN_LOG_INTERVAL_S));
}

int vin_monitor_start(void)
{
	if (!adc_is_ready_dt(&adc)) {
		return -ENODEV;
	}
	int err = adc_channel_setup_dt(&adc);

	if (err) {
		return err;
	}
	if (CONFIG_LINKY_VIN_LOG_INTERVAL_S > 0) {
		k_work_schedule(&vin_work, K_SECONDS(1));
	}
	return 0;
}

int vin_monitor_last_mv(void)
{
	return last_mv;
}
