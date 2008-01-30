#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#define PHYSICAL_PAGE_MASK	(PAGE_MASK & __PHYSICAL_MASK)

#define LARGE_PAGE_SIZE		(_AC(1,UL) << PMD_SHIFT)
#define LARGE_PAGE_MASK		(~(LARGE_PAGE_SIZE-1))

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#define __PHYSICAL_MASK		((_AC(1,UL) << __PHYSICAL_MASK_SHIFT) - 1)
#define __VIRTUAL_MASK		((_AC(1,UL) << __VIRTUAL_MASK_SHIFT) - 1)


#ifdef CONFIG_X86_64
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

#endif	/* CONFIG_X86_64 */

#ifdef CONFIG_X86_32

/*
 * This handles the memory map.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB.
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 */
#define __PAGE_OFFSET		_AC(CONFIG_PAGE_OFFSET, UL)

#ifdef CONFIG_X86_PAE
#define __PHYSICAL_MASK_SHIFT	36
#define __VIRTUAL_MASK_SHIFT	32
#else  /* !CONFIG_X86_PAE */
#define __PHYSICAL_MASK_SHIFT	32
#define __VIRTUAL_MASK_SHIFT	32
#endif	/* CONFIG_X86_PAE */

#ifdef CONFIG_HUGETLB_PAGE
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#endif	/* CONFIG_X86_32 */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS \
	(((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0 ) | \
	 VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)


#ifdef CONFIG_X86_32
# include "page_32.h"
#else
# include "page_64.h"
#endif

#endif	/* _ASM_X86_PAGE_H */
