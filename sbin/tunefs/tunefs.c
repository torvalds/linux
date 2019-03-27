/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)tunefs.c	8.2 (Berkeley) 4/19/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * tunefs: change layout parameters to an existing file system.
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/disklabel.h>
#include <sys/stat.h>

#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/dir.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <fstab.h>
#include <libufs.h>
#include <mntopts.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* the optimization warning string template */
#define	OPTWARN	"should optimize for %s with minfree %s %d%%"

static int blocks;
static char clrbuf[MAXBSIZE];
static struct uufsd disk;
#define	sblock disk.d_fs

static void usage(void);
static void printfs(void);
static int journal_alloc(int64_t size);
static void journal_clear(void);
static void sbdirty(void);

int
main(int argc, char *argv[])
{
	const char *avalue, *jvalue, *Jvalue, *Lvalue, *lvalue, *Nvalue, *nvalue;
	const char *tvalue;
	const char *special, *on;
	const char *name;
	int active;
	int Aflag, aflag, eflag, evalue, fflag, fvalue, jflag, Jflag, kflag;
	int kvalue, Lflag, lflag, mflag, mvalue, Nflag, nflag, oflag, ovalue;
	int pflag, sflag, svalue, Svalue, tflag;
	int ch, found_arg, i;
	int iovlen = 0;
	const char *chg[2];
	struct statfs stfs;
	struct iovec *iov = NULL;
	char errmsg[255] = {0};

	if (argc < 3)
		usage();
	Aflag = aflag = eflag = fflag = jflag = Jflag = kflag = Lflag = 0;
	lflag = mflag = Nflag = nflag = oflag = pflag = sflag = tflag = 0;
	avalue = jvalue = Jvalue = Lvalue = lvalue = Nvalue = nvalue = NULL;
	evalue = fvalue = mvalue = ovalue = svalue = Svalue = 0;
	active = 0;
	found_arg = 0;		/* At least one arg is required. */
	while ((ch = getopt(argc, argv, "Aa:e:f:j:J:k:L:l:m:N:n:o:ps:S:t:"))
	    != -1)
		switch (ch) {

		case 'A':
			found_arg = 1;
			Aflag++;
			break;

		case 'a':
			found_arg = 1;
			name = "POSIX.1e ACLs";
			avalue = optarg;
			if (strcmp(avalue, "enable") &&
			    strcmp(avalue, "disable")) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			aflag = 1;
			break;

		case 'e':
			found_arg = 1;
			name = "maximum blocks per file in a cylinder group";
			evalue = atoi(optarg);
			if (evalue < 1)
				errx(10, "%s must be >= 1 (was %s)",
				    name, optarg);
			eflag = 1;
			break;

		case 'f':
			found_arg = 1;
			name = "average file size";
			fvalue = atoi(optarg);
			if (fvalue < 1)
				errx(10, "%s must be >= 1 (was %s)",
				    name, optarg);
			fflag = 1;
			break;

		case 'j':
			found_arg = 1;
			name = "softdep journaled file system";
			jvalue = optarg;
			if (strcmp(jvalue, "enable") &&
			    strcmp(jvalue, "disable")) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			jflag = 1;
			break;

		case 'J':
			found_arg = 1;
			name = "gjournaled file system";
			Jvalue = optarg;
			if (strcmp(Jvalue, "enable") &&
			    strcmp(Jvalue, "disable")) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			Jflag = 1;
			break;

		case 'k':
			found_arg = 1;
			name = "space to hold for metadata blocks";
			kvalue = atoi(optarg);
			if (kvalue < 0)
				errx(10, "bad %s (%s)", name, optarg);
			kflag = 1;
			break;

		case 'L':
			found_arg = 1;
			name = "volume label";
			Lvalue = optarg;
			i = -1;
			while (isalnum(Lvalue[++i]) || Lvalue[i] == '_' ||
			    Lvalue[i] == '-')
				;
			if (Lvalue[i] != '\0') {
				errx(10, "bad %s. Valid characters are "
				    "alphanumerics, dashes, and underscores.",
				    name);
			}
			if (strlen(Lvalue) >= MAXVOLLEN) {
				errx(10, "bad %s. Length is longer than %d.",
				    name, MAXVOLLEN - 1);
			}
			Lflag = 1;
			break;

		case 'l':
			found_arg = 1;
			name = "multilabel MAC file system";
			lvalue = optarg;
			if (strcmp(lvalue, "enable") &&
			    strcmp(lvalue, "disable")) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			lflag = 1;
			break;

		case 'm':
			found_arg = 1;
			name = "minimum percentage of free space";
			mvalue = atoi(optarg);
			if (mvalue < 0 || mvalue > 99)
				errx(10, "bad %s (%s)", name, optarg);
			mflag = 1;
			break;

		case 'N':
			found_arg = 1;
			name = "NFSv4 ACLs";
			Nvalue = optarg;
			if (strcmp(Nvalue, "enable") &&
			    strcmp(Nvalue, "disable")) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			Nflag = 1;
			break;

		case 'n':
			found_arg = 1;
			name = "soft updates";
			nvalue = optarg;
			if (strcmp(nvalue, "enable") != 0 &&
			    strcmp(nvalue, "disable") != 0) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			nflag = 1;
			break;

		case 'o':
			found_arg = 1;
			name = "optimization preference";
			if (strcmp(optarg, "space") == 0)
				ovalue = FS_OPTSPACE;
			else if (strcmp(optarg, "time") == 0)
				ovalue = FS_OPTTIME;
			else
				errx(10,
				    "bad %s (options are `space' or `time')",
				    name);
			oflag = 1;
			break;

		case 'p':
			found_arg = 1;
			pflag = 1;
			break;

		case 's':
			found_arg = 1;
			name = "expected number of files per directory";
			svalue = atoi(optarg);
			if (svalue < 1)
				errx(10, "%s must be >= 1 (was %s)",
				    name, optarg);
			sflag = 1;
			break;

		case 'S':
			found_arg = 1;
			name = "Softdep Journal Size";
			Svalue = atoi(optarg);
			if (Svalue < SUJ_MIN)
				errx(10, "%s must be >= %d (was %s)",
				    name, SUJ_MIN, optarg);
			break;

		case 't':
			found_arg = 1;
			name = "trim";
			tvalue = optarg;
			if (strcmp(tvalue, "enable") != 0 &&
			    strcmp(tvalue, "disable") != 0) {
				errx(10, "bad %s (options are %s)",
				    name, "`enable' or `disable'");
			}
			tflag = 1;
			break;

		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (found_arg == 0 || argc != 1)
		usage();

	on = special = argv[0];
	if (ufs_disk_fillout(&disk, special) == -1)
		goto err;
	if (disk.d_name != special) {
		if (statfs(special, &stfs) != 0)
			warn("Can't stat %s", special);
		if (strcmp(special, stfs.f_mntonname) == 0)
			active = 1;
	}

	if (pflag) {
		printfs();
		exit(0);
	}
	if (Lflag) {
		name = "volume label";
		strncpy(sblock.fs_volname, Lvalue, MAXVOLLEN);
	}
	if (aflag) {
		name = "POSIX.1e ACLs";
		if (strcmp(avalue, "enable") == 0) {
			if (sblock.fs_flags & FS_ACLS) {
				warnx("%s remains unchanged as enabled", name);
			} else if (sblock.fs_flags & FS_NFS4ACLS) {
				warnx("%s and NFSv4 ACLs are mutually "
				    "exclusive", name);
			} else {
				sblock.fs_flags |= FS_ACLS;
				warnx("%s set", name);
			}
		} else if (strcmp(avalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_ACLS) ==
			    FS_ACLS) {
				warnx("%s remains unchanged as disabled",
				    name);
			} else {
				sblock.fs_flags &= ~FS_ACLS;
				warnx("%s cleared", name);
			}
		}
	}
	if (eflag) {
		name = "maximum blocks per file in a cylinder group";
		if (sblock.fs_maxbpg == evalue)
			warnx("%s remains unchanged as %d", name, evalue);
		else {
			warnx("%s changes from %d to %d",
			    name, sblock.fs_maxbpg, evalue);
			sblock.fs_maxbpg = evalue;
		}
	}
	if (fflag) {
		name = "average file size";
		if (sblock.fs_avgfilesize == (unsigned)fvalue) {
			warnx("%s remains unchanged as %d", name, fvalue);
		}
		else {
			warnx("%s changes from %d to %d",
					name, sblock.fs_avgfilesize, fvalue);
			sblock.fs_avgfilesize = fvalue;
		}
	}
	if (jflag) {
 		name = "soft updates journaling";
 		if (strcmp(jvalue, "enable") == 0) {
			if ((sblock.fs_flags & (FS_DOSOFTDEP | FS_SUJ)) ==
			    (FS_DOSOFTDEP | FS_SUJ)) {
				warnx("%s remains unchanged as enabled", name);
			} else if (sblock.fs_clean == 0) {
				warnx("%s cannot be enabled until fsck is run",
				    name);
			} else if (journal_alloc(Svalue) != 0) {
				warnx("%s cannot be enabled", name);
			} else {
 				sblock.fs_flags |= FS_DOSOFTDEP | FS_SUJ;
 				warnx("%s set", name);
			}
 		} else if (strcmp(jvalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_SUJ) == FS_SUJ) {
				warnx("%s remains unchanged as disabled", name);
			} else {
				journal_clear();
 				sblock.fs_flags &= ~FS_SUJ;
				sblock.fs_sujfree = 0;
 				warnx("%s cleared but soft updates still set.",
				    name);

				warnx("remove .sujournal to reclaim space");
			}
 		}
	}
	if (Jflag) {
		name = "gjournal";
		if (strcmp(Jvalue, "enable") == 0) {
			if (sblock.fs_flags & FS_GJOURNAL) {
				warnx("%s remains unchanged as enabled", name);
			} else {
				sblock.fs_flags |= FS_GJOURNAL;
				warnx("%s set", name);
			}
		} else if (strcmp(Jvalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_GJOURNAL) ==
			    FS_GJOURNAL) {
				warnx("%s remains unchanged as disabled",
				    name);
			} else {
				sblock.fs_flags &= ~FS_GJOURNAL;
				warnx("%s cleared", name);
			}
		}
	}
	if (kflag) {
		name = "space to hold for metadata blocks";
		if (sblock.fs_metaspace == kvalue)
			warnx("%s remains unchanged as %d", name, kvalue);
		else {
			kvalue = blknum(&sblock, kvalue);
			if (kvalue > sblock.fs_fpg / 2) {
				kvalue = blknum(&sblock, sblock.fs_fpg / 2);
				warnx("%s cannot exceed half the file system "
				    "space", name);
			}
			warnx("%s changes from %jd to %d",
				    name, sblock.fs_metaspace, kvalue);
			sblock.fs_metaspace = kvalue;
		}
	}
	if (lflag) {
		name = "multilabel";
		if (strcmp(lvalue, "enable") == 0) {
			if (sblock.fs_flags & FS_MULTILABEL) {
				warnx("%s remains unchanged as enabled", name);
			} else {
				sblock.fs_flags |= FS_MULTILABEL;
				warnx("%s set", name);
			}
		} else if (strcmp(lvalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_MULTILABEL) ==
			    FS_MULTILABEL) {
				warnx("%s remains unchanged as disabled",
				    name);
			} else {
				sblock.fs_flags &= ~FS_MULTILABEL;
				warnx("%s cleared", name);
			}
		}
	}
	if (mflag) {
		name = "minimum percentage of free space";
		if (sblock.fs_minfree == mvalue)
			warnx("%s remains unchanged as %d%%", name, mvalue);
		else {
			warnx("%s changes from %d%% to %d%%",
				    name, sblock.fs_minfree, mvalue);
			sblock.fs_minfree = mvalue;
			if (mvalue >= MINFREE && sblock.fs_optim == FS_OPTSPACE)
				warnx(OPTWARN, "time", ">=", MINFREE);
			if (mvalue < MINFREE && sblock.fs_optim == FS_OPTTIME)
				warnx(OPTWARN, "space", "<", MINFREE);
		}
	}
	if (Nflag) {
		name = "NFSv4 ACLs";
		if (strcmp(Nvalue, "enable") == 0) {
			if (sblock.fs_flags & FS_NFS4ACLS) {
				warnx("%s remains unchanged as enabled", name);
			} else if (sblock.fs_flags & FS_ACLS) {
				warnx("%s and POSIX.1e ACLs are mutually "
				    "exclusive", name);
			} else {
				sblock.fs_flags |= FS_NFS4ACLS;
				warnx("%s set", name);
			}
		} else if (strcmp(Nvalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_NFS4ACLS) ==
			    FS_NFS4ACLS) {
				warnx("%s remains unchanged as disabled",
				    name);
			} else {
				sblock.fs_flags &= ~FS_NFS4ACLS;
				warnx("%s cleared", name);
			}
		}
	}
	if (nflag) {
 		name = "soft updates";
 		if (strcmp(nvalue, "enable") == 0) {
			if (sblock.fs_flags & FS_DOSOFTDEP)
				warnx("%s remains unchanged as enabled", name);
			else if (sblock.fs_clean == 0) {
				warnx("%s cannot be enabled until fsck is run",
				    name);
			} else {
 				sblock.fs_flags |= FS_DOSOFTDEP;
 				warnx("%s set", name);
			}
 		} else if (strcmp(nvalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_DOSOFTDEP) == FS_DOSOFTDEP)
				warnx("%s remains unchanged as disabled", name);
			else {
 				sblock.fs_flags &= ~FS_DOSOFTDEP;
 				warnx("%s cleared", name);
			}
 		}
	}
	if (oflag) {
		name = "optimization preference";
		chg[FS_OPTSPACE] = "space";
		chg[FS_OPTTIME] = "time";
		if (sblock.fs_optim == ovalue)
			warnx("%s remains unchanged as %s", name, chg[ovalue]);
		else {
			warnx("%s changes from %s to %s",
				    name, chg[sblock.fs_optim], chg[ovalue]);
			sblock.fs_optim = ovalue;
			if (sblock.fs_minfree >= MINFREE &&
			    ovalue == FS_OPTSPACE)
				warnx(OPTWARN, "time", ">=", MINFREE);
			if (sblock.fs_minfree < MINFREE && ovalue == FS_OPTTIME)
				warnx(OPTWARN, "space", "<", MINFREE);
		}
	}
	if (sflag) {
		name = "expected number of files per directory";
		if (sblock.fs_avgfpdir == (unsigned)svalue) {
			warnx("%s remains unchanged as %d", name, svalue);
		}
		else {
			warnx("%s changes from %d to %d",
					name, sblock.fs_avgfpdir, svalue);
			sblock.fs_avgfpdir = svalue;
		}
	}
	if (tflag) {
		name = "issue TRIM to the disk";
 		if (strcmp(tvalue, "enable") == 0) {
			if (sblock.fs_flags & FS_TRIM)
				warnx("%s remains unchanged as enabled", name);
			else {
 				sblock.fs_flags |= FS_TRIM;
 				warnx("%s set", name);
			}
 		} else if (strcmp(tvalue, "disable") == 0) {
			if ((~sblock.fs_flags & FS_TRIM) == FS_TRIM)
				warnx("%s remains unchanged as disabled", name);
			else {
 				sblock.fs_flags &= ~FS_TRIM;
 				warnx("%s cleared", name);
			}
 		}
	}

	if (sbwrite(&disk, Aflag) == -1)
		goto err;
	ufs_disk_close(&disk);
	if (active) {
		build_iovec_argf(&iov, &iovlen, "fstype", "ufs");
		build_iovec_argf(&iov, &iovlen, "fspath", "%s", on);
		build_iovec(&iov, &iovlen, "errmsg", errmsg, sizeof(errmsg));
		if (nmount(iov, iovlen,
		    stfs.f_flags | MNT_UPDATE | MNT_RELOAD) < 0) {
			if (errmsg[0])
				err(9, "%s: reload: %s", special, errmsg);
			else
				err(9, "%s: reload", special);
		}
		warnx("file system reloaded");
	}
	exit(0);
err:
	if (disk.d_error != NULL)
		errx(11, "%s: %s", special, disk.d_error);
	else
		err(12, "%s", special);
}

static void
sbdirty(void)
{
	disk.d_fs.fs_flags |= FS_UNCLEAN | FS_NEEDSFSCK;
	disk.d_fs.fs_clean = 0;
}

static ufs2_daddr_t
journal_balloc(void)
{
	ufs2_daddr_t blk;
	struct cg *cgp;
	int valid;
	static int contig = 1;

	cgp = &disk.d_cg;
	for (;;) {
		blk = cgballoc(&disk);
		if (blk > 0)
			break;
		/*
		 * If we failed to allocate a block from this cg, move to
		 * the next.
		 */
		if (cgwrite(&disk) < 0) {
			warn("Failed to write updated cg");
			return (-1);
		}
		while ((valid = cgread(&disk)) == 1) {
			/*
			 * Try to minimize fragmentation by requiring a minimum
			 * number of blocks present.
			 */
			if (cgp->cg_cs.cs_nbfree > 256 * 1024)
				break;
			if (contig == 0 && cgp->cg_cs.cs_nbfree)
				break;
		}
		if (valid)
			continue;
		/*
		 * Try once through looking only for large contiguous regions
		 * and again taking any space we can find.
		 */
		if (contig) {
			contig = 0;
			disk.d_ccg = 0;
			warnx("Journal file fragmented.");
			continue;
		}
		warnx("Failed to find sufficient free blocks for the journal");
		return -1;
	}
	if (bwrite(&disk, fsbtodb(&sblock, blk), clrbuf,
	    sblock.fs_bsize) <= 0) {
		warn("Failed to initialize new block");
		return -1;
	}
	return (blk);
}

/*
 * Search a directory block for the SUJ_FILE.
 */
static ino_t
dir_search(ufs2_daddr_t blk, int bytes)
{
	char block[MAXBSIZE];
	struct direct *dp;
	int off;

	if (bread(&disk, fsbtodb(&sblock, blk), block, bytes) <= 0) {
		warn("Failed to read dir block");
		return (-1);
	}
	for (off = 0; off < bytes; off += dp->d_reclen) {
		dp = (struct direct *)&block[off];
		if (dp->d_reclen == 0)
			break;
		if (dp->d_ino == 0)
			continue;
		if (dp->d_namlen != strlen(SUJ_FILE))
			continue;
		if (bcmp(dp->d_name, SUJ_FILE, dp->d_namlen) != 0)
			continue;
		return (dp->d_ino);
	}

	return (0);
}

/*
 * Search in the UFS_ROOTINO for the SUJ_FILE.  If it exists we can not enable
 * journaling.
 */
static ino_t
journal_findfile(void)
{
	union dinodep dp;
	ino_t ino;
	int i;

	if (getinode(&disk, &dp, UFS_ROOTINO) != 0) {
		warn("Failed to get root inode: %s", disk.d_error);
		return (-1);
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		if ((off_t)dp.dp1->di_size >= lblktosize(&sblock, UFS_NDADDR)) {
			warnx("UFS_ROOTINO extends beyond direct blocks.");
			return (-1);
		}
		for (i = 0; i < UFS_NDADDR; i++) {
			if (dp.dp1->di_db[i] == 0)
				break;
			if ((ino = dir_search(dp.dp1->di_db[i],
			    sblksize(&sblock, (off_t)dp.dp1->di_size, i))) != 0)
				return (ino);
		}
	} else {
		if ((off_t)dp.dp2->di_size >= lblktosize(&sblock, UFS_NDADDR)) {
			warnx("UFS_ROOTINO extends beyond direct blocks.");
			return (-1);
		}
		for (i = 0; i < UFS_NDADDR; i++) {
			if (dp.dp2->di_db[i] == 0)
				break;
			if ((ino = dir_search(dp.dp2->di_db[i],
			    sblksize(&sblock, (off_t)dp.dp2->di_size, i))) != 0)
				return (ino);
		}
	}

	return (0);
}

static void
dir_clear_block(const char *block, off_t off)
{
	struct direct *dp;

	for (; off < sblock.fs_bsize; off += DIRBLKSIZ) {
		dp = (struct direct *)&block[off];
		dp->d_ino = 0;
		dp->d_reclen = DIRBLKSIZ;
		dp->d_type = DT_UNKNOWN;
	}
}

/*
 * Insert the journal at inode 'ino' into directory blk 'blk' at the first
 * free offset of 'off'.  DIRBLKSIZ blocks after off are initialized as
 * empty.
 */
static int
dir_insert(ufs2_daddr_t blk, off_t off, ino_t ino)
{
	struct direct *dp;
	char block[MAXBSIZE];

	if (bread(&disk, fsbtodb(&sblock, blk), block, sblock.fs_bsize) <= 0) {
		warn("Failed to read dir block");
		return (-1);
	}
	bzero(&block[off], sblock.fs_bsize - off);
	dp = (struct direct *)&block[off];
	dp->d_ino = ino;
	dp->d_reclen = DIRBLKSIZ;
	dp->d_type = DT_REG;
	dp->d_namlen = strlen(SUJ_FILE);
	bcopy(SUJ_FILE, &dp->d_name, strlen(SUJ_FILE));
	dir_clear_block(block, off + DIRBLKSIZ);
	if (bwrite(&disk, fsbtodb(&sblock, blk), block, sblock.fs_bsize) <= 0) {
		warn("Failed to write dir block");
		return (-1);
	}
	return (0);
}

/*
 * Extend a directory block in 'blk' by copying it to a full size block
 * and inserting the new journal inode into .sujournal.
 */
static int
dir_extend(ufs2_daddr_t blk, ufs2_daddr_t nblk, off_t size, ino_t ino)
{
	char block[MAXBSIZE];

	if (bread(&disk, fsbtodb(&sblock, blk), block,
	    roundup(size, sblock.fs_fsize)) <= 0) {
		warn("Failed to read dir block");
		return (-1);
	}
	dir_clear_block(block, size);
	if (bwrite(&disk, fsbtodb(&sblock, nblk), block, sblock.fs_bsize)
	    <= 0) {
		warn("Failed to write dir block");
		return (-1);
	}

	return (dir_insert(nblk, size, ino));
}

/*
 * Insert the journal file into the UFS_ROOTINO directory.  We always extend the
 * last frag
 */
static int
journal_insertfile(ino_t ino)
{
	union dinodep dp;
	ufs2_daddr_t nblk;
	ufs2_daddr_t blk;
	ufs_lbn_t lbn;
	int size;
	int off;

	if (getinode(&disk, &dp, UFS_ROOTINO) != 0) {
		warn("Failed to get root inode: %s", disk.d_error);
		sbdirty();
		return (-1);
	}
	blk = 0;
	size = 0;
	nblk = journal_balloc();
	if (nblk <= 0)
		return (-1);
	/*
	 * For simplicity sake we aways extend the UFS_ROOTINO into a new
	 * directory block rather than searching for space and inserting
	 * into an existing block.  However, if the rootino has frags
	 * have to free them and extend the block.
	 */
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		lbn = lblkno(&sblock, dp.dp1->di_size);
		off = blkoff(&sblock, dp.dp1->di_size);
		blk = dp.dp1->di_db[lbn];
		size = sblksize(&sblock, (off_t)dp.dp1->di_size, lbn);
	} else {
		lbn = lblkno(&sblock, dp.dp2->di_size);
		off = blkoff(&sblock, dp.dp2->di_size);
		blk = dp.dp2->di_db[lbn];
		size = sblksize(&sblock, (off_t)dp.dp2->di_size, lbn);
	}
	if (off != 0) {
		if (dir_extend(blk, nblk, off, ino) == -1)
			return (-1);
	} else {
		blk = 0;
		if (dir_insert(nblk, 0, ino) == -1)
			return (-1);
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		dp.dp1->di_blocks += (sblock.fs_bsize - size) / DEV_BSIZE;
		dp.dp1->di_db[lbn] = nblk;
		dp.dp1->di_size = lblktosize(&sblock, lbn+1);
	} else {
		dp.dp2->di_blocks += (sblock.fs_bsize - size) / DEV_BSIZE;
		dp.dp2->di_db[lbn] = nblk;
		dp.dp2->di_size = lblktosize(&sblock, lbn+1);
	}
	if (putinode(&disk) < 0) {
		warn("Failed to write root inode: %s", disk.d_error);
		return (-1);
	}
	if (cgwrite(&disk) < 0) {
		warn("Failed to write updated cg");
		sbdirty();
		return (-1);
	}
	if (blk) {
		if (cgbfree(&disk, blk, size) < 0) {
			warn("Failed to write cg");
			return (-1);
		}
	}

	return (0);
}

static int
indir_fill(ufs2_daddr_t blk, int level, int *resid)
{
	char indirbuf[MAXBSIZE];
	ufs1_daddr_t *bap1;
	ufs2_daddr_t *bap2;
	ufs2_daddr_t nblk;
	int ncnt;
	int cnt;
	int i;

	bzero(indirbuf, sizeof(indirbuf));
	bap1 = (ufs1_daddr_t *)indirbuf;
	bap2 = (void *)bap1;
	cnt = 0;
	for (i = 0; i < NINDIR(&sblock) && *resid != 0; i++) {
		nblk = journal_balloc();
		if (nblk <= 0)
			return (-1);
		cnt++;
		if (sblock.fs_magic == FS_UFS1_MAGIC)
			*bap1++ = nblk;
		else
			*bap2++ = nblk;
		if (level != 0) {
			ncnt = indir_fill(nblk, level - 1, resid);
			if (ncnt <= 0)
				return (-1);
			cnt += ncnt;
		} else 
			(*resid)--;
	}
	if (bwrite(&disk, fsbtodb(&sblock, blk), indirbuf,
	    sblock.fs_bsize) <= 0) {
		warn("Failed to write indirect");
		return (-1);
	}
	return (cnt);
}

/*
 * Clear the flag bits so the journal can be removed.
 */
static void
journal_clear(void)
{
	union dinodep dp;
	ino_t ino;

	ino = journal_findfile();
	if (ino == (ino_t)-1 || ino == 0) {
		warnx("Journal file does not exist");
		return;
	}
	printf("Clearing journal flags from inode %ju\n", (uintmax_t)ino);
	if (getinode(&disk, &dp, ino) != 0) {
		warn("Failed to get journal inode: %s", disk.d_error);
		return;
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		dp.dp1->di_flags = 0;
	else
		dp.dp2->di_flags = 0;
	if (putinode(&disk) < 0) {
		warn("Failed to write journal inode: %s", disk.d_error);
		return;
	}
}

static int
journal_alloc(int64_t size)
{
	union dinodep dp;
	ufs2_daddr_t blk;
	struct cg *cgp;
	int resid;
	ino_t ino;
	int blks;
	time_t utime;
	int i;

	cgp = &disk.d_cg;
	ino = 0;

	/*
	 * If the journal file exists we can't allocate it.
	 */
	ino = journal_findfile();
	if (ino == (ino_t)-1) {
		warnx("journal_findfile() failed.");
		return (-1);
	}
	if (ino > 0) {
		warnx("Journal file %s already exists, please remove.",
		    SUJ_FILE);
		return (-1);
	}
	/*
	 * If the user didn't supply a size pick one based on the filesystem
	 * size constrained with hardcoded MIN and MAX values.  We opt for
	 * 1/1024th of the filesystem up to MAX but not exceeding one CG and
	 * not less than the MIN.
	 */
	if (size == 0) {
		size = (sblock.fs_size * sblock.fs_bsize) / 1024;
		size = MIN(SUJ_MAX, size);
		if (size / sblock.fs_fsize > sblock.fs_fpg)
			size = sblock.fs_fpg * sblock.fs_fsize;
		size = MAX(SUJ_MIN, size);
	}
	/* fsck does not support fragments in journal files. */
	size = roundup(size, sblock.fs_bsize);
	resid = blocks = size / sblock.fs_bsize;
	if (sblock.fs_cstotal.cs_nbfree < blocks) {
		warn("Insufficient free space for %jd byte journal", size);
		return (-1);
	}
	/*
	 * Find a cg with enough blocks to satisfy the journal
	 * size.  Presently the journal does not span cgs.
	 */
	while (cgread(&disk) == 1) {
		if (cgp->cg_cs.cs_nifree == 0)
			continue;
		ino = cgialloc(&disk);
		if (ino <= 0)
			break;
		printf("Using inode %ju in cg %d for %jd byte journal\n",
		    (uintmax_t)ino, cgp->cg_cgx, size);
		if (getinode(&disk, &dp, ino) != 0) {
			warn("Failed to get allocated inode: %s", disk.d_error);
			sbdirty();
			goto out;
		}
		/*
		 * We leave fields unrelated to the number of allocated
		 * blocks and size uninitialized.  This causes legacy
		 * fsck implementations to clear the inode.
		 */
		time(&utime);
		if (sblock.fs_magic == FS_UFS1_MAGIC) {
			bzero(dp.dp1, sizeof(*dp.dp1));
			dp.dp1->di_size = size;
			dp.dp1->di_mode = IFREG | IREAD;
			dp.dp1->di_nlink = 1;
			dp.dp1->di_flags =
			    SF_IMMUTABLE | SF_NOUNLINK | UF_NODUMP;
			dp.dp1->di_atime = utime;
			dp.dp1->di_mtime = utime;
			dp.dp1->di_ctime = utime;
		} else {
			bzero(dp.dp2, sizeof(*dp.dp2));
			dp.dp2->di_size = size;
			dp.dp2->di_mode = IFREG | IREAD;
			dp.dp2->di_nlink = 1;
			dp.dp2->di_flags =
			    SF_IMMUTABLE | SF_NOUNLINK | UF_NODUMP;
			dp.dp2->di_atime = utime;
			dp.dp2->di_mtime = utime;
			dp.dp2->di_ctime = utime;
			dp.dp2->di_birthtime = utime;
		}
		for (i = 0; i < UFS_NDADDR && resid; i++, resid--) {
			blk = journal_balloc();
			if (blk <= 0)
				goto out;
			if (sblock.fs_magic == FS_UFS1_MAGIC) {
				dp.dp1->di_db[i] = blk;
				dp.dp1->di_blocks++;
			} else {
				dp.dp2->di_db[i] = blk;
				dp.dp2->di_blocks++;
			}
		}
		for (i = 0; i < UFS_NIADDR && resid; i++) {
			blk = journal_balloc();
			if (blk <= 0)
				goto out;
			blks = indir_fill(blk, i, &resid) + 1;
			if (blks <= 0) {
				sbdirty();
				goto out;
			}
			if (sblock.fs_magic == FS_UFS1_MAGIC) {
				dp.dp1->di_ib[i] = blk;
				dp.dp1->di_blocks += blks;
			} else {
				dp.dp2->di_ib[i] = blk;
				dp.dp2->di_blocks += blks;
			}
		}
		if (sblock.fs_magic == FS_UFS1_MAGIC)
			dp.dp1->di_blocks *= sblock.fs_bsize / disk.d_bsize;
		else
			dp.dp2->di_blocks *= sblock.fs_bsize / disk.d_bsize;
		if (putinode(&disk) < 0) {
			warn("Failed to write allocated inode: %s",
			    disk.d_error);
			sbdirty();
			return (-1);
		}
		if (cgwrite(&disk) < 0) {
			warn("Failed to write updated cg");
			sbdirty();
			return (-1);
		}
		if (journal_insertfile(ino) < 0) {
			sbdirty();
			return (-1);
		}
		sblock.fs_sujfree = 0;
		return (0);
	}
	warnx("Insufficient free space for the journal.");
out:
	return (-1);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n",
"usage: tunefs [-A] [-a enable | disable] [-e maxbpg] [-f avgfilesize]",
"              [-J enable | disable] [-j enable | disable] [-k metaspace]",
"              [-L volname] [-l enable | disable] [-m minfree]",
"              [-N enable | disable] [-n enable | disable]",
"              [-o space | time] [-p] [-s avgfpdir] [-t enable | disable]",
"              special | filesystem");
	exit(2);
}

static void
printfs(void)
{
	warnx("POSIX.1e ACLs: (-a)                                %s",
		(sblock.fs_flags & FS_ACLS)? "enabled" : "disabled");
	warnx("NFSv4 ACLs: (-N)                                   %s",
		(sblock.fs_flags & FS_NFS4ACLS)? "enabled" : "disabled");
	warnx("MAC multilabel: (-l)                               %s",
		(sblock.fs_flags & FS_MULTILABEL)? "enabled" : "disabled");
	warnx("soft updates: (-n)                                 %s", 
		(sblock.fs_flags & FS_DOSOFTDEP)? "enabled" : "disabled");
	warnx("soft update journaling: (-j)                       %s", 
		(sblock.fs_flags & FS_SUJ)? "enabled" : "disabled");
	warnx("gjournal: (-J)                                     %s",
		(sblock.fs_flags & FS_GJOURNAL)? "enabled" : "disabled");
	warnx("trim: (-t)                                         %s", 
		(sblock.fs_flags & FS_TRIM)? "enabled" : "disabled");
	warnx("maximum blocks per file in a cylinder group: (-e)  %d",
	      sblock.fs_maxbpg);
	warnx("average file size: (-f)                            %d",
	      sblock.fs_avgfilesize);
	warnx("average number of files in a directory: (-s)       %d",
	      sblock.fs_avgfpdir);
	warnx("minimum percentage of free space: (-m)             %d%%",
	      sblock.fs_minfree);
	warnx("space to hold for metadata blocks: (-k)            %jd",
	      sblock.fs_metaspace);
	warnx("optimization preference: (-o)                      %s",
	      sblock.fs_optim == FS_OPTSPACE ? "space" : "time");
	if (sblock.fs_minfree >= MINFREE &&
	    sblock.fs_optim == FS_OPTSPACE)
		warnx(OPTWARN, "time", ">=", MINFREE);
	if (sblock.fs_minfree < MINFREE &&
	    sblock.fs_optim == FS_OPTTIME)
		warnx(OPTWARN, "space", "<", MINFREE);
	warnx("volume label: (-L)                                 %s",
		sblock.fs_volname);
}
