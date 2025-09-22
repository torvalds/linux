/*	$OpenBSD: ufs12.c,v 1.1 2022/09/02 10:15:35 miod Exp $	*/
/*	$NetBSD: ufs.c,v 1.16 1996/09/30 16:01:22 ws Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *
 * Copyright (c) 1990, 1991 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Author: David Golub
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 *	Stand-alone file reading package.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <lib/libkern/libkern.h>

#include <lib/libsa/stand.h>
#include "ufs12.h"

/*
 * In-core open file.
 */
struct file {
	int		f_is2;		/* 0 if ufs1, nonzero if ufs2 */
	off_t		f_seekp;	/* seek pointer */
	struct fs	*f_fs;		/* pointer to super-block */
	union {
		struct ufs1_dinode i1;
		struct ufs2_dinode i2;
	}		f_di;		/* copy of on-disk inode */
	ufsino_t	f_ino;		/* our inode number */
	int		f_nindir[NIADDR];
					/* number of blocks mapped by
					   indirect block at level i */
	char		*f_blk[NIADDR];	/* buffer for indirect block at
					   level i */
	size_t		f_blksize[NIADDR];
					/* size of buffer */
	daddr_t		f_blkno[NIADDR];/* disk address of block in buffer */
	char		*f_buf;		/* buffer for data block */
	size_t		f_buf_size;	/* size of data block */
	daddr_t		f_buf_blkno;	/* block number of data block */
};

static int	read_inode(ufsino_t, struct open_file *);
#if 0
static int	chmod_inode(ufsino_t, struct open_file *, mode_t);
#endif
static int	block_map(struct open_file *, daddr_t, daddr_t *);
static int	buf_read_file(struct open_file *, char **, size_t *);
static int	search_directory(char *, struct open_file *, ufsino_t *);
static int	ufs12_close_internal(struct file *);
#ifdef COMPAT_UFS
static void	ffs_oldfscompat(struct fs *);
#endif

/*
 * Read a new inode into a file structure.
 */
static int
read_inode(ufsino_t inumber, struct open_file *f)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	char *buf;
	size_t rsize;
	int rc;

	/*
	 * Read inode and save it.
	 */
	buf = alloc(fs->fs_bsize);
	twiddle();
	rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
	    fsbtodb(fs, ino_to_fsba(fs, inumber)), fs->fs_bsize, buf, &rsize);
	if (rc)
		goto out;
	if (rsize != (size_t)fs->fs_bsize) {
		rc = EIO;
		goto out;
	}

	if (fp->f_is2) {
		struct ufs2_dinode *dp;

		dp = (struct ufs2_dinode *)buf;
		fp->f_di.i2 = dp[ino_to_fsbo(fs, inumber)];
	} else {
		struct ufs1_dinode *dp;

		dp = (struct ufs1_dinode *)buf;
		fp->f_di.i1 = dp[ino_to_fsbo(fs, inumber)];
	}

	/*
	 * Clear out the old buffers
	 */
	{
		int level;

		for (level = 0; level < NIADDR; level++)
			fp->f_blkno[level] = -1;
		fp->f_buf_blkno = -1;
		fp->f_seekp = 0;
	}
out:
	free(buf, fs->fs_bsize);
	return (rc);
}

#if 0
/*
 * Read a new inode into a file structure.
 */
static int
chmod_inode(ufsino_t inumber, struct open_file *f, mode_t mode)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	char *buf;
	size_t rsize;
	int rc;

	/*
	 * Read inode and save it.
	 */
	buf = alloc(fs->fs_bsize);
	twiddle();
	rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
	    fsbtodb(fs, ino_to_fsba(fs, inumber)), fs->fs_bsize, buf, &rsize);
	if (rc)
		goto out;
	if (rsize != (size_t)fs->fs_bsize) {
		rc = EIO;
		goto out;
	}

	if (fp->f_is2) {
		struct ufs2_dinode *dp;

		dp = &((struct ufs2_dinode *)buf)[ino_to_fsbo(fs, inumber)];
		dp->di_mode = mode;
	} else {
		struct ufs1_dinode *dp;

		dp = &((struct ufs1_dinode *)buf)[ino_to_fsbo(fs, inumber)];
		dp->di_mode = mode;
	}

	twiddle();
	rc = (f->f_dev->dv_strategy)(f->f_devdata, F_WRITE,
	    fsbtodb(fs, ino_to_fsba(fs, inumber)), fs->fs_bsize, buf, NULL);

out:
	free(buf, fs->fs_bsize);
	return (rc);
}
#endif

/*
 * Given an offset in a file, find the disk block number that
 * contains that block.
 */
static int
block_map(struct open_file *f, daddr_t file_block, daddr_t *disk_block_p)
{
	struct file *fp = (struct file *)f->f_fsdata;
	daddr_t ind_block_num, *ind_p;
	struct fs *fs = fp->f_fs;
	int level, idx, rc;

	/*
	 * Index structure of an inode:
	 *
	 * di_db[0..NDADDR-1]	hold block numbers for blocks
	 *			0..NDADDR-1
	 *
	 * di_ib[0]		index block 0 is the single indirect block
	 *			holds block numbers for blocks
	 *			NDADDR .. NDADDR + NINDIR(fs)-1
	 *
	 * di_ib[1]		index block 1 is the double indirect block
	 *			holds block numbers for INDEX blocks for blocks
	 *			NDADDR + NINDIR(fs) ..
	 *			NDADDR + NINDIR(fs) + NINDIR(fs)**2 - 1
	 *
	 * di_ib[2]		index block 2 is the triple indirect block
	 *			holds block numbers for double-indirect
	 *			blocks for blocks
	 *			NDADDR + NINDIR(fs) + NINDIR(fs)**2 ..
	 *			NDADDR + NINDIR(fs) + NINDIR(fs)**2
	 *				+ NINDIR(fs)**3 - 1
	 */

	if (file_block < NDADDR) {
		/* Direct block. */
		if (fp->f_is2)
			*disk_block_p = fp->f_di.i2.di_db[file_block];
		else
			*disk_block_p = fp->f_di.i1.di_db[file_block];
		return (0);
	}

	file_block -= NDADDR;

	/*
	 * nindir[0] = NINDIR
	 * nindir[1] = NINDIR**2
	 * nindir[2] = NINDIR**3
	 *	etc
	 */
	for (level = 0; level < NIADDR; level++) {
		if (file_block < fp->f_nindir[level])
			break;
		file_block -= fp->f_nindir[level];
	}
	if (level == NIADDR) {
		/* Block number too high */
		return (EFBIG);
	}

	if (fp->f_is2)
		ind_block_num = fp->f_di.i2.di_ib[level];
	else
		ind_block_num = fp->f_di.i1.di_ib[level];

	for (; level >= 0; level--) {
		if (ind_block_num == 0) {
			*disk_block_p = 0;	/* missing */
			return (0);
		}

		if (fp->f_blkno[level] != ind_block_num) {
			if (fp->f_blk[level] == NULL)
				fp->f_blk[level] =
				    alloc(fs->fs_bsize);
			twiddle();
			rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
			    fsbtodb(fp->f_fs, ind_block_num), fs->fs_bsize,
			    fp->f_blk[level], &fp->f_blksize[level]);
			if (rc)
				return (rc);
			if (fp->f_blksize[level] != (size_t)fs->fs_bsize)
				return (EIO);
			fp->f_blkno[level] = ind_block_num;
		}

		ind_p = (daddr_t *)fp->f_blk[level];

		if (level > 0) {
			idx = file_block / fp->f_nindir[level - 1];
			file_block %= fp->f_nindir[level - 1];
		} else
			idx = file_block;

		ind_block_num = ind_p[idx];
	}

	*disk_block_p = ind_block_num;
	return (0);
}

/*
 * Read a portion of a file into an internal buffer.  Return
 * the location in the buffer and the amount in the buffer.
 */
static int
buf_read_file(struct open_file *f, char **buf_p, size_t *size_p)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	daddr_t file_block, disk_block;
	size_t block_size;
	long off;
	int rc;
	u_int64_t fsize;

	off = blkoff(fs, fp->f_seekp);
	file_block = lblkno(fs, fp->f_seekp);
	if (fp->f_is2)
		block_size = dblksize(fs, &fp->f_di.i2, (u_int64_t)file_block);
	else
		block_size = dblksize(fs, &fp->f_di.i1, (u_int64_t)file_block);

	if (file_block != fp->f_buf_blkno) {
		rc = block_map(f, file_block, &disk_block);
		if (rc)
			return (rc);

		if (fp->f_buf == NULL)
			fp->f_buf = alloc(fs->fs_bsize);

		if (disk_block == 0) {
			bzero(fp->f_buf, block_size);
			fp->f_buf_size = block_size;
		} else {
			twiddle();
			rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
			    fsbtodb(fs, disk_block),
			    block_size, fp->f_buf, &fp->f_buf_size);
			if (rc)
				return (rc);
		}

		fp->f_buf_blkno = file_block;
	}

	/*
	 * Return address of byte in buffer corresponding to
	 * offset, and size of remainder of buffer after that
	 * byte.
	 */
	*buf_p = fp->f_buf + off;
	*size_p = block_size - off;

	/*
	 * But truncate buffer at end of file.
	 */
	if (fp->f_is2)
		fsize = fp->f_di.i2.di_size;
	else
		fsize = fp->f_di.i1.di_size;
	if (*size_p > fsize - fp->f_seekp)
		*size_p = fsize - fp->f_seekp;

	return (0);
}

/*
 * Search a directory for a name and return its
 * i_number.
 */
static int
search_directory(char *name, struct open_file *f, ufsino_t *inumber_p)
{
	struct file *fp = (struct file *)f->f_fsdata;
	int namlen, length, rc;
	struct direct *dp, *edp;
	size_t buf_size;
	char *buf;
	u_int64_t fsize;

	length = strlen(name);

	fp->f_seekp = 0;
	if (fp->f_is2)
		fsize = fp->f_di.i2.di_size;
	else
		fsize = fp->f_di.i1.di_size;
	while ((u_int64_t)fp->f_seekp < fsize) {
		rc = buf_read_file(f, &buf, &buf_size);
		if (rc)
			return (rc);

		dp = (struct direct *)buf;
		edp = (struct direct *)(buf + buf_size);
		while (dp < edp) {
			if (dp->d_ino == 0)
				goto next;
#if BYTE_ORDER == LITTLE_ENDIAN
			if (fp->f_fs->fs_maxsymlinklen <= 0)
				namlen = dp->d_type;
			else
#endif
				namlen = dp->d_namlen;
			if (namlen == length &&
			    !strcmp(name, dp->d_name)) {
				/* found entry */
				*inumber_p = dp->d_ino;
				return (0);
			}
		next:
			dp = (struct direct *)((char *)dp + dp->d_reclen);
		}
		fp->f_seekp += buf_size;
	}
	return (ENOENT);
}

/*
 * Open a file.
 */
int
ufs12_open(char *path, struct open_file *f)
{
	char namebuf[MAXPATHLEN+1], *cp, *ncp, *buf = NULL;
	ufsino_t inumber, parent_inumber;
	int rc, c, nlinks = 0;
	struct file *fp;
	size_t buf_size;
	struct fs *fs;
	u_int16_t mode;

	/* allocate file system specific data structure */
	fp = alloc(sizeof(struct file));
	bzero(fp, sizeof(struct file));
	f->f_fsdata = (void *)fp;

	/* allocate space and read super block */
	fs = alloc(SBSIZE);
	fp->f_fs = fs;
	fp->f_is2 = 0;
	twiddle();
	rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
	    SBLOCK, SBSIZE, (char *)fs, &buf_size);
	if (rc)
		goto out;

	if (buf_size != SBSIZE || fs->fs_magic != FS_MAGIC ||
	    (size_t)fs->fs_bsize > MAXBSIZE ||
	    (size_t)fs->fs_bsize < sizeof(struct fs)) {
		/* try ufs2 */
		rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
		    SBLOCK_UFS2 / DEV_BSIZE, SBSIZE, (char *)fs, &buf_size);
		if (rc)
			goto out;

		if (buf_size != SBSIZE || fs->fs_magic != FS_UFS2_MAGIC ||
		    (u_int64_t)fs->fs_bsize > MAXBSIZE ||
		    (u_int64_t)fs->fs_bsize < sizeof(struct fs)) {
			rc = EINVAL;
			goto out;
		}
		fp->f_is2 = 1;
	}
#ifdef COMPAT_UFS
	ffs_oldfscompat(fs);
#endif

	/*
	 * Calculate indirect block levels.
	 */
	{
		int mult;
		int level;

		mult = 1;
		for (level = 0; level < NIADDR; level++) {
			mult *= NINDIR(fs);
			fp->f_nindir[level] = mult;
		}
	}

	inumber = ROOTINO;
	if ((rc = read_inode(inumber, f)) != 0)
		goto out;

	cp = path;
	while (*cp) {

		/*
		 * Remove extra separators
		 */
		while (*cp == '/')
			cp++;
		if (*cp == '\0')
			break;

		/*
		 * Check that current node is a directory.
		 */
		if (fp->f_is2)
			mode = fp->f_di.i2.di_mode;
		else
			mode = fp->f_di.i1.di_mode;
		if ((mode & IFMT) != IFDIR) {
			rc = ENOTDIR;
			goto out;
		}

		/*
		 * Get next component of path name.
		 */
		{
			int len = 0;

			ncp = cp;
			while ((c = *cp) != '\0' && c != '/') {
				if (++len > MAXNAMLEN) {
					rc = ENOENT;
					goto out;
				}
				cp++;
			}
			*cp = '\0';
		}

		/*
		 * Look up component in current directory.
		 * Save directory inumber in case we find a
		 * symbolic link.
		 */
		parent_inumber = inumber;
		rc = search_directory(ncp, f, &inumber);
		*cp = c;
		if (rc)
			goto out;

		/*
		 * Open next component.
		 */
		if ((rc = read_inode(inumber, f)) != 0)
			goto out;

		/*
		 * Check for symbolic link.
		 */
		if (fp->f_is2)
			mode = fp->f_di.i2.di_mode;
		else
			mode = fp->f_di.i1.di_mode;
		if ((mode & IFMT) == IFLNK) {
			u_int64_t link_len;
			size_t len;

			if (fp->f_is2)
				link_len = fp->f_di.i2.di_size;
			else
				link_len = fp->f_di.i1.di_size;
			len = strlen(cp);

			if (link_len + len > MAXPATHLEN ||
			    ++nlinks > MAXSYMLINKS) {
				rc = ENOENT;
				goto out;
			}

			bcopy(cp, &namebuf[link_len], len + 1);

			if (link_len < (u_int64_t)fs->fs_maxsymlinklen) {
				if (fp->f_is2) {
					bcopy(fp->f_di.i2.di_shortlink, namebuf,
					    link_len);
				} else {
					bcopy(fp->f_di.i1.di_shortlink, namebuf,
					    link_len);
				}
			} else {
				/*
				 * Read file for symbolic link
				 */
				daddr_t disk_block;
				fs = fp->f_fs;

				if (!buf)
					buf = alloc(fs->fs_bsize);
				rc = block_map(f, (daddr_t)0, &disk_block);
				if (rc)
					goto out;

				twiddle();
				rc = (f->f_dev->dv_strategy)(f->f_devdata,
				    F_READ, fsbtodb(fs, disk_block),
				    fs->fs_bsize, buf, &buf_size);
				if (rc)
					goto out;

				bcopy(buf, namebuf, link_len);
			}

			/*
			 * If relative pathname, restart at parent directory.
			 * If absolute pathname, restart at root.
			 */
			cp = namebuf;
			if (*cp != '/')
				inumber = parent_inumber;
			else
				inumber = ROOTINO;

			if ((rc = read_inode(inumber, f)) != 0)
				goto out;
		}
	}

	/*
	 * Found terminal component.
	 */
	fp->f_ino = inumber;
	rc = 0;
out:
	if (buf)
		free(buf, fs->fs_bsize);
	if (rc)
		(void)ufs12_close_internal(fp);

	return (rc);
}

int
ufs12_close(struct open_file *f)
{
	struct file *fp = (struct file *)f->f_fsdata;

	f->f_fsdata = NULL;
	if (fp == NULL)
		return (0);

	return (ufs12_close_internal(fp));
}

static int
ufs12_close_internal(struct file *fp)
{
	int level;

	for (level = 0; level < NIADDR; level++) {
		if (fp->f_blk[level])
			free(fp->f_blk[level], fp->f_fs->fs_bsize);
	}
	if (fp->f_buf)
		free(fp->f_buf, fp->f_fs->fs_bsize);
	free(fp->f_fs, SBSIZE);
	free(fp, sizeof(struct file));
	return (0);
}

/*
 * Copy a portion of a file into kernel memory.
 * Cross block boundaries when necessary.
 */
int
ufs12_read(struct open_file *f, void *start, size_t size, size_t *resid)
{
	struct file *fp = (struct file *)f->f_fsdata;
	char *buf, *addr = start;
	size_t csize, buf_size;
	u_int64_t fsize;
	int rc = 0;

	while (size != 0) {
		if (fp->f_is2)
			fsize = fp->f_di.i2.di_size;
		else
			fsize = fp->f_di.i1.di_size;
		if ((u_int64_t)fp->f_seekp >= fsize)
			break;

		rc = buf_read_file(f, &buf, &buf_size);
		if (rc)
			break;

		csize = size;
		if (csize > buf_size)
			csize = buf_size;

		bcopy(buf, addr, csize);

		fp->f_seekp += csize;
		addr += csize;
		size -= csize;
	}
	if (resid)
		*resid = size;
	return (rc);
}

/*
 * Not implemented.
 */
int
ufs12_write(struct open_file *f, void *start, size_t size, size_t *resid)
{

	return (EROFS);
}

off_t
ufs12_seek(struct open_file *f, off_t offset, int where)
{
	struct file *fp = (struct file *)f->f_fsdata;
	u_int64_t fsize;

	switch (where) {
	case SEEK_SET:
		fp->f_seekp = offset;
		break;
	case SEEK_CUR:
		fp->f_seekp += offset;
		break;
	case SEEK_END:
		if (fp->f_is2)
			fsize = fp->f_di.i2.di_size;
		else
			fsize = fp->f_di.i1.di_size;
		fp->f_seekp = fsize - offset;
		break;
	default:
		return (-1);
	}
	return (fp->f_seekp);
}

int
ufs12_stat(struct open_file *f, struct stat *sb)
{
	struct file *fp = (struct file *)f->f_fsdata;

	/* only important stuff */
	if (fp->f_is2) {
		sb->st_mode = fp->f_di.i2.di_mode;
		sb->st_uid = fp->f_di.i2.di_uid;
		sb->st_gid = fp->f_di.i2.di_gid;
		sb->st_size = fp->f_di.i2.di_size;
	} else {
		sb->st_mode = fp->f_di.i1.di_mode;
		sb->st_uid = fp->f_di.i1.di_uid;
		sb->st_gid = fp->f_di.i1.di_gid;
		sb->st_size = fp->f_di.i1.di_size;
	}
	return (0);
}

int
ufs12_fchmod(struct open_file *f, mode_t mode)
{
#if 0
	struct file *fp = (struct file *)f->f_fsdata;

	return chmod_inode(fp->f_ino, f, mode);
#else
	return EIO;
#endif
}

#ifndef	NO_READDIR
int
ufs12_readdir(struct open_file *f, char *name)
{
#if 0
	struct file *fp = (struct file *)f->f_fsdata;
	struct direct *dp, *edp;
	size_t buf_size;
	int rc, namlen;
	char *buf;
	u_int64_t fsize;

	if (name == NULL)
		fp->f_seekp = 0;
	else {
			/* end of dir */
		if (fp->f_is2)
			fsize = fp->f_di.i2.di_size;
		else
			fsize = fp->f_di.i1.di_size;
		if ((u_int64_t)fp->f_seekp >= fsize) {
			*name = '\0';
			return -1;
		}

		do {
			if ((rc = buf_read_file(f, &buf, &buf_size)) != 0)
				return rc;

			dp = (struct direct *)buf;
			edp = (struct direct *)(buf + buf_size);
			while (dp < edp && dp->d_ino == 0)
				dp = (struct direct *)((char *)dp + dp->d_reclen);
			fp->f_seekp += buf_size -
			    ((u_int8_t *)edp - (u_int8_t *)dp);
		} while (dp >= edp);

#if BYTE_ORDER == LITTLE_ENDIAN
		if (fp->f_fs->fs_maxsymlinklen <= 0)
			namlen = dp->d_type;
		else
#endif
			namlen = dp->d_namlen;
		strncpy(name, dp->d_name, namlen + 1);

		fp->f_seekp += dp->d_reclen;
	}

	return 0;
#else
	return EIO;
#endif
}
#endif

#ifdef COMPAT_UFS
/*
 * Sanity checks for old file systems.
 *
 * XXX - goes away some day.
 */
static void
ffs_oldfscompat(struct fs *fs)
{
	int i;

	fs->fs_npsect = max(fs->fs_npsect, fs->fs_nsect);	/* XXX */
	fs->fs_interleave = max(fs->fs_interleave, 1);		/* XXX */
	if (fs->fs_postblformat == FS_42POSTBLFMT)		/* XXX */
		fs->fs_nrpos = 8;				/* XXX */
	if (fs->fs_inodefmt < FS_44INODEFMT) {			/* XXX */
		quad_t sizepb = fs->fs_bsize;			/* XXX */
								/* XXX */
		fs->fs_maxfilesize = fs->fs_bsize * NDADDR - 1;	/* XXX */
		for (i = 0; i < NIADDR; i++) {			/* XXX */
			sizepb *= NINDIR(fs);			/* XXX */
			fs->fs_maxfilesize += sizepb;		/* XXX */
		}						/* XXX */
		fs->fs_qbmask = ~fs->fs_bmask;			/* XXX */
		fs->fs_qfmask = ~fs->fs_fmask;			/* XXX */
	}							/* XXX */
}
#endif
