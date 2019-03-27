/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2002   - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project
 * or FreeBSD, Inc.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * movejobs.c - The lpc commands which move jobs around.
 */

#include <sys/file.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>	/* just for MAXNAMLEN, for job_cfname in lp.h! */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lp.h"
#include "lpc.h"
#include "matchjobs.h"
#include "extern.h"

/* Values for origcmd in tqbq_common() */
#define IS_TOPQ		1
#define IS_BOTQ 	2

static int	 process_jobs(int _argc, char *_argv[], process_jqe
		    _process_rtn, void *myinfo);
static process_jqe touch_jqe;
static void 	 tqbq_common(int _argc, char *_argv[], int _origcmd);

/*
 * isdigit is defined to work on an 'int', in the range 0 to 255, plus EOF.
 * Define a wrapper which can take 'char', either signed or unsigned.
 */
#define isdigitch(Anychar)    isdigit(((int) Anychar) & 255)

struct touchjqe_info {			/* for topq/bottomq */
	time_t	 newtime;
};

static int	 nitems;
static struct jobqueue **queue;

/*
 * Process all the jobs, as specified by the user.
 */
static int
process_jobs(int argc, char *argv[], process_jqe process_rtn, void *myinfo)
{
	struct jobspec_hdr jobs_wanted;
	int i, matchcnt, pjres;

	STAILQ_INIT(&jobs_wanted);
	for (i = 0; i < argc; i++) {
		pjres = parse_jobspec(argv[i], &jobs_wanted);
		if (pjres == 0) {
			printf("\tinvalid job specifier: %s\n", argv[i]);
			continue;
		}
	}
	matchcnt = scanq_jobspec(nitems, queue, SCQ_JSORDER, &jobs_wanted,
	    process_rtn, myinfo);

	free_jobspec(&jobs_wanted);
	return (matchcnt);
}

/*
 * Reposition the job by changing the modification time of the
 * control file.
 */
static int
touch_jqe(void *myinfo, struct jobqueue *jq, struct jobspec *jspec)
{
	struct timeval tvp[2];
	struct touchjqe_info *touch_info;
	int ret;

	/*
	 * If the entire queue has been scanned for the current jobspec,
	 * then let the user know if there were no jobs matched by that
	 * specification.
	 */
	if (jq == NULL) {
		if (jspec->matchcnt == 0) {
			format_jobspec(jspec, FMTJS_VERBOSE);
			if (jspec->pluralfmt)
				printf("\tjobs %s are not in the queue\n",
				    jspec->fmtoutput);
			else
				printf("\tjob %s is not in the queue\n",
				    jspec->fmtoutput);
		}
		return (1);
	}

	/*
	 * Do a little juggling with "matched" vs "processed", so a single
	 * job can be matched by multiple specifications, and yet it will
	 * be moved only once.  This is so, eg, 'topq lp 7 7' will not
	 * complain "job 7 is not in queue" for the second specification.
	 */
	jq->job_matched = 0;
	if (jq->job_processed) {
		printf("\tmoved %s earlier\n", jq->job_cfname);
		return (1);
	}
	jq->job_processed = 1;

	touch_info = myinfo;
	tvp[0].tv_sec = tvp[1].tv_sec = ++touch_info->newtime;
	tvp[0].tv_usec = tvp[1].tv_usec = 0;
	PRIV_START
	ret = utimes(jq->job_cfname, tvp);
	PRIV_END

	if (ret == 0) {
		if (jspec->matcheduser)
			printf("\tmoved %s  (user %s)\n", jq->job_cfname,
			    jspec->matcheduser);
		else
			printf("\tmoved %s\n", jq->job_cfname);
	}
	return (ret);
}

/*
 * Put the specified jobs at the bottom of printer queue.
 */
void
bottomq_cmd(int argc, char *argv[])
{

	if (argc < 3) {
		printf("usage: bottomq printer [jobspec ...]\n");
		return;
	}
	--argc;			/* First argv was the command name */
	++argv;

	tqbq_common(argc, argv, IS_BOTQ);
}

/*
 * Put the specified jobs at the top of printer queue.
 */
void
topq_cmd(int argc, char *argv[])
{

	if (argc < 3) {
		printf("usage: topq printer [jobspec ...]\n");
		return;
	}
	--argc;			/* First argv was the command name */
	++argv;

	tqbq_common(argc, argv, IS_TOPQ);
} 

/*
 * Processing in common between topq and bottomq commands.
 */
void
tqbq_common(int argc, char *argv[], int origcmd)
{
	struct printer myprinter, *pp;
	struct touchjqe_info touch_info;
	int i, movecnt, setres;

	pp = setup_myprinter(*argv, &myprinter, SUMP_CHDIR_SD);
	if (pp == NULL)
		return;
	--argc;			/* Second argv was the printer name */
	++argv;

	nitems = getq(pp, &queue);
	if (nitems == 0) {
		printf("\tthere are no jobs in the queue\n");
		free_printer(pp);
		return;
	}

	/*
	 * The only real difference between topq and bottomq is the
	 * initial value used for newtime.
	 */
	switch (origcmd) {
	case IS_BOTQ:
		/*
		 * When moving jobs to the bottom of the queue, pick a
		 * starting value which is one second after the last job
		 * in the queue.
		*/
		touch_info.newtime = queue[nitems - 1]->job_time + 1;
		break;
	case IS_TOPQ:
		/*
		 * When moving jobs to the top of the queue, the greatest
		 * number of jobs which could be moved is all the jobs
		 * that are in the queue.  Pick a starting value which
		 * leaves plenty of room for all existing jobs.
		 */
		touch_info.newtime = queue[0]->job_time - nitems - 5;
		break;
	default:
		printf("\ninternal error in topq/bottomq processing.\n");
		return;
	}

	movecnt = process_jobs(argc, argv, touch_jqe, &touch_info);

	/*
	 * If any jobs were moved, then chmod the lock file to notify any
	 * active process for this queue that the queue has changed, so
	 * it will rescan the queue to find out the new job order. 
	 */
	if (movecnt == 0)
		printf("\tqueue order unchanged\n");
	else {
		setres = set_qstate(SQS_QCHANGED, pp->lock_file);
		if (setres < 0)
			printf("\t* queue order changed for %s, but the\n"
			    "\t* attempt to set_qstate() failed [%d]!\n",
			    pp->printer, setres);
	}

	for (i = 0; i < nitems; i++)
		free(queue[i]);
	free(queue);
	free_printer(pp);
} 

