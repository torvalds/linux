/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved. */

/*
 * This header provides constants for binding nvidia,tegra234-gpio*.
 *
 * The first cell in Tegra's GPIO specifier is the GPIO ID. The macros below
 * provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA234_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA234_GPIO_H

#include <dt-bindings/gpio/gpio.h>

/* GPIOs implemented by main GPIO controller */
#define TEGRA234_MAIN_GPIO_PORT_A   0
#define TEGRA234_MAIN_GPIO_PORT_B   1
#define TEGRA234_MAIN_GPIO_PORT_C   2
#define TEGRA234_MAIN_GPIO_PORT_D   3
#define TEGRA234_MAIN_GPIO_PORT_E   4
#define TEGRA234_MAIN_GPIO_PORT_F   5
#define TEGRA234_MAIN_GPIO_PORT_G   6
#define TEGRA234_MAIN_GPIO_PORT_H   7
#define TEGRA234_MAIN_GPIO_PORT_I   8
#define TEGRA234_MAIN_GPIO_PORT_J   9
#define TEGRA234_MAIN_GPIO_PORT_K  10
#define TEGRA234_MAIN_GPIO_PORT_L  11
#define TEGRA234_MAIN_GPIO_PORT_M  12
#define TEGRA234_MAIN_GPIO_PORT_N  13
#define TEGRA234_MAIN_GPIO_PORT_P  14
#define TEGRA234_MAIN_GPIO_PORT_Q  15
#define TEGRA234_MAIN_GPIO_PORT_R  16
#define TEGRA234_MAIN_GPIO_PORT_S  17
#define TEGRA234_MAIN_GPIO_PORT_T  18
#define TEGRA234_MAIN_GPIO_PORT_U  19
#define TEGRA234_MAIN_GPIO_PORT_V  20
#define TEGRA234_MAIN_GPIO_PORT_X  21
#define TEGRA234_MAIN_GPIO_PORT_Y  22
#define TEGRA234_MAIN_GPIO_PORT_Z  23
#define TEGRA234_MAIN_GPIO_PORT_AC 24
#define TEGRA234_MAIN_GPIO_PORT_AD 25
#define TEGRA234_MAIN_GPIO_PORT_AE 26
#define TEGRA234_MAIN_GPIO_PORT_AF 27
#define TEGRA234_MAIN_GPIO_PORT_AG 28

#define TEGRA234_MAIN_GPIO(port, offset) \
	((TEGRA234_MAIN_GPIO_PORT_##port * 8) + offset)

/* GPIOs implemented by AON GPIO controller */
#define TEGRA234_AON_GPIO_PORT_AA 0
#define TEGRA234_AON_GPIO_PORT_BB 1
#define TEGRA234_AON_GPIO_PORT_CC 2
#define TEGRA234_AON_GPIO_PORT_DD 3
#define TEGRA234_AON_GPIO_PORT_EE 4
#define TEGRA234_AON_GPIO_PORT_GG 5

#define TEGRA234_AON_GPIO(port, offset) \
	((TEGRA234_AON_GPIO_PORT_##port * 8) + offset)

#endif
