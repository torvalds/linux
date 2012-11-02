/**
 * fs/f2fs/xattr.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Portions of this code from linux/fs/ext2/xattr.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher <agruen@suse.de>
 *
 * Fix by Harrison Xing <harrison@mountainviewdata.com>.
 * Extended attributes for symlinks and special files added per
 *  suggestion of Luka Renko <luka.renko@hermes.si>.
 * xattr consolidation Copyright (c) 2004 James Morris <jmorris@redhat.com>,
 *  Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/rwsem.h>
#include <linux/f2fs_fs.h>
#include "f2fs.h"
#include "xattr.h"

static size_t f2fs_xattr_generic_list(struct dentry *dentry, char *list,
		size_t list_size, const char *name, size_t name_len, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dentry->d_sb);
	int total_len, prefix_len = 0;
	const char *prefix = NULL;

	switch (type) {
	case F2FS_XATTR_INDEX_USER:
		if (!test_opt(sbi, XATTR_USER))
			return -EOPNOTSUPP;
		prefix = XATTR_USER_PREFIX;
		prefix_len = XATTR_USER_PREFIX_LEN;
		break;
	case F2FS_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		prefix = XATTR_TRUSTED_PREFIX;
		prefix_len = XATTR_TRUSTED_PREFIX_LEN;
		break;
	default:
		return -EINVAL;
	}

	total_len = prefix_len + name_len + 1;
	if (list && total_len <= list_size) {
		memcpy(list, prefix, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return total_len;
}

static int f2fs_xattr_generic_get(struct dentry *dentry, const char *name,
		void *buffer, size_t size, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dentry->d_sb);

	switch (type) {
	case F2FS_XATTR_INDEX_USER:
		if (!test_opt(sbi, XATTR_USER))
			return -EOPNOTSUPP;
		break;
	case F2FS_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		break;
	default:
		return -EINVAL;
	}
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return f2fs_getxattr(dentry->d_inode, type, name,
			buffer, size);
}

static int f2fs_xattr_generic_set(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dentry->d_sb);

	switch (type) {
	case F2FS_XATTR_INDEX_USER:
		if (!test_opt(sbi, XATTR_USER))
			return -EOPNOTSUPP;
		break;
	case F2FS_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		break;
	default:
		return -EINVAL;
	}
	if (strcmp(name, "") == 0)
		return -EINVAL;

	return f2fs_setxattr(dentry->d_inode, type, name, value, size);
}

const struct xattr_handler f2fs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= F2FS_XATTR_INDEX_USER,
	.list	= f2fs_xattr_generic_list,
	.get	= f2fs_xattr_generic_get,
	.set	= f2fs_xattr_generic_set,
};

const struct xattr_handler f2fs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= F2FS_XATTR_INDEX_TRUSTED,
	.list	= f2fs_xattr_generic_list,
	.get	= f2fs_xattr_generic_get,
	.set	= f2fs_xattr_generic_set,
};

static const struct xattr_handler *f2fs_xattr_handler_map[] = {
	[F2FS_XATTR_INDEX_USER] = &f2fs_xattr_user_handler,
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	[F2FS_XATTR_INDEX_POSIX_ACL_ACCESS] = &f2fs_xattr_acl_access_handler,
	[F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT] = &f2fs_xattr_acl_default_handler,
#endif
	[F2FS_XATTR_INDEX_TRUSTED] = &f2fs_xattr_trusted_handler,
	[F2FS_XATTR_INDEX_ADVISE] = &f2fs_xattr_advise_handler,
};

const struct xattr_handler *f2fs_xattr_handlers[] = {
	&f2fs_xattr_user_handler,
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	&f2fs_xattr_acl_access_handler,
	&f2fs_xattr_acl_default_handler,
#endif
	&f2fs_xattr_trusted_handler,
	&f2fs_xattr_advise_handler,
	NULL,
};

static inline const struct xattr_handler *f2fs_xattr_handler(int name_index)
{
	const struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < ARRAY_SIZE(f2fs_xattr_handler_map))
		handler = f2fs_xattr_handler_map[name_index];
	return handler;
}

int f2fs_getxattr(struct inode *inode, int name_index, const char *name,
		void *buffer, size_t buffer_size)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_xattr_entry *entry;
	struct page *page;
	void *base_addr;
	int error = 0, found = 0;
	int value_len, name_len;

	if (name == NULL)
		return -EINVAL;
	name_len = strlen(name);

	if (!fi->i_xattr_nid)
		return -ENODATA;

	page = get_node_page(sbi, fi->i_xattr_nid);
	base_addr = page_address(page);

	list_for_each_xattr(entry, base_addr) {
		if (entry->e_name_index != name_index)
			continue;
		if (entry->e_name_len != name_len)
			continue;
		if (!memcmp(entry->e_name, name, name_len)) {
			found = 1;
			break;
		}
	}
	if (!found) {
		error = -ENODATA;
		goto cleanup;
	}

	value_len = le16_to_cpu(entry->e_value_size);

	if (buffer && value_len > buffer_size) {
		error = -ERANGE;
		goto cleanup;
	}

	if (buffer) {
		char *pval = entry->e_name + entry->e_name_len;
		memcpy(buffer, pval, value_len);
	}
	error = value_len;

cleanup:
	f2fs_put_page(page, 1);
	return error;
}

ssize_t f2fs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_xattr_entry *entry;
	struct page *page;
	void *base_addr;
	int error = 0;
	size_t rest = buffer_size;

	if (!fi->i_xattr_nid)
		return 0;

	page = get_node_page(sbi, fi->i_xattr_nid);
	base_addr = page_address(page);

	list_for_each_xattr(entry, base_addr) {
		const struct xattr_handler *handler =
			f2fs_xattr_handler(entry->e_name_index);
		size_t size;

		if (!handler)
			continue;

		size = handler->list(dentry, buffer, rest, entry->e_name,
				entry->e_name_len, handler->flags);
		if (buffer && size > rest) {
			error = -ERANGE;
			goto cleanup;
		}

		if (buffer)
			buffer += size;
		rest -= size;
	}
	error = buffer_size - rest;
cleanup:
	f2fs_put_page(page, 1);
	return error;
}

int f2fs_setxattr(struct inode *inode, int name_index, const char *name,
					const void *value, size_t value_len)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_xattr_header *header = NULL;
	struct f2fs_xattr_entry *here, *last;
	struct page *page;
	void *base_addr;
	int error, found, free, name_len, newsize;
	char *pval;

	if (name == NULL)
		return -EINVAL;
	name_len = strlen(name);

	if (value == NULL)
		value_len = 0;

	if (name_len > 255 || value_len > MAX_VALUE_LEN)
		return -ERANGE;

	mutex_lock_op(sbi, NODE_NEW);
	if (!fi->i_xattr_nid) {
		/* Allocate new attribute block */
		struct dnode_of_data dn;

		if (!alloc_nid(sbi, &fi->i_xattr_nid)) {
			mutex_unlock_op(sbi, NODE_NEW);
			return -ENOSPC;
		}
		set_new_dnode(&dn, inode, NULL, NULL, fi->i_xattr_nid);
		mark_inode_dirty(inode);

		page = new_node_page(&dn, XATTR_NODE_OFFSET);
		if (IS_ERR(page)) {
			alloc_nid_failed(sbi, fi->i_xattr_nid);
			fi->i_xattr_nid = 0;
			mutex_unlock_op(sbi, NODE_NEW);
			return PTR_ERR(page);
		}

		alloc_nid_done(sbi, fi->i_xattr_nid);
		base_addr = page_address(page);
		header = XATTR_HDR(base_addr);
		header->h_magic = cpu_to_le32(F2FS_XATTR_MAGIC);
		header->h_refcount = cpu_to_le32(1);
	} else {
		/* The inode already has an extended attribute block. */
		page = get_node_page(sbi, fi->i_xattr_nid);
		if (IS_ERR(page)) {
			mutex_unlock_op(sbi, NODE_NEW);
			return PTR_ERR(page);
		}

		base_addr = page_address(page);
		header = XATTR_HDR(base_addr);
	}

	if (le32_to_cpu(header->h_magic) != F2FS_XATTR_MAGIC) {
		error = -EIO;
		goto cleanup;
	}

	/* find entry with wanted name. */
	found = 0;
	list_for_each_xattr(here, base_addr) {
		if (here->e_name_index != name_index)
			continue;
		if (here->e_name_len != name_len)
			continue;
		if (!memcmp(here->e_name, name, name_len)) {
			found = 1;
			break;
		}
	}

	last = here;

	while (!IS_XATTR_LAST_ENTRY(last))
		last = XATTR_NEXT_ENTRY(last);

	newsize = XATTR_ALIGN(sizeof(struct f2fs_xattr_entry) +
			name_len + value_len);

	/* 1. Check space */
	if (value) {
		/* If value is NULL, it is remove operation.
		 * In case of update operation, we caculate free.
		 */
		free = MIN_OFFSET - ((char *)last - (char *)header);
		if (found)
			free = free - ENTRY_SIZE(here);

		if (free < newsize) {
			error = -ENOSPC;
			goto cleanup;
		}
	}

	/* 2. Remove old entry */
	if (found) {
		/* If entry is found, remove old entry.
		 * If not found, remove operation is not needed.
		 */
		struct f2fs_xattr_entry *next = XATTR_NEXT_ENTRY(here);
		int oldsize = ENTRY_SIZE(here);

		memmove(here, next, (char *)last - (char *)next);
		last = (struct f2fs_xattr_entry *)((char *)last - oldsize);
		memset(last, 0, oldsize);
	}

	/* 3. Write new entry */
	if (value) {
		/* Before we come here, old entry is removed.
		 * We just write new entry. */
		memset(last, 0, newsize);
		last->e_name_index = name_index;
		last->e_name_len = name_len;
		memcpy(last->e_name, name, name_len);
		pval = last->e_name + name_len;
		memcpy(pval, value, value_len);
		last->e_value_size = cpu_to_le16(value_len);
	}

	set_page_dirty(page);
	f2fs_put_page(page, 1);

	if (is_inode_flag_set(fi, FI_ACL_MODE)) {
		inode->i_mode = fi->i_acl_mode;
		inode->i_ctime = CURRENT_TIME;
		clear_inode_flag(fi, FI_ACL_MODE);
	}
	f2fs_write_inode(inode, NULL);
	mutex_unlock_op(sbi, NODE_NEW);

	return 0;
cleanup:
	f2fs_put_page(page, 1);
	mutex_unlock_op(sbi, NODE_NEW);
	return error;
}
