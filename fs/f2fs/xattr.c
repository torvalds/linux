// SPDX-License-Identifier: GPL-2.0
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
 */
#include <linux/rwsem.h>
#include <linux/f2fs_fs.h>
#include <linux/security.h>
#include <linux/posix_acl_xattr.h>
#include "f2fs.h"
#include "xattr.h"
#include "segment.h"

static void *xattr_alloc(struct f2fs_sb_info *sbi, int size, bool *is_inline)
{
	if (likely(size == sbi->inline_xattr_slab_size)) {
		*is_inline = true;
		return f2fs_kmem_cache_alloc(sbi->inline_xattr_slab,
					GFP_F2FS_ZERO, false, sbi);
	}
	*is_inline = false;
	return f2fs_kzalloc(sbi, size, GFP_NOFS);
}

static void xattr_free(struct f2fs_sb_info *sbi, void *xattr_addr,
							bool is_inline)
{
	if (is_inline)
		kmem_cache_free(sbi->inline_xattr_slab, xattr_addr);
	else
		kfree(xattr_addr);
}

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
	case F2FS_XATTR_INDEX_SECURITY:
		break;
	default:
		return -EINVAL;
	}
	return f2fs_getxattr(inode, handler->flags, name,
			     buffer, size, NULL);
}

static int f2fs_xattr_generic_set(const struct xattr_handler *handler,
		struct mnt_idmap *idmap,
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
		struct mnt_idmap *idmap,
		struct dentry *unused, struct inode *inode,
		const char *name, const void *value,
		size_t size, int flags)
{
	unsigned char old_advise = F2FS_I(inode)->i_advise;
	unsigned char new_advise;

	if (!inode_owner_or_capable(&nop_mnt_idmap, inode))
		return -EPERM;
	if (value == NULL)
		return -EINVAL;

	new_advise = *(char *)value;
	if (new_advise & ~FADVISE_MODIFIABLE_BITS)
		return -EINVAL;

	new_advise = new_advise & FADVISE_MODIFIABLE_BITS;
	new_advise |= old_advise & ~FADVISE_MODIFIABLE_BITS;

	F2FS_I(inode)->i_advise = new_advise;
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
	.get	= f2fs_xattr_advise_get,
	.set	= f2fs_xattr_advise_set,
};

const struct xattr_handler f2fs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.flags	= F2FS_XATTR_INDEX_SECURITY,
	.get	= f2fs_xattr_generic_get,
	.set	= f2fs_xattr_generic_set,
};

static const struct xattr_handler * const f2fs_xattr_handler_map[] = {
	[F2FS_XATTR_INDEX_USER] = &f2fs_xattr_user_handler,
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	[F2FS_XATTR_INDEX_POSIX_ACL_ACCESS] = &nop_posix_acl_access,
	[F2FS_XATTR_INDEX_POSIX_ACL_DEFAULT] = &nop_posix_acl_default,
#endif
	[F2FS_XATTR_INDEX_TRUSTED] = &f2fs_xattr_trusted_handler,
#ifdef CONFIG_F2FS_FS_SECURITY
	[F2FS_XATTR_INDEX_SECURITY] = &f2fs_xattr_security_handler,
#endif
	[F2FS_XATTR_INDEX_ADVISE] = &f2fs_xattr_advise_handler,
};

const struct xattr_handler * const f2fs_xattr_handlers[] = {
	&f2fs_xattr_user_handler,
	&f2fs_xattr_trusted_handler,
#ifdef CONFIG_F2FS_FS_SECURITY
	&f2fs_xattr_security_handler,
#endif
	&f2fs_xattr_advise_handler,
	NULL,
};

static inline const char *f2fs_xattr_prefix(int index,
					    struct dentry *dentry)
{
	const struct xattr_handler *handler = NULL;

	if (index > 0 && index < ARRAY_SIZE(f2fs_xattr_handler_map))
		handler = f2fs_xattr_handler_map[index];

	if (!xattr_handler_can_list(handler, dentry))
		return NULL;

	return xattr_prefix(handler);
}

static struct f2fs_xattr_entry *__find_xattr(void *base_addr,
				void *last_base_addr, void **last_addr,
				int index, size_t len, const char *name)
{
	struct f2fs_xattr_entry *entry;

	list_for_each_xattr(entry, base_addr) {
		if ((void *)(entry) + sizeof(__u32) > last_base_addr ||
			(void *)XATTR_NEXT_ENTRY(entry) > last_base_addr) {
			if (last_addr)
				*last_addr = entry;
			return NULL;
		}

		if (entry->e_name_index != index)
			continue;
		if (entry->e_name_len != len)
			continue;
		if (!memcmp(entry->e_name, name, len))
			break;
	}
	return entry;
}

static struct f2fs_xattr_entry *__find_inline_xattr(struct inode *inode,
				void *base_addr, void **last_addr, int index,
				size_t len, const char *name)
{
	struct f2fs_xattr_entry *entry;
	unsigned int inline_size = inline_xattr_size(inode);
	void *max_addr = base_addr + inline_size;

	entry = __find_xattr(base_addr, max_addr, last_addr, index, len, name);
	if (!entry)
		return NULL;

	/* inline xattr header or entry across max inline xattr size */
	if (IS_XATTR_LAST_ENTRY(entry) &&
		(void *)entry + sizeof(__u32) > max_addr) {
		*last_addr = entry;
		return NULL;
	}
	return entry;
}

static int read_inline_xattr(struct inode *inode, struct page *ipage,
							void *txattr_addr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int inline_size = inline_xattr_size(inode);
	struct page *page = NULL;
	void *inline_addr;

	if (ipage) {
		inline_addr = inline_xattr_addr(inode, ipage);
	} else {
		page = f2fs_get_node_page(sbi, inode->i_ino);
		if (IS_ERR(page))
			return PTR_ERR(page);

		inline_addr = inline_xattr_addr(inode, page);
	}
	memcpy(txattr_addr, inline_addr, inline_size);
	f2fs_put_page(page, 1);

	return 0;
}

static int read_xattr_block(struct inode *inode, void *txattr_addr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t xnid = F2FS_I(inode)->i_xattr_nid;
	unsigned int inline_size = inline_xattr_size(inode);
	struct page *xpage;
	void *xattr_addr;

	/* The inode already has an extended attribute block. */
	xpage = f2fs_get_node_page(sbi, xnid);
	if (IS_ERR(xpage))
		return PTR_ERR(xpage);

	xattr_addr = page_address(xpage);
	memcpy(txattr_addr + inline_size, xattr_addr, VALID_XATTR_BLOCK_SIZE);
	f2fs_put_page(xpage, 1);

	return 0;
}

static int lookup_all_xattrs(struct inode *inode, struct page *ipage,
				unsigned int index, unsigned int len,
				const char *name, struct f2fs_xattr_entry **xe,
				void **base_addr, int *base_size,
				bool *is_inline)
{
	void *cur_addr, *txattr_addr, *last_txattr_addr;
	void *last_addr = NULL;
	nid_t xnid = F2FS_I(inode)->i_xattr_nid;
	unsigned int inline_size = inline_xattr_size(inode);
	int err;

	if (!xnid && !inline_size)
		return -ENODATA;

	*base_size = XATTR_SIZE(inode) + XATTR_PADDING_SIZE;
	txattr_addr = xattr_alloc(F2FS_I_SB(inode), *base_size, is_inline);
	if (!txattr_addr)
		return -ENOMEM;

	last_txattr_addr = (void *)txattr_addr + XATTR_SIZE(inode);

	/* read from inline xattr */
	if (inline_size) {
		err = read_inline_xattr(inode, ipage, txattr_addr);
		if (err)
			goto out;

		*xe = __find_inline_xattr(inode, txattr_addr, &last_addr,
						index, len, name);
		if (*xe) {
			*base_size = inline_size;
			goto check;
		}
	}

	/* read from xattr node block */
	if (xnid) {
		err = read_xattr_block(inode, txattr_addr);
		if (err)
			goto out;
	}

	if (last_addr)
		cur_addr = XATTR_HDR(last_addr) - 1;
	else
		cur_addr = txattr_addr;

	*xe = __find_xattr(cur_addr, last_txattr_addr, NULL, index, len, name);
	if (!*xe) {
		f2fs_err(F2FS_I_SB(inode), "inode (%lu) has corrupted xattr",
								inode->i_ino);
		set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_FSCK);
		err = -EFSCORRUPTED;
		f2fs_handle_error(F2FS_I_SB(inode),
					ERROR_CORRUPTED_XATTR);
		goto out;
	}
check:
	if (IS_XATTR_LAST_ENTRY(*xe)) {
		err = -ENODATA;
		goto out;
	}

	*base_addr = txattr_addr;
	return 0;
out:
	xattr_free(F2FS_I_SB(inode), txattr_addr, *is_inline);
	return err;
}

static int read_all_xattrs(struct inode *inode, struct page *ipage,
							void **base_addr)
{
	struct f2fs_xattr_header *header;
	nid_t xnid = F2FS_I(inode)->i_xattr_nid;
	unsigned int size = VALID_XATTR_BLOCK_SIZE;
	unsigned int inline_size = inline_xattr_size(inode);
	void *txattr_addr;
	int err;

	txattr_addr = f2fs_kzalloc(F2FS_I_SB(inode),
			inline_size + size + XATTR_PADDING_SIZE, GFP_NOFS);
	if (!txattr_addr)
		return -ENOMEM;

	/* read from inline xattr */
	if (inline_size) {
		err = read_inline_xattr(inode, ipage, txattr_addr);
		if (err)
			goto fail;
	}

	/* read from xattr node block */
	if (xnid) {
		err = read_xattr_block(inode, txattr_addr);
		if (err)
			goto fail;
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
	kfree(txattr_addr);
	return err;
}

static inline int write_all_xattrs(struct inode *inode, __u32 hsize,
				void *txattr_addr, struct page *ipage)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	size_t inline_size = inline_xattr_size(inode);
	struct page *in_page = NULL;
	void *xattr_addr;
	void *inline_addr = NULL;
	struct page *xpage;
	nid_t new_nid = 0;
	int err = 0;

	if (hsize > inline_size && !F2FS_I(inode)->i_xattr_nid)
		if (!f2fs_alloc_nid(sbi, &new_nid))
			return -ENOSPC;

	/* write to inline xattr */
	if (inline_size) {
		if (ipage) {
			inline_addr = inline_xattr_addr(inode, ipage);
		} else {
			in_page = f2fs_get_node_page(sbi, inode->i_ino);
			if (IS_ERR(in_page)) {
				f2fs_alloc_nid_failed(sbi, new_nid);
				return PTR_ERR(in_page);
			}
			inline_addr = inline_xattr_addr(inode, in_page);
		}

		f2fs_wait_on_page_writeback(ipage ? ipage : in_page,
							NODE, true, true);
		/* no need to use xattr node block */
		if (hsize <= inline_size) {
			err = f2fs_truncate_xattr_node(inode);
			f2fs_alloc_nid_failed(sbi, new_nid);
			if (err) {
				f2fs_put_page(in_page, 1);
				return err;
			}
			memcpy(inline_addr, txattr_addr, inline_size);
			set_page_dirty(ipage ? ipage : in_page);
			goto in_page_out;
		}
	}

	/* write to xattr node block */
	if (F2FS_I(inode)->i_xattr_nid) {
		xpage = f2fs_get_node_page(sbi, F2FS_I(inode)->i_xattr_nid);
		if (IS_ERR(xpage)) {
			err = PTR_ERR(xpage);
			f2fs_alloc_nid_failed(sbi, new_nid);
			goto in_page_out;
		}
		f2fs_bug_on(sbi, new_nid);
		f2fs_wait_on_page_writeback(xpage, NODE, true, true);
	} else {
		struct dnode_of_data dn;

		set_new_dnode(&dn, inode, NULL, NULL, new_nid);
		xpage = f2fs_new_node_page(&dn, XATTR_NODE_OFFSET);
		if (IS_ERR(xpage)) {
			err = PTR_ERR(xpage);
			f2fs_alloc_nid_failed(sbi, new_nid);
			goto in_page_out;
		}
		f2fs_alloc_nid_done(sbi, new_nid);
	}
	xattr_addr = page_address(xpage);

	if (inline_size)
		memcpy(inline_addr, txattr_addr, inline_size);
	memcpy(xattr_addr, txattr_addr + inline_size, VALID_XATTR_BLOCK_SIZE);

	if (inline_size)
		set_page_dirty(ipage ? ipage : in_page);
	set_page_dirty(xpage);

	f2fs_put_page(xpage, 1);
in_page_out:
	f2fs_put_page(in_page, 1);
	return err;
}

int f2fs_getxattr(struct inode *inode, int index, const char *name,
		void *buffer, size_t buffer_size, struct page *ipage)
{
	struct f2fs_xattr_entry *entry = NULL;
	int error;
	unsigned int size, len;
	void *base_addr = NULL;
	int base_size;
	bool is_inline;

	if (name == NULL)
		return -EINVAL;

	len = strlen(name);
	if (len > F2FS_NAME_LEN)
		return -ERANGE;

	if (!ipage)
		f2fs_down_read(&F2FS_I(inode)->i_xattr_sem);
	error = lookup_all_xattrs(inode, ipage, index, len, name,
				&entry, &base_addr, &base_size, &is_inline);
	if (!ipage)
		f2fs_up_read(&F2FS_I(inode)->i_xattr_sem);
	if (error)
		return error;

	size = le16_to_cpu(entry->e_value_size);

	if (buffer && size > buffer_size) {
		error = -ERANGE;
		goto out;
	}

	if (buffer) {
		char *pval = entry->e_name + entry->e_name_len;

		if (base_size - (pval - (char *)base_addr) < size) {
			error = -ERANGE;
			goto out;
		}
		memcpy(buffer, pval, size);
	}
	error = size;
out:
	xattr_free(F2FS_I_SB(inode), base_addr, is_inline);
	return error;
}

ssize_t f2fs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = d_inode(dentry);
	struct f2fs_xattr_entry *entry;
	void *base_addr, *last_base_addr;
	int error;
	size_t rest = buffer_size;

	f2fs_down_read(&F2FS_I(inode)->i_xattr_sem);
	error = read_all_xattrs(inode, NULL, &base_addr);
	f2fs_up_read(&F2FS_I(inode)->i_xattr_sem);
	if (error)
		return error;

	last_base_addr = (void *)base_addr + XATTR_SIZE(inode);

	list_for_each_xattr(entry, base_addr) {
		const char *prefix;
		size_t prefix_len;
		size_t size;

		prefix = f2fs_xattr_prefix(entry->e_name_index, dentry);

		if ((void *)(entry) + sizeof(__u32) > last_base_addr ||
			(void *)XATTR_NEXT_ENTRY(entry) > last_base_addr) {
			f2fs_err(F2FS_I_SB(inode), "inode (%lu) has corrupted xattr",
						inode->i_ino);
			set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_FSCK);
			error = -EFSCORRUPTED;
			f2fs_handle_error(F2FS_I_SB(inode),
						ERROR_CORRUPTED_XATTR);
			goto cleanup;
		}

		if (!prefix)
			continue;

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
	kfree(base_addr);
	return error;
}

static bool f2fs_xattr_value_same(struct f2fs_xattr_entry *entry,
					const void *value, size_t size)
{
	void *pval = entry->e_name + entry->e_name_len;

	return (le16_to_cpu(entry->e_value_size) == size) &&
					!memcmp(pval, value, size);
}

static int __f2fs_setxattr(struct inode *inode, int index,
			const char *name, const void *value, size_t size,
			struct page *ipage, int flags)
{
	struct f2fs_xattr_entry *here, *last;
	void *base_addr, *last_base_addr;
	int found, newsize;
	size_t len;
	__u32 new_hsize;
	int error;

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

	last_base_addr = (void *)base_addr + XATTR_SIZE(inode);

	/* find entry with wanted name. */
	here = __find_xattr(base_addr, last_base_addr, NULL, index, len, name);
	if (!here) {
		f2fs_err(F2FS_I_SB(inode), "inode (%lu) has corrupted xattr",
								inode->i_ino);
		set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_FSCK);
		error = -EFSCORRUPTED;
		f2fs_handle_error(F2FS_I_SB(inode),
					ERROR_CORRUPTED_XATTR);
		goto exit;
	}

	found = IS_XATTR_LAST_ENTRY(here) ? 0 : 1;

	if (found) {
		if ((flags & XATTR_CREATE)) {
			error = -EEXIST;
			goto exit;
		}

		if (value && f2fs_xattr_value_same(here, value, size))
			goto same;
	} else if ((flags & XATTR_REPLACE)) {
		error = -ENODATA;
		goto exit;
	}

	last = here;
	while (!IS_XATTR_LAST_ENTRY(last)) {
		if ((void *)(last) + sizeof(__u32) > last_base_addr ||
			(void *)XATTR_NEXT_ENTRY(last) > last_base_addr) {
			f2fs_err(F2FS_I_SB(inode), "inode (%lu) has invalid last xattr entry, entry_size: %zu",
					inode->i_ino, ENTRY_SIZE(last));
			set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_FSCK);
			error = -EFSCORRUPTED;
			f2fs_handle_error(F2FS_I_SB(inode),
						ERROR_CORRUPTED_XATTR);
			goto exit;
		}
		last = XATTR_NEXT_ENTRY(last);
	}

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

	if (index == F2FS_XATTR_INDEX_ENCRYPTION &&
			!strcmp(name, F2FS_XATTR_NAME_ENCRYPTION_CONTEXT))
		f2fs_set_encrypted_inode(inode);
	if (S_ISDIR(inode->i_mode))
		set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_CP);

same:
	if (is_inode_flag_set(inode, FI_ACL_MODE)) {
		inode->i_mode = F2FS_I(inode)->i_acl_mode;
		clear_inode_flag(inode, FI_ACL_MODE);
	}

	inode_set_ctime_current(inode);
	f2fs_mark_inode_dirty_sync(inode, true);
exit:
	kfree(base_addr);
	return error;
}

int f2fs_setxattr(struct inode *inode, int index, const char *name,
				const void *value, size_t size,
				struct page *ipage, int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int err;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	err = f2fs_dquot_initialize(inode);
	if (err)
		return err;

	/* this case is only from f2fs_init_inode_metadata */
	if (ipage)
		return __f2fs_setxattr(inode, index, name, value,
						size, ipage, flags);
	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	f2fs_down_write(&F2FS_I(inode)->i_xattr_sem);
	err = __f2fs_setxattr(inode, index, name, value, size, ipage, flags);
	f2fs_up_write(&F2FS_I(inode)->i_xattr_sem);
	f2fs_unlock_op(sbi);

	f2fs_update_time(sbi, REQ_TIME);
	return err;
}

int f2fs_init_xattr_caches(struct f2fs_sb_info *sbi)
{
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	char slab_name[32];

	sprintf(slab_name, "f2fs_xattr_entry-%u:%u", MAJOR(dev), MINOR(dev));

	sbi->inline_xattr_slab_size = F2FS_OPTION(sbi).inline_xattr_size *
					sizeof(__le32) + XATTR_PADDING_SIZE;

	sbi->inline_xattr_slab = f2fs_kmem_cache_create(slab_name,
					sbi->inline_xattr_slab_size);
	if (!sbi->inline_xattr_slab)
		return -ENOMEM;

	return 0;
}

void f2fs_destroy_xattr_caches(struct f2fs_sb_info *sbi)
{
	kmem_cache_destroy(sbi->inline_xattr_slab);
}
