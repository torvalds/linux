/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 ARM Limited, All Rights Reserved.
 */
#ifndef __LINUX_IRQCHIP_ARM_GIC_V5_H
#define __LINUX_IRQCHIP_ARM_GIC_V5_H

#include <linux/iopoll.h>

#include <asm/cacheflush.h>
#include <asm/smp.h>
#include <asm/sysreg.h>

#define GICV5_IPIS_PER_CPU		MAX_IPI

/*
 * INTID handling
 */
#define GICV5_HWIRQ_ID			GENMASK(23, 0)
#define GICV5_HWIRQ_TYPE		GENMASK(31, 29)
#define GICV5_HWIRQ_INTID		GENMASK_ULL(31, 0)

#define GICV5_HWIRQ_TYPE_PPI		UL(0x1)
#define GICV5_HWIRQ_TYPE_LPI		UL(0x2)
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
 * IRS registers and tables structures
 */
#define GICV5_IRS_IDR1			0x0004
#define GICV5_IRS_IDR2			0x0008
#define GICV5_IRS_IDR5			0x0014
#define GICV5_IRS_IDR6			0x0018
#define GICV5_IRS_IDR7			0x001c
#define GICV5_IRS_CR0			0x0080
#define GICV5_IRS_CR1			0x0084
#define GICV5_IRS_SYNCR			0x00c0
#define GICV5_IRS_SYNC_STATUSR		0x00c4
#define GICV5_IRS_SPI_SELR		0x0108
#define GICV5_IRS_SPI_CFGR		0x0114
#define GICV5_IRS_SPI_STATUSR		0x0118
#define GICV5_IRS_PE_SELR		0x0140
#define GICV5_IRS_PE_STATUSR		0x0144
#define GICV5_IRS_PE_CR0		0x0148
#define GICV5_IRS_IST_BASER		0x0180
#define GICV5_IRS_IST_CFGR		0x0190
#define GICV5_IRS_IST_STATUSR		0x0194
#define GICV5_IRS_MAP_L2_ISTR		0x01c0

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

#define GICV5_IRS_IST_L2SZ_SUPPORT_4KB(r)	FIELD_GET(BIT(11), (r))
#define GICV5_IRS_IST_L2SZ_SUPPORT_16KB(r)	FIELD_GET(BIT(12), (r))
#define GICV5_IRS_IST_L2SZ_SUPPORT_64KB(r)	FIELD_GET(BIT(13), (r))

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

#define GICV5_IRS_SYNCR_SYNC		BIT(31)

#define GICV5_IRS_SYNC_STATUSR_IDLE	BIT(0)

#define GICV5_IRS_SPI_STATUSR_V		BIT(1)
#define GICV5_IRS_SPI_STATUSR_IDLE	BIT(0)

#define GICV5_IRS_SPI_SELR_ID		GENMASK(23, 0)

#define GICV5_IRS_SPI_CFGR_TM		BIT(0)

#define GICV5_IRS_PE_SELR_IAFFID	GENMASK(15, 0)

#define GICV5_IRS_PE_STATUSR_V		BIT(1)
#define GICV5_IRS_PE_STATUSR_IDLE	BIT(0)

#define GICV5_IRS_PE_CR0_DPS		BIT(0)

#define GICV5_IRS_IST_STATUSR_IDLE	BIT(0)

#define GICV5_IRS_IST_CFGR_STRUCTURE	BIT(16)
#define GICV5_IRS_IST_CFGR_ISTSZ	GENMASK(8, 7)
#define GICV5_IRS_IST_CFGR_L2SZ		GENMASK(6, 5)
#define GICV5_IRS_IST_CFGR_LPI_ID_BITS	GENMASK(4, 0)

#define GICV5_IRS_IST_CFGR_STRUCTURE_LINEAR	0b0
#define GICV5_IRS_IST_CFGR_STRUCTURE_TWO_LEVEL	0b1

#define GICV5_IRS_IST_CFGR_ISTSZ_4	0b00
#define GICV5_IRS_IST_CFGR_ISTSZ_8	0b01
#define GICV5_IRS_IST_CFGR_ISTSZ_16	0b10

#define GICV5_IRS_IST_CFGR_L2SZ_4K	0b00
#define GICV5_IRS_IST_CFGR_L2SZ_16K	0b01
#define GICV5_IRS_IST_CFGR_L2SZ_64K	0b10

#define GICV5_IRS_IST_BASER_ADDR_MASK	GENMASK_ULL(55, 6)
#define GICV5_IRS_IST_BASER_VALID	BIT_ULL(0)

#define GICV5_IRS_MAP_L2_ISTR_ID	GENMASK(23, 0)

#define GICV5_ISTL1E_VALID		BIT_ULL(0)

#define GICV5_ISTL1E_L2_ADDR_MASK	GENMASK_ULL(55, 12)

/*
 * ITS registers and tables structures
 */
#define GICV5_ITS_IDR1		0x0004
#define GICV5_ITS_IDR2		0x0008
#define GICV5_ITS_CR0		0x0080
#define GICV5_ITS_CR1		0x0084
#define GICV5_ITS_DT_BASER	0x00c0
#define GICV5_ITS_DT_CFGR	0x00d0
#define GICV5_ITS_DIDR		0x0100
#define GICV5_ITS_EIDR		0x0108
#define GICV5_ITS_INV_EVENTR	0x010c
#define GICV5_ITS_INV_DEVICER	0x0110
#define GICV5_ITS_STATUSR	0x0120
#define GICV5_ITS_SYNCR		0x0140
#define GICV5_ITS_SYNC_STATUSR	0x0148

#define GICV5_ITS_IDR1_L2SZ			GENMASK(10, 8)
#define GICV5_ITS_IDR1_ITT_LEVELS		BIT(7)
#define GICV5_ITS_IDR1_DT_LEVELS		BIT(6)
#define GICV5_ITS_IDR1_DEVICEID_BITS		GENMASK(5, 0)

#define GICV5_ITS_IDR1_L2SZ_SUPPORT_4KB(r)	FIELD_GET(BIT(8), (r))
#define GICV5_ITS_IDR1_L2SZ_SUPPORT_16KB(r)	FIELD_GET(BIT(9), (r))
#define GICV5_ITS_IDR1_L2SZ_SUPPORT_64KB(r)	FIELD_GET(BIT(10), (r))

#define GICV5_ITS_IDR2_XDMN_EVENTs		GENMASK(6, 5)
#define GICV5_ITS_IDR2_EVENTID_BITS		GENMASK(4, 0)

#define GICV5_ITS_CR0_IDLE			BIT(1)
#define GICV5_ITS_CR0_ITSEN			BIT(0)

#define GICV5_ITS_CR1_ITT_RA			BIT(7)
#define GICV5_ITS_CR1_DT_RA			BIT(6)
#define GICV5_ITS_CR1_IC			GENMASK(5, 4)
#define GICV5_ITS_CR1_OC			GENMASK(3, 2)
#define GICV5_ITS_CR1_SH			GENMASK(1, 0)

#define GICV5_ITS_DT_CFGR_STRUCTURE		BIT(16)
#define GICV5_ITS_DT_CFGR_L2SZ			GENMASK(7, 6)
#define GICV5_ITS_DT_CFGR_DEVICEID_BITS		GENMASK(5, 0)

#define GICV5_ITS_DT_BASER_ADDR_MASK		GENMASK_ULL(55, 3)

#define GICV5_ITS_INV_DEVICER_I			BIT(31)
#define GICV5_ITS_INV_DEVICER_EVENTID_BITS	GENMASK(5, 1)
#define GICV5_ITS_INV_DEVICER_L1		BIT(0)

#define GICV5_ITS_DIDR_DEVICEID			GENMASK_ULL(31, 0)

#define GICV5_ITS_EIDR_EVENTID			GENMASK(15, 0)

#define GICV5_ITS_INV_EVENTR_I			BIT(31)
#define GICV5_ITS_INV_EVENTR_ITT_L2SZ		GENMASK(2, 1)
#define GICV5_ITS_INV_EVENTR_L1			BIT(0)

#define GICV5_ITS_STATUSR_IDLE			BIT(0)

#define GICV5_ITS_SYNCR_SYNC			BIT_ULL(63)
#define GICV5_ITS_SYNCR_SYNCALL			BIT_ULL(32)
#define GICV5_ITS_SYNCR_DEVICEID		GENMASK_ULL(31, 0)

#define GICV5_ITS_SYNC_STATUSR_IDLE		BIT(0)

#define GICV5_DTL1E_VALID			BIT_ULL(0)
/* Note that there is no shift for the address by design */
#define GICV5_DTL1E_L2_ADDR_MASK		GENMASK_ULL(55, 3)
#define GICV5_DTL1E_SPAN			GENMASK_ULL(63, 60)

#define GICV5_DTL2E_VALID			BIT_ULL(0)
#define GICV5_DTL2E_ITT_L2SZ			GENMASK_ULL(2, 1)
/* Note that there is no shift for the address by design */
#define GICV5_DTL2E_ITT_ADDR_MASK		GENMASK_ULL(55, 3)
#define GICV5_DTL2E_ITT_DSWE			BIT_ULL(57)
#define GICV5_DTL2E_ITT_STRUCTURE		BIT_ULL(58)
#define GICV5_DTL2E_EVENT_ID_BITS		GENMASK_ULL(63, 59)

#define GICV5_ITTL1E_VALID			BIT_ULL(0)
/* Note that there is no shift for the address by design */
#define GICV5_ITTL1E_L2_ADDR_MASK		GENMASK_ULL(55, 3)
#define GICV5_ITTL1E_SPAN			GENMASK_ULL(63, 60)

#define GICV5_ITTL2E_LPI_ID			GENMASK_ULL(23, 0)
#define GICV5_ITTL2E_DAC			GENMASK_ULL(29, 28)
#define GICV5_ITTL2E_VIRTUAL			BIT_ULL(30)
#define GICV5_ITTL2E_VALID			BIT_ULL(31)
#define GICV5_ITTL2E_VM_ID			GENMASK_ULL(47, 32)

#define GICV5_ITS_DT_ITT_CFGR_L2SZ_4k		0b00
#define GICV5_ITS_DT_ITT_CFGR_L2SZ_16k		0b01
#define GICV5_ITS_DT_ITT_CFGR_L2SZ_64k		0b10

#define GICV5_ITS_DT_ITT_CFGR_STRUCTURE_LINEAR		0
#define GICV5_ITS_DT_ITT_CFGR_STRUCTURE_TWO_LEVEL	1

#define GICV5_ITS_HWIRQ_DEVICE_ID		GENMASK_ULL(31, 0)
#define GICV5_ITS_HWIRQ_EVENT_ID		GENMASK_ULL(63, 32)

/*
 * IWB registers
 */
#define GICV5_IWB_IDR0				0x0000
#define GICV5_IWB_CR0				0x0080
#define GICV5_IWB_WENABLE_STATUSR		0x00c0
#define GICV5_IWB_WENABLER			0x2000
#define GICV5_IWB_WTMR				0x4000

#define GICV5_IWB_IDR0_INT_DOMS			GENMASK(14, 11)
#define GICV5_IWB_IDR0_IW_RANGE			GENMASK(10, 0)

#define GICV5_IWB_CR0_IDLE			BIT(1)
#define GICV5_IWB_CR0_IWBEN			BIT(0)

#define GICV5_IWB_WENABLE_STATUSR_IDLE		BIT(0)

/*
 * Global Data structures and functions
 */
struct gicv5_chip_data {
	struct fwnode_handle	*fwnode;
	struct irq_domain	*ppi_domain;
	struct irq_domain	*spi_domain;
	struct irq_domain	*lpi_domain;
	struct irq_domain	*ipi_domain;
	u32			global_spi_count;
	u8			cpuif_pri_bits;
	u8			cpuif_id_bits;
	u8			irs_pri_bits;
	struct {
		__le64 *l1ist_addr;
		u32 l2_size;
		u8 l2_bits;
		bool l2;
	} ist;
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

static inline int gicv5_wait_for_op_s(void __iomem *addr, u32 offset,
				      const char *reg_s, u32 mask)
{
	void __iomem *reg = addr + offset;
	u32 val;
	int ret;

	ret = readl_poll_timeout(reg, val, val & mask, 1, 10 * USEC_PER_MSEC);
	if (unlikely(ret == -ETIMEDOUT)) {
		pr_err_ratelimited("%s timeout...\n", reg_s);
		return ret;
	}

	return 0;
}

#define gicv5_wait_for_op_atomic(base, reg, mask, val) \
	gicv5_wait_for_op_s_atomic(base, reg, #reg, mask, val)

#define gicv5_wait_for_op(base, reg, mask) \
	gicv5_wait_for_op_s(base, reg, #reg, mask)

void __init gicv5_init_lpi_domain(void);
void __init gicv5_free_lpi_domain(void);

int gicv5_irs_of_probe(struct device_node *parent);
void gicv5_irs_remove(void);
int gicv5_irs_enable(void);
void gicv5_irs_its_probe(void);
int gicv5_irs_register_cpu(int cpuid);
int gicv5_irs_cpu_to_iaffid(int cpu_id, u16 *iaffid);
struct gicv5_irs_chip_data *gicv5_irs_lookup_by_spi_id(u32 spi_id);
int gicv5_spi_irq_set_type(struct irq_data *d, unsigned int type);
int gicv5_irs_iste_alloc(u32 lpi);
void gicv5_irs_syncr(void);

struct gicv5_its_devtab_cfg {
	union {
		struct {
			__le64	*devtab;
		} linear;
		struct {
			__le64	*l1devtab;
			__le64	**l2ptrs;
		} l2;
	};
	u32	cfgr;
};

struct gicv5_its_itt_cfg {
	union {
		struct {
			__le64		*itt;
			unsigned int	num_ents;
		} linear;
		struct {
			__le64		*l1itt;
			__le64		**l2ptrs;
			unsigned int	num_l1_ents;
			u8		l2sz;
		} l2;
	};
	u8	event_id_bits;
	bool	l2itt;
};

void gicv5_init_lpis(u32 max);
void gicv5_deinit_lpis(void);

int gicv5_alloc_lpi(void);
void gicv5_free_lpi(u32 lpi);

void __init gicv5_its_of_probe(struct device_node *parent);
#endif
