/*	$OpenBSD: fputws.c,v 1.9 2021/10/24 10:05:23 jsg Exp $	*/
/* $NetBSD: fputws.c,v 1.1 2003/03/07 07:11:37 tshiozak Exp $ */

/*-
 * Copyright (c) 2002 Tim J. Robbins.
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
 * Original version ID:
 * FreeBSD: src/lib/libc/stdio/fputws.c,v 1.4 2002/09/20 13:25:40 tjr Exp
 */

#include <errno.h>
#include <stdio.h>
#include <wchar.h>
#include "local.h"
#include "fvwrite.h"

int
fputws(const wchar_t * __restrict ws, FILE * __restrict fp)
{
	FLOCKFILE(fp);
	_SET_ORIENTATION(fp, 1);

	while (*ws != '\0') {
		if (__fputwc_unlock(*ws++, fp) == WEOF) {
			FUNLOCKFILE(fp);
			return (-1);
		}
	}

	FUNLOCKFILE(fp);

	return (0);
}
DEF_STRONG(fputws);
