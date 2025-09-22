/*	$OpenBSD: lpd.c,v 1.66 2022/12/28 21:30:17 jmc Exp $	*/
/*	$NetBSD: lpd.c,v 1.33 2002/01/21 14:42:29 wiz Exp $	*/

/*
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
 *	1. Spooling area is writable only by root and the group daemon.
 *	2. Files in spooling area are owned by user daemon, group daemon,
 *	   and are mode 660.
 *	3. lpd runs as root but spends most of its time with its effective
 *	   uid and gid set to the uid/gid specified in the passwd entry for
 *	   DEFUID (1, aka daemon).
 *	4. lpr and lprm run setuid daemon and setgrp daemon.  lpr opens
 *	   files to be printed with its real uid/gid and writes to
 *	   the spool dir with its effective uid/gid (i.e. daemon).
 *	   lprm need to run as user daemon so it can kill lpd.
 *	5. lpc and lpq run setgrp daemon.
 *
 * Users can't touch the spool w/o the help of one of the lp* programs.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"
#include "extern.h"

int	lflag;				/* log requests flag */
int	rflag;				/* allow 'of' for remote printers */
int	sflag;				/* secure (no inet) flag */
int	from_remote;			/* from remote socket */
char	**blist;			/* list of addresses to bind(2) to */
int	blist_size;
int	blist_addrs;

volatile sig_atomic_t child_count;	/* number of kids forked */

static void		reapchild(int);
static void		mcleanup(int);
static void		doit(void);
static void		startup(void);
static void		chkhost(struct sockaddr *);
static __dead void	usage(void);
static int		*socksetup(int, int, const char *);

/* unused, needed for lpc */
volatile sig_atomic_t gotintr;

int
main(int argc, char **argv)
{
	fd_set defreadfds;
	struct passwd *pw;
	struct sockaddr_un un, fromunix;
	struct sockaddr_storage frominet;
	sigset_t mask, omask;
	int i, funix, *finet;
	int options, maxfd;
	long l;
	long child_max = 32;	/* more than enough to hose the system */
	struct servent *sp;
	const char *port = "printer";
	char *cp;

	if (geteuid() != 0)
		errx(1, "must run as root");

	/*
	 * We want to run with euid of daemon most of the time.
	 */
	if ((pw = getpwuid(DEFUID)) == NULL)
		errx(1, "daemon uid (%u) not in password file", DEFUID);
	real_uid = pw->pw_uid;
	real_gid = pw->pw_gid;
	effective_uid = 0;
	effective_gid = getegid();
	PRIV_END;	/* run as daemon for most things */

	options = 0;
	gethostname(host, sizeof(host));

	while ((i = getopt(argc, argv, "b:dln:rsw:W")) != -1) {
		switch (i) {
		case 'b':
			if (blist_addrs >= blist_size) {
				char **newblist;
				int newblist_size = blist_size +
				    sizeof(char *) * 4;
				newblist = realloc(blist, newblist_size);
				if (newblist == NULL) {
					free(blist);
					blist_size = 0;
					blist = NULL;
				}
				blist = newblist;
				blist_size = newblist_size;
				if (blist == NULL)
					err(1, "cant allocate bind addr list");
			}
			blist[blist_addrs] = strdup(optarg);
			if (blist[blist_addrs++] == NULL)
				err(1, NULL);
			break;
		case 'd':
			options |= SO_DEBUG;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'n':
			child_max = strtol(optarg, &cp, 10);
			if (*cp != '\0' || child_max < 0 || child_max > 1024)
				errx(1, "invalid number of children: %s",
				    optarg);
			break;
		case 'r':
			rflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'w':
			l = strtol(optarg, &cp, 10);
			if (*cp != '\0' || l < 0 || l >= INT_MAX)
				errx(1, "wait time must be positive integer: %s",
				    optarg);
			wait_time = (u_int)l;
			if (wait_time < 30)
				warnx("warning: wait time less than 30 seconds");
			break;
		case 'W':	/* XXX deprecate */
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	switch (argc) {
	case 1:
		port = argv[0];
		l = strtol(port, &cp, 10);
		if (*cp != '\0' || l <= 0 || l > USHRT_MAX)
			errx(1, "port # %s is invalid", port);
		break;
	case 0:
		sp = getservbyname(port, "tcp");
		if (sp == NULL)
			errx(1, "%s/tcp: unknown service", port);
		break;
	default:
		usage();
	}

	funix = socket(AF_UNIX, SOCK_STREAM, 0);
	if (funix < 0)
		err(1, "socket");
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	strlcpy(un.sun_path, _PATH_SOCKETNAME, sizeof(un.sun_path));
	PRIV_START;
	if (connect(funix, (struct sockaddr *)&un, sizeof(un)) == 0)
		errx(1, "already running");
	if (errno != ENOENT)
		(void)unlink(un.sun_path);
	if (bind(funix, (struct sockaddr *)&un, sizeof(un)) < 0)
		err(1, "bind %s", un.sun_path);
	chmod(_PATH_SOCKETNAME, 0660);
	chown(_PATH_SOCKETNAME, -1, real_gid);
	PRIV_END;

#ifndef DEBUG
	/*
	 * Set up standard environment by detaching from the parent.
	 */
	daemon(0, 0);
#endif

	openlog("lpd", LOG_PID, LOG_LPR);
	syslog(LOG_INFO, "restarted");
	(void)umask(0);
	signal(SIGCHLD, reapchild);
	/*
	 * Restart all the printers.
	 */
	startup();

	sigemptyset(&mask);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, &omask);

	signal(SIGHUP, mcleanup);
	signal(SIGINT, mcleanup);
	signal(SIGQUIT, mcleanup);
	signal(SIGTERM, mcleanup);
	sigprocmask(SIG_SETMASK, &omask, NULL);
	FD_ZERO(&defreadfds);
	FD_SET(funix, &defreadfds);
	listen(funix, 5);
	if (!sflag || blist_addrs)
		finet = socksetup(PF_UNSPEC, options, port);
	else
		finet = NULL;	/* pretend we couldn't open TCP socket. */

	if (blist != NULL) {
		for (i = 0; i < blist_addrs; i++)
			free(blist[i]);
		free(blist);
	}

	maxfd = funix;
	if (finet) {
		for (i = 1; i <= *finet; i++) {
			FD_SET(finet[i], &defreadfds);
			listen(finet[i], 5);
			if (finet[i] > maxfd)
				maxfd = finet[i];
		}
	}
	/*
	 * Main loop: accept, do a request, continue.
	 */
	memset(&frominet, 0, sizeof(frominet));
	memset(&fromunix, 0, sizeof(fromunix));
	for (;;) {
		int domain, nfds, s;
		socklen_t fromlen;
		fd_set readfds;
		short sleeptime = 10;	/* overflows in about 2 hours */

		while (child_max < child_count) {
			syslog(LOG_WARNING,
			    "too many children, sleeping for %d seconds",
			    sleeptime);
			sleep(sleeptime);
			sleeptime <<= 1;
			if (sleeptime < 0) {
				syslog(LOG_CRIT, "sleeptime overflowed! help!");
				sleeptime = 10;
			}
		}

		FD_COPY(&defreadfds, &readfds);
		nfds = select(maxfd + 1, &readfds, NULL, NULL, NULL);
		if (nfds <= 0) {
			if (nfds < 0 && errno != EINTR)
				syslog(LOG_WARNING, "select: %m");
			continue;
		}
		if (FD_ISSET(funix, &readfds)) {
			domain = AF_UNIX;
			fromlen = sizeof(fromunix);
			s = accept(funix,
			    (struct sockaddr *)&fromunix, &fromlen);
		} else {
			domain = AF_INET;
			s = -1;
			for (i = 1; i <= *finet; i++)
				if (FD_ISSET(finet[i], &readfds)) {
					in_port_t port;

					fromlen = sizeof(frominet);
					s = accept(finet[i],
					    (struct sockaddr *)&frominet,
					    &fromlen);
					switch (frominet.ss_family) {
					case AF_INET:
						port = ((struct sockaddr_in *)
						    &frominet)->sin_port;
						break;
					case AF_INET6:
						port = ((struct sockaddr_in6 *)
						    &frominet)->sin6_port;
						break;
					default:
						port = 0;
					}
					/* check for ftp bounce attack */
					if (port == htons(20)) {
						close(s);
						continue;
					}
				}
		}
		if (s < 0) {
			if (errno != EINTR && errno != EWOULDBLOCK &&
			    errno != ECONNABORTED)
				syslog(LOG_WARNING, "accept: %m");
			continue;
		}

		switch (fork()) {
		case 0:
			signal(SIGCHLD, SIG_DFL);
			signal(SIGHUP, SIG_IGN);
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			signal(SIGTERM, SIG_IGN);
			(void)close(funix);
			if (!sflag && finet)
				for (i = 1; i <= *finet; i++)
					(void)close(finet[i]);
			if (s != STDOUT_FILENO) {
				dup2(s, STDOUT_FILENO);
				(void)close(s);
			}
			if (domain == AF_INET) {
				/* for both AF_INET and AF_INET6 */
				from_remote = 1;
				chkhost((struct sockaddr *)&frominet);
			} else
				from_remote = 0;
			doit();
			exit(0);
		case -1:
			syslog(LOG_WARNING, "fork: %m, sleeping for 10 seconds...");
			sleep(10);
			continue;
		default:
			child_count++;
		}
		(void)close(s);
	}
}

static void
reapchild(int signo)
{
	int save_errno = errno;
	int status;

	while (waitpid((pid_t)-1, &status, WNOHANG) > 0)
		child_count--;
	errno = save_errno;
}

static void
mcleanup(int signo)
{
	struct syslog_data sdata = SYSLOG_DATA_INIT;

	if (lflag)
		syslog_r(LOG_INFO, &sdata, "exiting");
	PRIV_START;
	unlink(_PATH_SOCKETNAME);
	_exit(0);
}

/*
 * Stuff for handling job specifications
 */
char	*user[MAXUSERS];	/* users to process */
int	users;			/* # of users in user array */
int	requ[MAXREQUESTS];	/* job number of spool entries */
int	requests;		/* # of spool requests */
char	*person;		/* name of person doing lprm */

char	fromb[NI_MAXHOST];	/* buffer for client's machine name */
char	cbuf[BUFSIZ];		/* command line buffer */
char	*cmdnames[] = {
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
	char *cp;
	int n;

	for (;;) {
		cp = cbuf;
		do {
			if (cp >= &cbuf[sizeof(cbuf) - 1])
				fatal("Command line too long");
			if ((n = read(STDOUT_FILENO, cp, 1)) != 1) {
				if (n < 0)
					fatal("Lost connection");
				return;
			}
		} while (*cp++ != '\n');
		*--cp = '\0';
		cp = cbuf;
		if (lflag) {
			if (*cp >= '\1' && *cp <= '\5') {
				syslog(LOG_INFO, "%s requests %s %s",
					from, cmdnames[(int)*cp], cp+1);
				setproctitle("serving %s: %s %s", from,
				    cmdnames[(int)*cp], cp+1);
			} else
				syslog(LOG_INFO, "bad request (%d) from %s",
					*cp, from);
		}
		switch (*cp++) {
		case '\1':	/* check the queue and print any jobs there */
			printer = cp;
			if (*printer == '\0')
				printer = DEFLP;
			printjob();
			break;
		case '\2':	/* receive files to be queued */
			if (!from_remote) {
				syslog(LOG_INFO, "illegal request (%d)", *cp);
				exit(1);
			}
			printer = cp;
			if (*printer == '\0')
				printer = DEFLP;
			recvjob();
			break;
		case '\3':	/* display the queue (short form) */
		case '\4':	/* display the queue (long form) */
			printer = cp;
			if (*printer == '\0')
				printer = DEFLP;
			while (*cp) {
				if (*cp != ' ') {
					cp++;
					continue;
				}
				*cp++ = '\0';
				while (isspace((unsigned char)*cp))
					cp++;
				if (*cp == '\0')
					break;
				if (isdigit((unsigned char)*cp)) {
					if (requests >= MAXREQUESTS)
						fatal("Too many requests");
					requ[requests++] = atoi(cp);
				} else {
					if (users >= MAXUSERS)
						fatal("Too many users");
					user[users++] = cp;
				}
			}
			displayq(cbuf[0] - '\3');
			exit(0);
		case '\5':	/* remove a job from the queue */
			if (!from_remote) {
				syslog(LOG_INFO, "illegal request (%d)", *cp);
				exit(1);
			}
			printer = cp;
			if (*printer == '\0')
				printer = DEFLP;
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
				while (isspace((unsigned char)*cp))
					cp++;
				if (*cp == '\0')
					break;
				if (isdigit((unsigned char)*cp)) {
					if (requests >= MAXREQUESTS)
						fatal("Too many requests");
					requ[requests++] = atoi(cp);
				} else {
					if (users >= MAXUSERS)
						fatal("Too many users");
					user[users++] = cp;
				}
			}
			rmjob();
			break;
		}
		fatal("Illegal service request");
	}
}

/*
 * Make a pass through the printcap database and start printing any
 * files left from the last time the machine went down.
 */
static void
startup(void)
{
	char *buf, *cp;

	/*
	 * Restart the daemons.
	 */
	while (cgetnext(&buf, printcapdb) > 0) {
		if (ckqueue(buf) <= 0) {
			free(buf);
			continue;	/* no work to do for this printer */
		}
		for (cp = buf; *cp; cp++)
			if (*cp == '|' || *cp == ':') {
				*cp = '\0';
				break;
			}
		if (lflag)
			syslog(LOG_INFO, "work for %s", buf);
		switch (fork()) {
		case -1:
			syslog(LOG_WARNING, "startup: cannot fork");
			mcleanup(0);
			/* NOTREACHED */
		case 0:
			printer = buf;
			setproctitle("working on printer %s", printer);
			cgetclose();
			printjob();
			/* NOTREACHED */
		default:
			child_count++;
			free(buf);
		}
	}
}

/*
 * Check to see if the from host has access to the line printer.
 */
static void
chkhost(struct sockaddr *f)
{
	struct addrinfo hints, *res, *r;
	FILE *hostf;
	int good = 0;
	char host[NI_MAXHOST], ip[NI_MAXHOST];
	char serv[NI_MAXSERV];
	int error;

	error = getnameinfo(f, f->sa_len, NULL, 0, serv, sizeof(serv),
	    NI_NUMERICSERV);
	if (error)
		fatal("Malformed from address");

	/* Need real hostname for temporary filenames */
	error = getnameinfo(f, f->sa_len, host, sizeof(host), NULL, 0,
	    NI_NAMEREQD);
	if (error) {
		error = getnameinfo(f, f->sa_len, host, sizeof(host), NULL, 0,
		    NI_NUMERICHOST);
		if (error)
			fatal("Host name for your address unknown");
		else
			fatal("Host name for your address (%s) unknown", host);
	}

	(void)strlcpy(fromb, host, sizeof(fromb));
	from = fromb;

	/* need address in stringform for comparison (no DNS lookup here) */
	error = getnameinfo(f, f->sa_len, host, sizeof(host), NULL, 0,
	    NI_NUMERICHOST);
	if (error)
		fatal("Cannot print address");

	/* Check for spoof, ala rlogind */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	error = getaddrinfo(fromb, NULL, &hints, &res);
	if (error) {
		fatal("hostname for your address (%s) unknown: %s", host,
		    gai_strerror(error));
	}
	for (good = 0, r = res; good == 0 && r; r = r->ai_next) {
		error = getnameinfo(r->ai_addr, r->ai_addrlen, ip, sizeof(ip),
		    NULL, 0, NI_NUMERICHOST);
		if (!error && !strcmp(host, ip))
			good = 1;
	}
	if (res)
		freeaddrinfo(res);
	if (good == 0)
		fatal("address for your hostname (%s) not matched", host);
	setproctitle("serving %s", from);
	PRIV_START;
	hostf = fopen(_PATH_HOSTSLPD, "r");
	PRIV_END;
	if (hostf) {
		if (allowedhost(hostf, f, f->sa_len) == 0) {
			(void)fclose(hostf);
			return;
		}
		(void)fclose(hostf);
		fatal("Your host does not have line printer access (/etc/hosts.lpd)");
	} else
		fatal("Your host does not have line printer access (no /etc/hosts.lpd)");
}

static __dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dlrs] [-b bind-address] [-n maxchild] "
	    "[-w maxwait] [port]\n", __progname);
	exit(1);
}

/*
 * Setup server socket for specified address family.
 * If af is PF_UNSPEC more than one socket may be returned.
 * The returned list is dynamically allocated, so the caller needs to free it.
 */
int *
socksetup(int af, int options, const char *port)
{
	struct addrinfo hints, *res, *r;
	int error, maxs = 0, *s, *socks = NULL, *newsocks, blidx = 0;
	const int on = 1;

	do {
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_PASSIVE;
		hints.ai_family = af;
		hints.ai_socktype = SOCK_STREAM;
		error = getaddrinfo((blist_addrs == 0) ? NULL : blist[blidx],
		    port ? port : "printer", &hints, &res);
		if (error) {
			if (blist_addrs)
				syslog(LOG_ERR, "%s: %s", blist[blidx],
				    gai_strerror(error));
			else
				syslog(LOG_ERR, "%s", gai_strerror(error));
			mcleanup(0);
		}

		/* Count max number of sockets we may open */
		for (r = res; r; r = r->ai_next, maxs++)
			;
		if (socks == NULL) {
			socks = calloc(maxs + 1, sizeof(int));
			if (socks)
				*socks = 0; /* num of sockets ctr at start */
		} else {
			newsocks = reallocarray(socks, maxs + 1, sizeof(int));
			if (newsocks)
				socks = newsocks;
			else {
				free(socks);
				socks = NULL;
			}
		}
		if (!socks) {
			syslog(LOG_ERR, "couldn't allocate memory for sockets");
			mcleanup(0);
		}

		s = socks + *socks + 1;
		for (r = res; r; r = r->ai_next) {
			*s = socket(r->ai_family, r->ai_socktype,
			            r->ai_protocol);
			if (*s < 0) {
				syslog(LOG_DEBUG, "socket(): %m");
				continue;
			}
			if (options & SO_DEBUG)
				if (setsockopt(*s, SOL_SOCKET, SO_DEBUG,
					       &on, sizeof(on)) < 0) {
					syslog(LOG_ERR,
					       "setsockopt (SO_DEBUG): %m");
					close (*s);
					continue;
				}
			PRIV_START;
			error = bind(*s, r->ai_addr, r->ai_addrlen);
			PRIV_END;
			if (error < 0) {
				syslog(LOG_DEBUG, "bind(): %m");
				close (*s);
				continue;
			}
			*socks = *socks + 1;
			s++;
		}

		if (res)
			freeaddrinfo(res);
	} while (++blidx < blist_addrs);

	if (socks == NULL || *socks == 0) {
		syslog(LOG_ERR, "Couldn't bind to any socket");
		free(socks);
		mcleanup(0);
	}
	return(socks);
}
