/*
 *  linux/include/asm-ia64/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the ia64 architecture specific IDE code.
 */

#ifndef __ASM_IA64_IDE_H
#define __ASM_IA64_IDE_H

#ifdef __KERNEL__


#include <linux/irq.h>

static inline int ide_default_irq(unsigned long base)
{
	switch (base) {
	      case 0x1f0: return isa_irq_to_vector(14);
	      case 0x170: return isa_irq_to_vector(15);
	      case 0x1e8: return isa_irq_to_vector(11);
	      case 0x168: return isa_irq_to_vector(10);
	      case 0x1e0: return isa_irq_to_vector(8);
	      case 0x160: return isa_irq_to_vector(12);
	      default:
		return 0;
	}
}

static inline unsigned long ide_default_io_base(int index)
{
	switch (index) {
	      case 0: return 0x1f0;
	      case 1: return 0x170;
	      case 2: return 0x1e8;
	      case 3: return 0x168;
	      case 4: return 0x1e0;
	      case 5: return 0x160;
	      default:
		return 0;
	}
}

#include <asm-generic/ide_iops.h>

#endif /* __KERNEL__ */

#endif /* __ASM_IA64_IDE_H */
