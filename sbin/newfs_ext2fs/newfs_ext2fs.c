/* $OpenBSD: newfs_ext2fs.c,v 1.30 2025/09/17 16:07:57 deraadt Exp $ */
/*	$NetBSD: newfs_ext2fs.c,v 1.8 2009/03/02 10:38:13 tsutsui Exp $	*/

/*
 * Copyright (c) 1983, 1989, 1993, 1994
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
 * newfs: friendly front end to mke2fs
 */
#include <sys/param.h>	/* powerof2 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/disklabel.h>
#include <sys/mount.h>

#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_dinode.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#include "extern.h"

static int64_t strsuftoi64(const char *, const char *, int64_t, int64_t, int *);
static void usage(void) __dead;

/*
 * For file systems smaller than SMALL_FSSIZE we use the S_DFL_* defaults,
 * otherwise if less than MEDIUM_FSSIZE use M_DFL_*, otherwise use
 * L_DFL_*.
 */
#define SMALL_FSSIZE	((4 * 1024 * 1024) / sectorsize)	/* 4MB */
#define S_DFL_BSIZE	1024
#define MEDIUM_FSSIZE	((512 * 1024 * 1024) / sectorsize)	/* 512MB */
#define M_DFL_BSIZE	1024
#define L_DFL_BSIZE	4096

/*
 * Each file system has a number of inodes statically allocated.
 * We allocate one inode slot per 2, 4, or 8 blocks, expecting this
 * to be far more than we will ever need.
 */
#define S_DFL_NINODE(blocks)	((blocks) / 8)
#define M_DFL_NINODE(blocks)	((blocks) / 4)
#define L_DFL_NINODE(blocks)	((blocks) / 2)

/*
 * Default sector size.
 */
#define	DFL_SECSIZE	512

int	Nflag;			/* run without writing file system */
int	Oflag = 0;		/* format as conservative REV0 by default */
int	verbosity;		/* amount of printf() output */
#define DEFAULT_VERBOSITY 4	/* 4 is traditional behavior of newfs(8) */
int64_t fssize;			/* file system size */
uint	sectorsize;		/* bytes/sector */
uint16_t inodesize = EXT2_REV0_DINODE_SIZE;	/* inode size */
uint	fsize = 0;		/* fragment size */
uint	bsize = 0;		/* block size */
uint	minfree = MINFREE;	/* free space threshold */
uint	density;		/* number of bytes per inode */
uint	num_inodes;		/* number of inodes (overrides density) */
int	max_cols;
char	*volname = NULL;	/* volume name */

static char *disktype = NULL;

struct disklabel *getdisklabel(const char *, int);
struct partition *getpartition(int, const char *, char *[], struct disklabel **);

int
main(int argc, char *argv[])
{
	struct statfs *mp;
	struct stat sb;
	int ch, fd, len, n, Fflag, Iflag, Zflag;
	char *s1, *s2, *special;
	const char *opstring;
	int byte_sized, fl;
	uint blocks;			/* number of blocks */
	struct partition *pp = NULL;
	struct disklabel *lp;
	struct winsize winsize;

	/* Get terminal width */
	if (ioctl(fileno(stdout), TIOCGWINSZ, &winsize) == 0)
		max_cols = winsize.ws_col;
	else
		max_cols = 80;

	if (pledge("stdio rpath wpath cpath disklabel", NULL) == -1)
		err(1, "pledge");

	Fflag = Iflag = Zflag = 0;
	verbosity = -1;
	opstring = "D:FINO:S:V:Zb:f:i:l:m:n:qs:t:v:";
	byte_sized = 0;
	while ((ch = getopt(argc, argv, opstring)) != -1)
		switch (ch) {
		case 'D':
			inodesize = (uint16_t)strtol(optarg, &s1, 0);
			if (*s1 || (inodesize != 128 && inodesize != 256))
				errx(1, "Bad inode size %d "
				    "(only 128 and 256 supported)", inodesize);
			break;
		case 'F':
			Fflag = 1;
			break;
		case 'I':
			Iflag = 1;
			break;
		case 'N':
			Nflag = 1;
			if (verbosity == -1)
				verbosity = DEFAULT_VERBOSITY;
			break;
		case 'O':
			Oflag = strsuftoi64("format", optarg, 0, 1, NULL);
			break;
		case 'S':
			/*
			 * XXX:
			 * non-512 byte sectors almost certainly don't work.
			 */
			sectorsize = strsuftoi64("sector size",
			    optarg, 512, 65536, NULL);
			if (!powerof2(sectorsize))
				errx(EXIT_FAILURE,
				    "sector size `%s' is not a power of 2.",
				    optarg);
			break;
		case 'V':
			verbosity = strsuftoi64("verbose", optarg, 0, 4, NULL);
			break;
		case 'Z':
			Zflag = 1;
			break;
		case 'b':
			bsize = strsuftoi64("block size",
			    optarg, MINBSIZE, EXT2_MAXBSIZE, NULL);
			break;
		case 'f':
			fsize = strsuftoi64("fragment size",
			    optarg, MINBSIZE, EXT2_MAXBSIZE, NULL);
			break;
		case 'i':
			density = strsuftoi64("bytes per inode",
			    optarg, 1, INT_MAX, NULL);
			break;
		case 'm':
			minfree = strsuftoi64("free space %",
			    optarg, 0, 99, NULL);
			break;
		case 'n':
			num_inodes = strsuftoi64("number of inodes",
			    optarg, 1, INT_MAX, NULL);
			break;
		case 'q':
			verbosity = 1;
			break;
		case 's':
			fssize = strsuftoi64("file system size",
			    optarg, INT64_MIN, INT64_MAX, &byte_sized);
			break;
		case 't':
			/* compat with newfs -t */
			break;
		case 'v':
			volname = optarg;
			if (volname[0] == '\0')
				errx(EXIT_FAILURE,
				    "Volume name cannot be zero length");
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (verbosity == -1)
		/* Default to showing cg info */
		verbosity = DEFAULT_VERBOSITY;

	if (argc != 1)
		usage();

	memset(&sb, 0, sizeof(sb));
	special = argv[0];
	fl = Nflag ? O_RDONLY : O_RDWR;

	if (Fflag) {
		/*
		 * It's a file system image
		 * no label, use fixed default for sectorsize.
		 */
		if (sectorsize == 0)
			sectorsize = DFL_SECSIZE;

		/* creating image in a regular file */
		if (!Nflag && fssize > 0)
			fl |= O_CREAT;
		fd = open(special, fl, 0666);
		if (fd == -1)
			err(EXIT_FAILURE, "can't open file %s", special);
		if (fstat(fd, &sb) == -1)
			err(EXIT_FAILURE, "can't fstat opened %s", special);
	} else {	/* !Fflag */
		fd = opendev(special, fl, 0, &special);
		if (fd == -1 || fstat(fd, &sb) == -1)
			err(EXIT_FAILURE, "%s: open", special);

		if (!Nflag) {
			/* Bail if target special is mounted */
			n = getmntinfo(&mp, MNT_NOWAIT);
			if (n == 0)
				err(EXIT_FAILURE, "%s: getmntinfo", special);

			len = sizeof(_PATH_DEV) - 1;
			s1 = special;
			if (strncmp(_PATH_DEV, s1, len) == 0)
				s1 += len;

			while (--n >= 0) {
				s2 = mp->f_mntfromname;
				if (strncmp(_PATH_DEV, s2, len) == 0) {
					s2 += len - 1;
					*s2 = 'r';
				}
				if (strcmp(s1, s2) == 0 ||
				    strcmp(s1, &s2[1]) == 0)
					errx(EXIT_FAILURE,
					    "%s is mounted on %s",
					    special, mp->f_mntonname);
				++mp;
			}
		}

		pp = getpartition(fd, special, argv, &lp);
		if (!Iflag) {
			static const char m[] =
			    "%s partition type is not `%s' (or use -I)";
			if (pp->p_fstype != FS_EXT2FS)
				errx(EXIT_FAILURE, m, special, "ext2fs");
		}
		if (sectorsize == 0) {
			sectorsize = lp->d_secsize;
			if (sectorsize <= 0)
				errx(EXIT_FAILURE, "no default sector size");
		}
	}

	if (byte_sized)
		fssize /= sectorsize;
	if (fssize <= 0) {
		if (sb.st_size != 0)
			fssize += sb.st_size / sectorsize;
		else if (pp)
			fssize += DL_GETPSIZE(pp);
		if (fssize <= 0)
			errx(EXIT_FAILURE,
			    "Unable to determine file system size");
	}

	/* XXXLUKEM: only ftruncate() regular files ? (dsl: or at all?) */
	if (Fflag && !Nflag
	    && ftruncate(fd, (off_t)fssize * sectorsize) == -1)
		err(1, "can't ftruncate %s to %" PRId64, special, fssize);

	if (Zflag && !Nflag) {	/* pre-zero (and de-sparce) the file */
		char *buf;
		int bufsize, i;
		off_t bufrem;
		struct statfs sfs;

		if (fstatfs(fd, &sfs) == -1) {
			warn("can't fstatvfs `%s'", special);
			bufsize = 8192;
		} else
			bufsize = sfs.f_iosize;

		if ((buf = calloc(1, bufsize)) == NULL)
			err(1, "can't allocate buffer of %d",
			bufsize);
		bufrem = fssize * sectorsize;
		if (verbosity > 0)
			printf("Creating file system image in `%s', "
			    "size %" PRId64 " bytes, in %d byte chunks.\n",
			    special, bufrem, bufsize);
		while (bufrem > 0) {
			i = write(fd, buf, MINIMUM(bufsize, bufrem));
			if (i == -1)
				err(1, "writing image");
			bufrem -= i;
		}
		free(buf);
	}

	/* Sort out fragment and block sizes */
	if (bsize == 0) {
		bsize = fsize;
		if (bsize == 0) {
			if (fssize < SMALL_FSSIZE)
				bsize = S_DFL_BSIZE;
			else if (fssize < MEDIUM_FSSIZE)
				bsize = M_DFL_BSIZE;
			else
				bsize = L_DFL_BSIZE;
		}
	}
	if (fsize == 0)
		fsize = bsize;

	blocks = fssize * sectorsize / bsize;

	if (num_inodes == 0) {
		if (density != 0)
			num_inodes = fssize / density;
		else {
			if (fssize < SMALL_FSSIZE)
				num_inodes = S_DFL_NINODE(blocks);
			else if (fssize < MEDIUM_FSSIZE)
				num_inodes = M_DFL_NINODE(blocks);
			else
				num_inodes = L_DFL_NINODE(blocks);
		}
	}
	mke2fs(special, fd);

	close(fd);
	exit(EXIT_SUCCESS);
}

static int64_t
strsuftoi64(const char *desc, const char *arg, int64_t min, int64_t max,
    int *num_suffix)
{
	int64_t result, r1;
	int shift = 0;
	char *ep;

	errno = 0;
	r1 = strtoll(arg, &ep, 10);
	if (ep[0] != '\0' && ep[1] != '\0')
		errx(EXIT_FAILURE,
		    "%s `%s' is not a valid number.", desc, arg);
	switch (ep[0]) {
	case '\0':
	case 's':
	case 'S':
		if (num_suffix != NULL)
			*num_suffix = 0;
		break;
	case 'g':
	case 'G':
		shift += 10;
		/* FALLTHROUGH */
	case 'm':
	case 'M':
		shift += 10;
		/* FALLTHROUGH */
	case 'k':
	case 'K':
		shift += 10;
		/* FALLTHROUGH */
	case 'b':
	case 'B':
		if (num_suffix != NULL)
			*num_suffix = 1;
		break;
	default:
		errx(EXIT_FAILURE,
		    "`%s' is not a valid suffix for %s.", ep, desc);
	}
	result = r1 << shift;
	if (errno == ERANGE || result >> shift != r1)
		errx(EXIT_FAILURE,
		    "%s `%s' is too large to convert.", desc, arg);
	if (result < min)
		errx(EXIT_FAILURE,
		    "%s `%s' (%" PRId64 ") is less than the minimum (%"
		    PRId64 ").", desc, arg, result, min);
	if (result > max)
		errx(EXIT_FAILURE,
		    "%s `%s' (%" PRId64 ") is greater than the maximum (%"
		    PRId64 ").", desc, arg, result, max);
	return result;
}

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [ fsoptions ] special-device\n", __progname);
	exit(EXIT_FAILURE);
}

struct disklabel *
getdisklabel(const char *s, int fd)
{
	static struct disklabel lab;

	if (ioctl(fd, DIOCGDINFO, (char *)&lab) == -1) {
		if (disktype != NULL) {
			struct disklabel *lp;

			//unlabeled++;
			lp = getdiskbyname(disktype);
			if (lp == NULL)
				errx(EXIT_FAILURE, "%s: unknown disk type",
				    disktype);
			return (lp);
		}
		warn("ioctl (GDINFO)");
		errx(EXIT_FAILURE,
		    "%s: can't read disk label; disk type must be specified",
		    s);
	}
	return (&lab);
}

struct partition *
getpartition(int fsi, const char *special, char *argv[], struct disklabel **dl)
{
	struct stat st;
	const char *cp;
	struct disklabel *lp;
	struct partition *pp;

	if (fstat(fsi, &st) == -1)
		err(EXIT_FAILURE, "%s", special);
	if (S_ISBLK(st.st_mode))
		errx(EXIT_FAILURE, "%s: block device", special);
	if (!S_ISCHR(st.st_mode))
		warnx("%s: not a character-special device", special);
	if (*argv[0] == '\0')
		errx(EXIT_FAILURE, "empty partition name supplied");
	cp = argv[0] + strlen(argv[0]) - 1;
	if (DL_PARTNAME2NUM(*cp) == -1 && !isdigit((unsigned char)*cp))
		errx(EXIT_FAILURE, "%s: can't figure out file system partition", argv[0]);
	lp = getdisklabel(special, fsi);
	if (isdigit((unsigned char)*cp))
		pp = &lp->d_partitions[0];
	else
		pp = &lp->d_partitions[DL_PARTNAME2NUM(*cp)];
	if (DL_GETPSIZE(pp) == 0) 
		errx(EXIT_FAILURE, "%s: `%c' partition is unavailable", argv[0], *cp);
	*dl = lp;
	return pp;
}

