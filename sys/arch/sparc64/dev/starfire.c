/*	$OpenBSD: starfire.c,v 1.1 2008/03/16 22:18:53 kettenis Exp $	*/

/*
 * Copyright (c) 2008 Mark Kettenis
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

#include <sparc64/dev/starfire.h>

void
starfire_pc_ittrans_init(int upaid)
{
	paddr_t pa;

	pa = STARFIRE_UPAID2UPS(upaid) | STARFIRE_PSI_BASE;
	pa |= (STARFIRE_PSI_PCREG_OFF | STARFIRE_PC_INT_MAP);

	/*
	 * Since we direct all interrupts to the boot processor, we
	 * simply program the port controller ITTR hardware with a
	 * single mapping.
	 */
	pa += CPU_UPAID * 0x10;
	stwa(pa, ASI_PHYS_NON_CACHED, STARFIRE_UPAID2HWMID(cpu_myid()));
}
