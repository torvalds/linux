/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file contains miscellaneous helper functions.
 */

#ifndef __UBIFS_MISC_H__
#define __UBIFS_MISC_H__

/**
 * ubifs_zn_dirty - check if znode is dirty.
 * @znode: znode to check
 *
 * This helper function returns %1 if @znode is dirty and %0 otherwise.
 */
static inline int ubifs_zn_dirty(const struct ubifs_znode *znode)
{
	return !!test_bit(DIRTY_ZNODE, &znode->flags);
}

/**
 * ubifs_wake_up_bgt - wake up background thread.
 * @c: UBIFS file-system description object
 */
static inline void ubifs_wake_up_bgt(struct ubifs_info *c)
{
	if (c->bgt && !c->need_bgt) {
		c->need_bgt = 1;
		wake_up_process(c->bgt);
	}
}

/**
 * ubifs_tnc_find_child - find next child in znode.
 * @znode: znode to search at
 * @start: the zbranch index to start at
 *
 * This helper function looks for znode child starting at index @start. Returns
 * the child or %NULL if no children were found.
 */
static inline struct ubifs_znode *
ubifs_tnc_find_child(struct ubifs_znode *znode, int start)
{
	while (start < znode->child_cnt) {
		if (znode->zbranch[start].znode)
			return znode->zbranch[start].znode;
		start += 1;
	}

	return NULL;
}

/**
 * ubifs_inode - get UBIFS inode information by VFS 'struct inode' object.
 * @inode: the VFS 'struct inode' pointer
 */
static inline struct ubifs_inode *ubifs_inode(const struct inode *inode)
{
	return container_of(inode, struct ubifs_inode, vfs_inode);
}

/**
 * ubifs_compr_present - check if compressor was compiled in.
 * @compr_type: compressor type to check
 *
 * This function returns %1 of compressor of type @compr_type is present, and
 * %0 if not.
 */
static inline int ubifs_compr_present(int compr_type)
{
	ubifs_assert(compr_type >= 0 && compr_type < UBIFS_COMPR_TYPES_CNT);
	return !!ubifs_compressors[compr_type]->capi_name;
}

/**
 * ubifs_compr_name - get compressor name string by its type.
 * @compr_type: compressor type
 *
 * This function returns compressor type string.
 */
static inline const char *ubifs_compr_name(int compr_type)
{
	ubifs_assert(compr_type >= 0 && compr_type < UBIFS_COMPR_TYPES_CNT);
	return ubifs_compressors[compr_type]->name;
}

/**
 * ubifs_wbuf_sync - synchronize write-buffer.
 * @wbuf: write-buffer to synchronize
 *
 * This is the same as as 'ubifs_wbuf_sync_nolock()' but it does not assume
 * that the write-buffer is already locked.
 */
static inline int ubifs_wbuf_sync(struct ubifs_wbuf *wbuf)
{
	int err;

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	err = ubifs_wbuf_sync_nolock(wbuf);
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

/**
 * ubifs_leb_unmap - unmap an LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to unmap
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static inline int ubifs_leb_unmap(const struct ubifs_info *c, int lnum)
{
	int err;

	if (c->ro_media)
		return -EROFS;
	err = ubi_leb_unmap(c->ubi, lnum);
	if (err) {
		ubifs_err("unmap LEB %d failed, error %d", lnum, err);
		return err;
	}

	return 0;
}

/**
 * ubifs_leb_write - write to a LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to write
 * @buf: buffer to write from
 * @offs: offset within LEB to write to
 * @len: length to write
 * @dtype: data type
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static inline int ubifs_leb_write(const struct ubifs_info *c, int lnum,
				  const void *buf, int offs, int len, int dtype)
{
	int err;

	if (c->ro_media)
		return -EROFS;
	err = ubi_leb_write(c->ubi, lnum, buf, offs, len, dtype);
	if (err) {
		ubifs_err("writing %d bytes at %d:%d, error %d",
			  len, lnum, offs, err);
		return err;
	}

	return 0;
}

/**
 * ubifs_leb_change - atomic LEB change.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to write
 * @buf: buffer to write from
 * @len: length to write
 * @dtype: data type
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static inline int ubifs_leb_change(const struct ubifs_info *c, int lnum,
				   const void *buf, int len, int dtype)
{
	int err;

	if (c->ro_media)
		return -EROFS;
	err = ubi_leb_change(c->ubi, lnum, buf, len, dtype);
	if (err) {
		ubifs_err("changing %d bytes in LEB %d, error %d",
			  len, lnum, err);
		return err;
	}

	return 0;
}

/**
 * ubifs_encode_dev - encode device node IDs.
 * @dev: UBIFS device node information
 * @rdev: device IDs to encode
 *
 * This is a helper function which encodes major/minor numbers of a device node
 * into UBIFS device node description. We use standard Linux "new" and "huge"
 * encodings.
 */
static inline int ubifs_encode_dev(union ubifs_dev_desc *dev, dev_t rdev)
{
	if (new_valid_dev(rdev)) {
		dev->new = cpu_to_le32(new_encode_dev(rdev));
		return sizeof(dev->new);
	} else {
		dev->huge = cpu_to_le64(huge_encode_dev(rdev));
		return sizeof(dev->huge);
	}
}

/**
 * ubifs_add_dirt - add dirty space to LEB properties.
 * @c: the UBIFS file-system description object
 * @lnum: LEB to add dirty space for
 * @dirty: dirty space to add
 *
 * This is a helper function which increased amount of dirty LEB space. Returns
 * zero in case of success and a negative error code in case of failure.
 */
static inline int ubifs_add_dirt(struct ubifs_info *c, int lnum, int dirty)
{
	return ubifs_update_one_lp(c, lnum, LPROPS_NC, dirty, 0, 0);
}

/**
 * ubifs_return_leb - return LEB to lprops.
 * @c: the UBIFS file-system description object
 * @lnum: LEB to return
 *
 * This helper function cleans the "taken" flag of a logical eraseblock in the
 * lprops. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static inline int ubifs_return_leb(struct ubifs_info *c, int lnum)
{
	return ubifs_change_one_lp(c, lnum, LPROPS_NC, LPROPS_NC, 0,
				   LPROPS_TAKEN, 0);
}

/**
 * ubifs_idx_node_sz - return index node size.
 * @c: the UBIFS file-system description object
 * @child_cnt: number of children of this index node
 */
static inline int ubifs_idx_node_sz(const struct ubifs_info *c, int child_cnt)
{
	return UBIFS_IDX_NODE_SZ + (UBIFS_BRANCH_SZ + c->key_len) * child_cnt;
}

/**
 * ubifs_idx_branch - return pointer to an index branch.
 * @c: the UBIFS file-system description object
 * @idx: index node
 * @bnum: branch number
 */
static inline
struct ubifs_branch *ubifs_idx_branch(const struct ubifs_info *c,
				      const struct ubifs_idx_node *idx,
				      int bnum)
{
	return (struct ubifs_branch *)((void *)idx->branches +
				       (UBIFS_BRANCH_SZ + c->key_len) * bnum);
}

/**
 * ubifs_idx_key - return pointer to an index key.
 * @c: the UBIFS file-system description object
 * @idx: index node
 */
static inline void *ubifs_idx_key(const struct ubifs_info *c,
				  const struct ubifs_idx_node *idx)
{
	return (void *)((struct ubifs_branch *)idx->branches)->key;
}

/**
 * ubifs_current_time - round current time to time granularity.
 * @inode: inode
 */
static inline struct timespec ubifs_current_time(struct inode *inode)
{
	return (inode->i_sb->s_time_gran < NSEC_PER_SEC) ?
		current_fs_time(inode->i_sb) : CURRENT_TIME_SEC;
}

/**
 * ubifs_tnc_lookup - look up a file-system node.
 * @c: UBIFS file-system description object
 * @key: node key to lookup
 * @node: the node is returned here
 *
 * This function look up and reads node with key @key. The caller has to make
 * sure the @node buffer is large enough to fit the node. Returns zero in case
 * of success, %-ENOENT if the node was not found, and a negative error code in
 * case of failure.
 */
static inline int ubifs_tnc_lookup(struct ubifs_info *c,
				   const union ubifs_key *key, void *node)
{
	return ubifs_tnc_locate(c, key, node, NULL, NULL);
}

#endif /* __UBIFS_MISC_H__ */
