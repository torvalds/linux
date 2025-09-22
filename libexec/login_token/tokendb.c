/*	$OpenBSD: tokendb.c,v 1.11 2019/06/28 13:32:53 deraadt Exp $	*/

/*-
 * Copyright (c) 1995 Migration Associates Corp. All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Berkeley Software Design,
 *      Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: tokendb.c,v 1.1 1996/08/26 20:13:10 prb Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <ctype.h>
#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "token.h"
#include "tokendb.h"

static	DB	*tokendb;

/*
 * Static function prototypes
 */

static	int	tokendb_open(void);
static	void	tokendb_close(void);

/*
 * Retrieve a user record from the token database file
 */

int
tokendb_getrec(char *username, TOKENDB_Rec *tokenrec)
{
	DBT	key;
	DBT	data;
	int	status = 0;

	key.data = username;
	key.size = strlen(username) + 1;
	memset(&data, 0, sizeof(data));

	if (tokendb_open())
		return(-1);

	status = (tokendb->get)(tokendb, &key, &data, 0);
	switch (status) {
	case 1:
		tokendb_close();
		return(ENOENT);
	case -1:
		tokendb_close();
		return(-1);
	}
	memcpy(tokenrec, data.data, sizeof(TOKENDB_Rec));
	if ((tokenrec->flags & TOKEN_USEMODES) == 0)
		tokenrec->mode = tt->modes & ~TOKEN_RIM;
	tokendb_close();
	return (0);
}

/*
 * Put a user record to the token database file.
 */

int
tokendb_putrec(char *username, TOKENDB_Rec *tokenrec)
{
	DBT	key;
	DBT	data;
	int	status = 0;

	key.data = username;
	key.size = strlen(username) + 1;

	if (tokenrec->mode)
		tokenrec->flags |= TOKEN_USEMODES;
	data.data = tokenrec;
	data.size = sizeof(TOKENDB_Rec);

	if (!tokendb_open()) {
		if (flock((tokendb->fd)(tokendb), LOCK_EX)) {
			tokendb_close();
			return (-1);
		}
		status = (tokendb->put)(tokendb, &key, &data, 0);
	}
	tokendb_close();
	return (status);
}

/*
 * Remove a user record from the token database file.
 */

int
tokendb_delrec(char *username)
{
	DBT	key;
	int	status = 0;

	key.data = username;
	key.size = strlen(username) + 1;

	if (!tokendb_open()) {
		if (flock((tokendb->fd)(tokendb), LOCK_EX)) {
			tokendb_close();
			return (-1);
		}
		status = (tokendb->del)(tokendb, &key, 0);
	}
	tokendb_close();
	return (status);
}

/*
 * Open the token database.  In order to expedite access in
 * heavily loaded conditions, we employ a N1 lock method.
 * Updates should be brief, so all locks wait infinitely.
 * Wait for a read (shared) lock as all updates read first.
 */

static	int
tokendb_open(void)
{
	int	must_set_perms = 0;
	int	must_set_mode = 0;
	struct	group	*grp;
	struct	stat	statb;

	if ((grp = getgrnam(TOKEN_GROUP)) == NULL) {
		printf("Missing %s group, authentication disabled\n",
		    TOKEN_GROUP);
		fflush(stdout);
		syslog(LOG_ALERT,
		    "the %s group is missing, token authentication disabled",
		    TOKEN_GROUP);
		return (-1);
	}

	if (stat(tt->db, &statb) == -1) {
		if (errno != ENOENT)
			return (-1);
		must_set_perms++;
	} else {
		if (statb.st_uid != 0 || statb.st_gid != grp->gr_gid) {
#ifdef PARANOID
			printf("Authentication disabled\n");
			fflush(stdout);
			syslog(LOG_ALERT,
			    "POTENTIAL COMPROMISE of %s. Owner was %u, "
			    "Group was %u", tt->db, statb.st_uid, statb.st_gid);
			return (-1);
#else
			must_set_perms++;
#endif
		}
		if ((statb.st_mode & 0777) != 0640) {
#ifdef PARANOID
			printf("Authentication disabled\n");
			fflush(stdout);
			syslog(LOG_ALERT,
			    "POTENTIAL COMPROMISE of %s. Mode was %o",
			    tt->db, statb.st_mode);
			return (-1);
#else
			must_set_mode++;
#endif
		}
	}
	if (!(tokendb =
	    dbopen(tt->db, O_CREAT | O_RDWR, 0640, DB_BTREE, 0)) )
		return (-1);

	if (flock((tokendb->fd)(tokendb), LOCK_SH)) {
		(tokendb->close)(tokendb);
		return (-1);
	}
	if (must_set_perms && fchown((tokendb->fd)(tokendb), 0, grp->gr_gid))
		syslog(LOG_INFO,
		    "Can't set owner/group of %s errno=%m", tt->db);
	if (must_set_mode && fchmod((tokendb->fd)(tokendb), 0640))
		syslog(LOG_INFO,
		    "Can't set mode of %s errno=%m", tt->db);

	return (0);
}

/*
 * Close the token database.  We are holding an unknown lock.
 * Release it, then close the db. Since little can be done
 * about errors, we ignore them.
 */

static	void
tokendb_close(void)
{
	if (tokendb) {
		(void)flock((tokendb->fd)(tokendb), LOCK_UN);
		(tokendb->close)(tokendb);
		tokendb = NULL;
	}
}

/*
 * Retrieve the first user record from the database, leaving the
 * database open for the next retrieval. If the march thru the
 * the database is aborted before end-of-file, the caller should
 * call tokendb_close to release the read lock.
 */

int
tokendb_firstrec(int reverse_flag, TOKENDB_Rec *tokenrec)
{
	DBT	key;
	DBT	data;
	int	status = 0;

	memset(&data, 0, sizeof(data));

	if (!tokendb_open()) {
		status = (tokendb->seq)(tokendb, &key, &data,
				reverse_flag ? R_LAST : R_FIRST);
	}
	if (status) {
		tokendb_close();
		return (status);
	}
	if (!data.data) {
		tokendb_close();
		return (ENOENT);
	}
	memcpy(tokenrec, data.data, sizeof(TOKENDB_Rec));
	if ((tokenrec->flags & TOKEN_USEMODES) == 0)
		tokenrec->mode = tt->modes & ~TOKEN_RIM;
	return (0);
}

/*
 * Retrieve the next sequential user record from the database. Close
 * the database only on end-of-file or error.
 */


int
tokendb_nextrec(int reverse_flag, TOKENDB_Rec *tokenrec)
{
	DBT	key;
	DBT	data;
	int	status;

	memset(&data, 0, sizeof(data));

	status = (tokendb->seq)(tokendb, &key, &data,
		reverse_flag ? R_PREV : R_NEXT);

	if (status) {
		tokendb_close();
		return (status);
	}
	if (!data.data) {
		tokendb_close();
		return (ENOENT);
	}
	memcpy(tokenrec, data.data, sizeof(TOKENDB_Rec));
	if ((tokenrec->flags & TOKEN_USEMODES) == 0)
		tokenrec->mode = tt->modes & ~TOKEN_RIM;
	return (0);
}

/*
 * Retrieve and lock a user record.  Since there are no facilities in
 * BSD for record locking, we hack a bit lock into the user record.
 */

int
tokendb_lockrec(char *username, TOKENDB_Rec *tokenrec, unsigned recflags)
{
	DBT	key;
	DBT	data;
	int	status;

	key.data = username;
	key.size = strlen(username) + 1;
	memset(&data, 0, sizeof(data));

	if (tokendb_open())
		return(-1);

	if (flock((tokendb->fd)(tokendb), LOCK_EX)) {
		tokendb_close();
		return(-1);
	}
	switch ((tokendb->get)(tokendb, &key, &data, 0)) {
	case 1:
		tokendb_close();
		return (ENOENT);
	case -1:
		tokendb_close();
		return(-1);
	}
	memcpy(tokenrec, data.data, sizeof(TOKENDB_Rec));

	if ((tokenrec->flags & TOKEN_LOCKED)||(tokenrec->flags & recflags)) {
		tokendb_close();
		return(1);
	}
	data.data = tokenrec;
	data.size = sizeof(TOKENDB_Rec);

	time(&tokenrec->lock_time);
	tokenrec->flags |= recflags;
	status = (tokendb->put)(tokendb, &key, &data, 0);
	tokendb_close();
	if (status)
		return(-1);
	if ((tokenrec->flags & TOKEN_USEMODES) == 0)
		tokenrec->mode = tt->modes & ~TOKEN_RIM;

	return(0);
}

