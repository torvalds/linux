/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGTABLE_32_H
#define _ASM_PGTABLE_32_H

#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/page.h>

#include <linux/linkage.h>
#include <asm/cachectl.h>
#include <asm/fixmap.h>

/*
 * - add_wired_entry() add a fixed TLB entry, and move wired register
 */
extern void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
			       unsigned long entryhi, unsigned long pagemask);

/*
 * - add_temporary_entry() add a temporary TLB entry. We use TLB entries
 *	starting at the top and working down. This is for populating the
 *	TLB before trap_init() puts the TLB miss handler in place. It
 *	should be used only for entries matching the actual page tables,
 *	to prevent inconsistencies.
 */
extern int add_temporary_entry(unsigned long entrylo0, unsigned long entrylo1,
			       unsigned long entryhi, unsigned long pagemask);


/* Basically we have the same two-level (which is the logical three level
 * Linux page table layout folded) page tables as the i386.  Some day
 * when we have proper page coloring support we can have a 1% quicker
 * tlb refill handling mechanism, but for now it is a bit slower but
 * works even with the cache aliasing problem the R4k and above have.
 */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#ifdef CONFIG_64BIT_PHYS_ADDR
#define PMD_SHIFT	21
#else
#define PMD_SHIFT	22
#endif
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	PMD_SHIFT
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use two-level, so
 * we don't really have any PMD directory physically.
 */
#ifdef CONFIG_64BIT_PHYS_ADDR
#define PGD_ORDER	1
#define PMD_ORDER	0
#define PTE_ORDER	0
#else
#define PGD_ORDER	0
#define PMD_ORDER	0
#define PTE_ORDER	0
#endif

#define PTRS_PER_PGD	((PAGE_SIZE << PGD_ORDER) / sizeof(pgd_t))
#define PTRS_PER_PMD	1
#define PTRS_PER_PTE	((PAGE_SIZE << PTE_ORDER) / sizeof(pte_t))

#define USER_PTRS_PER_PGD	(0x80000000UL/PGDIR_SIZE)
#define FIRST_USER_PGD_NR	0

#define VMALLOC_START     KSEG2

#ifdef CONFIG_HIGHMEM
# define VMALLOC_END	(PKMAP_BASE-2*PAGE_SIZE)
#else
# define VMALLOC_END	(FIXADDR_START-2*PAGE_SIZE)
#endif

#ifdef CONFIG_64BIT_PHYS_ADDR
#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016Lx.\n", __FILE__, __LINE__, pte_val(e))
#else
#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#endif
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

extern void load_pgd(unsigned long pg_dir);

extern pte_t invalid_pte_table[PAGE_SIZE/sizeof(pte_t)];

/*
 * Empty pgd/pmd entries point to the invalid_pte_table.
 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == (unsigned long) invalid_pte_table;
}

#define pmd_bad(pmd)		(pmd_val(pmd) & ~PAGE_MASK)

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != (unsigned long) invalid_pte_table;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = ((unsigned long) invalid_pte_table);
}

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
static inline void pgd_clear(pgd_t *pgdp)	{ }

#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_CPU_MIPS32)
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pte_pfn(x)		((unsigned long)((x).pte_high >> 6))
static inline pte_t
pfn_pte(unsigned long pfn, pgprot_t prot)
{
	pte_t pte;
	pte.pte_high = (pfn << 6) | (pgprot_val(prot) & 0x3f);
	pte.pte_low = pgprot_val(prot);
	return pte;
}

#else

#define pte_page(x)		pfn_to_page(pte_pfn(x))

#ifdef CONFIG_CPU_VR41XX
#define pte_pfn(x)		((unsigned long)((x).pte >> (PAGE_SHIFT + 2)))
#define pfn_pte(pfn, prot)	__pte(((pfn) << (PAGE_SHIFT + 2)) | pgprot_val(prot))
#else
#define pte_pfn(x)		((unsigned long)((x).pte >> PAGE_SHIFT))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#endif
#endif /* defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_CPU_MIPS32) */

#define __pgd_offset(address)	pgd_index(address)
#define __pmd_offset(address)	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define pgd_index(address)	((address) >> PGDIR_SHIFT)

/* to find an entry in a page-table-directory */
#define pgd_offset(mm,addr)	((mm)->pgd + pgd_index(addr))

/* Find an entry in the second-level page table.. */
static inline pmd_t *pmd_offset(pgd_t *dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */
#define __pte_offset(address)						\
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(dir, address)					\
	((pte_t *) (pmd_page_kernel(*dir)) + __pte_offset(address))
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_kernel(*(dir)) +  __pte_offset(address))

#define pte_offset_map(dir, address)                                    \
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))
#define pte_offset_map_nested(dir, address)                             \
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))
#define pte_unmap(pte) ((void)(pte))
#define pte_unmap_nested(pte) ((void)(pte))

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

/* Swap entries must have VALID bit cleared. */
#define __swp_type(x)		(((x).val >> 10) & 0x1f)
#define __swp_offset(x)		((x).val >> 15)
#define __swp_entry(type,offset)	\
	((swp_entry_t) { ((type) << 10) | ((offset) << 15) })

/*
 * Bits 0, 1, 2, 9 and 10 are taken, split up the 27 bits of offset
 * into this range:
 */
#define PTE_FILE_MAX_BITS	27

#define pte_to_pgoff(_pte) \
	((((_pte).pte >> 3) & 0x3f ) + (((_pte).pte >> 11) << 8 ))

#define pgoff_to_pte(off) \
	((pte_t) { (((off) & 0x3f) << 3) + (((off) >> 8) << 11) + _PAGE_FILE })

#else

/* Swap entries must have VALID and GLOBAL bits cleared. */
#define __swp_type(x)		(((x).val >> 8) & 0x1f)
#define __swp_offset(x)		((x).val >> 13)
#define __swp_entry(type,offset)	\
		((swp_entry_t) { ((type) << 8) | ((offset) << 13) })

/*
 * Bits 0, 1, 2, 7 and 8 are taken, split up the 27 bits of offset
 * into this range:
 */
#define PTE_FILE_MAX_BITS	27

#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_CPU_MIPS32)
	/* fixme */
#define pte_to_pgoff(_pte) (((_pte).pte_high >> 6) + ((_pte).pte_high & 0x3f))
#define pgoff_to_pte(off) \
 	((pte_t){(((off) & 0x3f) + ((off) << 6) + _PAGE_FILE)})

#else
#define pte_to_pgoff(_pte) \
	((((_pte).pte >> 3) & 0x1f ) + (((_pte).pte >> 9) << 6 ))

#define pgoff_to_pte(off) \
	((pte_t) { (((off) & 0x1f) << 3) + (((off) >> 6) << 9) + _PAGE_FILE })
#endif

#endif

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#endif /* _ASM_PGTABLE_32_H */
