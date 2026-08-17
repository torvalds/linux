/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved. */

/*
 * This header provides constants for binding nvidia,tegra238-gpio*.
 *
 * The first cell in Tegra's GPIO specifier is the GPIO ID. The macros below
 * provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA238_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA238_GPIO_H

#include <dt-bindings/gpio/gpio.h>

/* GPIOs implemented by main GPIO controller */
#define TEGRA238_MAIN_GPIO_PORT_A   0
#define TEGRA238_MAIN_GPIO_PORT_B   1
#define TEGRA238_MAIN_GPIO_PORT_C   2
#define TEGRA238_MAIN_GPIO_PORT_D   3
#define TEGRA238_MAIN_GPIO_PORT_E   4
#define TEGRA238_MAIN_GPIO_PORT_F   5
#define TEGRA238_MAIN_GPIO_PORT_G   6
#define TEGRA238_MAIN_GPIO_PORT_H   7
#define TEGRA238_MAIN_GPIO_PORT_J   8
#define TEGRA238_MAIN_GPIO_PORT_K   9
#define TEGRA238_MAIN_GPIO_PORT_L  10
#define TEGRA238_MAIN_GPIO_PORT_M  11
#define TEGRA238_MAIN_GPIO_PORT_N  12
#define TEGRA238_MAIN_GPIO_PORT_P  13
#define TEGRA238_MAIN_GPIO_PORT_Q  14
#define TEGRA238_MAIN_GPIO_PORT_R  15
#define TEGRA238_MAIN_GPIO_PORT_S  16
#define TEGRA238_MAIN_GPIO_PORT_T  17
#define TEGRA238_MAIN_GPIO_PORT_U  18
#define TEGRA238_MAIN_GPIO_PORT_V  19
#define TEGRA238_MAIN_GPIO_PORT_W  20
#define TEGRA238_MAIN_GPIO_PORT_X  21

#define TEGRA238_MAIN_GPIO(port, offset) \
	((TEGRA238_MAIN_GPIO_PORT_##port * 8) + (offset))

/* GPIOs implemented by AON GPIO controller */
#define TEGRA238_AON_GPIO_PORT_AA 0
#define TEGRA238_AON_GPIO_PORT_BB 1
#define TEGRA238_AON_GPIO_PORT_CC 2
#define TEGRA238_AON_GPIO_PORT_DD 3
#define TEGRA238_AON_GPIO_PORT_EE 4
#define TEGRA238_AON_GPIO_PORT_FF 5
#define TEGRA238_AON_GPIO_PORT_GG 6
#define TEGRA238_AON_GPIO_PORT_HH 7

#define TEGRA238_AON_GPIO(port, offset) \
	((TEGRA238_AON_GPIO_PORT_##port * 8) + (offset))

#endif
