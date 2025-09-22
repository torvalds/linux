/*	$OpenBSD: usrdb.c,v 1.10 2016/08/14 22:29:01 krw Exp $	*/
/*
 * Copyright (c) 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/acct.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include "extern.h"
#include "pathnames.h"

static int uid_compare(const DBT *, const DBT *);

static DB	*usracct_db;

int
usracct_init(void)
{
	DB *saved_usracct_db;
	BTREEINFO bti;
	int error;

	memset(&bti, 0, sizeof(bti));
	bti.compare = uid_compare;

	usracct_db = dbopen(NULL, O_RDWR, 0, DB_BTREE, &bti);
	if (usracct_db == NULL)
		return (-1);

	error = 0;
	if (!iflag) {
		DBT key, data;
		int serr, nerr;

		saved_usracct_db = dbopen(_PATH_USRACCT, O_RDONLY, 0, DB_BTREE,
		    &bti);
		if (saved_usracct_db == NULL) {
			error = (errno == ENOENT) ? 0 : -1;
			if (error)
				warn("retrieving user accounting summary");
			goto out;
		}

		serr = DB_SEQ(saved_usracct_db, &key, &data, R_FIRST);
		if (serr < 0) {
			warn("retrieving user accounting summary");
			error = -1;
			goto closeout;
		}
		while (serr == 0) {
			nerr = DB_PUT(usracct_db, &key, &data, 0);
			if (nerr < 0) {
				warn("initializing user accounting stats");
				error = -1;
				break;
			}

			serr = DB_SEQ(saved_usracct_db, &key, &data, R_NEXT);
			if (serr < 0) {
				warn("retrieving user accounting summary");
				error = -1;
				break;
			}
		}

closeout:
		if (DB_CLOSE(saved_usracct_db) < 0) {
			warn("closing user accounting summary");
			error = -1;
		}
	}

out:
	if (error != 0)
		usracct_destroy();
	return (error);
}

void
usracct_destroy(void)
{
	if (DB_CLOSE(usracct_db) < 0)
		warn("destroying user accounting stats");
}

int
usracct_add(const struct cmdinfo *ci)
{
	DBT key, data;
	struct userinfo newui;
	uid_t uid;
	int rv;

	uid = ci->ci_uid;
	key.data = &uid;
	key.size = sizeof(uid);

	rv = DB_GET(usracct_db, &key, &data, 0);
	if (rv < 0) {
		warn("get key %u from user accounting stats", uid);
		return (-1);
	} else if (rv == 0) {	/* it's there; copy whole thing */
		/* add the old data to the new data */
		memcpy(&newui, data.data, data.size);
		if (newui.ui_uid != uid) {
			warnx("key %u != expected record number %u",
			    newui.ui_uid, uid);
			warnx("inconsistent user accounting stats");
			return (-1);
		}
	} else {		/* it's not there; zero it and copy the key */
		memset(&newui, 0, sizeof(newui));
		newui.ui_uid = ci->ci_uid;
	}

	newui.ui_calls += ci->ci_calls;
	newui.ui_utime += ci->ci_utime;
	newui.ui_stime += ci->ci_stime;
	newui.ui_mem += ci->ci_mem;
	newui.ui_io += ci->ci_io;

	data.data = &newui;
	data.size = sizeof(newui);
	rv = DB_PUT(usracct_db, &key, &data, 0);
	if (rv < 0) {
		warn("add key %u to user accounting stats", uid);
		return (-1);
	} else if (rv != 0) {
		warnx("DB_PUT returned 1");
		return (-1);
	}

	return (0);
}

int
usracct_update(void)
{
	DB *saved_usracct_db;
	DBT key, data;
	BTREEINFO bti;
	int error, serr, nerr;

	memset(&bti, 0, sizeof(bti));
	bti.compare = uid_compare;

	saved_usracct_db = dbopen(_PATH_USRACCT, O_RDWR|O_CREAT|O_TRUNC, 0644,
	    DB_BTREE, &bti);
	if (saved_usracct_db == NULL) {
		warn("creating user accounting summary");
		return (-1);
	}

	error = 0;

	serr = DB_SEQ(usracct_db, &key, &data, R_FIRST);
	if (serr < 0) {
		warn("retrieving user accounting stats");
		error = -1;
	}
	while (serr == 0) {
		nerr = DB_PUT(saved_usracct_db, &key, &data, 0);
		if (nerr < 0) {
			warn("saving user accounting summary");
			error = -1;
			break;
		}

		serr = DB_SEQ(usracct_db, &key, &data, R_NEXT);
		if (serr < 0) {
			warn("retrieving user accounting stats");
			error = -1;
			break;
		}
	}

	if (DB_SYNC(saved_usracct_db, 0) < 0) {
		warn("syncing process accounting summary");
		error = -1;
	}
	if (DB_CLOSE(saved_usracct_db) < 0) {
		warn("closing process accounting summary");
		error = -1;
	}
	return error;
}

void
usracct_print(void)
{
	DBT key, data;
	struct userinfo uistore, *ui = &uistore;
	double t;
	int rv;

	rv = DB_SEQ(usracct_db, &key, &data, R_FIRST);
	if (rv < 0)
		warn("retrieving user accounting stats");

	while (rv == 0) {
		memcpy(ui, data.data, sizeof(struct userinfo));

		printf("%-8s %9llu ",
		    user_from_uid(ui->ui_uid, 0), ui->ui_calls);

		t = (double) (ui->ui_utime + ui->ui_stime) /
		    (double) AHZ;
		if (t < 0.0001)		/* kill divide by zero */
			t = 0.0001;

		printf("%12.2f%s ", t / 60.0, "cpu");

		/* ui->ui_calls is always != 0 */
		if (dflag)
			printf("%12llu%s", ui->ui_io / ui->ui_calls, "avio");
		else
			printf("%12llu%s", ui->ui_io, "tio");

		/* t is always >= 0.0001; see above */
		if (kflag)
			printf("%12.0f%s", ui->ui_mem / t, "k");
		else
			printf("%12llu%s", ui->ui_mem, "k*sec");

		printf("\n");

		rv = DB_SEQ(usracct_db, &key, &data, R_NEXT);
		if (rv < 0)
			warn("retrieving user accounting stats");
	}
}

static int
uid_compare(const DBT *k1, const DBT *k2)
{
	uid_t d1, d2;

	memcpy(&d1, k1->data, sizeof(d1));
	memcpy(&d2, k2->data, sizeof(d2));

	if (d1 < d2)
		return -1;
	else if (d1 == d2)
		return 0;
	else
		return 1;
}
