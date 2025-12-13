/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

/*
 * This header provides constants for the nvidia,tegra256-gpio DT binding.
 *
 * The first cell in Tegra's GPIO specifier is the GPIO ID.
 * The macros below provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA256_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA256_GPIO_H

#include <dt-bindings/gpio/gpio.h>

/* GPIOs implemented by main GPIO controller */
#define TEGRA256_MAIN_GPIO_PORT_A 0
#define TEGRA256_MAIN_GPIO_PORT_B 1
#define TEGRA256_MAIN_GPIO_PORT_C 2
#define TEGRA256_MAIN_GPIO_PORT_D 3

#define TEGRA256_MAIN_GPIO(port, offset) \
		((TEGRA256_MAIN_GPIO_PORT_##port * 8) + (offset))

#endif

