/*	$OpenBSD: fsck.c,v 1.41 2021/07/12 15:09:18 beck Exp $	*/
/*	$NetBSD: fsck.c,v 1.7 1996/10/03 20:06:30 christos Exp $	*/

/*
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
 * From: NetBSD: mount.c,v 1.24 1995/11/18 03:34:29 cgd Exp
 *
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

#include "pathnames.h"
#include "fsutil.h"

static enum { IN_LIST, NOT_IN_LIST } which = NOT_IN_LIST;
static enum { NONET_FILTER, NET_FILTER } filter = NONET_FILTER;

TAILQ_HEAD(fstypelist, entry) opthead, selhead;

struct entry {
	char *type;
	char *options;
	TAILQ_ENTRY(entry) entries;
};

static int maxrun;
static char *options;
static int flags;

int main(int, char *[]);

static int checkfs(const char *, const char *, const char *, void *, pid_t *);
static int selected(const char *);
static void addoption(char *);
static const char *getoptions(const char *);
static void addentry(struct fstypelist *, const char *, const char *);
static void maketypelist(char *);
static char *catopt(char *, const char *, int);
static void mangle(char *, int *, const char ***, int *);
static void usage(void);
static void *isok(struct fstab *);
static int hasopt(const char *, const char *);


int
main(int argc, char *argv[])
{
	const char *errstr;
	struct fstab *fs;
	int i, rval = 0;
	char *vfstype = NULL;
	char *p, globopt[3];
	struct rlimit rl;

	/* Increase our data size to the max */
	if (getrlimit(RLIMIT_DATA, &rl) == 0) {
		if (geteuid() == 0)
			rl.rlim_cur = rl.rlim_max = RLIM_INFINITY;
		else
			rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_DATA, &rl) == -1)
			warn("Can't set resource limit to max data size");
	} else
		warn("Can't get resource limit for data size");

	checkroot();

	if (unveil("/dev", "rw") == -1)
		err(1, "unveil /dev");
	if (unveil(_PATH_FSTAB, "r") == -1)
		err(1, "unveil %s", _PATH_FSTAB);
	if (unveil("/sbin", "x") == -1)
		err(1, "unveil /sbin");
	if (pledge("stdio rpath wpath disklabel proc exec", NULL) == -1)
		err(1, "pledge");

	globopt[0] = '-';
	globopt[2] = '\0';

	TAILQ_INIT(&selhead);
	TAILQ_INIT(&opthead);

	while ((i = getopt(argc, argv, "b:dfl:nNpT:t:vy")) != -1)
		switch (i) {
		case 'd':
			flags |= CHECK_DEBUG;
			break;

		case 'v':
			flags |= CHECK_VERBOSE;
			break;

		case 'p':
			flags |= CHECK_PREEN;
			/*FALLTHROUGH*/
		case 'n':
		case 'f':
		case 'y':
			globopt[1] = i;
			options = catopt(options, globopt, 1);
			break;

		case 'b':
			if (asprintf(&p, "-b %s", optarg) == -1)
				err(1, "malloc failed");
			options = catopt(options, p, 1);
			free(p);
			break;

		case 'l':
			maxrun = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-l %s: %s", optarg, errstr);

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

		case 'N':
			filter = NET_FILTER;
			break;

		case '?':
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		return checkfstab(flags, maxrun, isok, checkfs);

#define	BADTYPE(type)							\
	(strcmp(type, FSTAB_RO) &&					\
	    strcmp(type, FSTAB_RW) && strcmp(type, FSTAB_RQ))


	for (; argc--; argv++) {
		char *spec, *type;

		if ((strncmp(*argv, "/dev/", 5) == 0 || isduid(*argv, 0)) &&
		    (type = readlabelfs(*argv, 0))) {
			spec = *argv;
		} else if ((fs = getfsfile(*argv)) == NULL &&
		    (fs = getfsspec(*argv)) == NULL) {
			if (vfstype == NULL)
				errx(1,
				    "%s: unknown special file or file system.",
				    *argv);
			spec = *argv;
			type = vfstype;
		} else {
			spec = fs->fs_spec;
			type = fs->fs_vfstype;
			if (BADTYPE(fs->fs_type))
				errx(1, "%s has unknown file system type.",
				    *argv);
		}

		rval |= checkfs(type, blockcheck(spec), *argv, NULL, NULL);
	}

	return rval;
}


static void *
isok(struct fstab *fs)
{
	if (fs->fs_passno == 0)
		return NULL;

	if (BADTYPE(fs->fs_type))
		return NULL;

	switch (filter) {
	case NET_FILTER:
		if (!hasopt(fs->fs_mntops, "net"))
			return NULL;
		break;
	case NONET_FILTER:
		if (hasopt(fs->fs_mntops, "net"))
			return NULL;
		break;
	}
	if (!selected(fs->fs_vfstype))
		return NULL;

	return fs;
}


static int
checkfs(const char *vfstype, const char *spec, const char *mntpt, void *auxarg,
    pid_t *pidp)
{
	/* List of directories containing fsck_xxx subcommands. */
	static const char *edirs[] = {
		_PATH_SBIN,
		_PATH_USRSBIN,
		NULL
	};
	const char **argv, **edir;
	pid_t pid;
	int argc, i, status, maxargc;
	char *optbuf = NULL, fsname[PATH_MAX], execname[PATH_MAX];
	const char *extra = getoptions(vfstype);

	if (strcmp(vfstype, "ufs") == 0)
		vfstype = MOUNT_UFS;

	maxargc = 100;
	argv = ereallocarray(NULL, maxargc, sizeof(char *));

	argc = 0;
	(void)snprintf(fsname, sizeof(fsname), "fsck_%s", vfstype);
	argv[argc++] = fsname;

	if (options) {
		if (extra != NULL)
			optbuf = catopt(options, extra, 0);
		else
			optbuf = estrdup(options);
	}
	else if (extra)
		optbuf = estrdup(extra);

	if (optbuf)
		mangle(optbuf, &argc, &argv, &maxargc);

	argv[argc++] = spec;
	argv[argc] = NULL;

	if (flags & (CHECK_DEBUG|CHECK_VERBOSE)) {
		(void)printf("start %s %swait %s", mntpt,
			pidp ? "no" : "", fsname);
		for (i = 1; i < argc; i++)
			(void)printf(" %s", argv[i]);
		(void)printf("\n");
	}

	switch (pid = fork()) {
	case -1:				/* Error. */
		warn("fork");
		free(optbuf);
		free(argv);
		return (1);

	case 0:					/* Child. */
		if (flags & CHECK_DEBUG)
			_exit(0);

		/* Go find an executable. */
		edir = edirs;
		do {
			(void)snprintf(execname,
			    sizeof(execname), "%s/fsck_%s", *edir, vfstype);
			execv(execname, (char * const *)argv);
			if (errno != ENOENT) {
				if (spec)
					warn("exec %s for %s", execname, spec);
				else
					warn("exec %s", execname);
			}
		} while (*++edir != NULL);

		if (errno == ENOENT) {
			if (spec)
				warn("exec %s for %s", execname, spec);
			else
				warn("exec %s", execname);
		}
		exit(1);
		/* NOTREACHED */

	default:				/* Parent. */
		free(optbuf);
		free(argv);

		if (pidp) {
			*pidp = pid;
			return 0;
		}

		if (waitpid(pid, &status, 0) == -1) {
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
			e->options = catopt(e->options, newoptions, 1);
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


static char *
catopt(char *s0, const char *s1, int fr)
{
	char *cp;

	if (s0 && *s0) {
		if (asprintf(&cp, "%s,%s", s0, s1) == -1)
			err(1, "malloc failed");
	} else
		cp = estrdup(s1);

	if (fr)
		free(s0);
	return (cp);
}


static void
mangle(char *opts, int *argcp, const char ***argvp, int *maxargcp)
{
	char *p, *s;
	int argc = *argcp, maxargc = *maxargcp;
	const char **argv = *argvp;

	for (s = opts; (p = strsep(&s, ",")) != NULL;) {
		/* always leave space for one more argument and the NULL */
		if (argc >= maxargc - 3) {
			int newmaxargc = maxargc + 50;

			argv = ereallocarray(argv, newmaxargc, sizeof(char *));
			maxargc = newmaxargc;
		}
		if (*p != '\0') {
			if (*p == '-') {
				argv[argc++] = p;
				p = strchr(p, '=');
				if (p) {
					*p = '\0';
					argv[argc++] = p+1;
				}
			}
			else {
				argv[argc++] = "-o";
				argv[argc++] = p;
			}
		}
	}

	*argcp = argc;
	*argvp = argv;
	*maxargcp = maxargc;
}

static int
hasopt(const char *mntopts, const char *option)
{
	int found;
	char *opt, *optbuf;

	if (mntopts == NULL)
		return (0);
	optbuf = strdup(mntopts);
	found = 0;
	for (opt = optbuf; !found && opt != NULL; strsep(&opt, ","))
		found = !strncmp(opt, option, strlen(option));
	free(optbuf);
	return (found);
}


static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s "
	    "[-dfNnpvy] [-b block#] [-l maxparallel] [-T fstype:fsoptions]\n"
	    "            [-t fstype] [special | node ...]\n", __progname);
	exit(1);
}
