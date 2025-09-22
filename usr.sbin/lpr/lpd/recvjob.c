/*	$OpenBSD: recvjob.c,v 1.26 2015/01/16 06:40:18 deraadt Exp $	*/
/*	$NetBSD: recvjob.c,v 1.14 2001/12/04 22:52:44 christos Exp $	*/

/*
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

/*
 * Receive printer jobs from the network, queue them and
 * start the printer daemon.
 */
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include "lp.h"
#include "lp.local.h"
#include "extern.h"
#include "pathnames.h"

#define ack()	(void)write(STDOUT_FILENO, sp, 1);

static char	 dfname[NAME_MAX];	/* data files */
static int	 minfree;       /* keep at least minfree blocks available */
static char	*sp = "";
static char	 tfname[NAME_MAX];	/* tmp copy of cf before linking */

static int        chksize(int);
static void       frecverr(const char *, ...)
	__attribute__((__format__(__printf__, 1, 2)));
static int        noresponse(void);
static void       rcleanup(int);
static int        read_number(char *);
static int        readfile(char *, int);
static int        readjob(void);


void
recvjob(void)
{
	struct stat stb;
	int status;

	/*
	 * Perform lookup for printer name or abbreviation
	 */
	if ((status = cgetent(&bp, printcapdb, printer)) == -2)
		frecverr("cannot open printer description file");
	else if (status == -1)
		frecverr("unknown printer %s", printer);
	else if (status == -3)
		fatal("potential reference loop detected in printcap file");
	
	if (cgetstr(bp, "lf", &LF) == -1)
		LF = _PATH_CONSOLE;
	if (cgetstr(bp, "sd", &SD) == -1)
		SD = _PATH_DEFSPOOL;
	if (cgetstr(bp, "lo", &LO) == -1)
		LO = DEFLOCK;

	(void)close(2);			/* set up log file */
	PRIV_START;
	if (open(LF, O_WRONLY|O_APPEND, 0664) < 0) {
		syslog(LOG_ERR, "%s: %m", LF);
		(void)open(_PATH_DEVNULL, O_WRONLY);
	}
	PRIV_END;

	if (chdir(SD) < 0)
		frecverr("%s: %s: %m", printer, SD);
	if (stat(LO, &stb) == 0) {
		if (stb.st_mode & 010) {
			/* queue is disabled */
			putchar('\1');		/* return error code */
			exit(1);
		}
	} else if (stat(SD, &stb) < 0)
		frecverr("%s: %s: %m", printer, SD);

	minfree = 2 * read_number("minfree");	/* scale KB to 512 blocks */
	signal(SIGTERM, rcleanup);
	signal(SIGPIPE, rcleanup);

	if (readjob())
		printjob();
}

/*
 * Read printer jobs sent by lpd and copy them to the spooling directory.
 * Return the number of jobs successfully transferred.
 */
static int
readjob(void)
{
	int size, nfiles;
	char *cp;

	ack();
	nfiles = 0;
	for (;;) {
		/*
		 * Read a command to tell us what to do
		 */
		cp = line;
		do {
			if ((size = read(STDOUT_FILENO, cp, 1)) != 1) {
				if (size < 0)
					frecverr("%s: Lost connection",
					    printer);
				return(nfiles);
			}
		} while (*cp++ != '\n' && (cp - line + 1) < sizeof(line));
		if (cp - line + 1 >= sizeof(line))
			frecverr("readjob overflow");
		*--cp = '\0';
		cp = line;
		switch (*cp++) {
		case '\1':	/* cleanup because data sent was bad */
			rcleanup(0);
			continue;

		case '\2':	/* read cf file */
			size = 0;
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
			strlcpy(cp + 6, from, sizeof(line) + line - cp - 6);
			if (strchr(cp, '/'))
				frecverr("readjob: %s: illegal path name", cp);
			strlcpy(tfname, cp, sizeof(tfname));
			tfname[0] = 't';
			if (!chksize(size)) {
				(void)write(STDOUT_FILENO, "\2", 1);
				continue;
			}
			/*
			 * XXX
			 * We blindly believe what the remote host puts
			 * for the path to the df file.  In general this
			 * is OK since we don't allow paths with '/' in
			 * them.  Still, it would be better to sanity
			 * check the cf file sent to us and make the
			 * df name match the cf name we used.  That way
			 * we avoid any possible collisions.
			 */
			if (!readfile(tfname, size)) {
				rcleanup(0);
				continue;
			}
			if (link(tfname, cp) < 0)
				frecverr("link %s %s: %m", tfname, cp);
			(void)unlink(tfname);
			tfname[0] = '\0';
			nfiles++;
			continue;

		case '\3':	/* read df file */
			size = 0;
			while (*cp >= '0' && *cp <= '9')
				size = size * 10 + (*cp++ - '0');
			if (*cp++ != ' ')
				break;
			if (strchr(cp, '/'))
				frecverr("readjob: %s: illegal path name", cp);
			if (!chksize(size)) {
				(void)write(STDOUT_FILENO, "\2", 1);
				continue;
			}
			(void)strlcpy(dfname, cp, sizeof(dfname));
			(void)readfile(dfname, size);
			continue;
		}
		frecverr("protocol screwup: %s", line);
	}
}

/*
 * Read files send by lpd and copy them to the spooling directory.
 */
static int
readfile(char *file, int size)
{
	char *cp;
	char buf[BUFSIZ];
	int i, j, amt;
	int fd, err;

	if ((fd = open(file, O_CREAT|O_EXCL|O_WRONLY, FILMOD)) < 0)
		frecverr("readfile: %s: illegal path name: %m", file);
	ack();
	err = 0;
	for (i = 0; i < size; i += BUFSIZ) {
		amt = BUFSIZ;
		cp = buf;
		if (i + amt > size)
			amt = size - i;
		do {
			j = read(STDOUT_FILENO, cp, amt);
			if (j <= 0)
				frecverr("Lost connection");
			amt -= j;
			cp += j;
		} while (amt > 0);
		amt = BUFSIZ;
		if (i + amt > size)
			amt = size - i;
		if (write(fd, buf, amt) != amt) {
			err++;
			break;
		}
	}
	(void)close(fd);
	if (err)
		frecverr("%s: write error", file);
	if (noresponse()) {		/* file sent had bad data in it */
		if (strchr(file, '/') == NULL)
			(void)unlink(file);
		return(0);
	}
	ack();
	return(1);
}

static int
noresponse(void)
{
	char resp;

	if (read(STDOUT_FILENO, &resp, 1) != 1)
		frecverr("Lost connection");
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

	if (size <= 0)
		return (0);
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
read_number(char *fn)
{
	char lin[80];
	FILE *fp;

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
rcleanup(int signo)
{
	int save_errno = errno;

	if (tfname[0] && strchr(tfname, '/') == NULL)
		(void)unlink(tfname);
	if (dfname[0] && strchr(dfname, '/') == NULL) {
		do {
			do
				(void)unlink(dfname);
			while (dfname[2]-- != 'A')
				;
			dfname[2] = 'z';
		} while (dfname[0]-- != 'd');
	}
	dfname[0] = '\0';
	errno = save_errno;
}

static void
frecverr(const char *msg, ...)
{
	extern char fromb[];
	va_list ap;

	va_start(ap, msg);
	rcleanup(0);
	syslog(LOG_ERR, "%s", fromb);
	vsyslog(LOG_ERR, msg, ap);
	va_end(ap);
	putchar('\1');		/* return error code */
	exit(1);
}
