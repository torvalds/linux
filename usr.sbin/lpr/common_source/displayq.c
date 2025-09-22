/*	$OpenBSD: displayq.c,v 1.41 2024/04/23 13:34:51 jsg Exp $	*/
/*	$NetBSD: displayq.c,v 1.21 2001/08/30 00:51:50 itojun Exp $	*/

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

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

/*
 * Routines to display the state of the queue.
 */
#define JOBCOL	40		/* column for job # in -l format */
#define OWNCOL	7		/* start of Owner column in normal */
#define SIZCOL	62		/* start of Size column in normal */

/*
 * Stuff for handling job specifications
 */
extern int	requ[];		/* job number of spool entries */
extern int	requests;	/* # of spool requests */
extern char    *user[];	        /* users to process */
extern int	users;		/* # of users in user array */

static int	termwidth;
static int	col;		/* column on screen */
static char	current[NAME_MAX]; /* current file being printed */
static char	file[NAME_MAX];	/* print file name */
static int	first;		/* first file in ``files'' column? */
static int	lflag;		/* long output option */
static off_t	totsize;	/* total print job size in bytes */

static const char head0[] = "Rank   Owner      Job  Files";
static const char head1[] = "Total Size\n";

static void	alarmer(int);
static void	blankfill(int);
static void	dump(char *, char *, int);
static void	header(void);
static void	inform(char *, int);
static int	inlist(char *, char *);
static void	ldump(char *, char *, int);
static void	nodaemon(void);
static void	prank(int);
static void	show(char *, char *, int);

/*
 * Display the current state of the queue. Format = 1 if long format.
 */
void
displayq(int format)
{
	struct queue *q;
	int i, rank, nitems, fd, ret, len;
	char *cp, *ecp, *p;
	struct queue **queue;
	struct winsize win;
	struct stat statb;
	FILE *fp;

	termwidth = 0;
	if ((p = getenv("COLUMNS")) != NULL)
		termwidth = strtonum(p, 1, INT_MAX, NULL);
	if (termwidth == 0 && ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == 0 &&
	    win.ws_col > 0)
		termwidth = win.ws_col;
	if (termwidth == 0)
		termwidth = 80;

	if (termwidth < 60)
		termwidth = 60;

	lflag = format;
	totsize = 0;
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
	if (cgetstr(bp, "lo", &LO) < 0)
		LO = DEFLOCK;
	if (cgetstr(bp, "st", &ST) < 0)
		ST = DEFSTAT;
	cgetstr(bp, "rm", &RM);
	if ((cp = checkremote()) != NULL)
		printf("Warning: %s\n", cp);

	/*
	 * Print out local queue
	 * Find all the control files in the spooling directory
	 */
	PRIV_START;
	if (chdir(SD) < 0)
		fatal("cannot chdir to spooling directory");
	PRIV_END;
	if ((nitems = getq(&queue)) < 0)
		fatal("cannot examine spooling area");
	PRIV_START;
	ret = stat(LO, &statb);
	PRIV_END;
	if (ret >= 0) {
		if (statb.st_mode & S_IXUSR) {
			if (remote)
				printf("%s: ", host);
			printf("Warning: %s is down: ", printer);
			PRIV_START;
			fd = safe_open(ST, O_RDONLY|O_NOFOLLOW, 0);
			PRIV_END;
			if (fd >= 0 && flock(fd, LOCK_SH) == 0) {
				while ((i = read(fd, line, sizeof(line))) > 0)
					(void)fwrite(line, 1, i, stdout);
				(void)close(fd);	/* unlocks as well */
			} else
				putchar('\n');
		}
		if (statb.st_mode & S_IXGRP) {
			if (remote)
				printf("%s: ", host);
			printf("Warning: %s queue is turned off\n", printer);
		}
	}

	if (nitems) {
		PRIV_START;
		fd = safe_open(LO, O_RDONLY|O_NOFOLLOW, 0);
		PRIV_END;
		if (fd < 0 || (fp = fdopen(fd, "r")) == NULL) {
			if (fd >= 0)
				close(fd);
			nodaemon();
		} else {
			/* get daemon pid */
			cp = current;
			ecp = cp + sizeof(current) - 1;
			while ((i = getc(fp)) != EOF && i != '\n') {
				if (cp < ecp)
					*cp++ = i;
			}
			*cp = '\0';
			i = atoi(current);
			if (i <= 0) {
				ret = -1;
			} else {
				PRIV_START;
				ret = kill(i, 0);
				PRIV_END;
			}
			if (ret < 0 && errno != EPERM) {
				nodaemon();
			} else {
				/* read current file name */
				cp = current;
		    		ecp = cp + sizeof(current) - 1;
				while ((i = getc(fp)) != EOF && i != '\n') {
					if (cp < ecp)
						*cp++ = i;
				}
				*cp = '\0';
				/*
				 * Print the status file.
				 */
				if (remote)
					printf("%s: ", host);
				PRIV_START;
				fd = safe_open(ST, O_RDONLY|O_NOFOLLOW, 0);
				PRIV_END;
				if (fd >= 0 && flock(fd, LOCK_SH) == 0) {
					while ((i = read(fd, line, sizeof(line))) > 0)
						(void)fwrite(line, 1, i, stdout);
					(void)close(fd);	/* unlocks as well */
				} else
					putchar('\n');
			}
			(void)fclose(fp);
		}
		/*
		 * Now, examine the control files and print out the jobs to
		 * be done for each user.
		 */
		if (!lflag)
			header();
		/* The currently printed job is treated specially. */
		if (!remote && current[0] != '\0')
			inform(current, 0);
		for (i = 0, rank = 1; i < nitems; i++) {
			q = queue[i];
			if (remote || strcmp(current, q->q_name) != 0)
				inform(q->q_name, rank++);
			free(q);
		}
	}
	free(queue);
	if (!remote) {
		if (nitems == 0)
			puts("no entries");
		return;
	}

	/*
	 * Print foreign queue
	 * Note that a file in transit may show up in either queue.
	 */
	if (nitems)
		putchar('\n');
	(void)snprintf(line, sizeof(line), "%c%s", format + '\3', RP);
	cp = line;
	cp += strlen(cp);
	for (i = 0; i < requests && cp - line < sizeof(line) - 1; i++) {
		len = line + sizeof(line) - cp;
		if (snprintf(cp, len, " %d", requ[i]) >= len) {
			cp += strlen(cp);
			break;
		}
		cp += strlen(cp);
	}
	for (i = 0; i < users && cp - line < sizeof(line) - 1; i++) {
		len = line + sizeof(line) - cp;
		if (snprintf(cp, len, " %s", user[i]) >= len) {
			cp += strlen(cp);
			break;
		}
	}
	if (cp-line < sizeof(line) - 1)
		strlcat(line, "\n", sizeof(line));
	else
		line[sizeof(line) - 2] = '\n';
	fd = getport(RM, 0);
	if (fd < 0) {
		if (from != host)
			printf("%s: ", host);
		(void)printf("connection to %s is down\n", RM);
	}
	else {
		struct sigaction osa, nsa;
		char *visline;
		int n = 0;

		i = strlen(line);
		if (write(fd, line, i) != i)
			fatal("Lost connection");
		memset(&nsa, 0, sizeof(nsa));
		nsa.sa_handler = alarmer;
		sigemptyset(&nsa.sa_mask);
		nsa.sa_flags = 0;
		(void)sigaction(SIGALRM, &nsa, &osa);
		alarm(wait_time);
		if ((visline = malloc(4 * sizeof(line) + 1)) == NULL)
			fatal("Out of memory");
		while ((i = read(fd, line, sizeof(line))) > 0) {
			n = strvisx(visline, line, i, VIS_SAFE|VIS_NOSLASH);
			(void)fwrite(visline, 1, n, stdout);
			alarm(wait_time);
		}
		/* XXX some LPR implementations may not end stream with '\n' */
		if (n > 0 && visline[n-1] != '\n')
			putchar('\n');
		alarm(0);
		(void)sigaction(SIGALRM, &osa, NULL);
		free(visline);
		(void)close(fd);
	}
}

static void
alarmer(int s)
{
	/* nothing */
}

/*
 * Print a warning message if there is no daemon present.
 */
static void
nodaemon(void)
{
	if (remote)
		printf("\n%s: ", host);
	puts("Warning: no daemon present");
	current[0] = '\0';
}

/*
 * Print the header for the short listing format
 */
static void
header(void)
{
	printf(head0);
	col = strlen(head0)+1;
	blankfill(termwidth - (80 - SIZCOL));
	printf(head1);
}

static void
inform(char *cf, int rank)
{
	int fd, j;
	FILE *cfp = NULL;

	/*
	 * There's a chance the control file has gone away
	 * in the meantime; if this is the case just keep going
	 */
	PRIV_START;
	fd = safe_open(cf, O_RDONLY|O_NOFOLLOW, 0);
	PRIV_END;
	if (fd < 0 || (cfp = fdopen(fd, "r")) == NULL) {
		if (fd >= 0)
			close(fd);
		return;
	}

	j = 0;
	while (get_line(cfp)) {
		switch (line[0]) {
		case 'P': /* Was this file specified in the user's list? */
			if (!inlist(line+1, cf)) {
				fclose(cfp);
				return;
			}
			if (lflag) {
				printf("\n%s: ", line+1);
				col = strlen(line+1) + 2;
				prank(rank);
				blankfill(JOBCOL);
				printf(" [job %s]\n", cf+3);
			} else {
				col = 0;
				prank(rank);
				blankfill(OWNCOL);
				printf("%-10s %-3d  ", line+1, atoi(cf+3));
				col += 16;
				first = 1;
			}
			continue;
		default: /* some format specifer and file name? */
			if (line[0] < 'a' || line[0] > 'z')
				continue;
			if (j == 0 || strcmp(file, line+1) != 0)
				(void)strlcpy(file, line+1, sizeof(file));
			j++;
			continue;
		case 'N':
			show(line+1, file, j);
			file[0] = '\0';
			j = 0;
		}
	}
	fclose(cfp);
	if (!lflag) {
		blankfill(termwidth - (80 - SIZCOL));
		printf("%lld bytes\n", (long long)totsize);
		totsize = 0;
	}
}

static int
inlist(char *name, char *file)
{
	int *r, n;
	char **u, *cp;

	if (users == 0 && requests == 0)
		return(1);
	/*
	 * Check to see if it's in the user list
	 */
	for (u = user; u < &user[users]; u++)
		if (!strcmp(*u, name))
			return(1);
	/*
	 * Check the request list
	 */
	for (n = 0, cp = file+3; isdigit((unsigned char)*cp); )
		n = n * 10 + (*cp++ - '0');
	for (r = requ; r < &requ[requests]; r++)
		if (*r == n && !strcmp(cp, from))
			return(1);
	return(0);
}

static void
show(char *nfile, char *file, int copies)
{
	if (strcmp(nfile, " ") == 0)
		nfile = "(standard input)";
	if (lflag)
		ldump(nfile, file, copies);
	else
		dump(nfile, file, copies);
}

/*
 * Fill the line with blanks to the specified column
 */
static void
blankfill(int n)
{
	while (col++ < n)
		putchar(' ');
}

/*
 * Give the abbreviated dump of the file names
 */
static void
dump(char *nfile, char *file, int copies)
{
	int n, fill;
	struct stat lbuf;

	/*
	 * Print as many files as will fit
	 *  (leaving room for the total size)
	 */
	fill = first ? 0 : 2;	/* fill space for ``, '' */
	if (((n = strlen(nfile)) + col + fill) >=
	    (termwidth - (80 - SIZCOL)) - 4) {
		if (col < (termwidth - (80 - SIZCOL))) {
			printf(" ..."), col += 4;
			blankfill(termwidth - (80 - SIZCOL));
		}
	} else {
		if (first)
			first = 0;
		else
			printf(", ");
		printf("%s", nfile);
		col += n+fill;
	}
	PRIV_START;
	if (*file && !stat(file, &lbuf))
		totsize += copies * lbuf.st_size;
	PRIV_END;
}

/*
 * Print the long info about the file
 */
static void
ldump(char *nfile, char *file, int copies)
{
	struct stat lbuf;
	int ret;

	putchar('\t');
	if (copies > 1)
		printf("%-2d copies of %-19s", copies, nfile);
	else
		printf("%-32s", nfile);
	PRIV_START;
	ret = stat(file, &lbuf);
	PRIV_END;
	if (*file && !ret)
		printf(" %lld bytes", (long long)lbuf.st_size);
	else
		printf(" ??? bytes");
	putchar('\n');
}

/*
 * Print the job's rank in the queue,
 *   update col for screen management
 */
static void
prank(int n)
{
	char rline[100];
	static char *r[] = {
		"th", "st", "nd", "rd", "th", "th", "th", "th", "th", "th"
	};

	if (n == 0) {
		printf("active");
		col += 6;
		return;
	}
	if ((n/10)%10 == 1)
		(void)snprintf(rline, sizeof(rline), "%dth", n);
	else
		(void)snprintf(rline, sizeof(rline), "%d%s", n, r[n%10]);
	col += strlen(rline);
	printf("%s", rline);
}
