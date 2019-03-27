/*	$NetBSD: fsck.c,v 1.30 2003/08/07 10:04:15 agc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996 Christos Zoulas. All rights reserved.
 * Copyright (c) 1980, 1989, 1993, 1994
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
 *
 * From: @(#)mount.c	8.19 (Berkeley) 4/19/94
 * From: $NetBSD: mount.c,v 1.24 1995/11/18 03:34:29 cgd Exp 
 * $NetBSD: fsck.c,v 1.30 2003/08/07 10:04:15 agc Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <sys/disk.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fsutil.h"

static enum { IN_LIST, NOT_IN_LIST } which = NOT_IN_LIST;

static TAILQ_HEAD(fstypelist, entry) opthead, selhead;

struct entry {
	char *type;
	char *options;
	TAILQ_ENTRY(entry) entries;
};

static char *options = NULL;
static int flags = 0;
static int forceflag = 0;

static int checkfs(const char *, const char *, const char *, const char *, pid_t *);
static int selected(const char *);
static void addoption(char *);
static const char *getoptions(const char *);
static void addentry(struct fstypelist *, const char *, const char *);
static void maketypelist(char *);
static void catopt(char **, const char *);
static void mangle(char *, int *, const char ** volatile *, int *);
static const char *getfstype(const char *);
static void usage(void) __dead2;
static int isok(struct fstab *);

static struct {
	const char *ptype;
	const char *name;
} ptype_map[] = {
	{ "ufs",	"ffs" },
	{ "ffs",	"ffs" },
	{ "fat",	"msdosfs" },
	{ "efi",	"msdosfs" },
	{ NULL,		NULL },
};

int
main(int argc, char *argv[])
{
	struct fstab *fs;
	int i, rval = 0;
	const char *vfstype = NULL;
	char globopt[3];
	const char *etc_fstab;

	globopt[0] = '-';
	globopt[2] = '\0';

	TAILQ_INIT(&selhead);
	TAILQ_INIT(&opthead);

	etc_fstab = NULL;
	while ((i = getopt(argc, argv, "BCdvpfFnyl:t:T:c:")) != -1)
		switch (i) {
		case 'B':
			if (flags & CHECK_BACKGRD)
				errx(1, "Cannot specify -B and -F.");
			flags |= DO_BACKGRD;
			break;

		case 'd':
			flags |= CHECK_DEBUG;
			break;

		case 'v':
			flags |= CHECK_VERBOSE;
			break;

		case 'F':
			if (flags & DO_BACKGRD)
				errx(1, "Cannot specify -B and -F.");
			flags |= CHECK_BACKGRD;
			break;

		case 'p':
			flags |= CHECK_PREEN;
			/*FALLTHROUGH*/
		case 'C':
			flags |= CHECK_CLEAN;
			/*FALLTHROUGH*/
		case 'n':
		case 'y':
			globopt[1] = i;
			catopt(&options, globopt);
			break;

		case 'f':
			forceflag = 1;
			globopt[1] = i;
			catopt(&options, globopt);
			break;

		case 'l':
			warnx("Ignoring obsolete -l option\n");
			break;

		case 'T':
			if (*optarg)
				addoption(optarg);
			break;

		case 't':
			if (!TAILQ_EMPTY(&selhead))
				errx(1, "only one -t option may be specified.");

			maketypelist(optarg);
			vfstype = optarg;
			break;

		case 'c':
			etc_fstab = optarg;
			break;

		case '?':
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	if (etc_fstab != NULL)
		setfstab(etc_fstab);

	if (argc == 0)
		return checkfstab(flags, isok, checkfs);

#define	BADTYPE(type)							\
	(strcmp(type, FSTAB_RO) &&					\
	    strcmp(type, FSTAB_RW) && strcmp(type, FSTAB_RQ))


	for (; argc--; argv++) {
		const char *spec, *mntpt, *type, *cp;
		char device[MAXPATHLEN];
		struct statfs *mntp;

		mntpt = NULL;
		spec = *argv;
		cp = strrchr(spec, '/');
		if (cp == NULL) {
			(void)snprintf(device, sizeof(device), "%s%s",
				_PATH_DEV, spec);
			spec = device;
		}
		mntp = getmntpt(spec);
		if (mntp != NULL) {
			spec = mntp->f_mntfromname;
			mntpt = mntp->f_mntonname;
		}
		if ((fs = getfsfile(spec)) == NULL &&
		    (fs = getfsspec(spec)) == NULL) {
			if (vfstype == NULL)
				vfstype = getfstype(spec);
			if (vfstype == NULL)
				vfstype = "ufs";
			type = vfstype;
			devcheck(spec);
		} else {
			spec = fs->fs_spec;
			type = fs->fs_vfstype;
			mntpt = fs->fs_file;
			if (BADTYPE(fs->fs_type))
				errx(1, "%s has unknown file system type.",
				    spec);
		}
		if ((flags & CHECK_BACKGRD) &&
		    checkfs(type, spec, mntpt, "-F", NULL) == 0) {
			printf("%s: DEFER FOR BACKGROUND CHECKING\n", *argv);
			continue;
		}
		if ((flags & DO_BACKGRD) && forceflag == 0 &&
		    checkfs(type, spec, mntpt, "-F", NULL) != 0)
			continue;

		rval |= checkfs(type, spec, mntpt, NULL, NULL);
	}

	return rval;
}


static int
isok(struct fstab *fs)
{
	int i;

	if (fs->fs_passno == 0)
		return (0);
	if (BADTYPE(fs->fs_type))
		return (0);
	if (!selected(fs->fs_vfstype))
		return (0);
	/*
	 * If the -B flag has been given, then process the needed
	 * background checks. Background checks cannot be run on
	 * file systems that will be mounted read-only or that were
	 * not mounted at boot time (typically those marked `noauto').
	 * If these basic tests are passed, check with the file system
	 * itself to see if it is willing to do background checking
	 * by invoking its check program with the -F flag.
	 */
	if (flags & DO_BACKGRD) {
		if (!strcmp(fs->fs_type, FSTAB_RO))
			return (0);
		if (getmntpt(fs->fs_spec) == NULL)
			return (0);
		if (checkfs(fs->fs_vfstype, fs->fs_spec, fs->fs_file, "-F", 0))
			return (0);
		return (1);
	}
	/*
	 * If the -F flag has been given, then consider deferring the
	 * check to background. Background checks cannot be run on
	 * file systems that will be mounted read-only or that will
	 * not be mounted at boot time (e.g., marked `noauto'). If
	 * these basic tests are passed, check with the file system
	 * itself to see if it is willing to defer to background
	 * checking by invoking its check program with the -F flag.
	 */
	if ((flags & CHECK_BACKGRD) == 0 || !strcmp(fs->fs_type, FSTAB_RO))
		return (1);
	for (i = strlen(fs->fs_mntops) - 6; i >= 0; i--)
		if (!strncmp(&fs->fs_mntops[i], "noauto", 6))
			break;
	if (i >= 0)
		return (1);
	if (checkfs(fs->fs_vfstype, fs->fs_spec, fs->fs_file, "-F", NULL) != 0)
		return (1);
	printf("%s: DEFER FOR BACKGROUND CHECKING\n", fs->fs_spec);
	return (0);
}


static int
checkfs(const char *pvfstype, const char *spec, const char *mntpt,
    const char *auxopt, pid_t *pidp)
{
	const char ** volatile argv;
	pid_t pid;
	int argc, i, status, maxargc;
	char *optbuf, execbase[MAXPATHLEN];
	char *vfstype = NULL;
	const char *extra = NULL;

#ifdef __GNUC__
	/* Avoid vfork clobbering */
	(void) &optbuf;
	(void) &vfstype;
#endif
	/*
	 * We convert the vfstype to lowercase and any spaces to underscores
	 * to not confuse the issue
	 *
	 * XXX This is a kludge to make automatic filesystem type guessing
	 * from the disklabel work for "4.2BSD" filesystems.  It does a
	 * very limited subset of transliteration to a normalised form of
	 * filesystem name, and we do not seem to enforce a filesystem
	 * name character set.
	 */
	vfstype = strdup(pvfstype);
	if (vfstype == NULL)
		perr("strdup(pvfstype)");
	for (i = 0; i < (int)strlen(vfstype); i++) {
		vfstype[i] = tolower(vfstype[i]);
		if (vfstype[i] == ' ')
			vfstype[i] = '_';
	}

	extra = getoptions(vfstype);
	optbuf = NULL;
	if (options)
		catopt(&optbuf, options);
	if (extra)
		catopt(&optbuf, extra);
	if (auxopt)
		catopt(&optbuf, auxopt);
	else if (flags & DO_BACKGRD)
		catopt(&optbuf, "-B");

	maxargc = 64;
	argv = emalloc(sizeof(char *) * maxargc);

	(void) snprintf(execbase, sizeof(execbase), "fsck_%s", vfstype);
	argc = 0;
	argv[argc++] = execbase;
	if (optbuf)
		mangle(optbuf, &argc, &argv, &maxargc);
	argv[argc++] = spec;
	argv[argc] = NULL;

	if (flags & (CHECK_DEBUG|CHECK_VERBOSE)) {
		(void)printf("start %s %swait", mntpt, 
			pidp ? "no" : "");
		for (i = 0; i < argc; i++)
			(void)printf(" %s", argv[i]);
		(void)printf("\n");
	}

	switch (pid = vfork()) {
	case -1:				/* Error. */
		warn("vfork");
		if (optbuf)
			free(optbuf);
		free(vfstype);
		return (1);

	case 0:					/* Child. */
		if ((flags & CHECK_DEBUG) && auxopt == NULL)
			_exit(0);

		/* Go find an executable. */
		execvP(execbase, _PATH_SYSPATH, __DECONST(char * const *, argv));
		if (spec)
			warn("exec %s for %s in %s", execbase, spec, _PATH_SYSPATH);
		else
			warn("exec %s in %s", execbase, _PATH_SYSPATH);
		_exit(1);
		/* NOTREACHED */

	default:				/* Parent. */
		if (optbuf)
			free(optbuf);

		free(vfstype);

		if (pidp) {
			*pidp = pid;
			return 0;
		}

		if (waitpid(pid, &status, 0) < 0) {
			warn("waitpid");
			return (1);
		}

		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0)
				return (WEXITSTATUS(status));
		}
		else if (WIFSIGNALED(status)) {
			warnx("%s: %s", spec, strsignal(WTERMSIG(status)));
			return (1);
		}
		break;
	}

	return (0);
}


static int
selected(const char *type)
{
	struct entry *e;

	/* If no type specified, it's always selected. */
	TAILQ_FOREACH(e, &selhead, entries)
		if (!strncmp(e->type, type, MFSNAMELEN))
			return which == IN_LIST ? 1 : 0;

	return which == IN_LIST ? 0 : 1;
}


static const char *
getoptions(const char *type)
{
	struct entry *e;

	TAILQ_FOREACH(e, &opthead, entries)
		if (!strncmp(e->type, type, MFSNAMELEN))
			return e->options;
	return "";
}


static void
addoption(char *optstr)
{
	char *newoptions;
	struct entry *e;

	if ((newoptions = strchr(optstr, ':')) == NULL)
		errx(1, "Invalid option string");

	*newoptions++ = '\0';

	TAILQ_FOREACH(e, &opthead, entries)
		if (!strncmp(e->type, optstr, MFSNAMELEN)) {
			catopt(&e->options, newoptions);
			return;
		}
	addentry(&opthead, optstr, newoptions);
}


static void
addentry(struct fstypelist *list, const char *type, const char *opts)
{
	struct entry *e;

	e = emalloc(sizeof(struct entry));
	e->type = estrdup(type);
	e->options = estrdup(opts);
	TAILQ_INSERT_TAIL(list, e, entries);
}


static void
maketypelist(char *fslist)
{
	char *ptr;

	if ((fslist == NULL) || (fslist[0] == '\0'))
		errx(1, "empty type list");

	if (fslist[0] == 'n' && fslist[1] == 'o') {
		fslist += 2;
		which = NOT_IN_LIST;
	}
	else
		which = IN_LIST;

	while ((ptr = strsep(&fslist, ",")) != NULL)
		addentry(&selhead, ptr, "");

}


static void
catopt(char **sp, const char *o)
{
	char *s;
	size_t i, j;

	s = *sp;
	if (s) {
		i = strlen(s);
		j = i + 1 + strlen(o) + 1;
		s = erealloc(s, j);
		(void)snprintf(s + i, j, ",%s", o);
	} else
		s = estrdup(o);
	*sp = s;
}


static void
mangle(char *opts, int *argcp, const char ** volatile *argvp, int *maxargcp)
{
	char *p, *s;
	int argc, maxargc;
	const char **argv;

	argc = *argcp;
	argv = *argvp;
	maxargc = *maxargcp;

	for (s = opts; (p = strsep(&s, ",")) != NULL;) {
		/* Always leave space for one more argument and the NULL. */
		if (argc >= maxargc - 3) {
			maxargc <<= 1;
			argv = erealloc(argv, maxargc * sizeof(char *));
		}
		if (*p != '\0')  {
			if (*p == '-') {
				argv[argc++] = p;
				p = strchr(p, '=');
				if (p) {
					*p = '\0';
					argv[argc++] = p+1;
				}
			} else {
				argv[argc++] = "-o";
				argv[argc++] = p;
			}
		}
	}

	*argcp = argc;
	*argvp = argv;
	*maxargcp = maxargc;
}

static const char *
getfstype(const char *str)
{
	struct diocgattr_arg attr;
	int fd, i;

	if ((fd = open(str, O_RDONLY)) == -1)
		err(1, "cannot open `%s'", str);

	strncpy(attr.name, "PART::type", sizeof(attr.name));
	memset(&attr.value, 0, sizeof(attr.value));
	attr.len = sizeof(attr.value);
	if (ioctl(fd, DIOCGATTR, &attr) == -1) {
		(void) close(fd);
		return(NULL);
	}
	(void) close(fd);
	for (i = 0; ptype_map[i].ptype != NULL; i++)
		if (strstr(attr.value.str, ptype_map[i].ptype) != NULL)
			return (ptype_map[i].name);
	return (NULL);
}


static void
usage(void)
{
	static const char common[] =
	    "[-Cdfnpvy] [-B | -F] [-T fstype:fsoptions] [-t fstype] [-c fstab]";

	(void)fprintf(stderr, "usage: %s %s [special | node] ...\n",
	    getprogname(), common);
	exit(1);
}
