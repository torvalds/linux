/*	$OpenBSD: ypdb.c,v 1.12 2015/01/16 06:40:22 deraadt Exp $ */

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

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include <rpcsvc/yp.h>

#include "ypdb.h"

/*
 * Returns:
 *	*DBM on success
 *	NULL on failure
 */
extern DBM *
ypdb_open(const char *file, int flags, int mode)
{
	BTREEINFO info;
	char path[PATH_MAX];
	DBM *db;

	memset(&info, 0, sizeof info);
	info.flags = 0;
	info.cachesize = 0;
	info.maxkeypage = 0;
	info.minkeypage = 0;
	info.psize = 0;
	info.compare = NULL;
	info.prefix = NULL;
	info.lorder = 0;
	snprintf(path, sizeof(path), "%s%s", file, YPDB_SUFFIX);
	db = (DBM *)dbopen(path, flags, mode, DB_BTREE, (void *)&info);
	return (db);
}

/*
 * Returns:
 *	*DBM on success
 *	NULL on failure
 */
extern DBM *
ypdb_open_suf(const char *file, int flags, int mode)
{
	BTREEINFO info;
	DBM *db;

	memset(&info, 0, sizeof info);
	info.flags = 0;
	info.cachesize = 0;
	info.maxkeypage = 0;
	info.minkeypage = 0;
	info.psize = 0;
	info.compare = NULL;
	info.prefix = NULL;
	info.lorder = 0;
	db = (DBM *)dbopen(file, flags, mode, DB_BTREE, (void *)&info);
	return (db);
}

extern void
ypdb_close(DBM *db)
{
	(void)(db->close)(db);
}

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */
extern datum
ypdb_fetch(DBM *db, datum key)
{
	datum retval;
	DBT nk, nd;
	int status;

	nk.data = key.dptr;
	nk.size = key.dsize;

	status = (db->get)(db, &nk, &nd, 0);
	if (status) {
		retval.dptr = NULL;
		retval.dsize = 0;
	} else {
		retval.dptr = nd.data;
		retval.dsize = nd.size;
	}
	return (retval);
}

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */

extern datum
ypdb_firstkey(DBM *db)
{
	int status;
	datum retkey;
	DBT nk, nd;

	status = (db->seq)(db, &nk, &nd, R_FIRST);
	if (status) {
		retkey.dptr = NULL;
		retkey.dsize = 0;
	} else {
		retkey.dptr = nk.data;
		retkey.dsize = nk.size;
	}
	return (retkey);
}

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */

extern datum
ypdb_nextkey(DBM *db)
{
	int status;
	datum retkey;
	DBT nk, nd;

	status = (db->seq)(db, &nk, &nd, R_NEXT);
	if (status) {
		retkey.dptr = NULL;
		retkey.dsize = 0;
	} else {
		retkey.dptr = nk.data;
		retkey.dsize = nk.size;
	}
	return (retkey);
}

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */

extern datum
ypdb_setkey(DBM *db, datum key)
{
	int status;
	DBT nk, nd;

	nk.data = key.dptr;
	nk.size = key.dsize;
	status = (db->seq)(db, &nk, &nd, R_CURSOR);
	if (status) {
		key.dptr = NULL;
		key.dsize = 0;
	}
	return (key);
}

/*
 * Returns:
 *	 0 on success
 *	<0 failure
 *	 1 if YPDB_INSERT and entry exists
 */
int
ypdb_store(DBM *db, datum key, datum content, int flags)
{
	DBT nk, nd;

	if (key.dsize > YPMAXRECORD || content.dsize > YPMAXRECORD)
		return -1;
	nk.data = key.dptr;
	nk.size = key.dsize;
	nd.data = content.dptr;
	nd.size = content.dsize;
	return ((db->put)(db, &nk, &nd,
	    (flags == YPDB_INSERT) ? R_NOOVERWRITE : 0));
}
