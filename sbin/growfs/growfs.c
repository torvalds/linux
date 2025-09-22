/*	$OpenBSD: growfs.c,v 1.57 2024/04/23 13:34:50 jsg Exp $	*/
/*
 * Copyright (c) 2000 Christoph Herrmann, Thomas-Henning von Kamptz
 * Copyright (c) 1980, 1989, 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christoph Herrmann and Thomas-Henning von Kamptz, Munich and Frankfurt.
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
 *    must display the following acknowledgment:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors, as well as Christoph
 *      Herrmann and Thomas-Henning von Kamptz.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $TSHeader: src/sbin/growfs/growfs.c,v 1.5 2000/12/12 19:31:00 tomsoft Exp $
 * $FreeBSD: src/sbin/growfs/growfs.c,v 1.25 2006/07/17 20:48:36 stefanf Exp $
 *
 */

#include <sys/param.h>	/* DEV_BSIZE MAXBSIZE setbit isset isclr clrbit */
#include <sys/types.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/stat.h>

#include <stdio.h>
#include <paths.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

#define	rounddown(x, y)	(((x)/(y))*(y))
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

static int quiet;		/* quiet flag */

static union {
	struct	fs fs;
	char	pad[SBLOCKSIZE];
} fsun1, fsun2;
#define	sblock	fsun1.fs	/* the new superblock */
#define	osblock	fsun2.fs	/* the old superblock */

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = SBLOCKSEARCH;
static daddr_t sblockloc;

static union {
	struct	cg cg;
	char	pad[MAXBSIZE];
} cgun1, cgun2;
#define	acg	cgun1.cg	/* a cylinder cgroup (new) */
#define	aocg	cgun2.cg	/* an old cylinder group */

static char	ablk[MAXBSIZE];		/* a block */

static struct csum	*fscs;	/* cylinder summary */

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};
#define	DIP(dp, field) \
	((sblock.fs_magic == FS_UFS1_MAGIC) ? \
	(uint32_t)(dp)->dp1.field : (dp)->dp2.field)
#define	DIP_SET(dp, field, val) do { \
	if (sblock.fs_magic == FS_UFS1_MAGIC) \
		(dp)->dp1.field = (val); \
	else \
		(dp)->dp2.field = (val); \
	} while (0)
static daddr_t		inoblk;			/* inode block address */
static char		inobuf[MAXBSIZE];	/* inode block */
ino_t			maxino;			/* last valid inode */

/*
 * An array of elements of type struct gfs_bpp describes all blocks to
 * be relocated in order to free the space needed for the cylinder group
 * summary for all cylinder groups located in the first cylinder group.
 */
struct gfs_bpp {
	daddr_t		old;		/* old block number */
	daddr_t		new;		/* new block number */
#define GFS_FL_FIRST	1
#define GFS_FL_LAST	2
	unsigned int	flags;		/* special handling required */
	int		found;		/* how many references were updated */
};

static void	growfs(int, int, unsigned int);
static void	rdfs(daddr_t, size_t, void *, int);
static void	wtfs(daddr_t, size_t, void *, int, unsigned int);
static daddr_t alloc(void);
static int	charsperline(void);
static void	usage(void);
static int	isblock(struct fs *, unsigned char *, int);
static void	clrblock(struct fs *, unsigned char *, int);
static void	setblock(struct fs *, unsigned char *, int);
static void	initcg(u_int, time_t, int, unsigned int);
static void	updjcg(u_int, time_t, int, int, unsigned int);
static void	updcsloc(time_t, int, int, unsigned int);
static struct disklabel	*get_disklabel(int);
static void	return_disklabel(int, struct disklabel *, unsigned int);
static union dinode *ginode(ino_t, int, int);
static void	frag_adjust(daddr_t, int);
static int	cond_bl_upd(daddr_t *, struct gfs_bpp *, int, int,
		    unsigned int);
static void	updclst(int);
static void	updrefs(int, ino_t, struct gfs_bpp *, int, int, unsigned int);
static void	indirchk(daddr_t, daddr_t, daddr_t, daddr_t,
		    struct gfs_bpp *, int, int, unsigned int);
static void	ffs1_sb_update(struct fs *, daddr_t);

int	colwidth;

/*
 * Here we actually start growing the filesystem. We basically read the
 * cylinder summary from the first cylinder group as we want to update
 * this on the fly during our various operations. First we handle the
 * changes in the former last cylinder group. Afterwards we create all new
 * cylinder groups. Now we handle the cylinder group containing the
 * cylinder summary which might result in a relocation of the whole
 * structure. In the end we write back the updated cylinder summary, the
 * new superblock, and slightly patched versions of the super block
 * copies.
 */
static void
growfs(int fsi, int fso, unsigned int Nflag)
{
	int	i, j;
	u_int	cg;
	time_t	utime;
	char	tmpbuf[100];

	time(&utime);

	/*
	 * Get the cylinder summary into the memory.
	 */
	fscs = calloc(1, (size_t)sblock.fs_cssize);
	if (fscs == NULL)
		errx(1, "calloc failed");
	for (i = 0; i < osblock.fs_cssize; i += osblock.fs_bsize) {
		rdfs(fsbtodb(&osblock, osblock.fs_csaddr +
		    numfrags(&osblock, i)), (size_t)MINIMUM(osblock.fs_cssize - i,
		    osblock.fs_bsize), (void *)(((char *)fscs)+i), fsi);
	}

	/*
	 * Do all needed changes in the former last cylinder group.
	 */
	updjcg(osblock.fs_ncg - 1, utime, fsi, fso, Nflag);

	/*
	 * Dump out summary information about filesystem.
	 */
#define B2MBFACTOR (1 / (1024.0 * 1024.0))
	printf("growfs: %.1fMB (%jd sectors) block size %d, fragment size %d\n",
	    (float)sblock.fs_size * sblock.fs_fsize * B2MBFACTOR,
	    (intmax_t)fsbtodb(&sblock, sblock.fs_size), sblock.fs_bsize,
	    sblock.fs_fsize);
	printf("\tusing %u cylinder groups of %.2fMB, %d blks, %u inodes.\n",
	    sblock.fs_ncg, (float)sblock.fs_fpg * sblock.fs_fsize * B2MBFACTOR,
	    sblock.fs_fpg / sblock.fs_frag, sblock.fs_ipg);
#undef B2MBFACTOR

	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	if (!quiet)
		printf("super-block backups (for fsck -b #) at:\n");
	i = 0;

	/*
	 * Iterate for only the new cylinder groups.
	 */
	for (cg = osblock.fs_ncg; cg < sblock.fs_ncg; cg++) {
		initcg(cg, utime, fso, Nflag);
		if (quiet)
			continue;
		j = snprintf(tmpbuf, sizeof(tmpbuf), " %lld%s",
		    fsbtodb(&sblock, cgsblock(&sblock, cg)),
		    cg < (sblock.fs_ncg - 1) ? "," : "");
		if (j >= sizeof(tmpbuf))
			j = sizeof(tmpbuf) - 1;
		if (j < 0 || i + j >= colwidth) {
			printf("\n");
			i = 0;
		}
		i += j;
		printf("%s", tmpbuf);
		fflush(stdout);
	}
	if (!quiet)
		printf("\n");

	/*
	 * Do all needed changes in the first cylinder group.
	 * allocate blocks in new location
	 */
	updcsloc(utime, fsi, fso, Nflag);

	/*
	 * Now write the cylinder summary back to disk.
	 */
	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize) {
		wtfs(fsbtodb(&sblock, sblock.fs_csaddr + numfrags(&sblock, i)),
		    (size_t)MINIMUM(sblock.fs_cssize - i, sblock.fs_bsize),
		    (void *)(((char *)fscs) + i), fso, Nflag);
	}

	/*
	 * Now write the new superblock back to disk.
	 */
	sblock.fs_time = utime;
	sblock.fs_clean = 0;
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		sblock.fs_ffs1_time = (int32_t)sblock.fs_time;
		sblock.fs_ffs1_size = (int32_t)sblock.fs_size;
		sblock.fs_ffs1_dsize = (int32_t)sblock.fs_dsize;
		sblock.fs_ffs1_csaddr = (int32_t)sblock.fs_csaddr;
		sblock.fs_ffs1_cstotal.cs_ndir =
		    (int32_t)sblock.fs_cstotal.cs_ndir;
		sblock.fs_ffs1_cstotal.cs_nbfree =
		    (int32_t)sblock.fs_cstotal.cs_nbfree;
		sblock.fs_ffs1_cstotal.cs_nifree =
		    (int32_t)sblock.fs_cstotal.cs_nifree;
		sblock.fs_ffs1_cstotal.cs_nffree =
		    (int32_t)sblock.fs_cstotal.cs_nffree;
	}
	wtfs(sblockloc, (size_t)SBLOCKSIZE, (void *)&sblock, fso, Nflag);

	/*
	 * Clean up the dynamic fields in our superblock copies.
	 */
	sblock.fs_fmod = 0;
	sblock.fs_clean = 1;
	sblock.fs_ronly = 0;
	sblock.fs_cgrotor = 0;
	sblock.fs_state = 0;
	memset(&sblock.fs_fsmnt, 0, sizeof(sblock.fs_fsmnt));

	/*
	 * XXX
	 * The following fields are currently distributed from the  superblock
	 * to the copies:
	 *     fs_minfree
	 *     fs_rotdelay
	 *     fs_maxcontig
	 *     fs_maxbpg
	 *     fs_minfree,
	 *     fs_optim
	 *     fs_flags regarding SOFTPDATES
	 *
	 * We probably should rather change the summary for the cylinder group
	 * statistics here to the value of what would be in there, if the file
	 * system were created initially with the new size. Therefore we still
	 * need to find an easy way of calculating that.
	 * Possibly we can try to read the first superblock copy and apply the
	 * "diffed" stats between the old and new superblock by still  copying
	 * certain parameters onto that.
	 */

	/*
	 * Write out the duplicate superblocks.
	 */
	for (cg = 0; cg < sblock.fs_ncg; cg++) {
		wtfs(fsbtodb(&sblock, cgsblock(&sblock, cg)),
		    (size_t)SBLOCKSIZE, (void *)&sblock, fso, Nflag);
	}
}

/*
 * This creates a new cylinder group structure, for more details please  see
 * the  source of newfs(8), as this function is taken over almost unchanged.
 * As  this  is  never called for the  first  cylinder  group,  the  special
 * provisions for that case are removed here.
 */
static void
initcg(u_int cg, time_t utime, int fso, unsigned int Nflag)
{
	static char *iobuf;
	daddr_t d, dlower, dupper, blkno, start;
	daddr_t i, cbase, dmax;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2;
	struct csum *cs;
	ino_t j;
	size_t iobufsize;

	if (sblock.fs_bsize < SBLOCKSIZE)
		iobufsize = SBLOCKSIZE + 3 * sblock.fs_bsize;
	else
		iobufsize = 4 * sblock.fs_bsize;

	if (iobuf == NULL && (iobuf = malloc(iobufsize)) == NULL)
		errx(37, "panic: cannot allocate I/O buffer");
	bzero(iobuf, iobufsize);

	/*
	 * Determine block bounds for cylinder group.
	 * Allow space for super block summary information in first
	 * cylinder group.
	 */
	cbase = cgbase(&sblock, cg);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dlower = cgsblock(&sblock, cg) - cbase;
	dupper = cgdmin(&sblock, cg) - cbase;
	if (cg == 0) /* XXX fscs may be relocated */
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
	cs = &fscs[cg];
	memset(&acg, 0, sblock.fs_cgsize);
	acg.cg_ffs2_time = utime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cg;
	acg.cg_ffs2_niblk = sblock.fs_ipg;
	acg.cg_initediblk = MINIMUM(sblock.fs_ipg, 2 * INOPB(&sblock));
	acg.cg_ndblk = dmax - cbase;
	if (sblock.fs_contigsumsize > 0)
		acg.cg_nclusterblks = acg.cg_ndblk / sblock.fs_frag;
	start = sizeof(struct cg);
	if (sblock.fs_magic == FS_UFS2_MAGIC) {
		acg.cg_iusedoff = start;
	} else {
		if (cg == sblock.fs_ncg - 1)
			acg.cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
		else
			acg.cg_ncyl = sblock.fs_cpg;
		acg.cg_time = (int32_t)acg.cg_ffs2_time;
		acg.cg_ffs2_time = 0;
		acg.cg_niblk = (int16_t)acg.cg_ffs2_niblk;
		acg.cg_ffs2_niblk = 0;
		acg.cg_initediblk = 0;
		acg.cg_btotoff = start;
		acg.cg_boff = acg.cg_btotoff +
		    sblock.fs_cpg * sizeof(int32_t);
		acg.cg_iusedoff = acg.cg_boff +
		    sblock.fs_cpg * sizeof(u_int16_t);
	}
	acg.cg_freeoff = acg.cg_iusedoff + howmany(sblock.fs_ipg, CHAR_BIT);
	acg.cg_nextfreeoff = acg.cg_freeoff + howmany(sblock.fs_fpg, CHAR_BIT);
	if (sblock.fs_contigsumsize > 0) {
		acg.cg_clustersumoff =
		    roundup(acg.cg_nextfreeoff, sizeof(u_int32_t));
		acg.cg_clustersumoff -= sizeof(u_int32_t);
		acg.cg_clusteroff = acg.cg_clustersumoff +
		    (sblock.fs_contigsumsize + 1) * sizeof(u_int32_t);
		acg.cg_nextfreeoff = acg.cg_clusteroff +
		    howmany(fragstoblks(&sblock, sblock.fs_fpg), CHAR_BIT);
	}
	if (acg.cg_nextfreeoff > sblock.fs_cgsize) {
		/*
		 * This should never happen as we would have had that panic
		 *     already on filesystem creation
		 */
		errx(37, "panic: cylinder group too big");
	}
	acg.cg_cs.cs_nifree += sblock.fs_ipg;
	if (cg == 0) {
		for (i = 0; i < ROOTINO; i++) {
			setbit(cg_inosused(&acg), i);
			acg.cg_cs.cs_nifree--;
		}
	}
	if (cg > 0) {
		/*
		 * In cg 0, beginning space is reserved
		 * for boot and super blocks.
		 */
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			blkno = d / sblock.fs_frag;
			setblock(&sblock, cg_blksfree(&acg), blkno);
			if (sblock.fs_contigsumsize > 0)
				setbit(cg_clustersfree(&acg), blkno);
			acg.cg_cs.cs_nbfree++;
		}
		sblock.fs_dsize += dlower;
	}
	sblock.fs_dsize += acg.cg_ndblk - dupper;
	if ((i = dupper % sblock.fs_frag)) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree(&acg), dupper);
			acg.cg_cs.cs_nffree++;
		}
	}
	for (d = dupper; d + sblock.fs_frag <= acg.cg_ndblk;
	    d += sblock.fs_frag) {
		blkno = d / sblock.fs_frag;
		setblock(&sblock, cg_blksfree(&acg), blkno);
		if (sblock.fs_contigsumsize > 0)
			setbit(cg_clustersfree(&acg), blkno);
		acg.cg_cs.cs_nbfree++;
	}
	if (d < acg.cg_ndblk) {
		acg.cg_frsum[acg.cg_ndblk - d]++;
		for (; d < acg.cg_ndblk; d++) {
			setbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	if (sblock.fs_contigsumsize > 0) {
		int32_t	*sump = cg_clustersum(&acg);
		u_char	*mapp = cg_clustersfree(&acg);
		int	map = *mapp++;
		int	bit = 1;
		int	run = 0;

		for (i = 0; i < acg.cg_nclusterblks; i++) {
			if ((map & bit) != 0)
				run++;
			else if (run != 0) {
				if (run > sblock.fs_contigsumsize)
					run = sblock.fs_contigsumsize;
				sump[run]++;
				run = 0;
			}
			if ((i & (CHAR_BIT - 1)) != CHAR_BIT - 1)
				bit <<= 1;
			else {
				map = *mapp++;
				bit = 1;
			}
		}
		if (run != 0) {
			if (run > sblock.fs_contigsumsize)
				run = sblock.fs_contigsumsize;
			sump[run]++;
		}
	}
	sblock.fs_cstotal.cs_ndir += acg.cg_cs.cs_ndir;
	sblock.fs_cstotal.cs_nffree += acg.cg_cs.cs_nffree;
	sblock.fs_cstotal.cs_nbfree += acg.cg_cs.cs_nbfree;
	sblock.fs_cstotal.cs_nifree += acg.cg_cs.cs_nifree;
	*cs = acg.cg_cs;

	/*
	 * Write out the duplicate superblock, the cylinder group map
	 * and two blocks worth of inodes in a single write.
	 */
	bcopy(&sblock, iobuf, SBLOCKSIZE);
	start = sblock.fs_bsize > SBLOCKSIZE ? sblock.fs_bsize : SBLOCKSIZE;
	bcopy(&acg, &iobuf[start], sblock.fs_cgsize);
	start += sblock.fs_bsize;
	dp1 = (struct ufs1_dinode *)&iobuf[start];
	dp2 = (struct ufs2_dinode *)&iobuf[start];
	for (i = MINIMUM(sblock.fs_ipg, 2 * INOPB(&sblock)); i != 0; i--) {
		if (sblock.fs_magic == FS_UFS1_MAGIC) {
			dp1->di_gen = arc4random();
			dp1++;
		} else {
			dp2->di_gen = arc4random();
			dp2++;
		}
	}
	wtfs(fsbtodb(&sblock, cgsblock(&sblock, cg)), iobufsize,
	    iobuf, fso, Nflag);

	/* Initialize inodes for FFS1. */
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		for (i = 2 * sblock.fs_frag; i < sblock.fs_ipg / INOPF(&sblock);
		    i += sblock.fs_frag) {
			dp1 = (struct ufs1_dinode *)&iobuf[start];
			for (j = 0; j < INOPB(&sblock); j++) {
				dp1->di_gen = arc4random();
				dp1++;
			}
			wtfs(fsbtodb(&sblock, cgimin(&sblock, cg) + i),
			    (size_t)sblock.fs_bsize, &iobuf[start], fso, Nflag);
		}
	}
}

/*
 * Here  we add or subtract (sign +1/-1) the available fragments in  a  given
 * block to or from the fragment statistics. By subtracting before and adding
 * after  an operation on the free frag map we can easy update  the  fragment
 * statistic, which seems to be otherwise a rather complex operation.
 */
static void
frag_adjust(daddr_t frag, int sign)
{
	int fragsize;
	int f;

	fragsize = 0;
	/*
	 * Here frag only needs to point to any fragment in the block we want
	 * to examine.
	 */
	for (f = rounddown(frag, sblock.fs_frag);
	    f < roundup(frag + 1, sblock.fs_frag);
	    f++) {
		/*
		 * Count contiguous free fragments.
		 */
		if (isset(cg_blksfree(&acg), f)) {
			fragsize++;
		} else {
			if (fragsize && fragsize < sblock.fs_frag) {
				/*
				 * We found something in between.
				 */
				acg.cg_frsum[fragsize] += sign;
			}
			fragsize = 0;
		}
	}
	if (fragsize && fragsize < sblock.fs_frag) {
		/*
		 * We found something.
		 */
		acg.cg_frsum[fragsize] += sign;
	}
}

/*
 * Here we conditionally update a pointer to a fragment. We check for all
 * relocated blocks if any of its fragments is referenced by the current
 * field,  and update the pointer to the respective fragment in  our  new
 * block.  If  we find a reference we write back the  block  immediately,
 * as there is no easy way for our general block reading engine to figure
 * out if a write back operation is needed.
 */
static int
cond_bl_upd(daddr_t *block, struct gfs_bpp *field, int fsi, int fso,
    unsigned int Nflag)
{
	struct gfs_bpp	*f;
	daddr_t src, dst;
	int fragnum;
	void *ibuf;

	for (f = field; f->old != 0; f++) {
		src = *block;
		if (fragstoblks(&sblock, src) != f->old)
			continue;
		/*
		 * The fragment is part of the block, so update.
		 */
		dst = blkstofrags(&sblock, f->new);
		fragnum = fragnum(&sblock, src);
		*block = dst + fragnum;
		f->found++;

		/*
		 * Copy the block back immediately.
		 *
		 * XXX	If src is from an indirect block we have
		 *	to implement copy on write here in case of
		 *	active snapshots.
		 */
		ibuf = malloc(sblock.fs_bsize);
		if (!ibuf)
			errx(1, "malloc failed");
		src -= fragnum;
		rdfs(fsbtodb(&sblock, src), (size_t)sblock.fs_bsize, ibuf, fsi);
		wtfs(dst, (size_t)sblock.fs_bsize, ibuf, fso, Nflag);
		free(ibuf);
		/*
		 * The same block can't be found again in this loop.
		 */
		return (1);
	}

	return (0);
}

/*
 * Here we do all needed work for the former last cylinder group. It has to be
 * changed  in  any case, even if the filesystem ended exactly on the  end  of
 * this  group, as there is some slightly inconsistent handling of the  number
 * of cylinders in the cylinder group. We start again by reading the  cylinder
 * group from disk. If the last block was not fully available, we first handle
 * the  missing  fragments, then we handle all new full blocks  in  that  file
 * system  and  finally we handle the new last fragmented block  in  the  file
 * system.  We again have to handle the fragment statistics rotational  layout
 * tables and cluster summary during all those operations.
 */
static void
updjcg(u_int cg, time_t utime, int fsi, int fso, unsigned int Nflag)
{
	daddr_t	cbase, dmax, dupper;
	struct csum	*cs;
	int	i, k;
	int	j = 0;

	/*
	 * Read the former last (joining) cylinder group from disk, and make
	 * a copy.
	 */
	rdfs(fsbtodb(&osblock, cgtod(&osblock, cg)),
	    (size_t)osblock.fs_cgsize, (void *)&aocg, fsi);

	memcpy(&cgun1, &cgun2, sizeof(cgun2));

	/*
	 * If the cylinder group had already its new final size almost
	 * nothing is to be done ... except:
	 * For some reason the value of cg_ncyl in the last cylinder group has
	 * to  be  zero instead of fs_cpg. As this is now no longer  the  last
	 * cylinder group we have to change that value now to fs_cpg.
	 */
	if (cgbase(&osblock, cg+1) == osblock.fs_size) {
		if (sblock.fs_magic == FS_UFS1_MAGIC)
			acg.cg_ncyl = sblock.fs_cpg;

		wtfs(fsbtodb(&sblock, cgtod(&sblock, cg)),
		    (size_t)sblock.fs_cgsize, (void *)&acg, fso, Nflag);

		return;
	}

	/*
	 * Set up some variables needed later.
	 */
	cbase = cgbase(&sblock, cg);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dupper = cgdmin(&sblock, cg) - cbase;
	if (cg == 0)	/* XXX fscs may be relocated */
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);

	/*
	 * Set pointer to the cylinder summary for our cylinder group.
	 */
	cs = fscs + cg;

	/*
	 * Touch the cylinder group, update all fields in the cylinder group as
	 * needed, update the free space in the superblock.
	 */
	acg.cg_time = utime;
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		if (cg == sblock.fs_ncg - 1) {
			/*
			 * This is still the last cylinder group.
			 */
			acg.cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
		} else {
			acg.cg_ncyl = sblock.fs_cpg;
		}
	}
	acg.cg_ndblk = dmax - cbase;
	sblock.fs_dsize += acg.cg_ndblk-aocg.cg_ndblk;
	if (sblock.fs_contigsumsize > 0)
		acg.cg_nclusterblks = acg.cg_ndblk / sblock.fs_frag;

	/*
	 * Now  we have to update the free fragment bitmap for our new  free
	 * space.  There again we have to handle the fragmentation and  also
	 * the  rotational  layout tables and the cluster summary.  This  is
	 * also  done per fragment for the first new block if the  old  file
	 * system end was not on a block boundary, per fragment for the  new
	 * last block if the new filesystem end is not on a block boundary,
	 * and per block for all space in between.
	 *
	 * Handle the first new block here if it was partially available
	 * before.
	 */
	if (osblock.fs_size % sblock.fs_frag) {
		if (roundup(osblock.fs_size, sblock.fs_frag) <= sblock.fs_size) {
			/*
			 * The new space is enough to fill at least this
			 * block
			 */
			j = 0;
			for (i = roundup(osblock.fs_size-cbase, sblock.fs_frag) - 1;
			    i >= osblock.fs_size-cbase; i--) {
				setbit(cg_blksfree(&acg), i);
				acg.cg_cs.cs_nffree++;
				j++;
			}

			/*
			 * Check  if the fragment just created could join  an
			 * already existing fragment at the former end of the
			 * filesystem.
			 */
			if (isblock(&sblock, cg_blksfree(&acg),
			    ((osblock.fs_size - cgbase(&sblock, cg))/
			    sblock.fs_frag))) {
				/*
				 * The block is now completely available.
				 */
				acg.cg_frsum[osblock.fs_size%sblock.fs_frag]--;
				acg.cg_cs.cs_nbfree++;
				acg.cg_cs.cs_nffree-=sblock.fs_frag;
				k = rounddown(osblock.fs_size-cbase,
				    sblock.fs_frag);
				updclst((osblock.fs_size-cbase)/sblock.fs_frag);
			} else {
				/*
				 * Lets rejoin a possible partially growed
				 * fragment.
				 */
				k = 0;
				while (isset(cg_blksfree(&acg), i) &&
				    (i >= rounddown(osblock.fs_size - cbase,
				    sblock.fs_frag))) {
					i--;
					k++;
				}
				if (k)
					acg.cg_frsum[k]--;
				acg.cg_frsum[k + j]++;
			}
		} else {
			/*
			 * We only grow by some fragments within this last
			 * block.
			 */
			for (i = sblock.fs_size-cbase-1;
			    i >= osblock.fs_size-cbase; i--) {
				setbit(cg_blksfree(&acg), i);
				acg.cg_cs.cs_nffree++;
				j++;
			}
			/*
			 * Lets rejoin a possible partially growed fragment.
			 */
			k = 0;
			while (isset(cg_blksfree(&acg), i) &&
			    (i >= rounddown(osblock.fs_size - cbase,
			    sblock.fs_frag))) {
				i--;
				k++;
			}
			if (k)
				acg.cg_frsum[k]--;
			acg.cg_frsum[k + j]++;
		}
	}

	/*
	 * Handle all new complete blocks here.
	 */
	for (i = roundup(osblock.fs_size - cbase, sblock.fs_frag);
	    i + sblock.fs_frag <= dmax-cbase;	/* XXX <= or only < ? */
	    i += sblock.fs_frag) {
		j = i / sblock.fs_frag;
		setblock(&sblock, cg_blksfree(&acg), j);
		updclst(j);
		acg.cg_cs.cs_nbfree++;
	}

	/*
	 * Handle the last new block if there are stll some new fragments left.
	 * Here  we don't have to bother about the cluster summary or the  even
	 * the rotational layout table.
	 */
	if (i < (dmax - cbase)) {
		acg.cg_frsum[dmax - cbase - i]++;
		for (; i < dmax - cbase; i++) {
			setbit(cg_blksfree(&acg), i);
			acg.cg_cs.cs_nffree++;
		}
	}

	sblock.fs_cstotal.cs_nffree +=
	    (acg.cg_cs.cs_nffree - aocg.cg_cs.cs_nffree);
	sblock.fs_cstotal.cs_nbfree +=
	    (acg.cg_cs.cs_nbfree - aocg.cg_cs.cs_nbfree);
	/*
	 * The following statistics are not changed here:
	 *     sblock.fs_cstotal.cs_ndir
	 *     sblock.fs_cstotal.cs_nifree
	 * As the statistics for this cylinder group are ready, copy it to
	 * the summary information array.
	 */
	*cs = acg.cg_cs;

	/*
	 * Write the updated "joining" cylinder group back to disk.
	 */
	wtfs(fsbtodb(&sblock, cgtod(&sblock, cg)), (size_t)sblock.fs_cgsize,
	    (void *)&acg, fso, Nflag);
}

/*
 * Here  we update the location of the cylinder summary. We have  two  possible
 * ways of growing the cylinder summary.
 * (1)	We can try to grow the summary in the current location, and  relocate
 *	possibly used blocks within the current cylinder group.
 * (2)	Alternatively we can relocate the whole cylinder summary to the first
 *	new completely empty cylinder group. Once the cylinder summary is  no
 *	longer in the beginning of the first cylinder group you should  never
 *	use  a version of fsck which is not aware of the possibility to  have
 *	this structure in a non standard place.
 * Option (1) is considered to be less intrusive to the structure of the  file-
 * system. So we try to stick to that whenever possible. If there is not enough
 * space  in the cylinder group containing the cylinder summary we have to  use
 * method  (2). In case of active snapshots in the filesystem we  probably  can
 * completely avoid implementing copy on write if we stick to method (2) only.
 */
static void
updcsloc(time_t utime, int fsi, int fso, unsigned int Nflag)
{
	struct csum	*cs;
	int	ocscg, ncscg;
	int	blocks;
	daddr_t	cbase, dupper, odupper, d, f, g;
	int	ind;
	u_int	cg, inc;
	struct gfs_bpp	*bp;
	int	i, l;
	int	lcs = 0;
	int	block;

	if (howmany(sblock.fs_cssize, sblock.fs_fsize) ==
	    howmany(osblock.fs_cssize, osblock.fs_fsize)) {
		/*
		 * No new fragment needed.
		 */
		return;
	}
	ocscg = dtog(&osblock, osblock.fs_csaddr);
	cs = fscs + ocscg;
	blocks = 1+howmany(sblock.fs_cssize, sblock.fs_bsize)-
	    howmany(osblock.fs_cssize, osblock.fs_bsize);

	/*
	 * Read original cylinder group from disk, and make a copy.
	 * XXX	If Nflag is set in some very rare cases we now miss
	 *	some changes done in updjcg by reading the unmodified
	 *	block from disk.
	 */
	rdfs(fsbtodb(&osblock, cgtod(&osblock, ocscg)),
	    (size_t)osblock.fs_cgsize, (void *)&aocg, fsi);

	memcpy(&cgun1, &cgun2, sizeof(cgun2));

	/*
	 * Touch the cylinder group, set up local variables needed later
	 * and update the superblock.
	 */
	acg.cg_time = utime;

	/*
	 * XXX	In the case of having active snapshots we may need much more
	 *	blocks for the copy on write. We need each block twice,  and
	 *	also  up to 8*3 blocks for indirect blocks for all  possible
	 *	references.
	 */
	if (/*((int)sblock.fs_time & 0x3) > 0 || */ cs->cs_nbfree < blocks) {
		/*
		 * There  is  not enough space in the old cylinder  group  to
		 * relocate  all blocks as needed, so we relocate  the  whole
		 * cylinder  group summary to a new group. We try to use  the
		 * first complete new cylinder group just created. Within the
		 * cylinder  group we align the area immediately  after  the
		 * cylinder  group  information location in order  to  be  as
		 * close as possible to the original implementation of ffs.
		 *
		 * First  we have to make sure we'll find enough space in  the
		 * new  cylinder  group. If not, then we  currently  give  up.
		 * We  start  with freeing everything which was  used  by  the
		 * fragments of the old cylinder summary in the current group.
		 * Now  we write back the group meta data, read in the  needed
		 * meta data from the new cylinder group, and start allocating
		 * within  that  group. Here we can assume, the  group  to  be
		 * completely empty. Which makes the handling of fragments and
		 * clusters a lot easier.
		 */
		if (sblock.fs_ncg-osblock.fs_ncg < 2)
			errx(2, "panic: not enough space");

		/*
		 * Point "d" to the first fragment not used by the cylinder
		 * summary.
		 */
		d = osblock.fs_csaddr + (osblock.fs_cssize / osblock.fs_fsize);

		/*
		 * Set up last cluster size ("lcs") already here. Calculate
		 * the size for the trailing cluster just behind where  "d"
		 * points to.
		 */
		if (sblock.fs_contigsumsize > 0) {
			for (block = howmany(d % sblock.fs_fpg, sblock.fs_frag),
			    lcs = 0; lcs < sblock.fs_contigsumsize;
			    block++, lcs++) {
				if (isclr(cg_clustersfree(&acg), block))
					break;
			}
		}

		/*
		 * Point "d" to the last frag used by the cylinder summary.
		 */
		d--;

		if ((d + 1) % sblock.fs_frag) {
			/*
			 * The end of the cylinder summary is not a complete
			 * block.
			 */
			frag_adjust(d % sblock.fs_fpg, -1);
			for (; (d + 1) % sblock.fs_frag; d--) {
				setbit(cg_blksfree(&acg), d % sblock.fs_fpg);
				acg.cg_cs.cs_nffree++;
				sblock.fs_cstotal.cs_nffree++;
			}
			/*
			 * Point  "d" to the last fragment of the  last
			 * (incomplete) block of the cylinder summary.
			 */
			d++;
			frag_adjust(d % sblock.fs_fpg, 1);

			if (isblock(&sblock, cg_blksfree(&acg),
			    (d % sblock.fs_fpg) / sblock.fs_frag)) {
				acg.cg_cs.cs_nffree -= sblock.fs_frag;
				acg.cg_cs.cs_nbfree++;
				sblock.fs_cstotal.cs_nffree -= sblock.fs_frag;
				sblock.fs_cstotal.cs_nbfree++;
				if (sblock.fs_contigsumsize > 0) {
					setbit(cg_clustersfree(&acg),
					    (d % sblock.fs_fpg) / sblock.fs_frag);
					if (lcs < sblock.fs_contigsumsize) {
						if (lcs) {
							cg_clustersum(&acg)
							    [lcs]--;
						}
						lcs++;
						cg_clustersum(&acg)[lcs]++;
					}
				}
			}
			/*
			 * Point "d" to the first fragment of the block before
			 * the last incomplete block.
			 */
			d--;
		}

		for (d = rounddown(d, sblock.fs_frag); d >= osblock.fs_csaddr;
		    d -= sblock.fs_frag) {
			setblock(&sblock, cg_blksfree(&acg),
			    (d % sblock.fs_fpg) / sblock.fs_frag);
			acg.cg_cs.cs_nbfree++;
			sblock.fs_cstotal.cs_nbfree++;
			if (sblock.fs_contigsumsize > 0) {
				setbit(cg_clustersfree(&acg),
				    (d % sblock.fs_fpg) / sblock.fs_frag);
				/*
				 * The last cluster size is already set up.
				 */
				if (lcs < sblock.fs_contigsumsize) {
					if (lcs) {
						cg_clustersum(&acg)[lcs]--;
					}
					lcs++;
					cg_clustersum(&acg)[lcs]++;
				}
			}
		}
		*cs = acg.cg_cs;

		/*
		 * Now write the former cylinder group containing the cylinder
		 * summary back to disk.
		 */
		wtfs(fsbtodb(&sblock, cgtod(&sblock, ocscg)),
		    (size_t)sblock.fs_cgsize, (void *)&acg, fso, Nflag);

		/*
		 * Find the beginning of the new cylinder group containing the
		 * cylinder summary.
		 */
		sblock.fs_csaddr = cgdmin(&sblock, osblock.fs_ncg);
		ncscg = dtog(&sblock, sblock.fs_csaddr);
		cs = fscs + ncscg;


		/*
		 * If Nflag is specified, we would now read random data instead
		 * of an empty cg structure from disk. So we can't simulate that
		 * part for now.
		 */
		if (Nflag)
			return;

		/*
		 * Read the future cylinder group containing the cylinder
		 * summary from disk, and make a copy.
		 */
		rdfs(fsbtodb(&sblock, cgtod(&sblock, ncscg)),
		    (size_t)sblock.fs_cgsize, &aocg, fsi);

		memcpy(&cgun1, &cgun2, sizeof(cgun2));

		/*
		 * Allocate all complete blocks used by the new cylinder
		 * summary.
		 */
		for (d = sblock.fs_csaddr; d + sblock.fs_frag <=
		    sblock.fs_csaddr + (sblock.fs_cssize / sblock.fs_fsize);
		    d += sblock.fs_frag) {
			clrblock(&sblock, cg_blksfree(&acg),
			    (d%sblock.fs_fpg)/sblock.fs_frag);
			acg.cg_cs.cs_nbfree--;
			sblock.fs_cstotal.cs_nbfree--;
			if (sblock.fs_contigsumsize > 0) {
				clrbit(cg_clustersfree(&acg),
				    (d % sblock.fs_fpg) / sblock.fs_frag);
			}
		}

		/*
		 * Allocate all fragments used by the cylinder summary in the
		 * last block.
		 */
		if (d < sblock.fs_csaddr + (sblock.fs_cssize / sblock.fs_fsize)) {
			for (; d - sblock.fs_csaddr <
			    sblock.fs_cssize/sblock.fs_fsize;
			    d++) {
				clrbit(cg_blksfree(&acg), d%sblock.fs_fpg);
				acg.cg_cs.cs_nffree--;
				sblock.fs_cstotal.cs_nffree--;
			}
			acg.cg_cs.cs_nbfree--;
			acg.cg_cs.cs_nffree += sblock.fs_frag;
			sblock.fs_cstotal.cs_nbfree--;
			sblock.fs_cstotal.cs_nffree += sblock.fs_frag;
			if (sblock.fs_contigsumsize > 0) {
				clrbit(cg_clustersfree(&acg),
				    (d%sblock.fs_fpg) / sblock.fs_frag);
			}

			frag_adjust(d % sblock.fs_fpg, 1);
		}
		/*
		 * XXX	Handle the cluster statistics here in the case  this
		 *	cylinder group is now almost full, and the remaining
		 *	space is less then the maximum cluster size. This is
		 *	probably not needed, as you would hardly find a file
		 *	system which has only MAXCSBUFS+FS_MAXCONTIG of free
		 *	space right behind the cylinder group information in
		 *	any new cylinder group.
		 */

		/*
		 * Update our statistics in the cylinder summary.
		 */
		*cs = acg.cg_cs;

		/*
		 * Write the new cylinder group containing the cylinder summary
		 * back to disk.
		 */
		wtfs(fsbtodb(&sblock, cgtod(&sblock, ncscg)),
		    (size_t)sblock.fs_cgsize, (void *)&acg, fso, Nflag);
		return;
	}
	/*
	 * We have got enough of space in the current cylinder group, so we
	 * can relocate just a few blocks, and let the summary  information
	 * grow in place where it is right now.
	 */
	cbase = cgbase(&osblock, ocscg);	/* old and new are equal */
	dupper = sblock.fs_csaddr - cbase +
	    howmany(sblock.fs_cssize, sblock.fs_fsize);
	odupper = osblock.fs_csaddr - cbase +
	    howmany(osblock.fs_cssize, osblock.fs_fsize);

	sblock.fs_dsize -= dupper-odupper;

	/*
	 * Allocate the space for the array of blocks to be relocated.
	 */
	bp = calloc(((dupper-odupper) / sblock.fs_frag + 2),
	    sizeof(struct gfs_bpp));
	if (bp == NULL)
		errx(1, "calloc failed");

	/*
	 * Lock all new frags needed for the cylinder group summary. This  is
	 * done per fragment in the first and last block of the new  required
	 * area, and per block for all other blocks.
	 *
	 * Handle the first new  block here (but only if some fragments where
	 * already used for the cylinder summary).
	 */
	ind = 0;
	frag_adjust(odupper, -1);
	for (d = odupper; ((d < dupper) && (d % sblock.fs_frag)); d++) {
		if (isclr(cg_blksfree(&acg), d)) {
			if (!ind) {
				bp[ind].old = d / sblock.fs_frag;
				bp[ind].flags|=GFS_FL_FIRST;
				if (roundup(d, sblock.fs_frag) >= dupper)
					bp[ind].flags |= GFS_FL_LAST;
				ind++;
			}
		} else {
			clrbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree--;
			sblock.fs_cstotal.cs_nffree--;
		}
		/*
		 * No cluster handling is needed here, as there was at least
		 * one  fragment in use by the cylinder summary in  the  old
		 * filesystem.
		 * No block - free counter handling here as this block was not
		 * a free block.
		 */
	}
	frag_adjust(odupper, 1);

	/*
	 * Handle all needed complete blocks here.
	 */
	for (; d + sblock.fs_frag <= dupper; d += sblock.fs_frag) {
		if (!isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag)) {
			for (f = d; f < d + sblock.fs_frag; f++) {
				if (isset(cg_blksfree(&aocg), f)) {
					acg.cg_cs.cs_nffree--;
					sblock.fs_cstotal.cs_nffree--;
				}
			}
			clrblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag);
			bp[ind].old = d / sblock.fs_frag;
			ind++;
		} else {
			clrblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag);
			acg.cg_cs.cs_nbfree--;
			sblock.fs_cstotal.cs_nbfree--;
			if (sblock.fs_contigsumsize > 0) {
				clrbit(cg_clustersfree(&acg), d / sblock.fs_frag);
				for (lcs = 0, l = (d / sblock.fs_frag) + 1;
				    lcs < sblock.fs_contigsumsize;
				    l++, lcs++) {
					if (isclr(cg_clustersfree(&acg), l))
						break;
				}
				if (lcs < sblock.fs_contigsumsize) {
					cg_clustersum(&acg)[lcs + 1]--;
					if (lcs)
						cg_clustersum(&acg)[lcs]++;
				}
			}
		}
		/*
		 * No fragment counter handling is needed here, as this finally
		 * doesn't change after the relocation.
		 */
	}

	/*
	 * Handle all fragments needed in the last new affected block.
	 */
	if (d < dupper) {
		frag_adjust(dupper - 1, -1);

		if (isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag)) {
			acg.cg_cs.cs_nbfree--;
			sblock.fs_cstotal.cs_nbfree--;
			acg.cg_cs.cs_nffree+=sblock.fs_frag;
			sblock.fs_cstotal.cs_nffree+=sblock.fs_frag;
			if (sblock.fs_contigsumsize > 0) {
				clrbit(cg_clustersfree(&acg), d / sblock.fs_frag);
				for (lcs = 0, l = (d / sblock.fs_frag) + 1;
				    lcs < sblock.fs_contigsumsize;
				    l++, lcs++) {
					if (isclr(cg_clustersfree(&acg), l))
						break;
				}
				if (lcs < sblock.fs_contigsumsize) {
					cg_clustersum(&acg)[lcs + 1]--;
					if (lcs)
						cg_clustersum(&acg)[lcs]++;
				}
			}
		}

		for (; d < dupper; d++) {
			if (isclr(cg_blksfree(&acg), d)) {
				bp[ind].old = d / sblock.fs_frag;
				bp[ind].flags |= GFS_FL_LAST;
			} else {
				clrbit(cg_blksfree(&acg), d);
				acg.cg_cs.cs_nffree--;
				sblock.fs_cstotal.cs_nffree--;
			}
		}
		if (bp[ind].flags & GFS_FL_LAST) /* we have to advance here */
			ind++;
		frag_adjust(dupper - 1, 1);
	}

	/*
	 * If we found a block to relocate just do so.
	 */
	if (ind) {
		for (i = 0; i < ind; i++) {
			if (!bp[i].old) { /* no more blocks listed */
				/*
				 * XXX	A relative blocknumber should not be
				 *	zero,   which  is   not   explicitly
				 *	guaranteed by our code.
				 */
				break;
			}
			/*
			 * Allocate a complete block in the same (current)
			 * cylinder group.
			 */
			bp[i].new = alloc() / sblock.fs_frag;

			/*
			 * There is no frag_adjust() needed for the new block
			 * as it will have no fragments yet :-).
			 */
			for (f = bp[i].old * sblock.fs_frag,
			    g = bp[i].new * sblock.fs_frag;
			    f < (bp[i].old + 1) * sblock.fs_frag;
			    f++, g++) {
				if (isset(cg_blksfree(&aocg), f)) {
					setbit(cg_blksfree(&acg), g);
					acg.cg_cs.cs_nffree++;
					sblock.fs_cstotal.cs_nffree++;
				}
			}

			/*
			 * Special handling is required if this was the  first
			 * block. We have to consider the fragments which were
			 * used by the cylinder summary in the original  block
			 * which  re to be free in the copy of our  block.  We
			 * have  to be careful if this first block happens  to
			 * be also the last block to be relocated.
			 */
			if (bp[i].flags & GFS_FL_FIRST) {
				for (f = bp[i].old * sblock.fs_frag,
				    g = bp[i].new * sblock.fs_frag;
				    f < odupper;
				    f++, g++) {
					setbit(cg_blksfree(&acg), g);
					acg.cg_cs.cs_nffree++;
					sblock.fs_cstotal.cs_nffree++;
				}
				if (!(bp[i].flags & GFS_FL_LAST))
					frag_adjust(bp[i].new * sblock.fs_frag, 1);
			}

			/*
			 * Special handling is required if this is the last
			 * block to be relocated.
			 */
			if (bp[i].flags & GFS_FL_LAST) {
				frag_adjust(bp[i].new * sblock.fs_frag, 1);
				frag_adjust(bp[i].old * sblock.fs_frag, -1);
				for (f = dupper;
				    f < roundup(dupper, sblock.fs_frag);
				    f++) {
					if (isclr(cg_blksfree(&acg), f)) {
						setbit(cg_blksfree(&acg), f);
						acg.cg_cs.cs_nffree++;
						sblock.fs_cstotal.cs_nffree++;
					}
				}
				frag_adjust(bp[i].old * sblock.fs_frag, 1);
			}

			/*
			 * !!! Attach the cylindergroup offset here.
			 */
			bp[i].old += cbase / sblock.fs_frag;
			bp[i].new += cbase / sblock.fs_frag;

			/*
			 * Copy the content of the block.
			 */
			/*
			 * XXX	Here we will have to implement a copy on write
			 *	in the case we have any active snapshots.
			 */
			rdfs(fsbtodb(&sblock, bp[i].old * sblock.fs_frag),
			    (size_t)sblock.fs_bsize, (void *)&ablk, fsi);
			wtfs(fsbtodb(&sblock, bp[i].new * sblock.fs_frag),
			    (size_t)sblock.fs_bsize, (void *)&ablk, fso, Nflag);
		}

		/*
		 * Now we have to update all references to any fragment which
		 * belongs  to any block relocated. We iterate now  over  all
		 * cylinder  groups,  within those over all non  zero  length
		 * inodes.
		 */
		for (cg = 0; cg < osblock.fs_ncg; cg++) {
			for (inc = osblock.fs_ipg - 1; inc > 0; inc--) {
				updrefs(cg, (ino_t)inc, bp, fsi, fso, Nflag);
			}
		}

		/*
		 * All inodes are checked, now make sure the number of
		 * references found make sense.
		 */
		for (i = 0; i < ind; i++) {
			if (!bp[i].found || (bp[i].found > sblock.fs_frag)) {
				warnx("error: %jd refs found for block %jd.",
				    (intmax_t)bp[i].found, (intmax_t)bp[i].old);
			}

		}
	}
	/*
	 * The following statistics are not changed here:
	 *     sblock.fs_cstotal.cs_ndir
	 *     sblock.fs_cstotal.cs_nifree
	 * The following statistics were already updated on the fly:
	 *     sblock.fs_cstotal.cs_nffree
	 *     sblock.fs_cstotal.cs_nbfree
	 * As the statistics for this cylinder group are ready, copy it to
	 * the summary information array.
	 */

	*cs = acg.cg_cs;

	/*
	 * Write summary cylinder group back to disk.
	 */
	wtfs(fsbtodb(&sblock, cgtod(&sblock, ocscg)), (size_t)sblock.fs_cgsize,
	    (void *)&acg, fso, Nflag);
}

/*
 * Here we read some block(s) from disk.
 */
static void
rdfs(daddr_t bno, size_t size, void *bf, int fsi)
{
	ssize_t	n;

	if (bno < 0) {
		err(32, "rdfs: attempting to read negative block number");
	}
	if (lseek(fsi, (off_t)bno * DEV_BSIZE, SEEK_SET) == -1) {
		err(33, "rdfs: seek error: %jd", (intmax_t)bno);
	}
	n = read(fsi, bf, size);
	if (n != (ssize_t)size) {
		err(34, "rdfs: read error: %jd", (intmax_t)bno);
	}
}

/*
 * Here we write some block(s) to disk.
 */
static void
wtfs(daddr_t bno, size_t size, void *bf, int fso, unsigned int Nflag)
{
	ssize_t	n;

	if (Nflag)
		return;

	if (lseek(fso, (off_t)bno * DEV_BSIZE, SEEK_SET) == -1)
		err(35, "wtfs: seek error: %ld", (long)bno);
	n = write(fso, bf, size);
	if (n != (ssize_t)size)
		err(36, "wtfs: write error: %ld", (long)bno);
}

/*
 * Here we allocate a free block in the current cylinder group. It is assumed,
 * that  acg contains the current cylinder group. As we may take a block  from
 * somewhere in the filesystem we have to handle cluster summary here.
 */
static daddr_t
alloc(void)
{
	daddr_t	d, blkno;
	int	lcs1, lcs2;
	int	l;
	int	csmin, csmax;
	int	dlower, dupper, dmax;

	if (acg.cg_magic != CG_MAGIC) {
		warnx("acg: bad magic number");
		return (0);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		warnx("error: cylinder group ran out of space");
		return (0);
	}
	/*
	 * We start seeking for free blocks only from the space available after
	 * the  end of the new grown cylinder summary. Otherwise we allocate  a
	 * block here which we have to relocate a couple of seconds later again
	 * again, and we are not prepared to to this anyway.
	 */
	blkno = -1;
	dlower = cgsblock(&sblock, acg.cg_cgx) - cgbase(&sblock, acg.cg_cgx);
	dupper = cgdmin(&sblock, acg.cg_cgx) - cgbase(&sblock, acg.cg_cgx);
	dmax = cgbase(&sblock, acg.cg_cgx) + sblock.fs_fpg;
	if (dmax > sblock.fs_size) {
		dmax = sblock.fs_size;
	}
	dmax -= cgbase(&sblock, acg.cg_cgx); /* retransform into cg */
	csmin=sblock.fs_csaddr-cgbase(&sblock, acg.cg_cgx);
	csmax = csmin + howmany(sblock.fs_cssize, sblock.fs_fsize);

	for (d = 0; (d < dlower && blkno == -1); d += sblock.fs_frag) {
		if (d >= csmin && d <= csmax) {
			continue;
		}
		if (isblock(&sblock, cg_blksfree(&acg), fragstoblks(&sblock,
		    d))) {
			blkno = fragstoblks(&sblock, d);/* Yeah found a block */
			break;
		}
	}
	for (d = dupper; (d < dmax && blkno == -1); d += sblock.fs_frag) {
		if (d >= csmin && d <= csmax) {
			continue;
		}
		if (isblock(&sblock, cg_blksfree(&acg), fragstoblks(&sblock,
		    d))) {
			blkno = fragstoblks(&sblock, d);/* Yeah found a block */
			break;
		}
	}
	if (blkno == -1) {
		warnx("internal error: couldn't find promised block in cg");
		return (0);
	}

	/*
	 * This is needed if the block was found already in the first loop.
	 */
	d = blkstofrags(&sblock, blkno);

	clrblock(&sblock, cg_blksfree(&acg), blkno);
	if (sblock.fs_contigsumsize > 0) {
		/*
		 * Handle the cluster allocation bitmap.
		 */
		clrbit(cg_clustersfree(&acg), blkno);
		/*
		 * We  possibly have split a cluster here, so we have  to  do
		 * recalculate the sizes of the remaining cluster halves now,
		 * and use them for updating the cluster summary information.
		 *
		 * Lets start with the blocks before our allocated block ...
		 */
		for (lcs1 = 0, l = blkno - 1; lcs1 < sblock.fs_contigsumsize;
		    l--, lcs1++) {
			if (isclr(cg_clustersfree(&acg), l))
				break;
		}
		/*
		 * ... and continue with the blocks right after our allocated
		 * block.
		 */
		for (lcs2 = 0, l = blkno + 1; lcs2 < sblock.fs_contigsumsize;
		    l++, lcs2++) {
			if (isclr(cg_clustersfree(&acg), l))
				break;
		}

		/*
		 * Now update all counters.
		 */
		cg_clustersum(&acg)[MINIMUM(lcs1 + lcs2 + 1, sblock.fs_contigsumsize)]--;
		if (lcs1)
			cg_clustersum(&acg)[lcs1]++;
		if (lcs2)
			cg_clustersum(&acg)[lcs2]++;
	}
	/*
	 * Update all statistics based on blocks.
	 */
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;

	return (d);
}

/*
 * Here  we check if all frags of a block are free. For more details  again
 * please see the source of newfs(8), as this function is taken over almost
 * unchanged.
 */
static int
isblock(struct fs *fs, unsigned char *cp, int h)
{
	unsigned char	mask;

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
		fprintf(stderr, "isblock bad fs_frag %d\n", fs->fs_frag);
		return (0);
	}
}

/*
 * Here we allocate a complete block in the block map. For more details again
 * please  see the source of newfs(8), as this function is taken over  almost
 * unchanged.
 */
static void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch ((fs)->fs_frag) {
	case 8:
		cp[h] = 0;
		break;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		break;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		break;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		break;
	default:
		warnx("clrblock bad fs_frag %d", fs->fs_frag);
		break;
	}
}

/*
 * Here we free a complete block in the free block map. For more details again
 * please  see the source of newfs(8), as this function is taken  over  almost
 * unchanged.
 */
static void
setblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		break;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		break;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		break;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		break;
	default:
		warnx("setblock bad fs_frag %d", fs->fs_frag);
		break;
	}
}

/*
 * This function provides access to an individual inode. We find out in which
 * block  the  requested inode is located, read it from disk if  needed,  and
 * return  the pointer into that block. We maintain a cache of one  block  to
 * not  read the same block again and again if we iterate linearly  over  all
 * inodes.
 */
static union dinode *
ginode(ino_t inumber, int fsi, int cg)
{
	static ino_t	startinum = 0;	/* first inode in cached block */

	/*
	 * The inumber passed in is relative to the cg, so use it here to see
	 * if the inode has been allocated yet.
	 */
	if (isclr(cg_inosused(&aocg), inumber)) {
		return NULL;
	}
	/*
	 * Now make the inumber relative to the entire inode space so it can
	 * be sanity checked.
	 */
	inumber += (cg * sblock.fs_ipg);
	if (inumber < ROOTINO) {
		return NULL;
	}
	if (inumber > maxino)
		errx(8, "bad inode number %llu to ginode",
		    (unsigned long long)inumber);
	if (startinum == 0 ||
	    inumber < startinum || inumber >= startinum + INOPB(&sblock)) {
		inoblk = fsbtodb(&sblock, ino_to_fsba(&sblock, inumber));
		rdfs(inoblk, (size_t)sblock.fs_bsize, inobuf, fsi);
		startinum = (inumber / INOPB(&sblock)) * INOPB(&sblock);
	}
	if (sblock.fs_magic == FS_UFS1_MAGIC)
		return (union dinode *)((uintptr_t)inobuf +
		    (inumber % INOPB(&sblock)) * sizeof(struct ufs1_dinode));
	return (union dinode *)((uintptr_t)inobuf +
	    (inumber % INOPB(&sblock)) * sizeof(struct ufs2_dinode));
}

/*
 * Figure out how many lines our current terminal has. For more details again
 * please see the source of newfs(8), as this function is taken over almost
 * unchanged.
 */
static int
charsperline(void)
{
	int	columns;
	char	*cp;
	struct winsize	ws;

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

/*
 * growfs(8) is a utility which allows to increase the size of an existing
 * ufs filesystem. Currently this can only be done on unmounted file system.
 * It recognizes some command line options to specify the new desired size,
 * and it does some basic checkings. The old filesystem size is determined
 * and after some more checks like we can really access the new last block
 * on the disk etc. we calculate the new parameters for the superblock. After
 * having done this we just call growfs() which will do the work. Before
 * we finish the only thing left is to update the disklabel.
 * We still have to provide support for snapshots. Therefore we first have to
 * understand what data structures are always replicated in the snapshot on
 * creation, for all other blocks we touch during our procedure, we have to
 * keep the old blocks unchanged somewhere available for the snapshots. If we
 * are lucky, then we only have to handle our blocks to be relocated in that
 * way.
 * Also we have to consider in what order we actually update the critical
 * data structures of the filesystem to make sure, that in case of a disaster
 * fsck(8) is still able to restore any lost data.
 * The foreseen last step then will be to provide for growing even mounted
 * file systems. There we have to extend the mount() system call to provide
 * userland access to the filesystem locking facility.
 */
int
main(int argc, char **argv)
{
	char	*device, *lastsector;
	int	ch;
	long long	size = 0;
	unsigned int	Nflag = 0;
	int	ExpertFlag = 0;
	struct stat	st;
	struct disklabel	*lp;
	struct partition	*pp;
	int	i, fsi, fso;
	char	reply[5];
	const char *errstr;
#ifdef FSMAXSNAP
	int	j;
#endif /* FSMAXSNAP */

	while ((ch = getopt(argc, argv, "Nqs:vy")) != -1) {
		switch (ch) {
		case 'N':
			Nflag = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			size = strtonum(optarg, 1, LLONG_MAX, &errstr);
			if (errstr)
				usage();
			break;
		case 'v': /* for compatibility to newfs */
			break;
		case 'y':
			ExpertFlag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	colwidth = charsperline();

	/*
	 * Rather than guessing, use opendev() to get the device
	 * name, which we open for reading.
	 */
	if ((fsi = opendev(*argv, O_RDONLY, 0, &device)) == -1)
		err(1, "%s", *argv);

	/*
	 * Try to access our devices for writing ...
	 */
	if (Nflag) {
		fso = -1;
	} else {
		fso = open(device, O_WRONLY);
		if (fso == -1)
			err(1, "%s", device);
	}

	/*
	 * Now we have a file descriptor for our device, fstat() it to
	 * figure out the partition number.
	 */
	if (fstat(fsi, &st) == -1)
		err(1, "%s: fstat()", device);

	/*
	 * Try to read a label from the disk. Then get the partition from the
	 * device minor number, using DISKPART(). Probably don't need to
	 * check against getmaxpartitions().
	 */
	lp = get_disklabel(fsi);
	if (DISKPART(st.st_rdev) < getmaxpartitions())
		pp = &lp->d_partitions[DISKPART(st.st_rdev)];
	else
		errx(1, "%s: invalid partition number %u",
		    device, DISKPART(st.st_rdev));

	if (pledge("stdio disklabel", NULL) == -1)
		err(1, "pledge");

	/*
	 * Check if that partition is suitable for growing a file system.
	 */
	if (DL_GETPSIZE(pp) < 1)
		errx(1, "partition is unavailable");
	if (pp->p_fstype != FS_BSDFFS)
		errx(1, "can only grow ffs partitions");

	/*
	 * Read the current superblock, and take a backup.
	 */
	for (i = 0; sblock_try[i] != -1; i++) {
		sblockloc = sblock_try[i] / DEV_BSIZE;
		rdfs(sblockloc, (size_t)SBLOCKSIZE, (void *)&(osblock), fsi);
		if ((osblock.fs_magic == FS_UFS1_MAGIC ||
		     (osblock.fs_magic == FS_UFS2_MAGIC &&
		      osblock.fs_sblockloc == sblock_try[i])) &&
		    osblock.fs_bsize <= MAXBSIZE &&
		    osblock.fs_bsize >= (int32_t) sizeof(struct fs))
			break;
	}
	if (sblock_try[i] == -1)
		errx(1, "superblock not recognized");
	if (osblock.fs_clean == 0)
		errx(1, "filesystem not clean - run fsck");
	if (sblock.fs_magic == FS_UFS1_MAGIC &&
	    (sblock.fs_ffs1_flags & FS_FLAGS_UPDATED) == 0)
		ffs1_sb_update(&sblock, sblock_try[i]);
	memcpy(&fsun1, &fsun2, sizeof(fsun2));
	maxino = sblock.fs_ncg * sblock.fs_ipg;

	/*
	 * Determine size to grow to. Default to the full size specified in
	 * the disk label.
	 */
	sblock.fs_size = dbtofsb(&osblock, DL_SECTOBLK(lp, DL_GETPSIZE(pp)));
	if (size != 0) {
		if (size > DL_GETPSIZE(pp)) {
			errx(1, "there is not enough space (%llu < %lld)",
			    DL_GETPSIZE(pp), size);
		}
		sblock.fs_size = dbtofsb(&osblock, DL_SECTOBLK(lp, size));
	}

	/*
	 * Are we really growing ?
	 */
	if (osblock.fs_size >= sblock.fs_size) {
		errx(1, "we are not growing (%jd->%jd)",
		    (intmax_t)osblock.fs_size, (intmax_t)sblock.fs_size);
	}


#ifdef FSMAXSNAP
	/*
	 * Check if we find an active snapshot.
	 */
	if (ExpertFlag == 0) {
		for (j = 0; j < FSMAXSNAP; j++) {
			if (sblock.fs_snapinum[j]) {
				errx(1, "active snapshot found in filesystem\n"
				    "	please remove all snapshots before "
				    "using growfs");
			}
			if (!sblock.fs_snapinum[j])	/* list is dense */
				break;
		}
	}
#endif

	if (ExpertFlag == 0 && Nflag == 0) {
		printf("We strongly recommend you to make a backup "
		    "before growing the Filesystem\n\n"
		    " Did you backup your data (Yes/No) ? ");
		if (fgets(reply, (int)sizeof(reply), stdin) == NULL ||
		    strncasecmp(reply, "Yes", 3)) {
			printf("\n Nothing done \n");
			exit (0);
		}
	}

	if (!quiet)
		printf("new filesystem size is: %jd frags\n",
		    (intmax_t)sblock.fs_size);

	/*
	 * Try to access our new last sector in the filesystem. Even if we
	 * later on realize we have to abort our operation, on that sector
	 * there should be no data, so we can't destroy something yet.
	 */
	lastsector = calloc(1, lp->d_secsize);
	if (!lastsector)
		err(1, "No memory for last sector test write");
	wtfs(DL_SECTOBLK(lp, DL_GETPSIZE(pp) - 1), lp->d_secsize,
	    lastsector, fso, Nflag);
	free(lastsector);

	/*
	 * Now calculate new superblock values and check for reasonable
	 * bound for new filesystem size:
	 *     fs_size:    is derived from label or user input
	 *     fs_dsize:   should get updated in the routines creating or
	 *                 updating the cylinder groups on the fly
	 *     fs_cstotal: should get updated in the routines creating or
	 *                 updating the cylinder groups
	 */

	/*
	 * Update the number of cylinders and cylinder groups in the file system.
	 */
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		sblock.fs_ncyl = sblock.fs_size * NSPF(&sblock) / sblock.fs_spc;
		if (sblock.fs_size * NSPF(&sblock) >
		    sblock.fs_ncyl * sblock.fs_spc)
			sblock.fs_ncyl++;
	}
	sblock.fs_ncg = howmany(sblock.fs_size, sblock.fs_fpg);
	if ((ino_t)sblock.fs_ncg * sblock.fs_ipg > UINT_MAX)
		errx(1, "more than 2^32 inodes requested");
	maxino = sblock.fs_ncg * sblock.fs_ipg;

	if (sblock.fs_size % sblock.fs_fpg != 0 &&
	    sblock.fs_size % sblock.fs_fpg < cgdmin(&sblock, sblock.fs_ncg)) {
		/*
		 * The space in the new last cylinder group is too small,
		 * so revert back.
		 */
		sblock.fs_ncg--;
		if (sblock.fs_magic == FS_UFS1_MAGIC)
			sblock.fs_ncyl = sblock.fs_ncg * sblock.fs_cpg;
		if (!quiet)
			printf("Warning: %jd sector(s) cannot be allocated.\n",
			    (intmax_t)fsbtodb(&sblock,
			    sblock.fs_size % sblock.fs_fpg));
		sblock.fs_size = sblock.fs_ncg * sblock.fs_fpg;
	}

	/*
	 * Update the space for the cylinder group summary information in the
	 * respective cylinder group data area.
	 */
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));

	if (osblock.fs_size >= sblock.fs_size)
		errx(1, "not enough new space");

	/*
	 * Ok, everything prepared, so now let's do the tricks.
	 */
	growfs(fsi, fso, Nflag);

	/*
	 * Update the disk label.
	 */
	pp->p_fragblock =
	    DISKLABELV1_FFS_FRAGBLOCK(sblock.fs_fsize, sblock.fs_frag);
	pp->p_cpg = sblock.fs_fpg;

	return_disklabel(fso, lp, Nflag);

	close(fsi);
	if (fso > -1)
		close(fso);

	return 0;
}

/*
 * Write the updated disklabel back to disk.
 */
static void
return_disklabel(int fd, struct disklabel *lp, unsigned int Nflag)
{
	u_short	sum;
	u_short	*ptr;

	if (!lp)
		return;

	if (!Nflag) {
		lp->d_checksum = 0;
		sum = 0;
		ptr = (u_short *)lp;

		/*
		 * recalculate checksum
		 */
		while (ptr < (u_short *)&lp->d_partitions[lp->d_npartitions])
			sum ^= *ptr++;
		lp->d_checksum = sum;

		if (ioctl(fd, DIOCWDINFO, (char *)lp) == -1)
			errx(1, "DIOCWDINFO failed");
	}
	free(lp);

	return ;
}

/*
 * Read the disklabel from disk.
 */
static struct disklabel *
get_disklabel(int fd)
{
	static struct	disklabel *lab;

	lab = malloc(sizeof(struct disklabel));
	if (!lab)
		errx(1, "malloc failed");
	if (ioctl(fd, DIOCGDINFO, (char *)lab) != 0)
		err(1, "DIOCGDINFO");

	return (lab);
}


/*
 * Dump a line of usage.
 */
static void
usage(void)
{
	fprintf(stderr, "usage: growfs [-Nqy] [-s size] special\n");
	exit(1);
}

/*
 * This updates most parameters and the bitmap related to cluster. We have to
 * assume that sblock, osblock, acg are set up.
 */
static void
updclst(int block)
{
	static int	lcs = 0;

	if (sblock.fs_contigsumsize < 1)	/* no clustering */
		return;

	/*
	 * update cluster allocation map
	 */
	setbit(cg_clustersfree(&acg), block);

	/*
	 * update cluster summary table
	 */
	if (!lcs) {
		/*
		 * calculate size for the trailing cluster
		 */
		for (block--; lcs < sblock.fs_contigsumsize; block--, lcs++) {
			if (isclr(cg_clustersfree(&acg), block))
				break;
		}
	}
	if (lcs < sblock.fs_contigsumsize) {
		if (lcs)
			cg_clustersum(&acg)[lcs]--;
		lcs++;
		cg_clustersum(&acg)[lcs]++;
	}
}

/*
 * This updates all references to relocated blocks for the given inode.  The
 * inode is given as number within the cylinder group, and the number of the
 * cylinder group.
 */
static void
updrefs(int cg, ino_t in, struct gfs_bpp *bp, int fsi, int fso, unsigned int
    Nflag)
{
	daddr_t	len, lbn, numblks;
	daddr_t	iptr, blksperindir;
	union dinode	*ino;
	int		i, mode, inodeupdated;

	ino = ginode(in, fsi, cg);
	if (ino == NULL)
		return;

	mode = DIP(ino, di_mode) & IFMT;
	if (mode != IFDIR && mode != IFREG && mode != IFLNK)
		return; /* only check DIR, FILE, LINK */
	if (mode == IFLNK &&
	    DIP(ino, di_size) < (u_int64_t) sblock.fs_maxsymlinklen)
		return;	/* skip short symlinks */
	numblks = howmany(DIP(ino, di_size), sblock.fs_bsize);
	if (numblks == 0)
		return;	/* skip empty file */
	if (DIP(ino, di_blocks) == 0)
		return;	/* skip empty swiss cheesy file or old fastlink */

	/*
	 * Check all the blocks.
	 */
	inodeupdated = 0;
	len = numblks < NDADDR ? numblks : NDADDR;
	for (i = 0; i < len; i++) {
		iptr = DIP(ino, di_db[i]);
		if (iptr == 0)
			continue;
		if (cond_bl_upd(&iptr, bp, fsi, fso, Nflag)) {
			DIP_SET(ino, di_db[i], iptr);
			inodeupdated++;
		}
	}

	blksperindir = 1;
	len = numblks - NDADDR;
	lbn = NDADDR;
	for (i = 0; len > 0 && i < NIADDR; i++) {
		iptr = DIP(ino, di_ib[i]);
		if (iptr == 0)
			continue;
		if (cond_bl_upd(&iptr, bp, fsi, fso, Nflag)) {
			DIP_SET(ino, di_ib[i], iptr);
			inodeupdated++;
		}
		indirchk(blksperindir, lbn, iptr, numblks, bp, fsi, fso, Nflag);
		blksperindir *= NINDIR(&sblock);
		lbn += blksperindir;
		len -= blksperindir;
	}
	if (inodeupdated)
		wtfs(inoblk, sblock.fs_bsize, inobuf, fso, Nflag);
}

/*
 * Recursively check all the indirect blocks.
 */
static void
indirchk(daddr_t blksperindir, daddr_t lbn, daddr_t blkno,
    daddr_t lastlbn, struct gfs_bpp *bp, int fsi, int fso, unsigned int Nflag)
{
	void *ibuf;
	int i, last;
	daddr_t iptr;

	/* read in the indirect block. */
	ibuf = malloc(sblock.fs_bsize);
	if (!ibuf)
		errx(1, "malloc failed");
	rdfs(fsbtodb(&sblock, blkno), (size_t)sblock.fs_bsize, ibuf, fsi);
	last = howmany(lastlbn - lbn, blksperindir) < NINDIR(&sblock) ?
	    howmany(lastlbn - lbn, blksperindir) : NINDIR(&sblock);
	for (i = 0; i < last; i++) {
		if (sblock.fs_magic == FS_UFS1_MAGIC)
			iptr = ((int32_t *)ibuf)[i];
		else
			iptr = ((daddr_t *)ibuf)[i];
		if (iptr == 0)
			continue;
		if (cond_bl_upd(&iptr, bp, fsi, fso, Nflag)) {
			if (sblock.fs_magic == FS_UFS1_MAGIC)
				((int32_t *)ibuf)[i] = iptr;
			else
				((daddr_t *)ibuf)[i] = iptr;
		}
		if (blksperindir == 1)
			continue;
		indirchk(blksperindir / NINDIR(&sblock), lbn + blksperindir * i,
		    iptr, lastlbn, bp, fsi, fso, Nflag);
	}
	free(ibuf);
}

static void
ffs1_sb_update(struct fs *fs, daddr_t sbloc)
{
	fs->fs_flags = fs->fs_ffs1_flags;
	fs->fs_sblockloc = sbloc;
	fs->fs_maxbsize = fs->fs_bsize;
	fs->fs_time = fs->fs_ffs1_time;
	fs->fs_size = fs->fs_ffs1_size;
	fs->fs_dsize = fs->fs_ffs1_dsize;
	fs->fs_csaddr = fs->fs_ffs1_csaddr;
	fs->fs_cstotal.cs_ndir = fs->fs_ffs1_cstotal.cs_ndir;
	fs->fs_cstotal.cs_nbfree = fs->fs_ffs1_cstotal.cs_nbfree;
	fs->fs_cstotal.cs_nifree = fs->fs_ffs1_cstotal.cs_nifree;
	fs->fs_cstotal.cs_nffree = fs->fs_ffs1_cstotal.cs_nffree;
	fs->fs_ffs1_flags |= FS_FLAGS_UPDATED;
}
