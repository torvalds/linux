/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Author: Kamel Bouhra <kamel.bouhara@bootlin.com>
 */

#ifndef POWER_ON_REASON_H
#define POWER_ON_REASON_H

#define POWER_ON_REASON_REGULAR "regular power-up"
#define POWER_ON_REASON_RTC "RTC wakeup"
#define POWER_ON_REASON_WATCHDOG "watchdog timeout"
#define POWER_ON_REASON_SOFTWARE "software reset"
#define POWER_ON_REASON_RST_BTN "reset button action"
#define POWER_ON_REASON_CPU_CLK_FAIL "CPU clock failure"
#define POWER_ON_REASON_XTAL_FAIL "crystal oscillator failure"
#define POWER_ON_REASON_BROWN_OUT "brown-out reset"
#define POWER_ON_REASON_UNKNOWN "unknown reason"

#endif /* POWER_ON_REASON_H */
