// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003-2006, Cluster File Systems, Inc, info@clusterfs.com
 * Written by Alex Tomas <alex@clusterfs.com>
 *
 * Architecture independence:
 *   Copyright (c) 2005, Bull S.A.
 *   Written by Pierre Peiffer <pierre.peiffer@bull.net>
 */

/*
 * Extents support for EXT4
 *
 * TODO:
 *   - ext4*_error() should be used in some situations
 *   - analyze all BUG()/BUG_ON(), use -EIO where appropriate
 *   - smart tree reduction
 */

#include <linux/fs.h>
#include <linux/time.h>
#include <linux/jbd2.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fiemap.h>
#include <linux/iomap.h>
#include <linux/sched/mm.h>
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "xattr.h"

#include <trace/events/ext4.h>

/*
 * used by extent splitting.
 */
#define EXT4_EXT_MAY_ZEROOUT	0x1  /* safe to zeroout if split fails \
					due to ENOSPC */
#define EXT4_EXT_MARK_UNWRIT1	0x2  /* mark first half unwritten */
#define EXT4_EXT_MARK_UNWRIT2	0x4  /* mark second half unwritten */

#define EXT4_EXT_DATA_VALID1	0x8  /* first half contains valid data */
#define EXT4_EXT_DATA_VALID2	0x10 /* second half contains valid data */

static __le32 ext4_extent_block_csum(struct inode *inode,
				     struct ext4_extent_header *eh)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	__u32 csum;

	csum = ext4_chksum(sbi, ei->i_csum_seed, (__u8 *)eh,
			   EXT4_EXTENT_TAIL_OFFSET(eh));
	return cpu_to_le32(csum);
}

static int ext4_extent_block_csum_verify(struct inode *inode,
					 struct ext4_extent_header *eh)
{
	struct ext4_extent_tail *et;

	if (!ext4_has_metadata_csum(inode->i_sb))
		return 1;

	et = find_ext4_extent_tail(eh);
	if (et->et_checksum != ext4_extent_block_csum(inode, eh))
		return 0;
	return 1;
}

static void ext4_extent_block_csum_set(struct inode *inode,
				       struct ext4_extent_header *eh)
{
	struct ext4_extent_tail *et;

	if (!ext4_has_metadata_csum(inode->i_sb))
		return;

	et = find_ext4_extent_tail(eh);
	et->et_checksum = ext4_extent_block_csum(inode, eh);
}

static int ext4_split_extent_at(handle_t *handle,
			     struct inode *inode,
			     struct ext4_ext_path **ppath,
			     ext4_lblk_t split,
			     int split_flag,
			     int flags);

static int ext4_ext_trunc_restart_fn(struct inode *inode, int *dropped)
{
	/*
	 * Drop i_data_sem to avoid deadlock with ext4_map_blocks.  At this
	 * moment, get_block can be called only for blocks inside i_size since
	 * page cache has been already dropped and writes are blocked by
	 * i_rwsem. So we can safely drop the i_data_sem here.
	 */
	BUG_ON(EXT4_JOURNAL(inode) == NULL);
	ext4_discard_preallocations(inode, 0);
	up_write(&EXT4_I(inode)->i_data_sem);
	*dropped = 1;
	return 0;
}

static void ext4_ext_drop_refs(struct ext4_ext_path *path)
{
	int depth, i;

	if (!path)
		return;
	depth = path->p_depth;
	for (i = 0; i <= depth; i++, path++) {
		brelse(path->p_bh);
		path->p_bh = NULL;
	}
}

void ext4_free_ext_path(struct ext4_ext_path *path)
{
	ext4_ext_drop_refs(path);
	kfree(path);
}

/*
 * Make sure 'handle' has at least 'check_cred' credits. If not, restart
 * transaction with 'restart_cred' credits. The function drops i_data_sem
 * when restarting transaction and gets it after transaction is restarted.
 *
 * The function returns 0 on success, 1 if transaction had to be restarted,
 * and < 0 in case of fatal error.
 */
int ext4_datasem_ensure_credits(handle_t *handle, struct inode *inode,
				int check_cred, int restart_cred,
				int revoke_cred)
{
	int ret;
	int dropped = 0;

	ret = ext4_journal_ensure_credits_fn(handle, check_cred, restart_cred,
		revoke_cred, ext4_ext_trunc_restart_fn(inode, &dropped));
	if (dropped)
		down_write(&EXT4_I(inode)->i_data_sem);
	return ret;
}

/*
 * could return:
 *  - EROFS
 *  - ENOMEM
 */
static int ext4_ext_get_access(handle_t *handle, struct inode *inode,
				struct ext4_ext_path *path)
{
	int err = 0;

	if (path->p_bh) {
		/* path points to block */
		BUFFER_TRACE(path->p_bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, inode->i_sb,
						    path->p_bh, EXT4_JTR_NONE);
		/*
		 * The extent buffer's verified bit will be set again in
		 * __ext4_ext_dirty(). We could leave an inconsistent
		 * buffer if the extents updating procudure break off du
		 * to some error happens, force to check it again.
		 */
		if (!err)
			clear_buffer_verified(path->p_bh);
	}
	/* path points to leaf/index in inode body */
	/* we use in-core data, no need to protect them */
	return err;
}

/*
 * could return:
 *  - EROFS
 *  - ENOMEM
 *  - EIO
 */
static int __ext4_ext_dirty(const char *where, unsigned int line,
			    handle_t *handle, struct inode *inode,
			    struct ext4_ext_path *path)
{
	int err;

	WARN_ON(!rwsem_is_locked(&EXT4_I(inode)->i_data_sem));
	if (path->p_bh) {
		ext4_extent_block_csum_set(inode, ext_block_hdr(path->p_bh));
		/* path points to block */
		err = __ext4_handle_dirty_metadata(where, line, handle,
						   inode, path->p_bh);
		/* Extents updating done, re-set verified flag */
		if (!err)
			set_buffer_verified(path->p_bh);
	} else {
		/* path points to leaf/index in inode body */
		err = ext4_mark_inode_dirty(handle, inode);
	}
	return err;
}

#define ext4_ext_dirty(handle, inode, path) \
		__ext4_ext_dirty(__func__, __LINE__, (handle), (inode), (path))

static ext4_fsblk_t ext4_ext_find_goal(struct inode *inode,
			      struct ext4_ext_path *path,
			      ext4_lblk_t block)
{
	if (path) {
		int depth = path->p_depth;
		struct ext4_extent *ex;

		/*
		 * Try to predict block placement assuming that we are
		 * filling in a file which will eventually be
		 * non-sparse --- i.e., in the case of libbfd writing
		 * an ELF object sections out-of-order but in a way
		 * the eventually results in a contiguous object or
		 * executable file, or some database extending a table
		 * space file.  However, this is actually somewhat
		 * non-ideal if we are writing a sparse file such as
		 * qemu or KVM writing a raw image file that is going
		 * to stay fairly sparse, since it will end up
		 * fragmenting the file system's free space.  Maybe we
		 * should have some hueristics or some way to allow
		 * userspace to pass a hint to file system,
		 * especially if the latter case turns out to be
		 * common.
		 */
		ex = path[depth].p_ext;
		if (ex) {
			ext4_fsblk_t ext_pblk = ext4_ext_pblock(ex);
			ext4_lblk_t ext_block = le32_to_cpu(ex->ee_block);

			if (block > ext_block)
				return ext_pblk + (block - ext_block);
			else
				return ext_pblk - (ext_block - block);
		}

		/* it looks like index is empty;
		 * try to find starting block from index itself */
		if (path[depth].p_bh)
			return path[depth].p_bh->b_blocknr;
	}

	/* OK. use inode's group */
	return ext4_inode_to_goal_block(inode);
}

/*
 * Allocation for a meta data block
 */
static ext4_fsblk_t
ext4_ext_new_meta_block(handle_t *handle, struct inode *inode,
			struct ext4_ext_path *path,
			struct ext4_extent *ex, int *err, unsigned int flags)
{
	ext4_fsblk_t goal, newblock;

	goal = ext4_ext_find_goal(inode, path, le32_to_cpu(ex->ee_block));
	newblock = ext4_new_meta_blocks(handle, inode, goal, flags,
					NULL, err);
	return newblock;
}

static inline int ext4_ext_space_block(struct inode *inode, int check)
{
	int size;

	size = (inode->i_sb->s_blocksize - sizeof(struct ext4_extent_header))
			/ sizeof(struct ext4_extent);
#ifdef AGGRESSIVE_TEST
	if (!check && size > 6)
		size = 6;
#endif
	return size;
}

static inline int ext4_ext_space_block_idx(struct inode *inode, int check)
{
	int size;

	size = (inode->i_sb->s_blocksize - sizeof(struct ext4_extent_header))
			/ sizeof(struct ext4_extent_idx);
#ifdef AGGRESSIVE_TEST
	if (!check && size > 5)
		size = 5;
#endif
	return size;
}

static inline int ext4_ext_space_root(struct inode *inode, int check)
{
	int size;

	size = sizeof(EXT4_I(inode)->i_data);
	size -= sizeof(struct ext4_extent_header);
	size /= sizeof(struct ext4_extent);
#ifdef AGGRESSIVE_TEST
	if (!check && size > 3)
		size = 3;
#endif
	return size;
}

static inline int ext4_ext_space_root_idx(struct inode *inode, int check)
{
	int size;

	size = sizeof(EXT4_I(inode)->i_data);
	size -= sizeof(struct ext4_extent_header);
	size /= sizeof(struct ext4_extent_idx);
#ifdef AGGRESSIVE_TEST
	if (!check && size > 4)
		size = 4;
#endif
	return size;
}

static inline int
ext4_force_split_extent_at(handle_t *handle, struct inode *inode,
			   struct ext4_ext_path **ppath, ext4_lblk_t lblk,
			   int nofail)
{
	struct ext4_ext_path *path = *ppath;
	int unwritten = ext4_ext_is_unwritten(path[path->p_depth].p_ext);
	int flags = EXT4_EX_NOCACHE | EXT4_GET_BLOCKS_PRE_IO;

	if (nofail)
		flags |= EXT4_GET_BLOCKS_METADATA_NOFAIL | EXT4_EX_NOFAIL;

	return ext4_split_extent_at(handle, inode, ppath, lblk, unwritten ?
			EXT4_EXT_MARK_UNWRIT1|EXT4_EXT_MARK_UNWRIT2 : 0,
			flags);
}

static int
ext4_ext_max_entries(struct inode *inode, int depth)
{
	int max;

	if (depth == ext_depth(inode)) {
		if (depth == 0)
			max = ext4_ext_space_root(inode, 1);
		else
			max = ext4_ext_space_root_idx(inode, 1);
	} else {
		if (depth == 0)
			max = ext4_ext_space_block(inode, 1);
		else
			max = ext4_ext_space_block_idx(inode, 1);
	}

	return max;
}

static int ext4_valid_extent(struct inode *inode, struct ext4_extent *ext)
{
	ext4_fsblk_t block = ext4_ext_pblock(ext);
	int len = ext4_ext_get_actual_len(ext);
	ext4_lblk_t lblock = le32_to_cpu(ext->ee_block);

	/*
	 * We allow neither:
	 *  - zero length
	 *  - overflow/wrap-around
	 */
	if (lblock + len <= lblock)
		return 0;
	return ext4_inode_block_valid(inode, block, len);
}

static int ext4_valid_extent_idx(struct inode *inode,
				struct ext4_extent_idx *ext_idx)
{
	ext4_fsblk_t block = ext4_idx_pblock(ext_idx);

	return ext4_inode_block_valid(inode, block, 1);
}

static int ext4_valid_extent_entries(struct inode *inode,
				     struct ext4_extent_header *eh,
				     ext4_lblk_t lblk, ext4_fsblk_t *pblk,
				     int depth)
{
	unsigned short entries;
	ext4_lblk_t lblock = 0;
	ext4_lblk_t cur = 0;

	if (eh->eh_entries == 0)
		return 1;

	entries = le16_to_cpu(eh->eh_entries);

	if (depth == 0) {
		/* leaf entries */
		struct ext4_extent *ext = EXT_FIRST_EXTENT(eh);

		/*
		 * The logical block in the first entry should equal to
		 * the number in the index block.
		 */
		if (depth != ext_depth(inode) &&
		    lblk != le32_to_cpu(ext->ee_block))
			return 0;
		while (entries) {
			if (!ext4_valid_extent(inode, ext))
				return 0;

			/* Check for overlapping extents */
			lblock = le32_to_cpu(ext->ee_block);
			if (lblock < cur) {
				*pblk = ext4_ext_pblock(ext);
				return 0;
			}
			cur = lblock + ext4_ext_get_actual_len(ext);
			ext++;
			entries--;
		}
	} else {
		struct ext4_extent_idx *ext_idx = EXT_FIRST_INDEX(eh);

		/*
		 * The logical block in the first entry should equal to
		 * the number in the parent index block.
		 */
		if (depth != ext_depth(inode) &&
		    lblk != le32_to_cpu(ext_idx->ei_block))
			return 0;
		while (entries) {
			if (!ext4_valid_extent_idx(inode, ext_idx))
				return 0;

			/* Check for overlapping index extents */
			lblock = le32_to_cpu(ext_idx->ei_block);
			if (lblock < cur) {
				*pblk = ext4_idx_pblock(ext_idx);
				return 0;
			}
			ext_idx++;
			entries--;
			cur = lblock + 1;
		}
	}
	return 1;
}

static int __ext4_ext_check(const char *function, unsigned int line,
			    struct inode *inode, struct ext4_extent_header *eh,
			    int depth, ext4_fsblk_t pblk, ext4_lblk_t lblk)
{
	const char *error_msg;
	int max = 0, err = -EFSCORRUPTED;

	if (unlikely(eh->eh_magic != EXT4_EXT_MAGIC)) {
		error_msg = "invalid magic";
		goto corrupted;
	}
	if (unlikely(le16_to_cpu(eh->eh_depth) != depth)) {
		error_msg = "unexpected eh_depth";
		goto corrupted;
	}
	if (unlikely(eh->eh_max == 0)) {
		error_msg = "invalid eh_max";
		goto corrupted;
	}
	max = ext4_ext_max_entries(inode, depth);
	if (unlikely(le16_to_cpu(eh->eh_max) > max)) {
		error_msg = "too large eh_max";
		goto corrupted;
	}
	if (unlikely(le16_to_cpu(eh->eh_entries) > le16_to_cpu(eh->eh_max))) {
		error_msg = "invalid eh_entries";
		goto corrupted;
	}
	if (unlikely((eh->eh_entries == 0) && (depth > 0))) {
		error_msg = "eh_entries is 0 but eh_depth is > 0";
		goto corrupted;
	}
	if (!ext4_valid_extent_entries(inode, eh, lblk, &pblk, depth)) {
		error_msg = "invalid extent entries";
		goto corrupted;
	}
	if (unlikely(depth > 32)) {
		error_msg = "too large eh_depth";
		goto corrupted;
	}
	/* Verify checksum on non-root extent tree nodes */
	if (ext_depth(inode) != depth &&
	    !ext4_extent_block_csum_verify(inode, eh)) {
		error_msg = "extent tree corrupted";
		err = -EFSBADCRC;
		goto corrupted;
	}
	return 0;

corrupted:
	ext4_error_inode_err(inode, function, line, 0, -err,
			     "pblk %llu bad header/extent: %s - magic %x, "
			     "entries %u, max %u(%u), depth %u(%u)",
			     (unsigned long long) pblk, error_msg,
			     le16_to_cpu(eh->eh_magic),
			     le16_to_cpu(eh->eh_entries),
			     le16_to_cpu(eh->eh_max),
			     max, le16_to_cpu(eh->eh_depth), depth);
	return err;
}

#define ext4_ext_check(inode, eh, depth, pblk)			\
	__ext4_ext_check(__func__, __LINE__, (inode), (eh), (depth), (pblk), 0)

int ext4_ext_check_inode(struct inode *inode)
{
	return ext4_ext_check(inode, ext_inode_hdr(inode), ext_depth(inode), 0);
}

static void ext4_cache_extents(struct inode *inode,
			       struct ext4_extent_header *eh)
{
	struct ext4_extent *ex = EXT_FIRST_EXTENT(eh);
	ext4_lblk_t prev = 0;
	int i;

	for (i = le16_to_cpu(eh->eh_entries); i > 0; i--, ex++) {
		unsigned int status = EXTENT_STATUS_WRITTEN;
		ext4_lblk_t lblk = le32_to_cpu(ex->ee_block);
		int len = ext4_ext_get_actual_len(ex);

		if (prev && (prev != lblk))
			ext4_es_cache_extent(inode, prev, lblk - prev, ~0,
					     EXTENT_STATUS_HOLE);

		if (ext4_ext_is_unwritten(ex))
			status = EXTENT_STATUS_UNWRITTEN;
		ext4_es_cache_extent(inode, lblk, len,
				     ext4_ext_pblock(ex), status);
		prev = lblk + len;
	}
}

static struct buffer_head *
__read_extent_tree_block(const char *function, unsigned int line,
			 struct inode *inode, struct ext4_extent_idx *idx,
			 int depth, int flags)
{
	struct buffer_head		*bh;
	int				err;
	gfp_t				gfp_flags = __GFP_MOVABLE | GFP_NOFS;
	ext4_fsblk_t			pblk;

	if (flags & EXT4_EX_NOFAIL)
		gfp_flags |= __GFP_NOFAIL;

	pblk = ext4_idx_pblock(idx);
	bh = sb_getblk_gfp(inode->i_sb, pblk, gfp_flags);
	if (unlikely(!bh))
		return ERR_PTR(-ENOMEM);

	if (!bh_uptodate_or_lock(bh)) {
		trace_ext4_ext_load_extent(inode, pblk, _RET_IP_);
		err = ext4_read_bh(bh, 0, NULL);
		if (err < 0)
			goto errout;
	}
	if (buffer_verified(bh) && !(flags & EXT4_EX_FORCE_CACHE))
		return bh;
	err = __ext4_ext_check(function, line, inode, ext_block_hdr(bh),
			       depth, pblk, le32_to_cpu(idx->ei_block));
	if (err)
		goto errout;
	set_buffer_verified(bh);
	/*
	 * If this is a leaf block, cache all of its entries
	 */
	if (!(flags & EXT4_EX_NOCACHE) && depth == 0) {
		struct ext4_extent_header *eh = ext_block_hdr(bh);
		ext4_cache_extents(inode, eh);
	}
	return bh;
errout:
	put_bh(bh);
	return ERR_PTR(err);

}

#define read_extent_tree_block(inode, idx, depth, flags)		\
	__read_extent_tree_block(__func__, __LINE__, (inode), (idx),	\
				 (depth), (flags))

/*
 * This function is called to cache a file's extent information in the
 * extent status tree
 */
int ext4_ext_precache(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_ext_path *path = NULL;
	struct buffer_head *bh;
	int i = 0, depth, ret = 0;

	if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		return 0;	/* not an extent-mapped inode */

	down_read(&ei->i_data_sem);
	depth = ext_depth(inode);

	/* Don't cache anything if there are no external extent blocks */
	if (!depth) {
		up_read(&ei->i_data_sem);
		return ret;
	}

	path = kcalloc(depth + 1, sizeof(struct ext4_ext_path),
		       GFP_NOFS);
	if (path == NULL) {
		up_read(&ei->i_data_sem);
		return -ENOMEM;
	}

	path[0].p_hdr = ext_inode_hdr(inode);
	ret = ext4_ext_check(inode, path[0].p_hdr, depth, 0);
	if (ret)
		goto out;
	path[0].p_idx = EXT_FIRST_INDEX(path[0].p_hdr);
	while (i >= 0) {
		/*
		 * If this is a leaf block or we've reached the end of
		 * the index block, go up
		 */
		if ((i == depth) ||
		    path[i].p_idx > EXT_LAST_INDEX(path[i].p_hdr)) {
			brelse(path[i].p_bh);
			path[i].p_bh = NULL;
			i--;
			continue;
		}
		bh = read_extent_tree_block(inode, path[i].p_idx++,
					    depth - i - 1,
					    EXT4_EX_FORCE_CACHE);
		if (IS_ERR(bh)) {
			ret = PTR_ERR(bh);
			break;
		}
		i++;
		path[i].p_bh = bh;
		path[i].p_hdr = ext_block_hdr(bh);
		path[i].p_idx = EXT_FIRST_INDEX(path[i].p_hdr);
	}
	ext4_set_inode_state(inode, EXT4_STATE_EXT_PRECACHED);
out:
	up_read(&ei->i_data_sem);
	ext4_free_ext_path(path);
	return ret;
}

#ifdef EXT_DEBUG
static void ext4_ext_show_path(struct inode *inode, struct ext4_ext_path *path)
{
	int k, l = path->p_depth;

	ext_debug(inode, "path:");
	for (k = 0; k <= l; k++, path++) {
		if (path->p_idx) {
			ext_debug(inode, "  %d->%llu",
				  le32_to_cpu(path->p_idx->ei_block),
				  ext4_idx_pblock(path->p_idx));
		} else if (path->p_ext) {
			ext_debug(inode, "  %d:[%d]%d:%llu ",
				  le32_to_cpu(path->p_ext->ee_block),
				  ext4_ext_is_unwritten(path->p_ext),
				  ext4_ext_get_actual_len(path->p_ext),
				  ext4_ext_pblock(path->p_ext));
		} else
			ext_debug(inode, "  []");
	}
	ext_debug(inode, "\n");
}

static void ext4_ext_show_leaf(struct inode *inode, struct ext4_ext_path *path)
{
	int depth = ext_depth(inode);
	struct ext4_extent_header *eh;
	struct ext4_extent *ex;
	int i;

	if (!path)
		return;

	eh = path[depth].p_hdr;
	ex = EXT_FIRST_EXTENT(eh);

	ext_debug(inode, "Displaying leaf extents\n");

	for (i = 0; i < le16_to_cpu(eh->eh_entries); i++, ex++) {
		ext_debug(inode, "%d:[%d]%d:%llu ", le32_to_cpu(ex->ee_block),
			  ext4_ext_is_unwritten(ex),
			  ext4_ext_get_actual_len(ex), ext4_ext_pblock(ex));
	}
	ext_debug(inode, "\n");
}

static void ext4_ext_show_move(struct inode *inode, struct ext4_ext_path *path,
			ext4_fsblk_t newblock, int level)
{
	int depth = ext_depth(inode);
	struct ext4_extent *ex;

	if (depth != level) {
		struct ext4_extent_idx *idx;
		idx = path[level].p_idx;
		while (idx <= EXT_MAX_INDEX(path[level].p_hdr)) {
			ext_debug(inode, "%d: move %d:%llu in new index %llu\n",
				  level, le32_to_cpu(idx->ei_block),
				  ext4_idx_pblock(idx), newblock);
			idx++;
		}

		return;
	}

	ex = path[depth].p_ext;
	while (ex <= EXT_MAX_EXTENT(path[depth].p_hdr)) {
		ext_debug(inode, "move %d:%llu:[%d]%d in new leaf %llu\n",
				le32_to_cpu(ex->ee_block),
				ext4_ext_pblock(ex),
				ext4_ext_is_unwritten(ex),
				ext4_ext_get_actual_len(ex),
				newblock);
		ex++;
	}
}

#else
#define ext4_ext_show_path(inode, path)
#define ext4_ext_show_leaf(inode, path)
#define ext4_ext_show_move(inode, path, newblock, level)
#endif

/*
 * ext4_ext_binsearch_idx:
 * binary search for the closest index of the given block
 * the header must be checked before calling this
 */
static void
ext4_ext_binsearch_idx(struct inode *inode,
			struct ext4_ext_path *path, ext4_lblk_t block)
{
	struct ext4_extent_header *eh = path->p_hdr;
	struct ext4_extent_idx *r, *l, *m;


	ext_debug(inode, "binsearch for %u(idx):  ", block);

	l = EXT_FIRST_INDEX(eh) + 1;
	r = EXT_LAST_INDEX(eh);
	while (l <= r) {
		m = l + (r - l) / 2;
		ext_debug(inode, "%p(%u):%p(%u):%p(%u) ", l,
			  le32_to_cpu(l->ei_block), m, le32_to_cpu(m->ei_block),
			  r, le32_to_cpu(r->ei_block));

		if (block < le32_to_cpu(m->ei_block))
			r = m - 1;
		else
			l = m + 1;
	}

	path->p_idx = l - 1;
	ext_debug(inode, "  -> %u->%lld ", le32_to_cpu(path->p_idx->ei_block),
		  ext4_idx_pblock(path->p_idx));

#ifdef CHECK_BINSEARCH
	{
		struct ext4_extent_idx *chix, *ix;
		int k;

		chix = ix = EXT_FIRST_INDEX(eh);
		for (k = 0; k < le16_to_cpu(eh->eh_entries); k++, ix++) {
			if (k != 0 && le32_to_cpu(ix->ei_block) <=
			    le32_to_cpu(ix[-1].ei_block)) {
				printk(KERN_DEBUG "k=%d, ix=0x%p, "
				       "first=0x%p\n", k,
				       ix, EXT_FIRST_INDEX(eh));
				printk(KERN_DEBUG "%u <= %u\n",
				       le32_to_cpu(ix->ei_block),
				       le32_to_cpu(ix[-1].ei_block));
			}
			BUG_ON(k && le32_to_cpu(ix->ei_block)
					   <= le32_to_cpu(ix[-1].ei_block));
			if (block < le32_to_cpu(ix->ei_block))
				break;
			chix = ix;
		}
		BUG_ON(chix != path->p_idx);
	}
#endif

}

/*
 * ext4_ext_binsearch:
 * binary search for closest extent of the given block
 * the header must be checked before calling this
 */
static void
ext4_ext_binsearch(struct inode *inode,
		struct ext4_ext_path *path, ext4_lblk_t block)
{
	struct ext4_extent_header *eh = path->p_hdr;
	struct ext4_extent *r, *l, *m;

	if (eh->eh_entries == 0) {
		/*
		 * this leaf is empty:
		 * we get such a leaf in split/add case
		 */
		return;
	}

	ext_debug(inode, "binsearch for %u:  ", block);

	l = EXT_FIRST_EXTENT(eh) + 1;
	r = EXT_LAST_EXTENT(eh);

	while (l <= r) {
		m = l + (r - l) / 2;
		ext_debug(inode, "%p(%u):%p(%u):%p(%u) ", l,
			  le32_to_cpu(l->ee_block), m, le32_to_cpu(m->ee_block),
			  r, le32_to_cpu(r->ee_block));

		if (block < le32_to_cpu(m->ee_block))
			r = m - 1;
		else
			l = m + 1;
	}

	path->p_ext = l - 1;
	ext_debug(inode, "  -> %d:%llu:[%d]%d ",
			le32_to_cpu(path->p_ext->ee_block),
			ext4_ext_pblock(path->p_ext),
			ext4_ext_is_unwritten(path->p_ext),
			ext4_ext_get_actual_len(path->p_ext));

#ifdef CHECK_BINSEARCH
	{
		struct ext4_extent *chex, *ex;
		int k;

		chex = ex = EXT_FIRST_EXTENT(eh);
		for (k = 0; k < le16_to_cpu(eh->eh_entries); k++, ex++) {
			BUG_ON(k && le32_to_cpu(ex->ee_block)
					  <= le32_to_cpu(ex[-1].ee_block));
			if (block < le32_to_cpu(ex->ee_block))
				break;
			chex = ex;
		}
		BUG_ON(chex != path->p_ext);
	}
#endif

}

void ext4_ext_tree_init(handle_t *handle, struct inode *inode)
{
	struct ext4_extent_header *eh;

	eh = ext_inode_hdr(inode);
	eh->eh_depth = 0;
	eh->eh_entries = 0;
	eh->eh_magic = EXT4_EXT_MAGIC;
	eh->eh_max = cpu_to_le16(ext4_ext_space_root(inode, 0));
	eh->eh_generation = 0;
	ext4_mark_inode_dirty(handle, inode);
}

struct ext4_ext_path *
ext4_find_extent(struct inode *inode, ext4_lblk_t block,
		 struct ext4_ext_path **orig_path, int flags)
{
	struct ext4_extent_header *eh;
	struct buffer_head *bh;
	struct ext4_ext_path *path = orig_path ? *orig_path : NULL;
	short int depth, i, ppos = 0;
	int ret;
	gfp_t gfp_flags = GFP_NOFS;

	if (flags & EXT4_EX_NOFAIL)
		gfp_flags |= __GFP_NOFAIL;

	eh = ext_inode_hdr(inode);
	depth = ext_depth(inode);
	if (depth < 0 || depth > EXT4_MAX_EXTENT_DEPTH) {
		EXT4_ERROR_INODE(inode, "inode has invalid extent depth: %d",
				 depth);
		ret = -EFSCORRUPTED;
		goto err;
	}

	if (path) {
		ext4_ext_drop_refs(path);
		if (depth > path[0].p_maxdepth) {
			kfree(path);
			*orig_path = path = NULL;
		}
	}
	if (!path) {
		/* account possible depth increase */
		path = kcalloc(depth + 2, sizeof(struct ext4_ext_path),
				gfp_flags);
		if (unlikely(!path))
			return ERR_PTR(-ENOMEM);
		path[0].p_maxdepth = depth + 1;
	}
	path[0].p_hdr = eh;
	path[0].p_bh = NULL;

	i = depth;
	if (!(flags & EXT4_EX_NOCACHE) && depth == 0)
		ext4_cache_extents(inode, eh);
	/* walk through the tree */
	while (i) {
		ext_debug(inode, "depth %d: num %d, max %d\n",
			  ppos, le16_to_cpu(eh->eh_entries), le16_to_cpu(eh->eh_max));

		ext4_ext_binsearch_idx(inode, path + ppos, block);
		path[ppos].p_block = ext4_idx_pblock(path[ppos].p_idx);
		path[ppos].p_depth = i;
		path[ppos].p_ext = NULL;

		bh = read_extent_tree_block(inode, path[ppos].p_idx, --i, flags);
		if (IS_ERR(bh)) {
			ret = PTR_ERR(bh);
			goto err;
		}

		eh = ext_block_hdr(bh);
		ppos++;
		path[ppos].p_bh = bh;
		path[ppos].p_hdr = eh;
	}

	path[ppos].p_depth = i;
	path[ppos].p_ext = NULL;
	path[ppos].p_idx = NULL;

	/* find extent */
	ext4_ext_binsearch(inode, path + ppos, block);
	/* if not an empty leaf */
	if (path[ppos].p_ext)
		path[ppos].p_block = ext4_ext_pblock(path[ppos].p_ext);

	ext4_ext_show_path(inode, path);

	return path;

err:
	ext4_free_ext_path(path);
	if (orig_path)
		*orig_path = NULL;
	return ERR_PTR(ret);
}

/*
 * ext4_ext_insert_index:
 * insert new index [@logical;@ptr] into the block at @curp;
 * check where to insert: before @curp or after @curp
 */
static int ext4_ext_insert_index(handle_t *handle, struct inode *inode,
				 struct ext4_ext_path *curp,
				 int logical, ext4_fsblk_t ptr)
{
	struct ext4_extent_idx *ix;
	int len, err;

	err = ext4_ext_get_access(handle, inode, curp);
	if (err)
		return err;

	if (unlikely(logical == le32_to_cpu(curp->p_idx->ei_block))) {
		EXT4_ERROR_INODE(inode,
				 "logical %d == ei_block %d!",
				 logical, le32_to_cpu(curp->p_idx->ei_block));
		return -EFSCORRUPTED;
	}

	if (unlikely(le16_to_cpu(curp->p_hdr->eh_entries)
			     >= le16_to_cpu(curp->p_hdr->eh_max))) {
		EXT4_ERROR_INODE(inode,
				 "eh_entries %d >= eh_max %d!",
				 le16_to_cpu(curp->p_hdr->eh_entries),
				 le16_to_cpu(curp->p_hdr->eh_max));
		return -EFSCORRUPTED;
	}

	if (logical > le32_to_cpu(curp->p_idx->ei_block)) {
		/* insert after */
		ext_debug(inode, "insert new index %d after: %llu\n",
			  logical, ptr);
		ix = curp->p_idx + 1;
	} else {
		/* insert before */
		ext_debug(inode, "insert new index %d before: %llu\n",
			  logical, ptr);
		ix = curp->p_idx;
	}

	len = EXT_LAST_INDEX(curp->p_hdr) - ix + 1;
	BUG_ON(len < 0);
	if (len > 0) {
		ext_debug(inode, "insert new index %d: "
				"move %d indices from 0x%p to 0x%p\n",
				logical, len, ix, ix + 1);
		memmove(ix + 1, ix, len * sizeof(struct ext4_extent_idx));
	}

	if (unlikely(ix > EXT_MAX_INDEX(curp->p_hdr))) {
		EXT4_ERROR_INODE(inode, "ix > EXT_MAX_INDEX!");
		return -EFSCORRUPTED;
	}

	ix->ei_block = cpu_to_le32(logical);
	ext4_idx_store_pblock(ix, ptr);
	le16_add_cpu(&curp->p_hdr->eh_entries, 1);

	if (unlikely(ix > EXT_LAST_INDEX(curp->p_hdr))) {
		EXT4_ERROR_INODE(inode, "ix > EXT_LAST_INDEX!");
		return -EFSCORRUPTED;
	}

	err = ext4_ext_dirty(handle, inode, curp);
	ext4_std_error(inode->i_sb, err);

	return err;
}

/*
 * ext4_ext_split:
 * inserts new subtree into the path, using free index entry
 * at depth @at:
 * - allocates all needed blocks (new leaf and all intermediate index blocks)
 * - makes decision where to split
 * - moves remaining extents and index entries (right to the split point)
 *   into the newly allocated blocks
 * - initializes subtree
 */
static int ext4_ext_split(handle_t *handle, struct inode *inode,
			  unsigned int flags,
			  struct ext4_ext_path *path,
			  struct ext4_extent *newext, int at)
{
	struct buffer_head *bh = NULL;
	int depth = ext_depth(inode);
	struct ext4_extent_header *neh;
	struct ext4_extent_idx *fidx;
	int i = at, k, m, a;
	ext4_fsblk_t newblock, oldblock;
	__le32 border;
	ext4_fsblk_t *ablocks = NULL; /* array of allocated blocks */
	gfp_t gfp_flags = GFP_NOFS;
	int err = 0;
	size_t ext_size = 0;

	if (flags & EXT4_EX_NOFAIL)
		gfp_flags |= __GFP_NOFAIL;

	/* make decision: where to split? */
	/* FIXME: now decision is simplest: at current extent */

	/* if current leaf will be split, then we should use
	 * border from split point */
	if (unlikely(path[depth].p_ext > EXT_MAX_EXTENT(path[depth].p_hdr))) {
		EXT4_ERROR_INODE(inode, "p_ext > EXT_MAX_EXTENT!");
		return -EFSCORRUPTED;
	}
	if (path[depth].p_ext != EXT_MAX_EXTENT(path[depth].p_hdr)) {
		border = path[depth].p_ext[1].ee_block;
		ext_debug(inode, "leaf will be split."
				" next leaf starts at %d\n",
				  le32_to_cpu(border));
	} else {
		border = newext->ee_block;
		ext_debug(inode, "leaf will be added."
				" next leaf starts at %d\n",
				le32_to_cpu(border));
	}

	/*
	 * If error occurs, then we break processing
	 * and mark filesystem read-only. index won't
	 * be inserted and tree will be in consistent
	 * state. Next mount will repair buffers too.
	 */

	/*
	 * Get array to track all allocated blocks.
	 * We need this to handle errors and free blocks
	 * upon them.
	 */
	ablocks = kcalloc(depth, sizeof(ext4_fsblk_t), gfp_flags);
	if (!ablocks)
		return -ENOMEM;

	/* allocate all needed blocks */
	ext_debug(inode, "allocate %d blocks for indexes/leaf\n", depth - at);
	for (a = 0; a < depth - at; a++) {
		newblock = ext4_ext_new_meta_block(handle, inode, path,
						   newext, &err, flags);
		if (newblock == 0)
			goto cleanup;
		ablocks[a] = newblock;
	}

	/* initialize new leaf */
	newblock = ablocks[--a];
	if (unlikely(newblock == 0)) {
		EXT4_ERROR_INODE(inode, "newblock == 0!");
		err = -EFSCORRUPTED;
		goto cleanup;
	}
	bh = sb_getblk_gfp(inode->i_sb, newblock, __GFP_MOVABLE | GFP_NOFS);
	if (unlikely(!bh)) {
		err = -ENOMEM;
		goto cleanup;
	}
	lock_buffer(bh);

	err = ext4_journal_get_create_access(handle, inode->i_sb, bh,
					     EXT4_JTR_NONE);
	if (err)
		goto cleanup;

	neh = ext_block_hdr(bh);
	neh->eh_entries = 0;
	neh->eh_max = cpu_to_le16(ext4_ext_space_block(inode, 0));
	neh->eh_magic = EXT4_EXT_MAGIC;
	neh->eh_depth = 0;
	neh->eh_generation = 0;

	/* move remainder of path[depth] to the new leaf */
	if (unlikely(path[depth].p_hdr->eh_entries !=
		     path[depth].p_hdr->eh_max)) {
		EXT4_ERROR_INODE(inode, "eh_entries %d != eh_max %d!",
				 path[depth].p_hdr->eh_entries,
				 path[depth].p_hdr->eh_max);
		err = -EFSCORRUPTED;
		goto cleanup;
	}
	/* start copy from next extent */
	m = EXT_MAX_EXTENT(path[depth].p_hdr) - path[depth].p_ext++;
	ext4_ext_show_move(inode, path, newblock, depth);
	if (m) {
		struct ext4_extent *ex;
		ex = EXT_FIRST_EXTENT(neh);
		memmove(ex, path[depth].p_ext, sizeof(struct ext4_extent) * m);
		le16_add_cpu(&neh->eh_entries, m);
	}

	/* zero out unused area in the extent block */
	ext_size = sizeof(struct ext4_extent_header) +
		sizeof(struct ext4_extent) * le16_to_cpu(neh->eh_entries);
	memset(bh->b_data + ext_size, 0, inode->i_sb->s_blocksize - ext_size);
	ext4_extent_block_csum_set(inode, neh);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	err = ext4_handle_dirty_metadata(handle, inode, bh);
	if (err)
		goto cleanup;
	brelse(bh);
	bh = NULL;

	/* correct old leaf */
	if (m) {
		err = ext4_ext_get_access(handle, inode, path + depth);
		if (err)
			goto cleanup;
		le16_add_cpu(&path[depth].p_hdr->eh_entries, -m);
		err = ext4_ext_dirty(handle, inode, path + depth);
		if (err)
			goto cleanup;

	}

	/* create intermediate indexes */
	k = depth - at - 1;
	if (unlikely(k < 0)) {
		EXT4_ERROR_INODE(inode, "k %d < 0!", k);
		err = -EFSCORRUPTED;
		goto cleanup;
	}
	if (k)
		ext_debug(inode, "create %d intermediate indices\n", k);
	/* insert new index into current index block */
	/* current depth stored in i var */
	i = depth - 1;
	while (k--) {
		oldblock = newblock;
		newblock = ablocks[--a];
		bh = sb_getblk(inode->i_sb, newblock);
		if (unlikely(!bh)) {
			err = -ENOMEM;
			goto cleanup;
		}
		lock_buffer(bh);

		err = ext4_journal_get_create_access(handle, inode->i_sb, bh,
						     EXT4_JTR_NONE);
		if (err)
			goto cleanup;

		neh = ext_block_hdr(bh);
		neh->eh_entries = cpu_to_le16(1);
		neh->eh_magic = EXT4_EXT_MAGIC;
		neh->eh_max = cpu_to_le16(ext4_ext_space_block_idx(inode, 0));
		neh->eh_depth = cpu_to_le16(depth - i);
		neh->eh_generation = 0;
		fidx = EXT_FIRST_INDEX(neh);
		fidx->ei_block = border;
		ext4_idx_store_pblock(fidx, oldblock);

		ext_debug(inode, "int.index at %d (block %llu): %u -> %llu\n",
				i, newblock, le32_to_cpu(border), oldblock);

		/* move remainder of path[i] to the new index block */
		if (unlikely(EXT_MAX_INDEX(path[i].p_hdr) !=
					EXT_LAST_INDEX(path[i].p_hdr))) {
			EXT4_ERROR_INODE(inode,
					 "EXT_MAX_INDEX != EXT_LAST_INDEX ee_block %d!",
					 le32_to_cpu(path[i].p_ext->ee_block));
			err = -EFSCORRUPTED;
			goto cleanup;
		}
		/* start copy indexes */
		m = EXT_MAX_INDEX(path[i].p_hdr) - path[i].p_idx++;
		ext_debug(inode, "cur 0x%p, last 0x%p\n", path[i].p_idx,
				EXT_MAX_INDEX(path[i].p_hdr));
		ext4_ext_show_move(inode, path, newblock, i);
		if (m) {
			memmove(++fidx, path[i].p_idx,
				sizeof(struct ext4_extent_idx) * m);
			le16_add_cpu(&neh->eh_entries, m);
		}
		/* zero out unused area in the extent block */
		ext_size = sizeof(struct ext4_extent_header) +
		   (sizeof(struct ext4_extent) * le16_to_cpu(neh->eh_entries));
		memset(bh->b_data + ext_size, 0,
			inode->i_sb->s_blocksize - ext_size);
		ext4_extent_block_csum_set(inode, neh);
		set_buffer_uptodate(bh);
		unlock_buffer(bh);

		err = ext4_handle_dirty_metadata(handle, inode, bh);
		if (err)
			goto cleanup;
		brelse(bh);
		bh = NULL;

		/* correct old index */
		if (m) {
			err = ext4_ext_get_access(handle, inode, path + i);
			if (err)
				goto cleanup;
			le16_add_cpu(&path[i].p_hdr->eh_entries, -m);
			err = ext4_ext_dirty(handle, inode, path + i);
			if (err)
				goto cleanup;
		}

		i--;
	}

	/* insert new index */
	err = ext4_ext_insert_index(handle, inode, path + at,
				    le32_to_cpu(border), newblock);

cleanup:
	if (bh) {
		if (buffer_locked(bh))
			unlock_buffer(bh);
		brelse(bh);
	}

	if (err) {
		/* free all allocated blocks in error case */
		for (i = 0; i < depth; i++) {
			if (!ablocks[i])
				continue;
			ext4_free_blocks(handle, inode, NULL, ablocks[i], 1,
					 EXT4_FREE_BLOCKS_METADATA);
		}
	}
	kfree(ablocks);

	return err;
}

/*
 * ext4_ext_grow_indepth:
 * implements tree growing procedure:
 * - allocates new block
 * - moves top-level data (index block or leaf) into the new block
 * - initializes new top-level, creating index that points to the
 *   just created block
 */
static int ext4_ext_grow_indepth(handle_t *handle, struct inode *inode,
				 unsigned int flags)
{
	struct ext4_extent_header *neh;
	struct buffer_head *bh;
	ext4_fsblk_t newblock, goal = 0;
	struct ext4_super_block *es = EXT4_SB(inode->i_sb)->s_es;
	int err = 0;
	size_t ext_size = 0;

	/* Try to prepend new index to old one */
	if (ext_depth(inode))
		goal = ext4_idx_pblock(EXT_FIRST_INDEX(ext_inode_hdr(inode)));
	if (goal > le32_to_cpu(es->s_first_data_block)) {
		flags |= EXT4_MB_HINT_TRY_GOAL;
		goal--;
	} else
		goal = ext4_inode_to_goal_block(inode);
	newblock = ext4_new_meta_blocks(handle, inode, goal, flags,
					NULL, &err);
	if (newblock == 0)
		return err;

	bh = sb_getblk_gfp(inode->i_sb, newblock, __GFP_MOVABLE | GFP_NOFS);
	if (unlikely(!bh))
		return -ENOMEM;
	lock_buffer(bh);

	err = ext4_journal_get_create_access(handle, inode->i_sb, bh,
					     EXT4_JTR_NONE);
	if (err) {
		unlock_buffer(bh);
		goto out;
	}

	ext_size = sizeof(EXT4_I(inode)->i_data);
	/* move top-level index/leaf into new block */
	memmove(bh->b_data, EXT4_I(inode)->i_data, ext_size);
	/* zero out unused area in the extent block */
	memset(bh->b_data + ext_size, 0, inode->i_sb->s_blocksize - ext_size);

	/* set size of new block */
	neh = ext_block_hdr(bh);
	/* old root could have indexes or leaves
	 * so calculate e_max right way */
	if (ext_depth(inode))
		neh->eh_max = cpu_to_le16(ext4_ext_space_block_idx(inode, 0));
	else
		neh->eh_max = cpu_to_le16(ext4_ext_space_block(inode, 0));
	neh->eh_magic = EXT4_EXT_MAGIC;
	ext4_extent_block_csum_set(inode, neh);
	set_buffer_uptodate(bh);
	set_buffer_verified(bh);
	unlock_buffer(bh);

	err = ext4_handle_dirty_metadata(handle, inode, bh);
	if (err)
		goto out;

	/* Update top-level index: num,max,pointer */
	neh = ext_inode_hdr(inode);
	neh->eh_entries = cpu_to_le16(1);
	ext4_idx_store_pblock(EXT_FIRST_INDEX(neh), newblock);
	if (neh->eh_depth == 0) {
		/* Root extent block becomes index block */
		neh->eh_max = cpu_to_le16(ext4_ext_space_root_idx(inode, 0));
		EXT_FIRST_INDEX(neh)->ei_block =
			EXT_FIRST_EXTENT(neh)->ee_block;
	}
	ext_debug(inode, "new root: num %d(%d), lblock %d, ptr %llu\n",
		  le16_to_cpu(neh->eh_entries), le16_to_cpu(neh->eh_max),
		  le32_to_cpu(EXT_FIRST_INDEX(neh)->ei_block),
		  ext4_idx_pblock(EXT_FIRST_INDEX(neh)));

	le16_add_cpu(&neh->eh_depth, 1);
	err = ext4_mark_inode_dirty(handle, inode);
out:
	brelse(bh);

	return err;
}

/*
 * ext4_ext_create_new_leaf:
 * finds empty index and adds new leaf.
 * if no free index is found, then it requests in-depth growing.
 */
static int ext4_ext_create_new_leaf(handle_t *handle, struct inode *inode,
				    unsigned int mb_flags,
				    unsigned int gb_flags,
				    struct ext4_ext_path **ppath,
				    struct ext4_extent *newext)
{
	struct ext4_ext_path *path = *ppath;
	struct ext4_ext_path *curp;
	int depth, i, err = 0;

repeat:
	i = depth = ext_depth(inode);

	/* walk up to the tree and look for free index entry */
	curp = path + depth;
	while (i > 0 && !EXT_HAS_FREE_INDEX(curp)) {
		i--;
		curp--;
	}

	/* we use already allocated block for index block,
	 * so subsequent data blocks should be contiguous */
	if (EXT_HAS_FREE_INDEX(curp)) {
		/* if we found index with free entry, then use that
		 * entry: create all needed subtree and add new leaf */
		err = ext4_ext_split(handle, inode, mb_flags, path, newext, i);
		if (err)
			goto out;

		/* refill path */
		path = ext4_find_extent(inode,
				    (ext4_lblk_t)le32_to_cpu(newext->ee_block),
				    ppath, gb_flags);
		if (IS_ERR(path))
			err = PTR_ERR(path);
	} else {
		/* tree is full, time to grow in depth */
		err = ext4_ext_grow_indepth(handle, inode, mb_flags);
		if (err)
			goto out;

		/* refill path */
		path = ext4_find_extent(inode,
				   (ext4_lblk_t)le32_to_cpu(newext->ee_block),
				    ppath, gb_flags);
		if (IS_ERR(path)) {
			err = PTR_ERR(path);
			goto out;
		}

		/*
		 * only first (depth 0 -> 1) produces free space;
		 * in all other cases we have to split the grown tree
		 */
		depth = ext_depth(inode);
		if (path[depth].p_hdr->eh_entries == path[depth].p_hdr->eh_max) {
			/* now we need to split */
			goto repeat;
		}
	}

out:
	return err;
}

/*
 * search the closest allocated block to the left for *logical
 * and returns it at @logical + it's physical address at @phys
 * if *logical is the smallest allocated block, the function
 * returns 0 at @phys
 * return value contains 0 (success) or error code
 */
static int ext4_ext_search_left(struct inode *inode,
				struct ext4_ext_path *path,
				ext4_lblk_t *logical, ext4_fsblk_t *phys)
{
	struct ext4_extent_idx *ix;
	struct ext4_extent *ex;
	int depth, ee_len;

	if (unlikely(path == NULL)) {
		EXT4_ERROR_INODE(inode, "path == NULL *logical %d!", *logical);
		return -EFSCORRUPTED;
	}
	depth = path->p_depth;
	*phys = 0;

	if (depth == 0 && path->p_ext == NULL)
		return 0;

	/* usually extent in the path covers blocks smaller
	 * then *logical, but it can be that extent is the
	 * first one in the file */

	ex = path[depth].p_ext;
	ee_len = ext4_ext_get_actual_len(ex);
	if (*logical < le32_to_cpu(ex->ee_block)) {
		if (unlikely(EXT_FIRST_EXTENT(path[depth].p_hdr) != ex)) {
			EXT4_ERROR_INODE(inode,
					 "EXT_FIRST_EXTENT != ex *logical %d ee_block %d!",
					 *logical, le32_to_cpu(ex->ee_block));
			return -EFSCORRUPTED;
		}
		while (--depth >= 0) {
			ix = path[depth].p_idx;
			if (unlikely(ix != EXT_FIRST_INDEX(path[depth].p_hdr))) {
				EXT4_ERROR_INODE(inode,
				  "ix (%d) != EXT_FIRST_INDEX (%d) (depth %d)!",
				  ix != NULL ? le32_to_cpu(ix->ei_block) : 0,
				  le32_to_cpu(EXT_FIRST_INDEX(path[depth].p_hdr)->ei_block),
				  depth);
				return -EFSCORRUPTED;
			}
		}
		return 0;
	}

	if (unlikely(*logical < (le32_to_cpu(ex->ee_block) + ee_len))) {
		EXT4_ERROR_INODE(inode,
				 "logical %d < ee_block %d + ee_len %d!",
				 *logical, le32_to_cpu(ex->ee_block), ee_len);
		return -EFSCORRUPTED;
	}

	*logical = le32_to_cpu(ex->ee_block) + ee_len - 1;
	*phys = ext4_ext_pblock(ex) + ee_len - 1;
	return 0;
}

/*
 * Search the closest allocated block to the right for *logical
 * and returns it at @logical + it's physical address at @phys.
 * If not exists, return 0 and @phys is set to 0. We will return
 * 1 which means we found an allocated block and ret_ex is valid.
 * Or return a (< 0) error code.
 */
static int ext4_ext_search_right(struct inode *inode,
				 struct ext4_ext_path *path,
				 ext4_lblk_t *logical, ext4_fsblk_t *phys,
				 struct ext4_extent *ret_ex)
{
	struct buffer_head *bh = NULL;
	struct ext4_extent_header *eh;
	struct ext4_extent_idx *ix;
	struct ext4_extent *ex;
	int depth;	/* Note, NOT eh_depth; depth from top of tree */
	int ee_len;

	if (unlikely(path == NULL)) {
		EXT4_ERROR_INODE(inode, "path == NULL *logical %d!", *logical);
		return -EFSCORRUPTED;
	}
	depth = path->p_depth;
	*phys = 0;

	if (depth == 0 && path->p_ext == NULL)
		return 0;

	/* usually extent in the path covers blocks smaller
	 * then *logical, but it can be that extent is the
	 * first one in the file */

	ex = path[depth].p_ext;
	ee_len = ext4_ext_get_actual_len(ex);
	if (*logical < le32_to_cpu(ex->ee_block)) {
		if (unlikely(EXT_FIRST_EXTENT(path[depth].p_hdr) != ex)) {
			EXT4_ERROR_INODE(inode,
					 "first_extent(path[%d].p_hdr) != ex",
					 depth);
			return -EFSCORRUPTED;
		}
		while (--depth >= 0) {
			ix = path[depth].p_idx;
			if (unlikely(ix != EXT_FIRST_INDEX(path[depth].p_hdr))) {
				EXT4_ERROR_INODE(inode,
						 "ix != EXT_FIRST_INDEX *logical %d!",
						 *logical);
				return -EFSCORRUPTED;
			}
		}
		goto found_extent;
	}

	if (unlikely(*logical < (le32_to_cpu(ex->ee_block) + ee_len))) {
		EXT4_ERROR_INODE(inode,
				 "logical %d < ee_block %d + ee_len %d!",
				 *logical, le32_to_cpu(ex->ee_block), ee_len);
		return -EFSCORRUPTED;
	}

	if (ex != EXT_LAST_EXTENT(path[depth].p_hdr)) {
		/* next allocated block in this leaf */
		ex++;
		goto found_extent;
	}

	/* go up and search for index to the right */
	while (--depth >= 0) {
		ix = path[depth].p_idx;
		if (ix != EXT_LAST_INDEX(path[depth].p_hdr))
			goto got_index;
	}

	/* we've gone up to the root and found no index to the right */
	return 0;

got_index:
	/* we've found index to the right, let's
	 * follow it and find the closest allocated
	 * block to the right */
	ix++;
	while (++depth < path->p_depth) {
		/* subtract from p_depth to get proper eh_depth */
		bh = read_extent_tree_block(inode, ix, path->p_depth - depth, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		eh = ext_block_hdr(bh);
		ix = EXT_FIRST_INDEX(eh);
		put_bh(bh);
	}

	bh = read_extent_tree_block(inode, ix, path->p_depth - depth, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	eh = ext_block_hdr(bh);
	ex = EXT_FIRST_EXTENT(eh);
found_extent:
	*logical = le32_to_cpu(ex->ee_block);
	*phys = ext4_ext_pblock(ex);
	if (ret_ex)
		*ret_ex = *ex;
	if (bh)
		put_bh(bh);
	return 1;
}

/*
 * ext4_ext_next_allocated_block:
 * returns allocated block in subsequent extent or EXT_MAX_BLOCKS.
 * NOTE: it considers block number from index entry as
 * allocated block. Thus, index entries have to be consistent
 * with leaves.
 */
ext4_lblk_t
ext4_ext_next_allocated_block(struct ext4_ext_path *path)
{
	int depth;

	BUG_ON(path == NULL);
	depth = path->p_depth;

	if (depth == 0 && path->p_ext == NULL)
		return EXT_MAX_BLOCKS;

	while (depth >= 0) {
		struct ext4_ext_path *p = &path[depth];

		if (depth == path->p_depth) {
			/* leaf */
			if (p->p_ext && p->p_ext != EXT_LAST_EXTENT(p->p_hdr))
				return le32_to_cpu(p->p_ext[1].ee_block);
		} else {
			/* index */
			if (p->p_idx != EXT_LAST_INDEX(p->p_hdr))
				return le32_to_cpu(p->p_idx[1].ei_block);
		}
		depth--;
	}

	return EXT_MAX_BLOCKS;
}

/*
 * ext4_ext_next_leaf_block:
 * returns first allocated block from next leaf or EXT_MAX_BLOCKS
 */
static ext4_lblk_t ext4_ext_next_leaf_block(struct ext4_ext_path *path)
{
	int depth;

	BUG_ON(path == NULL);
	depth = path->p_depth;

	/* zero-tree has no leaf blocks at all */
	if (depth == 0)
		return EXT_MAX_BLOCKS;

	/* go to index block */
	depth--;

	while (depth >= 0) {
		if (path[depth].p_idx !=
				EXT_LAST_INDEX(path[depth].p_hdr))
			return (ext4_lblk_t)
				le32_to_cpu(path[depth].p_idx[1].ei_block);
		depth--;
	}

	return EXT_MAX_BLOCKS;
}

/*
 * ext4_ext_correct_indexes:
 * if leaf gets modified and modified extent is first in the leaf,
 * then we have to correct all indexes above.
 * TODO: do we need to correct tree in all cases?
 */
static int ext4_ext_correct_indexes(handle_t *handle, struct inode *inode,
				struct ext4_ext_path *path)
{
	struct ext4_extent_header *eh;
	int depth = ext_depth(inode);
	struct ext4_extent *ex;
	__le32 border;
	int k, err = 0;

	eh = path[depth].p_hdr;
	ex = path[depth].p_ext;

	if (unlikely(ex == NULL || eh == NULL)) {
		EXT4_ERROR_INODE(inode,
				 "ex %p == NULL or eh %p == NULL", ex, eh);
		return -EFSCORRUPTED;
	}

	if (depth == 0) {
		/* there is no tree at all */
		return 0;
	}

	if (ex != EXT_FIRST_EXTENT(eh)) {
		/* we correct tree if first leaf got modified only */
		return 0;
	}

	/*
	 * TODO: we need correction if border is smaller than current one
	 */
	k = depth - 1;
	border = path[depth].p_ext->ee_block;
	err = ext4_ext_get_access(handle, inode, path + k);
	if (err)
		return err;
	path[k].p_idx->ei_block = border;
	err = ext4_ext_dirty(handle, inode, path + k);
	if (err)
		return err;

	while (k--) {
		/* change all left-side indexes */
		if (path[k+1].p_idx != EXT_FIRST_INDEX(path[k+1].p_hdr))
			break;
		err = ext4_ext_get_access(handle, inode, path + k);
		if (err)
			break;
		path[k].p_idx->ei_block = border;
		err = ext4_ext_dirty(handle, inode, path + k);
		if (err)
			break;
	}

	return err;
}

static int ext4_can_extents_be_merged(struct inode *inode,
				      struct ext4_extent *ex1,
				      struct ext4_extent *ex2)
{
	unsigned short ext1_ee_len, ext2_ee_len;

	if (ext4_ext_is_unwritten(ex1) != ext4_ext_is_unwritten(ex2))
		return 0;

	ext1_ee_len = ext4_ext_get_actual_len(ex1);
	ext2_ee_len = ext4_ext_get_actual_len(ex2);

	if (le32_to_cpu(ex1->ee_block) + ext1_ee_len !=
			le32_to_cpu(ex2->ee_block))
		return 0;

	if (ext1_ee_len + ext2_ee_len > EXT_INIT_MAX_LEN)
		return 0;

	if (ext4_ext_is_unwritten(ex1) &&
	    ext1_ee_len + ext2_ee_len > EXT_UNWRITTEN_MAX_LEN)
		return 0;
#ifdef AGGRESSIVE_TEST
	if (ext1_ee_len >= 4)
		return 0;
#endif

	if (ext4_ext_pblock(ex1) + ext1_ee_len == ext4_ext_pblock(ex2))
		return 1;
	return 0;
}

/*
 * This function tries to merge the "ex" extent to the next extent in the tree.
 * It always tries to merge towards right. If you want to merge towards
 * left, pass "ex - 1" as argument instead of "ex".
 * Returns 0 if the extents (ex and ex+1) were _not_ merged and returns
 * 1 if they got merged.
 */
static int ext4_ext_try_to_merge_right(struct inode *inode,
				 struct ext4_ext_path *path,
				 struct ext4_extent *ex)
{
	struct ext4_extent_header *eh;
	unsigned int depth, len;
	int merge_done = 0, unwritten;

	depth = ext_depth(inode);
	BUG_ON(path[depth].p_hdr == NULL);
	eh = path[depth].p_hdr;

	while (ex < EXT_LAST_EXTENT(eh)) {
		if (!ext4_can_extents_be_merged(inode, ex, ex + 1))
			break;
		/* merge with next extent! */
		unwritten = ext4_ext_is_unwritten(ex);
		ex->ee_len = cpu_to_le16(ext4_ext_get_actual_len(ex)
				+ ext4_ext_get_actual_len(ex + 1));
		if (unwritten)
			ext4_ext_mark_unwritten(ex);

		if (ex + 1 < EXT_LAST_EXTENT(eh)) {
			len = (EXT_LAST_EXTENT(eh) - ex - 1)
				* sizeof(struct ext4_extent);
			memmove(ex + 1, ex + 2, len);
		}
		le16_add_cpu(&eh->eh_entries, -1);
		merge_done = 1;
		WARN_ON(eh->eh_entries == 0);
		if (!eh->eh_entries)
			EXT4_ERROR_INODE(inode, "eh->eh_entries = 0!");
	}

	return merge_done;
}

/*
 * This function does a very simple check to see if we can collapse
 * an extent tree with a single extent tree leaf block into the inode.
 */
static void ext4_ext_try_to_merge_up(handle_t *handle,
				     struct inode *inode,
				     struct ext4_ext_path *path)
{
	size_t s;
	unsigned max_root = ext4_ext_space_root(inode, 0);
	ext4_fsblk_t blk;

	if ((path[0].p_depth != 1) ||
	    (le16_to_cpu(path[0].p_hdr->eh_entries) != 1) ||
	    (le16_to_cpu(path[1].p_hdr->eh_entries) > max_root))
		return;

	/*
	 * We need to modify the block allocation bitmap and the block
	 * group descriptor to release the extent tree block.  If we
	 * can't get the journal credits, give up.
	 */
	if (ext4_journal_extend(handle, 2,
			ext4_free_metadata_revoke_credits(inode->i_sb, 1)))
		return;

	/*
	 * Copy the extent data up to the inode
	 */
	blk = ext4_idx_pblock(path[0].p_idx);
	s = le16_to_cpu(path[1].p_hdr->eh_entries) *
		sizeof(struct ext4_extent_idx);
	s += sizeof(struct ext4_extent_header);

	path[1].p_maxdepth = path[0].p_maxdepth;
	memcpy(path[0].p_hdr, path[1].p_hdr, s);
	path[0].p_depth = 0;
	path[0].p_ext = EXT_FIRST_EXTENT(path[0].p_hdr) +
		(path[1].p_ext - EXT_FIRST_EXTENT(path[1].p_hdr));
	path[0].p_hdr->eh_max = cpu_to_le16(max_root);

	brelse(path[1].p_bh);
	ext4_free_blocks(handle, inode, NULL, blk, 1,
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_FORGET);
}

/*
 * This function tries to merge the @ex extent to neighbours in the tree, then
 * tries to collapse the extent tree into the inode.
 */
static void ext4_ext_try_to_merge(handle_t *handle,
				  struct inode *inode,
				  struct ext4_ext_path *path,
				  struct ext4_extent *ex)
{
	struct ext4_extent_header *eh;
	unsigned int depth;
	int merge_done = 0;

	depth = ext_depth(inode);
	BUG_ON(path[depth].p_hdr == NULL);
	eh = path[depth].p_hdr;

	if (ex > EXT_FIRST_EXTENT(eh))
		merge_done = ext4_ext_try_to_merge_right(inode, path, ex - 1);

	if (!merge_done)
		(void) ext4_ext_try_to_merge_right(inode, path, ex);

	ext4_ext_try_to_merge_up(handle, inode, path);
}

/*
 * check if a portion of the "newext" extent overlaps with an
 * existing extent.
 *
 * If there is an overlap discovered, it updates the length of the newext
 * such that there will be no overlap, and then returns 1.
 * If there is no overlap found, it returns 0.
 */
static unsigned int ext4_ext_check_overlap(struct ext4_sb_info *sbi,
					   struct inode *inode,
					   struct ext4_extent *newext,
					   struct ext4_ext_path *path)
{
	ext4_lblk_t b1, b2;
	unsigned int depth, len1;
	unsigned int ret = 0;

	b1 = le32_to_cpu(newext->ee_block);
	len1 = ext4_ext_get_actual_len(newext);
	depth = ext_depth(inode);
	if (!path[depth].p_ext)
		goto out;
	b2 = EXT4_LBLK_CMASK(sbi, le32_to_cpu(path[depth].p_ext->ee_block));

	/*
	 * get the next allocated block if the extent in the path
	 * is before the requested block(s)
	 */
	if (b2 < b1) {
		b2 = ext4_ext_next_allocated_block(path);
		if (b2 == EXT_MAX_BLOCKS)
			goto out;
		b2 = EXT4_LBLK_CMASK(sbi, b2);
	}

	/* check for wrap through zero on extent logical start block*/
	if (b1 + len1 < b1) {
		len1 = EXT_MAX_BLOCKS - b1;
		newext->ee_len = cpu_to_le16(len1);
		ret = 1;
	}

	/* check for overlap */
	if (b1 + len1 > b2) {
		newext->ee_len = cpu_to_le16(b2 - b1);
		ret = 1;
	}
out:
	return ret;
}

/*
 * ext4_ext_insert_extent:
 * tries to merge requested extent into the existing extent or
 * inserts requested extent as new one into the tree,
 * creating new leaf in the no-space case.
 */
int ext4_ext_insert_extent(handle_t *handle, struct inode *inode,
				struct ext4_ext_path **ppath,
				struct ext4_extent *newext, int gb_flags)
{
	struct ext4_ext_path *path = *ppath;
	struct ext4_extent_header *eh;
	struct ext4_extent *ex, *fex;
	struct ext4_extent *nearex; /* nearest extent */
	struct ext4_ext_path *npath = NULL;
	int depth, len, err;
	ext4_lblk_t next;
	int mb_flags = 0, unwritten;

	if (gb_flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		mb_flags |= EXT4_MB_DELALLOC_RESERVED;
	if (unlikely(ext4_ext_get_actual_len(newext) == 0)) {
		EXT4_ERROR_INODE(inode, "ext4_ext_get_actual_len(newext) == 0");
		return -EFSCORRUPTED;
	}
	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	eh = path[depth].p_hdr;
	if (unlikely(path[depth].p_hdr == NULL)) {
		EXT4_ERROR_INODE(inode, "path[%d].p_hdr == NULL", depth);
		return -EFSCORRUPTED;
	}

	/* try to insert block into found extent and return */
	if (ex && !(gb_flags & EXT4_GET_BLOCKS_PRE_IO)) {

		/*
		 * Try to see whether we should rather test the extent on
		 * right from ex, or from the left of ex. This is because
		 * ext4_find_extent() can return either extent on the
		 * left, or on the right from the searched position. This
		 * will make merging more effective.
		 */
		if (ex < EXT_LAST_EXTENT(eh) &&
		    (le32_to_cpu(ex->ee_block) +
		    ext4_ext_get_actual_len(ex) <
		    le32_to_cpu(newext->ee_block))) {
			ex += 1;
			goto prepend;
		} else if ((ex > EXT_FIRST_EXTENT(eh)) &&
			   (le32_to_cpu(newext->ee_block) +
			   ext4_ext_get_actual_len(newext) <
			   le32_to_cpu(ex->ee_block)))
			ex -= 1;

		/* Try to append newex to the ex */
		if (ext4_can_extents_be_merged(inode, ex, newext)) {
			ext_debug(inode, "append [%d]%d block to %u:[%d]%d"
				  "(from %llu)\n",
				  ext4_ext_is_unwritten(newext),
				  ext4_ext_get_actual_len(newext),
				  le32_to_cpu(ex->ee_block),
				  ext4_ext_is_unwritten(ex),
				  ext4_ext_get_actual_len(ex),
				  ext4_ext_pblock(ex));
			err = ext4_ext_get_access(handle, inode,
						  path + depth);
			if (err)
				return err;
			unwritten = ext4_ext_is_unwritten(ex);
			ex->ee_len = cpu_to_le16(ext4_ext_get_actual_len(ex)
					+ ext4_ext_get_actual_len(newext));
			if (unwritten)
				ext4_ext_mark_unwritten(ex);
			nearex = ex;
			goto merge;
		}

prepend:
		/* Try to prepend newex to the ex */
		if (ext4_can_extents_be_merged(inode, newext, ex)) {
			ext_debug(inode, "prepend %u[%d]%d block to %u:[%d]%d"
				  "(from %llu)\n",
				  le32_to_cpu(newext->ee_block),
				  ext4_ext_is_unwritten(newext),
				  ext4_ext_get_actual_len(newext),
				  le32_to_cpu(ex->ee_block),
				  ext4_ext_is_unwritten(ex),
				  ext4_ext_get_actual_len(ex),
				  ext4_ext_pblock(ex));
			err = ext4_ext_get_access(handle, inode,
						  path + depth);
			if (err)
				return err;

			unwritten = ext4_ext_is_unwritten(ex);
			ex->ee_block = newext->ee_block;
			ext4_ext_store_pblock(ex, ext4_ext_pblock(newext));
			ex->ee_len = cpu_to_le16(ext4_ext_get_actual_len(ex)
					+ ext4_ext_get_actual_len(newext));
			if (unwritten)
				ext4_ext_mark_unwritten(ex);
			nearex = ex;
			goto merge;
		}
	}

	depth = ext_depth(inode);
	eh = path[depth].p_hdr;
	if (le16_to_cpu(eh->eh_entries) < le16_to_cpu(eh->eh_max))
		goto has_space;

	/* probably next leaf has space for us? */
	fex = EXT_LAST_EXTENT(eh);
	next = EXT_MAX_BLOCKS;
	if (le32_to_cpu(newext->ee_block) > le32_to_cpu(fex->ee_block))
		next = ext4_ext_next_leaf_block(path);
	if (next != EXT_MAX_BLOCKS) {
		ext_debug(inode, "next leaf block - %u\n", next);
		BUG_ON(npath != NULL);
		npath = ext4_find_extent(inode, next, NULL, gb_flags);
		if (IS_ERR(npath))
			return PTR_ERR(npath);
		BUG_ON(npath->p_depth != path->p_depth);
		eh = npath[depth].p_hdr;
		if (le16_to_cpu(eh->eh_entries) < le16_to_cpu(eh->eh_max)) {
			ext_debug(inode, "next leaf isn't full(%d)\n",
				  le16_to_cpu(eh->eh_entries));
			path = npath;
			goto has_space;
		}
		ext_debug(inode, "next leaf has no free space(%d,%d)\n",
			  le16_to_cpu(eh->eh_entries), le16_to_cpu(eh->eh_max));
	}

	/*
	 * There is no free space in the found leaf.
	 * We're gonna add a new leaf in the tree.
	 */
	if (gb_flags & EXT4_GET_BLOCKS_METADATA_NOFAIL)
		mb_flags |= EXT4_MB_USE_RESERVED;
	err = ext4_ext_create_new_leaf(handle, inode, mb_flags, gb_flags,
				       ppath, newext);
	if (err)
		goto cleanup;
	depth = ext_depth(inode);
	eh = path[depth].p_hdr;

has_space:
	nearex = path[depth].p_ext;

	err = ext4_ext_get_access(handle, inode, path + depth);
	if (err)
		goto cleanup;

	if (!nearex) {
		/* there is no extent in this leaf, create first one */
		ext_debug(inode, "first extent in the leaf: %u:%llu:[%d]%d\n",
				le32_to_cpu(newext->ee_block),
				ext4_ext_pblock(newext),
				ext4_ext_is_unwritten(newext),
				ext4_ext_get_actual_len(newext));
		nearex = EXT_FIRST_EXTENT(eh);
	} else {
		if (le32_to_cpu(newext->ee_block)
			   > le32_to_cpu(nearex->ee_block)) {
			/* Insert after */
			ext_debug(inode, "insert %u:%llu:[%d]%d before: "
					"nearest %p\n",
					le32_to_cpu(newext->ee_block),
					ext4_ext_pblock(newext),
					ext4_ext_is_unwritten(newext),
					ext4_ext_get_actual_len(newext),
					nearex);
			nearex++;
		} else {
			/* Insert before */
			BUG_ON(newext->ee_block == nearex->ee_block);
			ext_debug(inode, "insert %u:%llu:[%d]%d after: "
					"nearest %p\n",
					le32_to_cpu(newext->ee_block),
					ext4_ext_pblock(newext),
					ext4_ext_is_unwritten(newext),
					ext4_ext_get_actual_len(newext),
					nearex);
		}
		len = EXT_LAST_EXTENT(eh) - nearex + 1;
		if (len > 0) {
			ext_debug(inode, "insert %u:%llu:[%d]%d: "
					"move %d extents from 0x%p to 0x%p\n",
					le32_to_cpu(newext->ee_block),
					ext4_ext_pblock(newext),
					ext4_ext_is_unwritten(newext),
					ext4_ext_get_actual_len(newext),
					len, nearex, nearex + 1);
			memmove(nearex + 1, nearex,
				len * sizeof(struct ext4_extent));
		}
	}

	le16_add_cpu(&eh->eh_entries, 1);
	path[depth].p_ext = nearex;
	nearex->ee_block = newext->ee_block;
	ext4_ext_store_pblock(nearex, ext4_ext_pblock(newext));
	nearex->ee_len = newext->ee_len;

merge:
	/* try to merge extents */
	if (!(gb_flags & EXT4_GET_BLOCKS_PRE_IO))
		ext4_ext_try_to_merge(handle, inode, path, nearex);


	/* time to correct all indexes above */
	err = ext4_ext_correct_indexes(handle, inode, path);
	if (err)
		goto cleanup;

	err = ext4_ext_dirty(handle, inode, path + path->p_depth);

cleanup:
	ext4_free_ext_path(npath);
	return err;
}

static int ext4_fill_es_cache_info(struct inode *inode,
				   ext4_lblk_t block, ext4_lblk_t num,
				   struct fiemap_extent_info *fieinfo)
{
	ext4_lblk_t next, end = block + num - 1;
	struct extent_status es;
	unsigned char blksize_bits = inode->i_sb->s_blocksize_bits;
	unsigned int flags;
	int err;

	while (block <= end) {
		next = 0;
		flags = 0;
		if (!ext4_es_lookup_extent(inode, block, &next, &es))
			break;
		if (ext4_es_is_unwritten(&es))
			flags |= FIEMAP_EXTENT_UNWRITTEN;
		if (ext4_es_is_delayed(&es))
			flags |= (FIEMAP_EXTENT_DELALLOC |
				  FIEMAP_EXTENT_UNKNOWN);
		if (ext4_es_is_hole(&es))
			flags |= EXT4_FIEMAP_EXTENT_HOLE;
		if (next == 0)
			flags |= FIEMAP_EXTENT_LAST;
		if (flags & (FIEMAP_EXTENT_DELALLOC|
			     EXT4_FIEMAP_EXTENT_HOLE))
			es.es_pblk = 0;
		else
			es.es_pblk = ext4_es_pblock(&es);
		err = fiemap_fill_next_extent(fieinfo,
				(__u64)es.es_lblk << blksize_bits,
				(__u64)es.es_pblk << blksize_bits,
				(__u64)es.es_len << blksize_bits,
				flags);
		if (next == 0)
			break;
		block = next;
		if (err < 0)
			return err;
		if (err == 1)
			return 0;
	}
	return 0;
}


/*
 * ext4_ext_determine_hole - determine hole around given block
 * @inode:	inode we lookup in
 * @path:	path in extent tree to @lblk
 * @lblk:	pointer to logical block around which we want to determine hole
 *
 * Determine hole length (and start if easily possible) around given logical
 * block. We don't try too hard to find the beginning of the hole but @path
 * actually points to extent before @lblk, we provide it.
 *
 * The function returns the length of a hole starting at @lblk. We update @lblk
 * to the beginning of the hole if we managed to find it.
 */
static ext4_lblk_t ext4_ext_determine_hole(struct inode *inode,
					   struct ext4_ext_path *path,
					   ext4_lblk_t *lblk)
{
	int depth = ext_depth(inode);
	struct ext4_extent *ex;
	ext4_lblk_t len;

	ex = path[depth].p_ext;
	if (ex == NULL) {
		/* there is no extent yet, so gap is [0;-] */
		*lblk = 0;
		len = EXT_MAX_BLOCKS;
	} else if (*lblk < le32_to_cpu(ex->ee_block)) {
		len = le32_to_cpu(ex->ee_block) - *lblk;
	} else if (*lblk >= le32_to_cpu(ex->ee_block)
			+ ext4_ext_get_actual_len(ex)) {
		ext4_lblk_t next;

		*lblk = le32_to_cpu(ex->ee_block) + ext4_ext_get_actual_len(ex);
		next = ext4_ext_next_allocated_block(path);
		BUG_ON(next == *lblk);
		len = next - *lblk;
	} else {
		BUG();
	}
	return len;
}

/*
 * ext4_ext_put_gap_in_cache:
 * calculate boundaries of the gap that the requested block fits into
 * and cache this gap
 */
static void
ext4_ext_put_gap_in_cache(struct inode *inode, ext4_lblk_t hole_start,
			  ext4_lblk_t hole_len)
{
	struct extent_status es;

	ext4_es_find_extent_range(inode, &ext4_es_is_delayed, hole_start,
				  hole_start + hole_len - 1, &es);
	if (es.es_len) {
		/* There's delayed extent containing lblock? */
		if (es.es_lblk <= hole_start)
			return;
		hole_len = min(es.es_lblk - hole_start, hole_len);
	}
	ext_debug(inode, " -> %u:%u\n", hole_start, hole_len);
	ext4_es_insert_extent(inode, hole_start, hole_len, ~0,
			      EXTENT_STATUS_HOLE);
}

/*
 * ext4_ext_rm_idx:
 * removes index from the index block.
 */
static int ext4_ext_rm_idx(handle_t *handle, struct inode *inode,
			struct ext4_ext_path *path, int depth)
{
	int err;
	ext4_fsblk_t leaf;

	/* free index block */
	depth--;
	path = path + depth;
	leaf = ext4_idx_pblock(path->p_idx);
	if (unlikely(path->p_hdr->eh_entries == 0)) {
		EXT4_ERROR_INODE(inode, "path->p_hdr->eh_entries == 0");
		return -EFSCORRUPTED;
	}
	err = ext4_ext_get_access(handle, inode, path);
	if (err)
		return err;

	if (path->p_idx != EXT_LAST_INDEX(path->p_hdr)) {
		int len = EXT_LAST_INDEX(path->p_hdr) - path->p_idx;
		len *= sizeof(struct ext4_extent_idx);
		memmove(path->p_idx, path->p_idx + 1, len);
	}

	le16_add_cpu(&path->p_hdr->eh_entries, -1);
	err = ext4_ext_dirty(handle, inode, path);
	if (err)
		return err;
	ext_debug(inode, "index is empty, remove it, free block %llu\n", leaf);
	trace_ext4_ext_rm_idx(inode, leaf);

	ext4_free_blocks(handle, inode, NULL, leaf, 1,
			 EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_FORGET);

	while (--depth >= 0) {
		if (path->p_idx != EXT_FIRST_INDEX(path->p_hdr))
			break;
		path--;
		err = ext4_ext_get_access(handle, inode, path);
		if (err)
			break;
		path->p_idx->ei_block = (path+1)->p_idx->ei_block;
		err = ext4_ext_dirty(handle, inode, path);
		if (err)
			break;
	}
	return err;
}

/*
 * ext4_ext_calc_credits_for_single_extent:
 * This routine returns max. credits that needed to insert an extent
 * to the extent tree.
 * When pass the actual path, the caller should calculate credits
 * under i_data_sem.
 */
int ext4_ext_calc_credits_for_single_extent(struct inode *inode, int nrblocks,
						struct ext4_ext_path *path)
{
	if (path) {
		int depth = ext_depth(inode);
		int ret = 0;

		/* probably there is space in leaf? */
		if (le16_to_cpu(path[depth].p_hdr->eh_entries)
				< le16_to_cpu(path[depth].p_hdr->eh_max)) {

			/*
			 *  There are some space in the leaf tree, no
			 *  need to account for leaf block credit
			 *
			 *  bitmaps and block group descriptor blocks
			 *  and other metadata blocks still need to be
			 *  accounted.
			 */
			/* 1 bitmap, 1 block group descriptor */
			ret = 2 + EXT4_META_TRANS_BLOCKS(inode->i_sb);
			return ret;
		}
	}

	return ext4_chunk_trans_blocks(inode, nrblocks);
}

/*
 * How many index/leaf blocks need to change/allocate to add @extents extents?
 *
 * If we add a single extent, then in the worse case, each tree level
 * index/leaf need to be changed in case of the tree split.
 *
 * If more extents are inserted, they could cause the whole tree split more
 * than once, but this is really rare.
 */
int ext4_ext_index_trans_blocks(struct inode *inode, int extents)
{
	int index;
	int depth;

	/* If we are converting the inline data, only one is needed here. */
	if (ext4_has_inline_data(inode))
		return 1;

	depth = ext_depth(inode);

	if (extents <= 1)
		index = depth * 2;
	else
		index = depth * 3;

	return index;
}

static inline int get_default_free_blocks_flags(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode) ||
	    ext4_test_inode_flag(inode, EXT4_INODE_EA_INODE))
		return EXT4_FREE_BLOCKS_METADATA | EXT4_FREE_BLOCKS_FORGET;
	else if (ext4_should_journal_data(inode))
		return EXT4_FREE_BLOCKS_FORGET;
	return 0;
}

/*
 * ext4_rereserve_cluster - increment the reserved cluster count when
 *                          freeing a cluster with a pending reservation
 *
 * @inode - file containing the cluster
 * @lblk - logical block in cluster to be reserved
 *
 * Increments the reserved cluster count and adjusts quota in a bigalloc
 * file system when freeing a partial cluster containing at least one
 * delayed and unwritten block.  A partial cluster meeting that
 * requirement will have a pending reservation.  If so, the
 * RERESERVE_CLUSTER flag is used when calling ext4_free_blocks() to
 * defer reserved and allocated space accounting to a subsequent call
 * to this function.
 */
static void ext4_rereserve_cluster(struct inode *inode, ext4_lblk_t lblk)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);

	dquot_reclaim_block(inode, EXT4_C2B(sbi, 1));

	spin_lock(&ei->i_block_reservation_lock);
	ei->i_reserved_data_blocks++;
	percpu_counter_add(&sbi->s_dirtyclusters_counter, 1);
	spin_unlock(&ei->i_block_reservation_lock);

	percpu_counter_add(&sbi->s_freeclusters_counter, 1);
	ext4_remove_pending(inode, lblk);
}

static int ext4_remove_blocks(handle_t *handle, struct inode *inode,
			      struct ext4_extent *ex,
			      struct partial_cluster *partial,
			      ext4_lblk_t from, ext4_lblk_t to)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	unsigned short ee_len = ext4_ext_get_actual_len(ex);
	ext4_fsblk_t last_pblk, pblk;
	ext4_lblk_t num;
	int flags;

	/* only extent tail removal is allowed */
	if (from < le32_to_cpu(ex->ee_block) ||
	    to != le32_to_cpu(ex->ee_block) + ee_len - 1) {
		ext4_error(sbi->s_sb,
			   "strange request: removal(2) %u-%u from %u:%u",
			   from, to, le32_to_cpu(ex->ee_block), ee_len);
		return 0;
	}

#ifdef EXTENTS_STATS
	spin_lock(&sbi->s_ext_stats_lock);
	sbi->s_ext_blocks += ee_len;
	sbi->s_ext_extents++;
	if (ee_len < sbi->s_ext_min)
		sbi->s_ext_min = ee_len;
	if (ee_len > sbi->s_ext_max)
		sbi->s_ext_max = ee_len;
	if (ext_depth(inode) > sbi->s_depth_max)
		sbi->s_depth_max = ext_depth(inode);
	spin_unlock(&sbi->s_ext_stats_lock);
#endif

	trace_ext4_remove_blocks(inode, ex, from, to, partial);

	/*
	 * if we have a partial cluster, and it's different from the
	 * cluster of the last block in the extent, we free it
	 */
	last_pblk = ext4_ext_pblock(ex) + ee_len - 1;

	if (partial->state != initial &&
	    partial->pclu != EXT4_B2C(sbi, last_pblk)) {
		if (partial->state == tofree) {
			flags = get_default_free_blocks_flags(inode);
			if (ext4_is_pending(inode, partial->lblk))
				flags |= EXT4_FREE_BLOCKS_RERESERVE_CLUSTER;
			ext4_free_blocks(handle, inode, NULL,
					 EXT4_C2B(sbi, partial->pclu),
					 sbi->s_cluster_ratio, flags);
			if (flags & EXT4_FREE_BLOCKS_RERESERVE_CLUSTER)
				ext4_rereserve_cluster(inode, partial->lblk);
		}
		partial->state = initial;
	}

	num = le32_to_cpu(ex->ee_block) + ee_len - from;
	pblk = ext4_ext_pblock(ex) + ee_len - num;

	/*
	 * We free the partial cluster at the end of the extent (if any),
	 * unless the cluster is used by another extent (partial_cluster
	 * state is nofree).  If a partial cluster exists here, it must be
	 * shared with the last block in the extent.
	 */
	flags = get_default_free_blocks_flags(inode);

	/* partial, left end cluster aligned, right end unaligned */
	if ((EXT4_LBLK_COFF(sbi, to) != sbi->s_cluster_ratio - 1) &&
	    (EXT4_LBLK_CMASK(sbi, to) >= from) &&
	    (partial->state != nofree)) {
		if (ext4_is_pending(inode, to))
			flags |= EXT4_FREE_BLOCKS_RERESERVE_CLUSTER;
		ext4_free_blocks(handle, inode, NULL,
				 EXT4_PBLK_CMASK(sbi, last_pblk),
				 sbi->s_cluster_ratio, flags);
		if (flags & EXT4_FREE_BLOCKS_RERESERVE_CLUSTER)
			ext4_rereserve_cluster(inode, to);
		partial->state = initial;
		flags = get_default_free_blocks_flags(inode);
	}

	flags |= EXT4_FREE_BLOCKS_NOFREE_LAST_CLUSTER;

	/*
	 * For bigalloc file systems, we never free a partial cluster
	 * at the beginning of the extent.  Instead, we check to see if we
	 * need to free it on a subsequent call to ext4_remove_blocks,
	 * or at the end of ext4_ext_rm_leaf or ext4_ext_remove_space.
	 */
	flags |= EXT4_FREE_BLOCKS_NOFREE_FIRST_CLUSTER;
	ext4_free_blocks(handle, inode, NULL, pblk, num, flags);

	/* reset the partial cluster if we've freed past it */
	if (partial->state != initial && partial->pclu != EXT4_B2C(sbi, pblk))
		partial->state = initial;

	/*
	 * If we've freed the entire extent but the beginning is not left
	 * cluster aligned and is not marked as ineligible for freeing we
	 * record the partial cluster at the beginning of the extent.  It
	 * wasn't freed by the preceding ext4_free_blocks() call, and we
	 * need to look farther to the left to determine if it's to be freed
	 * (not shared with another extent). Else, reset the partial
	 * cluster - we're either  done freeing or the beginning of the
	 * extent is left cluster aligned.
	 */
	if (EXT4_LBLK_COFF(sbi, from) && num == ee_len) {
		if (partial->state == initial) {
			partial->pclu = EXT4_B2C(sbi, pblk);
			partial->lblk = from;
			partial->state = tofree;
		}
	} else {
		partial->state = initial;
	}

	return 0;
}

/*
 * ext4_ext_rm_leaf() Removes the extents associated with the
 * blocks appearing between "start" and "end".  Both "start"
 * and "end" must appear in the same extent or EIO is returned.
 *
 * @handle: The journal handle
 * @inode:  The files inode
 * @path:   The path to the leaf
 * @partial_cluster: The cluster which we'll have to free if all extents
 *                   has been released from it.  However, if this value is
 *                   negative, it's a cluster just to the right of the
 *                   punched region and it must not be freed.
 * @start:  The first block to remove
 * @end:   The last block to remove
 */
static int
ext4_ext_rm_leaf(handle_t *handle, struct inode *inode,
		 struct ext4_ext_path *path,
		 struct partial_cluster *partial,
		 ext4_lblk_t start, ext4_lblk_t end)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	int err = 0, correct_index = 0;
	int depth = ext_depth(inode), credits, revoke_credits;
	struct ext4_extent_header *eh;
	ext4_lblk_t a, b;
	unsigned num;
	ext4_lblk_t ex_ee_block;
	unsigned short ex_ee_len;
	unsigned unwritten = 0;
	struct ext4_extent *ex;
	ext4_fsblk_t pblk;

	/* the header must be checked already in ext4_ext_remove_space() */
	ext_debug(inode, "truncate since %u in leaf to %u\n", start, end);
	if (!path[depth].p_hdr)
		path[depth].p_hdr = ext_block_hdr(path[depth].p_bh);
	eh = path[depth].p_hdr;
	if (unlikely(path[depth].p_hdr == NULL)) {
		EXT4_ERROR_INODE(inode, "path[%d].p_hdr == NULL", depth);
		return -EFSCORRUPTED;
	}
	/* find where to start removing */
	ex = path[depth].p_ext;
	if (!ex)
		ex = EXT_LAST_EXTENT(eh);

	ex_ee_block = le32_to_cpu(ex->ee_block);
	ex_ee_len = ext4_ext_get_actual_len(ex);

	trace_ext4_ext_rm_leaf(inode, start, ex, partial);

	while (ex >= EXT_FIRST_EXTENT(eh) &&
			ex_ee_block + ex_ee_len > start) {

		if (ext4_ext_is_unwritten(ex))
			unwritten = 1;
		else
			unwritten = 0;

		ext_debug(inode, "remove ext %u:[%d]%d\n", ex_ee_block,
			  unwritten, ex_ee_len);
		path[depth].p_ext = ex;

		a = max(ex_ee_block, start);
		b = min(ex_ee_block + ex_ee_len - 1, end);

		ext_debug(inode, "  border %u:%u\n", a, b);

		/* If this extent is beyond the end of the hole, skip it */
		if (end < ex_ee_block) {
			/*
			 * We're going to skip this extent and move to another,
			 * so note that its first cluster is in use to avoid
			 * freeing it when removing blocks.  Eventually, the
			 * right edge of the truncated/punched region will
			 * be just to the left.
			 */
			if (sbi->s_cluster_ratio > 1) {
				pblk = ext4_ext_pblock(ex);
				partial->pclu = EXT4_B2C(sbi, pblk);
				partial->state = nofree;
			}
			ex--;
			ex_ee_block = le32_to_cpu(ex->ee_block);
			ex_ee_len = ext4_ext_get_actual_len(ex);
			continue;
		} else if (b != ex_ee_block + ex_ee_len - 1) {
			EXT4_ERROR_INODE(inode,
					 "can not handle truncate %u:%u "
					 "on extent %u:%u",
					 start, end, ex_ee_block,
					 ex_ee_block + ex_ee_len - 1);
			err = -EFSCORRUPTED;
			goto out;
		} else if (a != ex_ee_block) {
			/* remove tail of the extent */
			num = a - ex_ee_block;
		} else {
			/* remove whole extent: excellent! */
			num = 0;
		}
		/*
		 * 3 for leaf, sb, and inode plus 2 (bmap and group
		 * descriptor) for each block group; assume two block
		 * groups plus ex_ee_len/blocks_per_block_group for
		 * the worst case
		 */
		credits = 7 + 2*(ex_ee_len/EXT4_BLOCKS_PER_GROUP(inode->i_sb));
		if (ex == EXT_FIRST_EXTENT(eh)) {
			correct_index = 1;
			credits += (ext_depth(inode)) + 1;
		}
		credits += EXT4_MAXQUOTAS_TRANS_BLOCKS(inode->i_sb);
		/*
		 * We may end up freeing some index blocks and data from the
		 * punched range. Note that partial clusters are accounted for
		 * by ext4_free_data_revoke_credits().
		 */
		revoke_credits =
			ext4_free_metadata_revoke_credits(inode->i_sb,
							  ext_depth(inode)) +
			ext4_free_data_revoke_credits(inode, b - a + 1);

		err = ext4_datasem_ensure_credits(handle, inode, credits,
						  credits, revoke_credits);
		if (err) {
			if (err > 0)
				err = -EAGAIN;
			goto out;
		}

		err = ext4_ext_get_access(handle, inode, path + depth);
		if (err)
			goto out;

		err = ext4_remove_blocks(handle, inode, ex, partial, a, b);
		if (err)
			goto out;

		if (num == 0)
			/* this extent is removed; mark slot entirely unused */
			ext4_ext_store_pblock(ex, 0);

		ex->ee_len = cpu_to_le16(num);
		/*
		 * Do not mark unwritten if all the blocks in the
		 * extent have been removed.
		 */
		if (unwritten && num)
			ext4_ext_mark_unwritten(ex);
		/*
		 * If the extent was completely released,
		 * we need to remove it from the leaf
		 */
		if (num == 0) {
			if (end != EXT_MAX_BLOCKS - 1) {
				/*
				 * For hole punching, we need to scoot all the
				 * extents up when an extent is removed so that
				 * we dont have blank extents in the middle
				 */
				memmove(ex, ex+1, (EXT_LAST_EXTENT(eh) - ex) *
					sizeof(struct ext4_extent));

				/* Now get rid of the one at the end */
				memset(EXT_LAST_EXTENT(eh), 0,
					sizeof(struct ext4_extent));
			}
			le16_add_cpu(&eh->eh_entries, -1);
		}

		err = ext4_ext_dirty(handle, inode, path + depth);
		if (err)
			goto out;

		ext_debug(inode, "new extent: %u:%u:%llu\n", ex_ee_block, num,
				ext4_ext_pblock(ex));
		ex--;
		ex_ee_block = le32_to_cpu(ex->ee_block);
		ex_ee_len = ext4_ext_get_actual_len(ex);
	}

	if (correct_index && eh->eh_entries)
		err = ext4_ext_correct_indexes(handle, inode, path);

	/*
	 * If there's a partial cluster and at least one extent remains in
	 * the leaf, free the partial cluster if it isn't shared with the
	 * current extent.  If it is shared with the current extent
	 * we reset the partial cluster because we've reached the start of the
	 * truncated/punched region and we're done removing blocks.
	 */
	if (partial->state == tofree && ex >= EXT_FIRST_EXTENT(eh)) {
		pblk = ext4_ext_pblock(ex) + ex_ee_len - 1;
		if (partial->pclu != EXT4_B2C(sbi, pblk)) {
			int flags = get_default_free_blocks_flags(inode);

			if (ext4_is_pending(inode, partial->lblk))
				flags |= EXT4_FREE_BLOCKS_RERESERVE_CLUSTER;
			ext4_free_blocks(handle, inode, NULL,
					 EXT4_C2B(sbi, partial->pclu),
					 sbi->s_cluster_ratio, flags);
			if (flags & EXT4_FREE_BLOCKS_RERESERVE_CLUSTER)
				ext4_rereserve_cluster(inode, partial->lblk);
		}
		partial->state = initial;
	}

	/* if this leaf is free, then we should
	 * remove it from index block above */
	if (err == 0 && eh->eh_entries == 0 && path[depth].p_bh != NULL)
		err = ext4_ext_rm_idx(handle, inode, path, depth);

out:
	return err;
}

/*
 * ext4_ext_more_to_rm:
 * returns 1 if current index has to be freed (even partial)
 */
static int
ext4_ext_more_to_rm(struct ext4_ext_path *path)
{
	BUG_ON(path->p_idx == NULL);

	if (path->p_idx < EXT_FIRST_INDEX(path->p_hdr))
		return 0;

	/*
	 * if truncate on deeper level happened, it wasn't partial,
	 * so we have to consider current index for truncation
	 */
	if (le16_to_cpu(path->p_hdr->eh_entries) == path->p_block)
		return 0;
	return 1;
}

int ext4_ext_remove_space(struct inode *inode, ext4_lblk_t start,
			  ext4_lblk_t end)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	int depth = ext_depth(inode);
	struct ext4_ext_path *path = NULL;
	struct partial_cluster partial;
	handle_t *handle;
	int i = 0, err = 0;

	partial.pclu = 0;
	partial.lblk = 0;
	partial.state = initial;

	ext_debug(inode, "truncate since %u to %u\n", start, end);

	/* probably first extent we're gonna free will be last in block */
	handle = ext4_journal_start_with_revoke(inode, EXT4_HT_TRUNCATE,
			depth + 1,
			ext4_free_metadata_revoke_credits(inode->i_sb, depth));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

again:
	trace_ext4_ext_remove_space(inode, start, end, depth);

	/*
	 * Check if we are removing extents inside the extent tree. If that
	 * is the case, we are going to punch a hole inside the extent tree
	 * so we have to check whether we need to split the extent covering
	 * the last block to remove so we can easily remove the part of it
	 * in ext4_ext_rm_leaf().
	 */
	if (end < EXT_MAX_BLOCKS - 1) {
		struct ext4_extent *ex;
		ext4_lblk_t ee_block, ex_end, lblk;
		ext4_fsblk_t pblk;

		/* find extent for or closest extent to this block */
		path = ext4_find_extent(inode, end, NULL,
					EXT4_EX_NOCACHE | EXT4_EX_NOFAIL);
		if (IS_ERR(path)) {
			ext4_journal_stop(handle);
			return PTR_ERR(path);
		}
		depth = ext_depth(inode);
		/* Leaf not may not exist only if inode has no blocks at all */
		ex = path[depth].p_ext;
		if (!ex) {
			if (depth) {
				EXT4_ERROR_INODE(inode,
						 "path[%d].p_hdr == NULL",
						 depth);
				err = -EFSCORRUPTED;
			}
			goto out;
		}

		ee_block = le32_to_cpu(ex->ee_block);
		ex_end = ee_block + ext4_ext_get_actual_len(ex) - 1;

		/*
		 * See if the last block is inside the extent, if so split
		 * the extent at 'end' block so we can easily remove the
		 * tail of the first part of the split extent in
		 * ext4_ext_rm_leaf().
		 */
		if (end >= ee_block && end < ex_end) {

			/*
			 * If we're going to split the extent, note that
			 * the cluster containing the block after 'end' is
			 * in use to avoid freeing it when removing blocks.
			 */
			if (sbi->s_cluster_ratio > 1) {
				pblk = ext4_ext_pblock(ex) + end - ee_block + 1;
				partial.pclu = EXT4_B2C(sbi, pblk);
				partial.state = nofree;
			}

			/*
			 * Split the extent in two so that 'end' is the last
			 * block in the first new extent. Also we should not
			 * fail removing space due to ENOSPC so try to use
			 * reserved block if that happens.
			 */
			err = ext4_force_split_extent_at(handle, inode, &path,
							 end + 1, 1);
			if (err < 0)
				goto out;

		} else if (sbi->s_cluster_ratio > 1 && end >= ex_end &&
			   partial.state == initial) {
			/*
			 * If we're punching, there's an extent to the right.
			 * If the partial cluster hasn't been set, set it to
			 * that extent's first cluster and its state to nofree
			 * so it won't be freed should it contain blocks to be
			 * removed. If it's already set (tofree/nofree), we're
			 * retrying and keep the original partial cluster info
			 * so a cluster marked tofree as a result of earlier
			 * extent removal is not lost.
			 */
			lblk = ex_end + 1;
			err = ext4_ext_search_right(inode, path, &lblk, &pblk,
						    NULL);
			if (err < 0)
				goto out;
			if (pblk) {
				partial.pclu = EXT4_B2C(sbi, pblk);
				partial.state = nofree;
			}
		}
	}
	/*
	 * We start scanning from right side, freeing all the blocks
	 * after i_size and walking into the tree depth-wise.
	 */
	depth = ext_depth(inode);
	if (path) {
		int k = i = depth;
		while (--k > 0)
			path[k].p_block =
				le16_to_cpu(path[k].p_hdr->eh_entries)+1;
	} else {
		path = kcalloc(depth + 1, sizeof(struct ext4_ext_path),
			       GFP_NOFS | __GFP_NOFAIL);
		if (path == NULL) {
			ext4_journal_stop(handle);
			return -ENOMEM;
		}
		path[0].p_maxdepth = path[0].p_depth = depth;
		path[0].p_hdr = ext_inode_hdr(inode);
		i = 0;

		if (ext4_ext_check(inode, path[0].p_hdr, depth, 0)) {
			err = -EFSCORRUPTED;
			goto out;
		}
	}
	err = 0;

	while (i >= 0 && err == 0) {
		if (i == depth) {
			/* this is leaf block */
			err = ext4_ext_rm_leaf(handle, inode, path,
					       &partial, start, end);
			/* root level has p_bh == NULL, brelse() eats this */
			brelse(path[i].p_bh);
			path[i].p_bh = NULL;
			i--;
			continue;
		}

		/* this is index block */
		if (!path[i].p_hdr) {
			ext_debug(inode, "initialize header\n");
			path[i].p_hdr = ext_block_hdr(path[i].p_bh);
		}

		if (!path[i].p_idx) {
			/* this level hasn't been touched yet */
			path[i].p_idx = EXT_LAST_INDEX(path[i].p_hdr);
			path[i].p_block = le16_to_cpu(path[i].p_hdr->eh_entries)+1;
			ext_debug(inode, "init index ptr: hdr 0x%p, num %d\n",
				  path[i].p_hdr,
				  le16_to_cpu(path[i].p_hdr->eh_entries));
		} else {
			/* we were already here, see at next index */
			path[i].p_idx--;
		}

		ext_debug(inode, "level %d - index, first 0x%p, cur 0x%p\n",
				i, EXT_FIRST_INDEX(path[i].p_hdr),
				path[i].p_idx);
		if (ext4_ext_more_to_rm(path + i)) {
			struct buffer_head *bh;
			/* go to the next level */
			ext_debug(inode, "move to level %d (block %llu)\n",
				  i + 1, ext4_idx_pblock(path[i].p_idx));
			memset(path + i + 1, 0, sizeof(*path));
			bh = read_extent_tree_block(inode, path[i].p_idx,
						    depth - i - 1,
						    EXT4_EX_NOCACHE);
			if (IS_ERR(bh)) {
				/* should we reset i_size? */
				err = PTR_ERR(bh);
				break;
			}
			/* Yield here to deal with large extent trees.
			 * Should be a no-op if we did IO above. */
			cond_resched();
			if (WARN_ON(i + 1 > depth)) {
				err = -EFSCORRUPTED;
				break;
			}
			path[i + 1].p_bh = bh;

			/* save actual number of indexes since this
			 * number is changed at the next iteration */
			path[i].p_block = le16_to_cpu(path[i].p_hdr->eh_entries);
			i++;
		} else {
			/* we finished processing this index, go up */
			if (path[i].p_hdr->eh_entries == 0 && i > 0) {
				/* index is empty, remove it;
				 * handle must be already prepared by the
				 * truncatei_leaf() */
				err = ext4_ext_rm_idx(handle, inode, path, i);
			}
			/* root level has p_bh == NULL, brelse() eats this */
			brelse(path[i].p_bh);
			path[i].p_bh = NULL;
			i--;
			ext_debug(inode, "return to level %d\n", i);
		}
	}

	trace_ext4_ext_remove_space_done(inode, start, end, depth, &partial,
					 path->p_hdr->eh_entries);

	/*
	 * if there's a partial cluster and we have removed the first extent
	 * in the file, then we also free the partial cluster, if any
	 */
	if (partial.state == tofree && err == 0) {
		int flags = get_default_free_blocks_flags(inode);

		if (ext4_is_pending(inode, partial.lblk))
			flags |= EXT4_FREE_BLOCKS_RERESERVE_CLUSTER;
		ext4_free_blocks(handle, inode, NULL,
				 EXT4_C2B(sbi, partial.pclu),
				 sbi->s_cluster_ratio, flags);
		if (flags & EXT4_FREE_BLOCKS_RERESERVE_CLUSTER)
			ext4_rereserve_cluster(inode, partial.lblk);
		partial.state = initial;
	}

	/* TODO: flexible tree reduction should be here */
	if (path->p_hdr->eh_entries == 0) {
		/*
		 * truncate to zero freed all the tree,
		 * so we need to correct eh_depth
		 */
		err = ext4_ext_get_access(handle, inode, path);
		if (err == 0) {
			ext_inode_hdr(inode)->eh_depth = 0;
			ext_inode_hdr(inode)->eh_max =
				cpu_to_le16(ext4_ext_space_root(inode, 0));
			err = ext4_ext_dirty(handle, inode, path);
		}
	}
out:
	ext4_free_ext_path(path);
	path = NULL;
	if (err == -EAGAIN)
		goto again;
	ext4_journal_stop(handle);

	return err;
}

/*
 * called at mount time
 */
void ext4_ext_init(struct super_block *sb)
{
	/*
	 * possible initialization would be here
	 */

	if (ext4_has_feature_extents(sb)) {
#if defined(AGGRESSIVE_TEST) || defined(CHECK_BINSEARCH) || defined(EXTENTS_STATS)
		printk(KERN_INFO "EXT4-fs: file extents enabled"
#ifdef AGGRESSIVE_TEST
		       ", aggressive tests"
#endif
#ifdef CHECK_BINSEARCH
		       ", check binsearch"
#endif
#ifdef EXTENTS_STATS
		       ", stats"
#endif
		       "\n");
#endif
#ifdef EXTENTS_STATS
		spin_lock_init(&EXT4_SB(sb)->s_ext_stats_lock);
		EXT4_SB(sb)->s_ext_min = 1 << 30;
		EXT4_SB(sb)->s_ext_max = 0;
#endif
	}
}

/*
 * called at umount time
 */
void ext4_ext_release(struct super_block *sb)
{
	if (!ext4_has_feature_extents(sb))
		return;

#ifdef EXTENTS_STATS
	if (EXT4_SB(sb)->s_ext_blocks && EXT4_SB(sb)->s_ext_extents) {
		struct ext4_sb_info *sbi = EXT4_SB(sb);
		printk(KERN_ERR "EXT4-fs: %lu blocks in %lu extents (%lu ave)\n",
			sbi->s_ext_blocks, sbi->s_ext_extents,
			sbi->s_ext_blocks / sbi->s_ext_extents);
		printk(KERN_ERR "EXT4-fs: extents: %lu min, %lu max, max depth %lu\n",
			sbi->s_ext_min, sbi->s_ext_max, sbi->s_depth_max);
	}
#endif
}

static int ext4_zeroout_es(struct inode *inode, struct ext4_extent *ex)
{
	ext4_lblk_t  ee_block;
	ext4_fsblk_t ee_pblock;
	unsigned int ee_len;

	ee_block  = le32_to_cpu(ex->ee_block);
	ee_len    = ext4_ext_get_actual_len(ex);
	ee_pblock = ext4_ext_pblock(ex);

	if (ee_len == 0)
		return 0;

	return ext4_es_insert_extent(inode, ee_block, ee_len, ee_pblock,
				     EXTENT_STATUS_WRITTEN);
}

/* FIXME!! we need to try to merge to left or right after zero-out  */
static int ext4_ext_zeroout(struct inode *inode, struct ext4_extent *ex)
{
	ext4_fsblk_t ee_pblock;
	unsigned int ee_len;

	ee_len    = ext4_ext_get_actual_len(ex);
	ee_pblock = ext4_ext_pblock(ex);
	return ext4_issue_zeroout(inode, le32_to_cpu(ex->ee_block), ee_pblock,
				  ee_len);
}

/*
 * ext4_split_extent_at() splits an extent at given block.
 *
 * @handle: the journal handle
 * @inode: the file inode
 * @path: the path to the extent
 * @split: the logical block where the extent is splitted.
 * @split_flags: indicates if the extent could be zeroout if split fails, and
 *		 the states(init or unwritten) of new extents.
 * @flags: flags used to insert new extent to extent tree.
 *
 *
 * Splits extent [a, b] into two extents [a, @split) and [@split, b], states
 * of which are determined by split_flag.
 *
 * There are two cases:
 *  a> the extent are splitted into two extent.
 *  b> split is not needed, and just mark the extent.
 *
 * return 0 on success.
 */
static int ext4_split_extent_at(handle_t *handle,
			     struct inode *inode,
			     struct ext4_ext_path **ppath,
			     ext4_lblk_t split,
			     int split_flag,
			     int flags)
{
	struct ext4_ext_path *path = *ppath;
	ext4_fsblk_t newblock;
	ext4_lblk_t ee_block;
	struct ext4_extent *ex, newex, orig_ex, zero_ex;
	struct ext4_extent *ex2 = NULL;
	unsigned int ee_len, depth;
	int err = 0;

	BUG_ON((split_flag & (EXT4_EXT_DATA_VALID1 | EXT4_EXT_DATA_VALID2)) ==
	       (EXT4_EXT_DATA_VALID1 | EXT4_EXT_DATA_VALID2));

	ext_debug(inode, "logical block %llu\n", (unsigned long long)split);

	ext4_ext_show_leaf(inode, path);

	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);
	newblock = split - ee_block + ext4_ext_pblock(ex);

	BUG_ON(split < ee_block || split >= (ee_block + ee_len));
	BUG_ON(!ext4_ext_is_unwritten(ex) &&
	       split_flag & (EXT4_EXT_MAY_ZEROOUT |
			     EXT4_EXT_MARK_UNWRIT1 |
			     EXT4_EXT_MARK_UNWRIT2));

	err = ext4_ext_get_access(handle, inode, path + depth);
	if (err)
		goto out;

	if (split == ee_block) {
		/*
		 * case b: block @split is the block that the extent begins with
		 * then we just change the state of the extent, and splitting
		 * is not needed.
		 */
		if (split_flag & EXT4_EXT_MARK_UNWRIT2)
			ext4_ext_mark_unwritten(ex);
		else
			ext4_ext_mark_initialized(ex);

		if (!(flags & EXT4_GET_BLOCKS_PRE_IO))
			ext4_ext_try_to_merge(handle, inode, path, ex);

		err = ext4_ext_dirty(handle, inode, path + path->p_depth);
		goto out;
	}

	/* case a */
	memcpy(&orig_ex, ex, sizeof(orig_ex));
	ex->ee_len = cpu_to_le16(split - ee_block);
	if (split_flag & EXT4_EXT_MARK_UNWRIT1)
		ext4_ext_mark_unwritten(ex);

	/*
	 * path may lead to new leaf, not to original leaf any more
	 * after ext4_ext_insert_extent() returns,
	 */
	err = ext4_ext_dirty(handle, inode, path + depth);
	if (err)
		goto fix_extent_len;

	ex2 = &newex;
	ex2->ee_block = cpu_to_le32(split);
	ex2->ee_len   = cpu_to_le16(ee_len - (split - ee_block));
	ext4_ext_store_pblock(ex2, newblock);
	if (split_flag & EXT4_EXT_MARK_UNWRIT2)
		ext4_ext_mark_unwritten(ex2);

	err = ext4_ext_insert_extent(handle, inode, ppath, &newex, flags);
	if (err != -ENOSPC && err != -EDQUOT && err != -ENOMEM)
		goto out;

	if (EXT4_EXT_MAY_ZEROOUT & split_flag) {
		if (split_flag & (EXT4_EXT_DATA_VALID1|EXT4_EXT_DATA_VALID2)) {
			if (split_flag & EXT4_EXT_DATA_VALID1) {
				err = ext4_ext_zeroout(inode, ex2);
				zero_ex.ee_block = ex2->ee_block;
				zero_ex.ee_len = cpu_to_le16(
						ext4_ext_get_actual_len(ex2));
				ext4_ext_store_pblock(&zero_ex,
						      ext4_ext_pblock(ex2));
			} else {
				err = ext4_ext_zeroout(inode, ex);
				zero_ex.ee_block = ex->ee_block;
				zero_ex.ee_len = cpu_to_le16(
						ext4_ext_get_actual_len(ex));
				ext4_ext_store_pblock(&zero_ex,
						      ext4_ext_pblock(ex));
			}
		} else {
			err = ext4_ext_zeroout(inode, &orig_ex);
			zero_ex.ee_block = orig_ex.ee_block;
			zero_ex.ee_len = cpu_to_le16(
						ext4_ext_get_actual_len(&orig_ex));
			ext4_ext_store_pblock(&zero_ex,
					      ext4_ext_pblock(&orig_ex));
		}

		if (!err) {
			/* update the extent length and mark as initialized */
			ex->ee_len = cpu_to_le16(ee_len);
			ext4_ext_try_to_merge(handle, inode, path, ex);
			err = ext4_ext_dirty(handle, inode, path + path->p_depth);
			if (!err)
				/* update extent status tree */
				err = ext4_zeroout_es(inode, &zero_ex);
			/* If we failed at this point, we don't know in which
			 * state the extent tree exactly is so don't try to fix
			 * length of the original extent as it may do even more
			 * damage.
			 */
			goto out;
		}
	}

fix_extent_len:
	ex->ee_len = orig_ex.ee_len;
	/*
	 * Ignore ext4_ext_dirty return value since we are already in error path
	 * and err is a non-zero error code.
	 */
	ext4_ext_dirty(handle, inode, path + path->p_depth);
	return err;
out:
	ext4_ext_show_leaf(inode, path);
	return err;
}

/*
 * ext4_split_extents() splits an extent and mark extent which is covered
 * by @map as split_flags indicates
 *
 * It may result in splitting the extent into multiple extents (up to three)
 * There are three possibilities:
 *   a> There is no split required
 *   b> Splits in two extents: Split is happening at either end of the extent
 *   c> Splits in three extents: Somone is splitting in middle of the extent
 *
 */
static int ext4_split_extent(handle_t *handle,
			      struct inode *inode,
			      struct ext4_ext_path **ppath,
			      struct ext4_map_blocks *map,
			      int split_flag,
			      int flags)
{
	struct ext4_ext_path *path = *ppath;
	ext4_lblk_t ee_block;
	struct ext4_extent *ex;
	unsigned int ee_len, depth;
	int err = 0;
	int unwritten;
	int split_flag1, flags1;
	int allocated = map->m_len;

	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);
	unwritten = ext4_ext_is_unwritten(ex);

	if (map->m_lblk + map->m_len < ee_block + ee_len) {
		split_flag1 = split_flag & EXT4_EXT_MAY_ZEROOUT;
		flags1 = flags | EXT4_GET_BLOCKS_PRE_IO;
		if (unwritten)
			split_flag1 |= EXT4_EXT_MARK_UNWRIT1 |
				       EXT4_EXT_MARK_UNWRIT2;
		if (split_flag & EXT4_EXT_DATA_VALID2)
			split_flag1 |= EXT4_EXT_DATA_VALID1;
		err = ext4_split_extent_at(handle, inode, ppath,
				map->m_lblk + map->m_len, split_flag1, flags1);
		if (err)
			goto out;
	} else {
		allocated = ee_len - (map->m_lblk - ee_block);
	}
	/*
	 * Update path is required because previous ext4_split_extent_at() may
	 * result in split of original leaf or extent zeroout.
	 */
	path = ext4_find_extent(inode, map->m_lblk, ppath, flags);
	if (IS_ERR(path))
		return PTR_ERR(path);
	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	if (!ex) {
		EXT4_ERROR_INODE(inode, "unexpected hole at %lu",
				 (unsigned long) map->m_lblk);
		return -EFSCORRUPTED;
	}
	unwritten = ext4_ext_is_unwritten(ex);

	if (map->m_lblk >= ee_block) {
		split_flag1 = split_flag & EXT4_EXT_DATA_VALID2;
		if (unwritten) {
			split_flag1 |= EXT4_EXT_MARK_UNWRIT1;
			split_flag1 |= split_flag & (EXT4_EXT_MAY_ZEROOUT |
						     EXT4_EXT_MARK_UNWRIT2);
		}
		err = ext4_split_extent_at(handle, inode, ppath,
				map->m_lblk, split_flag1, flags);
		if (err)
			goto out;
	}

	ext4_ext_show_leaf(inode, path);
out:
	return err ? err : allocated;
}

/*
 * This function is called by ext4_ext_map_blocks() if someone tries to write
 * to an unwritten extent. It may result in splitting the unwritten
 * extent into multiple extents (up to three - one initialized and two
 * unwritten).
 * There are three possibilities:
 *   a> There is no split required: Entire extent should be initialized
 *   b> Splits in two extents: Write is happening at either end of the extent
 *   c> Splits in three extents: Somone is writing in middle of the extent
 *
 * Pre-conditions:
 *  - The extent pointed to by 'path' is unwritten.
 *  - The extent pointed to by 'path' contains a superset
 *    of the logical span [map->m_lblk, map->m_lblk + map->m_len).
 *
 * Post-conditions on success:
 *  - the returned value is the number of blocks beyond map->l_lblk
 *    that are allocated and initialized.
 *    It is guaranteed to be >= map->m_len.
 */
static int ext4_ext_convert_to_initialized(handle_t *handle,
					   struct inode *inode,
					   struct ext4_map_blocks *map,
					   struct ext4_ext_path **ppath,
					   int flags)
{
	struct ext4_ext_path *path = *ppath;
	struct ext4_sb_info *sbi;
	struct ext4_extent_header *eh;
	struct ext4_map_blocks split_map;
	struct ext4_extent zero_ex1, zero_ex2;
	struct ext4_extent *ex, *abut_ex;
	ext4_lblk_t ee_block, eof_block;
	unsigned int ee_len, depth, map_len = map->m_len;
	int allocated = 0, max_zeroout = 0;
	int err = 0;
	int split_flag = EXT4_EXT_DATA_VALID2;

	ext_debug(inode, "logical block %llu, max_blocks %u\n",
		  (unsigned long long)map->m_lblk, map_len);

	sbi = EXT4_SB(inode->i_sb);
	eof_block = (EXT4_I(inode)->i_disksize + inode->i_sb->s_blocksize - 1)
			>> inode->i_sb->s_blocksize_bits;
	if (eof_block < map->m_lblk + map_len)
		eof_block = map->m_lblk + map_len;

	depth = ext_depth(inode);
	eh = path[depth].p_hdr;
	ex = path[depth].p_ext;
	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);
	zero_ex1.ee_len = 0;
	zero_ex2.ee_len = 0;

	trace_ext4_ext_convert_to_initialized_enter(inode, map, ex);

	/* Pre-conditions */
	BUG_ON(!ext4_ext_is_unwritten(ex));
	BUG_ON(!in_range(map->m_lblk, ee_block, ee_len));

	/*
	 * Attempt to transfer newly initialized blocks from the currently
	 * unwritten extent to its neighbor. This is much cheaper
	 * than an insertion followed by a merge as those involve costly
	 * memmove() calls. Transferring to the left is the common case in
	 * steady state for workloads doing fallocate(FALLOC_FL_KEEP_SIZE)
	 * followed by append writes.
	 *
	 * Limitations of the current logic:
	 *  - L1: we do not deal with writes covering the whole extent.
	 *    This would require removing the extent if the transfer
	 *    is possible.
	 *  - L2: we only attempt to merge with an extent stored in the
	 *    same extent tree node.
	 */
	if ((map->m_lblk == ee_block) &&
		/* See if we can merge left */
		(map_len < ee_len) &&		/*L1*/
		(ex > EXT_FIRST_EXTENT(eh))) {	/*L2*/
		ext4_lblk_t prev_lblk;
		ext4_fsblk_t prev_pblk, ee_pblk;
		unsigned int prev_len;

		abut_ex = ex - 1;
		prev_lblk = le32_to_cpu(abut_ex->ee_block);
		prev_len = ext4_ext_get_actual_len(abut_ex);
		prev_pblk = ext4_ext_pblock(abut_ex);
		ee_pblk = ext4_ext_pblock(ex);

		/*
		 * A transfer of blocks from 'ex' to 'abut_ex' is allowed
		 * upon those conditions:
		 * - C1: abut_ex is initialized,
		 * - C2: abut_ex is logically abutting ex,
		 * - C3: abut_ex is physically abutting ex,
		 * - C4: abut_ex can receive the additional blocks without
		 *   overflowing the (initialized) length limit.
		 */
		if ((!ext4_ext_is_unwritten(abut_ex)) &&		/*C1*/
			((prev_lblk + prev_len) == ee_block) &&		/*C2*/
			((prev_pblk + prev_len) == ee_pblk) &&		/*C3*/
			(prev_len < (EXT_INIT_MAX_LEN - map_len))) {	/*C4*/
			err = ext4_ext_get_access(handle, inode, path + depth);
			if (err)
				goto out;

			trace_ext4_ext_convert_to_initialized_fastpath(inode,
				map, ex, abut_ex);

			/* Shift the start of ex by 'map_len' blocks */
			ex->ee_block = cpu_to_le32(ee_block + map_len);
			ext4_ext_store_pblock(ex, ee_pblk + map_len);
			ex->ee_len = cpu_to_le16(ee_len - map_len);
			ext4_ext_mark_unwritten(ex); /* Restore the flag */

			/* Extend abut_ex by 'map_len' blocks */
			abut_ex->ee_len = cpu_to_le16(prev_len + map_len);

			/* Result: number of initialized blocks past m_lblk */
			allocated = map_len;
		}
	} else if (((map->m_lblk + map_len) == (ee_block + ee_len)) &&
		   (map_len < ee_len) &&	/*L1*/
		   ex < EXT_LAST_EXTENT(eh)) {	/*L2*/
		/* See if we can merge right */
		ext4_lblk_t next_lblk;
		ext4_fsblk_t next_pblk, ee_pblk;
		unsigned int next_len;

		abut_ex = ex + 1;
		next_lblk = le32_to_cpu(abut_ex->ee_block);
		next_len = ext4_ext_get_actual_len(abut_ex);
		next_pblk = ext4_ext_pblock(abut_ex);
		ee_pblk = ext4_ext_pblock(ex);

		/*
		 * A transfer of blocks from 'ex' to 'abut_ex' is allowed
		 * upon those conditions:
		 * - C1: abut_ex is initialized,
		 * - C2: abut_ex is logically abutting ex,
		 * - C3: abut_ex is physically abutting ex,
		 * - C4: abut_ex can receive the additional blocks without
		 *   overflowing the (initialized) length limit.
		 */
		if ((!ext4_ext_is_unwritten(abut_ex)) &&		/*C1*/
		    ((map->m_lblk + map_len) == next_lblk) &&		/*C2*/
		    ((ee_pblk + ee_len) == next_pblk) &&		/*C3*/
		    (next_len < (EXT_INIT_MAX_LEN - map_len))) {	/*C4*/
			err = ext4_ext_get_access(handle, inode, path + depth);
			if (err)
				goto out;

			trace_ext4_ext_convert_to_initialized_fastpath(inode,
				map, ex, abut_ex);

			/* Shift the start of abut_ex by 'map_len' blocks */
			abut_ex->ee_block = cpu_to_le32(next_lblk - map_len);
			ext4_ext_store_pblock(abut_ex, next_pblk - map_len);
			ex->ee_len = cpu_to_le16(ee_len - map_len);
			ext4_ext_mark_unwritten(ex); /* Restore the flag */

			/* Extend abut_ex by 'map_len' blocks */
			abut_ex->ee_len = cpu_to_le16(next_len + map_len);

			/* Result: number of initialized blocks past m_lblk */
			allocated = map_len;
		}
	}
	if (allocated) {
		/* Mark the block containing both extents as dirty */
		err = ext4_ext_dirty(handle, inode, path + depth);

		/* Update path to point to the right extent */
		path[depth].p_ext = abut_ex;
		goto out;
	} else
		allocated = ee_len - (map->m_lblk - ee_block);

	WARN_ON(map->m_lblk < ee_block);
	/*
	 * It is safe to convert extent to initialized via explicit
	 * zeroout only if extent is fully inside i_size or new_size.
	 */
	split_flag |= ee_block + ee_len <= eof_block ? EXT4_EXT_MAY_ZEROOUT : 0;

	if (EXT4_EXT_MAY_ZEROOUT & split_flag)
		max_zeroout = sbi->s_extent_max_zeroout_kb >>
			(inode->i_sb->s_blocksize_bits - 10);

	/*
	 * five cases:
	 * 1. split the extent into three extents.
	 * 2. split the extent into two extents, zeroout the head of the first
	 *    extent.
	 * 3. split the extent into two extents, zeroout the tail of the second
	 *    extent.
	 * 4. split the extent into two extents with out zeroout.
	 * 5. no splitting needed, just possibly zeroout the head and / or the
	 *    tail of the extent.
	 */
	split_map.m_lblk = map->m_lblk;
	split_map.m_len = map->m_len;

	if (max_zeroout && (allocated > split_map.m_len)) {
		if (allocated <= max_zeroout) {
			/* case 3 or 5 */
			zero_ex1.ee_block =
				 cpu_to_le32(split_map.m_lblk +
					     split_map.m_len);
			zero_ex1.ee_len =
				cpu_to_le16(allocated - split_map.m_len);
			ext4_ext_store_pblock(&zero_ex1,
				ext4_ext_pblock(ex) + split_map.m_lblk +
				split_map.m_len - ee_block);
			err = ext4_ext_zeroout(inode, &zero_ex1);
			if (err)
				goto fallback;
			split_map.m_len = allocated;
		}
		if (split_map.m_lblk - ee_block + split_map.m_len <
								max_zeroout) {
			/* case 2 or 5 */
			if (split_map.m_lblk != ee_block) {
				zero_ex2.ee_block = ex->ee_block;
				zero_ex2.ee_len = cpu_to_le16(split_map.m_lblk -
							ee_block);
				ext4_ext_store_pblock(&zero_ex2,
						      ext4_ext_pblock(ex));
				err = ext4_ext_zeroout(inode, &zero_ex2);
				if (err)
					goto fallback;
			}

			split_map.m_len += split_map.m_lblk - ee_block;
			split_map.m_lblk = ee_block;
			allocated = map->m_len;
		}
	}

fallback:
	err = ext4_split_extent(handle, inode, ppath, &split_map, split_flag,
				flags);
	if (err > 0)
		err = 0;
out:
	/* If we have gotten a failure, don't zero out status tree */
	if (!err) {
		err = ext4_zeroout_es(inode, &zero_ex1);
		if (!err)
			err = ext4_zeroout_es(inode, &zero_ex2);
	}
	return err ? err : allocated;
}

/*
 * This function is called by ext4_ext_map_blocks() from
 * ext4_get_blocks_dio_write() when DIO to write
 * to an unwritten extent.
 *
 * Writing to an unwritten extent may result in splitting the unwritten
 * extent into multiple initialized/unwritten extents (up to three)
 * There are three possibilities:
 *   a> There is no split required: Entire extent should be unwritten
 *   b> Splits in two extents: Write is happening at either end of the extent
 *   c> Splits in three extents: Somone is writing in middle of the extent
 *
 * This works the same way in the case of initialized -> unwritten conversion.
 *
 * One of more index blocks maybe needed if the extent tree grow after
 * the unwritten extent split. To prevent ENOSPC occur at the IO
 * complete, we need to split the unwritten extent before DIO submit
 * the IO. The unwritten extent called at this time will be split
 * into three unwritten extent(at most). After IO complete, the part
 * being filled will be convert to initialized by the end_io callback function
 * via ext4_convert_unwritten_extents().
 *
 * Returns the size of unwritten extent to be written on success.
 */
static int ext4_split_convert_extents(handle_t *handle,
					struct inode *inode,
					struct ext4_map_blocks *map,
					struct ext4_ext_path **ppath,
					int flags)
{
	struct ext4_ext_path *path = *ppath;
	ext4_lblk_t eof_block;
	ext4_lblk_t ee_block;
	struct ext4_extent *ex;
	unsigned int ee_len;
	int split_flag = 0, depth;

	ext_debug(inode, "logical block %llu, max_blocks %u\n",
		  (unsigned long long)map->m_lblk, map->m_len);

	eof_block = (EXT4_I(inode)->i_disksize + inode->i_sb->s_blocksize - 1)
			>> inode->i_sb->s_blocksize_bits;
	if (eof_block < map->m_lblk + map->m_len)
		eof_block = map->m_lblk + map->m_len;
	/*
	 * It is safe to convert extent to initialized via explicit
	 * zeroout only if extent is fully inside i_size or new_size.
	 */
	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);

	/* Convert to unwritten */
	if (flags & EXT4_GET_BLOCKS_CONVERT_UNWRITTEN) {
		split_flag |= EXT4_EXT_DATA_VALID1;
	/* Convert to initialized */
	} else if (flags & EXT4_GET_BLOCKS_CONVERT) {
		split_flag |= ee_block + ee_len <= eof_block ?
			      EXT4_EXT_MAY_ZEROOUT : 0;
		split_flag |= (EXT4_EXT_MARK_UNWRIT2 | EXT4_EXT_DATA_VALID2);
	}
	flags |= EXT4_GET_BLOCKS_PRE_IO;
	return ext4_split_extent(handle, inode, ppath, map, split_flag, flags);
}

static int ext4_convert_unwritten_extents_endio(handle_t *handle,
						struct inode *inode,
						struct ext4_map_blocks *map,
						struct ext4_ext_path **ppath)
{
	struct ext4_ext_path *path = *ppath;
	struct ext4_extent *ex;
	ext4_lblk_t ee_block;
	unsigned int ee_len;
	int depth;
	int err = 0;

	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);

	ext_debug(inode, "logical block %llu, max_blocks %u\n",
		  (unsigned long long)ee_block, ee_len);

	/* If extent is larger than requested it is a clear sign that we still
	 * have some extent state machine issues left. So extent_split is still
	 * required.
	 * TODO: Once all related issues will be fixed this situation should be
	 * illegal.
	 */
	if (ee_block != map->m_lblk || ee_len > map->m_len) {
#ifdef CONFIG_EXT4_DEBUG
		ext4_warning(inode->i_sb, "Inode (%ld) finished: extent logical block %llu,"
			     " len %u; IO logical block %llu, len %u",
			     inode->i_ino, (unsigned long long)ee_block, ee_len,
			     (unsigned long long)map->m_lblk, map->m_len);
#endif
		err = ext4_split_convert_extents(handle, inode, map, ppath,
						 EXT4_GET_BLOCKS_CONVERT);
		if (err < 0)
			return err;
		path = ext4_find_extent(inode, map->m_lblk, ppath, 0);
		if (IS_ERR(path))
			return PTR_ERR(path);
		depth = ext_depth(inode);
		ex = path[depth].p_ext;
	}

	err = ext4_ext_get_access(handle, inode, path + depth);
	if (err)
		goto out;
	/* first mark the extent as initialized */
	ext4_ext_mark_initialized(ex);

	/* note: ext4_ext_correct_indexes() isn't needed here because
	 * borders are not changed
	 */
	ext4_ext_try_to_merge(handle, inode, path, ex);

	/* Mark modified extent as dirty */
	err = ext4_ext_dirty(handle, inode, path + path->p_depth);
out:
	ext4_ext_show_leaf(inode, path);
	return err;
}

static int
convert_initialized_extent(handle_t *handle, struct inode *inode,
			   struct ext4_map_blocks *map,
			   struct ext4_ext_path **ppath,
			   unsigned int *allocated)
{
	struct ext4_ext_path *path = *ppath;
	struct ext4_extent *ex;
	ext4_lblk_t ee_block;
	unsigned int ee_len;
	int depth;
	int err = 0;

	/*
	 * Make sure that the extent is no bigger than we support with
	 * unwritten extent
	 */
	if (map->m_len > EXT_UNWRITTEN_MAX_LEN)
		map->m_len = EXT_UNWRITTEN_MAX_LEN / 2;

	depth = ext_depth(inode);
	ex = path[depth].p_ext;
	ee_block = le32_to_cpu(ex->ee_block);
	ee_len = ext4_ext_get_actual_len(ex);

	ext_debug(inode, "logical block %llu, max_blocks %u\n",
		  (unsigned long long)ee_block, ee_len);

	if (ee_block != map->m_lblk || ee_len > map->m_len) {
		err = ext4_split_convert_extents(handle, inode, map, ppath,
				EXT4_GET_BLOCKS_CONVERT_UNWRITTEN);
		if (err < 0)
			return err;
		path = ext4_find_extent(inode, map->m_lblk, ppath, 0);
		if (IS_ERR(path))
			return PTR_ERR(path);
		depth = ext_depth(inode);
		ex = path[depth].p_ext;
		if (!ex) {
			EXT4_ERROR_INODE(inode, "unexpected hole at %lu",
					 (unsigned long) map->m_lblk);
			return -EFSCORRUPTED;
		}
	}

	err = ext4_ext_get_access(handle, inode, path + depth);
	if (err)
		return err;
	/* first mark the extent as unwritten */
	ext4_ext_mark_unwritten(ex);

	/* note: ext4_ext_correct_indexes() isn't needed here because
	 * borders are not changed
	 */
	ext4_ext_try_to_merge(handle, inode, path, ex);

	/* Mark modified extent as dirty */
	err = ext4_ext_dirty(handle, inode, path + path->p_depth);
	if (err)
		return err;
	ext4_ext_show_leaf(inode, path);

	ext4_update_inode_fsync_trans(handle, inode, 1);

	map->m_flags |= EXT4_MAP_UNWRITTEN;
	if (*allocated > map->m_len)
		*allocated = map->m_len;
	map->m_len = *allocated;
	return 0;
}

static int
ext4_ext_handle_unwritten_extents(handle_t *handle, struct inode *inode,
			struct ext4_map_blocks *map,
			struct ext4_ext_path **ppath, int flags,
			unsigned int allocated, ext4_fsblk_t newblock)
{
	struct ext4_ext_path __maybe_unused *path = *ppath;
	int ret = 0;
	int err = 0;

	ext_debug(inode, "logical block %llu, max_blocks %u, flags 0x%x, allocated %u\n",
		  (unsigned long long)map->m_lblk, map->m_len, flags,
		  allocated);
	ext4_ext_show_leaf(inode, path);

	/*
	 * When writing into unwritten space, we should not fail to
	 * allocate metadata blocks for the new extent block if needed.
	 */
	flags |= EXT4_GET_BLOCKS_METADATA_NOFAIL;

	trace_ext4_ext_handle_unwritten_extents(inode, map, flags,
						    allocated, newblock);

	/* get_block() before submitting IO, split the extent */
	if (flags & EXT4_GET_BLOCKS_PRE_IO) {
		ret = ext4_split_convert_extents(handle, inode, map, ppath,
					 flags | EXT4_GET_BLOCKS_CONVERT);
		if (ret < 0) {
			err = ret;
			goto out2;
		}
		/*
		 * shouldn't get a 0 return when splitting an extent unless
		 * m_len is 0 (bug) or extent has been corrupted
		 */
		if (unlikely(ret == 0)) {
			EXT4_ERROR_INODE(inode,
					 "unexpected ret == 0, m_len = %u",
					 map->m_len);
			err = -EFSCORRUPTED;
			goto out2;
		}
		map->m_flags |= EXT4_MAP_UNWRITTEN;
		goto out;
	}
	/* IO end_io complete, convert the filled extent to written */
	if (flags & EXT4_GET_BLOCKS_CONVERT) {
		err = ext4_convert_unwritten_extents_endio(handle, inode, map,
							   ppath);
		if (err < 0)
			goto out2;
		ext4_update_inode_fsync_trans(handle, inode, 1);
		goto map_out;
	}
	/* buffered IO cases */
	/*
	 * repeat fallocate creation request
	 * we already have an unwritten extent
	 */
	if (flags & EXT4_GET_BLOCKS_UNWRIT_EXT) {
		map->m_flags |= EXT4_MAP_UNWRITTEN;
		goto map_out;
	}

	/* buffered READ or buffered write_begin() lookup */
	if ((flags & EXT4_GET_BLOCKS_CREATE) == 0) {
		/*
		 * We have blocks reserved already.  We
		 * return allocated blocks so that delalloc
		 * won't do block reservation for us.  But
		 * the buffer head will be unmapped so that
		 * a read from the block returns 0s.
		 */
		map->m_flags |= EXT4_MAP_UNWRITTEN;
		goto out1;
	}

	/*
	 * Default case when (flags & EXT4_GET_BLOCKS_CREATE) == 1.
	 * For buffered writes, at writepage time, etc.  Convert a
	 * discovered unwritten extent to written.
	 */
	ret = ext4_ext_convert_to_initialized(handle, inode, map, ppath, flags);
	if (ret < 0) {
		err = ret;
		goto out2;
	}
	ext4_update_inode_fsync_trans(handle, inode, 1);
	/*
	 * shouldn't get a 0 return when converting an unwritten extent
	 * unless m_len is 0 (bug) or extent has been corrupted
	 */
	if (unlikely(ret == 0)) {
		EXT4_ERROR_INODE(inode, "unexpected ret == 0, m_len = %u",
				 map->m_len);
		err = -EFSCORRUPTED;
		goto out2;
	}

out:
	allocated = ret;
	map->m_flags |= EXT4_MAP_NEW;
map_out:
	map->m_flags |= EXT4_MAP_MAPPED;
out1:
	map->m_pblk = newblock;
	if (allocated > map->m_len)
		allocated = map->m_len;
	map->m_len = allocated;
	ext4_ext_show_leaf(inode, path);
out2:
	return err ? err : allocated;
}

/*
 * get_implied_cluster_alloc - check to see if the requested
 * allocation (in the map structure) overlaps with a cluster already
 * allocated in an extent.
 *	@sb	The filesystem superblock structure
 *	@map	The requested lblk->pblk mapping
 *	@ex	The extent structure which might contain an implied
 *			cluster allocation
 *
 * This function is called by ext4_ext_map_blocks() after we failed to
 * find blocks that were already in the inode's extent tree.  Hence,
 * we know that the beginning of the requested region cannot overlap
 * the extent from the inode's extent tree.  There are three cases we
 * want to catch.  The first is this case:
 *
 *		 |--- cluster # N--|
 *    |--- extent ---|	|---- requested region ---|
 *			|==========|
 *
 * The second case that we need to test for is this one:
 *
 *   |--------- cluster # N ----------------|
 *	   |--- requested region --|   |------- extent ----|
 *	   |=======================|
 *
 * The third case is when the requested region lies between two extents
 * within the same cluster:
 *          |------------- cluster # N-------------|
 * |----- ex -----|                  |---- ex_right ----|
 *                  |------ requested region ------|
 *                  |================|
 *
 * In each of the above cases, we need to set the map->m_pblk and
 * map->m_len so it corresponds to the return the extent labelled as
 * "|====|" from cluster #N, since it is already in use for data in
 * cluster EXT4_B2C(sbi, map->m_lblk).	We will then return 1 to
 * signal to ext4_ext_map_blocks() that map->m_pblk should be treated
 * as a new "allocated" block region.  Otherwise, we will return 0 and
 * ext4_ext_map_blocks() will then allocate one or more new clusters
 * by calling ext4_mb_new_blocks().
 */
static int get_implied_cluster_alloc(struct super_block *sb,
				     struct ext4_map_blocks *map,
				     struct ext4_extent *ex,
				     struct ext4_ext_path *path)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_lblk_t c_offset = EXT4_LBLK_COFF(sbi, map->m_lblk);
	ext4_lblk_t ex_cluster_start, ex_cluster_end;
	ext4_lblk_t rr_cluster_start;
	ext4_lblk_t ee_block = le32_to_cpu(ex->ee_block);
	ext4_fsblk_t ee_start = ext4_ext_pblock(ex);
	unsigned short ee_len = ext4_ext_get_actual_len(ex);

	/* The extent passed in that we are trying to match */
	ex_cluster_start = EXT4_B2C(sbi, ee_block);
	ex_cluster_end = EXT4_B2C(sbi, ee_block + ee_len - 1);

	/* The requested region passed into ext4_map_blocks() */
	rr_cluster_start = EXT4_B2C(sbi, map->m_lblk);

	if ((rr_cluster_start == ex_cluster_end) ||
	    (rr_cluster_start == ex_cluster_start)) {
		if (rr_cluster_start == ex_cluster_end)
			ee_start += ee_len - 1;
		map->m_pblk = EXT4_PBLK_CMASK(sbi, ee_start) + c_offset;
		map->m_len = min(map->m_len,
				 (unsigned) sbi->s_cluster_ratio - c_offset);
		/*
		 * Check for and handle this case:
		 *
		 *   |--------- cluster # N-------------|
		 *		       |------- extent ----|
		 *	   |--- requested region ---|
		 *	   |===========|
		 */

		if (map->m_lblk < ee_block)
			map->m_len = min(map->m_len, ee_block - map->m_lblk);

		/*
		 * Check for the case where there is already another allocated
		 * block to the right of 'ex' but before the end of the cluster.
		 *
		 *          |------------- cluster # N-------------|
		 * |----- ex -----|                  |---- ex_right ----|
		 *                  |------ requested region ------|
		 *                  |================|
		 */
		if (map->m_lblk > ee_block) {
			ext4_lblk_t next = ext4_ext_next_allocated_block(path);
			map->m_len = min(map->m_len, next - map->m_lblk);
		}

		trace_ext4_get_implied_cluster_alloc_exit(sb, map, 1);
		return 1;
	}

	trace_ext4_get_implied_cluster_alloc_exit(sb, map, 0);
	return 0;
}


/*
 * Block allocation/map/preallocation routine for extents based files
 *
 *
 * Need to be called with
 * down_read(&EXT4_I(inode)->i_data_sem) if not allocating file system block
 * (ie, create is zero). Otherwise down_write(&EXT4_I(inode)->i_data_sem)
 *
 * return > 0, number of blocks already mapped/allocated
 *          if create == 0 and these are pre-allocated blocks
 *          	buffer head is unmapped
 *          otherwise blocks are mapped
 *
 * return = 0, if plain look up failed (blocks have not been allocated)
 *          buffer head is unmapped
 *
 * return < 0, error case.
 */
int ext4_ext_map_blocks(handle_t *handle, struct inode *inode,
			struct ext4_map_blocks *map, int flags)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent newex, *ex, ex2;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	ext4_fsblk_t newblock = 0, pblk;
	int err = 0, depth, ret;
	unsigned int allocated = 0, offset = 0;
	unsigned int allocated_clusters = 0;
	struct ext4_allocation_request ar;
	ext4_lblk_t cluster_offset;

	ext_debug(inode, "blocks %u/%u requested\n", map->m_lblk, map->m_len);
	trace_ext4_ext_map_blocks_enter(inode, map->m_lblk, map->m_len, flags);

	/* find extent for this block */
	path = ext4_find_extent(inode, map->m_lblk, NULL, 0);
	if (IS_ERR(path)) {
		err = PTR_ERR(path);
		path = NULL;
		goto out;
	}

	depth = ext_depth(inode);

	/*
	 * consistent leaf must not be empty;
	 * this situation is possible, though, _during_ tree modification;
	 * this is why assert can't be put in ext4_find_extent()
	 */
	if (unlikely(path[depth].p_ext == NULL && depth != 0)) {
		EXT4_ERROR_INODE(inode, "bad extent address "
				 "lblock: %lu, depth: %d pblock %lld",
				 (unsigned long) map->m_lblk, depth,
				 path[depth].p_block);
		err = -EFSCORRUPTED;
		goto out;
	}

	ex = path[depth].p_ext;
	if (ex) {
		ext4_lblk_t ee_block = le32_to_cpu(ex->ee_block);
		ext4_fsblk_t ee_start = ext4_ext_pblock(ex);
		unsigned short ee_len;


		/*
		 * unwritten extents are treated as holes, except that
		 * we split out initialized portions during a write.
		 */
		ee_len = ext4_ext_get_actual_len(ex);

		trace_ext4_ext_show_extent(inode, ee_block, ee_start, ee_len);

		/* if found extent covers block, simply return it */
		if (in_range(map->m_lblk, ee_block, ee_len)) {
			newblock = map->m_lblk - ee_block + ee_start;
			/* number of remaining blocks in the extent */
			allocated = ee_len - (map->m_lblk - ee_block);
			ext_debug(inode, "%u fit into %u:%d -> %llu\n",
				  map->m_lblk, ee_block, ee_len, newblock);

			/*
			 * If the extent is initialized check whether the
			 * caller wants to convert it to unwritten.
			 */
			if ((!ext4_ext_is_unwritten(ex)) &&
			    (flags & EXT4_GET_BLOCKS_CONVERT_UNWRITTEN)) {
				err = convert_initialized_extent(handle,
					inode, map, &path, &allocated);
				goto out;
			} else if (!ext4_ext_is_unwritten(ex)) {
				map->m_flags |= EXT4_MAP_MAPPED;
				map->m_pblk = newblock;
				if (allocated > map->m_len)
					allocated = map->m_len;
				map->m_len = allocated;
				ext4_ext_show_leaf(inode, path);
				goto out;
			}

			ret = ext4_ext_handle_unwritten_extents(
				handle, inode, map, &path, flags,
				allocated, newblock);
			if (ret < 0)
				err = ret;
			else
				allocated = ret;
			goto out;
		}
	}

	/*
	 * requested block isn't allocated yet;
	 * we couldn't try to create block if create flag is zero
	 */
	if ((flags & EXT4_GET_BLOCKS_CREATE) == 0) {
		ext4_lblk_t hole_start, hole_len;

		hole_start = map->m_lblk;
		hole_len = ext4_ext_determine_hole(inode, path, &hole_start);
		/*
		 * put just found gap into cache to speed up
		 * subsequent requests
		 */
		ext4_ext_put_gap_in_cache(inode, hole_start, hole_len);

		/* Update hole_len to reflect hole size after map->m_lblk */
		if (hole_start != map->m_lblk)
			hole_len -= map->m_lblk - hole_start;
		map->m_pblk = 0;
		map->m_len = min_t(unsigned int, map->m_len, hole_len);

		goto out;
	}

	/*
	 * Okay, we need to do block allocation.
	 */
	newex.ee_block = cpu_to_le32(map->m_lblk);
	cluster_offset = EXT4_LBLK_COFF(sbi, map->m_lblk);

	/*
	 * If we are doing bigalloc, check to see if the extent returned
	 * by ext4_find_extent() implies a cluster we can use.
	 */
	if (cluster_offset && ex &&
	    get_implied_cluster_alloc(inode->i_sb, map, ex, path)) {
		ar.len = allocated = map->m_len;
		newblock = map->m_pblk;
		goto got_allocated_blocks;
	}

	/* find neighbour allocated blocks */
	ar.lleft = map->m_lblk;
	err = ext4_ext_search_left(inode, path, &ar.lleft, &ar.pleft);
	if (err)
		goto out;
	ar.lright = map->m_lblk;
	err = ext4_ext_search_right(inode, path, &ar.lright, &ar.pright, &ex2);
	if (err < 0)
		goto out;

	/* Check if the extent after searching to the right implies a
	 * cluster we can use. */
	if ((sbi->s_cluster_ratio > 1) && err &&
	    get_implied_cluster_alloc(inode->i_sb, map, &ex2, path)) {
		ar.len = allocated = map->m_len;
		newblock = map->m_pblk;
		goto got_allocated_blocks;
	}

	/*
	 * See if request is beyond maximum number of blocks we can have in
	 * a single extent. For an initialized extent this limit is
	 * EXT_INIT_MAX_LEN and for an unwritten extent this limit is
	 * EXT_UNWRITTEN_MAX_LEN.
	 */
	if (map->m_len > EXT_INIT_MAX_LEN &&
	    !(flags & EXT4_GET_BLOCKS_UNWRIT_EXT))
		map->m_len = EXT_INIT_MAX_LEN;
	else if (map->m_len > EXT_UNWRITTEN_MAX_LEN &&
		 (flags & EXT4_GET_BLOCKS_UNWRIT_EXT))
		map->m_len = EXT_UNWRITTEN_MAX_LEN;

	/* Check if we can really insert (m_lblk)::(m_lblk + m_len) extent */
	newex.ee_len = cpu_to_le16(map->m_len);
	err = ext4_ext_check_overlap(sbi, inode, &newex, path);
	if (err)
		allocated = ext4_ext_get_actual_len(&newex);
	else
		allocated = map->m_len;

	/* allocate new block */
	ar.inode = inode;
	ar.goal = ext4_ext_find_goal(inode, path, map->m_lblk);
	ar.logical = map->m_lblk;
	/*
	 * We calculate the offset from the beginning of the cluster
	 * for the logical block number, since when we allocate a
	 * physical cluster, the physical block should start at the
	 * same offset from the beginning of the cluster.  This is
	 * needed so that future calls to get_implied_cluster_alloc()
	 * work correctly.
	 */
	offset = EXT4_LBLK_COFF(sbi, map->m_lblk);
	ar.len = EXT4_NUM_B2C(sbi, offset+allocated);
	ar.goal -= offset;
	ar.logical -= offset;
	if (S_ISREG(inode->i_mode))
		ar.flags = EXT4_MB_HINT_DATA;
	else
		/* disable in-core preallocation for non-regular files */
		ar.flags = 0;
	if (flags & EXT4_GET_BLOCKS_NO_NORMALIZE)
		ar.flags |= EXT4_MB_HINT_NOPREALLOC;
	if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		ar.flags |= EXT4_MB_DELALLOC_RESERVED;
	if (flags & EXT4_GET_BLOCKS_METADATA_NOFAIL)
		ar.flags |= EXT4_MB_USE_RESERVED;
	newblock = ext4_mb_new_blocks(handle, &ar, &err);
	if (!newblock)
		goto out;
	allocated_clusters = ar.len;
	ar.len = EXT4_C2B(sbi, ar.len) - offset;
	ext_debug(inode, "allocate new block: goal %llu, found %llu/%u, requested %u\n",
		  ar.goal, newblock, ar.len, allocated);
	if (ar.len > allocated)
		ar.len = allocated;

got_allocated_blocks:
	/* try to insert new extent into found leaf and return */
	pblk = newblock + offset;
	ext4_ext_store_pblock(&newex, pblk);
	newex.ee_len = cpu_to_le16(ar.len);
	/* Mark unwritten */
	if (flags & EXT4_GET_BLOCKS_UNWRIT_EXT) {
		ext4_ext_mark_unwritten(&newex);
		map->m_flags |= EXT4_MAP_UNWRITTEN;
	}

	err = ext4_ext_insert_extent(handle, inode, &path, &newex, flags);
	if (err) {
		if (allocated_clusters) {
			int fb_flags = 0;

			/*
			 * free data blocks we just allocated.
			 * not a good idea to call discard here directly,
			 * but otherwise we'd need to call it every free().
			 */
			ext4_discard_preallocations(inode, 0);
			if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
				fb_flags = EXT4_FREE_BLOCKS_NO_QUOT_UPDATE;
			ext4_free_blocks(handle, inode, NULL, newblock,
					 EXT4_C2B(sbi, allocated_clusters),
					 fb_flags);
		}
		goto out;
	}

	/*
	 * Reduce the reserved cluster count to reflect successful deferred
	 * allocation of delayed allocated clusters or direct allocation of
	 * clusters discovered to be delayed allocated.  Once allocated, a
	 * cluster is not included in the reserved count.
	 */
	if (test_opt(inode->i_sb, DELALLOC) && allocated_clusters) {
		if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE) {
			/*
			 * When allocating delayed allocated clusters, simply
			 * reduce the reserved cluster count and claim quota
			 */
			ext4_da_update_reserve_space(inode, allocated_clusters,
							1);
		} else {
			ext4_lblk_t lblk, len;
			unsigned int n;

			/*
			 * When allocating non-delayed allocated clusters
			 * (from fallocate, filemap, DIO, or clusters
			 * allocated when delalloc has been disabled by
			 * ext4_nonda_switch), reduce the reserved cluster
			 * count by the number of allocated clusters that
			 * have previously been delayed allocated.  Quota
			 * has been claimed by ext4_mb_new_blocks() above,
			 * so release the quota reservations made for any
			 * previously delayed allocated clusters.
			 */
			lblk = EXT4_LBLK_CMASK(sbi, map->m_lblk);
			len = allocated_clusters << sbi->s_cluster_bits;
			n = ext4_es_delayed_clu(inode, lblk, len);
			if (n > 0)
				ext4_da_update_reserve_space(inode, (int) n, 0);
		}
	}

	/*
	 * Cache the extent and update transaction to commit on fdatasync only
	 * when it is _not_ an unwritten extent.
	 */
	if ((flags & EXT4_GET_BLOCKS_UNWRIT_EXT) == 0)
		ext4_update_inode_fsync_trans(handle, inode, 1);
	else
		ext4_update_inode_fsync_trans(handle, inode, 0);

	map->m_flags |= (EXT4_MAP_NEW | EXT4_MAP_MAPPED);
	map->m_pblk = pblk;
	map->m_len = ar.len;
	allocated = map->m_len;
	ext4_ext_show_leaf(inode, path);
out:
	ext4_free_ext_path(path);

	trace_ext4_ext_map_blocks_exit(inode, flags, map,
				       err ? err : allocated);
	return err ? err : allocated;
}

int ext4_ext_truncate(handle_t *handle, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	ext4_lblk_t last_block;
	int err = 0;

	/*
	 * TODO: optimization is possible here.
	 * Probably we need not scan at all,
	 * because page truncation is enough.
	 */

	/* we have to know where to truncate from in crash case */
	EXT4_I(inode)->i_disksize = inode->i_size;
	err = ext4_mark_inode_dirty(handle, inode);
	if (err)
		return err;

	last_block = (inode->i_size + sb->s_blocksize - 1)
			>> EXT4_BLOCK_SIZE_BITS(sb);
retry:
	err = ext4_es_remove_extent(inode, last_block,
				    EXT_MAX_BLOCKS - last_block);
	if (err == -ENOMEM) {
		memalloc_retry_wait(GFP_ATOMIC);
		goto retry;
	}
	if (err)
		return err;
retry_remove_space:
	err = ext4_ext_remove_space(inode, last_block, EXT_MAX_BLOCKS - 1);
	if (err == -ENOMEM) {
		memalloc_retry_wait(GFP_ATOMIC);
		goto retry_remove_space;
	}
	return err;
}

static int ext4_alloc_file_blocks(struct file *file, ext4_lblk_t offset,
				  ext4_lblk_t len, loff_t new_size,
				  int flags)
{
	struct inode *inode = file_inode(file);
	handle_t *handle;
	int ret = 0, ret2 = 0, ret3 = 0;
	int retries = 0;
	int depth = 0;
	struct ext4_map_blocks map;
	unsigned int credits;
	loff_t epos;

	BUG_ON(!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS));
	map.m_lblk = offset;
	map.m_len = len;
	/*
	 * Don't normalize the request if it can fit in one extent so
	 * that it doesn't get unnecessarily split into multiple
	 * extents.
	 */
	if (len <= EXT_UNWRITTEN_MAX_LEN)
		flags |= EXT4_GET_BLOCKS_NO_NORMALIZE;

	/*
	 * credits to insert 1 extent into extent tree
	 */
	credits = ext4_chunk_trans_blocks(inode, len);
	depth = ext_depth(inode);

retry:
	while (len) {
		/*
		 * Recalculate credits when extent tree depth changes.
		 */
		if (depth != ext_depth(inode)) {
			credits = ext4_chunk_trans_blocks(inode, len);
			depth = ext_depth(inode);
		}

		handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS,
					    credits);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			break;
		}
		ret = ext4_map_blocks(handle, inode, &map, flags);
		if (ret <= 0) {
			ext4_debug("inode #%lu: block %u: len %u: "
				   "ext4_ext_map_blocks returned %d",
				   inode->i_ino, map.m_lblk,
				   map.m_len, ret);
			ext4_mark_inode_dirty(handle, inode);
			ext4_journal_stop(handle);
			break;
		}
		/*
		 * allow a full retry cycle for any remaining allocations
		 */
		retries = 0;
		map.m_lblk += ret;
		map.m_len = len = len - ret;
		epos = (loff_t)map.m_lblk << inode->i_blkbits;
		inode->i_ctime = current_time(inode);
		if (new_size) {
			if (epos > new_size)
				epos = new_size;
			if (ext4_update_inode_size(inode, epos) & 0x1)
				inode->i_mtime = inode->i_ctime;
		}
		ret2 = ext4_mark_inode_dirty(handle, inode);
		ext4_update_inode_fsync_trans(handle, inode, 1);
		ret3 = ext4_journal_stop(handle);
		ret2 = ret3 ? ret3 : ret2;
		if (unlikely(ret2))
			break;
	}
	if (ret == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;

	return ret > 0 ? ret2 : ret;
}

static int ext4_collapse_range(struct file *file, loff_t offset, loff_t len);

static int ext4_insert_range(struct file *file, loff_t offset, loff_t len);

static long ext4_zero_range(struct file *file, loff_t offset,
			    loff_t len, int mode)
{
	struct inode *inode = file_inode(file);
	struct address_space *mapping = file->f_mapping;
	handle_t *handle = NULL;
	unsigned int max_blocks;
	loff_t new_size = 0;
	int ret = 0;
	int flags;
	int credits;
	int partial_begin, partial_end;
	loff_t start, end;
	ext4_lblk_t lblk;
	unsigned int blkbits = inode->i_blkbits;

	trace_ext4_zero_range(inode, offset, len, mode);

	/*
	 * Round up offset. This is not fallocate, we need to zero out
	 * blocks, so convert interior block aligned part of the range to
	 * unwritten and possibly manually zero out unaligned parts of the
	 * range.
	 */
	start = round_up(offset, 1 << blkbits);
	end = round_down((offset + len), 1 << blkbits);

	if (start < offset || end > offset + len)
		return -EINVAL;
	partial_begin = offset & ((1 << blkbits) - 1);
	partial_end = (offset + len) & ((1 << blkbits) - 1);

	lblk = start >> blkbits;
	max_blocks = (end >> blkbits);
	if (max_blocks < lblk)
		max_blocks = 0;
	else
		max_blocks -= lblk;

	inode_lock(inode);

	/*
	 * Indirect files do not support unwritten extents
	 */
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))) {
		ret = -EOPNOTSUPP;
		goto out_mutex;
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) &&
	    (offset + len > inode->i_size ||
	     offset + len > EXT4_I(inode)->i_disksize)) {
		new_size = offset + len;
		ret = inode_newsize_ok(inode, new_size);
		if (ret)
			goto out_mutex;
	}

	flags = EXT4_GET_BLOCKS_CREATE_UNWRIT_EXT;

	/* Wait all existing dio workers, newcomers will block on i_rwsem */
	inode_dio_wait(inode);

	ret = file_modified(file);
	if (ret)
		goto out_mutex;

	/* Preallocate the range including the unaligned edges */
	if (partial_begin || partial_end) {
		ret = ext4_alloc_file_blocks(file,
				round_down(offset, 1 << blkbits) >> blkbits,
				(round_up((offset + len), 1 << blkbits) -
				 round_down(offset, 1 << blkbits)) >> blkbits,
				new_size, flags);
		if (ret)
			goto out_mutex;

	}

	/* Zero range excluding the unaligned edges */
	if (max_blocks > 0) {
		flags |= (EXT4_GET_BLOCKS_CONVERT_UNWRITTEN |
			  EXT4_EX_NOCACHE);

		/*
		 * Prevent page faults from reinstantiating pages we have
		 * released from page cache.
		 */
		filemap_invalidate_lock(mapping);

		ret = ext4_break_layouts(inode);
		if (ret) {
			filemap_invalidate_unlock(mapping);
			goto out_mutex;
		}

		ret = ext4_update_disksize_before_punch(inode, offset, len);
		if (ret) {
			filemap_invalidate_unlock(mapping);
			goto out_mutex;
		}

		/*
		 * For journalled data we need to write (and checkpoint) pages
		 * before discarding page cache to avoid inconsitent data on
		 * disk in case of crash before zeroing trans is committed.
		 */
		if (ext4_should_journal_data(inode)) {
			ret = filemap_write_and_wait_range(mapping, start, end);
			if (ret) {
				filemap_invalidate_unlock(mapping);
				goto out_mutex;
			}
		}

		/* Now release the pages and zero block aligned part of pages */
		truncate_pagecache_range(inode, start, end - 1);
		inode->i_mtime = inode->i_ctime = current_time(inode);

		ret = ext4_alloc_file_blocks(file, lblk, max_blocks, new_size,
					     flags);
		filemap_invalidate_unlock(mapping);
		if (ret)
			goto out_mutex;
	}
	if (!partial_begin && !partial_end)
		goto out_mutex;

	/*
	 * In worst case we have to writeout two nonadjacent unwritten
	 * blocks and update the inode
	 */
	credits = (2 * ext4_ext_index_trans_blocks(inode, 2)) + 1;
	if (ext4_should_journal_data(inode))
		credits += 2;
	handle = ext4_journal_start(inode, EXT4_HT_MISC, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		ext4_std_error(inode->i_sb, ret);
		goto out_mutex;
	}

	inode->i_mtime = inode->i_ctime = current_time(inode);
	if (new_size)
		ext4_update_inode_size(inode, new_size);
	ret = ext4_mark_inode_dirty(handle, inode);
	if (unlikely(ret))
		goto out_handle;
	/* Zero out partial block at the edges of the range */
	ret = ext4_zero_partial_blocks(handle, inode, offset, len);
	if (ret >= 0)
		ext4_update_inode_fsync_trans(handle, inode, 1);

	if (file->f_flags & O_SYNC)
		ext4_handle_sync(handle);

out_handle:
	ext4_journal_stop(handle);
out_mutex:
	inode_unlock(inode);
	return ret;
}

/*
 * preallocate space for a file. This implements ext4's fallocate file
 * operation, which gets called from sys_fallocate system call.
 * For block-mapped files, posix_fallocate should fall back to the method
 * of writing zeroes to the required new blocks (the same behavior which is
 * expected for file systems which do not support fallocate() system call).
 */
long ext4_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	loff_t new_size = 0;
	unsigned int max_blocks;
	int ret = 0;
	int flags;
	ext4_lblk_t lblk;
	unsigned int blkbits = inode->i_blkbits;

	/*
	 * Encrypted inodes can't handle collapse range or insert
	 * range since we would need to re-encrypt blocks with a
	 * different IV or XTS tweak (which are based on the logical
	 * block number).
	 */
	if (IS_ENCRYPTED(inode) &&
	    (mode & (FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_INSERT_RANGE)))
		return -EOPNOTSUPP;

	/* Return error if mode is not supported */
	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
		     FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |
		     FALLOC_FL_INSERT_RANGE))
		return -EOPNOTSUPP;

	inode_lock(inode);
	ret = ext4_convert_inline_data(inode);
	inode_unlock(inode);
	if (ret)
		goto exit;

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		ret = ext4_punch_hole(file, offset, len);
		goto exit;
	}

	if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		ret = ext4_collapse_range(file, offset, len);
		goto exit;
	}

	if (mode & FALLOC_FL_INSERT_RANGE) {
		ret = ext4_insert_range(file, offset, len);
		goto exit;
	}

	if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = ext4_zero_range(file, offset, len, mode);
		goto exit;
	}
	trace_ext4_fallocate_enter(inode, offset, len, mode);
	lblk = offset >> blkbits;

	max_blocks = EXT4_MAX_BLOCKS(len, offset, blkbits);
	flags = EXT4_GET_BLOCKS_CREATE_UNWRIT_EXT;

	inode_lock(inode);

	/*
	 * We only support preallocation for extent-based files only
	 */
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) &&
	    (offset + len > inode->i_size ||
	     offset + len > EXT4_I(inode)->i_disksize)) {
		new_size = offset + len;
		ret = inode_newsize_ok(inode, new_size);
		if (ret)
			goto out;
	}

	/* Wait all existing dio workers, newcomers will block on i_rwsem */
	inode_dio_wait(inode);

	ret = file_modified(file);
	if (ret)
		goto out;

	ret = ext4_alloc_file_blocks(file, lblk, max_blocks, new_size, flags);
	if (ret)
		goto out;

	if (file->f_flags & O_SYNC && EXT4_SB(inode->i_sb)->s_journal) {
		ret = ext4_fc_commit(EXT4_SB(inode->i_sb)->s_journal,
					EXT4_I(inode)->i_sync_tid);
	}
out:
	inode_unlock(inode);
	trace_ext4_fallocate_exit(inode, offset, max_blocks, ret);
exit:
	return ret;
}

/*
 * This function convert a range of blocks to written extents
 * The caller of this function will pass the start offset and the size.
 * all unwritten extents within this range will be converted to
 * written extents.
 *
 * This function is called from the direct IO end io call back
 * function, to convert the fallocated extents after IO is completed.
 * Returns 0 on success.
 */
int ext4_convert_unwritten_extents(handle_t *handle, struct inode *inode,
				   loff_t offset, ssize_t len)
{
	unsigned int max_blocks;
	int ret = 0, ret2 = 0, ret3 = 0;
	struct ext4_map_blocks map;
	unsigned int blkbits = inode->i_blkbits;
	unsigned int credits = 0;

	map.m_lblk = offset >> blkbits;
	max_blocks = EXT4_MAX_BLOCKS(len, offset, blkbits);

	if (!handle) {
		/*
		 * credits to insert 1 extent into extent tree
		 */
		credits = ext4_chunk_trans_blocks(inode, max_blocks);
	}
	while (ret >= 0 && ret < max_blocks) {
		map.m_lblk += ret;
		map.m_len = (max_blocks -= ret);
		if (credits) {
			handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS,
						    credits);
			if (IS_ERR(handle)) {
				ret = PTR_ERR(handle);
				break;
			}
		}
		ret = ext4_map_blocks(handle, inode, &map,
				      EXT4_GET_BLOCKS_IO_CONVERT_EXT);
		if (ret <= 0)
			ext4_warning(inode->i_sb,
				     "inode #%lu: block %u: len %u: "
				     "ext4_ext_map_blocks returned %d",
				     inode->i_ino, map.m_lblk,
				     map.m_len, ret);
		ret2 = ext4_mark_inode_dirty(handle, inode);
		if (credits) {
			ret3 = ext4_journal_stop(handle);
			if (unlikely(ret3))
				ret2 = ret3;
		}

		if (ret <= 0 || ret2)
			break;
	}
	return ret > 0 ? ret2 : ret;
}

int ext4_convert_unwritten_io_end_vec(handle_t *handle, ext4_io_end_t *io_end)
{
	int ret = 0, err = 0;
	struct ext4_io_end_vec *io_end_vec;

	/*
	 * This is somewhat ugly but the idea is clear: When transaction is
	 * reserved, everything goes into it. Otherwise we rather start several
	 * smaller transactions for conversion of each extent separately.
	 */
	if (handle) {
		handle = ext4_journal_start_reserved(handle,
						     EXT4_HT_EXT_CONVERT);
		if (IS_ERR(handle))
			return PTR_ERR(handle);
	}

	list_for_each_entry(io_end_vec, &io_end->list_vec, list) {
		ret = ext4_convert_unwritten_extents(handle, io_end->inode,
						     io_end_vec->offset,
						     io_end_vec->size);
		if (ret)
			break;
	}

	if (handle)
		err = ext4_journal_stop(handle);

	return ret < 0 ? ret : err;
}

static int ext4_iomap_xattr_fiemap(struct inode *inode, struct iomap *iomap)
{
	__u64 physical = 0;
	__u64 length = 0;
	int blockbits = inode->i_sb->s_blocksize_bits;
	int error = 0;
	u16 iomap_type;

	/* in-inode? */
	if (ext4_test_inode_state(inode, EXT4_STATE_XATTR)) {
		struct ext4_iloc iloc;
		int offset;	/* offset of xattr in inode */

		error = ext4_get_inode_loc(inode, &iloc);
		if (error)
			return error;
		physical = (__u64)iloc.bh->b_blocknr << blockbits;
		offset = EXT4_GOOD_OLD_INODE_SIZE +
				EXT4_I(inode)->i_extra_isize;
		physical += offset;
		length = EXT4_SB(inode->i_sb)->s_inode_size - offset;
		brelse(iloc.bh);
		iomap_type = IOMAP_INLINE;
	} else if (EXT4_I(inode)->i_file_acl) { /* external block */
		physical = (__u64)EXT4_I(inode)->i_file_acl << blockbits;
		length = inode->i_sb->s_blocksize;
		iomap_type = IOMAP_MAPPED;
	} else {
		/* no in-inode or external block for xattr, so return -ENOENT */
		error = -ENOENT;
		goto out;
	}

	iomap->addr = physical;
	iomap->offset = 0;
	iomap->length = length;
	iomap->type = iomap_type;
	iomap->flags = 0;
out:
	return error;
}

static int ext4_iomap_xattr_begin(struct inode *inode, loff_t offset,
				  loff_t length, unsigned flags,
				  struct iomap *iomap, struct iomap *srcmap)
{
	int error;

	error = ext4_iomap_xattr_fiemap(inode, iomap);
	if (error == 0 && (offset >= iomap->length))
		error = -ENOENT;
	return error;
}

static const struct iomap_ops ext4_iomap_xattr_ops = {
	.iomap_begin		= ext4_iomap_xattr_begin,
};

static int ext4_fiemap_check_ranges(struct inode *inode, u64 start, u64 *len)
{
	u64 maxbytes;

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		maxbytes = inode->i_sb->s_maxbytes;
	else
		maxbytes = EXT4_SB(inode->i_sb)->s_bitmap_maxbytes;

	if (*len == 0)
		return -EINVAL;
	if (start > maxbytes)
		return -EFBIG;

	/*
	 * Shrink request scope to what the fs can actually handle.
	 */
	if (*len > maxbytes || (maxbytes - *len) < start)
		*len = maxbytes - start;
	return 0;
}

int ext4_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	int error = 0;

	if (fieinfo->fi_flags & FIEMAP_FLAG_CACHE) {
		error = ext4_ext_precache(inode);
		if (error)
			return error;
		fieinfo->fi_flags &= ~FIEMAP_FLAG_CACHE;
	}

	/*
	 * For bitmap files the maximum size limit could be smaller than
	 * s_maxbytes, so check len here manually instead of just relying on the
	 * generic check.
	 */
	error = ext4_fiemap_check_ranges(inode, start, &len);
	if (error)
		return error;

	if (fieinfo->fi_flags & FIEMAP_FLAG_XATTR) {
		fieinfo->fi_flags &= ~FIEMAP_FLAG_XATTR;
		return iomap_fiemap(inode, fieinfo, start, len,
				    &ext4_iomap_xattr_ops);
	}

	return iomap_fiemap(inode, fieinfo, start, len, &ext4_iomap_report_ops);
}

int ext4_get_es_cache(struct inode *inode, struct fiemap_extent_info *fieinfo,
		      __u64 start, __u64 len)
{
	ext4_lblk_t start_blk, len_blks;
	__u64 last_blk;
	int error = 0;

	if (ext4_has_inline_data(inode)) {
		int has_inline;

		down_read(&EXT4_I(inode)->xattr_sem);
		has_inline = ext4_has_inline_data(inode);
		up_read(&EXT4_I(inode)->xattr_sem);
		if (has_inline)
			return 0;
	}

	if (fieinfo->fi_flags & FIEMAP_FLAG_CACHE) {
		error = ext4_ext_precache(inode);
		if (error)
			return error;
		fieinfo->fi_flags &= ~FIEMAP_FLAG_CACHE;
	}

	error = fiemap_prep(inode, fieinfo, start, &len, 0);
	if (error)
		return error;

	error = ext4_fiemap_check_ranges(inode, start, &len);
	if (error)
		return error;

	start_blk = start >> inode->i_sb->s_blocksize_bits;
	last_blk = (start + len - 1) >> inode->i_sb->s_blocksize_bits;
	if (last_blk >= EXT_MAX_BLOCKS)
		last_blk = EXT_MAX_BLOCKS-1;
	len_blks = ((ext4_lblk_t) last_blk) - start_blk + 1;

	/*
	 * Walk the extent tree gathering extent information
	 * and pushing extents back to the user.
	 */
	return ext4_fill_es_cache_info(inode, start_blk, len_blks, fieinfo);
}

/*
 * ext4_ext_shift_path_extents:
 * Shift the extents of a path structure lying between path[depth].p_ext
 * and EXT_LAST_EXTENT(path[depth].p_hdr), by @shift blocks. @SHIFT tells
 * if it is right shift or left shift operation.
 */
static int
ext4_ext_shift_path_extents(struct ext4_ext_path *path, ext4_lblk_t shift,
			    struct inode *inode, handle_t *handle,
			    enum SHIFT_DIRECTION SHIFT)
{
	int depth, err = 0;
	struct ext4_extent *ex_start, *ex_last;
	bool update = false;
	int credits, restart_credits;
	depth = path->p_depth;

	while (depth >= 0) {
		if (depth == path->p_depth) {
			ex_start = path[depth].p_ext;
			if (!ex_start)
				return -EFSCORRUPTED;

			ex_last = EXT_LAST_EXTENT(path[depth].p_hdr);
			/* leaf + sb + inode */
			credits = 3;
			if (ex_start == EXT_FIRST_EXTENT(path[depth].p_hdr)) {
				update = true;
				/* extent tree + sb + inode */
				credits = depth + 2;
			}

			restart_credits = ext4_writepage_trans_blocks(inode);
			err = ext4_datasem_ensure_credits(handle, inode, credits,
					restart_credits, 0);
			if (err) {
				if (err > 0)
					err = -EAGAIN;
				goto out;
			}

			err = ext4_ext_get_access(handle, inode, path + depth);
			if (err)
				goto out;

			while (ex_start <= ex_last) {
				if (SHIFT == SHIFT_LEFT) {
					le32_add_cpu(&ex_start->ee_block,
						-shift);
					/* Try to merge to the left. */
					if ((ex_start >
					    EXT_FIRST_EXTENT(path[depth].p_hdr))
					    &&
					    ext4_ext_try_to_merge_right(inode,
					    path, ex_start - 1))
						ex_last--;
					else
						ex_start++;
				} else {
					le32_add_cpu(&ex_last->ee_block, shift);
					ext4_ext_try_to_merge_right(inode, path,
						ex_last);
					ex_last--;
				}
			}
			err = ext4_ext_dirty(handle, inode, path + depth);
			if (err)
				goto out;

			if (--depth < 0 || !update)
				break;
		}

		/* Update index too */
		err = ext4_ext_get_access(handle, inode, path + depth);
		if (err)
			goto out;

		if (SHIFT == SHIFT_LEFT)
			le32_add_cpu(&path[depth].p_idx->ei_block, -shift);
		else
			le32_add_cpu(&path[depth].p_idx->ei_block, shift);
		err = ext4_ext_dirty(handle, inode, path + depth);
		if (err)
			goto out;

		/* we are done if current index is not a starting index */
		if (path[depth].p_idx != EXT_FIRST_INDEX(path[depth].p_hdr))
			break;

		depth--;
	}

out:
	return err;
}

/*
 * ext4_ext_shift_extents:
 * All the extents which lies in the range from @start to the last allocated
 * block for the @inode are shifted either towards left or right (depending
 * upon @SHIFT) by @shift blocks.
 * On success, 0 is returned, error otherwise.
 */
static int
ext4_ext_shift_extents(struct inode *inode, handle_t *handle,
		       ext4_lblk_t start, ext4_lblk_t shift,
		       enum SHIFT_DIRECTION SHIFT)
{
	struct ext4_ext_path *path;
	int ret = 0, depth;
	struct ext4_extent *extent;
	ext4_lblk_t stop, *iterator, ex_start, ex_end;
	ext4_lblk_t tmp = EXT_MAX_BLOCKS;

	/* Let path point to the last extent */
	path = ext4_find_extent(inode, EXT_MAX_BLOCKS - 1, NULL,
				EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return PTR_ERR(path);

	depth = path->p_depth;
	extent = path[depth].p_ext;
	if (!extent)
		goto out;

	stop = le32_to_cpu(extent->ee_block);

       /*
	* For left shifts, make sure the hole on the left is big enough to
	* accommodate the shift.  For right shifts, make sure the last extent
	* won't be shifted beyond EXT_MAX_BLOCKS.
	*/
	if (SHIFT == SHIFT_LEFT) {
		path = ext4_find_extent(inode, start - 1, &path,
					EXT4_EX_NOCACHE);
		if (IS_ERR(path))
			return PTR_ERR(path);
		depth = path->p_depth;
		extent =  path[depth].p_ext;
		if (extent) {
			ex_start = le32_to_cpu(extent->ee_block);
			ex_end = le32_to_cpu(extent->ee_block) +
				ext4_ext_get_actual_len(extent);
		} else {
			ex_start = 0;
			ex_end = 0;
		}

		if ((start == ex_start && shift > ex_start) ||
		    (shift > start - ex_end)) {
			ret = -EINVAL;
			goto out;
		}
	} else {
		if (shift > EXT_MAX_BLOCKS -
		    (stop + ext4_ext_get_actual_len(extent))) {
			ret = -EINVAL;
			goto out;
		}
	}

	/*
	 * In case of left shift, iterator points to start and it is increased
	 * till we reach stop. In case of right shift, iterator points to stop
	 * and it is decreased till we reach start.
	 */
again:
	ret = 0;
	if (SHIFT == SHIFT_LEFT)
		iterator = &start;
	else
		iterator = &stop;

	if (tmp != EXT_MAX_BLOCKS)
		*iterator = tmp;

	/*
	 * Its safe to start updating extents.  Start and stop are unsigned, so
	 * in case of right shift if extent with 0 block is reached, iterator
	 * becomes NULL to indicate the end of the loop.
	 */
	while (iterator && start <= stop) {
		path = ext4_find_extent(inode, *iterator, &path,
					EXT4_EX_NOCACHE);
		if (IS_ERR(path))
			return PTR_ERR(path);
		depth = path->p_depth;
		extent = path[depth].p_ext;
		if (!extent) {
			EXT4_ERROR_INODE(inode, "unexpected hole at %lu",
					 (unsigned long) *iterator);
			return -EFSCORRUPTED;
		}
		if (SHIFT == SHIFT_LEFT && *iterator >
		    le32_to_cpu(extent->ee_block)) {
			/* Hole, move to the next extent */
			if (extent < EXT_LAST_EXTENT(path[depth].p_hdr)) {
				path[depth].p_ext++;
			} else {
				*iterator = ext4_ext_next_allocated_block(path);
				continue;
			}
		}

		tmp = *iterator;
		if (SHIFT == SHIFT_LEFT) {
			extent = EXT_LAST_EXTENT(path[depth].p_hdr);
			*iterator = le32_to_cpu(extent->ee_block) +
					ext4_ext_get_actual_len(extent);
		} else {
			extent = EXT_FIRST_EXTENT(path[depth].p_hdr);
			if (le32_to_cpu(extent->ee_block) > start)
				*iterator = le32_to_cpu(extent->ee_block) - 1;
			else if (le32_to_cpu(extent->ee_block) == start)
				iterator = NULL;
			else {
				extent = EXT_LAST_EXTENT(path[depth].p_hdr);
				while (le32_to_cpu(extent->ee_block) >= start)
					extent--;

				if (extent == EXT_LAST_EXTENT(path[depth].p_hdr))
					break;

				extent++;
				iterator = NULL;
			}
			path[depth].p_ext = extent;
		}
		ret = ext4_ext_shift_path_extents(path, shift, inode,
				handle, SHIFT);
		/* iterator can be NULL which means we should break */
		if (ret == -EAGAIN)
			goto again;
		if (ret)
			break;
	}
out:
	ext4_free_ext_path(path);
	return ret;
}

/*
 * ext4_collapse_range:
 * This implements the fallocate's collapse range functionality for ext4
 * Returns: 0 and non-zero on error.
 */
static int ext4_collapse_range(struct file *file, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct address_space *mapping = inode->i_mapping;
	ext4_lblk_t punch_start, punch_stop;
	handle_t *handle;
	unsigned int credits;
	loff_t new_size, ioffset;
	int ret;

	/*
	 * We need to test this early because xfstests assumes that a
	 * collapse range of (0, 1) will return EOPNOTSUPP if the file
	 * system does not support collapse range.
	 */
	if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		return -EOPNOTSUPP;

	/* Collapse range works only on fs cluster size aligned regions. */
	if (!IS_ALIGNED(offset | len, EXT4_CLUSTER_SIZE(sb)))
		return -EINVAL;

	trace_ext4_collapse_range(inode, offset, len);

	punch_start = offset >> EXT4_BLOCK_SIZE_BITS(sb);
	punch_stop = (offset + len) >> EXT4_BLOCK_SIZE_BITS(sb);

	inode_lock(inode);
	/*
	 * There is no need to overlap collapse range with EOF, in which case
	 * it is effectively a truncate operation
	 */
	if (offset + len >= inode->i_size) {
		ret = -EINVAL;
		goto out_mutex;
	}

	/* Currently just for extent based files */
	if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		ret = -EOPNOTSUPP;
		goto out_mutex;
	}

	/* Wait for existing dio to complete */
	inode_dio_wait(inode);

	ret = file_modified(file);
	if (ret)
		goto out_mutex;

	/*
	 * Prevent page faults from reinstantiating pages we have released from
	 * page cache.
	 */
	filemap_invalidate_lock(mapping);

	ret = ext4_break_layouts(inode);
	if (ret)
		goto out_mmap;

	/*
	 * Need to round down offset to be aligned with page size boundary
	 * for page size > block size.
	 */
	ioffset = round_down(offset, PAGE_SIZE);
	/*
	 * Write tail of the last page before removed range since it will get
	 * removed from the page cache below.
	 */
	ret = filemap_write_and_wait_range(mapping, ioffset, offset);
	if (ret)
		goto out_mmap;
	/*
	 * Write data that will be shifted to preserve them when discarding
	 * page cache below. We are also protected from pages becoming dirty
	 * by i_rwsem and invalidate_lock.
	 */
	ret = filemap_write_and_wait_range(mapping, offset + len,
					   LLONG_MAX);
	if (ret)
		goto out_mmap;
	truncate_pagecache(inode, ioffset);

	credits = ext4_writepage_trans_blocks(inode);
	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out_mmap;
	}
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_FALLOC_RANGE, handle);

	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_discard_preallocations(inode, 0);

	ret = ext4_es_remove_extent(inode, punch_start,
				    EXT_MAX_BLOCKS - punch_start);
	if (ret) {
		up_write(&EXT4_I(inode)->i_data_sem);
		goto out_stop;
	}

	ret = ext4_ext_remove_space(inode, punch_start, punch_stop - 1);
	if (ret) {
		up_write(&EXT4_I(inode)->i_data_sem);
		goto out_stop;
	}
	ext4_discard_preallocations(inode, 0);

	ret = ext4_ext_shift_extents(inode, handle, punch_stop,
				     punch_stop - punch_start, SHIFT_LEFT);
	if (ret) {
		up_write(&EXT4_I(inode)->i_data_sem);
		goto out_stop;
	}

	new_size = inode->i_size - len;
	i_size_write(inode, new_size);
	EXT4_I(inode)->i_disksize = new_size;

	up_write(&EXT4_I(inode)->i_data_sem);
	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
	inode->i_mtime = inode->i_ctime = current_time(inode);
	ret = ext4_mark_inode_dirty(handle, inode);
	ext4_update_inode_fsync_trans(handle, inode, 1);

out_stop:
	ext4_journal_stop(handle);
out_mmap:
	filemap_invalidate_unlock(mapping);
out_mutex:
	inode_unlock(inode);
	return ret;
}

/*
 * ext4_insert_range:
 * This function implements the FALLOC_FL_INSERT_RANGE flag of fallocate.
 * The data blocks starting from @offset to the EOF are shifted by @len
 * towards right to create a hole in the @inode. Inode size is increased
 * by len bytes.
 * Returns 0 on success, error otherwise.
 */
static int ext4_insert_range(struct file *file, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct address_space *mapping = inode->i_mapping;
	handle_t *handle;
	struct ext4_ext_path *path;
	struct ext4_extent *extent;
	ext4_lblk_t offset_lblk, len_lblk, ee_start_lblk = 0;
	unsigned int credits, ee_len;
	int ret = 0, depth, split_flag = 0;
	loff_t ioffset;

	/*
	 * We need to test this early because xfstests assumes that an
	 * insert range of (0, 1) will return EOPNOTSUPP if the file
	 * system does not support insert range.
	 */
	if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		return -EOPNOTSUPP;

	/* Insert range works only on fs cluster size aligned regions. */
	if (!IS_ALIGNED(offset | len, EXT4_CLUSTER_SIZE(sb)))
		return -EINVAL;

	trace_ext4_insert_range(inode, offset, len);

	offset_lblk = offset >> EXT4_BLOCK_SIZE_BITS(sb);
	len_lblk = len >> EXT4_BLOCK_SIZE_BITS(sb);

	inode_lock(inode);
	/* Currently just for extent based files */
	if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		ret = -EOPNOTSUPP;
		goto out_mutex;
	}

	/* Check whether the maximum file size would be exceeded */
	if (len > inode->i_sb->s_maxbytes - inode->i_size) {
		ret = -EFBIG;
		goto out_mutex;
	}

	/* Offset must be less than i_size */
	if (offset >= inode->i_size) {
		ret = -EINVAL;
		goto out_mutex;
	}

	/* Wait for existing dio to complete */
	inode_dio_wait(inode);

	ret = file_modified(file);
	if (ret)
		goto out_mutex;

	/*
	 * Prevent page faults from reinstantiating pages we have released from
	 * page cache.
	 */
	filemap_invalidate_lock(mapping);

	ret = ext4_break_layouts(inode);
	if (ret)
		goto out_mmap;

	/*
	 * Need to round down to align start offset to page size boundary
	 * for page size > block size.
	 */
	ioffset = round_down(offset, PAGE_SIZE);
	/* Write out all dirty pages */
	ret = filemap_write_and_wait_range(inode->i_mapping, ioffset,
			LLONG_MAX);
	if (ret)
		goto out_mmap;
	truncate_pagecache(inode, ioffset);

	credits = ext4_writepage_trans_blocks(inode);
	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out_mmap;
	}
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_FALLOC_RANGE, handle);

	/* Expand file to avoid data loss if there is error while shifting */
	inode->i_size += len;
	EXT4_I(inode)->i_disksize += len;
	inode->i_mtime = inode->i_ctime = current_time(inode);
	ret = ext4_mark_inode_dirty(handle, inode);
	if (ret)
		goto out_stop;

	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_discard_preallocations(inode, 0);

	path = ext4_find_extent(inode, offset_lblk, NULL, 0);
	if (IS_ERR(path)) {
		up_write(&EXT4_I(inode)->i_data_sem);
		goto out_stop;
	}

	depth = ext_depth(inode);
	extent = path[depth].p_ext;
	if (extent) {
		ee_start_lblk = le32_to_cpu(extent->ee_block);
		ee_len = ext4_ext_get_actual_len(extent);

		/*
		 * If offset_lblk is not the starting block of extent, split
		 * the extent @offset_lblk
		 */
		if ((offset_lblk > ee_start_lblk) &&
				(offset_lblk < (ee_start_lblk + ee_len))) {
			if (ext4_ext_is_unwritten(extent))
				split_flag = EXT4_EXT_MARK_UNWRIT1 |
					EXT4_EXT_MARK_UNWRIT2;
			ret = ext4_split_extent_at(handle, inode, &path,
					offset_lblk, split_flag,
					EXT4_EX_NOCACHE |
					EXT4_GET_BLOCKS_PRE_IO |
					EXT4_GET_BLOCKS_METADATA_NOFAIL);
		}

		ext4_free_ext_path(path);
		if (ret < 0) {
			up_write(&EXT4_I(inode)->i_data_sem);
			goto out_stop;
		}
	} else {
		ext4_free_ext_path(path);
	}

	ret = ext4_es_remove_extent(inode, offset_lblk,
			EXT_MAX_BLOCKS - offset_lblk);
	if (ret) {
		up_write(&EXT4_I(inode)->i_data_sem);
		goto out_stop;
	}

	/*
	 * if offset_lblk lies in a hole which is at start of file, use
	 * ee_start_lblk to shift extents
	 */
	ret = ext4_ext_shift_extents(inode, handle,
		max(ee_start_lblk, offset_lblk), len_lblk, SHIFT_RIGHT);

	up_write(&EXT4_I(inode)->i_data_sem);
	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
	if (ret >= 0)
		ext4_update_inode_fsync_trans(handle, inode, 1);

out_stop:
	ext4_journal_stop(handle);
out_mmap:
	filemap_invalidate_unlock(mapping);
out_mutex:
	inode_unlock(inode);
	return ret;
}

/**
 * ext4_swap_extents() - Swap extents between two inodes
 * @handle: handle for this transaction
 * @inode1:	First inode
 * @inode2:	Second inode
 * @lblk1:	Start block for first inode
 * @lblk2:	Start block for second inode
 * @count:	Number of blocks to swap
 * @unwritten: Mark second inode's extents as unwritten after swap
 * @erp:	Pointer to save error value
 *
 * This helper routine does exactly what is promise "swap extents". All other
 * stuff such as page-cache locking consistency, bh mapping consistency or
 * extent's data copying must be performed by caller.
 * Locking:
 *		i_rwsem is held for both inodes
 * 		i_data_sem is locked for write for both inodes
 * Assumptions:
 *		All pages from requested range are locked for both inodes
 */
int
ext4_swap_extents(handle_t *handle, struct inode *inode1,
		  struct inode *inode2, ext4_lblk_t lblk1, ext4_lblk_t lblk2,
		  ext4_lblk_t count, int unwritten, int *erp)
{
	struct ext4_ext_path *path1 = NULL;
	struct ext4_ext_path *path2 = NULL;
	int replaced_count = 0;

	BUG_ON(!rwsem_is_locked(&EXT4_I(inode1)->i_data_sem));
	BUG_ON(!rwsem_is_locked(&EXT4_I(inode2)->i_data_sem));
	BUG_ON(!inode_is_locked(inode1));
	BUG_ON(!inode_is_locked(inode2));

	*erp = ext4_es_remove_extent(inode1, lblk1, count);
	if (unlikely(*erp))
		return 0;
	*erp = ext4_es_remove_extent(inode2, lblk2, count);
	if (unlikely(*erp))
		return 0;

	while (count) {
		struct ext4_extent *ex1, *ex2, tmp_ex;
		ext4_lblk_t e1_blk, e2_blk;
		int e1_len, e2_len, len;
		int split = 0;

		path1 = ext4_find_extent(inode1, lblk1, NULL, EXT4_EX_NOCACHE);
		if (IS_ERR(path1)) {
			*erp = PTR_ERR(path1);
			path1 = NULL;
		finish:
			count = 0;
			goto repeat;
		}
		path2 = ext4_find_extent(inode2, lblk2, NULL, EXT4_EX_NOCACHE);
		if (IS_ERR(path2)) {
			*erp = PTR_ERR(path2);
			path2 = NULL;
			goto finish;
		}
		ex1 = path1[path1->p_depth].p_ext;
		ex2 = path2[path2->p_depth].p_ext;
		/* Do we have something to swap ? */
		if (unlikely(!ex2 || !ex1))
			goto finish;

		e1_blk = le32_to_cpu(ex1->ee_block);
		e2_blk = le32_to_cpu(ex2->ee_block);
		e1_len = ext4_ext_get_actual_len(ex1);
		e2_len = ext4_ext_get_actual_len(ex2);

		/* Hole handling */
		if (!in_range(lblk1, e1_blk, e1_len) ||
		    !in_range(lblk2, e2_blk, e2_len)) {
			ext4_lblk_t next1, next2;

			/* if hole after extent, then go to next extent */
			next1 = ext4_ext_next_allocated_block(path1);
			next2 = ext4_ext_next_allocated_block(path2);
			/* If hole before extent, then shift to that extent */
			if (e1_blk > lblk1)
				next1 = e1_blk;
			if (e2_blk > lblk2)
				next2 = e2_blk;
			/* Do we have something to swap */
			if (next1 == EXT_MAX_BLOCKS || next2 == EXT_MAX_BLOCKS)
				goto finish;
			/* Move to the rightest boundary */
			len = next1 - lblk1;
			if (len < next2 - lblk2)
				len = next2 - lblk2;
			if (len > count)
				len = count;
			lblk1 += len;
			lblk2 += len;
			count -= len;
			goto repeat;
		}

		/* Prepare left boundary */
		if (e1_blk < lblk1) {
			split = 1;
			*erp = ext4_force_split_extent_at(handle, inode1,
						&path1, lblk1, 0);
			if (unlikely(*erp))
				goto finish;
		}
		if (e2_blk < lblk2) {
			split = 1;
			*erp = ext4_force_split_extent_at(handle, inode2,
						&path2,  lblk2, 0);
			if (unlikely(*erp))
				goto finish;
		}
		/* ext4_split_extent_at() may result in leaf extent split,
		 * path must to be revalidated. */
		if (split)
			goto repeat;

		/* Prepare right boundary */
		len = count;
		if (len > e1_blk + e1_len - lblk1)
			len = e1_blk + e1_len - lblk1;
		if (len > e2_blk + e2_len - lblk2)
			len = e2_blk + e2_len - lblk2;

		if (len != e1_len) {
			split = 1;
			*erp = ext4_force_split_extent_at(handle, inode1,
						&path1, lblk1 + len, 0);
			if (unlikely(*erp))
				goto finish;
		}
		if (len != e2_len) {
			split = 1;
			*erp = ext4_force_split_extent_at(handle, inode2,
						&path2, lblk2 + len, 0);
			if (*erp)
				goto finish;
		}
		/* ext4_split_extent_at() may result in leaf extent split,
		 * path must to be revalidated. */
		if (split)
			goto repeat;

		BUG_ON(e2_len != e1_len);
		*erp = ext4_ext_get_access(handle, inode1, path1 + path1->p_depth);
		if (unlikely(*erp))
			goto finish;
		*erp = ext4_ext_get_access(handle, inode2, path2 + path2->p_depth);
		if (unlikely(*erp))
			goto finish;

		/* Both extents are fully inside boundaries. Swap it now */
		tmp_ex = *ex1;
		ext4_ext_store_pblock(ex1, ext4_ext_pblock(ex2));
		ext4_ext_store_pblock(ex2, ext4_ext_pblock(&tmp_ex));
		ex1->ee_len = cpu_to_le16(e2_len);
		ex2->ee_len = cpu_to_le16(e1_len);
		if (unwritten)
			ext4_ext_mark_unwritten(ex2);
		if (ext4_ext_is_unwritten(&tmp_ex))
			ext4_ext_mark_unwritten(ex1);

		ext4_ext_try_to_merge(handle, inode2, path2, ex2);
		ext4_ext_try_to_merge(handle, inode1, path1, ex1);
		*erp = ext4_ext_dirty(handle, inode2, path2 +
				      path2->p_depth);
		if (unlikely(*erp))
			goto finish;
		*erp = ext4_ext_dirty(handle, inode1, path1 +
				      path1->p_depth);
		/*
		 * Looks scarry ah..? second inode already points to new blocks,
		 * and it was successfully dirtied. But luckily error may happen
		 * only due to journal error, so full transaction will be
		 * aborted anyway.
		 */
		if (unlikely(*erp))
			goto finish;
		lblk1 += len;
		lblk2 += len;
		replaced_count += len;
		count -= len;

	repeat:
		ext4_free_ext_path(path1);
		ext4_free_ext_path(path2);
		path1 = path2 = NULL;
	}
	return replaced_count;
}

/*
 * ext4_clu_mapped - determine whether any block in a logical cluster has
 *                   been mapped to a physical cluster
 *
 * @inode - file containing the logical cluster
 * @lclu - logical cluster of interest
 *
 * Returns 1 if any block in the logical cluster is mapped, signifying
 * that a physical cluster has been allocated for it.  Otherwise,
 * returns 0.  Can also return negative error codes.  Derived from
 * ext4_ext_map_blocks().
 */
int ext4_clu_mapped(struct inode *inode, ext4_lblk_t lclu)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_ext_path *path;
	int depth, mapped = 0, err = 0;
	struct ext4_extent *extent;
	ext4_lblk_t first_lblk, first_lclu, last_lclu;

	/*
	 * if data can be stored inline, the logical cluster isn't
	 * mapped - no physical clusters have been allocated, and the
	 * file has no extents
	 */
	if (ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA) ||
	    ext4_has_inline_data(inode))
		return 0;

	/* search for the extent closest to the first block in the cluster */
	path = ext4_find_extent(inode, EXT4_C2B(sbi, lclu), NULL, 0);
	if (IS_ERR(path)) {
		err = PTR_ERR(path);
		path = NULL;
		goto out;
	}

	depth = ext_depth(inode);

	/*
	 * A consistent leaf must not be empty.  This situation is possible,
	 * though, _during_ tree modification, and it's why an assert can't
	 * be put in ext4_find_extent().
	 */
	if (unlikely(path[depth].p_ext == NULL && depth != 0)) {
		EXT4_ERROR_INODE(inode,
		    "bad extent address - lblock: %lu, depth: %d, pblock: %lld",
				 (unsigned long) EXT4_C2B(sbi, lclu),
				 depth, path[depth].p_block);
		err = -EFSCORRUPTED;
		goto out;
	}

	extent = path[depth].p_ext;

	/* can't be mapped if the extent tree is empty */
	if (extent == NULL)
		goto out;

	first_lblk = le32_to_cpu(extent->ee_block);
	first_lclu = EXT4_B2C(sbi, first_lblk);

	/*
	 * Three possible outcomes at this point - found extent spanning
	 * the target cluster, to the left of the target cluster, or to the
	 * right of the target cluster.  The first two cases are handled here.
	 * The last case indicates the target cluster is not mapped.
	 */
	if (lclu >= first_lclu) {
		last_lclu = EXT4_B2C(sbi, first_lblk +
				     ext4_ext_get_actual_len(extent) - 1);
		if (lclu <= last_lclu) {
			mapped = 1;
		} else {
			first_lblk = ext4_ext_next_allocated_block(path);
			first_lclu = EXT4_B2C(sbi, first_lblk);
			if (lclu == first_lclu)
				mapped = 1;
		}
	}

out:
	ext4_free_ext_path(path);

	return err ? err : mapped;
}

/*
 * Updates physical block address and unwritten status of extent
 * starting at lblk start and of len. If such an extent doesn't exist,
 * this function splits the extent tree appropriately to create an
 * extent like this.  This function is called in the fast commit
 * replay path.  Returns 0 on success and error on failure.
 */
int ext4_ext_replay_update_ex(struct inode *inode, ext4_lblk_t start,
			      int len, int unwritten, ext4_fsblk_t pblk)
{
	struct ext4_ext_path *path = NULL, *ppath;
	struct ext4_extent *ex;
	int ret;

	path = ext4_find_extent(inode, start, NULL, 0);
	if (IS_ERR(path))
		return PTR_ERR(path);
	ex = path[path->p_depth].p_ext;
	if (!ex) {
		ret = -EFSCORRUPTED;
		goto out;
	}

	if (le32_to_cpu(ex->ee_block) != start ||
		ext4_ext_get_actual_len(ex) != len) {
		/* We need to split this extent to match our extent first */
		ppath = path;
		down_write(&EXT4_I(inode)->i_data_sem);
		ret = ext4_force_split_extent_at(NULL, inode, &ppath, start, 1);
		up_write(&EXT4_I(inode)->i_data_sem);
		if (ret)
			goto out;
		kfree(path);
		path = ext4_find_extent(inode, start, NULL, 0);
		if (IS_ERR(path))
			return -1;
		ppath = path;
		ex = path[path->p_depth].p_ext;
		WARN_ON(le32_to_cpu(ex->ee_block) != start);
		if (ext4_ext_get_actual_len(ex) != len) {
			down_write(&EXT4_I(inode)->i_data_sem);
			ret = ext4_force_split_extent_at(NULL, inode, &ppath,
							 start + len, 1);
			up_write(&EXT4_I(inode)->i_data_sem);
			if (ret)
				goto out;
			kfree(path);
			path = ext4_find_extent(inode, start, NULL, 0);
			if (IS_ERR(path))
				return -EINVAL;
			ex = path[path->p_depth].p_ext;
		}
	}
	if (unwritten)
		ext4_ext_mark_unwritten(ex);
	else
		ext4_ext_mark_initialized(ex);
	ext4_ext_store_pblock(ex, pblk);
	down_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_ext_dirty(NULL, inode, &path[path->p_depth]);
	up_write(&EXT4_I(inode)->i_data_sem);
out:
	ext4_free_ext_path(path);
	ext4_mark_inode_dirty(NULL, inode);
	return ret;
}

/* Try to shrink the extent tree */
void ext4_ext_replay_shrink_inode(struct inode *inode, ext4_lblk_t end)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent *ex;
	ext4_lblk_t old_cur, cur = 0;

	while (cur < end) {
		path = ext4_find_extent(inode, cur, NULL, 0);
		if (IS_ERR(path))
			return;
		ex = path[path->p_depth].p_ext;
		if (!ex) {
			ext4_free_ext_path(path);
			ext4_mark_inode_dirty(NULL, inode);
			return;
		}
		old_cur = cur;
		cur = le32_to_cpu(ex->ee_block) + ext4_ext_get_actual_len(ex);
		if (cur <= old_cur)
			cur = old_cur + 1;
		ext4_ext_try_to_merge(NULL, inode, path, ex);
		down_write(&EXT4_I(inode)->i_data_sem);
		ext4_ext_dirty(NULL, inode, &path[path->p_depth]);
		up_write(&EXT4_I(inode)->i_data_sem);
		ext4_mark_inode_dirty(NULL, inode);
		ext4_free_ext_path(path);
	}
}

/* Check if *cur is a hole and if it is, skip it */
static int skip_hole(struct inode *inode, ext4_lblk_t *cur)
{
	int ret;
	struct ext4_map_blocks map;

	map.m_lblk = *cur;
	map.m_len = ((inode->i_size) >> inode->i_sb->s_blocksize_bits) - *cur;

	ret = ext4_map_blocks(NULL, inode, &map, 0);
	if (ret < 0)
		return ret;
	if (ret != 0)
		return 0;
	*cur = *cur + map.m_len;
	return 0;
}

/* Count number of blocks used by this inode and update i_blocks */
int ext4_ext_replay_set_iblocks(struct inode *inode)
{
	struct ext4_ext_path *path = NULL, *path2 = NULL;
	struct ext4_extent *ex;
	ext4_lblk_t cur = 0, end;
	int numblks = 0, i, ret = 0;
	ext4_fsblk_t cmp1, cmp2;
	struct ext4_map_blocks map;

	/* Determin the size of the file first */
	path = ext4_find_extent(inode, EXT_MAX_BLOCKS - 1, NULL,
					EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return PTR_ERR(path);
	ex = path[path->p_depth].p_ext;
	if (!ex) {
		ext4_free_ext_path(path);
		goto out;
	}
	end = le32_to_cpu(ex->ee_block) + ext4_ext_get_actual_len(ex);
	ext4_free_ext_path(path);

	/* Count the number of data blocks */
	cur = 0;
	while (cur < end) {
		map.m_lblk = cur;
		map.m_len = end - cur;
		ret = ext4_map_blocks(NULL, inode, &map, 0);
		if (ret < 0)
			break;
		if (ret > 0)
			numblks += ret;
		cur = cur + map.m_len;
	}

	/*
	 * Count the number of extent tree blocks. We do it by looking up
	 * two successive extents and determining the difference between
	 * their paths. When path is different for 2 successive extents
	 * we compare the blocks in the path at each level and increment
	 * iblocks by total number of differences found.
	 */
	cur = 0;
	ret = skip_hole(inode, &cur);
	if (ret < 0)
		goto out;
	path = ext4_find_extent(inode, cur, NULL, 0);
	if (IS_ERR(path))
		goto out;
	numblks += path->p_depth;
	ext4_free_ext_path(path);
	while (cur < end) {
		path = ext4_find_extent(inode, cur, NULL, 0);
		if (IS_ERR(path))
			break;
		ex = path[path->p_depth].p_ext;
		if (!ex) {
			ext4_free_ext_path(path);
			return 0;
		}
		cur = max(cur + 1, le32_to_cpu(ex->ee_block) +
					ext4_ext_get_actual_len(ex));
		ret = skip_hole(inode, &cur);
		if (ret < 0) {
			ext4_free_ext_path(path);
			break;
		}
		path2 = ext4_find_extent(inode, cur, NULL, 0);
		if (IS_ERR(path2)) {
			ext4_free_ext_path(path);
			break;
		}
		for (i = 0; i <= max(path->p_depth, path2->p_depth); i++) {
			cmp1 = cmp2 = 0;
			if (i <= path->p_depth)
				cmp1 = path[i].p_bh ?
					path[i].p_bh->b_blocknr : 0;
			if (i <= path2->p_depth)
				cmp2 = path2[i].p_bh ?
					path2[i].p_bh->b_blocknr : 0;
			if (cmp1 != cmp2 && cmp2 != 0)
				numblks++;
		}
		ext4_free_ext_path(path);
		ext4_free_ext_path(path2);
	}

out:
	inode->i_blocks = numblks << (inode->i_sb->s_blocksize_bits - 9);
	ext4_mark_inode_dirty(NULL, inode);
	return 0;
}

int ext4_ext_clear_bb(struct inode *inode)
{
	struct ext4_ext_path *path = NULL;
	struct ext4_extent *ex;
	ext4_lblk_t cur = 0, end;
	int j, ret = 0;
	struct ext4_map_blocks map;

	if (ext4_test_inode_flag(inode, EXT4_INODE_INLINE_DATA))
		return 0;

	/* Determin the size of the file first */
	path = ext4_find_extent(inode, EXT_MAX_BLOCKS - 1, NULL,
					EXT4_EX_NOCACHE);
	if (IS_ERR(path))
		return PTR_ERR(path);
	ex = path[path->p_depth].p_ext;
	if (!ex) {
		ext4_free_ext_path(path);
		return 0;
	}
	end = le32_to_cpu(ex->ee_block) + ext4_ext_get_actual_len(ex);
	ext4_free_ext_path(path);

	cur = 0;
	while (cur < end) {
		map.m_lblk = cur;
		map.m_len = end - cur;
		ret = ext4_map_blocks(NULL, inode, &map, 0);
		if (ret < 0)
			break;
		if (ret > 0) {
			path = ext4_find_extent(inode, map.m_lblk, NULL, 0);
			if (!IS_ERR_OR_NULL(path)) {
				for (j = 0; j < path->p_depth; j++) {

					ext4_mb_mark_bb(inode->i_sb,
							path[j].p_block, 1, 0);
					ext4_fc_record_regions(inode->i_sb, inode->i_ino,
							0, path[j].p_block, 1, 1);
				}
				ext4_free_ext_path(path);
			}
			ext4_mb_mark_bb(inode->i_sb, map.m_pblk, map.m_len, 0);
			ext4_fc_record_regions(inode->i_sb, inode->i_ino,
					map.m_lblk, map.m_pblk, map.m_len, 1);
		}
		cur = cur + map.m_len;
	}

	return 0;
}
