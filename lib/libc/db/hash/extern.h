/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)extern.h	8.4 (Berkeley) 6/16/94
 * $FreeBSD$
 */

BUFHEAD	*__add_ovflpage(HTAB *, BUFHEAD *);
int	 __addel(HTAB *, BUFHEAD *, const DBT *, const DBT *);
int	 __big_delete(HTAB *, BUFHEAD *);
int	 __big_insert(HTAB *, BUFHEAD *, const DBT *, const DBT *);
int	 __big_keydata(HTAB *, BUFHEAD *, DBT *, DBT *, int);
int	 __big_return(HTAB *, BUFHEAD *, int, DBT *, int);
int	 __big_split(HTAB *, BUFHEAD *, BUFHEAD *, BUFHEAD *,
		int, u_int32_t, SPLIT_RETURN *);
int	 __buf_free(HTAB *, int, int);
void	 __buf_init(HTAB *, int);
u_int32_t	 __call_hash(HTAB *, char *, int);
int	 __delpair(HTAB *, BUFHEAD *, int);
int	 __expand_table(HTAB *);
int	 __find_bigpair(HTAB *, BUFHEAD *, int, char *, int);
u_int16_t	 __find_last_page(HTAB *, BUFHEAD **);
void	 __free_ovflpage(HTAB *, BUFHEAD *);
BUFHEAD	*__get_buf(HTAB *, u_int32_t, BUFHEAD *, int);
int	 __get_page(HTAB *, char *, u_int32_t, int, int, int);
int	 __ibitmap(HTAB *, int, int, int);
u_int32_t	 __log2(u_int32_t);
int	 __put_page(HTAB *, char *, u_int32_t, int, int);
void	 __reclaim_buf(HTAB *, BUFHEAD *);
int	 __split_page(HTAB *, u_int32_t, u_int32_t);

/* Default hash routine. */
extern u_int32_t (*__default_hash)(const void *, size_t);

#ifdef HASH_STATISTICS
extern int hash_accesses, hash_collisions, hash_expansions, hash_overflows;
#endif
