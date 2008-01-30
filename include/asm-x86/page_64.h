#ifndef _X86_64_PAGE_H
#define _X86_64_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

extern unsigned long end_pfn;
extern unsigned long end_pfn_map;


extern unsigned long phys_base;

#endif /* !__ASSEMBLY__ */

#ifndef __ASSEMBLY__

#include <asm/bug.h>

#endif /* __ASSEMBLY__ */

#define __HAVE_ARCH_GATE_AREA 1	
#define vmemmap ((struct page *)VMEMMAP_START)

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* __KERNEL__ */

#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)          ((pfn) < end_pfn)
#endif


#endif /* _X86_64_PAGE_H */
