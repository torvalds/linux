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
static char sccsid[] = "@(#)displayq.c	8.4 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
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
 * Routines to display the state of the queue.
 */
#define JOBCOL	40		/* column for job # in -l format */
#define OWNCOL	7		/* start of Owner column in normal */
#define SIZCOL	62		/* start of Size column in normal */

/*
 * isprint() takes a parameter of 'int', but expect values in the range
 * of unsigned char.  Define a wrapper which takes a value of type 'char',
 * whether signed or unsigned, and ensure it ends up in the right range.
 */
#define	isprintch(Anychar) isprint((u_char)(Anychar))

/*
 * Stuff for handling job specifications
 */
static int	col;		/* column on screen */
static char	current[MAXNAMLEN+1];	/* current file being printed */
static char	file[MAXNAMLEN+1];	/* print file name */
static int	first;		/* first file in ``files'' column? */
static int	garbage;	/* # of garbage cf files */
static int	lflag;		/* long output option */
static int	rank;		/* order to be printed (-1=none, 0=active) */
static long	totsize;	/* total print job size in bytes */

static const char  *head0 = "Rank   Owner      Job  Files";
static const char  *head1 = "Total Size\n";

static void	alarmhandler(int _signo);
static void	filtered_write(char *_obuffer, int _wlen, FILE *_wstream);
static void	daemonwarn(const struct printer *_pp);

/*
 * Display the current state of the queue. Format = 1 if long format.
 */
void
displayq(struct printer *pp, int format)
{
	register struct jobqueue *q;
	register int i, nitems, fd, ret;
	char *cp, *endp;
	struct jobqueue **queue;
	struct stat statb;
	FILE *fp;
	void (*savealrm)(int);

	lflag = format;
	totsize = 0;
	rank = -1;

	if ((cp = checkremote(pp))) {
		printf("Warning: %s\n", cp);
		free(cp);
	}

	/*
	 * Print out local queue
	 * Find all the control files in the spooling directory
	 */
	PRIV_START
	if (chdir(pp->spool_dir) < 0)
		fatal(pp, "cannot chdir to spooling directory: %s",
		      strerror(errno));
	PRIV_END
	if ((nitems = getq(pp, &queue)) < 0)
		fatal(pp, "cannot examine spooling area\n");
	PRIV_START
	ret = stat(pp->lock_file, &statb);
	PRIV_END
	if (ret >= 0) {
		if (statb.st_mode & LFM_PRINT_DIS) {
			if (pp->remote)
				printf("%s: ", local_host);
			printf("Warning: %s is down: ", pp->printer);
			PRIV_START
			fd = open(pp->status_file, O_RDONLY|O_SHLOCK);
			PRIV_END
			if (fd >= 0) {
				while ((i = read(fd, line, sizeof(line))) > 0)
					(void) fwrite(line, 1, i, stdout);
				(void) close(fd);	/* unlocks as well */
			} else
				putchar('\n');
		}
		if (statb.st_mode & LFM_QUEUE_DIS) {
			if (pp->remote)
				printf("%s: ", local_host);
			printf("Warning: %s queue is turned off\n", 
			       pp->printer);
		}
	}

	if (nitems) {
		PRIV_START
		fp = fopen(pp->lock_file, "r");
		PRIV_END
		if (fp == NULL)
			daemonwarn(pp);
		else {
			/* get daemon pid */
			cp = current;
			endp = cp + sizeof(current) - 1;
			while ((i = getc(fp)) != EOF && i != '\n') {
				if (cp < endp)
					*cp++ = i;
			}
			*cp = '\0';
			i = atoi(current);
			if (i <= 0) {
				ret = -1;
			} else {
				PRIV_START
				ret = kill(i, 0);
				PRIV_END
			}
			if (ret < 0) {
				daemonwarn(pp);
			} else {
				/* read current file name */
				cp = current;
				endp = cp + sizeof(current) - 1;
				while ((i = getc(fp)) != EOF && i != '\n') {
					if (cp < endp)
						*cp++ = i;
				}
				*cp = '\0';
				/*
				 * Print the status file.
				 */
				if (pp->remote)
					printf("%s: ", local_host);
				PRIV_START
				fd = open(pp->status_file, O_RDONLY|O_SHLOCK);
				PRIV_END
				if (fd >= 0) {
					while ((i = read(fd, line,
							 sizeof(line))) > 0)
						fwrite(line, 1, i, stdout);
					close(fd);	/* unlocks as well */
				} else
					putchar('\n');
			}
			(void) fclose(fp);
		}
		/*
		 * Now, examine the control files and print out the jobs to
		 * be done for each user.
		 */
		if (!lflag)
			header();
		for (i = 0; i < nitems; i++) {
			q = queue[i];
			inform(pp, q->job_cfname);
			free(q);
		}
		free(queue);
	}
	if (!pp->remote) {
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
	(void) snprintf(line, sizeof(line), "%c%s", format ? '\4' : '\3',
			pp->remote_queue);
	cp = line;
	for (i = 0; i < requests && cp-line+10 < sizeof(line) - 1; i++) {
		cp += strlen(cp);
		(void) sprintf(cp, " %d", requ[i]);
	}
	for (i = 0; i < users && cp - line + 1 + strlen(user[i]) < 
		sizeof(line) - 1; i++) {
		cp += strlen(cp);
		*cp++ = ' ';
		(void) strcpy(cp, user[i]);
	}
	strcat(line, "\n");
	savealrm = signal(SIGALRM, alarmhandler);
	alarm(pp->conn_timeout);
	fd = getport(pp, pp->remote_host, 0);
	alarm(0);
	(void)signal(SIGALRM, savealrm);
	if (fd < 0) {
		if (from_host != local_host)
			printf("%s: ", local_host);
		printf("connection to %s is down\n", pp->remote_host);
	}
	else {
		i = strlen(line);
		if (write(fd, line, i) != i)
			fatal(pp, "Lost connection");
		while ((i = read(fd, line, sizeof(line))) > 0)
			filtered_write(line, i, stdout);
		filtered_write(NULL, -1, stdout);
		(void) close(fd);
	}
}

/*
 * The lpq-info read from remote hosts may contain unprintable characters,
 * or carriage-returns instead of line-feeds.  Clean those up before echoing
 * the lpq-info line(s) to stdout.  The info may also be missing any kind of
 * end-of-line character.  This also turns CRLF and LFCR into a plain LF.
 *
 * This routine may be called multiple times to process a single set of
 * information, and after a set is finished this routine must be called
 * one extra time with NULL specified as the buffer address.
 */
static void
filtered_write(char *wbuffer, int wlen, FILE *wstream)
{
	static char lastchar, savedchar;
	char *chkptr, *dest_end, *dest_ch, *nxtptr, *w_end;
	int destlen;
	char destbuf[BUFSIZ];

	if (wbuffer == NULL) {
		if (savedchar != '\0') {
			if (savedchar == '\r')
				savedchar = '\n';
			fputc(savedchar, wstream);
			lastchar = savedchar;
			savedchar = '\0';
		}
		if (lastchar != '\0' && lastchar != '\n')
			fputc('\n', wstream);
		lastchar = '\0';
		return;
	}

	dest_ch = &destbuf[0];
	dest_end = dest_ch + sizeof(destbuf);
	chkptr = wbuffer;
	w_end = wbuffer + wlen;
	lastchar = '\0';
	if (savedchar != '\0') {
		chkptr = &savedchar;
		nxtptr = wbuffer;
	} else
		nxtptr = chkptr + 1;

	while (chkptr < w_end) {
		if (nxtptr < w_end) {
			if ((*chkptr == '\r' && *nxtptr == '\n') ||
			    (*chkptr == '\n' && *nxtptr == '\r')) {
				*dest_ch++ = '\n';
				/* want to skip past that second character */
				nxtptr++;
				goto check_next;
			}
		} else {
			/* This is the last byte in the buffer given on this
			 * call, so check if it could be the first-byte of a
			 * significant two-byte sequence.  If it is, then
			 * don't write it out now, but save for checking in
			 * the next call.
			 */
			savedchar = '\0';
			if (*chkptr == '\r' || *chkptr == '\n') {
				savedchar = *chkptr;
				break;
			}
		}
		if (*chkptr == '\r')
			*dest_ch++ = '\n';
#if 0		/* XXX - don't translate unprintable characters (yet) */
		else if (*chkptr != '\t' && *chkptr != '\n' &&
		    !isprintch(*chkptr))
			*dest_ch++ = '?';
#endif
		else
			*dest_ch++ = *chkptr;

check_next:
		chkptr = nxtptr;
		nxtptr = chkptr + 1;
		if (dest_ch >= dest_end) {
			destlen = dest_ch - &destbuf[0];
			fwrite(destbuf, 1, destlen, wstream);
			lastchar = destbuf[destlen - 1];
			dest_ch = &destbuf[0];
		}
	}
	destlen = dest_ch - &destbuf[0];
	if (destlen > 0) {
		fwrite(destbuf, 1, destlen, wstream);
		lastchar = destbuf[destlen - 1];
	}
}

/*
 * Print a warning message if there is no daemon present.
 */
static void
daemonwarn(const struct printer *pp)
{
	if (pp->remote)
		printf("%s: ", local_host);
	puts("Warning: no daemon present");
	current[0] = '\0';
}

/*
 * Print the header for the short listing format
 */
void
header(void)
{
	printf("%s", head0);
	col = strlen(head0)+1;
	blankfill(SIZCOL);
	printf("%s", head1);
}

void
inform(const struct printer *pp, char *cf)
{
	int copycnt, jnum;
	char	 savedname[MAXPATHLEN+1];
	FILE	*cfp;

	/*
	 * There's a chance the control file has gone away
	 * in the meantime; if this is the case just keep going
	 */
	PRIV_START
	if ((cfp = fopen(cf, "r")) == NULL)
		return;
	PRIV_END

	if (rank < 0)
		rank = 0;
	if (pp->remote || garbage || strcmp(cf, current))
		rank++;

	/*
	 * The cf-file may include commands to print more than one datafile
	 * from the user.  For each datafile, the cf-file contains at least
	 * one line which starts with some format-specifier ('a'-'z'), and
	 * a second line ('N'ame) which indicates the original name the user
	 * specified for that file.  There can be multiple format-spec lines
	 * for a single Name-line, if the user requested multiple copies of
	 * that file.  Standard lpr puts the format-spec line(s) before the
	 * Name-line, while lprNG puts the Name-line before the format-spec
	 * line(s).  This section needs to handle the lines in either order.
	 */
	copycnt = 0;
	file[0] = '\0';
	savedname[0] = '\0';
	jnum = calc_jobnum(cf, NULL);
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
				printf("%-10s %-3d  ", line+1, jnum);
				col += 16;
				first = 1;
			}
			continue;
		default: /* some format specifer and file name? */
			if (line[0] < 'a' || line[0] > 'z')
				break;
			if (copycnt == 0 || strcmp(file, line+1) != 0) {
				strlcpy(file, line + 1, sizeof(file));
			}
			copycnt++;
			/*
			 * deliberately 'continue' to another get_line(), so
			 * all format-spec lines for this datafile are read
			 * in and counted before calling show()
			 */
			continue;
		case 'N':
			strlcpy(savedname, line + 1, sizeof(savedname));
			break;
		}
		if ((file[0] != '\0') && (savedname[0] != '\0')) {
			show(savedname, file, copycnt);
			copycnt = 0;
			file[0] = '\0';
			savedname[0] = '\0';
		}
	}
	fclose(cfp);
	/* check for a file which hasn't been shown yet */
	if (file[0] != '\0') {
		if (savedname[0] == '\0') {
			/* a safeguard in case the N-ame line is missing */
			strlcpy(savedname, file, sizeof(savedname));
		}
		show(savedname, file, copycnt);
	}
	if (!lflag) {
		blankfill(SIZCOL);
		printf("%ld bytes\n", totsize);
		totsize = 0;
	}
}

int
inlist(char *uname, char *cfile)
{
	int *r, jnum;
	char **u;
	const char *cfhost;

	if (users == 0 && requests == 0)
		return(1);
	/*
	 * Check to see if it's in the user list
	 */
	for (u = user; u < &user[users]; u++)
		if (!strcmp(*u, uname))
			return(1);
	/*
	 * Check the request list
	 */
	jnum = calc_jobnum(cfile, &cfhost);
	for (r = requ; r < &requ[requests]; r++)
		if (*r == jnum && !strcmp(cfhost, from_host))
			return(1);
	return(0);
}

void
show(const char *nfile, const char *datafile, int copies)
{
	if (strcmp(nfile, " ") == 0)
		nfile = "(standard input)";
	if (lflag)
		ldump(nfile, datafile, copies);
	else
		dump(nfile, datafile, copies);
}

/*
 * Fill the line with blanks to the specified column
 */
void
blankfill(int tocol)
{
	while (col++ < tocol)
		putchar(' ');
}

/*
 * Give the abbreviated dump of the file names
 */
void
dump(const char *nfile, const char *datafile, int copies)
{
	struct stat lbuf;
	const char etctmpl[] = ", ...";
	char	 etc[sizeof(etctmpl)];
	char	*lastsep;
	short	 fill, nlen;
	short	 rem, remetc;

	/*
	 * Print as many filenames as will fit
	 *      (leaving room for the 'total size' field)
	 */
	fill = first ? 0 : 2;	/* fill space for ``, '' */
	nlen = strlen(nfile);
	rem = SIZCOL - 1 - col;
	if (nlen + fill > rem) {
		if (first) {
			/* print the right-most part of the name */
			printf("...%s ", &nfile[3+nlen-rem]);
			col = SIZCOL;
		} else if (rem > 0) {
			/* fit as much of the etc-string as we can */
			remetc = rem;
			if (rem > strlen(etctmpl))
				remetc = strlen(etctmpl);
			etc[0] = '\0';
			strncat(etc, etctmpl, remetc);
			printf("%s", etc);
			col += remetc;
			rem -= remetc;
			/* room for the last segment of this filename? */
			lastsep = strrchr(nfile, '/');
			if ((lastsep != NULL) && (rem > strlen(lastsep))) {
				/* print the right-most part of this name */
				printf("%s", lastsep);
				col += strlen(lastsep);
			} else {
				/* do not pack any more names in here */
				blankfill(SIZCOL);
			}
		}
	} else {
		if (!first)
			printf(", ");
		printf("%s", nfile);
		col += nlen + fill;
	}
	first = 0;

	PRIV_START
	if (*datafile && !stat(datafile, &lbuf))
		totsize += copies * lbuf.st_size;
	PRIV_END
}

/*
 * Print the long info about the file
 */
void
ldump(const char *nfile, const char *datafile, int copies)
{
	struct stat lbuf;

	putchar('\t');
	if (copies > 1)
		printf("%-2d copies of %-19s", copies, nfile);
	else
		printf("%-32s", nfile);
	if (*datafile && !stat(datafile, &lbuf))
		printf(" %qd bytes", (long long) lbuf.st_size);
	else
		printf(" ??? bytes");
	putchar('\n');
}

/*
 * Print the job's rank in the queue,
 *   update col for screen management
 */
void
prank(int n)
{
	char rline[100];
	static const char *r[] = {
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

void
alarmhandler(int signo __unused)
{
	/* the signal is ignored */
	/* (the '__unused' is just to avoid a compile-time warning) */
}
