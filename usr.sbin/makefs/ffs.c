/*	$NetBSD: ffs.c,v 1.45 2011/10/09 22:49:26 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/param.h>

#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include "makefs.h"
#include "ffs.h"

#if HAVE_STRUCT_STATVFS_F_IOSIZE && HAVE_FSTATVFS
#include <sys/statvfs.h>
#endif

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>


#include "ffs/ufs_bswap.h"
#include "ffs/ufs_inode.h"
#include "ffs/newfs_extern.h"
#include "ffs/ffs_extern.h"

#undef DIP
#define DIP(dp, field) \
	((ffs_opts->version == 1) ? \
	(dp)->ffs1_din.di_##field : (dp)->ffs2_din.di_##field)

/*
 * Various file system defaults (cribbed from newfs(8)).
 */
#define	DFL_FRAGSIZE		4096		/* fragment size */
#define	DFL_BLKSIZE		32768		/* block size */
#define	DFL_SECSIZE		512		/* sector size */
#define	DFL_CYLSPERGROUP	65536		/* cylinders per group */
#define	DFL_FRAGSPERINODE	4		/* fragments per inode */
#define	DFL_ROTDELAY		0		/* rotational delay */
#define	DFL_NRPOS		1		/* rotational positions */
#define	DFL_RPM			3600		/* rpm of disk */
#define	DFL_NSECTORS		64		/* # of sectors */
#define	DFL_NTRACKS		16		/* # of tracks */


typedef struct {
	u_char		*buf;		/* buf for directory */
	doff_t		size;		/* full size of buf */
	doff_t		cur;		/* offset of current entry */
} dirbuf_t;


static	int	ffs_create_image(const char *, fsinfo_t *);
static	void	ffs_dump_fsinfo(fsinfo_t *);
static	void	ffs_dump_dirbuf(dirbuf_t *, const char *, int);
static	void	ffs_make_dirbuf(dirbuf_t *, const char *, fsnode *, int);
static	int	ffs_populate_dir(const char *, fsnode *, fsinfo_t *);
static	void	ffs_size_dir(fsnode *, fsinfo_t *);
static	void	ffs_validate(const char *, fsnode *, fsinfo_t *);
static	void	ffs_write_file(union dinode *, uint32_t, void *, fsinfo_t *);
static	void	ffs_write_inode(union dinode *, uint32_t, const fsinfo_t *);
static  void	*ffs_build_dinode1(struct ufs1_dinode *, dirbuf_t *, fsnode *,
				 fsnode *, fsinfo_t *);
static  void	*ffs_build_dinode2(struct ufs2_dinode *, dirbuf_t *, fsnode *,
				 fsnode *, fsinfo_t *);


	/* publicly visible functions */

void
ffs_prep_opts(fsinfo_t *fsopts)
{
	ffs_opt_t *ffs_opts = ecalloc(1, sizeof(*ffs_opts));

	const option_t ffs_options[] = {
	    { 'b', "bsize", &ffs_opts->bsize, OPT_INT32,
	      1, INT_MAX, "block size" },
	    { 'f', "fsize", &ffs_opts->fsize, OPT_INT32,
	      1, INT_MAX, "fragment size" },
	    { 'd', "density", &ffs_opts->density, OPT_INT32,
	      1, INT_MAX, "bytes per inode" },
	    { 'm', "minfree", &ffs_opts->minfree, OPT_INT32,
	      0, 99, "minfree" },
	    { 'M', "maxbpg", &ffs_opts->maxbpg, OPT_INT32,
	      1, INT_MAX, "max blocks per file in a cg" },
	    { 'a', "avgfilesize", &ffs_opts->avgfilesize, OPT_INT32,
	      1, INT_MAX, "expected average file size" },
	    { 'n', "avgfpdir", &ffs_opts->avgfpdir, OPT_INT32,
	      1, INT_MAX, "expected # of files per directory" },
	    { 'x', "extent", &ffs_opts->maxbsize, OPT_INT32,
	      1, INT_MAX, "maximum # extent size" },
	    { 'g', "maxbpcg", &ffs_opts->maxblkspercg, OPT_INT32,
	      1, INT_MAX, "max # of blocks per group" },
	    { 'v', "version", &ffs_opts->version, OPT_INT32,
	      1, 2, "UFS version" },
	    { 'o', "optimization", NULL, OPT_STRBUF,
	      0, 0, "Optimization (time|space)" },
	    { 'l', "label", ffs_opts->label, OPT_STRARRAY,
	      1, sizeof(ffs_opts->label), "UFS label" },
	    { 's', "softupdates", &ffs_opts->softupdates, OPT_INT32,
	      0, 1, "enable softupdates" },
	    { .name = NULL }
	};

	ffs_opts->bsize= -1;
	ffs_opts->fsize= -1;
	ffs_opts->cpg= -1;
	ffs_opts->density= -1;
	ffs_opts->minfree= -1;
	ffs_opts->optimization= -1;
	ffs_opts->maxcontig= -1;
	ffs_opts->maxbpg= -1;
	ffs_opts->avgfilesize= -1;
	ffs_opts->avgfpdir= -1;
	ffs_opts->version = 1;
	ffs_opts->softupdates = 0;

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

	if (debug & DEBUG_FS_PARSE_OPTS)
		printf("ffs_parse_opts: got `%s'\n", option);

	rv = set_option(ffs_options, option, buf, sizeof(buf));
	if (rv == -1)
		return 0;

	if (ffs_options[rv].name == NULL)
		abort();

	switch (ffs_options[rv].letter) {
	case 'o':
		if (strcmp(buf, "time") == 0) {
			ffs_opts->optimization = FS_OPTTIME;
		} else if (strcmp(buf, "space") == 0) {
			ffs_opts->optimization = FS_OPTSPACE;
		} else {
			warnx("Invalid optimization `%s'", buf);
			return 0;
		}
		break;
	default:
		break;
	}
	return 1;
}


void
ffs_makefs(const char *image, const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	struct fs	*superblock;
	struct timeval	start;

	assert(image != NULL);
	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);

	if (debug & DEBUG_FS_MAKEFS)
		printf("ffs_makefs: image %s directory %s root %p\n",
		    image, dir, root);

		/* validate tree and options */
	TIMER_START(start);
	ffs_validate(dir, root, fsopts);
	TIMER_RESULTS(start, "ffs_validate");

	printf("Calculated size of `%s': %lld bytes, %lld inodes\n",
	    image, (long long)fsopts->size, (long long)fsopts->inodes);

		/* create image */
	TIMER_START(start);
	if (ffs_create_image(image, fsopts) == -1)
		errx(1, "Image file `%s' not created.", image);
	TIMER_RESULTS(start, "ffs_create_image");

	fsopts->curinode = UFS_ROOTINO;

	if (debug & DEBUG_FS_MAKEFS)
		putchar('\n');

		/* populate image */
	printf("Populating `%s'\n", image);
	TIMER_START(start);
	if (! ffs_populate_dir(dir, root, fsopts))
		errx(1, "Image file `%s' not populated.", image);
	TIMER_RESULTS(start, "ffs_populate_dir");

		/* ensure no outstanding buffers remain */
	if (debug & DEBUG_FS_MAKEFS)
		bcleanup();

		/* update various superblock parameters */
	superblock = fsopts->superblock;
	superblock->fs_fmod = 0;
	superblock->fs_old_cstotal.cs_ndir   = superblock->fs_cstotal.cs_ndir;
	superblock->fs_old_cstotal.cs_nbfree = superblock->fs_cstotal.cs_nbfree;
	superblock->fs_old_cstotal.cs_nifree = superblock->fs_cstotal.cs_nifree;
	superblock->fs_old_cstotal.cs_nffree = superblock->fs_cstotal.cs_nffree;

		/* write out superblock; image is now complete */
	ffs_write_superblock(fsopts->superblock, fsopts);
	if (close(fsopts->fd) == -1)
		err(1, "Closing `%s'", image);
	fsopts->fd = -1;
	printf("Image `%s' complete\n", image);
}

	/* end of public functions */


static void
ffs_validate(const char *dir, fsnode *root, fsinfo_t *fsopts)
{
	int32_t	ncg = 1;
#ifdef notyet
	int32_t	spc, nspf, ncyl, fssize;
#endif
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);
	assert(ffs_opts != NULL);

	if (debug & DEBUG_FS_VALIDATE) {
		printf("ffs_validate: before defaults set:\n");
		ffs_dump_fsinfo(fsopts);
	}

		/* set FFS defaults */
	if (fsopts->sectorsize == -1)
		fsopts->sectorsize = DFL_SECSIZE;
	if (ffs_opts->fsize == -1)
		ffs_opts->fsize = MAX(DFL_FRAGSIZE, fsopts->sectorsize);
	if (ffs_opts->bsize == -1)
		ffs_opts->bsize = MIN(DFL_BLKSIZE, 8 * ffs_opts->fsize);
	if (ffs_opts->cpg == -1)
		ffs_opts->cpg = DFL_CYLSPERGROUP;
	else
		ffs_opts->cpgflg = 1;
				/* fsopts->density is set below */
	if (ffs_opts->nsectors == -1)
		ffs_opts->nsectors = DFL_NSECTORS;
	if (ffs_opts->minfree == -1)
		ffs_opts->minfree = MINFREE;
	if (ffs_opts->optimization == -1)
		ffs_opts->optimization = DEFAULTOPT;
	if (ffs_opts->maxcontig == -1)
		ffs_opts->maxcontig =
		    MAX(1, MIN(MAXPHYS, FFS_MAXBSIZE) / ffs_opts->bsize);
	/* XXX ondisk32 */
	if (ffs_opts->maxbpg == -1)
		ffs_opts->maxbpg = ffs_opts->bsize / sizeof(int32_t);
	if (ffs_opts->avgfilesize == -1)
		ffs_opts->avgfilesize = AVFILESIZ;
	if (ffs_opts->avgfpdir == -1)
		ffs_opts->avgfpdir = AFPDIR;

	if (fsopts->maxsize > 0 &&
	    roundup(fsopts->minsize, ffs_opts->bsize) > fsopts->maxsize)
		errx(1, "`%s' minsize of %lld rounded up to ffs bsize of %d "
		    "exceeds maxsize %lld.  Lower bsize, or round the minimum "
		    "and maximum sizes to bsize.", dir,
		    (long long)fsopts->minsize, ffs_opts->bsize,
		    (long long)fsopts->maxsize);

		/* calculate size of tree */
	ffs_size_dir(root, fsopts);
	fsopts->inodes += UFS_ROOTINO;		/* include first two inodes */

	if (debug & DEBUG_FS_VALIDATE)
		printf("ffs_validate: size of tree: %lld bytes, %lld inodes\n",
		    (long long)fsopts->size, (long long)fsopts->inodes);

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
		fsopts->size += ncg * DINODE1_SIZE *
		    roundup(fsopts->inodes / ncg, 
			ffs_opts->bsize / DINODE1_SIZE);
	else
		fsopts->size += ncg * DINODE2_SIZE *
		    roundup(fsopts->inodes / ncg, 
			ffs_opts->bsize / DINODE2_SIZE);

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

		/* round up to requested block size, if any */
	if (fsopts->roundup > 0)
		fsopts->size = roundup(fsopts->size, fsopts->roundup);

		/* calculate density if necessary */
	if (ffs_opts->density == -1)
		ffs_opts->density = fsopts->size / fsopts->inodes + 1;

	if (debug & DEBUG_FS_VALIDATE) {
		printf("ffs_validate: after defaults set:\n");
		ffs_dump_fsinfo(fsopts);
		printf("ffs_validate: dir %s; %lld bytes, %lld inodes\n",
		    dir, (long long)fsopts->size, (long long)fsopts->inodes);
	}
		/* now check calculated sizes vs requested sizes */
	if (fsopts->maxsize > 0 && fsopts->size > fsopts->maxsize) {
		errx(1, "`%s' size of %lld is larger than the maxsize of %lld.",
		    dir, (long long)fsopts->size, (long long)fsopts->maxsize);
	}
}


static void
ffs_dump_fsinfo(fsinfo_t *f)
{

	ffs_opt_t	*fs = f->fs_specific;

	printf("fsopts at %p\n", f);

	printf("\tsize %lld, inodes %lld, curinode %u\n",
	    (long long)f->size, (long long)f->inodes, f->curinode);

	printf("\tminsize %lld, maxsize %lld\n",
	    (long long)f->minsize, (long long)f->maxsize);
	printf("\tfree files %lld, freefile %% %d\n",
	    (long long)f->freefiles, f->freefilepc);
	printf("\tfree blocks %lld, freeblock %% %d\n",
	    (long long)f->freeblocks, f->freeblockpc);
	printf("\tneedswap %d, sectorsize %d\n", f->needswap, f->sectorsize);

	printf("\tbsize %d, fsize %d, cpg %d, density %d\n",
	    fs->bsize, fs->fsize, fs->cpg, fs->density);
	printf("\tnsectors %d, rpm %d, minfree %d\n",
	    fs->nsectors, fs->rpm, fs->minfree);
	printf("\tmaxcontig %d, maxbpg %d\n",
	    fs->maxcontig, fs->maxbpg);
	printf("\toptimization %s\n",
	    fs->optimization == FS_OPTSPACE ? "space" : "time");
}


static int
ffs_create_image(const char *image, fsinfo_t *fsopts)
{
#if HAVE_STRUCT_STATVFS_F_IOSIZE && HAVE_FSTATVFS
	struct statvfs	sfs;
#endif
	struct fs	*fs;
	char	*buf;
	int	i, bufsize;
	off_t	bufrem;
	int	oflags = O_RDWR | O_CREAT;
	time_t	tstamp;

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
#if HAVE_STRUCT_STATVFS_F_IOSIZE && HAVE_FSTATVFS
	if (fstatvfs(fsopts->fd, &sfs) == -1) {
#endif
		bufsize = 8192;
#if HAVE_STRUCT_STATVFS_F_IOSIZE && HAVE_FSTATVFS
		warn("can't fstatvfs `%s', using default %d byte chunk",
		    image, bufsize);
	} else
		bufsize = sfs.f_iosize;
#endif
	bufrem = fsopts->size;
	if (fsopts->sparse) {
		if (ftruncate(fsopts->fd, bufrem) == -1) {
			warn("sparse option disabled.");
			fsopts->sparse = 0;
		}
	}
	if (fsopts->sparse) {
		/* File truncated at bufrem. Remaining is 0 */
		bufrem = 0;
		buf = NULL;
	} else {
		if (debug & DEBUG_FS_CREATE_IMAGE)
			printf("zero-ing image `%s', %lld sectors, "
			    "using %d byte chunks\n", image, (long long)bufrem,
			    bufsize);
		buf = ecalloc(1, bufsize);
	}

	if (fsopts->offset != 0)
		if (lseek(fsopts->fd, fsopts->offset, SEEK_SET) == -1) {
			warn("can't seek");
			free(buf);
			return -1;
		}

	while (bufrem > 0) {
		i = write(fsopts->fd, buf, MIN(bufsize, bufrem));
		if (i == -1) {
			warn("zeroing image, %lld bytes to go",
			    (long long)bufrem);
			free(buf);
			return (-1);
		}
		bufrem -= i;
	}
	if (buf)
		free(buf);

		/* make the file system */
	if (debug & DEBUG_FS_CREATE_IMAGE)
		printf("calling mkfs(\"%s\", ...)\n", image);

	if (stampst.st_ino != 0)
		tstamp = stampst.st_ctime;
	else
		tstamp = start_time.tv_sec;

	srandom(tstamp);

	fs = ffs_mkfs(image, fsopts, tstamp);
	fsopts->superblock = (void *)fs;
	if (debug & DEBUG_FS_CREATE_IMAGE) {
		time_t t;

		t = (time_t)((struct fs *)fsopts->superblock)->fs_time;
		printf("mkfs returned %p; fs_time %s",
		    fsopts->superblock, ctime(&t));
		printf("fs totals: nbfree %lld, nffree %lld, nifree %lld, ndir %lld\n",
		    (long long)fs->fs_cstotal.cs_nbfree,
		    (long long)fs->fs_cstotal.cs_nffree,
		    (long long)fs->fs_cstotal.cs_nifree,
		    (long long)fs->fs_cstotal.cs_ndir);
	}

	if (fs->fs_cstotal.cs_nifree + UFS_ROOTINO < fsopts->inodes) {
		warnx(
		"Image file `%s' has %lld free inodes; %lld are required.",
		    image,
		    (long long)(fs->fs_cstotal.cs_nifree + UFS_ROOTINO),
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

	if (debug & DEBUG_FS_SIZE_DIR)
		printf("ffs_size_dir: entry: bytes %lld inodes %lld\n",
		    (long long)fsopts->size, (long long)fsopts->inodes);

#define	ADDDIRENT(e) do {						\
	tmpdir.d_namlen = strlen((e));					\
	this = DIRSIZ_SWAP(0, &tmpdir, 0);					\
	if (debug & DEBUG_FS_SIZE_DIR_ADD_DIRENT)			\
		printf("ADDDIRENT: was: %s (%d) this %d cur %d\n",	\
		    e, tmpdir.d_namlen, this, curdirsize);		\
	if (this + curdirsize > roundup(curdirsize, DIRBLKSIZ))		\
		curdirsize = roundup(curdirsize, DIRBLKSIZ);		\
	curdirsize += this;						\
	if (debug & DEBUG_FS_SIZE_DIR_ADD_DIRENT)			\
		printf("ADDDIRENT: now: %s (%d) this %d cur %d\n",	\
		    e, tmpdir.d_namlen, this, curdirsize);		\
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
			if (debug & DEBUG_FS_SIZE_DIR_NODE)
				printf("ffs_size_dir: `%s' size %lld\n",
				    node->name,
				    (long long)node->inode->st.st_size);
			fsopts->inodes++;
			if (node->type == S_IFREG)
				ADDSIZE(node->inode->st.st_size);
			if (node->type == S_IFLNK) {
				size_t slen;

				slen = strlen(node->symlink) + 1;
				if (slen >= (ffs_opts->version == 1 ?
						UFS1_MAXSYMLINKLEN :
						UFS2_MAXSYMLINKLEN))
					ADDSIZE(slen);
			}
		}
		if (node->type == S_IFDIR)
			ffs_size_dir(node->child, fsopts);
	}
	ADDSIZE(curdirsize);

	if (debug & DEBUG_FS_SIZE_DIR)
		printf("ffs_size_dir: exit: size %lld inodes %lld\n",
		    (long long)fsopts->size, (long long)fsopts->inodes);
}

static void *
ffs_build_dinode1(struct ufs1_dinode *dinp, dirbuf_t *dbufp, fsnode *cur,
		 fsnode *root, fsinfo_t *fsopts)
{
	size_t slen;
	void *membuf;
	struct stat *st = stampst.st_ino != 0 ? &stampst : &cur->inode->st;

	memset(dinp, 0, sizeof(*dinp));
	dinp->di_mode = cur->inode->st.st_mode;
	dinp->di_nlink = cur->inode->nlink;
	dinp->di_size = cur->inode->st.st_size;
#if HAVE_STRUCT_STAT_ST_FLAGS
	dinp->di_flags = cur->inode->st.st_flags;
#endif
	dinp->di_gen = random();
	dinp->di_uid = cur->inode->st.st_uid;
	dinp->di_gid = cur->inode->st.st_gid;

	dinp->di_atime = st->st_atime;
	dinp->di_mtime = st->st_mtime;
	dinp->di_ctime = st->st_ctime;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
	dinp->di_atimensec = st->st_atimensec;
	dinp->di_mtimensec = st->st_mtimensec;
	dinp->di_ctimensec = st->st_ctimensec;
#endif
		/* not set: di_db, di_ib, di_blocks, di_spare */

	membuf = NULL;
	if (cur == root) {			/* "."; write dirbuf */
		membuf = dbufp->buf;
		dinp->di_size = dbufp->size;
	} else if (S_ISBLK(cur->type) || S_ISCHR(cur->type)) {
		dinp->di_size = 0;	/* a device */
		dinp->di_rdev =
		    ufs_rw32(cur->inode->st.st_rdev, fsopts->needswap);
	} else if (S_ISLNK(cur->type)) {	/* symlink */
		slen = strlen(cur->symlink);
		if (slen < UFS1_MAXSYMLINKLEN) {	/* short link */
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
	struct stat *st = stampst.st_ino != 0 ? &stampst : &cur->inode->st;

	memset(dinp, 0, sizeof(*dinp));
	dinp->di_mode = cur->inode->st.st_mode;
	dinp->di_nlink = cur->inode->nlink;
	dinp->di_size = cur->inode->st.st_size;
#if HAVE_STRUCT_STAT_ST_FLAGS
	dinp->di_flags = cur->inode->st.st_flags;
#endif
	dinp->di_gen = random();
	dinp->di_uid = cur->inode->st.st_uid;
	dinp->di_gid = cur->inode->st.st_gid;

	dinp->di_atime = st->st_atime;
	dinp->di_mtime = st->st_mtime;
	dinp->di_ctime = st->st_ctime;
#if HAVE_STRUCT_STAT_ST_MTIMENSEC
	dinp->di_atimensec = st->st_atimensec;
	dinp->di_mtimensec = st->st_mtimensec;
	dinp->di_ctimensec = st->st_ctimensec;
#endif
#if HAVE_STRUCT_STAT_BIRTHTIME
	dinp->di_birthtime = st->st_birthtime;
	dinp->di_birthnsec = st->st_birthtimensec;
#endif
		/* not set: di_db, di_ib, di_blocks, di_spare */

	membuf = NULL;
	if (cur == root) {			/* "."; write dirbuf */
		membuf = dbufp->buf;
		dinp->di_size = dbufp->size;
	} else if (S_ISBLK(cur->type) || S_ISCHR(cur->type)) {
		dinp->di_size = 0;	/* a device */
		dinp->di_rdev =
		    ufs_rw64(cur->inode->st.st_rdev, fsopts->needswap);
	} else if (S_ISLNK(cur->type)) {	/* symlink */
		slen = strlen(cur->symlink);
		if (slen < UFS2_MAXSYMLINKLEN) {	/* short link */
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
	char		path[MAXPATHLEN + 1];
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;

	assert(dir != NULL);
	assert(root != NULL);
	assert(fsopts != NULL);
	assert(ffs_opts != NULL);

	(void)memset(&dirbuf, 0, sizeof(dirbuf));

	if (debug & DEBUG_FS_POPULATE)
		printf("ffs_populate_dir: PASS 1  dir %s node %p\n", dir, root);

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
		ffs_make_dirbuf(&dirbuf, cur->name, cur, fsopts->needswap);
		if (cur == root) {		/* we're at "."; add ".." */
			ffs_make_dirbuf(&dirbuf, "..",
			    cur->parent == NULL ? cur : cur->parent->first,
			    fsopts->needswap);
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
	if (debug & DEBUG_FS_POPULATE_DIRBUF)
		ffs_dump_dirbuf(&dirbuf, dir, fsopts->needswap);

		/*
		 * pass 2: write out dirbuf, then non-directories at this level
		 */
	if (debug & DEBUG_FS_POPULATE)
		printf("ffs_populate_dir: PASS 2  dir %s\n", dir);
	for (cur = root; cur != NULL; cur = cur->next) {
		if (cur->inode->flags & FI_WRITTEN)
			continue;		/* skip hard-linked entries */
		cur->inode->flags |= FI_WRITTEN;

		if (cur->contents == NULL) {
			if (snprintf(path, sizeof(path), "%s/%s/%s", cur->root,
			    cur->path, cur->name) >= (int)sizeof(path))
				errx(1, "Pathname too long.");
		}

		if (cur->child != NULL)
			continue;		/* child creates own inode */

				/* build on-disk inode */
		if (ffs_opts->version == 1)
			membuf = ffs_build_dinode1(&din.ffs1_din, &dirbuf, cur,
			    root, fsopts);
		else
			membuf = ffs_build_dinode2(&din.ffs2_din, &dirbuf, cur,
			    root, fsopts);

		if (debug & DEBUG_FS_POPULATE_NODE) {
			printf("ffs_populate_dir: writing ino %d, %s",
			    cur->inode->ino, inode_type(cur->type));
			if (cur->inode->nlink > 1)
				printf(", nlink %d", cur->inode->nlink);
			putchar('\n');
		}

		if (membuf != NULL) {
			ffs_write_file(&din, cur->inode->ino, membuf, fsopts);
		} else if (S_ISREG(cur->type)) {
			ffs_write_file(&din, cur->inode->ino,
			    (cur->contents) ?  cur->contents : path, fsopts);
		} else {
			assert (! S_ISDIR(cur->type));
			ffs_write_inode(&din, cur->inode->ino, fsopts);
		}
	}

		/*
		 * pass 3: write out sub-directories
		 */
	if (debug & DEBUG_FS_POPULATE)
		printf("ffs_populate_dir: PASS 3  dir %s\n", dir);
	for (cur = root; cur != NULL; cur = cur->next) {
		if (cur->child == NULL)
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir,
		    cur->name) >= sizeof(path))
			errx(1, "Pathname too long.");
		if (! ffs_populate_dir(path, cur->child, fsopts))
			return (0);
	}

	if (debug & DEBUG_FS_POPULATE)
		printf("ffs_populate_dir: DONE dir %s\n", dir);

		/* cleanup */
	if (dirbuf.buf != NULL)
		free(dirbuf.buf);
	return (1);
}


static void
ffs_write_file(union dinode *din, uint32_t ino, void *buf, fsinfo_t *fsopts)
{
	int 	isfile, ffd;
	char	*fbuf, *p;
	off_t	bufleft, chunk, offset;
	ssize_t nread;
	struct inode	in;
	struct buf *	bp;
	ffs_opt_t	*ffs_opts = fsopts->fs_specific;
	struct vnode vp = { fsopts, NULL };

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

	if (debug & DEBUG_FS_WRITE_FILE) {
		printf(
		    "ffs_write_file: ino %u, din %p, isfile %d, %s, size %lld",
		    ino, din, isfile, inode_type(DIP(din, mode) & S_IFMT),
		    (long long)DIP(din, size));
		if (isfile)
			printf(", file '%s'\n", (char *)buf);
		else
			printf(", buffer %p\n", buf);
	}

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
		if ((ffd = open((char *)buf, O_RDONLY, 0444)) == -1) {
			err(EXIT_FAILURE, "Can't open `%s' for reading", (char *)buf);
		}
	} else {
		p = buf;
	}

	chunk = 0;
	for (bufleft = DIP(din, size); bufleft > 0; bufleft -= chunk) {
		chunk = MIN(bufleft, ffs_opts->bsize);
		if (!isfile)
			;
		else if ((nread = read(ffd, fbuf, chunk)) == -1)
			err(EXIT_FAILURE, "Reading `%s', %lld bytes to go",
			    (char *)buf, (long long)bufleft);
		else if (nread != chunk)
			errx(EXIT_FAILURE, "Reading `%s', %lld bytes to go, "
			    "read %zd bytes, expected %ju bytes, does "
			    "metalog size= attribute mismatch source size?",
			    (char *)buf, (long long)bufleft, nread,
			    (uintmax_t)chunk);
		else
			p = fbuf;
		offset = DIP(din, size) - bufleft;
		if (debug & DEBUG_FS_WRITE_FILE_BLOCK)
			printf(
		"ffs_write_file: write %p offset %lld size %lld left %lld\n",
			    p, (long long)offset,
			    (long long)chunk, (long long)bufleft);
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
		brelse(bp);
		if (!isfile)
			p += chunk;
	}
  
 write_inode_and_leave:
	ffs_write_inode(&in.i_din, in.i_number, fsopts);
	if (fbuf)
		free(fbuf);
	if (ffd != -1)
		close(ffd);
}


static void
ffs_dump_dirbuf(dirbuf_t *dbuf, const char *dir, int needswap)
{
	doff_t		i;
	struct direct	*de;
	uint16_t	reclen;

	assert (dbuf != NULL);
	assert (dir != NULL);
	printf("ffs_dump_dirbuf: dir %s size %d cur %d\n",
	    dir, dbuf->size, dbuf->cur);

	for (i = 0; i < dbuf->size; ) {
		de = (struct direct *)(dbuf->buf + i);
		reclen = ufs_rw16(de->d_reclen, needswap);
		printf(
	    " inode %4d %7s offset %4d reclen %3d namlen %3d name %s\n",
		    ufs_rw32(de->d_ino, needswap),
		    inode_type(DTTOIF(de->d_type)), i, reclen,
		    de->d_namlen, de->d_name);
		i += reclen;
		assert(reclen > 0);
	}
}

static void
ffs_make_dirbuf(dirbuf_t *dbuf, const char *name, fsnode *node, int needswap)
{
	struct direct	de, *dp;
	uint16_t	llen, reclen;
	u_char		*newbuf;

	assert (dbuf != NULL);
	assert (name != NULL);
	assert (node != NULL);
					/* create direct entry */
	(void)memset(&de, 0, sizeof(de));
	de.d_ino = ufs_rw32(node->inode->ino, needswap);
	de.d_type = IFTODT(node->type);
	de.d_namlen = (uint8_t)strlen(name);
	strcpy(de.d_name, name);
	reclen = DIRSIZ_SWAP(0, &de, needswap);
	de.d_reclen = ufs_rw16(reclen, needswap);

	dp = (struct direct *)(dbuf->buf + dbuf->cur);
	llen = 0;
	if (dp != NULL)
		llen = DIRSIZ_SWAP(0, dp, needswap);

	if (debug & DEBUG_FS_MAKE_DIRBUF)
		printf(
		    "ffs_make_dirbuf: dbuf siz %d cur %d lastlen %d\n"
		    "  ino %d type %d reclen %d namlen %d name %.30s\n",
		    dbuf->size, dbuf->cur, llen,
		    ufs_rw32(de.d_ino, needswap), de.d_type, reclen,
		    de.d_namlen, de.d_name);

	if (reclen + dbuf->cur + llen > roundup(dbuf->size, DIRBLKSIZ)) {
		if (debug & DEBUG_FS_MAKE_DIRBUF)
			printf("ffs_make_dirbuf: growing buf to %d\n",
			    dbuf->size + DIRBLKSIZ);
		newbuf = erealloc(dbuf->buf, dbuf->size + DIRBLKSIZ);
		dbuf->buf = newbuf;
		dbuf->size += DIRBLKSIZ;
		memset(dbuf->buf + dbuf->size - DIRBLKSIZ, 0, DIRBLKSIZ);
		dbuf->cur = dbuf->size - DIRBLKSIZ;
	} else if (dp) {			/* shrink end of previous */
		dp->d_reclen = ufs_rw16(llen,needswap);
		dbuf->cur += llen;
	}
	dp = (struct direct *)(dbuf->buf + dbuf->cur);
	memcpy(dp, &de, reclen);
	dp->d_reclen = ufs_rw16(dbuf->size - dbuf->cur, needswap);
}

/*
 * cribbed from sys/ufs/ffs/ffs_alloc.c
 */
static void
ffs_write_inode(union dinode *dp, uint32_t ino, const fsinfo_t *fsopts)
{
	char 		*buf;
	struct ufs1_dinode *dp1;
	struct ufs2_dinode *dp2, *dip;
	struct cg	*cgp;
	struct fs	*fs;
	int		cg, cgino;
	uint32_t	i;
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
	if (debug & DEBUG_FS_WRITE_INODE)
		printf("ffs_write_inode: din %p ino %u cg %d cgino %d\n",
		    dp, ino, cg, cgino);

	ffs_rdfs(fsbtodb(fs, cgtod(fs, cg)), (int)fs->fs_cgsize, &sbbuf,
	    fsopts);
	cgp = (struct cg *)sbbuf;
	if (!cg_chkmagic_swap(cgp, fsopts->needswap))
		errx(1, "ffs_write_inode: cg %d: bad magic number", cg);

	assert (isclr(cg_inosused_swap(cgp, fsopts->needswap), cgino));

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
	setbit(cg_inosused_swap(cgp, fsopts->needswap), cgino);
	ufs_add32(cgp->cg_cs.cs_nifree, -1, fsopts->needswap);
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	if (S_ISDIR(DIP(dp, mode))) {
		ufs_add32(cgp->cg_cs.cs_ndir, 1, fsopts->needswap);
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++; 
	}

	/*
	 * Initialize inode blocks on the fly for UFS2.
	 */
	initediblk = ufs_rw32(cgp->cg_initediblk, fsopts->needswap);
	while (ffs_opts->version == 2 && cgino + INOPB(fs) > initediblk &&
	    initediblk < ufs_rw32(cgp->cg_niblk, fsopts->needswap)) {
		memset(buf, 0, fs->fs_bsize);
		dip = (struct ufs2_dinode *)buf;
		for (i = 0; i < INOPB(fs); i++) {
			dip->di_gen = random();
			dip++;
		}
		ffs_wtfs(fsbtodb(fs, ino_to_fsba(fs,
				  cg * fs->fs_ipg + initediblk)),
		    fs->fs_bsize, buf, fsopts);
		initediblk += INOPB(fs);
		cgp->cg_initediblk = ufs_rw32(initediblk, fsopts->needswap);
	}


	ffs_wtfs(fsbtodb(fs, cgtod(fs, cg)), (int)fs->fs_cgsize, &sbbuf,
	    fsopts);

					/* now write inode */
	d = fsbtodb(fs, ino_to_fsba(fs, ino));
	ffs_rdfs(d, fs->fs_bsize, buf, fsopts);
	if (fsopts->needswap) {
		if (ffs_opts->version == 1)
			ffs_dinode1_swap(&dp->ffs1_din,
			    &dp1[ino_to_fsbo(fs, ino)]);
		else
			ffs_dinode2_swap(&dp->ffs2_din,
			    &dp2[ino_to_fsbo(fs, ino)]);
	} else {
		if (ffs_opts->version == 1)
			dp1[ino_to_fsbo(fs, ino)] = dp->ffs1_din;
		else
			dp2[ino_to_fsbo(fs, ino)] = dp->ffs2_din;
	}
	ffs_wtfs(d, fs->fs_bsize, buf, fsopts);
	free(buf);
}

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
	exit(1);
}
