/*	$OpenBSD: rbootd.c,v 1.32 2019/06/28 13:32:50 deraadt Exp $	*/
/*	$NetBSD: rbootd.c,v 1.5 1995/10/06 05:12:17 thorpej Exp $	*/

/*
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
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
 *
 *	from: @(#)rbootd.c	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: rbootd.c 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 */

#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <poll.h>

#include "defs.h"

extern	char *__progname;	/* from crt0.o */

volatile sig_atomic_t	dodebugoff;
volatile sig_atomic_t	dodebugon;
volatile sig_atomic_t	doreconfig;

void DebugOff(int);
void DebugOn(int);
void ReConfig(int);
void Exit(int);

void DoDebugOff(void);
void DoDebugOn(void);
void DoReConfig(void);

void DoTimeout(void);
CLIENT *FindClient(RMPCONN *);

int
main(int argc, char *argv[])
{
	int c, fd;
	struct passwd *pw;
	struct pollfd pfd[1];

	closefrom(STDERR_FILENO + 1);

	if ((pw = getpwnam("_rbootd")) == NULL)
		err(1, "getpwnam");

	while ((c = getopt(argc, argv, "adi:")) != -1)
		switch (c) {
		case 'a':
			BootAny++;
			break;
		case 'd':
			DebugFlg++;
			break;
		case 'i':
			IntfName = optarg;
			break;
		}
	for (; optind < argc; optind++) {
		if (ConfigFile == NULL)
			ConfigFile = argv[optind];
		else {
			warnx("too many config files (`%s' ignored)",
			    argv[optind]);
		}
	}

	if (ConfigFile == NULL)			/* use default config file */
		ConfigFile = DfltConfig;

	if (DebugFlg) {
		DbgFp = stdout;				/* output to stdout */

		(void) signal(SIGUSR1, SIG_IGN);	/* dont muck w/DbgFp */
		(void) signal(SIGUSR2, SIG_IGN);
		(void) fclose(stderr);			/* finished with it */
	} else {
		if (daemon(0, 0))
			err(1, "can't detach from terminal");

		(void) signal(SIGUSR1, DebugOn);
		(void) signal(SIGUSR2, DebugOff);
	}

	/*
	 *  If no interface was specified, get one now.
	 *
	 *  This is convoluted because we want to get the default interface
	 *  name for the syslog("restarted") message.  If BpfGetIntfName()
	 *  runs into an error, it will return a syslog-able error message
	 *  (in `errmsg') which will be displayed here.
	 */
	if (IntfName == NULL) {
		char *errmsg;

		if ((IntfName = BpfGetIntfName(&errmsg)) == NULL) {
			/* BpfGetIntfName() returns safe names, using %m */
			syslog(LOG_ERR, "%s", errmsg);
			DoExit();
		}
	}

	openlog(__progname, LOG_PID, LOG_DAEMON);
	fd = BpfOpen();
	syslog(LOG_NOTICE, "restarted (%s)", IntfName);

	(void) signal(SIGHUP, ReConfig);
	(void) signal(SIGINT, Exit);
	(void) signal(SIGTERM, Exit);

	gethostname(MyHost, HOST_NAME_MAX+1);

	/*
	 *  All boot files are relative to the boot directory, we might
	 *  as well chdir() there to make life easier.
	 */
	if (chdir(BootDir) == -1) {
		syslog(LOG_ERR, "chdir: %m (%s)", BootDir);
		DoExit();
	}

	/*
	 *  Initial configuration.
	 */
	if (GetBootFiles() == 0)		/* get list of boot files */
		DoExit();
	if (ParseConfig() == 0)			/* parse config file */
		DoExit();

	if (chroot(BootDir) == -1) {
		syslog(LOG_CRIT, "chroot %s: %m", BootDir);
		exit(1);
	}
	if (chdir("/") == -1) {
		syslog(LOG_CRIT, "chdir(\"/\"): %m");
		exit(1);
	}
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid)) {
		syslog(LOG_CRIT, "can't drop privileges: %m");
		exit(1);
	}
	endpwent();

	/*
	 *  Main loop: receive a packet, determine where it came from,
	 *  and if we service this host, call routine to handle request.
	 */
	pfd[0].fd = fd;
	pfd[0].events = POLLIN;
	for (;;) {
		int nsel;

		/*
		 * Check pending actions
		 */
		if (dodebugoff) {
			DoDebugOff();
			dodebugoff = 0;
		}
		if (dodebugon) {
			DoDebugOn();
			dodebugon = 0;
		}
		if (doreconfig) {
			DoReConfig();
			doreconfig = 0;
		}

		nsel = poll(pfd, 1, RmpConns ? RMP_TIMEOUT * 100 : -1);

		if (nsel == -1) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "poll: %m");
			DoExit();
		} else if (nsel == 0) {		/* timeout */
			DoTimeout();		/* clear stale conns */
			continue;
		}

		if (pfd[0].revents & POLLIN) {
			RMPCONN rconn;
			CLIENT *client;
			int doread = 1;

			while (BpfRead(&rconn, doread)) {
				doread = 0;

				if (DbgFp != NULL)	/* display packet */
					DispPkt(&rconn,DIR_RCVD);

				/*
				 *  If we do not restrict service, set the
				 *  client to NULL (ProcessPacket() handles
				 *  this).  Otherwise, check that we can
				 *  service this host; if not, log a message
				 *  and ignore the packet.
				 */
				if (BootAny) {
					client = NULL;
				} else if ((client=FindClient(&rconn))==NULL) {
					syslog(LOG_INFO,
					    "%s: boot packet ignored",
					    EnetStr(&rconn));
					continue;
				}

				ProcessPacket(&rconn,client);
			}
		}
	}
}

/*
**  DoTimeout -- Free any connections that have timed out.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- Timed out connections in `RmpConns' will be freed.
*/
void
DoTimeout(void)
{
	RMPCONN *rtmp;
	struct timeval now;

	(void) gettimeofday(&now, NULL);

	/*
	 *  For each active connection, if RMP_TIMEOUT seconds have passed
	 *  since the last packet was sent, delete the connection.
	 */
	for (rtmp = RmpConns; rtmp != NULL; rtmp = rtmp->next)
		if ((rtmp->tstamp.tv_sec + RMP_TIMEOUT) < now.tv_sec) {
			syslog(LOG_WARNING, "%s: connection timed out (%u)",
			    EnetStr(rtmp), rtmp->rmp.r_type);
			RemoveConn(rtmp);
		}
}

/*
**  FindClient -- Find client associated with a packet.
**
**	Parameters:
**		rconn - the new packet.
**
**	Returns:
**		Pointer to client info if found, NULL otherwise.
**
**	Side Effects:
**		None.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked since
**		  a reconfigure can invalidate the information returned.
*/
CLIENT *
FindClient(RMPCONN *rconn)
{
	CLIENT *ctmp;

	for (ctmp = Clients; ctmp != NULL; ctmp = ctmp->next)
		if (bcmp((char *)&rconn->rmp.hp_hdr.saddr[0],
		    (char *)&ctmp->addr[0], RMP_ADDRLEN) == 0)
			break;

	return(ctmp);
}

/*
**  Exit -- Log an error message and exit.
**
**	Parameters:
**		sig - caught signal (or zero if not dying on a signal).
**
**	Returns:
**		Does not return.
**
**	Side Effects:
**		- This process ceases to exist.
*/
void
Exit(int sig)
{
	struct syslog_data sdata = SYSLOG_DATA_INIT;

	syslog_r(LOG_ERR, &sdata, "going down on signal %d", sig);
	_exit(1);
}

void
DoExit(void)
{
	syslog(LOG_ERR, "going down on fatal error");
	exit(1);
}

/*
**  ReConfig -- Get new list of boot files and reread config files.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- All active connections are dropped.
**		- List of bootable files is changed.
**		- List of clients is changed.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked.
*/
void
ReConfig(int signo)
{
	doreconfig = 1;
}

void
DoReConfig(void)
{
	syslog(LOG_NOTICE, "reconfiguring boot server");

	FreeConns();

	if (GetBootFiles() == 0)
		DoExit();

	if (ParseConfig() == 0)
		DoExit();
}

/*
**  DebugOff -- Turn off debugging.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- Debug file is closed.
*/
void
DebugOff(int signo)
{
	dodebugoff = 1;
}

void
DoDebugOff(void)
{
	if (DbgFp != NULL)
		(void) fclose(DbgFp);

	DbgFp = NULL;
}

/*
**  DebugOn -- Turn on debugging.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- Debug file is opened/truncated if not already opened,
**		  otherwise do nothing.
*/
void
DebugOn(int signo)
{
	dodebugon = 1;
}

void
DoDebugOn(void)
{
	if (DbgFp == NULL) {
		if ((DbgFp = fopen(DbgFile, "w")) == NULL)
			syslog(LOG_ERR, "can't open debug file (%s)", DbgFile);
	}
}
