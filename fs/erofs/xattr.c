// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021-2022, Alibaba Cloud
 */
#include <linux/security.h>
#include <linux/xxhash.h>
#include "xattr.h"

struct erofs_xattr_iter {
	struct super_block *sb;
	struct erofs_buf buf;
	erofs_off_t pos;
	void *kaddr;

	char *buffer;
	int buffer_size, buffer_ofs;

	/* getxattr */
	int index, infix_len;
	struct qstr name;

	/* listxattr */
	struct dentry *dentry;
};

static int erofs_init_inode_xattrs(struct inode *inode)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct erofs_xattr_iter it;
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
	erofs_init_metabuf(&it.buf, sb);
	it.pos = erofs_iloc(inode) + vi->inode_isize;

	/* read in shared xattr array (non-atomic, see kmalloc below) */
	it.kaddr = erofs_bread(&it.buf, erofs_blknr(sb, it.pos), EROFS_KMAP);
	if (IS_ERR(it.kaddr)) {
		ret = PTR_ERR(it.kaddr);
		goto out_unlock;
	}

	ih = it.kaddr + erofs_blkoff(sb, it.pos);
	vi->xattr_name_filter = le32_to_cpu(ih->h_name_filter);
	vi->xattr_shared_count = ih->h_shared_count;
	vi->xattr_shared_xattrs = kmalloc_array(vi->xattr_shared_count,
						sizeof(uint), GFP_KERNEL);
	if (!vi->xattr_shared_xattrs) {
		erofs_put_metabuf(&it.buf);
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* let's skip ibody header */
	it.pos += sizeof(struct erofs_xattr_ibody_header);

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		it.kaddr = erofs_bread(&it.buf, erofs_blknr(sb, it.pos),
				       EROFS_KMAP);
		if (IS_ERR(it.kaddr)) {
			kfree(vi->xattr_shared_xattrs);
			vi->xattr_shared_xattrs = NULL;
			ret = PTR_ERR(it.kaddr);
			goto out_unlock;
		}
		vi->xattr_shared_xattrs[i] = le32_to_cpu(*(__le32 *)
				(it.kaddr + erofs_blkoff(sb, it.pos)));
		it.pos += sizeof(__le32);
	}
	erofs_put_metabuf(&it.buf);

	/* paired with smp_mb() at the beginning of the function. */
	smp_mb();
	set_bit(EROFS_I_EA_INITED_BIT, &vi->flags);

out_unlock:
	clear_and_wake_up_bit(EROFS_I_BL_XATTR_BIT, &vi->flags);
	return ret;
}

static bool erofs_xattr_user_list(struct dentry *dentry)
{
	return test_opt(&EROFS_SB(dentry->d_sb)->opt, XATTR_USER);
}

static bool erofs_xattr_trusted_list(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
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

static int erofs_xattr_copy_to_buffer(struct erofs_xattr_iter *it,
				      unsigned int len)
{
	unsigned int slice, processed;
	struct super_block *sb = it->sb;
	void *src;

	for (processed = 0; processed < len; processed += slice) {
		it->kaddr = erofs_bread(&it->buf, erofs_blknr(sb, it->pos),
					EROFS_KMAP);
		if (IS_ERR(it->kaddr))
			return PTR_ERR(it->kaddr);

		src = it->kaddr + erofs_blkoff(sb, it->pos);
		slice = min_t(unsigned int, sb->s_blocksize -
				erofs_blkoff(sb, it->pos), len - processed);
		memcpy(it->buffer + it->buffer_ofs, src, slice);
		it->buffer_ofs += slice;
		it->pos += slice;
	}
	return 0;
}

static int erofs_listxattr_foreach(struct erofs_xattr_iter *it)
{
	struct erofs_xattr_entry entry;
	unsigned int base_index, name_total, prefix_len, infix_len = 0;
	const char *prefix, *infix = NULL;
	int err;

	/* 1. handle xattr entry */
	entry = *(struct erofs_xattr_entry *)
			(it->kaddr + erofs_blkoff(it->sb, it->pos));
	it->pos += sizeof(struct erofs_xattr_entry);

	base_index = entry.e_name_index;
	if (entry.e_name_index & EROFS_XATTR_LONG_PREFIX) {
		struct erofs_sb_info *sbi = EROFS_SB(it->sb);
		struct erofs_xattr_prefix_item *pf = sbi->xattr_prefixes +
			(entry.e_name_index & EROFS_XATTR_LONG_PREFIX_MASK);

		if (pf >= sbi->xattr_prefixes + sbi->xattr_prefix_count)
			return 0;
		infix = pf->prefix->infix;
		infix_len = pf->infix_len;
		base_index = pf->prefix->base_index;
	}

	prefix = erofs_xattr_prefix(base_index, it->dentry);
	if (!prefix)
		return 0;
	prefix_len = strlen(prefix);
	name_total = prefix_len + infix_len + entry.e_name_len + 1;

	if (!it->buffer) {
		it->buffer_ofs += name_total;
		return 0;
	}

	if (it->buffer_ofs + name_total > it->buffer_size)
		return -ERANGE;

	memcpy(it->buffer + it->buffer_ofs, prefix, prefix_len);
	memcpy(it->buffer + it->buffer_ofs + prefix_len, infix, infix_len);
	it->buffer_ofs += prefix_len + infix_len;

	/* 2. handle xattr name */
	err = erofs_xattr_copy_to_buffer(it, entry.e_name_len);
	if (err)
		return err;

	it->buffer[it->buffer_ofs++] = '\0';
	return 0;
}

static int erofs_getxattr_foreach(struct erofs_xattr_iter *it)
{
	struct super_block *sb = it->sb;
	struct erofs_xattr_entry entry;
	unsigned int slice, processed, value_sz;

	/* 1. handle xattr entry */
	entry = *(struct erofs_xattr_entry *)
			(it->kaddr + erofs_blkoff(sb, it->pos));
	it->pos += sizeof(struct erofs_xattr_entry);
	value_sz = le16_to_cpu(entry.e_value_size);

	/* should also match the infix for long name prefixes */
	if (entry.e_name_index & EROFS_XATTR_LONG_PREFIX) {
		struct erofs_sb_info *sbi = EROFS_SB(sb);
		struct erofs_xattr_prefix_item *pf = sbi->xattr_prefixes +
			(entry.e_name_index & EROFS_XATTR_LONG_PREFIX_MASK);

		if (pf >= sbi->xattr_prefixes + sbi->xattr_prefix_count)
			return -ENOATTR;

		if (it->index != pf->prefix->base_index ||
		    it->name.len != entry.e_name_len + pf->infix_len)
			return -ENOATTR;

		if (memcmp(it->name.name, pf->prefix->infix, pf->infix_len))
			return -ENOATTR;

		it->infix_len = pf->infix_len;
	} else {
		if (it->index != entry.e_name_index ||
		    it->name.len != entry.e_name_len)
			return -ENOATTR;

		it->infix_len = 0;
	}

	/* 2. handle xattr name */
	for (processed = 0; processed < entry.e_name_len; processed += slice) {
		it->kaddr = erofs_bread(&it->buf, erofs_blknr(sb, it->pos),
					EROFS_KMAP);
		if (IS_ERR(it->kaddr))
			return PTR_ERR(it->kaddr);

		slice = min_t(unsigned int,
				sb->s_blocksize - erofs_blkoff(sb, it->pos),
				entry.e_name_len - processed);
		if (memcmp(it->name.name + it->infix_len + processed,
			   it->kaddr + erofs_blkoff(sb, it->pos), slice))
			return -ENOATTR;
		it->pos += slice;
	}

	/* 3. handle xattr value */
	if (!it->buffer) {
		it->buffer_ofs = value_sz;
		return 0;
	}

	if (it->buffer_size < value_sz)
		return -ERANGE;

	return erofs_xattr_copy_to_buffer(it, value_sz);
}

static int erofs_xattr_iter_inline(struct erofs_xattr_iter *it,
				   struct inode *inode, bool getxattr)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	unsigned int xattr_header_sz, remaining, entry_sz;
	erofs_off_t next_pos;
	int ret;

	xattr_header_sz = sizeof(struct erofs_xattr_ibody_header) +
			  sizeof(u32) * vi->xattr_shared_count;
	if (xattr_header_sz >= vi->xattr_isize) {
		DBG_BUGON(xattr_header_sz > vi->xattr_isize);
		return -ENOATTR;
	}

	remaining = vi->xattr_isize - xattr_header_sz;
	it->pos = erofs_iloc(inode) + vi->inode_isize + xattr_header_sz;

	while (remaining) {
		it->kaddr = erofs_bread(&it->buf, erofs_blknr(it->sb, it->pos),
					EROFS_KMAP);
		if (IS_ERR(it->kaddr))
			return PTR_ERR(it->kaddr);

		entry_sz = erofs_xattr_entry_size(it->kaddr +
				erofs_blkoff(it->sb, it->pos));
		/* xattr on-disk corruption: xattr entry beyond xattr_isize */
		if (remaining < entry_sz) {
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
		remaining -= entry_sz;
		next_pos = it->pos + entry_sz;

		if (getxattr)
			ret = erofs_getxattr_foreach(it);
		else
			ret = erofs_listxattr_foreach(it);
		if ((getxattr && ret != -ENOATTR) || (!getxattr && ret))
			break;

		it->pos = next_pos;
	}
	return ret;
}

static int erofs_xattr_iter_shared(struct erofs_xattr_iter *it,
				   struct inode *inode, bool getxattr)
{
	struct erofs_inode *const vi = EROFS_I(inode);
	struct super_block *const sb = it->sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	unsigned int i;
	int ret = -ENOATTR;

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		it->pos = erofs_pos(sb, sbi->xattr_blkaddr) +
				vi->xattr_shared_xattrs[i] * sizeof(__le32);
		it->kaddr = erofs_bread(&it->buf, erofs_blknr(sb, it->pos),
					EROFS_KMAP);
		if (IS_ERR(it->kaddr))
			return PTR_ERR(it->kaddr);

		if (getxattr)
			ret = erofs_getxattr_foreach(it);
		else
			ret = erofs_listxattr_foreach(it);
		if ((getxattr && ret != -ENOATTR) || (!getxattr && ret))
			break;
	}
	return ret;
}

int erofs_getxattr(struct inode *inode, int index, const char *name,
		   void *buffer, size_t buffer_size)
{
	int ret;
	unsigned int hashbit;
	struct erofs_xattr_iter it;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);

	if (!name)
		return -EINVAL;

	ret = erofs_init_inode_xattrs(inode);
	if (ret)
		return ret;

	/* reserved flag is non-zero if there's any change of on-disk format */
	if (erofs_sb_has_xattr_filter(sbi) && !sbi->xattr_filter_reserved) {
		hashbit = xxh32(name, strlen(name),
				EROFS_XATTR_FILTER_SEED + index);
		hashbit &= EROFS_XATTR_FILTER_BITS - 1;
		if (vi->xattr_name_filter & (1U << hashbit))
			return -ENOATTR;
	}

	it.index = index;
	it.name = (struct qstr)QSTR_INIT(name, strlen(name));
	if (it.name.len > EROFS_NAME_LEN)
		return -ERANGE;

	it.sb = inode->i_sb;
	it.buf = __EROFS_BUF_INITIALIZER;
	erofs_init_metabuf(&it.buf, it.sb);
	it.buffer = buffer;
	it.buffer_size = buffer_size;
	it.buffer_ofs = 0;

	ret = erofs_xattr_iter_inline(&it, inode, true);
	if (ret == -ENOATTR)
		ret = erofs_xattr_iter_shared(&it, inode, true);
	erofs_put_metabuf(&it.buf);
	return ret ? ret : it.buffer_ofs;
}

ssize_t erofs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	int ret;
	struct erofs_xattr_iter it;
	struct inode *inode = d_inode(dentry);

	ret = erofs_init_inode_xattrs(inode);
	if (ret == -ENOATTR)
		return 0;
	if (ret)
		return ret;

	it.sb = dentry->d_sb;
	it.buf = __EROFS_BUF_INITIALIZER;
	erofs_init_metabuf(&it.buf, it.sb);
	it.dentry = dentry;
	it.buffer = buffer;
	it.buffer_size = buffer_size;
	it.buffer_ofs = 0;

	ret = erofs_xattr_iter_inline(&it, inode, false);
	if (!ret || ret == -ENOATTR)
		ret = erofs_xattr_iter_shared(&it, inode, false);
	if (ret == -ENOATTR)
		ret = 0;
	erofs_put_metabuf(&it.buf);
	return ret ? ret : it.buffer_ofs;
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

	if (sbi->packed_inode)
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
