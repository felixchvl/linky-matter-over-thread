/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tic_meter.h"

#include <string.h>

struct label_index {
	const char *label;
	uint8_t index; /* 1-based supplier index */
};

/* Historic energy labels and the supplier index they map to (spec table 9). */
static const struct label_index historic_energy[] = {
	{"BASE", 1},    {"HCHC", 1},    {"HCHP", 2},    {"EJPHN", 1},   {"EJPHPM", 2},
	{"BBRHCJB", 1}, {"BBRHPJB", 2}, {"BBRHCJW", 3}, {"BBRHPJW", 4}, {"BBRHCJR", 5},
	{"BBRHPJR", 6},
};

/* Historic PTEC values and the matching supplier index. */
static const struct label_index historic_period[] = {
	{"TH..", 1}, {"HC..", 1}, {"HP..", 2}, {"HN..", 1}, {"PM..", 2}, {"HCJB", 1},
	{"HPJB", 2}, {"HCJW", 3}, {"HPJW", 4}, {"HCJR", 5}, {"HPJR", 6},
};

/* Parses a field made only of decimal digits. Leading zeros are normal in the TIC. */
static bool parse_u32(const char *s, uint32_t *out)
{
	uint64_t v = 0;

	if (*s == '\0') {
		return false;
	}
	for (; *s; s++) {
		if (*s < '0' || *s > '9') {
			return false;
		}
		v = v * 10 + (uint64_t)(*s - '0');
		if (v > UINT32_MAX) {
			return false;
		}
	}
	*out = (uint32_t)v;
	return true;
}

static void copy_text(char *dst, size_t size, const char *src)
{
	size_t n = strlen(src);

	if (n >= size) {
		n = size - 1;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
	/* LTARF is space padded to 16 characters. */
	while (n > 0 && dst[n - 1] == ' ') {
		dst[--n] = '\0';
	}
}

static bool lookup(const struct label_index *table, size_t count, const char *key, uint8_t *index)
{
	for (size_t i = 0; i < count; i++) {
		if (strcmp(table[i].label, key) == 0) {
			*index = table[i].index;
			return true;
		}
	}
	return false;
}

static void set_energy(struct tic_reading *r, uint8_t index, const char *value)
{
	uint32_t wh;

	if (index < 1 || index > TIC_INDEX_COUNT || !parse_u32(value, &wh)) {
		return;
	}
	r->energy_wh[index - 1] = wh;
	r->index_mask |= (uint16_t)(1u << (index - 1));
}

static void set_u16(struct tic_reading *r, uint16_t *dst, uint32_t flag, const char *value)
{
	uint32_t v;

	if (parse_u32(value, &v) && v <= UINT16_MAX) {
		*dst = (uint16_t)v;
		r->fields |= flag;
	}
}

static void on_group(const struct tic_group *g, void *ctx)
{
	struct tic_meter *m = ctx;
	struct tic_reading *r = &m->pending;
	const char *l = g->label;
	uint8_t index;
	uint32_t v;

	r->mode = g->mode;

	if (g->mode == TIC_MODE_STANDARD) {
		if (strncmp(l, "EASF", 4) == 0 && strlen(l) == 6 && parse_u32(l + 4, &v)) {
			set_energy(r, (uint8_t)v, g->value);
		} else if (strcmp(l, "EAST") == 0) {
			if (parse_u32(g->value, &r->total_wh)) {
				r->fields |= TIC_HAS_TOTAL;
			}
		} else if (strcmp(l, "EAIT") == 0) {
			if (parse_u32(g->value, &r->exported_wh)) {
				r->fields |= TIC_HAS_EXPORTED;
			}
		} else if (strcmp(l, "NTARF") == 0) {
			if (parse_u32(g->value, &v) && v >= 1 && v <= TIC_INDEX_COUNT) {
				r->current_index = (uint8_t)v;
				r->fields |= TIC_HAS_CURRENT_INDEX;
			}
		} else if (strcmp(l, "SINSTS") == 0) {
			if (parse_u32(g->value, &r->apparent_power_va)) {
				r->fields |= TIC_HAS_APPARENT_POWER;
			}
		} else if (strcmp(l, "IRMS1") == 0) {
			set_u16(r, &r->current_a, TIC_HAS_CURRENT, g->value);
		} else if (strcmp(l, "URMS1") == 0) {
			set_u16(r, &r->voltage_v, TIC_HAS_VOLTAGE, g->value);
		} else if (strcmp(l, "PREF") == 0) {
			set_u16(r, &r->subscribed, TIC_HAS_SUBSCRIBED, g->value);
		} else if (strcmp(l, "LTARF") == 0) {
			copy_text(r->tariff_name, sizeof(r->tariff_name), g->value);
			r->fields |= TIC_HAS_TARIFF_NAME;
		} else if (strcmp(l, "ADSC") == 0) {
			copy_text(r->serial, sizeof(r->serial), g->value);
			r->fields |= TIC_HAS_SERIAL;
		}
		return;
	}

	if (lookup(historic_energy, sizeof(historic_energy) / sizeof(historic_energy[0]), l,
		   &index)) {
		set_energy(r, index, g->value);
	} else if (strcmp(l, "PTEC") == 0) {
		copy_text(r->tariff_name, sizeof(r->tariff_name), g->value);
		r->fields |= TIC_HAS_TARIFF_NAME;
		if (lookup(historic_period, sizeof(historic_period) / sizeof(historic_period[0]),
			   g->value, &index)) {
			r->current_index = index;
			r->fields |= TIC_HAS_CURRENT_INDEX;
		}
	} else if (strcmp(l, "PAPP") == 0) {
		if (parse_u32(g->value, &r->apparent_power_va)) {
			r->fields |= TIC_HAS_APPARENT_POWER;
		}
	} else if (strcmp(l, "IINST") == 0) {
		set_u16(r, &r->current_a, TIC_HAS_CURRENT, g->value);
	} else if (strcmp(l, "ISOUSC") == 0) {
		set_u16(r, &r->subscribed, TIC_HAS_SUBSCRIBED, g->value);
	} else if (strcmp(l, "ADCO") == 0) {
		copy_text(r->serial, sizeof(r->serial), g->value);
		r->fields |= TIC_HAS_SERIAL;
	}
}

static void on_frame(bool valid, void *ctx)
{
	struct tic_meter *m = ctx;
	struct tic_reading *r = &m->pending;

	if (valid && r->index_mask != 0) {
		/* Historic mode has no total counter: it is the sum of the indexes. */
		if (r->mode == TIC_MODE_HISTORIC) {
			uint64_t sum = 0;

			for (int i = 0; i < TIC_INDEX_COUNT; i++) {
				if (r->index_mask & (1u << i)) {
					sum += r->energy_wh[i];
				}
			}
			if (sum <= UINT32_MAX) {
				r->total_wh = (uint32_t)sum;
				r->fields |= TIC_HAS_TOTAL;
			}
		}
		m->last = *r;
		m->has_reading = true;
		if (m->on_reading) {
			m->on_reading(&m->last, m->ctx);
		}
	}
	memset(r, 0, sizeof(*r));
}

void tic_meter_init(struct tic_meter *m, tic_reading_cb on_reading, void *ctx)
{
	memset(m, 0, sizeof(*m));
	m->on_reading = on_reading;
	m->ctx = ctx;
	tic_parser_init(&m->parser, true, on_group, on_frame, m);
}

void tic_meter_feed(struct tic_meter *m, const uint8_t *data, size_t len)
{
	tic_parser_feed(&m->parser, data, len);
}

void tic_power_init(struct tic_power_estimator *e, uint32_t min_window_ms)
{
	memset(e, 0, sizeof(*e));
	e->min_window_ms = min_window_ms;
}

/* W = Wh * 3600 / s = Wh * 3 600 000 / ms, rounded */
static uint32_t watts(uint64_t wh, uint64_t ms)
{
	return (uint32_t)((wh * 3600000u + ms / 2) / ms);
}

bool tic_power_update(struct tic_power_estimator *e, uint32_t total_wh, uint64_t now_ms)
{
	if (!e->started || total_wh < e->last_wh || now_ms < e->last_step_ms) {
		/* First value, or the counter went backwards (meter swap): restart. */
		bool had_power = e->has_power;

		tic_power_init(e, e->min_window_ms);
		e->started = true;
		e->last_wh = total_wh;
		e->last_step_ms = now_ms;
		return had_power; /* has_power is now false: tell the caller */
	}

	if (total_wh != e->last_wh) {
		e->last_wh = total_wh;
		e->last_step_ms = now_ms;
		if (!e->has_window) {
			/* First step seen: the window starts on it. */
			e->has_window = true;
			e->window_wh = total_wh;
			e->window_ms = now_ms;
			return false;
		}
		uint64_t elapsed = now_ms - e->window_ms;

		if (elapsed < e->min_window_ms) {
			return false;
		}
		e->power_w = watts(total_wh - e->window_wh, elapsed);
		e->has_power = true;
		e->window_wh = total_wh;
		e->window_ms = now_ms;
		return true;
	}

	/* No step since last_step_ms: the power is below 1 Wh over that time. */
	uint64_t quiet = now_ms - e->last_step_ms;

	if (e->has_window && quiet >= e->min_window_ms) {
		uint32_t cap = watts(1, quiet);

		if (!e->has_power || cap < e->power_w) {
			e->power_w = cap;
			e->has_power = true;
			return true;
		}
	}
	return false;
}
