/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 */

#ifndef _ASM_FIXMAP_64_H
#define _ASM_FIXMAP_64_H

#include <linux/kernel.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#include <asm/vsyscall.h>
#include <asm/efi.h>

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process.
 *
 * These 'compile-time allocated' memory buffers are
 * fixed-size 4k pages (or larger if used with an increment
 * higher than 1). Use set_fixmap(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */

enum fixed_addresses {
	VSYSCALL_LAST_PAGE,
	VSYSCALL_FIRST_PAGE = VSYSCALL_LAST_PAGE
			    + ((VSYSCALL_END-VSYSCALL_START) >> PAGE_SHIFT) - 1,
	VSYSCALL_HPET,
	FIX_DBGP_BASE,
	FIX_EARLYCON_MEM_BASE,
	FIX_HPET_BASE,
	FIX_APIC_BASE,	/* local (CPU) APIC) -- required for SMP or not */
	FIX_IO_APIC_BASE_0,
	FIX_IO_APIC_BASE_END = FIX_IO_APIC_BASE_0 + MAX_IO_APICS - 1,
	FIX_EFI_IO_MAP_LAST_PAGE,
	FIX_EFI_IO_MAP_FIRST_PAGE = FIX_EFI_IO_MAP_LAST_PAGE
				  + MAX_EFI_IO_PAGES - 1,
#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	FIX_OHCI1394_BASE,
#endif
	__end_of_fixed_addresses
};

extern void __set_fixmap(enum fixed_addresses idx,
			 unsigned long phys, pgprot_t flags);

#define set_fixmap(idx, phys)			\
	__set_fixmap(idx, phys, PAGE_KERNEL)
/*
 * Some hardware wants to get fixmapped without caching.
 */
#define set_fixmap_nocache(idx, phys)			\
	__set_fixmap(idx, phys, PAGE_KERNEL_NOCACHE)

#define FIXADDR_TOP	(VSYSCALL_END-PAGE_SIZE)
#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

/* Only covers 32bit vsyscalls currently. Need another set for 64bit. */
#define FIXADDR_USER_START	((unsigned long)VSYSCALL32_VSYSCALL)
#define FIXADDR_USER_END	(FIXADDR_USER_START + PAGE_SIZE)

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))

extern void __this_fixmap_does_not_exist(void);

/*
 * 'index to address' translation. If anyone tries to use the idx
 * directly without translation, we catch the bug with a NULL-deference
 * kernel oops. Illegal ranges of incoming indices are caught too.
 */
static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	/*
	 * this branch gets completely eliminated after inlining,
	 * except when someone tries to use fixaddr indices in an
	 * illegal way. (such as mixing up address types or using
	 * out-of-range indices).
	 *
	 * If it doesn't get removed, the linker will complain
	 * loudly with a reasonably clear error message..
	 */
	if (idx >= __end_of_fixed_addresses)
		__this_fixmap_does_not_exist();

	return __fix_to_virt(idx);
}

#endif
