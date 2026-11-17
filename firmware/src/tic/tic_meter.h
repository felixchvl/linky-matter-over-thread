/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Turns decoded TIC groups into meter readings.
 *
 * Groups are collected while a frame is being received and only published
 * when the whole frame was valid, so a reading never mixes two frames.
 *
 * Both TIC modes end up in the same structure. Energy indexes use the
 * supplier index numbers of the standard mode (EASF01..EASF10); historic
 * labels are mapped onto them as in the Enedis spec (HCHC = 1, HCHP = 2,
 * BBRHCJB = 1 ... BBRHPJR = 6, and so on).
 */
#ifndef TIC_METER_H_
#define TIC_METER_H_

#include "tic_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIC_INDEX_COUNT 10

enum tic_field {
	TIC_HAS_TOTAL = 1u << 0,
	TIC_HAS_CURRENT_INDEX = 1u << 1,
	TIC_HAS_APPARENT_POWER = 1u << 2,
	TIC_HAS_CURRENT = 1u << 3,
	TIC_HAS_VOLTAGE = 1u << 4,
	TIC_HAS_SUBSCRIBED = 1u << 5,
	TIC_HAS_EXPORTED = 1u << 6,
	TIC_HAS_SERIAL = 1u << 7,
	TIC_HAS_TARIFF_NAME = 1u << 8,
};

struct tic_reading {
	enum tic_mode mode;
	uint32_t fields;            /* enum tic_field bits */
	uint16_t index_mask;        /* bit i set: energy_wh[i] is valid */
	uint32_t energy_wh[TIC_INDEX_COUNT];
	uint32_t total_wh;          /* EAST, or the sum of the indexes in historic mode */
	uint32_t exported_wh;       /* EAIT, standard mode only */
	uint8_t current_index;      /* 1..10, from NTARF or PTEC */
	uint32_t apparent_power_va; /* SINSTS or PAPP */
	uint16_t current_a;         /* IRMS1 or IINST */
	uint16_t voltage_v;         /* URMS1, standard mode only */
	uint16_t subscribed;        /* PREF in kVA (standard) or ISOUSC in A (historic) */
	char serial[13];            /* ADSC or ADCO */
	char tariff_name[17];       /* LTARF or PTEC */
};

typedef void (*tic_reading_cb)(const struct tic_reading *reading, void *ctx);

struct tic_meter {
	struct tic_parser parser;
	struct tic_reading pending;
	struct tic_reading last;
	bool has_reading;
	tic_reading_cb on_reading;
	void *ctx;
};

void tic_meter_init(struct tic_meter *m, tic_reading_cb on_reading, void *ctx);

/* Feed raw UART bytes (8N1, parity in bit 7). */
void tic_meter_feed(struct tic_meter *m, const uint8_t *data, size_t len);

/*
 * Active power estimated from the total energy counter.
 *
 * The Linky gives apparent power (VA) but no instantaneous active power. The
 * counter moves in 1 Wh steps, so the estimate is taken between two steps at
 * least min_window_ms apart: whole watt-hours over the time they took, with
 * no rounding error from the 1 Wh resolution. When the counter stops moving,
 * the power is capped at 1 Wh over the time since the last step, so it falls
 * towards zero instead of freezing.
 */
struct tic_power_estimator {
	uint32_t min_window_ms;
	bool started;
	uint32_t last_wh;
	uint64_t last_step_ms;
	bool has_window;
	uint32_t window_wh;
	uint64_t window_ms;
	bool has_power;
	uint32_t power_w;
};

void tic_power_init(struct tic_power_estimator *e, uint32_t min_window_ms);
/* Returns true when a new power value is available in e->power_w. */
bool tic_power_update(struct tic_power_estimator *e, uint32_t total_wh, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* TIC_METER_H_ */
