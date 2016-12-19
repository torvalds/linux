/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2006  NEC Corporation
 *
 * Created by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
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
	if (size < 4 * sizeof(struct jffs2_acl_entry_short)) {
		if (size % sizeof(struct jffs2_acl_entry_short))
			return -1;
		return size / sizeof(struct jffs2_acl_entry_short);
	} else {
		s = size - 4 * sizeof(struct jffs2_acl_entry_short);
		if (s % sizeof(struct jffs2_acl_entry))
			return -1;
		return s / sizeof(struct jffs2_acl_entry) + 4;
	}
}

static struct posix_acl *jffs2_acl_from_medium(void *value, size_t size)
{
	void *end = value + size;
	struct jffs2_acl_header *header = value;
	struct jffs2_acl_entry *entry;
	struct posix_acl *acl;
	uint32_t ver;
	int i, count;

	if (!value)
		return NULL;
	if (size < sizeof(struct jffs2_acl_header))
		return ERR_PTR(-EINVAL);
	ver = je32_to_cpu(header->a_version);
	if (ver != JFFS2_ACL_VERSION) {
		JFFS2_WARNING("Invalid ACL version. (=%u)\n", ver);
		return ERR_PTR(-EINVAL);
	}

	value += sizeof(struct jffs2_acl_header);
	count = jffs2_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	acl = posix_acl_alloc(count, GFP_KERNEL);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	for (i=0; i < count; i++) {
		entry = value;
		if (value + sizeof(struct jffs2_acl_entry_short) > end)
			goto fail;
		acl->a_entries[i].e_tag = je16_to_cpu(entry->e_tag);
		acl->a_entries[i].e_perm = je16_to_cpu(entry->e_perm);
		switch (acl->a_entries[i].e_tag) {
			case ACL_USER_OBJ:
			case ACL_GROUP_OBJ:
			case ACL_MASK:
			case ACL_OTHER:
				value += sizeof(struct jffs2_acl_entry_short);
				break;

			case ACL_USER:
				value += sizeof(struct jffs2_acl_entry);
				if (value > end)
					goto fail;
				acl->a_entries[i].e_uid =
					make_kuid(&init_user_ns,
						  je32_to_cpu(entry->e_id));
				break;
			case ACL_GROUP:
				value += sizeof(struct jffs2_acl_entry);
				if (value > end)
					goto fail;
				acl->a_entries[i].e_gid =
					make_kgid(&init_user_ns,
						  je32_to_cpu(entry->e_id));
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
	struct jffs2_acl_header *header;
	struct jffs2_acl_entry *entry;
	void *e;
	size_t i;

	*size = jffs2_acl_size(acl->a_count);
	header = kmalloc(sizeof(*header) + acl->a_count * sizeof(*entry), GFP_KERNEL);
	if (!header)
		return ERR_PTR(-ENOMEM);
	header->a_version = cpu_to_je32(JFFS2_ACL_VERSION);
	e = header + 1;
	for (i=0; i < acl->a_count; i++) {
		const struct posix_acl_entry *acl_e = &acl->a_entries[i];
		entry = e;
		entry->e_tag = cpu_to_je16(acl_e->e_tag);
		entry->e_perm = cpu_to_je16(acl_e->e_perm);
		switch(acl_e->e_tag) {
			case ACL_USER:
				entry->e_id = cpu_to_je32(
					from_kuid(&init_user_ns, acl_e->e_uid));
				e += sizeof(struct jffs2_acl_entry);
				break;
			case ACL_GROUP:
				entry->e_id = cpu_to_je32(
					from_kgid(&init_user_ns, acl_e->e_gid));
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
	return header;
 fail:
	kfree(header);
	return ERR_PTR(-EINVAL);
}

struct posix_acl *jffs2_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	char *value = NULL;
	int rc, xprefix;

	switch (type) {
	case ACL_TYPE_ACCESS:
		xprefix = JFFS2_XPREFIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		xprefix = JFFS2_XPREFIX_ACL_DEFAULT;
		break;
	default:
		BUG();
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
	kfree(value);
	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);
	return acl;
}

static int __jffs2_set_acl(struct inode *inode, int xprefix, struct posix_acl *acl)
{
	char *value = NULL;
	size_t size = 0;
	int rc;

	if (acl) {
		value = jffs2_acl_to_medium(acl, &size);
		if (IS_ERR(value))
			return PTR_ERR(value);
	}
	rc = do_jffs2_setxattr(inode, xprefix, "", value, size, 0);
	if (!value && rc == -ENODATA)
		rc = 0;
	kfree(value);

	return rc;
}

int jffs2_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int rc, xprefix;

	switch (type) {
	case ACL_TYPE_ACCESS:
		xprefix = JFFS2_XPREFIX_ACL_ACCESS;
		if (acl) {
			umode_t mode;

			rc = posix_acl_update_mode(inode, &mode, &acl);
			if (rc)
				return rc;
			if (inode->i_mode != mode) {
				struct iattr attr;

				attr.ia_valid = ATTR_MODE | ATTR_CTIME;
				attr.ia_mode = mode;
				attr.ia_ctime = CURRENT_TIME_SEC;
				rc = jffs2_do_setattr(inode, &attr);
				if (rc < 0)
					return rc;
			}
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
	rc = __jffs2_set_acl(inode, xprefix, acl);
	if (!rc)
		set_cached_acl(inode, type, acl);
	return rc;
}

int jffs2_init_acl_pre(struct inode *dir_i, struct inode *inode, umode_t *i_mode)
{
	struct posix_acl *default_acl, *acl;
	int rc;

	cache_no_acl(inode);

	rc = posix_acl_create(dir_i, i_mode, &default_acl, &acl);
	if (rc)
		return rc;

	if (default_acl) {
		set_cached_acl(inode, ACL_TYPE_DEFAULT, default_acl);
		posix_acl_release(default_acl);
	}
	if (acl) {
		set_cached_acl(inode, ACL_TYPE_ACCESS, acl);
		posix_acl_release(acl);
	}
	return 0;
}

int jffs2_init_acl_post(struct inode *inode)
{
	int rc;

	if (inode->i_default_acl) {
		rc = __jffs2_set_acl(inode, JFFS2_XPREFIX_ACL_DEFAULT, inode->i_default_acl);
		if (rc)
			return rc;
	}

	if (inode->i_acl) {
		rc = __jffs2_set_acl(inode, JFFS2_XPREFIX_ACL_ACCESS, inode->i_acl);
		if (rc)
			return rc;
	}

	return 0;
}
