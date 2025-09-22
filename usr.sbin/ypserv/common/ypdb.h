/*	$OpenBSD: ypdb.h,v 1.9 2003/07/15 06:10:46 deraadt Exp $ */

/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
 *
 * This code is derived from ndbm module of BSD4.4 db (hash) by
 * Mats O Jansson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _YPDB_H_
#define _YPDB_H_

#ifndef _DB_H_
#include <db.h>
#endif

#define YPDB_SUFFIX	".db"

/* Flags to ypdb_store(). */
#define YPDB_INSERT	0
#define YPDB_REPLACE	1

#ifndef DATUM
typedef struct {
	char	*dptr;
	int	dsize;
} datum;
#define DATUM
#endif

typedef DB DBM;

__BEGIN_DECLS
void	 ypdb_close(DBM *);
datum	 ypdb_fetch(DBM *, datum);
datum	 ypdb_firstkey(DBM *);
datum	 ypdb_nextkey(DBM *);
datum	 ypdb_setkey(DBM *, datum);
DBM	*ypdb_open(const char *, int, int);
DBM	*ypdb_open_suf(const char *, int, int);
int	ypdb_store(DBM *, datum, datum, int);

__END_DECLS

#endif /* !_YPDB_H_ */
