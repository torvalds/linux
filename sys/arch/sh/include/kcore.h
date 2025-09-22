/*	$OpenBSD: kcore.h,v 1.1 2007/03/03 21:37:27 miod Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_SH_KCORE_H_
#define	_SH_KCORE_H_

/* this should be >= VM_PHYSSEG_MAX from <machine/vmparam.h> */
#define	NPHYS_RAM_SEGS	8

typedef struct cpu_kcore_hdr {
	paddr_t		kcore_kptp;
	unsigned int	kcore_nsegs;
	phys_ram_seg_t	kcore_segs[NPHYS_RAM_SEGS];
} cpu_kcore_hdr_t;

#endif	/* _SH_KCORE_H_ */
