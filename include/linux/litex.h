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

/* LiteX SoCs support 8- or 32-bit CSR Bus data width (i.e., subreg. size) */
#if defined(CONFIG_LITEX_SUBREG_SIZE) && \
	(CONFIG_LITEX_SUBREG_SIZE == 1 || CONFIG_LITEX_SUBREG_SIZE == 4)
#define LITEX_SUBREG_SIZE      CONFIG_LITEX_SUBREG_SIZE
#else
#error LiteX subregister size (LITEX_SUBREG_SIZE) must be 4 or 1!
#endif
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

/*
 * LiteX SoC Generator, depending on the configuration, can split a single
 * logical CSR (Control&Status Register) into a series of consecutive physical
 * registers.
 *
 * For example, in the configuration with 8-bit CSR Bus, a 32-bit aligned,
 * 32-bit wide logical CSR will be laid out as four 32-bit physical
 * subregisters, each one containing one byte of meaningful data.
 *
 * For details see: https://github.com/enjoy-digital/litex/wiki/CSR-Bus
 */

/* number of LiteX subregisters needed to store a register of given reg_size */
#define _litex_num_subregs(reg_size) \
	(((reg_size) - 1) / LITEX_SUBREG_SIZE + 1)

/*
 * since the number of 4-byte aligned subregisters required to store a single
 * LiteX CSR (MMIO) register varies with LITEX_SUBREG_SIZE, the offset of the
 * next adjacent LiteX CSR register w.r.t. the offset of the current one also
 * depends on how many subregisters the latter is spread across
 */
#define _next_reg_off(off, size) \
	((off) + _litex_num_subregs(size) * LITEX_SUBREG_ALIGN)

/*
 * The purpose of `_litex_[set|get]_reg()` is to implement the logic of
 * writing to/reading from the LiteX CSR in a single place that can be then
 * reused by all LiteX drivers via the `litex_[write|read][8|16|32|64]()`
 * accessors for the appropriate data width.
 * NOTE: direct use of `_litex_[set|get]_reg()` by LiteX drivers is strongly
 * discouraged, as they perform no error checking on the requested data width!
 */

/**
 * _litex_set_reg() - Writes a value to the LiteX CSR (Control&Status Register)
 * @reg: Address of the CSR
 * @reg_size: The width of the CSR expressed in the number of bytes
 * @val: Value to be written to the CSR
 *
 * This function splits a single (possibly multi-byte) LiteX CSR write into
 * a series of subregister writes with a proper offset.
 * NOTE: caller is responsible for ensuring (0 < reg_size <= sizeof(u64)).
 */
static inline void _litex_set_reg(void __iomem *reg, size_t reg_size, u64 val)
{
	u8 shift = _litex_num_subregs(reg_size) * LITEX_SUBREG_SIZE_BIT;

	while (shift > 0) {
		shift -= LITEX_SUBREG_SIZE_BIT;
		_write_litex_subregister(val >> shift, reg);
		reg += LITEX_SUBREG_ALIGN;
	}
}

/**
 * _litex_get_reg() - Reads a value of the LiteX CSR (Control&Status Register)
 * @reg: Address of the CSR
 * @reg_size: The width of the CSR expressed in the number of bytes
 *
 * Return: Value read from the CSR
 *
 * This function generates a series of subregister reads with a proper offset
 * and joins their results into a single (possibly multi-byte) LiteX CSR value.
 * NOTE: caller is responsible for ensuring (0 < reg_size <= sizeof(u64)).
 */
static inline u64 _litex_get_reg(void __iomem *reg, size_t reg_size)
{
	u64 r;
	u8 i;

	r = _read_litex_subregister(reg);
	for (i = 1; i < _litex_num_subregs(reg_size); i++) {
		r <<= LITEX_SUBREG_SIZE_BIT;
		reg += LITEX_SUBREG_ALIGN;
		r |= _read_litex_subregister(reg);
	}
	return r;
}

static inline void litex_write8(void __iomem *reg, u8 val)
{
	_litex_set_reg(reg, sizeof(u8), val);
}

static inline void litex_write16(void __iomem *reg, u16 val)
{
	_litex_set_reg(reg, sizeof(u16), val);
}

static inline void litex_write32(void __iomem *reg, u32 val)
{
	_litex_set_reg(reg, sizeof(u32), val);
}

static inline void litex_write64(void __iomem *reg, u64 val)
{
	_litex_set_reg(reg, sizeof(u64), val);
}

static inline u8 litex_read8(void __iomem *reg)
{
	return _litex_get_reg(reg, sizeof(u8));
}

static inline u16 litex_read16(void __iomem *reg)
{
	return _litex_get_reg(reg, sizeof(u16));
}

static inline u32 litex_read32(void __iomem *reg)
{
	return _litex_get_reg(reg, sizeof(u32));
}

static inline u64 litex_read64(void __iomem *reg)
{
	return _litex_get_reg(reg, sizeof(u64));
}

#endif /* _LINUX_LITEX_H */
