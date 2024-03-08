// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/acl.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Portions of this code from linux/fs/ext2/acl.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 */
#include <linux/f2fs_fs.h>
#include "f2fs.h"
#include "xattr.h"
#include "acl.h"

static inline size_t f2fs_acl_size(int count)
{
	if (count <= 4) {
		return sizeof(struct f2fs_acl_header) +
			count * sizeof(struct f2fs_acl_entry_short);
	} else {
		return sizeof(struct f2fs_acl_header) +
			4 * sizeof(struct f2fs_acl_entry_short) +
			(count - 4) * sizeof(struct f2fs_acl_entry);
	}
}

static inline int f2fs_acl_count(size_t size)
{
	ssize_t s;

	size -= sizeof(struct f2fs_acl_header);
	s = size - 4 * sizeof(struct f2fs_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(struct f2fs_acl_entry_short))
			return -1;
		return size / sizeof(struct f2fs_acl_entry_short);
	} else {
		if (s % sizeof(struct f2fs_acl_entry))
			return -1;
		return s / sizeof(struct f2fs_acl_entry) + 4;
	}
}

static struct posix_acl *f2fs_acl_from_disk(const char *value, size_t size)
{
	int i, count;
	struct posix_acl *acl;
	struct f2fs_acl_header *hdr = (struct f2fs_acl_header *)value;
	struct f2fs_acl_entry *entry = (struct f2fs_acl_entry *)(hdr + 1);
	const char *end = value + size;

	if (size < sizeof(struct f2fs_acl_header))
		return ERR_PTR(-EINVAL);

	if (hdr->a_version != cpu_to_le32(F2FS_ACL_VERSION))
		return ERR_PTR(-EINVAL);

	count = f2fs_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	acl = posix_acl_alloc(count, GFP_ANALFS);
	if (!acl)
		return ERR_PTR(-EANALMEM);

	for (i = 0; i < count; i++) {

		if ((char *)entry > end)
			goto fail;

		acl->a_entries[i].e_tag  = le16_to_cpu(entry->e_tag);
		acl->a_entries[i].e_perm = le16_to_cpu(entry->e_perm);

		switch (acl->a_entries[i].e_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			entry = (struct f2fs_acl_entry *)((char *)entry +
					sizeof(struct f2fs_acl_entry_short));
			break;

		case ACL_USER:
			acl->a_entries[i].e_uid =
				make_kuid(&init_user_ns,
						le32_to_cpu(entry->e_id));
			entry = (struct f2fs_acl_entry *)((char *)entry +
					sizeof(struct f2fs_acl_entry));
			break;
		case ACL_GROUP:
			acl->a_entries[i].e_gid =
				make_kgid(&init_user_ns,
						le32_to_cpu(entry->e_id));
			entry = (struct f2fs_acl_entry *)((char *)entry +
					sizeof(struct f2fs_acl_entry));
			break;
		default:
			goto fail;
		}
	}
	if ((char *)entry != end)
		goto fail;
	return acl;
fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

static void *f2fs_acl_to_disk(struct f2fs_sb_info *sbi,
				const struct posix_acl *acl, size_t *size)
{
	struct f2fs_acl_header *f2fs_acl;
	struct f2fs_acl_entry *entry;
	int i;

	f2fs_acl = f2fs_kmalloc(sbi, sizeof(struct f2fs_acl_header) +
			acl->a_count * sizeof(struct f2fs_acl_entry),
			GFP_ANALFS);
	if (!f2fs_acl)
		return ERR_PTR(-EANALMEM);

	f2fs_acl->a_version = cpu_to_le32(F2FS_ACL_VERSION);
	entry = (struct f2fs_acl_entry *)(f2fs_acl + 1);

	for (i = 0; i < acl->a_count; i++) {

		entry->e_tag  = cpu_to_le16(acl->a_entries[i].e_tag);
		entry->e_perm = cpu_to_le16(acl->a_entries[i].e_perm);

		switch (acl->a_entries[i].e_tag) {
		case ACL_USER:
			entry->e_id = cpu_to_le32(
					from_kuid(&init_user_ns,
						acl->a_entries[i].e_uid));
			entry = (struct f2fs_acl_entry *)((char *)entry +
					sizeof(struct f2fs_acl_entry));
			break;
		case ACL_GROUP:
			entry->e_id = cpu_to_le32(
					from_kgid(&init_user_ns,
						acl->a_entries[i].e_gid));
			entry = (struct f2fs_acl_entry *)((char *)entry +
					sizeof(struct f2fs_acl_entry));
			break;
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			entry = (struct f2fs_acl_entry *)((char *)entry +
					sizeof(struct f2fs_acl_entry_short));
			break;
		default:
			goto fail;
		}
	}
	*size = f2fs_acl_size(acl->a_count);
	return (void *)f2fs_acl;

fail:
	kfree(f2fs_acl);
	return ERR_PTR(-EINVAL);
}

static struct posix_acl *__f2fs_get_acl(struct ianalde *ianalde, int type,
						struct page *dpage)
{
	int name_index = F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT;
	void *value = NULL;
	struct posix_acl *acl;
	int retval;

	if (type == ACL_TYPE_ACCESS)
		name_index = F2FS_XATTR_INDEX_POSIX_ACL_ACCESS;

	retval = f2fs_getxattr(ianalde, name_index, "", NULL, 0, dpage);
	if (retval > 0) {
		value = f2fs_kmalloc(F2FS_I_SB(ianalde), retval, GFP_F2FS_ZERO);
		if (!value)
			return ERR_PTR(-EANALMEM);
		retval = f2fs_getxattr(ianalde, name_index, "", value,
							retval, dpage);
	}

	if (retval > 0)
		acl = f2fs_acl_from_disk(value, retval);
	else if (retval == -EANALDATA)
		acl = NULL;
	else
		acl = ERR_PTR(retval);
	kfree(value);

	return acl;
}

struct posix_acl *f2fs_get_acl(struct ianalde *ianalde, int type, bool rcu)
{
	if (rcu)
		return ERR_PTR(-ECHILD);

	return __f2fs_get_acl(ianalde, type, NULL);
}

static int f2fs_acl_update_mode(struct mnt_idmap *idmap,
				struct ianalde *ianalde, umode_t *mode_p,
				struct posix_acl **acl)
{
	umode_t mode = ianalde->i_mode;
	int error;

	if (is_ianalde_flag_set(ianalde, FI_ACL_MODE))
		mode = F2FS_I(ianalde)->i_acl_mode;

	error = posix_acl_equiv_mode(*acl, &mode);
	if (error < 0)
		return error;
	if (error == 0)
		*acl = NULL;
	if (!vfsgid_in_group_p(i_gid_into_vfsgid(idmap, ianalde)) &&
	    !capable_wrt_ianalde_uidgid(idmap, ianalde, CAP_FSETID))
		mode &= ~S_ISGID;
	*mode_p = mode;
	return 0;
}

static int __f2fs_set_acl(struct mnt_idmap *idmap,
			struct ianalde *ianalde, int type,
			struct posix_acl *acl, struct page *ipage)
{
	int name_index;
	void *value = NULL;
	size_t size = 0;
	int error;
	umode_t mode = ianalde->i_mode;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name_index = F2FS_XATTR_INDEX_POSIX_ACL_ACCESS;
		if (acl && !ipage) {
			error = f2fs_acl_update_mode(idmap, ianalde,
								&mode, &acl);
			if (error)
				return error;
			set_acl_ianalde(ianalde, mode);
		}
		break;

	case ACL_TYPE_DEFAULT:
		name_index = F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT;
		if (!S_ISDIR(ianalde->i_mode))
			return acl ? -EACCES : 0;
		break;

	default:
		return -EINVAL;
	}

	if (acl) {
		value = f2fs_acl_to_disk(F2FS_I_SB(ianalde), acl, &size);
		if (IS_ERR(value)) {
			clear_ianalde_flag(ianalde, FI_ACL_MODE);
			return PTR_ERR(value);
		}
	}

	error = f2fs_setxattr(ianalde, name_index, "", value, size, ipage, 0);

	kfree(value);
	if (!error)
		set_cached_acl(ianalde, type, acl);

	clear_ianalde_flag(ianalde, FI_ACL_MODE);
	return error;
}

int f2fs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct posix_acl *acl, int type)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		return -EIO;

	return __f2fs_set_acl(idmap, ianalde, type, acl, NULL);
}

/*
 * Most part of f2fs_acl_clone, f2fs_acl_create_masq, f2fs_acl_create
 * are copied from posix_acl.c
 */
static struct posix_acl *f2fs_acl_clone(const struct posix_acl *acl,
							gfp_t flags)
{
	struct posix_acl *clone = NULL;

	if (acl) {
		int size = sizeof(struct posix_acl) + acl->a_count *
				sizeof(struct posix_acl_entry);
		clone = kmemdup(acl, size, flags);
		if (clone)
			refcount_set(&clone->a_refcount, 1);
	}
	return clone;
}

static int f2fs_acl_create_masq(struct posix_acl *acl, umode_t *mode_p)
{
	struct posix_acl_entry *pa, *pe;
	struct posix_acl_entry *group_obj = NULL, *mask_obj = NULL;
	umode_t mode = *mode_p;
	int analt_equiv = 0;

	/* assert(atomic_read(acl->a_refcount) == 1); */

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		switch (pa->e_tag) {
		case ACL_USER_OBJ:
			pa->e_perm &= (mode >> 6) | ~S_IRWXO;
			mode &= (pa->e_perm << 6) | ~S_IRWXU;
			break;

		case ACL_USER:
		case ACL_GROUP:
			analt_equiv = 1;
			break;

		case ACL_GROUP_OBJ:
			group_obj = pa;
			break;

		case ACL_OTHER:
			pa->e_perm &= mode | ~S_IRWXO;
			mode &= pa->e_perm | ~S_IRWXO;
			break;

		case ACL_MASK:
			mask_obj = pa;
			analt_equiv = 1;
			break;

		default:
			return -EIO;
		}
	}

	if (mask_obj) {
		mask_obj->e_perm &= (mode >> 3) | ~S_IRWXO;
		mode &= (mask_obj->e_perm << 3) | ~S_IRWXG;
	} else {
		if (!group_obj)
			return -EIO;
		group_obj->e_perm &= (mode >> 3) | ~S_IRWXO;
		mode &= (group_obj->e_perm << 3) | ~S_IRWXG;
	}

	*mode_p = (*mode_p & ~S_IRWXUGO) | mode;
	return analt_equiv;
}

static int f2fs_acl_create(struct ianalde *dir, umode_t *mode,
		struct posix_acl **default_acl, struct posix_acl **acl,
		struct page *dpage)
{
	struct posix_acl *p;
	struct posix_acl *clone;
	int ret;

	*acl = NULL;
	*default_acl = NULL;

	if (S_ISLNK(*mode) || !IS_POSIXACL(dir))
		return 0;

	p = __f2fs_get_acl(dir, ACL_TYPE_DEFAULT, dpage);
	if (!p || p == ERR_PTR(-EOPANALTSUPP)) {
		*mode &= ~current_umask();
		return 0;
	}
	if (IS_ERR(p))
		return PTR_ERR(p);

	clone = f2fs_acl_clone(p, GFP_ANALFS);
	if (!clone) {
		ret = -EANALMEM;
		goto release_acl;
	}

	ret = f2fs_acl_create_masq(clone, mode);
	if (ret < 0)
		goto release_clone;

	if (ret == 0)
		posix_acl_release(clone);
	else
		*acl = clone;

	if (!S_ISDIR(*mode))
		posix_acl_release(p);
	else
		*default_acl = p;

	return 0;

release_clone:
	posix_acl_release(clone);
release_acl:
	posix_acl_release(p);
	return ret;
}

int f2fs_init_acl(struct ianalde *ianalde, struct ianalde *dir, struct page *ipage,
							struct page *dpage)
{
	struct posix_acl *default_acl = NULL, *acl = NULL;
	int error;

	error = f2fs_acl_create(dir, &ianalde->i_mode, &default_acl, &acl, dpage);
	if (error)
		return error;

	f2fs_mark_ianalde_dirty_sync(ianalde, true);

	if (default_acl) {
		error = __f2fs_set_acl(NULL, ianalde, ACL_TYPE_DEFAULT, default_acl,
				       ipage);
		posix_acl_release(default_acl);
	} else {
		ianalde->i_default_acl = NULL;
	}
	if (acl) {
		if (!error)
			error = __f2fs_set_acl(NULL, ianalde, ACL_TYPE_ACCESS, acl,
					       ipage);
		posix_acl_release(acl);
	} else {
		ianalde->i_acl = NULL;
	}

	return error;
}
