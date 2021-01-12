/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common LiteX header providing
 * helper functions for accessing CSRs.
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
#define LITEX_SUBREG_SIZE	0x1
#define LITEX_SUBREG_SIZE_BIT	 (LITEX_SUBREG_SIZE * 8)

/* LiteX subregisters of any width are always aligned on a 4-byte boundary */
#define LITEX_SUBREG_ALIGN	  0x4

static inline void _write_litex_subregister(u32 val, void __iomem *addr)
{
	writel((u32 __force)cpu_to_le32(val), addr);
}

static inline u32 _read_litex_subregister(void __iomem *addr)
{
	return le32_to_cpu((__le32 __force)readl(addr));
}

#define WRITE_LITEX_SUBREGISTER(val, base_offset, subreg_id) \
	_write_litex_subregister(val, (base_offset) + \
					LITEX_SUBREG_ALIGN * (subreg_id))

#define READ_LITEX_SUBREGISTER(base_offset, subreg_id) \
	_read_litex_subregister((base_offset) + \
					LITEX_SUBREG_ALIGN * (subreg_id))

/*
 * LiteX SoC Generator, depending on the configuration, can split a single
 * logical CSR (Control&Status Register) into a series of consecutive physical
 * registers.
 *
 * For example, in the configuration with 8-bit CSR Bus, 32-bit aligned (the
 * default one for 32-bit CPUs) a 32-bit logical CSR will be generated as four
 * 32-bit physical registers, each one containing one byte of meaningful data.
 *
 * For details see: https://github.com/enjoy-digital/litex/wiki/CSR-Bus
 *
 * The purpose of `litex_set_reg`/`litex_get_reg` is to implement the logic
 * of writing to/reading from the LiteX CSR in a single place that can be
 * then reused by all LiteX drivers.
 */

/**
 * litex_set_reg() - Writes the value to the LiteX CSR (Control&Status Register)
 * @reg: Address of the CSR
 * @reg_size: The width of the CSR expressed in the number of bytes
 * @val: Value to be written to the CSR
 *
 * In the currently supported LiteX configuration (8-bit CSR Bus, 32-bit aligned),
 * a 32-bit LiteX CSR is generated as 4 consecutive 32-bit physical registers,
 * each one containing one byte of meaningful data.
 *
 * This function splits a single possibly multi-byte write into a series of
 * single-byte writes with a proper offset.
 */
static inline void litex_set_reg(void __iomem *reg, ulong reg_size, ulong val)
{
	ulong shifted_data, shift, i;

	for (i = 0; i < reg_size; ++i) {
		shift = ((reg_size - i - 1) * LITEX_SUBREG_SIZE_BIT);
		shifted_data = val >> shift;

		WRITE_LITEX_SUBREGISTER(shifted_data, reg, i);
	}
}

/**
 * litex_get_reg() - Reads the value of the LiteX CSR (Control&Status Register)
 * @reg: Address of the CSR
 * @reg_size: The width of the CSR expressed in the number of bytes
 *
 * Return: Value read from the CSR
 *
 * In the currently supported LiteX configuration (8-bit CSR Bus, 32-bit aligned),
 * a 32-bit LiteX CSR is generated as 4 consecutive 32-bit physical registers,
 * each one containing one byte of meaningful data.
 *
 * This function generates a series of single-byte reads with a proper offset
 * and joins their results into a single multi-byte value.
 */
static inline ulong litex_get_reg(void __iomem *reg, ulong reg_size)
{
	ulong shifted_data, shift, i;
	ulong result = 0;

	for (i = 0; i < reg_size; ++i) {
		shifted_data = READ_LITEX_SUBREGISTER(reg, i);

		shift = ((reg_size - i - 1) * LITEX_SUBREG_SIZE_BIT);
		result |= (shifted_data << shift);
	}

	return result;
}


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
