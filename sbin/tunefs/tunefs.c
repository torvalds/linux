/*	$OpenBSD: tunefs.c,v 1.41 2016/05/28 23:44:27 tb Exp $	*/
/*	$NetBSD: tunefs.c,v 1.33 2005/01/19 20:46:16 xtraeme Exp $	*/

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

/*
 * tunefs: change layout parameters to an existing file system.
 */
#include <sys/param.h>	/* DEV_BSIZE MAXBSIZE */

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

/* the optimization warning string template */
#define	OPTWARN	"should optimize for %s with minfree %s %d%%"

union {
	struct	fs sb;
	char pad[MAXBSIZE];
} sbun;
#define	sblock sbun.sb
char buf[MAXBSIZE];

int	fi;
int	is_ufs2 = 0;
off_t	sblockloc;

static off_t sblock_try[] = SBLOCKSEARCH;

static	void	bwrite(daddr_t, char *, int, const char *);
static	void	bread(daddr_t, char *, int, const char *);
static	int	getnum(const char *, const char *, int, int);
static	void	getsb(struct fs *, const char *);
static	int	openpartition(char *, int, char **);
static	void	usage(void);

int
main(int argc, char *argv[])
{
#define	OPTSTRING	"AFNe:g:h:m:o:"
	int		i, ch, Aflag, Fflag, Nflag, openflags;
	char		*special;
	const char	*chg[2];
	int		maxbpg, minfree, optim;
	int		avgfilesize, avgfpdir;

	Aflag = Fflag = Nflag = 0;
	maxbpg = minfree = optim = -1;
	avgfilesize = avgfpdir = -1;
	chg[FS_OPTSPACE] = "space";
	chg[FS_OPTTIME] = "time";

	while ((ch = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (ch) {

		case 'A':
			Aflag = 1;
			break;

		case 'F':
			Fflag = 1;
			break;

		case 'N':
			Nflag = 1;
			break;

		case 'e':
			maxbpg = getnum(optarg,
			    "maximum blocks per file in a cylinder group",
			    1, INT_MAX);
			break;

		case 'g':
			avgfilesize = getnum(optarg,
			    "average file size", 1, INT_MAX);
			break;

		case 'h':
			avgfpdir = getnum(optarg,
			    "expected number of files per directory",
			    1, INT_MAX);
			break;

		case 'm':
			minfree = getnum(optarg,
			    "minimum percentage of free space", 0, 99);
			break;

		case 'o':
			if (strcmp(optarg, chg[FS_OPTSPACE]) == 0)
				optim = FS_OPTSPACE;
			else if (strcmp(optarg, chg[FS_OPTTIME]) == 0)
				optim = FS_OPTTIME;
			else
				errx(10,
				    "bad %s (options are `space' or `time')",
				    "optimization preference");
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind; 
	if (argc != 1)
		usage();

	special = argv[0];
	openflags = Nflag ? O_RDONLY : O_RDWR;
	if (Fflag)
		fi = open(special, openflags);
	else
		fi = openpartition(special, openflags, &special);
	if (fi == -1)
		err(1, "%s", special);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	getsb(&sblock, special);

#define CHANGEVAL(old, new, type, suffix) do				\
	if ((new) != -1) {						\
		if ((new) == (old))					\
			warnx("%s remains unchanged at %d%s",		\
			    (type), (old), (suffix));			\
		else {							\
			warnx("%s changes from %d%s to %d%s",		\
			    (type), (old), (suffix), (new), (suffix));	\
			(old) = (new);					\
		}							\
	} while (/* CONSTCOND */0)

	warnx("tuning %s", special);
	CHANGEVAL(sblock.fs_maxbpg, maxbpg,
	    "maximum blocks per file in a cylinder group", "");
	CHANGEVAL(sblock.fs_minfree, minfree,
	    "minimum percentage of free space", "%");
	if (minfree != -1) {
		if (minfree >= MINFREE &&
		    sblock.fs_optim == FS_OPTSPACE)
			warnx(OPTWARN, "time", ">=", MINFREE);
		if (minfree < MINFREE &&
		    sblock.fs_optim == FS_OPTTIME)
			warnx(OPTWARN, "space", "<", MINFREE);
	}
	if (optim != -1) {
		if (sblock.fs_optim == optim) {
			warnx("%s remains unchanged as %s",
			    "optimization preference",
			    chg[optim]);
		} else {
			warnx("%s changes from %s to %s",
			    "optimization preference",
			    chg[sblock.fs_optim], chg[optim]);
			sblock.fs_optim = optim;
			if (sblock.fs_minfree >= MINFREE &&
			    optim == FS_OPTSPACE)
				warnx(OPTWARN, "time", ">=", MINFREE);
			if (sblock.fs_minfree < MINFREE &&
			    optim == FS_OPTTIME)
				warnx(OPTWARN, "space", "<", MINFREE);
		}
	}
	CHANGEVAL(sblock.fs_avgfilesize, avgfilesize,
	    "average file size", "");
	CHANGEVAL(sblock.fs_avgfpdir, avgfpdir,
	    "expected number of files per directory", "");

	if (Nflag) {
		fprintf(stdout, "tunefs: current settings of %s\n", special);
		fprintf(stdout, "\tmaximum contiguous block count %d\n",
		    sblock.fs_maxcontig);
		fprintf(stdout,
		    "\tmaximum blocks per file in a cylinder group %d\n",
		    sblock.fs_maxbpg);
		fprintf(stdout, "\tminimum percentage of free space %d%%\n",
		    sblock.fs_minfree);
		fprintf(stdout, "\toptimization preference: %s\n",
		    chg[sblock.fs_optim]);
		fprintf(stdout, "\taverage file size: %d\n",
		    sblock.fs_avgfilesize);
		fprintf(stdout,
		    "\texpected number of files per directory: %d\n",
		    sblock.fs_avgfpdir);
		fprintf(stdout, "tunefs: no changes made\n");
		exit(0);
	}

	memcpy(buf, (char *)&sblock, SBLOCKSIZE);
	bwrite(sblockloc, buf, SBLOCKSIZE, special);
	if (Aflag)
		for (i = 0; i < sblock.fs_ncg; i++)
			bwrite(fsbtodb(&sblock, cgsblock(&sblock, i)),
			    buf, SBLOCKSIZE, special);
	close(fi);
	exit(0);
}

static int
getnum(const char *num, const char *desc, int min, int max)
{
	int		n;
	const char	*errstr;

	n = strtonum(num, min, max, &errstr);
	if (errstr != NULL)
		errx(1, "Invalid number `%s' for %s: %s", num, desc, errstr);
	return (n);
}

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-AFN] [-e maxbpg] [-g avgfilesize] "
	    "[-h avgfpdir] [-m minfree]\n"
	    "\t[-o optimize_preference] special | filesys\n",
	    __progname);

	exit(2);
}

static void
getsb(struct fs *fs, const char *file)
{
	int i;

	for (i = 0; ; i++) {
		if (sblock_try[i] == -1)
			errx(5, "cannot find filesystem superblock");
		bread(sblock_try[i] / DEV_BSIZE, (char *)fs, SBLOCKSIZE, file);
		switch(fs->fs_magic) {
		case FS_UFS2_MAGIC:
			is_ufs2 = 1;
			/*FALLTHROUGH*/
		case FS_UFS1_MAGIC:
			break;
		default:
			continue;
		}
		if (!is_ufs2 && sblock_try[i] == SBLOCK_UFS2)
			continue;
		if ((is_ufs2 || fs->fs_flags & FS_FLAGS_UPDATED)
		    && fs->fs_sblockloc != sblock_try[i])
			continue;
		break;
	}

	sblockloc = sblock_try[i] / DEV_BSIZE;
}

static void
bwrite(daddr_t blk, char *buffer, int size, const char *file)
{
	if (pwrite(fi, buffer, size, blk * DEV_BSIZE) != size)
		err(7, "%s: writing %d bytes @ %lld", file, size,
		    (long long)(blk * DEV_BSIZE));
}

static void
bread(daddr_t blk, char *buffer, int cnt, const char *file)
{
	if ((pread(fi, buffer, cnt, (off_t)blk * DEV_BSIZE)) != cnt)
		errx(5, "%s: reading %d bytes @ %lld", file, cnt,
		    (long long)(blk * DEV_BSIZE));
}

static int
openpartition(char *name, int flags, char **devicep)
{
	char		rawspec[PATH_MAX], *p;
	struct fstab	*fs;
	int		fd;

	fs = getfsfile(name);
	if (fs) {
		if ((p = strrchr(fs->fs_spec, '/')) != NULL) {
			snprintf(rawspec, sizeof(rawspec), "%.*s/r%s",
			    (int)(p - fs->fs_spec), fs->fs_spec, p + 1);
			name = rawspec;
		} else
			name = fs->fs_spec;
	}
	fd = opendev(name, flags, 0, devicep);
	if (fd == -1 && errno == ENOENT)
		devicep = &name;
	return (fd);
}
