/*	$OpenBSD: installboot.c,v 1.20 2020/03/11 09:59:31 otto Exp $	*/
/*	$NetBSD: installboot.c,v 1.2 1997/04/06 08:41:12 cgd Exp $	*/

/*
 * Copyright (c) 1997 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "bbinfo.h"

#ifndef	ISO_DEFAULT_BLOCK_SIZE
#define	ISO_DEFAULT_BLOCK_SIZE	2048
#endif

int	verbose, nowrite, hflag;
char	*boot, *proto, *dev;

struct bbinfoloc *bbinfolocp;
struct bbinfo *bbinfop;
int	max_block_count;


char		*loadprotoblocks(char *, long *);
int		loadblocknums(char *, int, unsigned long);
static void	devread(int, void *, daddr_t, size_t, char *);
static void	usage(void);
static int	sbchk(struct fs *, daddr_t);
static void	sbread(int, daddr_t, struct fs **, char *);
int		main(int, char *[]);

int	isofsblk = 0;
int	isofseblk = 0;

static const daddr_t sbtry[] = SBLOCKSEARCH;

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: installboot [-n] [-v] [-s isofsblk -e isofseblk] "
	    "<boot> <proto> <device>\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int	c, devfd;
	char	*protostore;
	long	protosize;
	struct stat disksb, bootsb;
	struct disklabel dl;
	daddr_t partoffset;
#define BBPAD   0x1e0
	struct bb {
		char	bb_pad[BBPAD];	/* disklabel lives in here, actually */
		long	bb_secsize;	/* size of secondary boot block */
		long	bb_secstart;	/* start of secondary boot block */
		long	bb_flags;	/* unknown; always zero */
		long	bb_cksum;	/* checksum of the boot block, as longs. */
	} bb;
	long *lp, *ep;

	while ((c = getopt(argc, argv, "vns:e:")) != -1) {
		switch (c) {
		case 'n':
			/* Do not actually write the bootblock to disk */
			nowrite = 1;
			break;
		case 'v':
			/* Chat */
			verbose = 1;
			break;
		case 's':
			isofsblk = atoi(optarg);
			break;
		case 'e':
			isofseblk = atoi(optarg);
			break;
		default:
			usage();
		}
	}

	if (argc - optind < 3)
		usage();

	boot = argv[optind];
	proto = argv[optind + 1];
	dev = argv[optind + 2];

	if (verbose) {
		(void)printf("boot: %s\n", boot);
		(void)printf("proto: %s\n", proto);
		(void)printf("device: %s\n", dev);
	}

	/* Load proto blocks into core */
	if ((protostore = loadprotoblocks(proto, &protosize)) == NULL)
		exit(1);

	/* Open and check raw disk device */
	if ((devfd = opendev(dev, O_RDONLY, OPENDEV_PART, &dev)) < 0)
		err(1, "open: %s", dev);
	if (fstat(devfd, &disksb) == -1)
		err(1, "fstat: %s", dev);
	if (!S_ISCHR(disksb.st_mode))
		errx(1, "%s must be a character device node", dev);
	if ((minor(disksb.st_rdev) % getmaxpartitions()) != getrawpartition())
		errx(1, "%s must be the raw partition", dev);

	/* Extract and load block numbers */
	if (stat(boot, &bootsb) == -1)
		err(1, "stat: %s", boot);
	if (!S_ISREG(bootsb.st_mode))
		errx(1, "%s must be a regular file", boot);
	if ((minor(disksb.st_rdev) / getmaxpartitions()) !=
	    (minor(bootsb.st_dev) / getmaxpartitions()))
		errx(1, "%s must be somewhere on %s", boot, dev);

	/*
	 * Find the offset of the secondary boot block's partition
	 * into the disk.  If disklabels not supported, assume zero.
	 */
	if (ioctl(devfd, DIOCGDINFO, &dl) != -1) {
		partoffset = DL_GETPOFFSET(&dl.d_partitions[minor(bootsb.st_dev) %
		    getmaxpartitions()]);
	} else {
		if (errno != ENOTTY)
			err(1, "read disklabel: %s", dev);
		warnx("couldn't read label from %s, using part offset of 0",
		    dev);
		partoffset = 0;
	}
	if (verbose)
		(void)printf("%s partition offset = 0x%llx\n", boot, partoffset);

	/* Sync filesystems (make sure boot's block numbers are stable) */
	sync();
	sleep(2);
	sync();
	sleep(2);

	if (loadblocknums(boot, devfd, DL_SECTOBLK(&dl, partoffset)) != 0)
		exit(1);

	(void)close(devfd);

	if (nowrite)
		return 0;

#if 0
	/* Write patched proto bootblocks into the superblock */
	if (protosize > SBSIZE - DEV_BSIZE)
		errx(1, "proto bootblocks too big");
#endif

	if ((devfd = opendev(dev, O_RDWR, OPENDEV_PART, &dev)) < 0)
		err(1, "open: %s", dev);

	if (lseek(devfd, DEV_BSIZE, SEEK_SET) != DEV_BSIZE)
		err(1, "lseek bootstrap");

	if (write(devfd, protostore, protosize) != protosize)
		err(1, "write bootstrap");

	if (lseek(devfd, 0, SEEK_SET) != 0)
		err(1, "lseek label");

	if (read(devfd, &bb, sizeof (bb)) != sizeof (bb))
		err(1, "read label");

	bb.bb_secsize = 15;
	bb.bb_secstart = 1;
	bb.bb_flags = 0;
	bb.bb_cksum = 0;

	for (lp = (long *)&bb, ep = &bb.bb_cksum; lp < ep; lp++)
		bb.bb_cksum += *lp;

	if (lseek(devfd, 0, SEEK_SET) != 0)
		err(1, "lseek label 2");

	if (write(devfd, &bb, sizeof bb) != sizeof bb)
		err(1, "write label ");

	(void)close(devfd);
	return 0;
}

char *
loadprotoblocks(char *fname, long *size)
{
	int	fd, sz;
	char	*bp;
	struct	stat statbuf;
	u_int64_t *matchp;

	/*
	 * Read the prototype boot block into memory.
	 */
	if ((fd = open(fname, O_RDONLY)) < 0) {
		warn("open: %s", fname);
		return NULL;
	}
	if (fstat(fd, &statbuf) != 0) {
		warn("fstat: %s", fname);
		close(fd);
		return NULL;
	}
	sz = roundup(statbuf.st_size, DEV_BSIZE);
	if ((bp = calloc(sz, 1)) == NULL) {
		warnx("malloc: %s: no memory", fname);
		close(fd);
		return NULL;
	}
	if (read(fd, bp, statbuf.st_size) != statbuf.st_size) {
		warn("read: %s", fname);
		free(bp);
		close(fd);
		return NULL;
	}
	close(fd);

	/*
	 * Find the magic area of the program, and figure out where
	 * the 'blocks' struct is, from that.
	 */
	bbinfolocp = NULL;
	for (matchp = (u_int64_t *)bp; (char *)matchp < bp + sz; matchp++) {
		if (*matchp != 0xbabefacedeadbeef)
			continue;
		bbinfolocp = (struct bbinfoloc *)matchp;
		if (bbinfolocp->magic1 == 0xbabefacedeadbeef &&
		    bbinfolocp->magic2 == 0xdeadbeeffacebabe)
			break;
		bbinfolocp = NULL;
	}

	if (bbinfolocp == NULL) {
		warnx("%s: not a valid boot block?", fname);
		return NULL;
	}

	bbinfop = (struct bbinfo *)(bp + bbinfolocp->end - bbinfolocp->start);
	memset(bbinfop, 0, sz - (bbinfolocp->end - bbinfolocp->start));
	max_block_count =
	    ((char *)bbinfop->blocks - bp) / sizeof (bbinfop->blocks[0]);

	if (verbose) {
		(void)printf("boot block info locator at offset 0x%x\n",
			(char *)bbinfolocp - bp);
		(void)printf("boot block info at offset 0x%x\n",
			(char *)bbinfop - bp);
		(void)printf("max number of blocks: %d\n", max_block_count);
	}

	*size = sz;
	return (bp);
}

static void
devread(int fd, void *buf, daddr_t blk, size_t size, char *msg)
{
	if (pread(fd, buf, size, dbtob((off_t)blk)) != (ssize_t)size)
		err(1, "%s: devread: pread", msg);
}

static char sblock[SBSIZE];

int
loadblocknums(char *boot, int devfd, unsigned long partoffset)
{
	int		i, fd, ndb;
	struct	stat	statbuf;
	struct	statfs	statfsbuf;
	struct fs	*fs;
	char		*buf;
	daddr32_t	*ap1;
	daddr_t		blk, *ap2;
	struct ufs1_dinode	*ip1;
	struct ufs2_dinode	*ip2;
	int32_t		cksum;

	/*
	 * Open 2nd-level boot program and record the block numbers
	 * it occupies on the filesystem represented by `devfd'.
	 */
	if ((fd = open(boot, O_RDONLY)) < 0)
		err(1, "open: %s", boot);

	if (fstatfs(fd, &statfsbuf) != 0)
		err(1, "statfs: %s", boot);

	if (isofsblk) {
		bbinfop->bsize = ISO_DEFAULT_BLOCK_SIZE;
		bbinfop->nblocks = isofseblk - isofsblk + 1;
		if (bbinfop->nblocks > max_block_count)
			errx(1, "%s: Too many blocks", boot);
		if (verbose)
			(void)printf("%s: starting block %d (%d total):\n\t",
			    boot, isofsblk, bbinfop->nblocks);
		for (i = 0; i < bbinfop->nblocks; i++) {
			blk = (isofsblk + i) * (bbinfop->bsize / DEV_BSIZE);
			bbinfop->blocks[i] = blk;
			if (verbose)
				(void)printf("%d ", blk);
		}
		if (verbose)
			(void)printf("\n");

		cksum = 0;
		for (i = 0; i < bbinfop->nblocks +
		    (sizeof(*bbinfop) / sizeof(bbinfop->blocks[0])) - 1; i++)
			cksum += ((int32_t *)bbinfop)[i];
		bbinfop->cksum = -cksum;

		return 0;
	}

	if (strncmp(statfsbuf.f_fstypename, MOUNT_FFS, MFSNAMELEN))
		errx(1, "%s: must be on a FFS filesystem", boot);

	if (fsync(fd) != 0)
		err(1, "fsync: %s", boot);

	if (fstat(fd, &statbuf) != 0)
		err(1, "fstat: %s", boot);

	close(fd);

	/* Read superblock */
	sbread(devfd, partoffset, &fs, sblock);

	/* Read inode */
	if ((buf = malloc(fs->fs_bsize)) == NULL)
		errx(1, "No memory for filesystem block");

	blk = fsbtodb(fs, ino_to_fsba(fs, statbuf.st_ino));
	devread(devfd, buf, blk + partoffset, fs->fs_bsize, "inode");
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		ip1 = (struct ufs1_dinode *)(buf) + ino_to_fsbo(fs,
		    statbuf.st_ino);
		ndb = howmany(ip1->di_size, fs->fs_bsize);
	} else {
		ip2 = (struct ufs2_dinode *)(buf) + ino_to_fsbo(fs,
		    statbuf.st_ino);
		ndb = howmany(ip2->di_size, fs->fs_bsize);
	}
	/*
	 * Check the block numbers; we don't handle fragments
	 */
	if (ndb > max_block_count)
		errx(1, "%s: Too many blocks", boot);

	/*
	 * Register filesystem block size.
	 */
	bbinfop->bsize = fs->fs_bsize;

	/*
	 * Register block count.
	 */
	bbinfop->nblocks = ndb;

	if (verbose)
		(void)printf("%s: block numbers: ", boot);
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		ap1 = ip1->di_db;
		for (i = 0; i < NDADDR && *ap1 && ndb; i++, ap1++, ndb--) {
			blk = fsbtodb(fs, *ap1);
			bbinfop->blocks[i] = blk + partoffset;
			if (verbose)
				(void)printf("%d ", bbinfop->blocks[i]);
		}
	} else {
		ap2 = ip2->di_db;
		for (i = 0; i < NDADDR && *ap2 && ndb; i++, ap2++, ndb--) {
			blk = fsbtodb(fs, *ap2);
			bbinfop->blocks[i] = blk + partoffset;
			if (verbose)
				(void)printf("%d ", bbinfop->blocks[i]);
		}
	}
	if (verbose)
		(void)printf("\n");

	if (ndb == 0)
		goto checksum;

	/*
	 * Just one level of indirections; there isn't much room
	 * for more in the 1st-level bootblocks anyway.
	 */
	if (verbose)
		(void)printf("%s: block numbers (indirect): ", boot);
	if (fs->fs_magic == FS_UFS1_MAGIC) {
		blk = ip1->di_ib[0];
		devread(devfd, buf, blk + partoffset, fs->fs_bsize,
		    "indirect block");
		ap1 = (daddr32_t *)buf;
		for (; i < NINDIR(fs) && *ap1 && ndb; i++, ap1++, ndb--) {
			blk = fsbtodb(fs, *ap1);
			bbinfop->blocks[i] = blk + partoffset;
			if (verbose)
				(void)printf("%d ", bbinfop->blocks[i]);
		}
	} else {
		blk = ip2->di_ib[0];
		devread(devfd, buf, blk + partoffset, fs->fs_bsize,
		    "indirect block");
		ap2 = (daddr_t *)buf;
		for (; i < NINDIR(fs) && *ap2 && ndb; i++, ap2++, ndb--) {
			blk = fsbtodb(fs, *ap2);
			bbinfop->blocks[i] = blk + partoffset;
			if (verbose)
				(void)printf("%d ", bbinfop->blocks[i]);
		}
	}
	if (verbose)
		(void)printf("\n");

	if (ndb)
		errx(1, "%s: Too many blocks", boot);

checksum:
	cksum = 0;
	for (i = 0; i < bbinfop->nblocks +
	    (sizeof (*bbinfop) / sizeof (bbinfop->blocks[0])) - 1; i++)
		cksum += ((int32_t *)bbinfop)[i];
	bbinfop->cksum = -cksum;

	return 0;
}

static int
sbchk(struct fs *fs, daddr_t sbloc)
{
	if (verbose)
		fprintf(stderr, "looking for superblock at %lld\n", sbloc);

	if (fs->fs_magic != FS_UFS2_MAGIC && fs->fs_magic != FS_UFS1_MAGIC) {
		if (verbose)
			fprintf(stderr, "bad superblock magic 0x%x\n",
			    fs->fs_magic);
		return (0);
	}

	/*
	 * Looking for an FFS1 file system at SBLOCK_UFS2 will find the
	 * wrong superblock for file systems with 64k block size.
	 */
	if (fs->fs_magic == FS_UFS1_MAGIC && sbloc == SBLOCK_UFS2) {
		if (verbose)
			fprintf(stderr, "skipping ffs1 superblock at %lld\n",
			    sbloc);
		return (0);
	}

	if (fs->fs_bsize <= 0 || fs->fs_bsize < sizeof(struct fs) ||
	    fs->fs_bsize > MAXBSIZE) {
		if (verbose)
			fprintf(stderr, "invalid superblock block size %d\n",
			    fs->fs_bsize);
		return (0);
	}

	if (fs->fs_sbsize <= 0 || fs->fs_sbsize > SBSIZE) {
		if (verbose)
			fprintf(stderr, "invalid superblock size %d\n",
			    fs->fs_sbsize);
		return (0);
	}

	if (fs->fs_inopb <= 0) {
		if (verbose)
			fprintf(stderr, "invalid superblock inodes/block %d\n",
			    fs->fs_inopb);
		return (0);
	}

	if (verbose)
		fprintf(stderr, "found valid %s superblock\n",
		    fs->fs_magic == FS_UFS2_MAGIC ? "ffs2" : "ffs1");

	return (1);
}

static void
sbread(int fd, daddr_t poffset, struct fs **fs, char *sblock)
{
	int i;
	daddr_t sboff;

	for (i = 0; sbtry[i] != -1; i++) {
		sboff = sbtry[i] / DEV_BSIZE;
		devread(fd, sblock, poffset + sboff, SBSIZE, "superblock");
		*fs = (struct fs *)sblock;
		if (sbchk(*fs, sbtry[i]))
			break;
	}

	if (sbtry[i] == -1)
		errx(1, "couldn't find ffs superblock");
}
