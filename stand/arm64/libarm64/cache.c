/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <machine/armreg.h>

#include <stand.h>
#include <efi.h>

#include "cache.h"

static unsigned int
get_dcache_line_size(void)
{
	uint64_t ctr;
	unsigned int dcl_size;

	/* Accessible from all security levels */
	ctr = READ_SPECIALREG(ctr_el0);

	/*
	 * Relevant field [19:16] is LOG2
	 * of the number of words in DCache line
	 */
	dcl_size = CTR_DLINE_SIZE(ctr);

	/* Size of word shifted by cache line size */
	return (sizeof(int) << dcl_size);
}

void
cpu_flush_dcache(const void *ptr, size_t len)
{

	uint64_t cl_size;
	vm_offset_t addr, end;

	cl_size = get_dcache_line_size();

	/* Calculate end address to clean */
	end = (vm_offset_t)ptr + (vm_offset_t)len;
	/* Align start address to cache line */
	addr = (vm_offset_t)ptr;
	addr = rounddown2(addr, cl_size);

	for (; addr < end; addr += cl_size)
		__asm __volatile("dc	civac, %0" : : "r" (addr) : "memory");
	/* Full system DSB */
	__asm __volatile("dsb	sy" : : : "memory");
}

void
cpu_inval_icache(const void *ptr, size_t len)
{

	/* NULL ptr or 0 len means all */
	if (ptr == NULL || len == 0) {
		__asm __volatile(
		    "ic		ialluis	\n"
		    "dsb	ish	\n"
		    : : : "memory");
		return;
	}

	/* TODO: Other cache ranges if necessary */
}
