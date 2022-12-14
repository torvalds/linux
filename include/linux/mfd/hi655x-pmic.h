/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Device driver for regulators in hi655x IC
 *
 * Copyright (c) 2016 HiSilicon Ltd.
 *
 * Authors:
 * Chen Feng <puck.chen@hisilicon.com>
 * Fei  Wang <w.f@huawei.com>
 */

#ifndef __HI655X_PMIC_H
#define __HI655X_PMIC_H

#include <linux/gpio/consumer.h>

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

#define RESERVE_INT                     7
#define PWRON_D20R_INT                  6
#define PWRON_D20F_INT                  5
#define PWRON_D4SR_INT                  4
#define VSYS_6P0_D200UR_INT             3
#define VSYS_UV_D3R_INT                 2
#define VSYS_2P5_R_INT                  1
#define OTMP_D1R_INT                    0

#define RESERVE_INT_MASK                BIT(RESERVE_INT)
#define PWRON_D20R_INT_MASK             BIT(PWRON_D20R_INT)
#define PWRON_D20F_INT_MASK             BIT(PWRON_D20F_INT)
#define PWRON_D4SR_INT_MASK             BIT(PWRON_D4SR_INT)
#define VSYS_6P0_D200UR_INT_MASK        BIT(VSYS_6P0_D200UR_INT)
#define VSYS_UV_D3R_INT_MASK            BIT(VSYS_UV_D3R_INT)
#define VSYS_2P5_R_INT_MASK             BIT(VSYS_2P5_R_INT)
#define OTMP_D1R_INT_MASK               BIT(OTMP_D1R_INT)

struct hi655x_pmic {
	struct resource *res;
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *gpio;
	unsigned int ver;
	struct regmap_irq_chip_data *irq_data;
};

#endif
