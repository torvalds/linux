/*	$OpenBSD: lp_displayq.c,v 1.1.1.1 2018/04/27 16:14:36 eric Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lp.h"

#include "log.h"

static void dolong(int, struct lp_printer *, struct lp_jobfilter *, int,
    const char *, int);
static void doshort(int, struct lp_printer *, struct lp_jobfilter *, int,
    const char *, int);

void
lp_displayq(int ofd, struct lp_printer *lp, int lng, struct lp_jobfilter *jf)
{
	struct lp_queue q;
	pid_t currpid;
	char status[256], currjob[PATH_MAX];
	int i, active, qstate;

	/* Warn about the queue state if needed. */
	if (lp_getqueuestate(lp, 0, &qstate) == -1)
		log_warnx("cannot get queue state");
	else {
		if (qstate & LPQ_PRINTER_DOWN) {
			if (lp->lp_type == PRN_LPR)
				dprintf(ofd, "%s: ", lpd_hostname);
			dprintf(ofd, "Warning: %s is down\n", lp->lp_name);
		}
		if (qstate & LPQ_QUEUE_OFF) {
			if (lp->lp_type == PRN_LPR)
				dprintf(ofd, "%s: ", lpd_hostname);
			dprintf(ofd, "Warning: %s queue is turned off\n",
			    lp->lp_name);
		}
	}

	/* Read queue content. */
	if ((lp_readqueue(lp, &q)) == -1) {
		log_warnx("cannot read queue");
		if (lp->lp_type == PRN_LPR)
			dprintf(ofd, "%s: ", lpd_hostname);
		dprintf(ofd, "Warning: cannot read queue\n");
	}

	/* Display current printer status. */
	if (lp_getcurrtask(lp, &currpid, currjob, sizeof(currjob)) == -1)
		log_warnx("cannot get current task");

	if (currpid) {
		if (lp->lp_type == PRN_LPR)
			dprintf(ofd, "%s: ", lpd_hostname);
		if (lp_getstatus(lp, status, sizeof(status)) == -1) {
			log_warnx("cannot read printer status");
			dprintf(ofd, "Warning: cannot read status file\n");
		}
		else
			dprintf(ofd, "%s\n", status);
	}

	/* Display queue content. */
	if (q.count == 0) {
		if (lp->lp_type != PRN_LPR)
			dprintf(ofd, "no entries\n");
	}
	else {
		if (currpid == 0) {
			if (lp->lp_type == PRN_LPR)
				dprintf(ofd, "%s: ", lpd_hostname);
			dprintf(ofd, "Warning: no daemon present\n");
		}
		if (!lng) {
			dprintf(ofd, "Rank   Owner      Job  Files");
			dprintf(ofd, "%43s\n", "Total Size");
		}
		for (i = 0; i < q.count; i++) {
			active = !strcmp(q.cfname[i], currjob);
			if (lng)
				dolong(ofd, lp, jf, i+1, q.cfname[i], active);
			else
				doshort(ofd, lp, jf, i+1, q.cfname[i], active);
		}
	}

	lp_clearqueue(&q);
}

static int
checklists(const char *cfname, struct lp_jobfilter *jf, const char *person)
{
	int i;

	if (jf->nuser == 0 && jf->njob == 0)
		return 1;

	/* Check if user is in user list. */
	for (i = 0; i < jf->nuser; i++)
		if (!strcmp(jf->users[i], person))
			return 1;

	/* Skip if hostnames don't match. */
	if (strcmp(LP_JOBHOST(cfname), jf->hostfrom))
		return 0;

	/* Check for matching jobnum. */
	for (i = 0; i < jf->njob; i++)
		if (jf->jobs[i] == LP_JOBNUM(cfname))
			return 1;

	return 0;
}

static const char *
rankstr(int rank, int active)
{
	static char buf[16];
	const char *sfx;

	if (active)
		return "active";

	sfx = "th";
	switch (rank % 10) {
	case 1:
		if ((rank / 10) % 10 != 1)
			sfx = "st";
		break;
	case 2:
		sfx = "nd";
		break;
	case 3:
		sfx = "rd";

	}

	snprintf(buf, sizeof(buf), "%d%s", rank, sfx);
	return buf;
}

static void
doshort(int ofd, struct lp_printer *lp,  struct lp_jobfilter *jf, int rank,
    const char *cfname, int active)
{
	struct stat st;
	FILE *fp;
	const char *fname;
	char dfname[PATH_MAX], names[80], *line = NULL;
	ssize_t len;
	size_t linesz = 0, totalsz = 0;
	int copies = 0;

	fp = lp_fopen(lp, cfname);
	if (fp == NULL) {
		log_warn("cannot open %s", cfname);
		return;
	}

	names[0] = '\0';

	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len - 1] = '\0';
		switch (line[0]) {
		case 'P':
			if (!checklists(cfname, jf, line + 1))
				goto end;

			dprintf(ofd, "%-7s%-11s%-4i ", rankstr(rank, active),
			    line + 1, LP_JOBNUM(cfname));
			break;

		case 'N':
			fname = line + 1;
			if (!strcmp(fname, " "))
				fname = "(standard input)";

			if (names[0])
				(void)strlcat(names, ", ", sizeof(names));
			(void)strlcat(names, fname, sizeof(names));
			if (lp_stat(lp, dfname, &st) == -1)
				log_warn("cannot stat %s", dfname);
			else
				totalsz += copies * st.st_size;
			copies = 0;
			break;

		default:
			if (line[0] < 'a' || line[0] > 'z')
				continue;
			if (copies++ == 0)
				(void)strlcpy(dfname, line+1, sizeof(dfname));
			break;
		}
	}

	dprintf(ofd, "%-37s %lld bytes\n", names, (long long)totalsz);

    end:
	free(line);
}

static void
dolong(int ofd, struct lp_printer *lp,  struct lp_jobfilter *jf, int rank,
    const char *cfname, int active)
{
	struct stat st;
	FILE *fp;
	const char *fname;
	char dfname[PATH_MAX], names[80], buf[64], *line = NULL;
	ssize_t len;
	size_t linesz = 0;
	int copies = 0;

	fp = lp_fopen(lp, cfname);
	if (fp == NULL) {
		log_warn("cannot open %s", cfname);
		return;
	}

	names[0] = '\0';

	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len - 1] = '\0';
		switch (line[0]) {
		case 'P':
			if (!checklists(cfname, jf, line + 1))
				goto end;

			snprintf(buf, sizeof(buf), "%s: %s", line+1,
			    rankstr(rank, active));
			dprintf(ofd, "\n%-41s[job %s]\n", buf, cfname + 3);
			break;

		case 'N':
			fname = line + 1;
			if (!strcmp(fname, " "))
				fname = "(standard input)";

			if (copies > 1)
				dprintf(ofd, "\t%-2d copies of %-19s", copies,
				    fname);
			else
				dprintf(ofd, "\t%-32s", fname);

			if (lp_stat(lp, dfname, &st) == -1) {
				log_warn("cannot stat %s", dfname);
				dprintf(ofd, " ??? bytes\n");
			}
			else
				dprintf(ofd, " %lld bytes\n",
				    (long long)st.st_size);
			copies = 0;
			break;

		default:
			if (line[0] < 'a' || line[0] > 'z')
				continue;
			if (copies++ == 0)
				(void)strlcpy(dfname, line+1, sizeof(dfname));
			break;
		}
	}

    end:
	free(line);
}
