/*	$OpenBSD: freadptr.c,v 1.1 2024/08/12 20:56:55 guenther Exp $	*/
/*
 * Copyright (c) 2024 Philip Guenther <guenther@openbsd.org>
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

#include <stdlib.h>
#include <stdio_ext.h>
#include "local.h"

const char *
__freadptr(FILE *fp, size_t *sizep)
{
	if ((fp->_flags & __SRD) && fp->_r > 0) {
		*sizep = fp->_r;
		return fp->_p;
	}
	return NULL;
}

void
__freadptrinc(FILE *fp, size_t inc)
{
	if (fp->_flags & __SRD) {
		fp->_r -= inc;
		fp->_p += inc;
		if (fp->_r == 0 && HASUB(fp)) {
			/* consumed the pushback buffer; switch back */
			FREEUB(fp);
			if ((fp->_r = fp->_ur) != 0)
				fp->_p = fp->_up;
		}
	}
}
