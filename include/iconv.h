/*	$FreeBSD$	*/
/*	$NetBSD: iconv.h,v 1.6 2005/02/03 04:39:32 perry Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2003 Citrus Project,
 * Copyright (c) 2009, 2010 Gabor Kovesdan <gabor@FreeBSD.org>
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
 */

#ifndef _ICONV_H_
#define _ICONV_H_

#include <sys/cdefs.h>
#include <sys/types.h>

#include <wchar.h>

#include <sys/cdefs.h>
#include <sys/types.h>

#ifdef __cplusplus
typedef	bool	__iconv_bool;
#elif __STDC_VERSION__ >= 199901L
typedef	_Bool	__iconv_bool;
#else
typedef	int	__iconv_bool;
#endif

struct __tag_iconv_t;
typedef	struct __tag_iconv_t	*iconv_t;

__BEGIN_DECLS
iconv_t	iconv_open(const char *, const char *);
size_t	iconv(iconv_t, char ** __restrict,
	      size_t * __restrict, char ** __restrict,
	      size_t * __restrict);
int	iconv_close(iconv_t);
/*
 * non-portable interfaces for iconv
 */
int	__iconv_get_list(char ***, size_t *, __iconv_bool);
void	__iconv_free_list(char **, size_t);
size_t	__iconv(iconv_t, char **, size_t *, char **,
		     size_t *, __uint32_t, size_t *);
#define __ICONV_F_HIDE_INVALID	0x0001

/*
 * GNU interfaces for iconv
 */
typedef struct {
	void	*spaceholder[64];
} iconv_allocation_t;

int	 iconv_open_into(const char *, const char *, iconv_allocation_t *);
void	 iconv_set_relocation_prefix(const char *, const char *);

/*
 * iconvctl() request macros
 */
#define ICONV_TRIVIALP		0
#define	ICONV_GET_TRANSLITERATE	1
#define	ICONV_SET_TRANSLITERATE	2
#define ICONV_GET_DISCARD_ILSEQ	3
#define ICONV_SET_DISCARD_ILSEQ	4
#define ICONV_SET_HOOKS		5
#define ICONV_SET_FALLBACKS	6
#define ICONV_GET_ILSEQ_INVALID	128
#define ICONV_SET_ILSEQ_INVALID	129

typedef void (*iconv_unicode_char_hook) (unsigned int mbr, void *data);
typedef void (*iconv_wide_char_hook) (wchar_t wc, void *data);

struct iconv_hooks {
	iconv_unicode_char_hook		 uc_hook;
	iconv_wide_char_hook		 wc_hook;
	void				*data;
};

/*
 * Fallbacks aren't supported but type definitions are provided for
 * source compatibility.
 */
typedef void (*iconv_unicode_mb_to_uc_fallback) (const char*,
		size_t, void (*write_replacement) (const unsigned int *,
		size_t, void*),	void*, void*);
typedef void (*iconv_unicode_uc_to_mb_fallback) (unsigned int,
		void (*write_replacement) (const char *, size_t, void*),
		void*, void*);
typedef void (*iconv_wchar_mb_to_wc_fallback) (const char*, size_t,
		void (*write_replacement) (const wchar_t *, size_t, void*),
		void*, void*);
typedef void (*iconv_wchar_wc_to_mb_fallback) (wchar_t,
		void (*write_replacement) (const char *, size_t, void*),
		void*, void*);

struct iconv_fallbacks {
	iconv_unicode_mb_to_uc_fallback	 mb_to_uc_fallback;
	iconv_unicode_uc_to_mb_fallback  uc_to_mb_fallback;
	iconv_wchar_mb_to_wc_fallback	 mb_to_wc_fallback;
	iconv_wchar_wc_to_mb_fallback	 wc_to_mb_fallback;
	void				*data;
};


void		 iconvlist(int (*do_one) (unsigned int, const char * const *,
		    void *), void *);
const char	*iconv_canonicalize(const char *);
int		 iconvctl(iconv_t, int, void *);
__END_DECLS

#endif /* !_ICONV_H_ */
