/*
 * Copyright (C) 2015-2017  Dialog Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MFD_DA9062_CORE_H__
#define __MFD_DA9062_CORE_H__

#include <linux/interrupt.h>
#include <linux/mfd/da9062/registers.h>

enum da9062_compatible_types {
	COMPAT_TYPE_DA9061 = 1,
	COMPAT_TYPE_DA9062,
};

enum da9061_irqs {
	/* IRQ A */
	DA9061_IRQ_ONKEY,
	DA9061_IRQ_WDG_WARN,
	DA9061_IRQ_SEQ_RDY,
	/* IRQ B*/
	DA9061_IRQ_TEMP,
	DA9061_IRQ_LDO_LIM,
	DA9061_IRQ_DVC_RDY,
	DA9061_IRQ_VDD_WARN,
	/* IRQ C */
	DA9061_IRQ_GPI0,
	DA9061_IRQ_GPI1,
	DA9061_IRQ_GPI2,
	DA9061_IRQ_GPI3,
	DA9061_IRQ_GPI4,

	DA9061_NUM_IRQ,
};

enum da9062_irqs {
	/* IRQ A */
	DA9062_IRQ_ONKEY,
	DA9062_IRQ_ALARM,
	DA9062_IRQ_TICK,
	DA9062_IRQ_WDG_WARN,
	DA9062_IRQ_SEQ_RDY,
	/* IRQ B*/
	DA9062_IRQ_TEMP,
	DA9062_IRQ_LDO_LIM,
	DA9062_IRQ_DVC_RDY,
	DA9062_IRQ_VDD_WARN,
	/* IRQ C */
	DA9062_IRQ_GPI0,
	DA9062_IRQ_GPI1,
	DA9062_IRQ_GPI2,
	DA9062_IRQ_GPI3,
	DA9062_IRQ_GPI4,

	DA9062_NUM_IRQ,
};

struct da9062 {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *regmap_irq;
	enum da9062_compatible_types chip_type;
};

#endif /* __MFD_DA9062_CORE_H__ */
