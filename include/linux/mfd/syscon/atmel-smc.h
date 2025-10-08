/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Atmel SMC (Static Memory Controller) register offsets and bit definitions.
 *
 * Copyright (C) 2014 Atmel
 * Copyright (C) 2014 Free Electrons
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#ifndef _LINUX_MFD_SYSCON_ATMEL_SMC_H_
#define _LINUX_MFD_SYSCON_ATMEL_SMC_H_

#include <linux/bits.h>
#include <linux/types.h>

struct device_node;
struct regmap;

#define ATMEL_SMC_SETUP(cs)			(((cs) * 0x10))
#define ATMEL_HSMC_SETUP(layout, cs)		\
	((layout)->timing_regs_offset + ((cs) * 0x14))
#define ATMEL_SMC_PULSE(cs)			(((cs) * 0x10) + 0x4)
#define ATMEL_HSMC_PULSE(layout, cs)		\
	((layout)->timing_regs_offset + ((cs) * 0x14) + 0x4)
#define ATMEL_SMC_CYCLE(cs)			(((cs) * 0x10) + 0x8)
#define ATMEL_HSMC_CYCLE(layout, cs)			\
	((layout)->timing_regs_offset + ((cs) * 0x14) + 0x8)
#define ATMEL_SMC_NWE_SHIFT			0
#define ATMEL_SMC_NCS_WR_SHIFT			8
#define ATMEL_SMC_NRD_SHIFT			16
#define ATMEL_SMC_NCS_RD_SHIFT			24

#define ATMEL_SMC_MODE(cs)			(((cs) * 0x10) + 0xc)
#define ATMEL_HSMC_MODE(layout, cs)			\
	((layout)->timing_regs_offset + ((cs) * 0x14) + 0x10)
#define ATMEL_SMC_MODE_READMODE_MASK		BIT(0)
#define ATMEL_SMC_MODE_READMODE_NCS		(0 << 0)
#define ATMEL_SMC_MODE_READMODE_NRD		(1 << 0)
#define ATMEL_SMC_MODE_WRITEMODE_MASK		BIT(1)
#define ATMEL_SMC_MODE_WRITEMODE_NCS		(0 << 1)
#define ATMEL_SMC_MODE_WRITEMODE_NWE		(1 << 1)
#define ATMEL_SMC_MODE_EXNWMODE_MASK		GENMASK(5, 4)
#define ATMEL_SMC_MODE_EXNWMODE_DISABLE		(0 << 4)
#define ATMEL_SMC_MODE_EXNWMODE_FROZEN		(2 << 4)
#define ATMEL_SMC_MODE_EXNWMODE_READY		(3 << 4)
#define ATMEL_SMC_MODE_BAT_MASK			BIT(8)
#define ATMEL_SMC_MODE_BAT_SELECT		(0 << 8)
#define ATMEL_SMC_MODE_BAT_WRITE		(1 << 8)
#define ATMEL_SMC_MODE_DBW_MASK			GENMASK(13, 12)
#define ATMEL_SMC_MODE_DBW_8			(0 << 12)
#define ATMEL_SMC_MODE_DBW_16			(1 << 12)
#define ATMEL_SMC_MODE_DBW_32			(2 << 12)
#define ATMEL_SMC_MODE_TDF_MASK			GENMASK(19, 16)
#define ATMEL_SMC_MODE_TDF(x)			(((x) - 1) << 16)
#define ATMEL_SMC_MODE_TDF_MAX			16
#define ATMEL_SMC_MODE_TDF_MIN			1
#define ATMEL_SMC_MODE_TDFMODE_OPTIMIZED	BIT(20)
#define ATMEL_SMC_MODE_PMEN			BIT(24)
#define ATMEL_SMC_MODE_PS_MASK			GENMASK(29, 28)
#define ATMEL_SMC_MODE_PS_4			(0 << 28)
#define ATMEL_SMC_MODE_PS_8			(1 << 28)
#define ATMEL_SMC_MODE_PS_16			(2 << 28)
#define ATMEL_SMC_MODE_PS_32			(3 << 28)

#define ATMEL_HSMC_TIMINGS(layout, cs)			\
	((layout)->timing_regs_offset + ((cs) * 0x14) + 0xc)
#define ATMEL_HSMC_TIMINGS_OCMS			BIT(12)
#define ATMEL_HSMC_TIMINGS_RBNSEL(x)		((x) << 28)
#define ATMEL_HSMC_TIMINGS_NFSEL		BIT(31)
#define ATMEL_HSMC_TIMINGS_TCLR_SHIFT		0
#define ATMEL_HSMC_TIMINGS_TADL_SHIFT		4
#define ATMEL_HSMC_TIMINGS_TAR_SHIFT		8
#define ATMEL_HSMC_TIMINGS_TRR_SHIFT		16
#define ATMEL_HSMC_TIMINGS_TWB_SHIFT		24

struct atmel_hsmc_reg_layout {
	unsigned int timing_regs_offset;
};

/**
 * struct atmel_smc_cs_conf - SMC CS config as described in the datasheet.
 * @setup: NCS/NWE/NRD setup timings (not applicable to at91rm9200)
 * @pulse: NCS/NWE/NRD pulse timings (not applicable to at91rm9200)
 * @cycle: NWE/NRD cycle timings (not applicable to at91rm9200)
 * @timings: advanced NAND related timings (only applicable to HSMC)
 * @mode: all kind of config parameters (see the fields definition above).
 *	  The mode fields are different on at91rm9200
 */
struct atmel_smc_cs_conf {
	u32 setup;
	u32 pulse;
	u32 cycle;
	u32 timings;
	u32 mode;
};

void atmel_smc_cs_conf_init(struct atmel_smc_cs_conf *conf);
int atmel_smc_cs_conf_set_timing(struct atmel_smc_cs_conf *conf,
				 unsigned int shift,
				 unsigned int ncycles);
int atmel_smc_cs_conf_set_setup(struct atmel_smc_cs_conf *conf,
				unsigned int shift, unsigned int ncycles);
int atmel_smc_cs_conf_set_pulse(struct atmel_smc_cs_conf *conf,
				unsigned int shift, unsigned int ncycles);
int atmel_smc_cs_conf_set_cycle(struct atmel_smc_cs_conf *conf,
				unsigned int shift, unsigned int ncycles);
void atmel_smc_cs_conf_apply(struct regmap *regmap, int cs,
			     const struct atmel_smc_cs_conf *conf);
void atmel_hsmc_cs_conf_apply(struct regmap *regmap,
			      const struct atmel_hsmc_reg_layout *reglayout,
			      int cs, const struct atmel_smc_cs_conf *conf);
void atmel_smc_cs_conf_get(struct regmap *regmap, int cs,
			   struct atmel_smc_cs_conf *conf);
void atmel_hsmc_cs_conf_get(struct regmap *regmap,
			    const struct atmel_hsmc_reg_layout *reglayout,
			    int cs, struct atmel_smc_cs_conf *conf);
const struct atmel_hsmc_reg_layout *
atmel_hsmc_get_reg_layout(struct device_node *np);

#endif /* _LINUX_MFD_SYSCON_ATMEL_SMC_H_ */
