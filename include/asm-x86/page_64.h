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

extern unsigned long __phys_addr(unsigned long);

#endif /* __ASSEMBLY__ */

#define __pa(x)		__phys_addr((unsigned long)(x))
#define __pa_symbol(x)	__phys_addr((unsigned long)(x))

#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define __boot_va(x)		__va(x)
#define __boot_pa(x)		__pa(x)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_kaddr(pfn)      __va((pfn) << PAGE_SHIFT)

#define __HAVE_ARCH_GATE_AREA 1	
#define vmemmap ((struct page *)VMEMMAP_START)

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* __KERNEL__ */

#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)          ((pfn) < end_pfn)
#endif


#endif /* _X86_64_PAGE_H */
