/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common LiteX header providing
 * helper functions for accessing CSRs.
 *
 * Implementation of the functions is provided by
 * the LiteX SoC Controller driver.
 *
 * Copyright (C) 2019-2020 Antmicro <www.antmicro.com>
 */

#ifndef _LINUX_LITEX_H
#define _LINUX_LITEX_H

#include <linux/io.h>
#include <linux/types.h>
#include <linux/compiler_types.h>

/*
 * The parameters below are true for LiteX SoCs configured for 8-bit CSR Bus,
 * 32-bit aligned.
 *
 * Supporting other configurations will require extending the logic in this
 * header and in the LiteX SoC controller driver.
 */
#define LITEX_REG_SIZE	  0x4
#define LITEX_SUBREG_SIZE	0x1
#define LITEX_SUBREG_SIZE_BIT	 (LITEX_SUBREG_SIZE * 8)

#define WRITE_LITEX_SUBREGISTER(val, base_offset, subreg_id) \
	writel((u32 __force)cpu_to_le32(val), base_offset + (LITEX_REG_SIZE * subreg_id))

#define READ_LITEX_SUBREGISTER(base_offset, subreg_id) \
	le32_to_cpu((__le32 __force)readl(base_offset + (LITEX_REG_SIZE * subreg_id)))

void litex_set_reg(void __iomem *reg, unsigned long reg_sz, unsigned long val);

unsigned long litex_get_reg(void __iomem *reg, unsigned long reg_sz);

static inline void litex_write8(void __iomem *reg, u8 val)
{
	WRITE_LITEX_SUBREGISTER(val, reg, 0);
}

static inline void litex_write16(void __iomem *reg, u16 val)
{
	WRITE_LITEX_SUBREGISTER(val >> 8, reg, 0);
	WRITE_LITEX_SUBREGISTER(val, reg, 1);
}

static inline void litex_write32(void __iomem *reg, u32 val)
{
	WRITE_LITEX_SUBREGISTER(val >> 24, reg, 0);
	WRITE_LITEX_SUBREGISTER(val >> 16, reg, 1);
	WRITE_LITEX_SUBREGISTER(val >> 8, reg, 2);
	WRITE_LITEX_SUBREGISTER(val, reg, 3);
}

static inline void litex_write64(void __iomem *reg, u64 val)
{
	WRITE_LITEX_SUBREGISTER(val >> 56, reg, 0);
	WRITE_LITEX_SUBREGISTER(val >> 48, reg, 1);
	WRITE_LITEX_SUBREGISTER(val >> 40, reg, 2);
	WRITE_LITEX_SUBREGISTER(val >> 32, reg, 3);
	WRITE_LITEX_SUBREGISTER(val >> 24, reg, 4);
	WRITE_LITEX_SUBREGISTER(val >> 16, reg, 5);
	WRITE_LITEX_SUBREGISTER(val >> 8, reg, 6);
	WRITE_LITEX_SUBREGISTER(val, reg, 7);
}

static inline u8 litex_read8(void __iomem *reg)
{
	return READ_LITEX_SUBREGISTER(reg, 0);
}

static inline u16 litex_read16(void __iomem *reg)
{
	return (READ_LITEX_SUBREGISTER(reg, 0) << 8)
		| (READ_LITEX_SUBREGISTER(reg, 1));
}

static inline u32 litex_read32(void __iomem *reg)
{
	return (READ_LITEX_SUBREGISTER(reg, 0) << 24)
		| (READ_LITEX_SUBREGISTER(reg, 1) << 16)
		| (READ_LITEX_SUBREGISTER(reg, 2) << 8)
		| (READ_LITEX_SUBREGISTER(reg, 3));
}

static inline u64 litex_read64(void __iomem *reg)
{
	return ((u64)READ_LITEX_SUBREGISTER(reg, 0) << 56)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 1) << 48)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 2) << 40)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 3) << 32)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 4) << 24)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 5) << 16)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 6) << 8)
		| ((u64)READ_LITEX_SUBREGISTER(reg, 7));
}

#endif /* _LINUX_LITEX_H */
