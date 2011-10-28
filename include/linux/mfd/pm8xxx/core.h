/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * Qualcomm PMIC 8xxx driver header file
 *
 */

#ifndef __MFD_PM8XXX_CORE_H
#define __MFD_PM8XXX_CORE_H

#include <linux/mfd/core.h>

struct pm8xxx_drvdata {
	int	(*pmic_readb) (const struct device *dev, u16 addr, u8 *val);
	int	(*pmic_writeb) (const struct device *dev, u16 addr, u8 val);
	int	(*pmic_read_buf) (const struct device *dev, u16 addr, u8 *buf,
									int n);
	int	(*pmic_write_buf) (const struct device *dev, u16 addr, u8 *buf,
									int n);
	int	(*pmic_read_irq_stat) (const struct device *dev, int irq);
	void	*pm_chip_data;
};

static inline int pm8xxx_readb(const struct device *dev, u16 addr, u8 *val)
{
	struct pm8xxx_drvdata *dd = dev_get_drvdata(dev);

	if (!dd)
		return -EINVAL;
	return dd->pmic_readb(dev, addr, val);
}

static inline int pm8xxx_writeb(const struct device *dev, u16 addr, u8 val)
{
	struct pm8xxx_drvdata *dd = dev_get_drvdata(dev);

	if (!dd)
		return -EINVAL;
	return dd->pmic_writeb(dev, addr, val);
}

static inline int pm8xxx_read_buf(const struct device *dev, u16 addr, u8 *buf,
									int n)
{
	struct pm8xxx_drvdata *dd = dev_get_drvdata(dev);

	if (!dd)
		return -EINVAL;
	return dd->pmic_read_buf(dev, addr, buf, n);
}

static inline int pm8xxx_write_buf(const struct device *dev, u16 addr, u8 *buf,
									int n)
{
	struct pm8xxx_drvdata *dd = dev_get_drvdata(dev);

	if (!dd)
		return -EINVAL;
	return dd->pmic_write_buf(dev, addr, buf, n);
}

static inline int pm8xxx_read_irq_stat(const struct device *dev, int irq)
{
	struct pm8xxx_drvdata *dd = dev_get_drvdata(dev);

	if (!dd)
		return -EINVAL;
	return dd->pmic_read_irq_stat(dev, irq);
}

#endif
