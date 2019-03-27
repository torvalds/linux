/* $FreeBSD$ */
/* $NetBSD: citrus_hash.h,v 1.3 2004/01/02 21:49:35 itojun Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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

#ifndef _CITRUS_HASH_H_
#define _CITRUS_HASH_H_

#define _CITRUS_HASH_ENTRY(type) LIST_ENTRY(type)
#define _CITRUS_HASH_HEAD(headname, type, hashsize)	\
struct headname {					\
	LIST_HEAD(, type)	chh_table[hashsize];	\
}
#define _CITRUS_HASH_INIT(head, hashsize)			\
do {								\
	int _ch_loop;						\
								\
	for (_ch_loop = 0; _ch_loop < hashsize; _ch_loop++)	\
		LIST_INIT(&(head)->chh_table[_ch_loop]);	\
} while (0)
#define _CITRUS_HASH_REMOVE(elm, field)	LIST_REMOVE(elm, field)
#define _CITRUS_HASH_INSERT(head, elm, field, hashval)		\
    LIST_INSERT_HEAD(&(head)->chh_table[hashval], elm, field)
#define _CITRUS_HASH_SEARCH(head, elm, field, matchfunc, key, hashval)	\
do {									\
	LIST_FOREACH((elm), &(head)->chh_table[hashval], field)		\
		if (matchfunc((elm), key) == 0)				\
			break;						\
} while (0)

__BEGIN_DECLS
int	 _citrus_string_hash_func(const char *, int);
__END_DECLS

#endif
