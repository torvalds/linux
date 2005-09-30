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
 * The page copy blockops can use 0x2000000 to 0x10000000.
 * The PROM resides in an area spanning 0xf0000000 to 0x100000000.
 * The vmalloc area spans 0x100000000 to 0x200000000.
 * Since modules need to be in the lowest 32-bits of the address space,
 * we place them right before the OBP area from 0x10000000 to 0xf0000000.
 * There is a single static kernel PMD which maps from 0x0 to address
 * 0x400000000.
 */
#define	TLBTEMP_BASE		_AC(0x0000000002000000,UL)
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

/* Spitfire/Cheetah TTE bits. */
#define _PAGE_VALID	_AC(0x8000000000000000,UL) /* Valid TTE              */
#define _PAGE_R		_AC(0x8000000000000000,UL) /* Keep ref bit up to date*/
#define _PAGE_SZ4MB	_AC(0x6000000000000000,UL) /* 4MB Page               */
#define _PAGE_SZ512K	_AC(0x4000000000000000,UL) /* 512K Page              */
#define _PAGE_SZ64K	_AC(0x2000000000000000,UL) /* 64K Page               */
#define _PAGE_SZ8K	_AC(0x0000000000000000,UL) /* 8K Page                */
#define _PAGE_NFO	_AC(0x1000000000000000,UL) /* No Fault Only          */
#define _PAGE_IE	_AC(0x0800000000000000,UL) /* Invert Endianness      */
#define _PAGE_SOFT2	_AC(0x07FC000000000000,UL) /* Software bits, set 2   */
#define _PAGE_RES1	_AC(0x0002000000000000,UL) /* Reserved               */
#define _PAGE_SZ32MB	_AC(0x0001000000000000,UL) /* (Panther) 32MB page    */
#define _PAGE_SZ256MB	_AC(0x2001000000000000,UL) /* (Panther) 256MB page   */
#define _PAGE_SN	_AC(0x0000800000000000,UL) /* (Cheetah) Snoop        */
#define _PAGE_RES2	_AC(0x0000780000000000,UL) /* Reserved               */
#define _PAGE_PADDR_SF	_AC(0x000001FFFFFFE000,UL) /* (Spitfire) paddr[40:13]*/
#define _PAGE_PADDR	_AC(0x000007FFFFFFE000,UL) /* (Cheetah) paddr[42:13] */
#define _PAGE_SOFT	_AC(0x0000000000001F80,UL) /* Software bits          */
#define _PAGE_L		_AC(0x0000000000000040,UL) /* Locked TTE             */
#define _PAGE_CP	_AC(0x0000000000000020,UL) /* Cacheable in P-Cache   */
#define _PAGE_CV	_AC(0x0000000000000010,UL) /* Cacheable in V-Cache   */
#define _PAGE_E		_AC(0x0000000000000008,UL) /* side-Effect            */
#define _PAGE_P		_AC(0x0000000000000004,UL) /* Privileged Page        */
#define _PAGE_W		_AC(0x0000000000000002,UL) /* Writable               */
#define _PAGE_G		_AC(0x0000000000000001,UL) /* Global                 */

/* Here are the SpitFire software bits we use in the TTE's.
 *
 * WARNING: If you are going to try and start using some
 *          of the soft2 bits, you will need to make
 *          modifications to the swap entry implementation.
 *	    For example, one thing that could happen is that
 *          swp_entry_to_pte() would BUG_ON() if you tried
 *          to use one of the soft2 bits for _PAGE_FILE.
 *
 * Like other architectures, I have aliased _PAGE_FILE with
 * _PAGE_MODIFIED.  This works because _PAGE_FILE is never
 * interpreted that way unless _PAGE_PRESENT is clear.
 */
#define _PAGE_EXEC	_AC(0x0000000000001000,UL)	/* Executable SW bit */
#define _PAGE_MODIFIED	_AC(0x0000000000000800,UL)	/* Modified (dirty)  */
#define _PAGE_FILE	_AC(0x0000000000000800,UL)	/* Pagecache page    */
#define _PAGE_ACCESSED	_AC(0x0000000000000400,UL)	/* Accessed (ref'd)  */
#define _PAGE_READ	_AC(0x0000000000000200,UL)	/* Readable SW Bit   */
#define _PAGE_WRITE	_AC(0x0000000000000100,UL)	/* Writable SW Bit   */
#define _PAGE_PRESENT	_AC(0x0000000000000080,UL)	/* Present           */

#if PAGE_SHIFT == 13
#define _PAGE_SZBITS	_PAGE_SZ8K
#elif PAGE_SHIFT == 16
#define _PAGE_SZBITS	_PAGE_SZ64K
#elif PAGE_SHIFT == 19
#define _PAGE_SZBITS	_PAGE_SZ512K
#elif PAGE_SHIFT == 22
#define _PAGE_SZBITS	_PAGE_SZ4MB
#else
#error Wrong PAGE_SHIFT specified
#endif

#if defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define _PAGE_SZHUGE	_PAGE_SZ4MB
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
#define _PAGE_SZHUGE	_PAGE_SZ512K
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define _PAGE_SZHUGE	_PAGE_SZ64K
#endif

#define _PAGE_CACHE	(_PAGE_CP | _PAGE_CV)

#define __DIRTY_BITS	(_PAGE_MODIFIED | _PAGE_WRITE | _PAGE_W)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_READ | _PAGE_R)
#define __PRIV_BITS	_PAGE_P

#define PAGE_NONE	__pgprot (_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_CACHE)

/* Don't set the TTE _PAGE_W bit here, else the dirty bit never gets set. */
#define PAGE_SHARED	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS | _PAGE_WRITE | _PAGE_EXEC)

#define PAGE_COPY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS | _PAGE_EXEC)

#define PAGE_READONLY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS | _PAGE_EXEC)

#define PAGE_KERNEL	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __PRIV_BITS | \
				  __ACCESS_BITS | __DIRTY_BITS | _PAGE_EXEC)

#define PAGE_SHARED_NOEXEC	__pgprot (_PAGE_PRESENT | _PAGE_VALID | \
					  _PAGE_CACHE | \
					  __ACCESS_BITS | _PAGE_WRITE)

#define PAGE_COPY_NOEXEC	__pgprot (_PAGE_PRESENT | _PAGE_VALID | \
					  _PAGE_CACHE | __ACCESS_BITS)

#define PAGE_READONLY_NOEXEC	__pgprot (_PAGE_PRESENT | _PAGE_VALID | \
					  _PAGE_CACHE | __ACCESS_BITS)

#define _PFN_MASK	_PAGE_PADDR

#define pg_iobits (_PAGE_VALID | _PAGE_PRESENT | __DIRTY_BITS | \
		   __ACCESS_BITS | _PAGE_E)

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY_NOEXEC
#define __P010	PAGE_COPY_NOEXEC
#define __P011	PAGE_COPY_NOEXEC
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY_NOEXEC
#define __S010	PAGE_SHARED_NOEXEC
#define __S011	PAGE_SHARED_NOEXEC
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

#ifndef __ASSEMBLY__

extern unsigned long phys_base;
extern unsigned long pfn_base;

extern struct page *mem_map_zero;
#define ZERO_PAGE(vaddr)	(mem_map_zero)

/* PFNs are real physical page numbers.  However, mem_map only begins to record
 * per-page information starting at pfn_base.  This is to handle systems where
 * the first physical page in the machine is at some huge physical address,
 * such as 4GB.   This is common on a partitioned E10000, for example.
 */

#define pfn_pte(pfn, prot)	\
	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot) | _PAGE_SZBITS)
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

#define pte_pfn(x)		((pte_val(x) & _PAGE_PADDR)>>PAGE_SHIFT)
#define pte_page(x)		pfn_to_page(pte_pfn(x))

#define page_pte_prot(page, prot)	mk_pte(page, prot)
#define page_pte(page)			page_pte_prot(page, __pgprot(0))

static inline pte_t pte_modify(pte_t orig_pte, pgprot_t new_prot)
{
	pte_t __pte;
	const unsigned long preserve_mask = (_PFN_MASK |
					     _PAGE_MODIFIED | _PAGE_ACCESSED |
					     _PAGE_CACHE | _PAGE_E |
					     _PAGE_PRESENT | _PAGE_SZBITS);

	pte_val(__pte) = (pte_val(orig_pte) & preserve_mask) |
		(pgprot_val(new_prot) & ~preserve_mask);

	return __pte;
}
#define pmd_set(pmdp, ptep)	\
	(pmd_val(*(pmdp)) = (__pa((unsigned long) (ptep)) >> 11UL))
#define pud_set(pudp, pmdp)	\
	(pud_val(*(pudp)) = (__pa((unsigned long) (pmdp)) >> 11UL))
#define __pmd_page(pmd)		\
	((unsigned long) __va((((unsigned long)pmd_val(pmd))<<11UL)))
#define pmd_page(pmd) 			virt_to_page((void *)__pmd_page(pmd))
#define pud_page(pud)		\
	((unsigned long) __va((((unsigned long)pud_val(pud))<<11UL)))
#define pte_none(pte) 			(!pte_val(pte))
#define pte_present(pte)		(pte_val(pte) & _PAGE_PRESENT)
#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(0)
#define pmd_present(pmd)		(pmd_val(pmd) != 0U)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0U)
#define pud_none(pud)			(!pud_val(pud))
#define pud_bad(pud)			(0)
#define pud_present(pud)		(pud_val(pud) != 0U)
#define pud_clear(pudp)			(pud_val(*(pudp)) = 0U)

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_read(pte)		(pte_val(pte) & _PAGE_READ)
#define pte_exec(pte)		(pte_val(pte) & _PAGE_EXEC)
#define pte_write(pte)		(pte_val(pte) & _PAGE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_MODIFIED)
#define pte_young(pte)		(pte_val(pte) & _PAGE_ACCESSED)
#define pte_wrprotect(pte)	(__pte(pte_val(pte) & ~(_PAGE_WRITE|_PAGE_W)))
#define pte_rdprotect(pte)	\
	(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_READ))
#define pte_mkclean(pte)	\
	(__pte(pte_val(pte) & ~(_PAGE_MODIFIED|_PAGE_W)))
#define pte_mkold(pte)		\
	(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_ACCESSED))

/* Permanent address of a page. */
#define __page_address(page)	page_address(page)

/* Be very careful when you change these three, they are delicate. */
#define pte_mkyoung(pte)	(__pte(pte_val(pte) | _PAGE_ACCESSED | _PAGE_R))
#define pte_mkwrite(pte)	(__pte(pte_val(pte) | _PAGE_WRITE))
#define pte_mkdirty(pte)	(__pte(pte_val(pte) | _PAGE_MODIFIED | _PAGE_W))
#define pte_mkhuge(pte)		(__pte(pte_val(pte) | _PAGE_SZHUGE))

/* to find an entry in a page-table-directory. */
#define pgd_index(address)	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* extract the pgd cache used for optimizing the tlb miss
 * slow path when executing 32-bit compat processes
 */
#define get_pgd_cache(pgd)	((unsigned long) pgd_val(*pgd) << 11)

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

/* Make a non-present pseudo-TTE. */
static inline pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space)
{
	pte_t pte;
	pte_val(pte) = (((page) | pgprot_val(prot) | _PAGE_E) &
			~(unsigned long)_PAGE_CACHE);
	pte_val(pte) |= (((unsigned long)space) << 32);
	return pte;
}

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
#define pte_file(pte)		(pte_val(pte) & _PAGE_FILE)
#define pte_to_pgoff(pte)	(pte_val(pte) >> PAGE_SHIFT)
#define pgoff_to_pte(off)	(__pte(((off) << PAGE_SHIFT) | _PAGE_FILE))
#define PTE_FILE_MAX_BITS	(64UL - PAGE_SHIFT - 1UL)

extern unsigned long prom_virt_to_phys(unsigned long, int *);

static __inline__ unsigned long
sun4u_get_pte (unsigned long addr)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (addr >= PAGE_OFFSET)
		return addr & _PAGE_PADDR;
	if ((addr >= LOW_OBP_ADDRESS) && (addr < HI_OBP_ADDRESS))
		return prom_virt_to_phys(addr, NULL);
	pgdp = pgd_offset_k(addr);
	pudp = pud_offset(pgdp, addr);
	pmdp = pmd_offset(pudp, addr);
	ptep = pte_offset_kernel(pmdp, addr);
	return pte_val(*ptep) & _PAGE_PADDR;
}

static __inline__ unsigned long
__get_phys (unsigned long addr)
{
	return sun4u_get_pte (addr);
}

static __inline__ int
__get_iospace (unsigned long addr)
{
	return ((sun4u_get_pte (addr) & 0xf0000000) >> 28);
}

extern unsigned long *sparc64_valid_addr_bitmap;

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	\
	(test_bit(__pa((unsigned long)(addr))>>22, sparc64_valid_addr_bitmap))

extern int io_remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
			       unsigned long pfn,
			       unsigned long size, pgprot_t prot);

/* Clear virtual and physical cachability, set side-effect bit.  */
#define pgprot_noncached(prot) \
	(__pgprot((pgprot_val(prot) & ~(_PAGE_CP | _PAGE_CV)) | \
	 _PAGE_E))

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

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

extern void check_pgt_cache(void);

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_PGTABLE_H) */
