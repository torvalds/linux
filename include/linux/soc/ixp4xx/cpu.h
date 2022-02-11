/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IXP4XX cpu type detection
 *
 * Copyright (C) 2007 MontaVista Software, Inc.
 */

#ifndef __SOC_IXP4XX_CPU_H__
#define __SOC_IXP4XX_CPU_H__

#include <linux/io.h>
#include <linux/regmap.h>
#ifdef CONFIG_ARM
#include <asm/cputype.h>
#endif

/* Processor id value in CP15 Register 0 */
#define IXP42X_PROCESSOR_ID_VALUE	0x690541c0 /* including unused 0x690541Ex */
#define IXP42X_PROCESSOR_ID_MASK	0xffffffc0

#define IXP43X_PROCESSOR_ID_VALUE	0x69054040
#define IXP43X_PROCESSOR_ID_MASK	0xfffffff0

#define IXP46X_PROCESSOR_ID_VALUE	0x69054200 /* including IXP455 */
#define IXP46X_PROCESSOR_ID_MASK	0xfffffff0

/* Feature register in the expansion bus controller */
#define IXP4XX_EXP_CNFG2		0x2c

/* "fuse" bits of IXP_EXP_CFG2 */
/* All IXP4xx CPUs */
#define IXP4XX_FEATURE_RCOMP		(1 << 0)
#define IXP4XX_FEATURE_USB_DEVICE	(1 << 1)
#define IXP4XX_FEATURE_HASH		(1 << 2)
#define IXP4XX_FEATURE_AES		(1 << 3)
#define IXP4XX_FEATURE_DES		(1 << 4)
#define IXP4XX_FEATURE_HDLC		(1 << 5)
#define IXP4XX_FEATURE_AAL		(1 << 6)
#define IXP4XX_FEATURE_HSS		(1 << 7)
#define IXP4XX_FEATURE_UTOPIA		(1 << 8)
#define IXP4XX_FEATURE_NPEB_ETH0	(1 << 9)
#define IXP4XX_FEATURE_NPEC_ETH		(1 << 10)
#define IXP4XX_FEATURE_RESET_NPEA	(1 << 11)
#define IXP4XX_FEATURE_RESET_NPEB	(1 << 12)
#define IXP4XX_FEATURE_RESET_NPEC	(1 << 13)
#define IXP4XX_FEATURE_PCI		(1 << 14)
#define IXP4XX_FEATURE_UTOPIA_PHY_LIMIT	(3 << 16)
#define IXP4XX_FEATURE_XSCALE_MAX_FREQ	(3 << 22)
#define IXP42X_FEATURE_MASK		(IXP4XX_FEATURE_RCOMP            | \
					 IXP4XX_FEATURE_USB_DEVICE       | \
					 IXP4XX_FEATURE_HASH             | \
					 IXP4XX_FEATURE_AES              | \
					 IXP4XX_FEATURE_DES              | \
					 IXP4XX_FEATURE_HDLC             | \
					 IXP4XX_FEATURE_AAL              | \
					 IXP4XX_FEATURE_HSS              | \
					 IXP4XX_FEATURE_UTOPIA           | \
					 IXP4XX_FEATURE_NPEB_ETH0        | \
					 IXP4XX_FEATURE_NPEC_ETH         | \
					 IXP4XX_FEATURE_RESET_NPEA       | \
					 IXP4XX_FEATURE_RESET_NPEB       | \
					 IXP4XX_FEATURE_RESET_NPEC       | \
					 IXP4XX_FEATURE_PCI              | \
					 IXP4XX_FEATURE_UTOPIA_PHY_LIMIT | \
					 IXP4XX_FEATURE_XSCALE_MAX_FREQ)


/* IXP43x/46x CPUs */
#define IXP4XX_FEATURE_ECC_TIMESYNC	(1 << 15)
#define IXP4XX_FEATURE_USB_HOST		(1 << 18)
#define IXP4XX_FEATURE_NPEA_ETH		(1 << 19)
#define IXP43X_FEATURE_MASK		(IXP42X_FEATURE_MASK             | \
					 IXP4XX_FEATURE_ECC_TIMESYNC     | \
					 IXP4XX_FEATURE_USB_HOST         | \
					 IXP4XX_FEATURE_NPEA_ETH)

/* IXP46x CPU (including IXP455) only */
#define IXP4XX_FEATURE_NPEB_ETH_1_TO_3	(1 << 20)
#define IXP4XX_FEATURE_RSA		(1 << 21)
#define IXP46X_FEATURE_MASK		(IXP43X_FEATURE_MASK             | \
					 IXP4XX_FEATURE_NPEB_ETH_1_TO_3  | \
					 IXP4XX_FEATURE_RSA)

#ifdef CONFIG_ARCH_IXP4XX
#define cpu_is_ixp42x_rev_a0() ((read_cpuid_id() & (IXP42X_PROCESSOR_ID_MASK | 0xF)) == \
				IXP42X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp42x()	((read_cpuid_id() & IXP42X_PROCESSOR_ID_MASK) == \
			 IXP42X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp43x()	((read_cpuid_id() & IXP43X_PROCESSOR_ID_MASK) == \
			 IXP43X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp46x()	((read_cpuid_id() & IXP46X_PROCESSOR_ID_MASK) == \
			 IXP46X_PROCESSOR_ID_VALUE)

u32 ixp4xx_read_feature_bits(void);
void ixp4xx_write_feature_bits(u32 value);
static inline u32 cpu_ixp4xx_features(struct regmap *rmap)
{
	u32 val;

	regmap_read(rmap, IXP4XX_EXP_CNFG2, &val);
	/* For some reason this register is inverted */
	val = ~val;
	if (cpu_is_ixp42x_rev_a0())
		return IXP42X_FEATURE_MASK & ~(IXP4XX_FEATURE_RCOMP |
					       IXP4XX_FEATURE_AES);
	if (cpu_is_ixp42x())
		return val & IXP42X_FEATURE_MASK;
	if (cpu_is_ixp43x())
		return val & IXP43X_FEATURE_MASK;
	return val & IXP46X_FEATURE_MASK;
}
#else
#define cpu_is_ixp42x_rev_a0()		0
#define cpu_is_ixp42x()			0
#define cpu_is_ixp43x()			0
#define cpu_is_ixp46x()			0
static inline u32 ixp4xx_read_feature_bits(void)
{
	return 0;
}
static inline void ixp4xx_write_feature_bits(u32 value)
{
}
static inline u32 cpu_ixp4xx_features(struct regmap *rmap)
{
	return 0;
}
#endif

#endif  /* _ASM_ARCH_CPU_H */
