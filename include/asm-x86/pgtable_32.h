#ifndef _I386_PGTABLE_H
#define _I386_PGTABLE_H


/*
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <linux/threads.h>
#include <asm/paravirt.h>

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct mm_struct;
struct vm_area_struct;

extern pgd_t swapper_pg_dir[1024];
extern struct kmem_cache *pmd_cache;
void check_pgt_cache(void);

static inline void pgtable_cache_init(void) {}
void paging_init(void);


/*
 * The Linux x86 paging architecture is 'compile-time dual-mode', it
 * implements both the traditional 2-level x86 page tables and the
 * newer 3-level PAE-mode page tables.
 */
#ifdef CONFIG_X86_PAE
# include <asm/pgtable-3level-defs.h>
# define PMD_SIZE	(1UL << PMD_SHIFT)
# define PMD_MASK	(~(PMD_SIZE-1))
#else
# include <asm/pgtable-2level-defs.h>
#endif

#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(8*1024*1024)
#define VMALLOC_START	(((unsigned long) high_memory + \
			2*VMALLOC_OFFSET-1) & ~(VMALLOC_OFFSET-1))
#ifdef CONFIG_X86_PAE
#define LAST_PKMAP 512
#else
#define LAST_PKMAP 1024
#endif

#define PKMAP_BASE ((FIXADDR_BOOT_START - PAGE_SIZE*(LAST_PKMAP + 1)) & PMD_MASK)

#ifdef CONFIG_HIGHMEM
# define VMALLOC_END	(PKMAP_BASE-2*PAGE_SIZE)
#else
# define VMALLOC_END	(FIXADDR_START-2*PAGE_SIZE)
#endif

/*
 * Define this if things work differently on an i386 and an i486:
 * it will (on an i486) warn about kernel memory accesses that are
 * done without a 'access_ok(VERIFY_WRITE,..)'
 */
#undef TEST_ACCESS_OK

/* The boot page tables (all created as a single array) */
extern unsigned long pg0[];

#define pte_present(x)	((x).pte_low & (_PAGE_PRESENT | _PAGE_PROTNONE))

/* To avoid harmful races, pmd_none(x) should check only the lower when PAE */
#define pmd_none(x)	(!(unsigned long)pmd_val(x))
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)


#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))

#ifdef CONFIG_X86_PAE
# include <asm/pgtable-3level.h>
#else
# include <asm/pgtable-2level.h>
#endif

/*
 * clone_pgd_range(pgd_t *dst, pgd_t *src, int count);
 *
 *  dst - pointer to pgd range anwhere on a pgd page
 *  src - ""
 *  count - the number of pgds to copy.
 *
 * dst and src can be on the same page, but the range must not overlap,
 * and must not cross a page boundary.
 */
static inline void clone_pgd_range(pgd_t *dst, pgd_t *src, int count)
{
       memcpy(dst, src, count * sizeof(pgd_t));
}

/*
 * Macro to mark a page protection value as "uncacheable".  On processors which do not support
 * it, this is a no-op.
 */
#define pgprot_noncached(prot)	((boot_cpu_data.x86 > 3)					  \
				 ? (__pgprot(pgprot_val(prot) | _PAGE_PCD | _PAGE_PWT)) : (prot))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

/*
 * the pgd page can be thought of an array like this: pgd_t[PTRS_PER_PGD]
 *
 * this macro returns the index of the entry in the pgd page which would
 * control the given virtual address
 */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#define pgd_index_k(addr) pgd_index(addr)

/*
 * pgd_offset() returns a (pgd_t *)
 * pgd_index() is used get the offset into the pgd page's array of pgd_t's;
 */
#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/*
 * a shortcut which implies the use of the kernel's pgd, instead
 * of a process's
 */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

static inline int pud_large(pud_t pud) { return 0; }

/*
 * the pmd page can be thought of an array like this: pmd_t[PTRS_PER_PMD]
 *
 * this macro returns the index of the entry in the pmd page which would
 * control the given virtual address
 */
#define pmd_index(address) \
		(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/*
 * the pte page can be thought of an array like this: pte_t[PTRS_PER_PTE]
 *
 * this macro returns the index of the entry in the pte page which would
 * control the given virtual address
 */
#define pte_index(address) \
		(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_vaddr(*(dir)) +  pte_index(address))

#define pmd_page(pmd) (pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))

#define pmd_page_vaddr(pmd) \
		((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

#if defined(CONFIG_HIGHPTE)
#define pte_offset_map(dir, address) \
	((pte_t *)kmap_atomic_pte(pmd_page(*(dir)),KM_PTE0) + pte_index(address))
#define pte_offset_map_nested(dir, address) \
	((pte_t *)kmap_atomic_pte(pmd_page(*(dir)),KM_PTE1) + pte_index(address))
#define pte_unmap(pte) kunmap_atomic(pte, KM_PTE0)
#define pte_unmap_nested(pte) kunmap_atomic(pte, KM_PTE1)
#else
#define pte_offset_map(dir, address) \
	((pte_t *)page_address(pmd_page(*(dir))) + pte_index(address))
#define pte_offset_map_nested(dir, address) pte_offset_map(dir, address)
#define pte_unmap(pte) do { } while (0)
#define pte_unmap_nested(pte) do { } while (0)
#endif

/* Clear a kernel PTE and flush it from the TLB */
#define kpte_clear_flush(ptep, vaddr)					\
do {									\
	pte_clear(&init_mm, vaddr, ptep);				\
	__flush_tlb_one(vaddr);						\
} while (0)

/*
 * The i386 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
#define update_mmu_cache(vma,address,pte) do { } while (0)

void native_pagetable_setup_start(pgd_t *base);
void native_pagetable_setup_done(pgd_t *base);

#ifndef CONFIG_PARAVIRT
static inline void paravirt_pagetable_setup_start(pgd_t *base)
{
	native_pagetable_setup_start(base);
}

static inline void paravirt_pagetable_setup_done(pgd_t *base)
{
	native_pagetable_setup_done(base);
}
#endif	/* !CONFIG_PARAVIRT */

#endif /* !__ASSEMBLY__ */

/*
 * kern_addr_valid() is (1) for FLATMEM and (0) for
 * SPARSEMEM and DISCONTIGMEM
 */
#ifdef CONFIG_FLATMEM
#define kern_addr_valid(addr)	(1)
#else
#define kern_addr_valid(kaddr)	(0)
#endif

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)

#endif /* _I386_PGTABLE_H */
