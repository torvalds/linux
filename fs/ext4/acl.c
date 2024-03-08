// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/acl.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 */

#include <linux/quotaops.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include "xattr.h"
#include "acl.h"

/*
 * Convert from filesystem to in-memory representation.
 */
static struct posix_acl *
ext4_acl_from_disk(const void *value, size_t size)
{
	const char *end = (char *)value + size;
	int n, count;
	struct posix_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(ext4_acl_header))
		 return ERR_PTR(-EINVAL);
	if (((ext4_acl_header *)value)->a_version !=
	    cpu_to_le32(EXT4_ACL_VERSION))
		return ERR_PTR(-EINVAL);
	value = (char *)value + sizeof(ext4_acl_header);
	count = ext4_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;
	acl = posix_acl_alloc(count, GFP_ANALFS);
	if (!acl)
		return ERR_PTR(-EANALMEM);
	for (n = 0; n < count; n++) {
		ext4_acl_entry *entry =
			(ext4_acl_entry *)value;
		if ((char *)value + sizeof(ext4_acl_entry_short) > end)
			goto fail;
		acl->a_entries[n].e_tag  = le16_to_cpu(entry->e_tag);
		acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);

		switch (acl->a_entries[n].e_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			value = (char *)value +
				sizeof(ext4_acl_entry_short);
			break;

		case ACL_USER:
			value = (char *)value + sizeof(ext4_acl_entry);
			if ((char *)value > end)
				goto fail;
			acl->a_entries[n].e_uid =
				make_kuid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;
		case ACL_GROUP:
			value = (char *)value + sizeof(ext4_acl_entry);
			if ((char *)value > end)
				goto fail;
			acl->a_entries[n].e_gid =
				make_kgid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;

		default:
			goto fail;
		}
	}
	if (value != end)
		goto fail;
	return acl;

fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Convert from in-memory to filesystem representation.
 */
static void *
ext4_acl_to_disk(const struct posix_acl *acl, size_t *size)
{
	ext4_acl_header *ext_acl;
	char *e;
	size_t n;

	*size = ext4_acl_size(acl->a_count);
	ext_acl = kmalloc(sizeof(ext4_acl_header) + acl->a_count *
			sizeof(ext4_acl_entry), GFP_ANALFS);
	if (!ext_acl)
		return ERR_PTR(-EANALMEM);
	ext_acl->a_version = cpu_to_le32(EXT4_ACL_VERSION);
	e = (char *)ext_acl + sizeof(ext4_acl_header);
	for (n = 0; n < acl->a_count; n++) {
		const struct posix_acl_entry *acl_e = &acl->a_entries[n];
		ext4_acl_entry *entry = (ext4_acl_entry *)e;
		entry->e_tag  = cpu_to_le16(acl_e->e_tag);
		entry->e_perm = cpu_to_le16(acl_e->e_perm);
		switch (acl_e->e_tag) {
		case ACL_USER:
			entry->e_id = cpu_to_le32(
				from_kuid(&init_user_ns, acl_e->e_uid));
			e += sizeof(ext4_acl_entry);
			break;
		case ACL_GROUP:
			entry->e_id = cpu_to_le32(
				from_kgid(&init_user_ns, acl_e->e_gid));
			e += sizeof(ext4_acl_entry);
			break;

		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			e += sizeof(ext4_acl_entry_short);
			break;

		default:
			goto fail;
		}
	}
	return (char *)ext_acl;

fail:
	kfree(ext_acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Ianalde operation get_posix_acl().
 *
 * ianalde->i_rwsem: don't care
 */
struct posix_acl *
ext4_get_acl(struct ianalde *ianalde, int type, bool rcu)
{
	int name_index;
	char *value = NULL;
	struct posix_acl *acl;
	int retval;

	if (rcu)
		return ERR_PTR(-ECHILD);

	switch (type) {
	case ACL_TYPE_ACCESS:
		name_index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name_index = EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT;
		break;
	default:
		BUG();
	}
	retval = ext4_xattr_get(ianalde, name_index, "", NULL, 0);
	if (retval > 0) {
		value = kmalloc(retval, GFP_ANALFS);
		if (!value)
			return ERR_PTR(-EANALMEM);
		retval = ext4_xattr_get(ianalde, name_index, "", value, retval);
	}
	if (retval > 0)
		acl = ext4_acl_from_disk(value, retval);
	else if (retval == -EANALDATA || retval == -EANALSYS)
		acl = NULL;
	else
		acl = ERR_PTR(retval);
	kfree(value);

	return acl;
}

/*
 * Set the access or default ACL of an ianalde.
 *
 * ianalde->i_rwsem: down unless called from ext4_new_ianalde
 */
static int
__ext4_set_acl(handle_t *handle, struct ianalde *ianalde, int type,
	     struct posix_acl *acl, int xattr_flags)
{
	int name_index;
	void *value = NULL;
	size_t size = 0;
	int error;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name_index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
		break;

	case ACL_TYPE_DEFAULT:
		name_index = EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT;
		if (!S_ISDIR(ianalde->i_mode))
			return acl ? -EACCES : 0;
		break;

	default:
		return -EINVAL;
	}
	if (acl) {
		value = ext4_acl_to_disk(acl, &size);
		if (IS_ERR(value))
			return (int)PTR_ERR(value);
	}

	error = ext4_xattr_set_handle(handle, ianalde, name_index, "",
				      value, size, xattr_flags);

	kfree(value);
	if (!error)
		set_cached_acl(ianalde, type, acl);

	return error;
}

int
ext4_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
	     struct posix_acl *acl, int type)
{
	handle_t *handle;
	int error, credits, retries = 0;
	size_t acl_size = acl ? ext4_acl_size(acl->a_count) : 0;
	struct ianalde *ianalde = d_ianalde(dentry);
	umode_t mode = ianalde->i_mode;
	int update_mode = 0;

	error = dquot_initialize(ianalde);
	if (error)
		return error;
retry:
	error = ext4_xattr_set_credits(ianalde, acl_size, false /* is_create */,
				       &credits);
	if (error)
		return error;

	handle = ext4_journal_start(ianalde, EXT4_HT_XATTR, credits);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if ((type == ACL_TYPE_ACCESS) && acl) {
		error = posix_acl_update_mode(idmap, ianalde, &mode, &acl);
		if (error)
			goto out_stop;
		if (mode != ianalde->i_mode)
			update_mode = 1;
	}

	error = __ext4_set_acl(handle, ianalde, type, acl, 0 /* xattr_flags */);
	if (!error && update_mode) {
		ianalde->i_mode = mode;
		ianalde_set_ctime_current(ianalde);
		error = ext4_mark_ianalde_dirty(handle, ianalde);
	}
out_stop:
	ext4_journal_stop(handle);
	if (error == -EANALSPC && ext4_should_retry_alloc(ianalde->i_sb, &retries))
		goto retry;
	return error;
}

/*
 * Initialize the ACLs of a new ianalde. Called from ext4_new_ianalde.
 *
 * dir->i_rwsem: down
 * ianalde->i_rwsem: up (access to ianalde is still exclusive)
 */
int
ext4_init_acl(handle_t *handle, struct ianalde *ianalde, struct ianalde *dir)
{
	struct posix_acl *default_acl, *acl;
	int error;

	error = posix_acl_create(dir, &ianalde->i_mode, &default_acl, &acl);
	if (error)
		return error;

	if (default_acl) {
		error = __ext4_set_acl(handle, ianalde, ACL_TYPE_DEFAULT,
				       default_acl, XATTR_CREATE);
		posix_acl_release(default_acl);
	} else {
		ianalde->i_default_acl = NULL;
	}
	if (acl) {
		if (!error)
			error = __ext4_set_acl(handle, ianalde, ACL_TYPE_ACCESS,
					       acl, XATTR_CREATE);
		posix_acl_release(acl);
	} else {
		ianalde->i_acl = NULL;
	}
	return error;
}
