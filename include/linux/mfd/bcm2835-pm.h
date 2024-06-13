/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef BCM2835_MFD_PM_H
#define BCM2835_MFD_PM_H

#include <linux/regmap.h>

struct bcm2835_pm {
	struct device *dev;
	void __iomem *base;
	void __iomem *asb;
	void __iomem *rpivid_asb;
};

#endif /* BCM2835_MFD_PM_H */
