/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
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
 */

#if !defined(__BSD_SORT_H__)
#define	__BSD_SORT_H__

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sysexits.h>
#include <wchar.h>

#include <sys/types.h>
#include <md5.h>

#define	VERSION	"2.3-FreeBSD"

#ifdef WITHOUT_NLS
#define	getstr(n)	 nlsstr[n]
#else
#include <nl_types.h>

extern nl_catd catalog;
#define	getstr(n)	 catgets(catalog, 1, n, nlsstr[n])
#endif

extern const char *nlsstr[];

#if defined(SORT_THREADS)
#define	MT_SORT_THRESHOLD (10000)
extern unsigned int ncpu;
extern size_t nthreads;
#endif

/*
 * If true, we output some debug information.
 */
extern bool debug_sort;

/*
 * MD5 context for random hash function
 */
extern MD5_CTX md5_ctx;

/*
 * sort.c
 */

/*
 * This structure holds main sort options which are NOT affecting the sort ordering.
 */
struct sort_opts
{
	wint_t		field_sep;
	int		sort_method;
	bool		cflag;
	bool		csilentflag;
	bool		kflag;
	bool		mflag;
	bool		sflag;
	bool		uflag;
	bool		zflag;
	bool		tflag;
	bool		complex_sort;
};

/*
 * Key value structure forward declaration
 */
struct key_value;

/*
 * Cmp function
 */
typedef int (*cmpcoll_t)(struct key_value *kv1, struct key_value *kv2, size_t offset);

/*
 * This structure holds "sort modifiers" - options which are affecting the sort ordering.
 */
struct sort_mods
{
	cmpcoll_t	func;
	bool		bflag;
	bool		dflag;
	bool		fflag;
	bool		gflag;
	bool		iflag;
	bool		Mflag;
	bool		nflag;
	bool		rflag;
	bool		Rflag;
	bool		Vflag;
	bool		hflag;
};

extern bool need_hint;

extern struct sort_opts sort_opts_vals;

extern struct sort_mods * const default_sort_mods;

#endif /* __BSD_SORT_H__ */
