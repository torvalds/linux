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
static char sccsid[] = "@(#)printjob.c	8.7 (Berkeley) 5/10/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * printjob -- print jobs in the queue.
 *
 *	NOTE: the lock file is used to pass information to lpq and lprm.
 *	it does not need to be removed because file locks are dynamic.
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"
#include "extern.h"

#define DORETURN	0	/* dofork should return "can't fork" error */
#define DOABORT		1	/* dofork should just die if fork() fails */

/*
 * The buffer size to use when reading/writing spool files.
 */
#define	SPL_BUFSIZ	BUFSIZ

/*
 * Error tokens
 */
#define REPRINT		-2
#define ERROR		-1
#define	OK		0
#define	FATALERR	1
#define	NOACCT		2
#define	FILTERERR	3
#define	ACCESS		4

static dev_t	 fdev;		/* device of file pointed to by symlink */
static ino_t	 fino;		/* inode of file pointed to by symlink */
static FILE	*cfp;		/* control file */
static pid_t	 of_pid;	/* process id of output filter, if any */
static int	 child;		/* id of any filters */
static int	 job_dfcnt;	/* count of datafiles in current user job */
static int	 lfd;		/* lock file descriptor */
static int	 ofd;		/* output filter file descriptor */
static int	 tfd = -1;	/* output filter temp file output */
static int	 pfd;		/* prstatic inter file descriptor */
static int	 prchild;	/* id of pr process */
static char	 title[80];	/* ``pr'' title */
static char      locale[80];    /* ``pr'' locale */

/* these two are set from pp->daemon_user, but only if they are needed */
static char	*daemon_uname;	/* set from pwd->pw_name */
static int	 daemon_defgid;

static char	class[32];		/* classification field */
static char	origin_host[MAXHOSTNAMELEN];	/* user's host machine */
				/* indentation size in static characters */
static char	indent[10] = "-i0";
static char	jobname[100];		/* job or file name */
static char	length[10] = "-l";	/* page length in lines */
static char	logname[32];		/* user's login name */
static char	pxlength[10] = "-y";	/* page length in pixels */
static char	pxwidth[10] = "-x";	/* page width in pixels */
/* tempstderr is the filename used to catch stderr from exec-ing filters */
static char	tempstderr[] = "errs.XXXXXXX";
static char	width[10] = "-w";	/* page width in static characters */
#define TFILENAME "fltXXXXXX"
static char	tfile[] = TFILENAME;	/* file name for filter output */

static void	 abortpr(int _signo);
static void	 alarmhandler(int _signo);
static void	 banner(struct printer *_pp, char *_name1, char *_name2);
static int	 dofork(const struct printer *_pp, int _action);
static int	 dropit(int _c);
static int	 execfilter(struct printer *_pp, char *_f_cmd, char **_f_av,
		    int _infd, int _outfd);
static void	 init(struct printer *_pp);
static void	 openpr(const struct printer *_pp);
static void	 opennet(const struct printer *_pp);
static void	 opentty(const struct printer *_pp);
static void	 openrem(const struct printer *pp);
static int	 print(struct printer *_pp, int _format, char *_file);
static int	 printit(struct printer *_pp, char *_file);
static void	 pstatus(const struct printer *_pp, const char *_msg, ...)
		    __printflike(2, 3);
static char	 response(const struct printer *_pp);
static void	 scan_out(struct printer *_pp, int _scfd, char *_scsp, 
		    int _dlm);
static char	*scnline(int _key, char *_p, int _c);
static int	 sendfile(struct printer *_pp, int _type, char *_file, 
		    char _format, int _copyreq);
static int	 sendit(struct printer *_pp, char *_file);
static void	 sendmail(struct printer *_pp, char *_userid, int _bombed);
static void	 setty(const struct printer *_pp);
static void	 wait4data(struct printer *_pp, const char *_dfile);

void
printjob(struct printer *pp)
{
	struct stat stb;
	register struct jobqueue *q, **qp;
	struct jobqueue **queue;
	register int i, nitems;
	off_t pidoff;
	pid_t printpid;
	int errcnt, jobcount, statok, tempfd;

	jobcount = 0;
	init(pp); /* set up capabilities */
	(void) write(STDOUT_FILENO, "", 1);	/* ack that daemon is started */
	(void) close(STDERR_FILENO);			/* set up log file */
	if (open(pp->log_file, O_WRONLY|O_APPEND, LOG_FILE_MODE) < 0) {
		syslog(LOG_ERR, "%s: open(%s): %m", pp->printer,
		    pp->log_file);
		(void) open(_PATH_DEVNULL, O_WRONLY);
	}
	if(setgid(getegid()) != 0) err(1, "setgid() failed");
	printpid = getpid();			/* for use with lprm */
	setpgid((pid_t)0, printpid);

	/*
	 * At initial lpd startup, printjob may be called with various
	 * signal handlers in effect.  After that initial startup, any
	 * calls to printjob will have a *different* set of signal-handlers
	 * in effect.  Make sure all handlers are the ones we want.
	 */
	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, abortpr);
	signal(SIGINT, abortpr);
	signal(SIGQUIT, abortpr);
	signal(SIGTERM, abortpr);

	/*
	 * uses short form file names
	 */
	if (chdir(pp->spool_dir) < 0) {
		syslog(LOG_ERR, "%s: chdir(%s): %m", pp->printer,
		    pp->spool_dir);
		exit(1);
	}
	statok = stat(pp->lock_file, &stb);
	if (statok == 0 && (stb.st_mode & LFM_PRINT_DIS))
		exit(0);		/* printing disabled */
	umask(S_IWOTH);
	lfd = open(pp->lock_file, O_WRONLY|O_CREAT|O_EXLOCK|O_NONBLOCK, 
		   LOCK_FILE_MODE);
	if (lfd < 0) {
		if (errno == EWOULDBLOCK)	/* active daemon present */
			exit(0);
		syslog(LOG_ERR, "%s: open(%s): %m", pp->printer,
		    pp->lock_file);
		exit(1);
	}
	/*
	 * If the initial call to stat() failed, then lock_file will have
	 * been created by open().  Update &stb to match that new file.
	 */
	if (statok != 0)
		statok = stat(pp->lock_file, &stb);
	/* turn off non-blocking mode (was turned on for lock effects only) */
	if (fcntl(lfd, F_SETFL, 0) < 0) {
		syslog(LOG_ERR, "%s: fcntl(%s): %m", pp->printer,
		    pp->lock_file);
		exit(1);
	}
	ftruncate(lfd, 0);
	/*
	 * write process id for others to know
	 */
	sprintf(line, "%u\n", printpid);
	pidoff = i = strlen(line);
	if (write(lfd, line, i) != i) {
		syslog(LOG_ERR, "%s: write(%s): %m", pp->printer,
		    pp->lock_file);
		exit(1);
	}
	/*
	 * search the spool directory for work and sort by queue order.
	 */
	if ((nitems = getq(pp, &queue)) < 0) {
		syslog(LOG_ERR, "%s: can't scan %s", pp->printer, 
		    pp->spool_dir);
		exit(1);
	}
	if (nitems == 0)		/* no work to do */
		exit(0);
	if (stb.st_mode & LFM_RESET_QUE) { /* reset queue flag */
		if (fchmod(lfd, stb.st_mode & ~LFM_RESET_QUE) < 0)
			syslog(LOG_ERR, "%s: fchmod(%s): %m", pp->printer,
			    pp->lock_file);
	}

	/* create a file which will be used to hold stderr from filters */
	if ((tempfd = mkstemp(tempstderr)) == -1) {
		syslog(LOG_ERR, "%s: mkstemp(%s): %m", pp->printer,
		    tempstderr);
		exit(1);
	}
	if ((i = fchmod(tempfd, 0664)) == -1) {
		syslog(LOG_ERR, "%s: fchmod(%s): %m", pp->printer,
		    tempstderr);
		exit(1);
	}
	/* lpd doesn't need it to be open, it just needs it to exist */
	close(tempfd);

	openpr(pp);			/* open printer or remote */
again:
	/*
	 * we found something to do now do it --
	 *    write the name of the current control file into the lock file
	 *    so the spool queue program can tell what we're working on
	 */
	for (qp = queue; nitems--; free((char *) q)) {
		q = *qp++;
		if (stat(q->job_cfname, &stb) < 0)
			continue;
		errcnt = 0;
	restart:
		(void) lseek(lfd, pidoff, 0);
		(void) snprintf(line, sizeof(line), "%s\n", q->job_cfname);
		i = strlen(line);
		if (write(lfd, line, i) != i)
			syslog(LOG_ERR, "%s: write(%s): %m", pp->printer,
			    pp->lock_file);
		if (!pp->remote)
			i = printit(pp, q->job_cfname);
		else
			i = sendit(pp, q->job_cfname);
		/*
		 * Check to see if we are supposed to stop printing or
		 * if we are to rebuild the queue.
		 */
		if (fstat(lfd, &stb) == 0) {
			/* stop printing before starting next job? */
			if (stb.st_mode & LFM_PRINT_DIS)
				goto done;
			/* rebuild queue (after lpc topq) */
			if (stb.st_mode & LFM_RESET_QUE) {
				for (free(q); nitems--; free(q))
					q = *qp++;
				if (fchmod(lfd, stb.st_mode & ~LFM_RESET_QUE)
				    < 0)
					syslog(LOG_WARNING,
					    "%s: fchmod(%s): %m",
					    pp->printer, pp->lock_file);
				break;
			}
		}
		if (i == OK)		/* all files of this job printed */
			jobcount++;
		else if (i == REPRINT && ++errcnt < 5) {
			/* try reprinting the job */
			syslog(LOG_INFO, "restarting %s", pp->printer);
			if (of_pid > 0) {
				kill(of_pid, SIGCONT); /* to be sure */
				(void) close(ofd);
				while ((i = wait(NULL)) > 0 && i != of_pid)
					;
				if (i < 0)
					syslog(LOG_WARNING, "%s: after kill(of=%d), wait() returned: %m",
					    pp->printer, of_pid);
				of_pid = 0;
			}
			(void) close(pfd);	/* close printer */
			if (ftruncate(lfd, pidoff) < 0)
				syslog(LOG_WARNING, "%s: ftruncate(%s): %m", 
				    pp->printer, pp->lock_file);
			openpr(pp);		/* try to reopen printer */
			goto restart;
		} else {
			syslog(LOG_WARNING, "%s: job could not be %s (%s)", 
			    pp->printer,
			    pp->remote ? "sent to remote host" : "printed",
			    q->job_cfname);
			if (i == REPRINT) {
				/* ensure we don't attempt this job again */
				(void) unlink(q->job_cfname);
				q->job_cfname[0] = 'd';
				(void) unlink(q->job_cfname);
				if (logname[0])
					sendmail(pp, logname, FATALERR);
			}
		}
	}
	free(queue);
	/*
	 * search the spool directory for more work.
	 */
	if ((nitems = getq(pp, &queue)) < 0) {
		syslog(LOG_ERR, "%s: can't scan %s", pp->printer,
		    pp->spool_dir);
		exit(1);
	}
	if (nitems == 0) {		/* no more work to do */
	done:
		if (jobcount > 0) {	/* jobs actually printed */
			if (!pp->no_formfeed && !pp->tof)
				(void) write(ofd, pp->form_feed,
					     strlen(pp->form_feed));
			if (pp->trailer != NULL) /* output trailer */
				(void) write(ofd, pp->trailer,
					     strlen(pp->trailer));
		}
		(void) close(ofd);
		(void) wait(NULL);
		(void) unlink(tempstderr);
		exit(0);
	}
	goto again;
}

char	fonts[4][50];	/* fonts for troff */

char ifonts[4][40] = {
	_PATH_VFONTR,
	_PATH_VFONTI,
	_PATH_VFONTB,
	_PATH_VFONTS,
};

/*
 * The remaining part is the reading of the control file (cf)
 * and performing the various actions.
 */
static int
printit(struct printer *pp, char *file)
{
	register int i;
	char *cp;
	int bombed, didignorehdr;

	bombed = OK;
	didignorehdr = 0;
	/*
	 * open control file; ignore if no longer there.
	 */
	if ((cfp = fopen(file, "r")) == NULL) {
		syslog(LOG_INFO, "%s: fopen(%s): %m", pp->printer, file);
		return (OK);
	}
	/*
	 * Reset troff fonts.
	 */
	for (i = 0; i < 4; i++)
		strcpy(fonts[i], ifonts[i]);
	sprintf(&width[2], "%ld", pp->page_width);
	strcpy(indent+2, "0");

	/* initialize job-specific count of datafiles processed */
	job_dfcnt = 0;
	
	/*
	 *      read the control file for work to do
	 *
	 *      file format -- first character in the line is a command
	 *      rest of the line is the argument.
	 *      valid commands are:
	 *
	 *		S -- "stat info" for symbolic link protection
	 *		J -- "job name" on banner page
	 *		C -- "class name" on banner page
	 *              L -- "literal" user's name to print on banner
	 *		T -- "title" for pr
	 *		H -- "host name" of machine where lpr was done
	 *              P -- "person" user's login name
	 *              I -- "indent" amount to indent output
	 *		R -- laser dpi "resolution"
	 *              f -- "file name" name of text file to print
	 *		l -- "file name" text file with control chars
	 *		o -- "file name" postscript file, according to
	 *		     the RFC.  Here it is treated like an 'f'.
	 *		p -- "file name" text file to print with pr(1)
	 *		t -- "file name" troff(1) file to print
	 *		n -- "file name" ditroff(1) file to print
	 *		d -- "file name" dvi file to print
	 *		g -- "file name" plot(1G) file to print
	 *		v -- "file name" plain raster file to print
	 *		c -- "file name" cifplot file to print
	 *		1 -- "R font file" for troff
	 *		2 -- "I font file" for troff
	 *		3 -- "B font file" for troff
	 *		4 -- "S font file" for troff
	 *		N -- "name" of file (used by lpq)
	 *              U -- "unlink" name of file to remove
	 *                    (after we print it. (Pass 2 only)).
	 *		M -- "mail" to user when done printing
	 *              Z -- "locale" for pr
	 *
	 *      get_line reads a line and expands tabs to blanks
	 */

	/* pass 1 */

	while (get_line(cfp))
		switch (line[0]) {
		case 'H':
			strlcpy(origin_host, line + 1, sizeof(origin_host));
			if (class[0] == '\0') {
				strlcpy(class, line+1, sizeof(class));
			}
			continue;

		case 'P':
			strlcpy(logname, line + 1, sizeof(logname));
			if (pp->restricted) { /* restricted */
				if (getpwnam(logname) == NULL) {
					bombed = NOACCT;
					sendmail(pp, line+1, bombed);
					goto pass2;
				}
			}
			continue;

		case 'S':
			cp = line+1;
			i = 0;
			while (*cp >= '0' && *cp <= '9')
				i = i * 10 + (*cp++ - '0');
			fdev = i;
			cp++;
			i = 0;
			while (*cp >= '0' && *cp <= '9')
				i = i * 10 + (*cp++ - '0');
			fino = i;
			continue;

		case 'J':
			if (line[1] != '\0') {
				strlcpy(jobname, line + 1, sizeof(jobname));
			} else
				strcpy(jobname, " ");
			continue;

		case 'C':
			if (line[1] != '\0')
				strlcpy(class, line + 1, sizeof(class));
			else if (class[0] == '\0') {
				/* XXX - why call gethostname instead of
				 *       just strlcpy'ing local_host? */
				gethostname(class, sizeof(class));
				class[sizeof(class) - 1] = '\0';
			}
			continue;

		case 'T':	/* header title for pr */
			strlcpy(title, line + 1, sizeof(title));
			continue;

		case 'L':	/* identification line */
			if (!pp->no_header && !pp->header_last)
				banner(pp, line+1, jobname);
			continue;

		case '1':	/* troff fonts */
		case '2':
		case '3':
		case '4':
			if (line[1] != '\0') {
				strlcpy(fonts[line[0]-'1'], line + 1,
				    (size_t)50);
			}
			continue;

		case 'W':	/* page width */
			strlcpy(width+2, line + 1, sizeof(width) - 2);
			continue;

		case 'I':	/* indent amount */
			strlcpy(indent+2, line + 1, sizeof(indent) - 2);
			continue;

		case 'Z':       /* locale for pr */
			strlcpy(locale, line + 1, sizeof(locale));
			continue;

		default:	/* some file to print */
			/* only lowercase cmd-codes include a file-to-print */
			if ((line[0] < 'a') || (line[0] > 'z')) {
				/* ignore any other lines */
				if (lflag <= 1)
					continue;
				if (!didignorehdr) {
					syslog(LOG_INFO, "%s: in %s :",
					    pp->printer, file);
					didignorehdr = 1;
				}
				syslog(LOG_INFO, "%s: ignoring line: '%c' %s",
				    pp->printer, line[0], &line[1]);
				continue;
			}
			i = print(pp, line[0], line+1);
			switch (i) {
			case ERROR:
				if (bombed == OK)
					bombed = FATALERR;
				break;
			case REPRINT:
				(void) fclose(cfp);
				return (REPRINT);
			case FILTERERR:
			case ACCESS:
				bombed = i;
				sendmail(pp, logname, bombed);
			}
			title[0] = '\0';
			continue;

		case 'N':
		case 'U':
		case 'M':
		case 'R':
			continue;
		}

	/* pass 2 */

pass2:
	fseek(cfp, 0L, 0);
	while (get_line(cfp))
		switch (line[0]) {
		case 'L':	/* identification line */
			if (!pp->no_header && pp->header_last)
				banner(pp, line+1, jobname);
			continue;

		case 'M':
			if (bombed < NOACCT)	/* already sent if >= NOACCT */
				sendmail(pp, line+1, bombed);
			continue;

		case 'U':
			if (strchr(line+1, '/'))
				continue;
			(void) unlink(line+1);
		}
	/*
	 * clean-up in case another control file exists
	 */
	(void) fclose(cfp);
	(void) unlink(file);
	return (bombed == OK ? OK : ERROR);
}

/*
 * Print a file.
 * Set up the chain [ PR [ | {IF, OF} ] ] or {IF, RF, TF, NF, DF, CF, VF}.
 * Return -1 if a non-recoverable error occurred,
 * 2 if the filter detected some errors (but printed the job anyway),
 * 1 if we should try to reprint this job and
 * 0 if all is well.
 * Note: all filters take stdin as the file, stdout as the printer,
 * stderr as the log file, and must not ignore SIGINT.
 */
static int
print(struct printer *pp, int format, char *file)
{
	register int n, i;
	register char *prog;
	int fi, fo;
	FILE *fp;
	char *av[15], buf[SPL_BUFSIZ];
	pid_t wpid;
	int p[2], retcode, stopped, wstatus, wstatus_set;
	struct stat stb;

	/* Make sure the entire data file has arrived. */
	wait4data(pp, file);

	if (lstat(file, &stb) < 0 || (fi = open(file, O_RDONLY)) < 0) {
		syslog(LOG_INFO, "%s: unable to open %s ('%c' line)",
		    pp->printer, file, format);
		return (ERROR);
	}
	/*
	 * Check to see if data file is a symbolic link. If so, it should
	 * still point to the same file or someone is trying to print
	 * something he shouldn't.
	 */
	if ((stb.st_mode & S_IFMT) == S_IFLNK && fstat(fi, &stb) == 0 &&
	    (stb.st_dev != fdev || stb.st_ino != fino))
		return (ACCESS);

	job_dfcnt++;		/* increment datafile counter for this job */
	stopped = 0;		/* output filter is not stopped */

	/* everything seems OK, start it up */
	if (!pp->no_formfeed && !pp->tof) { /* start on a fresh page */
		(void) write(ofd, pp->form_feed, strlen(pp->form_feed));
		pp->tof = 1;
	}
	if (pp->filters[LPF_INPUT] == NULL
	    && (format == 'f' || format == 'l' || format == 'o')) {
		pp->tof = 0;
		while ((n = read(fi, buf, SPL_BUFSIZ)) > 0)
			if (write(ofd, buf, n) != n) {
				(void) close(fi);
				return (REPRINT);
			}
		(void) close(fi);
		return (OK);
	}
	switch (format) {
	case 'p':	/* print file using 'pr' */
		if (pp->filters[LPF_INPUT] == NULL) {	/* use output filter */
			prog = _PATH_PR;
			i = 0;
			av[i++] = "pr";
			av[i++] = width;
			av[i++] = length;
			av[i++] = "-h";
			av[i++] = *title ? title : " ";
			av[i++] = "-L";
			av[i++] = *locale ? locale : "C";
			av[i++] = "-F";
			av[i] = NULL;
			fo = ofd;
			goto start;
		}
		pipe(p);
		if ((prchild = dofork(pp, DORETURN)) == 0) {	/* child */
			dup2(fi, STDIN_FILENO);		/* file is stdin */
			dup2(p[1], STDOUT_FILENO);	/* pipe is stdout */
			closelog();
			closeallfds(3);
			execl(_PATH_PR, "pr", width, length,
			    "-h", *title ? title : " ",
			    "-L", *locale ? locale : "C",
			    "-F", (char *)0);
			syslog(LOG_ERR, "cannot execl %s", _PATH_PR);
			exit(2);
		}
		(void) close(p[1]);		/* close output side */
		(void) close(fi);
		if (prchild < 0) {
			prchild = 0;
			(void) close(p[0]);
			return (ERROR);
		}
		fi = p[0];			/* use pipe for input */
	case 'f':	/* print plain text file */
		prog = pp->filters[LPF_INPUT];
		av[1] = width;
		av[2] = length;
		av[3] = indent;
		n = 4;
		break;
	case 'o':	/* print postscript file */
		/*
		 * Treat this as a "plain file with control characters", and
		 * assume the standard LPF_INPUT filter will recognize that
		 * the data is postscript and know what to do with it.  These
		 * 'o'-file requests could come from MacOS 10.1 systems.
		 * (later versions of MacOS 10 will explicitly use 'l')
		 * A postscript file can contain binary data, which is why 'l'
		 * is somewhat more appropriate than 'f'.
		 */
		/* FALLTHROUGH */
	case 'l':	/* like 'f' but pass control characters */
		prog = pp->filters[LPF_INPUT];
		av[1] = "-c";
		av[2] = width;
		av[3] = length;
		av[4] = indent;
		n = 5;
		break;
	case 'r':	/* print a fortran text file */
		prog = pp->filters[LPF_FORTRAN];
		av[1] = width;
		av[2] = length;
		n = 3;
		break;
	case 't':	/* print troff output */
	case 'n':	/* print ditroff output */
	case 'd':	/* print tex output */
		(void) unlink(".railmag");
		if ((fo = creat(".railmag", FILMOD)) < 0) {
			syslog(LOG_ERR, "%s: cannot create .railmag", 
			    pp->printer);
			(void) unlink(".railmag");
		} else {
			for (n = 0; n < 4; n++) {
				if (fonts[n][0] != '/')
					(void) write(fo, _PATH_VFONT,
					    sizeof(_PATH_VFONT) - 1);
				(void) write(fo, fonts[n], strlen(fonts[n]));
				(void) write(fo, "\n", 1);
			}
			(void) close(fo);
		}
		prog = (format == 't') ? pp->filters[LPF_TROFF] 
			: ((format == 'n') ? pp->filters[LPF_DITROFF]
			   : pp->filters[LPF_DVI]);
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	case 'c':	/* print cifplot output */
		prog = pp->filters[LPF_CIFPLOT];
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	case 'g':	/* print plot(1G) output */
		prog = pp->filters[LPF_GRAPH];
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	case 'v':	/* print raster output */
		prog = pp->filters[LPF_RASTER];
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	default:
		(void) close(fi);
		syslog(LOG_ERR, "%s: illegal format character '%c'",
		    pp->printer, format);
		return (ERROR);
	}
	if (prog == NULL) {
		(void) close(fi);
		syslog(LOG_ERR,
		   "%s: no filter found in printcap for format character '%c'",
		   pp->printer, format);
		return (ERROR);
	}
	if ((av[0] = strrchr(prog, '/')) != NULL)
		av[0]++;
	else
		av[0] = prog;
	av[n++] = "-n";
	av[n++] = logname;
	av[n++] = "-h";
	av[n++] = origin_host;
	av[n++] = pp->acct_file;
	av[n] = NULL;
	fo = pfd;
	if (of_pid > 0) {		/* stop output filter */
		write(ofd, "\031\1", 2);
		while ((wpid =
		    wait3(&wstatus, WUNTRACED, 0)) > 0 && wpid != of_pid)
			;
		if (wpid < 0)
			syslog(LOG_WARNING,
			    "%s: after stopping 'of', wait3() returned: %m",
			    pp->printer);
		else if (!WIFSTOPPED(wstatus)) {
			(void) close(fi);
			syslog(LOG_WARNING, "%s: output filter died "
			    "(pid=%d retcode=%d termsig=%d)",
			    pp->printer, of_pid, WEXITSTATUS(wstatus),
			    WTERMSIG(wstatus));
			return (REPRINT);
		}
		stopped++;
	}
start:
	if ((child = dofork(pp, DORETURN)) == 0) { /* child */
		dup2(fi, STDIN_FILENO);
		dup2(fo, STDOUT_FILENO);
		/* setup stderr for the filter (child process)
		 * so it goes to our temporary errors file */
		n = open(tempstderr, O_WRONLY|O_TRUNC, 0664);
		if (n >= 0)
			dup2(n, STDERR_FILENO);
		closelog();
		closeallfds(3);
		execv(prog, av);
		syslog(LOG_ERR, "%s: cannot execv(%s): %m", pp->printer,
		    prog);
		exit(2);
	}
	(void) close(fi);
	wstatus_set = 0;
	if (child < 0)
		retcode = 100;
	else {
		while ((wpid = wait(&wstatus)) > 0 && wpid != child)
			;
		if (wpid < 0) {
			retcode = 100;
			syslog(LOG_WARNING,
			    "%s: after execv(%s), wait() returned: %m",
			    pp->printer, prog);
		} else {
			wstatus_set = 1;
			retcode = WEXITSTATUS(wstatus);
		}
	}
	child = 0;
	prchild = 0;
	if (stopped) {		/* restart output filter */
		if (kill(of_pid, SIGCONT) < 0) {
			syslog(LOG_ERR, "cannot restart output filter");
			exit(1);
		}
	}
	pp->tof = 0;

	/* Copy the filter's output to "lf" logfile */
	if ((fp = fopen(tempstderr, "r"))) {
		while (fgets(buf, sizeof(buf), fp))
			fputs(buf, stderr);
		fclose(fp);
	}

	if (wstatus_set && !WIFEXITED(wstatus)) {
		syslog(LOG_WARNING, "%s: filter '%c' terminated (termsig=%d)",
		    pp->printer, format, WTERMSIG(wstatus));
		return (ERROR);
	}
	switch (retcode) {
	case 0:
		pp->tof = 1;
		return (OK);
	case 1:
		return (REPRINT);
	case 2:
		return (ERROR);
	default:
		syslog(LOG_WARNING, "%s: filter '%c' exited (retcode=%d)",
		    pp->printer, format, retcode);
		return (FILTERERR);
	}
}

/*
 * Send the daemon control file (cf) and any data files.
 * Return -1 if a non-recoverable error occurred, 1 if a recoverable error and
 * 0 if all is well.
 */
static int
sendit(struct printer *pp, char *file)
{
	int dfcopies, err, i;
	char *cp, last[sizeof(line)];

	/*
	 * open control file
	 */
	if ((cfp = fopen(file, "r")) == NULL)
		return (OK);

	/* initialize job-specific count of datafiles processed */
	job_dfcnt = 0;

	/*
	 *      read the control file for work to do
	 *
	 *      file format -- first character in the line is a command
	 *      rest of the line is the argument.
	 *      commands of interest are:
	 *
	 *            a-z -- "file name" name of file to print
	 *              U -- "unlink" name of file to remove
	 *                    (after we print it. (Pass 2 only)).
	 */

	/*
	 * pass 1
	 */
	err = OK;
	while (get_line(cfp)) {
	again:
		if (line[0] == 'S') {
			cp = line+1;
			i = 0;
			while (*cp >= '0' && *cp <= '9')
				i = i * 10 + (*cp++ - '0');
			fdev = i;
			cp++;
			i = 0;
			while (*cp >= '0' && *cp <= '9')
				i = i * 10 + (*cp++ - '0');
			fino = i;
		} else if (line[0] == 'H') {
			strlcpy(origin_host, line + 1, sizeof(origin_host));
			if (class[0] == '\0') {
				strlcpy(class, line + 1, sizeof(class));
			}
		} else if (line[0] == 'P') {
			strlcpy(logname, line + 1, sizeof(logname));
			if (pp->restricted) { /* restricted */
				if (getpwnam(logname) == NULL) {
					sendmail(pp, line+1, NOACCT);
					err = ERROR;
					break;
				}
			}
		} else if (line[0] == 'I') {
			strlcpy(indent+2, line + 1, sizeof(indent) - 2);
		} else if (line[0] >= 'a' && line[0] <= 'z') {
			dfcopies = 1;
			strcpy(last, line);
			while ((i = get_line(cfp)) != 0) {
				if (strcmp(last, line) != 0)
					break;
				dfcopies++;
			}
			switch (sendfile(pp, '\3', last+1, *last, dfcopies)) {
			case OK:
				if (i)
					goto again;
				break;
			case REPRINT:
				(void) fclose(cfp);
				return (REPRINT);
			case ACCESS:
				sendmail(pp, logname, ACCESS);
			case ERROR:
				err = ERROR;
			}
			break;
		}
	}
	if (err == OK && sendfile(pp, '\2', file, '\0', 1) > 0) {
		(void) fclose(cfp);
		return (REPRINT);
	}
	/*
	 * pass 2
	 */
	fseek(cfp, 0L, 0);
	while (get_line(cfp))
		if (line[0] == 'U' && !strchr(line+1, '/'))
			(void) unlink(line+1);
	/*
	 * clean-up in case another control file exists
	 */
	(void) fclose(cfp);
	(void) unlink(file);
	return (err);
}

/*
 * Send a data file to the remote machine and spool it.
 * Return positive if we should try resending.
 */
static int
sendfile(struct printer *pp, int type, char *file, char format, int copyreq)
{
	int i, amt;
	struct stat stb;
	char *av[15], *filtcmd;
	char buf[SPL_BUFSIZ], opt_c[4], opt_h[4], opt_n[4];
	int copycnt, filtstat, narg, resp, sfd, sfres, sizerr, statrc;

	/* Make sure the entire data file has arrived. */
	wait4data(pp, file);

	statrc = lstat(file, &stb);
	if (statrc < 0) {
		syslog(LOG_ERR, "%s: error from lstat(%s): %m",
		    pp->printer, file);
		return (ERROR);
	}
	sfd = open(file, O_RDONLY);
	if (sfd < 0) {
		syslog(LOG_ERR, "%s: error from open(%s,O_RDONLY): %m",
		    pp->printer, file);
		return (ERROR);
	}
	/*
	 * Check to see if data file is a symbolic link. If so, it should
	 * still point to the same file or someone is trying to print something
	 * he shouldn't.
	 */
	if ((stb.st_mode & S_IFMT) == S_IFLNK && fstat(sfd, &stb) == 0 &&
	    (stb.st_dev != fdev || stb.st_ino != fino)) {
		close(sfd);
		return (ACCESS);
	}

	/* Everything seems OK for reading the file, now to send it */
	filtcmd = NULL;
	sizerr = 0;
	tfd = -1;
	if (type == '\3') {
		/*
		 * Type == 3 means this is a datafile, not a control file.
		 * Increment the counter of data-files in this job, and
		 * then check for input or output filters (which are only
		 * applied to datafiles, not control files).
		 */
		job_dfcnt++;

		/*
		 * Note that here we are filtering datafiles, one at a time,
		 * as they are sent to the remote machine.  Here, the *only*
		 * difference between an input filter (`if=') and an output
		 * filter (`of=') is the argument list that the filter is
		 * started up with.  Here, the output filter is executed
		 * for each individual file as it is sent.  This is not the
		 * same as local print queues, where the output filter is
		 * started up once, and then all jobs are passed thru that
		 * single invocation of the output filter.
		 *
		 * Also note that a queue for a remote-machine can have an
		 * input filter or an output filter, but not both.
		 */
		if (pp->filters[LPF_INPUT]) {
			filtcmd = pp->filters[LPF_INPUT];
			av[0] = filtcmd;
			narg = 0;
			strcpy(opt_c, "-c");
			strcpy(opt_h, "-h");
			strcpy(opt_n, "-n");
			if (format == 'l')
				av[++narg] = opt_c;
			av[++narg] = width;
			av[++narg] = length;
			av[++narg] = indent;
			av[++narg] = opt_n;
			av[++narg] = logname;
			av[++narg] = opt_h;
			av[++narg] = origin_host;
			av[++narg] = pp->acct_file;
			av[++narg] = NULL;
		} else if (pp->filters[LPF_OUTPUT]) {
			filtcmd = pp->filters[LPF_OUTPUT];
			av[0] = filtcmd;
			narg = 0;
			av[++narg] = width;
			av[++narg] = length;
			av[++narg] = NULL;
		}
	}
	if (filtcmd) {
		/*
		 * If there is an input or output filter, we have to run
		 * the datafile thru that filter and store the result as
		 * a temporary spool file, because the protocol requires
		 * that we send the remote host the file-size before we
		 * start to send any of the data.
		 */
		strcpy(tfile, TFILENAME);
		tfd = mkstemp(tfile);
		if (tfd == -1) {
			syslog(LOG_ERR, "%s: mkstemp(%s): %m", pp->printer,
			    TFILENAME);
			sfres = ERROR;
			goto return_sfres;
		}
		filtstat = execfilter(pp, filtcmd, av, sfd, tfd);

		/* process the return-code from the filter */
		switch (filtstat) {
		case 0:
			break;
		case 1:
			sfres = REPRINT;
			goto return_sfres;
		case 2:
			sfres = ERROR;
			goto return_sfres;
		default:
			syslog(LOG_WARNING,
			    "%s: filter '%c' exited (retcode=%d)",
			    pp->printer, format, filtstat);
			sfres = FILTERERR;
			goto return_sfres;
		}
		statrc = fstat(tfd, &stb);   /* to find size of tfile */
		if (statrc < 0)	{
			syslog(LOG_ERR,
			    "%s: error processing 'if', fstat(%s): %m",
			    pp->printer, tfile);
			sfres = ERROR;
			goto return_sfres;
		}
		close(sfd);
		sfd = tfd;
		lseek(sfd, 0, SEEK_SET);
	}

	copycnt = 0;
sendagain:
	copycnt++;

	if (copycnt < 2)
		(void) sprintf(buf, "%c%" PRId64 " %s\n", type, stb.st_size,
		    file);
	else
		(void) sprintf(buf, "%c%" PRId64 " %s_c%d\n", type, stb.st_size,
		    file, copycnt);
	amt = strlen(buf);
	for (i = 0;  ; i++) {
		if (write(pfd, buf, amt) != amt ||
		    (resp = response(pp)) < 0 || resp == '\1') {
			sfres = REPRINT;
			goto return_sfres;
		} else if (resp == '\0')
			break;
		if (i == 0)
			pstatus(pp,
				"no space on remote; waiting for queue to drain");
		if (i == 10)
			syslog(LOG_ALERT, "%s: can't send to %s; queue full",
			    pp->printer, pp->remote_host);
		sleep(5 * 60);
	}
	if (i)
		pstatus(pp, "sending to %s", pp->remote_host);
	/*
	 * XXX - we should change trstat_init()/trstat_write() to include
	 *	 the copycnt in the statistics record it may write.
	 */
	if (type == '\3')
		trstat_init(pp, file, job_dfcnt);
	for (i = 0; i < stb.st_size; i += SPL_BUFSIZ) {
		amt = SPL_BUFSIZ;
		if (i + amt > stb.st_size)
			amt = stb.st_size - i;
		if (sizerr == 0 && read(sfd, buf, amt) != amt)
			sizerr = 1;
		if (write(pfd, buf, amt) != amt) {
			sfres = REPRINT;
			goto return_sfres;
		}
	}

	if (sizerr) {
		syslog(LOG_INFO, "%s: %s: changed size", pp->printer, file);
		/* tell recvjob to ignore this file */
		(void) write(pfd, "\1", 1);
		sfres = ERROR;
		goto return_sfres;
	}
	if (write(pfd, "", 1) != 1 || response(pp)) {
		sfres = REPRINT;
		goto return_sfres;
	}
	if (type == '\3') {
		trstat_write(pp, TR_SENDING, stb.st_size, logname,
		    pp->remote_host, origin_host);
		/*
		 * Usually we only need to send one copy of a datafile,
		 * because the control-file will simply print the same
		 * file multiple times.  However, some printers ignore
		 * the control file, and simply print each data file as
		 * it arrives.  For such "remote hosts", we need to
		 * transfer the same data file multiple times.  Such a
		 * a host is indicated by adding 'rc' to the printcap
		 * entry.
		 * XXX - Right now this ONLY works for remote hosts which
		 *	do ignore the name of the data file, because
		 *	this sends the file multiple times with slight
		 *	changes to the filename.  To do this right would
		 *	require that we also rewrite the control file
		 *	to match those filenames.
		 */
		if (pp->resend_copies && (copycnt < copyreq)) {
			lseek(sfd, 0, SEEK_SET);
			goto sendagain;
		}
	}
	sfres = OK;

return_sfres:
	(void)close(sfd);
	if (tfd != -1) {
		/*
		 * If tfd is set, then it is the same value as sfd, and
		 * therefore it is already closed at this point.  All
		 * we need to do is remove the temporary file.
		 */
		tfd = -1;
		unlink(tfile);
	}
	return (sfres);
}

/*
 * Some print servers send the control-file first, and then start sending the
 * matching data file(s).  That is not the correct order.  If some queue is
 * already printing an active job, then when that job is finished the queue
 * may proceed to the control file of any incoming print job.  This turns
 * into a race between the process which is receiving the data file, and the
 * process which is actively printing the very same file.  When the remote
 * server sends files in the wrong order, it is even possible that a queue
 * will start to print a data file before the file has been created!
 *
 * So before we start to print() or send() a data file, we call this routine
 * to make sure the data file is not still changing in size.  Note that this
 * problem will only happen for jobs arriving from a remote host, and that
 * the process which has decided to print this job (and is thus making this
 * check) is *not* the process which is receiving the job.
 *
 * A second benefit of this is that any incoming job is guaranteed to appear
 * in a queue listing for at least a few seconds after it has arrived.  Some
 * lpr implementations get confused if they send a job and it disappears
 * from the queue before they can check on it.
 */
#define	MAXWAIT_ARRIVE	16	    /* max to wait for the file to *exist* */
#define	MAXWAIT_4DATA	(20*60)	    /* max to wait for it to stop changing */
#define	MINWAIT_4DATA	4	    /* This value must be >= 1 */
#define	DEBUG_MINWAIT	1
static void
wait4data(struct printer *pp, const char *dfile)
{
	const char *cp;
	int statres;
	u_int sleepreq;
	size_t dlen, hlen;
	time_t amtslept, cur_time, prev_mtime;
	struct stat statdf;

	/* Skip these checks if the print job is from the local host. */
	dlen = strlen(dfile);
	hlen = strlen(local_host);
	if (dlen > hlen) {
		cp = dfile + dlen - hlen;
		if (strcmp(cp, local_host) == 0)
			return;
	}

	/*
	 * If this data file does not exist, then wait up to MAXWAIT_ARRIVE
	 * seconds for it to arrive.
	 */
	amtslept = 0;
	statres = stat(dfile, &statdf);
	while (statres < 0 && amtslept < MAXWAIT_ARRIVE) {
		if (amtslept == 0)
			pstatus(pp, "Waiting for data file from remote host");
		amtslept += MINWAIT_4DATA - sleep(MINWAIT_4DATA);
		statres = stat(dfile, &statdf);
	}
	if (statres < 0) {
		/* The file still does not exist, so just give up on it. */
		syslog(LOG_WARNING, "%s: wait4data() abandoned wait for %s",
		    pp->printer, dfile);
		return;
	}

	/*
	 * The file exists, so keep waiting until the data file has not
	 * changed for some reasonable amount of time.  Extra care is
	 * taken when computing wait-times, just in case there are data
	 * files with a last-modify time in the future.  While that is
	 * very unlikely to happen, it can happen when the system has
	 * a flakey time-of-day clock.
	 */
	prev_mtime = statdf.st_mtime;
	cur_time = time(NULL);
	if (statdf.st_mtime >= cur_time - MINWAIT_4DATA) {
		if (statdf.st_mtime >= cur_time)	/* some TOD oddity */
			sleepreq = MINWAIT_4DATA;
		else
			sleepreq = cur_time - statdf.st_mtime;
		if (amtslept == 0)
			pstatus(pp, "Waiting for data file from remote host");
		amtslept += sleepreq - sleep(sleepreq);
		statres = stat(dfile, &statdf);
	}
	sleepreq = MINWAIT_4DATA;
	while (statres == 0 && amtslept < MAXWAIT_4DATA) {
		if (statdf.st_mtime == prev_mtime)
			break;
		prev_mtime = statdf.st_mtime;
		amtslept += sleepreq - sleep(sleepreq);
		statres = stat(dfile, &statdf);
	}

	if (statres != 0)
		syslog(LOG_WARNING, "%s: %s disappeared during wait4data()",
		    pp->printer, dfile);
	else if (amtslept > MAXWAIT_4DATA)
		syslog(LOG_WARNING,
		    "%s: %s still changing after %lu secs in wait4data()",
		    pp->printer, dfile, (unsigned long)amtslept);
#if DEBUG_MINWAIT
	else if (amtslept > MINWAIT_4DATA)
		syslog(LOG_INFO, "%s: slept %lu secs in wait4data(%s)",
		    pp->printer, (unsigned long)amtslept, dfile);
#endif
}
#undef	MAXWAIT_ARRIVE
#undef	MAXWAIT_4DATA
#undef	MINWAIT_4DATA

/*
 *  This routine is called to execute one of the filters as was
 *  specified in a printcap entry.  While the child-process will read
 *  all of 'infd', it is up to the caller to close that file descriptor
 *  in the parent process.
 */
static int
execfilter(struct printer *pp, char *f_cmd, char *f_av[], int infd, int outfd)
{
	pid_t fpid, wpid;
	int errfd, retcode, wstatus;
	FILE *errfp;
	char buf[BUFSIZ], *slash;

	fpid = dofork(pp, DORETURN);
	if (fpid != 0) {
		/*
		 * This is the parent process, which just waits for the child
		 * to complete and then returns the result.  Note that it is
		 * the child process which reads the input stream.
		 */
		if (fpid < 0)
			retcode = 100;
		else {
			while ((wpid = wait(&wstatus)) > 0 &&
			    wpid != fpid)
				;
			if (wpid < 0) {
				retcode = 100;
				syslog(LOG_WARNING,
				    "%s: after execv(%s), wait() returned: %m",
				    pp->printer, f_cmd);
			} else
				retcode = WEXITSTATUS(wstatus);
		}

		/*
		 * Copy everything the filter wrote to stderr from our
		 * temporary errors file to the "lf=" logfile.
		 */
		errfp = fopen(tempstderr, "r");
		if (errfp) {
			while (fgets(buf, sizeof(buf), errfp))
				fputs(buf, stderr);
			fclose(errfp);
		}

		return (retcode);
	}

	/*
	 * This is the child process, which is the one that executes the
	 * given filter.
	 */
	/*
	 * If the first parameter has any slashes in it, then change it
	 * to point to the first character after the last slash.
	 */
	slash = strrchr(f_av[0], '/');
	if (slash != NULL)
		f_av[0] = slash + 1;
	/*
	 * XXX - in the future, this should setup an explicit list of
	 *       environment variables and use execve()!
	 */

	/*
	 * Setup stdin, stdout, and stderr as we want them when the filter
	 * is running.  Stderr is setup so it points to a temporary errors
	 * file, and the parent process will copy that temporary file to
	 * the real logfile after the filter completes.
	 */
	dup2(infd, STDIN_FILENO);
	dup2(outfd, STDOUT_FILENO);
	errfd = open(tempstderr, O_WRONLY|O_TRUNC, 0664);
	if (errfd >= 0)
		dup2(errfd, STDERR_FILENO);
	closelog();
	closeallfds(3);
	execv(f_cmd, f_av);
	syslog(LOG_ERR, "%s: cannot execv(%s): %m", pp->printer, f_cmd);
	exit(2);
	/* NOTREACHED */
}

/*
 * Check to make sure there have been no errors and that both programs
 * are in sync with eachother.
 * Return non-zero if the connection was lost.
 */
static char
response(const struct printer *pp)
{
	char resp;

	if (read(pfd, &resp, 1) != 1) {
		syslog(LOG_INFO, "%s: lost connection", pp->printer);
		return (-1);
	}
	return (resp);
}

/*
 * Banner printing stuff
 */
static void
banner(struct printer *pp, char *name1, char *name2)
{
	time_t tvec;

	time(&tvec);
	if (!pp->no_formfeed && !pp->tof)
		(void) write(ofd, pp->form_feed, strlen(pp->form_feed));
	if (pp->short_banner) {	/* short banner only */
		if (class[0]) {
			(void) write(ofd, class, strlen(class));
			(void) write(ofd, ":", 1);
		}
		(void) write(ofd, name1, strlen(name1));
		(void) write(ofd, "  Job: ", 7);
		(void) write(ofd, name2, strlen(name2));
		(void) write(ofd, "  Date: ", 8);
		(void) write(ofd, ctime(&tvec), 24);
		(void) write(ofd, "\n", 1);
	} else {	/* normal banner */
		(void) write(ofd, "\n\n\n", 3);
		scan_out(pp, ofd, name1, '\0');
		(void) write(ofd, "\n\n", 2);
		scan_out(pp, ofd, name2, '\0');
		if (class[0]) {
			(void) write(ofd,"\n\n\n",3);
			scan_out(pp, ofd, class, '\0');
		}
		(void) write(ofd, "\n\n\n\n\t\t\t\t\tJob:  ", 15);
		(void) write(ofd, name2, strlen(name2));
		(void) write(ofd, "\n\t\t\t\t\tDate: ", 12);
		(void) write(ofd, ctime(&tvec), 24);
		(void) write(ofd, "\n", 1);
	}
	if (!pp->no_formfeed)
		(void) write(ofd, pp->form_feed, strlen(pp->form_feed));
	pp->tof = 1;
}

static char *
scnline(int key, char *p, int c)
{
	register int scnwidth;

	for (scnwidth = WIDTH; --scnwidth;) {
		key <<= 1;
		*p++ = key & 0200 ? c : BACKGND;
	}
	return (p);
}

#define TRC(q)	(((q)-' ')&0177)

static void
scan_out(struct printer *pp, int scfd, char *scsp, int dlm)
{
	register char *strp;
	register int nchrs, j;
	char outbuf[LINELEN+1], *sp, c, cc;
	int d, scnhgt;

	for (scnhgt = 0; scnhgt++ < HEIGHT+DROP; ) {
		strp = &outbuf[0];
		sp = scsp;
		for (nchrs = 0; ; ) {
			d = dropit(c = TRC(cc = *sp++));
			if ((!d && scnhgt > HEIGHT) || (scnhgt <= DROP && d))
				for (j = WIDTH; --j;)
					*strp++ = BACKGND;
			else
				strp = scnline(scnkey[(int)c][scnhgt-1-d], strp, cc);
			if (*sp == dlm || *sp == '\0' || 
			    nchrs++ >= pp->page_width/(WIDTH+1)-1)
				break;
			*strp++ = BACKGND;
			*strp++ = BACKGND;
		}
		while (*--strp == BACKGND && strp >= outbuf)
			;
		strp++;
		*strp++ = '\n';
		(void) write(scfd, outbuf, strp-outbuf);
	}
}

static int
dropit(int c)
{
	switch(c) {

	case TRC('_'):
	case TRC(';'):
	case TRC(','):
	case TRC('g'):
	case TRC('j'):
	case TRC('p'):
	case TRC('q'):
	case TRC('y'):
		return (DROP);

	default:
		return (0);
	}
}

/*
 * sendmail ---
 *   tell people about job completion
 */
static void
sendmail(struct printer *pp, char *userid, int bombed)
{
	register int i;
	int p[2], s;
	register const char *cp;
	struct stat stb;
	FILE *fp;

	pipe(p);
	if ((s = dofork(pp, DORETURN)) == 0) {		/* child */
		dup2(p[0], STDIN_FILENO);
		closelog();
		closeallfds(3);
		if ((cp = strrchr(_PATH_SENDMAIL, '/')) != NULL)
			cp++;
		else
			cp = _PATH_SENDMAIL;
		execl(_PATH_SENDMAIL, cp, "-t", (char *)0);
		_exit(0);
	} else if (s > 0) {				/* parent */
		dup2(p[1], STDOUT_FILENO);
		printf("To: %s@%s\n", userid, origin_host);
		printf("Subject: %s printer job \"%s\"\n", pp->printer,
			*jobname ? jobname : "<unknown>");
		printf("Reply-To: root@%s\n\n", local_host);
		printf("Your printer job ");
		if (*jobname)
			printf("(%s) ", jobname);

		switch (bombed) {
		case OK:
			cp = "OK";
			printf("\ncompleted successfully\n");
			break;
		default:
		case FATALERR:
			cp = "FATALERR";
			printf("\ncould not be printed\n");
			break;
		case NOACCT:
			cp = "NOACCT";
			printf("\ncould not be printed without an account on %s\n",
			    local_host);
			break;
		case FILTERERR:
			cp = "FILTERERR";
			if (stat(tempstderr, &stb) < 0 || stb.st_size == 0
			    || (fp = fopen(tempstderr, "r")) == NULL) {
				printf("\nhad some errors and may not have printed\n");
				break;
			}
			printf("\nhad the following errors and may not have printed:\n");
			while ((i = getc(fp)) != EOF)
				putchar(i);
			(void) fclose(fp);
			break;
		case ACCESS:
			cp = "ACCESS";
			printf("\nwas not printed because it was not linked to the original file\n");
		}
		fflush(stdout);
		(void) close(STDOUT_FILENO);
	} else {
		syslog(LOG_WARNING, "unable to send mail to %s: %m", userid);
		return;
	}
	(void) close(p[0]);
	(void) close(p[1]);
	wait(NULL);
	syslog(LOG_INFO, "mail sent to user %s about job %s on printer %s (%s)",
	    userid, *jobname ? jobname : "<unknown>", pp->printer, cp);
}

/*
 * dofork - fork with retries on failure
 */
static int
dofork(const struct printer *pp, int action)
{
	pid_t forkpid;
	int i, fail;
	struct passwd *pwd;

	forkpid = -1;
	if (daemon_uname == NULL) {
		pwd = getpwuid(pp->daemon_user);
		if (pwd == NULL) {
			syslog(LOG_ERR, "%s: Can't lookup default daemon uid (%ld) in password file",
			    pp->printer, pp->daemon_user);
			goto error_ret;
		}
		daemon_uname = strdup(pwd->pw_name);
		daemon_defgid = pwd->pw_gid;
	}

	for (i = 0; i < 20; i++) {
		forkpid = fork();
		if (forkpid < 0) {
			sleep((unsigned)(i*i));
			continue;
		}
		/*
		 * Child should run as daemon instead of root
		 */
		if (forkpid == 0) {
			errno = 0;
			fail = initgroups(daemon_uname, daemon_defgid);
			if (fail) {
				syslog(LOG_ERR, "%s: initgroups(%s,%u): %m",
				    pp->printer, daemon_uname, daemon_defgid);
				break;
			}
			fail = setgid(daemon_defgid);
			if (fail) {
				syslog(LOG_ERR, "%s: setgid(%u): %m",
				    pp->printer, daemon_defgid);
				break;
			}
			fail = setuid(pp->daemon_user);
			if (fail) {
				syslog(LOG_ERR, "%s: setuid(%ld): %m",
				    pp->printer, pp->daemon_user);
				break;
			}
		}
		return (forkpid);
	}

	/*
	 * An error occurred.  If the error is in the child process, then
	 * this routine MUST always exit().  DORETURN only effects how
	 * errors should be handled in the parent process.
	 */
error_ret:
	if (forkpid == 0) {
		syslog(LOG_ERR, "%s: dofork(): aborting child process...",
		    pp->printer);
		exit(1);
	}
	syslog(LOG_ERR, "%s: dofork(): failure in fork", pp->printer);

	sleep(1);		/* throttle errors, as a safety measure */
	switch (action) {
	case DORETURN:
		return (-1);
	default:
		syslog(LOG_ERR, "bad action (%d) to dofork", action);
		/* FALLTHROUGH */
	case DOABORT:
		exit(1);
	}
	/*NOTREACHED*/
}

/*
 * Kill child processes to abort current job.
 */
static void
abortpr(int signo __unused)
{

	(void) unlink(tempstderr);
	kill(0, SIGINT);
	if (of_pid > 0)
		kill(of_pid, SIGCONT);
	while (wait(NULL) > 0)
		;
	if (of_pid > 0 && tfd != -1)
		unlink(tfile);
	exit(0);
}

static void
init(struct printer *pp)
{
	char *s;

	sprintf(&width[2], "%ld", pp->page_width);
	sprintf(&length[2], "%ld", pp->page_length);
	sprintf(&pxwidth[2], "%ld", pp->page_pwidth);
	sprintf(&pxlength[2], "%ld", pp->page_plength);
	if ((s = checkremote(pp)) != NULL) {
		syslog(LOG_WARNING, "%s", s);
		free(s);
	}
}

void
startprinting(const char *printer)
{
	struct printer myprinter, *pp = &myprinter;
	int status;

	init_printer(pp);
	status = getprintcap(printer, pp);
	switch(status) {
	case PCAPERR_OSERR:
		syslog(LOG_ERR, "can't open printer description file: %m");
		exit(1);
	case PCAPERR_NOTFOUND:
		syslog(LOG_ERR, "unknown printer: %s", printer);
		exit(1);
	case PCAPERR_TCLOOP:
		fatal(pp, "potential reference loop detected in printcap file");
	default:
		break;
	}
	printjob(pp);
}

/*
 * Acquire line printer or remote connection.
 */
static void
openpr(const struct printer *pp)
{
	int p[2];
	char *cp;

	if (pp->remote) {
		openrem(pp);
		/*
		 * Lpd does support the setting of 'of=' filters for
		 * jobs going to remote machines, but that does not
		 * have the same meaning as 'of=' does when handling
		 * local print queues.  For remote machines, all 'of='
		 * filter processing is handled in sendfile(), and that
		 * does not use these global "output filter" variables.
		 */ 
		ofd = -1;
		of_pid = 0;
		return;
	} else if (*pp->lp) {
		if (strchr(pp->lp, '@') != NULL)
			opennet(pp);
		else
			opentty(pp);
	} else {
		syslog(LOG_ERR, "%s: no line printer device or host name",
		    pp->printer);
		exit(1);
	}

	/*
	 * Start up an output filter, if needed.
	 */
	if (pp->filters[LPF_OUTPUT] && !pp->filters[LPF_INPUT] && !of_pid) {
		pipe(p);
		if (pp->remote) {
			strcpy(tfile, TFILENAME);
			tfd = mkstemp(tfile);
		}
		if ((of_pid = dofork(pp, DOABORT)) == 0) {	/* child */
			dup2(p[0], STDIN_FILENO);	/* pipe is std in */
			/* tfile/printer is stdout */
			dup2(pp->remote ? tfd : pfd, STDOUT_FILENO);
			closelog();
			closeallfds(3);
			if ((cp = strrchr(pp->filters[LPF_OUTPUT], '/')) == NULL)
				cp = pp->filters[LPF_OUTPUT];
			else
				cp++;
			execl(pp->filters[LPF_OUTPUT], cp, width, length,
			      (char *)0);
			syslog(LOG_ERR, "%s: execl(%s): %m", pp->printer,
			    pp->filters[LPF_OUTPUT]);
			exit(1);
		}
		(void) close(p[0]);		/* close input side */
		ofd = p[1];			/* use pipe for output */
	} else {
		ofd = pfd;
		of_pid = 0;
	}
}

/*
 * Printer connected directly to the network
 * or to a terminal server on the net
 */
static void
opennet(const struct printer *pp)
{
	register int i;
	int resp;
	u_long port;
	char *ep;
	void (*savealrm)(int);

	port = strtoul(pp->lp, &ep, 0);
	if (*ep != '@' || port > 65535) {
		syslog(LOG_ERR, "%s: bad port number: %s", pp->printer,
		    pp->lp);
		exit(1);
	}
	ep++;

	for (i = 1; ; i = i < 256 ? i << 1 : i) {
		resp = -1;
		savealrm = signal(SIGALRM, alarmhandler);
		alarm(pp->conn_timeout);
		pfd = getport(pp, ep, port);
		alarm(0);
		(void)signal(SIGALRM, savealrm);
		if (pfd < 0 && errno == ECONNREFUSED)
			resp = 1;
		else if (pfd >= 0) {
			/*
			 * need to delay a bit for rs232 lines
			 * to stabilize in case printer is
			 * connected via a terminal server
			 */
			delay(500);
			break;
		}
		if (i == 1) {
			if (resp < 0)
				pstatus(pp, "waiting for %s to come up",
					pp->lp);
			else
				pstatus(pp, 
					"waiting for access to printer on %s",
					pp->lp);
		}
		sleep(i);
	}
	pstatus(pp, "sending to %s port %lu", ep, port);
}

/*
 * Printer is connected to an RS232 port on this host
 */
static void
opentty(const struct printer *pp)
{
	register int i;

	for (i = 1; ; i = i < 32 ? i << 1 : i) {
		pfd = open(pp->lp, pp->rw ? O_RDWR : O_WRONLY);
		if (pfd >= 0) {
			delay(500);
			break;
		}
		if (errno == ENOENT) {
			syslog(LOG_ERR, "%s: %m", pp->lp);
			exit(1);
		}
		if (i == 1)
			pstatus(pp, 
				"waiting for %s to become ready (offline?)",
				pp->printer);
		sleep(i);
	}
	if (isatty(pfd))
		setty(pp);
	pstatus(pp, "%s is ready and printing", pp->printer);
}

/*
 * Printer is on a remote host
 */
static void
openrem(const struct printer *pp)
{
	register int i;
	int resp;
	void (*savealrm)(int);

	for (i = 1; ; i = i < 256 ? i << 1 : i) {
		resp = -1;
		savealrm = signal(SIGALRM, alarmhandler);
		alarm(pp->conn_timeout);
		pfd = getport(pp, pp->remote_host, 0);
		alarm(0);
		(void)signal(SIGALRM, savealrm);
		if (pfd >= 0) {
			if ((writel(pfd, "\2", pp->remote_queue, "\n", 
				    (char *)0)
			     == 2 + strlen(pp->remote_queue))
			    && (resp = response(pp)) == 0)
				break;
			(void) close(pfd);
		}
		if (i == 1) {
			if (resp < 0)
				pstatus(pp, "waiting for %s to come up", 
					pp->remote_host);
			else {
				pstatus(pp,
					"waiting for queue to be enabled on %s",
					pp->remote_host);
				i = 256;
			}
		}
		sleep(i);
	}
	pstatus(pp, "sending to %s", pp->remote_host);
}

/*
 * setup tty lines.
 */
static void
setty(const struct printer *pp)
{
	struct termios ttybuf;

	if (ioctl(pfd, TIOCEXCL, (char *)0) < 0) {
		syslog(LOG_ERR, "%s: ioctl(TIOCEXCL): %m", pp->printer);
		exit(1);
	}
	if (tcgetattr(pfd, &ttybuf) < 0) {
		syslog(LOG_ERR, "%s: tcgetattr: %m", pp->printer);
		exit(1);
	}
	if (pp->baud_rate > 0)
		cfsetspeed(&ttybuf, pp->baud_rate);
	if (pp->mode_set) {
		char *s = strdup(pp->mode_set), *tmp;

		while ((tmp = strsep(&s, ",")) != NULL) {
			(void) msearch(tmp, &ttybuf);
		}
	}
	if (pp->mode_set != 0 || pp->baud_rate > 0) {
		if (tcsetattr(pfd, TCSAFLUSH, &ttybuf) == -1) {
			syslog(LOG_ERR, "%s: tcsetattr: %m", pp->printer);
		}
	}
}

#include <stdarg.h>

static void
pstatus(const struct printer *pp, const char *msg, ...)
{
	int fd;
	char *buf;
	va_list ap;
	va_start(ap, msg);

	umask(S_IWOTH);
	fd = open(pp->status_file, O_WRONLY|O_CREAT|O_EXLOCK, STAT_FILE_MODE);
	if (fd < 0) {
		syslog(LOG_ERR, "%s: open(%s): %m", pp->printer,
		    pp->status_file);
		exit(1);
	}
	ftruncate(fd, 0);
	vasprintf(&buf, msg, ap);
	va_end(ap);
	writel(fd, buf, "\n", (char *)0);
	close(fd);
	free(buf);
}

void
alarmhandler(int signo __unused)
{
	/* the signal is ignored */
	/* (the '__unused' is just to avoid a compile-time warning) */
}
