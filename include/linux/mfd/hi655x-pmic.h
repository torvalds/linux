/*
 * Device driver for regulators in hi655x IC
 *
 * Copyright (c) 2016 Hisilicon.
 *
 * Authors:
 * Chen Feng <puck.chen@hisilicon.com>
 * Fei  Wang <w.f@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __HI655X_PMIC_H
#define __HI655X_PMIC_H

/* Hi655x registers are mapped to memory bus in 4 bytes stride */
#define HI655X_STRIDE                   4
#define HI655X_BUS_ADDR(x)              ((x) << 2)

#define HI655X_BITS                     8

#define HI655X_NR_IRQ                   32

#define HI655X_IRQ_STAT_BASE            (0x003 << 2)
#define HI655X_IRQ_MASK_BASE            (0x007 << 2)
#define HI655X_ANA_IRQM_BASE            (0x1b5 << 2)
#define HI655X_IRQ_ARRAY                4
#define HI655X_IRQ_MASK                 0xFF
#define HI655X_IRQ_CLR                  0xFF
#define HI655X_VER_REG                  0x00

#define PMU_VER_START                   0x10
#define PMU_VER_END                     0x38

#define RESERVE_INT                     BIT(7)
#define PWRON_D20R_INT                  BIT(6)
#define PWRON_D20F_INT                  BIT(5)
#define PWRON_D4SR_INT                  BIT(4)
#define VSYS_6P0_D200UR_INT             BIT(3)
#define VSYS_UV_D3R_INT                 BIT(2)
#define VSYS_2P5_R_INT                  BIT(1)
#define OTMP_D1R_INT                    BIT(0)

struct hi655x_pmic {
	struct resource *res;
	struct device *dev;
	struct regmap *regmap;
	int gpio;
	unsigned int ver;
	struct regmap_irq_chip_data *irq_data;
};

#endif
