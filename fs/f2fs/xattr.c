/*
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
#include <linux/security.h>
#include <linux/posix_acl_xattr.h>
#include "f2fs.h"
#include "xattr.h"

static int f2fs_xattr_generic_get(const struct xattr_handler *handler,
		struct dentry *unused, struct inode *inode,
		const char *name, void *buffer, size_t size)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);

	switch (handler->flags) {
	case F2FS_XATTR_INDEX_USER:
		if (!test_opt(sbi, XATTR_USER))
			return -EOPNOTSUPP;
		break;
	case F2FS_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		break;
	case F2FS_XATTR_INDEX_SECURITY:
		break;
	default:
		return -EINVAL;
	}
	return f2fs_getxattr(inode, handler->flags, name,
			     buffer, size, NULL);
}

static int f2fs_xattr_generic_set(const struct xattr_handler *handler,
		struct dentry *unused, struct inode *inode,
		const char *name, const void *value,
		size_t size, int flags)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);

	switch (handler->flags) {
	case F2FS_XATTR_INDEX_USER:
		if (!test_opt(sbi, XATTR_USER))
			return -EOPNOTSUPP;
		break;
	case F2FS_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		break;
	case F2FS_XATTR_INDEX_SECURITY:
		break;
	default:
		return -EINVAL;
	}
	return f2fs_setxattr(inode, handler->flags, name,
					value, size, NULL, flags);
}

static bool f2fs_xattr_user_list(struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dentry->d_sb);

	return test_opt(sbi, XATTR_USER);
}

static bool f2fs_xattr_trusted_list(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
}

static int f2fs_xattr_advise_get(const struct xattr_handler *handler,
		struct dentry *unused, struct inode *inode,
		const char *name, void *buffer, size_t size)
{
	if (buffer)
		*((char *)buffer) = F2FS_I(inode)->i_advise;
	return sizeof(char);
}

static int f2fs_xattr_advise_set(const struct xattr_handler *handler,
		struct dentry *unused, struct inode *inode,
		const char *name, const void *value,
		size_t size, int flags)
{
	if (!inode_owner_or_capable(inode))
		return -EPERM;
	if (value == NULL)
		return -EINVAL;

	F2FS_I(inode)->i_advise |= *(char *)value;
	f2fs_mark_inode_dirty_sync(inode, true);
	return 0;
}

#ifdef CONFIG_F2FS_FS_SECURITY
static int f2fs_initxattrs(struct inode *inode, const struct xattr *xattr_array,
		void *page)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = f2fs_setxattr(inode, F2FS_XATTR_INDEX_SECURITY,
				xattr->name, xattr->value,
				xattr->value_len, (struct page *)page, 0);
		if (err < 0)
			break;
	}
	return err;
}

int f2fs_init_security(struct inode *inode, struct inode *dir,
				const struct qstr *qstr, struct page *ipage)
{
	return security_inode_init_security(inode, dir, qstr,
				&f2fs_initxattrs, ipage);
}
#endif

const struct xattr_handler f2fs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= F2FS_XATTR_INDEX_USER,
	.list	= f2fs_xattr_user_list,
	.get	= f2fs_xattr_generic_get,
	.set	= f2fs_xattr_generic_set,
};

const struct xattr_handler f2fs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= F2FS_XATTR_INDEX_TRUSTED,
	.list	= f2fs_xattr_trusted_list,
	.get	= f2fs_xattr_generic_get,
	.set	= f2fs_xattr_generic_set,
};

const struct xattr_handler f2fs_xattr_advise_handler = {
	.name	= F2FS_SYSTEM_ADVISE_NAME,
	.flags	= F2FS_XATTR_INDEX_ADVISE,
	.get    = f2fs_xattr_advise_get,
	.set    = f2fs_xattr_advise_set,
};

const struct xattr_handler f2fs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.flags	= F2FS_XATTR_INDEX_SECURITY,
	.get	= f2fs_xattr_generic_get,
	.set	= f2fs_xattr_generic_set,
};

static const struct xattr_handler *f2fs_xattr_handler_map[] = {
	[F2FS_XATTR_INDEX_USER] = &f2fs_xattr_user_handler,
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	[F2FS_XATTR_INDEX_POSIX_ACL_ACCESS] = &posix_acl_access_xattr_handler,
	[F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT] = &posix_acl_default_xattr_handler,
#endif
	[F2FS_XATTR_INDEX_TRUSTED] = &f2fs_xattr_trusted_handler,
#ifdef CONFIG_F2FS_FS_SECURITY
	[F2FS_XATTR_INDEX_SECURITY] = &f2fs_xattr_security_handler,
#endif
	[F2FS_XATTR_INDEX_ADVISE] = &f2fs_xattr_advise_handler,
};

const struct xattr_handler *f2fs_xattr_handlers[] = {
	&f2fs_xattr_user_handler,
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	&f2fs_xattr_trusted_handler,
#ifdef CONFIG_F2FS_FS_SECURITY
	&f2fs_xattr_security_handler,
#endif
	&f2fs_xattr_advise_handler,
	NULL,
};

static inline const struct xattr_handler *f2fs_xattr_handler(int index)
{
	const struct xattr_handler *handler = NULL;

	if (index > 0 && index < ARRAY_SIZE(f2fs_xattr_handler_map))
		handler = f2fs_xattr_handler_map[index];
	return handler;
}

static struct f2fs_xattr_entry *__find_xattr(void *base_addr, int index,
					size_t len, const char *name)
{
	struct f2fs_xattr_entry *entry;

	list_for_each_xattr(entry, base_addr) {
		if (entry->e_name_index != index)
			continue;
		if (entry->e_name_len != len)
			continue;
		if (!memcmp(entry->e_name, name, len))
			break;
	}
	return entry;
}

static int read_all_xattrs(struct inode *inode, struct page *ipage,
							void **base_addr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_xattr_header *header;
	size_t size = PAGE_SIZE, inline_size = 0;
	void *txattr_addr;
	int err;

	inline_size = inline_xattr_size(inode);

	txattr_addr = kzalloc(inline_size + size, GFP_F2FS_ZERO);
	if (!txattr_addr)
		return -ENOMEM;

	/* read from inline xattr */
	if (inline_size) {
		struct page *page = NULL;
		void *inline_addr;

		if (ipage) {
			inline_addr = inline_xattr_addr(ipage);
		} else {
			page = get_node_page(sbi, inode->i_ino);
			if (IS_ERR(page)) {
				err = PTR_ERR(page);
				goto fail;
			}
			inline_addr = inline_xattr_addr(page);
		}
		memcpy(txattr_addr, inline_addr, inline_size);
		f2fs_put_page(page, 1);
	}

	/* read from xattr node block */
	if (F2FS_I(inode)->i_xattr_nid) {
		struct page *xpage;
		void *xattr_addr;

		/* The inode already has an extended attribute block. */
		xpage = get_node_page(sbi, F2FS_I(inode)->i_xattr_nid);
		if (IS_ERR(xpage)) {
			err = PTR_ERR(xpage);
			goto fail;
		}

		xattr_addr = page_address(xpage);
		memcpy(txattr_addr + inline_size, xattr_addr, PAGE_SIZE);
		f2fs_put_page(xpage, 1);
	}

	header = XATTR_HDR(txattr_addr);

	/* never been allocated xattrs */
	if (le32_to_cpu(header->h_magic) != F2FS_XATTR_MAGIC) {
		header->h_magic = cpu_to_le32(F2FS_XATTR_MAGIC);
		header->h_refcount = cpu_to_le32(1);
	}
	*base_addr = txattr_addr;
	return 0;
fail:
	kzfree(txattr_addr);
	return err;
}

static inline int write_all_xattrs(struct inode *inode, __u32 hsize,
				void *txattr_addr, struct page *ipage)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	size_t inline_size = 0;
	void *xattr_addr;
	struct page *xpage;
	nid_t new_nid = 0;
	int err;

	inline_size = inline_xattr_size(inode);

	if (hsize > inline_size && !F2FS_I(inode)->i_xattr_nid)
		if (!alloc_nid(sbi, &new_nid))
			return -ENOSPC;

	/* write to inline xattr */
	if (inline_size) {
		struct page *page = NULL;
		void *inline_addr;

		if (ipage) {
			inline_addr = inline_xattr_addr(ipage);
			f2fs_wait_on_page_writeback(ipage, NODE, true);
			set_page_dirty(ipage);
		} else {
			page = get_node_page(sbi, inode->i_ino);
			if (IS_ERR(page)) {
				alloc_nid_failed(sbi, new_nid);
				return PTR_ERR(page);
			}
			inline_addr = inline_xattr_addr(page);
			f2fs_wait_on_page_writeback(page, NODE, true);
		}
		memcpy(inline_addr, txattr_addr, inline_size);
		f2fs_put_page(page, 1);

		/* no need to use xattr node block */
		if (hsize <= inline_size) {
			err = truncate_xattr_node(inode, ipage);
			alloc_nid_failed(sbi, new_nid);
			return err;
		}
	}

	/* write to xattr node block */
	if (F2FS_I(inode)->i_xattr_nid) {
		xpage = get_node_page(sbi, F2FS_I(inode)->i_xattr_nid);
		if (IS_ERR(xpage)) {
			alloc_nid_failed(sbi, new_nid);
			return PTR_ERR(xpage);
		}
		f2fs_bug_on(sbi, new_nid);
		f2fs_wait_on_page_writeback(xpage, NODE, true);
	} else {
		struct dnode_of_data dn;
		set_new_dnode(&dn, inode, NULL, NULL, new_nid);
		xpage = new_node_page(&dn, XATTR_NODE_OFFSET, ipage);
		if (IS_ERR(xpage)) {
			alloc_nid_failed(sbi, new_nid);
			return PTR_ERR(xpage);
		}
		alloc_nid_done(sbi, new_nid);
	}

	xattr_addr = page_address(xpage);
	memcpy(xattr_addr, txattr_addr + inline_size, PAGE_SIZE -
						sizeof(struct node_footer));
	set_page_dirty(xpage);
	f2fs_put_page(xpage, 1);

	/* need to checkpoint during fsync */
	F2FS_I(inode)->xattr_ver = cur_cp_version(F2FS_CKPT(sbi));
	return 0;
}

int f2fs_getxattr(struct inode *inode, int index, const char *name,
		void *buffer, size_t buffer_size, struct page *ipage)
{
	struct f2fs_xattr_entry *entry;
	void *base_addr;
	int error = 0;
	size_t size, len;

	if (name == NULL)
		return -EINVAL;

	len = strlen(name);
	if (len > F2FS_NAME_LEN)
		return -ERANGE;

	error = read_all_xattrs(inode, ipage, &base_addr);
	if (error)
		return error;

	entry = __find_xattr(base_addr, index, len, name);
	if (IS_XATTR_LAST_ENTRY(entry)) {
		error = -ENODATA;
		goto cleanup;
	}

	size = le16_to_cpu(entry->e_value_size);

	if (buffer && size > buffer_size) {
		error = -ERANGE;
		goto cleanup;
	}

	if (buffer) {
		char *pval = entry->e_name + entry->e_name_len;
		memcpy(buffer, pval, size);
	}
	error = size;

cleanup:
	kzfree(base_addr);
	return error;
}

ssize_t f2fs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = d_inode(dentry);
	struct f2fs_xattr_entry *entry;
	void *base_addr;
	int error = 0;
	size_t rest = buffer_size;

	error = read_all_xattrs(inode, NULL, &base_addr);
	if (error)
		return error;

	list_for_each_xattr(entry, base_addr) {
		const struct xattr_handler *handler =
			f2fs_xattr_handler(entry->e_name_index);
		const char *prefix;
		size_t prefix_len;
		size_t size;

		if (!handler || (handler->list && !handler->list(dentry)))
			continue;

		prefix = handler->prefix ?: handler->name;
		prefix_len = strlen(prefix);
		size = prefix_len + entry->e_name_len + 1;
		if (buffer) {
			if (size > rest) {
				error = -ERANGE;
				goto cleanup;
			}
			memcpy(buffer, prefix, prefix_len);
			buffer += prefix_len;
			memcpy(buffer, entry->e_name, entry->e_name_len);
			buffer += entry->e_name_len;
			*buffer++ = 0;
		}
		rest -= size;
	}
	error = buffer_size - rest;
cleanup:
	kzfree(base_addr);
	return error;
}

static int __f2fs_setxattr(struct inode *inode, int index,
			const char *name, const void *value, size_t size,
			struct page *ipage, int flags)
{
	struct f2fs_xattr_entry *here, *last;
	void *base_addr;
	int found, newsize;
	size_t len;
	__u32 new_hsize;
	int error = 0;

	if (name == NULL)
		return -EINVAL;

	if (value == NULL)
		size = 0;

	len = strlen(name);

	if (len > F2FS_NAME_LEN)
		return -ERANGE;

	if (size > MAX_VALUE_LEN(inode))
		return -E2BIG;

	error = read_all_xattrs(inode, ipage, &base_addr);
	if (error)
		return error;

	/* find entry with wanted name. */
	here = __find_xattr(base_addr, index, len, name);

	found = IS_XATTR_LAST_ENTRY(here) ? 0 : 1;

	if ((flags & XATTR_REPLACE) && !found) {
		error = -ENODATA;
		goto exit;
	} else if ((flags & XATTR_CREATE) && found) {
		error = -EEXIST;
		goto exit;
	}

	last = here;
	while (!IS_XATTR_LAST_ENTRY(last))
		last = XATTR_NEXT_ENTRY(last);

	newsize = XATTR_ALIGN(sizeof(struct f2fs_xattr_entry) + len + size);

	/* 1. Check space */
	if (value) {
		int free;
		/*
		 * If value is NULL, it is remove operation.
		 * In case of update operation, we calculate free.
		 */
		free = MIN_OFFSET(inode) - ((char *)last - (char *)base_addr);
		if (found)
			free = free + ENTRY_SIZE(here);

		if (unlikely(free < newsize)) {
			error = -E2BIG;
			goto exit;
		}
	}

	/* 2. Remove old entry */
	if (found) {
		/*
		 * If entry is found, remove old entry.
		 * If not found, remove operation is not needed.
		 */
		struct f2fs_xattr_entry *next = XATTR_NEXT_ENTRY(here);
		int oldsize = ENTRY_SIZE(here);

		memmove(here, next, (char *)last - (char *)next);
		last = (struct f2fs_xattr_entry *)((char *)last - oldsize);
		memset(last, 0, oldsize);
	}

	new_hsize = (char *)last - (char *)base_addr;

	/* 3. Write new entry */
	if (value) {
		char *pval;
		/*
		 * Before we come here, old entry is removed.
		 * We just write new entry.
		 */
		last->e_name_index = index;
		last->e_name_len = len;
		memcpy(last->e_name, name, len);
		pval = last->e_name + len;
		memcpy(pval, value, size);
		last->e_value_size = cpu_to_le16(size);
		new_hsize += newsize;
	}

	error = write_all_xattrs(inode, new_hsize, base_addr, ipage);
	if (error)
		goto exit;

	if (is_inode_flag_set(inode, FI_ACL_MODE)) {
		inode->i_mode = F2FS_I(inode)->i_acl_mode;
		inode->i_ctime = current_time(inode);
		clear_inode_flag(inode, FI_ACL_MODE);
	}
	if (index == F2FS_XATTR_INDEX_ENCRYPTION &&
			!strcmp(name, F2FS_XATTR_NAME_ENCRYPTION_CONTEXT))
		f2fs_set_encrypted_inode(inode);
	f2fs_mark_inode_dirty_sync(inode, true);
	if (!error && S_ISDIR(inode->i_mode))
		set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_CP);
exit:
	kzfree(base_addr);
	return error;
}

int f2fs_setxattr(struct inode *inode, int index, const char *name,
				const void *value, size_t size,
				struct page *ipage, int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int err;

	/* this case is only from init_inode_metadata */
	if (ipage)
		return __f2fs_setxattr(inode, index, name, value,
						size, ipage, flags);
	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	/* protect xattr_ver */
	down_write(&F2FS_I(inode)->i_sem);
	err = __f2fs_setxattr(inode, index, name, value, size, ipage, flags);
	up_write(&F2FS_I(inode)->i_sem);
	f2fs_unlock_op(sbi);

	f2fs_update_time(sbi, REQ_TIME);
	return err;
}
