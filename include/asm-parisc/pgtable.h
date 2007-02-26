#ifndef _PARISC_PGTABLE_H
#define _PARISC_PGTABLE_H

#include <asm-generic/4level-fixup.h>

#include <asm/fixmap.h>

#ifndef __ASSEMBLY__
/*
 * we simulate an x86-style page table for the linux mm code
 */

#include <linux/mm.h>		/* for vm_area_struct */
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/bitops.h>

/*
 * kern_addr_valid(ADDR) tests if ADDR is pointing to valid kernel
 * memory.  For the return value to be meaningful, ADDR must be >=
 * PAGE_OFFSET.  This operation can be relatively expensive (e.g.,
 * require a hash-, or multi-level tree-lookup or something of that
 * sort) but it guarantees to return TRUE only if accessing the page
 * at that address does not cause an error.  Note that there may be
 * addresses for which kern_addr_valid() returns FALSE even though an
 * access would not cause an error (e.g., this is typically true for
 * memory mapped I/O regions.
 *
 * XXX Need to implement this for parisc.
 */
#define kern_addr_valid(addr)	(1)

/* Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval)                                 \
        do{                                                     \
                *(pteptr) = (pteval);                           \
        } while(0)
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

#endif /* !__ASSEMBLY__ */

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, (unsigned long)pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, (unsigned long)pgd_val(e))

 /* Note: If you change ISTACK_SIZE, you need to change the corresponding
  * values in vmlinux.lds and vmlinux64.lds (init_istack section). Also,
  * the "order" and size need to agree.
  */

#define  ISTACK_SIZE  32768 /* Interrupt Stack Size */
#define  ISTACK_ORDER 3

/* This is the size of the initially mapped kernel memory */
#ifdef CONFIG_64BIT
#define KERNEL_INITIAL_ORDER	24	/* 0 to 1<<24 = 16MB */
#else
#define KERNEL_INITIAL_ORDER	23	/* 0 to 1<<23 = 8MB */
#endif
#define KERNEL_INITIAL_SIZE	(1 << KERNEL_INITIAL_ORDER)

#if defined(CONFIG_64BIT) && defined(CONFIG_PARISC_PAGE_SIZE_4KB)
#define PT_NLEVELS	3
#define PGD_ORDER	1 /* Number of pages per pgd */
#define PMD_ORDER	1 /* Number of pages per pmd */
#define PGD_ALLOC_ORDER	2 /* first pgd contains pmd */
#else
#define PT_NLEVELS	2
#define PGD_ORDER	1 /* Number of pages per pgd */
#define PGD_ALLOC_ORDER	PGD_ORDER
#endif

/* Definitions for 3rd level (we use PLD here for Page Lower directory
 * because PTE_SHIFT is used lower down to mean shift that has to be
 * done to get usable bits out of the PTE) */
#define PLD_SHIFT	PAGE_SHIFT
#define PLD_SIZE	PAGE_SIZE
#define BITS_PER_PTE	(PAGE_SHIFT - BITS_PER_PTE_ENTRY)
#define PTRS_PER_PTE    (1UL << BITS_PER_PTE)

/* Definitions for 2nd level */
#define pgtable_cache_init()	do { } while (0)

#define PMD_SHIFT       (PLD_SHIFT + BITS_PER_PTE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#if PT_NLEVELS == 3
#define BITS_PER_PMD	(PAGE_SHIFT + PMD_ORDER - BITS_PER_PMD_ENTRY)
#else
#define BITS_PER_PMD	0
#endif
#define PTRS_PER_PMD    (1UL << BITS_PER_PMD)

/* Definitions for 1st level */
#define PGDIR_SHIFT	(PMD_SHIFT + BITS_PER_PMD)
#define BITS_PER_PGD	(PAGE_SHIFT + PGD_ORDER - BITS_PER_PGD_ENTRY)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD    (1UL << BITS_PER_PGD)
#define USER_PTRS_PER_PGD       PTRS_PER_PGD

#define MAX_ADDRBITS	(PGDIR_SHIFT + BITS_PER_PGD)
#define MAX_ADDRESS	(1UL << MAX_ADDRBITS)

#define SPACEID_SHIFT	(MAX_ADDRBITS - 32)

/* This calculates the number of initial pages we need for the initial
 * page tables */
#if (KERNEL_INITIAL_ORDER) >= (PMD_SHIFT)
# define PT_INITIAL	(1 << (KERNEL_INITIAL_ORDER - PMD_SHIFT))
#else
# define PT_INITIAL	(1)  /* all initial PTEs fit into one page */
#endif

/*
 * pgd entries used up by user/kernel:
 */

#define FIRST_USER_ADDRESS	0

#ifndef __ASSEMBLY__
extern  void *vmalloc_start;
#define PCXL_DMA_MAP_SIZE   (8*1024*1024)
#define VMALLOC_START   ((unsigned long)vmalloc_start)
/* this is a fixmap remnant, see fixmap.h */
#define VMALLOC_END	(KERNEL_MAP_END)
#endif

/* NB: The tlb miss handlers make certain assumptions about the order */
/*     of the following bits, so be careful (One example, bits 25-31  */
/*     are moved together in one instruction).                        */

#define _PAGE_READ_BIT     31   /* (0x001) read access allowed */
#define _PAGE_WRITE_BIT    30   /* (0x002) write access allowed */
#define _PAGE_EXEC_BIT     29   /* (0x004) execute access allowed */
#define _PAGE_GATEWAY_BIT  28   /* (0x008) privilege promotion allowed */
#define _PAGE_DMB_BIT      27   /* (0x010) Data Memory Break enable (B bit) */
#define _PAGE_DIRTY_BIT    26   /* (0x020) Page Dirty (D bit) */
#define _PAGE_FILE_BIT	_PAGE_DIRTY_BIT	/* overload this bit */
#define _PAGE_REFTRAP_BIT  25   /* (0x040) Page Ref. Trap enable (T bit) */
#define _PAGE_NO_CACHE_BIT 24   /* (0x080) Uncached Page (U bit) */
#define _PAGE_ACCESSED_BIT 23   /* (0x100) Software: Page Accessed */
#define _PAGE_PRESENT_BIT  22   /* (0x200) Software: translation valid */
#define _PAGE_FLUSH_BIT    21   /* (0x400) Software: translation valid */
				/*             for cache flushing only */
#define _PAGE_USER_BIT     20   /* (0x800) Software: User accessible page */

/* N.B. The bits are defined in terms of a 32 bit word above, so the */
/*      following macro is ok for both 32 and 64 bit.                */

#define xlate_pabit(x) (31 - x)

/* this defines the shift to the usable bits in the PTE it is set so
 * that the valid bits _PAGE_PRESENT_BIT and _PAGE_USER_BIT are set
 * to zero */
#define PTE_SHIFT	   	xlate_pabit(_PAGE_USER_BIT)

/* PFN_PTE_SHIFT defines the shift of a PTE value to access the PFN field */
#define PFN_PTE_SHIFT		12


/* this is how many bits may be used by the file functions */
#define PTE_FILE_MAX_BITS	(BITS_PER_LONG - PTE_SHIFT)

#define pte_to_pgoff(pte) (pte_val(pte) >> PTE_SHIFT)
#define pgoff_to_pte(off) ((pte_t) { ((off) << PTE_SHIFT) | _PAGE_FILE })

#define _PAGE_READ     (1 << xlate_pabit(_PAGE_READ_BIT))
#define _PAGE_WRITE    (1 << xlate_pabit(_PAGE_WRITE_BIT))
#define _PAGE_RW       (_PAGE_READ | _PAGE_WRITE)
#define _PAGE_EXEC     (1 << xlate_pabit(_PAGE_EXEC_BIT))
#define _PAGE_GATEWAY  (1 << xlate_pabit(_PAGE_GATEWAY_BIT))
#define _PAGE_DMB      (1 << xlate_pabit(_PAGE_DMB_BIT))
#define _PAGE_DIRTY    (1 << xlate_pabit(_PAGE_DIRTY_BIT))
#define _PAGE_REFTRAP  (1 << xlate_pabit(_PAGE_REFTRAP_BIT))
#define _PAGE_NO_CACHE (1 << xlate_pabit(_PAGE_NO_CACHE_BIT))
#define _PAGE_ACCESSED (1 << xlate_pabit(_PAGE_ACCESSED_BIT))
#define _PAGE_PRESENT  (1 << xlate_pabit(_PAGE_PRESENT_BIT))
#define _PAGE_FLUSH    (1 << xlate_pabit(_PAGE_FLUSH_BIT))
#define _PAGE_USER     (1 << xlate_pabit(_PAGE_USER_BIT))
#define _PAGE_FILE     (1 << xlate_pabit(_PAGE_FILE_BIT))

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |  _PAGE_DIRTY | _PAGE_ACCESSED)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_KERNEL	(_PAGE_PRESENT | _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE | _PAGE_DIRTY | _PAGE_ACCESSED)

/* The pgd/pmd contains a ptr (in phys addr space); since all pgds/pmds
 * are page-aligned, we don't care about the PAGE_OFFSET bits, except
 * for a few meta-information bits, so we shift the address to be
 * able to effectively address 40/42/44-bits of physical address space
 * depending on 4k/16k/64k PAGE_SIZE */
#define _PxD_PRESENT_BIT   31
#define _PxD_ATTACHED_BIT  30
#define _PxD_VALID_BIT     29

#define PxD_FLAG_PRESENT  (1 << xlate_pabit(_PxD_PRESENT_BIT))
#define PxD_FLAG_ATTACHED (1 << xlate_pabit(_PxD_ATTACHED_BIT))
#define PxD_FLAG_VALID    (1 << xlate_pabit(_PxD_VALID_BIT))
#define PxD_FLAG_MASK     (0xf)
#define PxD_FLAG_SHIFT    (4)
#define PxD_VALUE_SHIFT   (8) /* (PAGE_SHIFT-PxD_FLAG_SHIFT) */

#ifndef __ASSEMBLY__

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_ACCESSED)
/* Others seem to make this executable, I don't know if that's correct
   or not.  The stack is mapped this way though so this is necessary
   in the short term - dhd@linuxcare.com, 2000-08-08 */
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_ACCESSED)
#define PAGE_WRITEONLY  __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_WRITE | _PAGE_ACCESSED)
#define PAGE_EXECREAD   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_EXEC |_PAGE_ACCESSED)
#define PAGE_COPY       PAGE_EXECREAD
#define PAGE_RWX        __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC |_PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_KERNEL)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_KERNEL & ~_PAGE_WRITE)
#define PAGE_KERNEL_UNC	__pgprot(_PAGE_KERNEL | _PAGE_NO_CACHE)
#define PAGE_GATEWAY    __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_GATEWAY| _PAGE_READ)
#define PAGE_FLUSH      __pgprot(_PAGE_FLUSH)


/*
 * We could have an execute only page using "gateway - promote to priv
 * level 3", but that is kind of silly. So, the way things are defined
 * now, we must always have read permission for pages with execute
 * permission. For the fun of it we'll go ahead and support write only
 * pages.
 */

	 /*xwr*/
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  __P000 /* copy on write */
#define __P011  __P001 /* copy on write */
#define __P100  PAGE_EXECREAD
#define __P101  PAGE_EXECREAD
#define __P110  __P100 /* copy on write */
#define __P111  __P101 /* copy on write */

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_WRITEONLY
#define __S011  PAGE_SHARED
#define __S100  PAGE_EXECREAD
#define __S101  PAGE_EXECREAD
#define __S110  PAGE_RWX
#define __S111  PAGE_RWX


extern pgd_t swapper_pg_dir[]; /* declared in init_task.c */

/* initial page tables for 0-8MB for kernel */

extern pte_t pg0[];

/* zero page used for uninitialized stuff */

extern unsigned long *empty_zero_page;

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */

#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

#define pte_none(x)     ((pte_val(x) == 0) || (pte_val(x) & _PAGE_FLUSH))
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm,addr,xp)	do { pte_val(*(xp)) = 0; } while (0)

#define pmd_flag(x)	(pmd_val(x) & PxD_FLAG_MASK)
#define pmd_address(x)	((unsigned long)(pmd_val(x) &~ PxD_FLAG_MASK) << PxD_VALUE_SHIFT)
#define pgd_flag(x)	(pgd_val(x) & PxD_FLAG_MASK)
#define pgd_address(x)	((unsigned long)(pgd_val(x) &~ PxD_FLAG_MASK) << PxD_VALUE_SHIFT)

#if PT_NLEVELS == 3
/* The first entry of the permanent pmd is not there if it contains
 * the gateway marker */
#define pmd_none(x)	(!pmd_val(x) || pmd_flag(x) == PxD_FLAG_ATTACHED)
#else
#define pmd_none(x)	(!pmd_val(x))
#endif
#define pmd_bad(x)	(!(pmd_flag(x) & PxD_FLAG_VALID))
#define pmd_present(x)	(pmd_flag(x) & PxD_FLAG_PRESENT)
static inline void pmd_clear(pmd_t *pmd) {
#if PT_NLEVELS == 3
	if (pmd_flag(*pmd) & PxD_FLAG_ATTACHED)
		/* This is the entry pointing to the permanent pmd
		 * attached to the pgd; cannot clear it */
		__pmd_val_set(*pmd, PxD_FLAG_ATTACHED);
	else
#endif
		__pmd_val_set(*pmd,  0);
}



#if PT_NLEVELS == 3
#define pgd_page_vaddr(pgd) ((unsigned long) __va(pgd_address(pgd)))
#define pgd_page(pgd)	virt_to_page((void *)pgd_page_vaddr(pgd))

/* For 64 bit we have three level tables */

#define pgd_none(x)     (!pgd_val(x))
#define pgd_bad(x)      (!(pgd_flag(x) & PxD_FLAG_VALID))
#define pgd_present(x)  (pgd_flag(x) & PxD_FLAG_PRESENT)
static inline void pgd_clear(pgd_t *pgd) {
#if PT_NLEVELS == 3
	if(pgd_flag(*pgd) & PxD_FLAG_ATTACHED)
		/* This is the permanent pmd attached to the pgd; cannot
		 * free it */
		return;
#endif
	__pgd_val_set(*pgd, 0);
}
#else
/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
extern inline void pgd_clear(pgd_t * pgdp)	{ }
#endif

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_READ; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_WRITE; }
extern inline int pte_file(pte_t pte)		{ return pte_val(pte) & _PAGE_FILE; }
extern inline int pte_user(pte_t pte) 		{ return pte_val(pte) & _PAGE_USER; }

extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_READ; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_WRITE; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_READ; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_WRITE; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define __mk_pte(addr,pgprot) \
({									\
	pte_t __pte;							\
									\
	pte_val(__pte) = ((((addr)>>PAGE_SHIFT)<<PFN_PTE_SHIFT) + pgprot_val(pgprot));	\
									\
	__pte;								\
})

#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pfn_pte(unsigned long pfn, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = (pfn << PFN_PTE_SHIFT) | pgprot_val(pgprot);
	return pte;
}

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

/* Permanent address of a page.  On parisc we don't have highmem. */

#define pte_pfn(x)		(pte_val(x) >> PFN_PTE_SHIFT)

#define pte_page(pte)		(pfn_to_page(pte_pfn(pte)))

#define pmd_page_vaddr(pmd)	((unsigned long) __va(pmd_address(pmd)))

#define __pmd_page(pmd) ((unsigned long) __va(pmd_address(pmd)))
#define pmd_page(pmd)	virt_to_page((void *)__pmd_page(pmd))

#define pgd_index(address) ((address) >> PGDIR_SHIFT)

/* to find an entry in a page-table-directory */
#define pgd_offset(mm, address) \
((mm)->pgd + ((address) >> PGDIR_SHIFT))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */

#if PT_NLEVELS == 3
#define pmd_offset(dir,address) \
((pmd_t *) pgd_page_vaddr(*(dir)) + (((address)>>PMD_SHIFT) & (PTRS_PER_PMD-1)))
#else
#define pmd_offset(dir,addr) ((pmd_t *) dir)
#endif

/* Find an entry in the third-level page table.. */ 
#define pte_index(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE-1))
#define pte_offset_kernel(pmd, address) \
	((pte_t *) pmd_page_vaddr(*(pmd)) + pte_index(address))
#define pte_offset_map(pmd, address) pte_offset_kernel(pmd, address)
#define pte_offset_map_nested(pmd, address) pte_offset_kernel(pmd, address)
#define pte_unmap(pte) do { } while (0)
#define pte_unmap_nested(pte) do { } while (0)

#define pte_unmap(pte)			do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

extern void paging_init (void);

/* Used for deferring calls to flush_dcache_page() */

#define PG_dcache_dirty         PG_arch_1

extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

/* Encode and de-code a swap entry */

#define __swp_type(x)                     ((x).val & 0x1f)
#define __swp_offset(x)                   ( (((x).val >> 6) &  0x7) | \
					  (((x).val >> 8) & ~0x7) )
#define __swp_entry(type, offset)         ((swp_entry_t) { (type) | \
					    ((offset &  0x7) << 6) | \
					    ((offset & ~0x7) << 8) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

static inline int ptep_test_and_clear_young(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_SMP
	if (!pte_young(*ptep))
		return 0;
	return test_and_clear_bit(xlate_pabit(_PAGE_ACCESSED_BIT), &pte_val(*ptep));
#else
	pte_t pte = *ptep;
	if (!pte_young(pte))
		return 0;
	set_pte_at(vma->vm_mm, addr, ptep, pte_mkold(pte));
	return 1;
#endif
}

static inline int ptep_test_and_clear_dirty(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_SMP
	if (!pte_dirty(*ptep))
		return 0;
	return test_and_clear_bit(xlate_pabit(_PAGE_DIRTY_BIT), &pte_val(*ptep));
#else
	pte_t pte = *ptep;
	if (!pte_dirty(pte))
		return 0;
	set_pte_at(vma->vm_mm, addr, ptep, pte_mkclean(pte));
	return 1;
#endif
}

extern spinlock_t pa_dbit_lock;

struct mm_struct;
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t old_pte;
	pte_t pte;

	spin_lock(&pa_dbit_lock);
	pte = old_pte = *ptep;
	pte_val(pte) &= ~_PAGE_PRESENT;
	pte_val(pte) |= _PAGE_FLUSH;
	set_pte_at(mm,addr,ptep,pte);
	spin_unlock(&pa_dbit_lock);

	return old_pte;
}

static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_SMP
	unsigned long new, old;

	do {
		old = pte_val(*ptep);
		new = pte_val(pte_wrprotect(__pte (old)));
	} while (cmpxchg((unsigned long *) ptep, old, new) != old);
#else
	pte_t old_pte = *ptep;
	set_pte_at(mm, addr, ptep, pte_wrprotect(old_pte));
#endif
}

#define pte_same(A,B)	(pte_val(A) == pte_val(B))

#endif /* !__ASSEMBLY__ */


/* TLB page size encoding - see table 3-1 in parisc20.pdf */
#define _PAGE_SIZE_ENCODING_4K		0
#define _PAGE_SIZE_ENCODING_16K		1
#define _PAGE_SIZE_ENCODING_64K		2
#define _PAGE_SIZE_ENCODING_256K	3
#define _PAGE_SIZE_ENCODING_1M		4
#define _PAGE_SIZE_ENCODING_4M		5
#define _PAGE_SIZE_ENCODING_16M		6
#define _PAGE_SIZE_ENCODING_64M		7

#if defined(CONFIG_PARISC_PAGE_SIZE_4KB)
# define _PAGE_SIZE_ENCODING_DEFAULT _PAGE_SIZE_ENCODING_4K
#elif defined(CONFIG_PARISC_PAGE_SIZE_16KB)
# define _PAGE_SIZE_ENCODING_DEFAULT _PAGE_SIZE_ENCODING_16K
#elif defined(CONFIG_PARISC_PAGE_SIZE_64KB)
# define _PAGE_SIZE_ENCODING_DEFAULT _PAGE_SIZE_ENCODING_64K
#endif


#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)

#define pgprot_noncached(prot) __pgprot(pgprot_val(prot) | _PAGE_NO_CACHE)

#define MK_IOSPACE_PFN(space, pfn)	(pfn)
#define GET_IOSPACE(pfn)		0
#define GET_PFN(pfn)			(pfn)

/* We provide our own get_unmapped_area to provide cache coherency */

#define HAVE_ARCH_UNMAPPED_AREA

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTE_SAME
#include <asm-generic/pgtable.h>

#endif /* _PARISC_PGTABLE_H */
