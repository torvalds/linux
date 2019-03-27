/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jef Poskanzer and Craig Leres of the Lawrence Berkeley Laboratory.
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
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)write.c	8.1 (Berkeley) 6/6/93";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/filio.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>
#include <wchar.h>
#include <wctype.h>

void done(int);
void do_write(int, char *, char *, const char *);
static void usage(void);
int term_chk(int, char *, int *, time_t *, int);
void wr_fputs(wchar_t *s);
void search_utmp(int, char *, char *, char *, uid_t);
int utmp_chk(char *, char *);

int
main(int argc, char **argv)
{
	unsigned long cmds[] = { TIOCGETA, TIOCGWINSZ, FIODGNAME };
	cap_rights_t rights;
	struct passwd *pwd;
	time_t atime;
	uid_t myuid;
	int msgsok, myttyfd;
	char tty[MAXPATHLEN], *mytty;
	const char *login;
	int devfd;

	(void)setlocale(LC_CTYPE, "");

	devfd = open(_PATH_DEV, O_RDONLY);
	if (devfd < 0)
		err(1, "open(/dev)");
	cap_rights_init(&rights, CAP_FCNTL, CAP_FSTAT, CAP_IOCTL, CAP_LOOKUP,
	    CAP_PWRITE);
	if (caph_rights_limit(devfd, &rights) < 0)
		err(1, "can't limit devfd rights");

	/*
	 * Can't use capsicum helpers here because we need the additional
	 * FIODGNAME ioctl.
	 */
	cap_rights_init(&rights, CAP_FCNTL, CAP_FSTAT, CAP_IOCTL, CAP_READ,
	    CAP_WRITE);
	if (caph_rights_limit(STDIN_FILENO, &rights) < 0 ||
	    caph_rights_limit(STDOUT_FILENO, &rights) < 0 ||
	    caph_rights_limit(STDERR_FILENO, &rights) < 0 ||
	    caph_ioctls_limit(STDIN_FILENO, cmds, nitems(cmds)) < 0 ||
	    caph_ioctls_limit(STDOUT_FILENO, cmds, nitems(cmds)) < 0 ||
	    caph_ioctls_limit(STDERR_FILENO, cmds, nitems(cmds)) < 0 ||
	    caph_fcntls_limit(STDIN_FILENO, CAP_FCNTL_GETFL) < 0 ||
	    caph_fcntls_limit(STDOUT_FILENO, CAP_FCNTL_GETFL) < 0 ||
	    caph_fcntls_limit(STDERR_FILENO, CAP_FCNTL_GETFL) < 0)
		err(1, "can't limit stdio rights");

	caph_cache_catpages();
	caph_cache_tzdata();

	/*
	 * Cache UTX database fds.
	 */
	setutxent();

	/*
	 * Determine our login name before we reopen() stdout
	 * and before entering capability sandbox.
	 */
	myuid = getuid();
	if ((login = getlogin()) == NULL) {
		if ((pwd = getpwuid(myuid)))
			login = pwd->pw_name;
		else
			login = "???";
	}

	if (caph_enter() < 0)
		err(1, "cap_enter");

	while (getopt(argc, argv, "") != -1)
		usage();
	argc -= optind;
	argv += optind;

	/* check that sender has write enabled */
	if (isatty(fileno(stdin)))
		myttyfd = fileno(stdin);
	else if (isatty(fileno(stdout)))
		myttyfd = fileno(stdout);
	else if (isatty(fileno(stderr)))
		myttyfd = fileno(stderr);
	else
		errx(1, "can't find your tty");
	if (!(mytty = ttyname(myttyfd)))
		errx(1, "can't find your tty's name");
	if (!strncmp(mytty, _PATH_DEV, strlen(_PATH_DEV)))
		mytty += strlen(_PATH_DEV);
	if (term_chk(devfd, mytty, &msgsok, &atime, 1))
		exit(1);
	if (!msgsok)
		errx(1, "you have write permission turned off");

	/* check args */
	switch (argc) {
	case 1:
		search_utmp(devfd, argv[0], tty, mytty, myuid);
		do_write(devfd, tty, mytty, login);
		break;
	case 2:
		if (!strncmp(argv[1], _PATH_DEV, strlen(_PATH_DEV)))
			argv[1] += strlen(_PATH_DEV);
		if (utmp_chk(argv[0], argv[1]))
			errx(1, "%s is not logged in on %s", argv[0], argv[1]);
		if (term_chk(devfd, argv[1], &msgsok, &atime, 1))
			exit(1);
		if (myuid && !msgsok)
			errx(1, "%s has messages disabled on %s", argv[0], argv[1]);
		do_write(devfd, argv[1], mytty, login);
		break;
	default:
		usage();
	}
	done(0);
	return (0);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: write user [tty]\n");
	exit(1);
}

/*
 * utmp_chk - checks that the given user is actually logged in on
 *     the given tty
 */
int
utmp_chk(char *user, char *tty)
{
	struct utmpx lu, *u;

	strncpy(lu.ut_line, tty, sizeof lu.ut_line);
	while ((u = getutxline(&lu)) != NULL)
		if (u->ut_type == USER_PROCESS &&
		    strcmp(user, u->ut_user) == 0) {
			endutxent();
			return(0);
		}
	endutxent();
	return(1);
}

/*
 * search_utmp - search utmp for the "best" terminal to write to
 *
 * Ignores terminals with messages disabled, and of the rest, returns
 * the one with the most recent access time.  Returns as value the number
 * of the user's terminals with messages enabled, or -1 if the user is
 * not logged in at all.
 *
 * Special case for writing to yourself - ignore the terminal you're
 * writing from, unless that's the only terminal with messages enabled.
 */
void
search_utmp(int devfd, char *user, char *tty, char *mytty, uid_t myuid)
{
	struct utmpx *u;
	time_t bestatime, atime;
	int nloggedttys, nttys, msgsok, user_is_me;

	nloggedttys = nttys = 0;
	bestatime = 0;
	user_is_me = 0;

	while ((u = getutxent()) != NULL)
		if (u->ut_type == USER_PROCESS &&
		    strcmp(user, u->ut_user) == 0) {
			++nloggedttys;
			if (term_chk(devfd, u->ut_line, &msgsok, &atime, 0))
				continue;	/* bad term? skip */
			if (myuid && !msgsok)
				continue;	/* skip ttys with msgs off */
			if (strcmp(u->ut_line, mytty) == 0) {
				user_is_me = 1;
				continue;	/* don't write to yourself */
			}
			++nttys;
			if (atime > bestatime) {
				bestatime = atime;
				(void)strlcpy(tty, u->ut_line, MAXPATHLEN);
			}
		}
	endutxent();

	if (nloggedttys == 0)
		errx(1, "%s is not logged in", user);
	if (nttys == 0) {
		if (user_is_me) {		/* ok, so write to yourself! */
			(void)strlcpy(tty, mytty, MAXPATHLEN);
			return;
		}
		errx(1, "%s has messages disabled", user);
	} else if (nttys > 1) {
		warnx("%s is logged in more than once; writing to %s", user, tty);
	}
}

/*
 * term_chk - check that a terminal exists, and get the message bit
 *     and the access time
 */
int
term_chk(int devfd, char *tty, int *msgsokP, time_t *atimeP, int showerror)
{
	struct stat s;

	if (fstatat(devfd, tty, &s, 0) < 0) {
		if (showerror)
			warn("%s%s", _PATH_DEV, tty);
		return(1);
	}
	*msgsokP = (s.st_mode & (S_IWRITE >> 3)) != 0;	/* group write bit */
	*atimeP = s.st_atime;
	return(0);
}

/*
 * do_write - actually make the connection
 */
void
do_write(int devfd, char *tty, char *mytty, const char *login)
{
	char *nows;
	time_t now;
	char host[MAXHOSTNAMELEN];
	wchar_t line[512];
	int fd;

	fd = openat(devfd, tty, O_WRONLY);
	if (fd < 0)
		err(1, "openat(%s%s)", _PATH_DEV, tty);
	fclose(stdout);
	stdout = fdopen(fd, "w");
	if (stdout == NULL)
		err(1, "%s%s", _PATH_DEV, tty);

	(void)signal(SIGINT, done);
	(void)signal(SIGHUP, done);

	/* print greeting */
	if (gethostname(host, sizeof(host)) < 0)
		(void)strcpy(host, "???");
	now = time((time_t *)NULL);
	nows = ctime(&now);
	nows[16] = '\0';
	(void)printf("\r\n\007\007\007Message from %s@%s on %s at %s ...\r\n",
	    login, host, mytty, nows + 11);

	while (fgetws(line, sizeof(line)/sizeof(wchar_t), stdin) != NULL)
		wr_fputs(line);
}

/*
 * done - cleanup and exit
 */
void
done(int n __unused)
{
	(void)printf("EOF\r\n");
	exit(0);
}

/*
 * wr_fputs - like fputs(), but makes control characters visible and
 *     turns \n into \r\n
 */
void
wr_fputs(wchar_t *s)
{

#define	PUTC(c)	if (putwchar(c) == WEOF) err(1, NULL);

	for (; *s != L'\0'; ++s) {
		if (*s == L'\n') {
			PUTC(L'\r');
			PUTC(L'\n');
		} else if (iswprint(*s) || iswspace(*s)) {
			PUTC(*s);
		} else {
			wprintf(L"<0x%X>", *s);
		}
	}
	return;
#undef PUTC
}
