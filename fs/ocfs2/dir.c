/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir.c
 *
 * Creates, reads, walks and deletes directory-nodes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 *  Portions of this code from linux/fs/ext3/dir.c
 *
 *  Copyright (C) 1992, 1993, 1994, 1995
 *  Remy Card (card@masi.ibp.fr)
 *  Laboratoire MASI - Institut Blaise pascal
 *  Universite Pierre et Marie Curie (Paris VI)
 *
 *   from
 *
 *   linux/fs/minix/dir.c
 *
 *   Copyright (C) 1991, 1992 Linux Torvalds
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>

#define MLOG_MASK_PREFIX ML_NAMEI
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dir.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "namei.h"
#include "suballoc.h"
#include "super.h"
#include "uptodate.h"

#include "buffer_head_io.h"

#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

static unsigned char ocfs2_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int ocfs2_extend_dir(struct ocfs2_super *osb,
			    struct inode *dir,
			    struct buffer_head *parent_fe_bh,
			    struct buffer_head **new_de_bh);
static int ocfs2_do_extend_dir(struct super_block *sb,
			       handle_t *handle,
			       struct inode *dir,
			       struct buffer_head *parent_fe_bh,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       struct buffer_head **new_bh);

int ocfs2_check_dir_entry(struct inode * dir,
			  struct ocfs2_dir_entry * de,
			  struct buffer_head * bh,
			  unsigned long offset)
{
	const char *error_msg = NULL;
	const int rlen = le16_to_cpu(de->rec_len);

	if (rlen < OCFS2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (rlen % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (rlen < OCFS2_DIR_REC_LEN(de->name_len))
		error_msg = "rec_len is too small for name_len";
	else if (((char *) de - bh->b_data) + rlen > dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";

	if (error_msg != NULL)
		mlog(ML_ERROR, "bad entry in directory #%llu: %s - "
		     "offset=%lu, inode=%llu, rec_len=%d, name_len=%d\n",
		     (unsigned long long)OCFS2_I(dir)->ip_blkno, error_msg,
		     offset, (unsigned long long)le64_to_cpu(de->inode), rlen,
		     de->name_len);
	return error_msg == NULL ? 1 : 0;
}

static inline int ocfs2_match(int len,
			      const char * const name,
			      struct ocfs2_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 * Returns 0 if not found, -1 on failure, and 1 on success
 */
static int inline ocfs2_search_dirblock(struct buffer_head *bh,
					struct inode *dir,
					const char *name, int namelen,
					unsigned long offset,
					struct ocfs2_dir_entry **res_dir)
{
	struct ocfs2_dir_entry *de;
	char *dlimit, *de_buf;
	int de_len;
	int ret = 0;

	mlog_entry_void();

	de_buf = bh->b_data;
	dlimit = de_buf + dir->i_sb->s_blocksize;

	while (de_buf < dlimit) {
		/* this code is executed quadratically often */
		/* do minimal checking `by hand' */

		de = (struct ocfs2_dir_entry *) de_buf;

		if (de_buf + namelen <= dlimit &&
		    ocfs2_match(namelen, name, de)) {
			/* found a match - just to be sure, do a full check */
			if (!ocfs2_check_dir_entry(dir, de, bh, offset)) {
				ret = -1;
				goto bail;
			}
			*res_dir = de;
			ret = 1;
			goto bail;
		}

		/* prevent looping on a bad block */
		de_len = le16_to_cpu(de->rec_len);
		if (de_len <= 0) {
			ret = -1;
			goto bail;
		}

		de_buf += de_len;
		offset += de_len;
	}

bail:
	mlog_exit(ret);
	return ret;
}

struct buffer_head *ocfs2_find_entry(const char *name, int namelen,
				     struct inode *dir,
				     struct ocfs2_dir_entry **res_dir)
{
	struct super_block *sb;
	struct buffer_head *bh_use[NAMEI_RA_SIZE];
	struct buffer_head *bh, *ret = NULL;
	unsigned long start, block, b;
	int ra_max = 0;		/* Number of bh's in the readahead
				   buffer, bh_use[] */
	int ra_ptr = 0;		/* Current index into readahead
				   buffer */
	int num = 0;
	int nblocks, i, err;

	mlog_entry_void();

	*res_dir = NULL;
	sb = dir->i_sb;

	nblocks = i_size_read(dir) >> sb->s_blocksize_bits;
	start = OCFS2_I(dir)->ip_dir_start_lookup;
	if (start >= nblocks)
		start = 0;
	block = start;

restart:
	do {
		/*
		 * We deal with the read-ahead logic here.
		 */
		if (ra_ptr >= ra_max) {
			/* Refill the readahead buffer */
			ra_ptr = 0;
			b = block;
			for (ra_max = 0; ra_max < NAMEI_RA_SIZE; ra_max++) {
				/*
				 * Terminate if we reach the end of the
				 * directory and must wrap, or if our
				 * search has finished at this block.
				 */
				if (b >= nblocks || (num && block == start)) {
					bh_use[ra_max] = NULL;
					break;
				}
				num++;

				bh = ocfs2_bread(dir, b++, &err, 1);
				bh_use[ra_max] = bh;
			}
		}
		if ((bh = bh_use[ra_ptr++]) == NULL)
			goto next;
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			/* read error, skip block & hope for the best */
			ocfs2_error(dir->i_sb, "reading directory %llu, "
				    "offset %lu\n",
				    (unsigned long long)OCFS2_I(dir)->ip_blkno,
				    block);
			brelse(bh);
			goto next;
		}
		i = ocfs2_search_dirblock(bh, dir, name, namelen,
					  block << sb->s_blocksize_bits,
					  res_dir);
		if (i == 1) {
			OCFS2_I(dir)->ip_dir_start_lookup = block;
			ret = bh;
			goto cleanup_and_exit;
		} else {
			brelse(bh);
			if (i < 0)
				goto cleanup_and_exit;
		}
	next:
		if (++block >= nblocks)
			block = 0;
	} while (block != start);

	/*
	 * If the directory has grown while we were searching, then
	 * search the last part of the directory before giving up.
	 */
	block = nblocks;
	nblocks = i_size_read(dir) >> sb->s_blocksize_bits;
	if (block < nblocks) {
		start = 0;
		goto restart;
	}

cleanup_and_exit:
	/* Clean up the read-ahead blocks */
	for (; ra_ptr < ra_max; ra_ptr++)
		brelse(bh_use[ra_ptr]);

	mlog_exit_ptr(ret);
	return ret;
}

/*
 * ocfs2_delete_entry deletes a directory entry by merging it with the
 * previous entry
 */
int ocfs2_delete_entry(handle_t *handle,
		       struct inode *dir,
		       struct ocfs2_dir_entry *de_del,
		       struct buffer_head *bh)
{
	struct ocfs2_dir_entry *de, *pde;
	int i, status = -ENOENT;

	mlog_entry("(0x%p, 0x%p, 0x%p, 0x%p)\n", handle, dir, de_del, bh);

	i = 0;
	pde = NULL;
	de = (struct ocfs2_dir_entry *) bh->b_data;
	while (i < bh->b_size) {
		if (!ocfs2_check_dir_entry(dir, de, bh, i)) {
			status = -EIO;
			mlog_errno(status);
			goto bail;
		}
		if (de == de_del)  {
			status = ocfs2_journal_access(handle, dir, bh,
						      OCFS2_JOURNAL_ACCESS_WRITE);
			if (status < 0) {
				status = -EIO;
				mlog_errno(status);
				goto bail;
			}
			if (pde)
				pde->rec_len =
					cpu_to_le16(le16_to_cpu(pde->rec_len) +
						    le16_to_cpu(de->rec_len));
			else
				de->inode = 0;
			dir->i_version++;
			status = ocfs2_journal_dirty(handle, bh);
			goto bail;
		}
		i += le16_to_cpu(de->rec_len);
		pde = de;
		de = (struct ocfs2_dir_entry *)((char *)de + le16_to_cpu(de->rec_len));
	}
bail:
	mlog_exit(status);
	return status;
}

/* we don't always have a dentry for what we want to add, so people
 * like orphan dir can call this instead.
 *
 * If you pass me insert_bh, I'll skip the search of the other dir
 * blocks and put the record in there.
 */
int __ocfs2_add_entry(handle_t *handle,
		      struct inode *dir,
		      const char *name, int namelen,
		      struct inode *inode, u64 blkno,
		      struct buffer_head *parent_fe_bh,
		      struct buffer_head *insert_bh)
{
	unsigned long offset;
	unsigned short rec_len;
	struct ocfs2_dir_entry *de, *de1;
	struct super_block *sb;
	int retval, status;

	mlog_entry_void();

	sb = dir->i_sb;

	if (!namelen)
		return -EINVAL;

	rec_len = OCFS2_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ocfs2_dir_entry *) insert_bh->b_data;
	while (1) {
		BUG_ON((char *)de >= sb->s_blocksize + insert_bh->b_data);
		/* These checks should've already been passed by the
		 * prepare function, but I guess we can leave them
		 * here anyway. */
		if (!ocfs2_check_dir_entry(dir, de, insert_bh, offset)) {
			retval = -ENOENT;
			goto bail;
		}
		if (ocfs2_match(namelen, name, de)) {
			retval = -EEXIST;
			goto bail;
		}
		if (((le64_to_cpu(de->inode) == 0) &&
		     (le16_to_cpu(de->rec_len) >= rec_len)) ||
		    (le16_to_cpu(de->rec_len) >=
		     (OCFS2_DIR_REC_LEN(de->name_len) + rec_len))) {
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			retval = ocfs2_mark_inode_dirty(handle, dir, parent_fe_bh);
			if (retval < 0) {
				mlog_errno(retval);
				goto bail;
			}

			status = ocfs2_journal_access(handle, dir, insert_bh,
						      OCFS2_JOURNAL_ACCESS_WRITE);
			/* By now the buffer is marked for journaling */
			offset += le16_to_cpu(de->rec_len);
			if (le64_to_cpu(de->inode)) {
				de1 = (struct ocfs2_dir_entry *)((char *) de +
					OCFS2_DIR_REC_LEN(de->name_len));
				de1->rec_len =
					cpu_to_le16(le16_to_cpu(de->rec_len) -
					OCFS2_DIR_REC_LEN(de->name_len));
				de->rec_len = cpu_to_le16(OCFS2_DIR_REC_LEN(de->name_len));
				de = de1;
			}
			de->file_type = OCFS2_FT_UNKNOWN;
			if (blkno) {
				de->inode = cpu_to_le64(blkno);
				ocfs2_set_de_type(de, inode->i_mode);
			} else
				de->inode = 0;
			de->name_len = namelen;
			memcpy(de->name, name, namelen);

			dir->i_version++;
			status = ocfs2_journal_dirty(handle, insert_bh);
			retval = 0;
			goto bail;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *) ((char *) de + le16_to_cpu(de->rec_len));
	}

	/* when you think about it, the assert above should prevent us
	 * from ever getting here. */
	retval = -ENOSPC;
bail:

	mlog_exit(retval);
	return retval;
}

static int ocfs2_dir_foreach_blk(struct inode *inode, unsigned long *f_version,
				 loff_t *f_pos, void *priv, filldir_t filldir)
{
	int error = 0;
	unsigned long offset, blk, last_ra_blk = 0;
	int i, stored;
	struct buffer_head * bh, * tmp;
	struct ocfs2_dir_entry * de;
	int err;
	struct super_block * sb = inode->i_sb;
	unsigned int ra_sectors = 16;

	stored = 0;
	bh = NULL;

	offset = (*f_pos) & (sb->s_blocksize - 1);

	while (!error && !stored && *f_pos < i_size_read(inode)) {
		blk = (*f_pos) >> sb->s_blocksize_bits;
		bh = ocfs2_bread(inode, blk, &err, 0);
		if (!bh) {
			mlog(ML_ERROR,
			     "directory #%llu contains a hole at offset %lld\n",
			     (unsigned long long)OCFS2_I(inode)->ip_blkno,
			     *f_pos);
			*f_pos += sb->s_blocksize - offset;
			continue;
		}

		/* The idea here is to begin with 8k read-ahead and to stay
		 * 4k ahead of our current position.
		 *
		 * TODO: Use the pagecache for this. We just need to
		 * make sure it's cluster-safe... */
		if (!last_ra_blk
		    || (((last_ra_blk - blk) << 9) <= (ra_sectors / 2))) {
			for (i = ra_sectors >> (sb->s_blocksize_bits - 9);
			     i > 0; i--) {
				tmp = ocfs2_bread(inode, ++blk, &err, 1);
				if (tmp)
					brelse(tmp);
			}
			last_ra_blk = blk;
			ra_sectors = 8;
		}

revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (*f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ocfs2_dir_entry *) (bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				if (le16_to_cpu(de->rec_len) <
				    OCFS2_DIR_REC_LEN(1))
					break;
				i += le16_to_cpu(de->rec_len);
			}
			offset = i;
			*f_pos = ((*f_pos) & ~(sb->s_blocksize - 1))
				| offset;
			*f_version = inode->i_version;
		}

		while (!error && *f_pos < i_size_read(inode)
		       && offset < sb->s_blocksize) {
			de = (struct ocfs2_dir_entry *) (bh->b_data + offset);
			if (!ocfs2_check_dir_entry(inode, de, bh, offset)) {
				/* On error, skip the f_pos to the
				   next block. */
				*f_pos = ((*f_pos) | (sb->s_blocksize - 1)) + 1;
				brelse(bh);
				goto out;
			}
			offset += le16_to_cpu(de->rec_len);
			if (le64_to_cpu(de->inode)) {
				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation.
				 */
				unsigned long version = *f_version;
				unsigned char d_type = DT_UNKNOWN;

				if (de->file_type < OCFS2_FT_MAX)
					d_type = ocfs2_filetype_table[de->file_type];
				error = filldir(priv, de->name,
						de->name_len,
						*f_pos,
						ino_from_blkno(sb, le64_to_cpu(de->inode)),
						d_type);
				if (error)
					break;
				if (version != *f_version)
					goto revalidate;
				stored ++;
			}
			*f_pos += le16_to_cpu(de->rec_len);
		}
		offset = 0;
		brelse(bh);
	}

	stored = 0;
out:
	return stored;
}

/*
 * ocfs2_readdir()
 *
 */
int ocfs2_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	int error = 0;
	struct inode *inode = filp->f_path.dentry->d_inode;
	int lock_level = 0;

	mlog_entry("dirino=%llu\n",
		   (unsigned long long)OCFS2_I(inode)->ip_blkno);

	error = ocfs2_meta_lock_atime(inode, filp->f_vfsmnt, &lock_level);
	if (lock_level && error >= 0) {
		/* We release EX lock which used to update atime
		 * and get PR lock again to reduce contention
		 * on commonly accessed directories. */
		ocfs2_meta_unlock(inode, 1);
		lock_level = 0;
		error = ocfs2_meta_lock(inode, NULL, 0);
	}
	if (error < 0) {
		if (error != -ENOENT)
			mlog_errno(error);
		/* we haven't got any yet, so propagate the error. */
		goto bail_nolock;
	}

	error = ocfs2_dir_foreach_blk(inode, &filp->f_version, &filp->f_pos,
				      dirent, filldir);

	ocfs2_meta_unlock(inode, lock_level);

bail_nolock:
	mlog_exit(error);

	return error;
}

/*
 * NOTE: this should always be called with parent dir i_mutex taken.
 */
int ocfs2_find_files_on_disk(const char *name,
			     int namelen,
			     u64 *blkno,
			     struct inode *inode,
			     struct buffer_head **dirent_bh,
			     struct ocfs2_dir_entry **dirent)
{
	int status = -ENOENT;

	mlog_entry("(name=%.*s, blkno=%p, inode=%p, dirent_bh=%p, dirent=%p)\n",
		   namelen, name, blkno, inode, dirent_bh, dirent);

	*dirent_bh = ocfs2_find_entry(name, namelen, inode, dirent);
	if (!*dirent_bh || !*dirent) {
		status = -ENOENT;
		goto leave;
	}

	*blkno = le64_to_cpu((*dirent)->inode);

	status = 0;
leave:
	if (status < 0) {
		*dirent = NULL;
		if (*dirent_bh) {
			brelse(*dirent_bh);
			*dirent_bh = NULL;
		}
	}

	mlog_exit(status);
	return status;
}

/* Check for a name within a directory.
 *
 * Return 0 if the name does not exist
 * Return -EEXIST if the directory contains the name
 *
 * Callers should have i_mutex + a cluster lock on dir
 */
int ocfs2_check_dir_for_entry(struct inode *dir,
			      const char *name,
			      int namelen)
{
	int ret;
	struct buffer_head *dirent_bh = NULL;
	struct ocfs2_dir_entry *dirent = NULL;

	mlog_entry("dir %llu, name '%.*s'\n",
		   (unsigned long long)OCFS2_I(dir)->ip_blkno, namelen, name);

	ret = -EEXIST;
	dirent_bh = ocfs2_find_entry(name, namelen, dir, &dirent);
	if (dirent_bh)
		goto bail;

	ret = 0;
bail:
	if (dirent_bh)
		brelse(dirent_bh);

	mlog_exit(ret);
	return ret;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int ocfs2_empty_dir(struct inode *inode)
{
	unsigned long offset;
	struct buffer_head * bh;
	struct ocfs2_dir_entry * de, * de1;
	struct super_block * sb;
	int err;

	sb = inode->i_sb;
	if ((i_size_read(inode) <
	     (OCFS2_DIR_REC_LEN(1) + OCFS2_DIR_REC_LEN(2))) ||
	    !(bh = ocfs2_bread(inode, 0, &err, 0))) {
	    	mlog(ML_ERROR, "bad directory (dir #%llu) - no data block\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		return 1;
	}

	de = (struct ocfs2_dir_entry *) bh->b_data;
	de1 = (struct ocfs2_dir_entry *)
			((char *)de + le16_to_cpu(de->rec_len));
	if ((le64_to_cpu(de->inode) != OCFS2_I(inode)->ip_blkno) ||
			!le64_to_cpu(de1->inode) ||
			strcmp(".", de->name) ||
			strcmp("..", de1->name)) {
	    	mlog(ML_ERROR, "bad directory (dir #%llu) - no `.' or `..'\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		brelse(bh);
		return 1;
	}
	offset = le16_to_cpu(de->rec_len) + le16_to_cpu(de1->rec_len);
	de = (struct ocfs2_dir_entry *)((char *)de1 + le16_to_cpu(de1->rec_len));
	while (offset < i_size_read(inode) ) {
		if (!bh || (void *)de >= (void *)(bh->b_data + sb->s_blocksize)) {
			brelse(bh);
			bh = ocfs2_bread(inode,
					 offset >> sb->s_blocksize_bits, &err, 0);
			if (!bh) {
				mlog(ML_ERROR, "dir %llu has a hole at %lu\n",
				     (unsigned long long)OCFS2_I(inode)->ip_blkno, offset);
				offset += sb->s_blocksize;
				continue;
			}
			de = (struct ocfs2_dir_entry *) bh->b_data;
		}
		if (!ocfs2_check_dir_entry(inode, de, bh, offset)) {
			brelse(bh);
			return 1;
		}
		if (le64_to_cpu(de->inode)) {
			brelse(bh);
			return 0;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *)
			((char *)de + le16_to_cpu(de->rec_len));
	}
	brelse(bh);
	return 1;
}

int ocfs2_fill_new_dir(struct ocfs2_super *osb,
		       handle_t *handle,
		       struct inode *parent,
		       struct inode *inode,
		       struct buffer_head *fe_bh,
		       struct ocfs2_alloc_context *data_ac)
{
	int status;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry *de = NULL;

	mlog_entry_void();

	status = ocfs2_do_extend_dir(osb->sb, handle, inode, fe_bh,
				     data_ac, NULL, &new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_set_new_buffer_uptodate(inode, new_bh);

	status = ocfs2_journal_access(handle, inode, new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	memset(new_bh->b_data, 0, osb->sb->s_blocksize);

	de = (struct ocfs2_dir_entry *) new_bh->b_data;
	de->inode = cpu_to_le64(OCFS2_I(inode)->ip_blkno);
	de->name_len = 1;
	de->rec_len =
		cpu_to_le16(OCFS2_DIR_REC_LEN(de->name_len));
	strcpy(de->name, ".");
	ocfs2_set_de_type(de, S_IFDIR);
	de = (struct ocfs2_dir_entry *) ((char *)de + le16_to_cpu(de->rec_len));
	de->inode = cpu_to_le64(OCFS2_I(parent)->ip_blkno);
	de->rec_len = cpu_to_le16(inode->i_sb->s_blocksize -
				  OCFS2_DIR_REC_LEN(1));
	de->name_len = 2;
	strcpy(de->name, "..");
	ocfs2_set_de_type(de, S_IFDIR);

	status = ocfs2_journal_dirty(handle, new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	i_size_write(inode, inode->i_sb->s_blocksize);
	inode->i_nlink = 2;
	inode->i_blocks = ocfs2_inode_sector_count(inode);
	status = ocfs2_mark_inode_dirty(handle, inode, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	if (new_bh)
		brelse(new_bh);

	mlog_exit(status);
	return status;
}

/* returns a bh of the 1st new block in the allocation. */
static int ocfs2_do_extend_dir(struct super_block *sb,
			       handle_t *handle,
			       struct inode *dir,
			       struct buffer_head *parent_fe_bh,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       struct buffer_head **new_bh)
{
	int status;
	int extend;
	u64 p_blkno, v_blkno;

	spin_lock(&OCFS2_I(dir)->ip_lock);
	extend = (i_size_read(dir) == ocfs2_clusters_to_bytes(sb, OCFS2_I(dir)->ip_clusters));
	spin_unlock(&OCFS2_I(dir)->ip_lock);

	if (extend) {
		u32 offset = OCFS2_I(dir)->ip_clusters;

		status = ocfs2_do_extend_allocation(OCFS2_SB(sb), dir, &offset,
						    1, 0, parent_fe_bh, handle,
						    data_ac, meta_ac, NULL);
		BUG_ON(status == -EAGAIN);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	v_blkno = ocfs2_blocks_for_bytes(sb, i_size_read(dir));
	status = ocfs2_extent_map_get_blocks(dir, v_blkno, &p_blkno, NULL, NULL);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	*new_bh = sb_getblk(sb, p_blkno);
	if (!*new_bh) {
		status = -EIO;
		mlog_errno(status);
		goto bail;
	}
	status = 0;
bail:
	mlog_exit(status);
	return status;
}

/* assumes you already have a cluster lock on the directory. */
static int ocfs2_extend_dir(struct ocfs2_super *osb,
			    struct inode *dir,
			    struct buffer_head *parent_fe_bh,
			    struct buffer_head **new_de_bh)
{
	int status = 0;
	int credits, num_free_extents, drop_alloc_sem = 0;
	loff_t dir_i_size;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) parent_fe_bh->b_data;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	handle_t *handle = NULL;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry * de;
	struct super_block *sb = osb->sb;

	mlog_entry_void();

	dir_i_size = i_size_read(dir);
	mlog(0, "extending dir %llu (i_size = %lld)\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno, dir_i_size);

	/* dir->i_size is always block aligned. */
	spin_lock(&OCFS2_I(dir)->ip_lock);
	if (dir_i_size == ocfs2_clusters_to_bytes(sb, OCFS2_I(dir)->ip_clusters)) {
		spin_unlock(&OCFS2_I(dir)->ip_lock);
		num_free_extents = ocfs2_num_free_extents(osb, dir, fe);
		if (num_free_extents < 0) {
			status = num_free_extents;
			mlog_errno(status);
			goto bail;
		}

		if (!num_free_extents) {
			status = ocfs2_reserve_new_metadata(osb, fe, &meta_ac);
			if (status < 0) {
				if (status != -ENOSPC)
					mlog_errno(status);
				goto bail;
			}
		}

		status = ocfs2_reserve_clusters(osb, 1, &data_ac);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto bail;
		}

		credits = ocfs2_calc_extend_credits(sb, fe, 1);
	} else {
		spin_unlock(&OCFS2_I(dir)->ip_lock);
		credits = OCFS2_SIMPLE_DIR_EXTEND_CREDITS;
	}

	down_write(&OCFS2_I(dir)->ip_alloc_sem);
	drop_alloc_sem = 1;

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_do_extend_dir(osb->sb, handle, dir, parent_fe_bh,
				     data_ac, meta_ac, &new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_set_new_buffer_uptodate(dir, new_bh);

	status = ocfs2_journal_access(handle, dir, new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	memset(new_bh->b_data, 0, sb->s_blocksize);
	de = (struct ocfs2_dir_entry *) new_bh->b_data;
	de->inode = 0;
	de->rec_len = cpu_to_le16(sb->s_blocksize);
	status = ocfs2_journal_dirty(handle, new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	dir_i_size += dir->i_sb->s_blocksize;
	i_size_write(dir, dir_i_size);
	dir->i_blocks = ocfs2_inode_sector_count(dir);
	status = ocfs2_mark_inode_dirty(handle, dir, parent_fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	*new_de_bh = new_bh;
	get_bh(*new_de_bh);
bail:
	if (drop_alloc_sem)
		up_write(&OCFS2_I(dir)->ip_alloc_sem);
	if (handle)
		ocfs2_commit_trans(osb, handle);

	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	if (new_bh)
		brelse(new_bh);

	mlog_exit(status);
	return status;
}

/*
 * Search the dir for a good spot, extending it if necessary. The
 * block containing an appropriate record is returned in ret_de_bh.
 */
int ocfs2_prepare_dir_for_insert(struct ocfs2_super *osb,
				 struct inode *dir,
				 struct buffer_head *parent_fe_bh,
				 const char *name,
				 int namelen,
				 struct buffer_head **ret_de_bh)
{
	unsigned long offset;
	struct buffer_head * bh = NULL;
	unsigned short rec_len;
	struct ocfs2_dinode *fe;
	struct ocfs2_dir_entry *de;
	struct super_block *sb;
	int status;

	mlog_entry_void();

	mlog(0, "getting ready to insert namelen %d into dir %llu\n",
	     namelen, (unsigned long long)OCFS2_I(dir)->ip_blkno);

	BUG_ON(!S_ISDIR(dir->i_mode));
	fe = (struct ocfs2_dinode *) parent_fe_bh->b_data;
	BUG_ON(le64_to_cpu(fe->i_size) != i_size_read(dir));

	sb = dir->i_sb;

	if (!namelen) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	bh = ocfs2_bread(dir, 0, &status, 0);
	if (!bh) {
		mlog_errno(status);
		goto bail;
	}

	rec_len = OCFS2_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ocfs2_dir_entry *) bh->b_data;
	while (1) {
		if ((char *)de >= sb->s_blocksize + bh->b_data) {
			brelse(bh);
			bh = NULL;

			if (i_size_read(dir) <= offset) {
				status = ocfs2_extend_dir(osb,
							  dir,
							  parent_fe_bh,
							  &bh);
				if (status < 0) {
					mlog_errno(status);
					goto bail;
				}
				BUG_ON(!bh);
				*ret_de_bh = bh;
				get_bh(*ret_de_bh);
				goto bail;
			}
			bh = ocfs2_bread(dir,
					 offset >> sb->s_blocksize_bits,
					 &status,
					 0);
			if (!bh) {
				mlog_errno(status);
				goto bail;
			}
			/* move to next block */
			de = (struct ocfs2_dir_entry *) bh->b_data;
		}
		if (!ocfs2_check_dir_entry(dir, de, bh, offset)) {
			status = -ENOENT;
			goto bail;
		}
		if (ocfs2_match(namelen, name, de)) {
			status = -EEXIST;
			goto bail;
		}
		if (((le64_to_cpu(de->inode) == 0) &&
		     (le16_to_cpu(de->rec_len) >= rec_len)) ||
		    (le16_to_cpu(de->rec_len) >=
		     (OCFS2_DIR_REC_LEN(de->name_len) + rec_len))) {
			/* Ok, we found a spot. Return this bh and let
			 * the caller actually fill it in. */
			*ret_de_bh = bh;
			get_bh(*ret_de_bh);
			status = 0;
			goto bail;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *)((char *) de + le16_to_cpu(de->rec_len));
	}

	status = 0;
bail:
	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}
