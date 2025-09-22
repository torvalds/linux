/*	$OpenBSD: mkfs.c,v 1.102 2024/01/09 03:16:00 guenther Exp $	*/
/*	$NetBSD: mkfs.c,v 1.25 1995/06/18 21:35:38 cgd Exp $	*/

/*
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1980, 1989, 1993
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

#include <sys/param.h>	/* MAXBSIZE DEV_BSIZE roundup btodb setbit */
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <err.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>

#ifndef STANDALONE
#include <stdio.h>
#include <errno.h>
#endif

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

/*
 * Default directory umask.
 */
#define UMASK		0755

#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

/*
 * 'Standard' bad FFS magic.
 */
#define FS_BAD_MAGIC	0x19960408

/*
 * The minimum number of cylinder groups that should be created.
 */
#define MINCYLGRPS	4

/*
 * variables set up by front end.
 */
extern int	mfs;		/* run as the memory based filesystem */
extern int	Nflag;		/* run mkfs without writing file system */
extern int	Oflag;		/* format as an 4.3BSD file system */
extern daddr_t fssize;		/* file system size in 512-byte blocks. */
extern long long	sectorsize;	/* bytes/sector */
extern int	fsize;		/* fragment size */
extern int	bsize;		/* block size */
extern int	maxfrgspercg;	/* maximum fragments per cylinder group */
extern int	minfree;	/* free space threshold */
extern int	opt;		/* optimization preference (space or time) */
extern int	density;	/* number of bytes per inode */
extern int	maxbpg;		/* maximum blocks per file in a cyl group */
extern int	avgfilesize;	/* expected average file size */
extern int	avgfilesperdir;	/* expected number of files per directory */
extern int	quiet;		/* quiet flag */
extern caddr_t	membase;	/* start address of memory based filesystem */

union fs_u {
	struct fs fs;
	char pad[SBSIZE];
} *fsun;
#define sblock	fsun->fs

struct	csum *fscs;

union cg_u {
	struct cg cg;
	char pad[MAXBSIZE];
} *cgun;
#define acg	cgun->cg

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};

int	fsi, fso;

static caddr_t iobuf;
static long iobufsize;

daddr_t	alloc(int, int);
static int	charsperline(void);
static int	ilog2(int);
void		initcg(u_int, time_t);
void		wtfs(daddr_t, int, void *);
int		fsinit1(time_t, mode_t, uid_t, gid_t);
int		fsinit2(time_t, mode_t, uid_t, gid_t);
int		makedir(struct direct *, int);
void		iput(union dinode *, ino_t);
void		setblock(struct fs *, unsigned char *, int);
void		clrblock(struct fs *, unsigned char *, int);
int		isblock(struct fs *, unsigned char *, int);
void		rdfs(daddr_t, int, void *);
void		mkfs(struct partition *, char *, int, int,
		    mode_t, uid_t, gid_t);
static		void checksz(void);

#ifndef STANDALONE
volatile sig_atomic_t cur_cylno;
volatile const char *cur_fsys;
void	siginfo(int sig);

void
siginfo(int sig)
{
	int save_errno = errno;

	dprintf(STDERR_FILENO, "%s: initializing cg %ld/%d\n",
	    cur_fsys, (long)cur_cylno, sblock.fs_ncg);
	errno = save_errno;
}
#endif

void
mkfs(struct partition *pp, char *fsys, int fi, int fo, mode_t mfsmode,
    uid_t mfsuid, gid_t mfsgid)
{
	time_t utime;
	quad_t sizepb;
	int i, j, width, origdensity, fragsperinode, minfpg, optimalfpg;
	int lastminfpg, mincylgrps;
	uint32_t bpg;
	long csfrags;
	u_int cg;
	char tmpbuf[100];	/* XXX this will break in about 2,500 years */

	if ((fsun = calloc(1, sizeof (union fs_u))) == NULL ||
	    (cgun = calloc(1, sizeof (union cg_u))) == NULL)
		err(1, "calloc");

#ifndef STANDALONE
	time(&utime);
#endif
	if (mfs) {
		size_t sz;
		if (fssize > SIZE_MAX / DEV_BSIZE) {
			errno = ENOMEM;
			err(12, "mmap");
		}
		sz = (size_t)fssize * DEV_BSIZE;
		membase = mmap(NULL, sz, PROT_READ|PROT_WRITE,
		    MAP_ANON|MAP_PRIVATE, -1, (off_t)0);
		if (membase == MAP_FAILED)
			err(12, "mmap");
		madvise(membase, sz, MADV_RANDOM);
	}
	fsi = fi;
	fso = fo;
	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 */
	if (Oflag <= 1 && fssize > INT_MAX)
		errx(13, "preposterous size %lld, max is %d", (long long)fssize,
		    INT_MAX);
	if (Oflag == 2 && fssize > MAXDISKSIZE)
		errx(13, "preposterous size %lld, max is %lld",
		    (long long)fssize, MAXDISKSIZE);

	wtfs(fssize - (sectorsize / DEV_BSIZE), sectorsize, (char *)&sblock);

	sblock.fs_postblformat = FS_DYNAMICPOSTBLFMT;
	sblock.fs_avgfilesize = avgfilesize;
	sblock.fs_avgfpdir = avgfilesperdir;

	/*
	 * Collect and verify the block and fragment sizes.
	 */
	if (!POWEROF2(bsize)) {
		errx(16, "block size must be a power of 2, not %d", bsize);
	}
	if (!POWEROF2(fsize)) {
		errx(17, "fragment size must be a power of 2, not %d",
		     fsize);
	}
	if (fsize < sectorsize) {
		errx(18, "fragment size %d is too small, minimum is %lld",
		     fsize, sectorsize);
	}
	if (bsize < MINBSIZE) {
		errx(19, "block size %d is too small, minimum is %d",
		     bsize, MINBSIZE);
	}
	if (bsize > MAXBSIZE) {
		errx(19, "block size %d is too large, maximum is %d",
		     bsize, MAXBSIZE);
	}
	if (bsize < fsize) {
		errx(20, "block size (%d) cannot be smaller than fragment size (%d)",
		     bsize, fsize);
	}
	sblock.fs_bsize = bsize;
	sblock.fs_fsize = fsize;

	/*
	 * Calculate the superblock bitmasks and shifts.
	 */
	sblock.fs_bmask = ~(sblock.fs_bsize - 1);
	sblock.fs_fmask = ~(sblock.fs_fsize - 1);
	sblock.fs_qbmask = ~sblock.fs_bmask;
	sblock.fs_qfmask = ~sblock.fs_fmask;
	sblock.fs_bshift = ilog2(sblock.fs_bsize);
	sblock.fs_fshift = ilog2(sblock.fs_fsize);
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	if (sblock.fs_frag > MAXFRAG) {
		errx(21, "fragment size %d is too small, minimum with block "
		    "size %d is %d", sblock.fs_fsize, sblock.fs_bsize,
		    sblock.fs_bsize / MAXFRAG);
	}
	sblock.fs_fragshift = ilog2(sblock.fs_frag);
	sblock.fs_fsbtodb = ilog2(sblock.fs_fsize / DEV_BSIZE);
	sblock.fs_size = dbtofsb(&sblock, fssize);
	sblock.fs_nspf = sblock.fs_fsize / DEV_BSIZE;
	sblock.fs_maxcontig = 1;
	sblock.fs_nrpos = 1;
	sblock.fs_cpg = 1;

	/*
	 * Before the file system is fully initialized, mark it as invalid.
	 */
	sblock.fs_magic = FS_BAD_MAGIC;

	/*
	 * Set the remaining superblock fields.  Note that for FFS1, media
	 * geometry fields are set to fake values.  This is for compatibility
	 * with really ancient kernels that might still inspect these values.
	 */
	if (Oflag <= 1) {
		sblock.fs_sblockloc = SBLOCK_UFS1;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(int32_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs1_dinode);
		sblock.fs_maxsymlinklen = MAXSYMLINKLEN_UFS1;
		sblock.fs_inodefmt = FS_44INODEFMT;
		sblock.fs_cgoffset = 0;
		sblock.fs_cgmask = 0xffffffff;
		sblock.fs_ffs1_size = sblock.fs_size;
		sblock.fs_rotdelay = 0;
		sblock.fs_rps = 60;
		sblock.fs_interleave = 1;
		sblock.fs_trackskew = 0;
		sblock.fs_cpc = 0;
	} else {
		sblock.fs_inodefmt = FS_44INODEFMT;
		sblock.fs_sblockloc = SBLOCK_UFS2;
		sblock.fs_nindir = sblock.fs_bsize / sizeof(int64_t);
		sblock.fs_inopb = sblock.fs_bsize / sizeof(struct ufs2_dinode);
		sblock.fs_maxsymlinklen = MAXSYMLINKLEN_UFS2;
	}
	sblock.fs_sblkno =
	    roundup(howmany(sblock.fs_sblockloc + SBLOCKSIZE, sblock.fs_fsize),
		sblock.fs_frag);
	sblock.fs_cblkno = (int32_t)(sblock.fs_sblkno +
	    roundup(howmany(SBSIZE, sblock.fs_fsize), sblock.fs_frag));
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;
	sblock.fs_maxfilesize = sblock.fs_bsize * NDADDR - 1;
	for (sizepb = sblock.fs_bsize, i = 0; i < NIADDR; i++) {
		sizepb *= NINDIR(&sblock);
		sblock.fs_maxfilesize += sizepb;
	}
#ifdef notyet
	/*
	 * It is impossible to create a snapshot in case fs_maxfilesize is
	 * smaller than fssize.
	 */
	if (sblock.fs_maxfilesize < (u_quad_t)fssize)
		warnx("WARNING: You will be unable to create snapshots on this "
		    "file system. Correct by using a larger blocksize.");
#endif
	/*
	 * Calculate the number of blocks to put into each cylinder group. The
	 * first goal is to have at least enough data blocks in each cylinder
	 * group to meet the density requirement. Once this goal is achieved
	 * we try to expand to have at least mincylgrps cylinder groups. Once
	 * this goal is achieved, we pack as many blocks into each cylinder
	 * group map as will fit.
	 *
	 * We start by calculating the smallest number of blocks that we can
	 * put into each cylinder group. If this is too big, we reduce the
	 * density until it fits.
	 */
	origdensity = density;
	for (;;) {
		fragsperinode = MAXIMUM(numfrags(&sblock, density), 1);

		minfpg = fragsperinode * INOPB(&sblock);
		if (minfpg > sblock.fs_size)
			minfpg = sblock.fs_size;

		sblock.fs_ipg = INOPB(&sblock);
		sblock.fs_fpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_fpg < minfpg)
			sblock.fs_fpg = minfpg;

		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
		sblock.fs_fpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_fpg < minfpg)
			sblock.fs_fpg = minfpg;

		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));

		if (CGSIZE(&sblock) < (unsigned long)sblock.fs_bsize)
			break;

		density -= sblock.fs_fsize;
	}
	if (density != origdensity)
		warnx("density reduced from %d to %d bytes per inode",
		    origdensity, density);

	/*
	 * Use a lower value for mincylgrps if the user specified a large
	 * number of blocks per cylinder group.  This is needed for, e.g. the
	 * install media which needs to pack 2 files very tightly.
	 */
	mincylgrps = MINCYLGRPS;
	if (maxfrgspercg != INT_MAX) {
		i = sblock.fs_size / maxfrgspercg;
		if (i < MINCYLGRPS)
			mincylgrps = i <= 0 ? 1 : i;
	}

	/*
	 * Start packing more blocks into the cylinder group until it cannot
	 * grow any larger, the number of cylinder groups drops below
	 * mincylgrps, or we reach the requested size.
	 */
	for (;;) {
		sblock.fs_fpg += sblock.fs_frag;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));

		if (sblock.fs_fpg > maxfrgspercg ||
		    sblock.fs_size / sblock.fs_fpg < mincylgrps ||
		    CGSIZE(&sblock) > (unsigned long)sblock.fs_bsize)
			break;
	}
	sblock.fs_fpg -= sblock.fs_frag;
	sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
	    INOPB(&sblock));
	if (sblock.fs_fpg > maxfrgspercg)
		warnx("can't honour -c: minimum is %d", sblock.fs_fpg);

	/*
	 * Check to be sure that the last cylinder group has enough blocks to
	 * be viable. If it is too small, reduce the number of blocks per
	 * cylinder group which will have the effect of moving more blocks into
	 * the last cylinder group.
	 */
	optimalfpg = sblock.fs_fpg;
	for (;;) {
		sblock.fs_ncg = howmany(sblock.fs_size, sblock.fs_fpg);
		lastminfpg = roundup(sblock.fs_iblkno +
		    sblock.fs_ipg / INOPF(&sblock), sblock.fs_frag);
		if (sblock.fs_size < lastminfpg)
			errx(28, "file system size %jd < minimum size of %d "
			    "fragments", (intmax_t)sblock.fs_size, lastminfpg);

		if (sblock.fs_size % sblock.fs_fpg >= lastminfpg ||
		    sblock.fs_size % sblock.fs_fpg == 0)
			break;

		sblock.fs_fpg -= sblock.fs_frag;
		sblock.fs_ipg = roundup(howmany(sblock.fs_fpg, fragsperinode),
		    INOPB(&sblock));
	}

	if (optimalfpg != sblock.fs_fpg)
		warnx("reduced number of fragments per cylinder group from %d"
		    " to %d to enlarge last cylinder group", optimalfpg,
		    sblock.fs_fpg);

	if ((ino_t)sblock.fs_ipg * sblock.fs_ncg >  UINT_MAX)
		errx(42, "more than 2^32 inodes, increase density, block or "
		    "fragment size");

	/*
	 * Back to filling superblock fields.
	 */
	if (Oflag <= 1) {
		sblock.fs_spc = sblock.fs_fpg * sblock.fs_nspf;
		sblock.fs_nsect = sblock.fs_spc;
		sblock.fs_npsect = sblock.fs_spc;
		sblock.fs_ncyl = sblock.fs_ncg;
	}
	sblock.fs_cgsize = fragroundup(&sblock, CGSIZE(&sblock));
	sblock.fs_dblkno = sblock.fs_iblkno + sblock.fs_ipg / INOPF(&sblock);
	sblock.fs_csaddr = cgdmin(&sblock, 0);
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));

	fscs = calloc(1, sblock.fs_cssize);
	if (fscs == NULL)
		errx(31, "calloc failed");

	sblock.fs_sbsize = fragroundup(&sblock, sizeof(struct fs));
	if (sblock.fs_sbsize > SBLOCKSIZE)
		sblock.fs_sbsize = SBLOCKSIZE;

	sblock.fs_minfree = minfree;
	sblock.fs_maxbpg = maxbpg;
	sblock.fs_optim = opt;
	sblock.fs_cgrotor = 0;
	sblock.fs_pendingblocks = 0;
	sblock.fs_pendinginodes = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
	sblock.fs_state = 0;
	sblock.fs_clean = 1;
	sblock.fs_id[0] = (u_int32_t)utime;
	sblock.fs_id[1] = (u_int32_t)arc4random();
	sblock.fs_fsmnt[0] = '\0';

	csfrags = howmany(sblock.fs_cssize, sblock.fs_fsize);
	sblock.fs_dsize = sblock.fs_size - sblock.fs_sblkno -
	    sblock.fs_ncg * (sblock.fs_dblkno - sblock.fs_sblkno);

	sblock.fs_cstotal.cs_nbfree = fragstoblks(&sblock, sblock.fs_dsize) -
	    howmany(csfrags, sblock.fs_frag);
	sblock.fs_cstotal.cs_nffree = fragnum(&sblock, sblock.fs_size) +
	    (fragnum(&sblock, csfrags) > 0 ?
	    sblock.fs_frag - fragnum(&sblock, csfrags) : 0);
	sblock.fs_cstotal.cs_nifree = sblock.fs_ncg * sblock.fs_ipg - ROOTINO;
	sblock.fs_cstotal.cs_ndir = 0;

	sblock.fs_dsize -= csfrags;
	sblock.fs_time = utime;

	if (Oflag <= 1) {
		sblock.fs_ffs1_time = sblock.fs_time;
		sblock.fs_ffs1_dsize = sblock.fs_dsize;
		sblock.fs_ffs1_csaddr = sblock.fs_csaddr;
		sblock.fs_ffs1_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_ffs1_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_ffs1_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_ffs1_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	}

	/*
	 * Dump out summary information about file system.
	 */
	if (!mfs) {
#define B2MBFACTOR (1 / (1024.0 * 1024.0))
		printf("%s: %.1fMB in %jd sectors of %lld bytes\n", fsys,
		    (float)sblock.fs_size * sblock.fs_fsize * B2MBFACTOR,
		    (intmax_t)fsbtodb(&sblock, sblock.fs_size) /
		    (sectorsize / DEV_BSIZE), sectorsize);
		printf("%u cylinder groups of %.2fMB, %d blocks, %u"
		    " inodes each\n", sblock.fs_ncg,
		    (float)sblock.fs_fpg * sblock.fs_fsize * B2MBFACTOR,
		    sblock.fs_fpg / sblock.fs_frag, sblock.fs_ipg);
#undef B2MBFACTOR
		checksz();
	}

	/*
	 * Wipe out old FFS1 superblock if necessary.
	 */
	if (Oflag >= 2) {
		union fs_u *fsun1;
		struct fs *fs1;

		fsun1 = calloc(1, sizeof(union fs_u));
		if (fsun1 == NULL)
			err(39, "calloc");
		fs1 = &fsun1->fs;
		rdfs(SBLOCK_UFS1 / DEV_BSIZE, SBSIZE, (char *)fs1);
		if (fs1->fs_magic == FS_UFS1_MAGIC) {
			fs1->fs_magic = FS_BAD_MAGIC;
			wtfs(SBLOCK_UFS1 / DEV_BSIZE, SBSIZE, (char *)fs1);
		}
		free(fsun1);
	}

	wtfs((int)sblock.fs_sblockloc / DEV_BSIZE, SBSIZE, (char *)&sblock);
	sblock.fs_magic = (Oflag <= 1) ? FS_UFS1_MAGIC : FS_UFS2_MAGIC;

	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	if (!quiet)
		printf("super-block backups (for fsck -b #) at:\n");
#ifndef STANDALONE
	else if (!mfs && isatty(STDIN_FILENO)) {
		signal(SIGINFO, siginfo);
		cur_fsys = fsys;
	}
#endif
	i = 0;
	width = charsperline();
	/*
	* Allocate space for superblock, cylinder group map, and two sets of
	* inode blocks.
	*/
	if (sblock.fs_bsize < SBLOCKSIZE)
		iobufsize = SBLOCKSIZE + 3 * sblock.fs_bsize;
	else
		iobufsize = 4 * sblock.fs_bsize;
	if ((iobuf = malloc(iobufsize)) == NULL)
		errx(38, "cannot allocate I/O buffer");
	bzero(iobuf, iobufsize);
	/*
	 * Make a copy of the superblock into the buffer that we will be
	 * writing out in each cylinder group.
	 */
	bcopy((char *)&sblock, iobuf, SBLOCKSIZE);
	for (cg = 0; cg < sblock.fs_ncg; cg++) {
		cur_cylno = (sig_atomic_t)cg;
		initcg(cg, utime);
		if (quiet)
			continue;
		j = snprintf(tmpbuf, sizeof tmpbuf, " %lld,",
		    (long long)fsbtodb(&sblock, cgsblock(&sblock, cg)));
		if (j >= sizeof tmpbuf)
			j = sizeof tmpbuf - 1;
		if (j < 0 || i+j >= width) {
			printf("\n");
			i = 0;
		}
		i += j;
		printf("%s", tmpbuf);
		fflush(stdout);
	}
	if (!quiet)
		printf("\n");
	if (Nflag && !mfs)
		exit(0);
	/*
	 * Now construct the initial file system, then write out the superblock.
	 */
	if (Oflag <= 1) {
		if (fsinit1(utime, mfsmode, mfsuid, mfsgid))
			errx(32, "fsinit1 failed");
		sblock.fs_ffs1_cstotal.cs_ndir = sblock.fs_cstotal.cs_ndir;
		sblock.fs_ffs1_cstotal.cs_nbfree = sblock.fs_cstotal.cs_nbfree;
		sblock.fs_ffs1_cstotal.cs_nifree = sblock.fs_cstotal.cs_nifree;
		sblock.fs_ffs1_cstotal.cs_nffree = sblock.fs_cstotal.cs_nffree;
	} else {
		if (fsinit2(utime, mfsmode, mfsuid, mfsgid))
			errx(32, "fsinit2 failed");
	}

	wtfs((int)sblock.fs_sblockloc / DEV_BSIZE, SBSIZE, (char *)&sblock);

	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize)
		wtfs(fsbtodb(&sblock, sblock.fs_csaddr + numfrags(&sblock, i)),
		    sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize,
		    ((char *)fscs) + i);

	/*
	 * Update information about this partition in pack label, to that it may
	 * be updated on disk.
	 */
	pp->p_fstype = FS_BSDFFS;
	pp->p_fragblock =
	    DISKLABELV1_FFS_FRAGBLOCK(sblock.fs_fsize, sblock.fs_frag);
	bpg = sblock.fs_fpg / sblock.fs_frag;
	while (bpg > USHRT_MAX)
		bpg >>= 1;
	pp->p_cpg = bpg;
}

/*
 * Initialize a cylinder group.
 */
void
initcg(u_int cg, time_t utime)
{
	u_int i, j, d, dlower, dupper, blkno, start;
	daddr_t cbase, dmax;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	struct csum *cs;

	/*
	 * Determine block bounds for cylinder group.  Allow space for
	 * super block summary information in first cylinder group.
	 */
	cbase = cgbase(&sblock, cg);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	if (fsbtodb(&sblock, cgsblock(&sblock, cg)) + iobufsize / DEV_BSIZE 
	    > fssize)
		errx(40, "inode table does not fit in cylinder group");

	dlower = cgsblock(&sblock, cg) - cbase;
	dupper = cgdmin(&sblock, cg) - cbase;
	if (cg == 0)
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
	cs = &fscs[cg];
	memset(&acg, 0, sblock.fs_cgsize);
	acg.cg_ffs2_time = utime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cg;
	acg.cg_ffs2_niblk = sblock.fs_ipg;
	acg.cg_initediblk = MINIMUM(sblock.fs_ipg, 2 * INOPB(&sblock));
	acg.cg_ndblk = dmax - cbase;

	start = sizeof(struct cg);
	if (Oflag <= 1) {
		/* Hack to maintain compatibility with old fsck. */
		if (cg == sblock.fs_ncg - 1)
			acg.cg_ncyl = 0;
		else
			acg.cg_ncyl = sblock.fs_cpg;
		acg.cg_time = acg.cg_ffs2_time;
		acg.cg_ffs2_time = 0;
		acg.cg_niblk = acg.cg_ffs2_niblk;
		acg.cg_ffs2_niblk = 0;
		acg.cg_initediblk = 0;
		acg.cg_btotoff = start;
		acg.cg_boff = acg.cg_btotoff + sblock.fs_cpg * sizeof(int32_t);
		acg.cg_iusedoff = acg.cg_boff +
		    sblock.fs_cpg * sizeof(u_int16_t);
	} else {
		acg.cg_iusedoff = start;
	}

	acg.cg_freeoff = acg.cg_iusedoff + howmany(sblock.fs_ipg, CHAR_BIT);
	acg.cg_nextfreeoff = acg.cg_freeoff + howmany(sblock.fs_fpg, CHAR_BIT);
	if (acg.cg_nextfreeoff > sblock.fs_cgsize)
		errx(37, "panic: cylinder group too big: %u > %d",
		    acg.cg_nextfreeoff, sblock.fs_cgsize);
	acg.cg_cs.cs_nifree += sblock.fs_ipg;
	if (cg == 0) {
		for (i = 0; i < ROOTINO; i++) {
			setbit(cg_inosused(&acg), i);
			acg.cg_cs.cs_nifree--;
		}
	}
	if (cg > 0) {
		/*
		 * In cg 0, space is reserved for boot and super blocks.
		 */
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			blkno = d / sblock.fs_frag;
			setblock(&sblock, cg_blksfree(&acg), blkno);
			acg.cg_cs.cs_nbfree++;
			if (Oflag <= 1) {
				cg_blktot(&acg)[cbtocylno(&sblock, d)]++;
				cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
				    [cbtorpos(&sblock, d)]++;
			}
		}
	}
	if ((i = dupper % sblock.fs_frag)) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree(&acg), dupper);
			acg.cg_cs.cs_nffree++;
		}
	}
	for (d = dupper;
	    d + sblock.fs_frag <= acg.cg_ndblk;
	    d += sblock.fs_frag) {
		blkno = d / sblock.fs_frag;
		setblock(&sblock, cg_blksfree(&acg), blkno);
		acg.cg_cs.cs_nbfree++;
		if (Oflag <= 1) {
			cg_blktot(&acg)[cbtocylno(&sblock, d)]++;
			cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
			    [cbtorpos(&sblock, d)]++;
		}
	}
	if (d < acg.cg_ndblk) {
		acg.cg_frsum[acg.cg_ndblk - d]++;
		for (; d < acg.cg_ndblk; d++) {
			setbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	*cs = acg.cg_cs;

	/*
	 * Write out the duplicate superblock, the cylinder group map
	 * and two blocks worth of inodes in a single write.
	 */
	start = sblock.fs_bsize > SBLOCKSIZE ? sblock.fs_bsize : SBLOCKSIZE;

	if (cg == 0 && acg.cg_cs.cs_nbfree == 0)
		errx(42, "cg 0: summary info is too large to fit");

	bcopy((char *)&acg, &iobuf[start], sblock.fs_cgsize);
	start += sblock.fs_bsize;
	dp1 = (struct ufs1_dinode *)(&iobuf[start]);
	dp2 = (struct ufs2_dinode *)(&iobuf[start]);
	for (i = MINIMUM(sblock.fs_ipg, 2 * INOPB(&sblock)); i != 0; i--) {
		if (sblock.fs_magic == FS_UFS1_MAGIC) {
			dp1->di_gen = arc4random();
			dp1++;
		} else {
			dp2->di_gen = arc4random();
			dp2++;
		}
	}
	wtfs(fsbtodb(&sblock, cgsblock(&sblock, cg)), iobufsize, iobuf);

	if (Oflag <= 1) {
		/* Initialize inodes for FFS1. */
		for (i = 2 * sblock.fs_frag;
		    i < sblock.fs_ipg / INOPF(&sblock);
		    i += sblock.fs_frag) {
			dp1 = (struct ufs1_dinode *)(&iobuf[start]);
			for (j = 0; j < INOPB(&sblock); j++) {
				dp1->di_gen = arc4random();
				dp1++;
			}
			wtfs(fsbtodb(&sblock, cgimin(&sblock, cg) + i),
			    sblock.fs_bsize, &iobuf[start]);
		}
	}
}

#define PREDEFDIR 2

struct direct root_dir[] = {
	{ ROOTINO, sizeof(struct direct), DT_DIR, 1, "." },
	{ ROOTINO, sizeof(struct direct), DT_DIR, 2, ".." },
};

int
fsinit1(time_t utime, mode_t mfsmode, uid_t mfsuid, gid_t mfsgid)
{
	union dinode node;

	/*
	 * Initialize the node
	 */
	memset(&node, 0, sizeof(node));
	node.dp1.di_atime = utime;
	node.dp1.di_mtime = utime;
	node.dp1.di_ctime = utime;

	/*
	 * Create the root directory.
	 */
	if (mfs) {
		node.dp1.di_mode = IFDIR | mfsmode;
		node.dp1.di_uid = mfsuid;
		node.dp1.di_gid = mfsgid;
	} else {
		node.dp1.di_mode = IFDIR | UMASK;
		node.dp1.di_uid = geteuid();
		node.dp1.di_gid = getegid();
	}
	node.dp1.di_nlink = PREDEFDIR;
	node.dp1.di_size = makedir(root_dir, PREDEFDIR);
	node.dp1.di_db[0] = alloc(sblock.fs_fsize, node.dp1.di_mode);
	if (node.dp1.di_db[0] == 0)
		return (1);

	node.dp1.di_blocks = btodb(fragroundup(&sblock, node.dp1.di_size));

	wtfs(fsbtodb(&sblock, node.dp1.di_db[0]), sblock.fs_fsize, iobuf);
	iput(&node, ROOTINO);

#ifdef notyet
	/*
	* Create the .snap directory.
	*/
	node.dp1.di_mode |= 020;
	node.dp1.di_gid = gid;
	node.dp1.di_nlink = SNAPLINKCNT;
	node.dp1.di_size = makedir(snap_dir, SNAPLINKCNT);

	node.dp1.di_db[0] = alloc(sblock.fs_fsize, node.dp1.di_mode);
	if (node.dp1.di_db[0] == 0)
		return (1);

	node.dp1.di_blocks = btodb(fragroundup(&sblock, node.dp1.di_size));

	wtfs(fsbtodb(&sblock, node.dp1.di_db[0]), sblock.fs_fsize, iobuf);
	iput(&node, ROOTINO + 1);
#endif
	return (0);
}

int
fsinit2(time_t utime, mode_t mfsmode, uid_t mfsuid, gid_t mfsgid)
{
	union dinode node;

	/*
	 * Initialize the node.
	 */
	memset(&node, 0, sizeof(node));
	node.dp2.di_atime = utime;
	node.dp2.di_mtime = utime;
	node.dp2.di_ctime = utime;

	/*
	 * Create the root directory.
	 */
	if (mfs) {
		node.dp2.di_mode = IFDIR | mfsmode;
		node.dp2.di_uid = mfsuid;
		node.dp2.di_gid = mfsgid;
	} else {
		node.dp2.di_mode = IFDIR | UMASK;
		node.dp2.di_uid = geteuid();
		node.dp2.di_gid = getegid();
	}
	node.dp2.di_nlink = PREDEFDIR;
	node.dp2.di_size = makedir(root_dir, PREDEFDIR);

	node.dp2.di_db[0] = alloc(sblock.fs_fsize, node.dp2.di_mode);
	if (node.dp2.di_db[0] == 0)
		return (1);

	node.dp2.di_blocks = btodb(fragroundup(&sblock, node.dp2.di_size));

	wtfs(fsbtodb(&sblock, node.dp2.di_db[0]), sblock.fs_fsize, iobuf);
	iput(&node, ROOTINO);

#ifdef notyet
	/*
	 * Create the .snap directory.
	 */
	node.dp2.di_mode |= 020;
	node.dp2.di_gid = gid;
	node.dp2.di_nlink = SNAPLINKCNT;
	node.dp2.di_size = makedir(snap_dir, SNAPLINKCNT);

	node.dp2.di_db[0] = alloc(sblock.fs_fsize, node.dp2.di_mode);
	if (node.dp2.di_db[0] == 0)
		return (1);

	node.dp2.di_blocks = btodb(fragroundup(&sblock, node.dp2.di_size));

	wtfs(fsbtodb(&sblock, node.dp2.di_db[0]), sblock.fs_fsize, iobuf);
	iput(&node, ROOTINO + 1);
#endif
	return (0);
}

/*
 * construct a set of directory entries in "buf".
 * return size of directory.
 */
int
makedir(struct direct *protodir, int entries)
{
	char *cp;
	int i, spcleft;

	spcleft = DIRBLKSIZ;
	for (cp = iobuf, i = 0; i < entries - 1; i++) {
		protodir[i].d_reclen = DIRSIZ(&protodir[i]);
		memcpy(cp, &protodir[i], protodir[i].d_reclen);
		cp += protodir[i].d_reclen;
		spcleft -= protodir[i].d_reclen;
	}
	protodir[i].d_reclen = spcleft;
	memcpy(cp, &protodir[i], DIRSIZ(&protodir[i]));
	return (DIRBLKSIZ);
}

/*
 * allocate a block or frag
 */
daddr_t
alloc(int size, int mode)
{
	int i, frag;
	daddr_t d, blkno;

	rdfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	if (acg.cg_magic != CG_MAGIC) {
		warnx("cg 0: bad magic number");
		return (0);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		warnx("first cylinder group ran out of space");
		return (0);
	}
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag))
			goto goth;
	warnx("internal error: can't find block in cyl 0");
	return (0);
goth:
	blkno = fragstoblks(&sblock, d);
	clrblock(&sblock, cg_blksfree(&acg), blkno);
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;
	fscs[0].cs_nbfree--;
	if (mode & IFDIR) {
		acg.cg_cs.cs_ndir++;
		sblock.fs_cstotal.cs_ndir++;
		fscs[0].cs_ndir++;
	}
	if (Oflag <= 1) {
		cg_blktot(&acg)[cbtocylno(&sblock, d)]--;
		cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
		    [cbtorpos(&sblock, d)]--;
	}
	if (size != sblock.fs_bsize) {
		frag = howmany(size, sblock.fs_fsize);
		fscs[0].cs_nffree += sblock.fs_frag - frag;
		sblock.fs_cstotal.cs_nffree += sblock.fs_frag - frag;
		acg.cg_cs.cs_nffree += sblock.fs_frag - frag;
		acg.cg_frsum[sblock.fs_frag - frag]++;
		for (i = frag; i < sblock.fs_frag; i++)
			setbit(cg_blksfree(&acg), d + i);
	}
	wtfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	return (d);
}

/*
 * Allocate an inode on the disk
 */
void
iput(union dinode *ip, ino_t ino)
{
	daddr_t d;

	if (Oflag <= 1)
		ip->dp1.di_gen = arc4random();
	else
		ip->dp2.di_gen = arc4random();

	rdfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	if (acg.cg_magic != CG_MAGIC)
		errx(41, "cg 0: bad magic number");

	acg.cg_cs.cs_nifree--;
	setbit(cg_inosused(&acg), ino);

	wtfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);

	sblock.fs_cstotal.cs_nifree--;
	fscs[0].cs_nifree--;
	if (ino >= sblock.fs_ipg * sblock.fs_ncg)
		errx(32, "fsinit: inode value %llu out of range",
		    (unsigned long long)ino);
	d = fsbtodb(&sblock, ino_to_fsba(&sblock, ino));
	rdfs(d, sblock.fs_bsize, iobuf);

	if (Oflag <= 1)
		((struct ufs1_dinode *)iobuf)[ino_to_fsbo(&sblock, ino)] =
		    ip->dp1;
	else
		((struct ufs2_dinode *)iobuf)[ino_to_fsbo(&sblock, ino)] =
		    ip->dp2;

	wtfs(d, sblock.fs_bsize, iobuf);
}

/*
 * read a block from the file system
 */
void
rdfs(daddr_t bno, int size, void *bf)
{
	int n;

	if (mfs) {
		memcpy(bf, membase + bno * DEV_BSIZE, size);
		return;
	}
	n = pread(fsi, bf, size, (off_t)bno * DEV_BSIZE);
	if (n != size) {
		err(34, "rdfs: read error on block %lld", (long long)bno);
	}
}

/*
 * write a block to the file system
 */
void
wtfs(daddr_t bno, int size, void *bf)
{
	int n;

	if (mfs) {
		memcpy(membase + bno * DEV_BSIZE, bf, size);
		return;
	}
	if (Nflag)
		return;
	n = pwrite(fso, bf, size, (off_t)bno * DEV_BSIZE);
	if (n != size) {
		err(36, "wtfs: write error on block %lld", (long long)bno);
	}
}

/*
 * check if a block is available
 */
int
isblock(struct fs *fs, unsigned char *cp, int h)
{
	unsigned char mask;

	switch (fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
#ifdef STANDALONE
		printf("isblock bad fs_frag %d\n", fs->fs_frag);
#else
		warnx("isblock bad fs_frag %d", fs->fs_frag);
#endif
		return (0);
	}
}

/*
 * take a block out of the map
 */
void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch ((fs)->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
#ifdef STANDALONE
		printf("clrblock bad fs_frag %d\n", fs->fs_frag);
#else
		warnx("clrblock bad fs_frag %d", fs->fs_frag);
#endif
		return;
	}
}

/*
 * put a block into the map
 */
void
setblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
#ifdef STANDALONE
		printf("setblock bad fs_frag %d\n", fs->fs_frag);
#else
		warnx("setblock bad fs_frag %d", fs->fs_frag);
#endif
		return;
	}
}

/*
 * Determine the number of characters in a
 * single line.
 */
static int
charsperline(void)
{
	int columns;
	char *cp;
	struct winsize ws;

	columns = 0;
	if ((cp = getenv("COLUMNS")) != NULL)
		columns = strtonum(cp, 1, INT_MAX, NULL);
	if (columns == 0 && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
	    ws.ws_col > 0)
		columns = ws.ws_col;
	if (columns == 0)
		columns = 80;

	return columns;
}

static int
ilog2(int val)
{
	int n;

	for (n = 0; n < sizeof(n) * CHAR_BIT; n++)
		if (1 << n == val)
			return (n);

	errx(1, "ilog2: %d is not a power of 2\n", val);
}

struct inoinfo {
        struct  inoinfo *i_nexthash;    /* next entry in hash chain */
        struct  inoinfo *i_child, *i_sibling, *i_parentp;
        size_t  i_isize;                /* size of inode */
        ino_t   i_number;               /* inode number of this entry */  
        ino_t   i_parent;               /* inode number of parent */
  
        ino_t   i_dotdot;               /* inode number of `..' */
        u_int   i_numblks;              /* size of block array in bytes */
        daddr_t i_blks[1];              /* actually longer */
};

static void
checksz(void)
{
	unsigned long long allocate, maxino, maxfsblock, ndir, bound;
	extern int64_t physmem;
	struct rlimit datasz;

	if (getrlimit(RLIMIT_DATA, &datasz) != 0)
		err(1, "can't get rlimit");

	bound = MINIMUM(datasz.rlim_max, physmem);

	allocate = 0;
	maxino = sblock.fs_ncg * (unsigned long long)sblock.fs_ipg;
	maxfsblock = sblock.fs_size;
	ndir = maxino / avgfilesperdir;

	allocate += roundup(howmany(maxfsblock, NBBY), sizeof(int16_t));
	allocate += (maxino + 1) * 3;
	allocate += sblock.fs_ncg * sizeof(long);
	allocate += (MAXIMUM(ndir, 128) + 10) * sizeof(struct inoinfo);
	allocate += MAXIMUM(ndir, 128) * sizeof(struct inoinfo);

	if (allocate > bound)
		warnx("warning: fsck_ffs will need %lluMB; "
		    "min(ulimit -dH,physmem) is %lluMB",
		    allocate / (1024ULL * 1024ULL),
		    bound / (1024ULL * 1024ULL));
}
