/*
 *  linux/include/asm-i386/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMi386_IDE_H
#define __ASMi386_IDE_H

#ifdef __KERNEL__


#ifndef MAX_HWIFS
# ifdef CONFIG_BLK_DEV_IDEPCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	6
# endif
#endif

#define IDE_ARCH_OBSOLETE_DEFAULTS

static __inline__ int ide_default_irq(unsigned long base)
{
	switch (base) {
		case 0x1f0: return 14;
		case 0x170: return 15;
		case 0x1e8: return 11;
		case 0x168: return 10;
		case 0x1e0: return 8;
		case 0x160: return 12;
		default:
			return 0;
	}
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	/*
	 *	If PCI is present then it is not safe to poke around
	 *	the other legacy IDE ports. Only 0x1f0 and 0x170 are
	 *	defined compatibility mode ports for PCI. A user can 
	 *	override this using ide= but we must default safe.
	 */
	if (no_pci_devices()) {
		switch(index) {
			case 2: return 0x1e8;
			case 3: return 0x168;
			case 4: return 0x1e0;
			case 5: return 0x160;
		}
	}
	switch (index) {
		case 0:	return 0x1f0;
		case 1:	return 0x170;
		default:
			return 0;
	}
}

#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#ifdef CONFIG_BLK_DEV_IDEPCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASMi386_IDE_H */
