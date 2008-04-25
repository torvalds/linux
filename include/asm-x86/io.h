#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#define ARCH_HAS_IOREMAP_WC

#ifdef CONFIG_X86_32
# include "io_32.h"
#else
# include "io_64.h"
#endif

extern void *xlate_dev_mem_ptr(unsigned long phys);
extern void unxlate_dev_mem_ptr(unsigned long phys, void *addr);

extern int ioremap_change_attr(unsigned long vaddr, unsigned long size,
				unsigned long prot_val);
extern void __iomem *ioremap_wc(unsigned long offset, unsigned long size);

#endif /* _ASM_X86_IO_H */
