/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOC_EP93XX_H
#define _SOC_EP93XX_H

struct regmap;
struct spinlock_t;

enum ep93xx_soc_model {
	EP93XX_9301_SOC,
	EP93XX_9307_SOC,
	EP93XX_9312_SOC,
};

#include <linux/auxiliary_bus.h>
#include <linux/compiler_types.h>
#include <linux/container_of.h>

#define EP93XX_CHIP_REV_D0	3
#define EP93XX_CHIP_REV_D1	4
#define EP93XX_CHIP_REV_E0	5
#define EP93XX_CHIP_REV_E1	6
#define EP93XX_CHIP_REV_E2	7

struct ep93xx_regmap_adev {
	struct auxiliary_device adev;
	struct regmap *map;
	void __iomem *base;
	spinlock_t *lock;
	void (*write)(struct regmap *map, spinlock_t *lock, unsigned int reg,
		      unsigned int val);
	void (*update_bits)(struct regmap *map, spinlock_t *lock,
			    unsigned int reg, unsigned int mask, unsigned int val);
};

#define to_ep93xx_regmap_adev(_adev) \
	container_of((_adev), struct ep93xx_regmap_adev, adev)

#endif
