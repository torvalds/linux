/* $Id: pgtable.h,v 1.156 2002/02/09 19:49:31 davem Exp $
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _SPARC64_PGTABLE_H
#define _SPARC64_PGTABLE_H

/* This file contains the functions and defines necessary to modify and use
 * the SpitFire page tables.
 */

#include <asm-generic/pgtable-nopud.h>

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/types.h>
#include <asm/spitfire.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/const.h>

/* The kernel image occupies 0x4000000 to 0x1000000 (4MB --> 32MB).
 * The page copy blockops can use 0x2000000 to 0x4000000.
 * The TSB is mapped in the 0x4000000 to 0x6000000 range.
 * The PROM resides in an area spanning 0xf0000000 to 0x100000000.
 * The vmalloc area spans 0x100000000 to 0x200000000.
 * Since modules need to be in the lowest 32-bits of the address space,
 * we place them right before the OBP area from 0x10000000 to 0xf0000000.
 * There is a single static kernel PMD which maps from 0x0 to address
 * 0x400000000.
 */
#define	TLBTEMP_BASE		_AC(0x0000000002000000,UL)
#define	TSBMAP_BASE		_AC(0x0000000004000000,UL)
#define MODULES_VADDR		_AC(0x0000000010000000,UL)
#define MODULES_LEN		_AC(0x00000000e0000000,UL)
#define MODULES_END		_AC(0x00000000f0000000,UL)
#define LOW_OBP_ADDRESS		_AC(0x00000000f0000000,UL)
#define HI_OBP_ADDRESS		_AC(0x0000000100000000,UL)
#define VMALLOC_START		_AC(0x0000000100000000,UL)
#define VMALLOC_END		_AC(0x0000000200000000,UL)

/* XXX All of this needs to be rethought so we can take advantage
 * XXX cheetah's full 64-bit virtual address space, ie. no more hole
 * XXX in the middle like on spitfire. -DaveM
 */
/*
 * Given a virtual address, the lowest PAGE_SHIFT bits determine offset
 * into the page; the next higher PAGE_SHIFT-3 bits determine the pte#
 * in the proper pagetable (the -3 is from the 8 byte ptes, and each page
 * table is a single page long). The next higher PMD_BITS determine pmd# 
 * in the proper pmdtable (where we must have PMD_BITS <= (PAGE_SHIFT-2) 
 * since the pmd entries are 4 bytes, and each pmd page is a single page 
 * long). Finally, the higher few bits determine pgde#.
 */

/* PMD_SHIFT determines the size of the area a second-level page
 * table can map
 */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(_AC(1,UL) << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PMD_BITS	(PAGE_SHIFT - 2)

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3) + PMD_BITS)
#define PGDIR_SIZE	(_AC(1,UL) << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PGDIR_BITS	(PAGE_SHIFT - 2)

#ifndef __ASSEMBLY__

#include <linux/sched.h>

/* Entries per page directory level. */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PMD	(1UL << PMD_BITS)
#define PTRS_PER_PGD	(1UL << PGDIR_BITS)

/* Kernel has a separate 44bit address space. */
#define FIRST_USER_ADDRESS	0

#define pte_ERROR(e)	__builtin_trap()
#define pmd_ERROR(e)	__builtin_trap()
#define pgd_ERROR(e)	__builtin_trap()

#endif /* !(__ASSEMBLY__) */

/* PTE bits which are the same in SUN4U and SUN4V format.  */
#define _PAGE_VALID	0x8000000000000000 /* Valid TTE              */
#define _PAGE_R	  	0x8000000000000000 /* Keep ref bit up to date*/

/* These are actually filled in at boot time by sun4{u,v}_pgprot_init() */
#define __P000	__pgprot(0)
#define __P001	__pgprot(0)
#define __P010	__pgprot(0)
#define __P011	__pgprot(0)
#define __P100	__pgprot(0)
#define __P101	__pgprot(0)
#define __P110	__pgprot(0)
#define __P111	__pgprot(0)

#define __S000	__pgprot(0)
#define __S001	__pgprot(0)
#define __S010	__pgprot(0)
#define __S011	__pgprot(0)
#define __S100	__pgprot(0)
#define __S101	__pgprot(0)
#define __S110	__pgprot(0)
#define __S111	__pgprot(0)

#ifndef __ASSEMBLY__

extern pte_t mk_pte_io(unsigned long, pgprot_t, int, unsigned long);

extern unsigned long pte_sz_bits(unsigned long size);

extern pgprot_t PAGE_KERNEL;
extern pgprot_t PAGE_KERNEL_LOCKED;
extern pgprot_t PAGE_COPY;

/* XXX This uglyness is for the atyfb driver's sparc mmap() support. XXX */
extern unsigned long _PAGE_IE;
extern unsigned long _PAGE_E;
extern unsigned long _PAGE_CACHE;

extern unsigned long pg_iobits;
extern unsigned long _PAGE_ALL_SZ_BITS;
extern unsigned long _PAGE_SZBITS;

extern unsigned long phys_base;
extern unsigned long pfn_base;

extern struct page *mem_map_zero;
#define ZERO_PAGE(vaddr)	(mem_map_zero)

/* PFNs are real physical page numbers.  However, mem_map only begins to record
 * per-page information starting at pfn_base.  This is to handle systems where
 * the first physical page in the machine is at some huge physical address,
 * such as 4GB.   This is common on a partitioned E10000, for example.
 */
extern pte_t pfn_pte(unsigned long, pgprot_t);
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))
extern unsigned long pte_pfn(pte_t);
#define pte_page(x) pfn_to_page(pte_pfn(x))
extern pte_t pte_modify(pte_t, pgprot_t);

#define pmd_set(pmdp, ptep)	\
	(pmd_val(*(pmdp)) = (__pa((unsigned long) (ptep)) >> 11UL))
#define pud_set(pudp, pmdp)	\
	(pud_val(*(pudp)) = (__pa((unsigned long) (pmdp)) >> 11UL))
#define __pmd_page(pmd)		\
	((unsigned long) __va((((unsigned long)pmd_val(pmd))<<11UL)))
#define pmd_page(pmd) 			virt_to_page((void *)__pmd_page(pmd))
#define pud_page(pud)		\
	((unsigned long) __va((((unsigned long)pud_val(pud))<<11UL)))
#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(0)
#define pmd_present(pmd)		(pmd_val(pmd) != 0U)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0U)
#define pud_none(pud)			(!pud_val(pud))
#define pud_bad(pud)			(0)
#define pud_present(pud)		(pud_val(pud) != 0U)
#define pud_clear(pudp)			(pud_val(*(pudp)) = 0U)

/* Same in both SUN4V and SUN4U.  */
#define pte_none(pte) 			(!pte_val(pte))

extern unsigned long pte_present(pte_t);

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern unsigned long pte_read(pte_t);
extern unsigned long pte_exec(pte_t);
extern unsigned long pte_write(pte_t);
extern unsigned long pte_dirty(pte_t);
extern unsigned long pte_young(pte_t);
extern pte_t pte_wrprotect(pte_t);
extern pte_t pte_rdprotect(pte_t);
extern pte_t pte_mkclean(pte_t);
extern pte_t pte_mkold(pte_t);

/* Be very careful when you change these three, they are delicate. */
extern pte_t pte_mkyoung(pte_t);
extern pte_t pte_mkwrite(pte_t);
extern pte_t pte_mkdirty(pte_t);
extern pte_t pte_mkhuge(pte_t);

/* to find an entry in a page-table-directory. */
#define pgd_index(address)	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
#define pmd_offset(pudp, address)	\
	((pmd_t *) pud_page(*(pudp)) + \
	 (((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1)))

/* Find an entry in the third-level page table.. */
#define pte_index(dir, address)	\
	((pte_t *) __pmd_page(*(dir)) + \
	 ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))
#define pte_offset_kernel		pte_index
#define pte_offset_map			pte_index
#define pte_offset_map_nested		pte_index
#define pte_unmap(pte)			do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

/* Actual page table PTE updates.  */
extern void tlb_batch_add(struct mm_struct *mm, unsigned long vaddr, pte_t *ptep, pte_t orig);

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pte)
{
	pte_t orig = *ptep;

	*ptep = pte;

	/* It is more efficient to let flush_tlb_kernel_range()
	 * handle init_mm tlb flushes.
	 *
	 * SUN4V NOTE: _PAGE_VALID is the same value in both the SUN4U
	 *             and SUN4V pte layout, so this inline test is fine.
	 */
	if (likely(mm != &init_mm) && (pte_val(orig) & _PAGE_VALID))
		tlb_batch_add(mm, addr, ptep, orig);
}

#define pte_clear(mm,addr,ptep)		\
	set_pte_at((mm), (addr), (ptep), __pte(0UL))

extern pgd_t swapper_pg_dir[2048];
extern pmd_t swapper_low_pmd_dir[2048];

extern void paging_init(void);
extern unsigned long find_ecache_flush_span(unsigned long size);

/* These do nothing with the way I have things setup. */
#define mmu_lockarea(vaddr, len)		(vaddr)
#define mmu_unlockarea(vaddr, len)		do { } while(0)

struct vm_area_struct;
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

/* Encode and de-code a swap entry */
#define __swp_type(entry)	(((entry).val >> PAGE_SHIFT) & 0xffUL)
#define __swp_offset(entry)	((entry).val >> (PAGE_SHIFT + 8UL))
#define __swp_entry(type, offset)	\
	( (swp_entry_t) \
	  { \
		(((long)(type) << PAGE_SHIFT) | \
                 ((long)(offset) << (PAGE_SHIFT + 8UL))) \
	  } )
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

/* File offset in PTE support. */
extern unsigned long pte_file(pte_t);
#define pte_to_pgoff(pte)	(pte_val(pte) >> PAGE_SHIFT)
extern pte_t pgoff_to_pte(unsigned long);
#define PTE_FILE_MAX_BITS	(64UL - PAGE_SHIFT - 1UL)

extern unsigned long prom_virt_to_phys(unsigned long, int *);

extern unsigned long sun4u_get_pte(unsigned long);

static inline unsigned long __get_phys(unsigned long addr)
{
	return sun4u_get_pte(addr);
}

static inline int __get_iospace(unsigned long addr)
{
	return ((sun4u_get_pte(addr) & 0xf0000000) >> 28);
}

extern unsigned long *sparc64_valid_addr_bitmap;

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	\
	(test_bit(__pa((unsigned long)(addr))>>22, sparc64_valid_addr_bitmap))

extern int io_remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
			       unsigned long pfn,
			       unsigned long size, pgprot_t prot);

/* Clear virtual and physical cachability, set side-effect bit.  */
extern pgprot_t pgprot_noncached(pgprot_t);

/*
 * For sparc32&64, the pfn in io_remap_pfn_range() carries <iospace> in
 * its high 4 bits.  These macros/functions put it there or get it from there.
 */
#define MK_IOSPACE_PFN(space, pfn)	(pfn | (space << (BITS_PER_LONG - 4)))
#define GET_IOSPACE(pfn)		(pfn >> (BITS_PER_LONG - 4))
#define GET_PFN(pfn)			(pfn & 0x0fffffffffffffffUL)

#include <asm-generic/pgtable.h>

/* We provide our own get_unmapped_area to cope with VA holes for userland */
#define HAVE_ARCH_UNMAPPED_AREA

/* We provide a special get_unmapped_area for framebuffer mmaps to try and use
 * the largest alignment possible such that larget PTEs can be used.
 */
extern unsigned long get_fb_unmapped_area(struct file *filp, unsigned long,
					  unsigned long, unsigned long,
					  unsigned long);
#define HAVE_ARCH_FB_UNMAPPED_AREA

extern void pgtable_cache_init(void);
extern void sun4v_register_fault_status(void);
extern void sun4v_ktsb_register(void);

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_PGTABLE_H) */
