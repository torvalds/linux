/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 ARM Limited, All Rights Reserved.
 */
#ifndef __LINUX_IRQCHIP_ARM_GIC_V5_H
#define __LINUX_IRQCHIP_ARM_GIC_V5_H

#include <linux/iopoll.h>

#include <asm/sysreg.h>

/*
 * INTID handling
 */
#define GICV5_HWIRQ_ID			GENMASK(23, 0)
#define GICV5_HWIRQ_TYPE		GENMASK(31, 29)
#define GICV5_HWIRQ_INTID		GENMASK_ULL(31, 0)

#define GICV5_HWIRQ_TYPE_PPI		UL(0x1)
#define GICV5_HWIRQ_TYPE_SPI		UL(0x3)

/*
 * Tables attributes
 */
#define GICV5_NO_READ_ALLOC		0b0
#define GICV5_READ_ALLOC		0b1
#define GICV5_NO_WRITE_ALLOC		0b0
#define GICV5_WRITE_ALLOC		0b1

#define GICV5_NON_CACHE			0b00
#define GICV5_WB_CACHE			0b01
#define GICV5_WT_CACHE			0b10

#define GICV5_NON_SHARE			0b00
#define GICV5_OUTER_SHARE		0b10
#define GICV5_INNER_SHARE		0b11

/*
 * IRS registers
 */
#define GICV5_IRS_IDR1			0x0004
#define GICV5_IRS_IDR2			0x0008
#define GICV5_IRS_IDR5			0x0014
#define GICV5_IRS_IDR6			0x0018
#define GICV5_IRS_IDR7			0x001c
#define GICV5_IRS_CR0			0x0080
#define GICV5_IRS_CR1			0x0084
#define GICV5_IRS_SPI_SELR		0x0108
#define GICV5_IRS_SPI_CFGR		0x0114
#define GICV5_IRS_SPI_STATUSR		0x0118
#define GICV5_IRS_PE_SELR		0x0140
#define GICV5_IRS_PE_STATUSR		0x0144
#define GICV5_IRS_PE_CR0		0x0148

#define GICV5_IRS_IDR1_PRIORITY_BITS	GENMASK(22, 20)
#define GICV5_IRS_IDR1_IAFFID_BITS	GENMASK(19, 16)

#define GICV5_IRS_IDR1_PRIORITY_BITS_1BITS	0b000
#define GICV5_IRS_IDR1_PRIORITY_BITS_2BITS	0b001
#define GICV5_IRS_IDR1_PRIORITY_BITS_3BITS	0b010
#define GICV5_IRS_IDR1_PRIORITY_BITS_4BITS	0b011
#define GICV5_IRS_IDR1_PRIORITY_BITS_5BITS	0b100

#define GICV5_IRS_IDR2_ISTMD_SZ		GENMASK(19, 15)
#define GICV5_IRS_IDR2_ISTMD		BIT(14)
#define GICV5_IRS_IDR2_IST_L2SZ		GENMASK(13, 11)
#define GICV5_IRS_IDR2_IST_LEVELS	BIT(10)
#define GICV5_IRS_IDR2_MIN_LPI_ID_BITS	GENMASK(9, 6)
#define GICV5_IRS_IDR2_LPI		BIT(5)
#define GICV5_IRS_IDR2_ID_BITS		GENMASK(4, 0)

#define GICV5_IRS_IDR5_SPI_RANGE	GENMASK(24, 0)
#define GICV5_IRS_IDR6_SPI_IRS_RANGE	GENMASK(24, 0)
#define GICV5_IRS_IDR7_SPI_BASE		GENMASK(23, 0)
#define GICV5_IRS_CR0_IDLE		BIT(1)
#define GICV5_IRS_CR0_IRSEN		BIT(0)

#define GICV5_IRS_CR1_VPED_WA		BIT(15)
#define GICV5_IRS_CR1_VPED_RA		BIT(14)
#define GICV5_IRS_CR1_VMD_WA		BIT(13)
#define GICV5_IRS_CR1_VMD_RA		BIT(12)
#define GICV5_IRS_CR1_VPET_WA		BIT(11)
#define GICV5_IRS_CR1_VPET_RA		BIT(10)
#define GICV5_IRS_CR1_VMT_WA		BIT(9)
#define GICV5_IRS_CR1_VMT_RA		BIT(8)
#define GICV5_IRS_CR1_IST_WA		BIT(7)
#define GICV5_IRS_CR1_IST_RA		BIT(6)
#define GICV5_IRS_CR1_IC		GENMASK(5, 4)
#define GICV5_IRS_CR1_OC		GENMASK(3, 2)
#define GICV5_IRS_CR1_SH		GENMASK(1, 0)

#define GICV5_IRS_SPI_STATUSR_V		BIT(1)
#define GICV5_IRS_SPI_STATUSR_IDLE	BIT(0)

#define GICV5_IRS_SPI_SELR_ID		GENMASK(23, 0)

#define GICV5_IRS_SPI_CFGR_TM		BIT(0)

#define GICV5_IRS_PE_SELR_IAFFID	GENMASK(15, 0)

#define GICV5_IRS_PE_STATUSR_V		BIT(1)
#define GICV5_IRS_PE_STATUSR_IDLE	BIT(0)

#define GICV5_IRS_PE_CR0_DPS		BIT(0)

/*
 * Global Data structures and functions
 */
struct gicv5_chip_data {
	struct fwnode_handle	*fwnode;
	struct irq_domain	*ppi_domain;
	struct irq_domain	*spi_domain;
	u32			global_spi_count;
	u8			cpuif_pri_bits;
	u8			irs_pri_bits;
};

extern struct gicv5_chip_data gicv5_global_data __read_mostly;

struct gicv5_irs_chip_data {
	struct list_head	entry;
	struct fwnode_handle	*fwnode;
	void __iomem		*irs_base;
	u32			flags;
	u32			spi_min;
	u32			spi_range;
	raw_spinlock_t		spi_config_lock;
};

static inline int gicv5_wait_for_op_s_atomic(void __iomem *addr, u32 offset,
					     const char *reg_s, u32 mask,
					     u32 *val)
{
	void __iomem *reg = addr + offset;
	u32 tmp;
	int ret;

	ret = readl_poll_timeout_atomic(reg, tmp, tmp & mask, 1, 10 * USEC_PER_MSEC);
	if (unlikely(ret == -ETIMEDOUT)) {
		pr_err_ratelimited("%s timeout...\n", reg_s);
		return ret;
	}

	if (val)
		*val = tmp;

	return 0;
}

#define gicv5_wait_for_op_atomic(base, reg, mask, val) \
	gicv5_wait_for_op_s_atomic(base, reg, #reg, mask, val)

int gicv5_irs_of_probe(struct device_node *parent);
void gicv5_irs_remove(void);
int gicv5_irs_register_cpu(int cpuid);
int gicv5_irs_cpu_to_iaffid(int cpu_id, u16 *iaffid);
struct gicv5_irs_chip_data *gicv5_irs_lookup_by_spi_id(u32 spi_id);
int gicv5_spi_irq_set_type(struct irq_data *d, unsigned int type);
#endif
