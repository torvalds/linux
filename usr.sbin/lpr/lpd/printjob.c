/*	$OpenBSD: printjob.c,v 1.62 2021/10/24 21:24:18 deraadt Exp $	*/
/*	$NetBSD: printjob.c,v 1.31 2002/01/21 14:42:30 wiz Exp $	*/

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
 * printjob -- print jobs in the queue.
 *
 *	NOTE: the lock file is used to pass information to lpq and lprm.
 *	it does not need to be removed because file locks are dynamic.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <syslog.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"
#include "extern.h"

#define DORETURN	0	/* absorb fork error */
#define DOABORT		1	/* abort if dofork fails */

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
static pid_t	 child;		/* pid of any filters */
static int	 lfd;		/* lock file descriptor */
static int	 ofd;		/* output filter file descriptor */
static pid_t	 ofilter;	/* pid of output filter, if any */
static int	 pfd;		/* prstatic inter file descriptor */
static pid_t	 pid;		/* pid of lpd process */
static pid_t	 prchild;	/* pid of pr process */
static char	 title[80];	/* ``pr'' title */
static int	 tof;		/* true if at top of form */

static char	class[32];		/* classification field */
static char	fromhost[HOST_NAME_MAX+1]; /* user's host machine */
				/* indentation size in static characters */
static char	indent[10] = "-i0";
static char	jobname[NAME_MAX];	/* job or file name */
static char	length[10] = "-l";	/* page length in lines */
static char	logname[LOGIN_NAME_MAX];/* user's login name */
static char	pxlength[10] = "-y";	/* page length in pixels */
static char	pxwidth[10] = "-x";	/* page width in pixels */
static char	tempfile[] = "errsXXXXXXXXXX"; /* file name for filter output */
static char	width[10] = "-w";	/* page width in static characters */

static void       abortpr(int);
static void       banner(char *, char *);
static void       delay(int);
static pid_t      dofork(int);
static int        dropit(int);
static void       init(void);
static void       openpr(void);
static void       opennet(char *);
static void       opentty(void);
static void       openrem(void);
static int        print(int, char *);
static int        printit(char *);
static void       pstatus(const char *, ...)
	__attribute__((__format__(__printf__, 1, 2)));
static char       response(void);
static void       scan_out(int, char *, int);
static char      *scnline(int, char *, int);
static int        sendfile(int, char *);
static int        sendit(char *);
static void       sendmail(char *, int);
static void       setty(void);
static void       alarmer(int);

void
printjob(void)
{
	struct stat stb;
	struct queue *q, **qp;
	struct queue **queue;
	struct sigaction sa;
	int i, fd, nitems;
	off_t pidoff;
	int errcnt, count = 0;

	init();					/* set up capabilities */
	(void)write(STDOUT_FILENO, "", 1);	/* ack that daemon is started */
	PRIV_START;
	fd = open(LF, O_WRONLY|O_APPEND);	/* set up log file */
	PRIV_END;
	if (fd < 0) {
		syslog(LOG_ERR, "%s: %m", LF);
		if ((fd = open(_PATH_DEVNULL, O_WRONLY)) < 0)
			exit(1);
	}
	if (fd != STDERR_FILENO) {
		if (dup2(fd, STDERR_FILENO) < 0) {
			syslog(LOG_ERR, "dup2: %m");
			exit(1);
		}
		(void)close(fd);
	}
	setpgid(0, 0);

	/* we add SIGINT to the mask so abortpr() doesn't kill itself */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = abortpr;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* so we can use short form file names */
	if (chdir(SD) < 0) {
		syslog(LOG_ERR, "%s: %m", SD);
		exit(1);
	}

	(void)mktemp(tempfile);			/* safe */

	lfd = safe_open(LO, O_WRONLY|O_CREAT|O_NOFOLLOW|O_EXLOCK, 0640);
	if (lfd < 0) {
		if (errno == EWOULDBLOCK)	/* active daemon present */
			exit(0);
		syslog(LOG_ERR, "%s: %s: %m", printer, LO);
		exit(1);
	}
	if (fstat(lfd, &stb) == 0 && (stb.st_mode & S_IXUSR))
		exit(0);		/* printing disabled */
	ftruncate(lfd, 0);
	/*
	 * write process id for others to know
	 */
	pid = getpid();
	if ((pidoff = i = snprintf(line, sizeof(line), "%d\n", pid)) < 0 ||
	    i >= sizeof(line)) {
		syslog(LOG_ERR, "impossibly large pid: %u", pid);
		exit(1);
	}
	if (write(lfd, line, i) != i) {
		syslog(LOG_ERR, "%s: %s: %m", printer, LO);
		exit(1);
	}
	/*
	 * search the spool directory for work and sort by queue order.
	 */
	if ((nitems = getq(&queue)) < 0) {
		syslog(LOG_ERR, "%s: can't scan %s", printer, SD);
		exit(1);
	}
	if (nitems == 0)		/* no work to do */
		exit(0);
	if (stb.st_mode & S_IXOTH) {		/* reset queue flag */
		stb.st_mode &= ~S_IXOTH;
		if (fchmod(lfd, stb.st_mode & 0777) < 0)
			syslog(LOG_ERR, "%s: %s: %m", printer, LO);
	}
	PRIV_START;
	openpr();			/* open printer or remote */
	PRIV_END;

again:
	/*
	 * we found something to do now do it --
	 *    write the name of the current control file into the lock file
	 *    so the spool queue program can tell what we're working on
	 */
	for (qp = queue; nitems--; free(q)) {
		q = *qp++;
		if (stat(q->q_name, &stb) < 0)
			continue;
		errcnt = 0;
	restart:
		fdev = (dev_t)-1;
		fino = (ino_t)-1;

		(void)lseek(lfd, pidoff, SEEK_SET);
		if ((i = snprintf(line, sizeof(line), "%s\n", q->q_name)) < 0 ||
		    i >= sizeof(line))
			i = sizeof(line) - 1;	/* can't happen */
		if (write(lfd, line, i) != i)
			syslog(LOG_ERR, "%s: %s: %m", printer, LO);
		if (!remote)
			i = printit(q->q_name);
		else
			i = sendit(q->q_name);
		/*
		 * Check to see if we are supposed to stop printing or
		 * if we are to rebuild the queue.
		 */
		if (fstat(lfd, &stb) == 0) {
			/* stop printing before starting next job? */
			if (stb.st_mode & S_IXUSR)
				goto done;
			/* rebuild queue (after lpc topq) */
			if (stb.st_mode & S_IXOTH) {
				for (free(q); nitems--; free(q))
					q = *qp++;
				stb.st_mode &= ~S_IXOTH;
				if (fchmod(lfd, stb.st_mode & 0777) < 0)
					syslog(LOG_WARNING, "%s: %s: %m",
						printer, LO);
				break;
			}
		}
		if (i == OK)		/* file ok and printed */
			count++;
		else if (i == REPRINT && ++errcnt < 5) {
			/* try reprinting the job */
			syslog(LOG_INFO, "restarting %s", printer);
			if (ofilter > 0) {
				kill(ofilter, SIGCONT);	/* to be sure */
				(void)close(ofd);
				while ((i = wait(NULL)) > 0 && i != ofilter)
					;
				ofilter = 0;
			}
			(void)close(pfd);	/* close printer */
			if (ftruncate(lfd, pidoff) < 0)
				syslog(LOG_WARNING, "%s: %s: %m", printer, LO);
			PRIV_START;
			openpr();		/* try to reopen printer */
			PRIV_END;
			goto restart;
		} else {
			syslog(LOG_WARNING, "%s: job could not be %s (%s)", printer,
				remote ? "sent to remote host" : "printed", q->q_name);
			if (i == REPRINT) {
				/* ensure we don't attempt this job again */
				PRIV_START;
				(void)unlink(q->q_name);
				q->q_name[0] = 'd';
				(void)unlink(q->q_name);
				PRIV_END;
				if (logname[0])
					sendmail(logname, FATALERR);
			}
		}
	}
	free(queue);
	/*
	 * search the spool directory for more work.
	 */
	if ((nitems = getq(&queue)) < 0) {
		syslog(LOG_ERR, "%s: can't scan %s", printer, SD);
		exit(1);
	}
	if (nitems == 0) {		/* no more work to do */
	done:
		if (count > 0) {	/* Files actually printed */
			if (!SF && !tof)
				(void)write(ofd, FF, strlen(FF));
			if (TR != NULL)		/* output trailer */
				(void)write(ofd, TR, strlen(TR));
		}
		(void)close(ofd);
		(void)wait(NULL);
		(void)unlink(tempfile);
		exit(0);
	}
	goto again;
}

#define	FONTLEN	50
char	fonts[4][FONTLEN];	/* fonts for troff */

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
printit(char *file)
{
	int i, fd;
	char *cp;
	int bombed = OK;

	/*
	 * open control file; ignore if no longer there.
	 */
	fd = safe_open(file, O_RDONLY|O_NOFOLLOW, 0);
	if (fd < 0 || (cfp = fdopen(fd, "r")) == NULL) {
		syslog(LOG_INFO, "%s: %s: %m", printer, file);
		if (fd >= 0)
			(void)close(fd);
		return(OK);
	}
	/*
	 * Reset troff fonts.
	 */
	for (i = 0; i < 4; i++)
		strlcpy(fonts[i], ifonts[i], FONTLEN);
	(void)snprintf(&width[2], sizeof(width) - 2, "%ld", PW);
	indent[2] = '0';
	indent[3] = '\0';

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
	 *
	 *      get_line reads a line and expands tabs to blanks
	 */

	/* pass 1 */

	while (get_line(cfp))
		switch (line[0]) {
		case 'H':
			strlcpy(fromhost, line+1, sizeof(fromhost));
			if (class[0] == '\0')
				strlcpy(class, line+1, sizeof(class));
			continue;

		case 'P':
			strlcpy(logname, line+1, sizeof(logname));
			if (RS) {			/* restricted */
				if (getpwnam(logname) == NULL) {
					bombed = NOACCT;
					sendmail(line+1, bombed);
					goto pass2;
				}
			}
			continue;

		case 'S':
			cp = line+1;
			fdev = 0;
			while (*cp >= '0' && *cp <= '9')
				fdev = fdev * 10 + (*cp++ - '0');
			cp++;
			fino = 0;
			while (*cp >= '0' && *cp <= '9')
				fino = fino * 10 + (*cp++ - '0');
			continue;

		case 'J':
			if (line[1] != '\0')
				strlcpy(jobname, line+1, sizeof(jobname));
			else {
				jobname[0] = ' ';
				jobname[1] = '\0';
			}
			continue;

		case 'C':
			if (line[1] != '\0')
				strlcpy(class, line+1, sizeof(class));
			else if (class[0] == '\0')
				gethostname(class, sizeof(class));
			continue;

		case 'T':	/* header title for pr */
			strlcpy(title, line+1, sizeof(title));
			continue;

		case 'L':	/* identification line */
			if (!SH && !HL)
				banner(line+1, jobname);
			continue;

		case '1':	/* troff fonts */
		case '2':
		case '3':
		case '4':
			if (line[1] != '\0')
				strlcpy(fonts[line[0]-'1'], line+1, FONTLEN);
			continue;

		case 'W':	/* page width */
			strlcpy(width+2, line+1, sizeof(width) - 2);
			continue;

		case 'I':	/* indent amount */
			strlcpy(indent+2, line+1, sizeof(indent) - 2);
			continue;

		default:	/* some file to print */
			switch (i = print(line[0], line+1)) {
			case ERROR:
				if (bombed == OK)
					bombed = FATALERR;
				break;
			case REPRINT:
				(void)fclose(cfp);
				return(REPRINT);
			case FILTERERR:
			case ACCESS:
				bombed = i;
				sendmail(logname, bombed);
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
	fseek(cfp, 0L, SEEK_SET);
	while (get_line(cfp))
		switch (line[0]) {
		case 'L':	/* identification line */
			if (!SH && HL)
				banner(line+1, jobname);
			continue;

		case 'M':
			if (bombed < NOACCT)	/* already sent if >= NOACCT */
				sendmail(line+1, bombed);
			continue;

		case 'U':
			if (strchr(line+1, '/'))
				continue;
			(void)unlink(line+1);
		}
	/*
	 * clean-up in case another control file exists
	 */
	(void)fclose(cfp);
	(void)unlink(file);
	return(bombed == OK ? OK : ERROR);
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
print(int format, char *file)
{
	ssize_t nread;
	struct stat stb;
	pid_t pid;
	char *prog, *av[17], buf[BUFSIZ];
	int fd, status, serrno;
	int n, fi, fo, p[2], stopped = 0, nofile;

	if (fdev != (dev_t)-1 && fino != (ino_t)-1) {
		/* symbolic link */
		PRIV_START;
		fi = safe_open(file, O_RDONLY, 0);
		PRIV_END;
		if (fi != -1) {
			/*
			 * The symbolic link should still point to the same file
			 * or someone is trying to print something he shouldn't.
			 */
			if (fstat(fi, &stb) == -1 ||
			    stb.st_dev != fdev || stb.st_ino != fino) {
				close(fi);
				return(ACCESS);
			}
		}
	} else {
		/* regular file */
		PRIV_START;
		fi = safe_open(file, O_RDONLY|O_NOFOLLOW, 0);
		PRIV_END;
	}
	if (fi == -1)
		return(ERROR);
	if (!SF && !tof) {		/* start on a fresh page */
		(void)write(ofd, FF, strlen(FF));
		tof = 1;
	}
	if (IF == NULL && (format == 'f' || format == 'l' || format == 'o')) {
		tof = 0;
		while ((n = read(fi, buf, BUFSIZ)) > 0)
			if (write(ofd, buf, n) != n) {
				(void)close(fi);
				return(REPRINT);
			}
		(void)close(fi);
		return(OK);
	}
	switch (format) {
	case 'p':	/* print file using 'pr' */
		if (IF == NULL) {	/* use output filter */
			prog = _PATH_PR;
			av[0] = "pr";
			av[1] = width;
			av[2] = length;
			av[3] = "-h";
			av[4] = *title ? title : " ";
			av[5] = NULL;
			fo = ofd;
			goto start;
		}
		pipe(p);
		if ((prchild = dofork(DORETURN)) == 0) {	/* child */
			dup2(fi, 0);		/* file is stdin */
			dup2(p[1], 1);		/* pipe is stdout */
			closelog();
			nofile = sysconf(_SC_OPEN_MAX);
			for (n = 3; n < nofile; n++)
				(void)close(n);
			execl(_PATH_PR, "pr", width, length,
			    "-h", *title ? title : " ", (char *)NULL);
			syslog(LOG_ERR, "cannot execl %s", _PATH_PR);
			exit(2);
		}
		(void)close(p[1]);		/* close output side */
		(void)close(fi);
		if (prchild < 0) {
			prchild = 0;
			(void)close(p[0]);
			return(ERROR);
		}
		fi = p[0];			/* use pipe for input */
	case 'f':	/* print plain text file */
		prog = IF;
		av[1] = width;
		av[2] = length;
		av[3] = indent;
		n = 4;
		break;
	case 'o':       /* print postscript file */
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
		prog = IF;
		av[1] = "-c";
		av[2] = width;
		av[3] = length;
		av[4] = indent;
		n = 5;
		break;
	case 'r':	/* print a fortran text file */
		prog = RF;
		av[1] = width;
		av[2] = length;
		n = 3;
		break;
	case 't':	/* print troff output */
	case 'n':	/* print ditroff output */
	case 'd':	/* print tex output */
		(void)unlink(".railmag");
		if ((fo = open(".railmag", O_CREAT|O_WRONLY|O_EXCL, FILMOD)) < 0) {
			syslog(LOG_ERR, "%s: cannot create .railmag", printer);
			(void)unlink(".railmag");
		} else {
			for (n = 0; n < 4; n++) {
				if (fonts[n][0] != '/')
					(void)write(fo, _PATH_VFONT,
					    sizeof(_PATH_VFONT) - 1);
				(void)write(fo, fonts[n], strlen(fonts[n]));
				(void)write(fo, "\n", 1);
			}
			(void)close(fo);
		}
		prog = (format == 't') ? TF : (format == 'n') ? NF : DF;
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	case 'c':	/* print cifplot output */
		prog = CF;
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	case 'g':	/* print plot(1G) output */
		prog = GF;
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	case 'v':	/* print raster output */
		prog = VF;
		av[1] = pxwidth;
		av[2] = pxlength;
		n = 3;
		break;
	default:
		(void)close(fi);
		syslog(LOG_ERR, "%s: illegal format character '%c'",
			printer, format);
		return(ERROR);
	}
	if (prog == NULL) {
		(void)close(fi);
		syslog(LOG_ERR,
		    "%s: no filter found in printcap for format character '%c'",
		    printer, format);
		return(ERROR);
	}
	if ((av[0] = strrchr(prog, '/')) != NULL)
		av[0]++;
	else
		av[0] = prog;
	av[n++] = "-n";
	av[n++] = logname;
	if (*jobname != '\0' && strcmp(jobname, " ") != 0) {
		av[n++] = "-j";
		av[n++] = jobname;
	}
	av[n++] = "-h";
	av[n++] = fromhost;
	av[n++] = AF;
	av[n] = 0;
	fo = pfd;
	if (ofilter > 0) {		/* stop output filter */
		write(ofd, "\031\1", 2);
		while ((pid = waitpid((pid_t)-1, &status, WUNTRACED)) > 0
		    && pid != ofilter)
			;
		if (WIFSTOPPED(status) == 0) {
			(void)close(fi);
			syslog(LOG_WARNING,
			    "%s: output filter died (retcode=%d termsig=%d)",
			    printer, WEXITSTATUS(status), WTERMSIG(status));
			return(REPRINT);
		}
		stopped++;
	}
start:
	if ((child = dofork(DORETURN)) == 0) {	/* child */
		dup2(fi, 0);
		dup2(fo, 1);
		unlink(tempfile);
		n = open(tempfile, O_WRONLY|O_CREAT|O_EXCL, 0664);
		if (n >= 0)
			dup2(n, 2);
		closelog();
		nofile = sysconf(_SC_OPEN_MAX);
		for (n = 3; n < nofile; n++)
			(void)close(n);
		execv(prog, av);
		syslog(LOG_ERR, "cannot execv %s", prog);
		_exit(2);
	}
	serrno = errno;
	(void)close(fi);
	errno = serrno;
	if (child < 0) {
		child = prchild = tof = 0;
		syslog(LOG_ERR, "cannot start child process: %m");
		return (ERROR);
	}
	while ((pid = wait(&status)) > 0 && pid != child)
		;
	child = 0;
	prchild = 0;
	if (stopped) {		/* restart output filter */
		if (kill(ofilter, SIGCONT) < 0) {
			syslog(LOG_ERR, "cannot restart output filter");
			exit(1);
		}
	}
	tof = 0;

	/* Copy filter output to "lf" logfile */
	fd = safe_open(tempfile, O_RDONLY|O_NOFOLLOW, 0);
	if (fd >= 0) {
		while ((nread = read(fd, buf, sizeof(buf))) > 0)
			(void)write(STDERR_FILENO, buf, nread);
		(void)close(fd);
	}

	if (!WIFEXITED(status)) {
		syslog(LOG_WARNING, "%s: filter '%c' terminated (termsig=%d)",
		    printer, format, WTERMSIG(status));
		return(ERROR);
	}
	switch (WEXITSTATUS(status)) {
	case 0:
		tof = 1;
		return(OK);
	case 1:
		return(REPRINT);
	case 2:
		return(ERROR);
	default:
		syslog(LOG_WARNING, "%s: filter '%c' exited (retcode=%d)",
		    printer, format, WEXITSTATUS(status));
		return(FILTERERR);
	}
}

/*
 * Send the daemon control file (cf) and any data files.
 * Return -1 if a non-recoverable error occurred, 1 if a recoverable error and
 * 0 if all is well.
 */
static int
sendit(char *file)
{
	int fd, i, err = OK;
	char *cp, last[BUFSIZ];

	/* open control file */
	fd = safe_open(file, O_RDONLY|O_NOFOLLOW, 0);
	if (fd < 0 || (cfp = fdopen(fd, "r")) == NULL)
		return(OK);
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
	while (get_line(cfp)) {
	again:
		if (line[0] == 'S') {
			cp = line+1;
			fdev = 0;
			while (*cp >= '0' && *cp <= '9')
				fdev = fdev * 10 + (*cp++ - '0');
			cp++;
			fino = 0;
			while (*cp >= '0' && *cp <= '9')
				fino = fino * 10 + (*cp++ - '0');
			continue;
		}
		if (line[0] >= 'a' && line[0] <= 'z') {
			strlcpy(last, line, sizeof(last));
			while ((i = get_line(cfp)) != 0)
				if (strcmp(last, line))
					break;
			switch (sendfile('\3', last+1)) {
			case OK:
				if (i)
					goto again;
				break;
			case REPRINT:
				(void)fclose(cfp);
				return(REPRINT);
			case ACCESS:
				sendmail(logname, ACCESS);
			case ERROR:
				err = ERROR;
			}
			break;
		}
	}
	if (err == OK && sendfile('\2', file) > 0) {
		(void)fclose(cfp);
		return(REPRINT);
	}
	/*
	 * pass 2
	 */
	fseek(cfp, 0L, SEEK_SET);
	while (get_line(cfp))
		if (line[0] == 'U' && strchr(line+1, '/') == 0)
			(void)unlink(line+1);
	/*
	 * clean-up in case another control file exists
	 */
	(void)fclose(cfp);
	(void)unlink(file);
	return(err);
}

/*
 * Send a data file to the remote machine and spool it.
 * Return positive if we should try resending.
 */
static int
sendfile(int type, char *file)
{
	int f, i, amt;
	struct stat stb;
	char buf[BUFSIZ];
	int sizerr, resp;

	if (fdev != (dev_t)-1 && fino != (ino_t)-1) {
		/* symbolic link */
		PRIV_START;
		f = safe_open(file, O_RDONLY, 0);
		PRIV_END;
		if (f != -1) {
			/*
			 * The symbolic link should still point to the same file
			 * or someone is trying to print something he shouldn't.
			 */
			if (fstat(f, &stb) == -1 ||
			    stb.st_dev != fdev || stb.st_ino != fino) {
				close(f);
				return(ACCESS);
			}
		}
	} else {
		/* regular file */
		PRIV_START;
		f = safe_open(file, O_RDONLY|O_NOFOLLOW, 0);
		PRIV_END;
		if (fstat(f, &stb) == -1) {
			close(f);
			f = -1;
		}
	}
	if (f == -1)
		return(ERROR);
	if ((amt = snprintf(buf, sizeof(buf), "%c%lld %s\n", type,
	    (long long)stb.st_size, file)) < 0 || amt >= sizeof(buf))
		return (ACCESS);		/* XXX hack */
	for (i = 0;  ; i++) {
		if (write(pfd, buf, amt) != amt ||
		    (resp = response()) < 0 || resp == '\1') {
			(void)close(f);
			return(REPRINT);
		} else if (resp == '\0')
			break;
		if (i == 0)
			pstatus("no space on remote; waiting for queue to drain");
		if (i == 10)
			syslog(LOG_ALERT, "%s: can't send to %s; queue full",
				printer, RM);
		sleep(5 * 60);
	}
	if (i)
		pstatus("sending to %s", RM);
	sizerr = 0;
	for (i = 0; i < stb.st_size; i += BUFSIZ) {
		struct sigaction osa, nsa;

		amt = BUFSIZ;
		if (i + amt > stb.st_size)
			amt = stb.st_size - i;
		if (sizerr == 0 && read(f, buf, amt) != amt)
			sizerr = 1;
		memset(&nsa, 0, sizeof(nsa));
		nsa.sa_handler = alarmer;
		sigemptyset(&nsa.sa_mask);
		nsa.sa_flags = 0;
		(void)sigaction(SIGALRM, &nsa, &osa);
		alarm(wait_time);
		if (write(pfd, buf, amt) != amt) {
			alarm(0);
			(void)sigaction(SIGALRM, &osa, NULL);
			(void)close(f);
			return(REPRINT);
		}
		alarm(0);
		(void)sigaction(SIGALRM, &osa, NULL);
	}

	(void)close(f);
	if (sizerr) {
		syslog(LOG_INFO, "%s: %s: changed size", printer, file);
		/* tell recvjob to ignore this file */
		(void)write(pfd, "\1", 1);
		return(ERROR);
	}
	if (write(pfd, "", 1) != 1 || response())
		return(REPRINT);
	return(OK);
}

/*
 * Check to make sure there have been no errors and that both programs
 * are in sync with eachother.
 * Return non-zero if the connection was lost.
 */
static char
response(void)
{
	struct sigaction osa, nsa;
	char resp;

	memset(&nsa, 0, sizeof(nsa));
	nsa.sa_handler = alarmer;
	sigemptyset(&nsa.sa_mask);
	nsa.sa_flags = 0;
	(void)sigaction(SIGALRM, &nsa, &osa);
	alarm(wait_time);
	if (read(pfd, &resp, 1) != 1) {
		syslog(LOG_INFO, "%s: lost connection", printer);
		resp = -1;
	}
	alarm(0);
	(void)sigaction(SIGALRM, &osa, NULL);
	return (resp);
}

/*
 * Banner printing stuff
 */
static void
banner(char *name1, char *name2)
{
	time_t tvec;

	time(&tvec);
	if (!SF && !tof)
		(void)write(ofd, FF, strlen(FF));
	if (SB) {	/* short banner only */
		if (class[0]) {
			(void)write(ofd, class, strlen(class));
			(void)write(ofd, ":", 1);
		}
		(void)write(ofd, name1, strlen(name1));
		(void)write(ofd, "  Job: ", 7);
		(void)write(ofd, name2, strlen(name2));
		(void)write(ofd, "  Date: ", 8);
		(void)write(ofd, ctime(&tvec), 24);
		(void)write(ofd, "\n", 1);
	} else {	/* normal banner */
		(void)write(ofd, "\n\n\n", 3);
		scan_out(ofd, name1, '\0');
		(void)write(ofd, "\n\n", 2);
		scan_out(ofd, name2, '\0');
		if (class[0]) {
			(void)write(ofd, "\n\n\n", 3);
			scan_out(ofd, class, '\0');
		}
		(void)write(ofd, "\n\n\n\n\t\t\t\t\tJob:  ", 15);
		(void)write(ofd, name2, strlen(name2));
		(void)write(ofd, "\n\t\t\t\t\tDate: ", 12);
		(void)write(ofd, ctime(&tvec), 24);
		(void)write(ofd, "\n", 1);
	}
	if (!SF)
		(void)write(ofd, FF, strlen(FF));
	tof = 1;
}

static char *
scnline(int key, char *p, int c)
{
	int scnwidth;

	for (scnwidth = WIDTH; --scnwidth;) {
		key <<= 1;
		*p++ = key & 0200 ? c : BACKGND;
	}
	return (p);
}

#define TRC(q)	(((q)-' ')&0177)

static void
scan_out(int scfd, char *scsp, int dlm)
{
	char *strp;
	int nchrs, j;
	char outbuf[LINELEN+1], *sp, c, cc;
	int d, scnhgt;
	extern char scnkey[][HEIGHT];	/* in lpdchar.c */

	for (scnhgt = 0; scnhgt++ < HEIGHT+DROP; ) {
		strp = &outbuf[0];
		sp = scsp;
		for (nchrs = 0; ; ) {
			d = dropit(c = TRC(cc = *sp++));
			if ((!d && scnhgt > HEIGHT) || (scnhgt <= DROP && d))
				for (j = WIDTH; --j;)
					*strp++ = BACKGND;
			else
				strp = scnline(scnkey[(int)c][scnhgt-1-d],
				    strp, cc);
			if (*sp == dlm || *sp == '\0' ||
			    nchrs++ >= PW/(WIDTH+1)-1)
				break;
			*strp++ = BACKGND;
			*strp++ = BACKGND;
		}
		while (*--strp == BACKGND && strp >= outbuf)
			;
		strp++;
		*strp++ = '\n';	
		(void)write(scfd, outbuf, strp-outbuf);
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
sendmail(char *user, int bombed)
{
	int i, p[2], s, nofile;
	char *cp = NULL;
	struct stat stb;
	FILE *fp;

	if (user[0] == '-' || user[0] == '/' || !isprint((unsigned char)user[0]))
		return;
	pipe(p);
	if ((s = dofork(DORETURN)) == 0) {		/* child */
		dup2(p[0], 0);
		closelog();
		nofile = sysconf(_SC_OPEN_MAX);
		for (i = 3; i < nofile; i++)
			(void)close(i);
		if ((cp = strrchr(_PATH_SENDMAIL, '/')) != NULL)
			cp++;
		else
			cp = _PATH_SENDMAIL;
		execl(_PATH_SENDMAIL, cp, "-t", (char *)NULL);
		_exit(0);
	} else if (s > 0) {				/* parent */
		dup2(p[1], 1);
		printf("Auto-Submitted: auto-generated\n");
		printf("To: %s@%s\n", user, fromhost);
		printf("Subject: %s printer job \"%s\"\n", printer,
			*jobname ? jobname : "<unknown>");
		printf("Reply-To: root@%s\n\n", host);
		printf("Your printer job ");
		if (*jobname)
			printf("(%s) ", jobname);
		switch (bombed) {
		case OK:
			printf("\ncompleted successfully\n");
			cp = "OK";
			break;
		default:
		case FATALERR:
			printf("\ncould not be printed\n");
			cp = "FATALERR";
			break;
		case NOACCT:
			printf("\ncould not be printed without an account on %s\n", host);
			cp = "NOACCT";
			break;
		case FILTERERR:
			cp = "FILTERERR";
			if (stat(tempfile, &stb) < 0 || stb.st_size == 0 ||
			    (fp = fopen(tempfile, "r")) == NULL) {
				printf("\nhad some errors and may not have printed\n");
				break;
			}
			printf("\nhad the following errors and may not have printed:\n");
			while ((i = getc(fp)) != EOF)
				putchar(i);
			(void)fclose(fp);
			break;
		case ACCESS:
			printf("\nwas not printed because it was not linked to the original file\n");
			cp = "ACCESS";
		}
		fflush(stdout);
		(void)close(1);
	} else {
		syslog(LOG_ERR, "fork for sendmail failed: %m");
	}
	(void)close(p[0]);
	(void)close(p[1]);
	if (s != -1) {
		wait(NULL);
		syslog(LOG_INFO,
		    "mail sent to user %s about job %s on printer %s (%s)",
		    user, *jobname ? jobname : "<unknown>", printer, cp);
	}
}

/* sleep n milliseconds */
static void
delay(int n)
{
	struct timespec tdelay;

	if (n <= 0 || n > 10000)
		fatal("unreasonable delay period (%d)", n);
	tdelay.tv_sec = n / 1000;
	tdelay.tv_nsec = n * 1000000 % 1000000000;
	nanosleep(&tdelay, NULL);
}

/*
 * dofork - fork with retries on failure
 */
static pid_t
dofork(int action)
{
	struct passwd *pw;
	pid_t pid;
	int i;

	for (i = 0; i < 20; i++) {
		if ((pid = fork()) < 0) {
			sleep((unsigned)(i*i));
			continue;
		}
		/*
		 * Child should run as daemon instead of root
		 */
		if (pid == 0) {
			(void)close(lfd);
			PRIV_START;
			pw = getpwuid(DU);
			if (pw == 0) {
				syslog(LOG_ERR, "uid %ld not in password file",
				    DU);
				break;
			}
			initgroups(pw->pw_name, pw->pw_gid);
			setgid(pw->pw_gid);
			setlogin("");
			setuid(DU);
		}
		return (pid);
	}
	syslog(LOG_ERR, "can't fork");

	switch (action) {
	case DORETURN:
		return (-1);
	default:
		syslog(LOG_ERR, "bad action (%d) to dofork", action);
		/*FALL THRU*/
	case DOABORT:
		exit(1);
	}
	/*NOTREACHED*/
}

/*
 * Kill child processes to abort current job.
 */
static void
abortpr(int signo)
{
	(void)close(lfd);
	(void)unlink(tempfile);
	(void)kill(0, SIGINT);
	if (ofilter > 0)
		kill(ofilter, SIGCONT);
	while (wait(NULL) > 0)
		;
	_exit(0);
}

static void
init(void)
{
	int status;
	char *s;

	PRIV_START;
	status = cgetent(&bp, printcapdb, printer);
	PRIV_END;

	switch (status) {
	case -1:
		syslog(LOG_ERR, "unknown printer: %s", printer);
		exit(1);
	case -2:
		syslog(LOG_ERR, "can't open printer description file");
		exit(1);
	case -3:
		fatal("potential reference loop detected in printcap file");
	default:
		break;
	}

	if (cgetstr(bp, DEFLP, &LP) == -1)
		LP = _PATH_DEFDEVLP;
	if (cgetstr(bp, "rp", &RP) == -1)
		RP = DEFLP;
	if (cgetstr(bp, "lo", &LO) == -1)
		LO = DEFLOCK;
	if (cgetstr(bp, "st", &ST) == -1)
		ST = DEFSTAT;
	if (cgetstr(bp, "lf", &LF) == -1)
		LF = _PATH_CONSOLE;
	if (cgetstr(bp, "sd", &SD) == -1)
		SD = _PATH_DEFSPOOL;
	if (cgetnum(bp, "du", &DU) < 0)
		DU = DEFUID;
	if (cgetstr(bp, "ff", &FF) == -1)
		FF = DEFFF;
	if (cgetnum(bp, "pw", &PW) < 0)
		PW = DEFWIDTH;
	(void)snprintf(&width[2], sizeof(width) - 2, "%ld", PW);
	if (cgetnum(bp, "pl", &PL) < 0)
		PL = DEFLENGTH;
	(void)snprintf(&length[2], sizeof(length) - 2, "%ld", PL);
	if (cgetnum(bp, "px", &PX) < 0)
		PX = 0;
	(void)snprintf(&pxwidth[2], sizeof(pxwidth) - 2, "%ld", PX);
	if (cgetnum(bp, "py", &PY) < 0)
		PY = 0;
	(void)snprintf(&pxlength[2], sizeof(pxlength) - 2, "%ld", PY);
	cgetstr(bp, "rm", &RM);
	if ((s = checkremote()) != NULL)
		syslog(LOG_WARNING, "%s", s);

	cgetstr(bp, "af", &AF);
	cgetstr(bp, "of", &OF);
	cgetstr(bp, "if", &IF);
	cgetstr(bp, "rf", &RF);
	cgetstr(bp, "tf", &TF);
	cgetstr(bp, "nf", &NF);
	cgetstr(bp, "df", &DF);
	cgetstr(bp, "gf", &GF);
	cgetstr(bp, "vf", &VF);
	cgetstr(bp, "cf", &CF);
	cgetstr(bp, "tr", &TR);

	RS = (cgetcap(bp, "rs", ':') != NULL);
	SF = (cgetcap(bp, "sf", ':') != NULL);
	SH = (cgetcap(bp, "sh", ':') != NULL);
	SB = (cgetcap(bp, "sb", ':') != NULL);
	HL = (cgetcap(bp, "hl", ':') != NULL);
	RW = (cgetcap(bp, "rw", ':') != NULL);

	cgetnum(bp, "br", &BR);
	cgetstr(bp, "ms", &MS);

	tof = (cgetcap(bp, "fo", ':') == NULL);
}

/*
 * Acquire line printer or remote connection.
 * XXX - should push down privs in here
 */
static void
openpr(void)
{
	int i, nofile;
	char *cp;
	extern int rflag;

	if (!remote && *LP) {
		if ((cp = strchr(LP, '@')))
			opennet(cp);
		else
			opentty();
	} else if (remote) {
		openrem();
	} else {
		syslog(LOG_ERR, "%s: no line printer device or host name",
			printer);
		exit(1);
	}

	/*
	 * Start up an output filter, if needed.
	 */
	if ((!remote || rflag) && OF) {
		int p[2];

		pipe(p);
		if ((ofilter = dofork(DOABORT)) == 0) {	/* child */
			dup2(p[0], 0);		/* pipe is std in */
			dup2(pfd, 1);		/* printer is std out */
			closelog();
			nofile = sysconf(_SC_OPEN_MAX);
			for (i = 3; i < nofile; i++)
				(void)close(i);
			if ((cp = strrchr(OF, '/')) == NULL)
				cp = OF;
			else
				cp++;
			execl(OF, cp, width, length, (char *)NULL);
			syslog(LOG_ERR, "%s: %s: %m", printer, OF);
			exit(1);
		}
		(void)close(p[0]);		/* close input side */
		ofd = p[1];			/* use pipe for output */
	} else {
		ofd = pfd;
		ofilter = 0;
	}
}

/*
 * Printer connected directly to the network
 * or to a terminal server on the net
 */
static void
opennet(char *cp)
{
	int i;
	int resp, port;
	char save_ch;

	save_ch = *cp;
	*cp = '\0';
	port = atoi(LP);
	if (port <= 0) {
		syslog(LOG_ERR, "%s: bad port number: %s", printer, LP);
		exit(1);
	}
	*cp++ = save_ch;

	for (i = 1; ; i = i < 256 ? i << 1 : i) {
		resp = -1;
		pfd = getport(cp, port);
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
			pstatus("waiting for %s to come up", LP);
		   else
			pstatus("waiting for access to printer on %s", LP);
		}
		sleep(i);
	}
	pstatus("sending to %s port %d", cp, port);
}

/*
 * Printer is connected to an RS232 port on this host
 */
static void
opentty(void)
{
	int i;

	for (i = 1; ; i = i < 32 ? i << 1 : i) {
		pfd = open(LP, RW ? O_RDWR : O_WRONLY);
		if (pfd >= 0) {
			delay(500);
			break;
		}
		if (errno == ENOENT) {
			syslog(LOG_ERR, "%s: %m", LP);
			exit(1);
		}
		if (i == 1)
			pstatus("waiting for %s to become ready (offline ?)",
				printer);
		sleep(i);
	}
	if (isatty(pfd))
		setty();
	pstatus("%s is ready and printing", printer);
}

/*
 * Printer is on a remote host
 */
static void
openrem(void)
{
	int i, n;
	int resp;

	for (i = 1; ; i = i < 256 ? i << 1 : i) {
		resp = -1;
		pfd = getport(RM, 0);
		if (pfd >= 0) {
			if ((n = snprintf(line, sizeof(line), "\2%s\n", RP)) < 0 ||
			    n >= sizeof(line))
				n = sizeof(line) - 1;
			if (write(pfd, line, n) == n &&
			    (resp = response()) == '\0')
				break;
			(void)close(pfd);
		}
		if (i == 1) {
			if (resp < 0)
				pstatus("waiting for %s to come up", RM);
			else {
				pstatus("waiting for queue to be enabled on %s",
					RM);
				i = 256;
			}
		}
		sleep(i);
	}
	pstatus("sending to %s", RM);
}

static void
alarmer(int s)
{
	/* nothing */
}

/*
 * setup tty lines.
 */
static void
setty(void)
{
	struct info i;
	char **argv, **ap, **ep, *p, *val;

	i.fd = pfd;
	i.set = i.wset = 0;
	if (ioctl(i.fd, TIOCEXCL, (char *)0) < 0) {
		syslog(LOG_ERR, "%s: ioctl(TIOCEXCL): %m", printer);
		exit(1);
	}
	if (tcgetattr(i.fd, &i.t) < 0) {
		syslog(LOG_ERR, "%s: tcgetattr: %m", printer);
		exit(1);
	}
	if (BR > 0) {
		cfsetspeed(&i.t, BR);
		i.set = 1;
	}
	if (MS) {
		if (ioctl(i.fd, TIOCGETD, &i.ldisc) < 0) {
			syslog(LOG_ERR, "%s: ioctl(TIOCGETD): %m", printer);
			exit(1);
		}
		if (ioctl(i.fd, TIOCGWINSZ, &i.win) < 0)
			syslog(LOG_INFO, "%s: ioctl(TIOCGWINSZ): %m",
			       printer);

		argv = calloc(256, sizeof(char *));
		if (argv == NULL) {
			syslog(LOG_ERR, "%s: malloc: %m", printer);
			exit(1);
		}
		p = strdup(MS);
		ap = argv;
		ep = argv + 255;
		while ((val = strsep(&p, " \t,")) != NULL) {
			if ((*ap++ = strdup(val)) == NULL) {
				syslog(LOG_ERR, "%s: strdup: %m", printer);
				exit(1);
			}
			if (ap == ep) {
				syslog(LOG_ERR, "%s: too many \"ms\" entries",
				    printer);
				exit(1);
			}
		}
		*ap = NULL;

		for (; *argv; ++argv) {
			if (ksearch(&argv, &i))
				continue;
			if (msearch(&argv, &i))
				continue;
			syslog(LOG_INFO, "%s: unknown stty flag: %s",
			       printer, *argv);
		}
	}

	if (i.set && tcsetattr(i.fd, TCSANOW, &i.t) < 0) {
		syslog(LOG_ERR, "%s: tcsetattr: %m", printer);
		exit(1);
	}
	if (i.wset && ioctl(i.fd, TIOCSWINSZ, &i.win) < 0)
		syslog(LOG_INFO, "%s: ioctl(TIOCSWINSZ): %m", printer);
	return;
}

static void
pstatus(const char *msg, ...)
{
	int fd, len;
	char buf[BUFSIZ];
	va_list ap;

	va_start(ap, msg);
	umask(0);
	fd = open(ST, O_WRONLY|O_CREAT|O_NOFOLLOW|O_EXLOCK, 0660);
	if (fd < 0) {
		syslog(LOG_ERR, "%s: %s: %m", printer, ST);
		exit(1);
	}
	ftruncate(fd, 0);
	len = vsnprintf(buf, sizeof(buf), msg, ap);
	va_end(ap);
	if (len < 0) {
		(void)close(fd);
		return;
	}
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	buf[len++] = '\n';		/* replace NUL with newline */
	(void)write(fd, buf, len);
	(void)close(fd);
}
