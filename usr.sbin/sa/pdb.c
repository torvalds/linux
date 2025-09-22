/*	$OpenBSD: pdb.c,v 1.9 2016/08/14 22:29:01 krw Exp $	*/
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
#include <stdio.h>
#include <string.h>
#include "extern.h"
#include "pathnames.h"

static int check_junk(struct cmdinfo *);
static void add_ci(const struct cmdinfo *, struct cmdinfo *);
static void print_ci(const struct cmdinfo *, const struct cmdinfo *);

static DB	*pacct_db;

int
pacct_init(void)
{
	DB *saved_pacct_db;
	int error;

	pacct_db = dbopen(NULL, O_RDWR, 0, DB_BTREE, NULL);
	if (pacct_db == NULL)
		return (-1);

	error = 0;
	if (!iflag) {
		DBT key, data;
		int serr, nerr;

		saved_pacct_db = dbopen(_PATH_SAVACCT, O_RDONLY, 0, DB_BTREE,
		    NULL);
		if (saved_pacct_db == NULL) {
			error = errno == ENOENT ? 0 : -1;
			if (error)
				warn("retrieving process accounting summary");
			goto out;
		}

		serr = DB_SEQ(saved_pacct_db, &key, &data, R_FIRST);
		if (serr < 0) {
			warn("retrieving process accounting summary");
			error = -1;
			goto closeout;
		}
		while (serr == 0) {
			nerr = DB_PUT(pacct_db, &key, &data, 0);
			if (nerr < 0) {
				warn("initializing process accounting stats");
				error = -1;
				break;
			}

			serr = DB_SEQ(saved_pacct_db, &key, &data, R_NEXT);
			if (serr < 0) {
				warn("retrieving process accounting summary");
				error = -1;
				break;
			}
		}

closeout:	if (DB_CLOSE(saved_pacct_db) < 0) {
			warn("closing process accounting summary");
			error = -1;
		}
	}

out:	if (error != 0)
		pacct_destroy();
	return (error);
}

void
pacct_destroy(void)
{
	if (DB_CLOSE(pacct_db) < 0)
		warn("destroying process accounting stats");
}

int
pacct_add(const struct cmdinfo *ci)
{
	DBT key, data;
	struct cmdinfo newci;
	char keydata[sizeof(ci->ci_comm)];
	int rv;

	memcpy(&keydata, ci->ci_comm, sizeof(keydata));
	key.data = &keydata;
	key.size = strlen(keydata);

	rv = DB_GET(pacct_db, &key, &data, 0);
	if (rv < 0) {
		warn("get key %s from process accounting stats", ci->ci_comm);
		return (-1);
	} else if (rv == 0) {	/* it's there; copy whole thing */
		/* XXX compare size if paranoid */
		/* add the old data to the new data */
		memcpy(&newci, data.data, data.size);
	} else {		/* it's not there; zero it and copy the key */
		memset(&newci, 0, sizeof(newci));
		memcpy(newci.ci_comm, key.data, key.size);
	}

	add_ci(ci, &newci);

	data.data = &newci;
	data.size = sizeof(newci);
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

int
pacct_update(void)
{
	DB *saved_pacct_db;
	DBT key, data;
	int error, serr, nerr;

	saved_pacct_db = dbopen(_PATH_SAVACCT, O_RDWR|O_CREAT|O_TRUNC, 0644,
	    DB_BTREE, NULL);
	if (saved_pacct_db == NULL) {
		warn("creating process accounting summary");
		return (-1);
	}

	error = 0;

	serr = DB_SEQ(pacct_db, &key, &data, R_FIRST);
	if (serr < 0) {
		warn("retrieving process accounting stats");
		error = -1;
	}
	while (serr == 0) {
		nerr = DB_PUT(saved_pacct_db, &key, &data, 0);
		if (nerr < 0) {
			warn("saving process accounting summary");
			error = -1;
			break;
		}

		serr = DB_SEQ(pacct_db, &key, &data, R_NEXT);
		if (serr < 0) {
			warn("retrieving process accounting stats");
			error = -1;
			break;
		}
	}

	if (DB_SYNC(saved_pacct_db, 0) < 0) {
		warn("syncing process accounting summary");
		error = -1;
	}
	if (DB_CLOSE(saved_pacct_db) < 0) {
		warn("closing process accounting summary");
		error = -1;
	}
	return error;
}

void
pacct_print(void)
{
	BTREEINFO bti;
	DBT key, data, ndata;
	DB *output_pacct_db;
	struct cmdinfo ci, ci_total, ci_other, ci_junk;
	int rv;

	memset(&ci_total, 0, sizeof(ci_total));
	strlcpy(ci_total.ci_comm, "", sizeof ci_total.ci_comm);
	memset(&ci_other, 0, sizeof(ci_other));
	strlcpy(ci_other.ci_comm, "***other", sizeof ci_other.ci_comm);
	memset(&ci_junk, 0, sizeof(ci_junk));
	strlcpy(ci_junk.ci_comm, "**junk**", sizeof ci_junk.ci_comm);

	/*
	 * Retrieve them into new DB, sorted by appropriate key.
	 * At the same time, cull 'other' and 'junk'
	 */
	memset(&bti, 0, sizeof(bti));
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
		memcpy(&ci, data.data, sizeof(ci));

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
		data.size = sizeof(ci_junk);
		rv = DB_PUT(output_pacct_db, &data, &ndata, 0);
		if (rv < 0)
			warn("sorting process accounting stats");
	}
	if (ci_other.ci_calls != 0) {
		data.data = &ci_other;
		data.size = sizeof(ci_other);
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
		memcpy(&ci, data.data, sizeof(ci));

		print_ci(&ci, &ci_total);

		rv = DB_SEQ(output_pacct_db, &data, &ndata,
		    rflag ? R_NEXT : R_PREV);
		if (rv < 0)
			warn("retrieving process accounting report");
	}
	DB_CLOSE(output_pacct_db);
}

static int
check_junk(struct cmdinfo *cip)
{
	char *cp;
	size_t len;

	fprintf(stderr, "%s (%llu) -- ", cip->ci_comm, cip->ci_calls);
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
	t = (cip->ci_utime + cip->ci_stime) / (double) AHZ;
	if (t < 0.01) {
		t = 0.01;
		uflow = 1;
	} else
		uflow = 0;

	printf("%8llu ", cip->ci_calls);
	if (cflag) {
		if (cip != totalcip)
			printf(" %4.2f%%  ",
			    cip->ci_calls / (double) totalcip->ci_calls);
		else
			printf(" %4s   ", "");
	}

	if (jflag)
		printf("%11.2fre ", cip->ci_etime / (double) (AHZ * c));
	else
		printf("%11.2fre ", cip->ci_etime / (60.0 * AHZ));
	if (cflag) {
		if (cip != totalcip)
			printf(" %4.2f%%  ",
			    cip->ci_etime / (double) totalcip->ci_etime);
		else
			printf(" %4s   ", "");
	}

	if (!lflag) {
		if (jflag)
			printf("%11.2fcp ", t / (double) cip->ci_calls);
		else
			printf("%11.2fcp ", t / 60.0);
		if (cflag) {
			if (cip != totalcip)
				printf(" %4.2f%%  ",
				    (cip->ci_utime + cip->ci_stime) / (double)
				    (totalcip->ci_utime + totalcip->ci_stime));
			else
				printf(" %4s   ", "");
		}
	} else {
		if (jflag)
			printf("%11.2fu ", cip->ci_utime / (double) (AHZ * c));
		else
			printf("%11.2fu ", cip->ci_utime / (60.0 * AHZ));
		if (cflag) {
			if (cip != totalcip)
				printf(" %4.2f%%  ", cip->ci_utime / (double) totalcip->ci_utime);
			else
				printf(" %4s   ", "");
		}
		if (jflag)
			printf("%11.2fs ", cip->ci_stime / (double) (AHZ * c));
		else
			printf("%11.2fs ", cip->ci_stime / (60.0 * AHZ));
		if (cflag) {
			if (cip != totalcip)
				printf(" %4.2f%%  ", cip->ci_stime / (double) totalcip->ci_stime);
			else
				printf(" %4s   ", "");
		}
	}

	if (tflag) {
		if (!uflow)
			printf("%8.2fre/cp ", cip->ci_etime / (double) (cip->ci_utime + cip->ci_stime));
		else
			printf("%8s ", "*ignore*");
	}

	if (Dflag)
		printf("%10llutio ", cip->ci_io);
	else
		printf("%8.0favio ", cip->ci_io / c);

	if (Kflag)
		printf("%10lluk*sec ", cip->ci_mem);
	else
		printf("%8.0fk ", cip->ci_mem / t);

	printf("  %s\n", cip->ci_comm);
}
