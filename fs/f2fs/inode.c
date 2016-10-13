/*
 * fs/f2fs/inode.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>

#include "f2fs.h"
#include "node.h"

#include <trace/events/f2fs.h>

void f2fs_mark_inode_dirty_sync(struct inode *inode)
{
	if (f2fs_inode_dirtied(inode))
		return;
	mark_inode_dirty_sync(inode);
}

void f2fs_set_inode_flags(struct inode *inode)
{
	unsigned int flags = F2FS_I(inode)->i_flags;
	unsigned int new_fl = 0;

	if (flags & FS_SYNC_FL)
		new_fl |= S_SYNC;
	if (flags & FS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & FS_NOATIME_FL)
		new_fl |= S_NOATIME;
	if (flags & FS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	inode_set_flags(inode, new_fl,
			S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	f2fs_mark_inode_dirty_sync(inode);
}

static void __get_inode_rdev(struct inode *inode, struct f2fs_inode *ri)
{
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
			S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		if (ri->i_addr[0])
			inode->i_rdev =
				old_decode_dev(le32_to_cpu(ri->i_addr[0]));
		else
			inode->i_rdev =
				new_decode_dev(le32_to_cpu(ri->i_addr[1]));
	}
}

static bool __written_first_block(struct f2fs_inode *ri)
{
	block_t addr = le32_to_cpu(ri->i_addr[0]);

	if (addr != NEW_ADDR && addr != NULL_ADDR)
		return true;
	return false;
}

static void __set_inode_rdev(struct inode *inode, struct f2fs_inode *ri)
{
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (old_valid_dev(inode->i_rdev)) {
			ri->i_addr[0] =
				cpu_to_le32(old_encode_dev(inode->i_rdev));
			ri->i_addr[1] = 0;
		} else {
			ri->i_addr[0] = 0;
			ri->i_addr[1] =
				cpu_to_le32(new_encode_dev(inode->i_rdev));
			ri->i_addr[2] = 0;
		}
	}
}

static void __recover_inline_status(struct inode *inode, struct page *ipage)
{
	void *inline_data = inline_data_addr(ipage);
	__le32 *start = inline_data;
	__le32 *end = start + MAX_INLINE_DATA / sizeof(__le32);

	while (start < end) {
		if (*start++) {
			f2fs_wait_on_page_writeback(ipage, NODE, true);

			set_inode_flag(inode, FI_DATA_EXIST);
			set_raw_inline(inode, F2FS_INODE(ipage));
			set_page_dirty(ipage);
			return;
		}
	}
	return;
}

static int do_read_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct page *node_page;
	struct f2fs_inode *ri;

	/* Check if ino is within scope */
	if (check_nid_range(sbi, inode->i_ino)) {
		f2fs_msg(inode->i_sb, KERN_ERR, "bad inode number: %lu",
			 (unsigned long) inode->i_ino);
		WARN_ON(1);
		return -EINVAL;
	}

	node_page = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(node_page))
		return PTR_ERR(node_page);

	ri = F2FS_INODE(node_page);

	inode->i_mode = le16_to_cpu(ri->i_mode);
	i_uid_write(inode, le32_to_cpu(ri->i_uid));
	i_gid_write(inode, le32_to_cpu(ri->i_gid));
	set_nlink(inode, le32_to_cpu(ri->i_links));
	inode->i_size = le64_to_cpu(ri->i_size);
	inode->i_blocks = le64_to_cpu(ri->i_blocks);

	inode->i_atime.tv_sec = le64_to_cpu(ri->i_atime);
	inode->i_ctime.tv_sec = le64_to_cpu(ri->i_ctime);
	inode->i_mtime.tv_sec = le64_to_cpu(ri->i_mtime);
	inode->i_atime.tv_nsec = le32_to_cpu(ri->i_atime_nsec);
	inode->i_ctime.tv_nsec = le32_to_cpu(ri->i_ctime_nsec);
	inode->i_mtime.tv_nsec = le32_to_cpu(ri->i_mtime_nsec);
	inode->i_generation = le32_to_cpu(ri->i_generation);

	fi->i_current_depth = le32_to_cpu(ri->i_current_depth);
	fi->i_xattr_nid = le32_to_cpu(ri->i_xattr_nid);
	fi->i_flags = le32_to_cpu(ri->i_flags);
	fi->flags = 0;
	fi->i_advise = ri->i_advise;
	fi->i_pino = le32_to_cpu(ri->i_pino);
	fi->i_dir_level = ri->i_dir_level;

	if (f2fs_init_extent_tree(inode, &ri->i_ext))
		set_page_dirty(node_page);

	get_inline_info(inode, ri);

	/* check data exist */
	if (f2fs_has_inline_data(inode) && !f2fs_exist_data(inode))
		__recover_inline_status(inode, node_page);

	/* get rdev by using inline_info */
	__get_inode_rdev(inode, ri);

	if (__written_first_block(ri))
		set_inode_flag(inode, FI_FIRST_BLOCK_WRITTEN);

	if (!need_inode_block_update(sbi, inode->i_ino))
		fi->last_disk_size = inode->i_size;

	f2fs_put_page(node_page, 1);

	stat_inc_inline_xattr(inode);
	stat_inc_inline_inode(inode);
	stat_inc_inline_dir(inode);

	return 0;
}

struct inode *f2fs_iget(struct super_block *sb, unsigned long ino)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct inode *inode;
	int ret = 0;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW)) {
		trace_f2fs_iget(inode);
		return inode;
	}
	if (ino == F2FS_NODE_INO(sbi) || ino == F2FS_META_INO(sbi))
		goto make_now;

	ret = do_read_inode(inode);
	if (ret)
		goto bad_inode;
make_now:
	if (ino == F2FS_NODE_INO(sbi)) {
		inode->i_mapping->a_ops = &f2fs_node_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_F2FS_ZERO);
	} else if (ino == F2FS_META_INO(sbi)) {
		inode->i_mapping->a_ops = &f2fs_meta_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_F2FS_ZERO);
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_op = &f2fs_file_inode_operations;
		inode->i_fop = &f2fs_file_operations;
		inode->i_mapping->a_ops = &f2fs_dblock_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &f2fs_dir_inode_operations;
		inode->i_fop = &f2fs_dir_operations;
		inode->i_mapping->a_ops = &f2fs_dblock_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_F2FS_HIGH_ZERO);
	} else if (S_ISLNK(inode->i_mode)) {
		if (f2fs_encrypted_inode(inode))
			inode->i_op = &f2fs_encrypted_symlink_inode_operations;
		else
			inode->i_op = &f2fs_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &f2fs_dblock_aops;
	} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
			S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		inode->i_op = &f2fs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
	} else {
		ret = -EIO;
		goto bad_inode;
	}
	unlock_new_inode(inode);
	trace_f2fs_iget(inode);
	return inode;

bad_inode:
	iget_failed(inode);
	trace_f2fs_iget_exit(inode, ret);
	return ERR_PTR(ret);
}

struct inode *f2fs_iget_retry(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
retry:
	inode = f2fs_iget(sb, ino);
	if (IS_ERR(inode)) {
		if (PTR_ERR(inode) == -ENOMEM) {
			congestion_wait(BLK_RW_ASYNC, HZ/50);
			goto retry;
		}
	}
	return inode;
}

int update_inode(struct inode *inode, struct page *node_page)
{
	struct f2fs_inode *ri;

	f2fs_inode_synced(inode);

	f2fs_wait_on_page_writeback(node_page, NODE, true);

	ri = F2FS_INODE(node_page);

	ri->i_mode = cpu_to_le16(inode->i_mode);
	ri->i_advise = F2FS_I(inode)->i_advise;
	ri->i_uid = cpu_to_le32(i_uid_read(inode));
	ri->i_gid = cpu_to_le32(i_gid_read(inode));
	ri->i_links = cpu_to_le32(inode->i_nlink);
	ri->i_size = cpu_to_le64(i_size_read(inode));
	ri->i_blocks = cpu_to_le64(inode->i_blocks);

	if (F2FS_I(inode)->extent_tree)
		set_raw_extent(&F2FS_I(inode)->extent_tree->largest,
							&ri->i_ext);
	else
		memset(&ri->i_ext, 0, sizeof(ri->i_ext));
	set_raw_inline(inode, ri);

	ri->i_atime = cpu_to_le64(inode->i_atime.tv_sec);
	ri->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	ri->i_mtime = cpu_to_le64(inode->i_mtime.tv_sec);
	ri->i_atime_nsec = cpu_to_le32(inode->i_atime.tv_nsec);
	ri->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	ri->i_mtime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);
	ri->i_current_depth = cpu_to_le32(F2FS_I(inode)->i_current_depth);
	ri->i_xattr_nid = cpu_to_le32(F2FS_I(inode)->i_xattr_nid);
	ri->i_flags = cpu_to_le32(F2FS_I(inode)->i_flags);
	ri->i_pino = cpu_to_le32(F2FS_I(inode)->i_pino);
	ri->i_generation = cpu_to_le32(inode->i_generation);
	ri->i_dir_level = F2FS_I(inode)->i_dir_level;

	__set_inode_rdev(inode, ri);
	set_cold_node(inode, node_page);

	/* deleted inode */
	if (inode->i_nlink == 0)
		clear_inline_node(node_page);

	return set_page_dirty(node_page);
}

int update_inode_page(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct page *node_page;
	int ret = 0;
retry:
	node_page = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(node_page)) {
		int err = PTR_ERR(node_page);
		if (err == -ENOMEM) {
			cond_resched();
			goto retry;
		} else if (err != -ENOENT) {
			f2fs_stop_checkpoint(sbi, false);
		}
		f2fs_inode_synced(inode);
		return 0;
	}
	ret = update_inode(inode, node_page);
	f2fs_put_page(node_page, 1);
	return ret;
}

int f2fs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi))
		return 0;

	if (!is_inode_flag_set(inode, FI_DIRTY_INODE))
		return 0;

	/*
	 * We need to balance fs here to prevent from producing dirty node pages
	 * during the urgent cleaning time when runing out of free sections.
	 */
	if (update_inode_page(inode))
		f2fs_balance_fs(sbi, true);
	return 0;
}

/*
 * Called at the last iput() if i_nlink is zero
 */
void f2fs_evict_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t xnid = F2FS_I(inode)->i_xattr_nid;
	int err = 0;

	/* some remained atomic pages should discarded */
	if (f2fs_is_atomic_file(inode))
		drop_inmem_pages(inode);

	trace_f2fs_evict_inode(inode);
	truncate_inode_pages_final(&inode->i_data);

	if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi))
		goto out_clear;

	f2fs_bug_on(sbi, get_dirty_pages(inode));
	remove_dirty_inode(inode);

	f2fs_destroy_extent_tree(inode);

	if (inode->i_nlink || is_bad_inode(inode))
		goto no_delete;

#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (time_to_inject(sbi, FAULT_EVICT_INODE))
		goto no_delete;
#endif

	sb_start_intwrite(inode->i_sb);
	set_inode_flag(inode, FI_NO_ALLOC);
	i_size_write(inode, 0);
retry:
	if (F2FS_HAS_BLOCKS(inode))
		err = f2fs_truncate(inode);

	if (!err) {
		f2fs_lock_op(sbi);
		err = remove_inode_page(inode);
		f2fs_unlock_op(sbi);
	}

	/* give more chances, if ENOMEM case */
	if (err == -ENOMEM) {
		err = 0;
		goto retry;
	}

	if (err)
		update_inode_page(inode);
	sb_end_intwrite(inode->i_sb);
no_delete:
	stat_dec_inline_xattr(inode);
	stat_dec_inline_dir(inode);
	stat_dec_inline_inode(inode);

	invalidate_mapping_pages(NODE_MAPPING(sbi), inode->i_ino, inode->i_ino);
	if (xnid)
		invalidate_mapping_pages(NODE_MAPPING(sbi), xnid, xnid);
	if (is_inode_flag_set(inode, FI_APPEND_WRITE))
		add_ino_entry(sbi, inode->i_ino, APPEND_INO);
	if (is_inode_flag_set(inode, FI_UPDATE_WRITE))
		add_ino_entry(sbi, inode->i_ino, UPDATE_INO);
	if (is_inode_flag_set(inode, FI_FREE_NID)) {
		alloc_nid_failed(sbi, inode->i_ino);
		clear_inode_flag(inode, FI_FREE_NID);
	}
	f2fs_bug_on(sbi, err &&
		!exist_written_data(sbi, inode->i_ino, ORPHAN_INO));
out_clear:
	fscrypt_put_encryption_info(inode, NULL);
	clear_inode(inode);
}

/* caller should call f2fs_lock_op() */
void handle_failed_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct node_info ni;

	/* don't make bad inode, since it becomes a regular file. */
	unlock_new_inode(inode);

	/*
	 * Note: we should add inode to orphan list before f2fs_unlock_op()
	 * so we can prevent losing this orphan when encoutering checkpoint
	 * and following suddenly power-off.
	 */
	get_node_info(sbi, inode->i_ino, &ni);

	if (ni.blk_addr != NULL_ADDR) {
		int err = acquire_orphan_inode(sbi);
		if (err) {
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			f2fs_msg(sbi->sb, KERN_WARNING,
				"Too many orphan inodes, run fsck to fix.");
		} else {
			add_orphan_inode(inode);
		}
		alloc_nid_done(sbi, inode->i_ino);
	} else {
		set_inode_flag(inode, FI_FREE_NID);
	}

	f2fs_unlock_op(sbi);

	/* iput will drop the inode object */
	iput(inode);
}
