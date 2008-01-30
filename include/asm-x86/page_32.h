#ifndef _I386_PAGE_H
#define _I386_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/string.h>

#ifdef CONFIG_X86_USE_3DNOW

#include <asm/mmx.h>

static inline void clear_page(void *page)
{
	mmx_clear_page(page);
}

static inline void copy_page(void *to, void *from)
{
	mmx_copy_page(to, from);
}

#else

/*
 *	On older X86 processors it's not a win to use MMX here it seems.
 *	Maybe the K6-III ?
 */
 
static inline void clear_page(void *page)
{
	memset(page, 0, PAGE_SIZE);
}

static inline void copy_page(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}

#endif

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
extern int nx_enabled;

#ifdef CONFIG_X86_PAE
typedef struct { unsigned long pte_low, pte_high; } pte_t;
typedef struct { unsigned long long pmd; } pmd_t;
typedef struct { unsigned long long pgd; } pgd_t;
typedef struct { unsigned long long pgprot; } pgprot_t;

static inline unsigned long long native_pgd_val(pgd_t pgd)
{
	return pgd.pgd;
}

static inline unsigned long long native_pmd_val(pmd_t pmd)
{
	return pmd.pmd;
}

static inline unsigned long long native_pte_val(pte_t pte)
{
	return pte.pte_low | ((unsigned long long)pte.pte_high << 32);
}

static inline pgd_t native_make_pgd(unsigned long long val)
{
	return (pgd_t) { val };
}

static inline pmd_t native_make_pmd(unsigned long long val)
{
	return (pmd_t) { val };
}

static inline pte_t native_make_pte(unsigned long long val)
{
	return (pte_t) { .pte_low = val, .pte_high = (val >> 32) } ;
}

#ifndef CONFIG_PARAVIRT
#define pmd_val(x)	native_pmd_val(x)
#define __pmd(x)	native_make_pmd(x)
#endif

#include <asm-generic/pgtable-nopud.h>
#else  /* !CONFIG_X86_PAE */
typedef struct { unsigned long pte_low; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
#define boot_pte_t pte_t /* or would you rather have a typedef */

static inline unsigned long native_pgd_val(pgd_t pgd)
{
	return pgd.pgd;
}

static inline unsigned long native_pte_val(pte_t pte)
{
	return pte.pte_low;
}

static inline pgd_t native_make_pgd(unsigned long val)
{
	return (pgd_t) { val };
}

static inline pte_t native_make_pte(unsigned long val)
{
	return (pte_t) { .pte_low = val };
}

#include <asm-generic/pgtable-nopmd.h>
#endif	/* CONFIG_X86_PAE */

#define PTE_MASK	PAGE_MASK

#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) } )

#ifndef CONFIG_PARAVIRT
#define pgd_val(x)	native_pgd_val(x)
#define __pgd(x)	native_make_pgd(x)
#define pte_val(x)	native_pte_val(x)
#define __pte(x)	native_make_pte(x)
#endif

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
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
/* __pa_symbol should be used for C visible symbols.
   This seems to be the official gcc blessed way to do such arithmetic. */
#define __pa_symbol(x)          __pa(RELOC_HIDE((unsigned long)(x),0))
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define pfn_to_kaddr(pfn)      __va((pfn) << PAGE_SHIFT)
#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)		((pfn) < max_mapnr)
#endif /* CONFIG_FLATMEM */
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#define __HAVE_ARCH_GATE_AREA 1
#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */
