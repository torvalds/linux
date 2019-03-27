/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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
 *	@(#)extern.h	8.3 (Berkeley) 6/4/94
 * $FreeBSD$
 */

#include "../btree/extern.h"

int	 __rec_close(DB *);
int	 __rec_delete(const DB *, const DBT *, u_int);
int	 __rec_dleaf(BTREE *, PAGE *, u_int32_t);
int	 __rec_fd(const DB *);
int	 __rec_fmap(BTREE *, recno_t);
int	 __rec_fout(BTREE *);
int	 __rec_fpipe(BTREE *, recno_t);
int	 __rec_get(const DB *, const DBT *, DBT *, u_int);
int	 __rec_iput(BTREE *, recno_t, const DBT *, u_int);
int	 __rec_put(const DB *dbp, DBT *, const DBT *, u_int);
int	 __rec_ret(BTREE *, EPG *, recno_t, DBT *, DBT *);
EPG	*__rec_search(BTREE *, recno_t, enum SRCHOP);
int	 __rec_seq(const DB *, DBT *, DBT *, u_int);
int	 __rec_sync(const DB *, u_int);
int	 __rec_vmap(BTREE *, recno_t);
int	 __rec_vout(BTREE *);
int	 __rec_vpipe(BTREE *, recno_t);
