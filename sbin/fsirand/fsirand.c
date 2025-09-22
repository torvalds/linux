/*	$OpenBSD: fsirand.c,v 1.44 2024/05/09 08:35:40 florian Exp $	*/

/*
 * Copyright (c) 1997 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

void usage(int);
int fsirand(char *);

extern char *__progname;

int printonly = 0, force = 0, ignorelabel = 0;

/*
 * Possible locations for the superblock.
 */
static const int sbtry[] = SBLOCKSEARCH;

int
main(int argc, char *argv[])
{
	int n, ex = 0;
	struct rlimit rl;

	while ((n = getopt(argc, argv, "bfp")) != -1) {
		switch (n) {
		case 'b':
			ignorelabel = 1;
			break;
		case 'p':
			printonly = 1;
			break;
		case 'f':
			force = 1;
			break;
		default:
			usage(1);
		}
	}
	if (argc - optind < 1)
		usage(1);

	/* Increase our data size to the max */
	if (getrlimit(RLIMIT_DATA, &rl) == 0) {
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_DATA, &rl) == -1)
			warn("Can't set resource limit to max data size");
	} else
		warn("Can't get resource limit for data size");

	for (n = optind; n < argc; n++) {
		if (argc - optind != 1)
			(void)puts(argv[n]);
		ex += fsirand(argv[n]);
		if (n < argc - 1)
			putchar('\n');
	}

	exit(ex);
}

int
fsirand(char *device)
{
	struct ufs1_dinode *dp1 = NULL;
	struct ufs2_dinode *dp2 = NULL;
	static char *inodebuf;
	size_t ibufsize, isize;
	struct fs *sblock, *tmpsblock;
	ino_t inumber;
	daddr_t sblockloc, dblk;
	char sbuf[SBSIZE], sbuftmp[SBSIZE];
	int devfd, n, i;
	u_int cg;
	char *devpath, *ib;
	u_int32_t bsize = DEV_BSIZE;
	struct disklabel label;

	if ((devfd = opendev(device, printonly ? O_RDONLY : O_RDWR,
	    0, &devpath)) == -1) {
		warn("Can't open %s", devpath);
		return (1);
	}

	/* Get block size (usually 512) from disklabel if possible */
	if (!ignorelabel) {
		if (ioctl(devfd, DIOCGDINFO, &label) == -1)
			warn("Can't read disklabel, using sector size of %d",
			    bsize);
		else
			bsize = label.d_secsize;
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	/* Read in master superblock */
	(void)memset(&sbuf, 0, sizeof(sbuf));
	sblock = (struct fs *)&sbuf;

	for (i = 0; sbtry[i] != -1; i++) {
		sblockloc = sbtry[i];

		if (lseek(devfd, sblockloc, SEEK_SET) == -1) {
			warn("Can't seek to superblock (%lld) on %s",
			    (long long)sblockloc, devpath);
			return (1);
		}

		if ((n = read(devfd, sblock, SBSIZE)) != SBSIZE) {
			warnx("Can't read superblock on %s: %s", devpath,
			    (n < SBSIZE) ? "short read" : strerror(errno));
			return (1);
		}

		/* Find a suitable superblock */
		if (sblock->fs_magic != FS_UFS1_MAGIC &&
		    sblock->fs_magic != FS_UFS2_MAGIC)
			continue; /* Not a superblock */

		/*
		 * Do not look for an FFS1 file system at SBLOCK_UFS2.
		 * Doing so will find the wrong super-block for file
		 * systems with 64k block size.
		 */
		if (sblock->fs_magic == FS_UFS1_MAGIC &&
		    sbtry[i] == SBLOCK_UFS2)
			continue;

		if (sblock->fs_magic == FS_UFS2_MAGIC &&
		    sblock->fs_sblockloc != sbtry[i])
		    	continue; /* Not a superblock */

		break;
	}

	if (sbtry[i] == -1) {
		warnx("Cannot find file system superblock");
		return (1);
	}

	/* Simple sanity checks on the superblock */
	if (sblock->fs_sbsize > SBSIZE) {
		warnx("Superblock size is preposterous");
		return (1);
	}

	if (sblock->fs_postblformat == FS_42POSTBLFMT) {
		warnx("Filesystem format is too old, sorry");
		return (1);
	}

	if (!force && !printonly && sblock->fs_clean != FS_ISCLEAN) {
		warnx("Filesystem is not clean, fsck %s first.", devpath);
		return (1);
	}

	/* Make sure backup superblocks are sane. */
	tmpsblock = (struct fs *)&sbuftmp;
	for (cg = 0; cg < sblock->fs_ncg; cg++) {
		dblk = fsbtodb(sblock, cgsblock(sblock, cg));
		if (lseek(devfd, (off_t)dblk * bsize, SEEK_SET) == -1) {
			warn("Can't seek to %lld", (long long)dblk * bsize);
			return (1);
		} else if ((n = read(devfd, tmpsblock, SBSIZE)) != SBSIZE) {
			warn("Can't read backup superblock %d on %s: %s",
			    cg + 1, devpath, (n < SBSIZE) ? "short read"
			    : strerror(errno));
			return (1);
		}
		if (tmpsblock->fs_magic != FS_UFS1_MAGIC &&
		    tmpsblock->fs_magic != FS_UFS2_MAGIC) {
			warnx("Bad magic number in backup superblock %d on %s",
			    cg + 1, devpath);
			return (1);
		}
		if (tmpsblock->fs_sbsize > SBSIZE) {
			warnx("Size of backup superblock %d on %s is preposterous",
			    cg + 1, devpath);
			return (1);
		}
	}

	/* XXX - should really cap buffer at 512kb or so */
	if (sblock->fs_magic == FS_UFS1_MAGIC)
		isize = sizeof(struct ufs1_dinode);
	else
		isize = sizeof(struct ufs2_dinode);

	if ((ib = reallocarray(inodebuf, sblock->fs_ipg, isize)) == NULL)
		errx(1, "Can't allocate memory for inode buffer");
	inodebuf = ib;
	ibufsize = sblock->fs_ipg * isize;

	if (printonly && (sblock->fs_id[0] || sblock->fs_id[1])) {
		if (sblock->fs_inodefmt >= FS_44INODEFMT && sblock->fs_id[0]) {
			time_t t = sblock->fs_id[0];	/* XXX 2038 */
			char *ct = ctime(&t);
			if (ct)
				(void)printf("%s was randomized on %s", devpath,
				    ct);
			else
				(void)printf("%s was randomized on %lld\n",
				    devpath, t);
		}
		(void)printf("fsid: %x %x\n", sblock->fs_id[0],
		    sblock->fs_id[1]);
	}

	/* Randomize fs_id unless old 4.2BSD filesystem */
	if ((sblock->fs_inodefmt >= FS_44INODEFMT) && !printonly) {
		/* Randomize fs_id and write out new sblock and backups */
		sblock->fs_id[0] = (u_int32_t)time(NULL);
		sblock->fs_id[1] = arc4random();

		if (lseek(devfd, SBOFF, SEEK_SET) == -1) {
			warn("Can't seek to superblock (%lld) on %s",
			    (long long)SBOFF, devpath);
			return (1);
		}
		if ((n = write(devfd, sblock, SBSIZE)) != SBSIZE) {
			warn("Can't write superblock on %s: %s", devpath,
			    (n < SBSIZE) ? "short write" : strerror(errno));
			return (1);
		}
	}

	/* For each cylinder group, randomize inodes and update backup sblock */
	for (cg = 0, inumber = 0; cg < sblock->fs_ncg; cg++) {
		/* Update superblock if appropriate */
		if ((sblock->fs_inodefmt >= FS_44INODEFMT) && !printonly) {
			dblk = fsbtodb(sblock, cgsblock(sblock, cg));
			if (lseek(devfd, (off_t)dblk * bsize,
			    SEEK_SET) == -1) {
				warn("Can't seek to %lld",
				    (long long)dblk * bsize);
				return (1);
			} else if ((n = write(devfd, sblock, SBSIZE)) !=
			    SBSIZE) {
				warn("Can't read backup superblock %d on %s: %s",
				    cg + 1, devpath, (n < SBSIZE) ? "short write"
				    : strerror(errno));
				return (1);
			}
		}

		/* Read in inodes, then print or randomize generation nums */
		dblk = fsbtodb(sblock, ino_to_fsba(sblock, inumber));
		if (lseek(devfd, (off_t)dblk * bsize, SEEK_SET) == -1) {
			warn("Can't seek to %lld", (long long)dblk * bsize);
			return (1);
		} else if ((n = read(devfd, inodebuf, ibufsize)) != ibufsize) {
			warnx("Can't read inodes: %s",
			    (n < ibufsize) ? "short read" : strerror(errno));
			return (1);
		}

		for (n = 0; n < sblock->fs_ipg; n++, inumber++) {
			if (sblock->fs_magic == FS_UFS1_MAGIC)
				dp1 = &((struct ufs1_dinode *)inodebuf)[n];
			else
				dp2 = &((struct ufs2_dinode *)inodebuf)[n];
			if (inumber >= ROOTINO) {
				if (printonly)
					(void)printf("ino %llu gen %x\n",
					    (unsigned long long)inumber,
					    sblock->fs_magic == FS_UFS1_MAGIC ?
					    dp1->di_gen : dp2->di_gen);
				else if (sblock->fs_magic == FS_UFS1_MAGIC)
					dp1->di_gen = arc4random();
				else
					dp2->di_gen = arc4random();
			}
		}

		/* Write out modified inodes */
		if (!printonly) {
			if (lseek(devfd, (off_t)dblk * bsize, SEEK_SET) == -1) {
				warn("Can't seek to %lld",
				    (long long)dblk * bsize);
				return (1);
			} else if ((n = write(devfd, inodebuf, ibufsize)) !=
				 ibufsize) {
				warnx("Can't write inodes: %s",
				    (n != ibufsize) ? "short write" :
				    strerror(errno));
				return (1);
			}
		}
	}
	(void)close(devfd);

	return(0);
}

void
usage(int ex)
{
	(void)fprintf(stderr, "usage: %s [-bfp] special ...\n",
	    __progname);
	exit(ex);
}
