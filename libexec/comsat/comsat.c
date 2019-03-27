/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)comsat.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <paths.h>
#include <pwd.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <utmpx.h>

static int	debug = 0;
#define	dsyslog	if (debug) syslog

#define MAXIDLE	120

static char	hostname[MAXHOSTNAMELEN];

static void	jkfprintf(FILE *, char[], char[], off_t);
static void	mailfor(char *);
static void	notify(struct utmpx *, char[], off_t, int);
static void	reapchildren(int);

int
main(int argc __unused, char *argv[] __unused)
{
	struct sockaddr_in from;
	socklen_t fromlen;
	int cc;
	char msgbuf[256];

	/* verify proper invocation */
	fromlen = sizeof(from);
	if (getsockname(0, (struct sockaddr *)&from, &fromlen) < 0)
		err(1, "getsockname");
	openlog("comsat", LOG_PID, LOG_DAEMON);
	if (chdir(_PATH_MAILDIR)) {
		syslog(LOG_ERR, "chdir: %s: %m", _PATH_MAILDIR);
		(void) recv(0, msgbuf, sizeof(msgbuf) - 1, 0);
		exit(1);
	}
	(void)gethostname(hostname, sizeof(hostname));
	(void)signal(SIGTTOU, SIG_IGN);
	(void)signal(SIGCHLD, reapchildren);
	for (;;) {
		cc = recv(0, msgbuf, sizeof(msgbuf) - 1, 0);
		if (cc <= 0) {
			if (errno != EINTR)
				sleep(1);
			errno = 0;
			continue;
		}
		msgbuf[cc] = '\0';
		mailfor(msgbuf);
		sigsetmask(0L);
	}
}

static void
reapchildren(int signo __unused)
{
	while (wait3(NULL, WNOHANG, NULL) > 0);
}

static void
mailfor(char *name)
{
	struct utmpx *utp;
	char *cp;
	char *file;
	off_t offset;
	int folder;
	char buf[sizeof(_PATH_MAILDIR) + sizeof(utp->ut_user) + 1];
	char buf2[sizeof(_PATH_MAILDIR) + sizeof(utp->ut_user) + 1];

	if (!(cp = strchr(name, '@')))
		return;
	*cp = '\0';
	offset = strtoll(cp + 1, NULL, 10);
	if (!(cp = strchr(cp + 1, ':')))
		file = name;
	else
		file = cp + 1;
	sprintf(buf, "%s/%.*s", _PATH_MAILDIR, (int)sizeof(utp->ut_user),
	    name);
	if (*file != '/') {
		sprintf(buf2, "%s/%.*s", _PATH_MAILDIR,
		    (int)sizeof(utp->ut_user), file);
		file = buf2;
	}
	folder = strcmp(buf, file);
	setutxent();
	while ((utp = getutxent()) != NULL)
		if (utp->ut_type == USER_PROCESS && !strcmp(utp->ut_user, name))
			notify(utp, file, offset, folder);
	endutxent();
}

static const char *cr;

static void
notify(struct utmpx *utp, char file[], off_t offset, int folder)
{
	FILE *tp;
	struct stat stb;
	struct termios tio;
	char tty[20];
	const char *s = utp->ut_line;

	if (strncmp(s, "pts/", 4) == 0)
		s += 4;
	if (strchr(s, '/')) {
		/* A slash is an attempt to break security... */
		syslog(LOG_AUTH | LOG_NOTICE, "Unexpected `/' in `%s'",
		    utp->ut_line);
		return;
	}
	(void)snprintf(tty, sizeof(tty), "%s%.*s",
	    _PATH_DEV, (int)sizeof(utp->ut_line), utp->ut_line);
	if (stat(tty, &stb) == -1 || !(stb.st_mode & (S_IXUSR | S_IXGRP))) {
		dsyslog(LOG_DEBUG, "%s: wrong mode on %s", utp->ut_user, tty);
		return;
	}
	dsyslog(LOG_DEBUG, "notify %s on %s", utp->ut_user, tty);
	switch (fork()) {
	case -1:
		syslog(LOG_NOTICE, "fork failed (%m)");
		return;
	case 0:
		break;
	default:
		return;
	}
	if ((tp = fopen(tty, "w")) == NULL) {
		dsyslog(LOG_ERR, "%s: %s", tty, strerror(errno));
		_exit(1);
	}
	(void)tcgetattr(fileno(tp), &tio);
	cr = ((tio.c_oflag & (OPOST|ONLCR)) == (OPOST|ONLCR)) ?  "\n" : "\n\r";
	switch (stb.st_mode & (S_IXUSR | S_IXGRP)) {
	case S_IXUSR:
	case (S_IXUSR | S_IXGRP):
		(void)fprintf(tp, 
		    "%s\007New mail for %s@%.*s\007 has arrived%s%s%s:%s----%s",
		    cr, utp->ut_user, (int)sizeof(hostname), hostname,
		    folder ? cr : "", folder ? "to " : "", folder ? file : "",
		    cr, cr);
		jkfprintf(tp, utp->ut_user, file, offset);
		break;
	case S_IXGRP:
		(void)fprintf(tp, "\007");
		(void)fflush(tp);      
		(void)sleep(1);
		(void)fprintf(tp, "\007");
		break;
	default:
		break;
	}	
	(void)fclose(tp);
	_exit(0);
}

static void
jkfprintf(FILE *tp, char user[], char file[], off_t offset)
{
	unsigned char *cp, ch;
	FILE *fi;
	int linecnt, charcnt, inheader;
	struct passwd *p;
	unsigned char line[BUFSIZ];

	/* Set effective uid to user in case mail drop is on nfs */
	if ((p = getpwnam(user)) != NULL)
		(void) setuid(p->pw_uid);

	if ((fi = fopen(file, "r")) == NULL)
		return;

	(void)fseeko(fi, offset, SEEK_CUR);
	/*
	 * Print the first 7 lines or 560 characters of the new mail
	 * (whichever comes first).  Skip header crap other than
	 * From, Subject, To, and Date.
	 */
	linecnt = 7;
	charcnt = 560;
	inheader = 1;
	while (fgets(line, sizeof(line), fi) != NULL) {
		if (inheader) {
			if (line[0] == '\n') {
				inheader = 0;
				continue;
			}
			if (line[0] == ' ' || line[0] == '\t' ||
			    (strncmp(line, "From:", 5) &&
			    strncmp(line, "Subject:", 8)))
				continue;
		}
		if (linecnt <= 0 || charcnt <= 0) {
			(void)fprintf(tp, "...more...%s", cr);
			(void)fclose(fi);
			return;
		}
		/* strip weird stuff so can't trojan horse stupid terminals */
		for (cp = line; (ch = *cp) && ch != '\n'; ++cp, --charcnt) {
			/* disable upper controls and enable all other
			   8bit codes due to lack of locale knowledge
			 */
			if (((ch & 0x80) && ch < 0xA0) ||
			    (!(ch & 0x80) && !isprint(ch) &&
			     !isspace(ch) && ch != '\a' && ch != '\b')
			   ) {
				if (ch & 0x80) {
					ch &= ~0x80;
					(void)fputs("M-", tp);
				}
				if (iscntrl(ch)) {
					ch ^= 0x40;
					(void)fputc('^', tp);
				}
			}
			(void)fputc(ch, tp);
		}
		(void)fputs(cr, tp);
		--linecnt;
	}
	(void)fprintf(tp, "----%s\n", cr);
	(void)fclose(fi);
}
