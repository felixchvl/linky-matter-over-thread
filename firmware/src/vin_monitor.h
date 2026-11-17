/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Measures the input voltage after the bridge (AIN6, 1 M / 150 k divider),
 * to check the power margin on the TIC while bringing the board up.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int vin_monitor_start(void);

/* Last measured input voltage in mV, or -1 before the first measurement. */
int vin_monitor_last_mv(void);

#ifdef __cplusplus
}
#endif
