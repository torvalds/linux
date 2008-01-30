#ifndef _X86_64_PAGE_H
#define _X86_64_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

extern unsigned long end_pfn;
extern unsigned long end_pfn_map;

void clear_page(void *page);
void copy_page(void *to, void *from);

struct page;

static void inline clear_user_page(void *page, unsigned long vaddr,
				struct page *pg)
{
	clear_page(page);
}

static void inline copy_user_page(void *to, void *from, unsigned long vaddr,
				struct page *topage)
{
	copy_page(to, from);
}

#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pud; } pud_t;
typedef struct { unsigned long pgd; } pgd_t;
#define PTE_MASK	PHYSICAL_PAGE_MASK

typedef struct { unsigned long pgprot; } pgprot_t;

extern unsigned long phys_base;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pud_val(x)	((x).pud)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x) ((pte_t) { (x) } )
#define __pmd(x) ((pmd_t) { (x) } )
#define __pud(x) ((pud_t) { (x) } )
#define __pgd(x) ((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

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
