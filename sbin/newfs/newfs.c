/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)newfs.c	8.13 (Berkeley) 5/1/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * newfs: friendly front end to mkfs
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/file.h>
#include <sys/mount.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/ufsmount.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <libutil.h>

#include "newfs.h"

int	Eflag;			/* Erase previous disk contents */
int	Lflag;			/* add a volume label */
int	Nflag;			/* run without writing file system */
int	Oflag = 2;		/* file system format (1 => UFS1, 2 => UFS2) */
int	Rflag;			/* regression test */
int	Uflag;			/* enable soft updates for file system */
int	jflag;			/* enable soft updates journaling for filesys */
int	Xflag = 0;		/* exit in middle of newfs for testing */
int	Jflag;			/* enable gjournal for file system */
int	lflag;			/* enable multilabel for file system */
int	nflag;			/* do not create .snap directory */
int	tflag;			/* enable TRIM */
intmax_t fssize;		/* file system size */
off_t	mediasize;		/* device size */
int	sectorsize;		/* bytes/sector */
int	realsectorsize;		/* bytes/sector in hardware */
int	fsize = 0;		/* fragment size */
int	bsize = 0;		/* block size */
int	maxbsize = 0;		/* maximum clustering */
int	maxblkspercg = MAXBLKSPERCG; /* maximum blocks per cylinder group */
int	minfree = MINFREE;	/* free space threshold */
int	metaspace;		/* space held for metadata blocks */
int	opt = DEFAULTOPT;	/* optimization preference (space or time) */
int	density;		/* number of bytes per inode */
int	maxcontig = 0;		/* max contiguous blocks to allocate */
int	maxbpg;			/* maximum blocks per file in a cyl group */
int	avgfilesize = AVFILESIZ;/* expected average file size */
int	avgfilesperdir = AFPDIR;/* expected number of files per directory */
u_char	*volumelabel = NULL;	/* volume label for filesystem */
struct uufsd disk;		/* libufs disk structure */

static char	device[MAXPATHLEN];
static u_char   bootarea[BBSIZE];
static int	is_file;		/* work on a file, not a device */
static char	*dkname;
static char	*disktype;

static void getfssize(intmax_t *, const char *p, intmax_t, intmax_t);
static struct disklabel *getdisklabel(void);
static void usage(void);
static int expand_number_int(const char *buf, int *num);

ufs2_daddr_t part_ofs; /* partition offset in blocks, used with files */

int
main(int argc, char *argv[])
{
	struct partition *pp;
	struct disklabel *lp;
	struct stat st;
	char *cp, *special;
	intmax_t reserved;
	int ch, i, rval;
	char part_name;		/* partition name, default to full disk */

	part_name = 'c';
	reserved = 0;
	while ((ch = getopt(argc, argv,
	    "EJL:NO:RS:T:UXa:b:c:d:e:f:g:h:i:jk:lm:no:p:r:s:t")) != -1)
		switch (ch) {
		case 'E':
			Eflag = 1;
			break;
		case 'J':
			Jflag = 1;
			break;
		case 'L':
			volumelabel = optarg;
			i = -1;
			while (isalnum(volumelabel[++i]) ||
			    volumelabel[i] == '_' || volumelabel[i] == '-');
			if (volumelabel[i] != '\0') {
				errx(1, "bad volume label. Valid characters "
				    "are alphanumerics, dashes, and underscores.");
			}
			if (strlen(volumelabel) >= MAXVOLLEN) {
				errx(1, "bad volume label. Length is longer than %d.",
				    MAXVOLLEN);
			}
			Lflag = 1;
			break;
		case 'N':
			Nflag = 1;
			break;
		case 'O':
			if ((Oflag = atoi(optarg)) < 1 || Oflag > 2)
				errx(1, "%s: bad file system format value",
				    optarg);
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'S':
			rval = expand_number_int(optarg, &sectorsize);
			if (rval < 0 || sectorsize <= 0)
				errx(1, "%s: bad sector size", optarg);
			break;
		case 'T':
			disktype = optarg;
			break;
		case 'j':
			jflag = 1;
			/* fall through to enable soft updates */
			/* FALLTHROUGH */
		case 'U':
			Uflag = 1;
			break;
		case 'X':
			Xflag++;
			break;
		case 'a':
			rval = expand_number_int(optarg, &maxcontig);
			if (rval < 0 || maxcontig <= 0)
				errx(1, "%s: bad maximum contiguous blocks",
				    optarg);
			break;
		case 'b':
			rval = expand_number_int(optarg, &bsize);
			if (rval < 0)
				 errx(1, "%s: bad block size",
                                    optarg);
			if (bsize < MINBSIZE)
				errx(1, "%s: block size too small, min is %d",
				    optarg, MINBSIZE);
			if (bsize > MAXBSIZE)
				errx(1, "%s: block size too large, max is %d",
				    optarg, MAXBSIZE);
			break;
		case 'c':
			rval = expand_number_int(optarg, &maxblkspercg);
			if (rval < 0 || maxblkspercg <= 0)
				errx(1, "%s: bad blocks per cylinder group",
				    optarg);
			break;
		case 'd':
			rval = expand_number_int(optarg, &maxbsize);
			if (rval < 0 || maxbsize < MINBSIZE)
				errx(1, "%s: bad extent block size", optarg);
			break;
		case 'e':
			rval = expand_number_int(optarg, &maxbpg);
			if (rval < 0 || maxbpg <= 0)
			  errx(1, "%s: bad blocks per file in a cylinder group",
				    optarg);
			break;
		case 'f':
			rval = expand_number_int(optarg, &fsize);
			if (rval < 0 || fsize <= 0)
				errx(1, "%s: bad fragment size", optarg);
			break;
		case 'g':
			rval = expand_number_int(optarg, &avgfilesize);
			if (rval < 0 || avgfilesize <= 0)
				errx(1, "%s: bad average file size", optarg);
			break;
		case 'h':
			rval = expand_number_int(optarg, &avgfilesperdir);
			if (rval < 0 || avgfilesperdir <= 0)
			       errx(1, "%s: bad average files per dir", optarg);
			break;
		case 'i':
			rval = expand_number_int(optarg, &density);
			if (rval < 0 || density <= 0)
				errx(1, "%s: bad bytes per inode", optarg);
			break;
		case 'l':
			lflag = 1;
			break;
		case 'k':
			if ((metaspace = atoi(optarg)) < 0)
				errx(1, "%s: bad metadata space %%", optarg);
			if (metaspace == 0)
				/* force to stay zero in mkfs */
				metaspace = -1;
			break;
		case 'm':
			if ((minfree = atoi(optarg)) < 0 || minfree > 99)
				errx(1, "%s: bad free space %%", optarg);
			break;
		case 'n':
			nflag = 1;
			break;
		case 'o':
			if (strcmp(optarg, "space") == 0)
				opt = FS_OPTSPACE;
			else if (strcmp(optarg, "time") == 0)
				opt = FS_OPTTIME;
			else
				errx(1, 
		"%s: unknown optimization preference: use `space' or `time'",
				    optarg);
			break;
		case 'r':
			errno = 0;
			reserved = strtoimax(optarg, &cp, 0);
			if (errno != 0 || cp == optarg ||
			    *cp != '\0' || reserved < 0)
				errx(1, "%s: bad reserved size", optarg);
			break;
		case 'p':
			is_file = 1;
			part_name = optarg[0];
			break;

		case 's':
			errno = 0;
			fssize = strtoimax(optarg, &cp, 0);
			if (errno != 0 || cp == optarg ||
			    *cp != '\0' || fssize < 0)
				errx(1, "%s: bad file system size", optarg);
			break;
		case 't':
			tflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	special = argv[0];
	if (!special[0])
		err(1, "empty file/special name");
	cp = strrchr(special, '/');
	if (cp == NULL) {
		/*
		 * No path prefix; try prefixing _PATH_DEV.
		 */
		snprintf(device, sizeof(device), "%s%s", _PATH_DEV, special);
		special = device;
	}

	if (is_file) {
		/* bypass ufs_disk_fillout_blank */
		bzero( &disk, sizeof(disk));
		disk.d_bsize = 1;
		disk.d_name = special;
		disk.d_fd = open(special, O_RDONLY);
		if (disk.d_fd < 0 ||
		    (!Nflag && ufs_disk_write(&disk) == -1))
			errx(1, "%s: ", special);
	} else if (ufs_disk_fillout_blank(&disk, special) == -1 ||
	    (!Nflag && ufs_disk_write(&disk) == -1)) {
		if (disk.d_error != NULL)
			errx(1, "%s: %s", special, disk.d_error);
		else
			err(1, "%s", special);
	}
	if (fstat(disk.d_fd, &st) < 0)
		err(1, "%s", special);
	if ((st.st_mode & S_IFMT) != S_IFCHR) {
		warn("%s: not a character-special device", special);
		is_file = 1;	/* assume it is a file */
		dkname = special;
		if (sectorsize == 0)
			sectorsize = 512;
		mediasize = st.st_size;
		/* set fssize from the partition */
	} else {
	    if (sectorsize == 0)
		if (ioctl(disk.d_fd, DIOCGSECTORSIZE, &sectorsize) == -1)
		    sectorsize = 0;	/* back out on error for safety */
	    if (sectorsize && ioctl(disk.d_fd, DIOCGMEDIASIZE, &mediasize) != -1)
		getfssize(&fssize, special, mediasize / sectorsize, reserved);
	}
	pp = NULL;
	lp = getdisklabel();
	if (lp != NULL) {
		if (!is_file) /* already set for files */
			part_name = special[strlen(special) - 1];
		if ((part_name < 'a' || part_name - 'a' >= MAXPARTITIONS) &&
				!isdigit(part_name))
			errx(1, "%s: can't figure out file system partition",
					special);
		cp = &part_name;
		if (isdigit(*cp))
			pp = &lp->d_partitions[RAW_PART];
		else
			pp = &lp->d_partitions[*cp - 'a'];
		if (pp->p_size == 0)
			errx(1, "%s: `%c' partition is unavailable",
			    special, *cp);
		if (pp->p_fstype == FS_BOOT)
			errx(1, "%s: `%c' partition overlaps boot program",
			    special, *cp);
		getfssize(&fssize, special, pp->p_size, reserved);
		if (sectorsize == 0)
			sectorsize = lp->d_secsize;
		if (fsize == 0)
			fsize = pp->p_fsize;
		if (bsize == 0)
			bsize = pp->p_frag * pp->p_fsize;
		if (is_file)
			part_ofs = pp->p_offset;
	}
	if (sectorsize <= 0)
		errx(1, "%s: no default sector size", special);
	if (fsize <= 0)
		fsize = MAX(DFL_FRAGSIZE, sectorsize);
	if (bsize <= 0)
		bsize = MIN(DFL_BLKSIZE, 8 * fsize);
	if (minfree < MINFREE && opt != FS_OPTSPACE) {
		fprintf(stderr, "Warning: changing optimization to space ");
		fprintf(stderr, "because minfree is less than %d%%\n", MINFREE);
		opt = FS_OPTSPACE;
	}
	realsectorsize = sectorsize;
	if (sectorsize != DEV_BSIZE) {		/* XXX */
		int secperblk = sectorsize / DEV_BSIZE;

		sectorsize = DEV_BSIZE;
		fssize *= secperblk;
		if (pp != NULL)
			pp->p_size *= secperblk;
	}
	mkfs(pp, special);
	ufs_disk_close(&disk);
	if (!jflag)
		exit(0);
	if (execlp("tunefs", "newfs", "-j", "enable", special, NULL) < 0)
		err(1, "Cannot enable soft updates journaling, tunefs");
	/* NOT REACHED */
}

void
getfssize(intmax_t *fsz, const char *s, intmax_t disksize, intmax_t reserved)
{
	intmax_t available;

	available = disksize - reserved;
	if (available <= 0)
		errx(1, "%s: reserved not less than device size %jd",
		    s, disksize);
	if (*fsz == 0)
		*fsz = available;
	else if (*fsz > available)
		errx(1, "%s: maximum file system size is %jd",
		    s, available);
}

struct disklabel *
getdisklabel(void)
{
	static struct disklabel lab;
	struct disklabel *lp;

	if (is_file) {
		if (read(disk.d_fd, bootarea, BBSIZE) != BBSIZE)
			err(4, "cannot read bootarea");
		if (bsd_disklabel_le_dec(
		    bootarea + (0 /* labeloffset */ +
				1 /* labelsoffset */ * sectorsize),
		    &lab, MAXPARTITIONS))
			errx(1, "no valid label found");

		lp = &lab;
		return &lab;
	}

	if (disktype) {
		lp = getdiskbyname(disktype);
		if (lp != NULL)
			return (lp);
	}
	return (NULL);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [ -fsoptions ] special-device%s\n",
	    getprogname(),
	    " [device-type]");
	fprintf(stderr, "where fsoptions are:\n");
	fprintf(stderr, "\t-E Erase previous disk content\n");
	fprintf(stderr, "\t-J Enable journaling via gjournal\n");
	fprintf(stderr, "\t-L volume label to add to superblock\n");
	fprintf(stderr,
	    "\t-N do not create file system, just print out parameters\n");
	fprintf(stderr, "\t-O file system format: 1 => UFS1, 2 => UFS2\n");
	fprintf(stderr, "\t-R regression test, suppress random factors\n");
	fprintf(stderr, "\t-S sector size\n");
	fprintf(stderr, "\t-T disktype\n");
	fprintf(stderr, "\t-U enable soft updates\n");
	fprintf(stderr, "\t-a maximum contiguous blocks\n");
	fprintf(stderr, "\t-b block size\n");
	fprintf(stderr, "\t-c blocks per cylinders group\n");
	fprintf(stderr, "\t-d maximum extent size\n");
	fprintf(stderr, "\t-e maximum blocks per file in a cylinder group\n");
	fprintf(stderr, "\t-f frag size\n");
	fprintf(stderr, "\t-g average file size\n");
	fprintf(stderr, "\t-h average files per directory\n");
	fprintf(stderr, "\t-i number of bytes per inode\n");
	fprintf(stderr, "\t-j enable soft updates journaling\n");
	fprintf(stderr, "\t-k space to hold for metadata blocks\n");
	fprintf(stderr, "\t-l enable multilabel MAC\n");
	fprintf(stderr, "\t-n do not create .snap directory\n");
	fprintf(stderr, "\t-m minimum free space %%\n");
	fprintf(stderr, "\t-o optimization preference (`space' or `time')\n");
	fprintf(stderr, "\t-p partition name (a..h)\n");
	fprintf(stderr, "\t-r reserved sectors at the end of device\n");
	fprintf(stderr, "\t-s file system size (sectors)\n");
	fprintf(stderr, "\t-t enable TRIM\n");
	exit(1);
}

static int
expand_number_int(const char *buf, int *num)
{
	int64_t num64;
	int rval;

	rval = expand_number(buf, &num64);
	if (rval < 0)
		return (rval);
	if (num64 > INT_MAX || num64 < INT_MIN) {
		errno = ERANGE;
		return (-1);
	}
	*num = (int)num64;
	return (0);
}
