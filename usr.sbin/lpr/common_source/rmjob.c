/*	$OpenBSD: rmjob.c,v 1.24 2019/07/03 03:24:03 deraadt Exp $	*/
/*	$NetBSD: rmjob.c,v 1.16 2000/04/16 14:43:58 mrg Exp $	*/

/*
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

#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

/*
 * rmjob - remove the specified jobs from the queue.
 */

/*
 * Stuff for handling lprm specifications
 */
extern char	*user[];		/* users to process */
extern int	users;			/* # of users in user array */
extern int	requ[];			/* job number of spool entries */
extern int	requests;		/* # of spool requests */
extern char	*person;		/* name of person doing lprm */

static char	root[] = "root";
static int	all = 0;		/* eliminate all files (root only) */
static int	cur_daemon;		/* daemon's pid */
static char	current[NAME_MAX];	/* active control file name */

static void	alarmer(int);
static int	chk(char *);
static void	do_unlink(char *);
static int	iscf(const struct dirent *);
static int	isowner(char *, char *);
static int	lockchk(char *);
static void	process(char *);
static void	rmremote(void);

void
rmjob(void)
{
	int i, nitems;
	int assassinated = 0;
	struct dirent **files;
	char *cp;

	if ((i = cgetent(&bp, printcapdb, printer)) == -2)
		fatal("can't open printer description file");
	else if (i == -1)
		fatal("unknown printer");
	else if (i == -3)
		fatal("potential reference loop detected in printcap file");
	if (cgetstr(bp, DEFLP, &LP) < 0)
		LP = _PATH_DEFDEVLP;
	if (cgetstr(bp, "rp", &RP) < 0)
		RP = DEFLP;
	if (cgetstr(bp, "sd", &SD) < 0)
		SD = _PATH_DEFSPOOL;
	if (cgetstr(bp,"lo", &LO) < 0)
		LO = DEFLOCK;
	cgetstr(bp, "rm", &RM);
	if ((cp = checkremote()) != NULL)
		printf("Warning: %s\n", cp);

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
		if (from == host)
			fatal("The login name \"-all\" is reserved");
		all = 1;	/* all those from 'from' */
		person = root;
	}

	PRIV_START;
	if (chdir(SD) < 0)
		fatal("cannot chdir to spool directory");
	if ((nitems = scandir(".", &files, iscf, NULL)) < 0)
		fatal("cannot access spool directory");
	PRIV_END;

	if (nitems) {
		/*
		 * Check for an active printer daemon.  If one is running
		 * and it is reading our file, kill it, then remove stuff.
		 * Lastly, restart the daemon if it is not (or no longer)
		 * running.
		 */
		if (lockchk(LO) && chk(current)) {
			PRIV_START;
			assassinated = kill(cur_daemon, SIGINT) == 0;
			PRIV_END;
			if (!assassinated)
				fatal("cannot kill printer daemon");
		}
		/*
		 * process the files
		 */
		for (i = 0; i < nitems; i++)
			process(files[i]->d_name);
	}
	rmremote();
	/*
	 * Restart the printer daemon if it was killed
	 */
	if (assassinated && !startdaemon(printer))
		fatal("cannot restart printer daemon");
	exit(0);
}

/*
 * Process a lock file: collect the pid of the active
 * daemon and the file name of the active spool entry.
 * Return boolean indicating existence of a lock file.
 */
static int
lockchk(char *s)
{
	FILE *fp = NULL;
	int fd, i, n;

	/* NOTE: lock file is owned by root, not the user. */
	PRIV_START;
	fd = safe_open(s, O_RDONLY|O_NOFOLLOW, 0);
	PRIV_END;
	if (fd < 0 || (fp = fdopen(fd, "r")) == NULL) {
		if (fd >= 0)
			close(fd);
		if (errno == EACCES)
			fatal("can't access lock file");
		else
			return(0);
	}
	if (!get_line(fp)) {
		(void)fclose(fp);
		return(0);		/* no daemon present */
	}
	cur_daemon = atoi(line);
	if (kill(cur_daemon, 0) < 0 && errno != EPERM) {
		(void)fclose(fp);
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
	(void)fclose(fp);
	return(1);
}

/*
 * Process a control file.
 */
static void
process(char *file)
{
	FILE *cfp = NULL;
	int fd;

	if (!chk(file))
		return;
	PRIV_START;
	fd = safe_open(file, O_RDONLY|O_NOFOLLOW, 0);
	PRIV_END;
	if (fd < 0 || (cfp = fdopen(fd, "r")) == NULL) {
		if (fd >= 0)
			close(fd);
		fatal("cannot open %s", file);
	}
	while (get_line(cfp)) {
		switch (line[0]) {
		case 'U':  /* unlink associated files */
			if (strchr(line+1, '/') || strncmp(line+1, "df", 2))
				break;
			do_unlink(line+1);
		}
	}
	(void)fclose(cfp);
	do_unlink(file);
}

static void
do_unlink(char *file)
{
	int	ret;

	if (from != host)
		printf("%s: ", host);
	PRIV_START;
	ret = unlink(file);
	PRIV_END;
	printf(ret ? "cannot dequeue %s\n" : "%s dequeued\n", file);
}

/*
 * Do the dirty work in checking
 */
static int
chk(char *file)
{
	int *r, n, fd;
	char **u, *cp;
	FILE *cfp = NULL;

	/*
	 * Check for valid cf file name (mostly checking current).
	 */
	if (strlen(file) < 7 || file[0] != 'c' || file[1] != 'f')
		return(0);

	if (all && (from == host || !strcmp(from, file+6)))
		return(1);

	/*
	 * get the owner's name from the control file.
	 */
	PRIV_START;
	fd = safe_open(file, O_RDONLY|O_NOFOLLOW, 0);
	PRIV_END;
	if (fd < 0 || (cfp = fdopen(fd, "r")) == NULL) {
		if (fd >= 0)
			close(fd);
		return(0);
	}
	while (get_line(cfp)) {
		if (line[0] == 'P')
			break;
	}
	(void)fclose(cfp);
	if (line[0] != 'P')
		return(0);

	if (users == 0 && requests == 0)
		return(!strcmp(file, current) && isowner(line+1, file));
	/*
	 * Check the request list
	 */
	for (n = 0, cp = file+3; isdigit((unsigned char)*cp); )
		n = n * 10 + (*cp++ - '0');
	for (r = requ; r < &requ[requests]; r++)
		if (*r == n && isowner(line+1, file))
			return(1);
	/*
	 * Check to see if it's in the user list
	 */
	for (u = user; u < &user[users]; u++)
		if (!strcmp(*u, line+1) && isowner(line+1, file))
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
isowner(char *owner, char *file)
{
	if (!strcmp(person, root) && (from == host || !strcmp(from, file+6)))
		return(1);
	if (!strcmp(person, owner) && !strcmp(from, file+6))
		return(1);
	if (from != host)
		printf("%s: ", host);
	printf("%s: Permission denied\n", file);
	return(0);
}

/*
 * Check to see if we are sending files to a remote machine. If we are,
 * then try removing files on the remote machine.
 */
static void
rmremote(void)
{
	char *cp;
	int i, rem;
	size_t n;
	char buf[BUFSIZ];

	if (!remote)
		return;	/* not sending to a remote machine */

	/*
	 * Flush stdout so the user can see what has been deleted
	 * while we wait (possibly) for the connection.
	 */
	fflush(stdout);

	/* the trailing space will be replaced with a newline later */
	n = snprintf(buf, sizeof(buf), "\5%s %s ", RP, all ? "-all" : person);
	if (n < 0 || n >= sizeof(buf))
		goto bad;
	cp = buf + n;
	for (i = 0; i < users; i++) {
		n = strlcpy(cp, user[i], sizeof(buf) - (cp - buf + 1));
		if (n >= sizeof(buf) - (cp - buf + 1))
			goto bad;
		cp += n;
		*cp++ = ' ';
	}
	*cp = '\0';
	for (i = 0; i < requests; i++) {
		n = snprintf(cp, sizeof(buf) - (cp - buf), "%d ", requ[i]);
		if (n < 0 || n >= sizeof(buf) - (cp - buf))
			goto bad;
		cp += n;
	}
	cp[-1] = '\n';		/* replace space with newline, leave the NUL */
	rem = getport(RM, 0);
	if (rem < 0) {
		if (from != host)
			printf("%s: ", host);
		printf("connection to %s is down\n", RM);
	} else {
		struct sigaction osa, nsa;

		memset(&nsa, 0, sizeof(nsa));
		nsa.sa_handler = alarmer;
		sigemptyset(&nsa.sa_mask);
		nsa.sa_flags = 0;
		(void)sigaction(SIGALRM, &nsa, &osa);
		alarm(wait_time);

		i = strlen(buf);
		if (write(rem, buf, i) != i)
			fatal("Lost connection");
		while ((i = read(rem, buf, sizeof(buf))) > 0)
			(void)fwrite(buf, 1, i, stdout);
		alarm(0);
		(void)sigaction(SIGALRM, &osa, NULL);
		(void)close(rem);
	}
	return;
bad:
	printf("remote buffer too large\n");
	return;
}

static void
alarmer(int s)
{
	/* nothing */
}

/*
 * Return 1 if the filename begins with 'cf'
 */
static int
iscf(const struct dirent *d)
{
	return(d->d_name[0] == 'c' && d->d_name[1] == 'f');
}
