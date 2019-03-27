/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2002,2011   - Garance Alistair Drosehn <gad@FreeBSD.org>.
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

#include <dirent.h>	/* for MAXNAMLEN, for job_cfname in lp.h! */
#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ctlinfo.h"
#include "lp.h"
#include "matchjobs.h"

#define DEBUG_PARSEJS	0	/* set to 1 when testing */
#define DEBUG_SCANJS	0	/* set to 1 when testing */

static int	 match_jobspec(struct jobqueue *_jq, struct jobspec *_jspec);

/*
 * isdigit is defined to work on an 'int', in the range 0 to 255, plus EOF.
 * Define a wrapper which can take 'char', either signed or unsigned.
 */
#define isdigitch(Anychar)    isdigit(((int) Anychar) & 255)

/*
 * Format a single jobspec into a string fit for printing.
 */
void
format_jobspec(struct jobspec *jspec, int fmt_wanted)
{
	char rangestr[40], buildstr[200];
	const char fromuser[] = "from user ";
	const char fromhost[] = "from host ";
	size_t strsize;

	/*
	 * If the struct already has a fmtstring, then release it
	 * before building a new one.
	 */
	if (jspec->fmtoutput != NULL) {
		free(jspec->fmtoutput);
		jspec->fmtoutput = NULL;
	}

	jspec->pluralfmt = 1;		/* assume a "plural result" */
	rangestr[0] = '\0';
	if (jspec->startnum >= 0) {
		if (jspec->startnum != jspec->endrange)
			snprintf(rangestr, sizeof(rangestr), "%ld-%ld",
			    jspec->startnum, jspec->endrange);
		else {
			jspec->pluralfmt = 0;
			snprintf(rangestr, sizeof(rangestr), "%ld",
			    jspec->startnum);
		}
	}

	strsize = sizeof(buildstr);
	buildstr[0] = '\0';
	switch (fmt_wanted) {
	case FMTJS_TERSE:
		/* Build everything but the hostname in a temp string. */
		if (jspec->wanteduser != NULL)
			strlcat(buildstr, jspec->wanteduser, strsize);
		if (rangestr[0] != '\0') {
			if (buildstr[0] != '\0')
				strlcat(buildstr, ":", strsize);
			strlcat(buildstr, rangestr, strsize);
		}
		if (jspec->wantedhost != NULL)
				strlcat(buildstr, "@", strsize);

		/* Get space for the final result, including hostname */
		strsize = strlen(buildstr) + 1;
		if (jspec->wantedhost != NULL)
			strsize += strlen(jspec->wantedhost);
		jspec->fmtoutput = malloc(strsize);

		/* Put together the final result */
		strlcpy(jspec->fmtoutput, buildstr, strsize);
		if (jspec->wantedhost != NULL)
			strlcat(jspec->fmtoutput, jspec->wantedhost, strsize);
		break;

	case FMTJS_VERBOSE:
	default:
		/* Build everything but the hostname in a temp string. */
		strlcat(buildstr, rangestr, strsize);
		if (jspec->wanteduser != NULL) {
			if (rangestr[0] != '\0')
				strlcat(buildstr, " ", strsize);
			strlcat(buildstr, fromuser, strsize);
			strlcat(buildstr, jspec->wanteduser, strsize);
		}
		if (jspec->wantedhost != NULL) {
			if (jspec->wanteduser == NULL) {
				if (rangestr[0] != '\0')
					strlcat(buildstr, " ", strsize);
				strlcat(buildstr, fromhost, strsize);
			} else
				strlcat(buildstr, "@", strsize);
		}

		/* Get space for the final result, including hostname */
		strsize = strlen(buildstr) + 1;
		if (jspec->wantedhost != NULL)
			strsize += strlen(jspec->wantedhost);
		jspec->fmtoutput = malloc(strsize);

		/* Put together the final result */
		strlcpy(jspec->fmtoutput, buildstr, strsize);
		if (jspec->wantedhost != NULL)
			strlcat(jspec->fmtoutput, jspec->wantedhost, strsize);
		break;
	}
}

/*
 * Free all the jobspec-related information.
 */
void
free_jobspec(struct jobspec_hdr *js_hdr)
{
	struct jobspec *jsinf;

	while (!STAILQ_EMPTY(js_hdr)) {
		jsinf = STAILQ_FIRST(js_hdr);
		STAILQ_REMOVE_HEAD(js_hdr, nextjs);
		if (jsinf->fmtoutput)
			free(jsinf->fmtoutput);
		if (jsinf->matcheduser)
			free(jsinf->matcheduser);
		free(jsinf);
	}
}

/*
 * This routine takes a string as typed in from the user, and parses it
 * into a job-specification.  A job specification would match one or more
 * jobs in the queue of some single printer (the specification itself does
 * not indicate which queue should be searched).
 *
 * This recognizes a job-number range by itself (all digits, or a range
 * indicated by "digits-digits"), or a userid by itself.  If a `:' is
 * found, it is treated as a separator between a job-number range and
 * a userid, where the job number range is the side which has a digit as
 * the first character.  If an `@' is found, everything to the right of
 * it is treated as the hostname the job originated from.
 *
 * So, the user can specify:
 *	jobrange       userid     userid:jobrange    jobrange:userid
 *	jobrange@hostname   jobrange:userid@hostname
 *	userid@hostname     userid:jobrange@hostname
 *
 * XXX - it would be nice to add "not options" too, such as ^user,
 *	^jobrange, and @^hostname.
 *
 * This routine may modify the original input string if that input is
 * valid.  If the input was *not* valid, then this routine should return
 * with the input string the same as when the routine was called.
 */
int
parse_jobspec(char *jobstr, struct jobspec_hdr *js_hdr)
{
	struct jobspec *jsinfo;
	char *atsign, *colon, *lhside, *numstr, *period, *rhside;
	int jobnum;

#if DEBUG_PARSEJS
	printf("\t [ pjs-input = %s ]\n", jobstr);
#endif

	if ((jobstr == NULL) || (*jobstr == '\0'))
		return (0);

	jsinfo = malloc(sizeof(struct jobspec));
	memset(jsinfo, 0, sizeof(struct jobspec));
	jsinfo->startnum = jsinfo->endrange = -1;

	/* Find the separator characters, and nullify them. */
	numstr = NULL;
	atsign = strchr(jobstr, '@');
	colon = strchr(jobstr, ':');
	if (atsign != NULL)
		*atsign = '\0';
	if (colon != NULL)
		*colon = '\0';

	/* The at-sign always indicates a hostname. */
	if (atsign != NULL) {
		rhside = atsign + 1;
		if (*rhside != '\0')
			jsinfo->wantedhost = rhside;
	}

	/* Finish splitting the input into three parts. */
	rhside = NULL;
	if (colon != NULL) {
		rhside = colon + 1;
		if (*rhside == '\0')
			rhside = NULL;
	}
	lhside = NULL;
	if (*jobstr != '\0')
		lhside = jobstr;

	/*
	 * If there is a `:' here, then it's either jobrange:userid,
	 * userid:jobrange, or (if @hostname was not given) perhaps it
	 * might be hostname:jobnum.  The side which has a digit as the
	 * first character is assumed to be the jobrange.  It is an
	 * input error if both sides start with a digit, or if neither
	 * side starts with a digit.
	 */
	if ((lhside != NULL) && (rhside != NULL)) {
		if (isdigitch(*lhside)) {
			if (isdigitch(*rhside))
				goto bad_input;
			numstr = lhside;
			jsinfo->wanteduser = rhside;
		} else if (isdigitch(*rhside)) {
			numstr = rhside;
			/*
			 * The original implementation of 'lpc topq' accepted
			 * hostname:jobnum.  If the input did not include a
			 * @hostname, then assume the userid is a hostname if
			 * it includes a '.'.
			 */
			period = strchr(lhside, '.');
			if ((atsign == NULL) && (period != NULL))
				jsinfo->wantedhost = lhside;
			else
				jsinfo->wanteduser = lhside;
		} else {
			/* Neither side is a job number = user error */
			goto bad_input;
		}
	} else if (lhside != NULL) {
		if (isdigitch(*lhside))
			numstr = lhside;
		else
			jsinfo->wanteduser = lhside;
	} else if (rhside != NULL) {
		if (isdigitch(*rhside))
			numstr = rhside;
		else
			jsinfo->wanteduser = rhside;
	}

	/*
	 * Break down the numstr.  It should be all digits, or a range
	 * specified as "\d+-\d+".
	 */
	if (numstr != NULL) {
		errno = 0;
		jobnum = strtol(numstr, &numstr, 10);
		if (errno != 0)		/* error in conversion */
			goto bad_input;
		if (jobnum < 0)		/* a bogus value for this purpose */
			goto bad_input;
		if (jobnum > 99999)	/* too large for job number */
			goto bad_input;
		jsinfo->startnum = jsinfo->endrange = jobnum;

		/* Check for a range of numbers */
		if ((*numstr == '-') && (isdigitch(*(numstr + 1)))) {
			numstr++;
			errno = 0;
			jobnum = strtol(numstr, &numstr, 10);
			if (errno != 0)		/* error in conversion */
				goto bad_input;
			if (jobnum < jsinfo->startnum)
				goto bad_input;
			if (jobnum > 99999)	/* too large for job number */
				goto bad_input;
			jsinfo->endrange = jobnum;
		}

		/*
		 * If there is anything left in the numstr, and if the
		 * original string did not include a userid or a hostname,
		 * then this might be the ancient form of '\d+hostname'
		 * (with no separator between jobnum and hostname).  Accept
		 * that for backwards compatibility, but otherwise any
		 * remaining characters mean a user-error.  Note that the
		 * ancient form accepted only a single number, but this
		 * will also accept a range of numbers.
		 */
		if (*numstr != '\0') {
			if (atsign != NULL)
				goto bad_input;
			if (jsinfo->wantedhost != NULL)
				goto bad_input;
			if (jsinfo->wanteduser != NULL)
				goto bad_input;
			/* Treat as the rest of the string as a hostname */
			jsinfo->wantedhost = numstr;
		}
	}

	if ((jsinfo->startnum < 0) && (jsinfo->wanteduser == NULL) &&
	    (jsinfo->wantedhost == NULL))
		goto bad_input;

	/*
	 * The input was valid, in the sense that it could be parsed
	 * into the individual parts.  Add this jobspec to the list
	 * of jobspecs.
	 */
	STAILQ_INSERT_TAIL(js_hdr, jsinfo, nextjs);

#if DEBUG_PARSEJS
	printf("\t [   will check for");
	if (jsinfo->startnum >= 0) {
		if (jsinfo->startnum == jsinfo->endrange)
			printf(" jobnum = %ld", jsinfo->startnum);
		else
			printf(" jobrange = %ld to %ld", jsinfo->startnum,
			    jsinfo->endrange);
	} else {
		printf(" jobs");
	}
	if ((jsinfo->wanteduser != NULL) || (jsinfo->wantedhost != NULL)) {
		printf(" from");
		if (jsinfo->wanteduser != NULL)
			printf(" user = %s", jsinfo->wanteduser);
		if (jsinfo->wantedhost != NULL)
			printf(" host = %s", jsinfo->wantedhost);
	}
	printf("]\n");
#endif

	return (1);

bad_input:
	/*
	 * Restore any `@' and `:', in case the calling routine wants to
	 * write an error message which includes the input string.
	 */
	if (atsign != NULL)
		*atsign = '@';
	if (colon != NULL)
		*colon = ':';
	if (jsinfo != NULL)
		free(jsinfo);
	return (0);
}

/*
 * Check to see if a given job (specified by a jobqueue entry) matches
 * all of the specifications in a given jobspec.
 *
 * Returns 0 if no match, 1 if the job does match.
 */
static int
match_jobspec(struct jobqueue *jq, struct jobspec *jspec)
{
	struct cjobinfo *cfinf;
	const char *cf_hoststr;
	int jnum, match;

#if DEBUG_SCANJS
	printf("\t [ match-js checking %s ]\n", jq->job_cfname);
#endif

	if (jspec == NULL || jq == NULL)
		return (0);

	/*
	 * Keep track of which jobs have already been matched by this
	 * routine, and thus (probably) already processed.
	 */
	if (jq->job_matched)
		return (0);

	jnum = calc_jobnum(jq->job_cfname, &cf_hoststr);
	cfinf = NULL;
	match = 0;			/* assume the job will not match */
	jspec->matcheduser = NULL;

	/*
	 * Check the job-number range.
	 */ 
	if (jspec->startnum >= 0) {
		if (jnum < jspec->startnum)
			goto nomatch;
		if (jnum > jspec->endrange)
			goto nomatch;
	}

	/*
	 * Check the hostname.  Strictly speaking this should be done by
	 * reading the control file, but it is less expensive to check
	 * the hostname-part of the control file name.  Also, this value
	 * can be easily seen in 'lpq -l', while there is no easy way for
	 * a user/operator to see the hostname in the control file.
	 */
	if (jspec->wantedhost != NULL) {
		if (fnmatch(jspec->wantedhost, cf_hoststr, 0) != 0)
			goto nomatch;
	}

	/*
	 * Check for a match on the user name.  This has to be done
	 * by reading the control file.
	 */
	if (jspec->wanteduser != NULL) {
		cfinf = ctl_readcf("fakeq", jq->job_cfname);
		if (cfinf == NULL)
			goto nomatch;
		if (fnmatch(jspec->wanteduser, cfinf->cji_acctuser, 0) != 0)
			goto nomatch;
	}

	/* This job matches all of the specified criteria. */
	match = 1;
	jq->job_matched = 1;		/* avoid matching the job twice */
	jspec->matchcnt++;
	if (jspec->wanteduser != NULL) {
		/*
		 * If the user specified a userid (which may have been a
		 * pattern), then the caller's "doentry()" routine might
		 * want to know the userid of this job that matched.
		 */
		jspec->matcheduser = strdup(cfinf->cji_acctuser);
	}
#if DEBUG_SCANJS
	printf("\t [ job matched! ]\n");
#endif

nomatch:
	if (cfinf != NULL)
		ctl_freeinf(cfinf);
	return (match);
}

/*
 * Scan a queue for all jobs which match a jobspec.  The queue is scanned
 * from top to bottom.
 *
 * The caller can provide a routine which will be executed for each job
 * that does match.  Note that the processing routine might do anything
 * to the matched job -- including the removal of it.
 *
 * This returns the number of jobs which were matched.
 */
int
scanq_jobspec(int qcount, struct jobqueue **squeue, int sopts, struct
    jobspec_hdr *js_hdr, process_jqe doentry, void *doentryinfo)
{
	struct jobqueue **qent;
	struct jobspec *jspec;
	int cnt, matched, total;

	if (qcount < 1)
		return (0);
	if (js_hdr == NULL)
		return (-1);

	/* The caller must specify one of the scanning orders */
	if ((sopts & (SCQ_JSORDER|SCQ_QORDER)) == 0)
		return (-1);

	total = 0;
	if (sopts & SCQ_JSORDER) {
		/*
		 * For each job specification, scan through the queue
		 * looking for every job that matches.
		 */
		STAILQ_FOREACH(jspec, js_hdr, nextjs) {
			for (qent = squeue, cnt = 0; cnt < qcount;
			    qent++, cnt++) {
				matched = match_jobspec(*qent, jspec);
				if (!matched)
					continue;
				total++;
				if (doentry != NULL)
					doentry(doentryinfo, *qent, jspec);
				if (jspec->matcheduser != NULL) {
					free(jspec->matcheduser);
					jspec->matcheduser = NULL;
				}
			}
			/*
			 * The entire queue has been scanned for this
			 * jobspec.  Call the user's routine again with
			 * a NULL queue-entry, so it can print out any
			 * kind of per-jobspec summary.
			 */
			if (doentry != NULL)
				doentry(doentryinfo, NULL, jspec);
		}
	} else {
		/*
		 * For each job in the queue, check all of the job
		 * specifications to see if any one of them matches
		 * that job.
		 */
		for (qent = squeue, cnt = 0; cnt < qcount;
		    qent++, cnt++) {
			STAILQ_FOREACH(jspec, js_hdr, nextjs) {
				matched = match_jobspec(*qent, jspec);
				if (!matched)
					continue;
				total++;
				if (doentry != NULL)
					doentry(doentryinfo, *qent, jspec);
				if (jspec->matcheduser != NULL) {
					free(jspec->matcheduser);
					jspec->matcheduser = NULL;
				}
				/*
				 * Once there is a match, then there is no
				 * point in checking this same job against
				 * all the other jobspec's.
				 */
				break;
			}
		}
	}

	return (total);
}
