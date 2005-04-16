/*
 *  linux/include/asm-m68k/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/* Copyright(c) 1996 Kars de Jong */
/* Based on the ide driver from 1.2.13pl8 */

/*
 * Credits (alphabetical):
 *
 *  - Bjoern Brauel
 *  - Kars de Jong
 *  - Torsten Ebeling
 *  - Dwight Engen
 *  - Thorsten Floeck
 *  - Roman Hodek
 *  - Guenther Kelleter
 *  - Chris Lawrence
 *  - Michael Rausch
 *  - Christian Sauer
 *  - Michael Schmitz
 *  - Jes Soerensen
 *  - Michael Thurm
 *  - Geert Uytterhoeven
 */

#ifndef _M68K_IDE_H
#define _M68K_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_ATARI
#include <linux/interrupt.h>
#include <asm/atari_stdma.h>
#endif

#ifdef CONFIG_MAC
#include <asm/macints.h>
#endif

#ifndef MAX_HWIFS
#define MAX_HWIFS	4	/* same as the other archs */
#endif

/*
 * Get rid of defs from io.h - ide has its private and conflicting versions
 * Since so far no single m68k platform uses ISA/PCI I/O space for IDE, we
 * always use the `raw' MMIO versions
 */
#undef inb
#undef inw
#undef insw
#undef inl
#undef insl
#undef outb
#undef outw
#undef outsw
#undef outl
#undef outsl
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel

#define inb				in_8
#define inw				in_be16
#define insw(port, addr, n)		raw_insw((u16 *)port, addr, n)
#define inl				in_be32
#define insl(port, addr, n)		raw_insl((u32 *)port, addr, n)
#define outb(val, port)			out_8(port, val)
#define outw(val, port)			out_be16(port, val)
#define outsw(port, addr, n)		raw_outsw((u16 *)port, addr, n)
#define outl(val, port)			out_be32(port, val)
#define outsl(port, addr, n)		raw_outsl((u32 *)port, addr, n)
#define readb				in_8
#define readw				in_be16
#define __ide_mm_insw(port, addr, n)	raw_insw((u16 *)port, addr, n)
#define readl				in_be32
#define __ide_mm_insl(port, addr, n)	raw_insl((u32 *)port, addr, n)
#define writeb(val, port)		out_8(port, val)
#define writew(val, port)		out_be16(port, val)
#define __ide_mm_outsw(port, addr, n)	raw_outsw((u16 *)port, addr, n)
#define writel(val, port)		out_be32(port, val)
#define __ide_mm_outsl(port, addr, n)	raw_outsl((u32 *)port, addr, n)
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
#define insw_swapw(port, addr, n)	raw_insw_swapw((u16 *)port, addr, n)
#define outsw_swapw(port, addr, n)	raw_outsw_swapw((u16 *)port, addr, n)
#endif


/* Q40 and Atari have byteswapped IDE busses and since many interesting
 * values in the identification string are text, chars and words they
 * happened to be almost correct without swapping.. However *_capacity
 * is needed for drives over 8 GB. RZ */
#if defined(CONFIG_Q40) || defined(CONFIG_ATARI)
#define M68K_IDE_SWAPW  (MACH_IS_Q40 || MACH_IS_ATARI)
#endif

#ifdef CONFIG_BLK_DEV_FALCON_IDE
#define IDE_ARCH_LOCK

extern int falconide_intr_lock;

static __inline__ void ide_release_lock (void)
{
	if (MACH_IS_ATARI) {
		if (falconide_intr_lock == 0) {
			printk("ide_release_lock: bug\n");
			return;
		}
		falconide_intr_lock = 0;
		stdma_release();
	}
}

static __inline__ void
ide_get_lock(irqreturn_t (*handler)(int, void *, struct pt_regs *), void *data)
{
	if (MACH_IS_ATARI) {
		if (falconide_intr_lock == 0) {
			if (in_interrupt() > 0)
				panic( "Falcon IDE hasn't ST-DMA lock in interrupt" );
			stdma_lock(handler, data);
			falconide_intr_lock = 1;
		}
	}
}
#endif /* CONFIG_BLK_DEV_FALCON_IDE */

#define IDE_ARCH_ACK_INTR
#define ide_ack_intr(hwif)	((hwif)->hw.ack_intr ? (hwif)->hw.ack_intr(hwif) : 1)

#endif /* __KERNEL__ */
#endif /* _M68K_IDE_H */
