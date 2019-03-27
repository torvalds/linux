/*-
 * Copyright (c) 2013 Andrew Turner <andrew@freebsd.org>
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

#ifndef _MACHINE_MACHDEP_H_
#define	_MACHINE_MACHDEP_H_

struct arm64_bootparams {
	vm_offset_t	modulep;
	vm_offset_t	kern_l1pt;	/* L1 page table for the kernel */
	uint64_t	kern_delta;
	vm_offset_t	kern_stack;
	vm_offset_t	kern_l0pt;	/* L1 page table for the kernel */
};

enum arm64_bus {
	ARM64_BUS_NONE,
	ARM64_BUS_FDT,
	ARM64_BUS_ACPI,
};

extern enum arm64_bus arm64_bus_method;

void dbg_init(void);
void initarm(struct arm64_bootparams *);
extern void (*pagezero)(void *);

#endif /* _MACHINE_MACHDEP_H_ */
