/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Stanislav Sedov <stas@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <libprocstat.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "functions.h"

/*
 * File access mode flags table.
 */
static const struct {
	int	flag;
	char	ch;
} fflags[] = {
	{PS_FST_FFLAG_WRITE,	'w'},
	{PS_FST_FFLAG_APPEND,	'a'},
	{PS_FST_FFLAG_DIRECT,	'd'},
	{PS_FST_FFLAG_SHLOCK,	's'},
	{PS_FST_FFLAG_EXLOCK,	'e'}
};
#define	NFFLAGS	(sizeof(fflags) / sizeof(*fflags))

/*
 * Usage flags translation table.
 */
static const struct {
	int	flag;
	char	ch;
} uflags[] = {
	{PS_FST_UFLAG_RDIR,	'r'},
	{PS_FST_UFLAG_CDIR,	'c'},
	{PS_FST_UFLAG_JAIL,	'j'},
	{PS_FST_UFLAG_TRACE,	't'},
	{PS_FST_UFLAG_TEXT,	'x'},
	{PS_FST_UFLAG_MMAP,	'm'},
	{PS_FST_UFLAG_CTTY,	'y'}
};
#define	NUFLAGS	(sizeof(uflags) / sizeof(*uflags))

struct consumer {
	pid_t	pid;
	uid_t	uid;
	int	fd;
	int	flags;
	int	uflags;
	STAILQ_ENTRY(consumer)	next;
};
struct reqfile {
	uint32_t	fsid;
	uint64_t	fileid;
	const char	*name;
	STAILQ_HEAD(, consumer) consumers;
};

/*
 * Option flags.
 */
#define	UFLAG	0x01	/* -u flag: show users				*/
#define	FFLAG	0x02	/* -f flag: specified files only		*/
#define	CFLAG	0x04	/* -c flag: treat as mpoints			*/
#define	MFLAG	0x10	/* -m flag: mmapped files too			*/
#define	KFLAG	0x20	/* -k flag: send signal (SIGKILL by default)	*/

static int flags = 0;	/* Option flags. */

static void	printflags(struct consumer *consumer);
static int	str2sig(const char *str);
static void	usage(void) __dead2;
static int	addfile(const char *path, struct reqfile *reqfile);
static void	dofiles(struct procstat *procstat, struct kinfo_proc *kp,
    struct reqfile *reqfiles, size_t nfiles);

static void
usage(void)
{

	fprintf(stderr,
"usage: fuser [-cfhkmu] [-M core] [-N system] [-s signal] file ...\n");
	exit(EX_USAGE);
}

static void
printflags(struct consumer *cons)
{
	unsigned int i;

	assert(cons);
	for (i = 0; i < NUFLAGS; i++)
		if ((cons->uflags & uflags[i].flag) != 0)
			fputc(uflags[i].ch, stderr);
	for (i = 0; i < NFFLAGS; i++)
		if ((cons->flags & fflags[i].flag) != 0)
			fputc(fflags[i].ch, stderr);
}

/*
 * Add file to the list.
 */
static int
addfile(const char *path, struct reqfile *reqfile)
{
	struct stat sb;

	assert(path);
	if (stat(path, &sb) != 0) {
		warn("%s", path);
		return (1);
	}
	reqfile->fileid = sb.st_ino;
	reqfile->fsid = sb.st_dev;
	reqfile->name = path;
	STAILQ_INIT(&reqfile->consumers);
	return (0);
}

int
do_fuser(int argc, char *argv[])
{
	struct consumer *consumer;
	struct kinfo_proc *p, *procs;
	struct procstat *procstat;
	struct reqfile *reqfiles;
	char *ep, *nlistf, *memf;
	int ch, cnt, sig;
	unsigned int i, nfiles;

	sig = SIGKILL;	/* Default to kill. */
	nlistf = NULL;
	memf = NULL;
	while ((ch = getopt(argc, argv, "M:N:cfhkms:u")) != -1)
		switch(ch) {
		case 'f':
			if ((flags & CFLAG) != 0)
				usage();
			flags |= FFLAG;
			break;
		case 'c':
			if ((flags & FFLAG) != 0)
				usage();
			flags |= CFLAG;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'u':
			flags |= UFLAG;
			break;
		case 'm':
			flags |= MFLAG;
			break;
		case 'k':
			flags |= KFLAG;
			break;
		case 's':
			if (isdigit(*optarg)) {
				sig = strtol(optarg, &ep, 10);
				if (*ep != '\0' || sig < 0 || sig >= sys_nsig)
					errx(EX_USAGE, "illegal signal number" ": %s",
					    optarg);
			} else {
				sig = str2sig(optarg);
				if (sig < 0)
					errx(EX_USAGE, "illegal signal name: "
					    "%s", optarg);
			}
			break;
		case 'h':
			/* PASSTHROUGH */
		default:
			usage();
			/* NORETURN */
		}
	argv += optind;
	argc -= optind;

	assert(argc >= 0);
	if (argc == 0)
		usage();
		/* NORETURN */

	/*
	 * Process named files.
	 */
	reqfiles = malloc(argc * sizeof(struct reqfile));
	if (reqfiles == NULL)
		err(EX_OSERR, "malloc()");
	nfiles = 0;
	while (argc--)
		if (!addfile(*(argv++), &reqfiles[nfiles]))
			nfiles++;
	if (nfiles == 0)
		errx(EX_IOERR, "files not accessible");

	if (memf != NULL)
		procstat = procstat_open_kvm(nlistf, memf);
	else
		procstat = procstat_open_sysctl();
	if (procstat == NULL)
		errx(1, "procstat_open()");
	procs = procstat_getprocs(procstat, KERN_PROC_PROC, 0, &cnt);
	if (procs == NULL)
		 errx(1, "procstat_getprocs()");

	/*
	 * Walk through process table and look for matching files.
	 */
	p = procs;
	while(cnt--)
		if (p->ki_stat != SZOMB)
			dofiles(procstat, p++, reqfiles, nfiles);

	for (i = 0; i < nfiles; i++) {
		fprintf(stderr, "%s:", reqfiles[i].name);
		fflush(stderr);
		STAILQ_FOREACH(consumer, &reqfiles[i].consumers, next) {
			if (consumer->flags != 0) {
				fprintf(stdout, "%6d", consumer->pid);
				fflush(stdout);
				printflags(consumer);
				if ((flags & UFLAG) != 0)
					fprintf(stderr, "(%s)",
					    user_from_uid(consumer->uid, 0));
				if ((flags & KFLAG) != 0)
					kill(consumer->pid, sig);
				fflush(stderr);
			}
		}
		(void)fprintf(stderr, "\n");
	}
	procstat_freeprocs(procstat, procs);
	procstat_close(procstat);
	free(reqfiles);
	return (0);
}

static void
dofiles(struct procstat *procstat, struct kinfo_proc *kp,
    struct reqfile *reqfiles, size_t nfiles)
{
	struct vnstat vn;
	struct consumer *cons;
	struct filestat *fst;
	struct filestat_list *head;
	int error, match;
	unsigned int i;
	char errbuf[_POSIX2_LINE_MAX];
	
	head = procstat_getfiles(procstat, kp, flags & MFLAG);
	if (head == NULL)
		return;
	STAILQ_FOREACH(fst, head, next) {
		if (fst->fs_type != PS_FST_TYPE_VNODE)
			continue;
		error = procstat_get_vnode_info(procstat, fst, &vn, errbuf);
		if (error != 0)
			continue;
		for (i = 0; i < nfiles; i++) {
			if (flags & CFLAG && reqfiles[i].fsid == vn.vn_fsid) {
				break;
			}
			else if (reqfiles[i].fsid == vn.vn_fsid &&
			    reqfiles[i].fileid == vn.vn_fileid) {
				break;
			}
			else if (!(flags & FFLAG) &&
			    (vn.vn_type == PS_FST_VTYPE_VCHR ||
			    vn.vn_type == PS_FST_VTYPE_VBLK) &&
			    vn.vn_fsid == reqfiles[i].fileid) {
				break;
			}
		}
		if (i == nfiles)
			continue;	/* No match. */

		/*
		 * Look for existing entries.
		 */
		match = 0;
		STAILQ_FOREACH(cons, &reqfiles[i].consumers, next)
			if (cons->pid == kp->ki_pid) {
				match = 1;
				break;
			}
		if (match == 1) {	/* Use old entry. */
			cons->flags |= fst->fs_fflags;
			cons->uflags |= fst->fs_uflags;
		} else {
			/*
			 * Create new entry in the consumer chain.
			 */
			cons = calloc(1, sizeof(struct consumer));
			if (cons == NULL) {
				warn("malloc()");
				continue;
			}
			cons->uid = kp->ki_uid;
			cons->pid = kp->ki_pid;
			cons->uflags = fst->fs_uflags;
			cons->flags = fst->fs_fflags;
			STAILQ_INSERT_TAIL(&reqfiles[i].consumers, cons, next);
		}
	}
	procstat_freefiles(procstat, head);
}

/*
 * Returns signal number for it's string representation.
 */
static int
str2sig(const char *str)
{
	int i;

	if (!strncasecmp(str, "SIG", 3))
		str += 3;
	for (i = 1; i < sys_nsig; i++) {
                if (!strcasecmp(sys_signame[i], str))
                        return (i);
        }
        return (-1);
}
