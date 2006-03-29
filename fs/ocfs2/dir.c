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
#include "uptodate.h"

#include "buffer_head_io.h"

static unsigned char ocfs2_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int ocfs2_extend_dir(struct ocfs2_super *osb,
			    struct inode *dir,
			    struct buffer_head *parent_fe_bh,
			    struct buffer_head **new_de_bh);
/*
 * ocfs2_readdir()
 *
 */
int ocfs2_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	int error = 0;
	unsigned long offset, blk;
	int i, num, stored;
	struct buffer_head * bh, * tmp;
	struct ocfs2_dir_entry * de;
	int err;
	struct inode *inode = filp->f_dentry->d_inode;
	struct super_block * sb = inode->i_sb;
	int have_disk_lock = 0;

	mlog_entry("dirino=%llu\n",
		   (unsigned long long)OCFS2_I(inode)->ip_blkno);

	stored = 0;
	bh = NULL;

	error = ocfs2_meta_lock(inode, NULL, NULL, 0);
	if (error < 0) {
		if (error != -ENOENT)
			mlog_errno(error);
		/* we haven't got any yet, so propagate the error. */
		stored = error;
		goto bail;
	}
	have_disk_lock = 1;

	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < i_size_read(inode)) {
		blk = (filp->f_pos) >> sb->s_blocksize_bits;
		bh = ocfs2_bread(inode, blk, &err, 0);
		if (!bh) {
			mlog(ML_ERROR,
			     "directory #%llu contains a hole at offset %lld\n",
			     (unsigned long long)OCFS2_I(inode)->ip_blkno,
			     filp->f_pos);
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

		/*
		 * Do the readahead (8k)
		 */
		if (!offset) {
			for (i = 16 >> (sb->s_blocksize_bits - 9), num = 0;
			     i > 0; i--) {
				tmp = ocfs2_bread(inode, ++blk, &err, 1);
				if (tmp)
					brelse(tmp);
			}
		}

revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
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
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}

		while (!error && filp->f_pos < i_size_read(inode)
		       && offset < sb->s_blocksize) {
			de = (struct ocfs2_dir_entry *) (bh->b_data + offset);
			if (!ocfs2_check_dir_entry(inode, de, bh, offset)) {
				/* On error, skip the f_pos to the
				   next block. */
				filp->f_pos = (filp->f_pos |
					       (sb->s_blocksize - 1)) + 1;
				brelse(bh);
				goto bail;
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
				unsigned long version = filp->f_version;
				unsigned char d_type = DT_UNKNOWN;

				if (de->file_type < OCFS2_FT_MAX)
					d_type = ocfs2_filetype_table[de->file_type];
				error = filldir(dirent, de->name,
						de->name_len,
						filp->f_pos,
						ino_from_blkno(sb, le64_to_cpu(de->inode)),
						d_type);
				if (error)
					break;
				if (version != filp->f_version)
					goto revalidate;
				stored ++;
			}
			filp->f_pos += le16_to_cpu(de->rec_len);
		}
		offset = 0;
		brelse(bh);
	}

	stored = 0;
bail:
	if (have_disk_lock)
		ocfs2_meta_unlock(inode, 0);

	mlog_exit(stored);

	return stored;
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
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry("(osb=%p, parent=%llu, name='%.*s', blkno=%p, inode=%p)\n",
		   osb, (unsigned long long)OCFS2_I(inode)->ip_blkno,
		   namelen, name, blkno, inode);

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

/* returns a bh of the 1st new block in the allocation. */
int ocfs2_do_extend_dir(struct super_block *sb,
			struct ocfs2_journal_handle *handle,
			struct inode *dir,
			struct buffer_head *parent_fe_bh,
			struct ocfs2_alloc_context *data_ac,
			struct ocfs2_alloc_context *meta_ac,
			struct buffer_head **new_bh)
{
	int status;
	int extend;
	u64 p_blkno;

	spin_lock(&OCFS2_I(dir)->ip_lock);
	extend = (i_size_read(dir) == ocfs2_clusters_to_bytes(sb, OCFS2_I(dir)->ip_clusters));
	spin_unlock(&OCFS2_I(dir)->ip_lock);

	if (extend) {
		status = ocfs2_do_extend_allocation(OCFS2_SB(sb), dir, 1,
						    parent_fe_bh, handle,
						    data_ac, meta_ac, NULL);
		BUG_ON(status == -EAGAIN);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	status = ocfs2_extent_map_get_blocks(dir, (dir->i_blocks >>
						   (sb->s_blocksize_bits - 9)),
					     1, &p_blkno, NULL);
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
	int credits, num_free_extents;
	loff_t dir_i_size;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) parent_fe_bh->b_data;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct ocfs2_journal_handle *handle = NULL;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry * de;
	struct super_block *sb = osb->sb;

	mlog_entry_void();

	dir_i_size = i_size_read(dir);
	mlog(0, "extending dir %llu (i_size = %lld)\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno, dir_i_size);

	handle = ocfs2_alloc_handle(osb);
	if (handle == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

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
			status = ocfs2_reserve_new_metadata(osb, handle,
							    fe, &meta_ac);
			if (status < 0) {
				if (status != -ENOSPC)
					mlog_errno(status);
				goto bail;
			}
		}

		status = ocfs2_reserve_clusters(osb, handle, 1, &data_ac);
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

	handle = ocfs2_start_trans(osb, handle, credits);
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
	dir->i_blocks = ocfs2_align_bytes_to_sectors(dir_i_size);
	status = ocfs2_mark_inode_dirty(handle, dir, parent_fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	*new_de_bh = new_bh;
	get_bh(*new_de_bh);
bail:
	if (handle)
		ocfs2_commit_trans(handle);

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
