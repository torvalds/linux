#ifndef _I386_PAGE_H
#define _I386_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

/*
 * These are used to make use of C type-checking..
 */
extern int nx_enabled;

#endif /* !__ASSEMBLY__ */

#ifndef __ASSEMBLY__

struct vm_area_struct;

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;

extern int sysctl_legacy_va_layout;

extern int page_is_ram(unsigned long pagenr);

#endif /* __ASSEMBLY__ */

#define VMALLOC_RESERVE		((unsigned long)__VMALLOC_RESERVE)
#define MAXMEM			(-__PAGE_OFFSET-__VMALLOC_RESERVE)


#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#define __HAVE_ARCH_GATE_AREA 1
#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */
