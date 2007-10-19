#ifndef _ASM_M32R_IDE_H
#define _ASM_M32R_IDE_H

/*
 *  linux/include/asm-m32r/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifdef __KERNEL__

#include <asm/m32r.h>

#ifndef MAX_HWIFS
# ifdef CONFIG_BLK_DEV_IDEPCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	2
# endif
#endif

#define IDE_ARCH_OBSOLETE_DEFAULTS

static __inline__ int ide_default_irq(unsigned long base)
{
	switch (base) {
#if defined(CONFIG_PLAT_M32700UT) || defined(CONFIG_PLAT_MAPPI2) \
	|| defined(CONFIG_PLAT_OPSPUT)
		case 0x1f0: return PLD_IRQ_CFIREQ;
		default:
			return 0;
#elif defined(CONFIG_PLAT_MAPPI3)
		case 0x1f0: return PLD_IRQ_CFIREQ;
		case 0x170: return PLD_IRQ_IDEIREQ;
		default:
			return 0;
#else
		case 0x1f0: return 14;
		case 0x170: return 15;
		case 0x1e8: return 11;
		case 0x168: return 10;
		case 0x1e0: return 8;
		case 0x160: return 12;
		default:
			return 0;
#endif
	}
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	switch (index) {
		case 0:	return 0x1f0;
		case 1:	return 0x170;
		case 2: return 0x1e8;
		case 3: return 0x168;
		case 4: return 0x1e0;
		case 5: return 0x160;
		default:
			return 0;
	}
}

#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#ifdef CONFIG_BLK_DEV_IDEPCI
#define ide_init_default_irq(base)     (0)
#else
#define ide_init_default_irq(base)     ide_default_irq(base)
#endif

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_IDE_H */
