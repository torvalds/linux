/*	$OpenBSD: ffs.c,v 1.39 2024/01/09 03:16:00 guenther Exp $	*/
/*	$NetBSD: ffs.c,v 1.66 2015/12/21 00:58:08 christos Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Luke Mewburn for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)ffs_alloc.c	8.19 (Berkeley) 7/13/95
 */

#include <sys/param.h>	/* roundup DEV_BSIZE */
#include <sys/types.h>
#include <sys/disklabel.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include "ffs/ufs_inode.h"
#include "ffs/ffs_extern.h"

#include "makefs.h"
#include "ffs.h"
#include "ffs/newfs_extern.h"

#undef DIP
#define DIP(dp, field) \
	((ffs_opts->version == 1) ? \
	(dp)->ffs1_din.di_##field : (dp)->ffs2_din.di_##field)

/*
 * Various file system defaults (cribbed from newfs(8)).
 */
#define	DFL_FRAGSIZE		2048		/* fragment size */
#define	DFL_BLKSIZE		16384		/* block size */
#define	DFL_SECSIZE		512		/* sector size */


typedef struct {
	u_char		*buf;		/* buf for directory */
	doff_t		size;		/* full size of buf */
	doff_t		cur;		/* offset of current entry */
} dirbuf_t;


static	int	ffs_create_image(const char *, fsinfo_t *);
static	void	ffs_make_dirbuf(dirbuf_t *, const char *, fsnode *);
static	int	ffs_populate_dir(const char *, fsnode *, fsinfo_t *);
static	void	ffs_size_dir(fsnode *, fsinfo_t *);
static	void	ffs_validate(const char *, fsnode *, fsinfo_t *);
static	void	ffs_write_file(union dinode *, uint32_t, void *, fsinfo_t *);
static	void	ffs_write_inode(union dinode *, uint32_t, const fsinfo_t *);
static  void	*ffs_build_dinode1(struct ufs1_dinode *, dirbuf_t *, fsnode *,
				 fsnode *, fsinfo_t *);
static  void	*ffs_build_dinode2(struct ufs2_dinode *, dirbuf_t *, fsnode *,
				 fsnode *, fsinfo_t *);

struct disklabel *ffs_makerdroot(const fsinfo_t *);


	/* publicly visible functions */
void
ffs_prep_opts(fsinfo_t *fsopts)
{
	ffs_opt_t *ffs_opts = ecalloc(1, sizeof(*ffs_opts));

	const option_t ffs_options[] = {
	    { "avgfilesize", &ffs_opts->avgfilesize, OPT_INT32, 1, INT_MAX },
	    { "avgfpdir", &ffs_opts->avgfpdir, OPT_INT32, 1, INT_MAX },
	    { "bsize", &ffs_opts->bsize, OPT_INT32, 1, INT_MAX },
	    { "density", &ffs_opts->density, OPT_INT32, 1, INT_MAX },
	    { "disklabel", NULL, OPT_STRBUF, 0, 0 },
	    { "extent", &ffs_opts->maxbsize, OPT_INT32, 1, INT_MAX },
	    { "fsize", &ffs_opts->fsize, OPT_INT32, 1, INT_MAX },
	    { "label", ffs_opts->label, OPT_STRARRAY, 1, MAXVOLLEN },
	    { "maxbpcg", &ffs_opts->maxblkspercg, OPT_INT32, 1, INT_MAX },
	    { "maxbpg", &ffs_opts->maxbpg, OPT_INT32, 1, INT_MAX },
	    { "minfree", &ffs_opts->minfree, OPT_INT32, 0, 99 },
	    { "optimization", NULL, OPT_STRBUF, 0, 0 },
	    { "rdroot", &ffs_opts->rdroot, OPT_INT32, 0, 1 },
	    { "version", &ffs_opts->version, OPT_INT32, 1, 2 },
	    { .name = NULL }
	};

	ffs_opts->bsize = -1;
	ffs_opts->fsize = -1;
	ffs_opts->density = -1;
	ffs_opts->minfree = MINFREE;
	ffs_opts->optimization = FS_OPTSPACE;
	ffs_opts->maxbpg = -1;
	ffs_opts->avgfilesize = AVFILESIZ;
	ffs_opts->avgfpdir = AFPDIR;
	ffs_opts->version = 1;
	ffs_opts->rdroot = 0;
	ffs_opts->lp = NULL;
	ffs_opts->pp = NULL;

	fsopts->fs_specific = ffs_opts;
	fsopts->fs_options = copy_opts(ffs_options);
}

void
ffs_cleanup_opts(fsinfo_t *fsopts)
{
	free(fsopts->fs_specific);
	free(fsopts->fs_options);
}

int
ffs_parse_opts(const char *option, fsinfo_t *fsopts)
{
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;
	option_t *ffs_options = fsopts->fs_options;
	char buf[1024];

	int	rv;

	assert(option != NULL);
	assert(fsopts != NULL);
	assert(ffs_opts != NULL);

	rv = set_option(ffs_options, option, buf, sizeof(buf));
	if (rv == -1)
		return 0;

	if (ffs_options[rv].name == NULL)
		abort();

	if (strcmp(ffs_options[rv].name, "disklabel") == 0) {
		struct disklabel *dp;

		dp = getdiskbyname(buf);
		if (dp == NULL)
			errx(1, "unknown disk type: %s", buf);

		ffs_opts->lp = emalloc(sizeof(struct disklabel));
		*ffs_opts->lp = *dp;
	} else if (strcmp(ffs_options[rv].name, "optimization") == 0) {
		if (strcmp(buf, "time") == 0) {
			ffs_opts->optimization = FS_OPTTIME;
		} else if (strcmp(buf, "space") == 0) {
			ffs_opts->optimization = FS_OPTSPACE;
		} else {
			warnx("Invalid optimization `%s'", buf);
			return 0;
		}
	}
	return 1;
}

struct disklabel *
ffs_makerdroot(const fsinfo_t *fsopts)
{
	const ffs_opt_t		*ffs_opts = fsopts->fs_specific;
	struct disklabel	*lp;
	struct partition	*pp;
	uint32_t		 rdsize, poffset;
	const uint32_t		 sectorsize = fsopts->sectorsize;
	const uint32_t		 fsize = ffs_opts->fsize;
	const uint32_t		 bsize = ffs_opts->bsize;

	rdsize = (fsopts->size + sectorsize - 1) / sectorsize;
	poffset = (fsopts->offset + sectorsize - 1) / sectorsize;

	lp = ecalloc(1, sizeof(struct disklabel));

	lp->d_version = 1;
	lp->d_type = DTYPE_RDROOT;
	strlcpy(lp->d_typename, "rdroot", sizeof(lp->d_typename));
	lp->d_npartitions = RAW_PART + 1;
	lp->d_secsize = sectorsize;
	lp->d_ntracks = lp->d_ncylinders = 1;
	lp->d_secpercyl = rdsize;
	DL_SETDSIZE(lp, rdsize);

	pp = &lp->d_partitions[0];		/* a.k.a. 'a' */
	pp->p_fstype = FS_BSDFFS;
	pp->p_fragblock = DISKLABELV1_FFS_FRAGBLOCK(fsize, bsize / fsize);
	DL_SETPOFFSET(pp, poffset);
	DL_SETPSIZE(pp, rdsize - poffset);

	pp = &lp->d_partitions[RAW_PART];	/* a.k.a. 'c' */
	DL_SETPOFFSET(pp, 0);
	DL_SETPSIZE(pp, DL_GETDSIZE(lp));

	return lp;
}

void
ffs_makefs(const char *image, const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	struct fs	*superblock;
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	assert(image != NULL);
	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);

		/* validate tree and options */
	ffs_validate(dir, root, fsopts);

	printf("Calculated size of `%s': %lld bytes, %lld inodes\n",
	    image, (long long)fsopts->size, (long long)fsopts->inodes);

		/* create image */
	if (ffs_create_image(image, fsopts) == -1)
		errx(1, "Image file `%s' not created.", image);

	fsopts->curinode = ROOTINO;

		/* populate image */
	printf("Populating `%s'\n", image);
	if (! ffs_populate_dir(dir, root, fsopts))
		errx(1, "Image file `%s' not populated.", image);

	bcleanup();

		/* update various superblock parameters */
	superblock = fsopts->superblock;
	superblock->fs_fmod = 0;
	superblock->fs_ffs1_cstotal.cs_ndir   = superblock->fs_cstotal.cs_ndir;
	superblock->fs_ffs1_cstotal.cs_nbfree = superblock->fs_cstotal.cs_nbfree;
	superblock->fs_ffs1_cstotal.cs_nifree = superblock->fs_cstotal.cs_nifree;
	superblock->fs_ffs1_cstotal.cs_nffree = superblock->fs_cstotal.cs_nffree;

		/* write out superblock; image is now complete */
	ffs_write_superblock(fsopts->superblock, fsopts);

	if (ffs_opts->rdroot == 1) {
		ffs_opts->lp = ffs_makerdroot(fsopts);
		ffs_opts->pp = &ffs_opts->lp->d_partitions[0];
	}

	if (ffs_opts->lp != NULL) {
		struct disklabel *lp = ffs_opts->lp;
		uint16_t *p, *end, sum = 0;
		ssize_t n;
		uint32_t bpg;

		bpg = superblock->fs_fpg / superblock->fs_frag;
		while (bpg > UINT16_MAX)
			bpg >>= 1;
		ffs_opts->pp->p_cpg = bpg;

		lp->d_magic = DISKMAGIC;
		lp->d_magic2 = DISKMAGIC;
		arc4random_buf(lp->d_uid, sizeof(lp->d_uid));
		lp->d_checksum = 0;

		p = (uint16_t *)lp;
		end = (uint16_t *)&lp->d_partitions[lp->d_npartitions];
		while (p < end)
			sum ^= *p++;
		lp->d_checksum = sum;

		n = pwrite(fsopts->fd, lp, sizeof(struct disklabel),
		    fsopts->offset + LABELSECTOR * DEV_BSIZE + LABELOFFSET);
		if (n == -1)
			err(1, "failed to write disklabel");
		else if (n < sizeof(struct disklabel))
			errx(1, "failed to write disklabel: short write");
	}

	if (close(fsopts->fd) == -1)
		err(1, "Closing `%s'", image);
	fsopts->fd = -1;
	printf("Image `%s' complete\n", image);
}

	/* end of public functions */


static void
ffs_validate(const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	ffs_opt_t *ffs_opts = fsopts->fs_specific;
	struct disklabel *lp = ffs_opts->lp;
	struct partition *pp = NULL;
	int32_t	ncg = 1;
	int i;

	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);
	assert(ffs_opts != NULL);

	if (lp != NULL && ffs_opts->rdroot == 1)
		errx(1, "rdroot and disklabel are mutually exclusive");

	if (lp != NULL) {
		for (i = 0; i < lp->d_npartitions; i++) {
			pp = &lp->d_partitions[i];
			if (pp->p_fstype == FS_BSDFFS &&
			    pp->p_offset * lp->d_secsize == fsopts->offset) {
				break;
			}
		}
		if (i == lp->d_npartitions)
			errx(1, "no matching partition found in the disklabel");
		ffs_opts->pp = pp;

		if (pp->p_fragblock == 0)
			errx(1, "fragment size missing in disktab");
		if (fsopts->freeblocks != 0 || fsopts->freeblockpc != 0 ||
		    fsopts->freefiles != 0 || fsopts->freefilepc != 0 ||
		    fsopts->minsize != 0 || fsopts->maxsize != 0 ||
		    fsopts->sectorsize != -1 || fsopts->size != 0)
			errx(1, "-bfMmSs and disklabel are mutually exclusive");
		if (ffs_opts->fsize != -1 || ffs_opts->bsize != -1)
			errx(1, "b/fsize and disklabel are mutually exclusive");

		fsopts->sectorsize = lp->d_secsize;
		fsopts->minsize = fsopts->maxsize =
		    DL_GETPSIZE(pp) * lp->d_secsize;
		ffs_opts->fsize = DISKLABELV1_FFS_FSIZE(pp->p_fragblock);
		ffs_opts->bsize = DISKLABELV1_FFS_BSIZE(pp->p_fragblock);
	} else if (ffs_opts->rdroot == 1) {
		if (fsopts->freeblocks != 0 || fsopts->freeblockpc != 0 ||
		    fsopts->freefiles != 0 || fsopts->freefilepc != 0 ||
		    fsopts->offset != 0 || fsopts->sectorsize != -1 ||
		    fsopts->minsize != fsopts->maxsize)
			errx(1, "rdroot and -bfMmOS are mutually exclusive");
		if (fsopts->minsize == 0 || fsopts->maxsize == 0)
			errx(1, "rdroot requires -s");
		if (ffs_opts->minfree != 0)
			errx(1, "rdroot requires minfree=0");
		if (ffs_opts->fsize == -1 || ffs_opts->bsize == -1 ||
		    ffs_opts->density == -1)
			errx(1, "rdroot requires bsize, fsize and density");
		fsopts->sectorsize = DEV_BSIZE;
	}

		/* set FFS defaults */
	if (fsopts->sectorsize == -1)
		fsopts->sectorsize = DFL_SECSIZE;
	if (ffs_opts->fsize == -1)
		ffs_opts->fsize = MAXIMUM(DFL_FRAGSIZE, fsopts->sectorsize);
	if (ffs_opts->bsize == -1)
		ffs_opts->bsize = MINIMUM(DFL_BLKSIZE, 8 * ffs_opts->fsize);
				/* fsopts->density is set below */
	/* XXX ondisk32 */
	if (ffs_opts->maxbpg == -1)
		ffs_opts->maxbpg = ffs_opts->bsize / sizeof(int32_t);

		/* calculate size of tree */
	ffs_size_dir(root, fsopts);
	fsopts->inodes += ROOTINO;		/* include first two inodes */

		/* add requested slop */
	fsopts->size += fsopts->freeblocks;
	fsopts->inodes += fsopts->freefiles;
	if (fsopts->freefilepc > 0)
		fsopts->inodes =
		    fsopts->inodes * (100 + fsopts->freefilepc) / 100;
	if (fsopts->freeblockpc > 0)
		fsopts->size =
		    fsopts->size * (100 + fsopts->freeblockpc) / 100;

		/* add space needed for superblocks */
	/*
	 * The old SBOFF (SBLOCK_UFS1) is used here because makefs is
	 * typically used for small filesystems where space matters.
	 * XXX make this an option.
	 */
	fsopts->size += (SBLOCK_UFS1 + SBLOCKSIZE) * ncg;
		/* add space needed to store inodes, x3 for blockmaps, etc */
	if (ffs_opts->version == 1)
		fsopts->size += ncg * sizeof(struct ufs1_dinode) *
		    roundup(fsopts->inodes / ncg,
			ffs_opts->bsize / sizeof(struct ufs1_dinode));
	else
		fsopts->size += ncg * sizeof(struct ufs2_dinode) *
		    roundup(fsopts->inodes / ncg,
			ffs_opts->bsize / sizeof(struct ufs2_dinode));

		/* add minfree */
	if (ffs_opts->minfree > 0)
		fsopts->size =
		    fsopts->size * (100 + ffs_opts->minfree) / 100;
	/*
	 * XXX	any other fs slop to add, such as csum's, bitmaps, etc ??
	 */

	if (fsopts->size < fsopts->minsize)	/* ensure meets minimum size */
		fsopts->size = fsopts->minsize;

		/* round up to the next block */
	fsopts->size = roundup(fsopts->size, ffs_opts->bsize);

		/* calculate density if necessary */
	if (ffs_opts->density == -1)
		ffs_opts->density = fsopts->size / fsopts->inodes + 1;

		/* now check calculated sizes vs requested sizes */
	if (fsopts->maxsize > 0 && fsopts->size > fsopts->maxsize) {
		errx(1, "`%s' size of %lld is larger than the maxsize of %lld.",
		    dir, (long long)fsopts->size, (long long)fsopts->maxsize);
	}
}


static int
ffs_create_image(const char *image, fsinfo_t *fsopts)
{
	struct fs	*fs;
	char	*buf;
	int	i, bufsize;
	off_t	bufrem;
	time_t	tstamp;
	int	oflags = O_RDWR | O_CREAT;

	assert (image != NULL);
	assert (fsopts != NULL);

		/* create image */
	if (fsopts->offset == 0)
		oflags |= O_TRUNC;
	if ((fsopts->fd = open(image, oflags, 0666)) == -1) {
		warn("Can't open `%s' for writing", image);
		return (-1);
	}

		/* zero image */
	bufsize = 8192;
	bufrem = fsopts->size;

	if (fsopts->offset != 0)
		if (lseek(fsopts->fd, fsopts->offset, SEEK_SET) == -1) {
			warn("can't seek");
			return -1;
		}

	if (bufrem > 0)
		buf = ecalloc(1, bufsize);
	while (bufrem > 0) {
		i = write(fsopts->fd, buf, MINIMUM(bufsize, bufrem));
		if (i == -1) {
			warn("zeroing image, %lld bytes to go",
			    (long long)bufrem);
			free(buf);
			return (-1);
		}
		bufrem -= i;
	}
	free(buf);

		/* make the file system */
	if (Tflag) {
		tstamp = stampts;
		srandom_deterministic(stampts);
	} else
		tstamp = start_time.tv_sec;

	fs = ffs_mkfs(image, fsopts, tstamp);
	fsopts->superblock = (void *)fs;

	if ((off_t)(fs->fs_cstotal.cs_nifree + ROOTINO) < fsopts->inodes) {
		warnx(
		"Image file `%s' has %lld free inodes; %lld are required.",
		    image,
		    (long long)(fs->fs_cstotal.cs_nifree + ROOTINO),
		    (long long)fsopts->inodes);
		return (-1);
	}
	return (fsopts->fd);
}


static void
ffs_size_dir(fsnode *root, fsinfo_t *fsopts)
{
	struct direct	tmpdir;
	fsnode *	node;
	int		curdirsize, this;
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	/* node may be NULL (empty directory) */
	assert(fsopts != NULL);
	assert(ffs_opts != NULL);

#define	ADDDIRENT(e) do {						\
	tmpdir.d_namlen = strlen((e));					\
	this = DIRSIZ(&tmpdir);						\
	if (this + curdirsize > roundup(curdirsize, DIRBLKSIZ))		\
		curdirsize = roundup(curdirsize, DIRBLKSIZ);		\
	curdirsize += this;						\
} while (0);

	/*
	 * XXX	this needs to take into account extra space consumed
	 *	by indirect blocks, etc.
	 */
#define	ADDSIZE(x) do {							\
	fsopts->size += roundup((x), ffs_opts->fsize);			\
} while (0);

	curdirsize = 0;
	for (node = root; node != NULL; node = node->next) {
		ADDDIRENT(node->name);
		if (node == root) {			/* we're at "." */
			assert(strcmp(node->name, ".") == 0);
			ADDDIRENT("..");
		} else if ((node->inode->flags & FI_SIZED) == 0) {
				/* don't count duplicate names */
			node->inode->flags |= FI_SIZED;
			fsopts->inodes++;
			if (node->type == S_IFREG)
				ADDSIZE(node->inode->st.st_size);
			if (node->type == S_IFLNK) {
				size_t	slen;

				slen = strlen(node->symlink) + 1;
				if (slen >= (ffs_opts->version == 1 ?
						MAXSYMLINKLEN_UFS1 :
						MAXSYMLINKLEN_UFS2))
					ADDSIZE(slen);
			}
		}
		if (node->type == S_IFDIR)
			ffs_size_dir(node->child, fsopts);
	}
	ADDSIZE(curdirsize);
}

static void *
ffs_build_dinode1(struct ufs1_dinode *dinp, dirbuf_t *dbufp, fsnode *cur,
		 fsnode *root, fsinfo_t *fsopts)
{
	size_t slen;
	void *membuf;

	memset(dinp, 0, sizeof(*dinp));
	dinp->di_mode = cur->inode->st.st_mode;
	dinp->di_nlink = cur->inode->nlink;
	dinp->di_size = cur->inode->st.st_size;
	dinp->di_flags = cur->inode->st.st_flags;
	dinp->di_gen = cur->inode->st.st_gen;
	dinp->di_uid = cur->inode->st.st_uid;
	dinp->di_gid = cur->inode->st.st_gid;

	dinp->di_atime = cur->inode->st.st_atime;
	dinp->di_mtime = cur->inode->st.st_mtime;
	dinp->di_ctime = cur->inode->st.st_ctime;
	dinp->di_atimensec = cur->inode->st.st_atim.tv_nsec;
	dinp->di_mtimensec = cur->inode->st.st_mtim.tv_nsec;
	dinp->di_ctimensec = cur->inode->st.st_ctim.tv_nsec;
		/* not set: di_db, di_ib, di_blocks, di_spare */

	membuf = NULL;
	if (cur == root) {			/* "."; write dirbuf */
		membuf = dbufp->buf;
		dinp->di_size = dbufp->size;
	} else if (S_ISBLK(cur->type) || S_ISCHR(cur->type)) {
		dinp->di_size = 0;	/* a device */
		dinp->di_rdev = cur->inode->st.st_rdev;
	} else if (S_ISLNK(cur->type)) {	/* symlink */
		slen = strlen(cur->symlink);
		if (slen < MAXSYMLINKLEN_UFS1) {	/* short link */
			memcpy(dinp->di_db, cur->symlink, slen);
		} else
			membuf = cur->symlink;
		dinp->di_size = slen;
	}
	return membuf;
}

static void *
ffs_build_dinode2(struct ufs2_dinode *dinp, dirbuf_t *dbufp, fsnode *cur,
		 fsnode *root, fsinfo_t *fsopts)
{
	size_t slen;
	void *membuf;

	memset(dinp, 0, sizeof(*dinp));
	dinp->di_mode = cur->inode->st.st_mode;
	dinp->di_nlink = cur->inode->nlink;
	dinp->di_size = cur->inode->st.st_size;
	dinp->di_flags = cur->inode->st.st_flags;
	dinp->di_gen = cur->inode->st.st_gen;
	dinp->di_uid = cur->inode->st.st_uid;
	dinp->di_gid = cur->inode->st.st_gid;

	dinp->di_atime = cur->inode->st.st_atime;
	dinp->di_mtime = cur->inode->st.st_mtime;
	dinp->di_ctime = cur->inode->st.st_ctime;
	dinp->di_atimensec = cur->inode->st.st_atim.tv_nsec;
	dinp->di_mtimensec = cur->inode->st.st_mtim.tv_nsec;
	dinp->di_ctimensec = cur->inode->st.st_ctim.tv_nsec;
		/* not set: di_db, di_ib, di_blocks, di_spare */

	membuf = NULL;
	if (cur == root) {			/* "."; write dirbuf */
		membuf = dbufp->buf;
		dinp->di_size = dbufp->size;
	} else if (S_ISBLK(cur->type) || S_ISCHR(cur->type)) {
		dinp->di_size = 0;	/* a device */
		dinp->di_rdev = cur->inode->st.st_rdev;
	} else if (S_ISLNK(cur->type)) {	/* symlink */
		slen = strlen(cur->symlink);
		if (slen < MAXSYMLINKLEN_UFS2) {	/* short link */
			memcpy(dinp->di_db, cur->symlink, slen);
		} else
			membuf = cur->symlink;
		dinp->di_size = slen;
	}
	return membuf;
}

static int
ffs_populate_dir(const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	fsnode		*cur;
	dirbuf_t	dirbuf;
	union dinode	din;
	void		*membuf;
	char		path[PATH_MAX + 1];
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);
	assert(ffs_opts != NULL);

	(void)memset(&dirbuf, 0, sizeof(dirbuf));

		/*
		 * pass 1: allocate inode numbers, build directory `file'
		 */
	for (cur = root; cur != NULL; cur = cur->next) {
		if ((cur->inode->flags & FI_ALLOCATED) == 0) {
			cur->inode->flags |= FI_ALLOCATED;
			if (cur == root && cur->parent != NULL)
				cur->inode->ino = cur->parent->inode->ino;
			else {
				cur->inode->ino = fsopts->curinode;
				fsopts->curinode++;
			}
		}
		ffs_make_dirbuf(&dirbuf, cur->name, cur);
		if (cur == root) {		/* we're at "."; add ".." */
			ffs_make_dirbuf(&dirbuf, "..",
			    cur->parent == NULL ? cur : cur->parent->first);
			root->inode->nlink++;	/* count my parent's link */
		} else if (cur->child != NULL)
			root->inode->nlink++;	/* count my child's link */

		/*
		 * XXX	possibly write file and long symlinks here,
		 *	ensuring that blocks get written before inodes?
		 *	otoh, this isn't a real filesystem, so who
		 *	cares about ordering? :-)
		 */
	}

		/*
		 * pass 2: write out dirbuf, then non-directories at this level
		 */
	for (cur = root; cur != NULL; cur = cur->next) {
		if (cur->inode->flags & FI_WRITTEN)
			continue;		/* skip hard-linked entries */
		cur->inode->flags |= FI_WRITTEN;

		if ((size_t)snprintf(path, sizeof(path), "%s/%s/%s", cur->root,
		    cur->path, cur->name) >= sizeof(path))
			errx(1, "Pathname too long.");

		if (cur->child != NULL)
			continue;		/* child creates own inode */

				/* build on-disk inode */
		if (ffs_opts->version == 1)
			membuf = ffs_build_dinode1(&din.ffs1_din, &dirbuf, cur,
			    root, fsopts);
		else
			membuf = ffs_build_dinode2(&din.ffs2_din, &dirbuf, cur,
			    root, fsopts);

		if (membuf != NULL) {
			ffs_write_file(&din, cur->inode->ino, membuf, fsopts);
		} else if (S_ISREG(cur->type)) {
			ffs_write_file(&din, cur->inode->ino, path, fsopts);
		} else {
			assert (! S_ISDIR(cur->type));
			ffs_write_inode(&din, cur->inode->ino, fsopts);
		}
	}

		/*
		 * pass 3: write out sub-directories
		 */
	for (cur = root; cur != NULL; cur = cur->next) {
		if (cur->child == NULL)
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir,
		    cur->name) >= sizeof(path))
			errx(1, "Pathname too long.");
		if (! ffs_populate_dir(path, cur->child, fsopts))
			return (0);
	}

		/* cleanup */
	free(dirbuf.buf);
	return (1);
}


static void
ffs_write_file(union dinode *din, uint32_t ino, void *buf, fsinfo_t *fsopts)
{
	int	isfile, ffd;
	char	*fbuf, *p;
	off_t	bufleft, chunk, offset;
	ssize_t nread;
	struct inode	in;
	struct mkfsbuf *	bp;
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;
	struct mkfsvnode vp = { fsopts, NULL };

	assert (din != NULL);
	assert (buf != NULL);
	assert (fsopts != NULL);
	assert (ffs_opts != NULL);

	isfile = S_ISREG(DIP(din, mode));
	fbuf = NULL;
	ffd = -1;
	p = NULL;

	in.i_fs = (struct fs *)fsopts->superblock;
	in.i_devvp = &vp;

	in.i_number = ino;
	in.i_size = DIP(din, size);
	if (ffs_opts->version == 1)
		memcpy(&in.i_din.ffs1_din, &din->ffs1_din,
		    sizeof(in.i_din.ffs1_din));
	else
		memcpy(&in.i_din.ffs2_din, &din->ffs2_din,
		    sizeof(in.i_din.ffs2_din));

	if (DIP(din, size) == 0)
		goto write_inode_and_leave;		/* mmm, cheating */

	if (isfile) {
		fbuf = emalloc(ffs_opts->bsize);
		if ((ffd = open((char *)buf, O_RDONLY)) == -1) {
			warn("Can't open `%s' for reading", (char *)buf);
			goto leave_ffs_write_file;
		}
	} else {
		p = buf;
	}

	chunk = 0;
	for (bufleft = DIP(din, size); bufleft > 0; bufleft -= chunk) {
		chunk = MINIMUM(bufleft, ffs_opts->bsize);
		if (!isfile)
			;
		else if ((nread = read(ffd, fbuf, chunk)) == -1)
			err(1, "Reading `%s', %lld bytes to go", (char *)buf,
			    (long long)bufleft);
		else if (nread != chunk)
			errx(1, "Reading `%s', %lld bytes to go, "
			    "read %zd bytes, expected %ju bytes, does "
			    "metalog size= attribute mismatch source size?",
			    (char *)buf, (long long)bufleft, nread,
			    (uintmax_t)chunk);
		else
			p = fbuf;
		offset = DIP(din, size) - bufleft;
	/*
	 * XXX	if holey support is desired, do the check here
	 *
	 * XXX	might need to write out last bit in fragroundup
	 *	sized chunk. however, ffs_balloc() handles this for us
	 */
		errno = ffs_balloc(&in, offset, chunk, &bp);
 bad_ffs_write_file:
		if (errno != 0)
			err(1,
			    "Writing inode %d (%s), bytes %lld + %lld",
			    ino,
			    isfile ? (char *)buf :
			      inode_type(DIP(din, mode) & S_IFMT),
			    (long long)offset, (long long)chunk);
		memcpy(bp->b_data, p, chunk);
		errno = bwrite(bp);
		if (errno != 0)
			goto bad_ffs_write_file;
		if (!isfile)
			p += chunk;
	}

 write_inode_and_leave:
	ffs_write_inode(&in.i_din, in.i_number, fsopts);

 leave_ffs_write_file:
	free(fbuf);
	if (ffd != -1)
		close(ffd);
}


static void
ffs_make_dirbuf(dirbuf_t *dbuf, const char *name, fsnode *node)
{
	struct direct	de, *dp;
	uint16_t	llen;
	u_char		*newbuf;

	assert (dbuf != NULL);
	assert (name != NULL);
	assert (node != NULL);
					/* create direct entry */
	(void)memset(&de, 0, sizeof(de));
	de.d_ino = node->inode->ino;
	de.d_type = IFTODT(node->type);
	de.d_namlen = (uint8_t)strlen(name);
	strlcpy(de.d_name, name, sizeof de.d_name);
	de.d_reclen = DIRSIZ(&de);

	dp = (struct direct *)(dbuf->buf + dbuf->cur);
	llen = 0;
	if (dp != NULL)
		llen = DIRSIZ(dp);

	if (de.d_reclen + dbuf->cur + llen > roundup(dbuf->size, DIRBLKSIZ)) {
		newbuf = erealloc(dbuf->buf, dbuf->size + DIRBLKSIZ);
		dbuf->buf = newbuf;
		dbuf->size += DIRBLKSIZ;
		memset(dbuf->buf + dbuf->size - DIRBLKSIZ, 0, DIRBLKSIZ);
		dbuf->cur = dbuf->size - DIRBLKSIZ;
	} else if (dp) {			/* shrink end of previous */
		dp->d_reclen = llen;
		dbuf->cur += llen;
	}
	dp = (struct direct *)(dbuf->buf + dbuf->cur);
	memcpy(dp, &de, de.d_reclen);
	dp->d_reclen = dbuf->size - dbuf->cur;
}

/*
 * cribbed from sys/ufs/ffs/ffs_alloc.c
 */
static void
ffs_write_inode(union dinode *dp, uint32_t ino, const fsinfo_t *fsopts)
{
	char		*buf;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2, *dip;
	struct cg	*cgp;
	struct fs	*fs;
	int		cg, cgino, i;
	daddr_t		d;
	char		sbbuf[FFS_MAXBSIZE];
	uint32_t	initediblk;
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	assert (dp != NULL);
	assert (ino > 0);
	assert (fsopts != NULL);
	assert (ffs_opts != NULL);

	fs = (struct fs *)fsopts->superblock;
	cg = ino_to_cg(fs, ino);
	cgino = ino % fs->fs_ipg;

	ffs_rdfs(fsbtodb(fs, cgtod(fs, cg)), (int)fs->fs_cgsize, &sbbuf,
	    fsopts);
	cgp = (struct cg *)sbbuf;
	if (!cg_chkmagic(cgp))
		errx(1, "ffs_write_inode: cg %d: bad magic number", cg);

	assert (isclr(cg_inosused(cgp), cgino));

	buf = emalloc(fs->fs_bsize);
	dp1 = (struct ufs1_dinode *)buf;
	dp2 = (struct ufs2_dinode *)buf;

	if (fs->fs_cstotal.cs_nifree == 0)
		errx(1, "ffs_write_inode: fs out of inodes for ino %u",
		    ino);
	if (fs->fs_cs(fs, cg).cs_nifree == 0)
		errx(1,
		    "ffs_write_inode: cg %d out of inodes for ino %u",
		    cg, ino);
	setbit(cg_inosused(cgp), cgino);
	cgp->cg_cs.cs_nifree -= 1;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	if (S_ISDIR(DIP(dp, mode))) {
		cgp->cg_cs.cs_ndir += 1;
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++;
	}

	/*
	 * Initialize inode blocks on the fly for UFS2.
	 */
	initediblk = cgp->cg_initediblk;
	if (ffs_opts->version == 2 &&
	    (uint32_t)(cgino + INOPB(fs)) > initediblk &&
	    initediblk < cgp->cg_ffs2_niblk) {
		memset(buf, 0, fs->fs_bsize);
		dip = (struct ufs2_dinode *)buf;
		for (i = 0; i < INOPB(fs); i++) {
			dip->di_gen = random() / 2 + 1;
			dip++;
		}
		ffs_wtfs(fsbtodb(fs, ino_to_fsba(fs,
				  cg * fs->fs_ipg + initediblk)),
		    fs->fs_bsize, buf, fsopts);
		initediblk += INOPB(fs);
		cgp->cg_initediblk = initediblk;
	}


	ffs_wtfs(fsbtodb(fs, cgtod(fs, cg)), (int)fs->fs_cgsize, &sbbuf,
	    fsopts);

					/* now write inode */
	d = fsbtodb(fs, ino_to_fsba(fs, ino));
	ffs_rdfs(d, fs->fs_bsize, buf, fsopts);
	if (ffs_opts->version == 1)
		dp1[ino_to_fsbo(fs, ino)] = dp->ffs1_din;
	else
		dp2[ino_to_fsbo(fs, ino)] = dp->ffs2_din;
	ffs_wtfs(d, fs->fs_bsize, buf, fsopts);
	free(buf);
}
