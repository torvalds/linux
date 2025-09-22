/*	$OpenBSD: fabs.c,v 1.12 2014/04/18 15:09:52 guenther Exp $	*/
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

#if !defined(__SH4__) || defined(__SH4_NOFPU__)
#include <sys/types.h>
#include <machine/ieee.h>
#endif /* !defined(__SH4__) || defined(__SH4_NOFPU__) */

#include <math.h>

double
fabs(double d)
{
#if defined(__SH4__) && !defined(__SH4_NOFPU__)
	__asm__ volatile("fabs %0" : "+f" (d));
#else
	struct ieee_double *p = (struct ieee_double *)&d;

	p->dbl_sign = 0;
#endif
	return (d);
}

__strong_alias(fabsl, fabs);
