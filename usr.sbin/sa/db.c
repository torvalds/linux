/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Diomidis Spinellis
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/acct.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <db.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "extern.h"

/* Key used to store the version of the database data elements. */
static char VERSION_KEY[] = "\0VERSION";

/*
 * Create the in-memory database, *mdb.
 * If iflag is not set, fill-in mdb with the records of the disk-based
 * database dbname.
 * Upgrade old-version records by calling v1_to_v2.
 * Return 0 if OK, -1 on error.
 */
int
db_copy_in(DB **mdb, const char *dbname, const char *uname, BTREEINFO *bti,
    int (*v1_to_v2)(DBT *key, DBT *data))
{
	DBT key, data;
	DB *ddb;
	int error, rv, version;

	if ((*mdb = dbopen(NULL, O_RDWR, 0, DB_BTREE, bti)) == NULL)
		return (-1);

	if (iflag)
		return (0);

	if ((ddb = dbopen(dbname, O_RDONLY, 0, DB_BTREE, bti)) == NULL) {
		if (errno == ENOENT)
			return (0);
		warn("retrieving %s summary", uname);
		db_destroy(*mdb, uname);
		return (-1);
	}

	error = 0;

	/* Obtain/set version. */
	version = 1;
	key.data = (void*)&VERSION_KEY;
	key.size = sizeof(VERSION_KEY);

	rv = DB_GET(ddb, &key, &data, 0);
	if (rv < 0) {
		warn("get version key from %s stats", uname);
		error = -1;
		goto closeout;
	} else if (rv == 0) {	/* It's there; verify version. */
		if (data.size != sizeof(version)) {
			warnx("invalid version size %zd in %s",
			    data.size, uname);
			error = -1;
			goto closeout;
		}
		memcpy(&version, data.data, data.size);
		if (version != 2) {
			warnx("unsupported version %d in %s",
			    version, uname);
			error = -1;
			goto closeout;
		}
	}

	for (rv = DB_SEQ(ddb, &key, &data, R_FIRST); rv == 0;
	    rv = DB_SEQ(ddb, &key, &data, R_NEXT)) {

		/* See if this is a version record. */
		if (key.size == sizeof(VERSION_KEY) &&
		    memcmp(key.data, VERSION_KEY, sizeof(VERSION_KEY)) == 0)
			continue;

		/* Convert record from v1, if needed. */
		if (version == 1 && v1_to_v2(&key, &data) < 0) {
			warn("converting %s stats", uname);
			error = -1;
			goto closeout;
		}

		/* Copy record to the in-memory database. */
		if ((rv = DB_PUT(*mdb, &key, &data, 0)) < 0) {
			warn("initializing %s stats", uname);
			error = -1;
			goto closeout;
		}
	}
	if (rv < 0) {
		warn("retrieving %s summary", uname);
		error = -1;
	}

closeout:
	if (DB_CLOSE(ddb) < 0) {
		warn("closing %s summary", uname);
		error = -1;
	}

	if (error)
		db_destroy(*mdb, uname);
	return (error);
}

/*
 * Save the in-memory database mdb to the disk database dbname.
 * Return 0 if OK, -1 on error.
 */
int
db_copy_out(DB *mdb, const char *dbname, const char *uname, BTREEINFO *bti)
{
	DB *ddb;
	DBT key, data;
	int error, rv, version;

	if ((ddb = dbopen(dbname, O_RDWR|O_CREAT|O_TRUNC, 0644,
	    DB_BTREE, bti)) == NULL) {
		warn("creating %s summary", uname);
		return (-1);
	}

	error = 0;

	for (rv = DB_SEQ(mdb, &key, &data, R_FIRST);
	    rv == 0; rv = DB_SEQ(mdb, &key, &data, R_NEXT)) {
		if ((rv = DB_PUT(ddb, &key, &data, 0)) < 0) {
			warn("saving %s summary", uname);
			error = -1;
			goto out;
		}
	}
	if (rv < 0) {
		warn("retrieving %s stats", uname);
		error = -1;
	}

out:
	/* Add a version record. */
	key.data = (void*)&VERSION_KEY;
	key.size = sizeof(VERSION_KEY);
	version = 2;
	data.data = &version;
	data.size = sizeof(version);
	if ((rv = DB_PUT(ddb, &key, &data, 0)) < 0) {
		warn("add version record to %s stats", uname);
		error = -1;
	} else if (rv == 1) {
		warnx("duplicate version record in %s stats", uname);
		error = -1;
	}

	if (DB_SYNC(ddb, 0) < 0) {
		warn("syncing %s summary", uname);
		error = -1;
	}
	if (DB_CLOSE(ddb) < 0) {
		warn("closing %s summary", uname);
		error = -1;
	}
	return error;
}

void
db_destroy(DB *db, const char *uname)
{
	if (DB_CLOSE(db) < 0)
		warn("destroying %s stats", uname);
}
