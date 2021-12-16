/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved. */

/*
 * This header provides constants for the nvidia,tegra241-gpio DT binding.
 *
 * The first cell in Tegra's GPIO specifier is the GPIO ID. The macros below
 * provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA241_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA241_GPIO_H

#include <dt-bindings/gpio/gpio.h>

/* GPIOs implemented by main GPIO controller */
#define TEGRA241_MAIN_GPIO_PORT_A 0
#define TEGRA241_MAIN_GPIO_PORT_B 1
#define TEGRA241_MAIN_GPIO_PORT_C 2
#define TEGRA241_MAIN_GPIO_PORT_D 3
#define TEGRA241_MAIN_GPIO_PORT_E 4
#define TEGRA241_MAIN_GPIO_PORT_F 5
#define TEGRA241_MAIN_GPIO_PORT_G 6
#define TEGRA241_MAIN_GPIO_PORT_H 7
#define TEGRA241_MAIN_GPIO_PORT_I 8
#define TEGRA241_MAIN_GPIO_PORT_J 9
#define TEGRA241_MAIN_GPIO_PORT_K 10
#define TEGRA241_MAIN_GPIO_PORT_L 11

#define TEGRA241_MAIN_GPIO(port, offset) \
	((TEGRA241_MAIN_GPIO_PORT_##port * 8) + (offset))

/* GPIOs implemented by AON GPIO controller */
#define TEGRA241_AON_GPIO_PORT_AA 0
#define TEGRA241_AON_GPIO_PORT_BB 1

#define TEGRA241_AON_GPIO(port, offset) \
	((TEGRA241_AON_GPIO_PORT_##port * 8) + (offset))

#endif
