#ifdef __KERNEL__
#ifndef _PPC_PGTABLE_H
#define _PPC_PGTABLE_H

#include <asm-generic/4level-fixup.h>


#ifndef __ASSEMBLY__
#include <linux/sched.h>
#include <linux/threads.h>
#include <asm/processor.h>		/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/io.h>			/* For sub-arch specific PPC_PIN_SIZE */
struct mm_struct;

extern unsigned long va_to_phys(unsigned long address);
extern pte_t *va_to_pte(unsigned long address);
extern unsigned long ioremap_bot, ioremap_base;
#endif /* __ASSEMBLY__ */

/*
 * The PowerPC MMU uses a hash table containing PTEs, together with
 * a set of 16 segment registers (on 32-bit implementations), to define
 * the virtual to physical address mapping.
 *
 * We use the hash table as an extended TLB, i.e. a cache of currently
 * active mappings.  We maintain a two-level page table tree, much
 * like that used by the i386, for the sake of the Linux memory
 * management code.  Low-level assembler code in hashtable.S
 * (procedure hash_page) is responsible for extracting ptes from the
 * tree and putting them into the hash table when necessary, and
 * updating the accessed and modified bits in the page table tree.
 */

/*
 * The PowerPC MPC8xx uses a TLB with hardware assisted, software tablewalk.
 * We also use the two level tables, but we can put the real bits in them
 * needed for the TLB and tablewalk.  These definitions require Mx_CTR.PPM = 0,
 * Mx_CTR.PPCS = 0, and MD_CTR.TWAM = 1.  The level 2 descriptor has
 * additional page protection (when Mx_CTR.PPCS = 1) that allows TLB hit
 * based upon user/super access.  The TLB does not have accessed nor write
 * protect.  We assume that if the TLB get loaded with an entry it is
 * accessed, and overload the changed bit for write protect.  We use
 * two bits in the software pte that are supposed to be set to zero in
 * the TLB entry (24 and 25) for these indicators.  Although the level 1
 * descriptor contains the guarded and writethrough/copyback bits, we can
 * set these at the page level since they get copied from the Mx_TWC
 * register when the TLB entry is loaded.  We will use bit 27 for guard, since
 * that is where it exists in the MD_TWC, and bit 26 for writethrough.
 * These will get masked from the level 2 descriptor at TLB load time, and
 * copied to the MD_TWC before it gets loaded.
 * Large page sizes added.  We currently support two sizes, 4K and 8M.
 * This also allows a TLB hander optimization because we can directly
 * load the PMD into MD_TWC.  The 8M pages are only used for kernel
 * mapping of well known areas.  The PMD (PGD) entries contain control
 * flags in addition to the address, so care must be taken that the
 * software no longer assumes these are only pointers.
 */

/*
 * At present, all PowerPC 400-class processors share a similar TLB
 * architecture. The instruction and data sides share a unified,
 * 64-entry, fully-associative TLB which is maintained totally under
 * software control. In addition, the instruction side has a
 * hardware-managed, 4-entry, fully-associative TLB which serves as a
 * first level to the shared TLB. These two TLBs are known as the UTLB
 * and ITLB, respectively (see "mmu.h" for definitions).
 */

/*
 * The normal case is that PTEs are 32-bits and we have a 1-page
 * 1024-entry pgdir pointing to 1-page 1024-entry PTE pages.  -- paulus
 *
 * For any >32-bit physical address platform, we can use the following
 * two level page table layout where the pgdir is 8KB and the MS 13 bits
 * are an index to the second level table.  The combined pgdir/pmd first
 * level has 2048 entries and the second level has 512 64-bit PTE entries.
 * -Matt
 */
/* PMD_SHIFT determines the size of the area mapped by the PTE pages */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_SHIFT)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a top-level page table entry can map */
#define PGDIR_SHIFT	PMD_SHIFT
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: our page-table tree is two-level, so
 * we don't really have any PMD directory.
 */
#define PTRS_PER_PTE	(1 << PTE_SHIFT)
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	(1 << (32 - PGDIR_SHIFT))

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte "PTE_FMT".\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 64MB value just means that there will be a 64MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 *
 * We no longer map larger than phys RAM with the BATs so we don't have
 * to worry about the VMALLOC_OFFSET causing problems.  We do have to worry
 * about clashes between our early calls to ioremap() that start growing down
 * from ioremap_base being run into the VM area allocations (growing upwards
 * from VMALLOC_START).  For this reason we have ioremap_bot to check when
 * we actually run into our mappings setup in the early boot with the VM
 * system.  This really does become a problem for machines with good amounts
 * of RAM.  -- Cort
 */
#define VMALLOC_OFFSET (0x1000000) /* 16M */
#ifdef PPC_PIN_SIZE
#define VMALLOC_START (((_ALIGN((long)high_memory, PPC_PIN_SIZE) + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#else
#define VMALLOC_START ((((long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#endif
#define VMALLOC_END	ioremap_bot

/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */

#if defined(CONFIG_40x)

/* There are several potential gotchas here.  The 40x hardware TLBLO
   field looks like this:

   0  1  2  3  4  ... 18 19 20 21 22 23 24 25 26 27 28 29 30 31
   RPN.....................  0  0 EX WR ZSEL.......  W  I  M  G

   Where possible we make the Linux PTE bits match up with this

   - bits 20 and 21 must be cleared, because we use 4k pages (40x can
     support down to 1k pages), this is done in the TLBMiss exception
     handler.
   - We use only zones 0 (for kernel pages) and 1 (for user pages)
     of the 16 available.  Bit 24-26 of the TLB are cleared in the TLB
     miss handler.  Bit 27 is PAGE_USER, thus selecting the correct
     zone.
   - PRESENT *must* be in the bottom two bits because swap cache
     entries use the top 30 bits.  Because 40x doesn't support SMP
     anyway, M is irrelevant so we borrow it for PAGE_PRESENT.  Bit 30
     is cleared in the TLB miss handler before the TLB entry is loaded.
   - All other bits of the PTE are loaded into TLBLO without
     modification, leaving us only the bits 20, 21, 24, 25, 26, 30 for
     software PTE bits.  We actually use use bits 21, 24, 25, and
     30 respectively for the software bits: ACCESSED, DIRTY, RW, and
     PRESENT.
*/

/* Definitions for 40x embedded chips. */
#define	_PAGE_GUARDED	0x001	/* G: page is guarded from prefetch */
#define _PAGE_FILE	0x001	/* when !present: nonlinear file mapping */
#define _PAGE_PRESENT	0x002	/* software: PTE contains a translation */
#define	_PAGE_NO_CACHE	0x004	/* I: caching is inhibited */
#define	_PAGE_WRITETHRU	0x008	/* W: caching is write-through */
#define	_PAGE_USER	0x010	/* matches one of the zone permission bits */
#define	_PAGE_RW	0x040	/* software: Writes permitted */
#define	_PAGE_DIRTY	0x080	/* software: dirty page */
#define _PAGE_HWWRITE	0x100	/* hardware: Dirty & RW, set in exception */
#define _PAGE_HWEXEC	0x200	/* hardware: EX permission */
#define _PAGE_ACCESSED	0x400	/* software: R: page referenced */

#define _PMD_PRESENT	0x400	/* PMD points to page of PTEs */
#define _PMD_BAD	0x802
#define _PMD_SIZE	0x0e0	/* size field, != 0 for large-page PMD entry */
#define _PMD_SIZE_4M	0x0c0
#define _PMD_SIZE_16M	0x0e0
#define PMD_PAGE_SIZE(pmdval)	(1024 << (((pmdval) & _PMD_SIZE) >> 4))

#elif defined(CONFIG_44x)
/*
 * Definitions for PPC440
 *
 * Because of the 3 word TLB entries to support 36-bit addressing,
 * the attribute are difficult to map in such a fashion that they
 * are easily loaded during exception processing.  I decided to
 * organize the entry so the ERPN is the only portion in the
 * upper word of the PTE and the attribute bits below are packed
 * in as sensibly as they can be in the area below a 4KB page size
 * oriented RPN.  This at least makes it easy to load the RPN and
 * ERPN fields in the TLB. -Matt
 *
 * Note that these bits preclude future use of a page size
 * less than 4KB.
 *
 *
 * PPC 440 core has following TLB attribute fields;
 *
 *   TLB1:
 *   0  1  2  3  4  ... 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
 *   RPN.................................  -  -  -  -  -  - ERPN.......
 *
 *   TLB2:
 *   0  1  2  3  4  ... 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
 *   -  -  -  -  -    - U0 U1 U2 U3 W  I  M  G  E   - UX UW UR SX SW SR
 *
 * There are some constrains and options, to decide mapping software bits
 * into TLB entry.
 *
 *   - PRESENT *must* be in the bottom three bits because swap cache
 *     entries use the top 29 bits for TLB2.
 *
 *   - FILE *must* be in the bottom three bits because swap cache
 *     entries use the top 29 bits for TLB2.
 *
 *   - CACHE COHERENT bit (M) has no effect on PPC440 core, because it
 *     doesn't support SMP. So we can use this as software bit, like
 *     DIRTY.
 *
 * With the PPC 44x Linux implementation, the 0-11th LSBs of the PTE are used
 * for memory protection related functions (see PTE structure in
 * include/asm-ppc/mmu.h).  The _PAGE_XXX definitions in this file map to the
 * above bits.  Note that the bit values are CPU specific, not architecture
 * specific.
 *
 * The kernel PTE entry holds an arch-dependent swp_entry structure under
 * certain situations. In other words, in such situations some portion of
 * the PTE bits are used as a swp_entry. In the PPC implementation, the
 * 3-24th LSB are shared with swp_entry, however the 0-2nd three LSB still
 * hold protection values. That means the three protection bits are
 * reserved for both PTE and SWAP entry at the most significant three
 * LSBs.
 *
 * There are three protection bits available for SWAP entry:
 *	_PAGE_PRESENT
 *	_PAGE_FILE
 *	_PAGE_HASHPTE (if HW has)
 *
 * So those three bits have to be inside of 0-2nd LSB of PTE.
 *
 */

#define _PAGE_PRESENT	0x00000001		/* S: PTE valid */
#define	_PAGE_RW	0x00000002		/* S: Write permission */
#define _PAGE_FILE	0x00000004		/* S: nonlinear file mapping */
#define _PAGE_ACCESSED	0x00000008		/* S: Page referenced */
#define _PAGE_HWWRITE	0x00000010		/* H: Dirty & RW */
#define _PAGE_HWEXEC	0x00000020		/* H: Execute permission */
#define	_PAGE_USER	0x00000040		/* S: User page */
#define	_PAGE_ENDIAN	0x00000080		/* H: E bit */
#define	_PAGE_GUARDED	0x00000100		/* H: G bit */
#define	_PAGE_DIRTY	0x00000200		/* S: Page dirty */
#define	_PAGE_NO_CACHE	0x00000400		/* H: I bit */
#define	_PAGE_WRITETHRU	0x00000800		/* H: W bit */

/* TODO: Add large page lowmem mapping support */
#define _PMD_PRESENT	0
#define _PMD_PRESENT_MASK (PAGE_MASK)
#define _PMD_BAD	(~PAGE_MASK)

/* ERPN in a PTE never gets cleared, ignore it */
#define _PTE_NONE_MASK	0xffffffff00000000ULL

#elif defined(CONFIG_FSL_BOOKE)
/*
   MMU Assist Register 3:

   32 33 34 35 36  ... 50 51 52 53 54 55 56 57 58 59 60 61 62 63
   RPN......................  0  0 U0 U1 U2 U3 UX SX UW SW UR SR

   - PRESENT *must* be in the bottom three bits because swap cache
     entries use the top 29 bits.

   - FILE *must* be in the bottom three bits because swap cache
     entries use the top 29 bits.
*/

/* Definitions for FSL Book-E Cores */
#define _PAGE_PRESENT	0x00001	/* S: PTE contains a translation */
#define _PAGE_USER	0x00002	/* S: User page (maps to UR) */
#define _PAGE_FILE	0x00002	/* S: when !present: nonlinear file mapping */
#define _PAGE_ACCESSED	0x00004	/* S: Page referenced */
#define _PAGE_HWWRITE	0x00008	/* H: Dirty & RW, set in exception */
#define _PAGE_RW	0x00010	/* S: Write permission */
#define _PAGE_HWEXEC	0x00020	/* H: UX permission */

#define _PAGE_ENDIAN	0x00040	/* H: E bit */
#define _PAGE_GUARDED	0x00080	/* H: G bit */
#define _PAGE_COHERENT	0x00100	/* H: M bit */
#define _PAGE_NO_CACHE	0x00200	/* H: I bit */
#define _PAGE_WRITETHRU	0x00400	/* H: W bit */

#ifdef CONFIG_PTE_64BIT
#define _PAGE_DIRTY	0x08000	/* S: Page dirty */

/* ERPN in a PTE never gets cleared, ignore it */
#define _PTE_NONE_MASK	0xffffffffffff0000ULL
#else
#define _PAGE_DIRTY	0x00800	/* S: Page dirty */
#endif

#define _PMD_PRESENT	0
#define _PMD_PRESENT_MASK (PAGE_MASK)
#define _PMD_BAD	(~PAGE_MASK)

#elif defined(CONFIG_8xx)
/* Definitions for 8xx embedded chips. */
#define _PAGE_PRESENT	0x0001	/* Page is valid */
#define _PAGE_FILE	0x0002	/* when !present: nonlinear file mapping */
#define _PAGE_NO_CACHE	0x0002	/* I: cache inhibit */
#define _PAGE_SHARED	0x0004	/* No ASID (context) compare */

/* These five software bits must be masked out when the entry is loaded
 * into the TLB.
 */
#define _PAGE_EXEC	0x0008	/* software: i-cache coherency required */
#define _PAGE_GUARDED	0x0010	/* software: guarded access */
#define _PAGE_DIRTY	0x0020	/* software: page changed */
#define _PAGE_RW	0x0040	/* software: user write access allowed */
#define _PAGE_ACCESSED	0x0080	/* software: page referenced */

/* Setting any bits in the nibble with the follow two controls will
 * require a TLB exception handler change.  It is assumed unused bits
 * are always zero.
 */
#define _PAGE_HWWRITE	0x0100	/* h/w write enable: never set in Linux PTE */
#define _PAGE_USER	0x0800	/* One of the PP bits, the other is USER&~RW */

#define _PMD_PRESENT	0x0001
#define _PMD_BAD	0x0ff0
#define _PMD_PAGE_MASK	0x000c
#define _PMD_PAGE_8M	0x000c

/*
 * The 8xx TLB miss handler allegedly sets _PAGE_ACCESSED in the PTE
 * for an address even if _PAGE_PRESENT is not set, as a performance
 * optimization.  This is a bug if you ever want to use swap unless
 * _PAGE_ACCESSED is 2, which it isn't, or unless you have 8xx-specific
 * definitions for __swp_entry etc. below, which would be gross.
 *  -- paulus
 */
#define _PTE_NONE_MASK _PAGE_ACCESSED

#else /* CONFIG_6xx */
/* Definitions for 60x, 740/750, etc. */
#define _PAGE_PRESENT	0x001	/* software: pte contains a translation */
#define _PAGE_HASHPTE	0x002	/* hash_page has made an HPTE for this pte */
#define _PAGE_FILE	0x004	/* when !present: nonlinear file mapping */
#define _PAGE_USER	0x004	/* usermode access allowed */
#define _PAGE_GUARDED	0x008	/* G: prohibit speculative access */
#define _PAGE_COHERENT	0x010	/* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x020	/* I: cache inhibit */
#define _PAGE_WRITETHRU	0x040	/* W: cache write-through */
#define _PAGE_DIRTY	0x080	/* C: page changed */
#define _PAGE_ACCESSED	0x100	/* R: page referenced */
#define _PAGE_EXEC	0x200	/* software: i-cache coherency required */
#define _PAGE_RW	0x400	/* software: user write access allowed */

#define _PTE_NONE_MASK	_PAGE_HASHPTE

#define _PMD_PRESENT	0
#define _PMD_PRESENT_MASK (PAGE_MASK)
#define _PMD_BAD	(~PAGE_MASK)
#endif

/*
 * Some bits are only used on some cpu families...
 */
#ifndef _PAGE_HASHPTE
#define _PAGE_HASHPTE	0
#endif
#ifndef _PTE_NONE_MASK
#define _PTE_NONE_MASK 0
#endif
#ifndef _PAGE_SHARED
#define _PAGE_SHARED	0
#endif
#ifndef _PAGE_HWWRITE
#define _PAGE_HWWRITE	0
#endif
#ifndef _PAGE_HWEXEC
#define _PAGE_HWEXEC	0
#endif
#ifndef _PAGE_EXEC
#define _PAGE_EXEC	0
#endif
#ifndef _PMD_PRESENT_MASK
#define _PMD_PRESENT_MASK	_PMD_PRESENT
#endif
#ifndef _PMD_SIZE
#define _PMD_SIZE	0
#define PMD_PAGE_SIZE(pmd)	bad_call_to_PMD_PAGE_SIZE()
#endif

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

/*
 * Note: the _PAGE_COHERENT bit automatically gets set in the hardware
 * PTE if CONFIG_SMP is defined (hash_page does this); there is no need
 * to have it in the Linux PTE, and in fact the bit could be reused for
 * another purpose.  -- paulus.
 */

#ifdef CONFIG_44x
#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_GUARDED)
#else
#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED)
#endif
#define _PAGE_WRENABLE	(_PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE)
#define _PAGE_KERNEL	(_PAGE_BASE | _PAGE_SHARED | _PAGE_WRENABLE)

#ifdef CONFIG_PPC_STD_MMU
/* On standard PPC MMU, no user access implies kernel read/write access,
 * so to write-protect kernel memory we must turn on user access */
#define _PAGE_KERNEL_RO	(_PAGE_BASE | _PAGE_SHARED | _PAGE_USER)
#else
#define _PAGE_KERNEL_RO	(_PAGE_BASE | _PAGE_SHARED)
#endif

#define _PAGE_IO	(_PAGE_KERNEL | _PAGE_NO_CACHE | _PAGE_GUARDED)
#define _PAGE_RAM	(_PAGE_KERNEL | _PAGE_HWEXEC)

#if defined(CONFIG_KGDB) || defined(CONFIG_XMON) || defined(CONFIG_BDI_SWITCH)
/* We want the debuggers to be able to set breakpoints anywhere, so
 * don't write protect the kernel text */
#define _PAGE_RAM_TEXT	_PAGE_RAM
#else
#define _PAGE_RAM_TEXT	(_PAGE_KERNEL_RO | _PAGE_HWEXEC)
#endif

#define PAGE_NONE	__pgprot(_PAGE_BASE)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_RW)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_RW | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)

#define PAGE_KERNEL		__pgprot(_PAGE_RAM)
#define PAGE_KERNEL_NOCACHE	__pgprot(_PAGE_IO)

/*
 * The PowerPC can only do execute protection on a segment (256MB) basis,
 * not on a page basis.  So we consider execute permission the same as read.
 * Also, write permissions imply read permissions.
 * This is the closest we can get..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY_X
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY_X
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY_X
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY_X

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY_X
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED_X
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY_X
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED_X

#ifndef __ASSEMBLY__
/* Make sure we get a link error if PMD_PAGE_SIZE is ever called on a
 * kernel without large page PMD support */
extern unsigned long bad_call_to_PMD_PAGE_SIZE(void);

/*
 * Conversions between PTE values and page frame numbers.
 */

/* in some case we want to additionaly adjust where the pfn is in the pte to
 * allow room for more flags */
#if defined(CONFIG_FSL_BOOKE) && defined(CONFIG_PTE_64BIT)
#define PFN_SHIFT_OFFSET	(PAGE_SHIFT + 8)
#else
#define PFN_SHIFT_OFFSET	(PAGE_SHIFT)
#endif

#define pte_pfn(x)		(pte_val(x) >> PFN_SHIFT_OFFSET)
#define pte_page(x)		pfn_to_page(pte_pfn(x))

#define pfn_pte(pfn, prot)	__pte(((pte_basic_t)(pfn) << PFN_SHIFT_OFFSET) |\
					pgprot_val(prot))
#define mk_pte(page, prot)	pfn_pte(page_to_pfn(page), prot)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[1024];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

#endif /* __ASSEMBLY__ */

#define pte_none(pte)		((pte_val(pte) & ~_PTE_NONE_MASK) == 0)
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(mm,addr,ptep)	do { set_pte_at((mm), (addr), (ptep), __pte(0)); } while (0)

#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_bad(pmd)		(pmd_val(pmd) & _PMD_BAD)
#define	pmd_present(pmd)	(pmd_val(pmd) & _PMD_PRESENT_MASK)
#define	pmd_clear(pmdp)		do { pmd_val(*(pmdp)) = 0; } while (0)

#ifndef __ASSEMBLY__
/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
#define pgd_clear(xp)				do { } while (0)

#define pgd_page_vaddr(pgd) \
	((unsigned long) __va(pgd_val(pgd) & PAGE_MASK))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
static inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
static inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_EXEC; }
static inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_file(pte_t pte)		{ return pte_val(pte) & _PAGE_FILE; }

static inline void pte_uncache(pte_t pte)       { pte_val(pte) |= _PAGE_NO_CACHE; }
static inline void pte_cache(pte_t pte)         { pte_val(pte) &= ~_PAGE_NO_CACHE; }

static inline pte_t pte_rdprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
static inline pte_t pte_wrprotect(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_RW | _PAGE_HWWRITE); return pte; }
static inline pte_t pte_exprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_EXEC; return pte; }
static inline pte_t pte_mkclean(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_HWWRITE); return pte; }
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

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

/*
 * When flushing the tlb entry for a page, we also need to flush the hash
 * table entry.  flush_hash_pages is assembler (for speed) in hashtable.S.
 */
extern int flush_hash_pages(unsigned context, unsigned long va,
			    unsigned long pmdval, int count);

/* Add an HPTE to the hash table */
extern void add_hash_page(unsigned context, unsigned long va,
			  unsigned long pmdval);

/*
 * Atomic PTE updates.
 *
 * pte_update clears and sets bit atomically, and returns
 * the old pte value.  In the 64-bit PTE case we lock around the
 * low PTE word since we expect ALL flag bits to be there
 */
#ifndef CONFIG_PTE_64BIT
static inline unsigned long pte_update(pte_t *p, unsigned long clr,
				       unsigned long set)
{
	unsigned long old, tmp;

	__asm__ __volatile__("\
1:	lwarx	%0,0,%3\n\
	andc	%1,%0,%4\n\
	or	%1,%1,%5\n"
	PPC405_ERR77(0,%3)
"	stwcx.	%1,0,%3\n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*p)
	: "r" (p), "r" (clr), "r" (set), "m" (*p)
	: "cc" );
	return old;
}
#else
static inline unsigned long long pte_update(pte_t *p, unsigned long clr,
				       unsigned long set)
{
	unsigned long long old;
	unsigned long tmp;

	__asm__ __volatile__("\
1:	lwarx	%L0,0,%4\n\
	lwzx	%0,0,%3\n\
	andc	%1,%L0,%5\n\
	or	%1,%1,%6\n"
	PPC405_ERR77(0,%3)
"	stwcx.	%1,0,%4\n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*p)
	: "r" (p), "r" ((unsigned long)(p) + 4), "r" (clr), "r" (set), "m" (*p)
	: "cc" );
	return old;
}
#endif

/*
 * set_pte stores a linux PTE into the linux page table.
 * On machines which use an MMU hash table we avoid changing the
 * _PAGE_HASHPTE bit.
 */
static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
#if _PAGE_HASHPTE != 0
	pte_update(ptep, ~_PAGE_HASHPTE, pte_val(pte) & ~_PAGE_HASHPTE);
#else
	*ptep = pte;
#endif
}

/*
 * 2.6 calles this without flushing the TLB entry, this is wrong
 * for our hash-based implementation, we fix that up here
 */
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int __ptep_test_and_clear_young(unsigned int context, unsigned long addr, pte_t *ptep)
{
	unsigned long old;
	old = pte_update(ptep, _PAGE_ACCESSED, 0);
#if _PAGE_HASHPTE != 0
	if (old & _PAGE_HASHPTE) {
		unsigned long ptephys = __pa(ptep) & PAGE_MASK;
		flush_hash_pages(context, addr, ptephys, 1);
	}
#endif
	return (old & _PAGE_ACCESSED) != 0;
}
#define ptep_test_and_clear_young(__vma, __addr, __ptep) \
	__ptep_test_and_clear_young((__vma)->vm_mm->context.id, __addr, __ptep)

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
static inline int ptep_test_and_clear_dirty(struct vm_area_struct *vma,
					    unsigned long addr, pte_t *ptep)
{
	return (pte_update(ptep, (_PAGE_DIRTY | _PAGE_HWWRITE), 0) & _PAGE_DIRTY) != 0;
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	return __pte(pte_update(ptep, ~_PAGE_HASHPTE, 0));
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{
	pte_update(ptep, (_PAGE_RW | _PAGE_HWWRITE), 0);
}

#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
static inline void __ptep_set_access_flags(pte_t *ptep, pte_t entry, int dirty)
{
	unsigned long bits = pte_val(entry) &
		(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW);
	pte_update(ptep, 0, bits);
}

#define  ptep_set_access_flags(__vma, __address, __ptep, __entry, __dirty) \
({									   \
	int __changed = !pte_same(*(__ptep), __entry);			   \
	if (__changed) {						   \
		__ptep_set_access_flags(__ptep, __entry, __dirty);    	   \
		flush_tlb_page_nohash(__vma, __address);		   \
	}								   \
	__changed;							   \
})

/*
 * Macro to mark a page protection value as "uncacheable".
 */
#define pgprot_noncached(prot)	(__pgprot(pgprot_val(prot) | _PAGE_NO_CACHE | _PAGE_GUARDED))

struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#define __HAVE_PHYS_MEM_ACCESS_PROT

#define __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	(((pte_val(A) ^ pte_val(B)) & ~_PAGE_HASHPTE) == 0)

/*
 * Note that on Book E processors, the pmd contains the kernel virtual
 * (lowmem) address of the pte page.  The physical address is less useful
 * because everything runs with translation enabled (even the TLB miss
 * handler).  On everything else the pmd contains the physical address
 * of the pte page.  -- paulus
 */
#ifndef CONFIG_BOOKE
#define pmd_page_vaddr(pmd)	\
	((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd)		\
	(mem_map + (pmd_val(pmd) >> PAGE_SHIFT))
#else
#define pmd_page_vaddr(pmd)	\
	((unsigned long) (pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd)		\
	(mem_map + (__pa(pmd_val(pmd)) >> PAGE_SHIFT))
#endif

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
#define pgd_index(address)	 ((address) >> PGDIR_SHIFT)
#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

/* Find an entry in the second-level page table.. */
static inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */
#define pte_index(address)		\
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, addr)	\
	((pte_t *) pmd_page_vaddr(*(dir)) + pte_index(addr))
#define pte_offset_map(dir, addr)		\
	((pte_t *) kmap_atomic(pmd_page(*(dir)), KM_PTE0) + pte_index(addr))
#define pte_offset_map_nested(dir, addr)	\
	((pte_t *) kmap_atomic(pmd_page(*(dir)), KM_PTE1) + pte_index(addr))

#define pte_unmap(pte)		kunmap_atomic(pte, KM_PTE0)
#define pte_unmap_nested(pte)	kunmap_atomic(pte, KM_PTE1)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

extern void paging_init(void);

/*
 * Encode and decode a swap entry.
 * Note that the bits we use in a PTE for representing a swap entry
 * must not include the _PAGE_PRESENT bit, the _PAGE_FILE bit, or the
 *_PAGE_HASHPTE bit (if used).  -- paulus
 */
#define __swp_type(entry)		((entry).val & 0x1f)
#define __swp_offset(entry)		((entry).val >> 5)
#define __swp_entry(type, offset)	((swp_entry_t) { (type) | ((offset) << 5) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) >> 3 })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val << 3 })

/* Encode and decode a nonlinear file mapping entry */
#define PTE_FILE_MAX_BITS	29
#define pte_to_pgoff(pte)	(pte_val(pte) >> 3)
#define pgoff_to_pte(off)	((pte_t) { ((off) << 3) | _PAGE_FILE })

/* CONFIG_APUS */
/* For virtual address to physical address conversion */
extern void cache_clear(__u32 addr, int length);
extern void cache_push(__u32 addr, int length);
extern int mm_end_of_chunk (unsigned long addr, int len);
extern unsigned long iopa(unsigned long addr);
extern unsigned long mm_ptov(unsigned long addr) __attribute_const__;

/* Values for nocacheflag and cmode */
/* These are not used by the APUS kernel_map, but prevents
   compilation errors. */
#define	KERNELMAP_FULL_CACHING		0
#define	KERNELMAP_NOCACHE_SER		1
#define	KERNELMAP_NOCACHE_NONSER	2
#define	KERNELMAP_NO_COPYBACK		3

/*
 * Map some physical address range into the kernel address space.
 */
extern unsigned long kernel_map(unsigned long paddr, unsigned long size,
				int nocacheflag, unsigned long *memavailp );

/*
 * Set cache mode of (kernel space) address range.
 */
extern void kernel_set_cachemode (unsigned long address, unsigned long size,
                                 unsigned int cmode);

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	(1)

#ifdef CONFIG_PHYS_64BIT
extern int remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
			unsigned long paddr, unsigned long size, pgprot_t prot);

static inline int io_remap_pfn_range(struct vm_area_struct *vma,
					unsigned long vaddr,
					unsigned long pfn,
					unsigned long size,
					pgprot_t prot)
{
	phys_addr_t paddr64 = fixup_bigphys_addr(pfn << PAGE_SHIFT, size);
	return remap_pfn_range(vma, vaddr, paddr64 >> PAGE_SHIFT, size, prot);
}
#else
#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)		\
		remap_pfn_range(vma, vaddr, pfn, size, prot)
#endif

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

extern int get_pteptr(struct mm_struct *mm, unsigned long addr, pte_t **ptep,
		      pmd_t **pmdp);

#include <asm-generic/pgtable.h>

#endif /* !__ASSEMBLY__ */

#endif /* _PPC_PGTABLE_H */
#endif /* __KERNEL__ */
