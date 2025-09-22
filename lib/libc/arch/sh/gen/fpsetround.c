/*	$OpenBSD: fpsetround.c,v 1.3 2014/04/18 15:09:52 guenther Exp $	*/
/*
 * Copyright (c) 2006 Miodrag Vallat.
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

#include <sys/types.h>
#include <ieeefp.h>

fp_rnd
fpsetround(fp_rnd rnd_dir)
{
	register_t fpscr, nfpscr;
#if defined(__SH4__) && !defined(__SH4_NOFPU__)
	extern register_t __fpscr_values[2];
#endif

	__asm__ volatile ("sts fpscr, %0" : "=r" (fpscr));
	if (rnd_dir == FP_RN || rnd_dir == FP_RZ) {
		nfpscr = (fpscr & ~0x03) | rnd_dir;
#if defined(__SH4__) && !defined(__SH4_NOFPU__)
		__fpscr_values[0] = (__fpscr_values[0] & ~0x03) | rnd_dir;
		__fpscr_values[1] = (__fpscr_values[1] & ~0x03) | rnd_dir;
#endif
		__asm__ volatile ("lds %0, fpscr" : : "r" (nfpscr));
	}
	/* else how report an error? */

	return (fpscr & 0x03);
}
