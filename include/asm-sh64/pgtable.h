#ifndef __ASM_SH64_PGTABLE_H
#define __ASM_SH64_PGTABLE_H

#include <asm-generic/4level-fixup.h>

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/pgtable.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 * Copyright (C) 2003, 2004  Richard Curnow
 *
 * This file contains the functions and defines necessary to modify and use
 * the SuperH page table tree.
 */

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/page.h>
#include <linux/threads.h>

struct vm_area_struct;

extern void paging_init(void);

/* We provide our own get_unmapped_area to avoid cache synonym issue */
#define HAVE_ARCH_UNMAPPED_AREA

/*
 * Basically we have the same two-level (which is the logical three level
 * Linux page table layout folded) page tables as the i386.
 */

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned char empty_zero_page[PAGE_SIZE];
#define ZERO_PAGE(vaddr) (mem_map + MAP_NR(empty_zero_page))

#endif /* !__ASSEMBLY__ */

/*
 * NEFF and NPHYS related defines.
 * FIXME : These need to be model-dependent.  For now this is OK, SH5-101 and SH5-103
 * implement 32 bits effective and 32 bits physical.  But future implementations may
 * extend beyond this.
 */
#define NEFF		32
#define	NEFF_SIGN	(1LL << (NEFF - 1))
#define	NEFF_MASK	(-1LL << NEFF)

#define NPHYS		32
#define	NPHYS_SIGN	(1LL << (NPHYS - 1))
#define	NPHYS_MASK	(-1LL << NPHYS)

/* Typically 2-level is sufficient up to 32 bits of virtual address space, beyond
   that 3-level would be appropriate. */
#if defined(CONFIG_SH64_PGTABLE_2_LEVEL)
/* For 4k pages, this contains 512 entries, i.e. 9 bits worth of address. */
#define PTRS_PER_PTE	((1<<PAGE_SHIFT)/sizeof(unsigned long long))
#define PTE_MAGNITUDE	3	      /* sizeof(unsigned long long) magnit. */
#define PTE_SHIFT	PAGE_SHIFT
#define PTE_BITS	(PAGE_SHIFT - PTE_MAGNITUDE)

/* top level: PMD. */
#define PGDIR_SHIFT	(PTE_SHIFT + PTE_BITS)
#define PGD_BITS	(NEFF - PGDIR_SHIFT)
#define PTRS_PER_PGD	(1<<PGD_BITS)

/* middle level: PMD. This doesn't do anything for the 2-level case. */
#define PTRS_PER_PMD	(1)

#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PMD_SHIFT	PGDIR_SHIFT
#define PMD_SIZE	PGDIR_SIZE
#define PMD_MASK	PGDIR_MASK

#elif defined(CONFIG_SH64_PGTABLE_3_LEVEL)
/*
 * three-level asymmetric paging structure: PGD is top level.
 * The asymmetry comes from 32-bit pointers and 64-bit PTEs.
 */
/* bottom level: PTE. It's 9 bits = 512 pointers */
#define PTRS_PER_PTE	((1<<PAGE_SHIFT)/sizeof(unsigned long long))
#define PTE_MAGNITUDE	3	      /* sizeof(unsigned long long) magnit. */
#define PTE_SHIFT	PAGE_SHIFT
#define PTE_BITS	(PAGE_SHIFT - PTE_MAGNITUDE)

/* middle level: PMD. It's 10 bits = 1024 pointers */
#define PTRS_PER_PMD	((1<<PAGE_SHIFT)/sizeof(unsigned long long *))
#define PMD_MAGNITUDE	2	      /* sizeof(unsigned long long *) magnit. */
#define PMD_SHIFT	(PTE_SHIFT + PTE_BITS)
#define PMD_BITS	(PAGE_SHIFT - PMD_MAGNITUDE)

/* top level: PMD. It's 1 bit = 2 pointers */
#define PGDIR_SHIFT	(PMD_SHIFT + PMD_BITS)
#define PGD_BITS	(NEFF - PGDIR_SHIFT)
#define PTRS_PER_PGD	(1<<PGD_BITS)

#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#else
#error "No defined number of page table levels"
#endif

/*
 * Error outputs.
 */
#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016Lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Table setting routines. Used within arch/mm only.
 */
#define set_pgd(pgdptr, pgdval) (*(pgdptr) = pgdval)
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)

static __inline__ void set_pte(pte_t *pteptr, pte_t pteval)
{
	unsigned long long x = ((unsigned long long) pteval.pte);
	unsigned long long *xp = (unsigned long long *) pteptr;
	/*
	 * Sign-extend based on NPHYS.
	 */
	*(xp) = (x & NPHYS_SIGN) ? (x | NPHYS_MASK) : x;
}
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

static __inline__ void pmd_set(pmd_t *pmdp,pte_t *ptep)
{
	pmd_val(*pmdp) = (unsigned long) ptep;
}

/*
 * PGD defines. Top level.
 */

/* To find an entry in a generic PGD. */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#define __pgd_offset(address) pgd_index(address)
#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/* To find an entry in a kernel PGD. */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/*
 * PGD level access routines.
 *
 * Note1:
 * There's no need to use physical addresses since the tree walk is all
 * in performed in software, until the PTE translation.
 *
 * Note 2:
 * A PGD entry can be uninitialized (_PGD_UNUSED), generically bad,
 * clear (_PGD_EMPTY), present. When present, lower 3 nibbles contain
 * _KERNPG_TABLE. Being a kernel virtual pointer also bit 31 must
 * be 1. Assuming an arbitrary clear value of bit 31 set to 0 and
 * lower 3 nibbles set to 0xFFF (_PGD_EMPTY) any other value is a
 * bad pgd that must be notified via printk().
 *
 */
#define _PGD_EMPTY		0x0

#if defined(CONFIG_SH64_PGTABLE_2_LEVEL)
static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
#define pgd_present(pgd) ((pgd_val(pgd) & _PAGE_PRESENT) ? 1 : 0)
#define pgd_clear(xx)				do { } while(0)

#elif defined(CONFIG_SH64_PGTABLE_3_LEVEL)
#define pgd_present(pgd_entry)	(1)
#define pgd_none(pgd_entry)	(pgd_val((pgd_entry)) == _PGD_EMPTY)
/* TODO: Think later about what a useful definition of 'bad' would be now. */
#define pgd_bad(pgd_entry)	(0)
#define pgd_clear(pgd_entry_p)	(set_pgd((pgd_entry_p), __pgd(_PGD_EMPTY)))

#endif


#define pgd_page(pgd_entry)	((unsigned long) (pgd_val(pgd_entry) & PAGE_MASK))

/*
 * PMD defines. Middle level.
 */

/* PGD to PMD dereferencing */
#if defined(CONFIG_SH64_PGTABLE_2_LEVEL)
static inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}
#elif defined(CONFIG_SH64_PGTABLE_3_LEVEL)
#define __pmd_offset(address) \
		(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))
#define pmd_offset(dir, addr) \
		((pmd_t *) ((pgd_val(*(dir))) & PAGE_MASK) + __pmd_offset((addr)))
#endif

/*
 * PMD level access routines. Same notes as above.
 */
#define _PMD_EMPTY		0x0
/* Either the PMD is empty or present, it's not paged out */
#define pmd_present(pmd_entry)	(pmd_val(pmd_entry) & _PAGE_PRESENT)
#define pmd_clear(pmd_entry_p)	(set_pmd((pmd_entry_p), __pmd(_PMD_EMPTY)))
#define pmd_none(pmd_entry)	(pmd_val((pmd_entry)) == _PMD_EMPTY)
#define pmd_bad(pmd_entry)	((pmd_val(pmd_entry) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)

#define pmd_page_kernel(pmd_entry) \
	((unsigned long) __va(pmd_val(pmd_entry) & PAGE_MASK))

#define pmd_page(pmd) \
	(virt_to_page(pmd_val(pmd)))

/* PMD to PTE dereferencing */
#define pte_index(address) \
		((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

#define pte_offset_kernel(dir, addr) \
		((pte_t *) ((pmd_val(*(dir))) & PAGE_MASK) + pte_index((addr)))

#define pte_offset_map(dir,addr)	pte_offset_kernel(dir, addr)
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel(dir, addr)
#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)	do { } while (0)

/* Round it up ! */
#define USER_PTRS_PER_PGD	((TASK_SIZE+PGDIR_SIZE-1)/PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0

#ifndef __ASSEMBLY__
#define VMALLOC_END	0xff000000
#define VMALLOC_START	0xf0000000
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

#define IOBASE_VADDR	0xff000000
#define IOBASE_END	0xffffffff

/*
 * PTEL coherent flags.
 * See Chapter 17 ST50 CPU Core Volume 1, Architecture.
 */
/* The bits that are required in the SH-5 TLB are placed in the h/w-defined
   positions, to avoid expensive bit shuffling on every refill.  The remaining
   bits are used for s/w purposes and masked out on each refill.

   Note, the PTE slots are used to hold data of type swp_entry_t when a page is
   swapped out.  Only the _PAGE_PRESENT flag is significant when the page is
   swapped out, and it must be placed so that it doesn't overlap either the
   type or offset fields of swp_entry_t.  For x86, offset is at [31:8] and type
   at [6:1], with _PAGE_PRESENT at bit 0 for both pte_t and swp_entry_t.  This
   scheme doesn't map to SH-5 because bit [0] controls cacheability.  So bit
   [2] is used for _PAGE_PRESENT and the type field of swp_entry_t is split
   into 2 pieces.  That is handled by SWP_ENTRY and SWP_TYPE below. */
#define _PAGE_WT	0x001  /* CB0: if cacheable, 1->write-thru, 0->write-back */
#define _PAGE_DEVICE	0x001  /* CB0: if uncacheable, 1->device (i.e. no write-combining or reordering at bus level) */
#define _PAGE_CACHABLE	0x002  /* CB1: uncachable/cachable */
#define _PAGE_PRESENT	0x004  /* software: page referenced */
#define _PAGE_FILE	0x004  /* software: only when !present */
#define _PAGE_SIZE0	0x008  /* SZ0-bit : size of page */
#define _PAGE_SIZE1	0x010  /* SZ1-bit : size of page */
#define _PAGE_SHARED	0x020  /* software: reflects PTEH's SH */
#define _PAGE_READ	0x040  /* PR0-bit : read access allowed */
#define _PAGE_EXECUTE	0x080  /* PR1-bit : execute access allowed */
#define _PAGE_WRITE	0x100  /* PR2-bit : write access allowed */
#define _PAGE_USER	0x200  /* PR3-bit : user space access allowed */
#define _PAGE_DIRTY	0x400  /* software: page accessed in write */
#define _PAGE_ACCESSED	0x800  /* software: page referenced */

/* Mask which drops software flags */
#define _PAGE_FLAGS_HARDWARE_MASK	0xfffffffffffff3dbLL

/*
 * HugeTLB support
 */
#if defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define _PAGE_SZHUGE	(_PAGE_SIZE0)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1MB)
#define _PAGE_SZHUGE	(_PAGE_SIZE1)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512MB)
#define _PAGE_SZHUGE	(_PAGE_SIZE0 | _PAGE_SIZE1)
#endif

/*
 * Default flags for a Kernel page.
 * This is fundametally also SHARED because the main use of this define
 * (other than for PGD/PMD entries) is for the VMALLOC pool which is
 * contextless.
 *
 * _PAGE_EXECUTE is required for modules
 *
 */
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			 _PAGE_EXECUTE | \
			 _PAGE_CACHABLE | _PAGE_ACCESSED | _PAGE_DIRTY | \
			 _PAGE_SHARED)

/* Default flags for a User page */
#define _PAGE_TABLE	(_KERNPG_TABLE | _PAGE_USER)

#define _PAGE_CHG_MASK	(PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_CACHABLE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				 _PAGE_CACHABLE | _PAGE_ACCESSED | _PAGE_USER | \
				 _PAGE_SHARED)
/* We need to include PAGE_EXECUTE in PAGE_COPY because it is the default
 * protection mode for the stack. */
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_CACHABLE | \
				 _PAGE_ACCESSED | _PAGE_USER | _PAGE_EXECUTE)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_CACHABLE | \
				 _PAGE_ACCESSED | _PAGE_USER)
#define PAGE_KERNEL	__pgprot(_KERNPG_TABLE)


/*
 * In ST50 we have full permissions (Read/Write/Execute/Shared).
 * Just match'em all. These are for mmap(), therefore all at least
 * User/Cachable/Present/Accessed. No point in making Fault on Write.
 */
#define __MMAP_COMMON	(_PAGE_PRESENT | _PAGE_USER | _PAGE_CACHABLE | _PAGE_ACCESSED)
       /* sxwr */
#define __P000	__pgprot(__MMAP_COMMON)
#define __P001	__pgprot(__MMAP_COMMON | _PAGE_READ)
#define __P010	__pgprot(__MMAP_COMMON)
#define __P011	__pgprot(__MMAP_COMMON | _PAGE_READ)
#define __P100	__pgprot(__MMAP_COMMON | _PAGE_EXECUTE)
#define __P101	__pgprot(__MMAP_COMMON | _PAGE_EXECUTE | _PAGE_READ)
#define __P110	__pgprot(__MMAP_COMMON | _PAGE_EXECUTE)
#define __P111	__pgprot(__MMAP_COMMON | _PAGE_EXECUTE | _PAGE_READ)

#define __S000	__pgprot(__MMAP_COMMON | _PAGE_SHARED)
#define __S001	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_READ)
#define __S010	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_WRITE)
#define __S011	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_READ | _PAGE_WRITE)
#define __S100	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_EXECUTE)
#define __S101	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_EXECUTE | _PAGE_READ)
#define __S110	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_EXECUTE | _PAGE_WRITE)
#define __S111	__pgprot(__MMAP_COMMON | _PAGE_SHARED | _PAGE_EXECUTE | _PAGE_READ | _PAGE_WRITE)

/* Make it a device mapping for maximum safety (e.g. for mapping device
   registers into user-space via /dev/map).  */
#define pgprot_noncached(x) __pgprot(((x).pgprot & ~(_PAGE_CACHABLE)) | _PAGE_DEVICE)
#define pgprot_writecombine(prot) __pgprot(pgprot_val(prot) & ~_PAGE_CACHABLE)

/*
 * Handling allocation failures during page table setup.
 */
extern void __handle_bad_pmd_kernel(pmd_t * pmd);
#define __handle_bad_pmd(x)	__handle_bad_pmd_kernel(x)

/*
 * PTE level access routines.
 *
 * Note1:
 * It's the tree walk leaf. This is physical address to be stored.
 *
 * Note 2:
 * Regarding the choice of _PTE_EMPTY:

   We must choose a bit pattern that cannot be valid, whether or not the page
   is present.  bit[2]==1 => present, bit[2]==0 => swapped out.  If swapped
   out, bits [31:8], [6:3], [1:0] are under swapper control, so only bit[7] is
   left for us to select.  If we force bit[7]==0 when swapped out, we could use
   the combination bit[7,2]=2'b10 to indicate an empty PTE.  Alternatively, if
   we force bit[7]==1 when swapped out, we can use all zeroes to indicate
   empty.  This is convenient, because the page tables get cleared to zero
   when they are allocated.

 */
#define _PTE_EMPTY	0x0
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm,addr,xp)	(set_pte_at(mm, addr, xp, __pte(_PTE_EMPTY)))
#define pte_none(x)	(pte_val(x) == _PTE_EMPTY)

/*
 * Some definitions to translate between mem_map, PTEs, and page
 * addresses:
 */

/*
 * Given a PTE, return the index of the mem_map[] entry corresponding
 * to the page frame the PTE. Get the absolute physical address, make
 * a relative physical address and translate it to an index.
 */
#define pte_pagenr(x)		(((unsigned long) (pte_val(x)) - \
				 __MEMORY_START) >> PAGE_SHIFT)

/*
 * Given a PTE, return the "struct page *".
 */
#define pte_page(x)		(mem_map + pte_pagenr(x))

/*
 * Return number of (down rounded) MB corresponding to x pages.
 */
#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))


/*
 * The following have defined behavior only work if pte_present() is true.
 */
static inline int pte_read(pte_t pte) { return pte_val(pte) & _PAGE_READ; }
static inline int pte_exec(pte_t pte) { return pte_val(pte) & _PAGE_EXECUTE; }
static inline int pte_dirty(pte_t pte){ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte){ return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_file(pte_t pte) { return pte_val(pte) & _PAGE_FILE; }
static inline int pte_write(pte_t pte){ return pte_val(pte) & _PAGE_WRITE; }

static inline pte_t pte_rdprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_READ)); return pte; }
static inline pte_t pte_wrprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_WRITE)); return pte; }
static inline pte_t pte_exprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_EXECUTE)); return pte; }
static inline pte_t pte_mkclean(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_DIRTY)); return pte; }
static inline pte_t pte_mkold(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_ACCESSED)); return pte; }

static inline pte_t pte_mkread(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_READ)); return pte; }
static inline pte_t pte_mkwrite(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_WRITE)); return pte; }
static inline pte_t pte_mkexec(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_EXECUTE)); return pte; }
static inline pte_t pte_mkdirty(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_DIRTY)); return pte; }
static inline pte_t pte_mkyoung(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_ACCESSED)); return pte; }
static inline pte_t pte_mkhuge(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_SZHUGE)); return pte; }


/*
 * Conversion functions: convert a page and protection to a page entry.
 *
 * extern pte_t mk_pte(struct page *page, pgprot_t pgprot)
 */
#define mk_pte(page,pgprot)							\
({										\
	pte_t __pte;								\
										\
	set_pte(&__pte, __pte((((page)-mem_map) << PAGE_SHIFT) | 		\
		__MEMORY_START | pgprot_val((pgprot))));			\
	__pte;									\
})

/*
 * This takes a (absolute) physical page address that is used
 * by the remapping functions
 */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; set_pte(&__pte, __pte(physpage | pgprot_val(pgprot))); __pte; })

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ set_pte(&pte, __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot))); return pte; }

typedef pte_t *pte_addr_t;
#define pgtable_cache_init()	do { } while (0)

extern void update_mmu_cache(struct vm_area_struct * vma,
			     unsigned long address, pte_t pte);

/* Encode and decode a swap entry */
#define __swp_type(x)			(((x).val & 3) + (((x).val >> 1) & 0x3c))
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((offset << 8) + ((type & 0x3c) << 1) + (type & 3)) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

/* Encode and decode a nonlinear file mapping entry */
#define PTE_FILE_MAX_BITS		29
#define pte_to_pgoff(pte)		(pte_val(pte))
#define pgoff_to_pte(off)		((pte_t) { (off) | _PAGE_FILE })

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)
#define kern_addr_valid(addr)	(1)

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)

#define MK_IOSPACE_PFN(space, pfn)	(pfn)
#define GET_IOSPACE(pfn)		0
#define GET_PFN(pfn)			(pfn)

#endif /* !__ASSEMBLY__ */

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()    do { } while (0)

#define pte_pfn(x)		(((unsigned long)((x).pte)) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#include <asm-generic/pgtable.h>

#endif /* __ASM_SH64_PGTABLE_H */
