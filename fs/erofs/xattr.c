// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021-2022, Alibaba Cloud
 */
#include <linux/security.h>
#include "xattr.h"

static inline erofs_blk_t erofs_xattr_blkaddr(struct super_block *sb,
					      unsigned int xattr_id)
{
	return EROFS_SB(sb)->xattr_blkaddr +
	       erofs_blknr(sb, xattr_id * sizeof(__u32));
}

static inline unsigned int erofs_xattr_blkoff(struct super_block *sb,
					      unsigned int xattr_id)
{
	return erofs_blkoff(sb, xattr_id * sizeof(__u32));
}

struct xattr_iter {
	struct super_block *sb;
	struct erofs_buf buf;
	void *kaddr;

	erofs_blk_t blkaddr;
	unsigned int ofs;
};

static int erofs_init_inode_xattrs(struct inode *inode)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct xattr_iter it;
	unsigned int i;
	struct erofs_xattr_ibody_header *ih;
	struct super_block *sb = inode->i_sb;
	int ret = 0;

	/* the most case is that xattrs of this inode are initialized. */
	if (test_bit(EROFS_I_EA_INITED_BIT, &vi->flags)) {
		/*
		 * paired with smp_mb() at the end of the function to ensure
		 * fields will only be observed after the bit is set.
		 */
		smp_mb();
		return 0;
	}

	if (wait_on_bit_lock(&vi->flags, EROFS_I_BL_XATTR_BIT, TASK_KILLABLE))
		return -ERESTARTSYS;

	/* someone has initialized xattrs for us? */
	if (test_bit(EROFS_I_EA_INITED_BIT, &vi->flags))
		goto out_unlock;

	/*
	 * bypass all xattr operations if ->xattr_isize is not greater than
	 * sizeof(struct erofs_xattr_ibody_header), in detail:
	 * 1) it is not enough to contain erofs_xattr_ibody_header then
	 *    ->xattr_isize should be 0 (it means no xattr);
	 * 2) it is just to contain erofs_xattr_ibody_header, which is on-disk
	 *    undefined right now (maybe use later with some new sb feature).
	 */
	if (vi->xattr_isize == sizeof(struct erofs_xattr_ibody_header)) {
		erofs_err(sb,
			  "xattr_isize %d of nid %llu is not supported yet",
			  vi->xattr_isize, vi->nid);
		ret = -EOPNOTSUPP;
		goto out_unlock;
	} else if (vi->xattr_isize < sizeof(struct erofs_xattr_ibody_header)) {
		if (vi->xattr_isize) {
			erofs_err(sb, "bogus xattr ibody @ nid %llu", vi->nid);
			DBG_BUGON(1);
			ret = -EFSCORRUPTED;
			goto out_unlock;	/* xattr ondisk layout error */
		}
		ret = -ENOATTR;
		goto out_unlock;
	}

	it.buf = __EROFS_BUF_INITIALIZER;
	it.blkaddr = erofs_blknr(sb, erofs_iloc(inode) + vi->inode_isize);
	it.ofs = erofs_blkoff(sb, erofs_iloc(inode) + vi->inode_isize);

	/* read in shared xattr array (non-atomic, see kmalloc below) */
	it.kaddr = erofs_read_metabuf(&it.buf, sb, it.blkaddr, EROFS_KMAP);
	if (IS_ERR(it.kaddr)) {
		ret = PTR_ERR(it.kaddr);
		goto out_unlock;
	}

	ih = (struct erofs_xattr_ibody_header *)(it.kaddr + it.ofs);
	vi->xattr_shared_count = ih->h_shared_count;
	vi->xattr_shared_xattrs = kmalloc_array(vi->xattr_shared_count,
						sizeof(uint), GFP_KERNEL);
	if (!vi->xattr_shared_xattrs) {
		erofs_put_metabuf(&it.buf);
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* let's skip ibody header */
	it.ofs += sizeof(struct erofs_xattr_ibody_header);

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		if (it.ofs >= sb->s_blocksize) {
			/* cannot be unaligned */
			DBG_BUGON(it.ofs != sb->s_blocksize);

			it.kaddr = erofs_read_metabuf(&it.buf, sb, ++it.blkaddr,
						      EROFS_KMAP);
			if (IS_ERR(it.kaddr)) {
				kfree(vi->xattr_shared_xattrs);
				vi->xattr_shared_xattrs = NULL;
				ret = PTR_ERR(it.kaddr);
				goto out_unlock;
			}
			it.ofs = 0;
		}
		vi->xattr_shared_xattrs[i] =
			le32_to_cpu(*(__le32 *)(it.kaddr + it.ofs));
		it.ofs += sizeof(__le32);
	}
	erofs_put_metabuf(&it.buf);

	/* paired with smp_mb() at the beginning of the function. */
	smp_mb();
	set_bit(EROFS_I_EA_INITED_BIT, &vi->flags);

out_unlock:
	clear_and_wake_up_bit(EROFS_I_BL_XATTR_BIT, &vi->flags);
	return ret;
}

/*
 * the general idea for these return values is
 * if    0 is returned, go on processing the current xattr;
 *       1 (> 0) is returned, skip this round to process the next xattr;
 *    -err (< 0) is returned, an error (maybe ENOXATTR) occurred
 *                            and need to be handled
 */
struct xattr_iter_handlers {
	int (*entry)(struct xattr_iter *_it, struct erofs_xattr_entry *entry);
	int (*name)(struct xattr_iter *_it, unsigned int processed, char *buf,
		    unsigned int len);
	int (*alloc_buffer)(struct xattr_iter *_it, unsigned int value_sz);
	void (*value)(struct xattr_iter *_it, unsigned int processed, char *buf,
		      unsigned int len);
};

static inline int xattr_iter_fixup(struct xattr_iter *it)
{
	if (it->ofs < it->sb->s_blocksize)
		return 0;

	it->blkaddr += erofs_blknr(it->sb, it->ofs);
	it->kaddr = erofs_read_metabuf(&it->buf, it->sb, it->blkaddr,
				       EROFS_KMAP);
	if (IS_ERR(it->kaddr))
		return PTR_ERR(it->kaddr);
	it->ofs = erofs_blkoff(it->sb, it->ofs);
	return 0;
}

static int inline_xattr_iter_begin(struct xattr_iter *it,
				   struct inode *inode)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	unsigned int xattr_header_sz, inline_xattr_ofs;

	xattr_header_sz = sizeof(struct erofs_xattr_ibody_header) +
			  sizeof(u32) * vi->xattr_shared_count;
	if (xattr_header_sz >= vi->xattr_isize) {
		DBG_BUGON(xattr_header_sz > vi->xattr_isize);
		return -ENOATTR;
	}

	inline_xattr_ofs = vi->inode_isize + xattr_header_sz;

	it->blkaddr = erofs_blknr(it->sb, erofs_iloc(inode) + inline_xattr_ofs);
	it->ofs = erofs_blkoff(it->sb, erofs_iloc(inode) + inline_xattr_ofs);
	it->kaddr = erofs_read_metabuf(&it->buf, inode->i_sb, it->blkaddr,
				       EROFS_KMAP);
	if (IS_ERR(it->kaddr))
		return PTR_ERR(it->kaddr);
	return vi->xattr_isize - xattr_header_sz;
}

/*
 * Regardless of success or failure, `xattr_foreach' will end up with
 * `ofs' pointing to the next xattr item rather than an arbitrary position.
 */
static int xattr_foreach(struct xattr_iter *it,
			 const struct xattr_iter_handlers *op,
			 unsigned int *tlimit)
{
	struct erofs_xattr_entry entry;
	unsigned int value_sz, processed, slice;
	int err;

	/* 0. fixup blkaddr, ofs, ipage */
	err = xattr_iter_fixup(it);
	if (err)
		return err;

	/*
	 * 1. read xattr entry to the memory,
	 *    since we do EROFS_XATTR_ALIGN
	 *    therefore entry should be in the page
	 */
	entry = *(struct erofs_xattr_entry *)(it->kaddr + it->ofs);
	if (tlimit) {
		unsigned int entry_sz = erofs_xattr_entry_size(&entry);

		/* xattr on-disk corruption: xattr entry beyond xattr_isize */
		if (*tlimit < entry_sz) {
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		*tlimit -= entry_sz;
	}

	it->ofs += sizeof(struct erofs_xattr_entry);
	value_sz = le16_to_cpu(entry.e_value_size);

	/* handle entry */
	err = op->entry(it, &entry);
	if (err) {
		it->ofs += entry.e_name_len + value_sz;
		goto out;
	}

	/* 2. handle xattr name (ofs will finally be at the end of name) */
	processed = 0;

	while (processed < entry.e_name_len) {
		if (it->ofs >= it->sb->s_blocksize) {
			DBG_BUGON(it->ofs > it->sb->s_blocksize);

			err = xattr_iter_fixup(it);
			if (err)
				goto out;
			it->ofs = 0;
		}

		slice = min_t(unsigned int, it->sb->s_blocksize - it->ofs,
			      entry.e_name_len - processed);

		/* handle name */
		err = op->name(it, processed, it->kaddr + it->ofs, slice);
		if (err) {
			it->ofs += entry.e_name_len - processed + value_sz;
			goto out;
		}

		it->ofs += slice;
		processed += slice;
	}

	/* 3. handle xattr value */
	processed = 0;

	if (op->alloc_buffer) {
		err = op->alloc_buffer(it, value_sz);
		if (err) {
			it->ofs += value_sz;
			goto out;
		}
	}

	while (processed < value_sz) {
		if (it->ofs >= it->sb->s_blocksize) {
			DBG_BUGON(it->ofs > it->sb->s_blocksize);

			err = xattr_iter_fixup(it);
			if (err)
				goto out;
			it->ofs = 0;
		}

		slice = min_t(unsigned int, it->sb->s_blocksize - it->ofs,
			      value_sz - processed);
		op->value(it, processed, it->kaddr + it->ofs, slice);
		it->ofs += slice;
		processed += slice;
	}

out:
	/* xattrs should be 4-byte aligned (on-disk constraint) */
	it->ofs = EROFS_XATTR_ALIGN(it->ofs);
	return err < 0 ? err : 0;
}

struct getxattr_iter {
	struct xattr_iter it;

	char *buffer;
	int buffer_size, index, infix_len;
	struct qstr name;
};

static int erofs_xattr_long_entrymatch(struct getxattr_iter *it,
				       struct erofs_xattr_entry *entry)
{
	struct erofs_sb_info *sbi = EROFS_SB(it->it.sb);
	struct erofs_xattr_prefix_item *pf = sbi->xattr_prefixes +
		(entry->e_name_index & EROFS_XATTR_LONG_PREFIX_MASK);

	if (pf >= sbi->xattr_prefixes + sbi->xattr_prefix_count)
		return -ENOATTR;

	if (it->index != pf->prefix->base_index ||
	    it->name.len != entry->e_name_len + pf->infix_len)
		return -ENOATTR;

	if (memcmp(it->name.name, pf->prefix->infix, pf->infix_len))
		return -ENOATTR;

	it->infix_len = pf->infix_len;
	return 0;
}

static int xattr_entrymatch(struct xattr_iter *_it,
			    struct erofs_xattr_entry *entry)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);

	/* should also match the infix for long name prefixes */
	if (entry->e_name_index & EROFS_XATTR_LONG_PREFIX)
		return erofs_xattr_long_entrymatch(it, entry);

	if (it->index != entry->e_name_index ||
	    it->name.len != entry->e_name_len)
		return -ENOATTR;
	it->infix_len = 0;
	return 0;
}

static int xattr_namematch(struct xattr_iter *_it,
			   unsigned int processed, char *buf, unsigned int len)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);

	if (memcmp(buf, it->name.name + it->infix_len + processed, len))
		return -ENOATTR;
	return 0;
}

static int xattr_checkbuffer(struct xattr_iter *_it,
			     unsigned int value_sz)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);
	int err = it->buffer_size < value_sz ? -ERANGE : 0;

	it->buffer_size = value_sz;
	return !it->buffer ? 1 : err;
}

static void xattr_copyvalue(struct xattr_iter *_it,
			    unsigned int processed,
			    char *buf, unsigned int len)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);

	memcpy(it->buffer + processed, buf, len);
}

static const struct xattr_iter_handlers find_xattr_handlers = {
	.entry = xattr_entrymatch,
	.name = xattr_namematch,
	.alloc_buffer = xattr_checkbuffer,
	.value = xattr_copyvalue
};

static int inline_getxattr(struct inode *inode, struct getxattr_iter *it)
{
	int ret;
	unsigned int remaining;

	ret = inline_xattr_iter_begin(&it->it, inode);
	if (ret < 0)
		return ret;

	remaining = ret;
	while (remaining) {
		ret = xattr_foreach(&it->it, &find_xattr_handlers, &remaining);
		if (ret != -ENOATTR)
			break;
	}
	return ret ? ret : it->buffer_size;
}

static int shared_getxattr(struct inode *inode, struct getxattr_iter *it)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct super_block *const sb = it->it.sb;
	unsigned int i, xsid;
	int ret = -ENOATTR;

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		xsid = vi->xattr_shared_xattrs[i];
		it->it.blkaddr = erofs_xattr_blkaddr(sb, xsid);
		it->it.ofs = erofs_xattr_blkoff(sb, xsid);
		it->it.kaddr = erofs_read_metabuf(&it->it.buf, sb,
						  it->it.blkaddr, EROFS_KMAP);
		if (IS_ERR(it->it.kaddr))
			return PTR_ERR(it->it.kaddr);

		ret = xattr_foreach(&it->it, &find_xattr_handlers, NULL);
		if (ret != -ENOATTR)
			break;
	}
	return ret ? ret : it->buffer_size;
}

static bool erofs_xattr_user_list(struct dentry *dentry)
{
	return test_opt(&EROFS_SB(dentry->d_sb)->opt, XATTR_USER);
}

static bool erofs_xattr_trusted_list(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
}

int erofs_getxattr(struct inode *inode, int index,
		   const char *name,
		   void *buffer, size_t buffer_size)
{
	int ret;
	struct getxattr_iter it;

	if (!name)
		return -EINVAL;

	ret = erofs_init_inode_xattrs(inode);
	if (ret)
		return ret;

	it.index = index;
	it.name.len = strlen(name);
	if (it.name.len > EROFS_NAME_LEN)
		return -ERANGE;

	it.it.buf = __EROFS_BUF_INITIALIZER;
	it.name.name = name;

	it.buffer = buffer;
	it.buffer_size = buffer_size;

	it.it.sb = inode->i_sb;
	ret = inline_getxattr(inode, &it);
	if (ret == -ENOATTR)
		ret = shared_getxattr(inode, &it);
	erofs_put_metabuf(&it.it.buf);
	return ret;
}

static int erofs_xattr_generic_get(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *name, void *buffer, size_t size)
{
	if (handler->flags == EROFS_XATTR_INDEX_USER &&
	    !test_opt(&EROFS_I_SB(inode)->opt, XATTR_USER))
		return -EOPNOTSUPP;

	return erofs_getxattr(inode, handler->flags, name, buffer, size);
}

const struct xattr_handler erofs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= EROFS_XATTR_INDEX_USER,
	.list	= erofs_xattr_user_list,
	.get	= erofs_xattr_generic_get,
};

const struct xattr_handler erofs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= EROFS_XATTR_INDEX_TRUSTED,
	.list	= erofs_xattr_trusted_list,
	.get	= erofs_xattr_generic_get,
};

#ifdef CONFIG_EROFS_FS_SECURITY
const struct xattr_handler __maybe_unused erofs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.flags	= EROFS_XATTR_INDEX_SECURITY,
	.get	= erofs_xattr_generic_get,
};
#endif

const struct xattr_handler *erofs_xattr_handlers[] = {
	&erofs_xattr_user_handler,
	&erofs_xattr_trusted_handler,
#ifdef CONFIG_EROFS_FS_SECURITY
	&erofs_xattr_security_handler,
#endif
	NULL,
};

struct listxattr_iter {
	struct xattr_iter it;

	struct dentry *dentry;
	char *buffer;
	int buffer_size, buffer_ofs;
};

static int xattr_entrylist(struct xattr_iter *_it,
			   struct erofs_xattr_entry *entry)
{
	struct listxattr_iter *it =
		container_of(_it, struct listxattr_iter, it);
	unsigned int base_index = entry->e_name_index;
	unsigned int prefix_len, infix_len = 0;
	const char *prefix, *infix = NULL;

	if (entry->e_name_index & EROFS_XATTR_LONG_PREFIX) {
		struct erofs_sb_info *sbi = EROFS_SB(_it->sb);
		struct erofs_xattr_prefix_item *pf = sbi->xattr_prefixes +
			(entry->e_name_index & EROFS_XATTR_LONG_PREFIX_MASK);

		if (pf >= sbi->xattr_prefixes + sbi->xattr_prefix_count)
			return 1;
		infix = pf->prefix->infix;
		infix_len = pf->infix_len;
		base_index = pf->prefix->base_index;
	}

	prefix = erofs_xattr_prefix(base_index, it->dentry);
	if (!prefix)
		return 1;
	prefix_len = strlen(prefix);

	if (!it->buffer) {
		it->buffer_ofs += prefix_len + infix_len +
					entry->e_name_len + 1;
		return 1;
	}

	if (it->buffer_ofs + prefix_len + infix_len +
		+ entry->e_name_len + 1 > it->buffer_size)
		return -ERANGE;

	memcpy(it->buffer + it->buffer_ofs, prefix, prefix_len);
	memcpy(it->buffer + it->buffer_ofs + prefix_len, infix, infix_len);
	it->buffer_ofs += prefix_len + infix_len;
	return 0;
}

static int xattr_namelist(struct xattr_iter *_it,
			  unsigned int processed, char *buf, unsigned int len)
{
	struct listxattr_iter *it =
		container_of(_it, struct listxattr_iter, it);

	memcpy(it->buffer + it->buffer_ofs, buf, len);
	it->buffer_ofs += len;
	return 0;
}

static int xattr_skipvalue(struct xattr_iter *_it,
			   unsigned int value_sz)
{
	struct listxattr_iter *it =
		container_of(_it, struct listxattr_iter, it);

	it->buffer[it->buffer_ofs++] = '\0';
	return 1;
}

static const struct xattr_iter_handlers list_xattr_handlers = {
	.entry = xattr_entrylist,
	.name = xattr_namelist,
	.alloc_buffer = xattr_skipvalue,
	.value = NULL
};

static int inline_listxattr(struct listxattr_iter *it)
{
	int ret;
	unsigned int remaining;

	ret = inline_xattr_iter_begin(&it->it, d_inode(it->dentry));
	if (ret < 0)
		return ret;

	remaining = ret;
	while (remaining) {
		ret = xattr_foreach(&it->it, &list_xattr_handlers, &remaining);
		if (ret)
			break;
	}
	return ret ? ret : it->buffer_ofs;
}

static int shared_listxattr(struct listxattr_iter *it)
{
	struct inode *const inode = d_inode(it->dentry);
	struct erofs_inode *const vi = EROFS_I(inode);
	struct super_block *const sb = it->it.sb;
	unsigned int i, xsid;
	int ret = 0;

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		xsid = vi->xattr_shared_xattrs[i];
		it->it.blkaddr = erofs_xattr_blkaddr(sb, xsid);
		it->it.ofs = erofs_xattr_blkoff(sb, xsid);
		it->it.kaddr = erofs_read_metabuf(&it->it.buf, sb,
						  it->it.blkaddr, EROFS_KMAP);
		if (IS_ERR(it->it.kaddr))
			return PTR_ERR(it->it.kaddr);

		ret = xattr_foreach(&it->it, &list_xattr_handlers, NULL);
		if (ret)
			break;
	}
	return ret ? ret : it->buffer_ofs;
}

ssize_t erofs_listxattr(struct dentry *dentry,
			char *buffer, size_t buffer_size)
{
	int ret;
	struct listxattr_iter it;

	ret = erofs_init_inode_xattrs(d_inode(dentry));
	if (ret == -ENOATTR)
		return 0;
	if (ret)
		return ret;

	it.it.buf = __EROFS_BUF_INITIALIZER;
	it.dentry = dentry;
	it.buffer = buffer;
	it.buffer_size = buffer_size;
	it.buffer_ofs = 0;

	it.it.sb = dentry->d_sb;

	ret = inline_listxattr(&it);
	if (ret >= 0 || ret == -ENOATTR)
		ret = shared_listxattr(&it);
	erofs_put_metabuf(&it.it.buf);
	return ret;
}

void erofs_xattr_prefixes_cleanup(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	int i;

	if (sbi->xattr_prefixes) {
		for (i = 0; i < sbi->xattr_prefix_count; i++)
			kfree(sbi->xattr_prefixes[i].prefix);
		kfree(sbi->xattr_prefixes);
		sbi->xattr_prefixes = NULL;
	}
}

int erofs_xattr_prefixes_init(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	erofs_off_t pos = (erofs_off_t)sbi->xattr_prefix_start << 2;
	struct erofs_xattr_prefix_item *pfs;
	int ret = 0, i, len;

	if (!sbi->xattr_prefix_count)
		return 0;

	pfs = kzalloc(sbi->xattr_prefix_count * sizeof(*pfs), GFP_KERNEL);
	if (!pfs)
		return -ENOMEM;

	if (erofs_sb_has_fragments(sbi))
		buf.inode = sbi->packed_inode;
	else
		erofs_init_metabuf(&buf, sb);

	for (i = 0; i < sbi->xattr_prefix_count; i++) {
		void *ptr = erofs_read_metadata(sb, &buf, &pos, &len);

		if (IS_ERR(ptr)) {
			ret = PTR_ERR(ptr);
			break;
		} else if (len < sizeof(*pfs->prefix) ||
			   len > EROFS_NAME_LEN + sizeof(*pfs->prefix)) {
			kfree(ptr);
			ret = -EFSCORRUPTED;
			break;
		}
		pfs[i].prefix = ptr;
		pfs[i].infix_len = len - sizeof(struct erofs_xattr_long_prefix);
	}

	erofs_put_metabuf(&buf);
	sbi->xattr_prefixes = pfs;
	if (ret)
		erofs_xattr_prefixes_cleanup(sb);
	return ret;
}

#ifdef CONFIG_EROFS_FS_POSIX_ACL
struct posix_acl *erofs_get_acl(struct inode *inode, int type, bool rcu)
{
	struct posix_acl *acl;
	int prefix, rc;
	char *value = NULL;

	if (rcu)
		return ERR_PTR(-ECHILD);

	switch (type) {
	case ACL_TYPE_ACCESS:
		prefix = EROFS_XATTR_INDEX_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		prefix = EROFS_XATTR_INDEX_POSIX_ACL_DEFAULT;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	rc = erofs_getxattr(inode, prefix, "", NULL, 0);
	if (rc > 0) {
		value = kmalloc(rc, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		rc = erofs_getxattr(inode, prefix, "", value, rc);
	}

	if (rc == -ENOATTR)
		acl = NULL;
	else if (rc < 0)
		acl = ERR_PTR(rc);
	else
		acl = posix_acl_from_xattr(&init_user_ns, value, rc);
	kfree(value);
	return acl;
}
#endif
