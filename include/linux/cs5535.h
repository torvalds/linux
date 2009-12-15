/*
 * AMD CS5535/CS5536 definitions
 * Copyright (C) 2006  Advanced Micro Devices, Inc.
 * Copyright (C) 2009  Andres Salomon <dilinger@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef _CS5535_H
#define _CS5535_H

/* MSRs */
#define MSR_LBAR_SMB		0x5140000B
#define MSR_LBAR_GPIO		0x5140000C
#define MSR_LBAR_MFGPT		0x5140000D
#define MSR_LBAR_ACPI		0x5140000E
#define MSR_LBAR_PMS		0x5140000F

/* resource sizes */
#define LBAR_GPIO_SIZE		0xFF
#define LBAR_MFGPT_SIZE		0x40
#define LBAR_ACPI_SIZE		0x40
#define LBAR_PMS_SIZE		0x80

/* GPIOs */
#define GPIO_OUTPUT_VAL		0x00
#define GPIO_OUTPUT_ENABLE	0x04
#define GPIO_OUTPUT_OPEN_DRAIN	0x08
#define GPIO_OUTPUT_INVERT	0x0C
#define GPIO_OUTPUT_AUX1	0x10
#define GPIO_OUTPUT_AUX2	0x14
#define GPIO_PULL_UP		0x18
#define GPIO_PULL_DOWN		0x1C
#define GPIO_INPUT_ENABLE	0x20
#define GPIO_INPUT_INVERT	0x24
#define GPIO_INPUT_FILTER	0x28
#define GPIO_INPUT_EVENT_COUNT	0x2C
#define GPIO_READ_BACK		0x30
#define GPIO_INPUT_AUX1		0x34
#define GPIO_EVENTS_ENABLE	0x38
#define GPIO_LOCK_ENABLE	0x3C
#define GPIO_POSITIVE_EDGE_EN	0x40
#define GPIO_NEGATIVE_EDGE_EN	0x44
#define GPIO_POSITIVE_EDGE_STS	0x48
#define GPIO_NEGATIVE_EDGE_STS	0x4C

#define GPIO_MAP_X		0xE0
#define GPIO_MAP_Y		0xE4
#define GPIO_MAP_Z		0xE8
#define GPIO_MAP_W		0xEC

void cs5535_gpio_set(unsigned offset, unsigned int reg);
void cs5535_gpio_clear(unsigned offset, unsigned int reg);
int cs5535_gpio_isset(unsigned offset, unsigned int reg);

#endif
