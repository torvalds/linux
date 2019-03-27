/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Peter Wemm
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
 * $FreeBSD$
 */

/*
 * These are ABI implementations for when the raw iconv_* symbol
 * space was exposed via libc.so.7 in its early life.  This is
 * a transition aide, these wrappers will not normally ever be
 * executed except via __sym_compat() references.
 */
#include <sys/types.h>
#include <iconv.h>
#include "iconv-internal.h"

size_t
__iconv_compat(iconv_t a, char ** b, size_t * c, char ** d,
     size_t * e, __uint32_t f, size_t *g)
{
	return __bsd___iconv(a, b, c, d, e, f, g);
}

void
__iconv_free_list_compat(char ** a, size_t b)
{
	__bsd___iconv_free_list(a, b);
}

int
__iconv_get_list_compat(char ***a, size_t *b, __iconv_bool c)
{
	return __bsd___iconv_get_list(a, b, c);
}

size_t
iconv_compat(iconv_t a, char ** __restrict b,
      size_t * __restrict c, char ** __restrict d,
      size_t * __restrict e)
{
	return __bsd_iconv(a, b, c, d, e);
}

const char *
iconv_canonicalize_compat(const char *a)
{
	return __bsd_iconv_canonicalize(a);
}

int
iconv_close_compat(iconv_t a)
{
	return __bsd_iconv_close(a);
}

iconv_t
iconv_open_compat(const char *a, const char *b)
{
	return __bsd_iconv_open(a, b);
}

int
iconv_open_into_compat(const char *a, const char *b, iconv_allocation_t *c)
{
	return __bsd_iconv_open_into(a, b, c);
}

void
iconv_set_relocation_prefix_compat(const char *a, const char *b)
{
	return __bsd_iconv_set_relocation_prefix(a, b);
}

int
iconvctl_compat(iconv_t a, int b, void *c)
{
	return __bsd_iconvctl(a, b, c);
}

void
iconvlist_compat(int (*a) (unsigned int, const char * const *, void *), void *b)
{
	return __bsd_iconvlist(a, b);
}

int _iconv_version_compat = 0x0108;	/* Magic - not used */

__sym_compat(__iconv, __iconv_compat, FBSD_1.2);
__sym_compat(__iconv_free_list, __iconv_free_list_compat, FBSD_1.2);
__sym_compat(__iconv_get_list, __iconv_get_list_compat, FBSD_1.2);
__sym_compat(_iconv_version, _iconv_version_compat, FBSD_1.3);
__sym_compat(iconv, iconv_compat, FBSD_1.3);
__sym_compat(iconv_canonicalize, iconv_canonicalize_compat, FBSD_1.2);
__sym_compat(iconv_close, iconv_close_compat, FBSD_1.3);
__sym_compat(iconv_open, iconv_open_compat, FBSD_1.3);
__sym_compat(iconv_open_into, iconv_open_into_compat, FBSD_1.3);
__sym_compat(iconv_set_relocation_prefix, iconv_set_relocation_prefix_compat, FBSD_1.3);
__sym_compat(iconvctl, iconvctl_compat, FBSD_1.3);
__sym_compat(iconvlist, iconvlist_compat, FBSD_1.3);
