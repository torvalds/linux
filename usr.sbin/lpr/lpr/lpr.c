/*	$OpenBSD: lpr.c,v 1.50 2022/12/28 21:30:17 jmc Exp $ */
/*	$NetBSD: lpr.c,v 1.19 2000/10/11 20:23:52 is Exp $	*/

/*
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
 *      lpr -- off line print
 *
 * Allows multiple printers and printers on remote machines by
 * using information from a printer data base.
 */

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <err.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

static char	*cfname;	/* daemon control files, linked from tf's */
static char	*class = host;	/* class title on header page */
static char	*dfname;	/* data files */
static char	*fonts[4];	/* troff font names */
static char	 format = 'f';	/* format char for printing files */
static int	 hdr = 1;	/* print header or not (default is yes) */
static int	 iflag;		/* indentation wanted */
static int	 inchar;	/* location to increment char in file names */
static int	 indent;	/* amount to indent */
static char	*jobname;	/* job name on header page */
static int	 mailflg;	/* send mail */
static int	 nact;		/* number of jobs to act on */
static int	 ncopies = 1;	/* # of copies to make */
static const char *person;	/* user name */
static int	 qflag;		/* q job, but don't exec daemon */
static int	 rflag;		/* remove files upon completion */	
static int	 sflag;		/* symbolic link flag */
static int	 tfd;		/* control file descriptor */
static char	*tfname;	/* tmp copy of cf before linking */
static char	*title;		/* pr'ing title */
static char	*width;		/* width for versatec printing */

static struct stat statb;

volatile sig_atomic_t gotintr;

static void	 card(int, const char *);
static void	 chkprinter(char *);
static void	 cleanup(int);
static void	 copy(int, char *);
static char	*itoa(int);
static char	*linked(char *);
static char	*lmktemp(char *, int);
static void	 mktemps(void);
static int	 nfile(char *);
static int	 test(char *);
static __dead void usage(void);

int
main(int argc, char **argv)
{
	struct passwd *pw;
	struct group *gptr;
	char *arg, *cp;
	char buf[PATH_MAX];
	int i, f, ch;
	struct stat stb;

	/*
	 * Simulate setuid daemon w/ PRIV_END called.
	 * We don't want lpr to actually be setuid daemon since that
	 * requires that the lpr binary be owned by user daemon, which
	 * is potentially unsafe.
	 */
	if ((pw = getpwuid(DEFUID)) == NULL)
		errx(1, "daemon uid (%u) not in password file", DEFUID);
	effective_uid = pw->pw_uid;
	real_uid = getuid();
	effective_gid = pw->pw_gid;
	real_gid = getgid();
	setresgid(real_gid, real_gid, effective_gid);
	setresuid(real_uid, real_uid, effective_uid);

	if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
		signal(SIGHUP, cleanup);
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		signal(SIGINT, cleanup);
	if (signal(SIGQUIT, SIG_IGN) != SIG_IGN)
		signal(SIGQUIT, cleanup);
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, cleanup);

	gethostname(host, sizeof (host));
	openlog("lpr", 0, LOG_LPR);

	while ((ch = getopt(argc, argv,
	    ":#:1:2:3:4:C:J:P:T:U:cdfghi:lmnpqrstvw:")) != -1) {
		switch (ch) {

		case '#':		/* n copies */
			if (isdigit((unsigned char)*optarg)) {
				i = atoi(optarg);
				if (i > 0)
					ncopies = i;
			}
			break;

		case '4':		/* troff fonts */
		case '3':
		case '2':
		case '1':
			fonts[ch - '1'] = optarg;
			break;

		case 'C':		/* classification spec */
			hdr++;
			class = optarg;
			break;

		case 'J':		/* job name */
			hdr++;
			jobname = optarg;
			break;

		case 'P':		/* specify printer name */
			printer = optarg;
			break;

		case 'T':		/* pr's title line */
			title = optarg;
			break;

		case 'U':		/* user name */
			hdr++;
			person = optarg;
			break;

		case 'c':		/* print cifplot output */
		case 'd':		/* print tex output (dvi files) */
		case 'g':		/* print graph(1G) output */
		case 'l':		/* literal output */
		case 'n':		/* print ditroff output */
		case 'p':		/* print using ``pr'' */
		case 't':		/* print troff output (cat files) */
		case 'v':		/* print vplot output */
			format = ch;
			break;

		case 'f':		/* print fortran output */
			format = 'r';
			break;

		case 'h':		/* toggle want of header page */
			hdr = !hdr;
			break;

		case 'i':		/* indent output */
			iflag++;
			indent = atoi(optarg);
			if (indent < 0)
				indent = 8;
			break;

		case 'm':		/* send mail when done */
			mailflg = 1;
			break;

		case 'q':		/* just q job */
			qflag = 1;
			break;

		case 'r':		/* remove file when done */
			rflag = 1;
			break;

		case 's':		/* try to link files */
			sflag = 1;
			break;

		case 'w':		/* versatec page width */
			width = optarg;
			break;

		case ':':               /* catch "missing argument" error */
			if (optopt == 'i') {
				iflag++; /* -i without args is valid */
				indent = 8;
			} else
				usage();
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (printer == NULL && (printer = getenv("PRINTER")) == NULL)
		printer = DEFLP;
	chkprinter(printer);
	if (SC && ncopies > 1)
		errx(1, "multiple copies are not allowed");
	if (MC > 0 && ncopies > MC)
		errx(1, "only %ld copies are allowed", MC);
	/*
	 * Get the identity of the person doing the lpr using the same
	 * algorithm as lprm. 
	 */
	if (real_uid != DU || person == NULL) {
		if ((pw = getpwuid(real_uid)) == NULL)
			errx(1, "Who are you?");
		if ((person = strdup(pw->pw_name)) == NULL)
			err(1, NULL);
	}
	/*
	 * Check for restricted group access.
	 */
	if (RG != NULL && real_uid != DU) {
		if ((gptr = getgrnam(RG)) == NULL)
			errx(1, "Restricted group specified incorrectly");
		if (gptr->gr_gid != getgid()) {
			while (*gptr->gr_mem != NULL) {
				if ((strcmp(person, *gptr->gr_mem)) == 0)
					break;
				gptr->gr_mem++;
			}
			if (*gptr->gr_mem == NULL)
				errx(1, "Not a member of the restricted group");
		}
	}
	/*
	 * Check to make sure queuing is enabled if real_uid is not root.
	 */
	(void)snprintf(buf, sizeof(buf), "%s/%s", SD, LO);
	if (real_uid && stat(buf, &stb) == 0 && (stb.st_mode & 010))
		errx(1, "Printer queue is disabled");
	/*
	 * Initialize the control file.
	 */
	mktemps();
	tfd = nfile(tfname);
	card('H', host);
	card('P', person);
	if (hdr) {
		if (jobname == NULL) {
			if (argc == 0)
				jobname = "stdin";
			else
				jobname = (arg = strrchr(argv[0], '/')) ?
				    arg + 1 : argv[0];
		}
		card('J', jobname);
		card('C', class);
		if (!SH)
			card('L', person);
	}
	if (iflag)
		card('I', itoa(indent));
	if (mailflg)
		card('M', person);
	if (format == 't' || format == 'n' || format == 'd')
		for (i = 0; i < 4; i++)
			if (fonts[i] != NULL)
				card('1'+i, fonts[i]);
	if (width != NULL)
		card('W', width);

	/*
	 * Read the files and spool them.
	 */
	if (argc == 0)
		copy(0, " ");
	else while (argc--) {
		if (argv[0][0] == '-' && argv[0][1] == '\0') {
			/* use stdin */
			copy(0, " ");
			argv++;
			continue;
		}
		if ((f = test(arg = *argv++)) < 0)
			continue;	/* file unreasonable */

		if (sflag && (cp = linked(arg)) != NULL) {
			(void)snprintf(buf, sizeof(buf), "%d %llu",
			    statb.st_dev, (unsigned long long)statb.st_ino);
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
			warnx("%s: not linked, copying instead", arg);
		if ((i = safe_open(arg, O_RDONLY, 0)) < 0)
			warn("%s", arg);
		else {
			copy(i, arg);
			(void)close(i);
			if (f && unlink(arg) < 0)
				warnx("%s: not removed", arg);
		}
	}

	if (nact) {
		(void)close(tfd);
		tfname[inchar]--;
		/*
		 * Touch the control file to fix position in the queue.
		 */
		PRIV_START;
		if ((tfd = safe_open(tfname, O_RDWR|O_NOFOLLOW, 0)) >= 0) {
			char c;

			if (read(tfd, &c, 1) == 1 &&
			    lseek(tfd, (off_t)0, SEEK_SET) == 0 &&
			    write(tfd, &c, 1) != 1) {
				warn("%s", tfname);
				tfname[inchar]++;
				cleanup(0);
			}
			(void)close(tfd);
		}
		if (link(tfname, cfname) < 0) {
			warn("cannot rename %s", cfname);
			tfname[inchar]++;
			cleanup(0);
		}
		unlink(tfname);
		PRIV_END;
		if (qflag)		/* just q things up */
			exit(0);
		if (!startdaemon(printer))
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
copy(int f, char *n)
{
	int fd, i, nr, nc;
	char buf[BUFSIZ];

	if (format == 'p')
		card('T', title ? title : n);
	for (i = 0; i < ncopies; i++)
		card(format, &dfname[inchar-2]);
	card('U', &dfname[inchar-2]);
	card('N', n);
	fd = nfile(dfname);
	nr = nc = 0;
	while ((i = read(f, buf, sizeof(buf))) > 0) {
		if (write(fd, buf, i) != i) {
			warn("%s", n);
			break;
		}
		nc += i;
		if (nc >= sizeof(buf)) {
			nc -= sizeof(buf);
			nr++;
			if (MX > 0 && nr > MX) {
				warnx("%s: copy file is too large", n);
				break;
			}
		}
	}
	(void)close(fd);
	if (nc == 0 && nr == 0) 
		warnx("%s: empty input file", f ? n : "stdin");
	else
		nact++;
}

/*
 * Try and link the file to dfname. Return a pointer to the full
 * path name if successful.
 */
static char *
linked(char *file)
{
	char *cp;
	static char buf[PATH_MAX];
	int ret;

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
		if (strlcat(buf, "/", sizeof(buf)) >= sizeof(buf) ||
		    strlcat(buf, file, sizeof(buf)) >= sizeof(buf))
			return(NULL);
		file = buf;
	}
	PRIV_START;
	ret = symlink(file, dfname);
	PRIV_END;
	return(ret ? NULL : file);
}

/*
 * Put a line into the control file.
 */
static void
card(int c, const char *p2)
{
	char buf[BUFSIZ];
	char *p1 = buf;
	int len = 2;

	if (strlen(p2) > sizeof(buf) - 2)
		errx(1, "Internal error:  String longer than %ld",
		    (long)sizeof(buf));

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
	int f;
	int oldumask = umask(0);		/* should block signals */

	PRIV_START;
	f = open(n, O_WRONLY|O_EXCL|O_CREAT, FILMOD);
	(void)umask(oldumask);
	if (f < 0) {
		warn("%s", n);
		cleanup(0);
	}
	PRIV_END;
	if (++n[inchar] > 'z') {
		if (++n[inchar-2] == 't') {
			warnx("too many files - break up the job");
			cleanup(0);
		}
		n[inchar] = 'A';
	} else if (n[inchar] == '[')
		n[inchar] = 'a';
	return (f);
}

/*
 * Cleanup after interrupts and errors.
 */
static void
cleanup(int signo)
{
	int i;

	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	i = inchar;
	PRIV_START;
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
	_exit(1);
}

/*
 * Test to see if this is a printable file.
 * Return -1 if it is not, 0 if its printable, and 1 if
 * we should remove it after printing.
 */
static int
test(char *file)
{
	int fd;
	char *cp;

	if ((fd = open(file, O_RDONLY|O_NONBLOCK)) < 0) {
		warn("cannot open %s", file);
		goto bad;
	}
	if (fstat(fd, &statb) < 0) {
		warn("cannot stat %s", file);
		goto bad;
	}
	if (S_ISDIR(statb.st_mode)) {
		warnx("%s is a directory", file);
		goto bad;
	}
	if (!S_ISREG(statb.st_mode)) {
		warnx("%s is not a regular file", file);
		goto bad;
	}
	if (statb.st_size == 0) {
		warnx("%s is an empty file", file);
		goto bad;
 	}
	(void)close(fd);
	if (rflag) {
		if ((cp = strrchr(file, '/')) == NULL) {
			if (access(".", 2) == 0)
				return(1);
		} else {
			if (cp == file) {
				fd = access("/", 2);
			} else {
				*cp = '\0';
				fd = access(file, 2);
				*cp = '/';
			}
			if (fd == 0)
				return(1);
		}
		warnx("%s is not removable by you", file);
	}
	return(0);
bad:
	return(-1);
}

/*
 * itoa - integer to string conversion
 */
static char *
itoa(int i)
{
	static char b[10] = "########";
	char *p;

	p = &b[8];
	do
		*p-- = i%10 + '0';
	while (i /= 10)
		;
	return(++p);
}

/*
 * Perform lookup for printer name or abbreviation --
 */
static void
chkprinter(char *s)
{
	int status;

	if ((status = cgetent(&bp, printcapdb, s)) == -2)
		errx(1, "cannot open printer description file");
	else if (status == -1)
		errx(1, "%s: unknown printer", s);
	if (cgetstr(bp, "sd", &SD) == -1)
		SD = _PATH_DEFSPOOL;
	if (cgetstr(bp, "lo", &LO) == -1)
		LO = DEFLOCK;
	cgetstr(bp, "rg", &RG);
	if (cgetnum(bp, "mx", &MX) < 0)
		MX = DEFMX;
	if (cgetnum(bp, "mc", &MC) < 0)
		MC = DEFMAXCOPIES;
	if (cgetnum(bp, "du", &DU) < 0)
		DU = DEFUID;
	SC = (cgetcap(bp, "sc", ':') != NULL);
	SH = (cgetcap(bp, "sh", ':') != NULL);
}

/*
 * Make the temp files.
 */
static void
mktemps(void)
{
	int len, fd, n;
	char *cp;
	char buf[BUFSIZ];
	struct stat stb;

	if (snprintf(buf, sizeof(buf), "%s/.seq", SD) >= sizeof(buf))
		errc(1, ENAMETOOLONG, "%s/.seq", SD);
	PRIV_START;
	if ((fd = safe_open(buf, O_RDWR|O_CREAT|O_NOFOLLOW, 0661)) < 0)
		err(1, "cannot open %s", buf);
	if (flock(fd, LOCK_EX))
		err(1, "cannot lock %s", buf);
	PRIV_END;
	n = 0;
	if ((len = read(fd, buf, sizeof(buf))) > 0) {
		for (cp = buf; len--; ) {
			if (*cp < '0' || *cp > '9')
				break;
			n = n * 10 + (*cp++ - '0');
		}
	}
	do {
		tfname = lmktemp("tf", n);
		cfname = lmktemp("cf", n);
		dfname = lmktemp("df", n);
		n = (n + 1) % 1000;
	} while (stat(tfname, &stb) == 0 || stat(cfname, &stb) == 0 ||
	    stat(dfname, &stb) == 0);
	inchar = strlen(SD) + 3;
	(void)lseek(fd, (off_t)0, SEEK_SET);
	snprintf(buf, sizeof(buf), "%03d\n", n);
	(void)write(fd, buf, strlen(buf));
	(void)close(fd);	/* unlocks as well */
}

/*
 * Make a temp file name.
 */
static char *
lmktemp(char *id, int num)
{
	char *s;

	if (asprintf(&s, "%s/%sA%03d%s", SD, id, num, host) == -1)
		err(1, NULL);

	return(s);
}

static __dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-cdfghlmnpqrstv] [-#num] [-1234 font] "
	    "[-C class] [-i [numcols]]\n"
	    "\t[-J job] [-Pprinter] [-T title] [-U user] "
	    "[-wnum] [name ...]\n", __progname);
	exit(1);
}
