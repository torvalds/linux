// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/namei.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/random.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/quotaops.h>

#include "f2fs.h"
#include "yesde.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
#include <trace/events/f2fs.h>

static struct iyesde *f2fs_new_iyesde(struct iyesde *dir, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	nid_t iyes;
	struct iyesde *iyesde;
	bool nid_free = false;
	int xattr_size = 0;
	int err;

	iyesde = new_iyesde(dir->i_sb);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	f2fs_lock_op(sbi);
	if (!f2fs_alloc_nid(sbi, &iyes)) {
		f2fs_unlock_op(sbi);
		err = -ENOSPC;
		goto fail;
	}
	f2fs_unlock_op(sbi);

	nid_free = true;

	iyesde_init_owner(iyesde, dir, mode);

	iyesde->i_iyes = iyes;
	iyesde->i_blocks = 0;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	F2FS_I(iyesde)->i_crtime = iyesde->i_mtime;
	iyesde->i_generation = prandom_u32();

	if (S_ISDIR(iyesde->i_mode))
		F2FS_I(iyesde)->i_current_depth = 1;

	err = insert_iyesde_locked(iyesde);
	if (err) {
		err = -EINVAL;
		goto fail;
	}

	if (f2fs_sb_has_project_quota(sbi) &&
		(F2FS_I(dir)->i_flags & F2FS_PROJINHERIT_FL))
		F2FS_I(iyesde)->i_projid = F2FS_I(dir)->i_projid;
	else
		F2FS_I(iyesde)->i_projid = make_kprojid(&init_user_ns,
							F2FS_DEF_PROJID);

	err = dquot_initialize(iyesde);
	if (err)
		goto fail_drop;

	set_iyesde_flag(iyesde, FI_NEW_INODE);

	/* If the directory encrypted, then we should encrypt the iyesde. */
	if ((IS_ENCRYPTED(dir) || DUMMY_ENCRYPTION_ENABLED(sbi)) &&
				f2fs_may_encrypt(iyesde))
		f2fs_set_encrypted_iyesde(iyesde);

	if (f2fs_sb_has_extra_attr(sbi)) {
		set_iyesde_flag(iyesde, FI_EXTRA_ATTR);
		F2FS_I(iyesde)->i_extra_isize = F2FS_TOTAL_EXTRA_ATTR_SIZE;
	}

	if (test_opt(sbi, INLINE_XATTR))
		set_iyesde_flag(iyesde, FI_INLINE_XATTR);

	if (test_opt(sbi, INLINE_DATA) && f2fs_may_inline_data(iyesde))
		set_iyesde_flag(iyesde, FI_INLINE_DATA);
	if (f2fs_may_inline_dentry(iyesde))
		set_iyesde_flag(iyesde, FI_INLINE_DENTRY);

	if (f2fs_sb_has_flexible_inline_xattr(sbi)) {
		f2fs_bug_on(sbi, !f2fs_has_extra_attr(iyesde));
		if (f2fs_has_inline_xattr(iyesde))
			xattr_size = F2FS_OPTION(sbi).inline_xattr_size;
		/* Otherwise, will be 0 */
	} else if (f2fs_has_inline_xattr(iyesde) ||
				f2fs_has_inline_dentry(iyesde)) {
		xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	}
	F2FS_I(iyesde)->i_inline_xattr_size = xattr_size;

	f2fs_init_extent_tree(iyesde, NULL);

	stat_inc_inline_xattr(iyesde);
	stat_inc_inline_iyesde(iyesde);
	stat_inc_inline_dir(iyesde);

	F2FS_I(iyesde)->i_flags =
		f2fs_mask_flags(mode, F2FS_I(dir)->i_flags & F2FS_FL_INHERITED);

	if (S_ISDIR(iyesde->i_mode))
		F2FS_I(iyesde)->i_flags |= F2FS_INDEX_FL;

	if (F2FS_I(iyesde)->i_flags & F2FS_PROJINHERIT_FL)
		set_iyesde_flag(iyesde, FI_PROJ_INHERIT);

	f2fs_set_iyesde_flags(iyesde);

	trace_f2fs_new_iyesde(iyesde, 0);
	return iyesde;

fail:
	trace_f2fs_new_iyesde(iyesde, err);
	make_bad_iyesde(iyesde);
	if (nid_free)
		set_iyesde_flag(iyesde, FI_FREE_NID);
	iput(iyesde);
	return ERR_PTR(err);
fail_drop:
	trace_f2fs_new_iyesde(iyesde, err);
	dquot_drop(iyesde);
	iyesde->i_flags |= S_NOQUOTA;
	if (nid_free)
		set_iyesde_flag(iyesde, FI_FREE_NID);
	clear_nlink(iyesde);
	unlock_new_iyesde(iyesde);
	iput(iyesde);
	return ERR_PTR(err);
}

static inline int is_extension_exist(const unsigned char *s, const char *sub)
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
static inline void set_file_temperature(struct f2fs_sb_info *sbi, struct iyesde *iyesde,
		const unsigned char *name)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;
	int i, cold_count, hot_count;

	down_read(&sbi->sb_lock);

	cold_count = le32_to_cpu(sbi->raw_super->extension_count);
	hot_count = sbi->raw_super->hot_ext_count;

	for (i = 0; i < cold_count + hot_count; i++) {
		if (is_extension_exist(name, extlist[i]))
			break;
	}

	up_read(&sbi->sb_lock);

	if (i == cold_count + hot_count)
		return;

	if (i < cold_count)
		file_set_cold(iyesde);
	else
		file_set_hot(iyesde);
}

int f2fs_update_extension_list(struct f2fs_sb_info *sbi, const char *name,
							bool hot, bool set)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;
	int cold_count = le32_to_cpu(sbi->raw_super->extension_count);
	int hot_count = sbi->raw_super->hot_ext_count;
	int total_count = cold_count + hot_count;
	int start, count;
	int i;

	if (set) {
		if (total_count == F2FS_MAX_EXTENSION)
			return -EINVAL;
	} else {
		if (!hot && !cold_count)
			return -EINVAL;
		if (hot && !hot_count)
			return -EINVAL;
	}

	if (hot) {
		start = cold_count;
		count = total_count;
	} else {
		start = 0;
		count = cold_count;
	}

	for (i = start; i < count; i++) {
		if (strcmp(name, extlist[i]))
			continue;

		if (set)
			return -EINVAL;

		memcpy(extlist[i], extlist[i + 1],
				F2FS_EXTENSION_LEN * (total_count - i - 1));
		memset(extlist[total_count - 1], 0, F2FS_EXTENSION_LEN);
		if (hot)
			sbi->raw_super->hot_ext_count = hot_count - 1;
		else
			sbi->raw_super->extension_count =
						cpu_to_le32(cold_count - 1);
		return 0;
	}

	if (!set)
		return -EINVAL;

	if (hot) {
		memcpy(extlist[count], name, strlen(name));
		sbi->raw_super->hot_ext_count = hot_count + 1;
	} else {
		char buf[F2FS_MAX_EXTENSION][F2FS_EXTENSION_LEN];

		memcpy(buf, &extlist[cold_count],
				F2FS_EXTENSION_LEN * hot_count);
		memset(extlist[cold_count], 0, F2FS_EXTENSION_LEN);
		memcpy(extlist[cold_count], name, strlen(name));
		memcpy(&extlist[cold_count + 1], buf,
				F2FS_EXTENSION_LEN * hot_count);
		sbi->raw_super->extension_count = cpu_to_le32(cold_count + 1);
	}
	return 0;
}

static int f2fs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
						bool excl)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct iyesde *iyesde;
	nid_t iyes = 0;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = f2fs_new_iyesde(dir, mode);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	if (!test_opt(sbi, DISABLE_EXT_IDENTIFY))
		set_file_temperature(sbi, iyesde, dentry->d_name.name);

	iyesde->i_op = &f2fs_file_iyesde_operations;
	iyesde->i_fop = &f2fs_file_operations;
	iyesde->i_mapping->a_ops = &f2fs_dblock_aops;
	iyes = iyesde->i_iyes;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, iyesde);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	f2fs_alloc_nid_done(sbi, iyes);

	d_instantiate_new(dentry, iyesde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_balance_fs(sbi, true);
	return 0;
out:
	f2fs_handle_failed_iyesde(iyesde);
	return err;
}

static int f2fs_link(struct dentry *old_dentry, struct iyesde *dir,
		struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(old_dentry);
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	err = fscrypt_prepare_link(old_dentry, dir, dentry);
	if (err)
		return err;

	if (is_iyesde_flag_set(dir, FI_PROJ_INHERIT) &&
			(!projid_eq(F2FS_I(dir)->i_projid,
			F2FS_I(old_dentry->d_iyesde)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(dir);
	if (err)
		return err;

	f2fs_balance_fs(sbi, true);

	iyesde->i_ctime = current_time(iyesde);
	ihold(iyesde);

	set_iyesde_flag(iyesde, FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, iyesde);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	d_instantiate(dentry, iyesde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;
out:
	clear_iyesde_flag(iyesde, FI_INC_LINK);
	iput(iyesde);
	f2fs_unlock_op(sbi);
	return err;
}

struct dentry *f2fs_get_parent(struct dentry *child)
{
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct page *page;
	unsigned long iyes = f2fs_iyesde_by_name(d_iyesde(child), &dotdot, &page);
	if (!iyes) {
		if (IS_ERR(page))
			return ERR_CAST(page);
		return ERR_PTR(-ENOENT);
	}
	return d_obtain_alias(f2fs_iget(child->d_sb, iyes));
}

static int __recover_dot_dentries(struct iyesde *dir, nid_t piyes)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct qstr dot = QSTR_INIT(".", 1);
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct f2fs_dir_entry *de;
	struct page *page;
	int err = 0;

	if (f2fs_readonly(sbi->sb)) {
		f2fs_info(sbi, "skip recovering inline_dots iyesde (iyes:%lu, piyes:%u) in readonly mountpoint",
			  dir->i_iyes, piyes);
		return 0;
	}

	err = dquot_initialize(dir);
	if (err)
		return err;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);

	de = f2fs_find_entry(dir, &dot, &page);
	if (de) {
		f2fs_put_page(page, 0);
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto out;
	} else {
		err = f2fs_do_add_link(dir, &dot, NULL, dir->i_iyes, S_IFDIR);
		if (err)
			goto out;
	}

	de = f2fs_find_entry(dir, &dotdot, &page);
	if (de)
		f2fs_put_page(page, 0);
	else if (IS_ERR(page))
		err = PTR_ERR(page);
	else
		err = f2fs_do_add_link(dir, &dotdot, NULL, piyes, S_IFDIR);
out:
	if (!err)
		clear_iyesde_flag(dir, FI_INLINE_DOTS);

	f2fs_unlock_op(sbi);
	return err;
}

static struct dentry *f2fs_lookup(struct iyesde *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct iyesde *iyesde = NULL;
	struct f2fs_dir_entry *de;
	struct page *page;
	struct dentry *new;
	nid_t iyes = -1;
	int err = 0;
	unsigned int root_iyes = F2FS_ROOT_INO(F2FS_I_SB(dir));
	struct fscrypt_name fname;

	trace_f2fs_lookup_start(dir, dentry, flags);

	if (dentry->d_name.len > F2FS_NAME_LEN) {
		err = -ENAMETOOLONG;
		goto out;
	}

	err = fscrypt_prepare_lookup(dir, dentry, &fname);
	if (err == -ENOENT)
		goto out_splice;
	if (err)
		goto out;
	de = __f2fs_find_entry(dir, &fname, &page);
	fscrypt_free_filename(&fname);

	if (!de) {
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out;
		}
		goto out_splice;
	}

	iyes = le32_to_cpu(de->iyes);
	f2fs_put_page(page, 0);

	iyesde = f2fs_iget(dir->i_sb, iyes);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out;
	}

	if ((dir->i_iyes == root_iyes) && f2fs_has_inline_dots(dir)) {
		err = __recover_dot_dentries(dir, root_iyes);
		if (err)
			goto out_iput;
	}

	if (f2fs_has_inline_dots(iyesde)) {
		err = __recover_dot_dentries(iyesde, dir->i_iyes);
		if (err)
			goto out_iput;
	}
	if (IS_ENCRYPTED(dir) &&
	    (S_ISDIR(iyesde->i_mode) || S_ISLNK(iyesde->i_mode)) &&
	    !fscrypt_has_permitted_context(dir, iyesde)) {
		f2fs_warn(F2FS_I_SB(iyesde), "Inconsistent encryption contexts: %lu/%lu",
			  dir->i_iyes, iyesde->i_iyes);
		err = -EPERM;
		goto out_iput;
	}
out_splice:
#ifdef CONFIG_UNICODE
	if (!iyesde && IS_CASEFOLDED(dir)) {
		/* Eventually we want to call d_add_ci(dentry, NULL)
		 * for negative dentries in the encoding case as
		 * well.  For yesw, prevent the negative dentry
		 * from being cached.
		 */
		trace_f2fs_lookup_end(dir, dentry, iyes, err);
		return NULL;
	}
#endif
	new = d_splice_alias(iyesde, dentry);
	err = PTR_ERR_OR_ZERO(new);
	trace_f2fs_lookup_end(dir, dentry, iyes, err);
	return new;
out_iput:
	iput(iyesde);
out:
	trace_f2fs_lookup_end(dir, dentry, iyes, err);
	return ERR_PTR(err);
}

static int f2fs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct iyesde *iyesde = d_iyesde(dentry);
	struct f2fs_dir_entry *de;
	struct page *page;
	int err = -ENOENT;

	trace_f2fs_unlink_enter(dir, dentry);

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;

	err = dquot_initialize(dir);
	if (err)
		return err;
	err = dquot_initialize(iyesde);
	if (err)
		return err;

	de = f2fs_find_entry(dir, &dentry->d_name, &page);
	if (!de) {
		if (IS_ERR(page))
			err = PTR_ERR(page);
		goto fail;
	}

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	err = f2fs_acquire_orphan_iyesde(sbi);
	if (err) {
		f2fs_unlock_op(sbi);
		f2fs_put_page(page, 0);
		goto fail;
	}
	f2fs_delete_entry(de, page, dir, iyesde);
#ifdef CONFIG_UNICODE
	/* VFS negative dentries are incompatible with Encoding and
	 * Case-insensitiveness. Eventually we'll want avoid
	 * invalidating the dentries here, alongside with returning the
	 * negative dentries at f2fs_lookup(), when it is  better
	 * supported by the VFS for the CI case.
	 */
	if (IS_CASEFOLDED(dir))
		d_invalidate(dentry);
#endif
	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
fail:
	trace_f2fs_unlink_exit(iyesde, err);
	return err;
}

static const char *f2fs_get_link(struct dentry *dentry,
				 struct iyesde *iyesde,
				 struct delayed_call *done)
{
	const char *link = page_get_link(dentry, iyesde, done);
	if (!IS_ERR(link) && !*link) {
		/* this is broken symlink case */
		do_delayed_call(done);
		clear_delayed_call(done);
		link = ERR_PTR(-ENOENT);
	}
	return link;
}

static int f2fs_symlink(struct iyesde *dir, struct dentry *dentry,
					const char *symname)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct iyesde *iyesde;
	size_t len = strlen(symname);
	struct fscrypt_str disk_link;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	err = fscrypt_prepare_symlink(dir, symname, len, dir->i_sb->s_blocksize,
				      &disk_link);
	if (err)
		return err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = f2fs_new_iyesde(dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	if (IS_ENCRYPTED(iyesde))
		iyesde->i_op = &f2fs_encrypted_symlink_iyesde_operations;
	else
		iyesde->i_op = &f2fs_symlink_iyesde_operations;
	iyesde_yeshighmem(iyesde);
	iyesde->i_mapping->a_ops = &f2fs_dblock_aops;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, iyesde);
	if (err)
		goto out_f2fs_handle_failed_iyesde;
	f2fs_unlock_op(sbi);
	f2fs_alloc_nid_done(sbi, iyesde->i_iyes);

	err = fscrypt_encrypt_symlink(iyesde, symname, len, &disk_link);
	if (err)
		goto err_out;

	err = page_symlink(iyesde, disk_link.name, disk_link.len);

err_out:
	d_instantiate_new(dentry, iyesde);

	/*
	 * Let's flush symlink data in order to avoid broken symlink as much as
	 * possible. Nevertheless, fsyncing is the best way, but there is yes
	 * way to get a file descriptor in order to flush that.
	 *
	 * Note that, it needs to do dir->fsync to make this recoverable.
	 * If the symlink path is stored into inline_data, there is yes
	 * performance regression.
	 */
	if (!err) {
		filemap_write_and_wait_range(iyesde->i_mapping, 0,
							disk_link.len - 1);

		if (IS_DIRSYNC(dir))
			f2fs_sync_fs(sbi->sb, 1);
	} else {
		f2fs_unlink(dir, dentry);
	}

	f2fs_balance_fs(sbi, true);
	goto out_free_encrypted_link;

out_f2fs_handle_failed_iyesde:
	f2fs_handle_failed_iyesde(iyesde);
out_free_encrypted_link:
	if (disk_link.name != (unsigned char *)symname)
		kvfree(disk_link.name);
	return err;
}

static int f2fs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct iyesde *iyesde;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = f2fs_new_iyesde(dir, S_IFDIR | mode);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	iyesde->i_op = &f2fs_dir_iyesde_operations;
	iyesde->i_fop = &f2fs_dir_operations;
	iyesde->i_mapping->a_ops = &f2fs_dblock_aops;
	iyesde_yeshighmem(iyesde);

	set_iyesde_flag(iyesde, FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, iyesde);
	if (err)
		goto out_fail;
	f2fs_unlock_op(sbi);

	f2fs_alloc_nid_done(sbi, iyesde->i_iyes);

	d_instantiate_new(dentry, iyesde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_balance_fs(sbi, true);
	return 0;

out_fail:
	clear_iyesde_flag(iyesde, FI_INC_LINK);
	f2fs_handle_failed_iyesde(iyesde);
	return err;
}

static int f2fs_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	if (f2fs_empty_dir(iyesde))
		return f2fs_unlink(dir, dentry);
	return -ENOTEMPTY;
}

static int f2fs_mkyesd(struct iyesde *dir, struct dentry *dentry,
				umode_t mode, dev_t rdev)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct iyesde *iyesde;
	int err = 0;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = f2fs_new_iyesde(dir, mode);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	init_special_iyesde(iyesde, iyesde->i_mode, rdev);
	iyesde->i_op = &f2fs_special_iyesde_operations;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, iyesde);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	f2fs_alloc_nid_done(sbi, iyesde->i_iyes);

	d_instantiate_new(dentry, iyesde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_balance_fs(sbi, true);
	return 0;
out:
	f2fs_handle_failed_iyesde(iyesde);
	return err;
}

static int __f2fs_tmpfile(struct iyesde *dir, struct dentry *dentry,
					umode_t mode, struct iyesde **whiteout)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct iyesde *iyesde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = f2fs_new_iyesde(dir, mode);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	if (whiteout) {
		init_special_iyesde(iyesde, iyesde->i_mode, WHITEOUT_DEV);
		iyesde->i_op = &f2fs_special_iyesde_operations;
	} else {
		iyesde->i_op = &f2fs_file_iyesde_operations;
		iyesde->i_fop = &f2fs_file_operations;
		iyesde->i_mapping->a_ops = &f2fs_dblock_aops;
	}

	f2fs_lock_op(sbi);
	err = f2fs_acquire_orphan_iyesde(sbi);
	if (err)
		goto out;

	err = f2fs_do_tmpfile(iyesde, dir);
	if (err)
		goto release_out;

	/*
	 * add this yesn-linked tmpfile to orphan list, in this way we could
	 * remove all unused data of tmpfile after abyesrmal power-off.
	 */
	f2fs_add_orphan_iyesde(iyesde);
	f2fs_alloc_nid_done(sbi, iyesde->i_iyes);

	if (whiteout) {
		f2fs_i_links_write(iyesde, false);
		*whiteout = iyesde;
	} else {
		d_tmpfile(dentry, iyesde);
	}
	/* link_count was changed by d_tmpfile as well. */
	f2fs_unlock_op(sbi);
	unlock_new_iyesde(iyesde);

	f2fs_balance_fs(sbi, true);
	return 0;

release_out:
	f2fs_release_orphan_iyesde(sbi);
out:
	f2fs_handle_failed_iyesde(iyesde);
	return err;
}

static int f2fs_tmpfile(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	if (IS_ENCRYPTED(dir) || DUMMY_ENCRYPTION_ENABLED(sbi)) {
		int err = fscrypt_get_encryption_info(dir);
		if (err)
			return err;
	}

	return __f2fs_tmpfile(dir, dentry, mode, NULL);
}

static int f2fs_create_whiteout(struct iyesde *dir, struct iyesde **whiteout)
{
	if (unlikely(f2fs_cp_error(F2FS_I_SB(dir))))
		return -EIO;

	return __f2fs_tmpfile(dir, NULL, S_IFCHR | WHITEOUT_MODE, whiteout);
}

static int f2fs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			struct iyesde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
	struct iyesde *whiteout = NULL;
	struct page *old_dir_page;
	struct page *old_page, *new_page = NULL;
	struct f2fs_dir_entry *old_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry;
	struct f2fs_dir_entry *new_entry;
	bool is_old_inline = f2fs_has_inline_dentry(old_dir);
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	if (is_iyesde_flag_set(new_dir, FI_PROJ_INHERIT) &&
			(!projid_eq(F2FS_I(new_dir)->i_projid,
			F2FS_I(old_dentry->d_iyesde)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(old_dir);
	if (err)
		goto out;

	err = dquot_initialize(new_dir);
	if (err)
		goto out;

	if (new_iyesde) {
		err = dquot_initialize(new_iyesde);
		if (err)
			goto out;
	}

	err = -ENOENT;
	old_entry = f2fs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_entry) {
		if (IS_ERR(old_page))
			err = PTR_ERR(old_page);
		goto out;
	}

	if (S_ISDIR(old_iyesde->i_mode)) {
		old_dir_entry = f2fs_parent_dir(old_iyesde, &old_dir_page);
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

	if (new_iyesde) {

		err = -ENOTEMPTY;
		if (old_dir_entry && !f2fs_empty_dir(new_iyesde))
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

		err = f2fs_acquire_orphan_iyesde(sbi);
		if (err)
			goto put_out_dir;

		f2fs_set_link(new_dir, new_entry, new_page, old_iyesde);

		new_iyesde->i_ctime = current_time(new_iyesde);
		down_write(&F2FS_I(new_iyesde)->i_sem);
		if (old_dir_entry)
			f2fs_i_links_write(new_iyesde, false);
		f2fs_i_links_write(new_iyesde, false);
		up_write(&F2FS_I(new_iyesde)->i_sem);

		if (!new_iyesde->i_nlink)
			f2fs_add_orphan_iyesde(new_iyesde);
		else
			f2fs_release_orphan_iyesde(sbi);
	} else {
		f2fs_balance_fs(sbi, true);

		f2fs_lock_op(sbi);

		err = f2fs_add_link(new_dentry, old_iyesde);
		if (err) {
			f2fs_unlock_op(sbi);
			goto out_whiteout;
		}

		if (old_dir_entry)
			f2fs_i_links_write(new_dir, true);

		/*
		 * old entry and new entry can locate in the same inline
		 * dentry in iyesde, when attaching new entry in inline dentry,
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

	down_write(&F2FS_I(old_iyesde)->i_sem);
	if (!old_dir_entry || whiteout)
		file_lost_piyes(old_iyesde);
	else
		/* adjust dir's i_piyes to pass fsck check */
		f2fs_i_piyes_write(old_iyesde, new_dir->i_iyes);
	up_write(&F2FS_I(old_iyesde)->i_sem);

	old_iyesde->i_ctime = current_time(old_iyesde);
	f2fs_mark_iyesde_dirty_sync(old_iyesde, false);

	f2fs_delete_entry(old_entry, old_page, old_dir, NULL);

	if (whiteout) {
		whiteout->i_state |= I_LINKABLE;
		set_iyesde_flag(whiteout, FI_INC_LINK);
		err = f2fs_add_link(old_dentry, whiteout);
		if (err)
			goto put_out_dir;
		whiteout->i_state &= ~I_LINKABLE;
		iput(whiteout);
	}

	if (old_dir_entry) {
		if (old_dir != new_dir && !whiteout)
			f2fs_set_link(old_iyesde, old_dir_entry,
						old_dir_page, new_dir);
		else
			f2fs_put_page(old_dir_page, 0);
		f2fs_i_links_write(old_dir, false);
	}
	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT) {
		f2fs_add_iyes_entry(sbi, new_dir->i_iyes, TRANS_DIR_INO);
		if (S_ISDIR(old_iyesde->i_mode))
			f2fs_add_iyes_entry(sbi, old_iyesde->i_iyes,
							TRANS_DIR_INO);
	}

	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_update_time(sbi, REQ_TIME);
	return 0;

put_out_dir:
	f2fs_unlock_op(sbi);
	if (new_page)
		f2fs_put_page(new_page, 0);
out_whiteout:
	if (whiteout)
		iput(whiteout);
out_dir:
	if (old_dir_entry)
		f2fs_put_page(old_dir_page, 0);
out_old:
	f2fs_put_page(old_page, 0);
out:
	return err;
}

static int f2fs_cross_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			     struct iyesde *new_dir, struct dentry *new_dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
	struct page *old_dir_page, *new_dir_page;
	struct page *old_page, *new_page;
	struct f2fs_dir_entry *old_dir_entry = NULL, *new_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry, *new_entry;
	int old_nlink = 0, new_nlink = 0;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	if ((is_iyesde_flag_set(new_dir, FI_PROJ_INHERIT) &&
			!projid_eq(F2FS_I(new_dir)->i_projid,
			F2FS_I(old_dentry->d_iyesde)->i_projid)) ||
	    (is_iyesde_flag_set(new_dir, FI_PROJ_INHERIT) &&
			!projid_eq(F2FS_I(old_dir)->i_projid,
			F2FS_I(new_dentry->d_iyesde)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(old_dir);
	if (err)
		goto out;

	err = dquot_initialize(new_dir);
	if (err)
		goto out;

	err = -ENOENT;
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
		if (S_ISDIR(old_iyesde->i_mode)) {
			old_dir_entry = f2fs_parent_dir(old_iyesde,
							&old_dir_page);
			if (!old_dir_entry) {
				if (IS_ERR(old_dir_page))
					err = PTR_ERR(old_dir_page);
				goto out_new;
			}
		}

		if (S_ISDIR(new_iyesde->i_mode)) {
			new_dir_entry = f2fs_parent_dir(new_iyesde,
							&new_dir_page);
			if (!new_dir_entry) {
				if (IS_ERR(new_dir_page))
					err = PTR_ERR(new_dir_page);
				goto out_old_dir;
			}
		}
	}

	/*
	 * If cross rename between file and directory those are yest
	 * in the same directory, we will inc nlink of file's parent
	 * later, so we should check upper boundary of its nlink.
	 */
	if ((!old_dir_entry || !new_dir_entry) &&
				old_dir_entry != new_dir_entry) {
		old_nlink = old_dir_entry ? -1 : 1;
		new_nlink = -old_nlink;
		err = -EMLINK;
		if ((old_nlink > 0 && old_dir->i_nlink >= F2FS_LINK_MAX) ||
			(new_nlink > 0 && new_dir->i_nlink >= F2FS_LINK_MAX))
			goto out_new_dir;
	}

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);

	/* update ".." directory entry info of old dentry */
	if (old_dir_entry)
		f2fs_set_link(old_iyesde, old_dir_entry, old_dir_page, new_dir);

	/* update ".." directory entry info of new dentry */
	if (new_dir_entry)
		f2fs_set_link(new_iyesde, new_dir_entry, new_dir_page, old_dir);

	/* update directory entry info of old dir iyesde */
	f2fs_set_link(old_dir, old_entry, old_page, new_iyesde);

	down_write(&F2FS_I(old_iyesde)->i_sem);
	if (!old_dir_entry)
		file_lost_piyes(old_iyesde);
	else
		/* adjust dir's i_piyes to pass fsck check */
		f2fs_i_piyes_write(old_iyesde, new_dir->i_iyes);
	up_write(&F2FS_I(old_iyesde)->i_sem);

	old_dir->i_ctime = current_time(old_dir);
	if (old_nlink) {
		down_write(&F2FS_I(old_dir)->i_sem);
		f2fs_i_links_write(old_dir, old_nlink > 0);
		up_write(&F2FS_I(old_dir)->i_sem);
	}
	f2fs_mark_iyesde_dirty_sync(old_dir, false);

	/* update directory entry info of new dir iyesde */
	f2fs_set_link(new_dir, new_entry, new_page, old_iyesde);

	down_write(&F2FS_I(new_iyesde)->i_sem);
	if (!new_dir_entry)
		file_lost_piyes(new_iyesde);
	else
		/* adjust dir's i_piyes to pass fsck check */
		f2fs_i_piyes_write(new_iyesde, old_dir->i_iyes);
	up_write(&F2FS_I(new_iyesde)->i_sem);

	new_dir->i_ctime = current_time(new_dir);
	if (new_nlink) {
		down_write(&F2FS_I(new_dir)->i_sem);
		f2fs_i_links_write(new_dir, new_nlink > 0);
		up_write(&F2FS_I(new_dir)->i_sem);
	}
	f2fs_mark_iyesde_dirty_sync(new_dir, false);

	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT) {
		f2fs_add_iyes_entry(sbi, old_dir->i_iyes, TRANS_DIR_INO);
		f2fs_add_iyes_entry(sbi, new_dir->i_iyes, TRANS_DIR_INO);
	}

	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_update_time(sbi, REQ_TIME);
	return 0;
out_new_dir:
	if (new_dir_entry) {
		f2fs_put_page(new_dir_page, 0);
	}
out_old_dir:
	if (old_dir_entry) {
		f2fs_put_page(old_dir_page, 0);
	}
out_new:
	f2fs_put_page(new_page, 0);
out_old:
	f2fs_put_page(old_page, 0);
out:
	return err;
}

static int f2fs_rename2(struct iyesde *old_dir, struct dentry *old_dentry,
			struct iyesde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	err = fscrypt_prepare_rename(old_dir, old_dentry, new_dir, new_dentry,
				     flags);
	if (err)
		return err;

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
					   struct iyesde *iyesde,
					   struct delayed_call *done)
{
	struct page *page;
	const char *target;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	page = read_mapping_page(iyesde->i_mapping, 0, NULL);
	if (IS_ERR(page))
		return ERR_CAST(page);

	target = fscrypt_get_symlink(iyesde, page_address(page),
				     iyesde->i_sb->s_blocksize, done);
	put_page(page);
	return target;
}

const struct iyesde_operations f2fs_encrypted_symlink_iyesde_operations = {
	.get_link       = f2fs_encrypted_get_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
#ifdef CONFIG_F2FS_FS_XATTR
	.listxattr	= f2fs_listxattr,
#endif
};

const struct iyesde_operations f2fs_dir_iyesde_operations = {
	.create		= f2fs_create,
	.lookup		= f2fs_lookup,
	.link		= f2fs_link,
	.unlink		= f2fs_unlink,
	.symlink	= f2fs_symlink,
	.mkdir		= f2fs_mkdir,
	.rmdir		= f2fs_rmdir,
	.mkyesd		= f2fs_mkyesd,
	.rename		= f2fs_rename2,
	.tmpfile	= f2fs_tmpfile,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.listxattr	= f2fs_listxattr,
#endif
	.fiemap		= f2fs_fiemap,
};

const struct iyesde_operations f2fs_symlink_iyesde_operations = {
	.get_link       = f2fs_get_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
#ifdef CONFIG_F2FS_FS_XATTR
	.listxattr	= f2fs_listxattr,
#endif
};

const struct iyesde_operations f2fs_special_iyesde_operations = {
	.getattr	= f2fs_getattr,
	.setattr        = f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.listxattr	= f2fs_listxattr,
#endif
};
