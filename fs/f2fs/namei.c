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
#include "analde.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
#include <trace/events/f2fs.h>

static inline bool is_extension_exist(const unsigned char *s, const char *sub,
						bool tmp_ext, bool tmp_dot)
{
	size_t slen = strlen(s);
	size_t sublen = strlen(sub);
	int i;

	if (sublen == 1 && *sub == '*')
		return true;

	/*
	 * filename format of multimedia file should be defined as:
	 * "filename + '.' + extension + (optional: '.' + temp extension)".
	 */
	if (slen < sublen + 2)
		return false;

	if (!tmp_ext) {
		/* file has anal temp extension */
		if (s[slen - sublen - 1] != '.')
			return false;
		return !strncasecmp(s + slen - sublen, sub, sublen);
	}

	for (i = 1; i < slen - sublen; i++) {
		if (s[i] != '.')
			continue;
		if (!strncasecmp(s + i + 1, sub, sublen)) {
			if (!tmp_dot)
				return true;
			if (i == slen - sublen - 1 || s[i + 1 + sublen] == '.')
				return true;
		}
	}

	return false;
}

static inline bool is_temperature_extension(const unsigned char *s, const char *sub)
{
	return is_extension_exist(s, sub, true, false);
}

static inline bool is_compress_extension(const unsigned char *s, const char *sub)
{
	return is_extension_exist(s, sub, true, true);
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

static void set_compress_new_ianalde(struct f2fs_sb_info *sbi, struct ianalde *dir,
				struct ianalde *ianalde, const unsigned char *name)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;
	unsigned char (*analext)[F2FS_EXTENSION_LEN] =
						F2FS_OPTION(sbi).analextensions;
	unsigned char (*ext)[F2FS_EXTENSION_LEN] = F2FS_OPTION(sbi).extensions;
	unsigned char ext_cnt = F2FS_OPTION(sbi).compress_ext_cnt;
	unsigned char analext_cnt = F2FS_OPTION(sbi).analcompress_ext_cnt;
	int i, cold_count, hot_count;

	if (!f2fs_sb_has_compression(sbi))
		return;

	if (S_ISDIR(ianalde->i_mode))
		goto inherit_comp;

	/* This name comes only from analrmal files. */
	if (!name)
		return;

	/* Don't compress hot files. */
	f2fs_down_read(&sbi->sb_lock);
	cold_count = le32_to_cpu(sbi->raw_super->extension_count);
	hot_count = sbi->raw_super->hot_ext_count;
	for (i = cold_count; i < cold_count + hot_count; i++)
		if (is_temperature_extension(name, extlist[i]))
			break;
	f2fs_up_read(&sbi->sb_lock);
	if (i < (cold_count + hot_count))
		return;

	/* Don't compress unallowed extension. */
	for (i = 0; i < analext_cnt; i++)
		if (is_compress_extension(name, analext[i]))
			return;

	/* Compress wanting extension. */
	for (i = 0; i < ext_cnt; i++) {
		if (is_compress_extension(name, ext[i])) {
			set_compress_context(ianalde);
			return;
		}
	}
inherit_comp:
	/* Inherit the {anal-}compression flag in directory */
	if (F2FS_I(dir)->i_flags & F2FS_ANALCOMP_FL) {
		F2FS_I(ianalde)->i_flags |= F2FS_ANALCOMP_FL;
		f2fs_mark_ianalde_dirty_sync(ianalde, true);
	} else if (F2FS_I(dir)->i_flags & F2FS_COMPR_FL) {
		set_compress_context(ianalde);
	}
}

/*
 * Set file's temperature for hot/cold data separation
 */
static void set_file_temperature(struct f2fs_sb_info *sbi, struct ianalde *ianalde,
		const unsigned char *name)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;
	int i, cold_count, hot_count;

	f2fs_down_read(&sbi->sb_lock);
	cold_count = le32_to_cpu(sbi->raw_super->extension_count);
	hot_count = sbi->raw_super->hot_ext_count;
	for (i = 0; i < cold_count + hot_count; i++)
		if (is_temperature_extension(name, extlist[i]))
			break;
	f2fs_up_read(&sbi->sb_lock);

	if (i == cold_count + hot_count)
		return;

	if (i < cold_count)
		file_set_cold(ianalde);
	else
		file_set_hot(ianalde);
}

static struct ianalde *f2fs_new_ianalde(struct mnt_idmap *idmap,
						struct ianalde *dir, umode_t mode,
						const char *name)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	nid_t ianal;
	struct ianalde *ianalde;
	bool nid_free = false;
	bool encrypt = false;
	int xattr_size = 0;
	int err;

	ianalde = new_ianalde(dir->i_sb);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	if (!f2fs_alloc_nid(sbi, &ianal)) {
		err = -EANALSPC;
		goto fail;
	}

	nid_free = true;

	ianalde_init_owner(idmap, ianalde, dir, mode);

	ianalde->i_ianal = ianal;
	ianalde->i_blocks = 0;
	simple_ianalde_init_ts(ianalde);
	F2FS_I(ianalde)->i_crtime = ianalde_get_mtime(ianalde);
	ianalde->i_generation = get_random_u32();

	if (S_ISDIR(ianalde->i_mode))
		F2FS_I(ianalde)->i_current_depth = 1;

	err = insert_ianalde_locked(ianalde);
	if (err) {
		err = -EINVAL;
		goto fail;
	}

	if (f2fs_sb_has_project_quota(sbi) &&
		(F2FS_I(dir)->i_flags & F2FS_PROJINHERIT_FL))
		F2FS_I(ianalde)->i_projid = F2FS_I(dir)->i_projid;
	else
		F2FS_I(ianalde)->i_projid = make_kprojid(&init_user_ns,
							F2FS_DEF_PROJID);

	err = fscrypt_prepare_new_ianalde(dir, ianalde, &encrypt);
	if (err)
		goto fail_drop;

	err = f2fs_dquot_initialize(ianalde);
	if (err)
		goto fail_drop;

	set_ianalde_flag(ianalde, FI_NEW_IANALDE);

	if (encrypt)
		f2fs_set_encrypted_ianalde(ianalde);

	if (f2fs_sb_has_extra_attr(sbi)) {
		set_ianalde_flag(ianalde, FI_EXTRA_ATTR);
		F2FS_I(ianalde)->i_extra_isize = F2FS_TOTAL_EXTRA_ATTR_SIZE;
	}

	if (test_opt(sbi, INLINE_XATTR))
		set_ianalde_flag(ianalde, FI_INLINE_XATTR);

	if (f2fs_may_inline_dentry(ianalde))
		set_ianalde_flag(ianalde, FI_INLINE_DENTRY);

	if (f2fs_sb_has_flexible_inline_xattr(sbi)) {
		f2fs_bug_on(sbi, !f2fs_has_extra_attr(ianalde));
		if (f2fs_has_inline_xattr(ianalde))
			xattr_size = F2FS_OPTION(sbi).inline_xattr_size;
		/* Otherwise, will be 0 */
	} else if (f2fs_has_inline_xattr(ianalde) ||
				f2fs_has_inline_dentry(ianalde)) {
		xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	}
	F2FS_I(ianalde)->i_inline_xattr_size = xattr_size;

	F2FS_I(ianalde)->i_flags =
		f2fs_mask_flags(mode, F2FS_I(dir)->i_flags & F2FS_FL_INHERITED);

	if (S_ISDIR(ianalde->i_mode))
		F2FS_I(ianalde)->i_flags |= F2FS_INDEX_FL;

	if (F2FS_I(ianalde)->i_flags & F2FS_PROJINHERIT_FL)
		set_ianalde_flag(ianalde, FI_PROJ_INHERIT);

	/* Check compression first. */
	set_compress_new_ianalde(sbi, dir, ianalde, name);

	/* Should enable inline_data after compression set */
	if (test_opt(sbi, INLINE_DATA) && f2fs_may_inline_data(ianalde))
		set_ianalde_flag(ianalde, FI_INLINE_DATA);

	if (name && !test_opt(sbi, DISABLE_EXT_IDENTIFY))
		set_file_temperature(sbi, ianalde, name);

	stat_inc_inline_xattr(ianalde);
	stat_inc_inline_ianalde(ianalde);
	stat_inc_inline_dir(ianalde);

	f2fs_set_ianalde_flags(ianalde);

	f2fs_init_extent_tree(ianalde);

	trace_f2fs_new_ianalde(ianalde, 0);
	return ianalde;

fail:
	trace_f2fs_new_ianalde(ianalde, err);
	make_bad_ianalde(ianalde);
	if (nid_free)
		set_ianalde_flag(ianalde, FI_FREE_NID);
	iput(ianalde);
	return ERR_PTR(err);
fail_drop:
	trace_f2fs_new_ianalde(ianalde, err);
	dquot_drop(ianalde);
	ianalde->i_flags |= S_ANALQUOTA;
	if (nid_free)
		set_ianalde_flag(ianalde, FI_FREE_NID);
	clear_nlink(ianalde);
	unlock_new_ianalde(ianalde);
	iput(ianalde);
	return ERR_PTR(err);
}

static int f2fs_create(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct ianalde *ianalde;
	nid_t ianal = 0;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	err = f2fs_dquot_initialize(dir);
	if (err)
		return err;

	ianalde = f2fs_new_ianalde(idmap, dir, mode, dentry->d_name.name);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ianalde->i_op = &f2fs_file_ianalde_operations;
	ianalde->i_fop = &f2fs_file_operations;
	ianalde->i_mapping->a_ops = &f2fs_dblock_aops;
	ianal = ianalde->i_ianal;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, ianalde);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	f2fs_alloc_nid_done(sbi, ianal);

	d_instantiate_new(dentry, ianalde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_balance_fs(sbi, true);
	return 0;
out:
	f2fs_handle_failed_ianalde(ianalde);
	return err;
}

static int f2fs_link(struct dentry *old_dentry, struct ianalde *dir,
		struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	err = fscrypt_prepare_link(old_dentry, dir, dentry);
	if (err)
		return err;

	if (is_ianalde_flag_set(dir, FI_PROJ_INHERIT) &&
			(!projid_eq(F2FS_I(dir)->i_projid,
			F2FS_I(old_dentry->d_ianalde)->i_projid)))
		return -EXDEV;

	err = f2fs_dquot_initialize(dir);
	if (err)
		return err;

	f2fs_balance_fs(sbi, true);

	ianalde_set_ctime_current(ianalde);
	ihold(ianalde);

	set_ianalde_flag(ianalde, FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, ianalde);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	d_instantiate(dentry, ianalde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
	return 0;
out:
	clear_ianalde_flag(ianalde, FI_INC_LINK);
	iput(ianalde);
	f2fs_unlock_op(sbi);
	return err;
}

struct dentry *f2fs_get_parent(struct dentry *child)
{
	struct page *page;
	unsigned long ianal = f2fs_ianalde_by_name(d_ianalde(child), &dotdot_name, &page);

	if (!ianal) {
		if (IS_ERR(page))
			return ERR_CAST(page);
		return ERR_PTR(-EANALENT);
	}
	return d_obtain_alias(f2fs_iget(child->d_sb, ianal));
}

static int __recover_dot_dentries(struct ianalde *dir, nid_t pianal)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct qstr dot = QSTR_INIT(".", 1);
	struct f2fs_dir_entry *de;
	struct page *page;
	int err = 0;

	if (f2fs_readonly(sbi->sb)) {
		f2fs_info(sbi, "skip recovering inline_dots ianalde (ianal:%lu, pianal:%u) in readonly mountpoint",
			  dir->i_ianal, pianal);
		return 0;
	}

	if (!S_ISDIR(dir->i_mode)) {
		f2fs_err(sbi, "inconsistent ianalde status, skip recovering inline_dots ianalde (ianal:%lu, i_mode:%u, pianal:%u)",
			  dir->i_ianal, dir->i_mode, pianal);
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		return -EANALTDIR;
	}

	err = f2fs_dquot_initialize(dir);
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
		err = f2fs_do_add_link(dir, &dot, NULL, dir->i_ianal, S_IFDIR);
		if (err)
			goto out;
	}

	de = f2fs_find_entry(dir, &dotdot_name, &page);
	if (de)
		f2fs_put_page(page, 0);
	else if (IS_ERR(page))
		err = PTR_ERR(page);
	else
		err = f2fs_do_add_link(dir, &dotdot_name, NULL, pianal, S_IFDIR);
out:
	if (!err)
		clear_ianalde_flag(dir, FI_INLINE_DOTS);

	f2fs_unlock_op(sbi);
	return err;
}

static struct dentry *f2fs_lookup(struct ianalde *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct ianalde *ianalde = NULL;
	struct f2fs_dir_entry *de;
	struct page *page;
	struct dentry *new;
	nid_t ianal = -1;
	int err = 0;
	unsigned int root_ianal = F2FS_ROOT_IANAL(F2FS_I_SB(dir));
	struct f2fs_filename fname;

	trace_f2fs_lookup_start(dir, dentry, flags);

	if (dentry->d_name.len > F2FS_NAME_LEN) {
		err = -ENAMETOOLONG;
		goto out;
	}

	err = f2fs_prepare_lookup(dir, dentry, &fname);
	generic_set_encrypted_ci_d_ops(dentry);
	if (err == -EANALENT)
		goto out_splice;
	if (err)
		goto out;
	de = __f2fs_find_entry(dir, &fname, &page);
	f2fs_free_filename(&fname);

	if (!de) {
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out;
		}
		err = -EANALENT;
		goto out_splice;
	}

	ianal = le32_to_cpu(de->ianal);
	f2fs_put_page(page, 0);

	ianalde = f2fs_iget(dir->i_sb, ianal);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out;
	}

	if ((dir->i_ianal == root_ianal) && f2fs_has_inline_dots(dir)) {
		err = __recover_dot_dentries(dir, root_ianal);
		if (err)
			goto out_iput;
	}

	if (f2fs_has_inline_dots(ianalde)) {
		err = __recover_dot_dentries(ianalde, dir->i_ianal);
		if (err)
			goto out_iput;
	}
	if (IS_ENCRYPTED(dir) &&
	    (S_ISDIR(ianalde->i_mode) || S_ISLNK(ianalde->i_mode)) &&
	    !fscrypt_has_permitted_context(dir, ianalde)) {
		f2fs_warn(F2FS_I_SB(ianalde), "Inconsistent encryption contexts: %lu/%lu",
			  dir->i_ianal, ianalde->i_ianal);
		err = -EPERM;
		goto out_iput;
	}
out_splice:
#if IS_ENABLED(CONFIG_UNICODE)
	if (!ianalde && IS_CASEFOLDED(dir)) {
		/* Eventually we want to call d_add_ci(dentry, NULL)
		 * for negative dentries in the encoding case as
		 * well.  For analw, prevent the negative dentry
		 * from being cached.
		 */
		trace_f2fs_lookup_end(dir, dentry, ianal, err);
		return NULL;
	}
#endif
	new = d_splice_alias(ianalde, dentry);
	trace_f2fs_lookup_end(dir, !IS_ERR_OR_NULL(new) ? new : dentry,
				ianal, IS_ERR(new) ? PTR_ERR(new) : err);
	return new;
out_iput:
	iput(ianalde);
out:
	trace_f2fs_lookup_end(dir, dentry, ianal, err);
	return ERR_PTR(err);
}

static int f2fs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct ianalde *ianalde = d_ianalde(dentry);
	struct f2fs_dir_entry *de;
	struct page *page;
	int err;

	trace_f2fs_unlink_enter(dir, dentry);

	if (unlikely(f2fs_cp_error(sbi))) {
		err = -EIO;
		goto fail;
	}

	err = f2fs_dquot_initialize(dir);
	if (err)
		goto fail;
	err = f2fs_dquot_initialize(ianalde);
	if (err)
		goto fail;

	de = f2fs_find_entry(dir, &dentry->d_name, &page);
	if (!de) {
		if (IS_ERR(page))
			err = PTR_ERR(page);
		goto fail;
	}

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	err = f2fs_acquire_orphan_ianalde(sbi);
	if (err) {
		f2fs_unlock_op(sbi);
		f2fs_put_page(page, 0);
		goto fail;
	}
	f2fs_delete_entry(de, page, dir, ianalde);
	f2fs_unlock_op(sbi);

#if IS_ENABLED(CONFIG_UNICODE)
	/* VFS negative dentries are incompatible with Encoding and
	 * Case-insensitiveness. Eventually we'll want avoid
	 * invalidating the dentries here, alongside with returning the
	 * negative dentries at f2fs_lookup(), when it is better
	 * supported by the VFS for the CI case.
	 */
	if (IS_CASEFOLDED(dir))
		d_invalidate(dentry);
#endif
	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);
fail:
	trace_f2fs_unlink_exit(ianalde, err);
	return err;
}

static const char *f2fs_get_link(struct dentry *dentry,
				 struct ianalde *ianalde,
				 struct delayed_call *done)
{
	const char *link = page_get_link(dentry, ianalde, done);

	if (!IS_ERR(link) && !*link) {
		/* this is broken symlink case */
		do_delayed_call(done);
		clear_delayed_call(done);
		link = ERR_PTR(-EANALENT);
	}
	return link;
}

static int f2fs_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, const char *symname)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct ianalde *ianalde;
	size_t len = strlen(symname);
	struct fscrypt_str disk_link;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	err = fscrypt_prepare_symlink(dir, symname, len, dir->i_sb->s_blocksize,
				      &disk_link);
	if (err)
		return err;

	err = f2fs_dquot_initialize(dir);
	if (err)
		return err;

	ianalde = f2fs_new_ianalde(idmap, dir, S_IFLNK | S_IRWXUGO, NULL);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	if (IS_ENCRYPTED(ianalde))
		ianalde->i_op = &f2fs_encrypted_symlink_ianalde_operations;
	else
		ianalde->i_op = &f2fs_symlink_ianalde_operations;
	ianalde_analhighmem(ianalde);
	ianalde->i_mapping->a_ops = &f2fs_dblock_aops;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, ianalde);
	if (err)
		goto out_f2fs_handle_failed_ianalde;
	f2fs_unlock_op(sbi);
	f2fs_alloc_nid_done(sbi, ianalde->i_ianal);

	err = fscrypt_encrypt_symlink(ianalde, symname, len, &disk_link);
	if (err)
		goto err_out;

	err = page_symlink(ianalde, disk_link.name, disk_link.len);

err_out:
	d_instantiate_new(dentry, ianalde);

	/*
	 * Let's flush symlink data in order to avoid broken symlink as much as
	 * possible. Nevertheless, fsyncing is the best way, but there is anal
	 * way to get a file descriptor in order to flush that.
	 *
	 * Analte that, it needs to do dir->fsync to make this recoverable.
	 * If the symlink path is stored into inline_data, there is anal
	 * performance regression.
	 */
	if (!err) {
		filemap_write_and_wait_range(ianalde->i_mapping, 0,
							disk_link.len - 1);

		if (IS_DIRSYNC(dir))
			f2fs_sync_fs(sbi->sb, 1);
	} else {
		f2fs_unlink(dir, dentry);
	}

	f2fs_balance_fs(sbi, true);
	goto out_free_encrypted_link;

out_f2fs_handle_failed_ianalde:
	f2fs_handle_failed_ianalde(ianalde);
out_free_encrypted_link:
	if (disk_link.name != (unsigned char *)symname)
		kfree(disk_link.name);
	return err;
}

static int f2fs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct ianalde *ianalde;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;

	err = f2fs_dquot_initialize(dir);
	if (err)
		return err;

	ianalde = f2fs_new_ianalde(idmap, dir, S_IFDIR | mode, NULL);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ianalde->i_op = &f2fs_dir_ianalde_operations;
	ianalde->i_fop = &f2fs_dir_operations;
	ianalde->i_mapping->a_ops = &f2fs_dblock_aops;
	mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);

	set_ianalde_flag(ianalde, FI_INC_LINK);
	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, ianalde);
	if (err)
		goto out_fail;
	f2fs_unlock_op(sbi);

	f2fs_alloc_nid_done(sbi, ianalde->i_ianal);

	d_instantiate_new(dentry, ianalde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_balance_fs(sbi, true);
	return 0;

out_fail:
	clear_ianalde_flag(ianalde, FI_INC_LINK);
	f2fs_handle_failed_ianalde(ianalde);
	return err;
}

static int f2fs_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	if (f2fs_empty_dir(ianalde))
		return f2fs_unlink(dir, dentry);
	return -EANALTEMPTY;
}

static int f2fs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct ianalde *ianalde;
	int err = 0;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	err = f2fs_dquot_initialize(dir);
	if (err)
		return err;

	ianalde = f2fs_new_ianalde(idmap, dir, mode, NULL);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	init_special_ianalde(ianalde, ianalde->i_mode, rdev);
	ianalde->i_op = &f2fs_special_ianalde_operations;

	f2fs_lock_op(sbi);
	err = f2fs_add_link(dentry, ianalde);
	if (err)
		goto out;
	f2fs_unlock_op(sbi);

	f2fs_alloc_nid_done(sbi, ianalde->i_ianal);

	d_instantiate_new(dentry, ianalde);

	if (IS_DIRSYNC(dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_balance_fs(sbi, true);
	return 0;
out:
	f2fs_handle_failed_ianalde(ianalde);
	return err;
}

static int __f2fs_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
			  struct file *file, umode_t mode, bool is_whiteout,
			  struct ianalde **new_ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct ianalde *ianalde;
	int err;

	err = f2fs_dquot_initialize(dir);
	if (err)
		return err;

	ianalde = f2fs_new_ianalde(idmap, dir, mode, NULL);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	if (is_whiteout) {
		init_special_ianalde(ianalde, ianalde->i_mode, WHITEOUT_DEV);
		ianalde->i_op = &f2fs_special_ianalde_operations;
	} else {
		ianalde->i_op = &f2fs_file_ianalde_operations;
		ianalde->i_fop = &f2fs_file_operations;
		ianalde->i_mapping->a_ops = &f2fs_dblock_aops;
	}

	f2fs_lock_op(sbi);
	err = f2fs_acquire_orphan_ianalde(sbi);
	if (err)
		goto out;

	err = f2fs_do_tmpfile(ianalde, dir);
	if (err)
		goto release_out;

	/*
	 * add this analn-linked tmpfile to orphan list, in this way we could
	 * remove all unused data of tmpfile after abanalrmal power-off.
	 */
	f2fs_add_orphan_ianalde(ianalde);
	f2fs_alloc_nid_done(sbi, ianalde->i_ianal);

	if (is_whiteout) {
		f2fs_i_links_write(ianalde, false);

		spin_lock(&ianalde->i_lock);
		ianalde->i_state |= I_LINKABLE;
		spin_unlock(&ianalde->i_lock);
	} else {
		if (file)
			d_tmpfile(file, ianalde);
		else
			f2fs_i_links_write(ianalde, false);
	}
	/* link_count was changed by d_tmpfile as well. */
	f2fs_unlock_op(sbi);
	unlock_new_ianalde(ianalde);

	if (new_ianalde)
		*new_ianalde = ianalde;

	f2fs_balance_fs(sbi, true);
	return 0;

release_out:
	f2fs_release_orphan_ianalde(sbi);
out:
	f2fs_handle_failed_ianalde(ianalde);
	return err;
}

static int f2fs_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
			struct file *file, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	err = __f2fs_tmpfile(idmap, dir, file, mode, false, NULL);

	return finish_open_simple(file, err);
}

static int f2fs_create_whiteout(struct mnt_idmap *idmap,
				struct ianalde *dir, struct ianalde **whiteout)
{
	return __f2fs_tmpfile(idmap, dir, NULL,
				S_IFCHR | WHITEOUT_MODE, true, whiteout);
}

int f2fs_get_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
		     struct ianalde **new_ianalde)
{
	return __f2fs_tmpfile(idmap, dir, NULL, S_IFREG, false, new_ianalde);
}

static int f2fs_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
			struct dentry *old_dentry, struct ianalde *new_dir,
			struct dentry *new_dentry, unsigned int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct ianalde *old_ianalde = d_ianalde(old_dentry);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct ianalde *whiteout = NULL;
	struct page *old_dir_page = NULL;
	struct page *old_page, *new_page = NULL;
	struct f2fs_dir_entry *old_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry;
	struct f2fs_dir_entry *new_entry;
	bool old_is_dir = S_ISDIR(old_ianalde->i_mode);
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	if (is_ianalde_flag_set(new_dir, FI_PROJ_INHERIT) &&
			(!projid_eq(F2FS_I(new_dir)->i_projid,
			F2FS_I(old_dentry->d_ianalde)->i_projid)))
		return -EXDEV;

	/*
	 * If new_ianalde is null, the below renaming flow will
	 * add a link in old_dir which can convert inline_dir.
	 * After then, if we failed to get the entry due to other
	 * reasons like EANALMEM, we had to remove the new entry.
	 * Instead of adding such the error handling routine, let's
	 * simply convert first here.
	 */
	if (old_dir == new_dir && !new_ianalde) {
		err = f2fs_try_convert_inline_dir(old_dir, new_dentry);
		if (err)
			return err;
	}

	if (flags & RENAME_WHITEOUT) {
		err = f2fs_create_whiteout(idmap, old_dir, &whiteout);
		if (err)
			return err;
	}

	err = f2fs_dquot_initialize(old_dir);
	if (err)
		goto out;

	err = f2fs_dquot_initialize(new_dir);
	if (err)
		goto out;

	if (new_ianalde) {
		err = f2fs_dquot_initialize(new_ianalde);
		if (err)
			goto out;
	}

	err = -EANALENT;
	old_entry = f2fs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_entry) {
		if (IS_ERR(old_page))
			err = PTR_ERR(old_page);
		goto out;
	}

	if (old_is_dir && old_dir != new_dir) {
		old_dir_entry = f2fs_parent_dir(old_ianalde, &old_dir_page);
		if (!old_dir_entry) {
			if (IS_ERR(old_dir_page))
				err = PTR_ERR(old_dir_page);
			goto out_old;
		}
	}

	if (new_ianalde) {

		err = -EANALTEMPTY;
		if (old_is_dir && !f2fs_empty_dir(new_ianalde))
			goto out_dir;

		err = -EANALENT;
		new_entry = f2fs_find_entry(new_dir, &new_dentry->d_name,
						&new_page);
		if (!new_entry) {
			if (IS_ERR(new_page))
				err = PTR_ERR(new_page);
			goto out_dir;
		}

		f2fs_balance_fs(sbi, true);

		f2fs_lock_op(sbi);

		err = f2fs_acquire_orphan_ianalde(sbi);
		if (err)
			goto put_out_dir;

		f2fs_set_link(new_dir, new_entry, new_page, old_ianalde);
		new_page = NULL;

		ianalde_set_ctime_current(new_ianalde);
		f2fs_down_write(&F2FS_I(new_ianalde)->i_sem);
		if (old_is_dir)
			f2fs_i_links_write(new_ianalde, false);
		f2fs_i_links_write(new_ianalde, false);
		f2fs_up_write(&F2FS_I(new_ianalde)->i_sem);

		if (!new_ianalde->i_nlink)
			f2fs_add_orphan_ianalde(new_ianalde);
		else
			f2fs_release_orphan_ianalde(sbi);
	} else {
		f2fs_balance_fs(sbi, true);

		f2fs_lock_op(sbi);

		err = f2fs_add_link(new_dentry, old_ianalde);
		if (err) {
			f2fs_unlock_op(sbi);
			goto out_dir;
		}

		if (old_is_dir)
			f2fs_i_links_write(new_dir, true);
	}

	f2fs_down_write(&F2FS_I(old_ianalde)->i_sem);
	if (!old_is_dir || whiteout)
		file_lost_pianal(old_ianalde);
	else
		/* adjust dir's i_pianal to pass fsck check */
		f2fs_i_pianal_write(old_ianalde, new_dir->i_ianal);
	f2fs_up_write(&F2FS_I(old_ianalde)->i_sem);

	ianalde_set_ctime_current(old_ianalde);
	f2fs_mark_ianalde_dirty_sync(old_ianalde, false);

	f2fs_delete_entry(old_entry, old_page, old_dir, NULL);
	old_page = NULL;

	if (whiteout) {
		set_ianalde_flag(whiteout, FI_INC_LINK);
		err = f2fs_add_link(old_dentry, whiteout);
		if (err)
			goto put_out_dir;

		spin_lock(&whiteout->i_lock);
		whiteout->i_state &= ~I_LINKABLE;
		spin_unlock(&whiteout->i_lock);

		iput(whiteout);
	}

	if (old_is_dir) {
		if (old_dir_entry)
			f2fs_set_link(old_ianalde, old_dir_entry,
						old_dir_page, new_dir);
		else
			f2fs_put_page(old_dir_page, 0);
		f2fs_i_links_write(old_dir, false);
	}
	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT) {
		f2fs_add_ianal_entry(sbi, new_dir->i_ianal, TRANS_DIR_IANAL);
		if (S_ISDIR(old_ianalde->i_mode))
			f2fs_add_ianal_entry(sbi, old_ianalde->i_ianal,
							TRANS_DIR_IANAL);
	}

	f2fs_unlock_op(sbi);

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		f2fs_sync_fs(sbi->sb, 1);

	f2fs_update_time(sbi, REQ_TIME);
	return 0;

put_out_dir:
	f2fs_unlock_op(sbi);
	f2fs_put_page(new_page, 0);
out_dir:
	if (old_dir_entry)
		f2fs_put_page(old_dir_page, 0);
out_old:
	f2fs_put_page(old_page, 0);
out:
	iput(whiteout);
	return err;
}

static int f2fs_cross_rename(struct ianalde *old_dir, struct dentry *old_dentry,
			     struct ianalde *new_dir, struct dentry *new_dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(old_dir);
	struct ianalde *old_ianalde = d_ianalde(old_dentry);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct page *old_dir_page, *new_dir_page;
	struct page *old_page, *new_page;
	struct f2fs_dir_entry *old_dir_entry = NULL, *new_dir_entry = NULL;
	struct f2fs_dir_entry *old_entry, *new_entry;
	int old_nlink = 0, new_nlink = 0;
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	if ((is_ianalde_flag_set(new_dir, FI_PROJ_INHERIT) &&
			!projid_eq(F2FS_I(new_dir)->i_projid,
			F2FS_I(old_dentry->d_ianalde)->i_projid)) ||
	    (is_ianalde_flag_set(new_dir, FI_PROJ_INHERIT) &&
			!projid_eq(F2FS_I(old_dir)->i_projid,
			F2FS_I(new_dentry->d_ianalde)->i_projid)))
		return -EXDEV;

	err = f2fs_dquot_initialize(old_dir);
	if (err)
		goto out;

	err = f2fs_dquot_initialize(new_dir);
	if (err)
		goto out;

	err = -EANALENT;
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
		if (S_ISDIR(old_ianalde->i_mode)) {
			old_dir_entry = f2fs_parent_dir(old_ianalde,
							&old_dir_page);
			if (!old_dir_entry) {
				if (IS_ERR(old_dir_page))
					err = PTR_ERR(old_dir_page);
				goto out_new;
			}
		}

		if (S_ISDIR(new_ianalde->i_mode)) {
			new_dir_entry = f2fs_parent_dir(new_ianalde,
							&new_dir_page);
			if (!new_dir_entry) {
				if (IS_ERR(new_dir_page))
					err = PTR_ERR(new_dir_page);
				goto out_old_dir;
			}
		}
	}

	/*
	 * If cross rename between file and directory those are analt
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
		f2fs_set_link(old_ianalde, old_dir_entry, old_dir_page, new_dir);

	/* update ".." directory entry info of new dentry */
	if (new_dir_entry)
		f2fs_set_link(new_ianalde, new_dir_entry, new_dir_page, old_dir);

	/* update directory entry info of old dir ianalde */
	f2fs_set_link(old_dir, old_entry, old_page, new_ianalde);

	f2fs_down_write(&F2FS_I(old_ianalde)->i_sem);
	if (!old_dir_entry)
		file_lost_pianal(old_ianalde);
	else
		/* adjust dir's i_pianal to pass fsck check */
		f2fs_i_pianal_write(old_ianalde, new_dir->i_ianal);
	f2fs_up_write(&F2FS_I(old_ianalde)->i_sem);

	ianalde_set_ctime_current(old_dir);
	if (old_nlink) {
		f2fs_down_write(&F2FS_I(old_dir)->i_sem);
		f2fs_i_links_write(old_dir, old_nlink > 0);
		f2fs_up_write(&F2FS_I(old_dir)->i_sem);
	}
	f2fs_mark_ianalde_dirty_sync(old_dir, false);

	/* update directory entry info of new dir ianalde */
	f2fs_set_link(new_dir, new_entry, new_page, old_ianalde);

	f2fs_down_write(&F2FS_I(new_ianalde)->i_sem);
	if (!new_dir_entry)
		file_lost_pianal(new_ianalde);
	else
		/* adjust dir's i_pianal to pass fsck check */
		f2fs_i_pianal_write(new_ianalde, old_dir->i_ianal);
	f2fs_up_write(&F2FS_I(new_ianalde)->i_sem);

	ianalde_set_ctime_current(new_dir);
	if (new_nlink) {
		f2fs_down_write(&F2FS_I(new_dir)->i_sem);
		f2fs_i_links_write(new_dir, new_nlink > 0);
		f2fs_up_write(&F2FS_I(new_dir)->i_sem);
	}
	f2fs_mark_ianalde_dirty_sync(new_dir, false);

	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT) {
		f2fs_add_ianal_entry(sbi, old_dir->i_ianal, TRANS_DIR_IANAL);
		f2fs_add_ianal_entry(sbi, new_dir->i_ianal, TRANS_DIR_IANAL);
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

static int f2fs_rename2(struct mnt_idmap *idmap,
			struct ianalde *old_dir, struct dentry *old_dentry,
			struct ianalde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;

	if (flags & ~(RENAME_ANALREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	trace_f2fs_rename_start(old_dir, old_dentry, new_dir, new_dentry,
								flags);

	err = fscrypt_prepare_rename(old_dir, old_dentry, new_dir, new_dentry,
				     flags);
	if (err)
		return err;

	if (flags & RENAME_EXCHANGE)
		err = f2fs_cross_rename(old_dir, old_dentry,
					new_dir, new_dentry);
	else
	/*
	 * VFS has already handled the new dentry existence case,
	 * here, we just deal with "RENAME_ANALREPLACE" as regular rename.
	 */
		err = f2fs_rename(idmap, old_dir, old_dentry,
					new_dir, new_dentry, flags);

	trace_f2fs_rename_end(old_dentry, new_dentry, flags, err);
	return err;
}

static const char *f2fs_encrypted_get_link(struct dentry *dentry,
					   struct ianalde *ianalde,
					   struct delayed_call *done)
{
	struct page *page;
	const char *target;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	page = read_mapping_page(ianalde->i_mapping, 0, NULL);
	if (IS_ERR(page))
		return ERR_CAST(page);

	target = fscrypt_get_symlink(ianalde, page_address(page),
				     ianalde->i_sb->s_blocksize, done);
	put_page(page);
	return target;
}

static int f2fs_encrypted_symlink_getattr(struct mnt_idmap *idmap,
					  const struct path *path,
					  struct kstat *stat, u32 request_mask,
					  unsigned int query_flags)
{
	f2fs_getattr(idmap, path, stat, request_mask, query_flags);

	return fscrypt_symlink_getattr(path, stat);
}

const struct ianalde_operations f2fs_encrypted_symlink_ianalde_operations = {
	.get_link	= f2fs_encrypted_get_link,
	.getattr	= f2fs_encrypted_symlink_getattr,
	.setattr	= f2fs_setattr,
	.listxattr	= f2fs_listxattr,
};

const struct ianalde_operations f2fs_dir_ianalde_operations = {
	.create		= f2fs_create,
	.lookup		= f2fs_lookup,
	.link		= f2fs_link,
	.unlink		= f2fs_unlink,
	.symlink	= f2fs_symlink,
	.mkdir		= f2fs_mkdir,
	.rmdir		= f2fs_rmdir,
	.mkanald		= f2fs_mkanald,
	.rename		= f2fs_rename2,
	.tmpfile	= f2fs_tmpfile,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_ianalde_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
	.listxattr	= f2fs_listxattr,
	.fiemap		= f2fs_fiemap,
	.fileattr_get	= f2fs_fileattr_get,
	.fileattr_set	= f2fs_fileattr_set,
};

const struct ianalde_operations f2fs_symlink_ianalde_operations = {
	.get_link	= f2fs_get_link,
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.listxattr	= f2fs_listxattr,
};

const struct ianalde_operations f2fs_special_ianalde_operations = {
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_ianalde_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
	.listxattr	= f2fs_listxattr,
};
