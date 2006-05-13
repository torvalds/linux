/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2006  NEC Corporation
 *
 * Created by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/crc32.h>
#include <linux/jffs2.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/mtd/mtd.h>
#include "nodelist.h"

static size_t jffs2_acl_size(int count)
{
	if (count <= 4) {
		return sizeof(struct jffs2_acl_header)
		       + count * sizeof(struct jffs2_acl_entry_short);
	} else {
		return sizeof(struct jffs2_acl_header)
		       + 4 * sizeof(struct jffs2_acl_entry_short)
		       + (count - 4) * sizeof(struct jffs2_acl_entry);
	}
}

static int jffs2_acl_count(size_t size)
{
	size_t s;

	size -= sizeof(struct jffs2_acl_header);
	s = size - 4 * sizeof(struct jffs2_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(struct jffs2_acl_entry_short))
			return -1;
		return size / sizeof(struct jffs2_acl_entry_short);
	} else {
		if (s % sizeof(struct jffs2_acl_entry))
			return -1;
		return s / sizeof(struct jffs2_acl_entry) + 4;
	}
}

static struct posix_acl *jffs2_acl_from_medium(const void *value, size_t size)
{
	const char *end = (char *)value + size;
	struct posix_acl *acl;
	uint32_t ver;
	int i, count;

	if (!value)
		return NULL;
	if (size < sizeof(struct jffs2_acl_header))
		return ERR_PTR(-EINVAL);
	ver = je32_to_cpu(((struct jffs2_acl_header *)value)->a_version);
	if (ver != JFFS2_ACL_VERSION) {
		JFFS2_WARNING("Invalid ACL version. (=%u)\n", ver);
		return ERR_PTR(-EINVAL);
	}

	value = (char *)value + sizeof(struct jffs2_acl_header);
	count = jffs2_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	acl = posix_acl_alloc(count, GFP_KERNEL);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	for (i=0; i < count; i++) {
		struct jffs2_acl_entry *entry = (struct jffs2_acl_entry *)value;
		if ((char *)value + sizeof(struct jffs2_acl_entry_short) > end)
			goto fail;
		acl->a_entries[i].e_tag = je16_to_cpu(entry->e_tag);
		acl->a_entries[i].e_perm = je16_to_cpu(entry->e_perm);
		switch (acl->a_entries[i].e_tag) {
			case ACL_USER_OBJ:
			case ACL_GROUP_OBJ:
			case ACL_MASK:
			case ACL_OTHER:
				value = (char *)value + sizeof(struct jffs2_acl_entry_short);
				acl->a_entries[i].e_id = ACL_UNDEFINED_ID;
				break;

			case ACL_USER:
			case ACL_GROUP:
				value = (char *)value + sizeof(struct jffs2_acl_entry);
				if ((char *)value > end)
					goto fail;
				acl->a_entries[i].e_id = je32_to_cpu(entry->e_id);
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

static void *jffs2_acl_to_medium(const struct posix_acl *acl, size_t *size)
{
	struct jffs2_acl_header *jffs2_acl;
	char *e;
	size_t i;

	*size = jffs2_acl_size(acl->a_count);
	jffs2_acl = kmalloc(sizeof(struct jffs2_acl_header)
			     + acl->a_count * sizeof(struct jffs2_acl_entry),
			    GFP_KERNEL);
	if (!jffs2_acl)
		return ERR_PTR(-ENOMEM);
	jffs2_acl->a_version = cpu_to_je32(JFFS2_ACL_VERSION);
	e = (char *)jffs2_acl + sizeof(struct jffs2_acl_header);
	for (i=0; i < acl->a_count; i++) {
		struct jffs2_acl_entry *entry = (struct jffs2_acl_entry *)e;
		entry->e_tag = cpu_to_je16(acl->a_entries[i].e_tag);
		entry->e_perm = cpu_to_je16(acl->a_entries[i].e_perm);
		switch(acl->a_entries[i].e_tag) {
			case ACL_USER:
			case ACL_GROUP:
				entry->e_id = cpu_to_je32(acl->a_entries[i].e_id);
				e += sizeof(struct jffs2_acl_entry);
				break;

			case ACL_USER_OBJ:
			case ACL_GROUP_OBJ:
			case ACL_MASK:
			case ACL_OTHER:
				e += sizeof(struct jffs2_acl_entry_short);
				break;

			default:
				goto fail;
		}
	}
	return (char *)jffs2_acl;
 fail:
	kfree(jffs2_acl);
	return ERR_PTR(-EINVAL);
}

static struct posix_acl *jffs2_iget_acl(struct inode *inode, struct posix_acl **i_acl)
{
	struct posix_acl *acl = JFFS2_ACL_NOT_CACHED;

	spin_lock(&inode->i_lock);
	if (*i_acl != JFFS2_ACL_NOT_CACHED)
		acl = posix_acl_dup(*i_acl);
	spin_unlock(&inode->i_lock);
	return acl;
}

static void jffs2_iset_acl(struct inode *inode, struct posix_acl **i_acl, struct posix_acl *acl)
{
	spin_lock(&inode->i_lock);
	if (*i_acl != JFFS2_ACL_NOT_CACHED)
		posix_acl_release(*i_acl);
	*i_acl = posix_acl_dup(acl);
	spin_unlock(&inode->i_lock);
}

static struct posix_acl *jffs2_get_acl(struct inode *inode, int type)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct posix_acl *acl;
	char *value = NULL;
	int rc, xprefix;

	switch (type) {
	case ACL_TYPE_ACCESS:
		acl = jffs2_iget_acl(inode, &f->i_acl_access);
		if (acl != JFFS2_ACL_NOT_CACHED)
			return acl;
		xprefix = JFFS2_XPREFIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		acl = jffs2_iget_acl(inode, &f->i_acl_default);
		if (acl != JFFS2_ACL_NOT_CACHED)
			return acl;
		xprefix = JFFS2_XPREFIX_ACL_DEFAULT;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	rc = do_jffs2_getxattr(inode, xprefix, "", NULL, 0);
	if (rc > 0) {
		value = kmalloc(rc, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		rc = do_jffs2_getxattr(inode, xprefix, "", value, rc);
	}
	if (rc > 0) {
		acl = jffs2_acl_from_medium(value, rc);
	} else if (rc == -ENODATA || rc == -ENOSYS) {
		acl = NULL;
	} else {
		acl = ERR_PTR(rc);
	}
	if (value)
		kfree(value);
	if (!IS_ERR(acl)) {
		switch (type) {
		case ACL_TYPE_ACCESS:
			jffs2_iset_acl(inode, &f->i_acl_access, acl);
			break;
		case ACL_TYPE_DEFAULT:
			jffs2_iset_acl(inode, &f->i_acl_default, acl);
			break;
		}
	}
	return acl;
}

static int jffs2_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	size_t size = 0;
	char *value = NULL;
	int rc, xprefix;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch (type) {
	case ACL_TYPE_ACCESS:
		xprefix = JFFS2_XPREFIX_ACL_ACCESS;
		if (acl) {
			mode_t mode = inode->i_mode;
			rc = posix_acl_equiv_mode(acl, &mode);
			if (rc < 0)
				return rc;
			if (inode->i_mode != mode) {
				inode->i_mode = mode;
				jffs2_dirty_inode(inode);
			}
			if (rc == 0)
				acl = NULL;
		}
		break;
	case ACL_TYPE_DEFAULT:
		xprefix = JFFS2_XPREFIX_ACL_DEFAULT;
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		break;
	default:
		return -EINVAL;
	}
	if (acl) {
		value = jffs2_acl_to_medium(acl, &size);
		if (IS_ERR(value))
			return PTR_ERR(value);
	}

	rc = do_jffs2_setxattr(inode, xprefix, "", value, size, 0);
	if (value)
		kfree(value);
	if (!rc) {
		switch(type) {
		case ACL_TYPE_ACCESS:
			jffs2_iset_acl(inode, &f->i_acl_access, acl);
			break;
		case ACL_TYPE_DEFAULT:
			jffs2_iset_acl(inode, &f->i_acl_default, acl);
			break;
		}
	}
	return rc;
}

static int jffs2_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl;
	int rc;

	acl = jffs2_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		rc = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return rc;
	}
	return -EAGAIN;
}

int jffs2_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	return generic_permission(inode, mask, jffs2_check_acl);
}

int jffs2_init_acl(struct inode *inode, struct inode *dir)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct posix_acl *acl = NULL, *clone;
	mode_t mode;
	int rc = 0;

	f->i_acl_access = JFFS2_ACL_NOT_CACHED;
	f->i_acl_default = JFFS2_ACL_NOT_CACHED;
	if (!S_ISLNK(inode->i_mode)) {
		acl = jffs2_get_acl(dir, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (!acl)
			inode->i_mode &= ~current->fs->umask;
	}
	if (acl) {
		if (S_ISDIR(inode->i_mode)) {
			rc = jffs2_set_acl(inode, ACL_TYPE_DEFAULT, acl);
			if (rc)
				goto cleanup;
		}
		clone = posix_acl_clone(acl, GFP_KERNEL);
		rc = -ENOMEM;
		if (!clone)
			goto cleanup;
		mode = inode->i_mode;
		rc = posix_acl_create_masq(clone, &mode);
		if (rc >= 0) {
			inode->i_mode = mode;
			if (rc > 0)
				rc = jffs2_set_acl(inode, ACL_TYPE_ACCESS, clone);
		}
		posix_acl_release(clone);
	}
 cleanup:
	posix_acl_release(acl);
	return rc;
}

void jffs2_clear_acl(struct inode *inode)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);

	if (f->i_acl_access && f->i_acl_access != JFFS2_ACL_NOT_CACHED) {
		posix_acl_release(f->i_acl_access);
		f->i_acl_access = JFFS2_ACL_NOT_CACHED;
	}
	if (f->i_acl_default && f->i_acl_default != JFFS2_ACL_NOT_CACHED) {
		posix_acl_release(f->i_acl_default);
		f->i_acl_default = JFFS2_ACL_NOT_CACHED;
	}
}

int jffs2_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int rc;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	acl = jffs2_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);
	clone = posix_acl_clone(acl, GFP_KERNEL);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;
	rc = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!rc)
		rc = jffs2_set_acl(inode, ACL_TYPE_ACCESS, clone);
	posix_acl_release(clone);
	return rc;
}

static size_t jffs2_acl_access_listxattr(struct inode *inode, char *list, size_t list_size,
					 const char *name, size_t name_len)
{
	const int retlen = sizeof(POSIX_ACL_XATTR_ACCESS);

	if (list && retlen <= list_size)
		strcpy(list, POSIX_ACL_XATTR_ACCESS);
	return retlen;
}

static size_t jffs2_acl_default_listxattr(struct inode *inode, char *list, size_t list_size,
					  const char *name, size_t name_len)
{
	const int retlen = sizeof(POSIX_ACL_XATTR_DEFAULT);

	if (list && retlen <= list_size)
		strcpy(list, POSIX_ACL_XATTR_DEFAULT);
	return retlen;
}

static int jffs2_acl_getxattr(struct inode *inode, int type, void *buffer, size_t size)
{
	struct posix_acl *acl;
	int rc;

	acl = jffs2_get_acl(inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (!acl)
		return -ENODATA;
	rc = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return rc;
}

static int jffs2_acl_access_getxattr(struct inode *inode, const char *name, void *buffer, size_t size)
{
	if (name[0] != '\0')
		return -EINVAL;
	return jffs2_acl_getxattr(inode, ACL_TYPE_ACCESS, buffer, size);
}

static int jffs2_acl_default_getxattr(struct inode *inode, const char *name, void *buffer, size_t size)
{
	if (name[0] != '\0')
		return -EINVAL;
	return jffs2_acl_getxattr(inode, ACL_TYPE_DEFAULT, buffer, size);
}

static int jffs2_acl_setxattr(struct inode *inode, int type, const void *value, size_t size)
{
	struct posix_acl *acl;
	int rc;

	if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
		return -EPERM;

	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (acl) {
			rc = posix_acl_valid(acl);
			if (rc)
				goto out;
		}
	} else {
		acl = NULL;
	}
	rc = jffs2_set_acl(inode, type, acl);
 out:
	posix_acl_release(acl);
	return rc;
}

static int jffs2_acl_access_setxattr(struct inode *inode, const char *name,
				     const void *buffer, size_t size, int flags)
{
	if (name[0] != '\0')
		return -EINVAL;
	return jffs2_acl_setxattr(inode, ACL_TYPE_ACCESS, buffer, size);
}

static int jffs2_acl_default_setxattr(struct inode *inode, const char *name,
				      const void *buffer, size_t size, int flags)
{
	if (name[0] != '\0')
		return -EINVAL;
	return jffs2_acl_setxattr(inode, ACL_TYPE_DEFAULT, buffer, size);
}

struct xattr_handler jffs2_acl_access_xattr_handler = {
	.prefix	= POSIX_ACL_XATTR_ACCESS,
	.list	= jffs2_acl_access_listxattr,
	.get	= jffs2_acl_access_getxattr,
	.set	= jffs2_acl_access_setxattr,
};

struct xattr_handler jffs2_acl_default_xattr_handler = {
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
	.list	= jffs2_acl_default_listxattr,
	.get	= jffs2_acl_default_getxattr,
	.set	= jffs2_acl_default_setxattr,
};
