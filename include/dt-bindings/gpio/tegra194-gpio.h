/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved. */

/*
 * This header provides constants for binding nvidia,tegra194-gpio*.
 *
 * The first cell in Tegra's GPIO specifier is the GPIO ID. The macros below
 * provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA194_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA194_GPIO_H

#include <dt-bindings/gpio/gpio.h>

/* GPIOs implemented by main GPIO controller */
#define TEGRA194_MAIN_GPIO_PORT_A 0
#define TEGRA194_MAIN_GPIO_PORT_B 1
#define TEGRA194_MAIN_GPIO_PORT_C 2
#define TEGRA194_MAIN_GPIO_PORT_D 3
#define TEGRA194_MAIN_GPIO_PORT_E 4
#define TEGRA194_MAIN_GPIO_PORT_F 5
#define TEGRA194_MAIN_GPIO_PORT_G 6
#define TEGRA194_MAIN_GPIO_PORT_H 7
#define TEGRA194_MAIN_GPIO_PORT_I 8
#define TEGRA194_MAIN_GPIO_PORT_J 9
#define TEGRA194_MAIN_GPIO_PORT_K 10
#define TEGRA194_MAIN_GPIO_PORT_L 11
#define TEGRA194_MAIN_GPIO_PORT_M 12
#define TEGRA194_MAIN_GPIO_PORT_N 13
#define TEGRA194_MAIN_GPIO_PORT_O 14
#define TEGRA194_MAIN_GPIO_PORT_P 15
#define TEGRA194_MAIN_GPIO_PORT_Q 16
#define TEGRA194_MAIN_GPIO_PORT_R 17
#define TEGRA194_MAIN_GPIO_PORT_S 18
#define TEGRA194_MAIN_GPIO_PORT_T 19
#define TEGRA194_MAIN_GPIO_PORT_U 20
#define TEGRA194_MAIN_GPIO_PORT_V 21
#define TEGRA194_MAIN_GPIO_PORT_W 22
#define TEGRA194_MAIN_GPIO_PORT_X 23
#define TEGRA194_MAIN_GPIO_PORT_Y 24
#define TEGRA194_MAIN_GPIO_PORT_Z 25
#define TEGRA194_MAIN_GPIO_PORT_FF 26
#define TEGRA194_MAIN_GPIO_PORT_GG 27

#define TEGRA194_MAIN_GPIO(port, offset) \
	((TEGRA194_MAIN_GPIO_PORT_##port * 8) + offset)

/* GPIOs implemented by AON GPIO controller */
#define TEGRA194_AON_GPIO_PORT_AA 0
#define TEGRA194_AON_GPIO_PORT_BB 1
#define TEGRA194_AON_GPIO_PORT_CC 2
#define TEGRA194_AON_GPIO_PORT_DD 3
#define TEGRA194_AON_GPIO_PORT_EE 4

#define TEGRA194_AON_GPIO(port, offset) \
	((TEGRA194_AON_GPIO_PORT_##port * 8) + offset)

#endif
