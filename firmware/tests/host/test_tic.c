/*
 * Host tests for the TIC decoder. Build and run with `make` in this folder.
 *
 * Frames are built here with the checksum rules of Enedis-NOI-CPT_54E, then
 * encoded as the UART sees them (7 data bits plus an even parity bit in bit 7).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tic_meter.h"
#include "tic_parser.h"

static int failures;

#define CHECK(cond)                                                                    \
	do {                                                                           \
		if (!(cond)) {                                                         \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			failures++;                                                    \
		}                                                                      \
	} while (0)

/* ------------------------------------------------------------------ frame builder */

struct frame {
	char text[4096];
	size_t len;
};

static void frame_start(struct frame *f)
{
	f->len = 0;
	f->text[f->len++] = 0x02;
}

static void frame_end(struct frame *f)
{
	f->text[f->len++] = 0x03;
}

static void put(struct frame *f, const char *s)
{
	size_t n = strlen(s);

	memcpy(f->text + f->len, s, n);
	f->len += n;
}

/* Historic group: LF label SP value SP checksum CR, checksum without the last SP. */
static void hist(struct frame *f, const char *label, const char *value)
{
	char body[160];

	snprintf(body, sizeof(body), "%s %s", label, value);
	char cs = tic_checksum(body, strlen(body));

	f->text[f->len++] = '\n';
	put(f, body);
	f->text[f->len++] = ' ';
	f->text[f->len++] = cs;
	f->text[f->len++] = '\r';
}

/* Standard group: LF label HT [timestamp HT] value HT checksum CR, checksum with the last HT. */
static void std_group(struct frame *f, const char *label, const char *ts, const char *value)
{
	char body[200];

	if (ts) {
		snprintf(body, sizeof(body), "%s\t%s\t%s\t", label, ts, value);
	} else {
		snprintf(body, sizeof(body), "%s\t%s\t", label, value);
	}
	char cs = tic_checksum(body, strlen(body));

	f->text[f->len++] = '\n';
	put(f, body);
	f->text[f->len++] = cs;
	f->text[f->len++] = '\r';
}

/* Adds the even parity bit in bit 7, as the 8N1 UART receives a 7E1 character. */
static size_t encode(const struct frame *f, uint8_t *out)
{
	for (size_t i = 0; i < f->len; i++) {
		uint8_t c = (uint8_t)f->text[i] & 0x7F;
		int ones = __builtin_popcount(c);

		out[i] = (ones & 1) ? (uint8_t)(c | 0x80) : c;
	}
	return f->len;
}

/* ------------------------------------------------------------------ capture */

struct capture {
	int readings;
	struct tic_reading last;
};

static void on_reading(const struct tic_reading *r, void *ctx)
{
	struct capture *c = ctx;

	c->readings++;
	c->last = *r;
}

static void feed_frame(struct tic_meter *m, const struct frame *f)
{
	static uint8_t raw[sizeof(((struct frame *)0)->text)];
	size_t n = encode(f, raw);

	tic_meter_feed(m, raw, n);
}

/* ------------------------------------------------------------------ frames */

static void historic_hc_frame(struct frame *f)
{
	frame_start(f);
	hist(f, "ADCO", "031762120162");
	hist(f, "OPTARIF", "HC..");
	hist(f, "ISOUSC", "45");
	hist(f, "HCHC", "012345678");
	hist(f, "HCHP", "000987654");
	hist(f, "PTEC", "HP..");
	hist(f, "IINST", "002");
	hist(f, "IMAX", "090");
	hist(f, "PAPP", "00450");
	hist(f, "HHPHC", "A");
	hist(f, "MOTDETAT", "000000");
	frame_end(f);
}

static void standard_frame(struct frame *f)
{
	static const char pjourf[] =
		"00008001 NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE NONUTILE "
		"NONUTILE NONUTILE NO";

	frame_start(f);
	std_group(f, "ADSC", NULL, "041876097059");
	std_group(f, "VTIC", NULL, "02");
	std_group(f, "DATE", "H231117221530", "");
	std_group(f, "NGTF", NULL, "  H PLEINE/CREUSE");
	std_group(f, "LTARF", NULL, "  HEURE  CREUSE  ");
	std_group(f, "EAST", NULL, "001234567");
	std_group(f, "EASF01", NULL, "000600000");
	std_group(f, "EASF02", NULL, "000634567");
	std_group(f, "EASD01", NULL, "000600000");
	std_group(f, "IRMS1", NULL, "003");
	std_group(f, "URMS1", NULL, "232");
	std_group(f, "PREF", NULL, "06");
	std_group(f, "PCOUP", NULL, "06");
	std_group(f, "SINSTS", NULL, "00720");
	std_group(f, "SMAXSN", "H231117084512", "04500");
	std_group(f, "CCASN", "H231117220000", "00580");
	std_group(f, "UMOY1", "H231117221000", "231");
	std_group(f, "STGE", NULL, "003A0001");
	std_group(f, "MSG1", NULL, "PAS DE          MESSAGE         ");
	std_group(f, "PRM", NULL, "12345678901234");
	std_group(f, "RELAIS", NULL, "000");
	std_group(f, "NTARF", NULL, "01");
	std_group(f, "NJOURF", NULL, "00");
	std_group(f, "NJOURF+1", NULL, "00");
	std_group(f, "PJOURF+1", NULL, pjourf);
	frame_end(f);
}

/* ------------------------------------------------------------------ tests */

static void test_known_checksum(void)
{
	/* "IINST 002 Y" is the classic historic example. */
	CHECK(tic_checksum("IINST 002", 9) == 'Y');
}

static void test_historic(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;

	tic_meter_init(&m, on_reading, &c);
	historic_hc_frame(&f);
	feed_frame(&m, &f);

	CHECK(c.readings == 1);
	CHECK(c.last.mode == TIC_MODE_HISTORIC);
	CHECK(c.last.index_mask == 0x3);
	CHECK(c.last.energy_wh[0] == 12345678);
	CHECK(c.last.energy_wh[1] == 987654);
	CHECK(c.last.total_wh == 12345678 + 987654);
	CHECK(c.last.current_index == 2);
	CHECK(strcmp(c.last.tariff_name, "HP..") == 0);
	CHECK(c.last.apparent_power_va == 450);
	CHECK(c.last.current_a == 2);
	CHECK(c.last.subscribed == 45);
	CHECK(!(c.last.fields & TIC_HAS_VOLTAGE));
	CHECK(strcmp(c.last.serial, "031762120162") == 0);
	CHECK(m.parser.stats.frames_ok == 1 && m.parser.stats.checksum_errors == 0);
}

static void test_standard(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;

	tic_meter_init(&m, on_reading, &c);
	standard_frame(&f);
	feed_frame(&m, &f);

	CHECK(c.readings == 1);
	CHECK(c.last.mode == TIC_MODE_STANDARD);
	CHECK(c.last.total_wh == 1234567);
	CHECK(c.last.index_mask == 0x3);
	CHECK(c.last.energy_wh[0] == 600000);
	CHECK(c.last.energy_wh[1] == 634567);
	CHECK(c.last.current_index == 1);
	CHECK(strcmp(c.last.tariff_name, "  HEURE  CREUSE") == 0);
	CHECK(c.last.apparent_power_va == 720);
	CHECK(c.last.current_a == 3);
	CHECK(c.last.voltage_v == 232);
	CHECK(c.last.subscribed == 6);
	CHECK(!(c.last.fields & TIC_HAS_EXPORTED));
	CHECK(m.parser.stats.format_errors == 0 && m.parser.stats.checksum_errors == 0);
}

struct ts_capture {
	int n;
	char ts[16];
	char value[16];
};

static void on_group_ts(const struct tic_group *g, void *ctx)
{
	struct ts_capture *seen = ctx;

	seen->n++;
	if (g->timestamp) {
		snprintf(seen->ts, sizeof(seen->ts), "%s", g->timestamp);
	}
	snprintf(seen->value, sizeof(seen->value), "%s", g->value);
}

static void test_standard_timestamped_group(void)
{
	struct tic_parser p;
	struct frame f;
	uint8_t raw[256];
	struct ts_capture seen = {0};

	tic_parser_init(&p, true, on_group_ts, NULL, &seen);
	frame_start(&f);
	std_group(&f, "SMAXSN", "E240714074553", "04500");
	std_group(&f, "DATE", "E240714074553", "");
	frame_end(&f);
	tic_parser_feed(&p, raw, encode(&f, raw));

	CHECK(seen.n == 2);
	CHECK(strcmp(seen.ts, "E240714074553") == 0);
	CHECK(strcmp(seen.value, "") == 0); /* DATE has an empty value */
	CHECK(p.stats.frames_ok == 1);
}

static void test_bad_checksum_drops_frame(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;

	tic_meter_init(&m, on_reading, &c);
	historic_hc_frame(&f);
	/* Corrupt one digit of HCHC without fixing its checksum. */
	char *p = strstr(f.text, "HCHC 0123");

	assert(p);
	p[5] = '9';
	feed_frame(&m, &f);
	CHECK(c.readings == 0);
	CHECK(m.parser.stats.checksum_errors == 1);
	CHECK(m.parser.stats.frames_bad == 1);

	historic_hc_frame(&f);
	feed_frame(&m, &f);
	CHECK(c.readings == 1);
}

static void test_parity_error_drops_frame(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;
	uint8_t raw[4096];

	tic_meter_init(&m, on_reading, &c);
	historic_hc_frame(&f);
	size_t n = encode(&f, raw);
	uint8_t *p = memchr(raw, 'P', n); /* first character of "PTEC" (even parity, no bit 7) */

	assert(p);
	*p ^= 0x80; /* flip only the parity bit: the 7-bit text is unchanged */
	tic_meter_feed(&m, raw, n);
	CHECK(c.readings == 0);
	CHECK(m.parser.stats.parity_errors == 1);
}

static void test_eot_and_mid_frame_start(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;
	uint8_t raw[4096];

	tic_meter_init(&m, on_reading, &c);

	/* Start listening in the middle of a frame: the tail must be ignored. */
	historic_hc_frame(&f);
	size_t n = encode(&f, raw);
	size_t cut = n / 2;

	tic_meter_feed(&m, raw + cut, n - cut);
	CHECK(c.readings == 0);

	/* A frame interrupted by EOT is dropped. */
	frame_start(&f);
	hist(&f, "HCHC", "012345678");
	f.text[f.len++] = 0x04;
	feed_frame(&m, &f);
	CHECK(c.readings == 0);
	CHECK(m.parser.stats.frames_bad == 1);

	historic_hc_frame(&f);
	feed_frame(&m, &f);
	CHECK(c.readings == 1);
}

static void test_chunking(void)
{
	struct frame f;
	uint8_t raw[4096];

	standard_frame(&f);
	size_t n = encode(&f, raw);

	for (size_t chunk = 1; chunk <= 37; chunk += 6) {
		struct tic_meter m;
		struct capture c = {0};

		tic_meter_init(&m, on_reading, &c);
		for (size_t i = 0; i < n; i += chunk) {
			tic_meter_feed(&m, raw + i, (i + chunk <= n) ? chunk : n - i);
		}
		CHECK(c.readings == 1);
		CHECK(c.last.total_wh == 1234567);
	}
}

static void test_space_checksum(void)
{
	/* A historic group whose checksum is itself a space must still parse. */
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;
	char value[10];
	int found = 0;

	for (int v = 0; v < 1000000 && !found; v++) {
		char body[32];

		snprintf(value, sizeof(value), "%09d", v);
		snprintf(body, sizeof(body), "BASE %s", value);
		found = tic_checksum(body, strlen(body)) == ' ';
	}
	assert(found);
	tic_meter_init(&m, on_reading, &c);
	frame_start(&f);
	hist(&f, "OPTARIF", "BASE");
	hist(&f, "BASE", value);
	hist(&f, "PTEC", "TH..");
	frame_end(&f);
	feed_frame(&m, &f);
	CHECK(c.readings == 1);
	CHECK(c.last.energy_wh[0] == (uint32_t)atoi(value));
	CHECK(c.last.current_index == 1);
}

static void test_tempo_mapping(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;

	tic_meter_init(&m, on_reading, &c);
	frame_start(&f);
	hist(&f, "OPTARIF", "BBR(");
	hist(&f, "BBRHCJB", "000000001");
	hist(&f, "BBRHPJB", "000000002");
	hist(&f, "BBRHCJW", "000000003");
	hist(&f, "BBRHPJW", "000000004");
	hist(&f, "BBRHCJR", "000000005");
	hist(&f, "BBRHPJR", "000000006");
	hist(&f, "PTEC", "HPJR");
	frame_end(&f);
	feed_frame(&m, &f);
	CHECK(c.readings == 1);
	CHECK(c.last.index_mask == 0x3F);
	for (int i = 0; i < 6; i++) {
		CHECK(c.last.energy_wh[i] == (uint32_t)(i + 1));
	}
	CHECK(c.last.total_wh == 21);
	CHECK(c.last.current_index == 6);
}

static void test_overlong_group(void)
{
	struct tic_meter m;
	struct capture c = {0};
	struct frame f;
	char big[TIC_GROUP_MAX + 20];

	memset(big, 'A', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	tic_meter_init(&m, on_reading, &c);
	frame_start(&f);
	hist(&f, "BASE", "000001000");
	hist(&f, "MSG", big);
	frame_end(&f);
	feed_frame(&m, &f);
	CHECK(c.readings == 0);
}

static void test_power_estimator(void)
{
	struct tic_power_estimator e;
	uint32_t wh = 1000;
	uint64_t t = 0;
	bool got = false;

	tic_power_init(&e, 60000);
	/* 1000 W: one watt-hour every 3.6 s; frames every 1.2 s. */
	for (int i = 0; i < 200; i++) {
		t += 1200;
		if (i % 3 == 2) {
			wh++;
		}
		got |= tic_power_update(&e, wh, t);
	}
	CHECK(got && e.has_power);
	CHECK(e.power_w == 1000);

	/* Load switched off: no step for two minutes, power falls to 30 W or less. */
	for (int i = 0; i < 100; i++) {
		t += 1200;
		tic_power_update(&e, wh, t);
	}
	CHECK(e.power_w <= 30);

	/* Counter went backwards (meter replaced): no power until a new window. */
	CHECK(tic_power_update(&e, 10, t + 1200));
	CHECK(!e.has_power);
}

int main(void)
{
	test_known_checksum();
	test_historic();
	test_standard();
	test_standard_timestamped_group();
	test_bad_checksum_drops_frame();
	test_parity_error_drops_frame();
	test_eot_and_mid_frame_start();
	test_chunking();
	test_space_checksum();
	test_tempo_mapping();
	test_overlong_group();
	test_power_estimator();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all TIC tests passed\n");
	return 0;
}
