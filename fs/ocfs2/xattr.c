/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.c
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * CREDITS:
 * Lots of code in this file is taken from ext3.
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

#define MLOG_MASK_PREFIX ML_XATTR
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
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
#include "xattr.h"


struct ocfs2_xattr_def_value_root {
	struct ocfs2_xattr_value_root	xv;
	struct ocfs2_extent_rec		er;
};

#define OCFS2_XATTR_ROOT_SIZE	(sizeof(struct ocfs2_xattr_def_value_root))
#define OCFS2_XATTR_INLINE_SIZE	80

static struct ocfs2_xattr_def_value_root def_xv = {
	.xv.xr_list.l_count = cpu_to_le16(1),
};

struct xattr_handler *ocfs2_xattr_handlers[] = {
	&ocfs2_xattr_user_handler,
	&ocfs2_xattr_trusted_handler,
	NULL
};

static struct xattr_handler *ocfs2_xattr_handler_map[] = {
	[OCFS2_XATTR_INDEX_USER]	= &ocfs2_xattr_user_handler,
	[OCFS2_XATTR_INDEX_TRUSTED]	= &ocfs2_xattr_trusted_handler,
};

struct ocfs2_xattr_info {
	int name_index;
	const char *name;
	const void *value;
	size_t value_len;
};

struct ocfs2_xattr_search {
	struct buffer_head *inode_bh;
	/*
	 * xattr_bh point to the block buffer head which has extended attribute
	 * when extended attribute in inode, xattr_bh is equal to inode_bh.
	 */
	struct buffer_head *xattr_bh;
	struct ocfs2_xattr_header *header;
	void *base;
	void *end;
	struct ocfs2_xattr_entry *here;
	int not_found;
};

static inline struct xattr_handler *ocfs2_xattr_handler(int name_index)
{
	struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < OCFS2_XATTR_MAX)
		handler = ocfs2_xattr_handler_map[name_index];

	return handler;
}

static inline u32 ocfs2_xattr_name_hash(struct inode *inode,
					char *prefix,
					int prefix_len,
					char *name,
					int name_len)
{
	/* Get hash value of uuid from super block */
	u32 hash = OCFS2_SB(inode->i_sb)->uuid_hash;
	int i;

	/* hash extended attribute prefix */
	for (i = 0; i < prefix_len; i++) {
		hash = (hash << OCFS2_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - OCFS2_HASH_SHIFT)) ^
		       *prefix++;
	}
	/* hash extended attribute name */
	for (i = 0; i < name_len; i++) {
		hash = (hash << OCFS2_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - OCFS2_HASH_SHIFT)) ^
		       *name++;
	}

	return hash;
}

/*
 * ocfs2_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
static void ocfs2_xattr_hash_entry(struct inode *inode,
				   struct ocfs2_xattr_header *header,
				   struct ocfs2_xattr_entry *entry)
{
	u32 hash = 0;
	struct xattr_handler *handler =
			ocfs2_xattr_handler(ocfs2_xattr_get_type(entry));
	char *prefix = handler->prefix;
	char *name = (char *)header + le16_to_cpu(entry->xe_name_offset);
	int prefix_len = strlen(handler->prefix);

	hash = ocfs2_xattr_name_hash(inode, prefix, prefix_len, name,
				     entry->xe_name_len);
	entry->xe_name_hash = cpu_to_le32(hash);

	return;
}

static int ocfs2_xattr_extend_allocation(struct inode *inode,
					 u32 clusters_to_add,
					 struct buffer_head *xattr_bh,
					 struct ocfs2_xattr_value_root *xv)
{
	int status = 0;
	int restart_func = 0;
	int credits = 0;
	handle_t *handle = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	enum ocfs2_alloc_restarted why;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_extent_list *root_el = &xv->xr_list;
	u32 prev_clusters, logical_start = le32_to_cpu(xv->xr_clusters);

	mlog(0, "(clusters_to_add for xattr= %u)\n", clusters_to_add);

restart_all:

	status = ocfs2_lock_allocators(inode, xattr_bh, root_el,
				       clusters_to_add, 0, &data_ac,
				       &meta_ac, OCFS2_XATTR_VALUE_EXTENT, xv);
	if (status) {
		mlog_errno(status);
		goto leave;
	}

	credits = ocfs2_calc_extend_credits(osb->sb, root_el, clusters_to_add);
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

restarted_transaction:
	status = ocfs2_journal_access(handle, inode, xattr_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	prev_clusters = le32_to_cpu(xv->xr_clusters);
	status = ocfs2_add_clusters_in_btree(osb,
					     inode,
					     &logical_start,
					     clusters_to_add,
					     0,
					     xattr_bh,
					     root_el,
					     handle,
					     data_ac,
					     meta_ac,
					     &why,
					     OCFS2_XATTR_VALUE_EXTENT,
					     xv);
	if ((status < 0) && (status != -EAGAIN)) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	status = ocfs2_journal_dirty(handle, xattr_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	clusters_to_add -= le32_to_cpu(xv->xr_clusters) - prev_clusters;

	if (why != RESTART_NONE && clusters_to_add) {
		if (why == RESTART_META) {
			mlog(0, "restarting function.\n");
			restart_func = 1;
		} else {
			BUG_ON(why != RESTART_TRANS);

			mlog(0, "restarting transaction.\n");
			/* TODO: This can be more intelligent. */
			credits = ocfs2_calc_extend_credits(osb->sb,
							    root_el,
							    clusters_to_add);
			status = ocfs2_extend_trans(handle, credits);
			if (status < 0) {
				/* handle still has to be committed at
				 * this point. */
				status = -ENOMEM;
				mlog_errno(status);
				goto leave;
			}
			goto restarted_transaction;
		}
	}

leave:
	if (handle) {
		ocfs2_commit_trans(osb, handle);
		handle = NULL;
	}
	if (data_ac) {
		ocfs2_free_alloc_context(data_ac);
		data_ac = NULL;
	}
	if (meta_ac) {
		ocfs2_free_alloc_context(meta_ac);
		meta_ac = NULL;
	}
	if ((!status) && restart_func) {
		restart_func = 0;
		goto restart_all;
	}

	return status;
}

static int __ocfs2_remove_xattr_range(struct inode *inode,
				      struct buffer_head *root_bh,
				      struct ocfs2_xattr_value_root *xv,
				      u32 cpos, u32 phys_cpos, u32 len,
				      struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;
	u64 phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	handle_t *handle;
	struct ocfs2_alloc_context *meta_ac = NULL;

	ret = ocfs2_lock_allocators(inode, root_bh, &xv->xr_list,
				    0, 1, NULL, &meta_ac,
				    OCFS2_XATTR_VALUE_EXTENT, xv);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	mutex_lock(&tl_inode->i_mutex);

	if (ocfs2_truncate_log_needs_flush(osb)) {
		ret = __ocfs2_flush_truncate_log(osb);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	handle = ocfs2_start_trans(osb, OCFS2_REMOVE_EXTENT_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access(handle, inode, root_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_remove_extent(inode, root_bh, cpos, len, handle, meta_ac,
				  dealloc, OCFS2_XATTR_VALUE_EXTENT, xv);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	le32_add_cpu(&xv->xr_clusters, -len);

	ret = ocfs2_journal_dirty(handle, root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_truncate_log_append(osb, handle, phys_blkno, len);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	mutex_unlock(&tl_inode->i_mutex);

	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	return ret;
}

static int ocfs2_xattr_shrink_size(struct inode *inode,
				   u32 old_clusters,
				   u32 new_clusters,
				   struct buffer_head *root_bh,
				   struct ocfs2_xattr_value_root *xv)
{
	int ret = 0;
	u32 trunc_len, cpos, phys_cpos, alloc_size;
	u64 block;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_cached_dealloc_ctxt dealloc;

	ocfs2_init_dealloc_ctxt(&dealloc);

	if (old_clusters <= new_clusters)
		return 0;

	cpos = new_clusters;
	trunc_len = old_clusters - new_clusters;
	while (trunc_len) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &phys_cpos,
					       &alloc_size, &xv->xr_list);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (alloc_size > trunc_len)
			alloc_size = trunc_len;

		ret = __ocfs2_remove_xattr_range(inode, root_bh, xv, cpos,
						 phys_cpos, alloc_size,
						 &dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		block = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
		ocfs2_remove_xattr_clusters_from_cache(inode, block,
						       alloc_size);
		cpos += alloc_size;
		trunc_len -= alloc_size;
	}

out:
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &dealloc);

	return ret;
}

static int ocfs2_xattr_value_truncate(struct inode *inode,
				      struct buffer_head *root_bh,
				      struct ocfs2_xattr_value_root *xv,
				      int len)
{
	int ret;
	u32 new_clusters = ocfs2_clusters_for_bytes(inode->i_sb, len);
	u32 old_clusters = le32_to_cpu(xv->xr_clusters);

	if (new_clusters == old_clusters)
		return 0;

	if (new_clusters > old_clusters)
		ret = ocfs2_xattr_extend_allocation(inode,
						    new_clusters - old_clusters,
						    root_bh, xv);
	else
		ret = ocfs2_xattr_shrink_size(inode,
					      old_clusters, new_clusters,
					      root_bh, xv);

	return ret;
}

static int ocfs2_xattr_list_entries(struct inode *inode,
				    struct ocfs2_xattr_header *header,
				    char *buffer, size_t buffer_size)
{
	size_t rest = buffer_size;
	int i;

	for (i = 0 ; i < le16_to_cpu(header->xh_count); i++) {
		struct ocfs2_xattr_entry *entry = &header->xh_entries[i];
		struct xattr_handler *handler =
			ocfs2_xattr_handler(ocfs2_xattr_get_type(entry));

		if (handler) {
			size_t size = handler->list(inode, buffer, rest,
					((char *)header +
					le16_to_cpu(entry->xe_name_offset)),
					entry->xe_name_len);
			if (buffer) {
				if (size > rest)
					return -ERANGE;
				buffer += size;
			}
			rest -= size;
		}
	}

	return buffer_size - rest;
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
	struct ocfs2_xattr_header *header = NULL;
	int ret = 0;

	if (!di->i_xattr_loc)
		return ret;

	ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
			       le64_to_cpu(di->i_xattr_loc),
			       &blk_bh, OCFS2_BH_CACHED, inode);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	/*Verify the signature of xattr block*/
	if (memcmp((void *)blk_bh->b_data, OCFS2_XATTR_BLOCK_SIGNATURE,
		   strlen(OCFS2_XATTR_BLOCK_SIGNATURE))) {
		ret = -EFAULT;
		goto cleanup;
	}

	header = &((struct ocfs2_xattr_block *)blk_bh->b_data)->
		 xb_attrs.xb_header;

	ret = ocfs2_xattr_list_entries(inode, header, buffer, buffer_size);
cleanup:
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
	struct ocfs2_inode_info *oi = OCFS2_I(dentry->d_inode);

	if (!(oi->ip_dyn_features & OCFS2_HAS_XATTR_FL))
		return ret;

	ret = ocfs2_inode_lock(dentry->d_inode, &di_bh, 0);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;

	down_read(&oi->ip_xattr_sem);
	i_ret = ocfs2_xattr_ibody_list(dentry->d_inode, di, buffer, size);
	if (i_ret < 0)
		b_ret = 0;
	else {
		if (buffer) {
			buffer += i_ret;
			size -= i_ret;
		}
		b_ret = ocfs2_xattr_block_list(dentry->d_inode, di,
					       buffer, size);
		if (b_ret < 0)
			i_ret = 0;
	}
	up_read(&oi->ip_xattr_sem);
	ocfs2_inode_unlock(dentry->d_inode, 0);

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
					 struct ocfs2_xattr_search *xs,
					 void *buffer,
					 size_t len)
{
	u32 cpos, p_cluster, num_clusters, bpc, clusters;
	u64 blkno;
	int i, ret = 0;
	size_t cplen, blocksize;
	struct buffer_head *bh = NULL;
	struct ocfs2_xattr_value_root *xv;
	struct ocfs2_extent_list *el;

	xv = (struct ocfs2_xattr_value_root *)
		(xs->base + le16_to_cpu(xs->here->xe_name_offset) +
		OCFS2_XATTR_SIZE(xs->here->xe_name_len));
	el = &xv->xr_list;
	clusters = le32_to_cpu(xv->xr_clusters);
	bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	blocksize = inode->i_sb->s_blocksize;

	cpos = 0;
	while (cpos < clusters) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &p_cluster,
					       &num_clusters, el);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		blkno = ocfs2_clusters_to_blocks(inode->i_sb, p_cluster);
		/* Copy ocfs2_xattr_value */
		for (i = 0; i < num_clusters * bpc; i++, blkno++) {
			ret = ocfs2_read_block(OCFS2_SB(inode->i_sb), blkno,
					       &bh, OCFS2_BH_CACHED, inode);
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
			ret = ocfs2_xattr_get_value_outside(inode, xs,
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
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)xs->inode_bh->b_data;
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_xattr_block *xb;
	size_t size;
	int ret = -ENODATA;

	if (!di->i_xattr_loc)
		return ret;

	ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
			       le64_to_cpu(di->i_xattr_loc),
			       &blk_bh, OCFS2_BH_CACHED, inode);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	/*Verify the signature of xattr block*/
	if (memcmp((void *)blk_bh->b_data, OCFS2_XATTR_BLOCK_SIGNATURE,
		   strlen(OCFS2_XATTR_BLOCK_SIGNATURE))) {
		ret = -EFAULT;
		goto cleanup;
	}

	xs->xattr_bh = blk_bh;
	xb = (struct ocfs2_xattr_block *)blk_bh->b_data;
	xs->header = &xb->xb_attrs.xb_header;
	xs->base = (void *)xs->header;
	xs->end = (void *)(blk_bh->b_data) + blk_bh->b_size;
	xs->here = xs->header->xh_entries;

	ret = ocfs2_xattr_find_entry(name_index, name, xs);
	if (ret)
		goto cleanup;
	size = le64_to_cpu(xs->here->xe_value_size);
	if (buffer) {
		ret = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
		if (ocfs2_xattr_is_local(xs->here)) {
			memcpy(buffer, (void *)xs->base +
			       le16_to_cpu(xs->here->xe_name_offset) +
			       OCFS2_XATTR_SIZE(xs->here->xe_name_len), size);
		} else {
			ret = ocfs2_xattr_get_value_outside(inode, xs,
							    buffer, size);
			if (ret < 0) {
				mlog_errno(ret);
				goto cleanup;
			}
		}
	}
	ret = size;
cleanup:
	brelse(blk_bh);

	return ret;
}

/* ocfs2_xattr_get()
 *
 * Copy an extended attribute into the buffer provided.
 * Buffer is NULL to compute the size of buffer required.
 */
int ocfs2_xattr_get(struct inode *inode,
		    int name_index,
		    const char *name,
		    void *buffer,
		    size_t buffer_size)
{
	int ret;
	struct ocfs2_dinode *di = NULL;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_xattr_search xis = {
		.not_found = -ENODATA,
	};
	struct ocfs2_xattr_search xbs = {
		.not_found = -ENODATA,
	};

	if (!(oi->ip_dyn_features & OCFS2_HAS_XATTR_FL))
		ret = -ENODATA;

	ret = ocfs2_inode_lock(inode, &di_bh, 0);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	xis.inode_bh = xbs.inode_bh = di_bh;
	di = (struct ocfs2_dinode *)di_bh->b_data;

	down_read(&oi->ip_xattr_sem);
	ret = ocfs2_xattr_ibody_get(inode, name_index, name, buffer,
				    buffer_size, &xis);
	if (ret == -ENODATA)
		ret = ocfs2_xattr_block_get(inode, name_index, name, buffer,
					    buffer_size, &xbs);
	up_read(&oi->ip_xattr_sem);
	ocfs2_inode_unlock(inode, 0);

	brelse(di_bh);

	return ret;
}

static int __ocfs2_xattr_set_value_outside(struct inode *inode,
					   struct ocfs2_xattr_value_root *xv,
					   const void *value,
					   int value_len)
{
	int ret = 0, i, cp_len, credits;
	u16 blocksize = inode->i_sb->s_blocksize;
	u32 p_cluster, num_clusters;
	u32 cpos = 0, bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	u32 clusters = ocfs2_clusters_for_bytes(inode->i_sb, value_len);
	u64 blkno;
	struct buffer_head *bh = NULL;
	handle_t *handle;

	BUG_ON(clusters > le32_to_cpu(xv->xr_clusters));

	credits = clusters * bpc;
	handle = ocfs2_start_trans(OCFS2_SB(inode->i_sb), credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	while (cpos < clusters) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &p_cluster,
					       &num_clusters, &xv->xr_list);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}

		blkno = ocfs2_clusters_to_blocks(inode->i_sb, p_cluster);

		for (i = 0; i < num_clusters * bpc; i++, blkno++) {
			ret = ocfs2_read_block(OCFS2_SB(inode->i_sb), blkno,
					       &bh, OCFS2_BH_CACHED, inode);
			if (ret) {
				mlog_errno(ret);
				goto out_commit;
			}

			ret = ocfs2_journal_access(handle,
						   inode,
						   bh,
						   OCFS2_JOURNAL_ACCESS_WRITE);
			if (ret < 0) {
				mlog_errno(ret);
				goto out_commit;
			}

			cp_len = value_len > blocksize ? blocksize : value_len;
			memcpy(bh->b_data, value, cp_len);
			value_len -= cp_len;
			value += cp_len;
			if (cp_len < blocksize)
				memset(bh->b_data + cp_len, 0,
				       blocksize - cp_len);

			ret = ocfs2_journal_dirty(handle, bh);
			if (ret < 0) {
				mlog_errno(ret);
				goto out_commit;
			}
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
out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	brelse(bh);

	return ret;
}

static int ocfs2_xattr_cleanup(struct inode *inode,
			       struct ocfs2_xattr_info *xi,
			       struct ocfs2_xattr_search *xs,
			       size_t offs)
{
	handle_t *handle = NULL;
	int ret = 0;
	size_t name_len = strlen(xi->name);
	void *val = xs->base + offs;
	size_t size = OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_ROOT_SIZE;

	handle = ocfs2_start_trans((OCFS2_SB(inode->i_sb)),
				   OCFS2_XATTR_BLOCK_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}
	ret = ocfs2_journal_access(handle, inode, xs->xattr_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}
	/* Decrease xattr count */
	le16_add_cpu(&xs->header->xh_count, -1);
	/* Remove the xattr entry and tree root which has already be set*/
	memset((void *)xs->here, 0, sizeof(struct ocfs2_xattr_entry));
	memset(val, 0, size);

	ret = ocfs2_journal_dirty(handle, xs->xattr_bh);
	if (ret < 0)
		mlog_errno(ret);
out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	return ret;
}

static int ocfs2_xattr_update_entry(struct inode *inode,
				    struct ocfs2_xattr_info *xi,
				    struct ocfs2_xattr_search *xs,
				    size_t offs)
{
	handle_t *handle = NULL;
	int ret = 0;

	handle = ocfs2_start_trans((OCFS2_SB(inode->i_sb)),
				   OCFS2_XATTR_BLOCK_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}
	ret = ocfs2_journal_access(handle, inode, xs->xattr_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	xs->here->xe_name_offset = cpu_to_le16(offs);
	xs->here->xe_value_size = cpu_to_le64(xi->value_len);
	if (xi->value_len <= OCFS2_XATTR_INLINE_SIZE)
		ocfs2_xattr_set_local(xs->here, 1);
	else
		ocfs2_xattr_set_local(xs->here, 0);
	ocfs2_xattr_hash_entry(inode, xs->header, xs->here);

	ret = ocfs2_journal_dirty(handle, xs->xattr_bh);
	if (ret < 0)
		mlog_errno(ret);
out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	return ret;
}

/*
 * ocfs2_xattr_set_value_outside()
 *
 * Set large size value in B tree.
 */
static int ocfs2_xattr_set_value_outside(struct inode *inode,
					 struct ocfs2_xattr_info *xi,
					 struct ocfs2_xattr_search *xs,
					 size_t offs)
{
	size_t name_len = strlen(xi->name);
	void *val = xs->base + offs;
	struct ocfs2_xattr_value_root *xv = NULL;
	size_t size = OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_ROOT_SIZE;
	int ret = 0;

	memset(val, 0, size);
	memcpy(val, xi->name, name_len);
	xv = (struct ocfs2_xattr_value_root *)
		(val + OCFS2_XATTR_SIZE(name_len));
	xv->xr_clusters = 0;
	xv->xr_last_eb_blk = 0;
	xv->xr_list.l_tree_depth = 0;
	xv->xr_list.l_count = cpu_to_le16(1);
	xv->xr_list.l_next_free_rec = 0;

	ret = ocfs2_xattr_value_truncate(inode, xs->xattr_bh, xv,
					 xi->value_len);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	ret = __ocfs2_xattr_set_value_outside(inode, xv, xi->value,
					      xi->value_len);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	ret = ocfs2_xattr_update_entry(inode, xi, xs, offs);
	if (ret < 0)
		mlog_errno(ret);

	return ret;
}

/*
 * ocfs2_xattr_set_entry_local()
 *
 * Set, replace or remove extended attribute in local.
 */
static void ocfs2_xattr_set_entry_local(struct inode *inode,
					struct ocfs2_xattr_info *xi,
					struct ocfs2_xattr_search *xs,
					struct ocfs2_xattr_entry *last,
					size_t min_offs)
{
	size_t name_len = strlen(xi->name);
	int i;

	if (xi->value && xs->not_found) {
		/* Insert the new xattr entry. */
		le16_add_cpu(&xs->header->xh_count, 1);
		ocfs2_xattr_set_type(last, xi->name_index);
		ocfs2_xattr_set_local(last, 1);
		last->xe_name_len = name_len;
	} else {
		void *first_val;
		void *val;
		size_t offs, size;

		first_val = xs->base + min_offs;
		offs = le16_to_cpu(xs->here->xe_name_offset);
		val = xs->base + offs;

		if (le64_to_cpu(xs->here->xe_value_size) >
		    OCFS2_XATTR_INLINE_SIZE)
			size = OCFS2_XATTR_SIZE(name_len) +
				OCFS2_XATTR_ROOT_SIZE;
		else
			size = OCFS2_XATTR_SIZE(name_len) +
			OCFS2_XATTR_SIZE(le64_to_cpu(xs->here->xe_value_size));

		if (xi->value && size == OCFS2_XATTR_SIZE(name_len) +
				OCFS2_XATTR_SIZE(xi->value_len)) {
			/* The old and the new value have the
			   same size. Just replace the value. */
			ocfs2_xattr_set_local(xs->here, 1);
			xs->here->xe_value_size = cpu_to_le64(xi->value_len);
			/* Clear value bytes. */
			memset(val + OCFS2_XATTR_SIZE(name_len),
			       0,
			       OCFS2_XATTR_SIZE(xi->value_len));
			memcpy(val + OCFS2_XATTR_SIZE(name_len),
			       xi->value,
			       xi->value_len);
			return;
		}
		/* Remove the old name+value. */
		memmove(first_val + size, first_val, val - first_val);
		memset(first_val, 0, size);
		xs->here->xe_name_hash = 0;
		xs->here->xe_name_offset = 0;
		ocfs2_xattr_set_local(xs->here, 1);
		xs->here->xe_value_size = 0;

		min_offs += size;

		/* Adjust all value offsets. */
		last = xs->header->xh_entries;
		for (i = 0 ; i < le16_to_cpu(xs->header->xh_count); i++) {
			size_t o = le16_to_cpu(last->xe_name_offset);

			if (o < offs)
				last->xe_name_offset = cpu_to_le16(o + size);
			last += 1;
		}

		if (!xi->value) {
			/* Remove the old entry. */
			last -= 1;
			memmove(xs->here, xs->here + 1,
				(void *)last - (void *)xs->here);
			memset(last, 0, sizeof(struct ocfs2_xattr_entry));
			le16_add_cpu(&xs->header->xh_count, -1);
		}
	}
	if (xi->value) {
		/* Insert the new name+value. */
		size_t size = OCFS2_XATTR_SIZE(name_len) +
				OCFS2_XATTR_SIZE(xi->value_len);
		void *val = xs->base + min_offs - size;

		xs->here->xe_name_offset = cpu_to_le16(min_offs - size);
		memset(val, 0, size);
		memcpy(val, xi->name, name_len);
		memcpy(val + OCFS2_XATTR_SIZE(name_len),
		       xi->value,
		       xi->value_len);
		xs->here->xe_value_size = cpu_to_le64(xi->value_len);
		ocfs2_xattr_set_local(xs->here, 1);
		ocfs2_xattr_hash_entry(inode, xs->header, xs->here);
	}

	return;
}

/*
 * ocfs2_xattr_set_entry()
 *
 * Set extended attribute entry into inode or block.
 *
 * If extended attribute value size > OCFS2_XATTR_INLINE_SIZE,
 * We first insert tree root(ocfs2_xattr_value_root) with set_entry_local(),
 * then set value in B tree with set_value_outside().
 */
static int ocfs2_xattr_set_entry(struct inode *inode,
				 struct ocfs2_xattr_info *xi,
				 struct ocfs2_xattr_search *xs,
				 int flag)
{
	struct ocfs2_xattr_entry *last;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)xs->inode_bh->b_data;
	size_t min_offs = xs->end - xs->base, name_len = strlen(xi->name);
	size_t size_l = 0;
	handle_t *handle = NULL;
	int free, i, ret;
	struct ocfs2_xattr_info xi_l = {
		.name_index = xi->name_index,
		.name = xi->name,
		.value = xi->value,
		.value_len = xi->value_len,
	};

	/* Compute min_offs, last and free space. */
	last = xs->header->xh_entries;

	for (i = 0 ; i < le16_to_cpu(xs->header->xh_count); i++) {
		size_t offs = le16_to_cpu(last->xe_name_offset);
		if (offs < min_offs)
			min_offs = offs;
		last += 1;
	}

	free = min_offs - ((void *)last - xs->base) - sizeof(__u32);
	if (free < 0)
		return -EFAULT;

	if (!xs->not_found) {
		size_t size = 0;
		if (ocfs2_xattr_is_local(xs->here))
			size = OCFS2_XATTR_SIZE(name_len) +
			OCFS2_XATTR_SIZE(le64_to_cpu(xs->here->xe_value_size));
		else
			size = OCFS2_XATTR_SIZE(name_len) +
				OCFS2_XATTR_ROOT_SIZE;
		free += (size + sizeof(struct ocfs2_xattr_entry));
	}
	/* Check free space in inode or block */
	if (xi->value && xi->value_len > OCFS2_XATTR_INLINE_SIZE) {
		if (free < sizeof(struct ocfs2_xattr_entry) +
			   OCFS2_XATTR_SIZE(name_len) +
			   OCFS2_XATTR_ROOT_SIZE) {
			ret = -ENOSPC;
			goto out;
		}
		size_l = OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_ROOT_SIZE;
		xi_l.value = (void *)&def_xv;
		xi_l.value_len = OCFS2_XATTR_ROOT_SIZE;
	} else if (xi->value) {
		if (free < sizeof(struct ocfs2_xattr_entry) +
			   OCFS2_XATTR_SIZE(name_len) +
			   OCFS2_XATTR_SIZE(xi->value_len)) {
			ret = -ENOSPC;
			goto out;
		}
	}

	if (!xs->not_found) {
		/* For existing extended attribute */
		size_t size = OCFS2_XATTR_SIZE(name_len) +
			OCFS2_XATTR_SIZE(le64_to_cpu(xs->here->xe_value_size));
		size_t offs = le16_to_cpu(xs->here->xe_name_offset);
		void *val = xs->base + offs;

		if (ocfs2_xattr_is_local(xs->here) && size == size_l) {
			/* Replace existing local xattr with tree root */
			ret = ocfs2_xattr_set_value_outside(inode, xi, xs,
							    offs);
			if (ret < 0)
				mlog_errno(ret);
			goto out;
		} else if (!ocfs2_xattr_is_local(xs->here)) {
			/* For existing xattr which has value outside */
			struct ocfs2_xattr_value_root *xv = NULL;
			xv = (struct ocfs2_xattr_value_root *)(val +
				OCFS2_XATTR_SIZE(name_len));

			if (xi->value_len > OCFS2_XATTR_INLINE_SIZE) {
				/*
				 * If new value need set outside also,
				 * first truncate old value to new value,
				 * then set new value with set_value_outside().
				 */
				ret = ocfs2_xattr_value_truncate(inode,
								 xs->xattr_bh,
								 xv,
								 xi->value_len);
				if (ret < 0) {
					mlog_errno(ret);
					goto out;
				}

				ret = __ocfs2_xattr_set_value_outside(inode,
								xv,
								xi->value,
								xi->value_len);
				if (ret < 0) {
					mlog_errno(ret);
					goto out;
				}

				ret = ocfs2_xattr_update_entry(inode,
							       xi,
							       xs,
							       offs);
				if (ret < 0)
					mlog_errno(ret);
				goto out;
			} else {
				/*
				 * If new value need set in local,
				 * just trucate old value to zero.
				 */
				 ret = ocfs2_xattr_value_truncate(inode,
								 xs->xattr_bh,
								 xv,
								 0);
				if (ret < 0)
					mlog_errno(ret);
			}
		}
	}

	handle = ocfs2_start_trans((OCFS2_SB(inode->i_sb)),
				   OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access(handle, inode, xs->inode_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	if (!(flag & OCFS2_INLINE_XATTR_FL)) {
		/*set extended attribue in external blcok*/
		ret = ocfs2_extend_trans(handle,
					 OCFS2_XATTR_BLOCK_UPDATE_CREDITS);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
		ret = ocfs2_journal_access(handle, inode, xs->xattr_bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
	}

	/*
	 * Set value in local, include set tree root in local.
	 * This is the first step for value size >INLINE_SIZE.
	 */
	ocfs2_xattr_set_entry_local(inode, &xi_l, xs, last, min_offs);

	if (!(flag & OCFS2_INLINE_XATTR_FL)) {
		ret = ocfs2_journal_dirty(handle, xs->xattr_bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_commit;
		}
	}

	if (!(oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL) &&
	    (flag & OCFS2_INLINE_XATTR_FL)) {
		struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
		unsigned int xattrsize = osb->s_xattr_inline_size;

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
	}
	/* Update xattr flag */
	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features |= flag;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);
	/* Update inode ctime */
	inode->i_ctime = CURRENT_TIME;
	di->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	di->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);

	ret = ocfs2_journal_dirty(handle, xs->inode_bh);
	if (ret < 0)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);

	if (!ret && xi->value_len > OCFS2_XATTR_INLINE_SIZE) {
		/*
		 * Set value outside in B tree.
		 * This is the second step for value size > INLINE_SIZE.
		 */
		size_t offs = le16_to_cpu(xs->here->xe_name_offset);
		ret = ocfs2_xattr_set_value_outside(inode, xi, xs, offs);
		if (ret < 0) {
			int ret2;

			mlog_errno(ret);
			/*
			 * If set value outside failed, we have to clean
			 * the junk tree root we have already set in local.
			 */
			ret2 = ocfs2_xattr_cleanup(inode, xi, xs, offs);
			if (ret2 < 0)
				mlog_errno(ret2);
		}
	}
out:
	return ret;

}

static int ocfs2_xattr_free_block(handle_t *handle,
				  struct ocfs2_super *osb,
				  struct ocfs2_xattr_block *xb)
{
	struct inode *xb_alloc_inode;
	struct buffer_head *xb_alloc_bh = NULL;
	u64 blk = le64_to_cpu(xb->xb_blkno);
	u16 bit = le16_to_cpu(xb->xb_suballoc_bit);
	u64 bg_blkno = ocfs2_which_suballoc_group(blk, bit);
	int ret = 0;

	xb_alloc_inode = ocfs2_get_system_file_inode(osb,
				EXTENT_ALLOC_SYSTEM_INODE,
				le16_to_cpu(xb->xb_suballoc_slot));
	if (!xb_alloc_inode) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	mutex_lock(&xb_alloc_inode->i_mutex);

	ret = ocfs2_inode_lock(xb_alloc_inode, &xb_alloc_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_mutex;
	}
	ret = ocfs2_extend_trans(handle, OCFS2_SUBALLOC_FREE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_unlock;
	}
	ret = ocfs2_free_suballoc_bits(handle, xb_alloc_inode, xb_alloc_bh,
				       bit, bg_blkno, 1);
	if (ret < 0)
		mlog_errno(ret);
out_unlock:
	ocfs2_inode_unlock(xb_alloc_inode, 1);
	brelse(xb_alloc_bh);
out_mutex:
	mutex_unlock(&xb_alloc_inode->i_mutex);
	iput(xb_alloc_inode);
out:
	return ret;
}

static int ocfs2_remove_value_outside(struct inode*inode,
				      struct buffer_head *bh,
				      struct ocfs2_xattr_header *header)
{
	int ret = 0, i;

	for (i = 0; i < le16_to_cpu(header->xh_count); i++) {
		struct ocfs2_xattr_entry *entry = &header->xh_entries[i];

		if (!ocfs2_xattr_is_local(entry)) {
			struct ocfs2_xattr_value_root *xv;
			void *val;

			val = (void *)header +
				le16_to_cpu(entry->xe_name_offset);
			xv = (struct ocfs2_xattr_value_root *)
				(val + OCFS2_XATTR_SIZE(entry->xe_name_len));
			ret = ocfs2_xattr_value_truncate(inode, bh, xv, 0);
			if (ret < 0) {
				mlog_errno(ret);
				return ret;
			}
		}
	}

	return ret;
}

static int ocfs2_xattr_ibody_remove(struct inode *inode,
				    struct buffer_head *di_bh)
{

	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_xattr_header *header;
	int ret;

	header = (struct ocfs2_xattr_header *)
		 ((void *)di + inode->i_sb->s_blocksize -
		 le16_to_cpu(di->i_xattr_inline_size));

	ret = ocfs2_remove_value_outside(inode, di_bh, header);

	return ret;
}

static int ocfs2_xattr_block_remove(struct inode *inode,
				    struct buffer_head *blk_bh)
{
	struct ocfs2_xattr_block *xb;
	struct ocfs2_xattr_header *header;
	int ret = 0;

	xb = (struct ocfs2_xattr_block *)blk_bh->b_data;
	header = &(xb->xb_attrs.xb_header);

	ret = ocfs2_remove_value_outside(inode, blk_bh, header);

	return ret;
}

/*
 * ocfs2_xattr_remove()
 *
 * Free extended attribute resources associated with this inode.
 */
int ocfs2_xattr_remove(struct inode *inode, struct buffer_head *di_bh)
{
	struct ocfs2_xattr_block *xb;
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	handle_t *handle;
	int ret;

	if (!(oi->ip_dyn_features & OCFS2_HAS_XATTR_FL))
		return 0;

	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL) {
		ret = ocfs2_xattr_ibody_remove(inode, di_bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}
	if (di->i_xattr_loc) {
		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       le64_to_cpu(di->i_xattr_loc),
				       &blk_bh, OCFS2_BH_CACHED, inode);
		if (ret < 0) {
			mlog_errno(ret);
			return ret;
		}
		/*Verify the signature of xattr block*/
		if (memcmp((void *)blk_bh->b_data, OCFS2_XATTR_BLOCK_SIGNATURE,
			   strlen(OCFS2_XATTR_BLOCK_SIGNATURE))) {
			ret = -EFAULT;
			goto out;
		}

		ret = ocfs2_xattr_block_remove(inode, blk_bh);
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
	ret = ocfs2_journal_access(handle, inode, di_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	if (di->i_xattr_loc) {
		xb = (struct ocfs2_xattr_block *)blk_bh->b_data;
		ocfs2_xattr_free_block(handle, osb, xb);
		di->i_xattr_loc = cpu_to_le64(0);
	}

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features &= ~(OCFS2_INLINE_XATTR_FL | OCFS2_HAS_XATTR_FL);
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret < 0)
		mlog_errno(ret);
out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	brelse(blk_bh);

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

/*
 * ocfs2_xattr_ibody_set()
 *
 * Set, replace or remove an extended attribute into inode block.
 *
 */
static int ocfs2_xattr_ibody_set(struct inode *inode,
				 struct ocfs2_xattr_info *xi,
				 struct ocfs2_xattr_search *xs)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)xs->inode_bh->b_data;
	int ret;

	if (inode->i_sb->s_blocksize == OCFS2_MIN_BLOCKSIZE)
		return -ENOSPC;

	down_write(&oi->ip_alloc_sem);
	if (!(oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL)) {
		if (!ocfs2_xattr_has_space_inline(inode, di)) {
			ret = -ENOSPC;
			goto out;
		}
	}

	ret = ocfs2_xattr_set_entry(inode, xi, xs,
				(OCFS2_INLINE_XATTR_FL | OCFS2_HAS_XATTR_FL));
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
	int ret = 0;

	if (!di->i_xattr_loc)
		return ret;

	ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
			       le64_to_cpu(di->i_xattr_loc),
			       &blk_bh, OCFS2_BH_CACHED, inode);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	/*Verify the signature of xattr block*/
	if (memcmp((void *)blk_bh->b_data, OCFS2_XATTR_BLOCK_SIGNATURE,
		   strlen(OCFS2_XATTR_BLOCK_SIGNATURE))) {
			ret = -EFAULT;
			goto cleanup;
	}

	xs->xattr_bh = blk_bh;
	xs->header = &((struct ocfs2_xattr_block *)blk_bh->b_data)->
			xb_attrs.xb_header;
	xs->base = (void *)xs->header;
	xs->end = (void *)(blk_bh->b_data) + blk_bh->b_size;
	xs->here = xs->header->xh_entries;

	ret = ocfs2_xattr_find_entry(name_index, name, xs);
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

/*
 * ocfs2_xattr_block_set()
 *
 * Set, replace or remove an extended attribute into external block.
 *
 */
static int ocfs2_xattr_block_set(struct inode *inode,
				 struct ocfs2_xattr_info *xi,
				 struct ocfs2_xattr_search *xs)
{
	struct buffer_head *new_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di =  (struct ocfs2_dinode *)xs->inode_bh->b_data;
	struct ocfs2_alloc_context *meta_ac = NULL;
	handle_t *handle = NULL;
	struct ocfs2_xattr_block *xblk = NULL;
	u16 suballoc_bit_start;
	u32 num_got;
	u64 first_blkno;
	int ret;

	if (!xs->xattr_bh) {
		/*
		 * Alloc one external block for extended attribute
		 * outside of inode.
		 */
		ret = ocfs2_reserve_new_metadata_blocks(osb, 1, &meta_ac);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
		handle = ocfs2_start_trans(osb,
					   OCFS2_XATTR_BLOCK_CREATE_CREDITS);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			mlog_errno(ret);
			goto out;
		}
		ret = ocfs2_journal_access(handle, inode, xs->inode_bh,
					   OCFS2_JOURNAL_ACCESS_CREATE);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_commit;
		}

		ret = ocfs2_claim_metadata(osb, handle, meta_ac, 1,
					   &suballoc_bit_start, &num_got,
					   &first_blkno);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_commit;
		}

		new_bh = sb_getblk(inode->i_sb, first_blkno);
		ocfs2_set_new_buffer_uptodate(inode, new_bh);

		ret = ocfs2_journal_access(handle, inode, new_bh,
					   OCFS2_JOURNAL_ACCESS_CREATE);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_commit;
		}

		/* Initialize ocfs2_xattr_block */
		xs->xattr_bh = new_bh;
		xblk = (struct ocfs2_xattr_block *)new_bh->b_data;
		memset(xblk, 0, inode->i_sb->s_blocksize);
		strcpy((void *)xblk, OCFS2_XATTR_BLOCK_SIGNATURE);
		xblk->xb_suballoc_slot = cpu_to_le16(osb->slot_num);
		xblk->xb_suballoc_bit = cpu_to_le16(suballoc_bit_start);
		xblk->xb_fs_generation = cpu_to_le32(osb->fs_generation);
		xblk->xb_blkno = cpu_to_le64(first_blkno);

		xs->header = &xblk->xb_attrs.xb_header;
		xs->base = (void *)xs->header;
		xs->end = (void *)xblk + inode->i_sb->s_blocksize;
		xs->here = xs->header->xh_entries;


		ret = ocfs2_journal_dirty(handle, new_bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_commit;
		}
		di->i_xattr_loc = cpu_to_le64(first_blkno);
		ret = ocfs2_journal_dirty(handle, xs->inode_bh);
		if (ret < 0)
			mlog_errno(ret);
out_commit:
		ocfs2_commit_trans(osb, handle);
out:
		if (meta_ac)
			ocfs2_free_alloc_context(meta_ac);
		if (ret < 0)
			return ret;
	}

	/* Set extended attribute into external block */
	ret = ocfs2_xattr_set_entry(inode, xi, xs, OCFS2_HAS_XATTR_FL);

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
	int ret;

	struct ocfs2_xattr_info xi = {
		.name_index = name_index,
		.name = name,
		.value = value,
		.value_len = value_len,
	};

	struct ocfs2_xattr_search xis = {
		.not_found = -ENODATA,
	};

	struct ocfs2_xattr_search xbs = {
		.not_found = -ENODATA,
	};

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}
	xis.inode_bh = xbs.inode_bh = di_bh;
	di = (struct ocfs2_dinode *)di_bh->b_data;

	down_write(&OCFS2_I(inode)->ip_xattr_sem);
	/*
	 * Scan inode and external block to find the same name
	 * extended attribute and collect search infomation.
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

	if (!value) {
		/* Remove existing extended attribute */
		if (!xis.not_found)
			ret = ocfs2_xattr_ibody_set(inode, &xi, &xis);
		else if (!xbs.not_found)
			ret = ocfs2_xattr_block_set(inode, &xi, &xbs);
	} else {
		/* We always try to set extended attribute into inode first*/
		ret = ocfs2_xattr_ibody_set(inode, &xi, &xis);
		if (!ret && !xbs.not_found) {
			/*
			 * If succeed and that extended attribute existing in
			 * external block, then we will remove it.
			 */
			xi.value = NULL;
			xi.value_len = 0;
			ret = ocfs2_xattr_block_set(inode, &xi, &xbs);
		} else if (ret == -ENOSPC) {
			if (di->i_xattr_loc && !xbs.xattr_bh) {
				ret = ocfs2_xattr_block_find(inode, name_index,
							     name, &xbs);
				if (ret)
					goto cleanup;
			}
			/*
			 * If no space in inode, we will set extended attribute
			 * into external block.
			 */
			ret = ocfs2_xattr_block_set(inode, &xi, &xbs);
			if (ret)
				goto cleanup;
			if (!xis.not_found) {
				/*
				 * If succeed and that extended attribute
				 * existing in inode, we will remove it.
				 */
				xi.value = NULL;
				xi.value_len = 0;
				ret = ocfs2_xattr_ibody_set(inode, &xi, &xis);
			}
		}
	}
cleanup:
	up_write(&OCFS2_I(inode)->ip_xattr_sem);
	ocfs2_inode_unlock(inode, 1);
	brelse(di_bh);
	brelse(xbs.xattr_bh);

	return ret;
}

