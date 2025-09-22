/*	$OpenBSD: common.c,v 1.42 2021/01/19 09:04:13 claudio Exp $	*/
/*	$NetBSD: common.c,v 1.21 2000/08/09 14:28:50 itojun Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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

#include <sys/stat.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <ifaddrs.h>
#include "lp.h"
#include "pathnames.h"

/*
 * Routines and data common to all the line printer functions.
 */

char	*AF;		/* accounting file */
long	 BR;		/* baud rate if lp is a tty */
char	*CF;		/* name of cifplot filter (per job) */
char	*DF;		/* name of tex filter (per job) */
long	 DU;		/* daemon user-id */
char	*FF;		/* form feed string */
char	*GF;		/* name of graph(1G) filter (per job) */
long	 HL;		/* print header last */
char	*IF;		/* name of input filter (created per job) */
char	*LF;		/* log file for error messages */
char	*LO;		/* lock file name */
char	*LP;		/* line printer device name */
long	 MC;		/* maximum number of copies allowed */
char	*MS;		/* stty flags to set if lp is a tty */
long	 MX;		/* maximum number of blocks to copy */
char	*NF;		/* name of ditroff filter (per job) */
char	*OF;		/* name of output filter (created once) */
long	 PL;		/* page length */
long	 PW;		/* page width */
long	 PX;		/* page width in pixels */
long	 PY;		/* page length in pixels */
char	*RF;		/* name of fortran text filter (per job) */
char    *RG;		/* restricted group */
char	*RM;		/* remote machine name */
char	*RP;		/* remote printer name */
long	 RS;		/* restricted to those with local accounts */
long	 RW;		/* open LP for reading and writing */
long	 SB;		/* short banner instead of normal header */
long	 SC;		/* suppress multiple copies */
char	*SD;		/* spool directory */
long	 SF;		/* suppress FF on each print job */
long	 SH;		/* suppress header page */
char	*ST;		/* status file name */
char	*TF;		/* name of troff filter (per job) */
char	*TR;		/* trailer string to be output when Q empties */
char	*VF;		/* name of vplot filter (per job) */

char	line[BUFSIZ];
int	remote;		/* true if sending files to a remote host */

static int compar(const void *, const void *);

/*
 * Create a TCP connection to host "rhost" at port "rport".
 * If rport == 0, then use the printer service port.
 * Most of this code comes from rcmd.c.
 */
int
getport(char *rhost, int rport)
{
	struct addrinfo hints, *res, *r;
	u_int timo = 1;
	int s, lport = IPPORT_RESERVED - 1;
	int error;
	int refuse, trial;
	char pbuf[NI_MAXSERV];

	/*
	 * Get the host address and port number to connect to.
	 */
	if (rhost == NULL)
		fatal("no remote host to connect to");
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (rport)
		snprintf(pbuf, sizeof(pbuf), "%d", rport);
	else
		snprintf(pbuf, sizeof(pbuf), "printer");
	siginterrupt(SIGINT, 1);
	error = getaddrinfo(rhost, pbuf, &hints, &res);
	siginterrupt(SIGINT, 0);
	if (error)
		fatal("printer/tcp: %s", gai_strerror(error));

	/*
	 * Try connecting to the server.
	 */
retry:
	s = -1;
	refuse = trial = 0;
	for (r = res; r; r = r->ai_next) {
		trial++;
retryport:
		PRIV_START;
		s = rresvport_af(&lport, r->ai_family);
		PRIV_END;
		if (s < 0) {
			/* fall back to non-privileged port */
			if (errno != EACCES ||
			    (s = socket(r->ai_family, SOCK_STREAM, 0)) < 0) {
				freeaddrinfo(res);
				return(-1);
			}
		}
		siginterrupt(SIGINT, 1);
		if (connect(s, r->ai_addr, r->ai_addrlen) < 0) {
			error = errno;
			siginterrupt(SIGINT, 0);
			(void)close(s);
			s = -1;
			errno = error;
			if (errno == EADDRINUSE) {
				lport--;
				goto retryport;
			} else if (errno == ECONNREFUSED)
				refuse++;
			continue;
		} else {
			siginterrupt(SIGINT, 0);
			break;
		}
	}
	if (s < 0 && trial == refuse && timo <= 16) {
		sleep(timo);
		timo *= 2;
		goto retry;
	}
	if (res)
		freeaddrinfo(res);

	/* Don't worry if we get an error from setsockopt(). */
	trial = 1;
	setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &trial, sizeof(trial));

	return(s);
}

/*
 * Getline reads a line from the control file cfp, removes tabs, converts
 *  new-line to null and leaves it in line.
 * Returns 0 at EOF or the number of characters read.
 */
int
get_line(FILE *cfp)
{
	int linel = 0;
	char *lp = line;
	int c;

	while ((c = getc(cfp)) != '\n' && linel+1<sizeof(line)) {
		if (c == EOF)
			return(0);
		if (c == '\t') {
			do {
				*lp++ = ' ';
				linel++;
			} while ((linel & 07) != 0 && linel+1 < sizeof(line));
			continue;
		}
		*lp++ = c;
		linel++;
	}
	*lp++ = '\0';
	return(linel);
}

/*
 * Scan the current directory and make a list of daemon files sorted by
 * creation time.
 * Return the number of entries and a pointer to the list.
 */
int
getq(struct queue ***namelist)
{
	struct dirent *d;
	struct queue *q, **queue = NULL;
	size_t nitems = 0, arraysz;
	struct stat stbuf;
	DIR *dirp;

	PRIV_START;
	dirp = opendir(SD);
	PRIV_END;
	if (dirp == NULL)
		return(-1);
	if (fstat(dirfd(dirp), &stbuf) < 0)
		goto errdone;

	/*
	 * Estimate the array size by taking the size of the directory file
	 * and dividing it by a multiple of the minimum size entry. 
	 */
	arraysz = (stbuf.st_size / 24);
	queue = calloc(arraysz, sizeof(struct queue *));
	if (queue == NULL)
		goto errdone;

	while ((d = readdir(dirp)) != NULL) {
		if (d->d_name[0] != 'c' || d->d_name[1] != 'f')
			continue;	/* daemon control files only */
		PRIV_START;
		if (stat(d->d_name, &stbuf) < 0) {
			PRIV_END;
			continue;	/* Doesn't exist */
		}
		PRIV_END;
		q = malloc(sizeof(struct queue));
		if (q == NULL)
			goto errdone;
		q->q_time = stbuf.st_mtime;
		strlcpy(q->q_name, d->d_name, sizeof(q->q_name));

		/*
		 * Check to make sure the array has space left and
		 * realloc the maximum size.
		 */
		if (nitems == arraysz) {
			struct queue **newqueue;
			newqueue = reallocarray(queue,
			    arraysz, 2 * sizeof(struct queue *));
			if (newqueue == NULL) {
				free(q);
				goto errdone;
			}
			arraysz *= 2;
			queue = newqueue;
		}
		queue[nitems++] = q;
	}
	closedir(dirp);
	if (nitems)
		qsort(queue, nitems, sizeof(struct queue *), compar);
	*namelist = queue;
	return(nitems);

errdone:
	if (queue != NULL) {
		while (nitems--)
			free(queue[nitems]);
		free(queue);
	}
	closedir(dirp);
	return(-1);
}

/*
 * Compare modification times.
 */
static int
compar(const void *v1, const void *v2)
{
	struct queue *p1 = *(struct queue **)v1;
	struct queue *p2 = *(struct queue **)v2;

	return(p1->q_time - p2->q_time);
}

/*
 * Figure out whether the local machine is the same
 * as the remote machine (RM) entry (if it exists).
 */
char *
checkremote(void)
{
	char lname[NI_MAXHOST], rname[NI_MAXHOST];
	struct addrinfo hints, *res, *res0;
	static char errbuf[128];
	int error;
	struct ifaddrs *ifap, *ifa;
	const int niflags = NI_NUMERICHOST;
	struct sockaddr *sa;
#ifdef __KAME__
	struct sockaddr_in6 sin6;
	struct sockaddr_in6 *sin6p;
#endif

	remote = 0;	/* assume printer is local on failure */

	if (RM == NULL || *RM == '\0')
		return NULL;

	/* get the local interface addresses */
	siginterrupt(SIGINT, 1);
	if (getifaddrs(&ifap) < 0) {
		(void)snprintf(errbuf, sizeof(errbuf),
		    "unable to get local interface address: %s",
		    strerror(errno));
		siginterrupt(SIGINT, 0);
		return errbuf;
	}
	siginterrupt(SIGINT, 0);

	/* get the remote host addresses (RM) */
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	res = NULL;
	siginterrupt(SIGINT, 1);
	error = getaddrinfo(RM, NULL, &hints, &res0);
	siginterrupt(SIGINT, 0);
	if (error) {
		(void)snprintf(errbuf, sizeof(errbuf),
		    "unable to resolve remote machine %s: %s",
		    RM, gai_strerror(error));
		freeifaddrs(ifap);
		return errbuf;
	}

	remote = 1;	/* assume printer is remote */

	for (res = res0; res; res = res->ai_next) {
		siginterrupt(SIGINT, 1);
		error = getnameinfo(res->ai_addr, res->ai_addrlen,
		    rname, sizeof(rname), NULL, 0, niflags);
		siginterrupt(SIGINT, 0);
		if (error != 0)
			continue;
		for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
			sa = ifa->ifa_addr;
#ifdef __KAME__
			sin6p = (struct sockaddr_in6 *)sa;
			if (sa->sa_family == AF_INET6 &&
			    sa->sa_len == sizeof(sin6) &&
			    IN6_IS_ADDR_LINKLOCAL(&sin6p->sin6_addr) &&
			    *(u_int16_t *)&sin6p->sin6_addr.s6_addr[2]) {
				/* kame scopeid hack */
				memcpy(&sin6, ifa->ifa_addr, sizeof(sin6));
				sin6.sin6_scope_id =
				    ntohs(*(u_int16_t *)&sin6p->sin6_addr.s6_addr[2]);
				sin6.sin6_addr.s6_addr[2] = 0;
				sin6.sin6_addr.s6_addr[3] = 0;
				sa = (struct sockaddr *)&sin6;
			}
#endif
			siginterrupt(SIGINT, 1);
			error = getnameinfo(sa, sa->sa_len,
			    lname, sizeof(lname), NULL, 0, niflags);
			siginterrupt(SIGINT, 0);
			if (error != 0)
				continue;

			if (strcmp(rname, lname) == 0) {
				remote = 0;
				goto done;
			}
		}
	}
done:
	freeaddrinfo(res0);
	freeifaddrs(ifap);
	return NULL;
}

__dead void
fatal(const char *msg, ...)
{
	extern char *__progname;
	va_list ap;

	va_start(ap, msg);
	if (from != host)
		(void)printf("%s: ", host);
	(void)printf("%s: ", __progname);
	if (printer)
		(void)printf("%s: ", printer);
	(void)vprintf(msg, ap);
	va_end(ap);
	(void)putchar('\n');
	exit(1);
}

int
safe_open(const char *path, int flags, mode_t mode)
{
	int fd, serrno;
	struct stat stbuf;

	if ((fd = open(path, flags|O_NONBLOCK, mode)) < 0 ||
	    fstat(fd, &stbuf) < 0) {
		if (fd >= 0) {
			serrno = errno;
			close(fd);
			errno = serrno;
		}
		return (-1);
	}
	if (!S_ISREG(stbuf.st_mode)) {
		close(fd);
		errno = EACCES;
		return (-1);
	}
	if (mode)
		(void)fchmod(fd, mode);
	return (fd);
}

/*
 * Make sure there's some work to do before forking off a child - lpd
 * Check to see if anything in queue - lpq
 */
int
ckqueue(char *cap)
{
	struct dirent *d;
	DIR *dirp;
	char *spooldir;

	if (cgetstr(cap, "sd", &spooldir) >= 0) {
		dirp = opendir(spooldir);
		free(spooldir);
	} else
		dirp = opendir(_PATH_DEFSPOOL);

	if (dirp == NULL)
		return (-1);
	while ((d = readdir(dirp)) != NULL) {
		if (d->d_name[0] == 'c' && d->d_name[1] == 'f') {
			closedir(dirp);
			return (1);		/* found a cf file */
		}
	}
	closedir(dirp);
	return (0);
}
