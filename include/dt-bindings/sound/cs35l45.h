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

#endif /* DT_CS35L45_H */
