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
#include <linux/quotaops.h>

#define MLOG_MASK_PREFIX ML_NAMEI
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "blockcheck.h"
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
			    unsigned int blocks_wanted,
			    struct buffer_head **new_de_bh);
static int ocfs2_do_extend_dir(struct super_block *sb,
			       handle_t *handle,
			       struct inode *dir,
			       struct buffer_head *parent_fe_bh,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       struct buffer_head **new_bh);

/*
 * These are distinct checks because future versions of the file system will
 * want to have a trailing dirent structure independent of indexing.
 */
static int ocfs2_dir_has_trailer(struct inode *dir)
{
	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	return ocfs2_meta_ecc(OCFS2_SB(dir->i_sb));
}

static int ocfs2_supports_dir_trailer(struct ocfs2_super *osb)
{
	return ocfs2_meta_ecc(osb);
}

static inline unsigned int ocfs2_dir_trailer_blk_off(struct super_block *sb)
{
	return sb->s_blocksize - sizeof(struct ocfs2_dir_block_trailer);
}

#define ocfs2_trailer_from_bh(_bh, _sb) ((struct ocfs2_dir_block_trailer *) ((_bh)->b_data + ocfs2_dir_trailer_blk_off((_sb))))

/* XXX ocfs2_block_dqtrailer() is similar but not quite - can we make
 * them more consistent? */
struct ocfs2_dir_block_trailer *ocfs2_dir_trailer_from_size(int blocksize,
							    void *data)
{
	char *p = data;

	p += blocksize - sizeof(struct ocfs2_dir_block_trailer);
	return (struct ocfs2_dir_block_trailer *)p;
}

/*
 * XXX: This is executed once on every dirent. We should consider optimizing
 * it.
 */
static int ocfs2_skip_dir_trailer(struct inode *dir,
				  struct ocfs2_dir_entry *de,
				  unsigned long offset,
				  unsigned long blklen)
{
	unsigned long toff = blklen - sizeof(struct ocfs2_dir_block_trailer);

	if (!ocfs2_dir_has_trailer(dir))
		return 0;

	if (offset != toff)
		return 0;

	return 1;
}

static void ocfs2_init_dir_trailer(struct inode *inode,
				   struct buffer_head *bh)
{
	struct ocfs2_dir_block_trailer *trailer;

	trailer = ocfs2_trailer_from_bh(bh, inode->i_sb);
	strcpy(trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE);
	trailer->db_compat_rec_len =
			cpu_to_le16(sizeof(struct ocfs2_dir_block_trailer));
	trailer->db_parent_dinode = cpu_to_le64(OCFS2_I(inode)->ip_blkno);
	trailer->db_blkno = cpu_to_le64(bh->b_blocknr);
}

/*
 * bh passed here can be an inode block or a dir data block, depending
 * on the inode inline data flag.
 */
static int ocfs2_check_dir_entry(struct inode * dir,
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
					char *first_de,
					unsigned int bytes,
					struct ocfs2_dir_entry **res_dir)
{
	struct ocfs2_dir_entry *de;
	char *dlimit, *de_buf;
	int de_len;
	int ret = 0;

	mlog_entry_void();

	de_buf = first_de;
	dlimit = de_buf + bytes;

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

static struct buffer_head *ocfs2_find_entry_id(const char *name,
					       int namelen,
					       struct inode *dir,
					       struct ocfs2_dir_entry **res_dir)
{
	int ret, found;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_inline_data *data;

	ret = ocfs2_read_inode_block(dir, &di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;
	data = &di->id2.i_data;

	found = ocfs2_search_dirblock(di_bh, dir, name, namelen, 0,
				      data->id_data, i_size_read(dir), res_dir);
	if (found == 1)
		return di_bh;

	brelse(di_bh);
out:
	return NULL;
}

static int ocfs2_validate_dir_block(struct super_block *sb,
				    struct buffer_head *bh)
{
	int rc;
	struct ocfs2_dir_block_trailer *trailer =
		ocfs2_trailer_from_bh(bh, sb);


	/*
	 * We don't validate dirents here, that's handled
	 * in-place when the code walks them.
	 */
	mlog(0, "Validating dirblock %llu\n",
	     (unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We know any error is
	 * local to this block.
	 *
	 * Note that we are safe to call this even if the directory
	 * doesn't have a trailer.  Filesystems without metaecc will do
	 * nothing, and filesystems with it will have one.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &trailer->db_check);
	if (rc)
		mlog(ML_ERROR, "Checksum failed for dinode %llu\n",
		     (unsigned long long)bh->b_blocknr);

	return rc;
}

/*
 * This function forces all errors to -EIO for consistency with its
 * predecessor, ocfs2_bread().  We haven't audited what returning the
 * real error codes would do to callers.  We log the real codes with
 * mlog_errno() before we squash them.
 */
static int ocfs2_read_dir_block(struct inode *inode, u64 v_block,
				struct buffer_head **bh, int flags)
{
	int rc = 0;
	struct buffer_head *tmp = *bh;
	struct ocfs2_dir_block_trailer *trailer;

	rc = ocfs2_read_virt_blocks(inode, v_block, 1, &tmp, flags,
				    ocfs2_validate_dir_block);
	if (rc) {
		mlog_errno(rc);
		goto out;
	}

	/*
	 * We check the trailer here rather than in
	 * ocfs2_validate_dir_block() because that function doesn't have
	 * the inode to test.
	 */
	if (!(flags & OCFS2_BH_READAHEAD) &&
	    ocfs2_dir_has_trailer(inode)) {
		trailer = ocfs2_trailer_from_bh(tmp, inode->i_sb);
		if (!OCFS2_IS_VALID_DIR_TRAILER(trailer)) {
			rc = -EINVAL;
			ocfs2_error(inode->i_sb,
				    "Invalid dirblock #%llu: "
				    "signature = %.*s\n",
				    (unsigned long long)tmp->b_blocknr, 7,
				    trailer->db_signature);
			goto out;
		}
		if (le64_to_cpu(trailer->db_blkno) != tmp->b_blocknr) {
			rc = -EINVAL;
			ocfs2_error(inode->i_sb,
				    "Directory block #%llu has an invalid "
				    "db_blkno of %llu",
				    (unsigned long long)tmp->b_blocknr,
				    (unsigned long long)le64_to_cpu(trailer->db_blkno));
			goto out;
		}
		if (le64_to_cpu(trailer->db_parent_dinode) !=
		    OCFS2_I(inode)->ip_blkno) {
			rc = -EINVAL;
			ocfs2_error(inode->i_sb,
				    "Directory block #%llu on dinode "
				    "#%llu has an invalid parent_dinode "
				    "of %llu",
				    (unsigned long long)tmp->b_blocknr,
				    (unsigned long long)OCFS2_I(inode)->ip_blkno,
				    (unsigned long long)le64_to_cpu(trailer->db_blkno));
			goto out;
		}
	}

	/* If ocfs2_read_virt_blocks() got us a new bh, pass it up. */
	if (!*bh)
		*bh = tmp;

out:
	return rc ? -EIO : 0;
}

static struct buffer_head *ocfs2_find_entry_el(const char *name, int namelen,
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

				bh = NULL;
				err = ocfs2_read_dir_block(dir, b++, &bh,
							   OCFS2_BH_READAHEAD);
				bh_use[ra_max] = bh;
			}
		}
		if ((bh = bh_use[ra_ptr++]) == NULL)
			goto next;
		if (ocfs2_read_dir_block(dir, block, &bh, 0)) {
			/* read error, skip block & hope for the best.
			 * ocfs2_read_dir_block() has released the bh. */
			ocfs2_error(dir->i_sb, "reading directory %llu, "
				    "offset %lu\n",
				    (unsigned long long)OCFS2_I(dir)->ip_blkno,
				    block);
			goto next;
		}
		i = ocfs2_search_dirblock(bh, dir, name, namelen,
					  block << sb->s_blocksize_bits,
					  bh->b_data, sb->s_blocksize,
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
 * Try to find an entry of the provided name within 'dir'.
 *
 * If nothing was found, NULL is returned. Otherwise, a buffer_head
 * and pointer to the dir entry are passed back.
 *
 * Caller can NOT assume anything about the contents of the
 * buffer_head - it is passed back only so that it can be passed into
 * any one of the manipulation functions (add entry, delete entry,
 * etc). As an example, bh in the extent directory case is a data
 * block, in the inline-data case it actually points to an inode.
 */
struct buffer_head *ocfs2_find_entry(const char *name, int namelen,
				     struct inode *dir,
				     struct ocfs2_dir_entry **res_dir)
{
	*res_dir = NULL;

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_find_entry_id(name, namelen, dir, res_dir);

	return ocfs2_find_entry_el(name, namelen, dir, res_dir);
}

/*
 * Update inode number and type of a previously found directory entry.
 */
int ocfs2_update_entry(struct inode *dir, handle_t *handle,
		       struct buffer_head *de_bh, struct ocfs2_dir_entry *de,
		       struct inode *new_entry_inode)
{
	int ret;
	ocfs2_journal_access_func access = ocfs2_journal_access_db;

	/*
	 * The same code works fine for both inline-data and extent
	 * based directories, so no need to split this up.  The only
	 * difference is the journal_access function.
	 */

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		access = ocfs2_journal_access_di;

	ret = access(handle, dir, de_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	de->inode = cpu_to_le64(OCFS2_I(new_entry_inode)->ip_blkno);
	ocfs2_set_de_type(de, new_entry_inode->i_mode);

	ocfs2_journal_dirty(handle, de_bh);

out:
	return ret;
}

static int __ocfs2_delete_entry(handle_t *handle, struct inode *dir,
				struct ocfs2_dir_entry *de_del,
				struct buffer_head *bh, char *first_de,
				unsigned int bytes)
{
	struct ocfs2_dir_entry *de, *pde;
	int i, status = -ENOENT;
	ocfs2_journal_access_func access = ocfs2_journal_access_db;

	mlog_entry("(0x%p, 0x%p, 0x%p, 0x%p)\n", handle, dir, de_del, bh);

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		access = ocfs2_journal_access_di;

	i = 0;
	pde = NULL;
	de = (struct ocfs2_dir_entry *) first_de;
	while (i < bytes) {
		if (!ocfs2_check_dir_entry(dir, de, bh, i)) {
			status = -EIO;
			mlog_errno(status);
			goto bail;
		}
		if (de == de_del)  {
			status = access(handle, dir, bh,
					OCFS2_JOURNAL_ACCESS_WRITE);
			if (status < 0) {
				status = -EIO;
				mlog_errno(status);
				goto bail;
			}
			if (pde)
				le16_add_cpu(&pde->rec_len,
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

static inline int ocfs2_delete_entry_id(handle_t *handle,
					struct inode *dir,
					struct ocfs2_dir_entry *de_del,
					struct buffer_head *bh)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_inline_data *data;

	ret = ocfs2_read_inode_block(dir, &di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;
	data = &di->id2.i_data;

	ret = __ocfs2_delete_entry(handle, dir, de_del, bh, data->id_data,
				   i_size_read(dir));

	brelse(di_bh);
out:
	return ret;
}

static inline int ocfs2_delete_entry_el(handle_t *handle,
					struct inode *dir,
					struct ocfs2_dir_entry *de_del,
					struct buffer_head *bh)
{
	return __ocfs2_delete_entry(handle, dir, de_del, bh, bh->b_data,
				    bh->b_size);
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
	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_delete_entry_id(handle, dir, de_del, bh);

	return ocfs2_delete_entry_el(handle, dir, de_del, bh);
}

/*
 * Check whether 'de' has enough room to hold an entry of
 * 'new_rec_len' bytes.
 */
static inline int ocfs2_dirent_would_fit(struct ocfs2_dir_entry *de,
					 unsigned int new_rec_len)
{
	unsigned int de_really_used;

	/* Check whether this is an empty record with enough space */
	if (le64_to_cpu(de->inode) == 0 &&
	    le16_to_cpu(de->rec_len) >= new_rec_len)
		return 1;

	/*
	 * Record might have free space at the end which we can
	 * use.
	 */
	de_really_used = OCFS2_DIR_REC_LEN(de->name_len);
	if (le16_to_cpu(de->rec_len) >= (de_really_used + new_rec_len))
	    return 1;

	return 0;
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
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)parent_fe_bh->b_data;
	struct super_block *sb = dir->i_sb;
	int retval, status;
	unsigned int size = sb->s_blocksize;
	char *data_start = insert_bh->b_data;

	mlog_entry_void();

	if (!namelen)
		return -EINVAL;

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		data_start = di->id2.i_data.id_data;
		size = i_size_read(dir);

		BUG_ON(insert_bh != parent_fe_bh);
	}

	rec_len = OCFS2_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ocfs2_dir_entry *) data_start;
	while (1) {
		BUG_ON((char *)de >= (size + data_start));

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

		/* We're guaranteed that we should have space, so we
		 * can't possibly have hit the trailer...right? */
		mlog_bug_on_msg(ocfs2_skip_dir_trailer(dir, de, offset, size),
				"Hit dir trailer trying to insert %.*s "
			        "(namelen %d) into directory %llu.  "
				"offset is %lu, trailer offset is %d\n",
				namelen, name, namelen,
				(unsigned long long)parent_fe_bh->b_blocknr,
				offset, ocfs2_dir_trailer_blk_off(dir->i_sb));

		if (ocfs2_dirent_would_fit(de, rec_len)) {
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			retval = ocfs2_mark_inode_dirty(handle, dir, parent_fe_bh);
			if (retval < 0) {
				mlog_errno(retval);
				goto bail;
			}

			if (insert_bh == parent_fe_bh)
				status = ocfs2_journal_access_di(handle, dir,
								 insert_bh,
								 OCFS2_JOURNAL_ACCESS_WRITE);
			else
				status = ocfs2_journal_access_db(handle, dir,
								 insert_bh,
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

static int ocfs2_dir_foreach_blk_id(struct inode *inode,
				    u64 *f_version,
				    loff_t *f_pos, void *priv,
				    filldir_t filldir, int *filldir_err)
{
	int ret, i, filldir_ret;
	unsigned long offset = *f_pos;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_inline_data *data;
	struct ocfs2_dir_entry *de;

	ret = ocfs2_read_inode_block(inode, &di_bh);
	if (ret) {
		mlog(ML_ERROR, "Unable to read inode block for dir %llu\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		goto out;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;
	data = &di->id2.i_data;

	while (*f_pos < i_size_read(inode)) {
revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (*f_version != inode->i_version) {
			for (i = 0; i < i_size_read(inode) && i < offset; ) {
				de = (struct ocfs2_dir_entry *)
					(data->id_data + i);
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
			*f_pos = offset = i;
			*f_version = inode->i_version;
		}

		de = (struct ocfs2_dir_entry *) (data->id_data + *f_pos);
		if (!ocfs2_check_dir_entry(inode, de, di_bh, *f_pos)) {
			/* On error, skip the f_pos to the end. */
			*f_pos = i_size_read(inode);
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
			u64 version = *f_version;
			unsigned char d_type = DT_UNKNOWN;

			if (de->file_type < OCFS2_FT_MAX)
				d_type = ocfs2_filetype_table[de->file_type];

			filldir_ret = filldir(priv, de->name,
					      de->name_len,
					      *f_pos,
					      le64_to_cpu(de->inode),
					      d_type);
			if (filldir_ret) {
				if (filldir_err)
					*filldir_err = filldir_ret;
				break;
			}
			if (version != *f_version)
				goto revalidate;
		}
		*f_pos += le16_to_cpu(de->rec_len);
	}

out:
	brelse(di_bh);

	return 0;
}

static int ocfs2_dir_foreach_blk_el(struct inode *inode,
				    u64 *f_version,
				    loff_t *f_pos, void *priv,
				    filldir_t filldir, int *filldir_err)
{
	int error = 0;
	unsigned long offset, blk, last_ra_blk = 0;
	int i, stored;
	struct buffer_head * bh, * tmp;
	struct ocfs2_dir_entry * de;
	struct super_block * sb = inode->i_sb;
	unsigned int ra_sectors = 16;

	stored = 0;
	bh = NULL;

	offset = (*f_pos) & (sb->s_blocksize - 1);

	while (!error && !stored && *f_pos < i_size_read(inode)) {
		blk = (*f_pos) >> sb->s_blocksize_bits;
		if (ocfs2_read_dir_block(inode, blk, &bh, 0)) {
			/* Skip the corrupt dirblock and keep trying */
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
				tmp = NULL;
				if (!ocfs2_read_dir_block(inode, ++blk, &tmp,
							  OCFS2_BH_READAHEAD))
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
						le64_to_cpu(de->inode),
						d_type);
				if (error) {
					if (filldir_err)
						*filldir_err = error;
					break;
				}
				if (version != *f_version)
					goto revalidate;
				stored ++;
			}
			*f_pos += le16_to_cpu(de->rec_len);
		}
		offset = 0;
		brelse(bh);
		bh = NULL;
	}

	stored = 0;
out:
	return stored;
}

static int ocfs2_dir_foreach_blk(struct inode *inode, u64 *f_version,
				 loff_t *f_pos, void *priv, filldir_t filldir,
				 int *filldir_err)
{
	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_dir_foreach_blk_id(inode, f_version, f_pos, priv,
						filldir, filldir_err);

	return ocfs2_dir_foreach_blk_el(inode, f_version, f_pos, priv, filldir,
					filldir_err);
}

/*
 * This is intended to be called from inside other kernel functions,
 * so we fake some arguments.
 */
int ocfs2_dir_foreach(struct inode *inode, loff_t *f_pos, void *priv,
		      filldir_t filldir)
{
	int ret = 0, filldir_err = 0;
	u64 version = inode->i_version;

	while (*f_pos < i_size_read(inode)) {
		ret = ocfs2_dir_foreach_blk(inode, &version, f_pos, priv,
					    filldir, &filldir_err);
		if (ret || filldir_err)
			break;
	}

	if (ret > 0)
		ret = -EIO;

	return 0;
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

	error = ocfs2_inode_lock_atime(inode, filp->f_vfsmnt, &lock_level);
	if (lock_level && error >= 0) {
		/* We release EX lock which used to update atime
		 * and get PR lock again to reduce contention
		 * on commonly accessed directories. */
		ocfs2_inode_unlock(inode, 1);
		lock_level = 0;
		error = ocfs2_inode_lock(inode, NULL, 0);
	}
	if (error < 0) {
		if (error != -ENOENT)
			mlog_errno(error);
		/* we haven't got any yet, so propagate the error. */
		goto bail_nolock;
	}

	error = ocfs2_dir_foreach_blk(inode, &filp->f_version, &filp->f_pos,
				      dirent, filldir, NULL);

	ocfs2_inode_unlock(inode, lock_level);

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
		brelse(*dirent_bh);
		*dirent_bh = NULL;
	}

	mlog_exit(status);
	return status;
}

/*
 * Convenience function for callers which just want the block number
 * mapped to a name and don't require the full dirent info, etc.
 */
int ocfs2_lookup_ino_from_name(struct inode *dir, const char *name,
			       int namelen, u64 *blkno)
{
	int ret;
	struct buffer_head *bh = NULL;
	struct ocfs2_dir_entry *dirent = NULL;

	ret = ocfs2_find_files_on_disk(name, namelen, blkno, dir, &bh, &dirent);
	brelse(bh);

	return ret;
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
	brelse(dirent_bh);

	mlog_exit(ret);
	return ret;
}

struct ocfs2_empty_dir_priv {
	unsigned seen_dot;
	unsigned seen_dot_dot;
	unsigned seen_other;
};
static int ocfs2_empty_dir_filldir(void *priv, const char *name, int name_len,
				   loff_t pos, u64 ino, unsigned type)
{
	struct ocfs2_empty_dir_priv *p = priv;

	/*
	 * Check the positions of "." and ".." records to be sure
	 * they're in the correct place.
	 */
	if (name_len == 1 && !strncmp(".", name, 1) && pos == 0) {
		p->seen_dot = 1;
		return 0;
	}

	if (name_len == 2 && !strncmp("..", name, 2) &&
	    pos == OCFS2_DIR_REC_LEN(1)) {
		p->seen_dot_dot = 1;
		return 0;
	}

	p->seen_other = 1;
	return 1;
}
/*
 * routine to check that the specified directory is empty (for rmdir)
 *
 * Returns 1 if dir is empty, zero otherwise.
 */
int ocfs2_empty_dir(struct inode *inode)
{
	int ret;
	loff_t start = 0;
	struct ocfs2_empty_dir_priv priv;

	memset(&priv, 0, sizeof(priv));

	ret = ocfs2_dir_foreach(inode, &start, &priv, ocfs2_empty_dir_filldir);
	if (ret)
		mlog_errno(ret);

	if (!priv.seen_dot || !priv.seen_dot_dot) {
		mlog(ML_ERROR, "bad directory (dir #%llu) - no `.' or `..'\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		/*
		 * XXX: Is it really safe to allow an unlink to continue?
		 */
		return 1;
	}

	return !priv.seen_other;
}

/*
 * Fills "." and ".." dirents in a new directory block. Returns dirent for
 * "..", which might be used during creation of a directory with a trailing
 * header. It is otherwise safe to ignore the return code.
 */
static struct ocfs2_dir_entry *ocfs2_fill_initial_dirents(struct inode *inode,
							  struct inode *parent,
							  char *start,
							  unsigned int size)
{
	struct ocfs2_dir_entry *de = (struct ocfs2_dir_entry *)start;

	de->inode = cpu_to_le64(OCFS2_I(inode)->ip_blkno);
	de->name_len = 1;
	de->rec_len =
		cpu_to_le16(OCFS2_DIR_REC_LEN(de->name_len));
	strcpy(de->name, ".");
	ocfs2_set_de_type(de, S_IFDIR);

	de = (struct ocfs2_dir_entry *) ((char *)de + le16_to_cpu(de->rec_len));
	de->inode = cpu_to_le64(OCFS2_I(parent)->ip_blkno);
	de->rec_len = cpu_to_le16(size - OCFS2_DIR_REC_LEN(1));
	de->name_len = 2;
	strcpy(de->name, "..");
	ocfs2_set_de_type(de, S_IFDIR);

	return de;
}

/*
 * This works together with code in ocfs2_mknod_locked() which sets
 * the inline-data flag and initializes the inline-data section.
 */
static int ocfs2_fill_new_dir_id(struct ocfs2_super *osb,
				 handle_t *handle,
				 struct inode *parent,
				 struct inode *inode,
				 struct buffer_head *di_bh)
{
	int ret;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_inline_data *data = &di->id2.i_data;
	unsigned int size = le16_to_cpu(data->id_count);

	ret = ocfs2_journal_access_di(handle, inode, di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_fill_initial_dirents(inode, parent, data->id_data, size);

	ocfs2_journal_dirty(handle, di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	i_size_write(inode, size);
	inode->i_nlink = 2;
	inode->i_blocks = ocfs2_inode_sector_count(inode);

	ret = ocfs2_mark_inode_dirty(handle, inode, di_bh);
	if (ret < 0)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_fill_new_dir_el(struct ocfs2_super *osb,
				 handle_t *handle,
				 struct inode *parent,
				 struct inode *inode,
				 struct buffer_head *fe_bh,
				 struct ocfs2_alloc_context *data_ac)
{
	int status;
	unsigned int size = osb->sb->s_blocksize;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry *de;

	mlog_entry_void();

	if (ocfs2_supports_dir_trailer(osb))
		size = ocfs2_dir_trailer_blk_off(parent->i_sb);

	status = ocfs2_do_extend_dir(osb->sb, handle, inode, fe_bh,
				     data_ac, NULL, &new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_set_new_buffer_uptodate(inode, new_bh);

	status = ocfs2_journal_access_db(handle, inode, new_bh,
					 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	memset(new_bh->b_data, 0, osb->sb->s_blocksize);

	de = ocfs2_fill_initial_dirents(inode, parent, new_bh->b_data, size);
	if (ocfs2_supports_dir_trailer(osb))
		ocfs2_init_dir_trailer(inode, new_bh);

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
	brelse(new_bh);

	mlog_exit(status);
	return status;
}

int ocfs2_fill_new_dir(struct ocfs2_super *osb,
		       handle_t *handle,
		       struct inode *parent,
		       struct inode *inode,
		       struct buffer_head *fe_bh,
		       struct ocfs2_alloc_context *data_ac)
{
	BUG_ON(!ocfs2_supports_inline_data(osb) && data_ac == NULL);

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_fill_new_dir_id(osb, handle, parent, inode, fe_bh);

	return ocfs2_fill_new_dir_el(osb, handle, parent, inode, fe_bh,
				     data_ac);
}

/*
 * Expand rec_len of the rightmost dirent in a directory block so that it
 * contains the end of our valid space for dirents. We do this during
 * expansion from an inline directory to one with extents. The first dir block
 * in that case is taken from the inline data portion of the inode block.
 *
 * We add the dir trailer if this filesystem wants it.
 */
static void ocfs2_expand_last_dirent(char *start, unsigned int old_size,
				     struct super_block *sb)
{
	struct ocfs2_dir_entry *de;
	struct ocfs2_dir_entry *prev_de;
	char *de_buf, *limit;
	unsigned int new_size = sb->s_blocksize;
	unsigned int bytes;

	if (ocfs2_supports_dir_trailer(OCFS2_SB(sb)))
		new_size = ocfs2_dir_trailer_blk_off(sb);

	bytes = new_size - old_size;

	limit = start + old_size;
	de_buf = start;
	de = (struct ocfs2_dir_entry *)de_buf;
	do {
		prev_de = de;
		de_buf += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *)de_buf;
	} while (de_buf < limit);

	le16_add_cpu(&prev_de->rec_len, bytes);
}

/*
 * We allocate enough clusters to fulfill "blocks_wanted", but set
 * i_size to exactly one block. Ocfs2_extend_dir() will handle the
 * rest automatically for us.
 *
 * *first_block_bh is a pointer to the 1st data block allocated to the
 *  directory.
 */
static int ocfs2_expand_inline_dir(struct inode *dir, struct buffer_head *di_bh,
				   unsigned int blocks_wanted,
				   struct buffer_head **first_block_bh)
{
	u32 alloc, bit_off, len;
	struct super_block *sb = dir->i_sb;
	int ret, credits = ocfs2_inline_to_extents_credits(sb);
	u64 blkno, bytes = blocks_wanted << sb->s_blocksize_bits;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ocfs2_inode_info *oi = OCFS2_I(dir);
	struct ocfs2_alloc_context *data_ac;
	struct buffer_head *dirdata_bh = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	handle_t *handle;
	struct ocfs2_extent_tree et;
	int did_quota = 0;

	ocfs2_init_dinode_extent_tree(&et, dir, di_bh);

	alloc = ocfs2_clusters_for_bytes(sb, bytes);

	/*
	 * We should never need more than 2 clusters for this -
	 * maximum dirent size is far less than one block. In fact,
	 * the only time we'd need more than one cluster is if
	 * blocksize == clustersize and the dirent won't fit in the
	 * extra space that the expansion to a single block gives. As
	 * of today, that only happens on 4k/4k file systems.
	 */
	BUG_ON(alloc > 2);

	ret = ocfs2_reserve_clusters(osb, alloc, &data_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	down_write(&oi->ip_alloc_sem);

	/*
	 * Prepare for worst case allocation scenario of two separate
	 * extents.
	 */
	if (alloc == 2)
		credits += OCFS2_SUBALLOC_ALLOC;

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_sem;
	}

	if (vfs_dq_alloc_space_nodirty(dir,
				ocfs2_clusters_to_bytes(osb->sb, alloc))) {
		ret = -EDQUOT;
		goto out_commit;
	}
	did_quota = 1;
	/*
	 * Try to claim as many clusters as the bitmap can give though
	 * if we only get one now, that's enough to continue. The rest
	 * will be claimed after the conversion to extents.
	 */
	ret = ocfs2_claim_clusters(osb, handle, data_ac, 1, &bit_off, &len);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * Operations are carefully ordered so that we set up the new
	 * data block first. The conversion from inline data to
	 * extents follows.
	 */
	blkno = ocfs2_clusters_to_blocks(dir->i_sb, bit_off);
	dirdata_bh = sb_getblk(sb, blkno);
	if (!dirdata_bh) {
		ret = -EIO;
		mlog_errno(ret);
		goto out_commit;
	}

	ocfs2_set_new_buffer_uptodate(dir, dirdata_bh);

	ret = ocfs2_journal_access_db(handle, dir, dirdata_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	memcpy(dirdata_bh->b_data, di->id2.i_data.id_data, i_size_read(dir));
	memset(dirdata_bh->b_data + i_size_read(dir), 0,
	       sb->s_blocksize - i_size_read(dir));
	ocfs2_expand_last_dirent(dirdata_bh->b_data, i_size_read(dir), sb);
	if (ocfs2_supports_dir_trailer(osb))
		ocfs2_init_dir_trailer(dir, dirdata_bh);

	ret = ocfs2_journal_dirty(handle, dirdata_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * Set extent, i_size, etc on the directory. After this, the
	 * inode should contain the same exact dirents as before and
	 * be fully accessible from system calls.
	 *
	 * We let the later dirent insert modify c/mtime - to the user
	 * the data hasn't changed.
	 */
	ret = ocfs2_journal_access_di(handle, dir, di_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features &= ~OCFS2_INLINE_DATA_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);

	ocfs2_dinode_new_extent_list(dir, di);

	i_size_write(dir, sb->s_blocksize);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;

	di->i_size = cpu_to_le64(sb->s_blocksize);
	di->i_ctime = di->i_mtime = cpu_to_le64(dir->i_ctime.tv_sec);
	di->i_ctime_nsec = di->i_mtime_nsec = cpu_to_le32(dir->i_ctime.tv_nsec);

	/*
	 * This should never fail as our extent list is empty and all
	 * related blocks have been journaled already.
	 */
	ret = ocfs2_insert_extent(osb, handle, dir, &et, 0, blkno, len,
				  0, NULL);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * Set i_blocks after the extent insert for the most up to
	 * date ip_clusters value.
	 */
	dir->i_blocks = ocfs2_inode_sector_count(dir);

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * We asked for two clusters, but only got one in the 1st
	 * pass. Claim the 2nd cluster as a separate extent.
	 */
	if (alloc > len) {
		ret = ocfs2_claim_clusters(osb, handle, data_ac, 1, &bit_off,
					   &len);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
		blkno = ocfs2_clusters_to_blocks(dir->i_sb, bit_off);

		ret = ocfs2_insert_extent(osb, handle, dir, &et, 1,
					  blkno, len, 0, NULL);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
	}

	*first_block_bh = dirdata_bh;
	dirdata_bh = NULL;

out_commit:
	if (ret < 0 && did_quota)
		vfs_dq_free_space_nodirty(dir,
			ocfs2_clusters_to_bytes(osb->sb, 2));
	ocfs2_commit_trans(osb, handle);

out_sem:
	up_write(&oi->ip_alloc_sem);

out:
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);

	brelse(dirdata_bh);

	return ret;
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
	int extend, did_quota = 0;
	u64 p_blkno, v_blkno;

	spin_lock(&OCFS2_I(dir)->ip_lock);
	extend = (i_size_read(dir) == ocfs2_clusters_to_bytes(sb, OCFS2_I(dir)->ip_clusters));
	spin_unlock(&OCFS2_I(dir)->ip_lock);

	if (extend) {
		u32 offset = OCFS2_I(dir)->ip_clusters;

		if (vfs_dq_alloc_space_nodirty(dir,
					ocfs2_clusters_to_bytes(sb, 1))) {
			status = -EDQUOT;
			goto bail;
		}
		did_quota = 1;

		status = ocfs2_add_inode_data(OCFS2_SB(sb), dir, &offset,
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
	if (did_quota && status < 0)
		vfs_dq_free_space_nodirty(dir, ocfs2_clusters_to_bytes(sb, 1));
	mlog_exit(status);
	return status;
}

/*
 * Assumes you already have a cluster lock on the directory.
 *
 * 'blocks_wanted' is only used if we have an inline directory which
 * is to be turned into an extent based one. The size of the dirent to
 * insert might be larger than the space gained by growing to just one
 * block, so we may have to grow the inode by two blocks in that case.
 */
static int ocfs2_extend_dir(struct ocfs2_super *osb,
			    struct inode *dir,
			    struct buffer_head *parent_fe_bh,
			    unsigned int blocks_wanted,
			    struct buffer_head **new_de_bh)
{
	int status = 0;
	int credits, num_free_extents, drop_alloc_sem = 0;
	loff_t dir_i_size;
	struct ocfs2_dinode *fe = (struct ocfs2_dinode *) parent_fe_bh->b_data;
	struct ocfs2_extent_list *el = &fe->id2.i_list;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	handle_t *handle = NULL;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry * de;
	struct super_block *sb = osb->sb;
	struct ocfs2_extent_tree et;

	mlog_entry_void();

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		status = ocfs2_expand_inline_dir(dir, parent_fe_bh,
						 blocks_wanted, &new_bh);
		if (status) {
			mlog_errno(status);
			goto bail;
		}

		if (blocks_wanted == 1) {
			/*
			 * If the new dirent will fit inside the space
			 * created by pushing out to one block, then
			 * we can complete the operation
			 * here. Otherwise we have to expand i_size
			 * and format the 2nd block below.
			 */
			BUG_ON(new_bh == NULL);
			goto bail_bh;
		}

		/*
		 * Get rid of 'new_bh' - we want to format the 2nd
		 * data block and return that instead.
		 */
		brelse(new_bh);
		new_bh = NULL;

		dir_i_size = i_size_read(dir);
		credits = OCFS2_SIMPLE_DIR_EXTEND_CREDITS;
		goto do_extend;
	}

	dir_i_size = i_size_read(dir);
	mlog(0, "extending dir %llu (i_size = %lld)\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno, dir_i_size);

	/* dir->i_size is always block aligned. */
	spin_lock(&OCFS2_I(dir)->ip_lock);
	if (dir_i_size == ocfs2_clusters_to_bytes(sb, OCFS2_I(dir)->ip_clusters)) {
		spin_unlock(&OCFS2_I(dir)->ip_lock);
		ocfs2_init_dinode_extent_tree(&et, dir, parent_fe_bh);
		num_free_extents = ocfs2_num_free_extents(osb, dir, &et);
		if (num_free_extents < 0) {
			status = num_free_extents;
			mlog_errno(status);
			goto bail;
		}

		if (!num_free_extents) {
			status = ocfs2_reserve_new_metadata(osb, el, &meta_ac);
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

		credits = ocfs2_calc_extend_credits(sb, el, 1);
	} else {
		spin_unlock(&OCFS2_I(dir)->ip_lock);
		credits = OCFS2_SIMPLE_DIR_EXTEND_CREDITS;
	}

do_extend:
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

	status = ocfs2_journal_access_db(handle, dir, new_bh,
					 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	memset(new_bh->b_data, 0, sb->s_blocksize);

	de = (struct ocfs2_dir_entry *) new_bh->b_data;
	de->inode = 0;
	if (ocfs2_dir_has_trailer(dir)) {
		de->rec_len = cpu_to_le16(ocfs2_dir_trailer_blk_off(sb));
		ocfs2_init_dir_trailer(dir, new_bh);
	} else {
		de->rec_len = cpu_to_le16(sb->s_blocksize);
	}
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

bail_bh:
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

	brelse(new_bh);

	mlog_exit(status);
	return status;
}

static int ocfs2_find_dir_space_id(struct inode *dir, struct buffer_head *di_bh,
				   const char *name, int namelen,
				   struct buffer_head **ret_de_bh,
				   unsigned int *blocks_wanted)
{
	int ret;
	struct super_block *sb = dir->i_sb;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_dir_entry *de, *last_de = NULL;
	char *de_buf, *limit;
	unsigned long offset = 0;
	unsigned int rec_len, new_rec_len, free_space = dir->i_sb->s_blocksize;

	/*
	 * This calculates how many free bytes we'd have in block zero, should
	 * this function force expansion to an extent tree.
	 */
	if (ocfs2_supports_dir_trailer(OCFS2_SB(sb)))
		free_space = ocfs2_dir_trailer_blk_off(sb) - i_size_read(dir);
	else
		free_space = dir->i_sb->s_blocksize - i_size_read(dir);

	de_buf = di->id2.i_data.id_data;
	limit = de_buf + i_size_read(dir);
	rec_len = OCFS2_DIR_REC_LEN(namelen);

	while (de_buf < limit) {
		de = (struct ocfs2_dir_entry *)de_buf;

		if (!ocfs2_check_dir_entry(dir, de, di_bh, offset)) {
			ret = -ENOENT;
			goto out;
		}
		if (ocfs2_match(namelen, name, de)) {
			ret = -EEXIST;
			goto out;
		}
		/*
		 * No need to check for a trailing dirent record here as
		 * they're not used for inline dirs.
		 */

		if (ocfs2_dirent_would_fit(de, rec_len)) {
			/* Ok, we found a spot. Return this bh and let
			 * the caller actually fill it in. */
			*ret_de_bh = di_bh;
			get_bh(*ret_de_bh);
			ret = 0;
			goto out;
		}

		last_de = de;
		de_buf += le16_to_cpu(de->rec_len);
		offset += le16_to_cpu(de->rec_len);
	}

	/*
	 * We're going to require expansion of the directory - figure
	 * out how many blocks we'll need so that a place for the
	 * dirent can be found.
	 */
	*blocks_wanted = 1;
	new_rec_len = le16_to_cpu(last_de->rec_len) + free_space;
	if (new_rec_len < (rec_len + OCFS2_DIR_REC_LEN(last_de->name_len)))
		*blocks_wanted = 2;

	ret = -ENOSPC;
out:
	return ret;
}

static int ocfs2_find_dir_space_el(struct inode *dir, const char *name,
				   int namelen, struct buffer_head **ret_de_bh)
{
	unsigned long offset;
	struct buffer_head *bh = NULL;
	unsigned short rec_len;
	struct ocfs2_dir_entry *de;
	struct super_block *sb = dir->i_sb;
	int status;
	int blocksize = dir->i_sb->s_blocksize;

	status = ocfs2_read_dir_block(dir, 0, &bh, 0);
	if (status) {
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
				/*
				 * Caller will have to expand this
				 * directory.
				 */
				status = -ENOSPC;
				goto bail;
			}
			status = ocfs2_read_dir_block(dir,
					     offset >> sb->s_blocksize_bits,
					     &bh, 0);
			if (status) {
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

		if (ocfs2_skip_dir_trailer(dir, de, offset % blocksize,
					   blocksize))
			goto next;

		if (ocfs2_dirent_would_fit(de, rec_len)) {
			/* Ok, we found a spot. Return this bh and let
			 * the caller actually fill it in. */
			*ret_de_bh = bh;
			get_bh(*ret_de_bh);
			status = 0;
			goto bail;
		}
next:
		offset += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *)((char *) de + le16_to_cpu(de->rec_len));
	}

	status = 0;
bail:
	brelse(bh);

	mlog_exit(status);
	return status;
}

int ocfs2_prepare_dir_for_insert(struct ocfs2_super *osb,
				 struct inode *dir,
				 struct buffer_head *parent_fe_bh,
				 const char *name,
				 int namelen,
				 struct buffer_head **ret_de_bh)
{
	int ret;
	unsigned int blocks_wanted = 1;
	struct buffer_head *bh = NULL;

	mlog(0, "getting ready to insert namelen %d into dir %llu\n",
	     namelen, (unsigned long long)OCFS2_I(dir)->ip_blkno);

	*ret_de_bh = NULL;

	if (!namelen) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto out;
	}

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_find_dir_space_id(dir, parent_fe_bh, name,
					      namelen, &bh, &blocks_wanted);
	} else
		ret = ocfs2_find_dir_space_el(dir, name, namelen, &bh);

	if (ret && ret != -ENOSPC) {
		mlog_errno(ret);
		goto out;
	}

	if (ret == -ENOSPC) {
		/*
		 * We have to expand the directory to add this name.
		 */
		BUG_ON(bh);

		ret = ocfs2_extend_dir(osb, dir, parent_fe_bh, blocks_wanted,
				       &bh);
		if (ret) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
			goto out;
		}

		BUG_ON(!bh);
	}

	*ret_de_bh = bh;
	bh = NULL;
out:
	brelse(bh);
	return ret;
}
