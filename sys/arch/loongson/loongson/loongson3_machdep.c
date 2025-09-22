/*	$OpenBSD: loongson3_machdep.c,v 1.1 2015/08/15 22:31:38 miod Exp $	*/

/*
 * Copyright (c) 2009, 2010, 2014 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/memconf.h>
#include <machine/pmon.h>

#ifdef HIBERNATE
#include <machine/hibernate_var.h>
#endif /* HIBERNATE */

extern struct phys_mem_desc mem_layout[MAXMEMSEGS];

void	loongson3a_setup(u_long, u_long);

#if 0
/* PCI view of CPU memory */
paddr_t loongson_dma_base = 0;
#endif

#define	MEMLO_BASE		0x00000000UL
#define	MEMHI_BASE		0x90000000UL	/* 2G + 256MB */

/*
 * Setup memory mappings for Loongson 3A processors.
 */

void
loongson3a_setup(u_long memlo, u_long memhi)
{
	physmem = memlo + memhi + 16;	/* in MB so far */

	memlo = atop(memlo << 20);
	memhi = atop(memhi << 20);
	physmem = memlo + memhi + atop(16 << 20);

	/* do NOT stomp on exception area */
	mem_layout[0].mem_first_page = atop(MEMLO_BASE) + 1;
	mem_layout[0].mem_last_page = atop(MEMLO_BASE) + memlo;
#ifdef HIBERNATE
	mem_layout[0].mem_first_page += HIBERNATE_RESERVED_PAGES;
#endif

	if (memhi != 0) {
#ifdef notyet
		mem_layout[1].mem_first_page = atop(MEMHI_BASE);
		mem_layout[1].mem_last_page = atop(MEMHI_BASE) +
		    memhi;
#endif
	}
}
