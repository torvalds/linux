/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header file for device driver Hi6421 PMIC
 *
 * Copyright (c) <2011-2014> HiSilicon Technologies Co., Ltd.
 *              http://www.hisilicon.com
 * Copyright (c) <2013-2014> Linaro Ltd.
 *              http://www.linaro.org
 *
 * Author: Guodong Xu <guodong.xu@linaro.org>
 */

#ifndef	__HI6421_PMIC_H
#define	__HI6421_PMIC_H

/* Hi6421 registers are mapped to memory bus in 4 bytes stride */
#define HI6421_REG_TO_BUS_ADDR(x)	(x << 2)

/* Hi6421 maximum register number */
#define HI6421_REG_MAX			0xFF

/* Hi6421 OCP (over current protection) and DEB (debounce) control register */
#define	HI6421_OCP_DEB_CTRL_REG		HI6421_REG_TO_BUS_ADDR(0x51)
#define	HI6421_OCP_DEB_SEL_MASK		0x0C
#define HI6421_OCP_DEB_SEL_8MS		0x00
#define HI6421_OCP_DEB_SEL_16MS		0x04
#define HI6421_OCP_DEB_SEL_32MS		0x08
#define HI6421_OCP_DEB_SEL_64MS		0x0C
#define HI6421_OCP_EN_DEBOUNCE_MASK	0x02
#define HI6421_OCP_EN_DEBOUNCE_ENABLE	0x02
#define HI6421_OCP_AUTO_STOP_MASK	0x01
#define HI6421_OCP_AUTO_STOP_ENABLE	0x01

struct hi6421_pmic {
	struct regmap		*regmap;
};

enum hi6421_type {
	HI6421 = 0,
	HI6421_V530,
};

#endif		/* __HI6421_PMIC_H */
