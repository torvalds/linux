#ifndef _X86_64_PAGE_H
#define _X86_64_PAGE_H

#define PAGETABLE_LEVELS	4

#define THREAD_ORDER	1
#define THREAD_SIZE  (PAGE_SIZE << THREAD_ORDER)
#define CURRENT_MASK (~(THREAD_SIZE-1))

#define EXCEPTION_STACK_ORDER 0
#define EXCEPTION_STKSZ (PAGE_SIZE << EXCEPTION_STACK_ORDER)

#define DEBUG_STACK_ORDER (EXCEPTION_STACK_ORDER + 1)
#define DEBUG_STKSZ (PAGE_SIZE << DEBUG_STACK_ORDER)

#define IRQSTACK_ORDER 2
#define IRQSTACKSIZE (PAGE_SIZE << IRQSTACK_ORDER)

#define STACKFAULT_STACK 1
#define DOUBLEFAULT_STACK 2
#define NMI_STACK 3
#define DEBUG_STACK 4
#define MCE_STACK 5
#define N_EXCEPTION_STACKS 5  /* hw limit: 7 */

#define PUD_PAGE_SIZE		(_AC(1, UL) << PUD_SHIFT)
#define PUD_PAGE_MASK		(~(PUD_PAGE_SIZE-1))

#define __PAGE_OFFSET           _AC(0xffff810000000000, UL)

#define __PHYSICAL_START	CONFIG_PHYSICAL_START
#define __KERNEL_ALIGN		0x200000

/*
 * Make sure kernel is aligned to 2MB address. Catching it at compile
 * time is better. Change your config file and compile the kernel
 * for a 2MB aligned address (CONFIG_PHYSICAL_START)
 */
#if (CONFIG_PHYSICAL_START % __KERNEL_ALIGN) != 0
#error "CONFIG_PHYSICAL_START must be a multiple of 2MB"
#endif

#define __START_KERNEL		(__START_KERNEL_map + __PHYSICAL_START)
#define __START_KERNEL_map	_AC(0xffffffff80000000, UL)

/* See Documentation/x86_64/mm.txt for a description of the memory map. */
#define __PHYSICAL_MASK_SHIFT	46
#define __VIRTUAL_MASK_SHIFT	48

#define KERNEL_TEXT_SIZE  (40*1024*1024)
#define KERNEL_TEXT_START _AC(0xffffffff80000000, UL)

#ifndef __ASSEMBLY__
void clear_page(void *page);
void copy_page(void *to, void *from);

extern unsigned long end_pfn;
extern unsigned long end_pfn_map;
extern unsigned long phys_base;

extern unsigned long __phys_addr(unsigned long);
#define __phys_reloc_hide(x)	(x)

/*
 * These are used to make use of C type-checking..
 */
typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;
typedef unsigned long	phys_addr_t;

typedef struct { pteval_t pte; } pte_t;

#define vmemmap ((struct page *)VMEMMAP_START)

#endif	/* !__ASSEMBLY__ */

#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)          ((pfn) < end_pfn)
#endif


#endif /* _X86_64_PAGE_H */
