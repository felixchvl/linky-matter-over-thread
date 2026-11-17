#include "tic_parser.h"

#include <string.h>

#define STX 0x02
#define ETX 0x03
#define EOT 0x04
#define LF  0x0A
#define CR  0x0D
#define SP  0x20
#define HT  0x09

#define TIC_LABEL_MAX 8

void tic_parser_init(struct tic_parser *p, bool check_parity, tic_group_cb on_group,
		     tic_frame_cb on_frame, void *ctx)
{
	memset(p, 0, sizeof(*p));
	p->check_parity = check_parity;
	p->on_group = on_group;
	p->on_frame = on_frame;
	p->ctx = ctx;
}

char tic_checksum(const char *s, size_t n)
{
	unsigned int sum = 0;

	for (size_t i = 0; i < n; i++) {
		sum += (uint8_t)s[i];
	}
	return (char)((sum & 0x3F) + 0x20);
}

static bool even_parity(uint8_t b)
{
	b ^= b >> 4;
	b ^= b >> 2;
	b ^= b >> 1;
	return (b & 1) == 0;
}

static void end_frame(struct tic_parser *p, bool valid)
{
	if (!p->in_frame) {
		return;
	}
	p->in_frame = false;
	p->in_group = false;
	if (valid) {
		p->stats.frames_ok++;
	} else {
		p->stats.frames_bad++;
	}
	if (p->on_frame) {
		p->on_frame(valid, p->ctx);
	}
}

/* Checks and splits the group in p->buf. Returns false if it is not valid. */
static bool parse_group(struct tic_parser *p, struct tic_group *g)
{
	char *b = p->buf;
	size_t n = p->len;

	/* Read from the end: the checksum is the last character and may itself be a space. */
	if (n < 4) {
		p->stats.format_errors++;
		return false;
	}
	char sep = b[n - 2];
	enum tic_mode mode;
	size_t covered;

	if (sep == HT) {
		mode = TIC_MODE_STANDARD;
		covered = n - 1; /* standard: the last separator is included */
	} else if (sep == SP) {
		mode = TIC_MODE_HISTORIC;
		covered = n - 2; /* historic: it is not */
	} else {
		p->stats.format_errors++;
		return false;
	}
	if (tic_checksum(b, covered) != b[n - 1]) {
		p->stats.checksum_errors++;
		return false;
	}

	b[n - 2] = '\0';
	char *first = strchr(b, sep);

	if (first == NULL || first == b || (size_t)(first - b) > TIC_LABEL_MAX) {
		p->stats.format_errors++;
		return false;
	}
	*first = '\0';
	g->mode = mode;
	g->label = b;
	g->timestamp = NULL;
	g->value = first + 1;

	if (mode == TIC_MODE_STANDARD) {
		char *second = strchr(first + 1, HT);

		if (second != NULL) {
			*second = '\0';
			g->timestamp = first + 1;
			g->value = second + 1;
		}
	}
	return true;
}

static void end_group(struct tic_parser *p)
{
	struct tic_group g;

	p->in_group = false;
	if (!p->group_ok || !parse_group(p, &g)) {
		p->frame_ok = false;
		return;
	}
	if (p->on_group) {
		p->on_group(&g, p->ctx);
	}
}

void tic_parser_feed(struct tic_parser *p, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t raw = data[i];
		bool parity_ok = !p->check_parity || even_parity(raw);
		uint8_t c = raw & 0x7F;

		if (!parity_ok) {
			p->stats.parity_errors++;
		}

		switch (c) {
		case STX:
			/* A new frame always restarts cleanly, even after a lost ETX. */
			end_frame(p, false);
			p->in_frame = true;
			p->in_group = false;
			p->frame_ok = parity_ok;
			break;
		case ETX:
			end_frame(p, p->frame_ok && !p->in_group && parity_ok);
			break;
		case EOT:
			/* The meter interrupted the frame. */
			end_frame(p, false);
			break;
		case LF:
			if (!p->in_frame) {
				break;
			}
			if (p->in_group) {
				p->frame_ok = false; /* previous group never ended */
			}
			p->in_group = true;
			p->group_ok = parity_ok;
			p->len = 0;
			break;
		case CR:
			if (p->in_frame && p->in_group) {
				p->group_ok = p->group_ok && parity_ok;
				end_group(p);
			}
			break;
		default:
			if (!p->in_frame || !p->in_group) {
				break;
			}
			if (!parity_ok) {
				p->group_ok = false;
			}
			if (p->len < TIC_GROUP_MAX) {
				p->buf[p->len++] = (char)c;
				p->buf[p->len] = '\0';
			} else {
				p->group_ok = false;
			}
			break;
		}
	}
}
