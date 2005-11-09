/*
 * linux/include/asm-xtensa/page.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_PGTABLE_H
#define _XTENSA_PGTABLE_H

#include <asm-generic/pgtable-nopmd.h>
#include <asm/page.h>

/* Assertions. */

#ifdef CONFIG_MMU


#if (XCHAL_MMU_RINGS < 2)
# error Linux build assumes at least 2 ring levels.
#endif

#if (XCHAL_MMU_CA_BITS != 4)
# error We assume exactly four bits for CA.
#endif

#if (XCHAL_MMU_SR_BITS != 0)
# error We have no room for SR bits.
#endif

/*
 * Use the first min-wired way for mapping page-table pages.
 * Page coloring requires a second min-wired way.
 */

#if (XCHAL_DTLB_MINWIRED_SETS == 0)
# error Need a min-wired way for mapping page-table pages
#endif

#define DTLB_WAY_PGTABLE XCHAL_DTLB_SET(XCHAL_DTLB_MINWIRED_SET0, WAY)

#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
# if XCHAL_DTLB_SET(XCHAL_DTLB_MINWIRED_SET0, WAYS) >= 2
#  define DTLB_WAY_DCACHE_ALIAS0 (DTLB_WAY_PGTABLE + 1)
#  define DTLB_WAY_DCACHE_ALIAS1 (DTLB_WAY_PGTABLE + 2)
# else
#  error Page coloring requires its own wired dtlb way!
# endif
#endif

#endif /* CONFIG_MMU */

/*
 * We only use two ring levels, user and kernel space.
 */

#define USER_RING		1	/* user ring level */
#define KERNEL_RING		0	/* kernel ring level */

/*
 * The Xtensa architecture port of Linux has a two-level page table system,
 * i.e. the logical three-level Linux page table layout are folded.
 * Each task has the following memory page tables:
 *
 *   PGD table (page directory), ie. 3rd-level page table:
 *	One page (4 kB) of 1024 (PTRS_PER_PGD) pointers to PTE tables
 *	(Architectures that don't have the PMD folded point to the PMD tables)
 *
 *	The pointer to the PGD table for a given task can be retrieved from
 *	the task structure (struct task_struct*) t, e.g. current():
 *	  (t->mm ? t->mm : t->active_mm)->pgd
 *
 *   PMD tables (page middle-directory), ie. 2nd-level page tables:
 *	Absent for the Xtensa architecture (folded, PTRS_PER_PMD == 1).
 *
 *   PTE tables (page table entry), ie. 1st-level page tables:
 *	One page (4 kB) of 1024 (PTRS_PER_PTE) PTEs with a special PTE
 *	invalid_pte_table for absent mappings.
 *
 * The individual pages are 4 kB big with special pages for the empty_zero_page.
 */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE		1024
#define PTRS_PER_PTE_SHIFT	10
#define PTRS_PER_PMD		1
#define PTRS_PER_PGD		1024
#define PGD_ORDER		0
#define PMD_ORDER		0
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_ADDRESS      XCHAL_SEG_MAPPABLE_VADDR
#define FIRST_USER_PGD_NR	(FIRST_USER_ADDRESS >> PGDIR_SHIFT)

/* virtual memory area. We keep a distance to other memory regions to be
 * on the safe side. We also use this area for cache aliasing.
 */

// FIXME: virtual memory area must be configuration-dependent

#define VMALLOC_START		0xC0000000
#define VMALLOC_END		0xC7FF0000

/* Xtensa Linux config PTE layout (when present):
 *	31-12:	PPN
 *	11-6:	Software
 *	5-4:	RING
 *	3-0:	CA
 *
 * Similar to the Alpha and MIPS ports, we need to keep track of the ref
 * and mod bits in software.  We have a software "you can read
 * from this page" bit, and a hardware one which actually lets the
 * process read from the page.  On the same token we have a software
 * writable bit and the real hardware one which actually lets the
 * process write to the page.
 *
 * See further below for PTE layout for swapped-out pages.
 */

#define _PAGE_VALID		(1<<0)	/* hardware: page is accessible */
#define _PAGE_WRENABLE		(1<<1)	/* hardware: page is writable */

/* None of these cache modes include MP coherency:  */
#define _PAGE_NO_CACHE		(0<<2)	/* bypass, non-speculative */
#if XCHAL_DCACHE_IS_WRITEBACK
# define _PAGE_WRITEBACK	(1<<2)	/* write back */
# define _PAGE_WRITETHRU	(2<<2)	/* write through */
#else
# define _PAGE_WRITEBACK	(1<<2)	/* assume write through */
# define _PAGE_WRITETHRU	(1<<2)
#endif
#define _PAGE_NOALLOC		(3<<2)	/* don't allocate cache,if not cached */
#define _CACHE_MASK		(3<<2)

#define _PAGE_USER		(1<<4)	/* user access (ring=1) */
#define _PAGE_KERNEL		(0<<4)	/* kernel access (ring=0) */

/* Software */
#define _PAGE_RW		(1<<6)	/* software: page writable */
#define _PAGE_DIRTY		(1<<7)	/* software: page dirty */
#define _PAGE_ACCESSED		(1<<8)	/* software: page accessed (read) */
#define _PAGE_FILE		(1<<9)	/* nonlinear file mapping*/

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _CACHE_MASK | _PAGE_DIRTY)
#define _PAGE_PRESENT	( _PAGE_VALID | _PAGE_WRITEBACK | _PAGE_ACCESSED)

#ifdef CONFIG_MMU

# define PAGE_NONE	__pgprot(_PAGE_PRESENT)
# define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_RW)
# define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER)
# define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER)
# define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_KERNEL | _PAGE_WRENABLE)
# define PAGE_INVALID	__pgprot(_PAGE_USER)

# if (DCACHE_WAY_SIZE > PAGE_SIZE)
#  define PAGE_DIRECTORY  __pgprot(_PAGE_VALID | _PAGE_ACCESSED | _PAGE_KERNEL)
# else
#  define PAGE_DIRECTORY  __pgprot(_PAGE_PRESENT | _PAGE_KERNEL)
# endif

#else /* no mmu */

# define PAGE_NONE       __pgprot(0)
# define PAGE_SHARED     __pgprot(0)
# define PAGE_COPY       __pgprot(0)
# define PAGE_READONLY   __pgprot(0)
# define PAGE_KERNEL     __pgprot(0)

#endif

/*
 * On certain configurations of Xtensa MMUs (eg. the initial Linux config),
 * the MMU can't do page protection for execute, and considers that the same as
 * read.  Also, write permissions may imply read permissions.
 * What follows is the closest we can get by reasonable means..
 * See linux/mm/mmap.c for protection_map[] array that uses these definitions.
 */
#define __P000	PAGE_NONE	/* private --- */
#define __P001	PAGE_READONLY	/* private --r */
#define __P010	PAGE_COPY	/* private -w- */
#define __P011	PAGE_COPY	/* private -wr */
#define __P100	PAGE_READONLY	/* private x-- */
#define __P101	PAGE_READONLY	/* private x-r */
#define __P110	PAGE_COPY	/* private xw- */
#define __P111	PAGE_COPY	/* private xwr */

#define __S000	PAGE_NONE	/* shared  --- */
#define __S001	PAGE_READONLY	/* shared  --r */
#define __S010	PAGE_SHARED	/* shared  -w- */
#define __S011	PAGE_SHARED	/* shared  -wr */
#define __S100	PAGE_READONLY	/* shared  x-- */
#define __S101	PAGE_READONLY	/* shared  x-r */
#define __S110	PAGE_SHARED	/* shared  xw- */
#define __S111	PAGE_SHARED	/* shared  xwr */

#ifndef __ASSEMBLY__

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd entry %08lx.\n", __FILE__, __LINE__, pgd_val(e))

extern unsigned long empty_zero_page[1024];

#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

extern pgd_t swapper_pg_dir[PAGE_SIZE/sizeof(pgd_t)];

/*
 * The pmd contains the kernel virtual address of the pte page.
 */
#define pmd_page_kernel(pmd) ((unsigned long)(pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd) virt_to_page(pmd_val(pmd))

/*
 * The following only work if pte_present() is true.
 */
#define pte_none(pte)	 (!(pte_val(pte) ^ _PAGE_USER))
#define pte_present(pte) (pte_val(pte) & _PAGE_VALID)
#define pte_clear(mm,addr,ptep)						\
	do { update_pte(ptep, __pte(_PAGE_USER)); } while(0)

#define pmd_none(pmd)	 (!pmd_val(pmd))
#define pmd_present(pmd) (pmd_val(pmd) & PAGE_MASK)
#define pmd_clear(pmdp)	 do { set_pmd(pmdp, __pmd(0)); } while (0)
#define pmd_bad(pmd)	 (pmd_val(pmd) & ~PAGE_MASK)

/* Note: We use the _PAGE_USER bit to indicate write-protect kernel memory */

static inline int pte_read(pte_t pte)  { return pte_val(pte) & _PAGE_USER; }
static inline int pte_write(pte_t pte) { return pte_val(pte) & _PAGE_RW; }
static inline int pte_dirty(pte_t pte) { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte) { return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_file(pte_t pte)  { return pte_val(pte) & _PAGE_FILE; }
static inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~(_PAGE_RW | _PAGE_WRENABLE); return pte; }
static inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
static inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
static inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_RW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pte_same(a,b)		(pte_val(a) == pte_val(b))
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define mk_pte(page, prot)	pfn_pte(page_to_pfn(page), prot)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

/*
 * Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void update_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
	__asm__ __volatile__ ("memw; dhwb %0, 0; dsync" :: "a" (ptep));
#endif
}

struct mm_struct;

static inline void
set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pteval)
{
	update_pte(ptep, pteval);
}


static inline void
set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	*pmdp = pmdval;
#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
	__asm__ __volatile__ ("memw; dhwb %0, 0; dsync" :: "a" (pmdp));
#endif
}

struct vm_area_struct;

static inline int
ptep_test_and_clear_young(struct vm_area_struct *vma, unsigned long addr,
    			  pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_young(pte))
		return 0;
	update_pte(ptep, pte_mkold(pte));
	return 1;
}

static inline int
ptep_test_and_clear_dirty(struct vm_area_struct *vma, unsigned long addr,
   			  pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_dirty(pte))
		return 0;
	update_pte(ptep, pte_mkclean(pte));
	return 1;
}

static inline pte_t
ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(mm, addr, ptep);
	return pte;
}

static inline void
ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
  	pte_t pte = *ptep;
  	update_pte(ptep, pte_wrprotect(pte));
}

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
#define pgd_offset(mm,address)	((mm)->pgd + pgd_index(address))

#define pgd_index(address)	((address) >> PGDIR_SHIFT)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir,address) ((pmd_t*)(dir))

/* Find an entry in the third-level page table.. */
#define pte_index(address)	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir,addr) 					\
	((pte_t*) pmd_page_kernel(*(dir)) + pte_index(addr))
#define pte_offset_map(dir,addr)	pte_offset_kernel((dir),(addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir),(addr))

#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)	do { } while (0)


/*
 * Encode and decode a swap entry.
 * Each PTE in a process VM's page table is either:
 *   "present" -- valid and not swapped out, protection bits are meaningful;
 *   "not present" -- which further subdivides in these two cases:
 *      "none" -- no mapping at all; identified by pte_none(), set by pte_clear(
 *      "swapped out" -- the page is swapped out, and the SWP macros below
 *                      are used to store swap file info in the PTE itself.
 *
 * In the Xtensa processor MMU, any PTE entries in user space (or anywhere
 * in virtual memory that can map differently across address spaces)
 * must have a correct ring value that represents the RASID field that
 * is changed when switching address spaces.  Eg. such PTE entries cannot
 * be set to ring zero, because that can cause a (global) kernel ASID
 * entry to be created in the TLBs (even with invalid cache attribute),
 * potentially causing a multihit exception when going back to another
 * address space that mapped the same virtual address at another ring.
 *
 * SO: we avoid using ring bits (_PAGE_RING_MASK) in "not present" PTEs.
 * We also avoid using the _PAGE_VALID bit which must be zero for non-present
 * pages.
 *
 * We end up with the following available bits:  1..3 and 7..31.
 * We don't bother with 1..3 for now (we can use them later if needed),
 * and chose to allocate 6 bits for SWP_TYPE and the remaining 19 bits
 * for SWP_OFFSET.  At least 5 bits are needed for SWP_TYPE, because it
 * is currently implemented as an index into swap_info[MAX_SWAPFILES]
 * and MAX_SWAPFILES is currently defined as 32 in <linux/swap.h>.
 * However, for some reason all other architectures in the 2.4 kernel
 * reserve either 6, 7, or 8 bits so I'll not detract from that for now.  :)
 * SWP_OFFSET is an offset into the swap file in page-size units, so
 * with 4 kB pages, 19 bits supports a maximum swap file size of 2 GB.
 *
 * FIXME:  2 GB isn't very big.  Other bits can be used to allow
 * larger swap sizes.  In the meantime, it appears relatively easy to get
 * around the 2 GB limitation by simply using multiple swap files.
 */

#define __swp_type(entry)	(((entry).val >> 7) & 0x3f)
#define __swp_offset(entry)	((entry).val >> 13)
#define __swp_entry(type,offs)	((swp_entry_t) {((type) << 7) | ((offs) << 13)})
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#define PTE_FILE_MAX_BITS	29
#define pte_to_pgoff(pte)	(pte_val(pte) >> 3)
#define pgoff_to_pte(off)	((pte_t) { ((off) << 3) | _PAGE_FILE })


#endif /*  !defined (__ASSEMBLY__) */


#ifdef __ASSEMBLY__

/* Assembly macro _PGD_INDEX is the same as C pgd_index(unsigned long),
 *                _PGD_OFFSET as C pgd_offset(struct mm_struct*, unsigned long),
 *                _PMD_OFFSET as C pmd_offset(pgd_t*, unsigned long)
 *                _PTE_OFFSET as C pte_offset(pmd_t*, unsigned long)
 *
 * Note: We require an additional temporary register which can be the same as
 *       the register that holds the address.
 *
 * ((pte_t*) ((unsigned long)(pmd_val(*pmd) & PAGE_MASK)) + pte_index(addr))
 *
 */
#define _PGD_INDEX(rt,rs)	extui	rt, rs, PGDIR_SHIFT, 32-PGDIR_SHIFT
#define _PTE_INDEX(rt,rs)	extui	rt, rs, PAGE_SHIFT, PTRS_PER_PTE_SHIFT

#define _PGD_OFFSET(mm,adr,tmp)		l32i	mm, mm, MM_PGD;		\
					_PGD_INDEX(tmp, adr);		\
					addx4	mm, tmp, mm

#define _PTE_OFFSET(pmd,adr,tmp)	_PTE_INDEX(tmp, adr);		\
					srli	pmd, pmd, PAGE_SHIFT;	\
					slli	pmd, pmd, PAGE_SHIFT;	\
					addx4	pmd, tmp, pmd

#else

extern void paging_init(void);

#define kern_addr_valid(addr)	(1)

extern  void update_mmu_cache(struct vm_area_struct * vma,
			      unsigned long address, pte_t pte);

/*
 * remap a physical page `pfn' of size `size' with page protection `prot'
 * into virtual address `from'
 */
#define io_remap_pfn_range(vma,from,pfn,size,prot) \
                remap_pfn_range(vma, from, pfn, size, prot)


/* No page table caches to init */

#define pgtable_cache_init()	do { } while (0)

typedef pte_t *pte_addr_t;

#endif /* !defined (__ASSEMBLY__) */

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTEP_MKDIRTY
#define __HAVE_ARCH_PTE_SAME

#include <asm-generic/pgtable.h>

#endif /* _XTENSA_PGTABLE_H */
