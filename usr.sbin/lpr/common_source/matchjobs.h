/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2002  - Garance Alistair Drosehn <gad@FreeBSD.org>.
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
 * $FreeBSD$
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

#include <sys/queue.h>

/*
 * The "matcheduser" field is *only* valid during the call to the
 * given "doentry()" routine, and is only set if the specification
 * included a userid.
 */
struct jobspec {
	STAILQ_ENTRY(jobspec) nextjs;
	char	*wantedhost;
	char	*wanteduser;
	char	*matcheduser;		/* only valid for "doentry()" */
	char	*fmtoutput;		/* set by format_jobspec() */
	long	 startnum;
	long	 endrange;
	int	 pluralfmt;		/* boolean set by format_jobspec() */
	uint	 matchcnt;
};
STAILQ_HEAD(jobspec_hdr, jobspec);

/*
 * Format options for format_jobspec.
 */
#define FMTJS_TERSE	1		/* user:jobrange@host */
#define FMTJS_VERBOSE	2		/* jobrange from user@host */

/*
 * Options for scanq_jobspec.
 *
 * The caller must choose the order that entries should be scanned:
 * 1) JSORDER: Matched jobs are processed (by calling the "doentry()"
 *    routine) in the order that the user specified those jobs.
 * 2) QORDER: Matched jobs are processed in the order that the jobs are
 *    listed the queue.  This guarantees that the "doentry()" routine
 *    will be called only once per job.
 *
 * There is a "job_matched" variable in struct jobqueue, which is used
 * to make sure that the "doentry()" will only be called once for any
 * given job in JSORDER processing.  The "doentry()" routine can turn
 * that off, if it does want to be called multiple times when the job
 * is matched by multiple specifiers.
 *
 * The JSORDER processing will also call the "doentry()" routine once
 * after each scan of the queue, with the jobqueue set to null.  This
 * provides a way for the caller to print out a summary message for
 * each jobspec that was given.
 */
#define SCQ_JSORDER	0x0001		/* follow the user-specified order */
#define SCQ_QORDER	0x0002		/* the order of jobs in the queue */

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */

__BEGIN_DECLS
struct	 jobqueue;

typedef	int	 process_jqe(void *_myinfo, struct jobqueue *_jq,
		    struct jobspec *_jspec);

void	 format_jobspec(struct jobspec *_jspec, int _fmt_wanted);
void	 free_jobspec(struct jobspec_hdr *_js_hdr);
int	 scanq_jobspec(int _qitems, struct jobqueue **_squeue, int _sopts,
	    struct jobspec_hdr *_js_hdr, process_jqe _doentry,
	    void *_doentryinfo);
int	 parse_jobspec(char *_jobstr, struct jobspec_hdr *_js_hdr);
__END_DECLS

