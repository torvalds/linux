/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1983, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
"@(#) Copyright (c) 1983, 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)lpr.c	8.4 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 *      lpr -- off line print
 *
 * Allows multiple printers and printers on remote machines by
 * using information from a printer data base.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <netinet/in.h>		/* N_BADMAG uses ntohl() */

#include <dirent.h>
#include <fcntl.h>
#include <a.out.h>
#include <err.h>
#include <locale.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

static char	*cfname;	/* daemon control files, linked from tf's */
static char	*class = local_host;	/* class title on header page */
static char	*dfname;	/* data files */
static char	*fonts[4];	/* troff font names */
static char	 format = 'f';	/* format char for printing files */
static int	 hdr = 1;	/* print header or not (default is yes) */
static int	 iflag;		/* indentation wanted */
static int	 inchar;	/* location to increment char in file names */
static int	 indent;	/* amount to indent */
static const char *jobname;	/* job name on header page */
static int	 mailflg;	/* send mail */
static int	 nact;		/* number of jobs to act on */
static int	 ncopies = 1;	/* # of copies to make */
static char	*lpr_username;  /* person sending the print job(s) */
static int	 qflag;		/* q job, but don't exec daemon */
static int	 rflag;		/* remove files upon completion */
static int	 sflag;		/* symbolic link flag */
static int	 tfd;		/* control file descriptor */
static char	*tfname;	/* tmp copy of cf before linking */
static char	*title;		/* pr'ing title */
static char     *locale;        /* pr'ing locale */
static int	 userid;	/* user id */
static char	*Uflag;		/* user name specified with -U flag */
static char	*width;		/* width for versatec printing */
static char	*Zflag;		/* extra filter options for LPRng servers */

static struct stat statb;

static void	 card(int _c, const char *_p2);
static int	 checkwriteperm(const char *_file, const char *_directory);
static void	 chkprinter(const char *_ptrname, struct printer *_pp);
static void	 cleanup(int _signo);
static void	 copy(const struct printer *_pp, int _f, const char _n[]);
static char	*itoa(int _i);
static const char  *linked(const char *_file);
int		 main(int _argc, char *_argv[]);
static char	*lmktemp(const struct printer *_pp, const char *_id,
		    int _num, int len);
static void	 mktemps(const struct printer *_pp);
static int	 nfile(char *_n);
static int	 test(const char *_file);
static void	 usage(void);

uid_t	uid, euid;

int
main(int argc, char *argv[])
{
	struct passwd *pw;
	struct group *gptr;
	const char *arg, *cp, *printer;
	char *p;
	char buf[BUFSIZ];
	int c, i, f, errs;
	int	 ret, didlink;
	struct stat stb;
	struct stat statb1, statb2;
	struct printer myprinter, *pp = &myprinter;

	printer = NULL;
	euid = geteuid();
	uid = getuid();
	PRIV_END
	if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
		signal(SIGHUP, cleanup);
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		signal(SIGINT, cleanup);
	if (signal(SIGQUIT, SIG_IGN) != SIG_IGN)
		signal(SIGQUIT, cleanup);
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, cleanup);

	progname = argv[0];
	gethostname(local_host, sizeof(local_host));
	openlog("lpd", 0, LOG_LPR);

	errs = 0;
	while ((c = getopt(argc, argv,
			   ":#:1:2:3:4:C:J:L:P:T:U:Z:cdfghi:lnmprstvw:"))
	       != -1)
		switch (c) {
		case '#':		/* n copies */
			i = strtol(optarg, &p, 10);
			if (*p)
				errx(1, "Bad argument to -#, number expected");
			if (i > 0)
				ncopies = i;
			break;

		case '1':		/* troff fonts */
		case '2':
		case '3':
		case '4':
			fonts[optopt - '1'] = optarg;
			break;

		case 'C':		/* classification spec */
			hdr++;
			class = optarg;
			break;

		case 'J':		/* job name */
			hdr++;
			jobname = optarg;
			break;

		case 'P':		/* specifiy printer name */
			printer = optarg;
			break;

		case 'L':               /* pr's locale */
			locale = optarg;
			break;

		case 'T':		/* pr's title line */
			title = optarg;
			break;

		case 'U':		/* user name */
			hdr++;
			Uflag = optarg;
			break;

		case 'Z':
			Zflag = optarg;
			break;

		case 'c':		/* print cifplot output */
		case 'd':		/* print tex output (dvi files) */
		case 'g':		/* print graph(1G) output */
		case 'l':		/* literal output */
		case 'n':		/* print ditroff output */
		case 't':		/* print troff output (cat files) */
		case 'p':		/* print using ``pr'' */
		case 'v':		/* print vplot output */
			format = optopt;
			break;

		case 'f':		/* print fortran output */
			format = 'r';
			break;

		case 'h':		/* nulifiy header page */
			hdr = 0;
			break;

		case 'i':		/* indent output */
			iflag++;
			indent = strtol(optarg, &p, 10);
			if (*p)
				errx(1, "Bad argument to -i, number expected");
			break;

		case 'm':		/* send mail when done */
			mailflg++;
			break;

		case 'q':		/* just queue job */
			qflag++;
			break;

		case 'r':		/* remove file when done */
			rflag++;
			break;

		case 's':		/* try to link files */
			sflag++;
			break;

		case 'w':		/* versatec page width */
			width = optarg;
			break;

		case ':':		/* catch "missing argument" error */
			if (optopt == 'i') {
				iflag++; /* -i without args is valid */
				indent = 8;
			} else
				errs++;
			break;

		default:
			errs++;
		}
	argc -= optind;
	argv += optind;
	if (errs)
		usage();
	if (printer == NULL && (printer = getenv("PRINTER")) == NULL)
		printer = DEFLP;
	chkprinter(printer, pp);
	if (pp->no_copies && ncopies > 1)
		errx(1, "multiple copies are not allowed");
	if (pp->max_copies > 0 && ncopies > pp->max_copies)
		errx(1, "only %ld copies are allowed", pp->max_copies);
	/*
	 * Get the identity of the person doing the lpr using the same
	 * algorithm as lprm.  Actually, not quite -- lprm will override
	 * the login name with "root" if the user is running as root;
	 * the daemon actually checks for the string "root" in its
	 * permission checking.  Sigh.
	 */
	userid = getuid();
	if (Uflag) {
		if (userid != 0 && userid != pp->daemon_user)
			errx(1, "only privileged users may use the `-U' flag");
		lpr_username = Uflag;		/* -U person doing 'lpr' */
	} else {
		lpr_username = getlogin();	/* person doing 'lpr' */
		if (userid != pp->daemon_user || lpr_username == 0) {
			if ((pw = getpwuid(userid)) == NULL)
				errx(1, "Who are you?");
			lpr_username = pw->pw_name;
		}
	}

	/*
	 * Check for restricted group access.
	 */
	if (pp->restrict_grp != NULL && userid != pp->daemon_user) {
		if ((gptr = getgrnam(pp->restrict_grp)) == NULL)
			errx(1, "Restricted group specified incorrectly");
		if (gptr->gr_gid != getgid()) {
			while (*gptr->gr_mem != NULL) {
				if ((strcmp(lpr_username, *gptr->gr_mem)) == 0)
					break;
				gptr->gr_mem++;
			}
			if (*gptr->gr_mem == NULL)
				errx(1, "Not a member of the restricted group");
		}
	}
	/*
	 * Check to make sure queuing is enabled if userid is not root.
	 */
	lock_file_name(pp, buf, sizeof buf);
	if (userid && stat(buf, &stb) == 0 && (stb.st_mode & LFM_QUEUE_DIS))
		errx(1, "Printer queue is disabled");
	/*
	 * Initialize the control file.
	 */
	mktemps(pp);
	tfd = nfile(tfname);
	PRIV_START
	(void) fchown(tfd, pp->daemon_user, -1);
	/* owned by daemon for protection */
	PRIV_END
	card('H', local_host);
	card('P', lpr_username);
	card('C', class);
	if (hdr && !pp->no_header) {
		if (jobname == NULL) {
			if (argc == 0)
				jobname = "stdin";
			else
				jobname = ((arg = strrchr(argv[0], '/'))
					   ? arg + 1 : argv[0]);
		}
		card('J', jobname);
		card('L', lpr_username);
	}
	if (format != 'p' && Zflag != 0)
		card('Z', Zflag);
	if (iflag)
		card('I', itoa(indent));
	if (mailflg)
		card('M', lpr_username);
	if (format == 't' || format == 'n' || format == 'd')
		for (i = 0; i < 4; i++)
			if (fonts[i] != NULL)
				card('1'+i, fonts[i]);
	if (width != NULL)
		card('W', width);
	/*
	 * XXX
	 * Our use of `Z' here is incompatible with LPRng's
	 * use.  We assume that the only use of our existing
	 * `Z' card is as shown for `p' format (pr) files.
	 */
	if (format == 'p') {
		char *s;

		if (locale)
			card('Z', locale);
		else if ((s = setlocale(LC_TIME, "")) != NULL)
			card('Z', s);
	}

	/*
	 * Read the files and spool them.
	 */
	if (argc == 0)
		copy(pp, 0, " ");
	else while (argc--) {
		if (argv[0][0] == '-' && argv[0][1] == '\0') {
			/* use stdin */
			copy(pp, 0, " ");
			argv++;
			continue;
		}
		if ((f = test(arg = *argv++)) < 0)
			continue;	/* file unreasonable */

		if (sflag && (cp = linked(arg)) != NULL) {
			(void)snprintf(buf, sizeof(buf), "%ju %ju",
			    (uintmax_t)statb.st_dev, (uintmax_t)statb.st_ino);
			card('S', buf);
			if (format == 'p')
				card('T', title ? title : arg);
			for (i = 0; i < ncopies; i++)
				card(format, &dfname[inchar-2]);
			card('U', &dfname[inchar-2]);
			if (f)
				card('U', cp);
			card('N', arg);
			dfname[inchar]++;
			nact++;
			continue;
		}
		if (sflag)
			printf("%s: %s: not linked, copying instead\n",
			    progname, arg);

		if (f) {
			/*
			 * The user wants the file removed after it is copied
			 * to the spool area, so see if the file can be moved
			 * instead of copy/unlink'ed.  This is much faster and
			 * uses less spool space than copying the file.  This
			 * can be very significant when running services like
			 * samba, pcnfs, CAP, et al.
			 */
			PRIV_START
			didlink = 0;
			/*
			 * There are several things to check to avoid any
			 * security issues.  Some of these are redundant
			 * under BSD's, but are necessary when lpr is built
			 * under some other OS's (which I do do...)
			 */
			if (lstat(arg, &statb1) < 0)
				goto nohardlink;
			if (S_ISLNK(statb1.st_mode))
				goto nohardlink;
			if (link(arg, dfname) != 0)
				goto nohardlink;
			didlink = 1;
			/*
			 * Make sure the user hasn't tried to trick us via
			 * any race conditions
			 */
			if (lstat(dfname, &statb2) < 0)
				goto nohardlink;
			if (statb1.st_dev != statb2.st_dev)
				goto nohardlink;
			if (statb1.st_ino != statb2.st_ino)
				goto nohardlink;
			/*
			 * Skip if the file already had multiple hard links,
			 * because changing the owner and access-bits would
			 * change ALL versions of the file
			 */
			if (statb2.st_nlink > 2)
				goto nohardlink;
			/*
			 * If we can access and remove the original file
			 * without special setuid-ness then this method is
			 * safe.  Otherwise, abandon the move and fall back
			 * to the (usual) copy method.
			 */
			PRIV_END
			ret = access(dfname, R_OK);
			if (ret == 0)
				ret = unlink(arg);
			PRIV_START
			if (ret != 0)
				goto nohardlink;
			/*
			 * Unlink of user file was successful.  Change the
			 * owner and permissions, add entries to the control
			 * file, and skip the file copying step.
			 */
			chown(dfname, pp->daemon_user, getegid());
			chmod(dfname, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
			PRIV_END
			if (format == 'p')
				card('T', title ? title : arg);
			for (i = 0; i < ncopies; i++)
				card(format, &dfname[inchar-2]);
			card('U', &dfname[inchar-2]);
			card('N', arg);
			nact++;
			continue;
		nohardlink:
			if (didlink)
				unlink(dfname);
			PRIV_END           /* restore old uid */
		} /* end: if (f) */

		if ((i = open(arg, O_RDONLY)) < 0) {
			printf("%s: cannot open %s\n", progname, arg);
		} else {
			copy(pp, i, arg);
			(void) close(i);
			if (f && unlink(arg) < 0)
				printf("%s: %s: not removed\n", progname, arg);
		}
	}

	if (nact) {
		(void) close(tfd);
		tfname[inchar]--;
		/*
		 * Touch the control file to fix position in the queue.
		 */
		PRIV_START
		if ((tfd = open(tfname, O_RDWR)) >= 0) {
			char touch_c;

			if (read(tfd, &touch_c, 1) == 1 &&
			    lseek(tfd, (off_t)0, 0) == 0 &&
			    write(tfd, &touch_c, 1) != 1) {
				printf("%s: cannot touch %s\n", progname,
				    tfname);
				tfname[inchar]++;
				cleanup(0);
			}
			(void) close(tfd);
		}
		if (link(tfname, cfname) < 0) {
			printf("%s: cannot rename %s\n", progname, cfname);
			tfname[inchar]++;
			cleanup(0);
		}
		unlink(tfname);
		PRIV_END
		if (qflag)		/* just q things up */
			exit(0);
		if (!startdaemon(pp))
			printf("jobs queued, but cannot start daemon.\n");
		exit(0);
	}
	cleanup(0);
	return (1);
	/* NOTREACHED */
}

/*
 * Create the file n and copy from file descriptor f.
 */
static void
copy(const struct printer *pp, int f, const char n[])
{
	register int fd, i, nr, nc;
	char buf[BUFSIZ];

	if (format == 'p')
		card('T', title ? title : n);
	for (i = 0; i < ncopies; i++)
		card(format, &dfname[inchar-2]);
	card('U', &dfname[inchar-2]);
	card('N', n);
	fd = nfile(dfname);
	nr = nc = 0;
	while ((i = read(f, buf, BUFSIZ)) > 0) {
		if (write(fd, buf, i) != i) {
			printf("%s: %s: temp file write error\n", progname, n);
			break;
		}
		nc += i;
		if (nc >= BUFSIZ) {
			nc -= BUFSIZ;
			nr++;
			if (pp->max_blocks > 0 && nr > pp->max_blocks) {
				printf("%s: %s: copy file is too large\n",
				    progname, n);
				break;
			}
		}
	}
	(void) close(fd);
	if (nc==0 && nr==0)
		printf("%s: %s: empty input file\n", progname,
		    f ? n : "stdin");
	else
		nact++;
}

/*
 * Try and link the file to dfname. Return a pointer to the full
 * path name if successful.
 */
static const char *
linked(const char *file)
{
	register char *cp;
	static char buf[MAXPATHLEN];
	register int ret;

	if (*file != '/') {
		if (getcwd(buf, sizeof(buf)) == NULL)
			return(NULL);
		while (file[0] == '.') {
			switch (file[1]) {
			case '/':
				file += 2;
				continue;
			case '.':
				if (file[2] == '/') {
					if ((cp = strrchr(buf, '/')) != NULL)
						*cp = '\0';
					file += 3;
					continue;
				}
			}
			break;
		}
		strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
		strncat(buf, file, sizeof(buf) - strlen(buf) - 1);
		file = buf;
	}
	PRIV_START
	ret = symlink(file, dfname);
	PRIV_END
	return(ret ? NULL : file);
}

/*
 * Put a line into the control file.
 */
static void
card(int c, const char *p2)
{
	char buf[BUFSIZ];
	register char *p1 = buf;
	size_t len = 2;

	*p1++ = c;
	while ((c = *p2++) != '\0' && len < sizeof(buf)) {
		*p1++ = (c == '\n') ? ' ' : c;
		len++;
	}
	*p1++ = '\n';
	write(tfd, buf, len);
}

/*
 * Create a new file in the spool directory.
 */
static int
nfile(char *n)
{
	register int f;
	int oldumask = umask(0);		/* should block signals */

	PRIV_START
	f = open(n, O_WRONLY | O_EXCL | O_CREAT, FILMOD);
	(void) umask(oldumask);
	if (f < 0) {
		printf("%s: cannot create %s\n", progname, n);
		cleanup(0);
	}
	if (fchown(f, userid, -1) < 0) {
		printf("%s: cannot chown %s\n", progname, n);
		cleanup(0);	/* cleanup does exit */
	}
	PRIV_END
	if (++n[inchar] > 'z') {
		if (++n[inchar-2] == 't') {
			printf("too many files - break up the job\n");
			cleanup(0);
		}
		n[inchar] = 'A';
	} else if (n[inchar] == '[')
		n[inchar] = 'a';
	return(f);
}

/*
 * Cleanup after interrupts and errors.
 */
static void
cleanup(int signo __unused)
{
	register int i;

	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	i = inchar;
	PRIV_START
	if (tfname)
		do
			unlink(tfname);
		while (tfname[i]-- != 'A');
	if (cfname)
		do
			unlink(cfname);
		while (cfname[i]-- != 'A');
	if (dfname)
		do {
			do
				unlink(dfname);
			while (dfname[i]-- != 'A');
			dfname[i] = 'z';
		} while (dfname[i-2]-- != 'd');
	exit(1);
}

/*
 * Test to see if this is a printable file.
 * Return -1 if it is not, 0 if its printable, and 1 if
 * we should remove it after printing.
 */
static int
test(const char *file)
{
	struct exec execb;
	size_t dlen;
	int fd;
	char *cp, *dirpath;

	if (access(file, 4) < 0) {
		printf("%s: cannot access %s\n", progname, file);
		return(-1);
	}
	if (stat(file, &statb) < 0) {
		printf("%s: cannot stat %s\n", progname, file);
		return(-1);
	}
	if ((statb.st_mode & S_IFMT) == S_IFDIR) {
		printf("%s: %s is a directory\n", progname, file);
		return(-1);
	}
	if (statb.st_size == 0) {
		printf("%s: %s is an empty file\n", progname, file);
		return(-1);
 	}
	if ((fd = open(file, O_RDONLY)) < 0) {
		printf("%s: cannot open %s\n", progname, file);
		return(-1);
	}
	/*
	 * XXX Shall we add a similar test for ELF?
	 */
	if (read(fd, &execb, sizeof(execb)) == sizeof(execb) &&
	    !N_BADMAG(execb)) {
		printf("%s: %s is an executable program", progname, file);
		goto error1;
	}
	(void) close(fd);
	if (rflag) {
		/*
		 * aside: note that 'cp' is technically a 'const char *'
		 * (because it points into 'file'), even though strrchr
		 * returns a value of type 'char *'.
		 */
		if ((cp = strrchr(file, '/')) == NULL) {
			if (checkwriteperm(file,".") == 0)
				return(1);
		} else {
			if (cp == file) {
				fd = checkwriteperm(file,"/");
			} else {
				/* strlcpy will change the '/' to '\0' */
				dlen = cp - file + 1;
				dirpath = malloc(dlen);
				strlcpy(dirpath, file, dlen);
				fd = checkwriteperm(file, dirpath);
				free(dirpath);
			}
			if (fd == 0)
				return(1);
		}
		printf("%s: %s: is not removable by you\n", progname, file);
	}
	return(0);

error1:
	printf(" and is unprintable\n");
	(void) close(fd);
	return(-1);
}

static int
checkwriteperm(const char *file, const char *directory)
{
	struct	stat	stats;
	if (access(directory, W_OK) == 0) {
		stat(directory, &stats);
		if (stats.st_mode & S_ISVTX) {
			stat(file, &stats);
			if(stats.st_uid == userid) {
				return(0);
			}
		} else return(0);
	}
	return(-1);
}

/*
 * itoa - integer to string conversion
 */
static char *
itoa(int i)
{
	static char b[10] = "########";
	register char *p;

	p = &b[8];
	do
		*p-- = i%10 + '0';
	while (i /= 10);
	return(++p);
}

/*
 * Perform lookup for printer name or abbreviation --
 */
static void
chkprinter(const char *ptrname, struct printer *pp)
{
	int status;

	init_printer(pp);
	status = getprintcap(ptrname, pp);
	switch(status) {
	case PCAPERR_OSERR:
	case PCAPERR_TCLOOP:
		errx(1, "%s: %s", ptrname, pcaperr(status));
	case PCAPERR_NOTFOUND:
		errx(1, "%s: unknown printer", ptrname);
	case PCAPERR_TCOPEN:
		warnx("%s: unresolved tc= reference(s)", ptrname);
	}
}

/*
 * Tell the user what we wanna get.
 */
static void
usage(void)
{
	fprintf(stderr, "%s\n",
"usage: lpr [-Pprinter] [-#num] [-C class] [-J job] [-T title] [-U user]\n"
	"\t[-Z daemon-options] [-i[numcols]] [-i[numcols]] [-1234 font]\n"
	"\t[-L locale] [-wnum] [-cdfghlnmprstv] [name ...]");
	exit(1);
}


/*
 * Make the temp files.
 */
static void
mktemps(const struct printer *pp)
{
	register int len, fd, n;
	register char *cp;
	char buf[BUFSIZ];

	(void) snprintf(buf, sizeof(buf), "%s/.seq", pp->spool_dir);
	PRIV_START
	if ((fd = open(buf, O_RDWR|O_CREAT, 0664)) < 0) {
		printf("%s: cannot create %s\n", progname, buf);
		exit(1);
	}
	if (flock(fd, LOCK_EX)) {
		printf("%s: cannot lock %s\n", progname, buf);
		exit(1);
	}
	PRIV_END
	n = 0;
	if ((len = read(fd, buf, sizeof(buf))) > 0) {
		for (cp = buf; len--; ) {
			if (*cp < '0' || *cp > '9')
				break;
			n = n * 10 + (*cp++ - '0');
		}
	}
	len = strlen(pp->spool_dir) + strlen(local_host) + 8;
	tfname = lmktemp(pp, "tf", n, len);
	cfname = lmktemp(pp, "cf", n, len);
	dfname = lmktemp(pp, "df", n, len);
	inchar = strlen(pp->spool_dir) + 3;
	n = (n + 1) % 1000;
	(void) lseek(fd, (off_t)0, 0);
	snprintf(buf, sizeof(buf), "%03d\n", n);
	(void) write(fd, buf, strlen(buf));
	(void) close(fd);	/* unlocks as well */
}

/*
 * Make a temp file name.
 */
static char *
lmktemp(const struct printer *pp, const char *id, int num, int len)
{
	register char *s;

	if ((s = malloc(len)) == NULL)
		errx(1, "out of memory");
	(void) snprintf(s, len, "%s/%sA%03d%s", pp->spool_dir, id, num,
	    local_host);
	return(s);
}
