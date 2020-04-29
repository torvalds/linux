/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Definitions for DA9063 MFD driver
 *
 * Copyright 2012 Dialog Semiconductor Ltd.
 *
 * Author: Michal Hajduk, Dialog Semiconductor
 * Author: Krystian Garbaciak, Dialog Semiconductor
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

#define PMIC_CHIP_ID_DA9063		0x61

enum da9063_type {
	PMIC_TYPE_DA9063 = 0,
	PMIC_TYPE_DA9063L,
};

enum da9063_variant_codes {
	PMIC_DA9063_AD = 0x3,
	PMIC_DA9063_BB = 0x5,
	PMIC_DA9063_CA = 0x6,
};

/* Interrupts */
enum da9063_irqs {
	DA9063_IRQ_ONKEY = 0,
	DA9063_IRQ_ALARM,
	DA9063_IRQ_TICK,
	DA9063_IRQ_ADC_RDY,
	DA9063_IRQ_SEQ_RDY,
	DA9063_IRQ_WAKE,
	DA9063_IRQ_TEMP,
	DA9063_IRQ_COMP_1V2,
	DA9063_IRQ_LDO_LIM,
	DA9063_IRQ_REG_UVOV,
	DA9063_IRQ_DVC_RDY,
	DA9063_IRQ_VDD_MON,
	DA9063_IRQ_WARN,
	DA9063_IRQ_GPI0,
	DA9063_IRQ_GPI1,
	DA9063_IRQ_GPI2,
	DA9063_IRQ_GPI3,
	DA9063_IRQ_GPI4,
	DA9063_IRQ_GPI5,
	DA9063_IRQ_GPI6,
	DA9063_IRQ_GPI7,
	DA9063_IRQ_GPI8,
	DA9063_IRQ_GPI9,
	DA9063_IRQ_GPI10,
	DA9063_IRQ_GPI11,
	DA9063_IRQ_GPI12,
	DA9063_IRQ_GPI13,
	DA9063_IRQ_GPI14,
	DA9063_IRQ_GPI15,
};

struct da9063 {
	/* Device */
	struct device	*dev;
	enum da9063_type type;
	unsigned char	variant_code;
	unsigned int	flags;

	/* Control interface */
	struct regmap	*regmap;

	/* Interrupts */
	int		chip_irq;
	unsigned int	irq_base;
	struct regmap_irq_chip_data *regmap_irq;
};

int da9063_device_init(struct da9063 *da9063, unsigned int irq);
int da9063_irq_init(struct da9063 *da9063);

#endif /* __MFD_DA9063_CORE_H__ */
