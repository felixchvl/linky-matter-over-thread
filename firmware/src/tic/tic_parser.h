/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Byte-level decoder for the Linky TIC output (Enedis-NOI-CPT_54E).
 *
 * The TIC sends 7E1 characters. The UART reads them as 8N1, so bit 7 of each
 * byte is the parity bit: the decoder checks it and strips it.
 *
 * A frame is STX, a series of groups, ETX. A group is LF, label, separator,
 * [timestamp, separator,] value, separator, checksum, CR. The separator is a
 * space in historic mode and a horizontal tab in standard mode.
 *
 * This file has no Zephyr dependency so it can be tested on the host.
 */
#ifndef TIC_PARSER_H_
#define TIC_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest group in the spec is PJOURF+1 (98 data characters). */
#define TIC_GROUP_MAX 128

enum tic_mode {
	TIC_MODE_UNKNOWN = 0,
	TIC_MODE_HISTORIC,
	TIC_MODE_STANDARD,
};

struct tic_group {
	enum tic_mode mode;
	const char *label;
	const char *timestamp; /* NULL when the group has none */
	const char *value;     /* may be empty */
};

struct tic_stats {
	uint32_t frames_ok;
	uint32_t frames_bad;
	uint32_t checksum_errors;
	uint32_t parity_errors;
	uint32_t format_errors;
};

/* Called for each valid group inside a frame. */
typedef void (*tic_group_cb)(const struct tic_group *group, void *ctx);
/* Called at the end of each frame. valid is false if any group was lost. */
typedef void (*tic_frame_cb)(bool valid, void *ctx);

struct tic_parser {
	char buf[TIC_GROUP_MAX + 1];
	size_t len;
	bool in_frame;
	bool in_group;
	bool group_ok;
	bool frame_ok;
	bool check_parity;
	tic_group_cb on_group;
	tic_frame_cb on_frame;
	void *ctx;
	struct tic_stats stats;
};

void tic_parser_init(struct tic_parser *p, bool check_parity, tic_group_cb on_group,
		     tic_frame_cb on_frame, void *ctx);

void tic_parser_feed(struct tic_parser *p, const uint8_t *data, size_t len);

/* Checksum character over n bytes: (sum & 0x3F) + 0x20. */
char tic_checksum(const char *s, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* TIC_PARSER_H_ */
