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
static char sccsid[] = "@(#)cmds.c	8.2 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * lpc -- line printer control program -- commands:
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <signal.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "lp.h"
#include "lp.local.h"
#include "lpc.h"
#include "extern.h"
#include "pathnames.h"

/*
 * Return values from kill_qtask().
 */
#define KQT_LFERROR	-2
#define KQT_KILLFAIL	-1
#define KQT_NODAEMON	0
#define KQT_KILLOK	1

static char	*args2line(int argc, char **argv);
static int	 doarg(char *_job);
static int	 doselect(const struct dirent *_d);
static int	 kill_qtask(const char *lf);
static int	 sortq(const struct dirent **a, const struct dirent **b);
static int	 touch(struct jobqueue *_jq);
static void	 unlinkf(char *_name);
static void	 upstat(struct printer *_pp, const char *_msg, int _notify);
static void	 wrapup_clean(int _laststatus);

/*
 * generic framework for commands which operate on all or a specified
 * set of printers
 */
enum	qsel_val {			/* how a given ptr was selected */
	QSEL_UNKNOWN = -1,		/* ... not selected yet */
	QSEL_BYNAME = 0,		/* ... user specifed it by name */
	QSEL_ALL = 1			/* ... user wants "all" printers */
					/*     (with more to come)    */
};

static enum qsel_val generic_qselect;	/* indicates how ptr was selected */
static int generic_initerr;		/* result of initrtn processing */
static char *generic_cmdname;
static char *generic_msg;		/* if a -msg was specified */
static char *generic_nullarg;
static void (*generic_wrapup)(int _last_status);   /* perform rtn wrap-up */

void
generic(void (*specificrtn)(struct printer *_pp), int cmdopts,
    void (*initrtn)(int _argc, char *_argv[]), int argc, char *argv[])
{
	int cmdstatus, more, targc;
	struct printer myprinter, *pp;
	char **margv, **targv;

	if (argc == 1) {
		/*
		 * Usage needs a special case for 'down': The user must
		 * either include `-msg', or only the first parameter
		 * that they give will be processed as a printer name.
		 */
		printf("usage: %s  {all | printer ...}", argv[0]);
		if (strcmp(argv[0], "down") == 0) {
			printf(" -msg [<text> ...]\n");
			printf("   or: down  {all | printer} [<text> ...]");
		} else if (cmdopts & LPC_MSGOPT)
			printf(" [-msg <text> ...]");
		printf("\n");
		return;
	}

	/* The first argument is the command name. */
	generic_cmdname = *argv++;
	argc--;

	/*
	 * The initialization routine for a command might set a generic
	 * "wrapup" routine, which should be called after processing all
	 * the printers in the command.  This might print summary info.
	 *
	 * Note that the initialization routine may also parse (and
	 * nullify) some of the parameters given on the command, leaving
	 * only the parameters which have to do with printer names.
	 */
	pp = &myprinter;
	generic_wrapup = NULL;
	generic_qselect = QSEL_UNKNOWN;
	cmdstatus = 0;
	/* this just needs to be a distinct value of type 'char *' */
	if (generic_nullarg == NULL)
		generic_nullarg = strdup("");

	/*
	 * Some commands accept a -msg argument, which indicates that
	 * all remaining arguments should be combined into a string.
	 */
	generic_msg = NULL;
	if (cmdopts & LPC_MSGOPT) {
		targc = argc;
		targv = argv;
		for (; targc > 0; targc--, targv++) {
			if (strcmp(*targv, "-msg") == 0) {
				argc -= targc;
				generic_msg = args2line(targc - 1, targv + 1);
				break;
			}
		}
		if (argc < 1) {
			printf("error: No printer name(s) specified before"
			    " '-msg'.\n");
			printf("usage: %s  {all | printer ...}",
			    generic_cmdname);
			printf(" [-msg <text> ...]\n");
			return;
		}
	}

	/* call initialization routine, if there is one for this cmd */
	if (initrtn != NULL) {
		generic_initerr = 0;
		(*initrtn)(argc, argv);
		if (generic_initerr)
			return;
		/*
		 * The initrtn may have null'ed out some of the parameters.
		 * Compact the parameter list to remove those nulls, and
		 * correct the arg-count.
		 */
		targc = argc;
		targv = argv;
		margv = argv;
		argc = 0;
		for (; targc > 0; targc--, targv++) {
			if (*targv != generic_nullarg) {
				if (targv != margv)
					*margv = *targv;
				margv++;
				argc++;
			}
		}
	}

	if (argc == 1 && strcmp(*argv, "all") == 0) {
		generic_qselect = QSEL_ALL;
		more = firstprinter(pp, &cmdstatus);
		if (cmdstatus)
			goto looperr;
		while (more) {
			(*specificrtn)(pp);
			do {
				more = nextprinter(pp, &cmdstatus);
looperr:
				switch (cmdstatus) {
				case PCAPERR_TCOPEN:
					printf("warning: %s: unresolved "
					       "tc= reference(s) ",
					       pp->printer);
				case PCAPERR_SUCCESS:
					break;
				default:
					fatal(pp, "%s", pcaperr(cmdstatus));
				}
			} while (more && cmdstatus);
		}
		goto wrapup;
	}

	generic_qselect = QSEL_BYNAME;		/* specifically-named ptrs */
	for (; argc > 0; argc--, argv++) {
		init_printer(pp);
		cmdstatus = getprintcap(*argv, pp);
		switch (cmdstatus) {
		default:
			fatal(pp, "%s", pcaperr(cmdstatus));
		case PCAPERR_NOTFOUND:
			printf("unknown printer %s\n", *argv);
			continue;
		case PCAPERR_TCOPEN:
			printf("warning: %s: unresolved tc= reference(s)\n",
			       *argv);
			break;
		case PCAPERR_SUCCESS:
			break;
		}
		(*specificrtn)(pp);
	}

wrapup:
	if (generic_wrapup) {
		(*generic_wrapup)(cmdstatus);
	}
	free_printer(pp);
	if (generic_msg)
		free(generic_msg);
}

/*
 * Convert an argv-array of character strings into a single string.
 */
static char *
args2line(int argc, char **argv)
{
	char *cp1, *cend;
	const char *cp2;
	char buf[1024];

	if (argc <= 0)
		return strdup("\n");

	cp1 = buf;
	cend = buf + sizeof(buf) - 1;		/* save room for '\0' */
	while (--argc >= 0) {
		cp2 = *argv++;
		while ((cp1 < cend) && (*cp1++ = *cp2++))
			;
		cp1[-1] = ' ';
	}
	cp1[-1] = '\n';
	*cp1 = '\0';
	return strdup(buf);
}

/*
 * Kill the current daemon, to stop printing of the active job.
 */
static int
kill_qtask(const char *lf)
{
	FILE *fp;
	pid_t pid;
	int errsav, killres, lockres, res;

	PRIV_START
	fp = fopen(lf, "r");
	errsav = errno;
	PRIV_END
	res = KQT_NODAEMON;
	if (fp == NULL) {
		/*
		 * If there is no lock file, then there is no daemon to
		 * kill.  Any other error return means there is some
		 * kind of problem with the lock file.
		 */
		if (errsav != ENOENT)
			res = KQT_LFERROR;
		goto killdone;
	}

	/* If the lock file is empty, then there is no daemon to kill */
	if (get_line(fp) == 0)
		goto killdone;

	/*
	 * If the file can be locked without blocking, then there
	 * no daemon to kill, or we should not try to kill it.
	 *
	 * XXX - not sure I understand the reasoning behind this...
	 */
	lockres = flock(fileno(fp), LOCK_SH|LOCK_NB);
	(void) fclose(fp);
	if (lockres == 0)
		goto killdone;

	pid = atoi(line);
	if (pid < 0) {
		/*
		 * If we got a negative pid, then the contents of the
		 * lock file is not valid.
		 */
		res = KQT_LFERROR;
		goto killdone;
	}

	PRIV_END
	killres = kill(pid, SIGTERM);
	errsav = errno;
	PRIV_END
	if (killres == 0) {
		res = KQT_KILLOK;
		printf("\tdaemon (pid %d) killed\n", pid);
	} else if (errno == ESRCH) {
		res = KQT_NODAEMON;
	} else {
		res = KQT_KILLFAIL;
		printf("\tWarning: daemon (pid %d) not killed:\n", pid);
		printf("\t    %s\n", strerror(errsav));
	}

killdone:
	switch (res) {
	case KQT_LFERROR:
		printf("\tcannot open lock file: %s\n",
		    strerror(errsav));
		break;
	case KQT_NODAEMON:
		printf("\tno daemon to abort\n");
		break;
	case KQT_KILLFAIL:
	case KQT_KILLOK:
		/* These two already printed messages to the user. */
		break;
	default:
		printf("\t<internal error in kill_qtask>\n");
		break;
	}

	return (res);
}

/*
 * Write a message into the status file.
 */
static void
upstat(struct printer *pp, const char *msg, int notifyuser)
{
	int fd;
	char statfile[MAXPATHLEN];

	status_file_name(pp, statfile, sizeof statfile);
	umask(0);
	PRIV_START
	fd = open(statfile, O_WRONLY|O_CREAT|O_EXLOCK, STAT_FILE_MODE);
	PRIV_END
	if (fd < 0) {
		printf("\tcannot create status file: %s\n", strerror(errno));
		return;
	}
	(void) ftruncate(fd, 0);
	if (msg == NULL)
		(void) write(fd, "\n", 1);
	else
		(void) write(fd, msg, strlen(msg));
	(void) close(fd);
	if (notifyuser) {
		if ((msg == (char *)NULL) || (strcmp(msg, "\n") == 0))
			printf("\tstatus message is now set to nothing.\n");
		else
			printf("\tstatus message is now: %s", msg);
	}
}

/*
 * kill an existing daemon and disable printing.
 */
void
abort_q(struct printer *pp)
{
	int killres, setres;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	/*
	 * Turn on the owner execute bit of the lock file to disable printing.
	 */
	setres = set_qstate(SQS_STOPP, lf);

	/*
	 * If set_qstate found that there already was a lock file, then
	 * call a routine which will read that lock file and kill the
	 * lpd-process which is listed in that lock file.  If the lock
	 * file did not exist, then either there is no daemon running
	 * for this queue, or there is one running but *it* could not
	 * write a lock file (which means we can not determine the
	 * process id of that lpd-process).
	 */
	switch (setres) {
	case SQS_CHGOK:
	case SQS_CHGFAIL:
		/* Kill the process */
		killres = kill_qtask(lf);
		break;
	case SQS_CREOK:
	case SQS_CREFAIL:
		printf("\tno daemon to abort\n");
		break;
	case SQS_STATFAIL:
		printf("\tassuming no daemon to abort\n");
		break;
	default:
		printf("\t<unexpected result (%d) from set_qstate>\n",
		    setres);
		break;
	}

	if (setres >= 0)
		upstat(pp, "printing disabled\n", 0);
}

/*
 * "global" variables for all the routines related to 'clean' and 'tclean'
 */
static time_t	 cln_now;		/* current time */
static double	 cln_minage;		/* minimum age before file is removed */
static long	 cln_sizecnt;		/* amount of space freed up */
static int 	 cln_debug;		/* print extra debugging msgs */
static int	 cln_filecnt;		/* number of files destroyed */
static int	 cln_foundcore;		/* found a core file! */
static int	 cln_queuecnt;		/* number of queues checked */
static int 	 cln_testonly;		/* remove-files vs just-print-info */

static int
doselect(const struct dirent *d)
{
	int c = d->d_name[0];

	if ((c == 'c' || c == 'd' || c == 'r' || c == 't') &&
	    d->d_name[1] == 'f')
		return 1;
	if (c == 'c') {
		if (!strcmp(d->d_name, "core"))
			cln_foundcore = 1;
	}
	if (c == 'e') {
		if (!strncmp(d->d_name, "errs.", 5))
			return 1;
	}
	return 0;
}

/*
 * Comparison routine that clean_q() uses for scandir.
 *
 * The purpose of this sort is to have all `df' files end up immediately
 * after the matching `cf' file.  For files matching `cf', `df', `rf', or
 * `tf', it sorts by job number and machine, then by `cf', `df', `rf', or
 * `tf', and then by the sequence letter (which is A-Z, or a-z).    This
 * routine may also see filenames which do not start with `cf', `df', `rf',
 * or `tf' (such as `errs.*'), and those are simply sorted by the full
 * filename.
 *
 * XXX
 *   This assumes that all control files start with `cfA*', and it turns
 *   out there are a few implementations of lpr which will create `cfB*'
 *   filenames (they will have datafile names which start with `dfB*').
 */
static int
sortq(const struct dirent **a, const struct dirent **b)
{
	const int a_lt_b = -1, a_gt_b = 1, cat_other = 10;
	const char *fname_a, *fname_b, *jnum_a, *jnum_b;
	int cat_a, cat_b, ch, res, seq_a, seq_b;

	fname_a = (*a)->d_name;
	fname_b = (*b)->d_name;

	/*
	 * First separate filenames into categories.  Categories are
	 * legitimate `cf', `df', `rf' & `tf' filenames, and "other" - in
	 * that order.  It is critical that the mapping be exactly the
	 * same for 'a' vs 'b', so define a macro for the job.
	 *
	 * [aside: the standard `cf' file has the jobnumber start in
	 * position 4, but some implementations have that as an extra
	 * file-sequence letter, and start the job number in position 5.]
	 */
#define MAP_TO_CAT(fname_X,cat_X,jnum_X,seq_X) do { \
	cat_X = cat_other;    \
	ch = *(fname_X + 2);  \
	jnum_X = fname_X + 3; \
	seq_X = 0;            \
	if ((*(fname_X + 1) == 'f') && (isalpha(ch))) { \
		seq_X = ch; \
		if (*fname_X == 'c') \
			cat_X = 1; \
		else if (*fname_X == 'd') \
			cat_X = 2; \
		else if (*fname_X == 'r') \
			cat_X = 3; \
		else if (*fname_X == 't') \
			cat_X = 4; \
		if (cat_X != cat_other) { \
			ch = *jnum_X; \
			if (!isdigit(ch)) { \
				if (isalpha(ch)) { \
					jnum_X++; \
					ch = *jnum_X; \
					seq_X = (seq_X << 8) + ch; \
				} \
				if (!isdigit(ch)) \
					cat_X = cat_other; \
			} \
		} \
	} \
} while (0)

	MAP_TO_CAT(fname_a, cat_a, jnum_a, seq_a);
	MAP_TO_CAT(fname_b, cat_b, jnum_b, seq_b);

#undef MAP_TO_CAT

	/* First handle all cases which have "other" files */
	if ((cat_a >= cat_other) || (cat_b >= cat_other)) {
		/* for two "other" files, just compare the full name */
		if (cat_a == cat_b)
			res = strcmp(fname_a, fname_b);
		else if (cat_a < cat_b)
			res = a_lt_b;
		else
			res = a_gt_b;
		goto have_res;
	}

	/*
	 * At this point, we know both files are legitimate `cf', `df', `rf',
	 * or `tf' files.  Compare them by job-number and machine name.
	 */
	res = strcmp(jnum_a, jnum_b);
	if (res != 0)
		goto have_res;

	/*
	 * We have two files which belong to the same job.  Sort based
	 * on the category of file (`c' before `d', etc).
	 */
	if (cat_a < cat_b) {
		res = a_lt_b;
		goto have_res;
	} else if (cat_a > cat_b) {
		res = a_gt_b;
		goto have_res;
	}

	/*
	 * Two files in the same category for a single job.  Sort based
	 * on the sequence letter(s).  (usually `A' through `Z', etc).
	 */
	if (seq_a < seq_b) {
		res = a_lt_b;
		goto have_res;
	} else if (seq_a > seq_b) {
		res = a_gt_b;
		goto have_res;
	}

	/*
	 * Given that the filenames in a directory are unique, this SHOULD
	 * never happen (unless there are logic errors in this routine).
	 * But if it does happen, we must return "is equal" or the caller
	 * might see inconsistent results in the sorting order, and that
	 * can trigger other problems.
	 */
	printf("\t*** Error in sortq: %s == %s !\n", fname_a, fname_b);
	printf("\t***       cat %d == %d ; seq = %d %d\n", cat_a, cat_b,
	    seq_a, seq_b);
	res = 0;

have_res:
	return res;
}

/*
 * Remove all spool files and temporaries from the spooling area.
 * Or, perhaps:
 * Remove incomplete jobs from spooling area.
 */

void
clean_gi(int argc, char *argv[])
{

	/* init some fields before 'clean' is called for each queue */
	cln_queuecnt = 0;
	cln_now = time(NULL);
	cln_minage = 3600.0;		/* only delete files >1h old */
	cln_filecnt = 0;
	cln_sizecnt = 0;
	cln_debug = 0;
	cln_testonly = 0;
	generic_wrapup = &wrapup_clean;

	/* see if there are any options specified before the ptr list */
	for (; argc > 0; argc--, argv++) {
		if (**argv != '-')
			break;
		if (strcmp(*argv, "-d") == 0) {
			/* just an example of an option... */
			cln_debug++;
			*argv = generic_nullarg;	/* "erase" it */
		} else {
			printf("Invalid option '%s'\n", *argv);
			generic_initerr = 1;
		}
	}

	return;
}

void
tclean_gi(int argc, char *argv[])
{

	/* only difference between 'clean' and 'tclean' is one value */
	/* (...and the fact that 'clean' is priv and 'tclean' is not) */
	clean_gi(argc, argv);
	cln_testonly = 1;

	return;
}

void
clean_q(struct printer *pp)
{
	char *cp, *cp1, *lp;
	struct dirent **queue;
	size_t linerem;
	int didhead, i, n, nitems, rmcp;

	cln_queuecnt++;

	didhead = 0;
	if (generic_qselect == QSEL_BYNAME) {
		printf("%s:\n", pp->printer);
		didhead = 1;
	}

	lp = line;
	cp = pp->spool_dir;
	while (lp < &line[sizeof(line) - 1]) {
		if ((*lp++ = *cp++) == 0)
			break;
	}
	lp[-1] = '/';
	linerem = sizeof(line) - (lp - line);

	cln_foundcore = 0;
	PRIV_START
	nitems = scandir(pp->spool_dir, &queue, doselect, sortq);
	PRIV_END
	if (nitems < 0) {
		if (!didhead) {
			printf("%s:\n", pp->printer);
			didhead = 1;
		}
		printf("\tcannot examine spool directory\n");
		return;
	}
	if (cln_foundcore) {
		if (!didhead) {
			printf("%s:\n", pp->printer);
			didhead = 1;
		}
		printf("\t** found a core file in %s !\n", pp->spool_dir);
	}
	if (nitems == 0)
		return;
	if (!didhead)
		printf("%s:\n", pp->printer);
	if (cln_debug) {
		printf("\t** ----- Sorted list of files being checked:\n");
		i = 0;
		do {
			cp = queue[i]->d_name;
			printf("\t** [%3d] = %s\n", i, cp);
		} while (++i < nitems);
		printf("\t** ----- end of sorted list\n");
	}
	i = 0;
	do {
		cp = queue[i]->d_name;
		rmcp = 0;
		if (*cp == 'c') {
			/*
			 * A control file.  Look for matching data-files.
			 */
			/* XXX
			 *  Note the logic here assumes that the hostname
			 *  part of cf-filenames match the hostname part
			 *  in df-filenames, and that is not necessarily
			 *  true (eg: for multi-homed hosts).  This needs
			 *  some further thought...
			 */
			n = 0;
			while (i + 1 < nitems) {
				cp1 = queue[i + 1]->d_name;
				if (*cp1 != 'd' || strcmp(cp + 3, cp1 + 3))
					break;
				i++;
				n++;
			}
			if (n == 0) {
				rmcp = 1;
			}
		} else if (*cp == 'e') {
			/*
			 * Must be an errrs or email temp file.
			 */
			rmcp = 1;
		} else {
			/*
			 * Must be a df with no cf (otherwise, it would have
			 * been skipped above) or an rf or tf file (which can
			 * always be removed if it is old enough).
			 */
			rmcp = 1;
		}
		if (rmcp) {
			if (strlen(cp) >= linerem) {
				printf("\t** internal error: 'line' overflow!\n");
				printf("\t**   spooldir = %s\n", pp->spool_dir);
				printf("\t**   cp = %s\n", cp);
				return;
			}
			strlcpy(lp, cp, linerem);
			unlinkf(line);
		}
     	} while (++i < nitems);
}

static void
wrapup_clean(int laststatus __unused)
{

	printf("Checked %d queues, and ", cln_queuecnt);
	if (cln_filecnt < 1) {
		printf("no cruft was found\n");
		return;
	}
	if (cln_testonly) {
		printf("would have ");
	}
	printf("removed %d files (%ld bytes).\n", cln_filecnt, cln_sizecnt);	
}
 
static void
unlinkf(char *name)
{
	struct stat stbuf;
	double agemod, agestat;
	int res;
	char linkbuf[BUFSIZ];

	/*
	 * We have to use lstat() instead of stat(), in case this is a df*
	 * "file" which is really a symlink due to 'lpr -s' processing.  In
	 * that case, we need to check the last-mod time of the symlink, and
	 * not the file that the symlink is pointed at.
	 */
	PRIV_START
	res = lstat(name, &stbuf);
	PRIV_END
	if (res < 0) {
		printf("\terror return from stat(%s):\n", name);
		printf("\t      %s\n", strerror(errno));
		return;
	}

	agemod = difftime(cln_now, stbuf.st_mtime);
	agestat = difftime(cln_now,  stbuf.st_ctime);
	if (cln_debug > 1) {
		/* this debugging-aid probably is not needed any more... */
		printf("\t\t  modify age=%g secs, stat age=%g secs\n",
		    agemod, agestat);
	}
	if ((agemod <= cln_minage) && (agestat <= cln_minage))
		return;

	/*
	 * if this file is a symlink, then find out the target of the
	 * symlink before unlink-ing the file itself
	 */
	if (S_ISLNK(stbuf.st_mode)) {
		PRIV_START
		res = readlink(name, linkbuf, sizeof(linkbuf));
		PRIV_END
		if (res < 0) {
			printf("\terror return from readlink(%s):\n", name);
			printf("\t      %s\n", strerror(errno));
			return;
		}
		if (res == sizeof(linkbuf))
			res--;
		linkbuf[res] = '\0';
	}

	cln_filecnt++;
	cln_sizecnt += stbuf.st_size;

	if (cln_testonly) {
		printf("\twould remove %s\n", name);
		if (S_ISLNK(stbuf.st_mode)) {
			printf("\t    (which is a symlink to %s)\n", linkbuf);
		}
	} else {
		PRIV_START
		res = unlink(name);
		PRIV_END
		if (res < 0)
			printf("\tcannot remove %s (!)\n", name);
		else
			printf("\tremoved %s\n", name);
		/* XXX
		 *  Note that for a df* file, this code should also check to see
		 *  if it is a symlink to some other file, and if the original
		 *  lpr command included '-r' ("remove file").  Of course, this
		 *  code would not be removing the df* file unless there was no
		 *  matching cf* file, and without the cf* file it is currently
		 *  impossible to determine if '-r' had been specified...
		 *
		 *  As a result of this quandry, we may be leaving behind a
		 *  user's file that was supposed to have been removed after
		 *  being printed.  This may effect services such as CAP or
		 *  samba, if they were configured to use 'lpr -r', and if
		 *  datafiles are not being properly removed.
		*/
		if (S_ISLNK(stbuf.st_mode)) {
			printf("\t    (which was a symlink to %s)\n", linkbuf);
		}
	}
}

/*
 * Enable queuing to the printer (allow lpr to add new jobs to the queue).
 */
void
enable_q(struct printer *pp)
{
	int setres;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	setres = set_qstate(SQS_ENABLEQ, lf);
}

/*
 * Disable queuing.
 */
void
disable_q(struct printer *pp)
{
	int setres;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	setres = set_qstate(SQS_DISABLEQ, lf);
}

/*
 * Disable queuing and printing and put a message into the status file
 * (reason for being down).  If the user specified `-msg', then use
 * everything after that as the message for the status file.  If the
 * user did NOT specify `-msg', then the command should take the first
 * parameter as the printer name, and all remaining parameters as the
 * message for the status file.  (This is to be compatible with the
 * original definition of 'down', which was implemented long before
 * `-msg' was around).
 */
void
down_gi(int argc, char *argv[])
{

	/* If `-msg' was specified, then this routine has nothing to do. */
	if (generic_msg != NULL)
		return;

	/*
	 * If the user only gave one parameter, then use a default msg.
	 * (if argc == 1 at this point, then *argv == name of printer).
	 */ 
	if (argc == 1) {
		generic_msg = strdup("printing disabled\n");
		return;
	}

	/*
	 * The user specified multiple parameters, and did not specify
	 * `-msg'.  Build a message from all the parameters after the
	 * first one (and nullify those parameters so generic-processing
	 * will not process them as printer-queue names).
	 */
	argc--;
	argv++;
	generic_msg = args2line(argc, argv);
	for (; argc > 0; argc--, argv++)
		*argv = generic_nullarg;	/* "erase" it */
}

void
down_q(struct printer *pp)
{
	int setres;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	setres = set_qstate(SQS_DISABLEQ+SQS_STOPP, lf);
	if (setres >= 0)
		upstat(pp, generic_msg, 1);
}

/*
 * Exit lpc
 */
void
quit(int argc __unused, char *argv[] __unused)
{
	exit(0);
}

/*
 * Kill and restart the daemon.
 */
void
restart_q(struct printer *pp)
{
	int killres, setres, startok;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	killres = kill_qtask(lf);

	/*
	 * XXX - if the kill worked, we should probably sleep for
	 *      a second or so before trying to restart the queue.
	 */

	/* make sure the queue is set to print jobs */
	setres = set_qstate(SQS_STARTP, lf);

	PRIV_START
	startok = startdaemon(pp);
	PRIV_END
	if (!startok)
		printf("\tcouldn't restart daemon\n");
	else
		printf("\tdaemon restarted\n");
}

/*
 * Set the status message of each queue listed.  Requires a "-msg"
 * parameter to indicate the end of the queue list and start of msg text.
 */
void
setstatus_gi(int argc __unused, char *argv[] __unused)
{

	if (generic_msg == NULL) {
		printf("You must specify '-msg' before the text of the new status message.\n");
		generic_initerr = 1;
	}
}

void
setstatus_q(struct printer *pp)
{
	struct stat stbuf;
	int not_shown;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	upstat(pp, generic_msg, 1);

	/*
	 * Warn the user if 'lpq' will not display this new status-message.
	 * Note that if lock file does not exist, then the queue is enabled
	 * for both queuing and printing.
	 */
	not_shown = 1;
	if (stat(lf, &stbuf) >= 0) {
		if (stbuf.st_mode & LFM_PRINT_DIS)
			not_shown = 0;
	}
	if (not_shown) {
		printf("\tnote: This queue currently has printing enabled,\n");
		printf("\t    so this -msg will only be shown by 'lpq' if\n");
		printf("\t    a job is actively printing on it.\n");
	}
}

/*
 * Enable printing on the specified printer and startup the daemon.
 */
void
start_q(struct printer *pp)
{
	int setres, startok;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	setres = set_qstate(SQS_STARTP, lf);

	PRIV_START
	startok = startdaemon(pp);
	PRIV_END
	if (!startok)
		printf("\tcouldn't start daemon\n");
	else
		printf("\tdaemon started\n");
	PRIV_END
}

/*
 * Print the status of the printer queue.
 */
void
status(struct printer *pp)
{
	struct stat stbuf;
	register int fd, i;
	register struct dirent *dp;
	DIR *dirp;
	char file[MAXPATHLEN];

	printf("%s:\n", pp->printer);
	lock_file_name(pp, file, sizeof file);
	if (stat(file, &stbuf) >= 0) {
		printf("\tqueuing is %s\n",
		       ((stbuf.st_mode & LFM_QUEUE_DIS) ? "disabled"
			: "enabled"));
		printf("\tprinting is %s\n",
		       ((stbuf.st_mode & LFM_PRINT_DIS) ? "disabled"
			: "enabled"));
	} else {
		printf("\tqueuing is enabled\n");
		printf("\tprinting is enabled\n");
	}
	if ((dirp = opendir(pp->spool_dir)) == NULL) {
		printf("\tcannot examine spool directory\n");
		return;
	}
	i = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (*dp->d_name == 'c' && dp->d_name[1] == 'f')
			i++;
	}
	closedir(dirp);
	if (i == 0)
		printf("\tno entries in spool area\n");
	else if (i == 1)
		printf("\t1 entry in spool area\n");
	else
		printf("\t%d entries in spool area\n", i);
	fd = open(file, O_RDONLY);
	if (fd < 0 || flock(fd, LOCK_SH|LOCK_NB) == 0) {
		(void) close(fd);	/* unlocks as well */
		printf("\tprinter idle\n");
		return;
	}
	(void) close(fd);
	/* print out the contents of the status file, if it exists */
	status_file_name(pp, file, sizeof file);
	fd = open(file, O_RDONLY|O_SHLOCK);
	if (fd >= 0) {
		(void) fstat(fd, &stbuf);
		if (stbuf.st_size > 0) {
			putchar('\t');
			while ((i = read(fd, line, sizeof(line))) > 0)
				(void) fwrite(line, 1, i, stdout);
		}
		(void) close(fd);	/* unlocks as well */
	}
}

/*
 * Stop the specified daemon after completing the current job and disable
 * printing.
 */
void
stop_q(struct printer *pp)
{
	int setres;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	setres = set_qstate(SQS_STOPP, lf);

	if (setres >= 0)
		upstat(pp, "printing disabled\n", 0);
}

struct	jobqueue **queue;
int	nitems;
time_t	mtime;

/*
 * Put the specified jobs at the top of printer queue.
 */
void
topq(int argc, char *argv[])
{
	register int i;
	struct stat stbuf;
	int cmdstatus, changed;
	struct printer myprinter, *pp = &myprinter;

	if (argc < 3) {
		printf("usage: topq printer [jobnum ...] [user ...]\n");
		return;
	}

	--argc;
	++argv;
	init_printer(pp);
	cmdstatus = getprintcap(*argv, pp);
	switch(cmdstatus) {
	default:
		fatal(pp, "%s", pcaperr(cmdstatus));
	case PCAPERR_NOTFOUND:
		printf("unknown printer %s\n", *argv);
		return;
	case PCAPERR_TCOPEN:
		printf("warning: %s: unresolved tc= reference(s)", *argv);
		break;
	case PCAPERR_SUCCESS:
		break;
	}
	printf("%s:\n", pp->printer);

	PRIV_START
	if (chdir(pp->spool_dir) < 0) {
		printf("\tcannot chdir to %s\n", pp->spool_dir);
		goto out;
	}
	PRIV_END
	nitems = getq(pp, &queue);
	if (nitems == 0)
		return;
	changed = 0;
	mtime = queue[0]->job_time;
	for (i = argc; --i; ) {
		if (doarg(argv[i]) == 0) {
			printf("\tjob %s is not in the queue\n", argv[i]);
			continue;
		} else
			changed++;
	}
	for (i = 0; i < nitems; i++)
		free(queue[i]);
	free(queue);
	if (!changed) {
		printf("\tqueue order unchanged\n");
		return;
	}
	/*
	 * Turn on the public execute bit of the lock file to
	 * get lpd to rebuild the queue after the current job.
	 */
	PRIV_START
	if (changed && stat(pp->lock_file, &stbuf) >= 0)
		(void) chmod(pp->lock_file, stbuf.st_mode | LFM_RESET_QUE);

out:
	PRIV_END
} 

/*
 * Reposition the job by changing the modification time of
 * the control file.
 */
static int
touch(struct jobqueue *jq)
{
	struct timeval tvp[2];
	int ret;

	tvp[0].tv_sec = tvp[1].tv_sec = --mtime;
	tvp[0].tv_usec = tvp[1].tv_usec = 0;
	PRIV_START
	ret = utimes(jq->job_cfname, tvp);
	PRIV_END
	return (ret);
}

/*
 * Checks if specified job name is in the printer's queue.
 * Returns:  negative (-1) if argument name is not in the queue.
 */
static int
doarg(char *job)
{
	register struct jobqueue **qq;
	register int jobnum, n;
	register char *cp, *machine;
	int cnt = 0;
	FILE *fp;

	/*
	 * Look for a job item consisting of system name, colon, number 
	 * (example: ucbarpa:114)  
	 */
	if ((cp = strchr(job, ':')) != NULL) {
		machine = job;
		*cp++ = '\0';
		job = cp;
	} else
		machine = NULL;

	/*
	 * Check for job specified by number (example: 112 or 235ucbarpa).
	 */
	if (isdigit(*job)) {
		jobnum = 0;
		do
			jobnum = jobnum * 10 + (*job++ - '0');
		while (isdigit(*job));
		for (qq = queue + nitems; --qq >= queue; ) {
			n = 0;
			for (cp = (*qq)->job_cfname+3; isdigit(*cp); )
				n = n * 10 + (*cp++ - '0');
			if (jobnum != n)
				continue;
			if (*job && strcmp(job, cp) != 0)
				continue;
			if (machine != NULL && strcmp(machine, cp) != 0)
				continue;
			if (touch(*qq) == 0) {
				printf("\tmoved %s\n", (*qq)->job_cfname);
				cnt++;
			}
		}
		return(cnt);
	}
	/*
	 * Process item consisting of owner's name (example: henry).
	 */
	for (qq = queue + nitems; --qq >= queue; ) {
		PRIV_START
		fp = fopen((*qq)->job_cfname, "r");
		PRIV_END
		if (fp == NULL)
			continue;
		while (get_line(fp) > 0)
			if (line[0] == 'P')
				break;
		(void) fclose(fp);
		if (line[0] != 'P' || strcmp(job, line+1) != 0)
			continue;
		if (touch(*qq) == 0) {
			printf("\tmoved %s\n", (*qq)->job_cfname);
			cnt++;
		}
	}
	return(cnt);
}

/*
 * Enable both queuing & printing, and start printer (undo `down').
 */
void
up_q(struct printer *pp)
{
	int setres, startok;
	char lf[MAXPATHLEN];

	lock_file_name(pp, lf, sizeof lf);
	printf("%s:\n", pp->printer);

	setres = set_qstate(SQS_ENABLEQ+SQS_STARTP, lf);

	PRIV_START
	startok = startdaemon(pp);
	PRIV_END
	if (!startok)
		printf("\tcouldn't start daemon\n");
	else
		printf("\tdaemon started\n");
}
