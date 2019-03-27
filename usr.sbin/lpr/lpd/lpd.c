/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
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
"@(#) Copyright (c) 1983, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)lpd.c	8.7 (Berkeley) 5/10/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * lpd -- line printer daemon.
 *
 * Listen for a connection and perform the requested operation.
 * Operations are:
 *	\1printer\n
 *		check the queue for jobs and print any found.
 *	\2printer\n
 *		receive a job from another machine and queue it.
 *	\3printer [users ...] [jobs ...]\n
 *		return the current state of the queue (short form).
 *	\4printer [users ...] [jobs ...]\n
 *		return the current state of the queue (long form).
 *	\5printer person [users ...] [jobs ...]\n
 *		remove jobs from the queue.
 *
 * Strategy to maintain protected spooling area:
 *	1. Spooling area is writable only by daemon and spooling group
 *	2. lpr runs setuid root and setgrp spooling group; it uses
 *	   root to access any file it wants (verifying things before
 *	   with an access call) and group id to know how it should
 *	   set up ownership of files in the spooling area.
 *	3. Files in spooling area are owned by root, group spooling
 *	   group, with mode 660.
 *	4. lpd, lpq and lprm run setuid daemon and setgrp spooling group to
 *	   access files and printer.  Users can't get to anything
 *	   w/o help of lpq and lprm programs.
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <ctype.h>
#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"
#include "extern.h"

int	lflag;				/* log requests flag */
int	sflag;				/* no incoming port flag */
int	from_remote;			/* from remote socket */

int		 main(int argc, char **_argv);
static void	 reapchild(int _signo);
static void	 mcleanup(int _signo);
static void	 doit(void);
static void	 startup(void);
static void	 chkhost(struct sockaddr *_f, int _ch_opts);
static int	 ckqueue(struct printer *_pp);
static void	 fhosterr(int _ch_opts, char *_sysmsg, char *_usermsg);
static int	*socksetup(int _af, int _debuglvl);
static void	 usage(void);

/* XXX from libc/net/rcmd.c */
extern int __ivaliduser_sa(FILE *, struct sockaddr *, socklen_t,
				const char *, const char *);

uid_t	uid, euid;

#define LPD_NOPORTCHK	0001		/* skip reserved-port check */
#define LPD_LOGCONNERR	0002		/* (sys)log connection errors */
#define LPD_ADDFROMLINE	0004		/* just used for fhosterr() */

int
main(int argc, char **argv)
{
	int ch_options, errs, f, funix, *finet, i, lfd, socket_debug;
	fd_set defreadfds;
	struct sockaddr_un un, fromunix;
	struct sockaddr_storage frominet;
	socklen_t fromlen;
	sigset_t omask, nmask;
	struct servent *sp, serv;
	int inet_flag = 0, inet6_flag = 0;

	euid = geteuid();	/* these shouldn't be different */
	uid = getuid();

	ch_options = 0;
	socket_debug = 0;
	gethostname(local_host, sizeof(local_host));

	progname = "lpd";

	if (euid != 0)
		errx(EX_NOPERM,"must run as root");

	errs = 0;
	while ((i = getopt(argc, argv, "cdlpswW46")) != -1)
		switch (i) {
		case 'c':
			/* log all kinds of connection-errors to syslog */
			ch_options |= LPD_LOGCONNERR;
			break;
		case 'd':
			socket_debug++;
			break;
		case 'l':
			lflag++;
			break;
		case 'p':		/* letter initially used for -s */
			/*
			 * This will probably be removed with 5.0-release.
			 */
			/* FALLTHROUGH */
		case 's':		/* secure (no inet) */
			sflag++;
			break;
		case 'w':		/* netbsd uses -w for maxwait */
			/*
			 * This will be removed after the release of 4.4, as
			 * it conflicts with -w in netbsd's lpd.  For now it
			 * is just a warning, so we won't suddenly break lpd
			 * for anyone who is currently using the option.
			 */
			syslog(LOG_WARNING,
			    "NOTE: the -w option has been renamed -W");
			syslog(LOG_WARNING,
			    "NOTE: please change your lpd config to use -W");
			/* FALLTHROUGH */
		case 'W':
			/* allow connections coming from a non-reserved port */
			/* (done by some lpr-implementations for MS-Windows) */ 
			ch_options |= LPD_NOPORTCHK;
			break;
		case '4':
			family = PF_INET;
			inet_flag++;
			break;
		case '6':
#ifdef INET6
			family = PF_INET6;
			inet6_flag++;
#else
			errx(EX_USAGE, "lpd compiled sans INET6 (IPv6 support)");
#endif
			break;
		/*
		 * The following options are not in FreeBSD (yet?), but are
		 * listed here to "reserve" them, because the option-letters
		 * are used by either NetBSD or OpenBSD (as of July 2001).
		 */ 
		case 'b':		/* set bind-addr */
		case 'n':		/* set max num of children */
		case 'r':		/* allow 'of' for remote ptrs */
					/* ...[not needed in freebsd] */
			/* FALLTHROUGH */
		default:
			errs++;
		}
	if (inet_flag && inet6_flag)
		family = PF_UNSPEC;
	argc -= optind;
	argv += optind;
	if (errs)
		usage();

	if (argc == 1) {
		if ((i = atoi(argv[0])) == 0)
			usage();
		if (i < 0 || i > USHRT_MAX)
			errx(EX_USAGE, "port # %d is invalid", i);

		serv.s_port = htons(i);
		sp = &serv;
		argc--;
	} else {
		sp = getservbyname("printer", "tcp");
		if (sp == NULL)
			errx(EX_OSFILE, "printer/tcp: unknown service");
	}

	if (argc != 0)
		usage();

	/*
	 * We run chkprintcap right away to catch any errors and blat them
	 * to stderr while we still have it open, rather than sending them
	 * to syslog and leaving the user wondering why lpd started and
	 * then stopped.  There should probably be a command-line flag to
	 * ignore errors from chkprintcap.
	 */
	{
		pid_t pid;
		int status;
		pid = fork();
		if (pid < 0) {
			err(EX_OSERR, "cannot fork");
		} else if (pid == 0) {	/* child */
			execl(_PATH_CHKPRINTCAP, _PATH_CHKPRINTCAP, (char *)0);
			err(EX_OSERR, "cannot execute %s", _PATH_CHKPRINTCAP);
		}
		if (waitpid(pid, &status, 0) < 0) {
			err(EX_OSERR, "cannot wait");
		}
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			errx(EX_OSFILE, "%d errors in printcap file, exiting",
			     WEXITSTATUS(status));
	}

#ifndef DEBUG
	/*
	 * Set up standard environment by detaching from the parent.
	 */
	daemon(0, 0);
#endif

	openlog("lpd", LOG_PID, LOG_LPR);
	syslog(LOG_INFO, "lpd startup: logging=%d%s%s", lflag,
	    socket_debug ? " dbg" : "", sflag ? " net-secure" : "");
	(void) umask(0);
	/*
	 * NB: This depends on O_NONBLOCK semantics doing the right thing;
	 * i.e., applying only to the O_EXLOCK and not to the rest of the
	 * open/creation.  As of 1997-12-02, this is the case for commonly-
	 * used filesystems.  There are other places in this code which
	 * make the same assumption.
	 */
	lfd = open(_PATH_MASTERLOCK, O_WRONLY|O_CREAT|O_EXLOCK|O_NONBLOCK,
		   LOCK_FILE_MODE);
	if (lfd < 0) {
		if (errno == EWOULDBLOCK)	/* active daemon present */
			exit(0);
		syslog(LOG_ERR, "%s: %m", _PATH_MASTERLOCK);
		exit(1);
	}
	fcntl(lfd, F_SETFL, 0);	/* turn off non-blocking mode */
	ftruncate(lfd, 0);
	/*
	 * write process id for others to know
	 */
	sprintf(line, "%u\n", getpid());
	f = strlen(line);
	if (write(lfd, line, f) != f) {
		syslog(LOG_ERR, "%s: %m", _PATH_MASTERLOCK);
		exit(1);
	}
	signal(SIGCHLD, reapchild);
	/*
	 * Restart all the printers.
	 */
	startup();
	(void) unlink(_PATH_SOCKETNAME);
	funix = socket(AF_UNIX, SOCK_STREAM, 0);
	if (funix < 0) {
		syslog(LOG_ERR, "socket: %m");
		exit(1);
	}

	sigemptyset(&nmask);
	sigaddset(&nmask, SIGHUP);
	sigaddset(&nmask, SIGINT);
	sigaddset(&nmask, SIGQUIT);
	sigaddset(&nmask, SIGTERM);
	sigprocmask(SIG_BLOCK, &nmask, &omask);

	(void) umask(07);
	signal(SIGHUP, mcleanup);
	signal(SIGINT, mcleanup);
	signal(SIGQUIT, mcleanup);
	signal(SIGTERM, mcleanup);
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	strcpy(un.sun_path, _PATH_SOCKETNAME);
#ifndef SUN_LEN
#define SUN_LEN(unp) (strlen((unp)->sun_path) + 2)
#endif
	if (bind(funix, (struct sockaddr *)&un, SUN_LEN(&un)) < 0) {
		syslog(LOG_ERR, "ubind: %m");
		exit(1);
	}
	(void) umask(0);
	sigprocmask(SIG_SETMASK, &omask, (sigset_t *)0);
	FD_ZERO(&defreadfds);
	FD_SET(funix, &defreadfds);
	listen(funix, 5);
	if (sflag == 0) {
		finet = socksetup(family, socket_debug);
	} else
		finet = NULL;	/* pretend we couldn't open TCP socket. */
	if (finet) {
		for (i = 1; i <= *finet; i++) {
			FD_SET(finet[i], &defreadfds);
			listen(finet[i], 5);
		}
	}
	/*
	 * Main loop: accept, do a request, continue.
	 */
	memset(&frominet, 0, sizeof(frominet));
	memset(&fromunix, 0, sizeof(fromunix));
	if (lflag)
		syslog(LOG_INFO, "lpd startup: ready to accept requests");
	/*
	 * XXX - should be redone for multi-protocol
	 */
	for (;;) {
		int domain, nfds, s;
		fd_set readfds;

		FD_COPY(&defreadfds, &readfds);
		nfds = select(20, &readfds, 0, 0, 0);
		if (nfds <= 0) {
			if (nfds < 0 && errno != EINTR)
				syslog(LOG_WARNING, "select: %m");
			continue;
		}
		domain = -1;		    /* avoid compile-time warning */
		s = -1;			    /* avoid compile-time warning */
		if (FD_ISSET(funix, &readfds)) {
			domain = AF_UNIX, fromlen = sizeof(fromunix);
			s = accept(funix,
			    (struct sockaddr *)&fromunix, &fromlen);
 		} else {
                        for (i = 1; i <= *finet; i++) 
				if (FD_ISSET(finet[i], &readfds)) {
					domain = AF_INET;
					fromlen = sizeof(frominet);
					s = accept(finet[i],
					    (struct sockaddr *)&frominet,
					    &fromlen);
				}
		}
		if (s < 0) {
			if (errno != EINTR)
				syslog(LOG_WARNING, "accept: %m");
			continue;
		}
		if (fork() == 0) {
			/*
			 * Note that printjob() also plays around with
			 * signal-handling routines, and may need to be
			 * changed when making changes to signal-handling.
			 */
			signal(SIGCHLD, SIG_DFL);
			signal(SIGHUP, SIG_IGN);
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			signal(SIGTERM, SIG_IGN);
			(void) close(funix);
			if (sflag == 0 && finet) {
                        	for (i = 1; i <= *finet; i++) 
					(void)close(finet[i]);
			}
			dup2(s, STDOUT_FILENO);
			(void) close(s);
			if (domain == AF_INET) {
				/* for both AF_INET and AF_INET6 */
				from_remote = 1;
 				chkhost((struct sockaddr *)&frominet,
				    ch_options);
			} else
				from_remote = 0;
			doit();
			exit(0);
		}
		(void) close(s);
	}
}

static void
reapchild(int signo __unused)
{
	int status;

	while (wait3(&status, WNOHANG, 0) > 0)
		;
}

static void
mcleanup(int signo)
{
	/*
	 * XXX syslog(3) is not signal-safe.
	 */
	if (lflag) {
		if (signo)
			syslog(LOG_INFO, "exiting on signal %d", signo);
		else
			syslog(LOG_INFO, "exiting");
	}
	unlink(_PATH_SOCKETNAME);
	exit(0);
}

/*
 * Stuff for handling job specifications
 */
char	*user[MAXUSERS];	/* users to process */
int	users;			/* # of users in user array */
int	requ[MAXREQUESTS];	/* job number of spool entries */
int	requests;		/* # of spool requests */
char	*person;		/* name of person doing lprm */

		 /* buffer to hold the client's machine-name */
static char	 frombuf[MAXHOSTNAMELEN];
char	cbuf[BUFSIZ];		/* command line buffer */
const char	*cmdnames[] = {
	"null",
	"printjob",
	"recvjob",
	"displayq short",
	"displayq long",
	"rmjob"
};

static void
doit(void)
{
	char *cp, *printer;
	int n;
	int status;
	struct printer myprinter, *pp = &myprinter;

	init_printer(&myprinter);

	for (;;) {
		cp = cbuf;
		do {
			if (cp >= &cbuf[sizeof(cbuf) - 1])
				fatal(0, "Command line too long");
			if ((n = read(STDOUT_FILENO, cp, 1)) != 1) {
				if (n < 0)
					fatal(0, "Lost connection");
				return;
			}
		} while (*cp++ != '\n');
		*--cp = '\0';
		cp = cbuf;
		if (lflag) {
			if (*cp >= '\1' && *cp <= '\5')
				syslog(LOG_INFO, "%s requests %s %s",
					from_host, cmdnames[(u_char)*cp], cp+1);
			else
				syslog(LOG_INFO, "bad request (%d) from %s",
					*cp, from_host);
		}
		switch (*cp++) {
		case CMD_CHECK_QUE: /* check the queue, print any jobs there */
			startprinting(cp);
			break;
		case CMD_TAKE_THIS: /* receive files to be queued */
			if (!from_remote) {
				syslog(LOG_INFO, "illegal request (%d)", *cp);
				exit(1);
			}
			recvjob(cp);
			break;
		case CMD_SHOWQ_SHORT: /* display the queue (short form) */
		case CMD_SHOWQ_LONG: /* display the queue (long form) */
			/* XXX - this all needs to be redone. */
			printer = cp;
			while (*cp) {
				if (*cp != ' ') {
					cp++;
					continue;
				}
				*cp++ = '\0';
				while (isspace(*cp))
					cp++;
				if (*cp == '\0')
					break;
				if (isdigit(*cp)) {
					if (requests >= MAXREQUESTS)
						fatal(0, "Too many requests");
					requ[requests++] = atoi(cp);
				} else {
					if (users >= MAXUSERS)
						fatal(0, "Too many users");
					user[users++] = cp;
				}
			}
			status = getprintcap(printer, pp);
			if (status < 0)
				fatal(pp, "%s", pcaperr(status));
			displayq(pp, cbuf[0] == CMD_SHOWQ_LONG);
			exit(0);
		case CMD_RMJOB:	/* remove a job from the queue */
			if (!from_remote) {
				syslog(LOG_INFO, "illegal request (%d)", *cp);
				exit(1);
			}
			printer = cp;
			while (*cp && *cp != ' ')
				cp++;
			if (!*cp)
				break;
			*cp++ = '\0';
			person = cp;
			while (*cp) {
				if (*cp != ' ') {
					cp++;
					continue;
				}
				*cp++ = '\0';
				while (isspace(*cp))
					cp++;
				if (*cp == '\0')
					break;
				if (isdigit(*cp)) {
					if (requests >= MAXREQUESTS)
						fatal(0, "Too many requests");
					requ[requests++] = atoi(cp);
				} else {
					if (users >= MAXUSERS)
						fatal(0, "Too many users");
					user[users++] = cp;
				}
			}
			rmjob(printer);
			break;
		}
		fatal(0, "Illegal service request");
	}
}

/*
 * Make a pass through the printcap database and start printing any
 * files left from the last time the machine went down.
 */
static void
startup(void)
{
	int pid, status, more;
	struct printer myprinter, *pp = &myprinter;

	more = firstprinter(pp, &status);
	if (status)
		goto errloop;
	while (more) {
		if (ckqueue(pp) <= 0) {
			goto next;
		}
		if (lflag)
			syslog(LOG_INFO, "lpd startup: work for %s",
			    pp->printer);
		if ((pid = fork()) < 0) {
			syslog(LOG_WARNING, "lpd startup: cannot fork for %s",
			    pp->printer);
			mcleanup(0);
		}
		if (pid == 0) {
			lastprinter();
			printjob(pp);
			/* NOTREACHED */
		}
		do {
next:
			more = nextprinter(pp, &status);
errloop:
			if (status)
				syslog(LOG_WARNING, 
				    "lpd startup: printcap entry for %s has errors, skipping",
				    pp->printer ? pp->printer : "<noname?>");
		} while (more && status);
	}
}

/*
 * Make sure there's some work to do before forking off a child
 */
static int
ckqueue(struct printer *pp)
{
	register struct dirent *d;
	DIR *dirp;
	char *spooldir;

	spooldir = pp->spool_dir;
	if ((dirp = opendir(spooldir)) == NULL)
		return (-1);
	while ((d = readdir(dirp)) != NULL) {
		if (d->d_name[0] != 'c' || d->d_name[1] != 'f')
			continue;	/* daemon control files only */
		closedir(dirp);
		return (1);		/* found something */
	}
	closedir(dirp);
	return (0);
}

#define DUMMY ":nobody::"

/*
 * Check to see if the host connecting to this host has access to any
 * lpd services on this host.
 */
static void
chkhost(struct sockaddr *f, int ch_opts)
{
	struct addrinfo hints, *res, *r;
	register FILE *hostf;
	char hostbuf[NI_MAXHOST], ip[NI_MAXHOST];
	char serv[NI_MAXSERV];
	char *syserr, *usererr;
	int error, errsav, fpass, good;

	from_host = ".na.";

	/* Need real hostname for temporary filenames */
	error = getnameinfo(f, f->sa_len, hostbuf, sizeof(hostbuf), NULL, 0,
	    NI_NAMEREQD);
	if (error) {
		errsav = error;
		error = getnameinfo(f, f->sa_len, hostbuf, sizeof(hostbuf),
		    NULL, 0, NI_NUMERICHOST);
		if (error) {
			asprintf(&syserr,
			    "can not determine hostname for remote host (%d,%d)",
			    errsav, error);
			asprintf(&usererr,
			    "Host name for your address is not known");
			fhosterr(ch_opts, syserr, usererr);
			/* NOTREACHED */
		}
		asprintf(&syserr,
		    "Host name for remote host (%s) not known (%d)",
		    hostbuf, errsav);
		asprintf(&usererr,
		    "Host name for your address (%s) is not known",
		    hostbuf);
		fhosterr(ch_opts, syserr, usererr);
		/* NOTREACHED */
	}

	strlcpy(frombuf, hostbuf, sizeof(frombuf));
	from_host = frombuf;
	ch_opts |= LPD_ADDFROMLINE;

	/* Need address in stringform for comparison (no DNS lookup here) */
	error = getnameinfo(f, f->sa_len, hostbuf, sizeof(hostbuf), NULL, 0,
	    NI_NUMERICHOST);
	if (error) {
		asprintf(&syserr, "Cannot print IP address (error %d)",
		    error);
		asprintf(&usererr, "Cannot print IP address for your host");
		fhosterr(ch_opts, syserr, usererr);
		/* NOTREACHED */
	}
	from_ip = strdup(hostbuf);

	/* Reject numeric addresses */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
	if (getaddrinfo(from_host, NULL, &hints, &res) == 0) {
		freeaddrinfo(res);
		/* This syslog message already includes from_host */
		ch_opts &= ~LPD_ADDFROMLINE;
		asprintf(&syserr, "reverse lookup results in non-FQDN %s",
		    from_host);
		/* same message to both syslog and remote user */
		fhosterr(ch_opts, syserr, syserr);
		/* NOTREACHED */
	}

	/* Check for spoof, ala rlogind */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	error = getaddrinfo(from_host, NULL, &hints, &res);
	if (error) {
		asprintf(&syserr, "dns lookup for address %s failed: %s",
		    from_ip, gai_strerror(error));
		asprintf(&usererr, "hostname for your address (%s) unknown: %s",
		    from_ip, gai_strerror(error));
		fhosterr(ch_opts, syserr, usererr);
		/* NOTREACHED */
	}
	good = 0;
	for (r = res; good == 0 && r; r = r->ai_next) {
		error = getnameinfo(r->ai_addr, r->ai_addrlen, ip, sizeof(ip),
		    NULL, 0, NI_NUMERICHOST);
		if (!error && !strcmp(from_ip, ip))
			good = 1;
	}
	if (res)
		freeaddrinfo(res);
	if (good == 0) {
		asprintf(&syserr, "address for remote host (%s) not matched",
		    from_ip);
		asprintf(&usererr,
		    "address for your hostname (%s) not matched", from_ip);
		fhosterr(ch_opts, syserr, usererr);
		/* NOTREACHED */
	}

	fpass = 1;
	hostf = fopen(_PATH_HOSTSEQUIV, "r");
again:
	if (hostf) {
		if (__ivaliduser_sa(hostf, f, f->sa_len, DUMMY, DUMMY) == 0) {
			(void) fclose(hostf);
			goto foundhost;
		}
		(void) fclose(hostf);
	}
	if (fpass == 1) {
		fpass = 2;
		hostf = fopen(_PATH_HOSTSLPD, "r");
		goto again;
	}
	/* This syslog message already includes from_host */
	ch_opts &= ~LPD_ADDFROMLINE;
	asprintf(&syserr, "refused connection from %s, sip=%s", from_host,
	    from_ip);
	asprintf(&usererr,
	    "Print-services are not available to your host (%s).", from_host);
	fhosterr(ch_opts, syserr, usererr);
	/* NOTREACHED */

foundhost:
	if (ch_opts & LPD_NOPORTCHK)
		return;			/* skip the reserved-port check */

	error = getnameinfo(f, f->sa_len, NULL, 0, serv, sizeof(serv),
	    NI_NUMERICSERV);
	if (error) {
		/* same message to both syslog and remote user */
		asprintf(&syserr, "malformed from-address (%d)", error);
		fhosterr(ch_opts, syserr, syserr);
		/* NOTREACHED */
	}

	if (atoi(serv) >= IPPORT_RESERVED) {
		/* same message to both syslog and remote user */
		asprintf(&syserr, "connected from invalid port (%s)", serv);
		fhosterr(ch_opts, syserr, syserr);
		/* NOTREACHED */
	}
}

/*
 * Handle fatal errors in chkhost.  The first message will optionally be
 * sent to syslog, the second one is sent to the connecting host.
 *
 * The idea is that the syslog message is meant for an administrator of a
 * print server (the host receiving connections), while the usermsg is meant
 * for a remote user who may or may not be clueful, and may or may not be
 * doing something nefarious.  Some remote users (eg, MS-Windows...) may not
 * even see whatever message is sent, which is why there's the option to
 * start 'lpd' with the connection-errors also sent to syslog.
 *
 * Given that hostnames can theoretically be fairly long (well, over 250
 * bytes), it would probably be helpful to have the 'from_host' field at
 * the end of any error messages which include that info.
 *
 * These are Fatal host-connection errors, so this routine does not return.
 */
static void
fhosterr(int ch_opts, char *sysmsg, char *usermsg)
{

	/*
	 * If lpd was started up to print connection errors, then write
	 * the syslog message before the user message.
	 * And for many of the syslog messages, it is helpful to first
	 * write the from_host (if it is known) as a separate syslog
	 * message, since the hostname may be so long.
	 */
	if (ch_opts & LPD_LOGCONNERR) {
		if (ch_opts & LPD_ADDFROMLINE) {
		    syslog(LOG_WARNING, "for connection from %s:", from_host);
		}
		syslog(LOG_WARNING, "%s", sysmsg);
	}

	/*
	 * Now send the error message to the remote host which is trying
	 * to make the connection.
	 */
	printf("%s [@%s]: %s\n", progname, local_host, usermsg);
	fflush(stdout);

	/* 
	 * Add a minimal delay before exiting (and disconnecting from the
	 * sending-host).  This is just in case that machine responds by
	 * INSTANTLY retrying (and instantly re-failing...).  This may also
	 * give the other side more time to read the error message.
	 */
	sleep(2);			/* a paranoid throttling measure */
	exit(1);
}

/* setup server socket for specified address family */
/* if af is PF_UNSPEC more than one socket may be returned */
/* the returned list is dynamically allocated, so caller needs to free it */
static int *
socksetup(int af, int debuglvl)
{
	struct addrinfo hints, *res, *r;
	int error, maxs, *s, *socks;
	const int on = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = af;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(NULL, "printer", &hints, &res);
	if (error) {
		syslog(LOG_ERR, "%s", gai_strerror(error));
		mcleanup(0);
	}

	/* Count max number of sockets we may open */
	for (maxs = 0, r = res; r; r = r->ai_next, maxs++)
		;
	socks = malloc((maxs + 1) * sizeof(int));
	if (!socks) {
		syslog(LOG_ERR, "couldn't allocate memory for sockets");
		mcleanup(0);
	}

	*socks = 0;   /* num of sockets counter at start of array */
	s = socks + 1;
	for (r = res; r; r = r->ai_next) {
		*s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (*s < 0) {
			syslog(LOG_DEBUG, "socket(): %m");
			continue;
		}
		if (setsockopt(*s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))
		    < 0) {
			syslog(LOG_ERR, "setsockopt(SO_REUSEADDR): %m");
			close(*s);
			continue;
		}
		if (debuglvl)
			if (setsockopt(*s, SOL_SOCKET, SO_DEBUG, &debuglvl,
			    sizeof(debuglvl)) < 0) {
				syslog(LOG_ERR, "setsockopt (SO_DEBUG): %m");
				close(*s);
				continue;
			}
		if (r->ai_family == AF_INET6) {
			if (setsockopt(*s, IPPROTO_IPV6, IPV6_V6ONLY,
				       &on, sizeof(on)) < 0) {
				syslog(LOG_ERR,
				       "setsockopt (IPV6_V6ONLY): %m");
				close(*s);
				continue;
			}
		}
		if (bind(*s, r->ai_addr, r->ai_addrlen) < 0) {
			syslog(LOG_DEBUG, "bind(): %m");
			close(*s);
			continue;
		}
		(*socks)++;
		s++;
	}

	if (res)
		freeaddrinfo(res);

	if (*socks == 0) {
		syslog(LOG_ERR, "Couldn't bind to any socket");
		free(socks);
		mcleanup(0);
	}
	return(socks);
}

static void
usage(void)
{
#ifdef INET6
	fprintf(stderr, "usage: lpd [-cdlsW46] [port#]\n");
#else
	fprintf(stderr, "usage: lpd [-cdlsW] [port#]\n");
#endif
	exit(EX_USAGE);
}
