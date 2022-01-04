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
 * For Linux support, upstream LiteX enforces a 32-bit wide CSR bus, which
 * means that only larger-than-32-bit CSRs will be split across multiple
 * subregisters (e.g., a 64-bit CSR will be spread across two consecutive
 * 32-bit subregisters).
 *
 * For details see: https://github.com/enjoy-digital/litex/wiki/CSR-Bus
 */

static inline void litex_write8(void __iomem *reg, u8 val)
{
	_write_litex_subregister(val, reg);
}

static inline void litex_write16(void __iomem *reg, u16 val)
{
	_write_litex_subregister(val, reg);
}

static inline void litex_write32(void __iomem *reg, u32 val)
{
	_write_litex_subregister(val, reg);
}

static inline void litex_write64(void __iomem *reg, u64 val)
{
	_write_litex_subregister(val >> 32, reg);
	_write_litex_subregister(val, reg + 4);
}

static inline u8 litex_read8(void __iomem *reg)
{
	return _read_litex_subregister(reg);
}

static inline u16 litex_read16(void __iomem *reg)
{
	return _read_litex_subregister(reg);
}

static inline u32 litex_read32(void __iomem *reg)
{
	return _read_litex_subregister(reg);
}

static inline u64 litex_read64(void __iomem *reg)
{
	return ((u64)_read_litex_subregister(reg) << 32) |
		_read_litex_subregister(reg + 4);
}

#endif /* _LINUX_LITEX_H */
