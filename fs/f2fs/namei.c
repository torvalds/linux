/*
 * fs/f2fs/namei.c
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
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/dcache.h>

#include "f2fs.h"
#include "node.h"
#include "xattr.h"
#include "acl.h"
#include <trace/events/f2fs.h>

static struct inode *f2fs_new_inode(struct inode *dir, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	nid_t ino;
	struct inode *inode;
	bool nid_free = false;
	int err;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	f2fs_lock_op(sbi);
	if (!alloc_nid(sbi, &ino)) {
		f2fs_unlock_op(sbi);
		err = -ENOSPC;
		goto fail;
	}
	f2fs_unlock_op(sbi);

	inode_init_owner(inode, dir, mode);

	inode->i_ino = ino;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_generation = sbi->s_next_generation++;

	err = insert_inode_locked(inode);
	if (err) {
		err = -EINVAL;
		nid_free = true;
		goto out;
	}
	trace_f2fs_new_inode(inode, 0);
	mark_inode_dirty(inode);
	return inode;

out:
	clear_nlink(inode);
	unlock_new_inode(inode);
fail:
	trace_f2fs_new_inode(inode, err);
	make_bad_inode(inode);
	iput(inode);
	if (nid_free)
		alloc_nid_failed(sbi, ino);
	return ERR_PTR(err);
}

static int is_multimedia_file(const unsigned char *s, const char *sub)
{
	size_t slen = strlen(s);
	size_t sublen = strlen(sub);

	if (sublen > slen)
		return 0;

	return !strncasecmp(s + slen - sublen, sub, sublen);
}

/*
 * Set multimedia files as cold files for hot/cold data separation
 */
static inline void set_cold_files(struct f2fs_sb_info *sbi, struct inode *inode,
		const unsigned char *name)
{
	int i;
	__u8 (*extlist)[8] = sbi->raw_super->extension_list;

	int count = le32_to_cpu(sbi->raw_super->extension_count);
	for (i = 0; i < count; i++) {
		if (is_multimedia_file(name, extlist[i])) {
			file_set_cold(inode);
			break;
		}
	}
}

static int f2fs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
						bool excl)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct inode *inode;
	nid_t ino = 0;
	int err;

	f2fs_balance_fs(sbi);

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!test_opt(sbi, DISABLE_EXT_IDENTIFY))
		set_cold_files(sbi, inode, dentry->d_name.name);

	inode->i_op = &f2fs_file_inode_operations;
	inode->i_fop = &f2fs_file_operations;
	inode->i_mapping->a_ops = &f2fs_dblock_aops;
	ino = inode->i_ino;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	f2fs_unlock_op(sbi);
	if (err)
		goto out;

	alloc_nid_done(sbi, ino);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return 0;
out:
	clear_nlink(inode);
	unlock_new_inode(inode);
	make_bad_inode(inode);
	iput(inode);
	alloc_nid_failed(sbi, ino);
	return err;
}

static int f2fs_link(struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	int err;

	f2fs_balance_fs(sbi);

	inode->i_ctime = CURRENT_TIME;
	ihold(inode);

	set_inode_flag(F2FS_I(inode), FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	f2fs_unlock_op(sbi);
	if (err)
		goto out;

	d_instantiate(dentry, inode);
	return 0;
out:
	clear_inode_flag(F2FS_I(inode), FI_INC_LINK);
	iput(inode);
	return err;
}

struct dentry *f2fs_get_parent(struct dentry *child)
{
	struct qstr dotdot = QSTR_INIT("..", 2);
	unsigned long ino = f2fs_inode_by_name(child->d_inode, &dotdot);
	if (!ino)
		return ERR_PTR(-ENOENT);
	return d_obtain_alias(f2fs_iget(child->d_inode->i_sb, ino));
}

static struct dentry *f2fs_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct inode *inode = NULL;
	struct f2fs_dir_entry *de;
	struct page *page;

	if (dentry->d_name.len > F2FS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	de = f2fs_find_entry(dir, &dentry->d_name, &page);
	if (de) {
		nid_t ino = le32_to_cpu(de->ino);
		kunmap(page);
		f2fs_put_page(page, 0);

		inode = f2fs_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);

		stat_inc_inline_inode(inode);
	}

	return d_splice_alias(inode, dentry);
}

static int f2fs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct inode *inode = dentry->d_inode;
	struct f2fs_dir_entry *de;
	struct page *page;
	int err = -ENOENT;

	trace_f2fs_unlink_enter(dir, dentry);
	f2fs_balance_fs(sbi);

	de = f2fs_find_entry(dir, &dentry->d_name, &page);
	if (!de)
		goto fail;

	f2fs_lock_op(sbi);
	err = acquire_orphan_inode(sbi);
	if (err) {
		f2fs_unlock_op(sbi);
		kunmap(page);
		f2fs_put_page(page, 0);
		goto fail;
	}
	f2fs_delete_entry(de, page, inode);
	f2fs_unlock_op(sbi);

	/* In order to evict this inode,  we set it dirty */
	mark_inode_dirty(inode);
fail:
	trace_f2fs_unlink_exit(inode, err);
	return err;
}

static int f2fs_symlink(struct inode *dir, struct dentry *dentry,
					const char *symname)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct inode *inode;
	size_t symlen = strlen(symname) + 1;
	int err;

	f2fs_balance_fs(sbi);

	inode = f2fs_new_inode(dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &f2fs_symlink_inode_operations;
	inode->i_mapping->a_ops = &f2fs_dblock_aops;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	f2fs_unlock_op(sbi);
	if (err)
		goto out;

	err = page_symlink(inode, symname, symlen);
	alloc_nid_done(sbi, inode->i_ino);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return err;
out:
	clear_nlink(inode);
	unlock_new_inode(inode);
	make_bad_inode(inode);
	iput(inode);
	alloc_nid_failed(sbi, inode->i_ino);
	return err;
}

static int f2fs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct inode *inode;
	int err;

	f2fs_balance_fs(sbi);

	inode = f2fs_new_inode(dir, S_IFDIR | mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &f2fs_dir_inode_operations;
	inode->i_fop = &f2fs_dir_operations;
	inode->i_mapping->a_ops = &f2fs_dblock_aops;
	mapping_set_gfp_mask(inode->i_mapping, GFP_F2FS_ZERO);

	set_inode_flag(F2FS_I(inode), FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	f2fs_unlock_op(sbi);
	if (err)
		goto out_fail;

	alloc_nid_done(sbi, inode->i_ino);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	return 0;

out_fail:
	clear_inode_flag(F2FS_I(inode), FI_INC_LINK);
	clear_nlink(inode);
	unlock_new_inode(inode);
	make_bad_inode(inode);
	iput(inode);
	alloc_nid_failed(sbi, inode->i_ino);
	return err;
}

static int f2fs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	if (f2fs_empty_dir(inode))
		return f2fs_unlink(dir, dentry);
	return -ENOTEMPTY;
}

static int f2fs_mknod(struct inode *dir, struct dentry *dentry,
				umode_t mode, dev_t rdev)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct inode *inode;
	int err = 0;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	f2fs_balance_fs(sbi);

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	init_special_inode(inode, inode->i_mode, rdev);
	inode->i_op = &f2fs_special_inode_operations;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	f2fs_unlock_op(sbi);
	if (err)
		goto out;

	alloc_nid_done(sbi, inode->i_ino);
	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return 0;
out:
	clear_nlink(inode);
	unlock_new_inode(inode);
	make_bad_inode(inode);
	iput(inode);
	alloc_nid_failed(sbi, inode->i_ino);
	return err;
}

static int f2fs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	struct f2fs_sb_info *sbi = F2FS_SB(old_dir->i_sb);
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct page *old_dir_page;
	struct page *old_page, *new_page;
	struct f2fs_dir_entry *old_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry;
	struct f2fs_dir_entry *new_entry;
	int err = -ENOENT;

	f2fs_balance_fs(sbi);

	old_entry = f2fs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_entry)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		old_dir_entry = f2fs_parent_dir(old_inode, &old_dir_page);
		if (!old_dir_entry)
			goto out_old;
	}

	if (new_inode) {

		err = -ENOTEMPTY;
		if (old_dir_entry && !f2fs_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_entry = f2fs_find_entry(new_dir, &new_dentry->d_name,
						&new_page);
		if (!new_entry)
			goto out_dir;

		f2fs_lock_op(sbi);

		err = acquire_orphan_inode(sbi);
		if (err)
			goto put_out_dir;

		if (update_dent_inode(old_inode, &new_dentry->d_name)) {
			release_orphan_inode(sbi);
			goto put_out_dir;
		}

		f2fs_set_link(new_dir, new_entry, new_page, old_inode);

		new_inode->i_ctime = CURRENT_TIME;
		down_write(&F2FS_I(new_inode)->i_sem);
		if (old_dir_entry)
			drop_nlink(new_inode);
		drop_nlink(new_inode);
		up_write(&F2FS_I(new_inode)->i_sem);

		mark_inode_dirty(new_inode);

		if (!new_inode->i_nlink)
			add_orphan_inode(sbi, new_inode->i_ino);
		else
			release_orphan_inode(sbi);

		update_inode_page(old_inode);
		update_inode_page(new_inode);
	} else {
		f2fs_lock_op(sbi);

		err = f2fs_add_link(new_dentry, old_inode);
		if (err) {
			f2fs_unlock_op(sbi);
			goto out_dir;
		}

		if (old_dir_entry) {
			inc_nlink(new_dir);
			update_inode_page(new_dir);
		}
	}

	down_write(&F2FS_I(old_inode)->i_sem);
	file_lost_pino(old_inode);
	up_write(&F2FS_I(old_inode)->i_sem);

	old_inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(old_inode);

	f2fs_delete_entry(old_entry, old_page, NULL);

	if (old_dir_entry) {
		if (old_dir != new_dir) {
			f2fs_set_link(old_inode, old_dir_entry,
						old_dir_page, new_dir);
			update_inode_page(old_inode);
		} else {
			kunmap(old_dir_page);
			f2fs_put_page(old_dir_page, 0);
		}
		drop_nlink(old_dir);
		mark_inode_dirty(old_dir);
		update_inode_page(old_dir);
	}

	f2fs_unlock_op(sbi);
	return 0;

put_out_dir:
	f2fs_unlock_op(sbi);
	kunmap(new_page);
	f2fs_put_page(new_page, 0);
out_dir:
	if (old_dir_entry) {
		kunmap(old_dir_page);
		f2fs_put_page(old_dir_page, 0);
	}
out_old:
	kunmap(old_page);
	f2fs_put_page(old_page, 0);
out:
	return err;
}

static int f2fs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct inode *inode;
	int err;

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &f2fs_file_inode_operations;
	inode->i_fop = &f2fs_file_operations;
	inode->i_mapping->a_ops = &f2fs_dblock_aops;

	f2fs_lock_op(sbi);
	err = acquire_orphan_inode(sbi);
	if (err)
		goto out;

	err = f2fs_do_tmpfile(inode, dir);
	if (err)
		goto release_out;

	/*
	 * add this non-linked tmpfile to orphan list, in this way we could
	 * remove all unused data of tmpfile after abnormal power-off.
	 */
	add_orphan_inode(sbi, inode->i_ino);
	f2fs_unlock_op(sbi);

	alloc_nid_done(sbi, inode->i_ino);
	d_tmpfile(dentry, inode);
	unlock_new_inode(inode);
	return 0;

release_out:
	release_orphan_inode(sbi);
out:
	f2fs_unlock_op(sbi);
	clear_nlink(inode);
	unlock_new_inode(inode);
	make_bad_inode(inode);
	iput(inode);
	alloc_nid_failed(sbi, inode->i_ino);
	return err;
}

const struct inode_operations f2fs_dir_inode_operations = {
	.create		= f2fs_create,
	.lookup		= f2fs_lookup,
	.link		= f2fs_link,
	.unlink		= f2fs_unlink,
	.symlink	= f2fs_symlink,
	.mkdir		= f2fs_mkdir,
	.rmdir		= f2fs_rmdir,
	.mknod		= f2fs_mknod,
	.rename		= f2fs_rename,
	.tmpfile	= f2fs_tmpfile,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= f2fs_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

const struct inode_operations f2fs_symlink_inode_operations = {
	.readlink       = generic_readlink,
	.follow_link    = page_follow_link_light,
	.put_link       = page_put_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
#ifdef CONFIG_F2FS_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= f2fs_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

const struct inode_operations f2fs_special_inode_operations = {
	.getattr	= f2fs_getattr,
	.setattr        = f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.setxattr       = generic_setxattr,
	.getxattr       = generic_getxattr,
	.listxattr	= f2fs_listxattr,
	.removexattr    = generic_removexattr,
#endif
};
