/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Svatopluk Kraus
 * Copyright (c) 2016 Michal Meloun
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

#ifndef _MACHINE_PMAP_H_
#define _MACHINE_PMAP_H_

#if __ARM_ARCH >= 6
#include <machine/pmap-v6.h>
#else
#include <machine/pmap-v4.h>
#endif

#ifdef _KERNEL
#include <sys/systm.h>

extern vm_paddr_t dump_avail[];
extern vm_paddr_t phys_avail[];

extern char *_tmppt;	/* poor name! */

extern vm_offset_t virtual_avail;
extern vm_offset_t virtual_end;

void *pmap_kenter_temporary(vm_paddr_t, int);
#define	pmap_page_is_write_mapped(m)	(((m)->aflags & PGA_WRITEABLE) != 0)
void pmap_page_set_memattr(vm_page_t, vm_memattr_t);

void *pmap_mapdev(vm_paddr_t, vm_size_t);
void pmap_unmapdev(vm_offset_t, vm_size_t);

static inline void *
pmap_mapdev_attr(vm_paddr_t addr, vm_size_t size, int attr)
{
	panic("%s is not implemented yet!\n", __func__);
}

struct pcb;
void pmap_set_pcb_pagedir(pmap_t, struct pcb *);

void pmap_kenter_device(vm_offset_t, vm_size_t, vm_paddr_t);
void pmap_kremove_device(vm_offset_t, vm_size_t);

vm_paddr_t pmap_kextract(vm_offset_t);
#define vtophys(va)	pmap_kextract((vm_offset_t)(va))

static inline int
pmap_vmspace_copy(pmap_t dst_pmap __unused, pmap_t src_pmap __unused)
{

	return (0);
}

#endif	/* _KERNEL */
#endif	/* !_MACHINE_PMAP_H_ */
