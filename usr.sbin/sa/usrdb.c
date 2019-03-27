/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/acct.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "extern.h"
#include "pathnames.h"

static int uid_compare(const DBT *, const DBT *);

static DB	*usracct_db;

/* Legacy format in AHZV1 units. */
struct userinfov1 {
	uid_t		ui_uid;			/* user id; for consistency */
	u_quad_t	ui_calls;		/* number of invocations */
	u_quad_t	ui_utime;		/* user time */
	u_quad_t	ui_stime;		/* system time */
	u_quad_t	ui_mem;			/* memory use */
	u_quad_t	ui_io;			/* number of disk i/o ops */
};

/*
 * Convert a v1 data record into the current version.
 * Return 0 if OK, -1 on error, setting errno.
 */
static int
v1_to_v2(DBT *key, DBT *data)
{
	struct userinfov1 uiv1;
	static struct userinfo uiv2;
	static uid_t uid;

	if (key->size != sizeof(u_long) || data->size != sizeof(uiv1)) {
		errno = EFTYPE;
		return (-1);
	}

	/* Convert key. */
	key->size = sizeof(uid_t);
	uid = (uid_t)*(u_long *)(key->data);
	key->data = &uid;

	/* Convert data. */
	memcpy(&uiv1, data->data, data->size);
	memset(&uiv2, 0, sizeof(uiv2));
	uiv2.ui_uid = uiv1.ui_uid;
	uiv2.ui_calls = uiv1.ui_calls;
	uiv2.ui_utime = ((double)uiv1.ui_utime / AHZV1) * 1000000;
	uiv2.ui_stime = ((double)uiv1.ui_stime / AHZV1) * 1000000;
	uiv2.ui_mem = uiv1.ui_mem;
	uiv2.ui_io = uiv1.ui_io;
	data->size = sizeof(uiv2);
	data->data = &uiv2;

	return (0);
}

/* Copy usrdb_file to in-memory usracct_db. */
int
usracct_init(void)
{
	BTREEINFO bti;

	bzero(&bti, sizeof bti);
	bti.compare = uid_compare;

	return (db_copy_in(&usracct_db, usrdb_file, "user accounting",
	    &bti, v1_to_v2));
}

void
usracct_destroy(void)
{
	db_destroy(usracct_db, "user accounting");
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
	key.size = sizeof uid;

	rv = DB_GET(usracct_db, &key, &data, 0);
	if (rv < 0) {
		warn("get key %u from user accounting stats", uid);
		return (-1);
	} else if (rv == 0) {	/* it's there; copy whole thing */
		/* add the old data to the new data */
		bcopy(data.data, &newui, data.size);
		if (newui.ui_uid != uid) {
			warnx("key %u != expected record number %u",
			    newui.ui_uid, uid);
			warnx("inconsistent user accounting stats");
			return (-1);
		}
	} else {		/* it's not there; zero it and copy the key */
		bzero(&newui, sizeof newui);
		newui.ui_uid = ci->ci_uid;
	}

	newui.ui_calls += ci->ci_calls;
	newui.ui_utime += ci->ci_utime;
	newui.ui_stime += ci->ci_stime;
	newui.ui_mem += ci->ci_mem;
	newui.ui_io += ci->ci_io;

	data.data = &newui;
	data.size = sizeof newui;
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

/* Copy in-memory usracct_db to usrdb_file. */
int
usracct_update(void)
{
	BTREEINFO bti;

	bzero(&bti, sizeof bti);
	bti.compare = uid_compare;

	return (db_copy_out(usracct_db, usrdb_file, "user accounting",
	    &bti));
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

		printf("%-*s %9ju ", MAXLOGNAME - 1,
		    user_from_uid(ui->ui_uid, 0), (uintmax_t)ui->ui_calls);

		t = (ui->ui_utime + ui->ui_stime) / 1000000;
		if (t < 0.000001)		/* kill divide by zero */
			t = 0.000001;

		printf("%12.2f%s ", t / 60.0, "cpu");

		/* ui->ui_calls is always != 0 */
		if (dflag)
			printf("%12.0f%s",
			    ui->ui_io / ui->ui_calls, "avio");
		else
			printf("%12.0f%s", ui->ui_io, "tio");

		/* t is always >= 0.000001; see above. */
		if (kflag)
			printf("%12.0f%s", ui->ui_mem / t, "k");
		else
			printf("%12.0f%s", ui->ui_mem, "k*sec");

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

	bcopy(k1->data, &d1, sizeof d1);
	bcopy(k2->data, &d2, sizeof d2);

	if (d1 < d2)
		return -1;
	else if (d1 == d2)
		return 0;
	else
		return 1;
}
