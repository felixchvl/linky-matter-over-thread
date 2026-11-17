/*
 * Reads the TIC from the UART and finds its speed on its own: 9600 baud in
 * standard mode, 1200 baud in historic mode.
 */
#pragma once

#include "tic_meter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called from the reader thread for every valid frame. */
typedef void (*tic_reader_cb)(const struct tic_reading *reading);

int tic_reader_start(tic_reader_cb cb);

#ifdef __cplusplus
}
#endif
