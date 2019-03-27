/*	$NetBSD: ufs.c,v 1.20 1998/03/01 07:15:39 ross Exp $	*/

/*-
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program
 *
 * Copyright (c) 1982, 1989, 1993
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *	Stand-alone file reading package.
 */

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/time.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>
#include "stand.h"
#include "string.h"

static int	ufs_open(const char *path, struct open_file *f);
static int	ufs_write(struct open_file *f, const void *buf, size_t size,
		size_t *resid);
static int	ufs_close(struct open_file *f);
static int	ufs_read(struct open_file *f, void *buf, size_t size, size_t *resid);
static off_t	ufs_seek(struct open_file *f, off_t offset, int where);
static int	ufs_stat(struct open_file *f, struct stat *sb);
static int	ufs_readdir(struct open_file *f, struct dirent *d);

struct fs_ops ufs_fsops = {
	"ufs",
	ufs_open,
	ufs_close,
	ufs_read,
	ufs_write,
	ufs_seek,
	ufs_stat,
	ufs_readdir
};

/*
 * In-core open file.
 */
struct file {
	off_t		f_seekp;	/* seek pointer */
	struct fs	*f_fs;		/* pointer to super-block */
	union dinode {
		struct ufs1_dinode di1;
		struct ufs2_dinode di2;
	}		f_di;		/* copy of on-disk inode */
	int		f_nindir[UFS_NIADDR];
					/* number of blocks mapped by
					   indirect block at level i */
	char		*f_blk[UFS_NIADDR];	/* buffer for indirect block at
					   level i */
	size_t		f_blksize[UFS_NIADDR];
					/* size of buffer */
	ufs2_daddr_t	f_blkno[UFS_NIADDR];/* disk address of block in buffer */
	ufs2_daddr_t	f_buf_blkno;	/* block number of data block */
	char		*f_buf;		/* buffer for data block */
	size_t		f_buf_size;	/* size of data block */
	int		f_inumber;	/* inumber */
};
#define DIP(fp, field) \
	((fp)->f_fs->fs_magic == FS_UFS1_MAGIC ? \
	(fp)->f_di.di1.field : (fp)->f_di.di2.field)

static int	read_inode(ino_t, struct open_file *);
static int	block_map(struct open_file *, ufs2_daddr_t, ufs2_daddr_t *);
static int	buf_read_file(struct open_file *, char **, size_t *);
static int	buf_write_file(struct open_file *, const char *, size_t *);
static int	search_directory(char *, struct open_file *, ino_t *);
static int	ufs_use_sa_read(void *, off_t, void **, int);

/* from ffs_subr.c */
int	ffs_sbget(void *, struct fs **, off_t, char *,
	    int (*)(void *, off_t, void **, int));
/*
 * Request standard superblock location in ffs_sbget
 */
#define	STDSB			-1	/* Fail if check-hash is bad */
#define	STDSB_NOHASHFAIL	-2	/* Ignore check-hash failure */

/*
 * Read a new inode into a file structure.
 */
static int
read_inode(inumber, f)
	ino_t inumber;
	struct open_file *f;
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	char *buf;
	size_t rsize;
	int rc;

	if (fs == NULL)
	    panic("fs == NULL");

	/*
	 * Read inode and save it.
	 */
	buf = malloc(fs->fs_bsize);
	twiddle(1);
	rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
		fsbtodb(fs, ino_to_fsba(fs, inumber)), fs->fs_bsize,
		buf, &rsize);
	if (rc)
		goto out;
	if (rsize != fs->fs_bsize) {
		rc = EIO;
		goto out;
	}

	if (fp->f_fs->fs_magic == FS_UFS1_MAGIC)
		fp->f_di.di1 = ((struct ufs1_dinode *)buf)
		    [ino_to_fsbo(fs, inumber)];
	else
		fp->f_di.di2 = ((struct ufs2_dinode *)buf)
		    [ino_to_fsbo(fs, inumber)];

	/*
	 * Clear out the old buffers
	 */
	{
		int level;

		for (level = 0; level < UFS_NIADDR; level++)
			fp->f_blkno[level] = -1;
		fp->f_buf_blkno = -1;
	}
	fp->f_seekp = 0;
	fp->f_inumber = inumber;
out:
	free(buf);
	return (rc);	 
}

/*
 * Given an offset in a file, find the disk block number that
 * contains that block.
 */
static int
block_map(f, file_block, disk_block_p)
	struct open_file *f;
	ufs2_daddr_t file_block;
	ufs2_daddr_t *disk_block_p;	/* out */
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	int level;
	int idx;
	ufs2_daddr_t ind_block_num;
	int rc;

	/*
	 * Index structure of an inode:
	 *
	 * di_db[0..UFS_NDADDR-1] hold block numbers for blocks
	 *			0..UFS_NDADDR-1
	 *
	 * di_ib[0]		index block 0 is the single indirect block
	 *			holds block numbers for blocks
	 *			UFS_NDADDR .. UFS_NDADDR + NINDIR(fs)-1
	 *
	 * di_ib[1]		index block 1 is the double indirect block
	 *			holds block numbers for INDEX blocks for blocks
	 *			UFS_NDADDR + NINDIR(fs) ..
	 *			UFS_NDADDR + NINDIR(fs) + NINDIR(fs)**2 - 1
	 *
	 * di_ib[2]		index block 2 is the triple indirect block
	 *			holds block numbers for double-indirect
	 *			blocks for blocks
	 *			UFS_NDADDR + NINDIR(fs) + NINDIR(fs)**2 ..
	 *			UFS_NDADDR + NINDIR(fs) + NINDIR(fs)**2
	 *				+ NINDIR(fs)**3 - 1
	 */

	if (file_block < UFS_NDADDR) {
		/* Direct block. */
		*disk_block_p = DIP(fp, di_db[file_block]);
		return (0);
	}

	file_block -= UFS_NDADDR;

	/*
	 * nindir[0] = NINDIR
	 * nindir[1] = NINDIR**2
	 * nindir[2] = NINDIR**3
	 *	etc
	 */
	for (level = 0; level < UFS_NIADDR; level++) {
		if (file_block < fp->f_nindir[level])
			break;
		file_block -= fp->f_nindir[level];
	}
	if (level == UFS_NIADDR) {
		/* Block number too high */
		return (EFBIG);
	}

	ind_block_num = DIP(fp, di_ib[level]);

	for (; level >= 0; level--) {
		if (ind_block_num == 0) {
			*disk_block_p = 0;	/* missing */
			return (0);
		}

		if (fp->f_blkno[level] != ind_block_num) {
			if (fp->f_blk[level] == (char *)0)
				fp->f_blk[level] =
					malloc(fs->fs_bsize);
			twiddle(1);
			rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
				fsbtodb(fp->f_fs, ind_block_num),
				fs->fs_bsize,
				fp->f_blk[level],
				&fp->f_blksize[level]);
			if (rc)
				return (rc);
			if (fp->f_blksize[level] != fs->fs_bsize)
				return (EIO);
			fp->f_blkno[level] = ind_block_num;
		}

		if (level > 0) {
			idx = file_block / fp->f_nindir[level - 1];
			file_block %= fp->f_nindir[level - 1];
		} else
			idx = file_block;

		if (fp->f_fs->fs_magic == FS_UFS1_MAGIC)
			ind_block_num = ((ufs1_daddr_t *)fp->f_blk[level])[idx];
		else
			ind_block_num = ((ufs2_daddr_t *)fp->f_blk[level])[idx];
	}

	*disk_block_p = ind_block_num;

	return (0);
}

/*
 * Write a portion of a file from an internal buffer.
 */
static int
buf_write_file(f, buf_p, size_p)
	struct open_file *f;
	const char *buf_p;
	size_t *size_p;		/* out */
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	long off;
	ufs_lbn_t file_block;
	ufs2_daddr_t disk_block;
	size_t block_size;
	int rc;

	/*
	 * Calculate the starting block address and offset.
	 */
	off = blkoff(fs, fp->f_seekp);
	file_block = lblkno(fs, fp->f_seekp);
	block_size = sblksize(fs, DIP(fp, di_size), file_block);

	rc = block_map(f, file_block, &disk_block);
	if (rc)
		return (rc);

 	if (disk_block == 0)
		/* Because we can't allocate space on the drive */
		return (EFBIG);

	/*
	 * Truncate buffer at end of file, and at the end of
	 * this block.
	 */
	if (*size_p > DIP(fp, di_size) - fp->f_seekp)
		*size_p = DIP(fp, di_size) - fp->f_seekp;
	if (*size_p > block_size - off) 
		*size_p = block_size - off;

	/*
	 * If we don't entirely occlude the block and it's not
	 * in memory already, read it in first.
	 */
	if (((off > 0) || (*size_p + off < block_size)) &&
	    (file_block != fp->f_buf_blkno)) {

		if (fp->f_buf == (char *)0)
			fp->f_buf = malloc(fs->fs_bsize);

		twiddle(4);
		rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
			fsbtodb(fs, disk_block),
			block_size, fp->f_buf, &fp->f_buf_size);
		if (rc)
			return (rc);

		fp->f_buf_blkno = file_block;
	}

	/*
	 *	Copy the user data into the cached block.
	 */
	bcopy(buf_p, fp->f_buf + off, *size_p);

	/*
	 *	Write the block out to storage.
	 */

	twiddle(4);
	rc = (f->f_dev->dv_strategy)(f->f_devdata, F_WRITE,
		fsbtodb(fs, disk_block),
		block_size, fp->f_buf, &fp->f_buf_size);
	return (rc);
}

/*
 * Read a portion of a file into an internal buffer.  Return
 * the location in the buffer and the amount in the buffer.
 */
static int
buf_read_file(f, buf_p, size_p)
	struct open_file *f;
	char **buf_p;		/* out */
	size_t *size_p;		/* out */
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct fs *fs = fp->f_fs;
	long off;
	ufs_lbn_t file_block;
	ufs2_daddr_t disk_block;
	size_t block_size;
	int rc;

	off = blkoff(fs, fp->f_seekp);
	file_block = lblkno(fs, fp->f_seekp);
	block_size = sblksize(fs, DIP(fp, di_size), file_block);

	if (file_block != fp->f_buf_blkno) {
		if (fp->f_buf == (char *)0)
			fp->f_buf = malloc(fs->fs_bsize);

		rc = block_map(f, file_block, &disk_block);
		if (rc)
			return (rc);

		if (disk_block == 0) {
			bzero(fp->f_buf, block_size);
			fp->f_buf_size = block_size;
		} else {
			twiddle(4);
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
	if (*size_p > DIP(fp, di_size) - fp->f_seekp)
		*size_p = DIP(fp, di_size) - fp->f_seekp;

	return (0);
}

/*
 * Search a directory for a name and return its
 * i_number.
 */
static int
search_directory(name, f, inumber_p)
	char *name;
	struct open_file *f;
	ino_t *inumber_p;		/* out */
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct direct *dp;
	struct direct *edp;
	char *buf;
	size_t buf_size;
	int namlen, length;
	int rc;

	length = strlen(name);

	fp->f_seekp = 0;
	while (fp->f_seekp < DIP(fp, di_size)) {
		rc = buf_read_file(f, &buf, &buf_size);
		if (rc)
			return (rc);

		dp = (struct direct *)buf;
		edp = (struct direct *)(buf + buf_size);
		while (dp < edp) {
			if (dp->d_ino == (ino_t)0)
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
static int
ufs_open(upath, f)
	const char *upath;
	struct open_file *f;
{
	char *cp, *ncp;
	int c;
	ino_t inumber, parent_inumber;
	struct file *fp;
	struct fs *fs;
	int rc;
	int nlinks = 0;
	char namebuf[MAXPATHLEN+1];
	char *buf = NULL;
	char *path = NULL;

	/* allocate file system specific data structure */
	fp = malloc(sizeof(struct file));
	bzero(fp, sizeof(struct file));
	f->f_fsdata = (void *)fp;

	/* read super block */
	twiddle(1);
	if ((rc = ffs_sbget(f, &fs, STDSB_NOHASHFAIL, "stand",
	     ufs_use_sa_read)) != 0)
		goto out;
	fp->f_fs = fs;
	/*
	 * Calculate indirect block levels.
	 */
	{
		ufs2_daddr_t mult;
		int level;

		mult = 1;
		for (level = 0; level < UFS_NIADDR; level++) {
			mult *= NINDIR(fs);
			fp->f_nindir[level] = mult;
		}
	}

	inumber = UFS_ROOTINO;
	if ((rc = read_inode(inumber, f)) != 0)
		goto out;

	cp = path = strdup(upath);
	if (path == NULL) {
	    rc = ENOMEM;
	    goto out;
	}
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
		if ((DIP(fp, di_mode) & IFMT) != IFDIR) {
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
				if (++len > UFS_MAXNAMLEN) {
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
		if ((DIP(fp, di_mode) & IFMT) == IFLNK) {
			int link_len = DIP(fp, di_size);
			int len;

			len = strlen(cp);

			if (link_len + len > MAXPATHLEN ||
			    ++nlinks > MAXSYMLINKS) {
				rc = ENOENT;
				goto out;
			}

			bcopy(cp, &namebuf[link_len], len + 1);

			if (link_len < fs->fs_maxsymlinklen) {
				if (fp->f_fs->fs_magic == FS_UFS1_MAGIC)
					cp = (caddr_t)(fp->f_di.di1.di_db);
				else
					cp = (caddr_t)(fp->f_di.di2.di_db);
				bcopy(cp, namebuf, (unsigned) link_len);
			} else {
				/*
				 * Read file for symbolic link
				 */
				size_t buf_size;
				ufs2_daddr_t disk_block;
				struct fs *fs = fp->f_fs;

				if (!buf)
					buf = malloc(fs->fs_bsize);
				rc = block_map(f, (ufs2_daddr_t)0, &disk_block);
				if (rc)
					goto out;
				
				twiddle(1);
				rc = (f->f_dev->dv_strategy)(f->f_devdata,
					F_READ, fsbtodb(fs, disk_block),
					fs->fs_bsize, buf, &buf_size);
				if (rc)
					goto out;

				bcopy((char *)buf, namebuf, (unsigned)link_len);
			}

			/*
			 * If relative pathname, restart at parent directory.
			 * If absolute pathname, restart at root.
			 */
			cp = namebuf;
			if (*cp != '/')
				inumber = parent_inumber;
			else
				inumber = (ino_t)UFS_ROOTINO;

			if ((rc = read_inode(inumber, f)) != 0)
				goto out;
		}
	}

	/*
	 * Found terminal component.
	 */
	rc = 0;
	fp->f_seekp = 0;
out:
	if (buf)
		free(buf);
	if (path)
		free(path);
	if (rc) {
		if (fp->f_buf)
			free(fp->f_buf);
		free(fp->f_fs);
		free(fp);
	}
	return (rc);
}

/*
 * A read function for use by standalone-layer routines.
 */
static int
ufs_use_sa_read(void *devfd, off_t loc, void **bufp, int size)
{
	struct open_file *f;
	size_t buf_size;
	int error;

	f = (struct open_file *)devfd;
	if ((*bufp = malloc(size)) == NULL)
		return (ENOSPC);
	error = (f->f_dev->dv_strategy)(f->f_devdata, F_READ, loc / DEV_BSIZE,
	    size, *bufp, &buf_size);
	if (error != 0)
		return (error);
	if (buf_size != size)
		return (EIO);
	return (0);
}

static int
ufs_close(f)
	struct open_file *f;
{
	struct file *fp = (struct file *)f->f_fsdata;
	int level;

	f->f_fsdata = (void *)0;
	if (fp == (struct file *)0)
		return (0);

	for (level = 0; level < UFS_NIADDR; level++) {
		if (fp->f_blk[level])
			free(fp->f_blk[level]);
	}
	if (fp->f_buf)
		free(fp->f_buf);
	free(fp->f_fs);
	free(fp);
	return (0);
}

/*
 * Copy a portion of a file into kernel memory.
 * Cross block boundaries when necessary.
 */
static int
ufs_read(f, start, size, resid)
	struct open_file *f;
	void *start;
	size_t size;
	size_t *resid;	/* out */
{
	struct file *fp = (struct file *)f->f_fsdata;
	size_t csize;
	char *buf;
	size_t buf_size;
	int rc = 0;
	char *addr = start;

	while (size != 0) {
		if (fp->f_seekp >= DIP(fp, di_size))
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
 * Write to a portion of an already allocated file.
 * Cross block boundaries when necessary. Can not
 * extend the file.
 */
static int
ufs_write(f, start, size, resid)
	struct open_file *f;
	const void *start;
	size_t size;
	size_t *resid;	/* out */
{
	struct file *fp = (struct file *)f->f_fsdata;
	size_t csize;
	int rc = 0;
	const char *addr = start;

	csize = size;
	while ((size != 0) && (csize != 0)) {
		if (fp->f_seekp >= DIP(fp, di_size))
			break;

		if (csize >= 512) csize = 512; /* XXX */

		rc = buf_write_file(f, addr, &csize);
		if (rc)
			break;

		fp->f_seekp += csize;
		addr += csize;
		size -= csize;
	}
	if (resid)
		*resid = size;
	return (rc);
}

static off_t
ufs_seek(f, offset, where)
	struct open_file *f;
	off_t offset;
	int where;
{
	struct file *fp = (struct file *)f->f_fsdata;

	switch (where) {
	case SEEK_SET:
		fp->f_seekp = offset;
		break;
	case SEEK_CUR:
		fp->f_seekp += offset;
		break;
	case SEEK_END:
		fp->f_seekp = DIP(fp, di_size) - offset;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}
	return (fp->f_seekp);
}

static int
ufs_stat(f, sb)
	struct open_file *f;
	struct stat *sb;
{
	struct file *fp = (struct file *)f->f_fsdata;

	/* only important stuff */
	sb->st_mode = DIP(fp, di_mode);
	sb->st_uid = DIP(fp, di_uid);
	sb->st_gid = DIP(fp, di_gid);
	sb->st_size = DIP(fp, di_size);
	sb->st_mtime = DIP(fp, di_mtime);
	/*
	 * The items below are ufs specific!
	 * Other fs types will need their own solution
	 * if these fields are needed.
	 */
	sb->st_ino = fp->f_inumber;
	/*
	 * We need something to differentiate devs.
	 * fs_id is unique but 64bit, we xor the two
	 * halves to squeeze it into 32bits.
	 */
	sb->st_dev = (dev_t)(fp->f_fs->fs_id[0] ^ fp->f_fs->fs_id[1]);

	return (0);
}

static int
ufs_readdir(struct open_file *f, struct dirent *d)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct direct *dp;
	char *buf;
	size_t buf_size;
	int error;

	/*
	 * assume that a directory entry will not be split across blocks
	 */
again:
	if (fp->f_seekp >= DIP(fp, di_size))
		return (ENOENT);
	error = buf_read_file(f, &buf, &buf_size);
	if (error)
		return (error);
	dp = (struct direct *)buf;
	fp->f_seekp += dp->d_reclen;
	if (dp->d_ino == (ino_t)0)
		goto again;
	d->d_type = dp->d_type;
	strcpy(d->d_name, dp->d_name);
	return (0);
}
