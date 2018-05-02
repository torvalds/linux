/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.c
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * CREDITS:
 * Lots of code in this file is copy from linux/fs/ext3/xattr.c.
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/splice.h>
#include <linux/mount.h>
#include <linux/writeback.h>
#include <linux/falloc.h>
#include <linux/sort.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/security.h>

#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "blockcheck.h"
#include "dlmglue.h"
#include "file.h"
#include "symlink.h"
#include "sysfile.h"
#include "inode.h"
#include "journal.h"
#include "ocfs2_fs.h"
#include "suballoc.h"
#include "uptodate.h"
#include "buffer_head_io.h"
#include "super.h"
#include "xattr.h"
#include "refcounttree.h"
#include "acl.h"
#include "ocfs2_trace.h"

struct ocfs2_xattr_def_value_root {
	struct ocfs2_xattr_value_root	xv;
	struct ocfs2_extent_rec		er;
};

struct ocfs2_xattr_bucket {
	/* The inode these xattrs are associated with */
	struct inode *bu_inode;

	/* The actual buffers that make up the bucket */
	struct buffer_head *bu_bhs[OCFS2_XATTR_MAX_BLOCKS_PER_BUCKET];

	/* How many blocks make up one bucket for this filesystem */
	int bu_blocks;
};

struct ocfs2_xattr_set_ctxt {
	handle_t *handle;
	struct ocfs2_alloc_context *meta_ac;
	struct ocfs2_alloc_context *data_ac;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	int set_abort;
};

#define OCFS2_XATTR_ROOT_SIZE	(sizeof(struct ocfs2_xattr_def_value_root))
#define OCFS2_XATTR_INLINE_SIZE	80
#define OCFS2_XATTR_HEADER_GAP	4
#define OCFS2_XATTR_FREE_IN_IBODY	(OCFS2_MIN_XATTR_INLINE_SIZE \
					 - sizeof(struct ocfs2_xattr_header) \
					 - OCFS2_XATTR_HEADER_GAP)
#define OCFS2_XATTR_FREE_IN_BLOCK(ptr)	((ptr)->i_sb->s_blocksize \
					 - sizeof(struct ocfs2_xattr_block) \
					 - sizeof(struct ocfs2_xattr_header) \
					 - OCFS2_XATTR_HEADER_GAP)

static struct ocfs2_xattr_def_value_root def_xv = {
	.xv.xr_list.l_count = cpu_to_le16(1),
};

const struct xattr_handler *ocfs2_xattr_handlers[] = {
	&ocfs2_xattr_user_handler,
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
	&ocfs2_xattr_trusted_handler,
	&ocfs2_xattr_security_handler,
	NULL
};

static const struct xattr_handler *ocfs2_xattr_handler_map[OCFS2_XATTR_MAX] = {
	[OCFS2_XATTR_INDEX_USER]	= &ocfs2_xattr_user_handler,
	[OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS]
					= &posix_acl_access_xattr_handler,
	[OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT]
					= &posix_acl_default_xattr_handler,
	[OCFS2_XATTR_INDEX_TRUSTED]	= &ocfs2_xattr_trusted_handler,
	[OCFS2_XATTR_INDEX_SECURITY]	= &ocfs2_xattr_security_handler,
};

struct ocfs2_xattr_info {
	int		xi_name_index;
	const char	*xi_name;
	int		xi_name_len;
	const void	*xi_value;
	size_t		xi_value_len;
};

struct ocfs2_xattr_search {
	struct buffer_head *inode_bh;
	/*
	 * xattr_bh point to the block buffer head which has extended attribute
	 * when extended attribute in inode, xattr_bh is equal to inode_bh.
	 */
	struct buffer_head *xattr_bh;
	struct ocfs2_xattr_header *header;
	struct ocfs2_xattr_bucket *bucket;
	void *base;
	void *end;
	struct ocfs2_xattr_entry *here;
	int not_found;
};

/* Operations on struct ocfs2_xa_entry */
struct ocfs2_xa_loc;
struct ocfs2_xa_loc_operations {
	/*
	 * Journal functions
	 */
	int (*xlo_journal_access)(handle_t *handle, struct ocfs2_xa_loc *loc,
				  int type);
	void (*xlo_journal_dirty)(handle_t *handle, struct ocfs2_xa_loc *loc);

	/*
	 * Return a pointer to the appropriate buffer in loc->xl_storage
	 * at the given offset from loc->xl_header.
	 */
	void *(*xlo_offset_pointer)(struct ocfs2_xa_loc *loc, int offset);

	/* Can we reuse the existing entry for the new value? */
	int (*xlo_can_reuse)(struct ocfs2_xa_loc *loc,
			     struct ocfs2_xattr_info *xi);

	/* How much space is needed for the new value? */
	int (*xlo_check_space)(struct ocfs2_xa_loc *loc,
			       struct ocfs2_xattr_info *xi);

	/*
	 * Return the offset of the first name+value pair.  This is
	 * the start of our downward-filling free space.
	 */
	int (*xlo_get_free_start)(struct ocfs2_xa_loc *loc);

	/*
	 * Remove the name+value at this location.  Do whatever is
	 * appropriate with the remaining name+value pairs.
	 */
	void (*xlo_wipe_namevalue)(struct ocfs2_xa_loc *loc);

	/* Fill xl_entry with a new entry */
	void (*xlo_add_entry)(struct ocfs2_xa_loc *loc, u32 name_hash);

	/* Add name+value storage to an entry */
	void (*xlo_add_namevalue)(struct ocfs2_xa_loc *loc, int size);

	/*
	 * Initialize the value buf's access and bh fields for this entry.
	 * ocfs2_xa_fill_value_buf() will handle the xv pointer.
	 */
	void (*xlo_fill_value_buf)(struct ocfs2_xa_loc *loc,
				   struct ocfs2_xattr_value_buf *vb);
};

/*
 * Describes an xattr entry location.  This is a memory structure
 * tracking the on-disk structure.
 */
struct ocfs2_xa_loc {
	/* This xattr belongs to this inode */
	struct inode *xl_inode;

	/* The ocfs2_xattr_header inside the on-disk storage. Not NULL. */
	struct ocfs2_xattr_header *xl_header;

	/* Bytes from xl_header to the end of the storage */
	int xl_size;

	/*
	 * The ocfs2_xattr_entry this location describes.  If this is
	 * NULL, this location describes the on-disk structure where it
	 * would have been.
	 */
	struct ocfs2_xattr_entry *xl_entry;

	/*
	 * Internal housekeeping
	 */

	/* Buffer(s) containing this entry */
	void *xl_storage;

	/* Operations on the storage backing this location */
	const struct ocfs2_xa_loc_operations *xl_ops;
};

/*
 * Convenience functions to calculate how much space is needed for a
 * given name+value pair
 */
static int namevalue_size(int name_len, uint64_t value_len)
{
	if (value_len > OCFS2_XATTR_INLINE_SIZE)
		return OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_ROOT_SIZE;
	else
		return OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_SIZE(value_len);
}

static int namevalue_size_xi(struct ocfs2_xattr_info *xi)
{
	return namevalue_size(xi->xi_name_len, xi->xi_value_len);
}

static int namevalue_size_xe(struct ocfs2_xattr_entry *xe)
{
	u64 value_len = le64_to_cpu(xe->xe_value_size);

	BUG_ON((value_len > OCFS2_XATTR_INLINE_SIZE) &&
	       ocfs2_xattr_is_local(xe));
	return namevalue_size(xe->xe_name_len, value_len);
}


static int ocfs2_xattr_bucket_get_name_value(struct super_block *sb,
					     struct ocfs2_xattr_header *xh,
					     int index,
					     int *block_off,
					     int *new_offset);

static int ocfs2_xattr_block_find(struct inode *inode,
				  int name_index,
				  const char *name,
				  struct ocfs2_xattr_search *xs);
static int ocfs2_xattr_index_block_find(struct inode *inode,
					struct buffer_head *root_bh,
					int name_index,
					const char *name,
					struct ocfs2_xattr_search *xs);

static int ocfs2_xattr_tree_list_index_block(struct inode *inode,
					struct buffer_head *blk_bh,
					char *buffer,
					size_t buffer_size);

static int ocfs2_xattr_create_index_block(struct inode *inode,
					  struct ocfs2_xattr_search *xs,
					  struct ocfs2_xattr_set_ctxt *ctxt);

static int ocfs2_xattr_set_entry_index_block(struct inode *inode,
					     struct ocfs2_xattr_info *xi,
					     struct ocfs2_xattr_search *xs,
					     struct ocfs2_xattr_set_ctxt *ctxt);

typedef int (xattr_tree_rec_func)(struct inode *inode,
				  struct buffer_head *root_bh,
				  u64 blkno, u32 cpos, u32 len, void *para);
static int ocfs2_iterate_xattr_index_block(struct inode *inode,
					   struct buffer_head *root_bh,
					   xattr_tree_rec_func *rec_func,
					   void *para);
static int ocfs2_delete_xattr_in_bucket(struct inode *inode,
					struct ocfs2_xattr_bucket *bucket,
					void *para);
static int ocfs2_rm_xattr_cluster(struct inode *inode,
				  struct buffer_head *root_bh,
				  u64 blkno,
				  u32 cpos,
				  u32 len,
				  void *para);

static int ocfs2_mv_xattr_buckets(struct inode *inode, handle_t *handle,
				  u64 src_blk, u64 last_blk, u64 to_blk,
				  unsigned int start_bucket,
				  u32 *first_hash);
static int ocfs2_prepare_refcount_xattr(struct inode *inode,
					struct ocfs2_dinode *di,
					struct ocfs2_xattr_info *xi,
					struct ocfs2_xattr_search *xis,
					struct ocfs2_xattr_search *xbs,
					struct ocfs2_refcount_tree **ref_tree,
					int *meta_need,
					int *credits);
static int ocfs2_get_xattr_tree_value_root(struct super_block *sb,
					   struct ocfs2_xattr_bucket *bucket,
					   int offset,
					   struct ocfs2_xattr_value_root **xv,
					   struct buffer_head **bh);

static inline u16 ocfs2_xattr_buckets_per_cluster(struct ocfs2_super *osb)
{
	return (1 << osb->s_clustersize_bits) / OCFS2_XATTR_BUCKET_SIZE;
}

static inline u16 ocfs2_blocks_per_xattr_bucket(struct super_block *sb)
{
	return OCFS2_XATTR_BUCKET_SIZE / (1 << sb->s_blocksize_bits);
}

#define bucket_blkno(_b) ((_b)->bu_bhs[0]->b_blocknr)
#define bucket_block(_b, _n) ((_b)->bu_bhs[(_n)]->b_data)
#define bucket_xh(_b) ((struct ocfs2_xattr_header *)bucket_block((_b), 0))

static struct ocfs2_xattr_bucket *ocfs2_xattr_bucket_new(struct inode *inode)
{
	struct ocfs2_xattr_bucket *bucket;
	int blks = ocfs2_blocks_per_xattr_bucket(inode->i_sb);

	BUG_ON(blks > OCFS2_XATTR_MAX_BLOCKS_PER_BUCKET);

	bucket = kzalloc(sizeof(struct ocfs2_xattr_bucket), GFP_NOFS);
	if (bucket) {
		bucket->bu_inode = inode;
		bucket->bu_blocks = blks;
	}

	return bucket;
}

static void ocfs2_xattr_bucket_relse(struct ocfs2_xattr_bucket *bucket)
{
	int i;

	for (i = 0; i < bucket->bu_blocks; i++) {
		brelse(bucket->bu_bhs[i]);
		bucket->bu_bhs[i] = NULL;
	}
}

static void ocfs2_xattr_bucket_free(struct ocfs2_xattr_bucket *bucket)
{
	if (bucket) {
		ocfs2_xattr_bucket_relse(bucket);
		bucket->bu_inode = NULL;
		kfree(bucket);
	}
}

/*
 * A bucket that has never been written to disk doesn't need to be
 * read.  We just need the buffer_heads.  Don't call this for
 * buckets that are already on disk.  ocfs2_read_xattr_bucket() initializes
 * them fully.
 */
static int ocfs2_init_xattr_bucket(struct ocfs2_xattr_bucket *bucket,
				   u64 xb_blkno, int new)
{
	int i, rc = 0;

	for (i = 0; i < bucket->bu_blocks; i++) {
		bucket->bu_bhs[i] = sb_getblk(bucket->bu_inode->i_sb,
					      xb_blkno + i);
		if (!bucket->bu_bhs[i]) {
			rc = -ENOMEM;
			mlog_errno(rc);
			break;
		}

		if (!ocfs2_buffer_uptodate(INODE_CACHE(bucket->bu_inode),
					   bucket->bu_bhs[i])) {
			if (new)
				ocfs2_set_new_buffer_uptodate(INODE_CACHE(bucket->bu_inode),
							      bucket->bu_bhs[i]);
			else {
				set_buffer_uptodate(bucket->bu_bhs[i]);
				ocfs2_set_buffer_uptodate(INODE_CACHE(bucket->bu_inode),
							  bucket->bu_bhs[i]);
			}
		}
	}

	if (rc)
		ocfs2_xattr_bucket_relse(bucket);
	return rc;
}

/* Read the xattr bucket at xb_blkno */
static int ocfs2_read_xattr_bucket(struct ocfs2_xattr_bucket *bucket,
				   u64 xb_blkno)
{
	int rc;

	rc = ocfs2_read_blocks(INODE_CACHE(bucket->bu_inode), xb_blkno,
			       bucket->bu_blocks, bucket->bu_bhs, 0,
			       NULL);
	if (!rc) {
		spin_lock(&OCFS2_SB(bucket->bu_inode->i_sb)->osb_xattr_lock);
		rc = ocfs2_validate_meta_ecc_bhs(bucket->bu_inode->i_sb,
						 bucket->bu_bhs,
						 bucket->bu_blocks,
						 &bucket_xh(bucket)->xh_check);
		spin_unlock(&OCFS2_SB(bucket->bu_inode->i_sb)->osb_xattr_lock);
		if (rc)
			mlog_errno(rc);
	}

	if (rc)
		ocfs2_xattr_bucket_relse(bucket);
	return rc;
}

static int ocfs2_xattr_bucket_journal_access(handle_t *handle,
					     struct ocfs2_xattr_bucket *bucket,
					     int type)
{
	int i, rc = 0;

	for (i = 0; i < bucket->bu_blocks; i++) {
		rc = ocfs2_journal_access(handle,
					  INODE_CACHE(bucket->bu_inode),
					  bucket->bu_bhs[i], type);
		if (rc) {
			mlog_errno(rc);
			break;
		}
	}

	return rc;
}

static void ocfs2_xattr_bucket_journal_dirty(handle_t *handle,
					     struct ocfs2_xattr_bucket *bucket)
{
	int i;

	spin_lock(&OCFS2_SB(bucket->bu_inode->i_sb)->osb_xattr_lock);
	ocfs2_compute_meta_ecc_bhs(bucket->bu_inode->i_sb,
				   bucket->bu_bhs, bucket->bu_blocks,
				   &bucket_xh(bucket)->xh_check);
	spin_unlock(&OCFS2_SB(bucket->bu_inode->i_sb)->osb_xattr_lock);

	for (i = 0; i < bucket->bu_blocks; i++)
		ocfs2_journal_dirty(handle, bucket->bu_bhs[i]);
}

static void ocfs2_xattr_bucket_copy_data(struct ocfs2_xattr_bucket *dest,
					 struct ocfs2_xattr_bucket *src)
{
	int i;
	int blocksize = src->bu_inode->i_sb->s_blocksize;

	BUG_ON(dest->bu_blocks != src->bu_blocks);
	BUG_ON(dest->bu_inode != src->bu_inode);

	for (i = 0; i < src->bu_blocks; i++) {
		memcpy(bucket_block(dest, i), bucket_block(src, i),
		       blocksize);
	}
}

static int ocfs2_validate_xattr_block(struct super_block *sb,
				      struct buffer_head *bh)
{
	int rc;
	struct ocfs2_xattr_block *xb =
		(struct ocfs2_xattr_block *)bh->b_data;

	trace_ocfs2_validate_xattr_block((unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We know any error is
	 * local to this block.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &xb->xb_check);
	if (rc)
		return rc;

	/*
	 * Errors after here are fatal
	 */

	if (!OCFS2_IS_VALID_XATTR_BLOCK(xb)) {
		return ocfs2_error(sb,
				   "Extended attribute block #%llu has bad signature %.*s\n",
				   (unsigned long long)bh->b_blocknr, 7,
				   xb->xb_signature);
	}

	if (le64_to_cpu(xb->xb_blkno) != bh->b_blocknr) {
		return ocfs2_error(sb,
				   "Extended attribute block #%llu has an invalid xb_blkno of %llu\n",
				   (unsigned long long)bh->b_blocknr,
				   (unsigned long long)le64_to_cpu(xb->xb_blkno));
	}

	if (le32_to_cpu(xb->xb_fs_generation) != OCFS2_SB(sb)->fs_generation) {
		return ocfs2_error(sb,
				   "Extended attribute block #%llu has an invalid xb_fs_generation of #%u\n",
				   (unsigned long long)bh->b_blocknr,
				   le32_to_cpu(xb->xb_fs_generation));
	}

	return 0;
}

static int ocfs2_read_xattr_block(struct inode *inode, u64 xb_blkno,
				  struct buffer_head **bh)
{
	int rc;
	struct buffer_head *tmp = *bh;

	rc = ocfs2_read_block(INODE_CACHE(inode), xb_blkno, &tmp,
			      ocfs2_validate_xattr_block);

	/* If ocfs2_read_block() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

static inline const char *ocfs2_xattr_prefix(int name_index)
{
	const struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < OCFS2_XATTR_MAX)
		handler = ocfs2_xattr_handler_map[name_index];
	return handler ? xattr_prefix(handler) : NULL;
}

static u32 ocfs2_xattr_name_hash(struct inode *inode,
				 const char *name,
				 int name_len)
{
	/* Get hash value of uuid from super block */
	u32 hash = OCFS2_SB(inode->i_sb)->uuid_hash;
	int i;

	/* hash extended attribute name */
	for (i = 0; i < name_len; i++) {
		hash = (hash << OCFS2_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - OCFS2_HASH_SHIFT)) ^
		       *name++;
	}

	return hash;
}

static int ocfs2_xattr_entry_real_size(int name_len, size_t value_len)
{
	return namevalue_size(name_len, value_len) +
		sizeof(struct ocfs2_xattr_entry);
}

static int ocfs2_xi_entry_usage(struct ocfs2_xattr_info *xi)
{
	return namevalue_size_xi(xi) +
		sizeof(struct ocfs2_xattr_entry);
}

static int ocfs2_xe_entry_usage(struct ocfs2_xattr_entry *xe)
{
	return namevalue_size_xe(xe) +
		sizeof(struct ocfs2_xattr_entry);
}

int ocfs2_calc_security_init(struct inode *dir,
			     struct ocfs2_security_xattr_info *si,
			     int *want_clusters,
			     int *xattr_credits,
			     struct ocfs2_alloc_context **xattr_ac)
{
	int ret = 0;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	int s_size = ocfs2_xattr_entry_real_size(strlen(si->name),
						 si->value_len);

	/*
	 * The max space of security xattr taken inline is
	 * 256(name) + 80(value) + 16(entry) = 352 bytes,
	 * So reserve one metadata block for it is ok.
	 */
	if (dir->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE ||
	    s_size > OCFS2_XATTR_FREE_IN_IBODY) {
		ret = ocfs2_reserve_new_metadata_blocks(osb, 1, xattr_ac);
		if (ret) {
			mlog_errno(ret);
			return ret;
		}
		*xattr_credits += OCFS2_XATTR_BLOCK_CREATE_CREDITS;
	}

	/* reserve clusters for xattr value which will be set in B tree*/
	if (si->value_len > OCFS2_XATTR_INLINE_SIZE) {
		int new_clusters = ocfs2_clusters_for_bytes(dir->i_sb,
							    si->value_len);

		*xattr_credits += ocfs2_clusters_to_blocks(dir->i_sb,
							   new_clusters);
		*want_clusters += new_clusters;
	}
	return ret;
}

int ocfs2_calc_xattr_init(struct inode *dir,
			  struct buffer_head *dir_bh,
			  umode_t mode,
			  struct ocfs2_security_xattr_info *si,
			  int *want_clusters,
			  int *xattr_credits,
			  int *want_meta)
{
	int ret = 0;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	int s_size = 0, a_size = 0, acl_len = 0, new_clusters;

	if (si->enable)
		s_size = ocfs2_xattr_entry_real_size(strlen(si->name),
						     si->value_len);

	if (osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL) {
		down_read(&OCFS2_I(dir)->ip_xattr_sem);
		acl_len = ocfs2_xattr_get_nolock(dir, dir_bh,
					OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT,
					"", NULL, 0);
		up_read(&OCFS2_I(dir)->ip_xattr_sem);
		if (acl_len > 0) {
			a_size = ocfs2_xattr_entry_real_size(0, acl_len);
			if (S_ISDIR(mode))
				a_size <<= 1;
		} else if (acl_len != 0 && acl_len != -ENODATA) {
			ret = acl_len;
			mlog_errno(ret);
			return ret;
		}
	}

	if (!(s_size + a_size))
		return ret;

	/*
	 * The max space of security xattr taken inline is
	 * 256(name) + 80(value) + 16(entry) = 352 bytes,
	 * The max space of acl xattr taken inline is
	 * 80(value) + 16(entry) * 2(if directory) = 192 bytes,
	 * when blocksize = 512, may reserve one more cluser for
	 * xattr bucket, otherwise reserve one metadata block
	 * for them is ok.
	 * If this is a new directory with inline data,
	 * we choose to reserve the entire inline area for
	 * directory contents and force an external xattr block.
	 */
	if (dir->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE ||
	    (S_ISDIR(mode) && ocfs2_supports_inline_data(osb)) ||
	    (s_size + a_size) > OCFS2_XATTR_FREE_IN_IBODY) {
		*want_meta = *want_meta + 1;
		*xattr_credits += OCFS2_XATTR_BLOCK_CREATE_CREDITS;
	}

	if (dir->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE &&
	    (s_size + a_size) > OCFS2_XATTR_FREE_IN_BLOCK(dir)) {
		*want_clusters += 1;
		*xattr_credits += ocfs2_blocks_per_xattr_bucket(dir->i_sb);
	}

	/*
	 * reserve credits and clusters for xattrs which has large value
	 * and have to be set outside
	 */
	if (si->enable && si->value_len > OCFS2_XATTR_INLINE_SIZE) {
		new_clusters = ocfs2_clusters_for_bytes(dir->i_sb,
							si->value_len);
		*xattr_credits += ocfs2_clusters_to_blocks(dir->i_sb,
							   new_clusters);
		*want_clusters += new_clusters;
	}
	if (osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL &&
	    acl_len > OCFS2_XATTR_INLINE_SIZE) {
		/* for directory, it has DEFAULT and ACCESS two types of acls */
		new_clusters = (S_ISDIR(mode) ? 2 : 1) *
				ocfs2_clusters_for_bytes(dir->i_sb, acl_len);
		*xattr_credits += ocfs2_clusters_to_blocks(dir->i_sb,
							   new_clusters);
		*want_clusters += new_clusters;
	}

	return ret;
}

static int ocfs2_xattr_extend_allocation(struct inode *inode,
					 u32 clusters_to_add,
					 struct ocfs2_xattr_value_buf *vb,
					 struct ocfs2_xattr_set_ctxt *ctxt)
{
	int status = 0, credits;
	handle_t *handle = ctxt->handle;
	enum ocfs2_alloc_restarted why;
	u32 prev_clusters, logical_start = le32_to_cpu(vb->vb_xv->xr_clusters);
	struct ocfs2_extent_tree et;

	ocfs2_init_xattr_value_extent_tree(&et, INODE_CACHE(inode), vb);

	while (clusters_to_add) {
		trace_ocfs2_xattr_extend_allocation(clusters_to_add);

		status = vb->vb_access(handle, INODE_CACHE(inode), vb->vb_bh,
				       OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			break;
		}

		prev_clusters = le32_to_cpu(vb->vb_xv->xr_clusters);
		status = ocfs2_add_clusters_in_btree(handle,
						     &et,
						     &logical_start,
						     clusters_to_add,
						     0,
						     ctxt->data_ac,
						     ctxt->meta_ac,
						     &why);
		if ((status < 0) && (status != -EAGAIN)) {
			if (status != -ENOSPC)
				mlog_errno(status);
			break;
		}

		ocfs2_journal_dirty(handle, vb->vb_bh);

		clusters_to_add -= le32_to_cpu(vb->vb_xv->xr_clusters) -
					 prev_clusters;

		if (why != RESTART_NONE && clusters_to_add) {
			/*
			 * We can only fail in case the alloc file doesn't give
			 * up enough clusters.
			 */
			BUG_ON(why == RESTART_META);

			credits = ocfs2_calc_extend_credits(inode->i_sb,
							    &vb->vb_xv->xr_list);
			status = ocfs2_extend_trans(handle, credits);
			if (status < 0) {
				status = -ENOMEM;
				mlog_errno(status);
				break;
			}
		}
	}

	return status;
}

static int __ocfs2_remove_xattr_range(struct inode *inode,
				      struct ocfs2_xattr_value_buf *vb,
				      u32 cpos, u32 phys_cpos, u32 len,
				      unsigned int ext_flags,
				      struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	u64 phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
	handle_t *handle = ctxt->handle;
	struct ocfs2_extent_tree et;

	ocfs2_init_xattr_value_extent_tree(&et, INODE_CACHE(inode), vb);

	ret = vb->vb_access(handle, INODE_CACHE(inode), vb->vb_bh,
			    OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_remove_extent(handle, &et, cpos, len, ctxt->meta_ac,
				  &ctxt->dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	le32_add_cpu(&vb->vb_xv->xr_clusters, -len);
	ocfs2_journal_dirty(handle, vb->vb_bh);

	if (ext_flags & OCFS2_EXT_REFCOUNTED)
		ret = ocfs2_decrease_refcount(inode, handle,
					ocfs2_blocks_to_clusters(inode->i_sb,
								 phys_blkno),
					len, ctxt->meta_ac, &ctxt->dealloc, 1);
	else
		ret = ocfs2_cache_cluster_dealloc(&ctxt->dealloc,
						  phys_blkno, len);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_xattr_shrink_size(struct inode *inode,
				   u32 old_clusters,
				   u32 new_clusters,
				   struct ocfs2_xattr_value_buf *vb,
				   struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret = 0;
	unsigned int ext_flags;
	u32 trunc_len, cpos, phys_cpos, alloc_size;
	u64 block;

	if (old_clusters <= new_clusters)
		return 0;

	cpos = new_clusters;
	trunc_len = old_clusters - new_clusters;
	while (trunc_len) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &phys_cpos,
					       &alloc_size,
					       &vb->vb_xv->xr_list, &ext_flags);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (alloc_size > trunc_len)
			alloc_size = trunc_len;

		ret = __ocfs2_remove_xattr_range(inode, vb, cpos,
						 phys_cpos, alloc_size,
						 ext_flags, ctxt);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		block = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
		ocfs2_remove_xattr_clusters_from_cache(INODE_CACHE(inode),
						       block, alloc_size);
		cpos += alloc_size;
		trunc_len -= alloc_size;
	}

out:
	return ret;
}

static int ocfs2_xattr_value_truncate(struct inode *inode,
				      struct ocfs2_xattr_value_buf *vb,
				      int len,
				      struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	u32 new_clusters = ocfs2_clusters_for_bytes(inode->i_sb, len);
	u32 old_clusters = le32_to_cpu(vb->vb_xv->xr_clusters);

	if (new_clusters == old_clusters)
		return 0;

	if (new_clusters > old_clusters)
		ret = ocfs2_xattr_extend_allocation(inode,
						    new_clusters - old_clusters,
						    vb, ctxt);
	else
		ret = ocfs2_xattr_shrink_size(inode,
					      old_clusters, new_clusters,
					      vb, ctxt);

	return ret;
}

static int ocfs2_xattr_list_entry(struct super_block *sb,
				  char *buffer, size_t size,
				  size_t *result, int type,
				  const char *name, int name_len)
{
	char *p = buffer + *result;
	const char *prefix;
	int prefix_len;
	int total_len;

	switch(type) {
	case OCFS2_XATTR_INDEX_USER:
		if (OCFS2_SB(sb)->s_mount_opt & OCFS2_MOUNT_NOUSERXATTR)
			return 0;
		break;

	case OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS:
	case OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT:
		if (!(sb->s_flags & SB_POSIXACL))
			return 0;
		break;

	case OCFS2_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return 0;
		break;
	}

	prefix = ocfs2_xattr_prefix(type);
	if (!prefix)
		return 0;
	prefix_len = strlen(prefix);
	total_len = prefix_len + name_len + 1;
	*result += total_len;

	/* we are just looking for how big our buffer needs to be */
	if (!size)
		return 0;

	if (*result > size)
		return -ERANGE;

	memcpy(p, prefix, prefix_len);
	memcpy(p + prefix_len, name, name_len);
	p[prefix_len + name_len] = '\0';

	return 0;
}

static int ocfs2_xattr_list_entries(struct inode *inode,
				    struct ocfs2_xattr_header *header,
				    char *buffer, size_t buffer_size)
{
	size_t result = 0;
	int i, type, ret;
	const char *name;

	for (i = 0 ; i < le16_to_cpu(header->xh_count); i++) {
		struct ocfs2_xattr_entry *entry = &header->xh_entries[i];
		type = ocfs2_xattr_get_type(entry);
		name = (const char *)header +
			le16_to_cpu(entry->xe_name_offset);

		ret = ocfs2_xattr_list_entry(inode->i_sb,
					     buffer, buffer_size,
					     &result, type, name,
					     entry->xe_name_len);
		if (ret)
			return ret;
	}

	return result;
}

int ocfs2_has_inline_xattr_value_outside(struct inode *inode,
					 struct ocfs2_dinode *di)
{
	struct ocfs2_xattr_header *xh;
	int i;

	xh = (struct ocfs2_xattr_header *)
		 ((void *)di + inode->i_sb->s_blocksize -
		 le16_to_cpu(di->i_xattr_inline_size));

	for (i = 0; i < le16_to_cpu(xh->xh_count); i++)
		if (!ocfs2_xattr_is_local(&xh->xh_entries[i]))
			return 1;

	return 0;
}

static int ocfs2_xattr_ibody_list(struct inode *inode,
				  struct ocfs2_dinode *di,
				  char *buffer,
				  size_t buffer_size)
{
	struct ocfs2_xattr_header *header = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	int ret = 0;

	if (!(oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL))
		return ret;

	header = (struct ocfs2_xattr_header *)
		 ((void *)di + inode->i_sb->s_blocksize -
		 le16_to_cpu(di->i_xattr_inline_size));

	ret = ocfs2_xattr_list_entries(inode, header, buffer, buffer_size);

	return ret;
}

static int ocfs2_xattr_block_list(struct inode *inode,
				  struct ocfs2_dinode *di,
				  char *buffer,
				  size_t buffer_size)
{
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_xattr_block *xb;
	int ret = 0;

	if (!di->i_xattr_loc)
		return ret;

	ret = ocfs2_read_xattr_block(inode, le64_to_cpu(di->i_xattr_loc),
				     &blk_bh);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	xb = (struct ocfs2_xattr_block *)blk_bh->b_data;
	if (!(le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED)) {
		struct ocfs2_xattr_header *header = &xb->xb_attrs.xb_header;
		ret = ocfs2_xattr_list_entries(inode, header,
					       buffer, buffer_size);
	} else
		ret = ocfs2_xattr_tree_list_index_block(inode, blk_bh,
						   buffer, buffer_size);

	brelse(blk_bh);

	return ret;
}

ssize_t ocfs2_listxattr(struct dentry *dentry,
			char *buffer,
			size_t size)
{
	int ret = 0, i_ret = 0, b_ret = 0;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(d_inode(dentry));

	if (!ocfs2_supports_xattr(OCFS2_SB(dentry->d_sb)))
		return -EOPNOTSUPP;

	if (!(oi->ip_dyn_features & OCFS2_HAS_XATTR_FL))
		return ret;

	ret = ocfs2_inode_lock(d_inode(dentry), &di_bh, 0);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;

	down_read(&oi->ip_xattr_sem);
	i_ret = ocfs2_xattr_ibody_list(d_inode(dentry), di, buffer, size);
	if (i_ret < 0)
		b_ret = 0;
	else {
		if (buffer) {
			buffer += i_ret;
			size -= i_ret;
		}
		b_ret = ocfs2_xattr_block_list(d_inode(dentry), di,
					       buffer, size);
		if (b_ret < 0)
			i_ret = 0;
	}
	up_read(&oi->ip_xattr_sem);
	ocfs2_inode_unlock(d_inode(dentry), 0);

	brelse(di_bh);

	return i_ret + b_ret;
}

static int ocfs2_xattr_find_entry(int name_index,
				  const char *name,
				  struct ocfs2_xattr_search *xs)
{
	struct ocfs2_xattr_entry *entry;
	size_t name_len;
	int i, cmp = 1;

	if (name == NULL)
		return -EINVAL;

	name_len = strlen(name);
	entry = xs->here;
	for (i = 0; i < le16_to_cpu(xs->header->xh_count); i++) {
		cmp = name_index - ocfs2_xattr_get_type(entry);
		if (!cmp)
			cmp = name_len - entry->xe_name_len;
		if (!cmp)
			cmp = memcmp(name, (xs->base +
				     le16_to_cpu(entry->xe_name_offset)),
				     name_len);
		if (cmp == 0)
			break;
		entry += 1;
	}
	xs->here = entry;

	return cmp ? -ENODATA : 0;
}

static int ocfs2_xattr_get_value_outside(struct inode *inode,
					 struct ocfs2_xattr_value_root *xv,
					 void *buffer,
					 size_t len)
{
	u32 cpos, p_cluster, num_clusters, bpc, clusters;
	u64 blkno;
	int i, ret = 0;
	size_t cplen, blocksize;
	struct buffer_head *bh = NULL;
	struct ocfs2_extent_list *el;

	el = &xv->xr_list;
	clusters = le32_to_cpu(xv->xr_clusters);
	bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	blocksize = inode->i_sb->s_blocksize;

	cpos = 0;
	while (cpos < clusters) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &p_cluster,
					       &num_clusters, el, NULL);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		blkno = ocfs2_clusters_to_blocks(inode->i_sb, p_cluster);
		/* Copy ocfs2_xattr_value */
		for (i = 0; i < num_clusters * bpc; i++, blkno++) {
			ret = ocfs2_read_block(INODE_CACHE(inode), blkno,
					       &bh, NULL);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			cplen = len >= blocksize ? blocksize : len;
			memcpy(buffer, bh->b_data, cplen);
			len -= cplen;
			buffer += cplen;

			brelse(bh);
			bh = NULL;
			if (len == 0)
				break;
		}
		cpos += num_clusters;
	}
out:
	return ret;
}

static int ocfs2_xattr_ibody_get(struct inode *inode,
				 int name_index,
				 const char *name,
				 void *buffer,
				 size_t buffer_size,
				 struct ocfs2_xattr_search *xs)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)xs->inode_bh->b_data;
	struct ocfs2_xattr_value_root *xv;
	size_t size;
	int ret = 0;

	if (!(oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL))
		return -ENODATA;

	xs->end = (void *)di + inode->i_sb->s_blocksize;
	xs->header = (struct ocfs2_xattr_header *)
			(xs->end - le16_to_cpu(di->i_xattr_inline_size));
	xs->base = (void *)xs->header;
	xs->here = xs->header->xh_entries;

	ret = ocfs2_xattr_find_entry(name_index, name, xs);
	if (ret)
		return ret;
	size = le64_to_cpu(xs->here->xe_value_size);
	if (buffer) {
		if (size > buffer_size)
			return -ERANGE;
		if (ocfs2_xattr_is_local(xs->here)) {
			memcpy(buffer, (void *)xs->base +
			       le16_to_cpu(xs->here->xe_name_offset) +
			       OCFS2_XATTR_SIZE(xs->here->xe_name_len), size);
		} else {
			xv = (struct ocfs2_xattr_value_root *)
				(xs->base + le16_to_cpu(
				 xs->here->xe_name_offset) +
				OCFS2_XATTR_SIZE(xs->here->xe_name_len));
			ret = ocfs2_xattr_get_value_outside(inode, xv,
							    buffer, size);
			if (ret < 0) {
				mlog_errno(ret);
				return ret;
			}
		}
	}

	return size;
}

static int ocfs2_xattr_block_get(struct inode *inode,
				 int name_index,
				 const char *name,
				 void *buffer,
				 size_t buffer_size,
				 struct ocfs2_xattr_search *xs)
{
	struct ocfs2_xattr_block *xb;
	struct ocfs2_xattr_value_root *xv;
	size_t size;
	int ret = -ENODATA, name_offset, name_len, i;
	int uninitialized_var(block_off);

	xs->bucket = ocfs2_xattr_bucket_new(inode);
	if (!xs->bucket) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto cleanup;
	}

	ret = ocfs2_xattr_block_find(inode, name_index, name, xs);
	if (ret) {
		mlog_errno(ret);
		goto cleanup;
	}

	if (xs->not_found) {
		ret = -ENODATA;
		goto cleanup;
	}

	xb = (struct ocfs2_xattr_block *)xs->xattr_bh->b_data;
	size = le64_to_cpu(xs->here->xe_value_size);
	if (buffer) {
		ret = -ERANGE;
		if (size > buffer_size)
			goto cleanup;

		name_offset = le16_to_cpu(xs->here->xe_name_offset);
		name_len = OCFS2_XATTR_SIZE(xs->here->xe_name_len);
		i = xs->here - xs->header->xh_entries;

		if (le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED) {
			ret = ocfs2_xattr_bucket_get_name_value(inode->i_sb,
								bucket_xh(xs->bucket),
								i,
								&block_off,
								&name_offset);
			if (ret) {
				mlog_errno(ret);
				goto cleanup;
			}
			xs->base = bucket_block(xs->bucket, block_off);
		}
		if (ocfs2_xattr_is_local(xs->here)) {
			memcpy(buffer, (void *)xs->base +
			       name_offset + name_len, size);
		} else {
			xv = (struct ocfs2_xattr_value_root *)
				(xs->base + name_offset + name_len);
			ret = ocfs2_xattr_get_value_outside(inode, xv,
							    buffer, size);
			if (ret < 0) {
				mlog_errno(ret);
				goto cleanup;
			}
		}
	}
	ret = size;
cleanup:
	ocfs2_xattr_bucket_free(xs->bucket);

	brelse(xs->xattr_bh);
	xs->xattr_bh = NULL;
	return ret;
}

int ocfs2_xattr_get_nolock(struct inode *inode,
			   struct buffer_head *di_bh,
			   int name_index,
			   const char *name,
			   void *buffer,
			   size_t buffer_size)
{
	int ret;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_xattr_search xis = {
		.not_found = -ENODATA,
	};
	struct ocfs2_xattr_search xbs = {
		.not_found = -ENODATA,
	};

	if (!ocfs2_supports_xattr(OCFS2_SB(inode->i_sb)))
		return -EOPNOTSUPP;

	if (!(oi->ip_dyn_features & OCFS2_HAS_XATTR_FL))
		return -ENODATA;

	xis.inode_bh = xbs.inode_bh = di_bh;
	di = (struct ocfs2_dinode *)di_bh->b_data;

	ret = ocfs2_xattr_ibody_get(inode, name_index, name, buffer,
				    buffer_size, &xis);
	if (ret == -ENODATA && di->i_xattr_loc)
		ret = ocfs2_xattr_block_get(inode, name_index, name, buffer,
					    buffer_size, &xbs);

	return ret;
}

/* ocfs2_xattr_get()
 *
 * Copy an extended attribute into the buffer provided.
 * Buffer is NULL to compute the size of buffer required.
 */
static int ocfs2_xattr_get(struct inode *inode,
			   int name_index,
			   const char *name,
			   void *buffer,
			   size_t buffer_size)
{
	int ret, had_lock;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_lock_holder oh;

	had_lock = ocfs2_inode_lock_tracker(inode, &di_bh, 0, &oh);
	if (had_lock < 0) {
		mlog_errno(had_lock);
		return had_lock;
	}
	down_read(&OCFS2_I(inode)->ip_xattr_sem);
	ret = ocfs2_xattr_get_nolock(inode, di_bh, name_index,
				     name, buffer, buffer_size);
	up_read(&OCFS2_I(inode)->ip_xattr_sem);

	ocfs2_inode_unlock_tracker(inode, 0, &oh, had_lock);

	brelse(di_bh);

	return ret;
}

static int __ocfs2_xattr_set_value_outside(struct inode *inode,
					   handle_t *handle,
					   struct ocfs2_xattr_value_buf *vb,
					   const void *value,
					   int value_len)
{
	int ret = 0, i, cp_len;
	u16 blocksize = inode->i_sb->s_blocksize;
	u32 p_cluster, num_clusters;
	u32 cpos = 0, bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	u32 clusters = ocfs2_clusters_for_bytes(inode->i_sb, value_len);
	u64 blkno;
	struct buffer_head *bh = NULL;
	unsigned int ext_flags;
	struct ocfs2_xattr_value_root *xv = vb->vb_xv;

	BUG_ON(clusters > le32_to_cpu(xv->xr_clusters));

	while (cpos < clusters) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &p_cluster,
					       &num_clusters, &xv->xr_list,
					       &ext_flags);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		BUG_ON(ext_flags & OCFS2_EXT_REFCOUNTED);

		blkno = ocfs2_clusters_to_blocks(inode->i_sb, p_cluster);

		for (i = 0; i < num_clusters * bpc; i++, blkno++) {
			ret = ocfs2_read_block(INODE_CACHE(inode), blkno,
					       &bh, NULL);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_journal_access(handle,
						   INODE_CACHE(inode),
						   bh,
						   OCFS2_JOURNAL_ACCESS_WRITE);
			if (ret < 0) {
				mlog_errno(ret);
				goto out;
			}

			cp_len = value_len > blocksize ? blocksize : value_len;
			memcpy(bh->b_data, value, cp_len);
			value_len -= cp_len;
			value += cp_len;
			if (cp_len < blocksize)
				memset(bh->b_data + cp_len, 0,
				       blocksize - cp_len);

			ocfs2_journal_dirty(handle, bh);
			brelse(bh);
			bh = NULL;

			/*
			 * XXX: do we need to empty all the following
			 * blocks in this cluster?
			 */
			if (!value_len)
				break;
		}
		cpos += num_clusters;
	}
out:
	brelse(bh);

	return ret;
}

static int ocfs2_xa_check_space_helper(int needed_space, int free_start,
				       int num_entries)
{
	int free_space;

	if (!needed_space)
		return 0;

	free_space = free_start -
		sizeof(struct ocfs2_xattr_header) -
		(num_entries * sizeof(struct ocfs2_xattr_entry)) -
		OCFS2_XATTR_HEADER_GAP;
	if (free_space < 0)
		return -EIO;
	if (free_space < needed_space)
		return -ENOSPC;

	return 0;
}

static int ocfs2_xa_journal_access(handle_t *handle, struct ocfs2_xa_loc *loc,
				   int type)
{
	return loc->xl_ops->xlo_journal_access(handle, loc, type);
}

static void ocfs2_xa_journal_dirty(handle_t *handle, struct ocfs2_xa_loc *loc)
{
	loc->xl_ops->xlo_journal_dirty(handle, loc);
}

/* Give a pointer into the storage for the given offset */
static void *ocfs2_xa_offset_pointer(struct ocfs2_xa_loc *loc, int offset)
{
	BUG_ON(offset >= loc->xl_size);
	return loc->xl_ops->xlo_offset_pointer(loc, offset);
}

/*
 * Wipe the name+value pair and allow the storage to reclaim it.  This
 * must be followed by either removal of the entry or a call to
 * ocfs2_xa_add_namevalue().
 */
static void ocfs2_xa_wipe_namevalue(struct ocfs2_xa_loc *loc)
{
	loc->xl_ops->xlo_wipe_namevalue(loc);
}

/*
 * Find lowest offset to a name+value pair.  This is the start of our
 * downward-growing free space.
 */
static int ocfs2_xa_get_free_start(struct ocfs2_xa_loc *loc)
{
	return loc->xl_ops->xlo_get_free_start(loc);
}

/* Can we reuse loc->xl_entry for xi? */
static int ocfs2_xa_can_reuse_entry(struct ocfs2_xa_loc *loc,
				    struct ocfs2_xattr_info *xi)
{
	return loc->xl_ops->xlo_can_reuse(loc, xi);
}

/* How much free space is needed to set the new value */
static int ocfs2_xa_check_space(struct ocfs2_xa_loc *loc,
				struct ocfs2_xattr_info *xi)
{
	return loc->xl_ops->xlo_check_space(loc, xi);
}

static void ocfs2_xa_add_entry(struct ocfs2_xa_loc *loc, u32 name_hash)
{
	loc->xl_ops->xlo_add_entry(loc, name_hash);
	loc->xl_entry->xe_name_hash = cpu_to_le32(name_hash);
	/*
	 * We can't leave the new entry's xe_name_offset at zero or
	 * add_namevalue() will go nuts.  We set it to the size of our
	 * storage so that it can never be less than any other entry.
	 */
	loc->xl_entry->xe_name_offset = cpu_to_le16(loc->xl_size);
}

static void ocfs2_xa_add_namevalue(struct ocfs2_xa_loc *loc,
				   struct ocfs2_xattr_info *xi)
{
	int size = namevalue_size_xi(xi);
	int nameval_offset;
	char *nameval_buf;

	loc->xl_ops->xlo_add_namevalue(loc, size);
	loc->xl_entry->xe_value_size = cpu_to_le64(xi->xi_value_len);
	loc->xl_entry->xe_name_len = xi->xi_name_len;
	ocfs2_xattr_set_type(loc->xl_entry, xi->xi_name_index);
	ocfs2_xattr_set_local(loc->xl_entry,
			      xi->xi_value_len <= OCFS2_XATTR_INLINE_SIZE);

	nameval_offset = le16_to_cpu(loc->xl_entry->xe_name_offset);
	nameval_buf = ocfs2_xa_offset_pointer(loc, nameval_offset);
	memset(nameval_buf, 0, size);
	memcpy(nameval_buf, xi->xi_name, xi->xi_name_len);
}

static void ocfs2_xa_fill_value_buf(struct ocfs2_xa_loc *loc,
				    struct ocfs2_xattr_value_buf *vb)
{
	int nameval_offset = le16_to_cpu(loc->xl_entry->xe_name_offset);
	int name_size = OCFS2_XATTR_SIZE(loc->xl_entry->xe_name_len);

	/* Value bufs are for value trees */
	BUG_ON(ocfs2_xattr_is_local(loc->xl_entry));
	BUG_ON(namevalue_size_xe(loc->xl_entry) !=
	       (name_size + OCFS2_XATTR_ROOT_SIZE));

	loc->xl_ops->xlo_fill_value_buf(loc, vb);
	vb->vb_xv =
		(struct ocfs2_xattr_value_root *)ocfs2_xa_offset_pointer(loc,
							nameval_offset +
							name_size);
}

static int ocfs2_xa_block_journal_access(handle_t *handle,
					 struct ocfs2_xa_loc *loc, int type)
{
	struct buffer_head *bh = loc->xl_storage;
	ocfs2_journal_access_func access;

	if (loc->xl_size == (bh->b_size -
			     offsetof(struct ocfs2_xattr_block,
				      xb_attrs.xb_header)))
		access = ocfs2_journal_access_xb;
	else
		access = ocfs2_journal_access_di;
	return access(handle, INODE_CACHE(loc->xl_inode), bh, type);
}

static void ocfs2_xa_block_journal_dirty(handle_t *handle,
					 struct ocfs2_xa_loc *loc)
{
	struct buffer_head *bh = loc->xl_storage;

	ocfs2_journal_dirty(handle, bh);
}

static void *ocfs2_xa_block_offset_pointer(struct ocfs2_xa_loc *loc,
					   int offset)
{
	return (char *)loc->xl_header + offset;
}

static int ocfs2_xa_block_can_reuse(struct ocfs2_xa_loc *loc,
				    struct ocfs2_xattr_info *xi)
{
	/*
	 * Block storage is strict.  If the sizes aren't exact, we will
	 * remove the old one and reinsert the new.
	 */
	return namevalue_size_xe(loc->xl_entry) ==
		namevalue_size_xi(xi);
}

static int ocfs2_xa_block_get_free_start(struct ocfs2_xa_loc *loc)
{
	struct ocfs2_xattr_header *xh = loc->xl_header;
	int i, count = le16_to_cpu(xh->xh_count);
	int offset, free_start = loc->xl_size;

	for (i = 0; i < count; i++) {
		offset = le16_to_cpu(xh->xh_entries[i].xe_name_offset);
		if (offset < free_start)
			free_start = offset;
	}

	return free_start;
}

static int ocfs2_xa_block_check_space(struct ocfs2_xa_loc *loc,
				      struct ocfs2_xattr_info *xi)
{
	int count = le16_to_cpu(loc->xl_header->xh_count);
	int free_start = ocfs2_xa_get_free_start(loc);
	int needed_space = ocfs2_xi_entry_usage(xi);

	/*
	 * Block storage will reclaim the original entry before inserting
	 * the new value, so we only need the difference.  If the new
	 * entry is smaller than the old one, we don't need anything.
	 */
	if (loc->xl_entry) {
		/* Don't need space if we're reusing! */
		if (ocfs2_xa_can_reuse_entry(loc, xi))
			needed_space = 0;
		else
			needed_space -= ocfs2_xe_entry_usage(loc->xl_entry);
	}
	if (needed_space < 0)
		needed_space = 0;
	return ocfs2_xa_check_space_helper(needed_space, free_start, count);
}

/*
 * Block storage for xattrs keeps the name+value pairs compacted.  When
 * we remove one, we have to shift any that preceded it towards the end.
 */
static void ocfs2_xa_block_wipe_namevalue(struct ocfs2_xa_loc *loc)
{
	int i, offset;
	int namevalue_offset, first_namevalue_offset, namevalue_size;
	struct ocfs2_xattr_entry *entry = loc->xl_entry;
	struct ocfs2_xattr_header *xh = loc->xl_header;
	int count = le16_to_cpu(xh->xh_count);

	namevalue_offset = le16_to_cpu(entry->xe_name_offset);
	namevalue_size = namevalue_size_xe(entry);
	first_namevalue_offset = ocfs2_xa_get_free_start(loc);

	/* Shift the name+value pairs */
	memmove((char *)xh + first_namevalue_offset + namevalue_size,
		(char *)xh + first_namevalue_offset,
		namevalue_offset - first_namevalue_offset);
	memset((char *)xh + first_namevalue_offset, 0, namevalue_size);

	/* Now tell xh->xh_entries about it */
	for (i = 0; i < count; i++) {
		offset = le16_to_cpu(xh->xh_entries[i].xe_name_offset);
		if (offset <= namevalue_offset)
			le16_add_cpu(&xh->xh_entries[i].xe_name_offset,
				     namevalue_size);
	}

	/*
	 * Note that we don't update xh_free_start or xh_name_value_len
	 * because they're not used in block-stored xattrs.
	 */
}

static void ocfs2_xa_block_add_entry(struct ocfs2_xa_loc *loc, u32 name_hash)
{
	int count = le16_to_cpu(loc->xl_header->xh_count);
	loc->xl_entry = &(loc->xl_header->xh_entries[count]);
	le16_add_cpu(&loc->xl_header->xh_count, 1);
	memset(loc->xl_entry, 0, sizeof(struct ocfs2_xattr_entry));
}

static void ocfs2_xa_block_add_namevalue(struct ocfs2_xa_loc *loc, int size)
{
	int free_start = ocfs2_xa_get_free_start(loc);

	loc->xl_entry->xe_name_offset = cpu_to_le16(free_start - size);
}

static void ocfs2_xa_block_fill_value_buf(struct ocfs2_xa_loc *loc,
					  struct ocfs2_xattr_value_buf *vb)
{
	struct buffer_head *bh = loc->xl_storage;

	if (loc->xl_size == (bh->b_size -
			     offsetof(struct ocfs2_xattr_block,
				      xb_attrs.xb_header)))
		vb->vb_access = ocfs2_journal_access_xb;
	else
		vb->vb_access = ocfs2_journal_access_di;
	vb->vb_bh = bh;
}

/*
 * Operations for xattrs stored in blocks.  This includes inline inode
 * storage and unindexed ocfs2_xattr_blocks.
 */
static const struct ocfs2_xa_loc_operations ocfs2_xa_block_loc_ops = {
	.xlo_journal_access	= ocfs2_xa_block_journal_access,
	.xlo_journal_dirty	= ocfs2_xa_block_journal_dirty,
	.xlo_offset_pointer	= ocfs2_xa_block_offset_pointer,
	.xlo_check_space	= ocfs2_xa_block_check_space,
	.xlo_can_reuse		= ocfs2_xa_block_can_reuse,
	.xlo_get_free_start	= ocfs2_xa_block_get_free_start,
	.xlo_wipe_namevalue	= ocfs2_xa_block_wipe_namevalue,
	.xlo_add_entry		= ocfs2_xa_block_add_entry,
	.xlo_add_namevalue	= ocfs2_xa_block_add_namevalue,
	.xlo_fill_value_buf	= ocfs2_xa_block_fill_value_buf,
};

static int ocfs2_xa_bucket_journal_access(handle_t *handle,
					  struct ocfs2_xa_loc *loc, int type)
{
	struct ocfs2_xattr_bucket *bucket = loc->xl_storage;

	return ocfs2_xattr_bucket_journal_access(handle, bucket, type);
}

static void ocfs2_xa_bucket_journal_dirty(handle_t *handle,
					  struct ocfs2_xa_loc *loc)
{
	struct ocfs2_xattr_bucket *bucket = loc->xl_storage;

	ocfs2_xattr_bucket_journal_dirty(handle, bucket);
}

static void *ocfs2_xa_bucket_offset_pointer(struct ocfs2_xa_loc *loc,
					    int offset)
{
	struct ocfs2_xattr_bucket *bucket = loc->xl_storage;
	int block, block_offset;

	/* The header is at the front of the bucket */
	block = offset >> loc->xl_inode->i_sb->s_blocksize_bits;
	block_offset = offset % loc->xl_inode->i_sb->s_blocksize;

	return bucket_block(bucket, block) + block_offset;
}

static int ocfs2_xa_bucket_can_reuse(struct ocfs2_xa_loc *loc,
				     struct ocfs2_xattr_info *xi)
{
	return namevalue_size_xe(loc->xl_entry) >=
		namevalue_size_xi(xi);
}

static int ocfs2_xa_bucket_get_free_start(struct ocfs2_xa_loc *loc)
{
	struct ocfs2_xattr_bucket *bucket = loc->xl_storage;
	return le16_to_cpu(bucket_xh(bucket)->xh_free_start);
}

static int ocfs2_bucket_align_free_start(struct super_block *sb,
					 int free_start, int size)
{
	/*
	 * We need to make sure that the name+value pair fits within
	 * one block.
	 */
	if (((free_start - size) >> sb->s_blocksize_bits) !=
	    ((free_start - 1) >> sb->s_blocksize_bits))
		free_start -= free_start % sb->s_blocksize;

	return free_start;
}

static int ocfs2_xa_bucket_check_space(struct ocfs2_xa_loc *loc,
				       struct ocfs2_xattr_info *xi)
{
	int rc;
	int count = le16_to_cpu(loc->xl_header->xh_count);
	int free_start = ocfs2_xa_get_free_start(loc);
	int needed_space = ocfs2_xi_entry_usage(xi);
	int size = namevalue_size_xi(xi);
	struct super_block *sb = loc->xl_inode->i_sb;

	/*
	 * Bucket storage does not reclaim name+value pairs it cannot
	 * reuse.  They live as holes until the bucket fills, and then
	 * the bucket is defragmented.  However, the bucket can reclaim
	 * the ocfs2_xattr_entry.
	 */
	if (loc->xl_entry) {
		/* Don't need space if we're reusing! */
		if (ocfs2_xa_can_reuse_entry(loc, xi))
			needed_space = 0;
		else
			needed_space -= sizeof(struct ocfs2_xattr_entry);
	}
	BUG_ON(needed_space < 0);

	if (free_start < size) {
		if (needed_space)
			return -ENOSPC;
	} else {
		/*
		 * First we check if it would fit in the first place.
		 * Below, we align the free start to a block.  This may
		 * slide us below the minimum gap.  By checking unaligned
		 * first, we avoid that error.
		 */
		rc = ocfs2_xa_check_space_helper(needed_space, free_start,
						 count);
		if (rc)
			return rc;
		free_start = ocfs2_bucket_align_free_start(sb, free_start,
							   size);
	}
	return ocfs2_xa_check_space_helper(needed_space, free_start, count);
}

static void ocfs2_xa_bucket_wipe_namevalue(struct ocfs2_xa_loc *loc)
{
	le16_add_cpu(&loc->xl_header->xh_name_value_len,
		     -namevalue_size_xe(loc->xl_entry));
}

static void ocfs2_xa_bucket_add_entry(struct ocfs2_xa_loc *loc, u32 name_hash)
{
	struct ocfs2_xattr_header *xh = loc->xl_header;
	int count = le16_to_cpu(xh->xh_count);
	int low = 0, high = count - 1, tmp;
	struct ocfs2_xattr_entry *tmp_xe;

	/*
	 * We keep buckets sorted by name_hash, so we need to find
	 * our insert place.
	 */
	while (low <= high && count) {
		tmp = (low + high) / 2;
		tmp_xe = &xh->xh_entries[tmp];

		if (name_hash > le32_to_cpu(tmp_xe->xe_name_hash))
			low = tmp + 1;
		else if (name_hash < le32_to_cpu(tmp_xe->xe_name_hash))
			high = tmp - 1;
		else {
			low = tmp;
			break;
		}
	}

	if (low != count)
		memmove(&xh->xh_entries[low + 1],
			&xh->xh_entries[low],
			((count - low) * sizeof(struct ocfs2_xattr_entry)));

	le16_add_cpu(&xh->xh_count, 1);
	loc->xl_entry = &xh->xh_entries[low];
	memset(loc->xl_entry, 0, sizeof(struct ocfs2_xattr_entry));
}

static void ocfs2_xa_bucket_add_namevalue(struct ocfs2_xa_loc *loc, int size)
{
	int free_start = ocfs2_xa_get_free_start(loc);
	struct ocfs2_xattr_header *xh = loc->xl_header;
	struct super_block *sb = loc->xl_inode->i_sb;
	int nameval_offset;

	free_start = ocfs2_bucket_align_free_start(sb, free_start, size);
	nameval_offset = free_start - size;
	loc->xl_entry->xe_name_offset = cpu_to_le16(nameval_offset);
	xh->xh_free_start = cpu_to_le16(nameval_offset);
	le16_add_cpu(&xh->xh_name_value_len, size);

}

static void ocfs2_xa_bucket_fill_value_buf(struct ocfs2_xa_loc *loc,
					   struct ocfs2_xattr_value_buf *vb)
{
	struct ocfs2_xattr_bucket *bucket = loc->xl_storage;
	struct super_block *sb = loc->xl_inode->i_sb;
	int nameval_offset = le16_to_cpu(loc->xl_entry->xe_name_offset);
	int size = namevalue_size_xe(loc->xl_entry);
	int block_offset = nameval_offset >> sb->s_blocksize_bits;

	/* Values are not allowed to straddle block boundaries */
	BUG_ON(block_offset !=
	       ((nameval_offset + size - 1) >> sb->s_blocksize_bits));
	/* We expect the bucket to be filled in */
	BUG_ON(!bucket->bu_bhs[block_offset]);

	vb->vb_access = ocfs2_journal_access;
	vb->vb_bh = bucket->bu_bhs[block_offset];
}

/* Operations for xattrs stored in buckets. */
static const struct ocfs2_xa_loc_operations ocfs2_xa_bucket_loc_ops = {
	.xlo_journal_access	= ocfs2_xa_bucket_journal_access,
	.xlo_journal_dirty	= ocfs2_xa_bucket_journal_dirty,
	.xlo_offset_pointer	= ocfs2_xa_bucket_offset_pointer,
	.xlo_check_space	= ocfs2_xa_bucket_check_space,
	.xlo_can_reuse		= ocfs2_xa_bucket_can_reuse,
	.xlo_get_free_start	= ocfs2_xa_bucket_get_free_start,
	.xlo_wipe_namevalue	= ocfs2_xa_bucket_wipe_namevalue,
	.xlo_add_entry		= ocfs2_xa_bucket_add_entry,
	.xlo_add_namevalue	= ocfs2_xa_bucket_add_namevalue,
	.xlo_fill_value_buf	= ocfs2_xa_bucket_fill_value_buf,
};

static unsigned int ocfs2_xa_value_clusters(struct ocfs2_xa_loc *loc)
{
	struct ocfs2_xattr_value_buf vb;

	if (ocfs2_xattr_is_local(loc->xl_entry))
		return 0;

	ocfs2_xa_fill_value_buf(loc, &vb);
	return le32_to_cpu(vb.vb_xv->xr_clusters);
}

static int ocfs2_xa_value_truncate(struct ocfs2_xa_loc *loc, u64 bytes,
				   struct ocfs2_xattr_set_ctxt *ctxt)
{
	int trunc_rc, access_rc;
	struct ocfs2_xattr_value_buf vb;

	ocfs2_xa_fill_value_buf(loc, &vb);
	trunc_rc = ocfs2_xattr_value_truncate(loc->xl_inode, &vb, bytes,
					      ctxt);

	/*
	 * The caller of ocfs2_xa_value_truncate() has already called
	 * ocfs2_xa_journal_access on the loc.  However, The truncate code
	 * calls ocfs2_extend_trans().  This may commit the previous
	 * transaction and open a new one.  If this is a bucket, truncate
	 * could leave only vb->vb_bh set up for journaling.  Meanwhile,
	 * the caller is expecting to dirty the entire bucket.  So we must
	 * reset the journal work.  We do this even if truncate has failed,
	 * as it could have failed after committing the extend.
	 */
	access_rc = ocfs2_xa_journal_access(ctxt->handle, loc,
					    OCFS2_JOURNAL_ACCESS_WRITE);

	/* Errors in truncate take precedence */
	return trunc_rc ? trunc_rc : access_rc;
}

static void ocfs2_xa_remove_entry(struct ocfs2_xa_loc *loc)
{
	int index, count;
	struct ocfs2_xattr_header *xh = loc->xl_header;
	struct ocfs2_xattr_entry *entry = loc->xl_entry;

	ocfs2_xa_wipe_namevalue(loc);
	loc->xl_entry = NULL;

	le16_add_cpu(&xh->xh_count, -1);
	count = le16_to_cpu(xh->xh_count);

	/*
	 * Only zero out the entry if there are more remaining.  This is
	 * important for an empty bucket, as it keeps track of the
	 * bucket's hash value.  It doesn't hurt empty block storage.
	 */
	if (count) {
		index = ((char *)entry - (char *)&xh->xh_entries) /
			sizeof(struct ocfs2_xattr_entry);
		memmove(&xh->xh_entries[index], &xh->xh_entries[index + 1],
			(count - index) * sizeof(struct ocfs2_xattr_entry));
		memset(&xh->xh_entries[count], 0,
		       sizeof(struct ocfs2_xattr_entry));
	}
}

/*
 * If we have a problem adjusting the size of an external value during
 * ocfs2_xa_prepare_entry() or ocfs2_xa_remove(), we may have an xattr
 * in an intermediate state.  For example, the value may be partially
 * truncated.
 *
 * If the value tree hasn't changed, the extend/truncate went nowhere.
 * We have nothing to do.  The caller can treat it as a straight error.
 *
 * If the value tree got partially truncated, we now have a corrupted
 * extended attribute.  We're going to wipe its entry and leak the
 * clusters.  Better to leak some storage than leave a corrupt entry.
 *
 * If the value tree grew, it obviously didn't grow enough for the
 * new entry.  We're not going to try and reclaim those clusters either.
 * If there was already an external value there (orig_clusters != 0),
 * the new clusters are attached safely and we can just leave the old
 * value in place.  If there was no external value there, we remove
 * the entry.
 *
 * This way, the xattr block we store in the journal will be consistent.
 * If the size change broke because of the journal, no changes will hit
 * disk anyway.
 */
static void ocfs2_xa_cleanup_value_truncate(struct ocfs2_xa_loc *loc,
					    const char *what,
					    unsigned int orig_clusters)
{
	unsigned int new_clusters = ocfs2_xa_value_clusters(loc);
	char *nameval_buf = ocfs2_xa_offset_pointer(loc,
				le16_to_cpu(loc->xl_entry->xe_name_offset));

	if (new_clusters < orig_clusters) {
		mlog(ML_ERROR,
		     "Partial truncate while %s xattr %.*s.  Leaking "
		     "%u clusters and removing the entry\n",
		     what, loc->xl_entry->xe_name_len, nameval_buf,
		     orig_clusters - new_clusters);
		ocfs2_xa_remove_entry(loc);
	} else if (!orig_clusters) {
		mlog(ML_ERROR,
		     "Unable to allocate an external value for xattr "
		     "%.*s safely.  Leaking %u clusters and removing the "
		     "entry\n",
		     loc->xl_entry->xe_name_len, nameval_buf,
		     new_clusters - orig_clusters);
		ocfs2_xa_remove_entry(loc);
	} else if (new_clusters > orig_clusters)
		mlog(ML_ERROR,
		     "Unable to grow xattr %.*s safely.  %u new clusters "
		     "have been added, but the value will not be "
		     "modified\n",
		     loc->xl_entry->xe_name_len, nameval_buf,
		     new_clusters - orig_clusters);
}

static int ocfs2_xa_remove(struct ocfs2_xa_loc *loc,
			   struct ocfs2_xattr_set_ctxt *ctxt)
{
	int rc = 0;
	unsigned int orig_clusters;

	if (!ocfs2_xattr_is_local(loc->xl_entry)) {
		orig_clusters = ocfs2_xa_value_clusters(loc);
		rc = ocfs2_xa_value_truncate(loc, 0, ctxt);
		if (rc) {
			mlog_errno(rc);
			/*
			 * Since this is remove, we can return 0 if
			 * ocfs2_xa_cleanup_value_truncate() is going to
			 * wipe the entry anyway.  So we check the
			 * cluster count as well.
			 */
			if (orig_clusters != ocfs2_xa_value_clusters(loc))
				rc = 0;
			ocfs2_xa_cleanup_value_truncate(loc, "removing",
							orig_clusters);
			if (rc)
				goto out;
		}
	}

	ocfs2_xa_remove_entry(loc);

out:
	return rc;
}

static void ocfs2_xa_install_value_root(struct ocfs2_xa_loc *loc)
{
	int name_size = OCFS2_XATTR_SIZE(loc->xl_entry->xe_name_len);
	char *nameval_buf;

	nameval_buf = ocfs2_xa_offset_pointer(loc,
				le16_to_cpu(loc->xl_entry->xe_name_offset));
	memcpy(nameval_buf + name_size, &def_xv, OCFS2_XATTR_ROOT_SIZE);
}

/*
 * Take an existing entry and make it ready for the new value.  This
 * won't allocate space, but it may free space.  It should be ready for
 * ocfs2_xa_prepare_entry() to finish the work.
 */
static int ocfs2_xa_reuse_entry(struct ocfs2_xa_loc *loc,
				struct ocfs2_xattr_info *xi,
				struct ocfs2_xattr_set_ctxt *ctxt)
{
	int rc = 0;
	int name_size = OCFS2_XATTR_SIZE(xi->xi_name_len);
	unsigned int orig_clusters;
	char *nameval_buf;
	int xe_local = ocfs2_xattr_is_local(loc->xl_entry);
	int xi_local = xi->xi_value_len <= OCFS2_XATTR_INLINE_SIZE;

	BUG_ON(OCFS2_XATTR_SIZE(loc->xl_entry->xe_name_len) !=
	       name_size);

	nameval_buf = ocfs2_xa_offset_pointer(loc,
				le16_to_cpu(loc->xl_entry->xe_name_offset));
	if (xe_local) {
		memset(nameval_buf + name_size, 0,
		       namevalue_size_xe(loc->xl_entry) - name_size);
		if (!xi_local)
			ocfs2_xa_install_value_root(loc);
	} else {
		orig_clusters = ocfs2_xa_value_clusters(loc);
		if (xi_local) {
			rc = ocfs2_xa_value_truncate(loc, 0, ctxt);
			if (rc < 0)
				mlog_errno(rc);
			else
				memset(nameval_buf + name_size, 0,
				       namevalue_size_xe(loc->xl_entry) -
				       name_size);
		} else if (le64_to_cpu(loc->xl_entry->xe_value_size) >
			   xi->xi_value_len) {
			rc = ocfs2_xa_value_truncate(loc, xi->xi_value_len,
						     ctxt);
			if (rc < 0)
				mlog_errno(rc);
		}

		if (rc) {
			ocfs2_xa_cleanup_value_truncate(loc, "reusing",
							orig_clusters);
			goto out;
		}
	}

	loc->xl_entry->xe_value_size = cpu_to_le64(xi->xi_value_len);
	ocfs2_xattr_set_local(loc->xl_entry, xi_local);

out:
	return rc;
}

/*
 * Prepares loc->xl_entry to receive the new xattr.  This includes
 * properly setting up the name+value pair region.  If loc->xl_entry
 * already exists, it will take care of modifying it appropriately.
 *
 * Note that this modifies the data.  You did journal_access already,
 * right?
 */
static int ocfs2_xa_prepare_entry(struct ocfs2_xa_loc *loc,
				  struct ocfs2_xattr_info *xi,
				  u32 name_hash,
				  struct ocfs2_xattr_set_ctxt *ctxt)
{
	int rc = 0;
	unsigned int orig_clusters;
	__le64 orig_value_size = 0;

	rc = ocfs2_xa_check_space(loc, xi);
	if (rc)
		goto out;

	if (loc->xl_entry) {
		if (ocfs2_xa_can_reuse_entry(loc, xi)) {
			orig_value_size = loc->xl_entry->xe_value_size;
			rc = ocfs2_xa_reuse_entry(loc, xi, ctxt);
			if (rc)
				goto out;
			goto alloc_value;
		}

		if (!ocfs2_xattr_is_local(loc->xl_entry)) {
			orig_clusters = ocfs2_xa_value_clusters(loc);
			rc = ocfs2_xa_value_truncate(loc, 0, ctxt);
			if (rc) {
				mlog_errno(rc);
				ocfs2_xa_cleanup_value_truncate(loc,
								"overwriting",
								orig_clusters);
				goto out;
			}
		}
		ocfs2_xa_wipe_namevalue(loc);
	} else
		ocfs2_xa_add_entry(loc, name_hash);

	/*
	 * If we get here, we have a blank entry.  Fill it.  We grow our
	 * name+value pair back from the end.
	 */
	ocfs2_xa_add_namevalue(loc, xi);
	if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE)
		ocfs2_xa_install_value_root(loc);

alloc_value:
	if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE) {
		orig_clusters = ocfs2_xa_value_clusters(loc);
		rc = ocfs2_xa_value_truncate(loc, xi->xi_value_len, ctxt);
		if (rc < 0) {
			ctxt->set_abort = 1;
			ocfs2_xa_cleanup_value_truncate(loc, "growing",
							orig_clusters);
			/*
			 * If we were growing an existing value,
			 * ocfs2_xa_cleanup_value_truncate() won't remove
			 * the entry. We need to restore the original value
			 * size.
			 */
			if (loc->xl_entry) {
				BUG_ON(!orig_value_size);
				loc->xl_entry->xe_value_size = orig_value_size;
			}
			mlog_errno(rc);
		}
	}

out:
	return rc;
}

/*
 * Store the value portion of the name+value pair.  This will skip
 * values that are stored externally.  Their tree roots were set up
 * by ocfs2_xa_prepare_entry().
 */
static int ocfs2_xa_store_value(struct ocfs2_xa_loc *loc,
				struct ocfs2_xattr_info *xi,
				struct ocfs2_xattr_set_ctxt *ctxt)
{
	int rc = 0;
	int nameval_offset = le16_to_cpu(loc->xl_entry->xe_name_offset);
	int name_size = OCFS2_XATTR_SIZE(xi->xi_name_len);
	char *nameval_buf;
	struct ocfs2_xattr_value_buf vb;

	nameval_buf = ocfs2_xa_offset_pointer(loc, nameval_offset);
	if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE) {
		ocfs2_xa_fill_value_buf(loc, &vb);
		rc = __ocfs2_xattr_set_value_outside(loc->xl_inode,
						     ctxt->handle, &vb,
						     xi->xi_value,
						     xi->xi_value_len);
	} else
		memcpy(nameval_buf + name_size, xi->xi_value, xi->xi_value_len);

	return rc;
}

static int ocfs2_xa_set(struct ocfs2_xa_loc *loc,
			struct ocfs2_xattr_info *xi,
			struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	u32 name_hash = ocfs2_xattr_name_hash(loc->xl_inode, xi->xi_name,
					      xi->xi_name_len);

	ret = ocfs2_xa_journal_access(ctxt->handle, loc,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * From here on out, everything is going to modify the buffer a
	 * little.  Errors are going to leave the xattr header in a
	 * sane state.  Thus, even with errors we dirty the sucker.
	 */

	/* Don't worry, we are never called with !xi_value and !xl_entry */
	if (!xi->xi_value) {
		ret = ocfs2_xa_remove(loc, ctxt);
		goto out_dirty;
	}

	ret = ocfs2_xa_prepare_entry(loc, xi, name_hash, ctxt);
	if (ret) {
		if (ret != -ENOSPC)
			mlog_errno(ret);
		goto out_dirty;
	}

	ret = ocfs2_xa_store_value(loc, xi, ctxt);
	if (ret)
		mlog_errno(ret);

out_dirty:
	ocfs2_xa_journal_dirty(ctxt->handle, loc);

out:
	return ret;
}

static void ocfs2_init_dinode_xa_loc(struct ocfs2_xa_loc *loc,
				     struct inode *inode,
				     struct buffer_head *bh,
				     struct ocfs2_xattr_entry *entry)
{
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)bh->b_data;

	BUG_ON(!(OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_XATTR_FL));

	loc->xl_inode = inode;
	loc->xl_ops = &ocfs2_xa_block_loc_ops;
	loc->xl_storage = bh;
	loc->xl_entry = entry;
	loc->xl_size = le16_to_cpu(di->i_xattr_inline_size);
	loc->xl_header =
		(struct ocfs2_xattr_header *)(bh->b_data + bh->b_size -
					      loc->xl_size);
}

static void ocfs2_init_xattr_block_xa_loc(struct ocfs2_xa_loc *loc,
					  struct inode *inode,
					  struct buffer_head *bh,
					  struct ocfs2_xattr_entry *entry)
{
	struct ocfs2_xattr_block *xb =
		(struct ocfs2_xattr_block *)bh->b_data;

	BUG_ON(le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED);

	loc->xl_inode = inode;
	loc->xl_ops = &ocfs2_xa_block_loc_ops;
	loc->xl_storage = bh;
	loc->xl_header = &(xb->xb_attrs.xb_header);
	loc->xl_entry = entry;
	loc->xl_size = bh->b_size - offsetof(struct ocfs2_xattr_block,
					     xb_attrs.xb_header);
}

static void ocfs2_init_xattr_bucket_xa_loc(struct ocfs2_xa_loc *loc,
					   struct ocfs2_xattr_bucket *bucket,
					   struct ocfs2_xattr_entry *entry)
{
	loc->xl_inode = bucket->bu_inode;
	loc->xl_ops = &ocfs2_xa_bucket_loc_ops;
	loc->xl_storage = bucket;
	loc->xl_header = bucket_xh(bucket);
	loc->xl_entry = entry;
	loc->xl_size = OCFS2_XATTR_BUCKET_SIZE;
}

/*
 * In xattr remove, if it is stored outside and refcounted, we may have
 * the chance to split the refcount tree. So need the allocators.
 */
static int ocfs2_lock_xattr_remove_allocators(struct inode *inode,
					struct ocfs2_xattr_value_root *xv,
					struct ocfs2_caching_info *ref_ci,
					struct buffer_head *ref_root_bh,
					struct ocfs2_alloc_context **meta_ac,
					int *ref_credits)
{
	int ret, meta_add = 0;
	u32 p_cluster, num_clusters;
	unsigned int ext_flags;

	*ref_credits = 0;
	ret = ocfs2_xattr_get_clusters(inode, 0, &p_cluster,
				       &num_clusters,
				       &xv->xr_list,
				       &ext_flags);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (!(ext_flags & OCFS2_EXT_REFCOUNTED))
		goto out;

	ret = ocfs2_refcounted_xattr_delete_need(inode, ref_ci,
						 ref_root_bh, xv,
						 &meta_add, ref_credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_reserve_new_metadata_blocks(OCFS2_SB(inode->i_sb),
						meta_add, meta_ac);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_remove_value_outside(struct inode*inode,
				      struct ocfs2_xattr_value_buf *vb,
				      struct ocfs2_xattr_header *header,
				      struct ocfs2_caching_info *ref_ci,
				      struct buffer_head *ref_root_bh)
{
	int ret = 0, i, ref_credits;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_xattr_set_ctxt ctxt = { NULL, NULL, };
	void *val;

	ocfs2_init_dealloc_ctxt(&ctxt.dealloc);

	for (i = 0; i < le16_to_cpu(header->xh_count); i++) {
		struct ocfs2_xattr_entry *entry = &header->xh_entries[i];

		if (ocfs2_xattr_is_local(entry))
			continue;

		val = (void *)header +
			le16_to_cpu(entry->xe_name_offset);
		vb->vb_xv = (struct ocfs2_xattr_value_root *)
			(val + OCFS2_XATTR_SIZE(entry->xe_name_len));

		ret = ocfs2_lock_xattr_remove_allocators(inode, vb->vb_xv,
							 ref_ci, ref_root_bh,
							 &ctxt.meta_ac,
							 &ref_credits);

		ctxt.handle = ocfs2_start_trans(osb, ref_credits +
					ocfs2_remove_extent_credits(osb->sb));
		if (IS_ERR(ctxt.handle)) {
			ret = PTR_ERR(ctxt.handle);
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_xattr_value_truncate(inode, vb, 0, &ctxt);

		ocfs2_commit_trans(osb, ctxt.handle);
		if (ctxt.meta_ac) {
			ocfs2_free_alloc_context(ctxt.meta_ac);
			ctxt.meta_ac = NULL;
		}

		if (ret < 0) {
			mlog_errno(ret);
			break;
		}

	}

	if (ctxt.meta_ac)
		ocfs2_free_alloc_context(ctxt.meta_ac);
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &ctxt.dealloc);
	return ret;
}

static int ocfs2_xattr_ibody_remove(struct inode *inode,
				    struct buffer_head *di_bh,
				    struct ocfs2_caching_info *ref_ci,
				    struct buffer_head *ref_root_bh)
{

	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_xattr_header *header;
	int ret;
	struct ocfs2_xattr_value_buf vb = {
		.vb_bh = di_bh,
		.vb_access = ocfs2_journal_access_di,
	};

	header = (struct ocfs2_xattr_header *)
		 ((void *)di + inode->i_sb->s_blocksize -
		 le16_to_cpu(di->i_xattr_inline_size));

	ret = ocfs2_remove_value_outside(inode, &vb, header,
					 ref_ci, ref_root_bh);

	return ret;
}

struct ocfs2_rm_xattr_bucket_para {
	struct ocfs2_caching_info *ref_ci;
	struct buffer_head *ref_root_bh;
};

static int ocfs2_xattr_block_remove(struct inode *inode,
				    struct buffer_head *blk_bh,
				    struct ocfs2_caching_info *ref_ci,
				    struct buffer_head *ref_root_bh)
{
	struct ocfs2_xattr_block *xb;
	int ret = 0;
	struct ocfs2_xattr_value_buf vb = {
		.vb_bh = blk_bh,
		.vb_access = ocfs2_journal_access_xb,
	};
	struct ocfs2_rm_xattr_bucket_para args = {
		.ref_ci = ref_ci,
		.ref_root_bh = ref_root_bh,
	};

	xb = (struct ocfs2_xattr_block *)blk_bh->b_data;
	if (!(le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED)) {
		struct ocfs2_xattr_header *header = &(xb->xb_attrs.xb_header);
		ret = ocfs2_remove_value_outside(inode, &vb, header,
						 ref_ci, ref_root_bh);
	} else
		ret = ocfs2_iterate_xattr_index_block(inode,
						blk_bh,
						ocfs2_rm_xattr_cluster,
						&args);

	return ret;
}

static int ocfs2_xattr_free_block(struct inode *inode,
				  u64 block,
				  struct ocfs2_caching_info *ref_ci,
				  struct buffer_head *ref_root_bh)
{
	struct inode *xb_alloc_inode;
	struct buffer_head *xb_alloc_bh = NULL;
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_xattr_block *xb;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	handle_t *handle;
	int ret = 0;
	u64 blk, bg_blkno;
	u16 bit;

	ret = ocfs2_read_xattr_block(inode, block, &blk_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_block_remove(inode, blk_bh, ref_ci, ref_root_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	xb = (struct ocfs2_xattr_block *)blk_bh->b_data;
	blk = le64_to_cpu(xb->xb_blkno);
	bit = le16_to_cpu(xb->xb_suballoc_bit);
	if (xb->xb_suballoc_loc)
		bg_blkno = le64_to_cpu(xb->xb_suballoc_loc);
	else
		bg_blkno = ocfs2_which_suballoc_group(blk, bit);

	xb_alloc_inode = ocfs2_get_system_file_inode(osb,
				EXTENT_ALLOC_SYSTEM_INODE,
				le16_to_cpu(xb->xb_suballoc_slot));
	if (!xb_alloc_inode) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	inode_lock(xb_alloc_inode);

	ret = ocfs2_inode_lock(xb_alloc_inode, &xb_alloc_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_mutex;
	}

	handle = ocfs2_start_trans(osb, OCFS2_SUBALLOC_FREE);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_unlock;
	}

	ret = ocfs2_free_suballoc_bits(handle, xb_alloc_inode, xb_alloc_bh,
				       bit, bg_blkno, 1);
	if (ret < 0)
		mlog_errno(ret);

	ocfs2_commit_trans(osb, handle);
out_unlock:
	ocfs2_inode_unlock(xb_alloc_inode, 1);
	brelse(xb_alloc_bh);
out_mutex:
	inode_unlock(xb_alloc_inode);
	iput(xb_alloc_inode);
out:
	brelse(blk_bh);
	return ret;
}

/*
 * ocfs2_xattr_remove()
 *
 * Free extended attribute resources associated with this inode.
 */
int ocfs2_xattr_remove(struct inode *inode, struct buffer_head *di_bh)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_refcount_tree *ref_tree = NULL;
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_caching_info *ref_ci = NULL;
	handle_t *handle;
	int ret;

	if (!ocfs2_supports_xattr(OCFS2_SB(inode->i_sb)))
		return 0;

	if (!(oi->ip_dyn_features & OCFS2_HAS_XATTR_FL))
		return 0;

	if (ocfs2_is_refcount_inode(inode)) {
		ret = ocfs2_lock_refcount_tree(OCFS2_SB(inode->i_sb),
					       le64_to_cpu(di->i_refcount_loc),
					       1, &ref_tree, &ref_root_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		ref_ci = &ref_tree->rf_ci;

	}

	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL) {
		ret = ocfs2_xattr_ibody_remove(inode, di_bh,
					       ref_ci, ref_root_bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (di->i_xattr_loc) {
		ret = ocfs2_xattr_free_block(inode,
					     le64_to_cpu(di->i_xattr_loc),
					     ref_ci, ref_root_bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	handle = ocfs2_start_trans((OCFS2_SB(inode->i_sb)),
				   OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}
	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	di->i_xattr_loc = 0;

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features &= ~(OCFS2_INLINE_XATTR_FL | OCFS2_HAS_XATTR_FL);
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);

	ocfs2_journal_dirty(handle, di_bh);
out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	if (ref_tree)
		ocfs2_unlock_refcount_tree(OCFS2_SB(inode->i_sb), ref_tree, 1);
	brelse(ref_root_bh);
	return ret;
}

static int ocfs2_xattr_has_space_inline(struct inode *inode,
					struct ocfs2_dinode *di)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	unsigned int xattrsize = OCFS2_SB(inode->i_sb)->s_xattr_inline_size;
	int free;

	if (xattrsize < OCFS2_MIN_XATTR_INLINE_SIZE)
		return 0;

	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		struct ocfs2_inline_data *idata = &di->id2.i_data;
		free = le16_to_cpu(idata->id_count) - le64_to_cpu(di->i_size);
	} else if (ocfs2_inode_is_fast_symlink(inode)) {
		free = ocfs2_fast_symlink_chars(inode->i_sb) -
			le64_to_cpu(di->i_size);
	} else {
		struct ocfs2_extent_list *el = &di->id2.i_list;
		free = (le16_to_cpu(el->l_count) -
			le16_to_cpu(el->l_next_free_rec)) *
			sizeof(struct ocfs2_extent_rec);
	}
	if (free >= xattrsize)
		return 1;

	return 0;
}

/*
 * ocfs2_xattr_ibody_find()
 *
 * Find extended attribute in inode block and
 * fill search info into struct ocfs2_xattr_search.
 */
static int ocfs2_xattr_ibody_find(struct inode *inode,
				  int name_index,
				  const char *name,
				  struct ocfs2_xattr_search *xs)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)xs->inode_bh->b_data;
	int ret;
	int has_space = 0;

	if (inode->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE)
		return 0;

	if (!(oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL)) {
		down_read(&oi->ip_alloc_sem);
		has_space = ocfs2_xattr_has_space_inline(inode, di);
		up_read(&oi->ip_alloc_sem);
		if (!has_space)
			return 0;
	}

	xs->xattr_bh = xs->inode_bh;
	xs->end = (void *)di + inode->i_sb->s_blocksize;
	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL)
		xs->header = (struct ocfs2_xattr_header *)
			(xs->end - le16_to_cpu(di->i_xattr_inline_size));
	else
		xs->header = (struct ocfs2_xattr_header *)
			(xs->end - OCFS2_SB(inode->i_sb)->s_xattr_inline_size);
	xs->base = (void *)xs->header;
	xs->here = xs->header->xh_entries;

	/* Find the named attribute. */
	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL) {
		ret = ocfs2_xattr_find_entry(name_index, name, xs);
		if (ret && ret != -ENODATA)
			return ret;
		xs->not_found = ret;
	}

	return 0;
}

static int ocfs2_xattr_ibody_init(struct inode *inode,
				  struct buffer_head *di_bh,
				  struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	unsigned int xattrsize = osb->s_xattr_inline_size;

	if (!ocfs2_xattr_has_space_inline(inode, di)) {
		ret = -ENOSPC;
		goto out;
	}

	ret = ocfs2_journal_access_di(ctxt->handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Adjust extent record count or inline data size
	 * to reserve space for extended attribute.
	 */
	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		struct ocfs2_inline_data *idata = &di->id2.i_data;
		le16_add_cpu(&idata->id_count, -xattrsize);
	} else if (!(ocfs2_inode_is_fast_symlink(inode))) {
		struct ocfs2_extent_list *el = &di->id2.i_list;
		le16_add_cpu(&el->l_count, -(xattrsize /
					     sizeof(struct ocfs2_extent_rec)));
	}
	di->i_xattr_inline_size = cpu_to_le16(xattrsize);

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features |= OCFS2_INLINE_XATTR_FL|OCFS2_HAS_XATTR_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);

	ocfs2_journal_dirty(ctxt->handle, di_bh);

out:
	return ret;
}

/*
 * ocfs2_xattr_ibody_set()
 *
 * Set, replace or remove an extended attribute into inode block.
 *
 */
static int ocfs2_xattr_ibody_set(struct inode *inode,
				 struct ocfs2_xattr_info *xi,
				 struct ocfs2_xattr_search *xs,
				 struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_xa_loc loc;

	if (inode->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE)
		return -ENOSPC;

	down_write(&oi->ip_alloc_sem);
	if (!(oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL)) {
		ret = ocfs2_xattr_ibody_init(inode, xs->inode_bh, ctxt);
		if (ret) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
			goto out;
		}
	}

	ocfs2_init_dinode_xa_loc(&loc, inode, xs->inode_bh,
				 xs->not_found ? NULL : xs->here);
	ret = ocfs2_xa_set(&loc, xi, ctxt);
	if (ret) {
		if (ret != -ENOSPC)
			mlog_errno(ret);
		goto out;
	}
	xs->here = loc.xl_entry;

out:
	up_write(&oi->ip_alloc_sem);

	return ret;
}

/*
 * ocfs2_xattr_block_find()
 *
 * Find extended attribute in external block and
 * fill search info into struct ocfs2_xattr_search.
 */
static int ocfs2_xattr_block_find(struct inode *inode,
				  int name_index,
				  const char *name,
				  struct ocfs2_xattr_search *xs)
{
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)xs->inode_bh->b_data;
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_xattr_block *xb;
	int ret = 0;

	if (!di->i_xattr_loc)
		return ret;

	ret = ocfs2_read_xattr_block(inode, le64_to_cpu(di->i_xattr_loc),
				     &blk_bh);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	xs->xattr_bh = blk_bh;
	xb = (struct ocfs2_xattr_block *)blk_bh->b_data;

	if (!(le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED)) {
		xs->header = &xb->xb_attrs.xb_header;
		xs->base = (void *)xs->header;
		xs->end = (void *)(blk_bh->b_data) + blk_bh->b_size;
		xs->here = xs->header->xh_entries;

		ret = ocfs2_xattr_find_entry(name_index, name, xs);
	} else
		ret = ocfs2_xattr_index_block_find(inode, blk_bh,
						   name_index,
						   name, xs);

	if (ret && ret != -ENODATA) {
		xs->xattr_bh = NULL;
		goto cleanup;
	}
	xs->not_found = ret;
	return 0;
cleanup:
	brelse(blk_bh);

	return ret;
}

static int ocfs2_create_xattr_block(struct inode *inode,
				    struct buffer_head *inode_bh,
				    struct ocfs2_xattr_set_ctxt *ctxt,
				    int indexed,
				    struct buffer_head **ret_bh)
{
	int ret;
	u16 suballoc_bit_start;
	u32 num_got;
	u64 suballoc_loc, first_blkno;
	struct ocfs2_dinode *di =  (struct ocfs2_dinode *)inode_bh->b_data;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_xattr_block *xblk;

	ret = ocfs2_journal_access_di(ctxt->handle, INODE_CACHE(inode),
				      inode_bh, OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret < 0) {
		mlog_errno(ret);
		goto end;
	}

	ret = ocfs2_claim_metadata(ctxt->handle, ctxt->meta_ac, 1,
				   &suballoc_loc, &suballoc_bit_start,
				   &num_got, &first_blkno);
	if (ret < 0) {
		mlog_errno(ret);
		goto end;
	}

	new_bh = sb_getblk(inode->i_sb, first_blkno);
	if (!new_bh) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto end;
	}

	ocfs2_set_new_buffer_uptodate(INODE_CACHE(inode), new_bh);

	ret = ocfs2_journal_access_xb(ctxt->handle, INODE_CACHE(inode),
				      new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret < 0) {
		mlog_errno(ret);
		goto end;
	}

	/* Initialize ocfs2_xattr_block */
	xblk = (struct ocfs2_xattr_block *)new_bh->b_data;
	memset(xblk, 0, inode->i_sb->s_blocksize);
	strcpy((void *)xblk, OCFS2_XATTR_BLOCK_SIGNATURE);
	xblk->xb_suballoc_slot = cpu_to_le16(ctxt->meta_ac->ac_alloc_slot);
	xblk->xb_suballoc_loc = cpu_to_le64(suballoc_loc);
	xblk->xb_suballoc_bit = cpu_to_le16(suballoc_bit_start);
	xblk->xb_fs_generation =
		cpu_to_le32(OCFS2_SB(inode->i_sb)->fs_generation);
	xblk->xb_blkno = cpu_to_le64(first_blkno);
	if (indexed) {
		struct ocfs2_xattr_tree_root *xr = &xblk->xb_attrs.xb_root;
		xr->xt_clusters = cpu_to_le32(1);
		xr->xt_last_eb_blk = 0;
		xr->xt_list.l_tree_depth = 0;
		xr->xt_list.l_count = cpu_to_le16(
					ocfs2_xattr_recs_per_xb(inode->i_sb));
		xr->xt_list.l_next_free_rec = cpu_to_le16(1);
		xblk->xb_flags = cpu_to_le16(OCFS2_XATTR_INDEXED);
	}
	ocfs2_journal_dirty(ctxt->handle, new_bh);

	/* Add it to the inode */
	di->i_xattr_loc = cpu_to_le64(first_blkno);

	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_dyn_features |= OCFS2_HAS_XATTR_FL;
	di->i_dyn_features = cpu_to_le16(OCFS2_I(inode)->ip_dyn_features);
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	ocfs2_journal_dirty(ctxt->handle, inode_bh);

	*ret_bh = new_bh;
	new_bh = NULL;

end:
	brelse(new_bh);
	return ret;
}

/*
 * ocfs2_xattr_block_set()
 *
 * Set, replace or remove an extended attribute into external block.
 *
 */
static int ocfs2_xattr_block_set(struct inode *inode,
				 struct ocfs2_xattr_info *xi,
				 struct ocfs2_xattr_search *xs,
				 struct ocfs2_xattr_set_ctxt *ctxt)
{
	struct buffer_head *new_bh = NULL;
	struct ocfs2_xattr_block *xblk = NULL;
	int ret;
	struct ocfs2_xa_loc loc;

	if (!xs->xattr_bh) {
		ret = ocfs2_create_xattr_block(inode, xs->inode_bh, ctxt,
					       0, &new_bh);
		if (ret) {
			mlog_errno(ret);
			goto end;
		}

		xs->xattr_bh = new_bh;
		xblk = (struct ocfs2_xattr_block *)xs->xattr_bh->b_data;
		xs->header = &xblk->xb_attrs.xb_header;
		xs->base = (void *)xs->header;
		xs->end = (void *)xblk + inode->i_sb->s_blocksize;
		xs->here = xs->header->xh_entries;
	} else
		xblk = (struct ocfs2_xattr_block *)xs->xattr_bh->b_data;

	if (!(le16_to_cpu(xblk->xb_flags) & OCFS2_XATTR_INDEXED)) {
		ocfs2_init_xattr_block_xa_loc(&loc, inode, xs->xattr_bh,
					      xs->not_found ? NULL : xs->here);

		ret = ocfs2_xa_set(&loc, xi, ctxt);
		if (!ret)
			xs->here = loc.xl_entry;
		else if ((ret != -ENOSPC) || ctxt->set_abort)
			goto end;
		else {
			ret = ocfs2_xattr_create_index_block(inode, xs, ctxt);
			if (ret)
				goto end;
		}
	}

	if (le16_to_cpu(xblk->xb_flags) & OCFS2_XATTR_INDEXED)
		ret = ocfs2_xattr_set_entry_index_block(inode, xi, xs, ctxt);

end:
	return ret;
}

/* Check whether the new xattr can be inserted into the inode. */
static int ocfs2_xattr_can_be_in_inode(struct inode *inode,
				       struct ocfs2_xattr_info *xi,
				       struct ocfs2_xattr_search *xs)
{
	struct ocfs2_xattr_entry *last;
	int free, i;
	size_t min_offs = xs->end - xs->base;

	if (!xs->header)
		return 0;

	last = xs->header->xh_entries;

	for (i = 0; i < le16_to_cpu(xs->header->xh_count); i++) {
		size_t offs = le16_to_cpu(last->xe_name_offset);
		if (offs < min_offs)
			min_offs = offs;
		last += 1;
	}

	free = min_offs - ((void *)last - xs->base) - OCFS2_XATTR_HEADER_GAP;
	if (free < 0)
		return 0;

	BUG_ON(!xs->not_found);

	if (free >= (sizeof(struct ocfs2_xattr_entry) + namevalue_size_xi(xi)))
		return 1;

	return 0;
}

static int ocfs2_calc_xattr_set_need(struct inode *inode,
				     struct ocfs2_dinode *di,
				     struct ocfs2_xattr_info *xi,
				     struct ocfs2_xattr_search *xis,
				     struct ocfs2_xattr_search *xbs,
				     int *clusters_need,
				     int *meta_need,
				     int *credits_need)
{
	int ret = 0, old_in_xb = 0;
	int clusters_add = 0, meta_add = 0, credits = 0;
	struct buffer_head *bh = NULL;
	struct ocfs2_xattr_block *xb = NULL;
	struct ocfs2_xattr_entry *xe = NULL;
	struct ocfs2_xattr_value_root *xv = NULL;
	char *base = NULL;
	int name_offset, name_len = 0;
	u32 new_clusters = ocfs2_clusters_for_bytes(inode->i_sb,
						    xi->xi_value_len);
	u64 value_size;

	/*
	 * Calculate the clusters we need to write.
	 * No matter whether we replace an old one or add a new one,
	 * we need this for writing.
	 */
	if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE)
		credits += new_clusters *
			   ocfs2_clusters_to_blocks(inode->i_sb, 1);

	if (xis->not_found && xbs->not_found) {
		credits += ocfs2_blocks_per_xattr_bucket(inode->i_sb);

		if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE) {
			clusters_add += new_clusters;
			credits += ocfs2_calc_extend_credits(inode->i_sb,
							&def_xv.xv.xr_list);
		}

		goto meta_guess;
	}

	if (!xis->not_found) {
		xe = xis->here;
		name_offset = le16_to_cpu(xe->xe_name_offset);
		name_len = OCFS2_XATTR_SIZE(xe->xe_name_len);
		base = xis->base;
		credits += OCFS2_INODE_UPDATE_CREDITS;
	} else {
		int i, block_off = 0;
		xb = (struct ocfs2_xattr_block *)xbs->xattr_bh->b_data;
		xe = xbs->here;
		name_offset = le16_to_cpu(xe->xe_name_offset);
		name_len = OCFS2_XATTR_SIZE(xe->xe_name_len);
		i = xbs->here - xbs->header->xh_entries;
		old_in_xb = 1;

		if (le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED) {
			ret = ocfs2_xattr_bucket_get_name_value(inode->i_sb,
							bucket_xh(xbs->bucket),
							i, &block_off,
							&name_offset);
			base = bucket_block(xbs->bucket, block_off);
			credits += ocfs2_blocks_per_xattr_bucket(inode->i_sb);
		} else {
			base = xbs->base;
			credits += OCFS2_XATTR_BLOCK_UPDATE_CREDITS;
		}
	}

	/*
	 * delete a xattr doesn't need metadata and cluster allocation.
	 * so just calculate the credits and return.
	 *
	 * The credits for removing the value tree will be extended
	 * by ocfs2_remove_extent itself.
	 */
	if (!xi->xi_value) {
		if (!ocfs2_xattr_is_local(xe))
			credits += ocfs2_remove_extent_credits(inode->i_sb);

		goto out;
	}

	/* do cluster allocation guess first. */
	value_size = le64_to_cpu(xe->xe_value_size);

	if (old_in_xb) {
		/*
		 * In xattr set, we always try to set the xe in inode first,
		 * so if it can be inserted into inode successfully, the old
		 * one will be removed from the xattr block, and this xattr
		 * will be inserted into inode as a new xattr in inode.
		 */
		if (ocfs2_xattr_can_be_in_inode(inode, xi, xis)) {
			clusters_add += new_clusters;
			credits += ocfs2_remove_extent_credits(inode->i_sb) +
				    OCFS2_INODE_UPDATE_CREDITS;
			if (!ocfs2_xattr_is_local(xe))
				credits += ocfs2_calc_extend_credits(
							inode->i_sb,
							&def_xv.xv.xr_list);
			goto out;
		}
	}

	if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE) {
		/* the new values will be stored outside. */
		u32 old_clusters = 0;

		if (!ocfs2_xattr_is_local(xe)) {
			old_clusters =	ocfs2_clusters_for_bytes(inode->i_sb,
								 value_size);
			xv = (struct ocfs2_xattr_value_root *)
			     (base + name_offset + name_len);
			value_size = OCFS2_XATTR_ROOT_SIZE;
		} else
			xv = &def_xv.xv;

		if (old_clusters >= new_clusters) {
			credits += ocfs2_remove_extent_credits(inode->i_sb);
			goto out;
		} else {
			meta_add += ocfs2_extend_meta_needed(&xv->xr_list);
			clusters_add += new_clusters - old_clusters;
			credits += ocfs2_calc_extend_credits(inode->i_sb,
							     &xv->xr_list);
			if (value_size >= OCFS2_XATTR_ROOT_SIZE)
				goto out;
		}
	} else {
		/*
		 * Now the new value will be stored inside. So if the new
		 * value is smaller than the size of value root or the old
		 * value, we don't need any allocation, otherwise we have
		 * to guess metadata allocation.
		 */
		if ((ocfs2_xattr_is_local(xe) &&
		     (value_size >= xi->xi_value_len)) ||
		    (!ocfs2_xattr_is_local(xe) &&
		     OCFS2_XATTR_ROOT_SIZE >= xi->xi_value_len))
			goto out;
	}

meta_guess:
	/* calculate metadata allocation. */
	if (di->i_xattr_loc) {
		if (!xbs->xattr_bh) {
			ret = ocfs2_read_xattr_block(inode,
						     le64_to_cpu(di->i_xattr_loc),
						     &bh);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			xb = (struct ocfs2_xattr_block *)bh->b_data;
		} else
			xb = (struct ocfs2_xattr_block *)xbs->xattr_bh->b_data;

		/*
		 * If there is already an xattr tree, good, we can calculate
		 * like other b-trees. Otherwise we may have the chance of
		 * create a tree, the credit calculation is borrowed from
		 * ocfs2_calc_extend_credits with root_el = NULL. And the
		 * new tree will be cluster based, so no meta is needed.
		 */
		if (le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED) {
			struct ocfs2_extent_list *el =
				 &xb->xb_attrs.xb_root.xt_list;
			meta_add += ocfs2_extend_meta_needed(el);
			credits += ocfs2_calc_extend_credits(inode->i_sb,
							     el);
		} else
			credits += OCFS2_SUBALLOC_ALLOC + 1;

		/*
		 * This cluster will be used either for new bucket or for
		 * new xattr block.
		 * If the cluster size is the same as the bucket size, one
		 * more is needed since we may need to extend the bucket
		 * also.
		 */
		clusters_add += 1;
		credits += ocfs2_blocks_per_xattr_bucket(inode->i_sb);
		if (OCFS2_XATTR_BUCKET_SIZE ==
			OCFS2_SB(inode->i_sb)->s_clustersize) {
			credits += ocfs2_blocks_per_xattr_bucket(inode->i_sb);
			clusters_add += 1;
		}
	} else {
		credits += OCFS2_XATTR_BLOCK_CREATE_CREDITS;
		if (xi->xi_value_len > OCFS2_XATTR_INLINE_SIZE) {
			struct ocfs2_extent_list *el = &def_xv.xv.xr_list;
			meta_add += ocfs2_extend_meta_needed(el);
			credits += ocfs2_calc_extend_credits(inode->i_sb,
							     el);
		} else {
			meta_add += 1;
		}
	}
out:
	if (clusters_need)
		*clusters_need = clusters_add;
	if (meta_need)
		*meta_need = meta_add;
	if (credits_need)
		*credits_need = credits;
	brelse(bh);
	return ret;
}

static int ocfs2_init_xattr_set_ctxt(struct inode *inode,
				     struct ocfs2_dinode *di,
				     struct ocfs2_xattr_info *xi,
				     struct ocfs2_xattr_search *xis,
				     struct ocfs2_xattr_search *xbs,
				     struct ocfs2_xattr_set_ctxt *ctxt,
				     int extra_meta,
				     int *credits)
{
	int clusters_add, meta_add, ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	memset(ctxt, 0, sizeof(struct ocfs2_xattr_set_ctxt));

	ocfs2_init_dealloc_ctxt(&ctxt->dealloc);

	ret = ocfs2_calc_xattr_set_need(inode, di, xi, xis, xbs,
					&clusters_add, &meta_add, credits);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	meta_add += extra_meta;
	trace_ocfs2_init_xattr_set_ctxt(xi->xi_name, meta_add,
					clusters_add, *credits);

	if (meta_add) {
		ret = ocfs2_reserve_new_metadata_blocks(osb, meta_add,
							&ctxt->meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (clusters_add) {
		ret = ocfs2_reserve_clusters(osb, clusters_add, &ctxt->data_ac);
		if (ret)
			mlog_errno(ret);
	}
out:
	if (ret) {
		if (ctxt->meta_ac) {
			ocfs2_free_alloc_context(ctxt->meta_ac);
			ctxt->meta_ac = NULL;
		}

		/*
		 * We cannot have an error and a non null ctxt->data_ac.
		 */
	}

	return ret;
}

static int __ocfs2_xattr_set_handle(struct inode *inode,
				    struct ocfs2_dinode *di,
				    struct ocfs2_xattr_info *xi,
				    struct ocfs2_xattr_search *xis,
				    struct ocfs2_xattr_search *xbs,
				    struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret = 0, credits, old_found;

	if (!xi->xi_value) {
		/* Remove existing extended attribute */
		if (!xis->not_found)
			ret = ocfs2_xattr_ibody_set(inode, xi, xis, ctxt);
		else if (!xbs->not_found)
			ret = ocfs2_xattr_block_set(inode, xi, xbs, ctxt);
	} else {
		/* We always try to set extended attribute into inode first*/
		ret = ocfs2_xattr_ibody_set(inode, xi, xis, ctxt);
		if (!ret && !xbs->not_found) {
			/*
			 * If succeed and that extended attribute existing in
			 * external block, then we will remove it.
			 */
			xi->xi_value = NULL;
			xi->xi_value_len = 0;

			old_found = xis->not_found;
			xis->not_found = -ENODATA;
			ret = ocfs2_calc_xattr_set_need(inode,
							di,
							xi,
							xis,
							xbs,
							NULL,
							NULL,
							&credits);
			xis->not_found = old_found;
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_extend_trans(ctxt->handle, credits);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
			ret = ocfs2_xattr_block_set(inode, xi, xbs, ctxt);
		} else if ((ret == -ENOSPC) && !ctxt->set_abort) {
			if (di->i_xattr_loc && !xbs->xattr_bh) {
				ret = ocfs2_xattr_block_find(inode,
							     xi->xi_name_index,
							     xi->xi_name, xbs);
				if (ret)
					goto out;

				old_found = xis->not_found;
				xis->not_found = -ENODATA;
				ret = ocfs2_calc_xattr_set_need(inode,
								di,
								xi,
								xis,
								xbs,
								NULL,
								NULL,
								&credits);
				xis->not_found = old_found;
				if (ret) {
					mlog_errno(ret);
					goto out;
				}

				ret = ocfs2_extend_trans(ctxt->handle, credits);
				if (ret) {
					mlog_errno(ret);
					goto out;
				}
			}
			/*
			 * If no space in inode, we will set extended attribute
			 * into external block.
			 */
			ret = ocfs2_xattr_block_set(inode, xi, xbs, ctxt);
			if (ret)
				goto out;
			if (!xis->not_found) {
				/*
				 * If succeed and that extended attribute
				 * existing in inode, we will remove it.
				 */
				xi->xi_value = NULL;
				xi->xi_value_len = 0;
				xbs->not_found = -ENODATA;
				ret = ocfs2_calc_xattr_set_need(inode,
								di,
								xi,
								xis,
								xbs,
								NULL,
								NULL,
								&credits);
				if (ret) {
					mlog_errno(ret);
					goto out;
				}

				ret = ocfs2_extend_trans(ctxt->handle, credits);
				if (ret) {
					mlog_errno(ret);
					goto out;
				}
				ret = ocfs2_xattr_ibody_set(inode, xi,
							    xis, ctxt);
			}
		}
	}

	if (!ret) {
		/* Update inode ctime. */
		ret = ocfs2_journal_access_di(ctxt->handle, INODE_CACHE(inode),
					      xis->inode_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		inode->i_ctime = current_time(inode);
		di->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
		di->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
		ocfs2_journal_dirty(ctxt->handle, xis->inode_bh);
	}
out:
	return ret;
}

/*
 * This function only called duing creating inode
 * for init security/acl xattrs of the new inode.
 * All transanction credits have been reserved in mknod.
 */
int ocfs2_xattr_set_handle(handle_t *handle,
			   struct inode *inode,
			   struct buffer_head *di_bh,
			   int name_index,
			   const char *name,
			   const void *value,
			   size_t value_len,
			   int flags,
			   struct ocfs2_alloc_context *meta_ac,
			   struct ocfs2_alloc_context *data_ac)
{
	struct ocfs2_dinode *di;
	int ret;

	struct ocfs2_xattr_info xi = {
		.xi_name_index = name_index,
		.xi_name = name,
		.xi_name_len = strlen(name),
		.xi_value = value,
		.xi_value_len = value_len,
	};

	struct ocfs2_xattr_search xis = {
		.not_found = -ENODATA,
	};

	struct ocfs2_xattr_search xbs = {
		.not_found = -ENODATA,
	};

	struct ocfs2_xattr_set_ctxt ctxt = {
		.handle = handle,
		.meta_ac = meta_ac,
		.data_ac = data_ac,
	};

	if (!ocfs2_supports_xattr(OCFS2_SB(inode->i_sb)))
		return -EOPNOTSUPP;

	/*
	 * In extreme situation, may need xattr bucket when
	 * block size is too small. And we have already reserved
	 * the credits for bucket in mknod.
	 */
	if (inode->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE) {
		xbs.bucket = ocfs2_xattr_bucket_new(inode);
		if (!xbs.bucket) {
			mlog_errno(-ENOMEM);
			return -ENOMEM;
		}
	}

	xis.inode_bh = xbs.inode_bh = di_bh;
	di = (struct ocfs2_dinode *)di_bh->b_data;

	down_write(&OCFS2_I(inode)->ip_xattr_sem);

	ret = ocfs2_xattr_ibody_find(inode, name_index, name, &xis);
	if (ret)
		goto cleanup;
	if (xis.not_found) {
		ret = ocfs2_xattr_block_find(inode, name_index, name, &xbs);
		if (ret)
			goto cleanup;
	}

	ret = __ocfs2_xattr_set_handle(inode, di, &xi, &xis, &xbs, &ctxt);

cleanup:
	up_write(&OCFS2_I(inode)->ip_xattr_sem);
	brelse(xbs.xattr_bh);
	ocfs2_xattr_bucket_free(xbs.bucket);

	return ret;
}

/*
 * ocfs2_xattr_set()
 *
 * Set, replace or remove an extended attribute for this inode.
 * value is NULL to remove an existing extended attribute, else either
 * create or replace an extended attribute.
 */
int ocfs2_xattr_set(struct inode *inode,
		    int name_index,
		    const char *name,
		    const void *value,
		    size_t value_len,
		    int flags)
{
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	int ret, credits, had_lock, ref_meta = 0, ref_credits = 0;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	struct ocfs2_xattr_set_ctxt ctxt = { NULL, NULL, NULL, };
	struct ocfs2_refcount_tree *ref_tree = NULL;
	struct ocfs2_lock_holder oh;

	struct ocfs2_xattr_info xi = {
		.xi_name_index = name_index,
		.xi_name = name,
		.xi_name_len = strlen(name),
		.xi_value = value,
		.xi_value_len = value_len,
	};

	struct ocfs2_xattr_search xis = {
		.not_found = -ENODATA,
	};

	struct ocfs2_xattr_search xbs = {
		.not_found = -ENODATA,
	};

	if (!ocfs2_supports_xattr(osb))
		return -EOPNOTSUPP;

	/*
	 * Only xbs will be used on indexed trees.  xis doesn't need a
	 * bucket.
	 */
	xbs.bucket = ocfs2_xattr_bucket_new(inode);
	if (!xbs.bucket) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	had_lock = ocfs2_inode_lock_tracker(inode, &di_bh, 1, &oh);
	if (had_lock < 0) {
		ret = had_lock;
		mlog_errno(ret);
		goto cleanup_nolock;
	}
	xis.inode_bh = xbs.inode_bh = di_bh;
	di = (struct ocfs2_dinode *)di_bh->b_data;

	down_write(&OCFS2_I(inode)->ip_xattr_sem);
	/*
	 * Scan inode and external block to find the same name
	 * extended attribute and collect search information.
	 */
	ret = ocfs2_xattr_ibody_find(inode, name_index, name, &xis);
	if (ret)
		goto cleanup;
	if (xis.not_found) {
		ret = ocfs2_xattr_block_find(inode, name_index, name, &xbs);
		if (ret)
			goto cleanup;
	}

	if (xis.not_found && xbs.not_found) {
		ret = -ENODATA;
		if (flags & XATTR_REPLACE)
			goto cleanup;
		ret = 0;
		if (!value)
			goto cleanup;
	} else {
		ret = -EEXIST;
		if (flags & XATTR_CREATE)
			goto cleanup;
	}

	/* Check whether the value is refcounted and do some preparation. */
	if (ocfs2_is_refcount_inode(inode) &&
	    (!xis.not_found || !xbs.not_found)) {
		ret = ocfs2_prepare_refcount_xattr(inode, di, &xi,
						   &xis, &xbs, &ref_tree,
						   &ref_meta, &ref_credits);
		if (ret) {
			mlog_errno(ret);
			goto cleanup;
		}
	}

	inode_lock(tl_inode);

	if (ocfs2_truncate_log_needs_flush(osb)) {
		ret = __ocfs2_flush_truncate_log(osb);
		if (ret < 0) {
			inode_unlock(tl_inode);
			mlog_errno(ret);
			goto cleanup;
		}
	}
	inode_unlock(tl_inode);

	ret = ocfs2_init_xattr_set_ctxt(inode, di, &xi, &xis,
					&xbs, &ctxt, ref_meta, &credits);
	if (ret) {
		mlog_errno(ret);
		goto cleanup;
	}

	/* we need to update inode's ctime field, so add credit for it. */
	credits += OCFS2_INODE_UPDATE_CREDITS;
	ctxt.handle = ocfs2_start_trans(osb, credits + ref_credits);
	if (IS_ERR(ctxt.handle)) {
		ret = PTR_ERR(ctxt.handle);
		mlog_errno(ret);
		goto out_free_ac;
	}

	ret = __ocfs2_xattr_set_handle(inode, di, &xi, &xis, &xbs, &ctxt);
	ocfs2_update_inode_fsync_trans(ctxt.handle, inode, 0);

	ocfs2_commit_trans(osb, ctxt.handle);

out_free_ac:
	if (ctxt.data_ac)
		ocfs2_free_alloc_context(ctxt.data_ac);
	if (ctxt.meta_ac)
		ocfs2_free_alloc_context(ctxt.meta_ac);
	if (ocfs2_dealloc_has_cluster(&ctxt.dealloc))
		ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &ctxt.dealloc);

cleanup:
	if (ref_tree)
		ocfs2_unlock_refcount_tree(osb, ref_tree, 1);
	up_write(&OCFS2_I(inode)->ip_xattr_sem);
	if (!value && !ret) {
		ret = ocfs2_try_remove_refcount_tree(inode, di_bh);
		if (ret)
			mlog_errno(ret);
	}
	ocfs2_inode_unlock_tracker(inode, 1, &oh, had_lock);
cleanup_nolock:
	brelse(di_bh);
	brelse(xbs.xattr_bh);
	ocfs2_xattr_bucket_free(xbs.bucket);

	return ret;
}

/*
 * Find the xattr extent rec which may contains name_hash.
 * e_cpos will be the first name hash of the xattr rec.
 * el must be the ocfs2_xattr_header.xb_attrs.xb_root.xt_list.
 */
static int ocfs2_xattr_get_rec(struct inode *inode,
			       u32 name_hash,
			       u64 *p_blkno,
			       u32 *e_cpos,
			       u32 *num_clusters,
			       struct ocfs2_extent_list *el)
{
	int ret = 0, i;
	struct buffer_head *eb_bh = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec = NULL;
	u64 e_blkno = 0;

	if (el->l_tree_depth) {
		ret = ocfs2_find_leaf(INODE_CACHE(inode), el, name_hash,
				      &eb_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ret = ocfs2_error(inode->i_sb,
					  "Inode %lu has non zero tree depth in xattr tree block %llu\n",
					  inode->i_ino,
					  (unsigned long long)eb_bh->b_blocknr);
			goto out;
		}
	}

	for (i = le16_to_cpu(el->l_next_free_rec) - 1; i >= 0; i--) {
		rec = &el->l_recs[i];

		if (le32_to_cpu(rec->e_cpos) <= name_hash) {
			e_blkno = le64_to_cpu(rec->e_blkno);
			break;
		}
	}

	if (!e_blkno) {
		ret = ocfs2_error(inode->i_sb, "Inode %lu has bad extent record (%u, %u, 0) in xattr\n",
				  inode->i_ino,
				  le32_to_cpu(rec->e_cpos),
				  ocfs2_rec_clusters(el, rec));
		goto out;
	}

	*p_blkno = le64_to_cpu(rec->e_blkno);
	*num_clusters = le16_to_cpu(rec->e_leaf_clusters);
	if (e_cpos)
		*e_cpos = le32_to_cpu(rec->e_cpos);
out:
	brelse(eb_bh);
	return ret;
}

typedef int (xattr_bucket_func)(struct inode *inode,
				struct ocfs2_xattr_bucket *bucket,
				void *para);

static int ocfs2_find_xe_in_bucket(struct inode *inode,
				   struct ocfs2_xattr_bucket *bucket,
				   int name_index,
				   const char *name,
				   u32 name_hash,
				   u16 *xe_index,
				   int *found)
{
	int i, ret = 0, cmp = 1, block_off, new_offset;
	struct ocfs2_xattr_header *xh = bucket_xh(bucket);
	size_t name_len = strlen(name);
	struct ocfs2_xattr_entry *xe = NULL;
	char *xe_name;

	/*
	 * We don't use binary search in the bucket because there
	 * may be multiple entries with the same name hash.
	 */
	for (i = 0; i < le16_to_cpu(xh->xh_count); i++) {
		xe = &xh->xh_entries[i];

		if (name_hash > le32_to_cpu(xe->xe_name_hash))
			continue;
		else if (name_hash < le32_to_cpu(xe->xe_name_hash))
			break;

		cmp = name_index - ocfs2_xattr_get_type(xe);
		if (!cmp)
			cmp = name_len - xe->xe_name_len;
		if (cmp)
			continue;

		ret = ocfs2_xattr_bucket_get_name_value(inode->i_sb,
							xh,
							i,
							&block_off,
							&new_offset);
		if (ret) {
			mlog_errno(ret);
			break;
		}


		xe_name = bucket_block(bucket, block_off) + new_offset;
		if (!memcmp(name, xe_name, name_len)) {
			*xe_index = i;
			*found = 1;
			ret = 0;
			break;
		}
	}

	return ret;
}

/*
 * Find the specified xattr entry in a series of buckets.
 * This series start from p_blkno and last for num_clusters.
 * The ocfs2_xattr_header.xh_num_buckets of the first bucket contains
 * the num of the valid buckets.
 *
 * Return the buffer_head this xattr should reside in. And if the xattr's
 * hash is in the gap of 2 buckets, return the lower bucket.
 */
static int ocfs2_xattr_bucket_find(struct inode *inode,
				   int name_index,
				   const char *name,
				   u32 name_hash,
				   u64 p_blkno,
				   u32 first_hash,
				   u32 num_clusters,
				   struct ocfs2_xattr_search *xs)
{
	int ret, found = 0;
	struct ocfs2_xattr_header *xh = NULL;
	struct ocfs2_xattr_entry *xe = NULL;
	u16 index = 0;
	u16 blk_per_bucket = ocfs2_blocks_per_xattr_bucket(inode->i_sb);
	int low_bucket = 0, bucket, high_bucket;
	struct ocfs2_xattr_bucket *search;
	u32 last_hash;
	u64 blkno, lower_blkno = 0;

	search = ocfs2_xattr_bucket_new(inode);
	if (!search) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_xattr_bucket(search, p_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	xh = bucket_xh(search);
	high_bucket = le16_to_cpu(xh->xh_num_buckets) - 1;
	while (low_bucket <= high_bucket) {
		ocfs2_xattr_bucket_relse(search);

		bucket = (low_bucket + high_bucket) / 2;
		blkno = p_blkno + bucket * blk_per_bucket;
		ret = ocfs2_read_xattr_bucket(search, blkno);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		xh = bucket_xh(search);
		xe = &xh->xh_entries[0];
		if (name_hash < le32_to_cpu(xe->xe_name_hash)) {
			high_bucket = bucket - 1;
			continue;
		}

		/*
		 * Check whether the hash of the last entry in our
		 * bucket is larger than the search one. for an empty
		 * bucket, the last one is also the first one.
		 */
		if (xh->xh_count)
			xe = &xh->xh_entries[le16_to_cpu(xh->xh_count) - 1];

		last_hash = le32_to_cpu(xe->xe_name_hash);

		/* record lower_blkno which may be the insert place. */
		lower_blkno = blkno;

		if (name_hash > le32_to_cpu(xe->xe_name_hash)) {
			low_bucket = bucket + 1;
			continue;
		}

		/* the searched xattr should reside in this bucket if exists. */
		ret = ocfs2_find_xe_in_bucket(inode, search,
					      name_index, name, name_hash,
					      &index, &found);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		break;
	}

	/*
	 * Record the bucket we have found.
	 * When the xattr's hash value is in the gap of 2 buckets, we will
	 * always set it to the previous bucket.
	 */
	if (!lower_blkno)
		lower_blkno = p_blkno;

	/* This should be in cache - we just read it during the search */
	ret = ocfs2_read_xattr_bucket(xs->bucket, lower_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	xs->header = bucket_xh(xs->bucket);
	xs->base = bucket_block(xs->bucket, 0);
	xs->end = xs->base + inode->i_sb->s_blocksize;

	if (found) {
		xs->here = &xs->header->xh_entries[index];
		trace_ocfs2_xattr_bucket_find(OCFS2_I(inode)->ip_blkno,
			name, name_index, name_hash,
			(unsigned long long)bucket_blkno(xs->bucket),
			index);
	} else
		ret = -ENODATA;

out:
	ocfs2_xattr_bucket_free(search);
	return ret;
}

static int ocfs2_xattr_index_block_find(struct inode *inode,
					struct buffer_head *root_bh,
					int name_index,
					const char *name,
					struct ocfs2_xattr_search *xs)
{
	int ret;
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)root_bh->b_data;
	struct ocfs2_xattr_tree_root *xb_root = &xb->xb_attrs.xb_root;
	struct ocfs2_extent_list *el = &xb_root->xt_list;
	u64 p_blkno = 0;
	u32 first_hash, num_clusters = 0;
	u32 name_hash = ocfs2_xattr_name_hash(inode, name, strlen(name));

	if (le16_to_cpu(el->l_next_free_rec) == 0)
		return -ENODATA;

	trace_ocfs2_xattr_index_block_find(OCFS2_I(inode)->ip_blkno,
					name, name_index, name_hash,
					(unsigned long long)root_bh->b_blocknr,
					-1);

	ret = ocfs2_xattr_get_rec(inode, name_hash, &p_blkno, &first_hash,
				  &num_clusters, el);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	BUG_ON(p_blkno == 0 || num_clusters == 0 || first_hash > name_hash);

	trace_ocfs2_xattr_index_block_find_rec(OCFS2_I(inode)->ip_blkno,
					name, name_index, first_hash,
					(unsigned long long)p_blkno,
					num_clusters);

	ret = ocfs2_xattr_bucket_find(inode, name_index, name, name_hash,
				      p_blkno, first_hash, num_clusters, xs);

out:
	return ret;
}

static int ocfs2_iterate_xattr_buckets(struct inode *inode,
				       u64 blkno,
				       u32 clusters,
				       xattr_bucket_func *func,
				       void *para)
{
	int i, ret = 0;
	u32 bpc = ocfs2_xattr_buckets_per_cluster(OCFS2_SB(inode->i_sb));
	u32 num_buckets = clusters * bpc;
	struct ocfs2_xattr_bucket *bucket;

	bucket = ocfs2_xattr_bucket_new(inode);
	if (!bucket) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	trace_ocfs2_iterate_xattr_buckets(
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		(unsigned long long)blkno, clusters);

	for (i = 0; i < num_buckets; i++, blkno += bucket->bu_blocks) {
		ret = ocfs2_read_xattr_bucket(bucket, blkno);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		/*
		 * The real bucket num in this series of blocks is stored
		 * in the 1st bucket.
		 */
		if (i == 0)
			num_buckets = le16_to_cpu(bucket_xh(bucket)->xh_num_buckets);

		trace_ocfs2_iterate_xattr_bucket((unsigned long long)blkno,
		     le32_to_cpu(bucket_xh(bucket)->xh_entries[0].xe_name_hash));
		if (func) {
			ret = func(inode, bucket, para);
			if (ret && ret != -ERANGE)
				mlog_errno(ret);
			/* Fall through to bucket_relse() */
		}

		ocfs2_xattr_bucket_relse(bucket);
		if (ret)
			break;
	}

	ocfs2_xattr_bucket_free(bucket);
	return ret;
}

struct ocfs2_xattr_tree_list {
	char *buffer;
	size_t buffer_size;
	size_t result;
};

static int ocfs2_xattr_bucket_get_name_value(struct super_block *sb,
					     struct ocfs2_xattr_header *xh,
					     int index,
					     int *block_off,
					     int *new_offset)
{
	u16 name_offset;

	if (index < 0 || index >= le16_to_cpu(xh->xh_count))
		return -EINVAL;

	name_offset = le16_to_cpu(xh->xh_entries[index].xe_name_offset);

	*block_off = name_offset >> sb->s_blocksize_bits;
	*new_offset = name_offset % sb->s_blocksize;

	return 0;
}

static int ocfs2_list_xattr_bucket(struct inode *inode,
				   struct ocfs2_xattr_bucket *bucket,
				   void *para)
{
	int ret = 0, type;
	struct ocfs2_xattr_tree_list *xl = (struct ocfs2_xattr_tree_list *)para;
	int i, block_off, new_offset;
	const char *name;

	for (i = 0 ; i < le16_to_cpu(bucket_xh(bucket)->xh_count); i++) {
		struct ocfs2_xattr_entry *entry = &bucket_xh(bucket)->xh_entries[i];
		type = ocfs2_xattr_get_type(entry);

		ret = ocfs2_xattr_bucket_get_name_value(inode->i_sb,
							bucket_xh(bucket),
							i,
							&block_off,
							&new_offset);
		if (ret)
			break;

		name = (const char *)bucket_block(bucket, block_off) +
			new_offset;
		ret = ocfs2_xattr_list_entry(inode->i_sb,
					     xl->buffer,
					     xl->buffer_size,
					     &xl->result,
					     type, name,
					     entry->xe_name_len);
		if (ret)
			break;
	}

	return ret;
}

static int ocfs2_iterate_xattr_index_block(struct inode *inode,
					   struct buffer_head *blk_bh,
					   xattr_tree_rec_func *rec_func,
					   void *para)
{
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)blk_bh->b_data;
	struct ocfs2_extent_list *el = &xb->xb_attrs.xb_root.xt_list;
	int ret = 0;
	u32 name_hash = UINT_MAX, e_cpos = 0, num_clusters = 0;
	u64 p_blkno = 0;

	if (!el->l_next_free_rec || !rec_func)
		return 0;

	while (name_hash > 0) {
		ret = ocfs2_xattr_get_rec(inode, name_hash, &p_blkno,
					  &e_cpos, &num_clusters, el);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ret = rec_func(inode, blk_bh, p_blkno, e_cpos,
			       num_clusters, para);
		if (ret) {
			if (ret != -ERANGE)
				mlog_errno(ret);
			break;
		}

		if (e_cpos == 0)
			break;

		name_hash = e_cpos - 1;
	}

	return ret;

}

static int ocfs2_list_xattr_tree_rec(struct inode *inode,
				     struct buffer_head *root_bh,
				     u64 blkno, u32 cpos, u32 len, void *para)
{
	return ocfs2_iterate_xattr_buckets(inode, blkno, len,
					   ocfs2_list_xattr_bucket, para);
}

static int ocfs2_xattr_tree_list_index_block(struct inode *inode,
					     struct buffer_head *blk_bh,
					     char *buffer,
					     size_t buffer_size)
{
	int ret;
	struct ocfs2_xattr_tree_list xl = {
		.buffer = buffer,
		.buffer_size = buffer_size,
		.result = 0,
	};

	ret = ocfs2_iterate_xattr_index_block(inode, blk_bh,
					      ocfs2_list_xattr_tree_rec, &xl);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = xl.result;
out:
	return ret;
}

static int cmp_xe(const void *a, const void *b)
{
	const struct ocfs2_xattr_entry *l = a, *r = b;
	u32 l_hash = le32_to_cpu(l->xe_name_hash);
	u32 r_hash = le32_to_cpu(r->xe_name_hash);

	if (l_hash > r_hash)
		return 1;
	if (l_hash < r_hash)
		return -1;
	return 0;
}

static void swap_xe(void *a, void *b, int size)
{
	struct ocfs2_xattr_entry *l = a, *r = b, tmp;

	tmp = *l;
	memcpy(l, r, sizeof(struct ocfs2_xattr_entry));
	memcpy(r, &tmp, sizeof(struct ocfs2_xattr_entry));
}

/*
 * When the ocfs2_xattr_block is filled up, new bucket will be created
 * and all the xattr entries will be moved to the new bucket.
 * The header goes at the start of the bucket, and the names+values are
 * filled from the end.  This is why *target starts as the last buffer.
 * Note: we need to sort the entries since they are not saved in order
 * in the ocfs2_xattr_block.
 */
static void ocfs2_cp_xattr_block_to_bucket(struct inode *inode,
					   struct buffer_head *xb_bh,
					   struct ocfs2_xattr_bucket *bucket)
{
	int i, blocksize = inode->i_sb->s_blocksize;
	int blks = ocfs2_blocks_per_xattr_bucket(inode->i_sb);
	u16 offset, size, off_change;
	struct ocfs2_xattr_entry *xe;
	struct ocfs2_xattr_block *xb =
				(struct ocfs2_xattr_block *)xb_bh->b_data;
	struct ocfs2_xattr_header *xb_xh = &xb->xb_attrs.xb_header;
	struct ocfs2_xattr_header *xh = bucket_xh(bucket);
	u16 count = le16_to_cpu(xb_xh->xh_count);
	char *src = xb_bh->b_data;
	char *target = bucket_block(bucket, blks - 1);

	trace_ocfs2_cp_xattr_block_to_bucket_begin(
				(unsigned long long)xb_bh->b_blocknr,
				(unsigned long long)bucket_blkno(bucket));

	for (i = 0; i < blks; i++)
		memset(bucket_block(bucket, i), 0, blocksize);

	/*
	 * Since the xe_name_offset is based on ocfs2_xattr_header,
	 * there is a offset change corresponding to the change of
	 * ocfs2_xattr_header's position.
	 */
	off_change = offsetof(struct ocfs2_xattr_block, xb_attrs.xb_header);
	xe = &xb_xh->xh_entries[count - 1];
	offset = le16_to_cpu(xe->xe_name_offset) + off_change;
	size = blocksize - offset;

	/* copy all the names and values. */
	memcpy(target + offset, src + offset, size);

	/* Init new header now. */
	xh->xh_count = xb_xh->xh_count;
	xh->xh_num_buckets = cpu_to_le16(1);
	xh->xh_name_value_len = cpu_to_le16(size);
	xh->xh_free_start = cpu_to_le16(OCFS2_XATTR_BUCKET_SIZE - size);

	/* copy all the entries. */
	target = bucket_block(bucket, 0);
	offset = offsetof(struct ocfs2_xattr_header, xh_entries);
	size = count * sizeof(struct ocfs2_xattr_entry);
	memcpy(target + offset, (char *)xb_xh + offset, size);

	/* Change the xe offset for all the xe because of the move. */
	off_change = OCFS2_XATTR_BUCKET_SIZE - blocksize +
		 offsetof(struct ocfs2_xattr_block, xb_attrs.xb_header);
	for (i = 0; i < count; i++)
		le16_add_cpu(&xh->xh_entries[i].xe_name_offset, off_change);

	trace_ocfs2_cp_xattr_block_to_bucket_end(offset, size, off_change);

	sort(target + offset, count, sizeof(struct ocfs2_xattr_entry),
	     cmp_xe, swap_xe);
}

/*
 * After we move xattr from block to index btree, we have to
 * update ocfs2_xattr_search to the new xe and base.
 *
 * When the entry is in xattr block, xattr_bh indicates the storage place.
 * While if the entry is in index b-tree, "bucket" indicates the
 * real place of the xattr.
 */
static void ocfs2_xattr_update_xattr_search(struct inode *inode,
					    struct ocfs2_xattr_search *xs,
					    struct buffer_head *old_bh)
{
	char *buf = old_bh->b_data;
	struct ocfs2_xattr_block *old_xb = (struct ocfs2_xattr_block *)buf;
	struct ocfs2_xattr_header *old_xh = &old_xb->xb_attrs.xb_header;
	int i;

	xs->header = bucket_xh(xs->bucket);
	xs->base = bucket_block(xs->bucket, 0);
	xs->end = xs->base + inode->i_sb->s_blocksize;

	if (xs->not_found)
		return;

	i = xs->here - old_xh->xh_entries;
	xs->here = &xs->header->xh_entries[i];
}

static int ocfs2_xattr_create_index_block(struct inode *inode,
					  struct ocfs2_xattr_search *xs,
					  struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	u32 bit_off, len;
	u64 blkno;
	handle_t *handle = ctxt->handle;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct buffer_head *xb_bh = xs->xattr_bh;
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)xb_bh->b_data;
	struct ocfs2_xattr_tree_root *xr;
	u16 xb_flags = le16_to_cpu(xb->xb_flags);

	trace_ocfs2_xattr_create_index_block_begin(
				(unsigned long long)xb_bh->b_blocknr);

	BUG_ON(xb_flags & OCFS2_XATTR_INDEXED);
	BUG_ON(!xs->bucket);

	/*
	 * XXX:
	 * We can use this lock for now, and maybe move to a dedicated mutex
	 * if performance becomes a problem later.
	 */
	down_write(&oi->ip_alloc_sem);

	ret = ocfs2_journal_access_xb(handle, INODE_CACHE(inode), xb_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = __ocfs2_claim_clusters(handle, ctxt->data_ac,
				     1, 1, &bit_off, &len);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * The bucket may spread in many blocks, and
	 * we will only touch the 1st block and the last block
	 * in the whole bucket(one for entry and one for data).
	 */
	blkno = ocfs2_clusters_to_blocks(inode->i_sb, bit_off);

	trace_ocfs2_xattr_create_index_block((unsigned long long)blkno);

	ret = ocfs2_init_xattr_bucket(xs->bucket, blkno, 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_bucket_journal_access(handle, xs->bucket,
						OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_cp_xattr_block_to_bucket(inode, xb_bh, xs->bucket);
	ocfs2_xattr_bucket_journal_dirty(handle, xs->bucket);

	ocfs2_xattr_update_xattr_search(inode, xs, xb_bh);

	/* Change from ocfs2_xattr_header to ocfs2_xattr_tree_root */
	memset(&xb->xb_attrs, 0, inode->i_sb->s_blocksize -
	       offsetof(struct ocfs2_xattr_block, xb_attrs));

	xr = &xb->xb_attrs.xb_root;
	xr->xt_clusters = cpu_to_le32(1);
	xr->xt_last_eb_blk = 0;
	xr->xt_list.l_tree_depth = 0;
	xr->xt_list.l_count = cpu_to_le16(ocfs2_xattr_recs_per_xb(inode->i_sb));
	xr->xt_list.l_next_free_rec = cpu_to_le16(1);

	xr->xt_list.l_recs[0].e_cpos = 0;
	xr->xt_list.l_recs[0].e_blkno = cpu_to_le64(blkno);
	xr->xt_list.l_recs[0].e_leaf_clusters = cpu_to_le16(1);

	xb->xb_flags = cpu_to_le16(xb_flags | OCFS2_XATTR_INDEXED);

	ocfs2_journal_dirty(handle, xb_bh);

out:
	up_write(&oi->ip_alloc_sem);

	return ret;
}

static int cmp_xe_offset(const void *a, const void *b)
{
	const struct ocfs2_xattr_entry *l = a, *r = b;
	u32 l_name_offset = le16_to_cpu(l->xe_name_offset);
	u32 r_name_offset = le16_to_cpu(r->xe_name_offset);

	if (l_name_offset < r_name_offset)
		return 1;
	if (l_name_offset > r_name_offset)
		return -1;
	return 0;
}

/*
 * defrag a xattr bucket if we find that the bucket has some
 * holes beteen name/value pairs.
 * We will move all the name/value pairs to the end of the bucket
 * so that we can spare some space for insertion.
 */
static int ocfs2_defrag_xattr_bucket(struct inode *inode,
				     handle_t *handle,
				     struct ocfs2_xattr_bucket *bucket)
{
	int ret, i;
	size_t end, offset, len;
	struct ocfs2_xattr_header *xh;
	char *entries, *buf, *bucket_buf = NULL;
	u64 blkno = bucket_blkno(bucket);
	u16 xh_free_start;
	size_t blocksize = inode->i_sb->s_blocksize;
	struct ocfs2_xattr_entry *xe;

	/*
	 * In order to make the operation more efficient and generic,
	 * we copy all the blocks into a contiguous memory and do the
	 * defragment there, so if anything is error, we will not touch
	 * the real block.
	 */
	bucket_buf = kmalloc(OCFS2_XATTR_BUCKET_SIZE, GFP_NOFS);
	if (!bucket_buf) {
		ret = -EIO;
		goto out;
	}

	buf = bucket_buf;
	for (i = 0; i < bucket->bu_blocks; i++, buf += blocksize)
		memcpy(buf, bucket_block(bucket, i), blocksize);

	ret = ocfs2_xattr_bucket_journal_access(handle, bucket,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	xh = (struct ocfs2_xattr_header *)bucket_buf;
	entries = (char *)xh->xh_entries;
	xh_free_start = le16_to_cpu(xh->xh_free_start);

	trace_ocfs2_defrag_xattr_bucket(
	     (unsigned long long)blkno, le16_to_cpu(xh->xh_count),
	     xh_free_start, le16_to_cpu(xh->xh_name_value_len));

	/*
	 * sort all the entries by their offset.
	 * the largest will be the first, so that we can
	 * move them to the end one by one.
	 */
	sort(entries, le16_to_cpu(xh->xh_count),
	     sizeof(struct ocfs2_xattr_entry),
	     cmp_xe_offset, swap_xe);

	/* Move all name/values to the end of the bucket. */
	xe = xh->xh_entries;
	end = OCFS2_XATTR_BUCKET_SIZE;
	for (i = 0; i < le16_to_cpu(xh->xh_count); i++, xe++) {
		offset = le16_to_cpu(xe->xe_name_offset);
		len = namevalue_size_xe(xe);

		/*
		 * We must make sure that the name/value pair
		 * exist in the same block. So adjust end to
		 * the previous block end if needed.
		 */
		if (((end - len) / blocksize !=
			(end - 1) / blocksize))
			end = end - end % blocksize;

		if (end > offset + len) {
			memmove(bucket_buf + end - len,
				bucket_buf + offset, len);
			xe->xe_name_offset = cpu_to_le16(end - len);
		}

		mlog_bug_on_msg(end < offset + len, "Defrag check failed for "
				"bucket %llu\n", (unsigned long long)blkno);

		end -= len;
	}

	mlog_bug_on_msg(xh_free_start > end, "Defrag check failed for "
			"bucket %llu\n", (unsigned long long)blkno);

	if (xh_free_start == end)
		goto out;

	memset(bucket_buf + xh_free_start, 0, end - xh_free_start);
	xh->xh_free_start = cpu_to_le16(end);

	/* sort the entries by their name_hash. */
	sort(entries, le16_to_cpu(xh->xh_count),
	     sizeof(struct ocfs2_xattr_entry),
	     cmp_xe, swap_xe);

	buf = bucket_buf;
	for (i = 0; i < bucket->bu_blocks; i++, buf += blocksize)
		memcpy(bucket_block(bucket, i), buf, blocksize);
	ocfs2_xattr_bucket_journal_dirty(handle, bucket);

out:
	kfree(bucket_buf);
	return ret;
}

/*
 * prev_blkno points to the start of an existing extent.  new_blkno
 * points to a newly allocated extent.  Because we know each of our
 * clusters contains more than bucket, we can easily split one cluster
 * at a bucket boundary.  So we take the last cluster of the existing
 * extent and split it down the middle.  We move the last half of the
 * buckets in the last cluster of the existing extent over to the new
 * extent.
 *
 * first_bh is the buffer at prev_blkno so we can update the existing
 * extent's bucket count.  header_bh is the bucket were we were hoping
 * to insert our xattr.  If the bucket move places the target in the new
 * extent, we'll update first_bh and header_bh after modifying the old
 * extent.
 *
 * first_hash will be set as the 1st xe's name_hash in the new extent.
 */
static int ocfs2_mv_xattr_bucket_cross_cluster(struct inode *inode,
					       handle_t *handle,
					       struct ocfs2_xattr_bucket *first,
					       struct ocfs2_xattr_bucket *target,
					       u64 new_blkno,
					       u32 num_clusters,
					       u32 *first_hash)
{
	int ret;
	struct super_block *sb = inode->i_sb;
	int blks_per_bucket = ocfs2_blocks_per_xattr_bucket(sb);
	int num_buckets = ocfs2_xattr_buckets_per_cluster(OCFS2_SB(sb));
	int to_move = num_buckets / 2;
	u64 src_blkno;
	u64 last_cluster_blkno = bucket_blkno(first) +
		((num_clusters - 1) * ocfs2_clusters_to_blocks(sb, 1));

	BUG_ON(le16_to_cpu(bucket_xh(first)->xh_num_buckets) < num_buckets);
	BUG_ON(OCFS2_XATTR_BUCKET_SIZE == OCFS2_SB(sb)->s_clustersize);

	trace_ocfs2_mv_xattr_bucket_cross_cluster(
				(unsigned long long)last_cluster_blkno,
				(unsigned long long)new_blkno);

	ret = ocfs2_mv_xattr_buckets(inode, handle, bucket_blkno(first),
				     last_cluster_blkno, new_blkno,
				     to_move, first_hash);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* This is the first bucket that got moved */
	src_blkno = last_cluster_blkno + (to_move * blks_per_bucket);

	/*
	 * If the target bucket was part of the moved buckets, we need to
	 * update first and target.
	 */
	if (bucket_blkno(target) >= src_blkno) {
		/* Find the block for the new target bucket */
		src_blkno = new_blkno +
			(bucket_blkno(target) - src_blkno);

		ocfs2_xattr_bucket_relse(first);
		ocfs2_xattr_bucket_relse(target);

		/*
		 * These shouldn't fail - the buffers are in the
		 * journal from ocfs2_cp_xattr_bucket().
		 */
		ret = ocfs2_read_xattr_bucket(first, new_blkno);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		ret = ocfs2_read_xattr_bucket(target, src_blkno);
		if (ret)
			mlog_errno(ret);

	}

out:
	return ret;
}

/*
 * Find the suitable pos when we divide a bucket into 2.
 * We have to make sure the xattrs with the same hash value exist
 * in the same bucket.
 *
 * If this ocfs2_xattr_header covers more than one hash value, find a
 * place where the hash value changes.  Try to find the most even split.
 * The most common case is that all entries have different hash values,
 * and the first check we make will find a place to split.
 */
static int ocfs2_xattr_find_divide_pos(struct ocfs2_xattr_header *xh)
{
	struct ocfs2_xattr_entry *entries = xh->xh_entries;
	int count = le16_to_cpu(xh->xh_count);
	int delta, middle = count / 2;

	/*
	 * We start at the middle.  Each step gets farther away in both
	 * directions.  We therefore hit the change in hash value
	 * nearest to the middle.  Note that this loop does not execute for
	 * count < 2.
	 */
	for (delta = 0; delta < middle; delta++) {
		/* Let's check delta earlier than middle */
		if (cmp_xe(&entries[middle - delta - 1],
			   &entries[middle - delta]))
			return middle - delta;

		/* For even counts, don't walk off the end */
		if ((middle + delta + 1) == count)
			continue;

		/* Now try delta past middle */
		if (cmp_xe(&entries[middle + delta],
			   &entries[middle + delta + 1]))
			return middle + delta + 1;
	}

	/* Every entry had the same hash */
	return count;
}

/*
 * Move some xattrs in old bucket(blk) to new bucket(new_blk).
 * first_hash will record the 1st hash of the new bucket.
 *
 * Normally half of the xattrs will be moved.  But we have to make
 * sure that the xattrs with the same hash value are stored in the
 * same bucket. If all the xattrs in this bucket have the same hash
 * value, the new bucket will be initialized as an empty one and the
 * first_hash will be initialized as (hash_value+1).
 */
static int ocfs2_divide_xattr_bucket(struct inode *inode,
				    handle_t *handle,
				    u64 blk,
				    u64 new_blk,
				    u32 *first_hash,
				    int new_bucket_head)
{
	int ret, i;
	int count, start, len, name_value_len = 0, name_offset = 0;
	struct ocfs2_xattr_bucket *s_bucket = NULL, *t_bucket = NULL;
	struct ocfs2_xattr_header *xh;
	struct ocfs2_xattr_entry *xe;
	int blocksize = inode->i_sb->s_blocksize;

	trace_ocfs2_divide_xattr_bucket_begin((unsigned long long)blk,
					      (unsigned long long)new_blk);

	s_bucket = ocfs2_xattr_bucket_new(inode);
	t_bucket = ocfs2_xattr_bucket_new(inode);
	if (!s_bucket || !t_bucket) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_xattr_bucket(s_bucket, blk);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_bucket_journal_access(handle, s_bucket,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Even if !new_bucket_head, we're overwriting t_bucket.  Thus,
	 * there's no need to read it.
	 */
	ret = ocfs2_init_xattr_bucket(t_bucket, new_blk, new_bucket_head);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Hey, if we're overwriting t_bucket, what difference does
	 * ACCESS_CREATE vs ACCESS_WRITE make?  See the comment in the
	 * same part of ocfs2_cp_xattr_bucket().
	 */
	ret = ocfs2_xattr_bucket_journal_access(handle, t_bucket,
						new_bucket_head ?
						OCFS2_JOURNAL_ACCESS_CREATE :
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	xh = bucket_xh(s_bucket);
	count = le16_to_cpu(xh->xh_count);
	start = ocfs2_xattr_find_divide_pos(xh);

	if (start == count) {
		xe = &xh->xh_entries[start-1];

		/*
		 * initialized a new empty bucket here.
		 * The hash value is set as one larger than
		 * that of the last entry in the previous bucket.
		 */
		for (i = 0; i < t_bucket->bu_blocks; i++)
			memset(bucket_block(t_bucket, i), 0, blocksize);

		xh = bucket_xh(t_bucket);
		xh->xh_free_start = cpu_to_le16(blocksize);
		xh->xh_entries[0].xe_name_hash = xe->xe_name_hash;
		le32_add_cpu(&xh->xh_entries[0].xe_name_hash, 1);

		goto set_num_buckets;
	}

	/* copy the whole bucket to the new first. */
	ocfs2_xattr_bucket_copy_data(t_bucket, s_bucket);

	/* update the new bucket. */
	xh = bucket_xh(t_bucket);

	/*
	 * Calculate the total name/value len and xh_free_start for
	 * the old bucket first.
	 */
	name_offset = OCFS2_XATTR_BUCKET_SIZE;
	name_value_len = 0;
	for (i = 0; i < start; i++) {
		xe = &xh->xh_entries[i];
		name_value_len += namevalue_size_xe(xe);
		if (le16_to_cpu(xe->xe_name_offset) < name_offset)
			name_offset = le16_to_cpu(xe->xe_name_offset);
	}

	/*
	 * Now begin the modification to the new bucket.
	 *
	 * In the new bucket, We just move the xattr entry to the beginning
	 * and don't touch the name/value. So there will be some holes in the
	 * bucket, and they will be removed when ocfs2_defrag_xattr_bucket is
	 * called.
	 */
	xe = &xh->xh_entries[start];
	len = sizeof(struct ocfs2_xattr_entry) * (count - start);
	trace_ocfs2_divide_xattr_bucket_move(len,
			(int)((char *)xe - (char *)xh),
			(int)((char *)xh->xh_entries - (char *)xh));
	memmove((char *)xh->xh_entries, (char *)xe, len);
	xe = &xh->xh_entries[count - start];
	len = sizeof(struct ocfs2_xattr_entry) * start;
	memset((char *)xe, 0, len);

	le16_add_cpu(&xh->xh_count, -start);
	le16_add_cpu(&xh->xh_name_value_len, -name_value_len);

	/* Calculate xh_free_start for the new bucket. */
	xh->xh_free_start = cpu_to_le16(OCFS2_XATTR_BUCKET_SIZE);
	for (i = 0; i < le16_to_cpu(xh->xh_count); i++) {
		xe = &xh->xh_entries[i];
		if (le16_to_cpu(xe->xe_name_offset) <
		    le16_to_cpu(xh->xh_free_start))
			xh->xh_free_start = xe->xe_name_offset;
	}

set_num_buckets:
	/* set xh->xh_num_buckets for the new xh. */
	if (new_bucket_head)
		xh->xh_num_buckets = cpu_to_le16(1);
	else
		xh->xh_num_buckets = 0;

	ocfs2_xattr_bucket_journal_dirty(handle, t_bucket);

	/* store the first_hash of the new bucket. */
	if (first_hash)
		*first_hash = le32_to_cpu(xh->xh_entries[0].xe_name_hash);

	/*
	 * Now only update the 1st block of the old bucket.  If we
	 * just added a new empty bucket, there is no need to modify
	 * it.
	 */
	if (start == count)
		goto out;

	xh = bucket_xh(s_bucket);
	memset(&xh->xh_entries[start], 0,
	       sizeof(struct ocfs2_xattr_entry) * (count - start));
	xh->xh_count = cpu_to_le16(start);
	xh->xh_free_start = cpu_to_le16(name_offset);
	xh->xh_name_value_len = cpu_to_le16(name_value_len);

	ocfs2_xattr_bucket_journal_dirty(handle, s_bucket);

out:
	ocfs2_xattr_bucket_free(s_bucket);
	ocfs2_xattr_bucket_free(t_bucket);

	return ret;
}

/*
 * Copy xattr from one bucket to another bucket.
 *
 * The caller must make sure that the journal transaction
 * has enough space for journaling.
 */
static int ocfs2_cp_xattr_bucket(struct inode *inode,
				 handle_t *handle,
				 u64 s_blkno,
				 u64 t_blkno,
				 int t_is_new)
{
	int ret;
	struct ocfs2_xattr_bucket *s_bucket = NULL, *t_bucket = NULL;

	BUG_ON(s_blkno == t_blkno);

	trace_ocfs2_cp_xattr_bucket((unsigned long long)s_blkno,
				    (unsigned long long)t_blkno,
				    t_is_new);

	s_bucket = ocfs2_xattr_bucket_new(inode);
	t_bucket = ocfs2_xattr_bucket_new(inode);
	if (!s_bucket || !t_bucket) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_xattr_bucket(s_bucket, s_blkno);
	if (ret)
		goto out;

	/*
	 * Even if !t_is_new, we're overwriting t_bucket.  Thus,
	 * there's no need to read it.
	 */
	ret = ocfs2_init_xattr_bucket(t_bucket, t_blkno, t_is_new);
	if (ret)
		goto out;

	/*
	 * Hey, if we're overwriting t_bucket, what difference does
	 * ACCESS_CREATE vs ACCESS_WRITE make?  Well, if we allocated a new
	 * cluster to fill, we came here from
	 * ocfs2_mv_xattr_buckets(), and it is really new -
	 * ACCESS_CREATE is required.  But we also might have moved data
	 * out of t_bucket before extending back into it.
	 * ocfs2_add_new_xattr_bucket() can do this - its call to
	 * ocfs2_add_new_xattr_cluster() may have created a new extent
	 * and copied out the end of the old extent.  Then it re-extends
	 * the old extent back to create space for new xattrs.  That's
	 * how we get here, and the bucket isn't really new.
	 */
	ret = ocfs2_xattr_bucket_journal_access(handle, t_bucket,
						t_is_new ?
						OCFS2_JOURNAL_ACCESS_CREATE :
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret)
		goto out;

	ocfs2_xattr_bucket_copy_data(t_bucket, s_bucket);
	ocfs2_xattr_bucket_journal_dirty(handle, t_bucket);

out:
	ocfs2_xattr_bucket_free(t_bucket);
	ocfs2_xattr_bucket_free(s_bucket);

	return ret;
}

/*
 * src_blk points to the start of an existing extent.  last_blk points to
 * last cluster in that extent.  to_blk points to a newly allocated
 * extent.  We copy the buckets from the cluster at last_blk to the new
 * extent.  If start_bucket is non-zero, we skip that many buckets before
 * we start copying.  The new extent's xh_num_buckets gets set to the
 * number of buckets we copied.  The old extent's xh_num_buckets shrinks
 * by the same amount.
 */
static int ocfs2_mv_xattr_buckets(struct inode *inode, handle_t *handle,
				  u64 src_blk, u64 last_blk, u64 to_blk,
				  unsigned int start_bucket,
				  u32 *first_hash)
{
	int i, ret, credits;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	int blks_per_bucket = ocfs2_blocks_per_xattr_bucket(inode->i_sb);
	int num_buckets = ocfs2_xattr_buckets_per_cluster(osb);
	struct ocfs2_xattr_bucket *old_first, *new_first;

	trace_ocfs2_mv_xattr_buckets((unsigned long long)last_blk,
				     (unsigned long long)to_blk);

	BUG_ON(start_bucket >= num_buckets);
	if (start_bucket) {
		num_buckets -= start_bucket;
		last_blk += (start_bucket * blks_per_bucket);
	}

	/* The first bucket of the original extent */
	old_first = ocfs2_xattr_bucket_new(inode);
	/* The first bucket of the new extent */
	new_first = ocfs2_xattr_bucket_new(inode);
	if (!old_first || !new_first) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_xattr_bucket(old_first, src_blk);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * We need to update the first bucket of the old extent and all
	 * the buckets going to the new extent.
	 */
	credits = ((num_buckets + 1) * blks_per_bucket);
	ret = ocfs2_extend_trans(handle, credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_bucket_journal_access(handle, old_first,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	for (i = 0; i < num_buckets; i++) {
		ret = ocfs2_cp_xattr_bucket(inode, handle,
					    last_blk + (i * blks_per_bucket),
					    to_blk + (i * blks_per_bucket),
					    1);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * Get the new bucket ready before we dirty anything
	 * (This actually shouldn't fail, because we already dirtied
	 * it once in ocfs2_cp_xattr_bucket()).
	 */
	ret = ocfs2_read_xattr_bucket(new_first, to_blk);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	ret = ocfs2_xattr_bucket_journal_access(handle, new_first,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* Now update the headers */
	le16_add_cpu(&bucket_xh(old_first)->xh_num_buckets, -num_buckets);
	ocfs2_xattr_bucket_journal_dirty(handle, old_first);

	bucket_xh(new_first)->xh_num_buckets = cpu_to_le16(num_buckets);
	ocfs2_xattr_bucket_journal_dirty(handle, new_first);

	if (first_hash)
		*first_hash = le32_to_cpu(bucket_xh(new_first)->xh_entries[0].xe_name_hash);

out:
	ocfs2_xattr_bucket_free(new_first);
	ocfs2_xattr_bucket_free(old_first);
	return ret;
}

/*
 * Move some xattrs in this cluster to the new cluster.
 * This function should only be called when bucket size == cluster size.
 * Otherwise ocfs2_mv_xattr_bucket_cross_cluster should be used instead.
 */
static int ocfs2_divide_xattr_cluster(struct inode *inode,
				      handle_t *handle,
				      u64 prev_blk,
				      u64 new_blk,
				      u32 *first_hash)
{
	u16 blk_per_bucket = ocfs2_blocks_per_xattr_bucket(inode->i_sb);
	int ret, credits = 2 * blk_per_bucket;

	BUG_ON(OCFS2_XATTR_BUCKET_SIZE < OCFS2_SB(inode->i_sb)->s_clustersize);

	ret = ocfs2_extend_trans(handle, credits);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	/* Move half of the xattr in start_blk to the next bucket. */
	return  ocfs2_divide_xattr_bucket(inode, handle, prev_blk,
					  new_blk, first_hash, 1);
}

/*
 * Move some xattrs from the old cluster to the new one since they are not
 * contiguous in ocfs2 xattr tree.
 *
 * new_blk starts a new separate cluster, and we will move some xattrs from
 * prev_blk to it. v_start will be set as the first name hash value in this
 * new cluster so that it can be used as e_cpos during tree insertion and
 * don't collide with our original b-tree operations. first_bh and header_bh
 * will also be updated since they will be used in ocfs2_extend_xattr_bucket
 * to extend the insert bucket.
 *
 * The problem is how much xattr should we move to the new one and when should
 * we update first_bh and header_bh?
 * 1. If cluster size > bucket size, that means the previous cluster has more
 *    than 1 bucket, so just move half nums of bucket into the new cluster and
 *    update the first_bh and header_bh if the insert bucket has been moved
 *    to the new cluster.
 * 2. If cluster_size == bucket_size:
 *    a) If the previous extent rec has more than one cluster and the insert
 *       place isn't in the last cluster, copy the entire last cluster to the
 *       new one. This time, we don't need to upate the first_bh and header_bh
 *       since they will not be moved into the new cluster.
 *    b) Otherwise, move the bottom half of the xattrs in the last cluster into
 *       the new one. And we set the extend flag to zero if the insert place is
 *       moved into the new allocated cluster since no extend is needed.
 */
static int ocfs2_adjust_xattr_cross_cluster(struct inode *inode,
					    handle_t *handle,
					    struct ocfs2_xattr_bucket *first,
					    struct ocfs2_xattr_bucket *target,
					    u64 new_blk,
					    u32 prev_clusters,
					    u32 *v_start,
					    int *extend)
{
	int ret;

	trace_ocfs2_adjust_xattr_cross_cluster(
			(unsigned long long)bucket_blkno(first),
			(unsigned long long)new_blk, prev_clusters);

	if (ocfs2_xattr_buckets_per_cluster(OCFS2_SB(inode->i_sb)) > 1) {
		ret = ocfs2_mv_xattr_bucket_cross_cluster(inode,
							  handle,
							  first, target,
							  new_blk,
							  prev_clusters,
							  v_start);
		if (ret)
			mlog_errno(ret);
	} else {
		/* The start of the last cluster in the first extent */
		u64 last_blk = bucket_blkno(first) +
			((prev_clusters - 1) *
			 ocfs2_clusters_to_blocks(inode->i_sb, 1));

		if (prev_clusters > 1 && bucket_blkno(target) != last_blk) {
			ret = ocfs2_mv_xattr_buckets(inode, handle,
						     bucket_blkno(first),
						     last_blk, new_blk, 0,
						     v_start);
			if (ret)
				mlog_errno(ret);
		} else {
			ret = ocfs2_divide_xattr_cluster(inode, handle,
							 last_blk, new_blk,
							 v_start);
			if (ret)
				mlog_errno(ret);

			if ((bucket_blkno(target) == last_blk) && extend)
				*extend = 0;
		}
	}

	return ret;
}

/*
 * Add a new cluster for xattr storage.
 *
 * If the new cluster is contiguous with the previous one, it will be
 * appended to the same extent record, and num_clusters will be updated.
 * If not, we will insert a new extent for it and move some xattrs in
 * the last cluster into the new allocated one.
 * We also need to limit the maximum size of a btree leaf, otherwise we'll
 * lose the benefits of hashing because we'll have to search large leaves.
 * So now the maximum size is OCFS2_MAX_XATTR_TREE_LEAF_SIZE(or clustersize,
 * if it's bigger).
 *
 * first_bh is the first block of the previous extent rec and header_bh
 * indicates the bucket we will insert the new xattrs. They will be updated
 * when the header_bh is moved into the new cluster.
 */
static int ocfs2_add_new_xattr_cluster(struct inode *inode,
				       struct buffer_head *root_bh,
				       struct ocfs2_xattr_bucket *first,
				       struct ocfs2_xattr_bucket *target,
				       u32 *num_clusters,
				       u32 prev_cpos,
				       int *extend,
				       struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	u16 bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	u32 prev_clusters = *num_clusters;
	u32 clusters_to_add = 1, bit_off, num_bits, v_start = 0;
	u64 block;
	handle_t *handle = ctxt->handle;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_extent_tree et;

	trace_ocfs2_add_new_xattr_cluster_begin(
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		(unsigned long long)bucket_blkno(first),
		prev_cpos, prev_clusters);

	ocfs2_init_xattr_tree_extent_tree(&et, INODE_CACHE(inode), root_bh);

	ret = ocfs2_journal_access_xb(handle, INODE_CACHE(inode), root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto leave;
	}

	ret = __ocfs2_claim_clusters(handle, ctxt->data_ac, 1,
				     clusters_to_add, &bit_off, &num_bits);
	if (ret < 0) {
		if (ret != -ENOSPC)
			mlog_errno(ret);
		goto leave;
	}

	BUG_ON(num_bits > clusters_to_add);

	block = ocfs2_clusters_to_blocks(osb->sb, bit_off);
	trace_ocfs2_add_new_xattr_cluster((unsigned long long)block, num_bits);

	if (bucket_blkno(first) + (prev_clusters * bpc) == block &&
	    (prev_clusters + num_bits) << osb->s_clustersize_bits <=
	     OCFS2_MAX_XATTR_TREE_LEAF_SIZE) {
		/*
		 * If this cluster is contiguous with the old one and
		 * adding this new cluster, we don't surpass the limit of
		 * OCFS2_MAX_XATTR_TREE_LEAF_SIZE, cool. We will let it be
		 * initialized and used like other buckets in the previous
		 * cluster.
		 * So add it as a contiguous one. The caller will handle
		 * its init process.
		 */
		v_start = prev_cpos + prev_clusters;
		*num_clusters = prev_clusters + num_bits;
	} else {
		ret = ocfs2_adjust_xattr_cross_cluster(inode,
						       handle,
						       first,
						       target,
						       block,
						       prev_clusters,
						       &v_start,
						       extend);
		if (ret) {
			mlog_errno(ret);
			goto leave;
		}
	}

	trace_ocfs2_add_new_xattr_cluster_insert((unsigned long long)block,
						 v_start, num_bits);
	ret = ocfs2_insert_extent(handle, &et, v_start, block,
				  num_bits, 0, ctxt->meta_ac);
	if (ret < 0) {
		mlog_errno(ret);
		goto leave;
	}

	ocfs2_journal_dirty(handle, root_bh);

leave:
	return ret;
}

/*
 * We are given an extent.  'first' is the bucket at the very front of
 * the extent.  The extent has space for an additional bucket past
 * bucket_xh(first)->xh_num_buckets.  'target_blkno' is the block number
 * of the target bucket.  We wish to shift every bucket past the target
 * down one, filling in that additional space.  When we get back to the
 * target, we split the target between itself and the now-empty bucket
 * at target+1 (aka, target_blkno + blks_per_bucket).
 */
static int ocfs2_extend_xattr_bucket(struct inode *inode,
				     handle_t *handle,
				     struct ocfs2_xattr_bucket *first,
				     u64 target_blk,
				     u32 num_clusters)
{
	int ret, credits;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	u16 blk_per_bucket = ocfs2_blocks_per_xattr_bucket(inode->i_sb);
	u64 end_blk;
	u16 new_bucket = le16_to_cpu(bucket_xh(first)->xh_num_buckets);

	trace_ocfs2_extend_xattr_bucket((unsigned long long)target_blk,
					(unsigned long long)bucket_blkno(first),
					num_clusters, new_bucket);

	/* The extent must have room for an additional bucket */
	BUG_ON(new_bucket >=
	       (num_clusters * ocfs2_xattr_buckets_per_cluster(osb)));

	/* end_blk points to the last existing bucket */
	end_blk = bucket_blkno(first) + ((new_bucket - 1) * blk_per_bucket);

	/*
	 * end_blk is the start of the last existing bucket.
	 * Thus, (end_blk - target_blk) covers the target bucket and
	 * every bucket after it up to, but not including, the last
	 * existing bucket.  Then we add the last existing bucket, the
	 * new bucket, and the first bucket (3 * blk_per_bucket).
	 */
	credits = (end_blk - target_blk) + (3 * blk_per_bucket);
	ret = ocfs2_extend_trans(handle, credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_bucket_journal_access(handle, first,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	while (end_blk != target_blk) {
		ret = ocfs2_cp_xattr_bucket(inode, handle, end_blk,
					    end_blk + blk_per_bucket, 0);
		if (ret)
			goto out;
		end_blk -= blk_per_bucket;
	}

	/* Move half of the xattr in target_blkno to the next bucket. */
	ret = ocfs2_divide_xattr_bucket(inode, handle, target_blk,
					target_blk + blk_per_bucket, NULL, 0);

	le16_add_cpu(&bucket_xh(first)->xh_num_buckets, 1);
	ocfs2_xattr_bucket_journal_dirty(handle, first);

out:
	return ret;
}

/*
 * Add new xattr bucket in an extent record and adjust the buckets
 * accordingly.  xb_bh is the ocfs2_xattr_block, and target is the
 * bucket we want to insert into.
 *
 * In the easy case, we will move all the buckets after target down by
 * one. Half of target's xattrs will be moved to the next bucket.
 *
 * If current cluster is full, we'll allocate a new one.  This may not
 * be contiguous.  The underlying calls will make sure that there is
 * space for the insert, shifting buckets around if necessary.
 * 'target' may be moved by those calls.
 */
static int ocfs2_add_new_xattr_bucket(struct inode *inode,
				      struct buffer_head *xb_bh,
				      struct ocfs2_xattr_bucket *target,
				      struct ocfs2_xattr_set_ctxt *ctxt)
{
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)xb_bh->b_data;
	struct ocfs2_xattr_tree_root *xb_root = &xb->xb_attrs.xb_root;
	struct ocfs2_extent_list *el = &xb_root->xt_list;
	u32 name_hash =
		le32_to_cpu(bucket_xh(target)->xh_entries[0].xe_name_hash);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	int ret, num_buckets, extend = 1;
	u64 p_blkno;
	u32 e_cpos, num_clusters;
	/* The bucket at the front of the extent */
	struct ocfs2_xattr_bucket *first;

	trace_ocfs2_add_new_xattr_bucket(
				(unsigned long long)bucket_blkno(target));

	/* The first bucket of the original extent */
	first = ocfs2_xattr_bucket_new(inode);
	if (!first) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_get_rec(inode, name_hash, &p_blkno, &e_cpos,
				  &num_clusters, el);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_xattr_bucket(first, p_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	num_buckets = ocfs2_xattr_buckets_per_cluster(osb) * num_clusters;
	if (num_buckets == le16_to_cpu(bucket_xh(first)->xh_num_buckets)) {
		/*
		 * This can move first+target if the target bucket moves
		 * to the new extent.
		 */
		ret = ocfs2_add_new_xattr_cluster(inode,
						  xb_bh,
						  first,
						  target,
						  &num_clusters,
						  e_cpos,
						  &extend,
						  ctxt);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (extend) {
		ret = ocfs2_extend_xattr_bucket(inode,
						ctxt->handle,
						first,
						bucket_blkno(target),
						num_clusters);
		if (ret)
			mlog_errno(ret);
	}

out:
	ocfs2_xattr_bucket_free(first);

	return ret;
}

/*
 * Truncate the specified xe_off entry in xattr bucket.
 * bucket is indicated by header_bh and len is the new length.
 * Both the ocfs2_xattr_value_root and the entry will be updated here.
 *
 * Copy the new updated xe and xe_value_root to new_xe and new_xv if needed.
 */
static int ocfs2_xattr_bucket_value_truncate(struct inode *inode,
					     struct ocfs2_xattr_bucket *bucket,
					     int xe_off,
					     int len,
					     struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret, offset;
	u64 value_blk;
	struct ocfs2_xattr_entry *xe;
	struct ocfs2_xattr_header *xh = bucket_xh(bucket);
	size_t blocksize = inode->i_sb->s_blocksize;
	struct ocfs2_xattr_value_buf vb = {
		.vb_access = ocfs2_journal_access,
	};

	xe = &xh->xh_entries[xe_off];

	BUG_ON(!xe || ocfs2_xattr_is_local(xe));

	offset = le16_to_cpu(xe->xe_name_offset) +
		 OCFS2_XATTR_SIZE(xe->xe_name_len);

	value_blk = offset / blocksize;

	/* We don't allow ocfs2_xattr_value to be stored in different block. */
	BUG_ON(value_blk != (offset + OCFS2_XATTR_ROOT_SIZE - 1) / blocksize);

	vb.vb_bh = bucket->bu_bhs[value_blk];
	BUG_ON(!vb.vb_bh);

	vb.vb_xv = (struct ocfs2_xattr_value_root *)
		(vb.vb_bh->b_data + offset % blocksize);

	/*
	 * From here on out we have to dirty the bucket.  The generic
	 * value calls only modify one of the bucket's bhs, but we need
	 * to send the bucket at once.  So if they error, they *could* have
	 * modified something.  We have to assume they did, and dirty
	 * the whole bucket.  This leaves us in a consistent state.
	 */
	trace_ocfs2_xattr_bucket_value_truncate(
			(unsigned long long)bucket_blkno(bucket), xe_off, len);
	ret = ocfs2_xattr_value_truncate(inode, &vb, len, ctxt);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_bucket_journal_access(ctxt->handle, bucket,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	xe->xe_value_size = cpu_to_le64(len);

	ocfs2_xattr_bucket_journal_dirty(ctxt->handle, bucket);

out:
	return ret;
}

static int ocfs2_rm_xattr_cluster(struct inode *inode,
				  struct buffer_head *root_bh,
				  u64 blkno,
				  u32 cpos,
				  u32 len,
				  void *para)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	handle_t *handle;
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)root_bh->b_data;
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_extent_tree et;

	ret = ocfs2_iterate_xattr_buckets(inode, blkno, len,
					  ocfs2_delete_xattr_in_bucket, para);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	ocfs2_init_xattr_tree_extent_tree(&et, INODE_CACHE(inode), root_bh);

	ocfs2_init_dealloc_ctxt(&dealloc);

	trace_ocfs2_rm_xattr_cluster(
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(unsigned long long)blkno, cpos, len);

	ocfs2_remove_xattr_clusters_from_cache(INODE_CACHE(inode), blkno,
					       len);

	ret = ocfs2_lock_allocators(inode, &et, 0, 1, NULL, &meta_ac);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	inode_lock(tl_inode);

	if (ocfs2_truncate_log_needs_flush(osb)) {
		ret = __ocfs2_flush_truncate_log(osb);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	handle = ocfs2_start_trans(osb, ocfs2_remove_extent_credits(osb->sb));
	if (IS_ERR(handle)) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_xb(handle, INODE_CACHE(inode), root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_remove_extent(handle, &et, cpos, len, meta_ac,
				  &dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	le32_add_cpu(&xb->xb_attrs.xb_root.xt_clusters, -len);
	ocfs2_journal_dirty(handle, root_bh);

	ret = ocfs2_truncate_log_append(osb, handle, blkno, len);
	if (ret)
		mlog_errno(ret);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	ocfs2_schedule_truncate_log_flush(osb, 1);

	inode_unlock(tl_inode);

	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	ocfs2_run_deallocs(osb, &dealloc);

	return ret;
}

/*
 * check whether the xattr bucket is filled up with the same hash value.
 * If we want to insert the xattr with the same hash, return -ENOSPC.
 * If we want to insert a xattr with different hash value, go ahead
 * and ocfs2_divide_xattr_bucket will handle this.
 */
static int ocfs2_check_xattr_bucket_collision(struct inode *inode,
					      struct ocfs2_xattr_bucket *bucket,
					      const char *name)
{
	struct ocfs2_xattr_header *xh = bucket_xh(bucket);
	u32 name_hash = ocfs2_xattr_name_hash(inode, name, strlen(name));

	if (name_hash != le32_to_cpu(xh->xh_entries[0].xe_name_hash))
		return 0;

	if (xh->xh_entries[le16_to_cpu(xh->xh_count) - 1].xe_name_hash ==
	    xh->xh_entries[0].xe_name_hash) {
		mlog(ML_ERROR, "Too much hash collision in xattr bucket %llu, "
		     "hash = %u\n",
		     (unsigned long long)bucket_blkno(bucket),
		     le32_to_cpu(xh->xh_entries[0].xe_name_hash));
		return -ENOSPC;
	}

	return 0;
}

/*
 * Try to set the entry in the current bucket.  If we fail, the caller
 * will handle getting us another bucket.
 */
static int ocfs2_xattr_set_entry_bucket(struct inode *inode,
					struct ocfs2_xattr_info *xi,
					struct ocfs2_xattr_search *xs,
					struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;
	struct ocfs2_xa_loc loc;

	trace_ocfs2_xattr_set_entry_bucket(xi->xi_name);

	ocfs2_init_xattr_bucket_xa_loc(&loc, xs->bucket,
				       xs->not_found ? NULL : xs->here);
	ret = ocfs2_xa_set(&loc, xi, ctxt);
	if (!ret) {
		xs->here = loc.xl_entry;
		goto out;
	}
	if (ret != -ENOSPC) {
		mlog_errno(ret);
		goto out;
	}

	/* Ok, we need space.  Let's try defragmenting the bucket. */
	ret = ocfs2_defrag_xattr_bucket(inode, ctxt->handle,
					xs->bucket);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xa_set(&loc, xi, ctxt);
	if (!ret) {
		xs->here = loc.xl_entry;
		goto out;
	}
	if (ret != -ENOSPC)
		mlog_errno(ret);


out:
	return ret;
}

static int ocfs2_xattr_set_entry_index_block(struct inode *inode,
					     struct ocfs2_xattr_info *xi,
					     struct ocfs2_xattr_search *xs,
					     struct ocfs2_xattr_set_ctxt *ctxt)
{
	int ret;

	trace_ocfs2_xattr_set_entry_index_block(xi->xi_name);

	ret = ocfs2_xattr_set_entry_bucket(inode, xi, xs, ctxt);
	if (!ret)
		goto out;
	if (ret != -ENOSPC) {
		mlog_errno(ret);
		goto out;
	}

	/* Ack, need more space.  Let's try to get another bucket! */

	/*
	 * We do not allow for overlapping ranges between buckets. And
	 * the maximum number of collisions we will allow for then is
	 * one bucket's worth, so check it here whether we need to
	 * add a new bucket for the insert.
	 */
	ret = ocfs2_check_xattr_bucket_collision(inode,
						 xs->bucket,
						 xi->xi_name);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_add_new_xattr_bucket(inode,
					 xs->xattr_bh,
					 xs->bucket,
					 ctxt);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * ocfs2_add_new_xattr_bucket() will have updated
	 * xs->bucket if it moved, but it will not have updated
	 * any of the other search fields.  Thus, we drop it and
	 * re-search.  Everything should be cached, so it'll be
	 * quick.
	 */
	ocfs2_xattr_bucket_relse(xs->bucket);
	ret = ocfs2_xattr_index_block_find(inode, xs->xattr_bh,
					   xi->xi_name_index,
					   xi->xi_name, xs);
	if (ret && ret != -ENODATA)
		goto out;
	xs->not_found = ret;

	/* Ok, we have a new bucket, let's try again */
	ret = ocfs2_xattr_set_entry_bucket(inode, xi, xs, ctxt);
	if (ret && (ret != -ENOSPC))
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_delete_xattr_in_bucket(struct inode *inode,
					struct ocfs2_xattr_bucket *bucket,
					void *para)
{
	int ret = 0, ref_credits;
	struct ocfs2_xattr_header *xh = bucket_xh(bucket);
	u16 i;
	struct ocfs2_xattr_entry *xe;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_xattr_set_ctxt ctxt = {NULL, NULL,};
	int credits = ocfs2_remove_extent_credits(osb->sb) +
		ocfs2_blocks_per_xattr_bucket(inode->i_sb);
	struct ocfs2_xattr_value_root *xv;
	struct ocfs2_rm_xattr_bucket_para *args =
			(struct ocfs2_rm_xattr_bucket_para *)para;

	ocfs2_init_dealloc_ctxt(&ctxt.dealloc);

	for (i = 0; i < le16_to_cpu(xh->xh_count); i++) {
		xe = &xh->xh_entries[i];
		if (ocfs2_xattr_is_local(xe))
			continue;

		ret = ocfs2_get_xattr_tree_value_root(inode->i_sb, bucket,
						      i, &xv, NULL);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_lock_xattr_remove_allocators(inode, xv,
							 args->ref_ci,
							 args->ref_root_bh,
							 &ctxt.meta_ac,
							 &ref_credits);

		ctxt.handle = ocfs2_start_trans(osb, credits + ref_credits);
		if (IS_ERR(ctxt.handle)) {
			ret = PTR_ERR(ctxt.handle);
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_xattr_bucket_value_truncate(inode, bucket,
							i, 0, &ctxt);

		ocfs2_commit_trans(osb, ctxt.handle);
		if (ctxt.meta_ac) {
			ocfs2_free_alloc_context(ctxt.meta_ac);
			ctxt.meta_ac = NULL;
		}
		if (ret) {
			mlog_errno(ret);
			break;
		}
	}

	if (ctxt.meta_ac)
		ocfs2_free_alloc_context(ctxt.meta_ac);
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &ctxt.dealloc);
	return ret;
}

/*
 * Whenever we modify a xattr value root in the bucket(e.g, CoW
 * or change the extent record flag), we need to recalculate
 * the metaecc for the whole bucket. So it is done here.
 *
 * Note:
 * We have to give the extra credits for the caller.
 */
static int ocfs2_xattr_bucket_post_refcount(struct inode *inode,
					    handle_t *handle,
					    void *para)
{
	int ret;
	struct ocfs2_xattr_bucket *bucket =
			(struct ocfs2_xattr_bucket *)para;

	ret = ocfs2_xattr_bucket_journal_access(handle, bucket,
						OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	ocfs2_xattr_bucket_journal_dirty(handle, bucket);

	return 0;
}

/*
 * Special action we need if the xattr value is refcounted.
 *
 * 1. If the xattr is refcounted, lock the tree.
 * 2. CoW the xattr if we are setting the new value and the value
 *    will be stored outside.
 * 3. In other case, decrease_refcount will work for us, so just
 *    lock the refcount tree, calculate the meta and credits is OK.
 *
 * We have to do CoW before ocfs2_init_xattr_set_ctxt since
 * currently CoW is a completed transaction, while this function
 * will also lock the allocators and let us deadlock. So we will
 * CoW the whole xattr value.
 */
static int ocfs2_prepare_refcount_xattr(struct inode *inode,
					struct ocfs2_dinode *di,
					struct ocfs2_xattr_info *xi,
					struct ocfs2_xattr_search *xis,
					struct ocfs2_xattr_search *xbs,
					struct ocfs2_refcount_tree **ref_tree,
					int *meta_add,
					int *credits)
{
	int ret = 0;
	struct ocfs2_xattr_block *xb;
	struct ocfs2_xattr_entry *xe;
	char *base;
	u32 p_cluster, num_clusters;
	unsigned int ext_flags;
	int name_offset, name_len;
	struct ocfs2_xattr_value_buf vb;
	struct ocfs2_xattr_bucket *bucket = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_post_refcount refcount;
	struct ocfs2_post_refcount *p = NULL;
	struct buffer_head *ref_root_bh = NULL;

	if (!xis->not_found) {
		xe = xis->here;
		name_offset = le16_to_cpu(xe->xe_name_offset);
		name_len = OCFS2_XATTR_SIZE(xe->xe_name_len);
		base = xis->base;
		vb.vb_bh = xis->inode_bh;
		vb.vb_access = ocfs2_journal_access_di;
	} else {
		int i, block_off = 0;
		xb = (struct ocfs2_xattr_block *)xbs->xattr_bh->b_data;
		xe = xbs->here;
		name_offset = le16_to_cpu(xe->xe_name_offset);
		name_len = OCFS2_XATTR_SIZE(xe->xe_name_len);
		i = xbs->here - xbs->header->xh_entries;

		if (le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED) {
			ret = ocfs2_xattr_bucket_get_name_value(inode->i_sb,
							bucket_xh(xbs->bucket),
							i, &block_off,
							&name_offset);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
			base = bucket_block(xbs->bucket, block_off);
			vb.vb_bh = xbs->bucket->bu_bhs[block_off];
			vb.vb_access = ocfs2_journal_access;

			if (ocfs2_meta_ecc(osb)) {
				/*create parameters for ocfs2_post_refcount. */
				bucket = xbs->bucket;
				refcount.credits = bucket->bu_blocks;
				refcount.para = bucket;
				refcount.func =
					ocfs2_xattr_bucket_post_refcount;
				p = &refcount;
			}
		} else {
			base = xbs->base;
			vb.vb_bh = xbs->xattr_bh;
			vb.vb_access = ocfs2_journal_access_xb;
		}
	}

	if (ocfs2_xattr_is_local(xe))
		goto out;

	vb.vb_xv = (struct ocfs2_xattr_value_root *)
				(base + name_offset + name_len);

	ret = ocfs2_xattr_get_clusters(inode, 0, &p_cluster,
				       &num_clusters, &vb.vb_xv->xr_list,
				       &ext_flags);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * We just need to check the 1st extent record, since we always
	 * CoW the whole xattr. So there shouldn't be a xattr with
	 * some REFCOUNT extent recs after the 1st one.
	 */
	if (!(ext_flags & OCFS2_EXT_REFCOUNTED))
		goto out;

	ret = ocfs2_lock_refcount_tree(osb, le64_to_cpu(di->i_refcount_loc),
				       1, ref_tree, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * If we are deleting the xattr or the new size will be stored inside,
	 * cool, leave it there, the xattr truncate process will remove them
	 * for us(it still needs the refcount tree lock and the meta, credits).
	 * And the worse case is that every cluster truncate will split the
	 * refcount tree, and make the original extent become 3. So we will need
	 * 2 * cluster more extent recs at most.
	 */
	if (!xi->xi_value || xi->xi_value_len <= OCFS2_XATTR_INLINE_SIZE) {

		ret = ocfs2_refcounted_xattr_delete_need(inode,
							 &(*ref_tree)->rf_ci,
							 ref_root_bh, vb.vb_xv,
							 meta_add, credits);
		if (ret)
			mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_refcount_cow_xattr(inode, di, &vb,
				       *ref_tree, ref_root_bh, 0,
				       le32_to_cpu(vb.vb_xv->xr_clusters), p);
	if (ret)
		mlog_errno(ret);

out:
	brelse(ref_root_bh);
	return ret;
}

/*
 * Add the REFCOUNTED flags for all the extent rec in ocfs2_xattr_value_root.
 * The physical clusters will be added to refcount tree.
 */
static int ocfs2_xattr_value_attach_refcount(struct inode *inode,
				struct ocfs2_xattr_value_root *xv,
				struct ocfs2_extent_tree *value_et,
				struct ocfs2_caching_info *ref_ci,
				struct buffer_head *ref_root_bh,
				struct ocfs2_cached_dealloc_ctxt *dealloc,
				struct ocfs2_post_refcount *refcount)
{
	int ret = 0;
	u32 clusters = le32_to_cpu(xv->xr_clusters);
	u32 cpos, p_cluster, num_clusters;
	struct ocfs2_extent_list *el = &xv->xr_list;
	unsigned int ext_flags;

	cpos = 0;
	while (cpos < clusters) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &p_cluster,
					       &num_clusters, el, &ext_flags);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		cpos += num_clusters;
		if ((ext_flags & OCFS2_EXT_REFCOUNTED))
			continue;

		BUG_ON(!p_cluster);

		ret = ocfs2_add_refcount_flag(inode, value_et,
					      ref_ci, ref_root_bh,
					      cpos - num_clusters,
					      p_cluster, num_clusters,
					      dealloc, refcount);
		if (ret) {
			mlog_errno(ret);
			break;
		}
	}

	return ret;
}

/*
 * Given a normal ocfs2_xattr_header, refcount all the entries which
 * have value stored outside.
 * Used for xattrs stored in inode and ocfs2_xattr_block.
 */
static int ocfs2_xattr_attach_refcount_normal(struct inode *inode,
				struct ocfs2_xattr_value_buf *vb,
				struct ocfs2_xattr_header *header,
				struct ocfs2_caching_info *ref_ci,
				struct buffer_head *ref_root_bh,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{

	struct ocfs2_xattr_entry *xe;
	struct ocfs2_xattr_value_root *xv;
	struct ocfs2_extent_tree et;
	int i, ret = 0;

	for (i = 0; i < le16_to_cpu(header->xh_count); i++) {
		xe = &header->xh_entries[i];

		if (ocfs2_xattr_is_local(xe))
			continue;

		xv = (struct ocfs2_xattr_value_root *)((void *)header +
			le16_to_cpu(xe->xe_name_offset) +
			OCFS2_XATTR_SIZE(xe->xe_name_len));

		vb->vb_xv = xv;
		ocfs2_init_xattr_value_extent_tree(&et, INODE_CACHE(inode), vb);

		ret = ocfs2_xattr_value_attach_refcount(inode, xv, &et,
							ref_ci, ref_root_bh,
							dealloc, NULL);
		if (ret) {
			mlog_errno(ret);
			break;
		}
	}

	return ret;
}

static int ocfs2_xattr_inline_attach_refcount(struct inode *inode,
				struct buffer_head *fe_bh,
				struct ocfs2_caching_info *ref_ci,
				struct buffer_head *ref_root_bh,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)fe_bh->b_data;
	struct ocfs2_xattr_header *header = (struct ocfs2_xattr_header *)
				(fe_bh->b_data + inode->i_sb->s_blocksize -
				le16_to_cpu(di->i_xattr_inline_size));
	struct ocfs2_xattr_value_buf vb = {
		.vb_bh = fe_bh,
		.vb_access = ocfs2_journal_access_di,
	};

	return ocfs2_xattr_attach_refcount_normal(inode, &vb, header,
						  ref_ci, ref_root_bh, dealloc);
}

struct ocfs2_xattr_tree_value_refcount_para {
	struct ocfs2_caching_info *ref_ci;
	struct buffer_head *ref_root_bh;
	struct ocfs2_cached_dealloc_ctxt *dealloc;
};

static int ocfs2_get_xattr_tree_value_root(struct super_block *sb,
					   struct ocfs2_xattr_bucket *bucket,
					   int offset,
					   struct ocfs2_xattr_value_root **xv,
					   struct buffer_head **bh)
{
	int ret, block_off, name_offset;
	struct ocfs2_xattr_header *xh = bucket_xh(bucket);
	struct ocfs2_xattr_entry *xe = &xh->xh_entries[offset];
	void *base;

	ret = ocfs2_xattr_bucket_get_name_value(sb,
						bucket_xh(bucket),
						offset,
						&block_off,
						&name_offset);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	base = bucket_block(bucket, block_off);

	*xv = (struct ocfs2_xattr_value_root *)(base + name_offset +
			 OCFS2_XATTR_SIZE(xe->xe_name_len));

	if (bh)
		*bh = bucket->bu_bhs[block_off];
out:
	return ret;
}

/*
 * For a given xattr bucket, refcount all the entries which
 * have value stored outside.
 */
static int ocfs2_xattr_bucket_value_refcount(struct inode *inode,
					     struct ocfs2_xattr_bucket *bucket,
					     void *para)
{
	int i, ret = 0;
	struct ocfs2_extent_tree et;
	struct ocfs2_xattr_tree_value_refcount_para *ref =
			(struct ocfs2_xattr_tree_value_refcount_para *)para;
	struct ocfs2_xattr_header *xh =
			(struct ocfs2_xattr_header *)bucket->bu_bhs[0]->b_data;
	struct ocfs2_xattr_entry *xe;
	struct ocfs2_xattr_value_buf vb = {
		.vb_access = ocfs2_journal_access,
	};
	struct ocfs2_post_refcount refcount = {
		.credits = bucket->bu_blocks,
		.para = bucket,
		.func = ocfs2_xattr_bucket_post_refcount,
	};
	struct ocfs2_post_refcount *p = NULL;

	/* We only need post_refcount if we support metaecc. */
	if (ocfs2_meta_ecc(OCFS2_SB(inode->i_sb)))
		p = &refcount;

	trace_ocfs2_xattr_bucket_value_refcount(
				(unsigned long long)bucket_blkno(bucket),
				le16_to_cpu(xh->xh_count));
	for (i = 0; i < le16_to_cpu(xh->xh_count); i++) {
		xe = &xh->xh_entries[i];

		if (ocfs2_xattr_is_local(xe))
			continue;

		ret = ocfs2_get_xattr_tree_value_root(inode->i_sb, bucket, i,
						      &vb.vb_xv, &vb.vb_bh);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ocfs2_init_xattr_value_extent_tree(&et,
						   INODE_CACHE(inode), &vb);

		ret = ocfs2_xattr_value_attach_refcount(inode, vb.vb_xv,
							&et, ref->ref_ci,
							ref->ref_root_bh,
							ref->dealloc, p);
		if (ret) {
			mlog_errno(ret);
			break;
		}
	}

	return ret;

}

static int ocfs2_refcount_xattr_tree_rec(struct inode *inode,
				     struct buffer_head *root_bh,
				     u64 blkno, u32 cpos, u32 len, void *para)
{
	return ocfs2_iterate_xattr_buckets(inode, blkno, len,
					   ocfs2_xattr_bucket_value_refcount,
					   para);
}

static int ocfs2_xattr_block_attach_refcount(struct inode *inode,
				struct buffer_head *blk_bh,
				struct ocfs2_caching_info *ref_ci,
				struct buffer_head *ref_root_bh,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret = 0;
	struct ocfs2_xattr_block *xb =
				(struct ocfs2_xattr_block *)blk_bh->b_data;

	if (!(le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED)) {
		struct ocfs2_xattr_header *header = &xb->xb_attrs.xb_header;
		struct ocfs2_xattr_value_buf vb = {
			.vb_bh = blk_bh,
			.vb_access = ocfs2_journal_access_xb,
		};

		ret = ocfs2_xattr_attach_refcount_normal(inode, &vb, header,
							 ref_ci, ref_root_bh,
							 dealloc);
	} else {
		struct ocfs2_xattr_tree_value_refcount_para para = {
			.ref_ci = ref_ci,
			.ref_root_bh = ref_root_bh,
			.dealloc = dealloc,
		};

		ret = ocfs2_iterate_xattr_index_block(inode, blk_bh,
						ocfs2_refcount_xattr_tree_rec,
						&para);
	}

	return ret;
}

int ocfs2_xattr_attach_refcount_tree(struct inode *inode,
				     struct buffer_head *fe_bh,
				     struct ocfs2_caching_info *ref_ci,
				     struct buffer_head *ref_root_bh,
				     struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)fe_bh->b_data;
	struct buffer_head *blk_bh = NULL;

	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL) {
		ret = ocfs2_xattr_inline_attach_refcount(inode, fe_bh,
							 ref_ci, ref_root_bh,
							 dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (!di->i_xattr_loc)
		goto out;

	ret = ocfs2_read_xattr_block(inode, le64_to_cpu(di->i_xattr_loc),
				     &blk_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_xattr_block_attach_refcount(inode, blk_bh, ref_ci,
						ref_root_bh, dealloc);
	if (ret)
		mlog_errno(ret);

	brelse(blk_bh);
out:

	return ret;
}

typedef int (should_xattr_reflinked)(struct ocfs2_xattr_entry *xe);
/*
 * Store the information we need in xattr reflink.
 * old_bh and new_bh are inode bh for the old and new inode.
 */
struct ocfs2_xattr_reflink {
	struct inode *old_inode;
	struct inode *new_inode;
	struct buffer_head *old_bh;
	struct buffer_head *new_bh;
	struct ocfs2_caching_info *ref_ci;
	struct buffer_head *ref_root_bh;
	struct ocfs2_cached_dealloc_ctxt *dealloc;
	should_xattr_reflinked *xattr_reflinked;
};

/*
 * Given a xattr header and xe offset,
 * return the proper xv and the corresponding bh.
 * xattr in inode, block and xattr tree have different implementaions.
 */
typedef int (get_xattr_value_root)(struct super_block *sb,
				   struct buffer_head *bh,
				   struct ocfs2_xattr_header *xh,
				   int offset,
				   struct ocfs2_xattr_value_root **xv,
				   struct buffer_head **ret_bh,
				   void *para);

/*
 * Calculate all the xattr value root metadata stored in this xattr header and
 * credits we need if we create them from the scratch.
 * We use get_xattr_value_root so that all types of xattr container can use it.
 */
static int ocfs2_value_metas_in_xattr_header(struct super_block *sb,
					     struct buffer_head *bh,
					     struct ocfs2_xattr_header *xh,
					     int *metas, int *credits,
					     int *num_recs,
					     get_xattr_value_root *func,
					     void *para)
{
	int i, ret = 0;
	struct ocfs2_xattr_value_root *xv;
	struct ocfs2_xattr_entry *xe;

	for (i = 0; i < le16_to_cpu(xh->xh_count); i++) {
		xe = &xh->xh_entries[i];
		if (ocfs2_xattr_is_local(xe))
			continue;

		ret = func(sb, bh, xh, i, &xv, NULL, para);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		*metas += le16_to_cpu(xv->xr_list.l_tree_depth) *
			  le16_to_cpu(xv->xr_list.l_next_free_rec);

		*credits += ocfs2_calc_extend_credits(sb,
						&def_xv.xv.xr_list);

		/*
		 * If the value is a tree with depth > 1, We don't go deep
		 * to the extent block, so just calculate a maximum record num.
		 */
		if (!xv->xr_list.l_tree_depth)
			*num_recs += le16_to_cpu(xv->xr_list.l_next_free_rec);
		else
			*num_recs += ocfs2_clusters_for_bytes(sb,
							      XATTR_SIZE_MAX);
	}

	return ret;
}

/* Used by xattr inode and block to return the right xv and buffer_head. */
static int ocfs2_get_xattr_value_root(struct super_block *sb,
				      struct buffer_head *bh,
				      struct ocfs2_xattr_header *xh,
				      int offset,
				      struct ocfs2_xattr_value_root **xv,
				      struct buffer_head **ret_bh,
				      void *para)
{
	struct ocfs2_xattr_entry *xe = &xh->xh_entries[offset];

	*xv = (struct ocfs2_xattr_value_root *)((void *)xh +
		le16_to_cpu(xe->xe_name_offset) +
		OCFS2_XATTR_SIZE(xe->xe_name_len));

	if (ret_bh)
		*ret_bh = bh;

	return 0;
}

/*
 * Lock the meta_ac and caculate how much credits we need for reflink xattrs.
 * It is only used for inline xattr and xattr block.
 */
static int ocfs2_reflink_lock_xattr_allocators(struct ocfs2_super *osb,
					struct ocfs2_xattr_header *xh,
					struct buffer_head *ref_root_bh,
					int *credits,
					struct ocfs2_alloc_context **meta_ac)
{
	int ret, meta_add = 0, num_recs = 0;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_root_bh->b_data;

	*credits = 0;

	ret = ocfs2_value_metas_in_xattr_header(osb->sb, NULL, xh,
						&meta_add, credits, &num_recs,
						ocfs2_get_xattr_value_root,
						NULL);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * We need to add/modify num_recs in refcount tree, so just calculate
	 * an approximate number we need for refcount tree change.
	 * Sometimes we need to split the tree, and after split,  half recs
	 * will be moved to the new block, and a new block can only provide
	 * half number of recs. So we multiple new blocks by 2.
	 */
	num_recs = num_recs / ocfs2_refcount_recs_per_rb(osb->sb) * 2;
	meta_add += num_recs;
	*credits += num_recs + num_recs * OCFS2_EXPAND_REFCOUNT_TREE_CREDITS;
	if (le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL)
		*credits += le16_to_cpu(rb->rf_list.l_tree_depth) *
			    le16_to_cpu(rb->rf_list.l_next_free_rec) + 1;
	else
		*credits += 1;

	ret = ocfs2_reserve_new_metadata_blocks(osb, meta_add, meta_ac);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

/*
 * Given a xattr header, reflink all the xattrs in this container.
 * It can be used for inode, block and bucket.
 *
 * NOTE:
 * Before we call this function, the caller has memcpy the xattr in
 * old_xh to the new_xh.
 *
 * If args.xattr_reflinked is set, call it to decide whether the xe should
 * be reflinked or not. If not, remove it from the new xattr header.
 */
static int ocfs2_reflink_xattr_header(handle_t *handle,
				      struct ocfs2_xattr_reflink *args,
				      struct buffer_head *old_bh,
				      struct ocfs2_xattr_header *xh,
				      struct buffer_head *new_bh,
				      struct ocfs2_xattr_header *new_xh,
				      struct ocfs2_xattr_value_buf *vb,
				      struct ocfs2_alloc_context *meta_ac,
				      get_xattr_value_root *func,
				      void *para)
{
	int ret = 0, i, j;
	struct super_block *sb = args->old_inode->i_sb;
	struct buffer_head *value_bh;
	struct ocfs2_xattr_entry *xe, *last;
	struct ocfs2_xattr_value_root *xv, *new_xv;
	struct ocfs2_extent_tree data_et;
	u32 clusters, cpos, p_cluster, num_clusters;
	unsigned int ext_flags = 0;

	trace_ocfs2_reflink_xattr_header((unsigned long long)old_bh->b_blocknr,
					 le16_to_cpu(xh->xh_count));

	last = &new_xh->xh_entries[le16_to_cpu(new_xh->xh_count)];
	for (i = 0, j = 0; i < le16_to_cpu(xh->xh_count); i++, j++) {
		xe = &xh->xh_entries[i];

		if (args->xattr_reflinked && !args->xattr_reflinked(xe)) {
			xe = &new_xh->xh_entries[j];

			le16_add_cpu(&new_xh->xh_count, -1);
			if (new_xh->xh_count) {
				memmove(xe, xe + 1,
					(void *)last - (void *)xe);
				memset(last, 0,
				       sizeof(struct ocfs2_xattr_entry));
			}

			/*
			 * We don't want j to increase in the next round since
			 * it is already moved ahead.
			 */
			j--;
			continue;
		}

		if (ocfs2_xattr_is_local(xe))
			continue;

		ret = func(sb, old_bh, xh, i, &xv, NULL, para);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ret = func(sb, new_bh, new_xh, j, &new_xv, &value_bh, para);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		/*
		 * For the xattr which has l_tree_depth = 0, all the extent
		 * recs have already be copied to the new xh with the
		 * propriate OCFS2_EXT_REFCOUNTED flag we just need to
		 * increase the refount count int the refcount tree.
		 *
		 * For the xattr which has l_tree_depth > 0, we need
		 * to initialize it to the empty default value root,
		 * and then insert the extents one by one.
		 */
		if (xv->xr_list.l_tree_depth) {
			memcpy(new_xv, &def_xv, OCFS2_XATTR_ROOT_SIZE);
			vb->vb_xv = new_xv;
			vb->vb_bh = value_bh;
			ocfs2_init_xattr_value_extent_tree(&data_et,
					INODE_CACHE(args->new_inode), vb);
		}

		clusters = le32_to_cpu(xv->xr_clusters);
		cpos = 0;
		while (cpos < clusters) {
			ret = ocfs2_xattr_get_clusters(args->old_inode,
						       cpos,
						       &p_cluster,
						       &num_clusters,
						       &xv->xr_list,
						       &ext_flags);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			BUG_ON(!p_cluster);

			if (xv->xr_list.l_tree_depth) {
				ret = ocfs2_insert_extent(handle,
						&data_et, cpos,
						ocfs2_clusters_to_blocks(
							args->old_inode->i_sb,
							p_cluster),
						num_clusters, ext_flags,
						meta_ac);
				if (ret) {
					mlog_errno(ret);
					goto out;
				}
			}

			ret = ocfs2_increase_refcount(handle, args->ref_ci,
						      args->ref_root_bh,
						      p_cluster, num_clusters,
						      meta_ac, args->dealloc);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			cpos += num_clusters;
		}
	}

out:
	return ret;
}

static int ocfs2_reflink_xattr_inline(struct ocfs2_xattr_reflink *args)
{
	int ret = 0, credits = 0;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(args->old_inode->i_sb);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)args->old_bh->b_data;
	int inline_size = le16_to_cpu(di->i_xattr_inline_size);
	int header_off = osb->sb->s_blocksize - inline_size;
	struct ocfs2_xattr_header *xh = (struct ocfs2_xattr_header *)
					(args->old_bh->b_data + header_off);
	struct ocfs2_xattr_header *new_xh = (struct ocfs2_xattr_header *)
					(args->new_bh->b_data + header_off);
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct ocfs2_inode_info *new_oi;
	struct ocfs2_dinode *new_di;
	struct ocfs2_xattr_value_buf vb = {
		.vb_bh = args->new_bh,
		.vb_access = ocfs2_journal_access_di,
	};

	ret = ocfs2_reflink_lock_xattr_allocators(osb, xh, args->ref_root_bh,
						  &credits, &meta_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(args->new_inode),
				      args->new_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	memcpy(args->new_bh->b_data + header_off,
	       args->old_bh->b_data + header_off, inline_size);

	new_di = (struct ocfs2_dinode *)args->new_bh->b_data;
	new_di->i_xattr_inline_size = cpu_to_le16(inline_size);

	ret = ocfs2_reflink_xattr_header(handle, args, args->old_bh, xh,
					 args->new_bh, new_xh, &vb, meta_ac,
					 ocfs2_get_xattr_value_root, NULL);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	new_oi = OCFS2_I(args->new_inode);
	/*
	 * Adjust extent record count to reserve space for extended attribute.
	 * Inline data count had been adjusted in ocfs2_duplicate_inline_data().
	 */
	if (!(new_oi->ip_dyn_features & OCFS2_INLINE_DATA_FL) &&
	    !(ocfs2_inode_is_fast_symlink(args->new_inode))) {
		struct ocfs2_extent_list *el = &new_di->id2.i_list;
		le16_add_cpu(&el->l_count, -(inline_size /
					sizeof(struct ocfs2_extent_rec)));
	}
	spin_lock(&new_oi->ip_lock);
	new_oi->ip_dyn_features |= OCFS2_HAS_XATTR_FL | OCFS2_INLINE_XATTR_FL;
	new_di->i_dyn_features = cpu_to_le16(new_oi->ip_dyn_features);
	spin_unlock(&new_oi->ip_lock);

	ocfs2_journal_dirty(handle, args->new_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);

out:
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);
	return ret;
}

static int ocfs2_create_empty_xattr_block(struct inode *inode,
					  struct buffer_head *fe_bh,
					  struct buffer_head **ret_bh,
					  int indexed)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_xattr_set_ctxt ctxt;

	memset(&ctxt, 0, sizeof(ctxt));
	ret = ocfs2_reserve_new_metadata_blocks(osb, 1, &ctxt.meta_ac);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	ctxt.handle = ocfs2_start_trans(osb, OCFS2_XATTR_BLOCK_CREATE_CREDITS);
	if (IS_ERR(ctxt.handle)) {
		ret = PTR_ERR(ctxt.handle);
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_create_empty_xattr_block(
				(unsigned long long)fe_bh->b_blocknr, indexed);
	ret = ocfs2_create_xattr_block(inode, fe_bh, &ctxt, indexed,
				       ret_bh);
	if (ret)
		mlog_errno(ret);

	ocfs2_commit_trans(osb, ctxt.handle);
out:
	ocfs2_free_alloc_context(ctxt.meta_ac);
	return ret;
}

static int ocfs2_reflink_xattr_block(struct ocfs2_xattr_reflink *args,
				     struct buffer_head *blk_bh,
				     struct buffer_head *new_blk_bh)
{
	int ret = 0, credits = 0;
	handle_t *handle;
	struct ocfs2_inode_info *new_oi = OCFS2_I(args->new_inode);
	struct ocfs2_dinode *new_di;
	struct ocfs2_super *osb = OCFS2_SB(args->new_inode->i_sb);
	int header_off = offsetof(struct ocfs2_xattr_block, xb_attrs.xb_header);
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)blk_bh->b_data;
	struct ocfs2_xattr_header *xh = &xb->xb_attrs.xb_header;
	struct ocfs2_xattr_block *new_xb =
			(struct ocfs2_xattr_block *)new_blk_bh->b_data;
	struct ocfs2_xattr_header *new_xh = &new_xb->xb_attrs.xb_header;
	struct ocfs2_alloc_context *meta_ac;
	struct ocfs2_xattr_value_buf vb = {
		.vb_bh = new_blk_bh,
		.vb_access = ocfs2_journal_access_xb,
	};

	ret = ocfs2_reflink_lock_xattr_allocators(osb, xh, args->ref_root_bh,
						  &credits, &meta_ac);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	/* One more credits in case we need to add xattr flags in new inode. */
	handle = ocfs2_start_trans(osb, credits + 1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	if (!(new_oi->ip_dyn_features & OCFS2_HAS_XATTR_FL)) {
		ret = ocfs2_journal_access_di(handle,
					      INODE_CACHE(args->new_inode),
					      args->new_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
	}

	ret = ocfs2_journal_access_xb(handle, INODE_CACHE(args->new_inode),
				      new_blk_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	memcpy(new_blk_bh->b_data + header_off, blk_bh->b_data + header_off,
	       osb->sb->s_blocksize - header_off);

	ret = ocfs2_reflink_xattr_header(handle, args, blk_bh, xh,
					 new_blk_bh, new_xh, &vb, meta_ac,
					 ocfs2_get_xattr_value_root, NULL);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ocfs2_journal_dirty(handle, new_blk_bh);

	if (!(new_oi->ip_dyn_features & OCFS2_HAS_XATTR_FL)) {
		new_di = (struct ocfs2_dinode *)args->new_bh->b_data;
		spin_lock(&new_oi->ip_lock);
		new_oi->ip_dyn_features |= OCFS2_HAS_XATTR_FL;
		new_di->i_dyn_features = cpu_to_le16(new_oi->ip_dyn_features);
		spin_unlock(&new_oi->ip_lock);

		ocfs2_journal_dirty(handle, args->new_bh);
	}

out_commit:
	ocfs2_commit_trans(osb, handle);

out:
	ocfs2_free_alloc_context(meta_ac);
	return ret;
}

struct ocfs2_reflink_xattr_tree_args {
	struct ocfs2_xattr_reflink *reflink;
	struct buffer_head *old_blk_bh;
	struct buffer_head *new_blk_bh;
	struct ocfs2_xattr_bucket *old_bucket;
	struct ocfs2_xattr_bucket *new_bucket;
};

/*
 * NOTE:
 * We have to handle the case that both old bucket and new bucket
 * will call this function to get the right ret_bh.
 * So The caller must give us the right bh.
 */
static int ocfs2_get_reflink_xattr_value_root(struct super_block *sb,
					struct buffer_head *bh,
					struct ocfs2_xattr_header *xh,
					int offset,
					struct ocfs2_xattr_value_root **xv,
					struct buffer_head **ret_bh,
					void *para)
{
	struct ocfs2_reflink_xattr_tree_args *args =
			(struct ocfs2_reflink_xattr_tree_args *)para;
	struct ocfs2_xattr_bucket *bucket;

	if (bh == args->old_bucket->bu_bhs[0])
		bucket = args->old_bucket;
	else
		bucket = args->new_bucket;

	return ocfs2_get_xattr_tree_value_root(sb, bucket, offset,
					       xv, ret_bh);
}

struct ocfs2_value_tree_metas {
	int num_metas;
	int credits;
	int num_recs;
};

static int ocfs2_value_tree_metas_in_bucket(struct super_block *sb,
					struct buffer_head *bh,
					struct ocfs2_xattr_header *xh,
					int offset,
					struct ocfs2_xattr_value_root **xv,
					struct buffer_head **ret_bh,
					void *para)
{
	struct ocfs2_xattr_bucket *bucket =
				(struct ocfs2_xattr_bucket *)para;

	return ocfs2_get_xattr_tree_value_root(sb, bucket, offset,
					       xv, ret_bh);
}

static int ocfs2_calc_value_tree_metas(struct inode *inode,
				      struct ocfs2_xattr_bucket *bucket,
				      void *para)
{
	struct ocfs2_value_tree_metas *metas =
			(struct ocfs2_value_tree_metas *)para;
	struct ocfs2_xattr_header *xh =
			(struct ocfs2_xattr_header *)bucket->bu_bhs[0]->b_data;

	/* Add the credits for this bucket first. */
	metas->credits += bucket->bu_blocks;
	return ocfs2_value_metas_in_xattr_header(inode->i_sb, bucket->bu_bhs[0],
					xh, &metas->num_metas,
					&metas->credits, &metas->num_recs,
					ocfs2_value_tree_metas_in_bucket,
					bucket);
}

/*
 * Given a xattr extent rec starting from blkno and having len clusters,
 * iterate all the buckets calculate how much metadata we need for reflinking
 * all the ocfs2_xattr_value_root and lock the allocators accordingly.
 */
static int ocfs2_lock_reflink_xattr_rec_allocators(
				struct ocfs2_reflink_xattr_tree_args *args,
				struct ocfs2_extent_tree *xt_et,
				u64 blkno, u32 len, int *credits,
				struct ocfs2_alloc_context **meta_ac,
				struct ocfs2_alloc_context **data_ac)
{
	int ret, num_free_extents;
	struct ocfs2_value_tree_metas metas;
	struct ocfs2_super *osb = OCFS2_SB(args->reflink->old_inode->i_sb);
	struct ocfs2_refcount_block *rb;

	memset(&metas, 0, sizeof(metas));

	ret = ocfs2_iterate_xattr_buckets(args->reflink->old_inode, blkno, len,
					  ocfs2_calc_value_tree_metas, &metas);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	*credits = metas.credits;

	/*
	 * Calculate we need for refcount tree change.
	 *
	 * We need to add/modify num_recs in refcount tree, so just calculate
	 * an approximate number we need for refcount tree change.
	 * Sometimes we need to split the tree, and after split,  half recs
	 * will be moved to the new block, and a new block can only provide
	 * half number of recs. So we multiple new blocks by 2.
	 * In the end, we have to add credits for modifying the already
	 * existed refcount block.
	 */
	rb = (struct ocfs2_refcount_block *)args->reflink->ref_root_bh->b_data;
	metas.num_recs =
		(metas.num_recs + ocfs2_refcount_recs_per_rb(osb->sb) - 1) /
		 ocfs2_refcount_recs_per_rb(osb->sb) * 2;
	metas.num_metas += metas.num_recs;
	*credits += metas.num_recs +
		    metas.num_recs * OCFS2_EXPAND_REFCOUNT_TREE_CREDITS;
	if (le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL)
		*credits += le16_to_cpu(rb->rf_list.l_tree_depth) *
			    le16_to_cpu(rb->rf_list.l_next_free_rec) + 1;
	else
		*credits += 1;

	/* count in the xattr tree change. */
	num_free_extents = ocfs2_num_free_extents(xt_et);
	if (num_free_extents < 0) {
		ret = num_free_extents;
		mlog_errno(ret);
		goto out;
	}

	if (num_free_extents < len)
		metas.num_metas += ocfs2_extend_meta_needed(xt_et->et_root_el);

	*credits += ocfs2_calc_extend_credits(osb->sb,
					      xt_et->et_root_el);

	if (metas.num_metas) {
		ret = ocfs2_reserve_new_metadata_blocks(osb, metas.num_metas,
							meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (len) {
		ret = ocfs2_reserve_clusters(osb, len, data_ac);
		if (ret)
			mlog_errno(ret);
	}
out:
	if (ret) {
		if (*meta_ac) {
			ocfs2_free_alloc_context(*meta_ac);
			*meta_ac = NULL;
		}
	}

	return ret;
}

static int ocfs2_reflink_xattr_bucket(handle_t *handle,
				u64 blkno, u64 new_blkno, u32 clusters,
				u32 *cpos, int num_buckets,
				struct ocfs2_alloc_context *meta_ac,
				struct ocfs2_alloc_context *data_ac,
				struct ocfs2_reflink_xattr_tree_args *args)
{
	int i, j, ret = 0;
	struct super_block *sb = args->reflink->old_inode->i_sb;
	int bpb = args->old_bucket->bu_blocks;
	struct ocfs2_xattr_value_buf vb = {
		.vb_access = ocfs2_journal_access,
	};

	for (i = 0; i < num_buckets; i++, blkno += bpb, new_blkno += bpb) {
		ret = ocfs2_read_xattr_bucket(args->old_bucket, blkno);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_init_xattr_bucket(args->new_bucket, new_blkno, 1);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_xattr_bucket_journal_access(handle,
						args->new_bucket,
						OCFS2_JOURNAL_ACCESS_CREATE);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		for (j = 0; j < bpb; j++)
			memcpy(bucket_block(args->new_bucket, j),
			       bucket_block(args->old_bucket, j),
			       sb->s_blocksize);

		/*
		 * Record the start cpos so that we can use it to initialize
		 * our xattr tree we also set the xh_num_bucket for the new
		 * bucket.
		 */
		if (i == 0) {
			*cpos = le32_to_cpu(bucket_xh(args->new_bucket)->
					    xh_entries[0].xe_name_hash);
			bucket_xh(args->new_bucket)->xh_num_buckets =
				cpu_to_le16(num_buckets);
		}

		ocfs2_xattr_bucket_journal_dirty(handle, args->new_bucket);

		ret = ocfs2_reflink_xattr_header(handle, args->reflink,
					args->old_bucket->bu_bhs[0],
					bucket_xh(args->old_bucket),
					args->new_bucket->bu_bhs[0],
					bucket_xh(args->new_bucket),
					&vb, meta_ac,
					ocfs2_get_reflink_xattr_value_root,
					args);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		/*
		 * Re-access and dirty the bucket to calculate metaecc.
		 * Because we may extend the transaction in reflink_xattr_header
		 * which will let the already accessed block gone.
		 */
		ret = ocfs2_xattr_bucket_journal_access(handle,
						args->new_bucket,
						OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ocfs2_xattr_bucket_journal_dirty(handle, args->new_bucket);

		ocfs2_xattr_bucket_relse(args->old_bucket);
		ocfs2_xattr_bucket_relse(args->new_bucket);
	}

	ocfs2_xattr_bucket_relse(args->old_bucket);
	ocfs2_xattr_bucket_relse(args->new_bucket);
	return ret;
}

static int ocfs2_reflink_xattr_buckets(handle_t *handle,
				struct inode *inode,
				struct ocfs2_reflink_xattr_tree_args *args,
				struct ocfs2_extent_tree *et,
				struct ocfs2_alloc_context *meta_ac,
				struct ocfs2_alloc_context *data_ac,
				u64 blkno, u32 cpos, u32 len)
{
	int ret, first_inserted = 0;
	u32 p_cluster, num_clusters, reflink_cpos = 0;
	u64 new_blkno;
	unsigned int num_buckets, reflink_buckets;
	unsigned int bpc =
		ocfs2_xattr_buckets_per_cluster(OCFS2_SB(inode->i_sb));

	ret = ocfs2_read_xattr_bucket(args->old_bucket, blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	num_buckets = le16_to_cpu(bucket_xh(args->old_bucket)->xh_num_buckets);
	ocfs2_xattr_bucket_relse(args->old_bucket);

	while (len && num_buckets) {
		ret = ocfs2_claim_clusters(handle, data_ac,
					   1, &p_cluster, &num_clusters);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		new_blkno = ocfs2_clusters_to_blocks(inode->i_sb, p_cluster);
		reflink_buckets = min(num_buckets, bpc * num_clusters);

		ret = ocfs2_reflink_xattr_bucket(handle, blkno,
						 new_blkno, num_clusters,
						 &reflink_cpos, reflink_buckets,
						 meta_ac, data_ac, args);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * For the 1st allocated cluster, we make it use the same cpos
		 * so that the xattr tree looks the same as the original one
		 * in the most case.
		 */
		if (!first_inserted) {
			reflink_cpos = cpos;
			first_inserted = 1;
		}
		ret = ocfs2_insert_extent(handle, et, reflink_cpos, new_blkno,
					  num_clusters, 0, meta_ac);
		if (ret)
			mlog_errno(ret);

		trace_ocfs2_reflink_xattr_buckets((unsigned long long)new_blkno,
						  num_clusters, reflink_cpos);

		len -= num_clusters;
		blkno += ocfs2_clusters_to_blocks(inode->i_sb, num_clusters);
		num_buckets -= reflink_buckets;
	}
out:
	return ret;
}

/*
 * Create the same xattr extent record in the new inode's xattr tree.
 */
static int ocfs2_reflink_xattr_rec(struct inode *inode,
				   struct buffer_head *root_bh,
				   u64 blkno,
				   u32 cpos,
				   u32 len,
				   void *para)
{
	int ret, credits = 0;
	handle_t *handle;
	struct ocfs2_reflink_xattr_tree_args *args =
			(struct ocfs2_reflink_xattr_tree_args *)para;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_extent_tree et;

	trace_ocfs2_reflink_xattr_rec((unsigned long long)blkno, len);

	ocfs2_init_xattr_tree_extent_tree(&et,
					  INODE_CACHE(args->reflink->new_inode),
					  args->new_blk_bh);

	ret = ocfs2_lock_reflink_xattr_rec_allocators(args, &et, blkno,
						      len, &credits,
						      &meta_ac, &data_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_reflink_xattr_buckets(handle, inode, args, &et,
					  meta_ac, data_ac,
					  blkno, cpos, len);
	if (ret)
		mlog_errno(ret);

	ocfs2_commit_trans(osb, handle);

out:
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	return ret;
}

/*
 * Create reflinked xattr buckets.
 * We will add bucket one by one, and refcount all the xattrs in the bucket
 * if they are stored outside.
 */
static int ocfs2_reflink_xattr_tree(struct ocfs2_xattr_reflink *args,
				    struct buffer_head *blk_bh,
				    struct buffer_head *new_blk_bh)
{
	int ret;
	struct ocfs2_reflink_xattr_tree_args para;

	memset(&para, 0, sizeof(para));
	para.reflink = args;
	para.old_blk_bh = blk_bh;
	para.new_blk_bh = new_blk_bh;

	para.old_bucket = ocfs2_xattr_bucket_new(args->old_inode);
	if (!para.old_bucket) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	para.new_bucket = ocfs2_xattr_bucket_new(args->new_inode);
	if (!para.new_bucket) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_iterate_xattr_index_block(args->old_inode, blk_bh,
					      ocfs2_reflink_xattr_rec,
					      &para);
	if (ret)
		mlog_errno(ret);

out:
	ocfs2_xattr_bucket_free(para.old_bucket);
	ocfs2_xattr_bucket_free(para.new_bucket);
	return ret;
}

static int ocfs2_reflink_xattr_in_block(struct ocfs2_xattr_reflink *args,
					struct buffer_head *blk_bh)
{
	int ret, indexed = 0;
	struct buffer_head *new_blk_bh = NULL;
	struct ocfs2_xattr_block *xb =
			(struct ocfs2_xattr_block *)blk_bh->b_data;


	if (le16_to_cpu(xb->xb_flags) & OCFS2_XATTR_INDEXED)
		indexed = 1;

	ret = ocfs2_create_empty_xattr_block(args->new_inode, args->new_bh,
					     &new_blk_bh, indexed);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (!indexed)
		ret = ocfs2_reflink_xattr_block(args, blk_bh, new_blk_bh);
	else
		ret = ocfs2_reflink_xattr_tree(args, blk_bh, new_blk_bh);
	if (ret)
		mlog_errno(ret);

out:
	brelse(new_blk_bh);
	return ret;
}

static int ocfs2_reflink_xattr_no_security(struct ocfs2_xattr_entry *xe)
{
	int type = ocfs2_xattr_get_type(xe);

	return type != OCFS2_XATTR_INDEX_SECURITY &&
	       type != OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS &&
	       type != OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT;
}

int ocfs2_reflink_xattrs(struct inode *old_inode,
			 struct buffer_head *old_bh,
			 struct inode *new_inode,
			 struct buffer_head *new_bh,
			 bool preserve_security)
{
	int ret;
	struct ocfs2_xattr_reflink args;
	struct ocfs2_inode_info *oi = OCFS2_I(old_inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)old_bh->b_data;
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_refcount_tree *ref_tree;
	struct buffer_head *ref_root_bh = NULL;

	ret = ocfs2_lock_refcount_tree(OCFS2_SB(old_inode->i_sb),
				       le64_to_cpu(di->i_refcount_loc),
				       1, &ref_tree, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_init_dealloc_ctxt(&dealloc);

	args.old_inode = old_inode;
	args.new_inode = new_inode;
	args.old_bh = old_bh;
	args.new_bh = new_bh;
	args.ref_ci = &ref_tree->rf_ci;
	args.ref_root_bh = ref_root_bh;
	args.dealloc = &dealloc;
	if (preserve_security)
		args.xattr_reflinked = NULL;
	else
		args.xattr_reflinked = ocfs2_reflink_xattr_no_security;

	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL) {
		ret = ocfs2_reflink_xattr_inline(&args);
		if (ret) {
			mlog_errno(ret);
			goto out_unlock;
		}
	}

	if (!di->i_xattr_loc)
		goto out_unlock;

	ret = ocfs2_read_xattr_block(old_inode, le64_to_cpu(di->i_xattr_loc),
				     &blk_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_unlock;
	}

	ret = ocfs2_reflink_xattr_in_block(&args, blk_bh);
	if (ret)
		mlog_errno(ret);

	brelse(blk_bh);

out_unlock:
	ocfs2_unlock_refcount_tree(OCFS2_SB(old_inode->i_sb),
				   ref_tree, 1);
	brelse(ref_root_bh);

	if (ocfs2_dealloc_has_cluster(&dealloc)) {
		ocfs2_schedule_truncate_log_flush(OCFS2_SB(old_inode->i_sb), 1);
		ocfs2_run_deallocs(OCFS2_SB(old_inode->i_sb), &dealloc);
	}

out:
	return ret;
}

/*
 * Initialize security and acl for a already created inode.
 * Used for reflink a non-preserve-security file.
 *
 * It uses common api like ocfs2_xattr_set, so the caller
 * must not hold any lock expect i_mutex.
 */
int ocfs2_init_security_and_acl(struct inode *dir,
				struct inode *inode,
				const struct qstr *qstr)
{
	int ret = 0;
	struct buffer_head *dir_bh = NULL;

	ret = ocfs2_init_security_get(inode, dir, qstr, NULL);
	if (ret) {
		mlog_errno(ret);
		goto leave;
	}

	ret = ocfs2_inode_lock(dir, &dir_bh, 0);
	if (ret) {
		mlog_errno(ret);
		goto leave;
	}
	ret = ocfs2_init_acl(NULL, inode, dir, NULL, dir_bh, NULL, NULL);
	if (ret)
		mlog_errno(ret);

	ocfs2_inode_unlock(dir, 0);
	brelse(dir_bh);
leave:
	return ret;
}

/*
 * 'security' attributes support
 */
static int ocfs2_xattr_security_get(const struct xattr_handler *handler,
				    struct dentry *unused, struct inode *inode,
				    const char *name, void *buffer, size_t size)
{
	return ocfs2_xattr_get(inode, OCFS2_XATTR_INDEX_SECURITY,
			       name, buffer, size);
}

static int ocfs2_xattr_security_set(const struct xattr_handler *handler,
				    struct dentry *unused, struct inode *inode,
				    const char *name, const void *value,
				    size_t size, int flags)
{
	return ocfs2_xattr_set(inode, OCFS2_XATTR_INDEX_SECURITY,
			       name, value, size, flags);
}

static int ocfs2_initxattrs(struct inode *inode, const struct xattr *xattr_array,
		     void *fs_info)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = ocfs2_xattr_set(inode, OCFS2_XATTR_INDEX_SECURITY,
				      xattr->name, xattr->value,
				      xattr->value_len, XATTR_CREATE);
		if (err)
			break;
	}
	return err;
}

int ocfs2_init_security_get(struct inode *inode,
			    struct inode *dir,
			    const struct qstr *qstr,
			    struct ocfs2_security_xattr_info *si)
{
	/* check whether ocfs2 support feature xattr */
	if (!ocfs2_supports_xattr(OCFS2_SB(dir->i_sb)))
		return -EOPNOTSUPP;
	if (si)
		return security_old_inode_init_security(inode, dir, qstr,
							&si->name, &si->value,
							&si->value_len);

	return security_inode_init_security(inode, dir, qstr,
					    &ocfs2_initxattrs, NULL);
}

int ocfs2_init_security_set(handle_t *handle,
			    struct inode *inode,
			    struct buffer_head *di_bh,
			    struct ocfs2_security_xattr_info *si,
			    struct ocfs2_alloc_context *xattr_ac,
			    struct ocfs2_alloc_context *data_ac)
{
	return ocfs2_xattr_set_handle(handle, inode, di_bh,
				     OCFS2_XATTR_INDEX_SECURITY,
				     si->name, si->value, si->value_len, 0,
				     xattr_ac, data_ac);
}

const struct xattr_handler ocfs2_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= ocfs2_xattr_security_get,
	.set	= ocfs2_xattr_security_set,
};

/*
 * 'trusted' attributes support
 */
static int ocfs2_xattr_trusted_get(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *name, void *buffer, size_t size)
{
	return ocfs2_xattr_get(inode, OCFS2_XATTR_INDEX_TRUSTED,
			       name, buffer, size);
}

static int ocfs2_xattr_trusted_set(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *name, const void *value,
				   size_t size, int flags)
{
	return ocfs2_xattr_set(inode, OCFS2_XATTR_INDEX_TRUSTED,
			       name, value, size, flags);
}

const struct xattr_handler ocfs2_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.get	= ocfs2_xattr_trusted_get,
	.set	= ocfs2_xattr_trusted_set,
};

/*
 * 'user' attributes support
 */
static int ocfs2_xattr_user_get(const struct xattr_handler *handler,
				struct dentry *unused, struct inode *inode,
				const char *name, void *buffer, size_t size)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (osb->s_mount_opt & OCFS2_MOUNT_NOUSERXATTR)
		return -EOPNOTSUPP;
	return ocfs2_xattr_get(inode, OCFS2_XATTR_INDEX_USER, name,
			       buffer, size);
}

static int ocfs2_xattr_user_set(const struct xattr_handler *handler,
				struct dentry *unused, struct inode *inode,
				const char *name, const void *value,
				size_t size, int flags)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (osb->s_mount_opt & OCFS2_MOUNT_NOUSERXATTR)
		return -EOPNOTSUPP;

	return ocfs2_xattr_set(inode, OCFS2_XATTR_INDEX_USER,
			       name, value, size, flags);
}

const struct xattr_handler ocfs2_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= ocfs2_xattr_user_get,
	.set	= ocfs2_xattr_user_set,
};
