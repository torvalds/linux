#ifndef _PPC64_PGTABLE_H
#define _PPC64_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the ppc64 hashed page table.
 */

#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/stddef.h>
#include <asm/processor.h>		/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/tlbflush.h>
#endif /* __ASSEMBLY__ */

/*
 * Entries per page directory level.  The PTE level must use a 64b record
 * for each page table entry.  The PMD and PGD level use a 32b record for 
 * each entry by assuming that each entry is page aligned.
 */
#define PTE_INDEX_SIZE  9
#define PMD_INDEX_SIZE  7
#define PUD_INDEX_SIZE  7
#define PGD_INDEX_SIZE  9

#define PTE_TABLE_SIZE	(sizeof(pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PUD_TABLE_SIZE	(sizeof(pud_t) << PUD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PUD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PUD_SHIFT determines what a third-level page table entry can map */
#define PUD_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define FIRST_USER_ADDRESS	0

/*
 * Size of EA range mapped by our pagetables.
 */
#define PGTABLE_EADDR_SIZE (PTE_INDEX_SIZE + PMD_INDEX_SIZE + \
                	    PUD_INDEX_SIZE + PGD_INDEX_SIZE + PAGE_SHIFT)
#define PGTABLE_RANGE (1UL << PGTABLE_EADDR_SIZE)

#if TASK_SIZE_USER64 > PGTABLE_RANGE
#error TASK_SIZE_USER64 exceeds pagetable range
#endif

#if TASK_SIZE_USER64 > (1UL << (USER_ESID_BITS + SID_SHIFT))
#error TASK_SIZE_USER64 exceeds user VSID range
#endif

/*
 * Define the address range of the vmalloc VM area.
 */
#define VMALLOC_START (0xD000000000000000ul)
#define VMALLOC_SIZE  (0x80000000000UL)
#define VMALLOC_END   (VMALLOC_START + VMALLOC_SIZE)

/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */
#define _PAGE_PRESENT	0x0001 /* software: pte contains a translation */
#define _PAGE_USER	0x0002 /* matches one of the PP bits */
#define _PAGE_FILE	0x0002 /* (!present only) software: pte holds file offset */
#define _PAGE_EXEC	0x0004 /* No execute on POWER4 and newer (we invert) */
#define _PAGE_GUARDED	0x0008
#define _PAGE_COHERENT	0x0010 /* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x0020 /* I: cache inhibit */
#define _PAGE_WRITETHRU	0x0040 /* W: cache write-through */
#define _PAGE_DIRTY	0x0080 /* C: page changed */
#define _PAGE_ACCESSED	0x0100 /* R: page referenced */
#define _PAGE_RW	0x0200 /* software: user write access allowed */
#define _PAGE_HASHPTE	0x0400 /* software: pte has an associated HPTE */
#define _PAGE_BUSY	0x0800 /* software: PTE & hash are busy */ 
#define _PAGE_SECONDARY 0x8000 /* software: HPTE is in secondary group */
#define _PAGE_GROUP_IX  0x7000 /* software: HPTE index within group */
#define _PAGE_HUGE	0x10000 /* 16MB page */
/* Bits 0x7000 identify the index within an HPT Group */
#define _PAGE_HPTEFLAGS (_PAGE_BUSY | _PAGE_HASHPTE | _PAGE_SECONDARY | _PAGE_GROUP_IX)
/* PAGE_MASK gives the right answer below, but only by accident */
/* It should be preserving the high 48 bits and then specifically */
/* preserving _PAGE_SECONDARY | _PAGE_GROUP_IX */
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_HPTEFLAGS)

#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_COHERENT)

#define _PAGE_WRENABLE	(_PAGE_RW | _PAGE_DIRTY)

/* __pgprot defined in asm-ppc64/page.h */
#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_USER)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_USER | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_WRENABLE)
#define PAGE_KERNEL_CI	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | \
			       _PAGE_WRENABLE | _PAGE_NO_CACHE | _PAGE_GUARDED)
#define PAGE_KERNEL_EXEC __pgprot(_PAGE_BASE | _PAGE_WRENABLE | _PAGE_EXEC)

#define PAGE_AGP	__pgprot(_PAGE_BASE | _PAGE_WRENABLE | _PAGE_NO_CACHE)
#define HAVE_PAGE_AGP

/*
 * This bit in a hardware PTE indicates that the page is *not* executable.
 */
#define HW_NO_EXEC	_PAGE_EXEC

/*
 * POWER4 and newer have per page execute protection, older chips can only
 * do this on a segment (256MB) basis.
 *
 * Also, write permissions imply read permissions.
 * This is the closest we can get..
 *
 * Note due to the way vm flags are laid out, the bits are XWR
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_X
#define __P101	PAGE_READONLY_X
#define __P110	PAGE_COPY_X
#define __P111	PAGE_COPY_X

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_X
#define __S101	PAGE_READONLY_X
#define __S110	PAGE_SHARED_X
#define __S111	PAGE_SHARED_X

#ifndef __ASSEMBLY__

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE/sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))
#endif /* __ASSEMBLY__ */

/* shift to put page number into pte */
#define PTE_SHIFT (17)

#ifdef CONFIG_HUGETLB_PAGE

#ifndef __ASSEMBLY__
int hash_huge_page(struct mm_struct *mm, unsigned long access,
		   unsigned long ea, unsigned long vsid, int local);
#endif /* __ASSEMBLY__ */

#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
#else

#define hash_huge_page(mm,a,ea,vsid,local)	-1

#endif

#ifndef __ASSEMBLY__

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * mk_pte takes a (struct page *) as input
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pfn_pte(unsigned long pfn, pgprot_t pgprot)
{
	pte_t pte;


	pte_val(pte) = (pfn << PTE_SHIFT) | pgprot_val(pgprot);
	return pte;
}

#define pte_modify(_pte, newprot) \
  (__pte((pte_val(_pte) & _PAGE_CHG_MASK) | pgprot_val(newprot)))

#define pte_none(pte)		((pte_val(pte) & ~_PAGE_HPTEFLAGS) == 0)
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)

/* pte_clear moved to later in this file */

#define pte_pfn(x)		((unsigned long)((pte_val(x) >> PTE_SHIFT)))
#define pte_page(x)		pfn_to_page(pte_pfn(x))

#define pmd_set(pmdp, ptep) 	({BUG_ON((u64)ptep < KERNELBASE); pmd_val(*(pmdp)) = (unsigned long)(ptep);})
#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_bad(pmd)		(pmd_val(pmd) == 0)
#define	pmd_present(pmd)	(pmd_val(pmd) != 0)
#define	pmd_clear(pmdp)		(pmd_val(*(pmdp)) = 0)
#define pmd_page_kernel(pmd)	(pmd_val(pmd))
#define pmd_page(pmd)		virt_to_page(pmd_page_kernel(pmd))

#define pud_set(pudp, pmdp)	(pud_val(*(pudp)) = (unsigned long)(pmdp))
#define pud_none(pud)		(!pud_val(pud))
#define pud_bad(pud)		((pud_val(pud)) == 0)
#define pud_present(pud)	(pud_val(pud) != 0)
#define pud_clear(pudp)		(pud_val(*(pudp)) = 0)
#define pud_page(pud)		(pud_val(pud))

#define pgd_set(pgdp, pudp)	({pgd_val(*(pgdp)) = (unsigned long)(pudp);})
#define pgd_none(pgd)		(!pgd_val(pgd))
#define pgd_bad(pgd)		(pgd_val(pgd) == 0)
#define pgd_present(pgd)	(pgd_val(pgd) != 0)
#define pgd_clear(pgdp)		(pgd_val(*(pgdp)) = 0)
#define pgd_page(pgd)		(pgd_val(pgd))

/* 
 * Find an entry in a page-table-directory.  We combine the address region 
 * (the high order N bits) and the pgd portion of the address.
 */
/* to avoid overflow in free_pgtables we don't use PTRS_PER_PGD here */
#define pgd_index(address) (((address) >> (PGDIR_SHIFT)) & 0x1ff)

#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

#define pud_offset(pgdp, addr)	\
  (((pud_t *) pgd_page(*(pgdp))) + (((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1)))

#define pmd_offset(pudp,addr) \
  (((pmd_t *) pud_page(*(pudp))) + (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1)))

#define pte_offset_kernel(dir,addr) \
  (((pte_t *) pmd_page_kernel(*(dir))) + (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while(0)
#define pte_unmap_nested(pte)		do { } while(0)

/* to find an entry in a kernel page-table-directory */
/* This now only contains the vmalloc pages */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)  { return pte_val(pte) & _PAGE_USER;}
static inline int pte_write(pte_t pte) { return pte_val(pte) & _PAGE_RW;}
static inline int pte_exec(pte_t pte)  { return pte_val(pte) & _PAGE_EXEC;}
static inline int pte_dirty(pte_t pte) { return pte_val(pte) & _PAGE_DIRTY;}
static inline int pte_young(pte_t pte) { return pte_val(pte) & _PAGE_ACCESSED;}
static inline int pte_file(pte_t pte) { return pte_val(pte) & _PAGE_FILE;}
static inline int pte_huge(pte_t pte) { return pte_val(pte) & _PAGE_HUGE;}

static inline void pte_uncache(pte_t pte) { pte_val(pte) |= _PAGE_NO_CACHE; }
static inline void pte_cache(pte_t pte)   { pte_val(pte) &= ~_PAGE_NO_CACHE; }

static inline pte_t pte_rdprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
static inline pte_t pte_exprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_EXEC; return pte; }
static inline pte_t pte_wrprotect(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_RW); return pte; }
static inline pte_t pte_mkclean(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_DIRTY); return pte; }
static inline pte_t pte_mkold(pte_t pte) {
	pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }

static inline pte_t pte_mkread(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
static inline pte_t pte_mkexec(pte_t pte) {
	pte_val(pte) |= _PAGE_USER | _PAGE_EXEC; return pte; }
static inline pte_t pte_mkwrite(pte_t pte) {
	pte_val(pte) |= _PAGE_RW; return pte; }
static inline pte_t pte_mkdirty(pte_t pte) {
	pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte) {
	pte_val(pte) |= _PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkhuge(pte_t pte) {
	pte_val(pte) |= _PAGE_HUGE; return pte; }

/* Atomic PTE updates */
static inline unsigned long pte_update(pte_t *p, unsigned long clr)
{
	unsigned long old, tmp;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3		# pte_update\n\
	andi.	%1,%0,%6\n\
	bne-	1b \n\
	andc	%1,%0,%4 \n\
	stdcx.	%1,0,%3 \n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*p)
	: "r" (p), "r" (clr), "m" (*p), "i" (_PAGE_BUSY)
	: "cc" );
	return old;
}

/* PTE updating functions, this function puts the PTE in the
 * batch, doesn't actually triggers the hash flush immediately,
 * you need to call flush_tlb_pending() to do that.
 */
extern void hpte_update(struct mm_struct *mm, unsigned long addr, unsigned long pte,
			int wrprot);

static inline int __ptep_test_and_clear_young(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	unsigned long old;

       	if ((pte_val(*ptep) & (_PAGE_ACCESSED | _PAGE_HASHPTE)) == 0)
		return 0;
	old = pte_update(ptep, _PAGE_ACCESSED);
	if (old & _PAGE_HASHPTE) {
		hpte_update(mm, addr, old, 0);
		flush_tlb_pending();
	}
	return (old & _PAGE_ACCESSED) != 0;
}
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __addr, __ptep)		   \
({									   \
	int __r;							   \
	__r = __ptep_test_and_clear_young((__vma)->vm_mm, __addr, __ptep); \
	__r;								   \
})

/*
 * On RW/DIRTY bit transitions we can avoid flushing the hpte. For the
 * moment we always flush but we need to fix hpte_update and test if the
 * optimisation is worth it.
 */
static inline int __ptep_test_and_clear_dirty(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	unsigned long old;

       	if ((pte_val(*ptep) & _PAGE_DIRTY) == 0)
		return 0;
	old = pte_update(ptep, _PAGE_DIRTY);
	if (old & _PAGE_HASHPTE)
		hpte_update(mm, addr, old, 0);
	return (old & _PAGE_DIRTY) != 0;
}
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
#define ptep_test_and_clear_dirty(__vma, __addr, __ptep)		   \
({									   \
	int __r;							   \
	__r = __ptep_test_and_clear_dirty((__vma)->vm_mm, __addr, __ptep); \
	__r;								   \
})

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	unsigned long old;

       	if ((pte_val(*ptep) & _PAGE_RW) == 0)
       		return;
	old = pte_update(ptep, _PAGE_RW);
	if (old & _PAGE_HASHPTE)
		hpte_update(mm, addr, old, 0);
}

/*
 * We currently remove entries from the hashtable regardless of whether
 * the entry was young or dirty. The generic routines only flush if the
 * entry was young or dirty which is not good enough.
 *
 * We should be more intelligent about this but for the moment we override
 * these functions and force a tlb flush unconditionally
 */
#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young(__vma, __address, __ptep)		\
({									\
	int __young = __ptep_test_and_clear_young((__vma)->vm_mm, __address, \
						  __ptep);		\
	__young;							\
})

#define __HAVE_ARCH_PTEP_CLEAR_DIRTY_FLUSH
#define ptep_clear_flush_dirty(__vma, __address, __ptep)		\
({									\
	int __dirty = __ptep_test_and_clear_dirty((__vma)->vm_mm, __address, \
						  __ptep); 		\
	flush_tlb_page(__vma, __address);				\
	__dirty;							\
})

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	unsigned long old = pte_update(ptep, ~0UL);

	if (old & _PAGE_HASHPTE)
		hpte_update(mm, addr, old, 0);
	return __pte(old);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t * ptep)
{
	unsigned long old = pte_update(ptep, ~0UL);

	if (old & _PAGE_HASHPTE)
		hpte_update(mm, addr, old, 0);
}

/*
 * set_pte stores a linux PTE into the linux page table.
 */
static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	if (pte_present(*ptep)) {
		pte_clear(mm, addr, ptep);
		flush_tlb_pending();
	}
	*ptep = __pte(pte_val(pte) & ~_PAGE_HPTEFLAGS);
}

/* Set the dirty and/or accessed bits atomically in a linux PTE, this
 * function doesn't need to flush the hash entry
 */
#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
static inline void __ptep_set_access_flags(pte_t *ptep, pte_t entry, int dirty)
{
	unsigned long bits = pte_val(entry) &
		(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW | _PAGE_EXEC);
	unsigned long old, tmp;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%4\n\
		andi.	%1,%0,%6\n\
		bne-	1b \n\
		or	%0,%3,%0\n\
		stdcx.	%0,0,%4\n\
		bne-	1b"
	:"=&r" (old), "=&r" (tmp), "=m" (*ptep)
	:"r" (bits), "r" (ptep), "m" (*ptep), "i" (_PAGE_BUSY)
	:"cc");
}
#define  ptep_set_access_flags(__vma, __address, __ptep, __entry, __dirty) \
	do {								   \
		__ptep_set_access_flags(__ptep, __entry, __dirty);	   \
		flush_tlb_page_nohash(__vma, __address);	       	   \
	} while(0)

/*
 * Macro to mark a page protection value as "uncacheable".
 */
#define pgprot_noncached(prot)	(__pgprot(pgprot_val(prot) | _PAGE_NO_CACHE | _PAGE_GUARDED))

struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#define __HAVE_PHYS_MEM_ACCESS_PROT

#define __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	(((pte_val(A) ^ pte_val(B)) & ~_PAGE_HPTEFLAGS) == 0)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pud_ERROR(e) \
	printk("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

extern pgd_t swapper_pg_dir[];

extern void paging_init(void);

#ifdef CONFIG_HUGETLB_PAGE
#define hugetlb_free_pgd_range(tlb, addr, end, floor, ceiling) \
	free_pgd_range(tlb, addr, end, floor, ceiling)
#endif

/*
 * This gets called at the end of handling a page fault, when
 * the kernel has put a new PTE into the page table for the process.
 * We use it to put a corresponding HPTE into the hash table
 * ahead of time, instead of waiting for the inevitable extra
 * hash-table miss exception.
 */
struct vm_area_struct;
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

/* Encode and de-code a swap entry */
#define __swp_type(entry)	(((entry).val >> 1) & 0x3f)
#define __swp_offset(entry)	((entry).val >> 8)
#define __swp_entry(type, offset) ((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) >> PTE_SHIFT })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val << PTE_SHIFT })
#define pte_to_pgoff(pte)	(pte_val(pte) >> PTE_SHIFT)
#define pgoff_to_pte(off)	((pte_t) {((off) << PTE_SHIFT)|_PAGE_FILE})
#define PTE_FILE_MAX_BITS	(BITS_PER_LONG - PTE_SHIFT)

/*
 * kern_addr_valid is intended to indicate whether an address is a valid
 * kernel address.  Most 32-bit archs define it as always true (like this)
 * but most 64-bit archs actually perform a test.  What should we do here?
 * The only use is in fs/ncpfs/dir.c
 */
#define kern_addr_valid(addr)	(1)

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)

void pgtable_cache_init(void);

/*
 * find_linux_pte returns the address of a linux pte for a given 
 * effective address and directory.  If not found, it returns zero.
 */
static inline pte_t *find_linux_pte(pgd_t *pgdir, unsigned long ea)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	pte_t *pt = NULL;
	pte_t pte;

	pg = pgdir + pgd_index(ea);
	if (!pgd_none(*pg)) {
		pu = pud_offset(pg, ea);
		if (!pud_none(*pu)) {
			pm = pmd_offset(pu, ea);
			if (pmd_present(*pm)) {
				pt = pte_offset_kernel(pm, ea);
				pte = *pt;
				if (!pte_present(pte))
					pt = NULL;
			}
		}
	}

	return pt;
}

#include <asm-generic/pgtable.h>

#endif /* __ASSEMBLY__ */

#endif /* _PPC64_PGTABLE_H */
