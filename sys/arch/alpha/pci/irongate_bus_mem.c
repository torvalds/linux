/*	$OpenBSD: irongate_bus_mem.c,v 1.7 2025/06/29 15:55:21 miod Exp $	*/
/* $NetBSD: irongate_bus_mem.c,v 1.7 2001/04/17 21:52:00 thorpej Exp $ */

/*-
 * Copyright (c) 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <alpha/pci/irongatereg.h>
#include <alpha/pci/irongatevar.h>

#define	CHIP		irongate

#define	CHIP_EX_MALLOC_SAFE(v)	(((struct irongate_config *)(v))->ic_mallocsafe)
#define	CHIP_MEM_EXTENT(v)	(((struct irongate_config *)(v))->ic_mem_ex)

#define	CHIP_MEM_SYS_START(v)	IRONGATE_MEM_BASE

/*
 * AMD 751 core logic appears on EV6.  We require at least EV56
 * support for the assembler to emit BWX opcodes.
 */
__asm(".arch ev6");

#include <alpha/pci/pci_bwx_bus_mem_chipdep.c>

#include <sys/kcore.h>

#include <dev/isa/isareg.h>

extern phys_ram_seg_t mem_clusters[];
extern int mem_cluster_cnt;

void
irongate_bus_mem_init2(bus_space_tag_t t, void *v)
{
	u_long size, start, end;
	int i, error;

	/*
	 * Since the AMD 751 doesn't have DMA windows, we need to
	 * allocate RAM out of the extent map.
	 */
	for (i = 0; i < mem_cluster_cnt; i++) {
		start = mem_clusters[i].start;
		size = mem_clusters[i].size & ~PAGE_MASK;
		end = mem_clusters[i].start + size;

		if (start <= IOM_BEGIN && end >= IOM_END) {
			/*
			 * The ISA hole lies somewhere in this
			 * memory cluster.  The UP1000 firmware
			 * doesn't report this to us properly,
			 * so we have to cope, since devices are
			 * mapped into the ISA hole, but RAM is
			 * not.
			 *
			 * Sigh, the UP1000 is a really cool machine,
			 * but it is sometimes too PC-like for my
			 * taste.
			 */
			if (start < IOM_BEGIN) {
				error = extent_alloc_region(CHIP_MEM_EXTENT(v),
				    start, (IOM_BEGIN - start),
				    EX_NOWAIT |
				    (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
				if (error) {
					printf("WARNING: unable to reserve "
					    "chunk from mem cluster %d "
					    "(0x%lx - 0x%lx)\n", i,
					    start, (u_long) IOM_BEGIN - 1);
				}
			}
			if (end > IOM_END) {
				error = extent_alloc_region(CHIP_MEM_EXTENT(v),
				    IOM_END, (end - IOM_END),
				    EX_NOWAIT |
				    (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
				if (error) {
					printf("WARNING: unable to reserve "
					    "chunk from mem cluster %d "
					    "(0x%lx - 0x%lx)\n", i,
					    (u_long) IOM_END, end - 1);
				}
			}
		} else {
			error = extent_alloc_region(CHIP_MEM_EXTENT(v),
			    start, size,
			    EX_NOWAIT |
			    (CHIP_EX_MALLOC_SAFE(v) ? EX_MALLOCOK : 0));
			if (error) {
				printf("WARNING: unable reserve mem cluster %d "
				    "(0x%lx - 0x%lx)\n", i, start,
				    start + (size - 1));
			}
		}
	}
}
