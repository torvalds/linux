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
#include <linux/namei.h>

#include "f2fs.h"
#include "node.h"
#include "xattr.h"
#include "acl.h"
#include <trace/events/f2fs.h>

static struct inode *f2fs_new_inode(struct inode *dir, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
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
		goto fail;
	}

	/* If the directory encrypted, then we should encrypt the inode. */
	if (f2fs_encrypted_inode(dir) && f2fs_may_encrypt(inode))
		f2fs_set_encrypted_inode(inode);

	set_inode_flag(inode, FI_NEW_INODE);

	if (test_opt(sbi, INLINE_XATTR))
		set_inode_flag(inode, FI_INLINE_XATTR);
	if (test_opt(sbi, INLINE_DATA) && f2fs_may_inline_data(inode))
		set_inode_flag(inode, FI_INLINE_DATA);
	if (f2fs_may_inline_dentry(inode))
		set_inode_flag(inode, FI_INLINE_DENTRY);

	f2fs_init_extent_tree(inode, NULL);

	stat_inc_inline_xattr(inode);
	stat_inc_inline_inode(inode);
	stat_inc_inline_dir(inode);

	trace_f2fs_new_inode(inode, 0);
	return inode;

fail:
	trace_f2fs_new_inode(inode, err);
	make_bad_inode(inode);
	if (nid_free)
		set_inode_flag(inode, FI_FREE_NID);
	iput(inode);
	return ERR_PTR(err);
}

static int is_multimedia_file(const unsigned char *s, const char *sub)
{
	size_t slen = strlen(s);
	size_t sublen = strlen(sub);
	int i;

	/*
	 * filename format of multimedia file should be defined as:
	 * "filename + '.' + extension + (optional: '.' + temp extension)".
	 */
	if (slen < sublen + 2)
		return 0;

	for (i = 1; i < slen - sublen; i++) {
		if (s[i] != '.')
			continue;
		if (!strncasecmp(s + i + 1, sub, sublen))
			return 1;
	}

	return 0;
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
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	nid_t ino = 0;
	int err;

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!test_opt(sbi, DISABLE_EXT_IDENTIFY))
		set_cold_files(sbi, inode, dentry->d_name.name);

	inode->i_op = &f2fs_file_inode_operations;
	inode->i_fop = &f2fs_file_operations;
	inode->i_mapping->a_ops = &f2fs_dblock_aops;
	ino = inode->i_ino;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	alloc_nid_done(sbi, ino);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;
out:
	handle_failed_inode(inode);
	return err;
}

static int f2fs_link(struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	int err;

	if (f2fs_encrypted_inode(dir) &&
			!fscrypt_has_permitted_context(dir, inode))
		return -EPERM;

	f2fs_balance_fs(sbi, true);

	inode->i_ctime = CURRENT_TIME;
	ihold(inode);

	set_inode_flag(inode, FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	d_instantiate(dentry, inode);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;
out:
	clear_inode_flag(inode, FI_INC_LINK);
	iput(inode);
	f2fs_unlock_op(sbi);
	return err;
}

struct dentry *f2fs_get_parent(struct dentry *child)
{
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct page *page;
	unsigned long ino = f2fs_inode_by_name(d_inode(child), &dotdot, &page);
	if (!ino) {
		if (IS_ERR(page))
			return ERR_CAST(page);
		return ERR_PTR(-ENOENT);
	}
	return d_obtain_alias(f2fs_iget(child->d_sb, ino));
}

static int __recover_dot_dentries(struct inode *dir, nid_t pino)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct qstr dot = QSTR_INIT(".", 1);
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct f2fs_dir_entry *de;
	struct page *page;
	int err = 0;

	if (f2fs_readonly(sbi->sb)) {
		f2fs_msg(sbi->sb, KERN_INFO,
			"skip recovering inline_dots inode (ino:%lu, pino:%u) "
			"in readonly mountpoint", dir->i_ino, pino);
		return 0;
	}

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);

	de = f2fs_find_entry(dir, &dot, &page);
	if (de) {
		f2fs_dentry_kunmap(dir, page);
		f2fs_put_page(page, 0);
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto out;
	} else {
		err = __f2fs_add_link(dir, &dot, NULL, dir->i_ino, S_IFDIR);
		if (err)
			goto out;
	}

	de = f2fs_find_entry(dir, &dotdot, &page);
	if (de) {
		f2fs_dentry_kunmap(dir, page);
		f2fs_put_page(page, 0);
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
	} else {
		err = __f2fs_add_link(dir, &dotdot, NULL, pino, S_IFDIR);
	}
out:
	if (!err)
		clear_inode_flag(dir, FI_INLINE_DOTS);

	f2fs_unlock_op(sbi);
	return err;
}

static struct dentry *f2fs_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct inode *inode = NULL;
	struct f2fs_dir_entry *de;
	struct page *page;
	nid_t ino;
	int err = 0;
	unsigned int root_ino = F2FS_ROOT_INO(F2FS_I_SB(dir));

	if (f2fs_encrypted_inode(dir)) {
		int res = fscrypt_get_encryption_info(dir);

		/*
		 * DCACHE_ENCRYPTED_WITH_KEY is set if the dentry is
		 * created while the directory was encrypted and we
		 * don't have access to the key.
		 */
		if (fscrypt_has_encryption_key(dir))
			fscrypt_set_encrypted_dentry(dentry);
		fscrypt_set_d_op(dentry);
		if (res && res != -ENOKEY)
			return ERR_PTR(res);
	}

	if (dentry->d_name.len > F2FS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	de = f2fs_find_entry(dir, &dentry->d_name, &page);
	if (!de) {
		if (IS_ERR(page))
			return (struct dentry *)page;
		return d_splice_alias(inode, dentry);
	}

	ino = le32_to_cpu(de->ino);
	f2fs_dentry_kunmap(dir, page);
	f2fs_put_page(page, 0);

	inode = f2fs_iget(dir->i_sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if ((dir->i_ino == root_ino) && f2fs_has_inline_dots(dir)) {
		err = __recover_dot_dentries(dir, root_ino);
		if (err)
			goto err_out;
	}

	if (f2fs_has_inline_dots(inode)) {
		err = __recover_dot_dentries(inode, dir->i_ino);
		if (err)
			goto err_out;
	}
	if (!IS_ERR(inode) && f2fs_encrypted_inode(dir) &&
			(S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)) &&
			!fscrypt_has_permitted_context(dir, inode)) {
		bool nokey = f2fs_encrypted_inode(inode) &&
			!fscrypt_has_encryption_key(inode);
		err = nokey ? -ENOKEY : -EPERM;
		goto err_out;
	}
	return d_splice_alias(inode, dentry);

err_out:
	iput(inode);
	return ERR_PTR(err);
}

static int f2fs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode = d_inode(dentry);
	struct f2fs_dir_entry *de;
	struct page *page;
	int err = -ENOENT;

	trace_f2fs_unlink_enter(dir, dentry);

	de = f2fs_find_entry(dir, &dentry->d_name, &page);
	if (!de) {
		if (IS_ERR(page))
			err = PTR_ERR(page);
		goto fail;
	}

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	err = acquire_orphan_inode(sbi);
	if (err) {
		f2fs_unlock_op(sbi);
		f2fs_dentry_kunmap(dir, page);
		f2fs_put_page(page, 0);
		goto fail;
	}
	f2fs_delete_entry(de, page, dir, inode);
	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
fail:
	trace_f2fs_unlink_exit(inode, err);
	return err;
}

static const char *f2fs_get_link(struct dentry *dentry,
				 struct inode *inode,
				 struct delayed_call *done)
{
	const char *link = page_get_link(dentry, inode, done);
	if (!IS_ERR(link) && !*link) {
		/* this is broken symlink case */
		do_delayed_call(done);
		clear_delayed_call(done);
		link = ERR_PTR(-ENOENT);
	}
	return link;
}

static int f2fs_symlink(struct inode *dir, struct dentry *dentry,
					const char *symname)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	size_t len = strlen(symname);
	struct fscrypt_str disk_link = FSTR_INIT((char *)symname, len + 1);
	struct fscrypt_symlink_data *sd = NULL;
	int err;

	if (f2fs_encrypted_inode(dir)) {
		err = fscrypt_get_encryption_info(dir);
		if (err)
			return err;

		if (!fscrypt_has_encryption_key(dir))
			return -EPERM;

		disk_link.len = (fscrypt_fname_encrypted_size(dir, len) +
				sizeof(struct fscrypt_symlink_data));
	}

	if (disk_link.len > dir->i_sb->s_blocksize)
		return -ENAMETOOLONG;

	inode = f2fs_new_inode(dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (f2fs_encrypted_inode(inode))
		inode->i_op = &f2fs_encrypted_symlink_inode_operations;
	else
		inode->i_op = &f2fs_symlink_inode_operations;
	inode_nohighmem(inode);
	inode->i_mapping->a_ops = &f2fs_dblock_aops;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);
	alloc_nid_done(sbi, inode->i_ino);

	if (f2fs_encrypted_inode(inode)) {
		struct qstr istr = QSTR_INIT(symname, len);
		struct fscrypt_str ostr;

		sd = kzalloc(disk_link.len, GFP_NOFS);
		if (!sd) {
			err = -ENOMEM;
			goto err_out;
		}

		err = fscrypt_get_encryption_info(inode);
		if (err)
			goto err_out;

		if (!fscrypt_has_encryption_key(inode)) {
			err = -EPERM;
			goto err_out;
		}

		ostr.name = sd->encrypted_path;
		ostr.len = disk_link.len;
		err = fscrypt_fname_usr_to_disk(inode, &istr, &ostr);
		if (err < 0)
			goto err_out;

		sd->len = cpu_to_le16(ostr.len);
		disk_link.name = (char *)sd;
	}

	err = page_symlink(inode, disk_link.name, disk_link.len);

err_out:
	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	/*
	 * Let's flush symlink data in order to avoid broken symlink as much as
	 * possible. Nevertheless, fsyncing is the best way, but there is no
	 * way to get a file descriptor in order to flush that.
	 *
	 * Note that, it needs to do dir->fsync to make this recoverable.
	 * If the symlink path is stored into inline_data, there is no
	 * performance regression.
	 */
	if (!err) {
		filemap_write_and_wait_range(inode->i_mapping, 0,
							disk_link.len - 1);

		if (IS_DIRSYNC(dir))
			f2fs_sync_fs(sbi->sb, 1);
	} else {
		f2fs_unlink(dir, dentry);
	}

	kfree(sd);
	return err;
out:
	handle_failed_inode(inode);
	return err;
}

static int f2fs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	int err;

	inode = f2fs_new_inode(dir, S_IFDIR | mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &f2fs_dir_inode_operations;
	inode->i_fop = &f2fs_dir_operations;
	inode->i_mapping->a_ops = &f2fs_dblock_aops;
	mapping_set_gfp_mask(inode->i_mapping, GFP_F2FS_HIGH_ZERO);

	f2fs_balance_fs(sbi, true);

	set_inode_flag(inode, FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out_fail;
	f2fs_unlock_op(sbi);

	alloc_nid_done(sbi, inode->i_ino);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;

out_fail:
	clear_inode_flag(inode, FI_INC_LINK);
	handle_failed_inode(inode);
	return err;
}

static int f2fs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	if (f2fs_empty_dir(inode))
		return f2fs_unlink(dir, dentry);
	return -ENOTEMPTY;
}

static int f2fs_mknod(struct inode *dir, struct dentry *dentry,
				umode_t mode, dev_t rdev)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	int err = 0;

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	init_special_inode(inode, inode->i_mode, rdev);
	inode->i_op = &f2fs_special_inode_operations;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, inode);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	alloc_nid_done(sbi, inode->i_ino);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;
out:
	handle_failed_inode(inode);
	return err;
}

static int __f2fs_tmpfile(struct inode *dir, struct dentry *dentry,
					umode_t mode, struct inode **whiteout)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct inode *inode;
	int err;

	inode = f2fs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (whiteout) {
		init_special_inode(inode, inode->i_mode, WHITEOUT_DEV);
		inode->i_op = &f2fs_special_inode_operations;
	} else {
		inode->i_op = &f2fs_file_inode_operations;
		inode->i_fop = &f2fs_file_operations;
		inode->i_mapping->a_ops = &f2fs_dblock_aops;
	}

	f2fs_balance_fs(sbi, true);

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
	add_orphan_inode(inode);
	alloc_nid_done(sbi, inode->i_ino);

	if (whiteout) {
		f2fs_i_links_write(inode, false);
		*whiteout = inode;
	} else {
		d_tmpfile(dentry, inode);
	}
	/* link_count was changed by d_tmpfile as well. */
	f2fs_unlock_op(sbi);
	unlock_new_inode(inode);
	return 0;

release_out:
	release_orphan_inode(sbi);
out:
	handle_failed_inode(inode);
	return err;
}

static int f2fs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if (f2fs_encrypted_inode(dir)) {
		int err = fscrypt_get_encryption_info(dir);
		if (err)
			return err;
	}

	return __f2fs_tmpfile(dir, dentry, mode, NULL);
}

static int f2fs_create_whiteout(struct inode *dir, struct inode **whiteout)
{
	return __f2fs_tmpfile(dir, NULL, S_IFCHR | WHITEOUT_MODE, whiteout);
}

static int f2fs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct inode *whiteout = NULL;
	struct page *old_dir_page;
	struct page *old_page, *new_page = NULL;
	struct f2fs_dir_entry *old_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry;
	struct f2fs_dir_entry *new_entry;
	bool is_old_inline = f2fs_has_inline_dentry(old_dir);
	int err = -ENOENT;

	if ((old_dir != new_dir) && f2fs_encrypted_inode(new_dir) &&
			!fscrypt_has_permitted_context(new_dir, old_inode)) {
		err = -EPERM;
		goto out;
	}

	old_entry = f2fs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_entry) {
		if (IS_ERR(old_page))
			err = PTR_ERR(old_page);
		goto out;
	}

	if (S_ISDIR(old_inode->i_mode)) {
		old_dir_entry = f2fs_parent_dir(old_inode, &old_dir_page);
		if (!old_dir_entry) {
			if (IS_ERR(old_dir_page))
				err = PTR_ERR(old_dir_page);
			goto out_old;
		}
	}

	if (flags & RENAME_WHITEOUT) {
		err = f2fs_create_whiteout(old_dir, &whiteout);
		if (err)
			goto out_dir;
	}

	if (new_inode) {

		err = -ENOTEMPTY;
		if (old_dir_entry && !f2fs_empty_dir(new_inode))
			goto out_whiteout;

		err = -ENOENT;
		new_entry = f2fs_find_entry(new_dir, &new_dentry->d_name,
						&new_page);
		if (!new_entry) {
			if (IS_ERR(new_page))
				err = PTR_ERR(new_page);
			goto out_whiteout;
		}

		f2fs_balance_fs(sbi, true);

		f2fs_lock_op(sbi);

		err = acquire_orphan_inode(sbi);
		if (err)
			goto put_out_dir;

		err = update_dent_inode(old_inode, new_inode,
						&new_dentry->d_name);
		if (err) {
			release_orphan_inode(sbi);
			goto put_out_dir;
		}

		f2fs_set_link(new_dir, new_entry, new_page, old_inode);

		new_inode->i_ctime = CURRENT_TIME;
		down_write(&F2FS_I(new_inode)->i_sem);
		if (old_dir_entry)
			f2fs_i_links_write(new_inode, false);
		f2fs_i_links_write(new_inode, false);
		up_write(&F2FS_I(new_inode)->i_sem);

		if (!new_inode->i_nlink)
			add_orphan_inode(new_inode);
		else
			release_orphan_inode(sbi);
	} else {
		f2fs_balance_fs(sbi, true);

		f2fs_lock_op(sbi);

		err = f2fs_add_link(new_dentry, old_inode);
		if (err) {
			f2fs_unlock_op(sbi);
			goto out_whiteout;
		}

		if (old_dir_entry)
			f2fs_i_links_write(new_dir, true);

		/*
		 * old entry and new entry can locate in the same inline
		 * dentry in inode, when attaching new entry in inline dentry,
		 * it could force inline dentry conversion, after that,
		 * old_entry and old_page will point to wrong address, in
		 * order to avoid this, let's do the check and update here.
		 */
		if (is_old_inline && !f2fs_has_inline_dentry(old_dir)) {
			f2fs_put_page(old_page, 0);
			old_page = NULL;

			old_entry = f2fs_find_entry(old_dir,
						&old_dentry->d_name, &old_page);
			if (!old_entry) {
				err = -ENOENT;
				if (IS_ERR(old_page))
					err = PTR_ERR(old_page);
				f2fs_unlock_op(sbi);
				goto out_whiteout;
			}
		}
	}

	down_write(&F2FS_I(old_inode)->i_sem);
	file_lost_pino(old_inode);
	if (new_inode && file_enc_name(new_inode))
		file_set_enc_name(old_inode);
	up_write(&F2FS_I(old_inode)->i_sem);

	old_inode->i_ctime = CURRENT_TIME;
	f2fs_mark_inode_dirty_sync(old_inode);

	f2fs_delete_entry(old_entry, old_page, old_dir, NULL);

	if (whiteout) {
		whiteout->i_state |= I_LINKABLE;
		set_inode_flag(whiteout, FI_INC_LINK);
		err = f2fs_add_link(old_dentry, whiteout);
		if (err)
			goto put_out_dir;
		whiteout->i_state &= ~I_LINKABLE;
		iput(whiteout);
	}

	if (old_dir_entry) {
		if (old_dir != new_dir && !whiteout) {
			f2fs_set_link(old_inode, old_dir_entry,
						old_dir_page, new_dir);
		} else {
			f2fs_dentry_kunmap(old_inode, old_dir_page);
			f2fs_put_page(old_dir_page, 0);
		}
		f2fs_i_links_write(old_dir, false);
	}

	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;

put_out_dir:
	f2fs_unlock_op(sbi);
	if (new_page) {
		f2fs_dentry_kunmap(new_dir, new_page);
		f2fs_put_page(new_page, 0);
	}
out_whiteout:
	if (whiteout)
		iput(whiteout);
out_dir:
	if (old_dir_entry) {
		f2fs_dentry_kunmap(old_inode, old_dir_page);
		f2fs_put_page(old_dir_page, 0);
	}
out_old:
	f2fs_dentry_kunmap(old_dir, old_page);
	f2fs_put_page(old_page, 0);
out:
	return err;
}

static int f2fs_cross_rename(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct page *old_dir_page, *new_dir_page;
	struct page *old_page, *new_page;
	struct f2fs_dir_entry *old_dir_entry = NULL, *new_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry, *new_entry;
	int old_nlink = 0, new_nlink = 0;
	int err = -ENOENT;

	if ((f2fs_encrypted_inode(old_dir) || f2fs_encrypted_inode(new_dir)) &&
			(old_dir != new_dir) &&
			(!fscrypt_has_permitted_context(new_dir, old_inode) ||
			 !fscrypt_has_permitted_context(old_dir, new_inode)))
		return -EPERM;

	old_entry = f2fs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_entry) {
		if (IS_ERR(old_page))
			err = PTR_ERR(old_page);
		goto out;
	}

	new_entry = f2fs_find_entry(new_dir, &new_dentry->d_name, &new_page);
	if (!new_entry) {
		if (IS_ERR(new_page))
			err = PTR_ERR(new_page);
		goto out_old;
	}

	/* prepare for updating ".." directory entry info later */
	if (old_dir != new_dir) {
		if (S_ISDIR(old_inode->i_mode)) {
			old_dir_entry = f2fs_parent_dir(old_inode,
							&old_dir_page);
			if (!old_dir_entry) {
				if (IS_ERR(old_dir_page))
					err = PTR_ERR(old_dir_page);
				goto out_new;
			}
		}

		if (S_ISDIR(new_inode->i_mode)) {
			new_dir_entry = f2fs_parent_dir(new_inode,
							&new_dir_page);
			if (!new_dir_entry) {
				if (IS_ERR(new_dir_page))
					err = PTR_ERR(new_dir_page);
				goto out_old_dir;
			}
		}
	}

	/*
	 * If cross rename between file and directory those are not
	 * in the same directory, we will inc nlink of file's parent
	 * later, so we should check upper boundary of its nlink.
	 */
	if ((!old_dir_entry || !new_dir_entry) &&
				old_dir_entry != new_dir_entry) {
		old_nlink = old_dir_entry ? -1 : 1;
		new_nlink = -old_nlink;
		err = -EMLINK;
		if ((old_nlink > 0 && old_inode->i_nlink >= F2FS_LINK_MAX) ||
			(new_nlink > 0 && new_inode->i_nlink >= F2FS_LINK_MAX))
			goto out_new_dir;
	}

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);

	err = update_dent_inode(old_inode, new_inode, &new_dentry->d_name);
	if (err)
		goto out_unlock;
	if (file_enc_name(new_inode))
		file_set_enc_name(old_inode);

	err = update_dent_inode(new_inode, old_inode, &old_dentry->d_name);
	if (err)
		goto out_undo;
	if (file_enc_name(old_inode))
		file_set_enc_name(new_inode);

	/* update ".." directory entry info of old dentry */
	if (old_dir_entry)
		f2fs_set_link(old_inode, old_dir_entry, old_dir_page, new_dir);

	/* update ".." directory entry info of new dentry */
	if (new_dir_entry)
		f2fs_set_link(new_inode, new_dir_entry, new_dir_page, old_dir);

	/* update directory entry info of old dir inode */
	f2fs_set_link(old_dir, old_entry, old_page, new_inode);

	down_write(&F2FS_I(old_inode)->i_sem);
	file_lost_pino(old_inode);
	up_write(&F2FS_I(old_inode)->i_sem);

	old_dir->i_ctime = CURRENT_TIME;
	if (old_nlink) {
		down_write(&F2FS_I(old_dir)->i_sem);
		f2fs_i_links_write(old_dir, old_nlink > 0);
		up_write(&F2FS_I(old_dir)->i_sem);
	}
	f2fs_mark_inode_dirty_sync(old_dir);

	/* update directory entry info of new dir inode */
	f2fs_set_link(new_dir, new_entry, new_page, old_inode);

	down_write(&F2FS_I(new_inode)->i_sem);
	file_lost_pino(new_inode);
	up_write(&F2FS_I(new_inode)->i_sem);

	new_dir->i_ctime = CURRENT_TIME;
	if (new_nlink) {
		down_write(&F2FS_I(new_dir)->i_sem);
		f2fs_i_links_write(new_dir, new_nlink > 0);
		up_write(&F2FS_I(new_dir)->i_sem);
	}
	f2fs_mark_inode_dirty_sync(new_dir);

	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;
out_undo:
	/*
	 * Still we may fail to recover name info of f2fs_inode here
	 * Drop it, once its name is set as encrypted
	 */
	update_dent_inode(old_inode, old_inode, &old_dentry->d_name);
out_unlock:
	f2fs_unlock_op(sbi);
out_new_dir:
	if (new_dir_entry) {
		f2fs_dentry_kunmap(new_inode, new_dir_page);
		f2fs_put_page(new_dir_page, 0);
	}
out_old_dir:
	if (old_dir_entry) {
		f2fs_dentry_kunmap(old_inode, old_dir_page);
		f2fs_put_page(old_dir_page, 0);
	}
out_new:
	f2fs_dentry_kunmap(new_dir, new_page);
	f2fs_put_page(new_page, 0);
out_old:
	f2fs_dentry_kunmap(old_dir, old_page);
	f2fs_put_page(old_page, 0);
out:
	return err;
}

static int f2fs_rename2(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE) {
		return f2fs_cross_rename(old_dir, old_dentry,
					 new_dir, new_dentry);
	}
	/*
	 * VFS has already handled the new dentry existence case,
	 * here, we just deal with "RENAME_NOREPLACE" as regular rename.
	 */
	return f2fs_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

static const char *f2fs_encrypted_get_link(struct dentry *dentry,
					   struct inode *inode,
					   struct delayed_call *done)
{
	struct page *cpage = NULL;
	char *caddr, *paddr = NULL;
	struct fscrypt_str cstr = FSTR_INIT(NULL, 0);
	struct fscrypt_str pstr = FSTR_INIT(NULL, 0);
	struct fscrypt_symlink_data *sd;
	u32 max_size = inode->i_sb->s_blocksize;
	int res;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	res = fscrypt_get_encryption_info(inode);
	if (res)
		return ERR_PTR(res);

	cpage = read_mapping_page(inode->i_mapping, 0, NULL);
	if (IS_ERR(cpage))
		return ERR_CAST(cpage);
	caddr = page_address(cpage);

	/* Symlink is encrypted */
	sd = (struct fscrypt_symlink_data *)caddr;
	cstr.name = sd->encrypted_path;
	cstr.len = le16_to_cpu(sd->len);

	/* this is broken symlink case */
	if (unlikely(cstr.len == 0)) {
		res = -ENOENT;
		goto errout;
	}

	if ((cstr.len + sizeof(struct fscrypt_symlink_data) - 1) > max_size) {
		/* Symlink data on the disk is corrupted */
		res = -EIO;
		goto errout;
	}
	res = fscrypt_fname_alloc_buffer(inode, cstr.len, &pstr);
	if (res)
		goto errout;

	res = fscrypt_fname_disk_to_usr(inode, 0, 0, &cstr, &pstr);
	if (res < 0)
		goto errout;

	/* this is broken symlink case */
	if (unlikely(pstr.name[0] == 0)) {
		res = -ENOENT;
		goto errout;
	}

	paddr = pstr.name;

	/* Null-terminate the name */
	paddr[res] = '\0';

	put_page(cpage);
	set_delayed_call(done, kfree_link, paddr);
	return paddr;
errout:
	fscrypt_fname_free_buffer(&pstr);
	put_page(cpage);
	return ERR_PTR(res);
}

const struct inode_operations f2fs_encrypted_symlink_inode_operations = {
	.readlink       = generic_readlink,
	.get_link       = f2fs_encrypted_get_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
#ifdef CONFIG_F2FS_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= f2fs_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

const struct inode_operations f2fs_dir_inode_operations = {
	.create		= f2fs_create,
	.lookup		= f2fs_lookup,
	.link		= f2fs_link,
	.unlink		= f2fs_unlink,
	.symlink	= f2fs_symlink,
	.mkdir		= f2fs_mkdir,
	.rmdir		= f2fs_rmdir,
	.mknod		= f2fs_mknod,
	.rename2	= f2fs_rename2,
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
	.get_link       = f2fs_get_link,
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
