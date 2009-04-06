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
#include <linux/sort.h>

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
#include "sysfile.h"
#include "uptodate.h"

#include "buffer_head_io.h"

#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

static unsigned char ocfs2_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int ocfs2_do_extend_dir(struct super_block *sb,
			       handle_t *handle,
			       struct inode *dir,
			       struct buffer_head *parent_fe_bh,
			       struct ocfs2_alloc_context *data_ac,
			       struct ocfs2_alloc_context *meta_ac,
			       struct buffer_head **new_bh);
static int ocfs2_dir_indexed(struct inode *inode);

/*
 * These are distinct checks because future versions of the file system will
 * want to have a trailing dirent structure independent of indexing.
 */
static int ocfs2_supports_dir_trailer(struct inode *dir)
{
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	return ocfs2_meta_ecc(osb) || ocfs2_dir_indexed(dir);
}

/*
 * "new' here refers to the point at which we're creating a new
 * directory via "mkdir()", but also when we're expanding an inline
 * directory. In either case, we don't yet have the indexing bit set
 * on the directory, so the standard checks will fail in when metaecc
 * is turned off. Only directory-initialization type functions should
 * use this then. Everything else wants ocfs2_supports_dir_trailer()
 */
static int ocfs2_new_dir_wants_trailer(struct inode *dir)
{
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);

	return ocfs2_meta_ecc(osb) ||
		ocfs2_supports_indexed_dirs(osb);
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

	if (!ocfs2_supports_dir_trailer(dir))
		return 0;

	if (offset != toff)
		return 0;

	return 1;
}

static void ocfs2_init_dir_trailer(struct inode *inode,
				   struct buffer_head *bh, u16 rec_len)
{
	struct ocfs2_dir_block_trailer *trailer;

	trailer = ocfs2_trailer_from_bh(bh, inode->i_sb);
	strcpy(trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE);
	trailer->db_compat_rec_len =
			cpu_to_le16(sizeof(struct ocfs2_dir_block_trailer));
	trailer->db_parent_dinode = cpu_to_le64(OCFS2_I(inode)->ip_blkno);
	trailer->db_blkno = cpu_to_le64(bh->b_blocknr);
	trailer->db_free_rec_len = cpu_to_le16(rec_len);
}
/*
 * Link an unindexed block with a dir trailer structure into the index free
 * list. This function will modify dirdata_bh, but assumes you've already
 * passed it to the journal.
 */
static int ocfs2_dx_dir_link_trailer(struct inode *dir, handle_t *handle,
				     struct buffer_head *dx_root_bh,
				     struct buffer_head *dirdata_bh)
{
	int ret;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dir_block_trailer *trailer;

	ret = ocfs2_journal_access_dr(handle, dir, dx_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	trailer = ocfs2_trailer_from_bh(dirdata_bh, dir->i_sb);
	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;

	trailer->db_free_next = dx_root->dr_free_blk;
	dx_root->dr_free_blk = cpu_to_le64(dirdata_bh->b_blocknr);

	ocfs2_journal_dirty(handle, dx_root_bh);

out:
	return ret;
}

static int ocfs2_free_list_at_root(struct ocfs2_dir_lookup_result *res)
{
	return res->dl_prev_leaf_bh == NULL;
}

void ocfs2_free_dir_lookup_result(struct ocfs2_dir_lookup_result *res)
{
	brelse(res->dl_dx_root_bh);
	brelse(res->dl_leaf_bh);
	brelse(res->dl_dx_leaf_bh);
	brelse(res->dl_prev_leaf_bh);
}

static int ocfs2_dir_indexed(struct inode *inode)
{
	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INDEXED_DIR_FL)
		return 1;
	return 0;
}

static inline int ocfs2_dx_root_inline(struct ocfs2_dx_root_block *dx_root)
{
	return dx_root->dr_flags & OCFS2_DX_FLAG_INLINE;
}

/*
 * Hashing code adapted from ext3
 */
#define DELTA 0x9E3779B9

static void TEA_transform(__u32 buf[4], __u32 const in[])
{
	__u32	sum = 0;
	__u32	b0 = buf[0], b1 = buf[1];
	__u32	a = in[0], b = in[1], c = in[2], d = in[3];
	int	n = 16;

	do {
		sum += DELTA;
		b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);
		b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);
	} while (--n);

	buf[0] += b0;
	buf[1] += b1;
}

static void str2hashbuf(const char *msg, int len, __u32 *buf, int num)
{
	__u32	pad, val;
	int	i;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > num*4)
		len = num * 4;
	for (i = 0; i < len; i++) {
		if ((i % 4) == 0)
			val = pad;
		val = msg[i] + (val << 8);
		if ((i % 4) == 3) {
			*buf++ = val;
			val = pad;
			num--;
		}
	}
	if (--num >= 0)
		*buf++ = val;
	while (--num >= 0)
		*buf++ = pad;
}

static void ocfs2_dx_dir_name_hash(struct inode *dir, const char *name, int len,
				   struct ocfs2_dx_hinfo *hinfo)
{
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	const char	*p;
	__u32		in[8], buf[4];

	/*
	 * XXX: Is this really necessary, if the index is never looked
	 * at by readdir? Is a hash value of '0' a bad idea?
	 */
	if ((len == 1 && !strncmp(".", name, 1)) ||
	    (len == 2 && !strncmp("..", name, 2))) {
		buf[0] = buf[1] = 0;
		goto out;
	}

#ifdef OCFS2_DEBUG_DX_DIRS
	/*
	 * This makes it very easy to debug indexing problems. We
	 * should never allow this to be selected without hand editing
	 * this file though.
	 */
	buf[0] = buf[1] = len;
	goto out;
#endif

	memcpy(buf, osb->osb_dx_seed, sizeof(buf));

	p = name;
	while (len > 0) {
		str2hashbuf(p, len, in, 4);
		TEA_transform(buf, in);
		len -= 16;
		p += 16;
	}

out:
	hinfo->major_hash = buf[0];
	hinfo->minor_hash = buf[1];
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
 * Validate a directory trailer.
 *
 * We check the trailer here rather than in ocfs2_validate_dir_block()
 * because that function doesn't have the inode to test.
 */
static int ocfs2_check_dir_trailer(struct inode *dir, struct buffer_head *bh)
{
	int rc = 0;
	struct ocfs2_dir_block_trailer *trailer;

	trailer = ocfs2_trailer_from_bh(bh, dir->i_sb);
	if (!OCFS2_IS_VALID_DIR_TRAILER(trailer)) {
		rc = -EINVAL;
		ocfs2_error(dir->i_sb,
			    "Invalid dirblock #%llu: "
			    "signature = %.*s\n",
			    (unsigned long long)bh->b_blocknr, 7,
			    trailer->db_signature);
		goto out;
	}
	if (le64_to_cpu(trailer->db_blkno) != bh->b_blocknr) {
		rc = -EINVAL;
		ocfs2_error(dir->i_sb,
			    "Directory block #%llu has an invalid "
			    "db_blkno of %llu",
			    (unsigned long long)bh->b_blocknr,
			    (unsigned long long)le64_to_cpu(trailer->db_blkno));
		goto out;
	}
	if (le64_to_cpu(trailer->db_parent_dinode) !=
	    OCFS2_I(dir)->ip_blkno) {
		rc = -EINVAL;
		ocfs2_error(dir->i_sb,
			    "Directory block #%llu on dinode "
			    "#%llu has an invalid parent_dinode "
			    "of %llu",
			    (unsigned long long)bh->b_blocknr,
			    (unsigned long long)OCFS2_I(dir)->ip_blkno,
			    (unsigned long long)le64_to_cpu(trailer->db_blkno));
		goto out;
	}
out:
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

	rc = ocfs2_read_virt_blocks(inode, v_block, 1, &tmp, flags,
				    ocfs2_validate_dir_block);
	if (rc) {
		mlog_errno(rc);
		goto out;
	}

	if (!(flags & OCFS2_BH_READAHEAD) &&
	    ocfs2_supports_dir_trailer(inode)) {
		rc = ocfs2_check_dir_trailer(inode, tmp);
		if (rc) {
			if (!*bh)
				brelse(tmp);
			mlog_errno(rc);
			goto out;
		}
	}

	/* If ocfs2_read_virt_blocks() got us a new bh, pass it up. */
	if (!*bh)
		*bh = tmp;

out:
	return rc ? -EIO : 0;
}

/*
 * Read the block at 'phys' which belongs to this directory
 * inode. This function does no virtual->physical block translation -
 * what's passed in is assumed to be a valid directory block.
 */
static int ocfs2_read_dir_block_direct(struct inode *dir, u64 phys,
				       struct buffer_head **bh)
{
	int ret;
	struct buffer_head *tmp = *bh;

	ret = ocfs2_read_block(dir, phys, &tmp, ocfs2_validate_dir_block);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (ocfs2_supports_dir_trailer(dir)) {
		ret = ocfs2_check_dir_trailer(dir, tmp);
		if (ret) {
			if (!*bh)
				brelse(tmp);
			mlog_errno(ret);
			goto out;
		}
	}

	if (!ret && !*bh)
		*bh = tmp;
out:
	return ret;
}

static int ocfs2_validate_dx_root(struct super_block *sb,
				  struct buffer_head *bh)
{
	int ret;
	struct ocfs2_dx_root_block *dx_root;

	BUG_ON(!buffer_uptodate(bh));

	dx_root = (struct ocfs2_dx_root_block *) bh->b_data;

	ret = ocfs2_validate_meta_ecc(sb, bh->b_data, &dx_root->dr_check);
	if (ret) {
		mlog(ML_ERROR,
		     "Checksum failed for dir index root block %llu\n",
		     (unsigned long long)bh->b_blocknr);
		return ret;
	}

	if (!OCFS2_IS_VALID_DX_ROOT(dx_root)) {
		ocfs2_error(sb,
			    "Dir Index Root # %llu has bad signature %.*s",
			    (unsigned long long)le64_to_cpu(dx_root->dr_blkno),
			    7, dx_root->dr_signature);
		return -EINVAL;
	}

	return 0;
}

static int ocfs2_read_dx_root(struct inode *dir, struct ocfs2_dinode *di,
			      struct buffer_head **dx_root_bh)
{
	int ret;
	u64 blkno = le64_to_cpu(di->i_dx_root);
	struct buffer_head *tmp = *dx_root_bh;

	ret = ocfs2_read_block(dir, blkno, &tmp, ocfs2_validate_dx_root);

	/* If ocfs2_read_block() got us a new bh, pass it up. */
	if (!ret && !*dx_root_bh)
		*dx_root_bh = tmp;

	return ret;
}

static int ocfs2_validate_dx_leaf(struct super_block *sb,
				  struct buffer_head *bh)
{
	int ret;
	struct ocfs2_dx_leaf *dx_leaf = (struct ocfs2_dx_leaf *)bh->b_data;

	BUG_ON(!buffer_uptodate(bh));

	ret = ocfs2_validate_meta_ecc(sb, bh->b_data, &dx_leaf->dl_check);
	if (ret) {
		mlog(ML_ERROR,
		     "Checksum failed for dir index leaf block %llu\n",
		     (unsigned long long)bh->b_blocknr);
		return ret;
	}

	if (!OCFS2_IS_VALID_DX_LEAF(dx_leaf)) {
		ocfs2_error(sb, "Dir Index Leaf has bad signature %.*s",
			    7, dx_leaf->dl_signature);
		return -EROFS;
	}

	return 0;
}

static int ocfs2_read_dx_leaf(struct inode *dir, u64 blkno,
			      struct buffer_head **dx_leaf_bh)
{
	int ret;
	struct buffer_head *tmp = *dx_leaf_bh;

	ret = ocfs2_read_block(dir, blkno, &tmp, ocfs2_validate_dx_leaf);

	/* If ocfs2_read_block() got us a new bh, pass it up. */
	if (!ret && !*dx_leaf_bh)
		*dx_leaf_bh = tmp;

	return ret;
}

/*
 * Read a series of dx_leaf blocks. This expects all buffer_head
 * pointers to be NULL on function entry.
 */
static int ocfs2_read_dx_leaves(struct inode *dir, u64 start, int num,
				struct buffer_head **dx_leaf_bhs)
{
	int ret;

	ret = ocfs2_read_blocks(dir, start, num, dx_leaf_bhs, 0,
				ocfs2_validate_dx_leaf);
	if (ret)
		mlog_errno(ret);

	return ret;
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

static int ocfs2_dx_dir_lookup_rec(struct inode *inode,
				   struct ocfs2_extent_list *el,
				   u32 major_hash,
				   u32 *ret_cpos,
				   u64 *ret_phys_blkno,
				   unsigned int *ret_clen)
{
	int ret = 0, i, found;
	struct buffer_head *eb_bh = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec = NULL;

	if (el->l_tree_depth) {
		ret = ocfs2_find_leaf(inode, el, major_hash, &eb_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ocfs2_error(inode->i_sb,
				    "Inode %lu has non zero tree depth in "
				    "btree tree block %llu\n", inode->i_ino,
				    (unsigned long long)eb_bh->b_blocknr);
			ret = -EROFS;
			goto out;
		}
	}

	found = 0;
	for (i = le16_to_cpu(el->l_next_free_rec) - 1; i >= 0; i--) {
		rec = &el->l_recs[i];

		if (le32_to_cpu(rec->e_cpos) <= major_hash) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ocfs2_error(inode->i_sb, "Inode %lu has bad extent "
			    "record (%u, %u, 0) in btree", inode->i_ino,
			    le32_to_cpu(rec->e_cpos),
			    ocfs2_rec_clusters(el, rec));
		ret = -EROFS;
		goto out;
	}

	if (ret_phys_blkno)
		*ret_phys_blkno = le64_to_cpu(rec->e_blkno);
	if (ret_cpos)
		*ret_cpos = le32_to_cpu(rec->e_cpos);
	if (ret_clen)
		*ret_clen = le16_to_cpu(rec->e_leaf_clusters);

out:
	brelse(eb_bh);
	return ret;
}

/*
 * Returns the block index, from the start of the cluster which this
 * hash belongs too.
 */
static inline unsigned int __ocfs2_dx_dir_hash_idx(struct ocfs2_super *osb,
						   u32 minor_hash)
{
	return minor_hash & osb->osb_dx_mask;
}

static inline unsigned int ocfs2_dx_dir_hash_idx(struct ocfs2_super *osb,
					  struct ocfs2_dx_hinfo *hinfo)
{
	return __ocfs2_dx_dir_hash_idx(osb, hinfo->minor_hash);
}

static int ocfs2_dx_dir_lookup(struct inode *inode,
			       struct ocfs2_extent_list *el,
			       struct ocfs2_dx_hinfo *hinfo,
			       u32 *ret_cpos,
			       u64 *ret_phys_blkno)
{
	int ret = 0;
	unsigned int cend, uninitialized_var(clen);
	u32 uninitialized_var(cpos);
	u64 uninitialized_var(blkno);
	u32 name_hash = hinfo->major_hash;

	ret = ocfs2_dx_dir_lookup_rec(inode, el, name_hash, &cpos, &blkno,
				      &clen);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	cend = cpos + clen;
	if (name_hash >= cend) {
		/* We want the last cluster */
		blkno += ocfs2_clusters_to_blocks(inode->i_sb, clen - 1);
		cpos += clen - 1;
	} else {
		blkno += ocfs2_clusters_to_blocks(inode->i_sb,
						  name_hash - cpos);
		cpos = name_hash;
	}

	/*
	 * We now have the cluster which should hold our entry. To
	 * find the exact block from the start of the cluster to
	 * search, we take the lower bits of the hash.
	 */
	blkno += ocfs2_dx_dir_hash_idx(OCFS2_SB(inode->i_sb), hinfo);

	if (ret_phys_blkno)
		*ret_phys_blkno = blkno;
	if (ret_cpos)
		*ret_cpos = cpos;

out:

	return ret;
}

static int ocfs2_dx_dir_search(const char *name, int namelen,
			       struct inode *dir,
			       struct ocfs2_dx_root_block *dx_root,
			       struct ocfs2_dir_lookup_result *res)
{
	int ret, i, found;
	u64 uninitialized_var(phys);
	struct buffer_head *dx_leaf_bh = NULL;
	struct ocfs2_dx_leaf *dx_leaf;
	struct ocfs2_dx_entry *dx_entry = NULL;
	struct buffer_head *dir_ent_bh = NULL;
	struct ocfs2_dir_entry *dir_ent = NULL;
	struct ocfs2_dx_hinfo *hinfo = &res->dl_hinfo;
	struct ocfs2_extent_list *dr_el;
	struct ocfs2_dx_entry_list *entry_list;

	ocfs2_dx_dir_name_hash(dir, name, namelen, &res->dl_hinfo);

	if (ocfs2_dx_root_inline(dx_root)) {
		entry_list = &dx_root->dr_entries;
		goto search;
	}

	dr_el = &dx_root->dr_list;

	ret = ocfs2_dx_dir_lookup(dir, dr_el, hinfo, NULL, &phys);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	mlog(0, "Dir %llu: name: \"%.*s\", lookup of hash: %u.0x%x "
	     "returns: %llu\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno,
	     namelen, name, hinfo->major_hash, hinfo->minor_hash,
	     (unsigned long long)phys);

	ret = ocfs2_read_dx_leaf(dir, phys, &dx_leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dx_leaf = (struct ocfs2_dx_leaf *) dx_leaf_bh->b_data;

	mlog(0, "leaf info: num_used: %d, count: %d\n",
	     le16_to_cpu(dx_leaf->dl_list.de_num_used),
	     le16_to_cpu(dx_leaf->dl_list.de_count));

	entry_list = &dx_leaf->dl_list;

search:
	/*
	 * Empty leaf is legal, so no need to check for that.
	 */
	found = 0;
	for (i = 0; i < le16_to_cpu(entry_list->de_num_used); i++) {
		dx_entry = &entry_list->de_entries[i];

		if (hinfo->major_hash != le32_to_cpu(dx_entry->dx_major_hash)
		    || hinfo->minor_hash != le32_to_cpu(dx_entry->dx_minor_hash))
			continue;

		/*
		 * Search unindexed leaf block now. We're not
		 * guaranteed to find anything.
		 */
		ret = ocfs2_read_dir_block_direct(dir,
					  le64_to_cpu(dx_entry->dx_dirent_blk),
					  &dir_ent_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * XXX: We should check the unindexed block here,
		 * before using it.
		 */

		found = ocfs2_search_dirblock(dir_ent_bh, dir, name, namelen,
					      0, dir_ent_bh->b_data,
					      dir->i_sb->s_blocksize, &dir_ent);
		if (found == 1)
			break;

		if (found == -1) {
			/* This means we found a bad directory entry. */
			ret = -EIO;
			mlog_errno(ret);
			goto out;
		}

		brelse(dir_ent_bh);
		dir_ent_bh = NULL;
	}

	if (found <= 0) {
		ret = -ENOENT;
		goto out;
	}

	res->dl_leaf_bh = dir_ent_bh;
	res->dl_entry = dir_ent;
	res->dl_dx_leaf_bh = dx_leaf_bh;
	res->dl_dx_entry = dx_entry;

	ret = 0;
out:
	if (ret) {
		brelse(dx_leaf_bh);
		brelse(dir_ent_bh);
	}
	return ret;
}

static int ocfs2_find_entry_dx(const char *name, int namelen,
			       struct inode *dir,
			       struct ocfs2_dir_lookup_result *lookup)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	struct buffer_head *dx_root_bh = NULL;
	struct ocfs2_dx_root_block *dx_root;

	ret = ocfs2_read_inode_block(dir, &di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;

	ret = ocfs2_read_dx_root(dir, di, &dx_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	dx_root = (struct ocfs2_dx_root_block *) dx_root_bh->b_data;

	ret = ocfs2_dx_dir_search(name, namelen, dir, dx_root, lookup);
	if (ret) {
		if (ret != -ENOENT)
			mlog_errno(ret);
		goto out;
	}

	lookup->dl_dx_root_bh = dx_root_bh;
	dx_root_bh = NULL;
out:
	brelse(di_bh);
	brelse(dx_root_bh);
	return ret;
}

/*
 * Try to find an entry of the provided name within 'dir'.
 *
 * If nothing was found, -ENOENT is returned. Otherwise, zero is
 * returned and the struct 'res' will contain information useful to
 * other directory manipulation functions.
 *
 * Caller can NOT assume anything about the contents of the
 * buffer_heads - they are passed back only so that it can be passed
 * into any one of the manipulation functions (add entry, delete
 * entry, etc). As an example, bh in the extent directory case is a
 * data block, in the inline-data case it actually points to an inode,
 * in the indexed directory case, multiple buffers are involved.
 */
int ocfs2_find_entry(const char *name, int namelen,
		     struct inode *dir, struct ocfs2_dir_lookup_result *lookup)
{
	struct buffer_head *bh;
	struct ocfs2_dir_entry *res_dir = NULL;

	if (ocfs2_dir_indexed(dir))
		return ocfs2_find_entry_dx(name, namelen, dir, lookup);

	/*
	 * The unindexed dir code only uses part of the lookup
	 * structure, so there's no reason to push it down further
	 * than this.
	 */
	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		bh = ocfs2_find_entry_id(name, namelen, dir, &res_dir);
	else
		bh = ocfs2_find_entry_el(name, namelen, dir, &res_dir);

	if (bh == NULL)
		return -ENOENT;

	lookup->dl_leaf_bh = bh;
	lookup->dl_entry = res_dir;
	return 0;
}

/*
 * Update inode number and type of a previously found directory entry.
 */
int ocfs2_update_entry(struct inode *dir, handle_t *handle,
		       struct ocfs2_dir_lookup_result *res,
		       struct inode *new_entry_inode)
{
	int ret;
	ocfs2_journal_access_func access = ocfs2_journal_access_db;
	struct ocfs2_dir_entry *de = res->dl_entry;
	struct buffer_head *de_bh = res->dl_leaf_bh;

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

/*
 * __ocfs2_delete_entry deletes a directory entry by merging it with the
 * previous entry
 */
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

static unsigned int ocfs2_figure_dirent_hole(struct ocfs2_dir_entry *de)
{
	unsigned int hole;

	if (le64_to_cpu(de->inode) == 0)
		hole = le16_to_cpu(de->rec_len);
	else
		hole = le16_to_cpu(de->rec_len) -
			OCFS2_DIR_REC_LEN(de->name_len);

	return hole;
}

static int ocfs2_find_max_rec_len(struct super_block *sb,
				  struct buffer_head *dirblock_bh)
{
	int size, this_hole, largest_hole = 0;
	char *trailer, *de_buf, *limit, *start = dirblock_bh->b_data;
	struct ocfs2_dir_entry *de;

	trailer = (char *)ocfs2_trailer_from_bh(dirblock_bh, sb);
	size = ocfs2_dir_trailer_blk_off(sb);
	limit = start + size;
	de_buf = start;
	de = (struct ocfs2_dir_entry *)de_buf;
	do {
		if (de_buf != trailer) {
			this_hole = ocfs2_figure_dirent_hole(de);
			if (this_hole > largest_hole)
				largest_hole = this_hole;
		}

		de_buf += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *)de_buf;
	} while (de_buf < limit);

	if (largest_hole >= OCFS2_DIR_MIN_REC_LEN)
		return largest_hole;
	return 0;
}

static void ocfs2_dx_list_remove_entry(struct ocfs2_dx_entry_list *entry_list,
				       int index)
{
	int num_used = le16_to_cpu(entry_list->de_num_used);

	if (num_used == 1 || index == (num_used - 1))
		goto clear;

	memmove(&entry_list->de_entries[index],
		&entry_list->de_entries[index + 1],
		(num_used - index - 1)*sizeof(struct ocfs2_dx_entry));
clear:
	num_used--;
	memset(&entry_list->de_entries[num_used], 0,
	       sizeof(struct ocfs2_dx_entry));
	entry_list->de_num_used = cpu_to_le16(num_used);
}

static int ocfs2_delete_entry_dx(handle_t *handle, struct inode *dir,
				 struct ocfs2_dir_lookup_result *lookup)
{
	int ret, index, max_rec_len, add_to_free_list = 0;
	struct buffer_head *dx_root_bh = lookup->dl_dx_root_bh;
	struct buffer_head *leaf_bh = lookup->dl_leaf_bh;
	struct ocfs2_dx_leaf *dx_leaf;
	struct ocfs2_dx_entry *dx_entry = lookup->dl_dx_entry;
	struct ocfs2_dir_block_trailer *trailer;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_entry_list *entry_list;

	/*
	 * This function gets a bit messy because we might have to
	 * modify the root block, regardless of whether the indexed
	 * entries are stored inline.
	 */

	/*
	 * *Only* set 'entry_list' here, based on where we're looking
	 * for the indexed entries. Later, we might still want to
	 * journal both blocks, based on free list state.
	 */
	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;
	if (ocfs2_dx_root_inline(dx_root)) {
		entry_list = &dx_root->dr_entries;
	} else {
		dx_leaf = (struct ocfs2_dx_leaf *) lookup->dl_dx_leaf_bh->b_data;
		entry_list = &dx_leaf->dl_list;
	}

	/* Neither of these are a disk corruption - that should have
	 * been caught by lookup, before we got here. */
	BUG_ON(le16_to_cpu(entry_list->de_count) <= 0);
	BUG_ON(le16_to_cpu(entry_list->de_num_used) <= 0);

	index = (char *)dx_entry - (char *)entry_list->de_entries;
	index /= sizeof(*dx_entry);

	if (index >= le16_to_cpu(entry_list->de_num_used)) {
		mlog(ML_ERROR, "Dir %llu: Bad dx_entry ptr idx %d, (%p, %p)\n",
		     (unsigned long long)OCFS2_I(dir)->ip_blkno, index,
		     entry_list, dx_entry);
		return -EIO;
	}

	/*
	 * We know that removal of this dirent will leave enough room
	 * for a new one, so add this block to the free list if it
	 * isn't already there.
	 */
	trailer = ocfs2_trailer_from_bh(leaf_bh, dir->i_sb);
	if (trailer->db_free_rec_len == 0)
		add_to_free_list = 1;

	/*
	 * Add the block holding our index into the journal before
	 * removing the unindexed entry. If we get an error return
	 * from __ocfs2_delete_entry(), then it hasn't removed the
	 * entry yet. Likewise, successful return means we *must*
	 * remove the indexed entry.
	 *
	 * We're also careful to journal the root tree block here as
	 * the entry count needs to be updated. Also, we might be
	 * adding to the start of the free list.
	 */
	ret = ocfs2_journal_access_dr(handle, dir, dx_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (!ocfs2_dx_root_inline(dx_root)) {
		ret = ocfs2_journal_access_dl(handle, dir,
					      lookup->dl_dx_leaf_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	mlog(0, "Dir %llu: delete entry at index: %d\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno, index);

	ret = __ocfs2_delete_entry(handle, dir, lookup->dl_entry,
				   leaf_bh, leaf_bh->b_data, leaf_bh->b_size);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	max_rec_len = ocfs2_find_max_rec_len(dir->i_sb, leaf_bh);
	trailer->db_free_rec_len = cpu_to_le16(max_rec_len);
	if (add_to_free_list) {
		trailer->db_free_next = dx_root->dr_free_blk;
		dx_root->dr_free_blk = cpu_to_le64(leaf_bh->b_blocknr);
		ocfs2_journal_dirty(handle, dx_root_bh);
	}

	/* leaf_bh was journal_accessed for us in __ocfs2_delete_entry */
	ocfs2_journal_dirty(handle, leaf_bh);

	le32_add_cpu(&dx_root->dr_num_entries, -1);
	ocfs2_journal_dirty(handle, dx_root_bh);

	ocfs2_dx_list_remove_entry(entry_list, index);

	if (!ocfs2_dx_root_inline(dx_root))
		ocfs2_journal_dirty(handle, lookup->dl_dx_leaf_bh);

out:
	return ret;
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
 * Delete a directory entry. Hide the details of directory
 * implementation from the caller.
 */
int ocfs2_delete_entry(handle_t *handle,
		       struct inode *dir,
		       struct ocfs2_dir_lookup_result *res)
{
	if (ocfs2_dir_indexed(dir))
		return ocfs2_delete_entry_dx(handle, dir, res);

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_delete_entry_id(handle, dir, res->dl_entry,
					     res->dl_leaf_bh);

	return ocfs2_delete_entry_el(handle, dir, res->dl_entry,
				     res->dl_leaf_bh);
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

static void ocfs2_dx_dir_leaf_insert_tail(struct ocfs2_dx_leaf *dx_leaf,
					  struct ocfs2_dx_entry *dx_new_entry)
{
	int i;

	i = le16_to_cpu(dx_leaf->dl_list.de_num_used);
	dx_leaf->dl_list.de_entries[i] = *dx_new_entry;

	le16_add_cpu(&dx_leaf->dl_list.de_num_used, 1);
}

static void ocfs2_dx_entry_list_insert(struct ocfs2_dx_entry_list *entry_list,
				       struct ocfs2_dx_hinfo *hinfo,
				       u64 dirent_blk)
{
	int i;
	struct ocfs2_dx_entry *dx_entry;

	i = le16_to_cpu(entry_list->de_num_used);
	dx_entry = &entry_list->de_entries[i];

	memset(dx_entry, 0, sizeof(*dx_entry));
	dx_entry->dx_major_hash = cpu_to_le32(hinfo->major_hash);
	dx_entry->dx_minor_hash = cpu_to_le32(hinfo->minor_hash);
	dx_entry->dx_dirent_blk = cpu_to_le64(dirent_blk);

	le16_add_cpu(&entry_list->de_num_used, 1);
}

static int __ocfs2_dx_dir_leaf_insert(struct inode *dir, handle_t *handle,
				      struct ocfs2_dx_hinfo *hinfo,
				      u64 dirent_blk,
				      struct buffer_head *dx_leaf_bh)
{
	int ret;
	struct ocfs2_dx_leaf *dx_leaf;

	ret = ocfs2_journal_access_dl(handle, dir, dx_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_bh->b_data;
	ocfs2_dx_entry_list_insert(&dx_leaf->dl_list, hinfo, dirent_blk);
	ocfs2_journal_dirty(handle, dx_leaf_bh);

out:
	return ret;
}

static void ocfs2_dx_inline_root_insert(struct inode *dir, handle_t *handle,
					struct ocfs2_dx_hinfo *hinfo,
					u64 dirent_blk,
					struct ocfs2_dx_root_block *dx_root)
{
	ocfs2_dx_entry_list_insert(&dx_root->dr_entries, hinfo, dirent_blk);
}

static int ocfs2_dx_dir_insert(struct inode *dir, handle_t *handle,
			       struct ocfs2_dir_lookup_result *lookup)
{
	int ret = 0;
	struct ocfs2_dx_root_block *dx_root;
	struct buffer_head *dx_root_bh = lookup->dl_dx_root_bh;

	ret = ocfs2_journal_access_dr(handle, dir, dx_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dx_root = (struct ocfs2_dx_root_block *)lookup->dl_dx_root_bh->b_data;
	if (ocfs2_dx_root_inline(dx_root)) {
		ocfs2_dx_inline_root_insert(dir, handle,
					    &lookup->dl_hinfo,
					    lookup->dl_leaf_bh->b_blocknr,
					    dx_root);
	} else {
		ret = __ocfs2_dx_dir_leaf_insert(dir, handle, &lookup->dl_hinfo,
						 lookup->dl_leaf_bh->b_blocknr,
						 lookup->dl_dx_leaf_bh);
		if (ret)
			goto out;
	}

	le32_add_cpu(&dx_root->dr_num_entries, 1);
	ocfs2_journal_dirty(handle, dx_root_bh);

out:
	return ret;
}

static void ocfs2_remove_block_from_free_list(struct inode *dir,
				       handle_t *handle,
				       struct ocfs2_dir_lookup_result *lookup)
{
	struct ocfs2_dir_block_trailer *trailer, *prev;
	struct ocfs2_dx_root_block *dx_root;
	struct buffer_head *bh;

	trailer = ocfs2_trailer_from_bh(lookup->dl_leaf_bh, dir->i_sb);

	if (ocfs2_free_list_at_root(lookup)) {
		bh = lookup->dl_dx_root_bh;
		dx_root = (struct ocfs2_dx_root_block *)bh->b_data;
		dx_root->dr_free_blk = trailer->db_free_next;
	} else {
		bh = lookup->dl_prev_leaf_bh;
		prev = ocfs2_trailer_from_bh(bh, dir->i_sb);
		prev->db_free_next = trailer->db_free_next;
	}

	trailer->db_free_rec_len = cpu_to_le16(0);
	trailer->db_free_next = cpu_to_le64(0);

	ocfs2_journal_dirty(handle, bh);
	ocfs2_journal_dirty(handle, lookup->dl_leaf_bh);
}

/*
 * This expects that a journal write has been reserved on
 * lookup->dl_prev_leaf_bh or lookup->dl_dx_root_bh
 */
static void ocfs2_recalc_free_list(struct inode *dir, handle_t *handle,
				   struct ocfs2_dir_lookup_result *lookup)
{
	int max_rec_len;
	struct ocfs2_dir_block_trailer *trailer;

	/* Walk dl_leaf_bh to figure out what the new free rec_len is. */
	max_rec_len = ocfs2_find_max_rec_len(dir->i_sb, lookup->dl_leaf_bh);
	if (max_rec_len) {
		/*
		 * There's still room in this block, so no need to remove it
		 * from the free list. In this case, we just want to update
		 * the rec len accounting.
		 */
		trailer = ocfs2_trailer_from_bh(lookup->dl_leaf_bh, dir->i_sb);
		trailer->db_free_rec_len = cpu_to_le16(max_rec_len);
		ocfs2_journal_dirty(handle, lookup->dl_leaf_bh);
	} else {
		ocfs2_remove_block_from_free_list(dir, handle, lookup);
	}
}

/* we don't always have a dentry for what we want to add, so people
 * like orphan dir can call this instead.
 *
 * The lookup context must have been filled from
 * ocfs2_prepare_dir_for_insert.
 */
int __ocfs2_add_entry(handle_t *handle,
		      struct inode *dir,
		      const char *name, int namelen,
		      struct inode *inode, u64 blkno,
		      struct buffer_head *parent_fe_bh,
		      struct ocfs2_dir_lookup_result *lookup)
{
	unsigned long offset;
	unsigned short rec_len;
	struct ocfs2_dir_entry *de, *de1;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)parent_fe_bh->b_data;
	struct super_block *sb = dir->i_sb;
	int retval, status;
	unsigned int size = sb->s_blocksize;
	struct buffer_head *insert_bh = lookup->dl_leaf_bh;
	char *data_start = insert_bh->b_data;

	mlog_entry_void();

	if (!namelen)
		return -EINVAL;

	if (ocfs2_dir_indexed(dir)) {
		struct buffer_head *bh;

		/*
		 * An indexed dir may require that we update the free space
		 * list. Reserve a write to the previous node in the list so
		 * that we don't fail later.
		 *
		 * XXX: This can be either a dx_root_block, or an unindexed
		 * directory tree leaf block.
		 */
		if (ocfs2_free_list_at_root(lookup)) {
			bh = lookup->dl_dx_root_bh;
			retval = ocfs2_journal_access_dr(handle, dir, bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		} else {
			bh = lookup->dl_prev_leaf_bh;
			retval = ocfs2_journal_access_db(handle, dir, bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		}
		if (retval) {
			mlog_errno(retval);
			return retval;
		}
	} else if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
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
			else {
				status = ocfs2_journal_access_db(handle, dir,
								 insert_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);

				if (ocfs2_dir_indexed(dir)) {
					status = ocfs2_dx_dir_insert(dir,
								handle,
								lookup);
					if (status) {
						mlog_errno(status);
						goto bail;
					}
				}
			}

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

			if (ocfs2_dir_indexed(dir))
				ocfs2_recalc_free_list(dir, handle, lookup);

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

/*
 * NOTE: This function can be called against unindexed directories,
 * and indexed ones.
 */
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
			     struct ocfs2_dir_lookup_result *lookup)
{
	int status = -ENOENT;

	mlog(0, "name=%.*s, blkno=%p, inode=%llu\n", namelen, name, blkno,
	     (unsigned long long)OCFS2_I(inode)->ip_blkno);

	status = ocfs2_find_entry(name, namelen, inode, lookup);
	if (status)
		goto leave;

	*blkno = le64_to_cpu(lookup->dl_entry->inode);

	status = 0;
leave:

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
	struct ocfs2_dir_lookup_result lookup = { NULL, };

	ret = ocfs2_find_files_on_disk(name, namelen, blkno, dir, &lookup);
	ocfs2_free_dir_lookup_result(&lookup);

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
	struct ocfs2_dir_lookup_result lookup = { NULL, };

	mlog_entry("dir %llu, name '%.*s'\n",
		   (unsigned long long)OCFS2_I(dir)->ip_blkno, namelen, name);

	ret = -EEXIST;
	if (ocfs2_find_entry(name, namelen, dir, &lookup) == 0)
		goto bail;

	ret = 0;
bail:
	ocfs2_free_dir_lookup_result(&lookup);

	mlog_exit(ret);
	return ret;
}

struct ocfs2_empty_dir_priv {
	unsigned seen_dot;
	unsigned seen_dot_dot;
	unsigned seen_other;
	unsigned dx_dir;
};
static int ocfs2_empty_dir_filldir(void *priv, const char *name, int name_len,
				   loff_t pos, u64 ino, unsigned type)
{
	struct ocfs2_empty_dir_priv *p = priv;

	/*
	 * Check the positions of "." and ".." records to be sure
	 * they're in the correct place.
	 *
	 * Indexed directories don't need to proceed past the first
	 * two entries, so we end the scan after seeing '..'. Despite
	 * that, we allow the scan to proceed In the event that we
	 * have a corrupted indexed directory (no dot or dot dot
	 * entries). This allows us to double check for existing
	 * entries which might not have been found in the index.
	 */
	if (name_len == 1 && !strncmp(".", name, 1) && pos == 0) {
		p->seen_dot = 1;
		return 0;
	}

	if (name_len == 2 && !strncmp("..", name, 2) &&
	    pos == OCFS2_DIR_REC_LEN(1)) {
		p->seen_dot_dot = 1;

		if (p->dx_dir && p->seen_dot)
			return 1;

		return 0;
	}

	p->seen_other = 1;
	return 1;
}

static int ocfs2_empty_dir_dx(struct inode *inode,
			      struct ocfs2_empty_dir_priv *priv)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	struct buffer_head *dx_root_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_dx_root_block *dx_root;

	priv->dx_dir = 1;

	ret = ocfs2_read_inode_block(inode, &di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	di = (struct ocfs2_dinode *)di_bh->b_data;

	ret = ocfs2_read_dx_root(inode, di, &dx_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;

	if (le32_to_cpu(dx_root->dr_num_entries) != 2)
		priv->seen_other = 1;

out:
	brelse(di_bh);
	brelse(dx_root_bh);
	return ret;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 *
 * Returns 1 if dir is empty, zero otherwise.
 *
 * XXX: This is a performance problem for unindexed directories.
 */
int ocfs2_empty_dir(struct inode *inode)
{
	int ret;
	loff_t start = 0;
	struct ocfs2_empty_dir_priv priv;

	memset(&priv, 0, sizeof(priv));

	if (ocfs2_dir_indexed(inode)) {
		ret = ocfs2_empty_dir_dx(inode, &priv);
		if (ret)
			mlog_errno(ret);
		/*
		 * We still run ocfs2_dir_foreach to get the checks
		 * for "." and "..".
		 */
	}

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
				 struct ocfs2_alloc_context *data_ac,
				 struct buffer_head **ret_new_bh)
{
	int status;
	unsigned int size = osb->sb->s_blocksize;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry *de;

	mlog_entry_void();

	if (ocfs2_new_dir_wants_trailer(inode))
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
	if (ocfs2_new_dir_wants_trailer(inode)) {
		int size = le16_to_cpu(de->rec_len);

		/*
		 * Figure out the size of the hole left over after
		 * insertion of '.' and '..'. The trailer wants this
		 * information.
		 */
		size -= OCFS2_DIR_REC_LEN(2);
		size -= sizeof(struct ocfs2_dir_block_trailer);

		ocfs2_init_dir_trailer(inode, new_bh, size);
	}

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
	if (ret_new_bh) {
		*ret_new_bh = new_bh;
		new_bh = NULL;
	}
bail:
	brelse(new_bh);

	mlog_exit(status);
	return status;
}

static int ocfs2_dx_dir_attach_index(struct ocfs2_super *osb,
				     handle_t *handle, struct inode *dir,
				     struct buffer_head *di_bh,
				     struct buffer_head *dirdata_bh,
				     struct ocfs2_alloc_context *meta_ac,
				     int dx_inline, u32 num_entries,
				     struct buffer_head **ret_dx_root_bh)
{
	int ret;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *) di_bh->b_data;
	u16 dr_suballoc_bit;
	u64 dr_blkno;
	unsigned int num_bits;
	struct buffer_head *dx_root_bh = NULL;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dir_block_trailer *trailer =
		ocfs2_trailer_from_bh(dirdata_bh, dir->i_sb);

	ret = ocfs2_claim_metadata(osb, handle, meta_ac, 1, &dr_suballoc_bit,
				   &num_bits, &dr_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	mlog(0, "Dir %llu, attach new index block: %llu\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno,
	     (unsigned long long)dr_blkno);

	dx_root_bh = sb_getblk(osb->sb, dr_blkno);
	if (dx_root_bh == NULL) {
		ret = -EIO;
		goto out;
	}
	ocfs2_set_new_buffer_uptodate(dir, dx_root_bh);

	ret = ocfs2_journal_access_dr(handle, dir, dx_root_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;
	memset(dx_root, 0, osb->sb->s_blocksize);
	strcpy(dx_root->dr_signature, OCFS2_DX_ROOT_SIGNATURE);
	dx_root->dr_suballoc_slot = cpu_to_le16(osb->slot_num);
	dx_root->dr_suballoc_bit = cpu_to_le16(dr_suballoc_bit);
	dx_root->dr_fs_generation = cpu_to_le32(osb->fs_generation);
	dx_root->dr_blkno = cpu_to_le64(dr_blkno);
	dx_root->dr_dir_blkno = cpu_to_le64(OCFS2_I(dir)->ip_blkno);
	dx_root->dr_num_entries = cpu_to_le32(num_entries);
	if (le16_to_cpu(trailer->db_free_rec_len))
		dx_root->dr_free_blk = cpu_to_le64(dirdata_bh->b_blocknr);
	else
		dx_root->dr_free_blk = cpu_to_le64(0);

	if (dx_inline) {
		dx_root->dr_flags |= OCFS2_DX_FLAG_INLINE;
		dx_root->dr_entries.de_count =
			cpu_to_le16(ocfs2_dx_entries_per_root(osb->sb));
	} else {
		dx_root->dr_list.l_count =
			cpu_to_le16(ocfs2_extent_recs_per_dx_root(osb->sb));
	}

	ret = ocfs2_journal_dirty(handle, dx_root_bh);
	if (ret)
		mlog_errno(ret);

	ret = ocfs2_journal_access_di(handle, dir, di_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	di->i_dx_root = cpu_to_le64(dr_blkno);

	OCFS2_I(dir)->ip_dyn_features |= OCFS2_INDEXED_DIR_FL;
	di->i_dyn_features = cpu_to_le16(OCFS2_I(dir)->ip_dyn_features);

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret)
		mlog_errno(ret);

	*ret_dx_root_bh = dx_root_bh;
	dx_root_bh = NULL;

out:
	brelse(dx_root_bh);
	return ret;
}

static int ocfs2_dx_dir_format_cluster(struct ocfs2_super *osb,
				       handle_t *handle, struct inode *dir,
				       struct buffer_head **dx_leaves,
				       int num_dx_leaves, u64 start_blk)
{
	int ret, i;
	struct ocfs2_dx_leaf *dx_leaf;
	struct buffer_head *bh;

	for (i = 0; i < num_dx_leaves; i++) {
		bh = sb_getblk(osb->sb, start_blk + i);
		if (bh == NULL) {
			ret = -EIO;
			goto out;
		}
		dx_leaves[i] = bh;

		ocfs2_set_new_buffer_uptodate(dir, bh);

		ret = ocfs2_journal_access_dl(handle, dir, bh,
					      OCFS2_JOURNAL_ACCESS_CREATE);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		dx_leaf = (struct ocfs2_dx_leaf *) bh->b_data;

		memset(dx_leaf, 0, osb->sb->s_blocksize);
		strcpy(dx_leaf->dl_signature, OCFS2_DX_LEAF_SIGNATURE);
		dx_leaf->dl_fs_generation = cpu_to_le32(osb->fs_generation);
		dx_leaf->dl_blkno = cpu_to_le64(bh->b_blocknr);
		dx_leaf->dl_list.de_count =
			cpu_to_le16(ocfs2_dx_entries_per_leaf(osb->sb));

		mlog(0,
		     "Dir %llu, format dx_leaf: %llu, entry count: %u\n",
		     (unsigned long long)OCFS2_I(dir)->ip_blkno,
		     (unsigned long long)bh->b_blocknr,
		     le16_to_cpu(dx_leaf->dl_list.de_count));

		ocfs2_journal_dirty(handle, bh);
	}

	ret = 0;
out:
	return ret;
}

/*
 * Allocates and formats a new cluster for use in an indexed dir
 * leaf. This version will not do the extent insert, so that it can be
 * used by operations which need careful ordering.
 */
static int __ocfs2_dx_dir_new_cluster(struct inode *dir,
				      u32 cpos, handle_t *handle,
				      struct ocfs2_alloc_context *data_ac,
				      struct buffer_head **dx_leaves,
				      int num_dx_leaves, u64 *ret_phys_blkno)
{
	int ret;
	u32 phys, num;
	u64 phys_blkno;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);

	/*
	 * XXX: For create, this should claim cluster for the index
	 * *before* the unindexed insert so that we have a better
	 * chance of contiguousness as the directory grows in number
	 * of entries.
	 */
	ret = __ocfs2_claim_clusters(osb, handle, data_ac, 1, 1, &phys, &num);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Format the new cluster first. That way, we're inserting
	 * valid data.
	 */
	phys_blkno = ocfs2_clusters_to_blocks(osb->sb, phys);
	ret = ocfs2_dx_dir_format_cluster(osb, handle, dir, dx_leaves,
					  num_dx_leaves, phys_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	*ret_phys_blkno = phys_blkno;
out:
	return ret;
}

static int ocfs2_dx_dir_new_cluster(struct inode *dir,
				    struct ocfs2_extent_tree *et,
				    u32 cpos, handle_t *handle,
				    struct ocfs2_alloc_context *data_ac,
				    struct ocfs2_alloc_context *meta_ac,
				    struct buffer_head **dx_leaves,
				    int num_dx_leaves)
{
	int ret;
	u64 phys_blkno;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);

	ret = __ocfs2_dx_dir_new_cluster(dir, cpos, handle, data_ac, dx_leaves,
					 num_dx_leaves, &phys_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_insert_extent(osb, handle, dir, et, cpos, phys_blkno, 1, 0,
				  meta_ac);
	if (ret)
		mlog_errno(ret);
out:
	return ret;
}

static struct buffer_head **ocfs2_dx_dir_kmalloc_leaves(struct super_block *sb,
							int *ret_num_leaves)
{
	int num_dx_leaves = ocfs2_clusters_to_blocks(sb, 1);
	struct buffer_head **dx_leaves;

	dx_leaves = kcalloc(num_dx_leaves, sizeof(struct buffer_head *),
			    GFP_NOFS);
	if (dx_leaves && ret_num_leaves)
		*ret_num_leaves = num_dx_leaves;

	return dx_leaves;
}

static int ocfs2_fill_new_dir_dx(struct ocfs2_super *osb,
				 handle_t *handle,
				 struct inode *parent,
				 struct inode *inode,
				 struct buffer_head *di_bh,
				 struct ocfs2_alloc_context *data_ac,
				 struct ocfs2_alloc_context *meta_ac)
{
	int ret;
	struct buffer_head *leaf_bh = NULL;
	struct buffer_head *dx_root_bh = NULL;
	struct ocfs2_dx_hinfo hinfo;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_entry_list *entry_list;

	/*
	 * Our strategy is to create the directory as though it were
	 * unindexed, then add the index block. This works with very
	 * little complication since the state of a new directory is a
	 * very well known quantity.
	 *
	 * Essentially, we have two dirents ("." and ".."), in the 1st
	 * block which need indexing. These are easily inserted into
	 * the index block.
	 */

	ret = ocfs2_fill_new_dir_el(osb, handle, parent, inode, di_bh,
				    data_ac, &leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_dx_dir_attach_index(osb, handle, inode, di_bh, leaf_bh,
					meta_ac, 1, 2, &dx_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;
	entry_list = &dx_root->dr_entries;

	/* Buffer has been journaled for us by ocfs2_dx_dir_attach_index */
	ocfs2_dx_dir_name_hash(inode, ".", 1, &hinfo);
	ocfs2_dx_entry_list_insert(entry_list, &hinfo, leaf_bh->b_blocknr);

	ocfs2_dx_dir_name_hash(inode, "..", 2, &hinfo);
	ocfs2_dx_entry_list_insert(entry_list, &hinfo, leaf_bh->b_blocknr);

out:
	brelse(dx_root_bh);
	brelse(leaf_bh);
	return ret;
}

int ocfs2_fill_new_dir(struct ocfs2_super *osb,
		       handle_t *handle,
		       struct inode *parent,
		       struct inode *inode,
		       struct buffer_head *fe_bh,
		       struct ocfs2_alloc_context *data_ac,
		       struct ocfs2_alloc_context *meta_ac)

{
	BUG_ON(!ocfs2_supports_inline_data(osb) && data_ac == NULL);

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_fill_new_dir_id(osb, handle, parent, inode, fe_bh);

	if (ocfs2_supports_indexed_dirs(osb))
		return ocfs2_fill_new_dir_dx(osb, handle, parent, inode, fe_bh,
					     data_ac, meta_ac);

	return ocfs2_fill_new_dir_el(osb, handle, parent, inode, fe_bh,
				     data_ac, NULL);
}

static int ocfs2_dx_dir_index_block(struct inode *dir,
				    handle_t *handle,
				    struct buffer_head **dx_leaves,
				    int num_dx_leaves,
				    u32 *num_dx_entries,
				    struct buffer_head *dirent_bh)
{
	int ret, namelen, i;
	char *de_buf, *limit;
	struct ocfs2_dir_entry *de;
	struct buffer_head *dx_leaf_bh;
	struct ocfs2_dx_hinfo hinfo;
	u64 dirent_blk = dirent_bh->b_blocknr;

	de_buf = dirent_bh->b_data;
	limit = de_buf + dir->i_sb->s_blocksize;

	while (de_buf < limit) {
		de = (struct ocfs2_dir_entry *)de_buf;

		namelen = de->name_len;
		if (!namelen || !de->inode)
			goto inc;

		ocfs2_dx_dir_name_hash(dir, de->name, namelen, &hinfo);

		i = ocfs2_dx_dir_hash_idx(OCFS2_SB(dir->i_sb), &hinfo);
		dx_leaf_bh = dx_leaves[i];

		ret = __ocfs2_dx_dir_leaf_insert(dir, handle, &hinfo,
						 dirent_blk, dx_leaf_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		*num_dx_entries = *num_dx_entries + 1;

inc:
		de_buf += le16_to_cpu(de->rec_len);
	}

out:
	return ret;
}

/*
 * XXX: This expects dx_root_bh to already be part of the transaction.
 */
static void ocfs2_dx_dir_index_root_block(struct inode *dir,
					 struct buffer_head *dx_root_bh,
					 struct buffer_head *dirent_bh)
{
	char *de_buf, *limit;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dir_entry *de;
	struct ocfs2_dx_hinfo hinfo;
	u64 dirent_blk = dirent_bh->b_blocknr;

	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;

	de_buf = dirent_bh->b_data;
	limit = de_buf + dir->i_sb->s_blocksize;

	while (de_buf < limit) {
		de = (struct ocfs2_dir_entry *)de_buf;

		if (!de->name_len || !de->inode)
			goto inc;

		ocfs2_dx_dir_name_hash(dir, de->name, de->name_len, &hinfo);

		mlog(0,
		     "dir: %llu, major: 0x%x minor: 0x%x, index: %u, name: %.*s\n",
		     (unsigned long long)dir->i_ino, hinfo.major_hash,
		     hinfo.minor_hash,
		     le16_to_cpu(dx_root->dr_entries.de_num_used),
		     de->name_len, de->name);

		ocfs2_dx_entry_list_insert(&dx_root->dr_entries, &hinfo,
					   dirent_blk);

		le32_add_cpu(&dx_root->dr_num_entries, 1);
inc:
		de_buf += le16_to_cpu(de->rec_len);
	}
}

/*
 * Count the number of inline directory entries in di_bh and compare
 * them against the number of entries we can hold in an inline dx root
 * block.
 */
static int ocfs2_new_dx_should_be_inline(struct inode *dir,
					 struct buffer_head *di_bh)
{
	int dirent_count = 0;
	char *de_buf, *limit;
	struct ocfs2_dir_entry *de;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

	de_buf = di->id2.i_data.id_data;
	limit = de_buf + i_size_read(dir);

	while (de_buf < limit) {
		de = (struct ocfs2_dir_entry *)de_buf;

		if (de->name_len && de->inode)
			dirent_count++;

		de_buf += le16_to_cpu(de->rec_len);
	}

	/* We are careful to leave room for one extra record. */
	return dirent_count < ocfs2_dx_entries_per_root(dir->i_sb);
}

/*
 * Expand rec_len of the rightmost dirent in a directory block so that it
 * contains the end of our valid space for dirents. We do this during
 * expansion from an inline directory to one with extents. The first dir block
 * in that case is taken from the inline data portion of the inode block.
 *
 * This will also return the largest amount of contiguous space for a dirent
 * in the block. That value is *not* necessarily the last dirent, even after
 * expansion. The directory indexing code wants this value for free space
 * accounting. We do this here since we're already walking the entire dir
 * block.
 *
 * We add the dir trailer if this filesystem wants it.
 */
static unsigned int ocfs2_expand_last_dirent(char *start, unsigned int old_size,
					     struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	struct ocfs2_dir_entry *de;
	struct ocfs2_dir_entry *prev_de;
	char *de_buf, *limit;
	unsigned int new_size = sb->s_blocksize;
	unsigned int bytes, this_hole;
	unsigned int largest_hole = 0;

	if (ocfs2_new_dir_wants_trailer(dir))
		new_size = ocfs2_dir_trailer_blk_off(sb);

	bytes = new_size - old_size;

	limit = start + old_size;
	de_buf = start;
	de = (struct ocfs2_dir_entry *)de_buf;
	do {
		this_hole = ocfs2_figure_dirent_hole(de);
		if (this_hole > largest_hole)
			largest_hole = this_hole;

		prev_de = de;
		de_buf += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *)de_buf;
	} while (de_buf < limit);

	le16_add_cpu(&prev_de->rec_len, bytes);

	/* We need to double check this after modification of the final
	 * dirent. */
	this_hole = ocfs2_figure_dirent_hole(prev_de);
	if (this_hole > largest_hole)
		largest_hole = this_hole;

	if (largest_hole >= OCFS2_DIR_MIN_REC_LEN)
		return largest_hole;
	return 0;
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
				   struct ocfs2_dir_lookup_result *lookup,
				   struct buffer_head **first_block_bh)
{
	u32 alloc, dx_alloc, bit_off, len, num_dx_entries = 0;
	struct super_block *sb = dir->i_sb;
	int ret, i, num_dx_leaves = 0, dx_inline = 0,
		credits = ocfs2_inline_to_extents_credits(sb);
	u64 dx_insert_blkno, blkno,
		bytes = blocks_wanted << sb->s_blocksize_bits;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ocfs2_inode_info *oi = OCFS2_I(dir);
	struct ocfs2_alloc_context *data_ac;
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct buffer_head *dirdata_bh = NULL;
	struct buffer_head *dx_root_bh = NULL;
	struct buffer_head **dx_leaves = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	handle_t *handle;
	struct ocfs2_extent_tree et;
	struct ocfs2_extent_tree dx_et;
	int did_quota = 0, bytes_allocated = 0;

	ocfs2_init_dinode_extent_tree(&et, dir, di_bh);

	alloc = ocfs2_clusters_for_bytes(sb, bytes);
	dx_alloc = 0;

	if (ocfs2_supports_indexed_dirs(osb)) {
		credits += ocfs2_add_dir_index_credits(sb);

		dx_inline = ocfs2_new_dx_should_be_inline(dir, di_bh);
		if (!dx_inline) {
			/* Add one more cluster for an index leaf */
			dx_alloc++;
			dx_leaves = ocfs2_dx_dir_kmalloc_leaves(sb,
								&num_dx_leaves);
			if (!dx_leaves) {
				ret = -ENOMEM;
				mlog_errno(ret);
				goto out;
			}
		}

		/* This gets us the dx_root */
		ret = ocfs2_reserve_new_metadata_blocks(osb, 1, &meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * We should never need more than 2 clusters for the unindexed
	 * tree - maximum dirent size is far less than one block. In
	 * fact, the only time we'd need more than one cluster is if
	 * blocksize == clustersize and the dirent won't fit in the
	 * extra space that the expansion to a single block gives. As
	 * of today, that only happens on 4k/4k file systems.
	 */
	BUG_ON(alloc > 2);

	ret = ocfs2_reserve_clusters(osb, alloc + dx_alloc, &data_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	down_write(&oi->ip_alloc_sem);

	/*
	 * Prepare for worst case allocation scenario of two separate
	 * extents in the unindexed tree.
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
				ocfs2_clusters_to_bytes(osb->sb,
							alloc + dx_alloc))) {
		ret = -EDQUOT;
		goto out_commit;
	}
	did_quota = 1;

	if (ocfs2_supports_indexed_dirs(osb) && !dx_inline) {
		/*
		 * Allocate our index cluster first, to maximize the
		 * possibility that unindexed leaves grow
		 * contiguously.
		 */
		ret = __ocfs2_dx_dir_new_cluster(dir, 0, handle, data_ac,
						 dx_leaves, num_dx_leaves,
						 &dx_insert_blkno);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
		bytes_allocated += ocfs2_clusters_to_bytes(dir->i_sb, 1);
	}

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
	bytes_allocated += ocfs2_clusters_to_bytes(dir->i_sb, 1);

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
	i = ocfs2_expand_last_dirent(dirdata_bh->b_data, i_size_read(dir), dir);
	if (ocfs2_new_dir_wants_trailer(dir)) {
		/*
		 * Prepare the dir trailer up front. It will otherwise look
		 * like a valid dirent. Even if inserting the index fails
		 * (unlikely), then all we'll have done is given first dir
		 * block a small amount of fragmentation.
		 */
		ocfs2_init_dir_trailer(dir, dirdata_bh, i);
	}

	ret = ocfs2_journal_dirty(handle, dirdata_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	if (ocfs2_supports_indexed_dirs(osb) && !dx_inline) {
		/*
		 * Dx dirs with an external cluster need to do this up
		 * front. Inline dx root's get handled later, after
		 * we've allocated our root block. We get passed back
		 * a total number of items so that dr_num_entries can
		 * be correctly set once the dx_root has been
		 * allocated.
		 */
		ret = ocfs2_dx_dir_index_block(dir, handle, dx_leaves,
					       num_dx_leaves, &num_dx_entries,
					       dirdata_bh);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
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

	if (ocfs2_supports_indexed_dirs(osb)) {
		ret = ocfs2_dx_dir_attach_index(osb, handle, dir, di_bh,
						dirdata_bh, meta_ac, dx_inline,
						num_dx_entries, &dx_root_bh);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}

		if (dx_inline) {
			ocfs2_dx_dir_index_root_block(dir, dx_root_bh,
						      dirdata_bh);
		} else {
			ocfs2_init_dx_root_extent_tree(&dx_et, dir, dx_root_bh);
			ret = ocfs2_insert_extent(osb, handle, dir, &dx_et, 0,
						  dx_insert_blkno, 1, 0, NULL);
			if (ret)
				mlog_errno(ret);
		}
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
		bytes_allocated += ocfs2_clusters_to_bytes(dir->i_sb, 1);
	}

	*first_block_bh = dirdata_bh;
	dirdata_bh = NULL;
	if (ocfs2_supports_indexed_dirs(osb)) {
		unsigned int off;

		if (!dx_inline) {
			/*
			 * We need to return the correct block within the
			 * cluster which should hold our entry.
			 */
			off = ocfs2_dx_dir_hash_idx(OCFS2_SB(dir->i_sb),
						    &lookup->dl_hinfo);
			get_bh(dx_leaves[off]);
			lookup->dl_dx_leaf_bh = dx_leaves[off];
		}
		lookup->dl_dx_root_bh = dx_root_bh;
		dx_root_bh = NULL;
	}

out_commit:
	if (ret < 0 && did_quota)
		vfs_dq_free_space_nodirty(dir, bytes_allocated);

	ocfs2_commit_trans(osb, handle);

out_sem:
	up_write(&oi->ip_alloc_sem);

out:
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	if (dx_leaves) {
		for (i = 0; i < num_dx_leaves; i++)
			brelse(dx_leaves[i]);
		kfree(dx_leaves);
	}

	brelse(dirdata_bh);
	brelse(dx_root_bh);

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
 *
 * If the directory is already indexed, dx_root_bh must be provided.
 */
static int ocfs2_extend_dir(struct ocfs2_super *osb,
			    struct inode *dir,
			    struct buffer_head *parent_fe_bh,
			    unsigned int blocks_wanted,
			    struct ocfs2_dir_lookup_result *lookup,
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
	struct buffer_head *dx_root_bh = lookup->dl_dx_root_bh;

	mlog_entry_void();

	if (OCFS2_I(dir)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		/*
		 * This would be a code error as an inline directory should
		 * never have an index root.
		 */
		BUG_ON(dx_root_bh);

		status = ocfs2_expand_inline_dir(dir, parent_fe_bh,
						 blocks_wanted, lookup,
						 &new_bh);
		if (status) {
			mlog_errno(status);
			goto bail;
		}

		/* Expansion from inline to an indexed directory will
		 * have given us this. */
		dx_root_bh = lookup->dl_dx_root_bh;

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
	if (ocfs2_dir_indexed(dir))
		credits++; /* For attaching the new dirent block to the
			    * dx_root */

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
	if (ocfs2_supports_dir_trailer(dir)) {
		de->rec_len = cpu_to_le16(ocfs2_dir_trailer_blk_off(sb));

		ocfs2_init_dir_trailer(dir, new_bh, le16_to_cpu(de->rec_len));

		if (ocfs2_dir_indexed(dir)) {
			status = ocfs2_dx_dir_link_trailer(dir, handle,
							   dx_root_bh, new_bh);
			if (status) {
				mlog_errno(status);
				goto bail;
			}
		}
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
	if (ocfs2_new_dir_wants_trailer(dir))
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

static int dx_leaf_sort_cmp(const void *a, const void *b)
{
	const struct ocfs2_dx_entry *entry1 = a;
	const struct ocfs2_dx_entry *entry2 = b;
	u32 major_hash1 = le32_to_cpu(entry1->dx_major_hash);
	u32 major_hash2 = le32_to_cpu(entry2->dx_major_hash);
	u32 minor_hash1 = le32_to_cpu(entry1->dx_minor_hash);
	u32 minor_hash2 = le32_to_cpu(entry2->dx_minor_hash);

	if (major_hash1 > major_hash2)
		return 1;
	if (major_hash1 < major_hash2)
		return -1;

	/*
	 * It is not strictly necessary to sort by minor
	 */
	if (minor_hash1 > minor_hash2)
		return 1;
	if (minor_hash1 < minor_hash2)
		return -1;
	return 0;
}

static void dx_leaf_sort_swap(void *a, void *b, int size)
{
	struct ocfs2_dx_entry *entry1 = a;
	struct ocfs2_dx_entry *entry2 = b;
	struct ocfs2_dx_entry tmp;

	BUG_ON(size != sizeof(*entry1));

	tmp = *entry1;
	*entry1 = *entry2;
	*entry2 = tmp;
}

static int ocfs2_dx_leaf_same_major(struct ocfs2_dx_leaf *dx_leaf)
{
	struct ocfs2_dx_entry_list *dl_list = &dx_leaf->dl_list;
	int i, num = le16_to_cpu(dl_list->de_num_used);

	for (i = 0; i < (num - 1); i++) {
		if (le32_to_cpu(dl_list->de_entries[i].dx_major_hash) !=
		    le32_to_cpu(dl_list->de_entries[i + 1].dx_major_hash))
			return 0;
	}

	return 1;
}

/*
 * Find the optimal value to split this leaf on. This expects the leaf
 * entries to be in sorted order.
 *
 * leaf_cpos is the cpos of the leaf we're splitting. insert_hash is
 * the hash we want to insert.
 *
 * This function is only concerned with the major hash - that which
 * determines which cluster an item belongs to.
 */
static int ocfs2_dx_dir_find_leaf_split(struct ocfs2_dx_leaf *dx_leaf,
					u32 leaf_cpos, u32 insert_hash,
					u32 *split_hash)
{
	struct ocfs2_dx_entry_list *dl_list = &dx_leaf->dl_list;
	int i, num_used = le16_to_cpu(dl_list->de_num_used);
	int allsame;

	/*
	 * There's a couple rare, but nasty corner cases we have to
	 * check for here. All of them involve a leaf where all value
	 * have the same hash, which is what we look for first.
	 *
	 * Most of the time, all of the above is false, and we simply
	 * pick the median value for a split.
	 */
	allsame = ocfs2_dx_leaf_same_major(dx_leaf);
	if (allsame) {
		u32 val = le32_to_cpu(dl_list->de_entries[0].dx_major_hash);

		if (val == insert_hash) {
			/*
			 * No matter where we would choose to split,
			 * the new entry would want to occupy the same
			 * block as these. Since there's no space left
			 * in their existing block, we know there
			 * won't be space after the split.
			 */
			return -ENOSPC;
		}

		if (val == leaf_cpos) {
			/*
			 * Because val is the same as leaf_cpos (which
			 * is the smallest value this leaf can have),
			 * yet is not equal to insert_hash, then we
			 * know that insert_hash *must* be larger than
			 * val (and leaf_cpos). At least cpos+1 in value.
			 *
			 * We also know then, that there cannot be an
			 * adjacent extent (otherwise we'd be looking
			 * at it). Choosing this value gives us a
			 * chance to get some contiguousness.
			 */
			*split_hash = leaf_cpos + 1;
			return 0;
		}

		if (val > insert_hash) {
			/*
			 * val can not be the same as insert hash, and
			 * also must be larger than leaf_cpos. Also,
			 * we know that there can't be a leaf between
			 * cpos and val, otherwise the entries with
			 * hash 'val' would be there.
			 */
			*split_hash = val;
			return 0;
		}

		*split_hash = insert_hash;
		return 0;
	}

	/*
	 * Since the records are sorted and the checks above
	 * guaranteed that not all records in this block are the same,
	 * we simple travel forward, from the median, and pick the 1st
	 * record whose value is larger than leaf_cpos.
	 */
	for (i = (num_used / 2); i < num_used; i++)
		if (le32_to_cpu(dl_list->de_entries[i].dx_major_hash) >
		    leaf_cpos)
			break;

	BUG_ON(i == num_used); /* Should be impossible */
	*split_hash = le32_to_cpu(dl_list->de_entries[i].dx_major_hash);
	return 0;
}

/*
 * Transfer all entries in orig_dx_leaves whose major hash is equal to or
 * larger than split_hash into new_dx_leaves. We use a temporary
 * buffer (tmp_dx_leaf) to make the changes to the original leaf blocks.
 *
 * Since the block offset inside a leaf (cluster) is a constant mask
 * of minor_hash, we can optimize - an item at block offset X within
 * the original cluster, will be at offset X within the new cluster.
 */
static void ocfs2_dx_dir_transfer_leaf(struct inode *dir, u32 split_hash,
				       handle_t *handle,
				       struct ocfs2_dx_leaf *tmp_dx_leaf,
				       struct buffer_head **orig_dx_leaves,
				       struct buffer_head **new_dx_leaves,
				       int num_dx_leaves)
{
	int i, j, num_used;
	u32 major_hash;
	struct ocfs2_dx_leaf *orig_dx_leaf, *new_dx_leaf;
	struct ocfs2_dx_entry_list *orig_list, *new_list, *tmp_list;
	struct ocfs2_dx_entry *dx_entry;

	tmp_list = &tmp_dx_leaf->dl_list;

	for (i = 0; i < num_dx_leaves; i++) {
		orig_dx_leaf = (struct ocfs2_dx_leaf *) orig_dx_leaves[i]->b_data;
		orig_list = &orig_dx_leaf->dl_list;
		new_dx_leaf = (struct ocfs2_dx_leaf *) new_dx_leaves[i]->b_data;
		new_list = &new_dx_leaf->dl_list;

		num_used = le16_to_cpu(orig_list->de_num_used);

		memcpy(tmp_dx_leaf, orig_dx_leaf, dir->i_sb->s_blocksize);
		tmp_list->de_num_used = cpu_to_le16(0);
		memset(&tmp_list->de_entries, 0, sizeof(*dx_entry)*num_used);

		for (j = 0; j < num_used; j++) {
			dx_entry = &orig_list->de_entries[j];
			major_hash = le32_to_cpu(dx_entry->dx_major_hash);
			if (major_hash >= split_hash)
				ocfs2_dx_dir_leaf_insert_tail(new_dx_leaf,
							      dx_entry);
			else
				ocfs2_dx_dir_leaf_insert_tail(tmp_dx_leaf,
							      dx_entry);
		}
		memcpy(orig_dx_leaf, tmp_dx_leaf, dir->i_sb->s_blocksize);

		ocfs2_journal_dirty(handle, orig_dx_leaves[i]);
		ocfs2_journal_dirty(handle, new_dx_leaves[i]);
	}
}

static int ocfs2_dx_dir_rebalance_credits(struct ocfs2_super *osb,
					  struct ocfs2_dx_root_block *dx_root)
{
	int credits = ocfs2_clusters_to_blocks(osb->sb, 2);

	credits += ocfs2_calc_extend_credits(osb->sb, &dx_root->dr_list, 1);
	credits += ocfs2_quota_trans_credits(osb->sb);
	return credits;
}

/*
 * Find the median value in dx_leaf_bh and allocate a new leaf to move
 * half our entries into.
 */
static int ocfs2_dx_dir_rebalance(struct ocfs2_super *osb, struct inode *dir,
				  struct buffer_head *dx_root_bh,
				  struct buffer_head *dx_leaf_bh,
				  struct ocfs2_dx_hinfo *hinfo, u32 leaf_cpos,
				  u64 leaf_blkno)
{
	struct ocfs2_dx_leaf *dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_bh->b_data;
	int credits, ret, i, num_used, did_quota = 0;
	u32 cpos, split_hash, insert_hash = hinfo->major_hash;
	u64 orig_leaves_start;
	int num_dx_leaves;
	struct buffer_head **orig_dx_leaves = NULL;
	struct buffer_head **new_dx_leaves = NULL;
	struct ocfs2_alloc_context *data_ac = NULL, *meta_ac = NULL;
	struct ocfs2_extent_tree et;
	handle_t *handle = NULL;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_leaf *tmp_dx_leaf = NULL;

	mlog(0, "DX Dir: %llu, rebalance leaf leaf_blkno: %llu insert: %u\n",
	     (unsigned long long)OCFS2_I(dir)->ip_blkno,
	     (unsigned long long)leaf_blkno, insert_hash);

	ocfs2_init_dx_root_extent_tree(&et, dir, dx_root_bh);

	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;
	/*
	 * XXX: This is a rather large limit. We should use a more
	 * realistic value.
	 */
	if (le32_to_cpu(dx_root->dr_clusters) == UINT_MAX)
		return -ENOSPC;

	num_used = le16_to_cpu(dx_leaf->dl_list.de_num_used);
	if (num_used < le16_to_cpu(dx_leaf->dl_list.de_count)) {
		mlog(ML_ERROR, "DX Dir: %llu, Asked to rebalance empty leaf: "
		     "%llu, %d\n", (unsigned long long)OCFS2_I(dir)->ip_blkno,
		     (unsigned long long)leaf_blkno, num_used);
		ret = -EIO;
		goto out;
	}

	orig_dx_leaves = ocfs2_dx_dir_kmalloc_leaves(osb->sb, &num_dx_leaves);
	if (!orig_dx_leaves) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	new_dx_leaves = ocfs2_dx_dir_kmalloc_leaves(osb->sb, NULL);
	if (!new_dx_leaves) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_lock_allocators(dir, &et, 1, 0, &data_ac, &meta_ac);
	if (ret) {
		if (ret != -ENOSPC)
			mlog_errno(ret);
		goto out;
	}

	credits = ocfs2_dx_dir_rebalance_credits(osb, dx_root);
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(ret);
		goto out;
	}

	if (vfs_dq_alloc_space_nodirty(dir,
				       ocfs2_clusters_to_bytes(dir->i_sb, 1))) {
		ret = -EDQUOT;
		goto out_commit;
	}
	did_quota = 1;

	ret = ocfs2_journal_access_dl(handle, dir, dx_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * This block is changing anyway, so we can sort it in place.
	 */
	sort(dx_leaf->dl_list.de_entries, num_used,
	     sizeof(struct ocfs2_dx_entry), dx_leaf_sort_cmp,
	     dx_leaf_sort_swap);

	ret = ocfs2_journal_dirty(handle, dx_leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_dx_dir_find_leaf_split(dx_leaf, leaf_cpos, insert_hash,
					   &split_hash);
	if (ret) {
		mlog_errno(ret);
		goto  out_commit;
	}

	mlog(0, "Split leaf (%u) at %u, insert major hash is %u\n",
	     leaf_cpos, split_hash, insert_hash);

	/*
	 * We have to carefully order operations here. There are items
	 * which want to be in the new cluster before insert, but in
	 * order to put those items in the new cluster, we alter the
	 * old cluster. A failure to insert gets nasty.
	 *
	 * So, start by reserving writes to the old
	 * cluster. ocfs2_dx_dir_new_cluster will reserve writes on
	 * the new cluster for us, before inserting it. The insert
	 * won't happen if there's an error before that. Once the
	 * insert is done then, we can transfer from one leaf into the
	 * other without fear of hitting any error.
	 */

	/*
	 * The leaf transfer wants some scratch space so that we don't
	 * wind up doing a bunch of expensive memmove().
	 */
	tmp_dx_leaf = kmalloc(osb->sb->s_blocksize, GFP_NOFS);
	if (!tmp_dx_leaf) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out_commit;
	}

	orig_leaves_start = ocfs2_block_to_cluster_start(dir->i_sb, leaf_blkno);
	ret = ocfs2_read_dx_leaves(dir, orig_leaves_start, num_dx_leaves,
				   orig_dx_leaves);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	for (i = 0; i < num_dx_leaves; i++) {
		ret = ocfs2_journal_access_dl(handle, dir, orig_dx_leaves[i],
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
	}

	cpos = split_hash;
	ret = ocfs2_dx_dir_new_cluster(dir, &et, cpos, handle,
				       data_ac, meta_ac, new_dx_leaves,
				       num_dx_leaves);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ocfs2_dx_dir_transfer_leaf(dir, split_hash, handle, tmp_dx_leaf,
				   orig_dx_leaves, new_dx_leaves, num_dx_leaves);

out_commit:
	if (ret < 0 && did_quota)
		vfs_dq_free_space_nodirty(dir,
				ocfs2_clusters_to_bytes(dir->i_sb, 1));

	ocfs2_commit_trans(osb, handle);

out:
	if (orig_dx_leaves || new_dx_leaves) {
		for (i = 0; i < num_dx_leaves; i++) {
			if (orig_dx_leaves)
				brelse(orig_dx_leaves[i]);
			if (new_dx_leaves)
				brelse(new_dx_leaves[i]);
		}
		kfree(orig_dx_leaves);
		kfree(new_dx_leaves);
	}

	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);

	kfree(tmp_dx_leaf);
	return ret;
}

static int ocfs2_find_dir_space_dx(struct ocfs2_super *osb, struct inode *dir,
				   struct buffer_head *di_bh,
				   struct buffer_head *dx_root_bh,
				   const char *name, int namelen,
				   struct ocfs2_dir_lookup_result *lookup)
{
	int ret, rebalanced = 0;
	struct ocfs2_dx_root_block *dx_root;
	struct buffer_head *dx_leaf_bh = NULL;
	struct ocfs2_dx_leaf *dx_leaf;
	u64 blkno;
	u32 leaf_cpos;

	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;

restart_search:
	ret = ocfs2_dx_dir_lookup(dir, &dx_root->dr_list, &lookup->dl_hinfo,
				  &leaf_cpos, &blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_dx_leaf(dir, blkno, &dx_leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_bh->b_data;

	if (le16_to_cpu(dx_leaf->dl_list.de_num_used) >=
	    le16_to_cpu(dx_leaf->dl_list.de_count)) {
		if (rebalanced) {
			/*
			 * Rebalancing should have provided us with
			 * space in an appropriate leaf.
			 *
			 * XXX: Is this an abnormal condition then?
			 * Should we print a message here?
			 */
			ret = -ENOSPC;
			goto out;
		}

		ret = ocfs2_dx_dir_rebalance(osb, dir, dx_root_bh, dx_leaf_bh,
					     &lookup->dl_hinfo, leaf_cpos,
					     blkno);
		if (ret) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
			goto out;
		}

		/*
		 * Restart the lookup. The rebalance might have
		 * changed which block our item fits into. Mark our
		 * progress, so we only execute this once.
		 */
		brelse(dx_leaf_bh);
		dx_leaf_bh = NULL;
		rebalanced = 1;
		goto restart_search;
	}

	lookup->dl_dx_leaf_bh = dx_leaf_bh;
	dx_leaf_bh = NULL;

out:
	brelse(dx_leaf_bh);
	return ret;
}

static int ocfs2_search_dx_free_list(struct inode *dir,
				     struct buffer_head *dx_root_bh,
				     int namelen,
				     struct ocfs2_dir_lookup_result *lookup)
{
	int ret = -ENOSPC;
	struct buffer_head *leaf_bh = NULL, *prev_leaf_bh = NULL;
	struct ocfs2_dir_block_trailer *db;
	u64 next_block;
	int rec_len = OCFS2_DIR_REC_LEN(namelen);
	struct ocfs2_dx_root_block *dx_root;

	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;
	next_block = le64_to_cpu(dx_root->dr_free_blk);

	while (next_block) {
		brelse(prev_leaf_bh);
		prev_leaf_bh = leaf_bh;
		leaf_bh = NULL;

		ret = ocfs2_read_dir_block_direct(dir, next_block, &leaf_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		db = ocfs2_trailer_from_bh(leaf_bh, dir->i_sb);
		if (rec_len <= le16_to_cpu(db->db_free_rec_len)) {
			lookup->dl_leaf_bh = leaf_bh;
			lookup->dl_prev_leaf_bh = prev_leaf_bh;
			leaf_bh = NULL;
			prev_leaf_bh = NULL;
			break;
		}

		next_block = le64_to_cpu(db->db_free_next);
	}

	if (!next_block)
		ret = -ENOSPC;

out:

	brelse(leaf_bh);
	brelse(prev_leaf_bh);
	return ret;
}

static int ocfs2_expand_inline_dx_root(struct inode *dir,
				       struct buffer_head *dx_root_bh)
{
	int ret, num_dx_leaves, i, j, did_quota = 0;
	struct buffer_head **dx_leaves = NULL;
	struct ocfs2_extent_tree et;
	u64 insert_blkno;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	handle_t *handle = NULL;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_entry_list *entry_list;
	struct ocfs2_dx_entry *dx_entry;
	struct ocfs2_dx_leaf *target_leaf;

	ret = ocfs2_reserve_clusters(osb, 1, &data_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dx_leaves = ocfs2_dx_dir_kmalloc_leaves(osb->sb, &num_dx_leaves);
	if (!dx_leaves) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	handle = ocfs2_start_trans(osb, ocfs2_calc_dxi_expand_credits(osb->sb));
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	if (vfs_dq_alloc_space_nodirty(dir,
				       ocfs2_clusters_to_bytes(osb->sb, 1))) {
		ret = -EDQUOT;
		goto out_commit;
	}
	did_quota = 1;

	/*
	 * We do this up front, before the allocation, so that a
	 * failure to add the dx_root_bh to the journal won't result
	 * us losing clusters.
	 */
	ret = ocfs2_journal_access_dr(handle, dir, dx_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = __ocfs2_dx_dir_new_cluster(dir, 0, handle, data_ac, dx_leaves,
					 num_dx_leaves, &insert_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * Transfer the entries from our dx_root into the appropriate
	 * block
	 */
	dx_root = (struct ocfs2_dx_root_block *) dx_root_bh->b_data;
	entry_list = &dx_root->dr_entries;

	for (i = 0; i < le16_to_cpu(entry_list->de_num_used); i++) {
		dx_entry = &entry_list->de_entries[i];

		j = __ocfs2_dx_dir_hash_idx(osb,
					    le32_to_cpu(dx_entry->dx_minor_hash));
		target_leaf = (struct ocfs2_dx_leaf *)dx_leaves[j]->b_data;

		ocfs2_dx_dir_leaf_insert_tail(target_leaf, dx_entry);

		/* Each leaf has been passed to the journal already
		 * via __ocfs2_dx_dir_new_cluster() */
	}

	dx_root->dr_flags &= ~OCFS2_DX_FLAG_INLINE;
	memset(&dx_root->dr_list, 0, osb->sb->s_blocksize -
	       offsetof(struct ocfs2_dx_root_block, dr_list));
	dx_root->dr_list.l_count =
		cpu_to_le16(ocfs2_extent_recs_per_dx_root(osb->sb));

	/* This should never fail considering we start with an empty
	 * dx_root. */
	ocfs2_init_dx_root_extent_tree(&et, dir, dx_root_bh);
	ret = ocfs2_insert_extent(osb, handle, dir, &et, 0,
				  insert_blkno, 1, 0, NULL);
	if (ret)
		mlog_errno(ret);
	did_quota = 0;

	ocfs2_journal_dirty(handle, dx_root_bh);

out_commit:
	if (ret < 0 && did_quota)
		vfs_dq_free_space_nodirty(dir,
					  ocfs2_clusters_to_bytes(dir->i_sb, 1));

	ocfs2_commit_trans(osb, handle);

out:
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);

	if (dx_leaves) {
		for (i = 0; i < num_dx_leaves; i++)
			brelse(dx_leaves[i]);
		kfree(dx_leaves);
	}
	return ret;
}

static int ocfs2_inline_dx_has_space(struct buffer_head *dx_root_bh)
{
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_entry_list *entry_list;

	dx_root = (struct ocfs2_dx_root_block *) dx_root_bh->b_data;
	entry_list = &dx_root->dr_entries;

	if (le16_to_cpu(entry_list->de_num_used) >=
	    le16_to_cpu(entry_list->de_count))
		return -ENOSPC;

	return 0;
}

static int ocfs2_prepare_dx_dir_for_insert(struct inode *dir,
					   struct buffer_head *di_bh,
					   const char *name,
					   int namelen,
					   struct ocfs2_dir_lookup_result *lookup)
{
	int ret, free_dx_root = 1;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct buffer_head *dx_root_bh = NULL;
	struct buffer_head *leaf_bh = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_dx_root_block *dx_root;

	ret = ocfs2_read_dx_root(dir, di, &dx_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;
	if (le32_to_cpu(dx_root->dr_num_entries) == OCFS2_DX_ENTRIES_MAX) {
		ret = -ENOSPC;
		mlog_errno(ret);
		goto out;
	}

	if (ocfs2_dx_root_inline(dx_root)) {
		ret = ocfs2_inline_dx_has_space(dx_root_bh);

		if (ret == 0)
			goto search_el;

		/*
		 * We ran out of room in the root block. Expand it to
		 * an extent, then allow ocfs2_find_dir_space_dx to do
		 * the rest.
		 */
		ret = ocfs2_expand_inline_dx_root(dir, dx_root_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * Insert preparation for an indexed directory is split into two
	 * steps. The call to find_dir_space_dx reserves room in the index for
	 * an additional item. If we run out of space there, it's a real error
	 * we can't continue on.
	 */
	ret = ocfs2_find_dir_space_dx(osb, dir, di_bh, dx_root_bh, name,
				      namelen, lookup);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

search_el:
	/*
	 * Next, we need to find space in the unindexed tree. This call
	 * searches using the free space linked list. If the unindexed tree
	 * lacks sufficient space, we'll expand it below. The expansion code
	 * is smart enough to add any new blocks to the free space list.
	 */
	ret = ocfs2_search_dx_free_list(dir, dx_root_bh, namelen, lookup);
	if (ret && ret != -ENOSPC) {
		mlog_errno(ret);
		goto out;
	}

	/* Do this up here - ocfs2_extend_dir might need the dx_root */
	lookup->dl_dx_root_bh = dx_root_bh;
	free_dx_root = 0;

	if (ret == -ENOSPC) {
		ret = ocfs2_extend_dir(osb, dir, di_bh, 1, lookup, &leaf_bh);

		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * We make the assumption here that new leaf blocks are added
		 * to the front of our free list.
		 */
		lookup->dl_prev_leaf_bh = NULL;
		lookup->dl_leaf_bh = leaf_bh;
	}

out:
	if (free_dx_root)
		brelse(dx_root_bh);
	return ret;
}

/*
 * Get a directory ready for insert. Any directory allocation required
 * happens here. Success returns zero, and enough context in the dir
 * lookup result that ocfs2_add_entry() will be able complete the task
 * with minimal performance impact.
 */
int ocfs2_prepare_dir_for_insert(struct ocfs2_super *osb,
				 struct inode *dir,
				 struct buffer_head *parent_fe_bh,
				 const char *name,
				 int namelen,
				 struct ocfs2_dir_lookup_result *lookup)
{
	int ret;
	unsigned int blocks_wanted = 1;
	struct buffer_head *bh = NULL;

	mlog(0, "getting ready to insert namelen %d into dir %llu\n",
	     namelen, (unsigned long long)OCFS2_I(dir)->ip_blkno);

	if (!namelen) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Do this up front to reduce confusion.
	 *
	 * The directory might start inline, then be turned into an
	 * indexed one, in which case we'd need to hash deep inside
	 * ocfs2_find_dir_space_id(). Since
	 * ocfs2_prepare_dx_dir_for_insert() also needs this hash
	 * done, there seems no point in spreading out the calls. We
	 * can optimize away the case where the file system doesn't
	 * support indexing.
	 */
	if (ocfs2_supports_indexed_dirs(osb))
		ocfs2_dx_dir_name_hash(dir, name, namelen, &lookup->dl_hinfo);

	if (ocfs2_dir_indexed(dir)) {
		ret = ocfs2_prepare_dx_dir_for_insert(dir, parent_fe_bh,
						      name, namelen, lookup);
		if (ret)
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
				       lookup, &bh);
		if (ret) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
			goto out;
		}

		BUG_ON(!bh);
	}

	lookup->dl_leaf_bh = bh;
	bh = NULL;
out:
	brelse(bh);
	return ret;
}

static int ocfs2_dx_dir_remove_index(struct inode *dir,
				     struct buffer_head *di_bh,
				     struct buffer_head *dx_root_bh)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_dx_root_block *dx_root;
	struct inode *dx_alloc_inode = NULL;
	struct buffer_head *dx_alloc_bh = NULL;
	handle_t *handle;
	u64 blk;
	u16 bit;
	u64 bg_blkno;

	dx_root = (struct ocfs2_dx_root_block *) dx_root_bh->b_data;

	dx_alloc_inode = ocfs2_get_system_file_inode(osb,
					EXTENT_ALLOC_SYSTEM_INODE,
					le16_to_cpu(dx_root->dr_suballoc_slot));
	if (!dx_alloc_inode) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	mutex_lock(&dx_alloc_inode->i_mutex);

	ret = ocfs2_inode_lock(dx_alloc_inode, &dx_alloc_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out_mutex;
	}

	handle = ocfs2_start_trans(osb, OCFS2_DX_ROOT_REMOVE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_unlock;
	}

	ret = ocfs2_journal_access_di(handle, dir, di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	OCFS2_I(dir)->ip_dyn_features &= ~OCFS2_INDEXED_DIR_FL;
	di->i_dyn_features = cpu_to_le16(OCFS2_I(dir)->ip_dyn_features);
	di->i_dx_root = cpu_to_le64(0ULL);

	ocfs2_journal_dirty(handle, di_bh);

	blk = le64_to_cpu(dx_root->dr_blkno);
	bit = le16_to_cpu(dx_root->dr_suballoc_bit);
	bg_blkno = ocfs2_which_suballoc_group(blk, bit);
	ret = ocfs2_free_suballoc_bits(handle, dx_alloc_inode, dx_alloc_bh,
				       bit, bg_blkno, 1);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);

out_unlock:
	ocfs2_inode_unlock(dx_alloc_inode, 1);

out_mutex:
	mutex_unlock(&dx_alloc_inode->i_mutex);
	brelse(dx_alloc_bh);
out:
	iput(dx_alloc_inode);
	return ret;
}

int ocfs2_dx_dir_truncate(struct inode *dir, struct buffer_head *di_bh)
{
	int ret;
	unsigned int uninitialized_var(clen);
	u32 major_hash = UINT_MAX, p_cpos, uninitialized_var(cpos);
	u64 uninitialized_var(blkno);
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct buffer_head *dx_root_bh = NULL;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_extent_tree et;

	ocfs2_init_dealloc_ctxt(&dealloc);

	if (!ocfs2_dir_indexed(dir))
		return 0;

	ret = ocfs2_read_dx_root(dir, di, &dx_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	dx_root = (struct ocfs2_dx_root_block *)dx_root_bh->b_data;

	if (ocfs2_dx_root_inline(dx_root))
		goto remove_index;

	ocfs2_init_dx_root_extent_tree(&et, dir, dx_root_bh);

	/* XXX: What if dr_clusters is too large? */
	while (le32_to_cpu(dx_root->dr_clusters)) {
		ret = ocfs2_dx_dir_lookup_rec(dir, &dx_root->dr_list,
					      major_hash, &cpos, &blkno, &clen);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		p_cpos = ocfs2_blocks_to_clusters(dir->i_sb, blkno);

		ret = ocfs2_remove_btree_range(dir, &et, cpos, p_cpos, clen,
					       &dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (cpos == 0)
			break;

		major_hash = cpos - 1;
	}

remove_index:
	ret = ocfs2_dx_dir_remove_index(dir, di_bh, dx_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_remove_from_cache(dir, dx_root_bh);
out:
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &dealloc);

	brelse(dx_root_bh);
	return ret;
}
