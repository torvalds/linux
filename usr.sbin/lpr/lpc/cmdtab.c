/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
static char sccsid[] = "@(#)cmdtab.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include "lpc.h"
#include "extern.h"

/*
 * lpc -- command tables
 */
char	aborthelp[] =	"terminate a spooling daemon immediately and disable printing";
char	botmqhelp[] =	"move job(s) to the bottom of printer queue";
char	cleanhelp[] =	"remove cruft files from a queue";
char	enablehelp[] =	"turn a spooling queue on";
char	disablehelp[] =	"turn a spooling queue off";
char	downhelp[] =	"do a 'stop' followed by 'disable' and put a message in status";
char	helphelp[] =	"get help on commands";
char	quithelp[] =	"exit lpc";
char	restarthelp[] =	"kill (if possible) and restart a spooling daemon";
char	setstatushelp[] = "set the status message of a queue, requires\n"
			"\t\t\"-msg\" before the text of the new message";
char	starthelp[] =	"enable printing and start a spooling daemon";
char	statushelp[] =	"show status of daemon and queue";
char	stophelp[] =	"stop a spooling daemon after current job completes and disable printing";
char	tcleanhelp[] =	"test to see what files a clean cmd would remove";
char	topqhelp[] =	"move job(s) to the top of printer queue";
char	uphelp[] =	"enable everything and restart spooling daemon";

/* Use some abbreviations so entries won't need to wrap */
#define PR	LPC_PRIVCMD
#define M	LPC_MSGOPT

struct cmd cmdtab[] = {
	{ "abort",	aborthelp,	PR,	0,		abort_q },
	{ "bottomq",	botmqhelp,	PR,	bottomq_cmd,	0 },
	{ "clean",	cleanhelp,	PR,	clean_gi,	clean_q },
	{ "enable",	enablehelp,	PR,	0,		enable_q },
	{ "exit",	quithelp,	0,	quit,		0 },
	{ "disable",	disablehelp,	PR,	0, 		disable_q },
	{ "down",	downhelp,	PR|M,	down_gi,	down_q },
	{ "help",	helphelp,	0,	help,		0 },
	{ "quit",	quithelp,	0,	quit,		0 },
	{ "restart",	restarthelp,	0,	0,		restart_q },
	{ "start",	starthelp,	PR,	0,		start_q },
	{ "status",	statushelp,	0,	0,		status },
	{ "setstatus",	setstatushelp,	PR|M,	setstatus_gi,	setstatus_q },
	{ "stop",	stophelp,	PR,	0,		stop_q },
	{ "tclean",	tcleanhelp,	0,	tclean_gi,	clean_q },
	{ "topq",	topqhelp,	PR,	topq_cmd,	0 },
	{ "up",		uphelp,		PR,	0,		up_q },
	{ "?",		helphelp,	0,	help,		0 },
	{ "xtopq",	topqhelp,	PR,	topq,		0 },
	{ 0, 0, 0, 0, 0},
};

int	NCMDS = sizeof (cmdtab) / sizeof (cmdtab[0]);
