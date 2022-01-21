// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS block mapping.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "nilfs.h"
#include "bmap.h"
#include "btree.h"
#include "direct.h"
#include "btnode.h"
#include "mdt.h"
#include "dat.h"
#include "alloc.h"

struct inode *nilfs_bmap_get_dat(const struct nilfs_bmap *bmap)
{
	struct the_nilfs *nilfs = bmap->b_inode->i_sb->s_fs_info;

	return nilfs->ns_dat;
}

static int nilfs_bmap_convert_error(struct nilfs_bmap *bmap,
				     const char *fname, int err)
{
	struct inode *inode = bmap->b_inode;

	if (err == -EINVAL) {
		__nilfs_error(inode->i_sb, fname,
			      "broken bmap (inode number=%lu)", inode->i_ino);
		err = -EIO;
	}
	return err;
}

/**
 * nilfs_bmap_lookup_at_level - find a data block or node block
 * @bmap: bmap
 * @key: key
 * @level: level
 * @ptrp: place to store the value associated to @key
 *
 * Description: nilfs_bmap_lookup_at_level() finds a record whose key
 * matches @key in the block at @level of the bmap.
 *
 * Return Value: On success, 0 is returned and the record associated with @key
 * is stored in the place pointed by @ptrp. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - A record associated with @key does not exist.
 */
int nilfs_bmap_lookup_at_level(struct nilfs_bmap *bmap, __u64 key, int level,
			       __u64 *ptrp)
{
	sector_t blocknr;
	int ret;

	down_read(&bmap->b_sem);
	ret = bmap->b_ops->bop_lookup(bmap, key, level, ptrp);
	if (ret < 0) {
		ret = nilfs_bmap_convert_error(bmap, __func__, ret);
		goto out;
	}
	if (NILFS_BMAP_USE_VBN(bmap)) {
		ret = nilfs_dat_translate(nilfs_bmap_get_dat(bmap), *ptrp,
					  &blocknr);
		if (!ret)
			*ptrp = blocknr;
	}

 out:
	up_read(&bmap->b_sem);
	return ret;
}

int nilfs_bmap_lookup_contig(struct nilfs_bmap *bmap, __u64 key, __u64 *ptrp,
			     unsigned int maxblocks)
{
	int ret;

	down_read(&bmap->b_sem);
	ret = bmap->b_ops->bop_lookup_contig(bmap, key, ptrp, maxblocks);
	up_read(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

static int nilfs_bmap_do_insert(struct nilfs_bmap *bmap, __u64 key, __u64 ptr)
{
	__u64 keys[NILFS_BMAP_SMALL_HIGH + 1];
	__u64 ptrs[NILFS_BMAP_SMALL_HIGH + 1];
	int ret, n;

	if (bmap->b_ops->bop_check_insert != NULL) {
		ret = bmap->b_ops->bop_check_insert(bmap, key);
		if (ret > 0) {
			n = bmap->b_ops->bop_gather_data(
				bmap, keys, ptrs, NILFS_BMAP_SMALL_HIGH + 1);
			if (n < 0)
				return n;
			ret = nilfs_btree_convert_and_insert(
				bmap, key, ptr, keys, ptrs, n);
			if (ret == 0)
				bmap->b_u.u_flags |= NILFS_BMAP_LARGE;

			return ret;
		} else if (ret < 0)
			return ret;
	}

	return bmap->b_ops->bop_insert(bmap, key, ptr);
}

/**
 * nilfs_bmap_insert - insert a new key-record pair into a bmap
 * @bmap: bmap
 * @key: key
 * @rec: record
 *
 * Description: nilfs_bmap_insert() inserts the new key-record pair specified
 * by @key and @rec into @bmap.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EEXIST - A record associated with @key already exist.
 */
int nilfs_bmap_insert(struct nilfs_bmap *bmap, __u64 key, unsigned long rec)
{
	int ret;

	down_write(&bmap->b_sem);
	ret = nilfs_bmap_do_insert(bmap, key, rec);
	up_write(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

static int nilfs_bmap_do_delete(struct nilfs_bmap *bmap, __u64 key)
{
	__u64 keys[NILFS_BMAP_LARGE_LOW + 1];
	__u64 ptrs[NILFS_BMAP_LARGE_LOW + 1];
	int ret, n;

	if (bmap->b_ops->bop_check_delete != NULL) {
		ret = bmap->b_ops->bop_check_delete(bmap, key);
		if (ret > 0) {
			n = bmap->b_ops->bop_gather_data(
				bmap, keys, ptrs, NILFS_BMAP_LARGE_LOW + 1);
			if (n < 0)
				return n;
			ret = nilfs_direct_delete_and_convert(
				bmap, key, keys, ptrs, n);
			if (ret == 0)
				bmap->b_u.u_flags &= ~NILFS_BMAP_LARGE;

			return ret;
		} else if (ret < 0)
			return ret;
	}

	return bmap->b_ops->bop_delete(bmap, key);
}

/**
 * nilfs_bmap_seek_key - seek a valid entry and return its key
 * @bmap: bmap struct
 * @start: start key number
 * @keyp: place to store valid key
 *
 * Description: nilfs_bmap_seek_key() seeks a valid key on @bmap
 * starting from @start, and stores it to @keyp if found.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - No valid entry was found
 */
int nilfs_bmap_seek_key(struct nilfs_bmap *bmap, __u64 start, __u64 *keyp)
{
	int ret;

	down_read(&bmap->b_sem);
	ret = bmap->b_ops->bop_seek_key(bmap, start, keyp);
	up_read(&bmap->b_sem);

	if (ret < 0)
		ret = nilfs_bmap_convert_error(bmap, __func__, ret);
	return ret;
}

int nilfs_bmap_last_key(struct nilfs_bmap *bmap, __u64 *keyp)
{
	int ret;

	down_read(&bmap->b_sem);
	ret = bmap->b_ops->bop_last_key(bmap, keyp);
	up_read(&bmap->b_sem);

	if (ret < 0)
		ret = nilfs_bmap_convert_error(bmap, __func__, ret);
	return ret;
}

/**
 * nilfs_bmap_delete - delete a key-record pair from a bmap
 * @bmap: bmap
 * @key: key
 *
 * Description: nilfs_bmap_delete() deletes the key-record pair specified by
 * @key from @bmap.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - A record associated with @key does not exist.
 */
int nilfs_bmap_delete(struct nilfs_bmap *bmap, __u64 key)
{
	int ret;

	down_write(&bmap->b_sem);
	ret = nilfs_bmap_do_delete(bmap, key);
	up_write(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

static int nilfs_bmap_do_truncate(struct nilfs_bmap *bmap, __u64 key)
{
	__u64 lastkey;
	int ret;

	ret = bmap->b_ops->bop_last_key(bmap, &lastkey);
	if (ret < 0) {
		if (ret == -ENOENT)
			ret = 0;
		return ret;
	}

	while (key <= lastkey) {
		ret = nilfs_bmap_do_delete(bmap, lastkey);
		if (ret < 0)
			return ret;
		ret = bmap->b_ops->bop_last_key(bmap, &lastkey);
		if (ret < 0) {
			if (ret == -ENOENT)
				ret = 0;
			return ret;
		}
	}
	return 0;
}

/**
 * nilfs_bmap_truncate - truncate a bmap to a specified key
 * @bmap: bmap
 * @key: key
 *
 * Description: nilfs_bmap_truncate() removes key-record pairs whose keys are
 * greater than or equal to @key from @bmap.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_bmap_truncate(struct nilfs_bmap *bmap, __u64 key)
{
	int ret;

	down_write(&bmap->b_sem);
	ret = nilfs_bmap_do_truncate(bmap, key);
	up_write(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

/**
 * nilfs_bmap_clear - free resources a bmap holds
 * @bmap: bmap
 *
 * Description: nilfs_bmap_clear() frees resources associated with @bmap.
 */
void nilfs_bmap_clear(struct nilfs_bmap *bmap)
{
	down_write(&bmap->b_sem);
	if (bmap->b_ops->bop_clear != NULL)
		bmap->b_ops->bop_clear(bmap);
	up_write(&bmap->b_sem);
}

/**
 * nilfs_bmap_propagate - propagate dirty state
 * @bmap: bmap
 * @bh: buffer head
 *
 * Description: nilfs_bmap_propagate() marks the buffers that directly or
 * indirectly refer to the block specified by @bh dirty.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_bmap_propagate(struct nilfs_bmap *bmap, struct buffer_head *bh)
{
	int ret;

	down_write(&bmap->b_sem);
	ret = bmap->b_ops->bop_propagate(bmap, bh);
	up_write(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

/**
 * nilfs_bmap_lookup_dirty_buffers -
 * @bmap: bmap
 * @listp: pointer to buffer head list
 */
void nilfs_bmap_lookup_dirty_buffers(struct nilfs_bmap *bmap,
				     struct list_head *listp)
{
	if (bmap->b_ops->bop_lookup_dirty_buffers != NULL)
		bmap->b_ops->bop_lookup_dirty_buffers(bmap, listp);
}

/**
 * nilfs_bmap_assign - assign a new block number to a block
 * @bmap: bmap
 * @bh: pointer to buffer head
 * @blocknr: block number
 * @binfo: block information
 *
 * Description: nilfs_bmap_assign() assigns the block number @blocknr to the
 * buffer specified by @bh.
 *
 * Return Value: On success, 0 is returned and the buffer head of a newly
 * create buffer and the block information associated with the buffer are
 * stored in the place pointed by @bh and @binfo, respectively. On error, one
 * of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_bmap_assign(struct nilfs_bmap *bmap,
		      struct buffer_head **bh,
		      unsigned long blocknr,
		      union nilfs_binfo *binfo)
{
	int ret;

	down_write(&bmap->b_sem);
	ret = bmap->b_ops->bop_assign(bmap, bh, blocknr, binfo);
	up_write(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

/**
 * nilfs_bmap_mark - mark block dirty
 * @bmap: bmap
 * @key: key
 * @level: level
 *
 * Description: nilfs_bmap_mark() marks the block specified by @key and @level
 * as dirty.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_bmap_mark(struct nilfs_bmap *bmap, __u64 key, int level)
{
	int ret;

	if (bmap->b_ops->bop_mark == NULL)
		return 0;

	down_write(&bmap->b_sem);
	ret = bmap->b_ops->bop_mark(bmap, key, level);
	up_write(&bmap->b_sem);

	return nilfs_bmap_convert_error(bmap, __func__, ret);
}

/**
 * nilfs_bmap_test_and_clear_dirty - test and clear a bmap dirty state
 * @bmap: bmap
 *
 * Description: nilfs_test_and_clear() is the atomic operation to test and
 * clear the dirty state of @bmap.
 *
 * Return Value: 1 is returned if @bmap is dirty, or 0 if clear.
 */
int nilfs_bmap_test_and_clear_dirty(struct nilfs_bmap *bmap)
{
	int ret;

	down_write(&bmap->b_sem);
	ret = nilfs_bmap_dirty(bmap);
	nilfs_bmap_clear_dirty(bmap);
	up_write(&bmap->b_sem);
	return ret;
}


/*
 * Internal use only
 */
__u64 nilfs_bmap_data_get_key(const struct nilfs_bmap *bmap,
			      const struct buffer_head *bh)
{
	struct buffer_head *pbh;
	__u64 key;

	key = page_index(bh->b_page) << (PAGE_SHIFT -
					 bmap->b_inode->i_blkbits);
	for (pbh = page_buffers(bh->b_page); pbh != bh; pbh = pbh->b_this_page)
		key++;

	return key;
}

__u64 nilfs_bmap_find_target_seq(const struct nilfs_bmap *bmap, __u64 key)
{
	__s64 diff;

	diff = key - bmap->b_last_allocated_key;
	if ((nilfs_bmap_keydiff_abs(diff) < NILFS_INODE_BMAP_SIZE) &&
	    (bmap->b_last_allocated_ptr != NILFS_BMAP_INVALID_PTR) &&
	    (bmap->b_last_allocated_ptr + diff > 0))
		return bmap->b_last_allocated_ptr + diff;
	else
		return NILFS_BMAP_INVALID_PTR;
}

#define NILFS_BMAP_GROUP_DIV	8
__u64 nilfs_bmap_find_target_in_group(const struct nilfs_bmap *bmap)
{
	struct inode *dat = nilfs_bmap_get_dat(bmap);
	unsigned long entries_per_group = nilfs_palloc_entries_per_group(dat);
	unsigned long group = bmap->b_inode->i_ino / entries_per_group;

	return group * entries_per_group +
		(bmap->b_inode->i_ino % NILFS_BMAP_GROUP_DIV) *
		(entries_per_group / NILFS_BMAP_GROUP_DIV);
}

static struct lock_class_key nilfs_bmap_dat_lock_key;
static struct lock_class_key nilfs_bmap_mdt_lock_key;

/**
 * nilfs_bmap_read - read a bmap from an inode
 * @bmap: bmap
 * @raw_inode: on-disk inode
 *
 * Description: nilfs_bmap_read() initializes the bmap @bmap.
 *
 * Return Value: On success, 0 is returned. On error, the following negative
 * error code is returned.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_bmap_read(struct nilfs_bmap *bmap, struct nilfs_inode *raw_inode)
{
	if (raw_inode == NULL)
		memset(bmap->b_u.u_data, 0, NILFS_BMAP_SIZE);
	else
		memcpy(bmap->b_u.u_data, raw_inode->i_bmap, NILFS_BMAP_SIZE);

	init_rwsem(&bmap->b_sem);
	bmap->b_state = 0;
	bmap->b_inode = &NILFS_BMAP_I(bmap)->vfs_inode;
	switch (bmap->b_inode->i_ino) {
	case NILFS_DAT_INO:
		bmap->b_ptr_type = NILFS_BMAP_PTR_P;
		bmap->b_last_allocated_key = 0;
		bmap->b_last_allocated_ptr = NILFS_BMAP_NEW_PTR_INIT;
		lockdep_set_class(&bmap->b_sem, &nilfs_bmap_dat_lock_key);
		break;
	case NILFS_CPFILE_INO:
	case NILFS_SUFILE_INO:
		bmap->b_ptr_type = NILFS_BMAP_PTR_VS;
		bmap->b_last_allocated_key = 0;
		bmap->b_last_allocated_ptr = NILFS_BMAP_INVALID_PTR;
		lockdep_set_class(&bmap->b_sem, &nilfs_bmap_mdt_lock_key);
		break;
	case NILFS_IFILE_INO:
		lockdep_set_class(&bmap->b_sem, &nilfs_bmap_mdt_lock_key);
		fallthrough;
	default:
		bmap->b_ptr_type = NILFS_BMAP_PTR_VM;
		bmap->b_last_allocated_key = 0;
		bmap->b_last_allocated_ptr = NILFS_BMAP_INVALID_PTR;
		break;
	}

	return (bmap->b_u.u_flags & NILFS_BMAP_LARGE) ?
		nilfs_btree_init(bmap) : nilfs_direct_init(bmap);
}

/**
 * nilfs_bmap_write - write back a bmap to an inode
 * @bmap: bmap
 * @raw_inode: on-disk inode
 *
 * Description: nilfs_bmap_write() stores @bmap in @raw_inode.
 */
void nilfs_bmap_write(struct nilfs_bmap *bmap, struct nilfs_inode *raw_inode)
{
	down_write(&bmap->b_sem);
	memcpy(raw_inode->i_bmap, bmap->b_u.u_data,
	       NILFS_INODE_BMAP_SIZE * sizeof(__le64));
	if (bmap->b_inode->i_ino == NILFS_DAT_INO)
		bmap->b_last_allocated_ptr = NILFS_BMAP_NEW_PTR_INIT;

	up_write(&bmap->b_sem);
}

void nilfs_bmap_init_gc(struct nilfs_bmap *bmap)
{
	memset(&bmap->b_u, 0, NILFS_BMAP_SIZE);
	init_rwsem(&bmap->b_sem);
	bmap->b_inode = &NILFS_BMAP_I(bmap)->vfs_inode;
	bmap->b_ptr_type = NILFS_BMAP_PTR_U;
	bmap->b_last_allocated_key = 0;
	bmap->b_last_allocated_ptr = NILFS_BMAP_INVALID_PTR;
	bmap->b_state = 0;
	nilfs_btree_init_gc(bmap);
}

void nilfs_bmap_save(const struct nilfs_bmap *bmap,
		     struct nilfs_bmap_store *store)
{
	memcpy(store->data, bmap->b_u.u_data, sizeof(store->data));
	store->last_allocated_key = bmap->b_last_allocated_key;
	store->last_allocated_ptr = bmap->b_last_allocated_ptr;
	store->state = bmap->b_state;
}

void nilfs_bmap_restore(struct nilfs_bmap *bmap,
			const struct nilfs_bmap_store *store)
{
	memcpy(bmap->b_u.u_data, store->data, sizeof(store->data));
	bmap->b_last_allocated_key = store->last_allocated_key;
	bmap->b_last_allocated_ptr = store->last_allocated_ptr;
	bmap->b_state = store->state;
}
