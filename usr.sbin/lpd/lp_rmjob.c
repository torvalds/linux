/*	$OpenBSD: lp_rmjob.c,v 1.1.1.1 2018/04/27 16:14:36 eric Exp $	*/

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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lp.h"

#include "log.h"

static int docheck(struct lp_printer *, const char *, struct lp_jobfilter *,
    const char *, int, int);
static void doremove(int, struct lp_printer *, const char *);

int
lp_rmjob(int ofd, struct lp_printer *lp, const char *agent,
    struct lp_jobfilter *jf)
{
	struct lp_queue q;
	char currjob[PATH_MAX];
	pid_t currpid;
	int active, i, killed = 0;

	if ((lp_readqueue(lp, &q)) == -1) {
		log_warnx("cannot read queue");
		return 0;
	}

	if (q.count == 0) {
		lp_clearqueue(&q);
		return 0;
	}

	/*
	 * Find the current task being printed, and kill the printer process
	 * if the file is to be removed.
	 */
	if (lp_getcurrtask(lp, &currpid, currjob, sizeof(currjob)) == -1)
		log_warnx("cannot get current task");

	if (currjob[0] && docheck(lp, agent, jf, currjob, 1, 0) == 1) {
		if (kill(currpid, SIGINT) == -1)
			log_warn("lpr: cannot kill printer process %d",
			    (int)currpid);
		else
			killed = 1;
	}

	for(i = 0; i < q.count; i++) {
		active = !strcmp(q.cfname[i], currjob);
		switch (docheck(lp, agent, jf, q.cfname[i], active, 0)) {
		case 0:
			break;
		case 1:
			doremove(ofd, lp, q.cfname[i]);
			break;
		case 2:
			if (lp->lp_type == PRN_LPR)
				dprintf(ofd, "%s: ", lpd_hostname);
			dprintf(ofd, "%s: Permission denied\n", q.cfname[i]);
			break;
		}
	}

	lp_clearqueue(&q);

	return killed;
}

/*
 * Check if a file must be removed.
 *
 * Return:
 *	0: no
 *	1: yes
 *	2: yes but user has no right to do so
 */
static int
docheck(struct lp_printer *lp, const char *agent, struct lp_jobfilter *jf,
    const char *cfname, int current, int local)
{
	FILE *fp;
	ssize_t len;
	size_t linesz = 0;
	char *line = NULL, *person = NULL;
	int i, own = 0;

	/* The "-all" agent means remove all jobs from the client host. */
	if (!strcmp(agent, "-all") && !strcmp(LP_JOBHOST(cfname), jf->hostfrom))
		return 1;

	/*
	 * Consider the root user owns local files, and files sent from
	 * the same machine.
	 */
	if (!strcmp(agent, "root"))
		own = local || !strcmp(LP_JOBHOST(cfname), jf->hostfrom);

	/* Check if the task person matches the agent. */
	fp = lp_fopen(lp, cfname);
	if (fp == NULL) {
		log_warn("cannot open %s", cfname);
		return 0;
	}
	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len - 1] = '\0';
		if (line[0] == 'P') {
			person = line + 1;
			if (!strcmp(person, agent) &&
			    !strcmp(LP_JOBHOST(cfname), jf->hostfrom))
				own = 1;
			break;
		}
	}
	fclose(fp);

	if (person == NULL) {
		free(line);
		return 0;
	}

	/* Remove the current task if the request list is empty. */
	if (current && jf->nuser == 0 && jf->njob == 0)
		goto remove;

	/* Check for matching jobnum. */
	for (i = 0; i < jf->njob; i++)
		if (jf->jobs[i] == LP_JOBNUM(cfname))
			goto remove;

	/* Check if person is in user list. */
	for (i = 0; i < jf->nuser; i++)
		if (!strcmp(jf->users[i], person))
			goto remove;

	free(line);
	return 0;

    remove:
	free(line);
	return own ? 1 : 2;
}

static void
doremove(int ofd, struct lp_printer *lp, const char *cfname)
{
	FILE *fp;
	ssize_t len;
	size_t linesz = 0;
	char *line = NULL;

	fp = lp_fopen(lp, cfname);
	if (fp == NULL) {
		log_warn("cannot open %s", cfname);
		return;
	}

	if (lp->lp_type == PRN_LPR)
		dprintf(ofd, "%s: ", lpd_hostname);

	/* First, remove the control file. */
	if (lp_unlink(lp, cfname) == -1) {
		log_warn("cannot unlink %s", cfname);
		dprintf(ofd, "cannot dequeue %s\n", cfname);
	}
	else {
		log_info("removed job %s", cfname);
		dprintf(ofd, "%s dequeued\n", cfname);
	}

	/* Then unlink all data files. */
	while ((len = getline(&line, &linesz, fp)) != -1) {
		if (line[len-1] == '\n')
			line[len - 1] = '\0';
		if (line[0] != 'U')
			continue;
		if (strchr(line+1, '/') || strncmp(line+1, "df", 2))
			continue;
		if (lp->lp_type == PRN_LPR)
			dprintf(ofd, "%s: ", lpd_hostname);
		if (lp_unlink(lp, line + 1) == -1) {
			log_warn("cannot unlink %s", line + 1);
			dprintf(ofd, "cannot dequeue %s\n", line + 1);
		}
		else
			dprintf(ofd, "%s dequeued\n", line + 1);
	}

	fclose(fp);
}
