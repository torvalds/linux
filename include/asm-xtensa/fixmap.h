/*
 * include/asm-xtensa/fixmap.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_FIXMAP_H
#define _XTENSA_FIXMAP_H

#include <asm/processor.h>

#ifdef CONFIG_MMU

/*
 * Here we define all the compile-time virtual addresses.
 */

#if XCHAL_SEG_MAPPABLE_VADDR != 0
# error "Current port requires virtual user space starting at 0"
#endif
#if XCHAL_SEG_MAPPABLE_SIZE < 0x80000000
# error "Current port requires at least 0x8000000 bytes for user space"
#endif

/* Verify instruction/data ram/rom and xlmi don't overlay vmalloc space. */

#define __IN_VMALLOC(addr)						\
	(((addr) >= VMALLOC_START) && ((addr) < VMALLOC_END))
#define __SPAN_VMALLOC(start,end)					\
	(((start) < VMALLOC_START) && ((end) >= VMALLOC_END))
#define INSIDE_VMALLOC(start,end) 					\
	(__IN_VMALLOC((start)) || __IN_VMALLOC(end) || __SPAN_VMALLOC((start),(end)))

#if XCHAL_NUM_INSTROM
# if XCHAL_NUM_INSTROM == 1
#  if INSIDE_VMALLOC(XCHAL_INSTROM0_VADDR,XCHAL_INSTROM0_VADDR+XCHAL_INSTROM0_SIZE)
#   error vmalloc range conflicts with instrom0
#  endif
# endif
# if XCHAL_NUM_INSTROM == 2
#  if INSIDE_VMALLOC(XCHAL_INSTROM1_VADDR,XCHAL_INSTROM1_VADDR+XCHAL_INSTROM1_SIZE)
#   error vmalloc range conflicts with instrom1
#  endif
# endif
#endif

#if XCHAL_NUM_INSTRAM
# if XCHAL_NUM_INSTRAM == 1
#  if INSIDE_VMALLOC(XCHAL_INSTRAM0_VADDR,XCHAL_INSTRAM0_VADDR+XCHAL_INSTRAM0_SIZE)
#   error vmalloc range conflicts with instram0
#  endif
# endif
# if XCHAL_NUM_INSTRAM == 2
#  if INSIDE_VMALLOC(XCHAL_INSTRAM1_VADDR,XCHAL_INSTRAM1_VADDR+XCHAL_INSTRAM1_SIZE)
#   error vmalloc range conflicts with instram1
#  endif
# endif
#endif

#if XCHAL_NUM_DATAROM
# if XCHAL_NUM_DATAROM == 1
#  if INSIDE_VMALLOC(XCHAL_DATAROM0_VADDR,XCHAL_DATAROM0_VADDR+XCHAL_DATAROM0_SIZE)
#   error vmalloc range conflicts with datarom0
#  endif
# endif
# if XCHAL_NUM_DATAROM == 2
#  if INSIDE_VMALLOC(XCHAL_DATAROM1_VADDR,XCHAL_DATAROM1_VADDR+XCHAL_DATAROM1_SIZE)
#   error vmalloc range conflicts with datarom1
#  endif
# endif
#endif

#if XCHAL_NUM_DATARAM
# if XCHAL_NUM_DATARAM == 1
#  if INSIDE_VMALLOC(XCHAL_DATARAM0_VADDR,XCHAL_DATARAM0_VADDR+XCHAL_DATARAM0_SIZE)
#   error vmalloc range conflicts with dataram0
#  endif
# endif
# if XCHAL_NUM_DATARAM == 2
#  if INSIDE_VMALLOC(XCHAL_DATARAM1_VADDR,XCHAL_DATARAM1_VADDR+XCHAL_DATARAM1_SIZE)
#   error vmalloc range conflicts with dataram1
#  endif
# endif
#endif

#if XCHAL_NUM_XLMI
# if XCHAL_NUM_XLMI == 1
#  if INSIDE_VMALLOC(XCHAL_XLMI0_VADDR,XCHAL_XLMI0_VADDR+XCHAL_XLMI0_SIZE)
#   error vmalloc range conflicts with xlmi0
#  endif
# endif
# if XCHAL_NUM_XLMI == 2
#  if INSIDE_VMALLOC(XCHAL_XLMI1_VADDR,XCHAL_XLMI1_VADDR+XCHAL_XLMI1_SIZE)
#   error vmalloc range conflicts with xlmi1
#  endif
# endif
#endif

#if (XCHAL_NUM_INSTROM > 2) || \
    (XCHAL_NUM_INSTRAM > 2) || \
    (XCHAL_NUM_DATARAM > 2) || \
    (XCHAL_NUM_DATAROM > 2) || \
    (XCHAL_NUM_XLMI    > 2)
# error Insufficient checks on vmalloc above for more than 2 devices
#endif

/*
 * USER_VM_SIZE does not necessarily equal TASK_SIZE.  We bumped
 * TASK_SIZE down to 0x4000000 to simplify the handling of windowed
 * call instructions (currently limited to a range of 1 GByte).  User
 * tasks may very well reclaim the VM space from 0x40000000 to
 * 0x7fffffff in the future, so we do not want the kernel becoming
 * accustomed to having any of its stuff (e.g., page tables) in this
 * region.  This VM region is no-man's land for now.
 */

#define USER_VM_START		XCHAL_SEG_MAPPABLE_VADDR
#define USER_VM_SIZE		0x80000000

/*  Size of page table:  */

#define PGTABLE_SIZE_BITS	(32 - XCHAL_MMU_MIN_PTE_PAGE_SIZE + 2)
#define PGTABLE_SIZE		(1L << PGTABLE_SIZE_BITS)

/*  All kernel-mappable space:  */

#define KERNEL_ALLMAP_START	(USER_VM_START + USER_VM_SIZE)
#define KERNEL_ALLMAP_SIZE	(XCHAL_SEG_MAPPABLE_SIZE - KERNEL_ALLMAP_START)

/*  Carve out page table at start of kernel-mappable area:  */

#if KERNEL_ALLMAP_SIZE < PGTABLE_SIZE
#error "Gimme some space for page table!"
#endif
#define PGTABLE_START		KERNEL_ALLMAP_START

/*  Remaining kernel-mappable space:  */

#define KERNEL_MAPPED_START	(KERNEL_ALLMAP_START + PGTABLE_SIZE)
#define KERNEL_MAPPED_SIZE	(KERNEL_ALLMAP_SIZE - PGTABLE_SIZE)

#if KERNEL_MAPPED_SIZE < 0x01000000	/* 16 MB is arbitrary for now */
# error "Shouldn't the kernel have at least *some* mappable space?"
#endif

#define MAX_LOW_MEMORY		XCHAL_KSEG_CACHED_SIZE

#endif

/*
 *  Some constants used elsewhere, but perhaps only in Xtensa header
 *  files, so maybe we can get rid of some and access compile-time HAL
 *  directly...
 *
 *  Note:  We assume that system RAM is located at the very start of the
 *  	   kernel segments !!
 */
#define KERNEL_VM_LOW           XCHAL_KSEG_CACHED_VADDR
#define KERNEL_VM_HIGH          XCHAL_KSEG_BYPASS_VADDR
#define KERNEL_SPACE            XCHAL_KSEG_CACHED_VADDR

/*
 * Returns the physical/virtual addresses of the kernel space
 * (works with the cached kernel segment only, which is the
 *  one normally used for kernel operation).
 */

/*			PHYSICAL	BYPASS		CACHED
 *
 *  bypass vaddr	bypass paddr	*		cached vaddr
 *  cached vaddr	cached paddr	bypass vaddr	*
 *  bypass paddr	*		bypass vaddr	cached vaddr
 *  cached paddr	*		bypass vaddr	cached vaddr
 *  other		*		*		*
 */

#define PHYSADDR(a)							      \
(((unsigned)(a) >= XCHAL_KSEG_BYPASS_VADDR				      \
  && (unsigned)(a) < XCHAL_KSEG_BYPASS_VADDR + XCHAL_KSEG_BYPASS_SIZE) ?      \
    (unsigned)(a) - XCHAL_KSEG_BYPASS_VADDR + XCHAL_KSEG_BYPASS_PADDR :       \
    ((unsigned)(a) >= XCHAL_KSEG_CACHED_VADDR				      \
     && (unsigned)(a) < XCHAL_KSEG_CACHED_VADDR + XCHAL_KSEG_CACHED_SIZE) ?   \
        (unsigned)(a) - XCHAL_KSEG_CACHED_VADDR + XCHAL_KSEG_CACHED_PADDR :   \
	(unsigned)(a))

#define BYPASS_ADDR(a)							      \
(((unsigned)(a) >= XCHAL_KSEG_BYPASS_PADDR				      \
  && (unsigned)(a) < XCHAL_KSEG_BYPASS_PADDR + XCHAL_KSEG_BYPASS_SIZE) ?      \
    (unsigned)(a) - XCHAL_KSEG_BYPASS_PADDR + XCHAL_KSEG_BYPASS_VADDR :       \
    ((unsigned)(a) >= XCHAL_KSEG_CACHED_PADDR				      \
     && (unsigned)(a) < XCHAL_KSEG_CACHED_PADDR + XCHAL_KSEG_CACHED_SIZE) ?   \
        (unsigned)(a) - XCHAL_KSEG_CACHED_PADDR + XCHAL_KSEG_BYPASS_VADDR :   \
        ((unsigned)(a) >= XCHAL_KSEG_CACHED_VADDR			      \
         && (unsigned)(a) < XCHAL_KSEG_CACHED_VADDR+XCHAL_KSEG_CACHED_SIZE)?  \
            (unsigned)(a) - XCHAL_KSEG_CACHED_VADDR+XCHAL_KSEG_BYPASS_VADDR:  \
	    (unsigned)(a))

#define CACHED_ADDR(a)							      \
(((unsigned)(a) >= XCHAL_KSEG_BYPASS_PADDR				      \
  && (unsigned)(a) < XCHAL_KSEG_BYPASS_PADDR + XCHAL_KSEG_BYPASS_SIZE) ?      \
    (unsigned)(a) - XCHAL_KSEG_BYPASS_PADDR + XCHAL_KSEG_CACHED_VADDR :       \
    ((unsigned)(a) >= XCHAL_KSEG_CACHED_PADDR			              \
     && (unsigned)(a) < XCHAL_KSEG_CACHED_PADDR + XCHAL_KSEG_CACHED_SIZE) ?   \
        (unsigned)(a) - XCHAL_KSEG_CACHED_PADDR + XCHAL_KSEG_CACHED_VADDR :   \
        ((unsigned)(a) >= XCHAL_KSEG_BYPASS_VADDR			      \
         && (unsigned)(a) < XCHAL_KSEG_BYPASS_VADDR+XCHAL_KSEG_BYPASS_SIZE) ? \
            (unsigned)(a) - XCHAL_KSEG_BYPASS_VADDR+XCHAL_KSEG_CACHED_VADDR : \
	    (unsigned)(a))

#define PHYSADDR_IO(a)							      \
(((unsigned)(a) >= XCHAL_KIO_BYPASS_VADDR				      \
  && (unsigned)(a) < XCHAL_KIO_BYPASS_VADDR + XCHAL_KIO_BYPASS_SIZE) ?	      \
    (unsigned)(a) - XCHAL_KIO_BYPASS_VADDR + XCHAL_KIO_BYPASS_PADDR :	      \
    ((unsigned)(a) >= XCHAL_KIO_CACHED_VADDR				      \
     && (unsigned)(a) < XCHAL_KIO_CACHED_VADDR + XCHAL_KIO_CACHED_SIZE) ?     \
        (unsigned)(a) - XCHAL_KIO_CACHED_VADDR + XCHAL_KIO_CACHED_PADDR :     \
	(unsigned)(a))

#define BYPASS_ADDR_IO(a)						      \
(((unsigned)(a) >= XCHAL_KIO_BYPASS_PADDR				      \
  && (unsigned)(a) < XCHAL_KIO_BYPASS_PADDR + XCHAL_KIO_BYPASS_SIZE) ?	      \
    (unsigned)(a) - XCHAL_KIO_BYPASS_PADDR + XCHAL_KIO_BYPASS_VADDR :	      \
    ((unsigned)(a) >= XCHAL_KIO_CACHED_PADDR				      \
     && (unsigned)(a) < XCHAL_KIO_CACHED_PADDR + XCHAL_KIO_CACHED_SIZE) ?     \
        (unsigned)(a) - XCHAL_KIO_CACHED_PADDR + XCHAL_KIO_BYPASS_VADDR :     \
        ((unsigned)(a) >= XCHAL_KIO_CACHED_VADDR			      \
         && (unsigned)(a) < XCHAL_KIO_CACHED_VADDR + XCHAL_KIO_CACHED_SIZE) ? \
            (unsigned)(a) - XCHAL_KIO_CACHED_VADDR + XCHAL_KIO_BYPASS_VADDR : \
	    (unsigned)(a))

#define CACHED_ADDR_IO(a)						      \
(((unsigned)(a) >= XCHAL_KIO_BYPASS_PADDR				      \
  && (unsigned)(a) < XCHAL_KIO_BYPASS_PADDR + XCHAL_KIO_BYPASS_SIZE) ?	      \
    (unsigned)(a) - XCHAL_KIO_BYPASS_PADDR + XCHAL_KIO_CACHED_VADDR :	      \
    ((unsigned)(a) >= XCHAL_KIO_CACHED_PADDR				      \
     && (unsigned)(a) < XCHAL_KIO_CACHED_PADDR + XCHAL_KIO_CACHED_SIZE) ?     \
        (unsigned)(a) - XCHAL_KIO_CACHED_PADDR + XCHAL_KIO_CACHED_VADDR :     \
        ((unsigned)(a) >= XCHAL_KIO_BYPASS_VADDR			      \
         && (unsigned)(a) < XCHAL_KIO_BYPASS_VADDR + XCHAL_KIO_BYPASS_SIZE) ? \
            (unsigned)(a) - XCHAL_KIO_BYPASS_VADDR + XCHAL_KIO_CACHED_VADDR : \
	    (unsigned)(a))

#endif /* _XTENSA_ADDRSPACE_H */





