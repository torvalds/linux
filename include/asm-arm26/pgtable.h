/*
 *  linux/include/asm-arm26/pgtable.h
 *
 *  Copyright (C) 2000-2002 Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <asm-generic/4level-fixup.h>

#include <asm/memory.h>

/*
 * The table below defines the page protection levels that we insert into our
 * Linux page table version.  These get translated into the best that the
 * architecture can perform.  Note that on most ARM hardware:
 *  1) We cannot do execute protection
 *  2) If we could do execute protection, then read is implied
 *  3) write implies read permissions
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_READONLY
#define __S101  PAGE_READONLY
#define __S110  PAGE_SHARED
#define __S111  PAGE_SHARED

/*
 * PMD_SHIFT determines the size of the area a second-level page table can map
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#define PGD_SHIFT		25
#define PMD_SHIFT		20

#define PGD_SIZE                (1UL << PGD_SHIFT)
#define PGD_MASK                (~(PGD_SIZE-1))
#define PMD_SIZE                (1UL << PMD_SHIFT)
#define PMD_MASK                (~(PMD_SIZE-1))

/* The kernel likes to use these names for the above (ick) */
#define PGDIR_SIZE PGD_SIZE
#define PGDIR_MASK PGD_MASK

#define PTRS_PER_PGD            32
#define PTRS_PER_PMD            1
#define PTRS_PER_PTE            32

/*
 * This is the lowest virtual address we can permit any user space
 * mapping to be mapped at.  This is particularly important for
 * non-high vector CPUs.
 */
#define FIRST_USER_ADDRESS	PAGE_SIZE

#define FIRST_USER_PGD_NR       1
#define USER_PTRS_PER_PGD       ((TASK_SIZE/PGD_SIZE) - FIRST_USER_PGD_NR)

// FIXME - WTF?
#define LIBRARY_TEXT_START	0x0c000000



#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, unsigned long val);
extern void __pmd_error(const char *file, int line, unsigned long val);
extern void __pgd_error(const char *file, int line, unsigned long val);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte_val(pte))
#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd_val(pmd))
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd_val(pgd))

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)	(empty_zero_page)

#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pte_page(pte)           (pfn_to_page(pte_pfn(pte)))
#define pfn_pte(pfn,prot)	(__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot)))
#define pages_to_mb(x)		((x) >> (20 - PAGE_SHIFT))
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page),prot)

/*
 * Terminology: PGD = Page Directory, PMD = Page Middle Directory,
 *              PTE = Page Table Entry
 *
 * on arm26 we have no 2nd level page table. we simulate this by removing the
 * PMD.
 *
 * pgd_none is 0 to prevernt pmd_alloc() calling __pmd_alloc(). This causes it
 * to return pmd_offset(pgd,addr) which is a pointer to the pgd (IOW, a no-op).
 *
 * however, to work this way, whilst we are allocating 32 pgds, containing 32
 * PTEs, the actual work is done on the PMDs, thus:
 *
 * instead of  mm->pgd->pmd->pte
 * we have     mm->pgdpmd->pte
 *
 * IOW, think of PGD operations and PMD ones as being the same thing, just
 * that PGD stuff deals with the mm_struct side of things, wheras PMD stuff
 * deals with the pte side of things.
 *
 * additionally, we store some bits in the PGD and PTE pointers: 
 * PGDs:
 *   o The lowest (1) bit of the PGD is to determine if it is present or swap.
 *   o The 2nd bit of the PGD is unused and must be zero.
 *   o The top 6 bits of the PGD must be zero. 
 * PTEs:
 *   o The lower 5 bits of a pte are flags. bit 1 is the 'present' flag. The
 *     others determine the pages attributes.
 *
 * the pgd_val, pmd_val, and pte_val macros seem to be private to our code.
 * They get the RAW value of the PGD/PMD/PTE entry, including our flags
 * encoded into the pointers.
 * 
 * The pgd_offset, pmd_offset, and pte_offset macros are used by the kernel,
 * so they shouldnt have our flags attached.
 *
 * If you understood that, feel free to explain it to me...
 *
 */

#define _PMD_PRESENT     (0x01)

/* These definitions allow us to optimise out stuff like pmd_alloc() */
#define pgd_none(pgd)		(0) 
#define pgd_bad(pgd)		(0)
#define pgd_present(pgd)	(1)
#define pgd_clear(pgdp)		do { } while (0)

/* Whilst these handle our actual 'page directory' (the agglomeration of pgd and pmd)
 */
#define pmd_none(pmd)           (!pmd_val(pmd))
#define pmd_bad(pmd)            ((pmd_val(pmd) & 0xfc000002))
#define pmd_present(pmd)        (pmd_val(pmd) & _PMD_PRESENT)
#define set_pmd(pmd_ptr, pmd)   ((*(pmd_ptr)) = (pmd))
#define pmd_clear(pmdp)         set_pmd(pmdp, __pmd(0))

/* and these handle our pte tables */
#define pte_none(pte)           (!pte_val(pte))
#define pte_present(pte)        (pte_val(pte) & _PAGE_PRESENT)
#define set_pte(pte_ptr, pte)   ((*(pte_ptr)) = (pte))
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)
#define pte_clear(mm,addr,ptep)	set_pte_at((mm),(addr),(ptep), __pte(0))

/* macros to ease the getting of pointers to stuff... */
#define pgd_offset(mm, addr)	((pgd_t *)(mm)->pgd        + __pgd_index(addr))
#define pmd_offset(pgd, addr)	((pmd_t *)(pgd))
#define pte_offset(pmd, addr)   ((pte_t *)pmd_page(*(pmd)) + __pte_index(addr))

/* there is no __pmd_index as we dont use pmds */
#define __pgd_index(addr)	((addr) >> PGD_SHIFT)
#define __pte_index(addr)	(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))


/* Keep the kernel happy */
#define pgd_index(addr)         __pgd_index(addr)
#define pgd_offset_k(addr)	(pgd_offset(&init_mm, addr))

/*
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;) FIXME: surely 1 page not 4k ?
 */
#define VMALLOC_START     0x01a00000
#define VMALLOC_END       0x01c00000

/* Is pmd_page supposed to return a pointer to a page in some arches? ours seems to
 * return a pointer to memory (no special alignment)
 */
#define pmd_page(pmd)  ((struct page *)(pmd_val((pmd)) & ~_PMD_PRESENT))
#define pmd_page_vaddr(pmd) ((pte_t *)(pmd_val((pmd)) & ~_PMD_PRESENT))

#define pte_offset_kernel(dir,addr)     (pmd_page_vaddr(*(dir)) + __pte_index(addr))

#define pte_offset_map(dir,addr)        (pmd_page_vaddr(*(dir)) + __pte_index(addr))
#define pte_offset_map_nested(dir,addr) (pmd_page_vaddr(*(dir)) + __pte_index(addr))
#define pte_unmap(pte)                  do { } while (0)
#define pte_unmap_nested(pte)           do { } while (0)


#define _PAGE_PRESENT   0x01
#define _PAGE_READONLY  0x02
#define _PAGE_NOT_USER  0x04
#define _PAGE_OLD       0x08
#define _PAGE_CLEAN     0x10

// an old page has never been read.
// a clean page has never been written.

/*                               -- present --   -- !dirty --  --- !write ---   ---- !user --- */
#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY | _PAGE_NOT_USER)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_CLEAN                                  )
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY                 )
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY                 )
#define PAGE_KERNEL     __pgprot(_PAGE_PRESENT                                | _PAGE_NOT_USER)

#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_OLD | _PAGE_CLEAN)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_write(pte)                  (!(pte_val(pte) & _PAGE_READONLY))
#define pte_dirty(pte)                  (!(pte_val(pte) & _PAGE_CLEAN))
#define pte_young(pte)                  (!(pte_val(pte) & _PAGE_OLD))
//ONLY when !pte_present() I think. nicked from arm32 (FIXME!)
#define pte_file(pte)                   (!(pte_val(pte) & _PAGE_OLD))

#define PTE_BIT_FUNC(fn,op)                     \
static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

PTE_BIT_FUNC(wrprotect, |=  _PAGE_READONLY);
PTE_BIT_FUNC(mkwrite,   &= ~_PAGE_READONLY);
PTE_BIT_FUNC(mkclean,   |=  _PAGE_CLEAN);
PTE_BIT_FUNC(mkdirty,   &= ~_PAGE_CLEAN);
PTE_BIT_FUNC(mkold,     |=  _PAGE_OLD);
PTE_BIT_FUNC(mkyoung,   &= ~_PAGE_OLD);

/*
 * We don't store cache state bits in the page table here. FIXME - or do we?
 */
#define pgprot_noncached(prot)  (prot)
#define pgprot_writecombine(prot) (prot) //FIXME - is a no-op?

extern void pgtable_cache_init(void);

//FIXME - nicked from arm32 and brutally hacked. probably wrong.
#define pte_to_pgoff(x) (pte_val(x) >> 2)
#define pgoff_to_pte(x) __pte(((x) << 2) & ~_PAGE_OLD)

//FIXME - next line borrowed from arm32. is it right?
#define PTE_FILE_MAX_BITS       30


static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* Encode and decode a swap entry.
 *
 * We support up to 32GB of swap on 4k machines
 */
#define __swp_type(x)		(((x).val >> 2) & 0x7f)
#define __swp_offset(x)		((x).val >> 9)
#define __swp_entry(type,offset) ((swp_entry_t) { ((type) << 2) | ((offset) << 9) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
/* FIXME: this is not correct */
#define kern_addr_valid(addr)	(1)

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
        pte_t pte;
        pte_val(pte) = physpage | pgprot_val(pgprot);
        return pte;
}


#include <asm-generic/pgtable.h>

/*
 * remap a physical page `pfn' of size `size' with page protection `prot'
 * into virtual address `from'
 */
#define io_remap_pfn_range(vma,from,pfn,size,prot) \
		remap_pfn_range(vma, from, pfn, size, prot)

#endif /* !__ASSEMBLY__ */

#endif /* _ASMARM_PGTABLE_H */
