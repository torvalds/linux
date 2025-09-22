/*	$OpenBSD: comsat.c,v 1.51 2023/03/08 04:43:05 guenther Exp $	*/

/*
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

#include <sys/limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>
#include <vis.h>
#include <err.h>

int	debug = 0;
#define	dsyslog	if (debug) syslog

#define MAXIDLE	120

char	hostname[HOST_NAME_MAX+1];
struct	utmp *utmp = NULL;
time_t	lastmsgtime;
int	nutmp, uf;

void jkfprintf(FILE *, char[], off_t);
void mailfor(char *);
void notify(struct utmp *, off_t);
void readutmp(int);
void doreadutmp(void);
void reapchildren(int);

volatile sig_atomic_t wantreadutmp;

int
main(int argc, char *argv[])
{
	struct sockaddr_storage from;
	struct sigaction sa;
	ssize_t cc;
	socklen_t fromlen;
	char msgbuf[100];
	sigset_t sigset;

	/* verify proper invocation */
	fromlen = sizeof(from);
	if (getsockname(0, (struct sockaddr *)&from, &fromlen) == -1) {
		(void)fprintf(stderr,
		    "comsat: getsockname: %s.\n", strerror(errno));
		exit(1);
	}

	if (unveil(_PATH_MAILDIR, "r") == -1)
		err(1, "unveil %s", _PATH_MAILDIR);
	if (unveil(_PATH_UTMP, "r") == -1)
		err(1, "unveil %s", _PATH_UTMP);
	if (unveil("/tmp", "w") == -1)
		err(1, "unveil /tmp");
	if (unveil(_PATH_DEV, "rw") == -1)
		err(1, "unveil %s", _PATH_DEV);
	if (pledge("stdio rpath wpath proc tty", NULL) == -1)
		err(1, "pledge");

	openlog("comsat", LOG_PID, LOG_DAEMON);
	if (chdir(_PATH_MAILDIR)) {
		syslog(LOG_ERR, "chdir: %s: %m", _PATH_MAILDIR);
		(void) recv(0, msgbuf, sizeof(msgbuf) - 1, 0);
		exit(1);
	}
	if ((uf = open(_PATH_UTMP, O_RDONLY)) == -1) {
		syslog(LOG_ERR, "open: %s: %m", _PATH_UTMP);
		(void) recv(0, msgbuf, sizeof(msgbuf) - 1, 0);
		exit(1);
	}
	(void)time(&lastmsgtime);
	(void)gethostname(hostname, sizeof(hostname));
	doreadutmp();

	(void)signal(SIGTTOU, SIG_IGN);

	bzero(&sa, sizeof sa);
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = readutmp;
	sa.sa_flags = 0;			/* no SA_RESTART */
	(void)sigaction(SIGALRM, &sa, NULL);

	sa.sa_handler = reapchildren;
	sa.sa_flags = SA_RESTART;
	(void)sigaction(SIGCHLD, &sa, NULL);

	for (;;) {
		if (wantreadutmp) {
			wantreadutmp = 0;
			doreadutmp();
		}

		cc = recv(0, msgbuf, sizeof(msgbuf) - 1, 0);
		if (cc <= 0) {
			if (errno != EINTR)
				sleep(1);
			continue;
		}
		if (!nutmp)		/* no one has logged in yet */
			continue;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGALRM);
		sigprocmask(SIG_SETMASK, &sigset, NULL);
		msgbuf[cc] = '\0';
		(void)time(&lastmsgtime);
		mailfor(msgbuf);
		sigemptyset(&sigset);
		sigprocmask(SIG_SETMASK, &sigset, NULL);
	}
}

void
reapchildren(int signo)
{
	int save_errno = errno;

	while (wait3(NULL, WNOHANG, NULL) > 0)
		;
	errno = save_errno;
}

void
readutmp(int signo)
{
	wantreadutmp = 1;
}

void
doreadutmp(void)
{
	static u_int utmpsize;		/* last malloced size for utmp */
	static time_t utmpmtime;	/* last modification time for utmp */
	struct stat statbf;
	int n;

	if (time(NULL) - lastmsgtime >= MAXIDLE)
		exit(0);
	(void)fstat(uf, &statbf);
	if (statbf.st_mtime > utmpmtime) {
		utmpmtime = statbf.st_mtime;
		/* avoid int overflow */
		if (statbf.st_size > INT_MAX - 10 * sizeof(struct utmp)) {
			syslog(LOG_ALERT, "utmp file excessively large");
			exit(1);
		}
		if (statbf.st_size > utmpsize) {
			u_int nutmpsize = statbf.st_size + 10 *
			    sizeof(struct utmp);
			struct utmp *u;

			if ((u = recallocarray(utmp, utmpsize,
			    nutmpsize, 1)) == NULL) {
				free(utmp);
				syslog(LOG_ERR, "%s", strerror(errno));
				exit(1);
			}
			utmp = u;
			utmpsize = nutmpsize;
		}
		n = pread(uf, utmp, statbf.st_size, 0);
		if (n == -1)
			n = 0;
		nutmp = n / sizeof(struct utmp);
		dsyslog(LOG_DEBUG, "read %d utmp entries", nutmp);
	}
	(void)alarm(15);
}

void
mailfor(char *name)
{
	struct utmp *utp = &utmp[nutmp];
	char utname[UT_NAMESIZE+1];
	const char *errstr;
	char *cp;
	off_t offset;

	dsyslog(LOG_DEBUG, "mail for '%s'", name);
	if (!(cp = strchr(name, '@')))
		return;
	*cp++ = '\0';
	cp[strcspn(cp, " \t\n")] = '\0';
	offset = strtonum(cp, 0, LLONG_MAX, &errstr);
	if (errstr) {
		syslog(LOG_ERR, "'%s' is %s", cp + 1, errstr);
		return;
	}
	while (--utp >= utmp) {
		memcpy(utname, utp->ut_name, UT_NAMESIZE);
		utname[UT_NAMESIZE] = '\0';
		dsyslog(LOG_DEBUG, "check %s against %s", name, utname);
		if (!strncmp(utname, name, UT_NAMESIZE))
			notify(utp, offset);
	}
}

static char *cr;

void
notify(struct utmp *utp, off_t offset)
{
	int fd;
	FILE *tp;
	struct stat stb;
	struct termios ttybuf;
	char tty[PATH_MAX], name[UT_NAMESIZE + 1];

	(void)snprintf(tty, sizeof(tty), "%s%.*s",
	    _PATH_DEV, (int)sizeof(utp->ut_line), utp->ut_line);
	if (strchr(tty + sizeof(_PATH_DEV) - 1, '/')) {
		/* A slash is an attempt to break security... */
		syslog(LOG_AUTH | LOG_NOTICE, "'/' in \"%s\"", tty);
		return;
	}
	if (stat(tty, &stb) || !(stb.st_mode & S_IEXEC)) {
		dsyslog(LOG_DEBUG, "%.*s: wrong mode on %s",
		    (int)sizeof(utp->ut_name), utp->ut_name, tty);
		return;
	}
	dsyslog(LOG_DEBUG, "notify %.*s on %s", (int)sizeof(utp->ut_name),
	    utp->ut_name, tty);
	if (fork())
		return;
	(void)signal(SIGALRM, SIG_DFL);
	(void)alarm(30);
	fd = open(tty, O_WRONLY);
	if (fd == -1 || (tp = fdopen(fd, "w")) == NULL) {
		dsyslog(LOG_ERR, "%s: %s", tty, strerror(errno));
		_exit(1);
	}
	(void)tcgetattr(fileno(tp), &ttybuf);
	cr = (ttybuf.c_oflag & ONLCR) && (ttybuf.c_oflag & OPOST) ?
	    "\n" : "\n\r";
	memcpy(name, utp->ut_name, UT_NAMESIZE);
	name[UT_NAMESIZE] = '\0';
	(void)fprintf(tp, "%s\007New mail for %s@%.*s\007 has arrived:%s----%s",
	    cr, name, (int)sizeof(hostname), hostname, cr, cr);
	jkfprintf(tp, name, offset);
	(void)fclose(tp);
	_exit(0);
}

void
jkfprintf(FILE *tp, char name[], off_t offset)
{
	char *cp, ch;
	char visout[5], *s2;
	FILE *fi;
	int linecnt, charcnt, inheader;
	char line[BUFSIZ];

	if ((fi = fopen(name, "r")) == NULL)
		return;

	(void)fseeko(fi, offset, SEEK_SET);
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
			ch = toascii(ch);
			vis(visout, ch, VIS_SAFE|VIS_NOSLASH, cp[1]);
			for (s2 = visout; *s2; s2++)
				(void)fputc(*s2, tp);
		}
		(void)fputs(cr, tp);
		--linecnt;
	}
	(void)fprintf(tp, "----%s\n", cr);
	(void)fclose(fi);
}
