/*-
 * Copyright (c) 1999,2000 Jonathan Lemon <jlemon@freebsd.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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

#include <sys/param.h>
#include <sys/time.h>
#include "stand.h"
#include "string.h"

static int	ext2fs_open(const char *path, struct open_file *f);
static int	ext2fs_close(struct open_file *f);
static int	ext2fs_read(struct open_file *f, void *buf,
			 size_t size, size_t *resid);
static off_t	ext2fs_seek(struct open_file *f, off_t offset, int where);
static int	ext2fs_stat(struct open_file *f, struct stat *sb);
static int	ext2fs_readdir(struct open_file *f, struct dirent *d);

static int dtmap[] = { DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR,
			 DT_BLK, DT_FIFO, DT_SOCK, DT_LNK };
#define EXTFTODT(x)	(x) > sizeof(dtmap) / sizeof(dtmap[0]) ? \
			DT_UNKNOWN : dtmap[x]

struct fs_ops ext2fs_fsops = {
	"ext2fs",
	ext2fs_open,
	ext2fs_close,
	ext2fs_read,
	null_write,
	ext2fs_seek,
	ext2fs_stat,
	ext2fs_readdir
};

#define	EXT2_SBSIZE	1024
#define	EXT2_SBLOCK	(1024 / DEV_BSIZE)	/* block offset of superblock */
#define EXT2_MAGIC	0xef53
#define EXT2_ROOTINO	2

#define EXT2_REV0		0	/* original revision of ext2 */
#define EXT2_R0_ISIZE		128	/* inode size */
#define EXT2_R0_FIRSTINO	11	/* first inode */

#define EXT2_MINBSHIFT		10	/* mininum block shift */
#define EXT2_MINFSHIFT		10	/* mininum frag shift */

#define EXT2_NDADDR		12	/* # of direct blocks */
#define EXT2_NIADDR		3	/* # of indirect blocks */

/*
 * file system block to disk address
 */
#define fsb_to_db(fs, blk)	((blk) << (fs)->fs_fsbtodb)

/*
 * inode to block group offset
 * inode to block group
 * inode to disk address
 * inode to block offset
 */
#define ino_to_bgo(fs, ino)	(((ino) - 1) % (fs)->fs_ipg)
#define ino_to_bg(fs, ino)	(((ino) - 1) / (fs)->fs_ipg)
#define ino_to_db(fs, bg, ino) \
	fsb_to_db(fs, ((bg)[ino_to_bg(fs, ino)].bg_inotbl + \
	    ino_to_bgo(fs, ino) / (fs)->fs_ipb))
#define ino_to_bo(fs, ino)	(ino_to_bgo(fs, ino) % (fs)->fs_ipb)

#define nindir(fs) \
	((fs)->fs_bsize / sizeof(uint32_t))
#define lblkno(fs, loc)				/* loc / bsize */ \
	((loc) >> (fs)->fs_bshift)
#define smalllblktosize(fs, blk)		/* blk * bsize */ \
	((blk) << (fs)->fs_bshift)
#define blkoff(fs, loc)				/* loc % bsize */ \
	((loc) & (fs)->fs_bmask)
#define fragroundup(fs, size)			/* roundup(size, fsize) */ \
	(((size) + (fs)->fs_fmask) & ~(fs)->fs_fmask)
#define dblksize(fs, dip, lbn) \
	(((lbn) >= EXT2_NDADDR || (dip)->di_size >= smalllblktosize(fs, (lbn) + 1)) \
	    ? (fs)->fs_bsize \
	    : (fragroundup(fs, blkoff(fs, (dip)->di_size))))

/*
 * superblock describing ext2fs
 */
struct ext2fs_disk {
	uint32_t	fd_inodes;	/* # of inodes */
	uint32_t	fd_blocks;	/* # of blocks */
	uint32_t	fd_resblk;	/* # of reserved blocks */
	uint32_t	fd_freeblk;	/* # of free blocks */
	uint32_t	fd_freeino;	/* # of free inodes */
	uint32_t	fd_firstblk;	/* first data block */
	uint32_t	fd_bsize;	/* block size */
	uint32_t	fd_fsize;	/* frag size */
	uint32_t	fd_bpg;		/* blocks per group */
	uint32_t	fd_fpg;		/* frags per group */
	uint32_t	fd_ipg;		/* inodes per group */
	uint32_t	fd_mtime;	/* mount time */
	uint32_t	fd_wtime;	/* write time */
	uint16_t	fd_mount;	/* # of mounts */
	int16_t		fd_maxmount;	/* max # of mounts */
	uint16_t	fd_magic;	/* magic number */
	uint16_t	fd_state;	/* state */
	uint16_t	fd_eflag;	/* error flags */
	uint16_t	fd_mnrrev;	/* minor revision */
	uint32_t	fd_lastchk;	/* last check */
	uint32_t	fd_chkintvl;	/* maximum check interval */
	uint32_t	fd_os;		/* os */
	uint32_t	fd_revision;	/* revision */
	uint16_t	fd_uid;		/* uid for reserved blocks */
	uint16_t	fd_gid;		/* gid for reserved blocks */

	uint32_t	fd_firstino;	/* first non-reserved inode */
	uint16_t	fd_isize;	/* inode size */
	uint16_t	fd_nblkgrp;	/* block group # of superblock */
	uint32_t	fd_fcompat;	/* compatible features */
	uint32_t	fd_fincompat;	/* incompatible features */
	uint32_t	fd_frocompat;	/* read-only compatibilties */
	uint8_t		fd_uuid[16];	/* volume uuid */
	char 		fd_volname[16];	/* volume name */
	char 		fd_fsmnt[64];	/* name last mounted on */
	uint32_t	fd_bitmap;	/* compression bitmap */

	uint8_t		fd_nblkpa;	/* # of blocks to preallocate */	
	uint8_t		fd_ndblkpa;	/* # of dir blocks to preallocate */
};

struct ext2fs_core {
	int		fc_bsize;	/* block size */
	int		fc_bshift;	/* block shift amount */
	int		fc_bmask;	/* block mask */
	int		fc_fsize;	/* frag size */
	int		fc_fshift;	/* frag shift amount */
	int		fc_fmask;	/* frag mask */
	int		fc_isize;	/* inode size */
	int		fc_imask;	/* inode mask */
	int		fc_firstino;	/* first non-reserved inode */
	int		fc_ipb;		/* inodes per block */
	int		fc_fsbtodb;	/* fsb to ds shift */
};

struct ext2fs {
	struct		ext2fs_disk fs_fd;
	char		fs_pad[EXT2_SBSIZE - sizeof(struct ext2fs_disk)];
	struct		ext2fs_core fs_fc;

#define fs_magic	fs_fd.fd_magic
#define fs_revision	fs_fd.fd_revision
#define fs_blocks	fs_fd.fd_blocks
#define fs_firstblk	fs_fd.fd_firstblk
#define fs_bpg		fs_fd.fd_bpg
#define fs_ipg		fs_fd.fd_ipg
	    
#define fs_bsize	fs_fc.fc_bsize
#define fs_bshift	fs_fc.fc_bshift
#define fs_bmask	fs_fc.fc_bmask
#define fs_fsize	fs_fc.fc_fsize
#define fs_fshift	fs_fc.fc_fshift
#define fs_fmask	fs_fc.fc_fmask
#define fs_isize	fs_fc.fc_isize
#define fs_imask	fs_fc.fc_imask
#define fs_firstino	fs_fc.fc_firstino
#define fs_ipb		fs_fc.fc_ipb
#define fs_fsbtodb	fs_fc.fc_fsbtodb
};

struct ext2blkgrp {
	uint32_t	bg_blkmap;	/* block bitmap */
	uint32_t	bg_inomap;	/* inode bitmap */
	uint32_t	bg_inotbl;	/* inode table */
	uint16_t	bg_nfblk;	/* # of free blocks */
	uint16_t	bg_nfino;	/* # of free inodes */
	uint16_t	bg_ndirs;	/* # of dirs */
	char		bg_pad[14];
};

struct ext2dinode {
	uint16_t	di_mode;	/* mode */
	uint16_t	di_uid;		/* uid */
	uint32_t	di_size;	/* byte size */
	uint32_t	di_atime;	/* access time */
	uint32_t	di_ctime;	/* creation time */
	uint32_t	di_mtime;	/* modification time */
	uint32_t	di_dtime;	/* deletion time */
	uint16_t	di_gid;		/* gid */
	uint16_t	di_nlink;	/* link count */
	uint32_t	di_nblk;	/* block count */
	uint32_t	di_flags;	/* file flags */

	uint32_t	di_osdep1;	/* os dependent stuff */

	uint32_t	di_db[EXT2_NDADDR]; /* direct blocks */
	uint32_t	di_ib[EXT2_NIADDR]; /* indirect blocks */
	uint32_t	di_version;	/* version */
	uint32_t	di_facl;	/* file acl */
	uint32_t	di_dacl;	/* dir acl */
	uint32_t	di_faddr;	/* fragment addr */

	uint8_t		di_frag;	/* fragment number */
	uint8_t		di_fsize;	/* fragment size */

	char		di_pad[10];

#define di_shortlink	di_db
};

#define EXT2_MAXNAMLEN       255

struct ext2dirent {
	uint32_t	d_ino;		/* inode */
	uint16_t	d_reclen;	/* directory entry length */
	uint8_t		d_namlen;	/* name length */
	uint8_t		d_type;		/* file type */
	char		d_name[EXT2_MAXNAMLEN];
};

struct file {
	off_t		f_seekp;		/* seek pointer */
	struct 		ext2fs *f_fs;		/* pointer to super-block */
	struct 		ext2blkgrp *f_bg;	/* pointer to blkgrp map */
	struct 		ext2dinode f_di;	/* copy of on-disk inode */
	int		f_nindir[EXT2_NIADDR];	/* number of blocks mapped by
						   indirect block at level i */
	char		*f_blk[EXT2_NIADDR];	/* buffer for indirect block
						   at level i */
	size_t		f_blksize[EXT2_NIADDR];	/* size of buffer */
	daddr_t		f_blkno[EXT2_NIADDR];	/* disk address of block in
						   buffer */
	char		*f_buf;			/* buffer for data block */
	size_t		f_buf_size;		/* size of data block */
	daddr_t		f_buf_blkno;		/* block number of data block */
};

/* forward decls */
static int 	read_inode(ino_t inumber, struct open_file *f);
static int	block_map(struct open_file *f, daddr_t file_block,
		    daddr_t *disk_block_p);
static int	buf_read_file(struct open_file *f, char **buf_p,
		    size_t *size_p);
static int	search_directory(char *name, struct open_file *f,
		    ino_t *inumber_p);

/*
 * Open a file.
 */
static int
ext2fs_open(const char *upath, struct open_file *f)
{
	struct file *fp;
	struct ext2fs *fs;
	size_t buf_size;
	ino_t inumber, parent_inumber;
	int i, len, groups, bg_per_blk, blkgrps, mult;
	int nlinks = 0;
	int error = 0;
	char *cp, *ncp, *path = NULL, *buf = NULL;
	char namebuf[MAXPATHLEN+1];
	char c;

	/* allocate file system specific data structure */
	fp = malloc(sizeof(struct file));
	if (fp == NULL)
		return (ENOMEM);
	bzero(fp, sizeof(struct file));
	f->f_fsdata = (void *)fp;

	/* allocate space and read super block */
	fs = (struct ext2fs *)malloc(sizeof(*fs));
	fp->f_fs = fs;
	twiddle(1);
	error = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
	    EXT2_SBLOCK, EXT2_SBSIZE, (char *)fs, &buf_size);
	if (error)
		goto out;

	if (buf_size != EXT2_SBSIZE || fs->fs_magic != EXT2_MAGIC) {
		error = EINVAL;
		goto out;
	}

	/*
	 * compute in-core values for the superblock
	 */
	fs->fs_bshift = EXT2_MINBSHIFT + fs->fs_fd.fd_bsize;
	fs->fs_bsize = 1 << fs->fs_bshift;
	fs->fs_bmask = fs->fs_bsize - 1;

	fs->fs_fshift = EXT2_MINFSHIFT + fs->fs_fd.fd_fsize;
	fs->fs_fsize = 1 << fs->fs_fshift;
	fs->fs_fmask = fs->fs_fsize - 1;

	if (fs->fs_revision == EXT2_REV0) {
		fs->fs_isize = EXT2_R0_ISIZE;
		fs->fs_firstino = EXT2_R0_FIRSTINO;
	} else {
		fs->fs_isize = fs->fs_fd.fd_isize;
		fs->fs_firstino = fs->fs_fd.fd_firstino;
	}
	fs->fs_imask = fs->fs_isize - 1;
	fs->fs_ipb = fs->fs_bsize / fs->fs_isize;
	fs->fs_fsbtodb = (fs->fs_bsize / DEV_BSIZE) - 1;

	/*
	 * we have to load in the "group descriptors" here
	 */
	groups = howmany(fs->fs_blocks - fs->fs_firstblk, fs->fs_bpg);
	bg_per_blk = fs->fs_bsize / sizeof(struct ext2blkgrp);
	blkgrps = howmany(groups, bg_per_blk);
	len = blkgrps * fs->fs_bsize;

	fp->f_bg = malloc(len);
	twiddle(1);
	error = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
	    EXT2_SBLOCK + EXT2_SBSIZE / DEV_BSIZE, len,
	    (char *)fp->f_bg, &buf_size);
	if (error)
		goto out;

	/*
	 * XXX
	 * validation of values?  (blocksize, descriptors, etc?)
	 */

	/*
	 * Calculate indirect block levels.
	 */
	mult = 1;
	for (i = 0; i < EXT2_NIADDR; i++) {
		mult *= nindir(fs);
		fp->f_nindir[i] = mult;
	}

	inumber = EXT2_ROOTINO;
	if ((error = read_inode(inumber, f)) != 0)
		goto out;

	path = strdup(upath);
	if (path == NULL) {
		error = ENOMEM;
		goto out;
	}
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
		if (! S_ISDIR(fp->f_di.di_mode)) {
			error = ENOTDIR;
			goto out;
		}

		/*
		 * Get next component of path name.
		 */
		len = 0;

		ncp = cp;
		while ((c = *cp) != '\0' && c != '/') {
			if (++len > EXT2_MAXNAMLEN) {
				error = ENOENT;
				goto out;
			}
			cp++;
		}
		*cp = '\0';

		/*
		 * Look up component in current directory.
		 * Save directory inumber in case we find a
		 * symbolic link.
		 */
		parent_inumber = inumber;
		error = search_directory(ncp, f, &inumber);
		*cp = c;
		if (error)
			goto out;

		/*
		 * Open next component.
		 */
		if ((error = read_inode(inumber, f)) != 0)
			goto out;

		/*
		 * Check for symbolic link.
		 */
		if (S_ISLNK(fp->f_di.di_mode)) {
			int link_len = fp->f_di.di_size;
			int len;

			len = strlen(cp);
			if (link_len + len > MAXPATHLEN ||
			    ++nlinks > MAXSYMLINKS) {
				error = ENOENT;
				goto out;
			}

			bcopy(cp, &namebuf[link_len], len + 1);
			if (fp->f_di.di_nblk == 0) {
				bcopy(fp->f_di.di_shortlink,
				    namebuf, link_len);
			} else {
				/*
				 * Read file for symbolic link
				 */
				struct ext2fs *fs = fp->f_fs;
				daddr_t	disk_block;
				size_t buf_size;

				if (! buf)
					buf = malloc(fs->fs_bsize);
				error = block_map(f, (daddr_t)0, &disk_block);
				if (error)
					goto out;
				
				twiddle(1);
				error = (f->f_dev->dv_strategy)(f->f_devdata,
				    F_READ, fsb_to_db(fs, disk_block),
				    fs->fs_bsize, buf, &buf_size);
				if (error)
					goto out;

				bcopy((char *)buf, namebuf, link_len);
			}

			/*
			 * If relative pathname, restart at parent directory.
			 * If absolute pathname, restart at root.
			 */
			cp = namebuf;
			if (*cp != '/')
				inumber = parent_inumber;
			else
				inumber = (ino_t)EXT2_ROOTINO;

			if ((error = read_inode(inumber, f)) != 0)
				goto out;
		}
	}

	/*
	 * Found terminal component.
	 */
	error = 0;
	fp->f_seekp = 0;
out:
	if (buf)
		free(buf);
	if (path)
		free(path);
	if (error) {
		if (fp->f_buf)
			free(fp->f_buf);
		free(fp->f_fs);
		free(fp);
	}
	return (error);
}

/*
 * Read a new inode into a file structure.
 */
static int
read_inode(ino_t inumber, struct open_file *f)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct ext2fs *fs = fp->f_fs;
	struct ext2dinode *dp;
	char *buf;
	size_t rsize;
	int level, error = 0;

	/*
	 * Read inode and save it.
	 */
	buf = malloc(fs->fs_bsize);
	twiddle(1);
	error = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
	    ino_to_db(fs, fp->f_bg, inumber), fs->fs_bsize, buf, &rsize);
	if (error)
		goto out;
	if (rsize != fs->fs_bsize) {
		error = EIO;
		goto out;
	}

	dp = (struct ext2dinode *)buf;
	fp->f_di = dp[ino_to_bo(fs, inumber)];

	/* clear out old buffers */
	for (level = 0; level < EXT2_NIADDR; level++)
		fp->f_blkno[level] = -1;
	fp->f_buf_blkno = -1;
	fp->f_seekp = 0;

out:
	free(buf);
	return (error);	 
}

/*
 * Given an offset in a file, find the disk block number that
 * contains that block.
 */
static int
block_map(struct open_file *f, daddr_t file_block, daddr_t *disk_block_p)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct ext2fs *fs = fp->f_fs;
	daddr_t ind_block_num;
	int32_t *ind_p;
	int idx, level;
	int error;

	/*
	 * Index structure of an inode:
	 *
	 * di_db[0..EXT2_NDADDR-1] hold block numbers for blocks
	 *			0..EXT2_NDADDR-1
	 *
	 * di_ib[0]		index block 0 is the single indirect block
	 *			holds block numbers for blocks
	 *			EXT2_NDADDR .. EXT2_NDADDR + NINDIR(fs)-1
	 *
	 * di_ib[1]		index block 1 is the double indirect block
	 *			holds block numbers for INDEX blocks for blocks
	 *			EXT2_NDADDR + NINDIR(fs) ..
	 *			EXT2_NDADDR + NINDIR(fs) + NINDIR(fs)**2 - 1
	 *
	 * di_ib[2]		index block 2 is the triple indirect block
	 *			holds block numbers for double-indirect
	 *			blocks for blocks
	 *			EXT2_NDADDR + NINDIR(fs) + NINDIR(fs)**2 ..
	 *			EXT2_NDADDR + NINDIR(fs) + NINDIR(fs)**2
	 *				+ NINDIR(fs)**3 - 1
	 */

	if (file_block < EXT2_NDADDR) {
		/* Direct block. */
		*disk_block_p = fp->f_di.di_db[file_block];
		return (0);
	}

	file_block -= EXT2_NDADDR;

	/*
	 * nindir[0] = NINDIR
	 * nindir[1] = NINDIR**2
	 * nindir[2] = NINDIR**3
	 *	etc
	 */
	for (level = 0; level < EXT2_NIADDR; level++) {
		if (file_block < fp->f_nindir[level])
			break;
		file_block -= fp->f_nindir[level];
	}
	if (level == EXT2_NIADDR) {
		/* Block number too high */
		return (EFBIG);
	}

	ind_block_num = fp->f_di.di_ib[level];

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
			error = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
			    fsb_to_db(fp->f_fs, ind_block_num), fs->fs_bsize,
			    fp->f_blk[level], &fp->f_blksize[level]);
			if (error)
				return (error);
			if (fp->f_blksize[level] != fs->fs_bsize)
				return (EIO);
			fp->f_blkno[level] = ind_block_num;
		}

		ind_p = (int32_t *)fp->f_blk[level];

		if (level > 0) {
			idx = file_block / fp->f_nindir[level - 1];
			file_block %= fp->f_nindir[level - 1];
		} else {
			idx = file_block;
		}
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
	struct ext2fs *fs = fp->f_fs;
	long off;
	daddr_t file_block;
	daddr_t	disk_block;
	size_t block_size;
	int error = 0;

	off = blkoff(fs, fp->f_seekp);
	file_block = lblkno(fs, fp->f_seekp);
	block_size = dblksize(fs, &fp->f_di, file_block);

	if (file_block != fp->f_buf_blkno) {
		error = block_map(f, file_block, &disk_block);
		if (error)
			goto done;

		if (fp->f_buf == (char *)0)
			fp->f_buf = malloc(fs->fs_bsize);

		if (disk_block == 0) {
			bzero(fp->f_buf, block_size);
			fp->f_buf_size = block_size;
		} else {
			twiddle(4);
			error = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
			    fsb_to_db(fs, disk_block), block_size,
			    fp->f_buf, &fp->f_buf_size);
			if (error)
				goto done;
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
	if (*size_p > fp->f_di.di_size - fp->f_seekp)
		*size_p = fp->f_di.di_size - fp->f_seekp;
done:
	return (error);
}

/*
 * Search a directory for a name and return its
 * i_number.
 */
static int
search_directory(char *name, struct open_file *f, ino_t *inumber_p)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct ext2dirent *dp, *edp;
	char *buf;
	size_t buf_size;
	int namlen, length;
	int error;

	length = strlen(name);
	fp->f_seekp = 0;
	while (fp->f_seekp < fp->f_di.di_size) {
		error = buf_read_file(f, &buf, &buf_size);
		if (error)
			return (error);
		dp = (struct ext2dirent *)buf;
		edp = (struct ext2dirent *)(buf + buf_size);
		while (dp < edp) {
			if (dp->d_ino == (ino_t)0)
				goto next;
			namlen = dp->d_namlen;
			if (namlen == length &&
			    strncmp(name, dp->d_name, length) == 0) {
				/* found entry */
				*inumber_p = dp->d_ino;
				return (0);
			}
		next:
			dp = (struct ext2dirent *)((char *)dp + dp->d_reclen);
		}
		fp->f_seekp += buf_size;
	}
	return (ENOENT);
}

static int
ext2fs_close(struct open_file *f)
{
	struct file *fp = (struct file *)f->f_fsdata;
	int level;

	f->f_fsdata = (void *)0;
	if (fp == (struct file *)0)
		return (0);

	for (level = 0; level < EXT2_NIADDR; level++) {
		if (fp->f_blk[level])
			free(fp->f_blk[level]);
	}
	if (fp->f_buf)
		free(fp->f_buf);
	if (fp->f_bg)
		free(fp->f_bg);
	free(fp->f_fs);
	free(fp);
	return (0);
}

static int
ext2fs_read(struct open_file *f, void *addr, size_t size, size_t *resid)
{
	struct file *fp = (struct file *)f->f_fsdata;
	size_t csize, buf_size;
	char *buf;
	int error = 0;

	while (size != 0) {
		if (fp->f_seekp >= fp->f_di.di_size)
			break;

		error = buf_read_file(f, &buf, &buf_size);
		if (error)
			break;

		csize = size;
		if (csize > buf_size)
			csize = buf_size;

		bcopy(buf, addr, csize);

		fp->f_seekp += csize;
		addr = (char *)addr + csize;
		size -= csize;
	}
	if (resid)
		*resid = size;
	return (error);
}

static off_t
ext2fs_seek(struct open_file *f, off_t offset, int where)
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
		fp->f_seekp = fp->f_di.di_size - offset;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}
	return (fp->f_seekp);
}

static int
ext2fs_stat(struct open_file *f, struct stat *sb)
{
	struct file *fp = (struct file *)f->f_fsdata;

	/* only important stuff */
	sb->st_mode = fp->f_di.di_mode;
	sb->st_uid = fp->f_di.di_uid;
	sb->st_gid = fp->f_di.di_gid;
	sb->st_size = fp->f_di.di_size;
	return (0);
}

static int
ext2fs_readdir(struct open_file *f, struct dirent *d)
{
	struct file *fp = (struct file *)f->f_fsdata;
	struct ext2dirent *ed;
	char *buf;
	size_t buf_size;
	int error;

	/*
	 * assume that a directory entry will not be split across blocks
	 */
again:
	if (fp->f_seekp >= fp->f_di.di_size)
		return (ENOENT);
	error = buf_read_file(f, &buf, &buf_size);
	if (error)
		return (error);
	ed = (struct ext2dirent *)buf;
	fp->f_seekp += ed->d_reclen;
	if (ed->d_ino == (ino_t)0)
		goto again;
	d->d_type = EXTFTODT(ed->d_type);
	strncpy(d->d_name, ed->d_name, ed->d_namlen);
	d->d_name[ed->d_namlen] = '\0';
	return (0);
}
