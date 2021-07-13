/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Intel Corporation
 * Author: johannes@sipsolutions.net
 */
#ifndef _LOGIC_IO_H
#define _LOGIC_IO_H
#include <linux/types.h>

/* include this file into asm/io.h */

#ifdef CONFIG_INDIRECT_IOMEM

#ifdef CONFIG_INDIRECT_IOMEM_FALLBACK
/*
 * If you want emulated IO memory to fall back to 'normal' IO memory
 * if a region wasn't registered as emulated, then you need to have
 * all of the real_* functions implemented.
 */
#if !defined(real_ioremap) || !defined(real_iounmap) || \
    !defined(real_raw_readb) || !defined(real_raw_writeb) || \
    !defined(real_raw_readw) || !defined(real_raw_writew) || \
    !defined(real_raw_readl) || !defined(real_raw_writel) || \
    (defined(CONFIG_64BIT) && \
     (!defined(real_raw_readq) || !defined(real_raw_writeq))) || \
    !defined(real_memset_io) || \
    !defined(real_memcpy_fromio) || \
    !defined(real_memcpy_toio)
#error "Must provide fallbacks for real IO memory access"
#endif /* defined ... */
#endif /* CONFIG_INDIRECT_IOMEM_FALLBACK */

#define ioremap ioremap
void __iomem *ioremap(phys_addr_t offset, size_t size);

#define iounmap iounmap
void iounmap(void __iomem *addr);

#define __raw_readb __raw_readb
u8 __raw_readb(const volatile void __iomem *addr);

#define __raw_readw __raw_readw
u16 __raw_readw(const volatile void __iomem *addr);

#define __raw_readl __raw_readl
u32 __raw_readl(const volatile void __iomem *addr);

#ifdef CONFIG_64BIT
#define __raw_readq __raw_readq
u64 __raw_readq(const volatile void __iomem *addr);
#endif /* CONFIG_64BIT */

#define __raw_writeb __raw_writeb
void __raw_writeb(u8 value, volatile void __iomem *addr);

#define __raw_writew __raw_writew
void __raw_writew(u16 value, volatile void __iomem *addr);

#define __raw_writel __raw_writel
void __raw_writel(u32 value, volatile void __iomem *addr);

#ifdef CONFIG_64BIT
#define __raw_writeq __raw_writeq
void __raw_writeq(u64 value, volatile void __iomem *addr);
#endif /* CONFIG_64BIT */

#define memset_io memset_io
void memset_io(volatile void __iomem *addr, int value, size_t size);

#define memcpy_fromio memcpy_fromio
void memcpy_fromio(void *buffer, const volatile void __iomem *addr,
		   size_t size);

#define memcpy_toio memcpy_toio
void memcpy_toio(volatile void __iomem *addr, const void *buffer, size_t size);

#endif /* CONFIG_INDIRECT_IOMEM */
#endif /* _LOGIC_IO_H */
