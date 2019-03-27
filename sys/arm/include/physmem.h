/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_PHYSMEM_H_
#define	_MACHINE_PHYSMEM_H_

/*
 * The physical address at which the kernel was loaded.
 */
extern vm_paddr_t arm_physmem_kernaddr;

/*
 * Routines to help configure physical ram.
 *
 * Multiple regions of contiguous physical ram can be added (in any order).
 *
 * Multiple regions of physical ram that should be excluded from crash dumps, or
 * memory allocation, or both, can be added (in any order).
 *
 * After all early kernel init is done and it's time to configure all
 * remainining non-excluded physical ram for use by other parts of the kernel,
 * arm_physmem_init_kernel_globals() processes the hardware regions and
 * exclusion regions to generate the global dump_avail and phys_avail arrays
 * that communicate physical ram configuration to other parts of the kernel.
 */

#define	EXFLAG_NODUMP	0x01
#define	EXFLAG_NOALLOC	0x02

void arm_physmem_hardware_region(uint64_t pa, uint64_t sz);
void arm_physmem_exclude_region(vm_paddr_t pa, vm_size_t sz, uint32_t flags);
size_t arm_physmem_avail(vm_paddr_t *avail, size_t maxavail);
void arm_physmem_init_kernel_globals(void);
void arm_physmem_print_tables(void);

/*
 * Convenience routines for FDT.
 */

#ifdef FDT

#include <machine/ofw_machdep.h>

static inline void
arm_physmem_hardware_regions(struct mem_region * mrptr, int mrcount)
{
	while (mrcount--) {
		arm_physmem_hardware_region(mrptr->mr_start, mrptr->mr_size);
		++mrptr;
	}
}

static inline void
arm_physmem_exclude_regions(struct mem_region * mrptr, int mrcount,
    uint32_t exflags)
{
	while (mrcount--) {
		arm_physmem_exclude_region(mrptr->mr_start, mrptr->mr_size,
		    exflags);
		++mrptr;
	}
}

#endif /* FDT */

#endif

