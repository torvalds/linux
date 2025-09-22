/*	$OpenBSD: ndbm.c,v 1.28 2023/02/17 17:59:36 miod Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
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
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <ndbm.h>
#include "hash.h"

/*
 *
 * This package provides dbm and ndbm compatible interfaces to DB.
 * First are the DBM routines, which call the NDBM routines, and
 * the NDBM routines, which call the DB routines.
 */

static DBM *_dbm_open(const char *, const char *, int, mode_t);

/*
 * Returns:
 * 	*DBM on success
 *	 NULL on failure
 */
static DBM *
_dbm_open(const char *file, const char *suff, int flags, mode_t mode)
{
	HASHINFO info;
	char path[PATH_MAX];
	int len;

	len = snprintf(path, sizeof path, "%s%s", file, suff);
	if (len < 0 || len >= sizeof path) {
		errno = ENAMETOOLONG;
		return (NULL);
	}
	/* O_WRONLY not supported by db(3) but traditional ndbm allowed it. */
	if ((flags & O_ACCMODE) == O_WRONLY) {
		flags &= ~O_WRONLY;
		flags |= O_RDWR;
	}
	info.bsize = 4096;
	info.ffactor = 40;
	info.nelem = 1;
	info.cachesize = 0;
	info.hash = NULL;
	info.lorder = 0;
	return ((DBM *)__hash_open(path, flags, mode, &info, 0));
}

/*
 * Returns:
 * 	*DBM on success
 *	 NULL on failure
 */
DBM *
dbm_open(const char *file, int flags, mode_t mode)
{

	return(_dbm_open(file, DBM_SUFFIX, flags, mode));
}

/*
 * Returns:
 *	Nothing.
 */
void
dbm_close(DBM *db)
{

	(void)(db->close)(db);
}
DEF_WEAK(dbm_close);

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */
datum
dbm_fetch(DBM *db, datum key)
{
	datum retdata;
	int status;
	DBT dbtkey, dbtretdata;

	dbtkey.data = key.dptr;
	dbtkey.size = key.dsize;
	status = (db->get)(db, &dbtkey, &dbtretdata, 0);
	if (status) {
		dbtretdata.data = NULL;
		dbtretdata.size = 0;
	}
	retdata.dptr = dbtretdata.data;
	retdata.dsize = dbtretdata.size;
	return (retdata);
}
DEF_WEAK(dbm_fetch);

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */
datum
dbm_firstkey(DBM *db)
{
	int status;
	datum retkey;
	DBT dbtretkey, dbtretdata;

	status = (db->seq)(db, &dbtretkey, &dbtretdata, R_FIRST);
	if (status)
		dbtretkey.data = NULL;
	retkey.dptr = dbtretkey.data;
	retkey.dsize = dbtretkey.size;
	return (retkey);
}
DEF_WEAK(dbm_firstkey);

/*
 * Returns:
 *	DATUM on success
 *	NULL on failure
 */
datum
dbm_nextkey(DBM *db)
{
	int status;
	datum retkey;
	DBT dbtretkey, dbtretdata;

	status = (db->seq)(db, &dbtretkey, &dbtretdata, R_NEXT);
	if (status)
		dbtretkey.data = NULL;
	retkey.dptr = dbtretkey.data;
	retkey.dsize = dbtretkey.size;
	return (retkey);
}
DEF_WEAK(dbm_nextkey);

/*
 * Returns:
 *	 0 on success
 *	<0 on failure
 */
int
dbm_delete(DBM *db, datum key)
{
	int status;
	DBT dbtkey;

	dbtkey.data = key.dptr;
	dbtkey.size = key.dsize;
	status = (db->del)(db, &dbtkey, 0);
	if (status)
		return (-1);
	else
		return (0);
}
DEF_WEAK(dbm_delete);

/*
 * Returns:
 *	 0 on success
 *	<0 on failure
 *	 1 if DBM_INSERT and entry exists
 */
int
dbm_store(DBM *db, datum key, datum data, int flags)
{
	DBT dbtkey, dbtdata;

	dbtkey.data = key.dptr;
	dbtkey.size = key.dsize;
	dbtdata.data = data.dptr;
	dbtdata.size = data.dsize;
	return ((db->put)(db, &dbtkey, &dbtdata,
	    (flags == DBM_INSERT) ? R_NOOVERWRITE : 0));
}
DEF_WEAK(dbm_store);

int
dbm_error(DBM *db)
{
	HTAB *hp;

	hp = (HTAB *)db->internal;
	return (hp->err);
}

int
dbm_clearerr(DBM *db)
{
	HTAB *hp;

	hp = (HTAB *)db->internal;
	hp->err = 0;
	return (0);
}

int
dbm_dirfno(DBM *db)
{

	return(((HTAB *)db->internal)->fp);
}

int
dbm_rdonly(DBM *dbp)
{
	HTAB *hashp = (HTAB *)dbp->internal;

	/* Could use DBM_RDONLY instead if we wanted... */
	return ((hashp->flags & O_ACCMODE) == O_RDONLY);
}
DEF_WEAK(dbm_rdonly);
