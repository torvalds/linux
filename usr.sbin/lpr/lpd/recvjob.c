/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)recvjob.c	8.2 (Berkeley) 4/27/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * Receive printer jobs from the network, queue them and
 * start the printer daemon.
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lp.h"
#include "lp.local.h"
#include "ctlinfo.h"
#include "extern.h"
#include "pathnames.h"

#define ack()	(void) write(STDOUT_FILENO, sp, (size_t)1);

/*
 * The buffer size to use when reading/writing spool files.
 */
#define	SPL_BUFSIZ	BUFSIZ

static char	 dfname[NAME_MAX];	/* data files */
static int	 minfree;       /* keep at least minfree blocks available */
static const char	*sp = "";
static char	 tfname[NAME_MAX];	/* tmp copy of cf before linking */

static int	 chksize(int _size);
static void	 frecverr(const char *_msg, ...) __printf0like(1, 2);
static int	 noresponse(void);
static void	 rcleanup(int _signo);
static int	 read_number(const char *_fn);
static int	 readfile(struct printer *_pp, char *_file, size_t _size);
static int	 readjob(struct printer *_pp);


void
recvjob(const char *printer)
{
	struct stat stb;
	int status;
	struct printer myprinter, *pp = &myprinter;

	/*
	 * Perform lookup for printer name or abbreviation
	 */
	init_printer(pp);
	status = getprintcap(printer, pp);
	switch (status) {
	case PCAPERR_OSERR:
		frecverr("cannot open printer description file");
		break;
	case PCAPERR_NOTFOUND:
		frecverr("unknown printer %s", printer);
		break;
	case PCAPERR_TCLOOP:
		fatal(pp, "potential reference loop detected in printcap file");
	default:
		break;
	}
	
	(void) close(STDERR_FILENO);			/* set up log file */
	if (open(pp->log_file, O_WRONLY|O_APPEND, 0664) < 0) {
		syslog(LOG_ERR, "%s: %m", pp->log_file);
		(void) open(_PATH_DEVNULL, O_WRONLY);
	}

	if (chdir(pp->spool_dir) < 0)
		frecverr("%s: chdir(%s): %s", pp->printer, pp->spool_dir,
		    strerror(errno));
	if (stat(pp->lock_file, &stb) == 0) {
		if (stb.st_mode & 010) {
			/* queue is disabled */
			putchar('\1');		/* return error code */
			exit(1);
		}
	} else if (stat(pp->spool_dir, &stb) < 0)
		frecverr("%s: stat(%s): %s", pp->printer, pp->spool_dir,
		    strerror(errno));
	minfree = 2 * read_number("minfree");	/* scale KB to 512 blocks */
	signal(SIGTERM, rcleanup);
	signal(SIGPIPE, rcleanup);

	if (readjob(pp))
		printjob(pp);
}

/*
 * Read printer jobs sent by lpd and copy them to the spooling directory.
 * Return the number of jobs successfully transferred.
 */
static int
readjob(struct printer *pp)
{
	register int size;
	int cfcnt, dfcnt;
	char *cp, *clastp, *errmsg;
	char givenid[32], givenhost[MAXHOSTNAMELEN];

	ack();
	cfcnt = 0;
	dfcnt = 0;
	for (;;) {
		/*
		 * Read a command to tell us what to do
		 */
		cp = line;
		clastp = line + sizeof(line) - 1;
		do {
			size = read(STDOUT_FILENO, cp, (size_t)1);
			if (size != (ssize_t)1) {
				if (size < (ssize_t)0) {
					frecverr("%s: lost connection",
					    pp->printer);
					/*NOTREACHED*/
				}
				return (cfcnt);
			}
		} while ((*cp++ != '\n') && (cp <= clastp));
		if (cp > clastp) {
			frecverr("%s: readjob overflow", pp->printer);
			/*NOTREACHED*/
		}
		*--cp = '\0';
		cp = line;
		switch (*cp++) {
		case '\1':	/* cleanup because data sent was bad */
			rcleanup(0);
			continue;

		case '\2':	/* read cf file */
			size = 0;
			dfcnt = 0;
			while (*cp >= '0' && *cp <= '9')
				size = size * 10 + (*cp++ - '0');
			if (*cp++ != ' ')
				break;
			/*
			 * host name has been authenticated, we use our
			 * view of the host name since we may be passed
			 * something different than what gethostbyaddr()
			 * returns
			 */
			strlcpy(cp + 6, from_host, sizeof(line)
			    + (size_t)(line - cp - 6));
			if (strchr(cp, '/')) {
				frecverr("readjob: %s: illegal path name", cp);
				/*NOTREACHED*/
			}
			strlcpy(tfname, cp, sizeof(tfname));
			tfname[sizeof (tfname) - 1] = '\0';
			tfname[0] = 't';
			if (!chksize(size)) {
				(void) write(STDOUT_FILENO, "\2", (size_t)1);
				continue;
			}
			if (!readfile(pp, tfname, (size_t)size)) {
				rcleanup(0);
				continue;
			}
			errmsg = ctl_renametf(pp->printer, tfname);
			tfname[0] = '\0';
			if (errmsg != NULL) {
				frecverr("%s: %s", pp->printer, errmsg);
				/*NOTREACHED*/
			}
			cfcnt++;
			continue;

		case '\3':	/* read df file */
			*givenid = '\0';
			*givenhost = '\0';
			size = 0;
			while (*cp >= '0' && *cp <= '9')
				size = size * 10 + (*cp++ - '0');
			if (*cp++ != ' ')
				break;
			if (strchr(cp, '/')) {
				frecverr("readjob: %s: illegal path name", cp);
				/*NOTREACHED*/
			}
			if (!chksize(size)) {
				(void) write(STDOUT_FILENO, "\2", (size_t)1);
				continue;
			}
			strlcpy(dfname, cp, sizeof(dfname));
			dfcnt++;
			trstat_init(pp, dfname, dfcnt);
			(void) readfile(pp, dfname, (size_t)size);
			trstat_write(pp, TR_RECVING, (size_t)size, givenid,
			    from_host, givenhost);
			continue;
		}
		frecverr("protocol screwup: %s", line);
		/*NOTREACHED*/
	}
}

/*
 * Read files send by lpd and copy them to the spooling directory.
 */
static int
readfile(struct printer *pp, char *file, size_t size)
{
	register char *cp;
	char buf[SPL_BUFSIZ];
	size_t amt, i;
	int err, fd, j;

	fd = open(file, O_CREAT|O_EXCL|O_WRONLY, FILMOD);
	if (fd < 0) {
		frecverr("%s: readfile: error on open(%s): %s",
			 pp->printer, file, strerror(errno));
		/*NOTREACHED*/
	}
	ack();
	err = 0;
	for (i = 0; i < size; i += SPL_BUFSIZ) {
		amt = SPL_BUFSIZ;
		cp = buf;
		if (i + amt > size)
			amt = size - i;
		do {
			j = read(STDOUT_FILENO, cp, amt);
			if (j <= 0) {
				frecverr("%s: lost connection", pp->printer);
				/*NOTREACHED*/
			}
			amt -= j;
			cp += j;
		} while (amt > 0);
		amt = SPL_BUFSIZ;
		if (i + amt > size)
			amt = size - i;
		if (write(fd, buf, amt) != (ssize_t)amt) {
			err++;
			break;
		}
	}
	(void) close(fd);
	if (err) {
		frecverr("%s: write error on close(%s)", pp->printer, file);
		/*NOTREACHED*/
	}
	if (noresponse()) {		/* file sent had bad data in it */
		if (strchr(file, '/') == NULL)
			(void) unlink(file);
		return (0);
	}
	ack();
	return (1);
}

static int
noresponse(void)
{
	char resp;

	if (read(STDOUT_FILENO, &resp, (size_t)1) != 1) {
		frecverr("lost connection in noresponse()");
		/*NOTREACHED*/
	}
	if (resp == '\0')
		return(0);
	return(1);
}

/*
 * Check to see if there is enough space on the disk for size bytes.
 * 1 == OK, 0 == Not OK.
 */
static int
chksize(int size)
{
	int64_t spacefree;
	struct statfs sfb;

	if (statfs(".", &sfb) < 0) {
		syslog(LOG_ERR, "%s: %m", "statfs(\".\")");
		return (1);
	}
	spacefree = sfb.f_bavail * (sfb.f_bsize / 512);
	size = (size + 511) / 512;
	if (minfree + size > spacefree)
		return(0);
	return(1);
}

static int
read_number(const char *fn)
{
	char lin[80];
	register FILE *fp;

	if ((fp = fopen(fn, "r")) == NULL)
		return (0);
	if (fgets(lin, sizeof(lin), fp) == NULL) {
		fclose(fp);
		return (0);
	}
	fclose(fp);
	return (atoi(lin));
}

/*
 * Remove all the files associated with the current job being transferred.
 */
static void
rcleanup(int signo __unused)
{
	if (tfname[0] && strchr(tfname, '/') == NULL)
		(void) unlink(tfname);
	if (dfname[0] && strchr(dfname, '/') == NULL) {
		do {
			do
				(void) unlink(dfname);
			while (dfname[2]-- != 'A');
			dfname[2] = 'z';
		} while (dfname[0]-- != 'd');
	}
	dfname[0] = '\0';
}

#include <stdarg.h>

static void
frecverr(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	syslog(LOG_ERR, "Error receiving job from %s:", from_host);
	vsyslog(LOG_ERR, msg, ap);
	va_end(ap);
	/*
	 * rcleanup is not called until AFTER logging the error message,
	 * because rcleanup will zap some variables which may have been
	 * supplied as parameters for that msg...
	 */
	rcleanup(0);
	/* 
	 * Add a minimal delay before returning the final error code to
	 * the sending host.  This just in case that machine responds
	 * this error by INSTANTLY retrying (and instantly re-failing...).
	 * It would be stupid of the sending host to do that, but if there
	 * was a broken implementation which did it, the result might be
	 * obscure performance problems and a flood of syslog messages on
	 * the receiving host.
	 */ 
	sleep(2);		/* a paranoid throttling measure */
	putchar('\1');		/* return error code */
	exit(1);
}
