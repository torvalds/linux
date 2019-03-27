/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993, John Brezak
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

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#ifdef DEBUG
#include <errno.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <syslog.h>
#include <utmpx.h>
#ifdef XIDLE
#include <setjmp.h>
#include <X11/Xlib.h>
#include <X11/extensions/xidle.h>
#endif
#include <rpcsvc/rnusers.h>

#include "extern.h"

#ifndef _PATH_DEV
#define _PATH_DEV "/dev"
#endif

static utmpidle utmp_idle[MAXUSERS];
static utmp old_utmp[MAXUSERS];
static struct utmpx utmp_list[MAXUSERS];

#ifdef XIDLE
static Display *dpy;

static jmp_buf openAbort;

static void
abortOpen(void)
{
    longjmp (openAbort, 1);
}

XqueryIdle(char *display)
{
	int first_event, first_error;
	Time IdleTime;

	(void) signal (SIGALRM, abortOpen);
	(void) alarm ((unsigned) 10);
	if (!setjmp (openAbort)) {
		if (!(dpy= XOpenDisplay(display))) {
			syslog(LOG_ERR, "Cannot open display %s", display);
			return(-1);
		}
		if (XidleQueryExtension(dpy, &first_event, &first_error)) {
			if (!XGetIdleTime(dpy, &IdleTime)) {
				syslog(LOG_ERR, "%s: unable to get idle time", display);
				return(-1);
			}
		} else {
			syslog(LOG_ERR, "%s: Xidle extension not loaded", display);
			return(-1);
		}
		XCloseDisplay(dpy);
	} else {
		syslog(LOG_ERR, "%s: server grabbed for over 10 seconds", display);
		return(-1);
	}
	(void) signal (SIGALRM, SIG_DFL);
	(void) alarm ((unsigned) 0);

	IdleTime /= 1000;
	return((IdleTime + 30) / 60);
}
#endif

static u_int
getidle(const char *tty, const char *display __unused)
{
	struct stat st;
	char ttyname[PATH_MAX];
	time_t now;
	u_long idle;

	/*
	 * If this is an X terminal or console, then try the
	 * XIdle extension
	 */
#ifdef XIDLE
	if (display && *display && (idle = XqueryIdle(display)) >= 0)
		return(idle);
#endif
	idle = 0;
	if (*tty == 'X') {
		u_long kbd_idle, mouse_idle;
#if	!defined(__FreeBSD__)
		kbd_idle = getidle("kbd", NULL);
#else
		kbd_idle = getidle("vga", NULL);
#endif
		mouse_idle = getidle("mouse", NULL);
		idle = (kbd_idle < mouse_idle)?kbd_idle:mouse_idle;
	} else {
		sprintf(ttyname, "%s/%s", _PATH_DEV, tty);
		if (stat(ttyname, &st) < 0) {
#ifdef DEBUG
			printf("%s: %s\n", ttyname, strerror(errno));
#endif
			return(-1);
		}
		time(&now);
#ifdef DEBUG
		printf("%s: now=%d atime=%d\n", ttyname, now,
		       st.st_atime);
#endif
		idle = now - st.st_atime;
		idle = (idle + 30) / 60; /* secs->mins */
	}

	return(idle);
}

static utmpidlearr *
do_names_2(void)
{
	static utmpidlearr ut;
	struct utmpx *usr;
	int nusers = 0;

	memset(&ut, 0, sizeof(ut));
	ut.utmpidlearr_val = &utmp_idle[0];

	setutxent();
	while ((usr = getutxent()) != NULL && nusers < MAXUSERS) {
		if (usr->ut_type != USER_PROCESS)
			continue;

		memcpy(&utmp_list[nusers], usr, sizeof(*usr));
		utmp_idle[nusers].ui_utmp.ut_time = usr->ut_tv.tv_sec;
		utmp_idle[nusers].ui_idle =
		    getidle(usr->ut_line, usr->ut_host);
		utmp_idle[nusers].ui_utmp.ut_line =
		    utmp_list[nusers].ut_line;
		utmp_idle[nusers].ui_utmp.ut_name =
		    utmp_list[nusers].ut_user;
		utmp_idle[nusers].ui_utmp.ut_host =
		    utmp_list[nusers].ut_host;

		nusers++;
	}
	endutxent();

	ut.utmpidlearr_len = nusers;
	return(&ut);
}

static int *
rusers_num(void *argp __unused, struct svc_req *rqstp __unused)
{
	static int num_users = 0;
	struct utmpx *usr;
 
	setutxent();
	while ((usr = getutxent()) != NULL) {
		if (usr->ut_type != USER_PROCESS)
			continue;
		num_users++;
	}
	endutxent();
 
	return(&num_users);
}

static utmparr *
do_names_1(void)
{
	utmpidlearr *utidle;
	static utmparr ut;
	unsigned int i;

	bzero((char *)&ut, sizeof(ut));

	utidle = do_names_2();
	if (utidle) {
		ut.utmparr_len = utidle->utmpidlearr_len;
		ut.utmparr_val = &old_utmp[0];
		for (i = 0; i < ut.utmparr_len; i++)
			bcopy(&utmp_idle[i].ui_utmp, &old_utmp[i],
			      sizeof(old_utmp[0]));

	}

	return(&ut);
}

utmpidlearr *
rusersproc_names_2_svc(void *argp __unused, struct svc_req *rqstp __unused)
{

	return (do_names_2());
}

utmpidlearr *
rusersproc_allnames_2_svc(void *argp __unused, struct svc_req *rqstp __unused)
{

	return (do_names_2());
}

utmparr *
rusersproc_names_1_svc(void *argp __unused, struct svc_req *rqstp __unused)
{

	return (do_names_1());
}

utmparr *
rusersproc_allnames_1_svc(void *argp __unused, struct svc_req *rqstp __unused)
{

	return (do_names_1());
}

typedef void *(*rusersproc_t)(void *, struct svc_req *);

void
rusers_service(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		int fill;
	} argument;
	char *result;
	xdrproc_t xdr_argument, xdr_result;
	rusersproc_t local;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void)svc_sendreply(transp, (xdrproc_t)xdr_void, NULL);
		goto leave;

	case RUSERSPROC_NUM:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_int;
		local = (rusersproc_t)rusers_num;
		break;

	case RUSERSPROC_NAMES:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_utmpidlearr;
		switch (rqstp->rq_vers) {
		case RUSERSVERS_ORIG:
			local = (rusersproc_t)rusersproc_names_1_svc;
			break;
		case RUSERSVERS_IDLE:
			local = (rusersproc_t)rusersproc_names_2_svc;
			break;
		default:
			svcerr_progvers(transp, RUSERSVERS_ORIG, RUSERSVERS_IDLE);
			goto leave;
			/*NOTREACHED*/
		}
		break;

	case RUSERSPROC_ALLNAMES:
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_utmpidlearr;
		switch (rqstp->rq_vers) {
		case RUSERSVERS_ORIG:
			local = (rusersproc_t)rusersproc_allnames_1_svc;
			break;
		case RUSERSVERS_IDLE:
			local = (rusersproc_t)rusersproc_allnames_2_svc;
			break;
		default:
			svcerr_progvers(transp, RUSERSVERS_ORIG, RUSERSVERS_IDLE);
			goto leave;
			/*NOTREACHED*/
		}
		break;

	default:
		svcerr_noproc(transp);
		goto leave;
	}
	bzero(&argument, sizeof(argument));
	if (!svc_getargs(transp, (xdrproc_t)xdr_argument, &argument)) {
		svcerr_decode(transp);
		goto leave;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL &&
	    !svc_sendreply(transp, (xdrproc_t)xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, (xdrproc_t)xdr_argument, &argument)) {
		syslog(LOG_ERR, "unable to free arguments");
		exit(1);
	}
leave:
	if (from_inetd)
		exit(0);
}
