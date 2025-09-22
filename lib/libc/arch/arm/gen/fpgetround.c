/*	$OpenBSD: fpgetround.c,v 1.1 2018/02/28 11:16:54 kettenis Exp $	*/
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
#include <stdint.h>

__weak_alias(_fpgetround,fpgetround);

fp_rnd
fpgetround(void)
{
	uint32_t fpscr;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));

	return ((fpscr >> 22) & 3);
}
DEF_WEAK(fpgetround);
