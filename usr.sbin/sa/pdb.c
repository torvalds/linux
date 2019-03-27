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

#include <sys/types.h>
#include <sys/acct.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "extern.h"
#include "pathnames.h"

static int check_junk(const struct cmdinfo *);
static void add_ci(const struct cmdinfo *, struct cmdinfo *);
static void print_ci(const struct cmdinfo *, const struct cmdinfo *);

static DB	*pacct_db;

/* Legacy format in AHZV1 units. */
struct cmdinfov1 {
	char		ci_comm[MAXCOMLEN+2];	/* command name (+ '*') */
	uid_t		ci_uid;			/* user id */
	u_quad_t	ci_calls;		/* number of calls */
	u_quad_t	ci_etime;		/* elapsed time */
	u_quad_t	ci_utime;		/* user time */
	u_quad_t	ci_stime;		/* system time */
	u_quad_t	ci_mem;			/* memory use */
	u_quad_t	ci_io;			/* number of disk i/o ops */
	u_int		ci_flags;		/* flags; see below */
};

/*
 * Convert a v1 data record into the current version.
 * Return 0 if OK, -1 on error, setting errno.
 */
static int
v1_to_v2(DBT *key __unused, DBT *data)
{
	struct cmdinfov1 civ1;
	static struct cmdinfo civ2;

	if (data->size != sizeof(civ1)) {
		errno = EFTYPE;
		return (-1);
	}
	memcpy(&civ1, data->data, data->size);
	memset(&civ2, 0, sizeof(civ2));
	memcpy(civ2.ci_comm, civ1.ci_comm, sizeof(civ2.ci_comm));
	civ2.ci_uid = civ1.ci_uid;
	civ2.ci_calls = civ1.ci_calls;
	civ2.ci_etime = ((double)civ1.ci_etime / AHZV1) * 1000000;
	civ2.ci_utime = ((double)civ1.ci_utime / AHZV1) * 1000000;
	civ2.ci_stime = ((double)civ1.ci_stime / AHZV1) * 1000000;
	civ2.ci_mem = civ1.ci_mem;
	civ2.ci_io = civ1.ci_io;
	civ2.ci_flags = civ1.ci_flags;
	data->size = sizeof(civ2);
	data->data = &civ2;
	return (0);
}

/* Copy pdb_file to in-memory pacct_db. */
int
pacct_init(void)
{
	return (db_copy_in(&pacct_db, pdb_file, "process accounting",
	    NULL, v1_to_v2));
}

void
pacct_destroy(void)
{
	db_destroy(pacct_db, "process accounting");
}

int
pacct_add(const struct cmdinfo *ci)
{
	DBT key, data;
	struct cmdinfo newci;
	char keydata[sizeof ci->ci_comm];
	int rv;

	bcopy(ci->ci_comm, &keydata, sizeof keydata);
	key.data = &keydata;
	key.size = strlen(keydata);

	rv = DB_GET(pacct_db, &key, &data, 0);
	if (rv < 0) {
		warn("get key %s from process accounting stats", ci->ci_comm);
		return (-1);
	} else if (rv == 0) {	/* it's there; copy whole thing */
		/* XXX compare size if paranoid */
		/* add the old data to the new data */
		bcopy(data.data, &newci, data.size);
	} else {		/* it's not there; zero it and copy the key */
		bzero(&newci, sizeof newci);
		bcopy(key.data, newci.ci_comm, key.size);
	}

	add_ci(ci, &newci);

	data.data = &newci;
	data.size = sizeof newci;
	rv = DB_PUT(pacct_db, &key, &data, 0);
	if (rv < 0) {
		warn("add key %s to process accounting stats", ci->ci_comm);
		return (-1);
	} else if (rv == 1) {
		warnx("duplicate key %s in process accounting stats",
		    ci->ci_comm);
		return (-1);
	}

	return (0);
}

/* Copy in-memory pacct_db to pdb_file. */
int
pacct_update(void)
{
	return (db_copy_out(pacct_db, pdb_file, "process accounting",
	    NULL));
}

void
pacct_print(void)
{
	BTREEINFO bti;
	DBT key, data, ndata;
	DB *output_pacct_db;
	struct cmdinfo *cip, ci, ci_total, ci_other, ci_junk;
	int rv;

	bzero(&ci_total, sizeof ci_total);
	strcpy(ci_total.ci_comm, "");
	bzero(&ci_other, sizeof ci_other);
	strcpy(ci_other.ci_comm, "***other");
	bzero(&ci_junk, sizeof ci_junk);
	strcpy(ci_junk.ci_comm, "**junk**");

	/*
	 * Retrieve them into new DB, sorted by appropriate key.
	 * At the same time, cull 'other' and 'junk'
	 */
	bzero(&bti, sizeof bti);
	bti.compare = sa_cmp;
	output_pacct_db = dbopen(NULL, O_RDWR, 0, DB_BTREE, &bti);
	if (output_pacct_db == NULL) {
		warn("couldn't sort process accounting stats");
		return;
	}

	ndata.data = NULL;
	ndata.size = 0;
	rv = DB_SEQ(pacct_db, &key, &data, R_FIRST);
	if (rv < 0)
		warn("retrieving process accounting stats");
	while (rv == 0) {
		cip = (struct cmdinfo *) data.data;
		bcopy(cip, &ci, sizeof ci);

		/* add to total */
		add_ci(&ci, &ci_total);

		if (vflag && ci.ci_calls <= cutoff &&
		    (fflag || check_junk(&ci))) {
			/* put it into **junk** */
			add_ci(&ci, &ci_junk);
			goto next;
		}
		if (!aflag &&
		    ((ci.ci_flags & CI_UNPRINTABLE) != 0 || ci.ci_calls <= 1)) {
			/* put into ***other */
			add_ci(&ci, &ci_other);
			goto next;
		}
		rv = DB_PUT(output_pacct_db, &data, &ndata, 0);
		if (rv < 0)
			warn("sorting process accounting stats");

next:		rv = DB_SEQ(pacct_db, &key, &data, R_NEXT);
		if (rv < 0)
			warn("retrieving process accounting stats");
	}

	/* insert **junk** and ***other */
	if (ci_junk.ci_calls != 0) {
		data.data = &ci_junk;
		data.size = sizeof ci_junk;
		rv = DB_PUT(output_pacct_db, &data, &ndata, 0);
		if (rv < 0)
			warn("sorting process accounting stats");
	}
	if (ci_other.ci_calls != 0) {
		data.data = &ci_other;
		data.size = sizeof ci_other;
		rv = DB_PUT(output_pacct_db, &data, &ndata, 0);
		if (rv < 0)
			warn("sorting process accounting stats");
	}

	/* print out the total */
	print_ci(&ci_total, &ci_total);

	/* print out; if reversed, print first (smallest) first */
	rv = DB_SEQ(output_pacct_db, &data, &ndata, rflag ? R_FIRST : R_LAST);
	if (rv < 0)
		warn("retrieving process accounting report");
	while (rv == 0) {
		cip = (struct cmdinfo *) data.data;
		bcopy(cip, &ci, sizeof ci);

		print_ci(&ci, &ci_total);

		rv = DB_SEQ(output_pacct_db, &data, &ndata,
		    rflag ? R_NEXT : R_PREV);
		if (rv < 0)
			warn("retrieving process accounting report");
	}
	DB_CLOSE(output_pacct_db);
}

static int
check_junk(const struct cmdinfo *cip)
{
	char *cp;
	size_t len;

	fprintf(stderr, "%s (%ju) -- ", cip->ci_comm, (uintmax_t)cip->ci_calls);
	cp = fgetln(stdin, &len);

	return (cp && (cp[0] == 'y' || cp[0] == 'Y')) ? 1 : 0;
}

static void
add_ci(const struct cmdinfo *fromcip, struct cmdinfo *tocip)
{
	tocip->ci_calls += fromcip->ci_calls;
	tocip->ci_etime += fromcip->ci_etime;
	tocip->ci_utime += fromcip->ci_utime;
	tocip->ci_stime += fromcip->ci_stime;
	tocip->ci_mem += fromcip->ci_mem;
	tocip->ci_io += fromcip->ci_io;
}

static void
print_ci(const struct cmdinfo *cip, const struct cmdinfo *totalcip)
{
	double t, c;
	int uflow;

	c = cip->ci_calls ? cip->ci_calls : 1;
	t = (cip->ci_utime + cip->ci_stime) / 1000000;
	if (t < 0.01) {
		t = 0.01;
		uflow = 1;
	} else
		uflow = 0;

	printf("%8ju ", (uintmax_t)cip->ci_calls);
	if (cflag) {
		if (cip != totalcip)
			printf(" %4.1f%%  ", cip->ci_calls /
			    (double)totalcip->ci_calls * 100);
		else
			printf(" %4s   ", "");
	}

	if (jflag)
		printf("%11.3fre ", cip->ci_etime / (1000000 * c));
	else
		printf("%11.3fre ", cip->ci_etime / (60.0 * 1000000));
	if (cflag) {
		if (cip != totalcip)
			printf(" %4.1f%%  ", cip->ci_etime /
			    totalcip->ci_etime * 100);
		else
			printf(" %4s   ", "");
	}

	if (!lflag) {
		if (jflag)
			printf("%11.3fcp ", t / (double) cip->ci_calls);
		else
			printf("%11.2fcp ", t / 60.0);
		if (cflag) {
			if (cip != totalcip)
				printf(" %4.1f%%  ",
				    (cip->ci_utime + cip->ci_stime) /
				    (totalcip->ci_utime + totalcip->ci_stime) *
				    100);
			else
				printf(" %4s   ", "");
		}
	} else {
		if (jflag)
			printf("%11.3fu ", cip->ci_utime / (1000000 * c));
		else
			printf("%11.2fu ", cip->ci_utime / (60.0 * 1000000));
		if (cflag) {
			if (cip != totalcip)
				printf(" %4.1f%%  ", cip->ci_utime /
				    (double)totalcip->ci_utime * 100);
			else
				printf(" %4s   ", "");
		}
		if (jflag)
			printf("%11.3fs ", cip->ci_stime / (1000000 * c));
		else
			printf("%11.2fs ", cip->ci_stime / (60.0 * 1000000));
		if (cflag) {
			if (cip != totalcip)
				printf(" %4.1f%%  ", cip->ci_stime /
				    (double)totalcip->ci_stime * 100);
			else
				printf(" %4s   ", "");
		}
	}

	if (tflag) {
		if (!uflow)
			printf("%8.2fre/cp ",
			    cip->ci_etime /
			    (cip->ci_utime + cip->ci_stime));
		else
			printf("*ignore*      ");
	}

	if (Dflag)
		printf("%10.0fio ", cip->ci_io);
	else
		printf("%8.0favio ", cip->ci_io / c);

	if (Kflag)
		printf("%10.0fk*sec ", cip->ci_mem);
	else
		printf("%8.0fk ", cip->ci_mem / t);

	printf("  %s\n", cip->ci_comm);
}
