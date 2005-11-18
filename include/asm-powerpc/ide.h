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

#ifndef MAX_HWIFS
#ifdef __powerpc64__
#define MAX_HWIFS	10
#else
#define MAX_HWIFS	8
#endif
#endif

#ifndef  __powerpc64__
#include <linux/config.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <asm/io.h>

extern void __ide_mm_insw(void __iomem *port, void *addr, u32 count);
extern void __ide_mm_outsw(void __iomem *port, void *addr, u32 count);
extern void __ide_mm_insl(void __iomem *port, void *addr, u32 count);
extern void __ide_mm_outsl(void __iomem *port, void *addr, u32 count);

struct ide_machdep_calls {
        int         (*default_irq)(unsigned long base);
        unsigned long (*default_io_base)(int index);
        void        (*ide_init_hwif)(hw_regs_t *hw,
                                     unsigned long data_port,
                                     unsigned long ctrl_port,
                                     int *irq);
};

extern struct ide_machdep_calls ppc_ide_md;

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0

#define IDE_ARCH_OBSOLETE_DEFAULTS

static __inline__ int ide_default_irq(unsigned long base)
{
	if (ppc_ide_md.default_irq)
		return ppc_ide_md.default_irq(base);
	return 0;
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	if (ppc_ide_md.default_io_base)
		return ppc_ide_md.default_io_base(index);
	return 0;
}

#ifdef CONFIG_PCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

#if (defined CONFIG_APUS || defined CONFIG_BLK_DEV_MPC8xx_IDE )
#define IDE_ARCH_ACK_INTR  1
#define ide_ack_intr(hwif) (hwif->hw.ack_intr ? hwif->hw.ack_intr(hwif) : 1)
#endif

#endif /* __powerpc64__ */

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_IDE_H */
