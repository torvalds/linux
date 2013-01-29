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
#include <linux/writeback.h>

#include "f2fs.h"
#include "node.h"

struct f2fs_iget_args {
	u64 ino;
	int on_free;
};

void f2fs_set_inode_flags(struct inode *inode)
{
	unsigned int flags = F2FS_I(inode)->i_flags;

	inode->i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE |
			S_NOATIME | S_DIRSYNC);

	if (flags & FS_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & FS_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & FS_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & FS_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & FS_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

static int f2fs_iget_test(struct inode *inode, void *data)
{
	struct f2fs_iget_args *args = data;

	if (inode->i_ino != args->ino)
		return 0;
	if (inode->i_state & (I_FREEING | I_WILL_FREE)) {
		args->on_free = 1;
		return 0;
	}
	return 1;
}

struct inode *f2fs_iget_nowait(struct super_block *sb, unsigned long ino)
{
	struct f2fs_iget_args args = {
		.ino = ino,
		.on_free = 0
	};
	struct inode *inode = ilookup5(sb, ino, f2fs_iget_test, &args);

	if (inode)
		return inode;
	if (!args.on_free)
		return f2fs_iget(sb, ino);
	return ERR_PTR(-ENOENT);
}

static int do_read_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct page *node_page;
	struct f2fs_node *rn;
	struct f2fs_inode *ri;

	/* Check if ino is within scope */
	check_nid_range(sbi, inode->i_ino);

	node_page = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(node_page))
		return PTR_ERR(node_page);

	rn = page_address(node_page);
	ri = &(rn->i);

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
	if (ri->i_addr[0])
		inode->i_rdev = old_decode_dev(le32_to_cpu(ri->i_addr[0]));
	else
		inode->i_rdev = new_decode_dev(le32_to_cpu(ri->i_addr[1]));

	fi->i_current_depth = le32_to_cpu(ri->i_current_depth);
	fi->i_xattr_nid = le32_to_cpu(ri->i_xattr_nid);
	fi->i_flags = le32_to_cpu(ri->i_flags);
	fi->flags = 0;
	fi->data_version = le64_to_cpu(F2FS_CKPT(sbi)->checkpoint_ver) - 1;
	fi->i_advise = ri->i_advise;
	fi->i_pino = le32_to_cpu(ri->i_pino);
	get_extent_info(&fi->ext, ri->i_ext);
	f2fs_put_page(node_page, 1);
	return 0;
}

struct inode *f2fs_iget(struct super_block *sb, unsigned long ino)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct inode *inode;
	int ret;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	if (ino == F2FS_NODE_INO(sbi) || ino == F2FS_META_INO(sbi))
		goto make_now;

	ret = do_read_inode(inode);
	if (ret)
		goto bad_inode;

	if (!sbi->por_doing && inode->i_nlink == 0) {
		ret = -ENOENT;
		goto bad_inode;
	}

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
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER_MOVABLE |
				__GFP_ZERO);
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &f2fs_symlink_inode_operations;
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

	return inode;

bad_inode:
	iget_failed(inode);
	return ERR_PTR(ret);
}

void update_inode(struct inode *inode, struct page *node_page)
{
	struct f2fs_node *rn;
	struct f2fs_inode *ri;

	wait_on_page_writeback(node_page);

	rn = page_address(node_page);
	ri = &(rn->i);

	ri->i_mode = cpu_to_le16(inode->i_mode);
	ri->i_advise = F2FS_I(inode)->i_advise;
	ri->i_uid = cpu_to_le32(i_uid_read(inode));
	ri->i_gid = cpu_to_le32(i_gid_read(inode));
	ri->i_links = cpu_to_le32(inode->i_nlink);
	ri->i_size = cpu_to_le64(i_size_read(inode));
	ri->i_blocks = cpu_to_le64(inode->i_blocks);
	set_raw_extent(&F2FS_I(inode)->ext, &ri->i_ext);

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

	set_cold_node(inode, node_page);
	set_page_dirty(node_page);
}

int f2fs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct page *node_page;
	bool need_lock = false;

	if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi))
		return 0;

	if (wbc)
		f2fs_balance_fs(sbi);

	node_page = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(node_page))
		return PTR_ERR(node_page);

	if (!PageDirty(node_page)) {
		need_lock = true;
		f2fs_put_page(node_page, 1);
		mutex_lock(&sbi->write_inode);
		node_page = get_node_page(sbi, inode->i_ino);
		if (IS_ERR(node_page)) {
			mutex_unlock(&sbi->write_inode);
			return PTR_ERR(node_page);
		}
	}
	update_inode(inode, node_page);
	f2fs_put_page(node_page, 1);
	if (need_lock)
		mutex_unlock(&sbi->write_inode);
	return 0;
}

/*
 * Called at the last iput() if i_nlink is zero
 */
void f2fs_evict_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);

	truncate_inode_pages(&inode->i_data, 0);

	if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi))
		goto no_delete;

	BUG_ON(atomic_read(&F2FS_I(inode)->dirty_dents));
	remove_dirty_dir_inode(inode);

	if (inode->i_nlink || is_bad_inode(inode))
		goto no_delete;

	sb_start_intwrite(inode->i_sb);
	set_inode_flag(F2FS_I(inode), FI_NO_ALLOC);
	i_size_write(inode, 0);

	if (F2FS_HAS_BLOCKS(inode))
		f2fs_truncate(inode);

	remove_inode_page(inode);
	sb_end_intwrite(inode->i_sb);
no_delete:
	clear_inode(inode);
}
