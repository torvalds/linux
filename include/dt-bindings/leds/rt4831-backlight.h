/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for rt4831 backlight bindings.
 *
 * Copyright (C) 2020, Richtek Technology Corp.
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#ifndef _DT_BINDINGS_RT4831_BACKLIGHT_H
#define _DT_BINDINGS_RT4831_BACKLIGHT_H

#define RT4831_BLOVPLVL_17V	0
#define RT4831_BLOVPLVL_21V	1
#define RT4831_BLOVPLVL_25V	2
#define RT4831_BLOVPLVL_29V	3

#define RT4831_BLED_CH1EN	(1 << 0)
#define RT4831_BLED_CH2EN	(1 << 1)
#define RT4831_BLED_CH3EN	(1 << 2)
#define RT4831_BLED_CH4EN	(1 << 3)
#define RT4831_BLED_ALLCHEN	((1 << 4) - 1)

#endif /* _DT_BINDINGS_RT4831_BACKLIGHT_H */
