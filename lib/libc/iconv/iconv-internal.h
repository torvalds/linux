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
 * Internal prototypes for our back-end functions.
 */
size_t	__bsd___iconv(iconv_t, char **, size_t *, char **,
		size_t *, __uint32_t, size_t *);
void	__bsd___iconv_free_list(char **, size_t);
int	__bsd___iconv_get_list(char ***, size_t *, __iconv_bool);
size_t	__bsd_iconv(iconv_t, char ** __restrict,
		    size_t * __restrict, char ** __restrict,
		    size_t * __restrict);
const char *__bsd_iconv_canonicalize(const char *);
int	__bsd_iconv_close(iconv_t);
iconv_t	__bsd_iconv_open(const char *, const char *);
int	__bsd_iconv_open_into(const char *, const char *, iconv_allocation_t *);
void	__bsd_iconv_set_relocation_prefix(const char *, const char *);
int	__bsd_iconvctl(iconv_t, int, void *);
void	__bsd_iconvlist(int (*) (unsigned int, const char * const *, void *), void *);
