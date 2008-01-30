#ifndef _ASM_X86_PGTABLE_H
#define _ASM_X86_PGTABLE_H

#define USER_PTRS_PER_PGD	((TASK_SIZE-1)/PGDIR_SIZE+1)
#define FIRST_USER_ADDRESS	0

#define _PAGE_BIT_PRESENT	0
#define _PAGE_BIT_RW		1
#define _PAGE_BIT_USER		2
#define _PAGE_BIT_PWT		3
#define _PAGE_BIT_PCD		4
#define _PAGE_BIT_ACCESSED	5
#define _PAGE_BIT_DIRTY		6
#define _PAGE_BIT_FILE		6
#define _PAGE_BIT_PSE		7	/* 4 MB (or 2MB) page */
#define _PAGE_BIT_GLOBAL	8	/* Global TLB entry PPro+ */
#define _PAGE_BIT_UNUSED1	9	/* available for programmer */
#define _PAGE_BIT_UNUSED2	10
#define _PAGE_BIT_UNUSED3	11
#define _PAGE_BIT_NX           63       /* No execute: only valid after cpuid check */

#define _PAGE_PRESENT	(_AC(1, L)<<_PAGE_BIT_PRESENT)
#define _PAGE_RW	(_AC(1, L)<<_PAGE_BIT_RW)
#define _PAGE_USER	(_AC(1, L)<<_PAGE_BIT_USER)
#define _PAGE_PWT	(_AC(1, L)<<_PAGE_BIT_PWT)
#define _PAGE_PCD	(_AC(1, L)<<_PAGE_BIT_PCD)
#define _PAGE_ACCESSED	(_AC(1, L)<<_PAGE_BIT_ACCESSED)
#define _PAGE_DIRTY	(_AC(1, L)<<_PAGE_BIT_DIRTY)
#define _PAGE_PSE	(_AC(1, L)<<_PAGE_BIT_PSE)	/* 2MB page */
#define _PAGE_GLOBAL	(_AC(1, L)<<_PAGE_BIT_GLOBAL)	/* Global TLB entry */
#define _PAGE_UNUSED1	(_AC(1, L)<<_PAGE_BIT_UNUSED1)
#define _PAGE_UNUSED2	(_AC(1, L)<<_PAGE_BIT_UNUSED2)
#define _PAGE_UNUSED3	(_AC(1, L)<<_PAGE_BIT_UNUSED3)

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
#define _PAGE_NX	(_AC(1, ULL) << _PAGE_BIT_NX)
#else
#define _PAGE_NX	0
#endif

/* If _PAGE_PRESENT is clear, we use these: */
#define _PAGE_FILE	_PAGE_DIRTY	/* nonlinear file mapping, saved PTE; unset:swap */
#define _PAGE_PROTNONE	_PAGE_PSE	/* if the user mapped it with PROT_NONE;
					   pte_present gives true */

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)

#define _PAGE_CHG_MASK	(PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_NX)

#define PAGE_SHARED_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY_NOEXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_COPY_EXEC		__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY		PAGE_COPY_NOEXEC
#define PAGE_READONLY		__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)

#ifdef CONFIG_X86_32
#define _PAGE_KERNEL_EXEC \
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define _PAGE_KERNEL (_PAGE_KERNEL_EXEC | _PAGE_NX)

#ifndef __ASSEMBLY__
extern unsigned long long __PAGE_KERNEL, __PAGE_KERNEL_EXEC;
#endif	/* __ASSEMBLY__ */
#else
#define __PAGE_KERNEL_EXEC						\
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define __PAGE_KERNEL		(__PAGE_KERNEL_EXEC | _PAGE_NX)
#endif

#define __PAGE_KERNEL_RO		(__PAGE_KERNEL & ~_PAGE_RW)
#define __PAGE_KERNEL_RX		(__PAGE_KERNEL_EXEC & ~_PAGE_RW)
#define __PAGE_KERNEL_NOCACHE		(__PAGE_KERNEL | _PAGE_PCD | _PAGE_PWT)
#define __PAGE_KERNEL_VSYSCALL		(__PAGE_KERNEL_RX | _PAGE_USER)
#define __PAGE_KERNEL_VSYSCALL_NOCACHE	(__PAGE_KERNEL_VSYSCALL | _PAGE_PCD | _PAGE_PWT)
#define __PAGE_KERNEL_LARGE		(__PAGE_KERNEL | _PAGE_PSE)
#define __PAGE_KERNEL_LARGE_EXEC	(__PAGE_KERNEL_EXEC | _PAGE_PSE)

#ifdef CONFIG_X86_32
# define MAKE_GLOBAL(x)			__pgprot((x))
#else
# define MAKE_GLOBAL(x)			__pgprot((x) | _PAGE_GLOBAL)
#endif

#define PAGE_KERNEL			MAKE_GLOBAL(__PAGE_KERNEL)
#define PAGE_KERNEL_RO			MAKE_GLOBAL(__PAGE_KERNEL_RO)
#define PAGE_KERNEL_EXEC		MAKE_GLOBAL(__PAGE_KERNEL_EXEC)
#define PAGE_KERNEL_RX			MAKE_GLOBAL(__PAGE_KERNEL_RX)
#define PAGE_KERNEL_NOCACHE		MAKE_GLOBAL(__PAGE_KERNEL_NOCACHE)
#define PAGE_KERNEL_LARGE		MAKE_GLOBAL(__PAGE_KERNEL_LARGE)
#define PAGE_KERNEL_LARGE_EXEC		MAKE_GLOBAL(__PAGE_KERNEL_LARGE_EXEC)
#define PAGE_KERNEL_VSYSCALL		MAKE_GLOBAL(__PAGE_KERNEL_VSYSCALL)
#define PAGE_KERNEL_VSYSCALL_NOCACHE	MAKE_GLOBAL(__PAGE_KERNEL_VSYSCALL_NOCACHE)

/*         xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

#ifndef __ASSEMBLY__
/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
static inline int pte_file(pte_t pte)		{ return pte_val(pte) & _PAGE_FILE; }
static inline int pte_huge(pte_t pte)		{ return pte_val(pte) & _PAGE_PSE; }

static inline int pmd_large(pmd_t pte) {
	return (pmd_val(pte) & (_PAGE_PSE|_PAGE_PRESENT)) ==
		(_PAGE_PSE|_PAGE_PRESENT);
}

static inline pte_t pte_mkclean(pte_t pte)	{ return __pte(pte_val(pte) & ~_PAGE_DIRTY); }
static inline pte_t pte_mkold(pte_t pte)	{ return __pte(pte_val(pte) & ~_PAGE_ACCESSED); }
static inline pte_t pte_wrprotect(pte_t pte)	{ return __pte(pte_val(pte) & ~_PAGE_RW); }
static inline pte_t pte_mkexec(pte_t pte)	{ return __pte(pte_val(pte) & ~_PAGE_NX); }
static inline pte_t pte_mkdirty(pte_t pte)	{ return __pte(pte_val(pte) | _PAGE_DIRTY); }
static inline pte_t pte_mkyoung(pte_t pte)	{ return __pte(pte_val(pte) | _PAGE_ACCESSED); }
static inline pte_t pte_mkwrite(pte_t pte)	{ return __pte(pte_val(pte) | _PAGE_RW); }
static inline pte_t pte_mkhuge(pte_t pte)	{ return __pte(pte_val(pte) | _PAGE_PSE); }
static inline pte_t pte_clrhuge(pte_t pte)	{ return __pte(pte_val(pte) & ~_PAGE_PSE); }

#endif	/* __ASSEMBLY__ */

#ifdef CONFIG_X86_32
# include "pgtable_32.h"
#else
# include "pgtable_64.h"
#endif

#endif	/* _ASM_X86_PGTABLE_H */
