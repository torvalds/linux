/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)pigs.c	8.2 (Berkeley) 9/23/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Pigs display from Bill Reeves at Lucasfilm
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/user.h>

#include <curses.h>
#include <math.h>
#include <pwd.h>
#include <stdlib.h>

#include "systat.h"
#include "extern.h"

int compar(const void *, const void *);

static int nproc;
static struct p_times {
	float pt_pctcpu;
	struct kinfo_proc *pt_kp;
} *pt;

static int    fscale;
static double  lccpu;

WINDOW *
openpigs(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closepigs(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

void
showpigs(void)
{
	int i, j, y, k;
	const char *uname, *pname;
	char pidname[30];

	if (pt == NULL)
		return;

	qsort(pt, nproc, sizeof (struct p_times), compar);
	y = 1;
	i = nproc;
	if (i > getmaxy(wnd)-2)
		i = getmaxy(wnd)-2;
	for (k = 0; i > 0 && pt[k].pt_pctcpu > 0.01; i--, y++, k++) {
		uname = user_from_uid(pt[k].pt_kp->ki_uid, 0);
		pname = pt[k].pt_kp->ki_comm;
		wmove(wnd, y, 0);
		wclrtoeol(wnd);
		mvwaddstr(wnd, y, 0, uname);
		snprintf(pidname, sizeof(pidname), "%10.10s", pname);
		mvwaddstr(wnd, y, 9, pidname);
		wmove(wnd, y, 20);
		for (j = pt[k].pt_pctcpu * 50 + 0.5; j > 0; j--)
			waddch(wnd, 'X');
	}
	wmove(wnd, y, 0); wclrtobot(wnd);
}

int
initpigs(void)
{
	fixpt_t ccpu;
	size_t len;
	int err;

	len = sizeof(ccpu);
	err = sysctlbyname("kern.ccpu", &ccpu, &len, NULL, 0);
	if (err || len != sizeof(ccpu)) {
		perror("kern.ccpu");
		return (0);
	}

	len = sizeof(fscale);
	err = sysctlbyname("kern.fscale", &fscale, &len, NULL, 0);
	if (err || len != sizeof(fscale)) {
		perror("kern.fscale");
		return (0);
	}

	lccpu = log((double) ccpu / fscale);

	return(1);
}

void
fetchpigs(void)
{
	int i;
	float ftime;
	float *pctp;
	struct kinfo_proc *kpp;
	static int lastnproc = 0;

	if ((kpp = kvm_getprocs(kd, KERN_PROC_ALL, 0, &nproc)) == NULL) {
		error("%s", kvm_geterr(kd));
		if (pt)
			free(pt);
		return;
	}
	if (nproc > lastnproc) {
		free(pt);
		if ((pt =
		    malloc(nproc * sizeof(struct p_times))) == NULL) {
			error("Out of memory");
			die(0);
		}
	}
	lastnproc = nproc;
	/*
	 * calculate %cpu for each proc
	 */
	for (i = 0; i < nproc; i++) {
		pt[i].pt_kp = &kpp[i];
		pctp = &pt[i].pt_pctcpu;
		ftime = kpp[i].ki_swtime;
		if (ftime == 0 || (kpp[i].ki_flag & P_INMEM) == 0)
			*pctp = 0;
		else
			*pctp = ((double) kpp[i].ki_pctcpu /
					fscale) / (1.0 - exp(ftime * lccpu));
	}
}

void
labelpigs(void)
{
	wmove(wnd, 0, 0);
	wclrtoeol(wnd);
	mvwaddstr(wnd, 0, 20,
	    "/0%  /10  /20  /30  /40  /50  /60  /70  /80  /90  /100");
}

int
compar(const void *a, const void *b)
{
	return (((const struct p_times *) a)->pt_pctcpu >
		((const struct p_times *) b)->pt_pctcpu)? -1: 1;
}
