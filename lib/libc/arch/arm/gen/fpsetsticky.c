/*	$OpenBSD: fpsetsticky.c,v 1.1 2018/02/28 11:16:54 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

#include <ieeefp.h>
#ifdef SOFTFLOAT_FOR_GCC
#include "softfloat-for-gcc.h"
#endif
#include "milieu.h"
#include <softfloat.h>

#define FP_X_MASK	(FP_X_INV | FP_X_DZ | FP_X_OFL | FP_X_UFL | FP_X_IMP)

__weak_alias(_fpsetsticky,fpsetsticky);

fp_except
fpsetsticky(fp_except except)
{
	fp_except old, new;

	__asm volatile("vmrs %0, fpscr" : "=r" (old));
	new = old & ~(FP_X_MASK);
	new |= (except & FP_X_MASK);
	__asm volatile("vmsr fpscr, %0" :: "r" (new));

	old |= float_exception_flags;
	float_exception_flags = except;

	return (old & FP_X_MASK);
}
