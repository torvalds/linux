/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * cs35l45.h -- CS35L45 ALSA SoC audio driver DT bindings header
 *
 * Copyright 2022 Cirrus Logic, Inc.
 */

#ifndef DT_CS35L45_H
#define DT_CS35L45_H

/*
 * cirrus,asp-sdout-hiz-ctrl
 *
 * TX_HIZ_UNUSED:   TX pin high-impedance during unused slots.
 * TX_HIZ_DISABLED: TX pin high-impedance when all channels disabled.
 */
#define CS35L45_ASP_TX_HIZ_UNUSED	0x1
#define CS35L45_ASP_TX_HIZ_DISABLED	0x2

/*
 * Optional GPIOX Sub-nodes:
 *  The cs35l45 node can have up to three "cirrus,gpio-ctrlX" ('X' = [1,2,3])
 *  sub-nodes for configuring the GPIO pins.
 *
 * - gpio-dir : GPIO pin direction. Valid only when 'gpio-ctrl'
 *   is 1.
 *    0 = Output
 *    1 = Input (Default)
 *
 * - gpio-lvl : GPIO level. Valid only when 'gpio-ctrl' is 1 and 'gpio-dir' is 0.
 *
 *    0 = Low (Default)
 *    1 = High
 *
 * - gpio-op-cfg : GPIO output configuration. Valid only when 'gpio-ctrl' is 1
 *   and 'gpio-dir' is 0.
 *
 *    0 = CMOS (Default)
 *    1 = Open Drain
 *
 * - gpio-pol : GPIO output polarity select. Valid only when 'gpio-ctrl' is 1
 *   and 'gpio-dir' is 0.
 *
 *    0 = Non-inverted, Active High (Default)
 *    1 = Inverted, Active Low
 *
 * - gpio-invert : Defines the polarity of the GPIO pin if configured
 *   as input.
 *
 *    0 = Not inverted (Default)
 *    1 = Inverted
 *
 * - gpio-ctrl : Defines the function of the GPIO pin.
 *
 * GPIO1:
 *   0 = High impedance input (Default)
 *   1 = Pin acts as a GPIO, direction controlled by 'gpio-dir'
 *   2 = Pin acts as MDSYNC, direction controlled by MDSYNC
 *   3-7 = Reserved
 *
 * GPIO2:
 *   0 = High impedance input (Default)
 *   1 = Pin acts as a GPIO, direction controlled by 'gpio-dir'
 *   2 = Pin acts as open drain INT
 *   3 = Reserved
 *   4 = Pin acts as push-pull output INT. Active low.
 *   5 = Pin acts as push-pull output INT. Active high.
 *   6,7 = Reserved
 *
 * GPIO3:
 *   0 = High impedance input (Default)
 *   1 = Pin acts as a GPIO, direction controlled by 'gpio-dir'
 *   2-7 = Reserved
 */
#define CS35L45_NUM_GPIOS	0x3

#endif /* DT_CS35L45_H */
