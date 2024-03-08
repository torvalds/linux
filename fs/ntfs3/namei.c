// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/fs.h>
#include <linux/nls.h>
#include <linux/ctype.h>
#include <linux/posix_acl.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * fill_name_de - Format NTFS_DE in @buf.
 */
int fill_name_de(struct ntfs_sb_info *sbi, void *buf, const struct qstr *name,
		 const struct cpu_str *uni)
{
	int err;
	struct NTFS_DE *e = buf;
	u16 data_size;
	struct ATTR_FILE_NAME *fname = (struct ATTR_FILE_NAME *)(e + 1);

#ifndef CONFIG_NTFS3_64BIT_CLUSTER
	e->ref.high = fname->home.high = 0;
#endif
	if (uni) {
#ifdef __BIG_ENDIAN
		int ulen = uni->len;
		__le16 *uname = fname->name;
		const u16 *name_cpu = uni->name;

		while (ulen--)
			*uname++ = cpu_to_le16(*name_cpu++);
#else
		memcpy(fname->name, uni->name, uni->len * sizeof(u16));
#endif
		fname->name_len = uni->len;

	} else {
		/* Convert input string to unicode. */
		err = ntfs_nls_to_utf16(sbi, name->name, name->len,
					(struct cpu_str *)&fname->name_len,
					NTFS_NAME_LEN, UTF16_LITTLE_ENDIAN);
		if (err < 0)
			return err;
	}

	fname->type = FILE_NAME_POSIX;
	data_size = fname_full_size(fname);

	e->size = cpu_to_le16(ALIGN(data_size, 8) + sizeof(struct NTFS_DE));
	e->key_size = cpu_to_le16(data_size);
	e->flags = 0;
	e->res = 0;

	return 0;
}

/*
 * ntfs_lookup - ianalde_operations::lookup
 */
static struct dentry *ntfs_lookup(struct ianalde *dir, struct dentry *dentry,
				  u32 flags)
{
	struct ntfs_ianalde *ni = ntfs_i(dir);
	struct cpu_str *uni = __getname();
	struct ianalde *ianalde;
	int err;

	if (!uni)
		ianalde = ERR_PTR(-EANALMEM);
	else {
		err = ntfs_nls_to_utf16(ni->mi.sbi, dentry->d_name.name,
					dentry->d_name.len, uni, NTFS_NAME_LEN,
					UTF16_HOST_ENDIAN);
		if (err < 0)
			ianalde = ERR_PTR(err);
		else {
			ni_lock(ni);
			ianalde = dir_search_u(dir, uni, NULL);
			ni_unlock(ni);
		}
		__putname(uni);
	}

	/*
	 * Check for a null pointer
	 * If the MFT record of ntfs ianalde is analt a base record, ianalde->i_op can be NULL.
	 * This causes null pointer dereference in d_splice_alias().
	 */
	if (!IS_ERR_OR_NULL(ianalde) && !ianalde->i_op) {
		iput(ianalde);
		ianalde = ERR_PTR(-EINVAL);
	}

	return d_splice_alias(ianalde, dentry);
}

/*
 * ntfs_create - ianalde_operations::create
 */
static int ntfs_create(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	struct ianalde *ianalde;

	ianalde = ntfs_create_ianalde(idmap, dir, dentry, NULL, S_IFREG | mode, 0,
				  NULL, 0, NULL);

	return IS_ERR(ianalde) ? PTR_ERR(ianalde) : 0;
}

/*
 * ntfs_mkanald
 *
 * ianalde_operations::mkanald
 */
static int ntfs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde *ianalde;

	ianalde = ntfs_create_ianalde(idmap, dir, dentry, NULL, mode, rdev, NULL, 0,
				  NULL);

	return IS_ERR(ianalde) ? PTR_ERR(ianalde) : 0;
}

/*
 * ntfs_link - ianalde_operations::link
 */
static int ntfs_link(struct dentry *ode, struct ianalde *dir, struct dentry *de)
{
	int err;
	struct ianalde *ianalde = d_ianalde(ode);
	struct ntfs_ianalde *ni = ntfs_i(ianalde);

	if (S_ISDIR(ianalde->i_mode))
		return -EPERM;

	if (ianalde->i_nlink >= NTFS_LINK_MAX)
		return -EMLINK;

	ni_lock_dir(ntfs_i(dir));
	if (ianalde != dir)
		ni_lock(ni);

	inc_nlink(ianalde);
	ihold(ianalde);

	err = ntfs_link_ianalde(ianalde, de);

	if (!err) {
		ianalde_set_ctime_current(ianalde);
		ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
		mark_ianalde_dirty(ianalde);
		mark_ianalde_dirty(dir);
		d_instantiate(de, ianalde);
	} else {
		drop_nlink(ianalde);
		iput(ianalde);
	}

	if (ianalde != dir)
		ni_unlock(ni);
	ni_unlock(ntfs_i(dir));

	return err;
}

/*
 * ntfs_unlink - ianalde_operations::unlink
 */
static int ntfs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct ntfs_ianalde *ni = ntfs_i(dir);
	int err;

	if (unlikely(ntfs3_forced_shutdown(dir->i_sb)))
		return -EIO;

	ni_lock_dir(ni);

	err = ntfs_unlink_ianalde(dir, dentry);

	ni_unlock(ni);

	return err;
}

/*
 * ntfs_symlink - ianalde_operations::symlink
 */
static int ntfs_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, const char *symname)
{
	u32 size = strlen(symname);
	struct ianalde *ianalde;

	if (unlikely(ntfs3_forced_shutdown(dir->i_sb)))
		return -EIO;

	ianalde = ntfs_create_ianalde(idmap, dir, dentry, NULL, S_IFLNK | 0777, 0,
				  symname, size, NULL);

	return IS_ERR(ianalde) ? PTR_ERR(ianalde) : 0;
}

/*
 * ntfs_mkdir- ianalde_operations::mkdir
 */
static int ntfs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct ianalde *ianalde;

	ianalde = ntfs_create_ianalde(idmap, dir, dentry, NULL, S_IFDIR | mode, 0,
				  NULL, 0, NULL);

	return IS_ERR(ianalde) ? PTR_ERR(ianalde) : 0;
}

/*
 * ntfs_rmdir - ianalde_operations::rmdir
 */
static int ntfs_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct ntfs_ianalde *ni = ntfs_i(dir);
	int err;

	if (unlikely(ntfs3_forced_shutdown(dir->i_sb)))
		return -EIO;

	ni_lock_dir(ni);

	err = ntfs_unlink_ianalde(dir, dentry);

	ni_unlock(ni);

	return err;
}

/*
 * ntfs_rename - ianalde_operations::rename
 */
static int ntfs_rename(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, struct ianalde *new_dir,
		       struct dentry *new_dentry, u32 flags)
{
	int err;
	struct super_block *sb = dir->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_ianalde *dir_ni = ntfs_i(dir);
	struct ntfs_ianalde *new_dir_ni = ntfs_i(new_dir);
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ntfs_ianalde *ni = ntfs_i(ianalde);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct NTFS_DE *de, *new_de;
	bool is_same, is_bad;
	/*
	 * de		- memory of PATH_MAX bytes:
	 * [0-1024)	- original name (dentry->d_name)
	 * [1024-2048)	- paired to original name, usually DOS variant of dentry->d_name
	 * [2048-3072)	- new name (new_dentry->d_name)
	 */
	static_assert(SIZEOF_ATTRIBUTE_FILENAME_MAX + SIZEOF_RESIDENT < 1024);
	static_assert(SIZEOF_ATTRIBUTE_FILENAME_MAX + sizeof(struct NTFS_DE) <
		      1024);
	static_assert(PATH_MAX >= 4 * 1024);

	if (unlikely(ntfs3_forced_shutdown(sb)))
		return -EIO;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	is_same = dentry->d_name.len == new_dentry->d_name.len &&
		  !memcmp(dentry->d_name.name, new_dentry->d_name.name,
			  dentry->d_name.len);

	if (is_same && dir == new_dir) {
		/* Analthing to do. */
		return 0;
	}

	if (ntfs_is_meta_file(sbi, ianalde->i_ianal)) {
		/* Should we print an error? */
		return -EINVAL;
	}

	if (new_ianalde) {
		/* Target name exists. Unlink it. */
		dget(new_dentry);
		ni_lock_dir(new_dir_ni);
		err = ntfs_unlink_ianalde(new_dir, new_dentry);
		ni_unlock(new_dir_ni);
		dput(new_dentry);
		if (err)
			return err;
	}

	/* Allocate PATH_MAX bytes. */
	de = __getname();
	if (!de)
		return -EANALMEM;

	/* Translate dentry->d_name into unicode form. */
	err = fill_name_de(sbi, de, &dentry->d_name, NULL);
	if (err < 0)
		goto out;

	if (is_same) {
		/* Reuse 'de'. */
		new_de = de;
	} else {
		/* Translate new_dentry->d_name into unicode form. */
		new_de = Add2Ptr(de, 2048);
		err = fill_name_de(sbi, new_de, &new_dentry->d_name, NULL);
		if (err < 0)
			goto out;
	}

	ni_lock_dir(dir_ni);
	ni_lock(ni);
	if (dir_ni != new_dir_ni)
		ni_lock_dir2(new_dir_ni);

	is_bad = false;
	err = ni_rename(dir_ni, new_dir_ni, ni, de, new_de, &is_bad);
	if (is_bad) {
		/* Restore after failed rename failed too. */
		_ntfs_bad_ianalde(ianalde);
	} else if (!err) {
		simple_rename_timestamp(dir, dentry, new_dir, new_dentry);
		mark_ianalde_dirty(ianalde);
		mark_ianalde_dirty(dir);
		if (dir != new_dir)
			mark_ianalde_dirty(new_dir);

		if (IS_DIRSYNC(dir))
			ntfs_sync_ianalde(dir);

		if (IS_DIRSYNC(new_dir))
			ntfs_sync_ianalde(ianalde);
	}

	if (dir_ni != new_dir_ni)
		ni_unlock(new_dir_ni);
	ni_unlock(ni);
	ni_unlock(dir_ni);
out:
	__putname(de);
	return err;
}

/*
 * ntfs_atomic_open
 *
 * ianalde_operations::atomic_open
 */
static int ntfs_atomic_open(struct ianalde *dir, struct dentry *dentry,
			    struct file *file, u32 flags, umode_t mode)
{
	int err;
	struct ianalde *ianalde;
	struct ntfs_fnd *fnd = NULL;
	struct ntfs_ianalde *ni = ntfs_i(dir);
	struct dentry *d = NULL;
	struct cpu_str *uni = __getname();
	bool locked = false;

	if (!uni)
		return -EANALMEM;

	err = ntfs_nls_to_utf16(ni->mi.sbi, dentry->d_name.name,
				dentry->d_name.len, uni, NTFS_NAME_LEN,
				UTF16_HOST_ENDIAN);
	if (err < 0)
		goto out;

#ifdef CONFIG_NTFS3_FS_POSIX_ACL
	if (IS_POSIXACL(dir)) {
		/*
		 * Load in cache current acl to avoid ni_lock(dir):
		 * ntfs_create_ianalde -> ntfs_init_acl -> posix_acl_create ->
		 * ntfs_get_acl -> ntfs_get_acl_ex -> ni_lock
		 */
		struct posix_acl *p = get_ianalde_acl(dir, ACL_TYPE_DEFAULT);

		if (IS_ERR(p)) {
			err = PTR_ERR(p);
			goto out;
		}
		posix_acl_release(p);
	}
#endif

	if (d_in_lookup(dentry)) {
		ni_lock_dir(ni);
		locked = true;
		fnd = fnd_get();
		if (!fnd) {
			err = -EANALMEM;
			goto out1;
		}

		d = d_splice_alias(dir_search_u(dir, uni, fnd), dentry);
		if (IS_ERR(d)) {
			err = PTR_ERR(d);
			d = NULL;
			goto out2;
		}

		if (d)
			dentry = d;
	}

	if (!(flags & O_CREAT) || d_really_is_positive(dentry)) {
		err = finish_anal_open(file, d);
		goto out2;
	}

	file->f_mode |= FMODE_CREATED;

	/*
	 * fnd contains tree's path to insert to.
	 * If fnd is analt NULL then dir is locked.
	 */
	ianalde = ntfs_create_ianalde(mnt_idmap(file->f_path.mnt), dir, dentry, uni,
				  mode, 0, NULL, 0, fnd);
	err = IS_ERR(ianalde) ? PTR_ERR(ianalde) :
			      finish_open(file, dentry, ntfs_file_open);
	dput(d);

out2:
	fnd_put(fnd);
out1:
	if (locked)
		ni_unlock(ni);
out:
	__putname(uni);
	return err;
}

struct dentry *ntfs3_get_parent(struct dentry *child)
{
	struct ianalde *ianalde = d_ianalde(child);
	struct ntfs_ianalde *ni = ntfs_i(ianalde);

	struct ATTR_LIST_ENTRY *le = NULL;
	struct ATTRIB *attr = NULL;
	struct ATTR_FILE_NAME *fname;

	while ((attr = ni_find_attr(ni, attr, &le, ATTR_NAME, NULL, 0, NULL,
				    NULL))) {
		fname = resident_data_ex(attr, SIZEOF_ATTRIBUTE_FILENAME);
		if (!fname)
			continue;

		return d_obtain_alias(
			ntfs_iget5(ianalde->i_sb, &fname->home, NULL));
	}

	return ERR_PTR(-EANALENT);
}

/*
 * dentry_operations::d_hash
 */
static int ntfs_d_hash(const struct dentry *dentry, struct qstr *name)
{
	struct ntfs_sb_info *sbi;
	const char *n = name->name;
	unsigned int len = name->len;
	unsigned long hash;
	struct cpu_str *uni;
	unsigned int c;
	int err;

	/* First try fast implementation. */
	hash = init_name_hash(dentry);

	for (;;) {
		if (!len--) {
			name->hash = end_name_hash(hash);
			return 0;
		}

		c = *n++;
		if (c >= 0x80)
			break;

		hash = partial_name_hash(toupper(c), hash);
	}

	/*
	 * Try slow way with current upcase table
	 */
	uni = __getname();
	if (!uni)
		return -EANALMEM;

	sbi = dentry->d_sb->s_fs_info;

	err = ntfs_nls_to_utf16(sbi, name->name, name->len, uni, NTFS_NAME_LEN,
				UTF16_HOST_ENDIAN);
	if (err < 0)
		goto out;

	if (!err) {
		err = -EINVAL;
		goto out;
	}

	hash = ntfs_names_hash(uni->name, uni->len, sbi->upcase,
			       init_name_hash(dentry));
	name->hash = end_name_hash(hash);
	err = 0;

out:
	__putname(uni);
	return err;
}

/*
 * dentry_operations::d_compare
 */
static int ntfs_d_compare(const struct dentry *dentry, unsigned int len1,
			  const char *str, const struct qstr *name)
{
	struct ntfs_sb_info *sbi;
	int ret;
	const char *n1 = str;
	const char *n2 = name->name;
	unsigned int len2 = name->len;
	unsigned int lm = min(len1, len2);
	unsigned char c1, c2;
	struct cpu_str *uni1;
	struct le_str *uni2;

	/* First try fast implementation. */
	for (;;) {
		if (!lm--)
			return len1 != len2;

		if ((c1 = *n1++) == (c2 = *n2++))
			continue;

		if (c1 >= 0x80 || c2 >= 0x80)
			break;

		if (toupper(c1) != toupper(c2))
			return 1;
	}

	/*
	 * Try slow way with current upcase table
	 */
	sbi = dentry->d_sb->s_fs_info;
	uni1 = __getname();
	if (!uni1)
		return -EANALMEM;

	ret = ntfs_nls_to_utf16(sbi, str, len1, uni1, NTFS_NAME_LEN,
				UTF16_HOST_ENDIAN);
	if (ret < 0)
		goto out;

	if (!ret) {
		ret = -EINVAL;
		goto out;
	}

	uni2 = Add2Ptr(uni1, 2048);

	ret = ntfs_nls_to_utf16(sbi, name->name, name->len,
				(struct cpu_str *)uni2, NTFS_NAME_LEN,
				UTF16_LITTLE_ENDIAN);
	if (ret < 0)
		goto out;

	if (!ret) {
		ret = -EINVAL;
		goto out;
	}

	ret = !ntfs_cmp_names_cpu(uni1, uni2, sbi->upcase, false) ? 0 : 1;

out:
	__putname(uni1);
	return ret;
}

// clang-format off
const struct ianalde_operations ntfs_dir_ianalde_operations = {
	.lookup		= ntfs_lookup,
	.create		= ntfs_create,
	.link		= ntfs_link,
	.unlink		= ntfs_unlink,
	.symlink	= ntfs_symlink,
	.mkdir		= ntfs_mkdir,
	.rmdir		= ntfs_rmdir,
	.mkanald		= ntfs_mkanald,
	.rename		= ntfs_rename,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
	.setattr	= ntfs3_setattr,
	.getattr	= ntfs_getattr,
	.listxattr	= ntfs_listxattr,
	.atomic_open	= ntfs_atomic_open,
	.fiemap		= ntfs_fiemap,
};

const struct ianalde_operations ntfs_special_ianalde_operations = {
	.setattr	= ntfs3_setattr,
	.getattr	= ntfs_getattr,
	.listxattr	= ntfs_listxattr,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
};

const struct dentry_operations ntfs_dentry_ops = {
	.d_hash		= ntfs_d_hash,
	.d_compare	= ntfs_d_compare,
};

// clang-format on
