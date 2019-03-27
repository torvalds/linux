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
static char sccsid[] = "@(#)rmjob.c	8.2 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/uio.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define psignal foil_gcc_psignal
#define	sys_siglist foil_gcc_siglist
#include <unistd.h>
#undef psignal
#undef sys_siglist

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

/*
 * rmjob - remove the specified jobs from the queue.
 */

/*
 * Stuff for handling lprm specifications
 */
static char	root[] = "root";
static int	all = 0;		/* eliminate all files (root only) */
static int	cur_daemon;		/* daemon's pid */
static char	current[7+MAXHOSTNAMELEN];  /* active control file name */

static	void	alarmhandler(int _signo);
static	void	do_unlink(char *_file);
static int	 isowner(char *_owner, char *_file, const char *_cfhost);

void
rmjob(const char *printer)
{
	register int i, nitems;
	int assassinated = 0;
	struct dirent **files;
	char *cp;
	struct printer myprinter, *pp = &myprinter;

	init_printer(pp);
	if ((i = getprintcap(printer, pp)) < 0)
		fatal(pp, "getprintcap: %s", pcaperr(i));
	if ((cp = checkremote(pp))) {
		printf("Warning: %s\n", cp);
		free(cp);
	}

	/*
	 * If the format was `lprm -' and the user isn't the super-user,
	 *  then fake things to look like he said `lprm user'.
	 */
	if (users < 0) {
		if (getuid() == 0)
			all = 1;	/* all files in local queue */
		else {
			user[0] = person;
			users = 1;
		}
	}
	if (!strcmp(person, "-all")) {
		if (from_host == local_host)
			fatal(pp, "The login name \"-all\" is reserved");
		all = 1;	/* all those from 'from_host' */
		person = root;
	}

	PRIV_START
	if (chdir(pp->spool_dir) < 0)
		fatal(pp, "cannot chdir to spool directory");
	if ((nitems = scandir(".", &files, iscf, NULL)) < 0)
		fatal(pp, "cannot access spool directory");
	PRIV_END

	if (nitems) {
		/*
		 * Check for an active printer daemon (in which case we
		 *  kill it if it is reading our file) then remove stuff
		 *  (after which we have to restart the daemon).
		 */
		if (lockchk(pp, pp->lock_file) && chk(current)) {
			PRIV_START
			assassinated = kill(cur_daemon, SIGINT) == 0;
			PRIV_END
			if (!assassinated)
				fatal(pp, "cannot kill printer daemon");
		}
		/*
		 * process the files
		 */
		for (i = 0; i < nitems; i++)
			process(pp, files[i]->d_name);
	}
	rmremote(pp);
	/*
	 * Restart the printer daemon if it was killed
	 */
	if (assassinated && !startdaemon(pp))
		fatal(pp, "cannot restart printer daemon\n");
	exit(0);
}

/*
 * Process a lock file: collect the pid of the active
 *  daemon and the file name of the active spool entry.
 * Return boolean indicating existence of a lock file.
 */
int
lockchk(struct printer *pp, char *slockf)
{
	register FILE *fp;
	register int i, n;

	PRIV_START
	if ((fp = fopen(slockf, "r")) == NULL) {
		if (errno == EACCES)
			fatal(pp, "%s: %s", slockf, strerror(errno));
		else
			return(0);
	}
	PRIV_END
	if (!get_line(fp)) {
		(void) fclose(fp);
		return(0);		/* no daemon present */
	}
	cur_daemon = atoi(line);
	if (kill(cur_daemon, 0) < 0 && errno != EPERM) {
		(void) fclose(fp);
		return(0);		/* no daemon present */
	}
	for (i = 1; (n = fread(current, sizeof(char), sizeof(current), fp)) <= 0; i++) {
		if (i > 5) {
			n = 1;
			break;
		}
		sleep(i);
	}
	current[n-1] = '\0';
	(void) fclose(fp);
	return(1);
}

/*
 * Process a control file.
 */
void
process(const struct printer *pp, char *file)
{
	FILE *cfp;

	if (!chk(file))
		return;
	PRIV_START
	if ((cfp = fopen(file, "r")) == NULL)
		fatal(pp, "cannot open %s", file);
	PRIV_END
	while (get_line(cfp)) {
		switch (line[0]) {
		case 'U':  /* unlink associated files */
			if (strchr(line+1, '/') || strncmp(line+1, "df", 2))
				break;
			do_unlink(line+1);
		}
	}
	(void) fclose(cfp);
	do_unlink(file);
}

static void
do_unlink(char *file)
{
	int	ret;

	if (from_host != local_host)
		printf("%s: ", local_host);
	PRIV_START
	ret = unlink(file);
	PRIV_END
	printf(ret ? "cannot dequeue %s\n" : "%s dequeued\n", file);
}

/*
 * Do the dirty work in checking
 */
int
chk(char *file)
{
	int *r, jnum;
	char **u;
	const char *cfhost;
	FILE *cfp;

	/*
	 * Check for valid cf file name (mostly checking current).
	 */
	if (strlen(file) < 7 || file[0] != 'c' || file[1] != 'f')
		return(0);

	jnum = calc_jobnum(file, &cfhost);
	if (all && (from_host == local_host || !strcmp(from_host, cfhost)))
		return(1);

	/*
	 * get the owner's name from the control file.
	 */
	PRIV_START
	if ((cfp = fopen(file, "r")) == NULL)
		return(0);
	PRIV_END
	while (get_line(cfp)) {
		if (line[0] == 'P')
			break;
	}
	(void) fclose(cfp);
	if (line[0] != 'P')
		return(0);

	if (users == 0 && requests == 0)
		return(!strcmp(file, current) && isowner(line+1, file, cfhost));
	/*
	 * Check the request list
	 */
	for (r = requ; r < &requ[requests]; r++)
		if (*r == jnum && isowner(line+1, file, cfhost))
			return(1);
	/*
	 * Check to see if it's in the user list
	 */
	for (u = user; u < &user[users]; u++)
		if (!strcmp(*u, line+1) && isowner(line+1, file, cfhost))
			return(1);
	return(0);
}

/*
 * If root is removing a file on the local machine, allow it.
 * If root is removing a file from a remote machine, only allow
 * files sent from the remote machine to be removed.
 * Normal users can only remove the file from where it was sent.
 */
static int
isowner(char *owner, char *file, const char *cfhost)
{
	if (!strcmp(person, root) && (from_host == local_host ||
	    !strcmp(from_host, cfhost)))
		return (1);
	if (!strcmp(person, owner) && !strcmp(from_host, cfhost))
		return (1);
	if (from_host != local_host)
		printf("%s: ", local_host);
	printf("%s: Permission denied\n", file);
	return(0);
}

/*
 * Check to see if we are sending files to a remote machine. If we are,
 * then try removing files on the remote machine.
 */
void
rmremote(const struct printer *pp)
{
	int i, elem, firstreq, niov, rem, totlen;
	char buf[BUFSIZ];
	void (*savealrm)(int);
	struct iovec *iov;

	if (!pp->remote)
		return;	/* not sending to a remote machine */

	/*
	 * Flush stdout so the user can see what has been deleted
	 * while we wait (possibly) for the connection.
	 */
	fflush(stdout);

	/*
	 * Counting:
	 *	4 == "\5" + remote_queue + " " + person
	 *	2 * users == " " + user[i] for each user
	 *	requests == asprintf results for each request
	 *	1 == "\n"
	 * Although laborious, doing it this way makes it possible for
	 * us to process requests of indeterminate length without
	 * applying an arbitrary limit.  Arbitrary Limits Are Bad (tm).
	 */
	if (users > 0)
		niov = 4 + 2 * users + requests + 1;
	else
		niov = 4 + requests + 1;
	iov = malloc(niov * sizeof *iov);
	if (iov == NULL)
		fatal(pp, "out of memory in rmremote()");
	iov[0].iov_base = "\5";
	iov[1].iov_base = pp->remote_queue;
	iov[2].iov_base = " ";
	iov[3].iov_base = all ? "-all" : person;
	elem = 4;
	for (i = 0; i < users; i++) {
		iov[elem].iov_base = " ";
		iov[elem + 1].iov_base = user[i];
		elem += 2;
	}
	firstreq = elem;
	for (i = 0; i < requests; i++) {
		asprintf((char **)&iov[elem].iov_base, " %d", requ[i]);
		if (iov[elem].iov_base == 0)
			fatal(pp, "out of memory in rmremote()");
		elem++;
	}
	iov[elem++].iov_base = "\n";
	for (totlen = i = 0; i < niov; i++)
		totlen += (iov[i].iov_len = strlen(iov[i].iov_base));

	savealrm = signal(SIGALRM, alarmhandler);
	alarm(pp->conn_timeout);
	rem = getport(pp, pp->remote_host, 0);
	(void)signal(SIGALRM, savealrm);
	if (rem < 0) {
		if (from_host != local_host)
			printf("%s: ", local_host);
		printf("connection to %s is down\n", pp->remote_host);
	} else {
		if (writev(rem, iov, niov) != totlen)
			fatal(pp, "Lost connection");
		while ((i = read(rem, buf, sizeof(buf))) > 0)
			(void) fwrite(buf, 1, i, stdout);
		(void) close(rem);
	}
	for (i = 0; i < requests; i++)
		free(iov[firstreq + i].iov_base);
	free(iov);
}

/*
 * Return 1 if the filename begins with 'cf'
 */
int
iscf(const struct dirent *d)
{
	return(d->d_name[0] == 'c' && d->d_name[1] == 'f');
}

void
alarmhandler(int signo __unused)
{
	/* the signal is ignored */
	/* (the '__unused' is just to avoid a compile-time warning) */
}
