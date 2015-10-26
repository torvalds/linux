/*
 * Header file for device driver Hi655X PMIC
 *
 * Copyright (C) 2015 Hisilicon Co. Ltd.
 *
 * Fei Wang <w.f@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#ifndef __HI655X_PMIC_H
#define __HI655X_PMIC_H

/* Hi655x registers are mapped to memory bus in 4 bytes stride */
#define HI655X_REG_TO_BUS_ADDR(x)	((x) << 2)

#define HI655X_BITS			            (8)

/*numb of sub-interrupt*/
#define HI655X_NR_IRQ			        (32)

#define HI655X_IRQ_STAT_BASE	        (0x003)
#define HI655X_IRQ_MASK_BASE	        (0x007)
#define HI655X_IRQ_ARRAY			    (4)
#define HI655X_IRQ_MASK                 (0xFF)
#define HI655X_IRQ_CLR                  (0xFF)
#define HI655X_VER_REG                  (0x000)

#define PMU_VER_START  0x10
#define PMU_VER_END    0x38

struct hi655x_pmic {
	struct resource		*res;
	struct device		*dev;
	struct regmap		*regmap;
	spinlock_t ssi_hw_lock;
	struct clk          *clk;
	struct irq_domain	*domain;
	int			irq;
	int			gpio;
	unsigned int    irqs[HI655X_NR_IRQ];
	unsigned int       ver;
};
#endif
