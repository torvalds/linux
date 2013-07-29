/*
 * Definitions for DA9063 MFD driver
 *
 * Copyright 2012 Dialog Semiconductor Ltd.
 *
 * Author: Michal Hajduk <michal.hajduk@diasemi.com>
 *	   Krystian Garbaciak <krystian.garbaciak@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_DA9063_CORE_H__
#define __MFD_DA9063_CORE_H__

#include <linux/interrupt.h>
#include <linux/mfd/da9063/registers.h>

/* DA9063 modules */
#define DA9063_DRVNAME_CORE		"da9063-core"
#define DA9063_DRVNAME_REGULATORS	"da9063-regulators"
#define DA9063_DRVNAME_LEDS		"da9063-leds"
#define DA9063_DRVNAME_WATCHDOG		"da9063-watchdog"
#define DA9063_DRVNAME_HWMON		"da9063-hwmon"
#define DA9063_DRVNAME_ONKEY		"da9063-onkey"
#define DA9063_DRVNAME_RTC		"da9063-rtc"
#define DA9063_DRVNAME_VIBRATION	"da9063-vibration"

enum da9063_models {
	PMIC_DA9063 = 0x61,
};

struct da9063 {
	/* Device */
	struct device	*dev;
	unsigned short	model;
	unsigned short	revision;
	unsigned int	flags;

	/* Control interface */
	struct regmap	*regmap;

	/* Interrupts */
	int		chip_irq;
	unsigned int	irq_base;
};

int da9063_device_init(struct da9063 *da9063, unsigned int irq);

void da9063_device_exit(struct da9063 *da9063);

#endif /* __MFD_DA9063_CORE_H__ */
