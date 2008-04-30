/*
 *  Copyright (C) 1994-1996 Linus Torvalds & authors
 *
 *  This file contains the powerpc architecture specific IDE code.
 */
#ifndef _ASM_POWERPC_IDE_H
#define _ASM_POWERPC_IDE_H

#ifdef __KERNEL__

#ifndef __powerpc64__
#include <linux/sched.h>
#include <asm/mpc8xx.h>
#endif
#include <asm/io.h>

#ifndef MAX_HWIFS
#ifdef __powerpc64__
#define MAX_HWIFS	10
#else
#define MAX_HWIFS	8
#endif
#endif

#define __ide_mm_insw(p, a, c)	readsw((void __iomem *)(p), (a), (c))
#define __ide_mm_insl(p, a, c)	readsl((void __iomem *)(p), (a), (c))
#define __ide_mm_outsw(p, a, c)	writesw((void __iomem *)(p), (a), (c))
#define __ide_mm_outsl(p, a, c)	writesl((void __iomem *)(p), (a), (c))

#ifndef  __powerpc64__
#include <linux/hdreg.h>
#include <linux/ioport.h>

/* FIXME: use ide_platform host driver */
static __inline__ int ide_default_irq(unsigned long base)
{
#ifdef CONFIG_PPLUS
	switch (base) {
	case 0x1f0:	return 14;
	case 0x170:	return 15;
	}
#endif
#ifdef CONFIG_PPC_PREP
	switch (base) {
	case 0x1f0:	return 13;
	case 0x170:	return 13;
	case 0x1e8:	return 11;
	case 0x168:	return 10;
	case 0xfff0:	return 14;	/* MCP(N)750 ide0 */
	case 0xffe0:	return 15;	/* MCP(N)750 ide1 */
	}
#endif
	return 0;
}

/* FIXME: use ide_platform host driver */
static __inline__ unsigned long ide_default_io_base(int index)
{
#ifdef CONFIG_PPLUS
	switch (index) {
	case 0:		return 0x1f0;
	case 1:		return 0x170;
	}
#endif
#ifdef CONFIG_PPC_PREP
	switch (index) {
	case 0:		return 0x1f0;
	case 1:		return 0x170;
	case 2:		return 0x1e8;
	case 3:		return 0x168;
	}
#endif
	return 0;
}

#ifdef CONFIG_BLK_DEV_MPC8xx_IDE
#define IDE_ARCH_ACK_INTR  1
#define ide_ack_intr(hwif) ((hwif)->ack_intr ? (hwif)->ack_intr(hwif) : 1)
#endif

#endif /* __powerpc64__ */

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_IDE_H */
