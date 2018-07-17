// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "compress.h"
#include "extents.h"
#include "fs.h"
#include "rebalance.h"
#include "str_hash.h"
#include "xattr.h"

#include <linux/dcache.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>

static const struct xattr_handler *bch2_xattr_type_to_handler(unsigned);

static u64 bch2_xattr_hash(const struct bch_hash_info *info,
			  const struct xattr_search_key *key)
{
	struct bch_str_hash_ctx ctx;

	bch2_str_hash_init(&ctx, info);
	bch2_str_hash_update(&ctx, info, &key->type, sizeof(key->type));
	bch2_str_hash_update(&ctx, info, key->name.name, key->name.len);

	return bch2_str_hash_end(&ctx, info);
}

static u64 xattr_hash_key(const struct bch_hash_info *info, const void *key)
{
	return bch2_xattr_hash(info, key);
}

static u64 xattr_hash_bkey(const struct bch_hash_info *info, struct bkey_s_c k)
{
	struct bkey_s_c_xattr x = bkey_s_c_to_xattr(k);

	return bch2_xattr_hash(info,
		 &X_SEARCH(x.v->x_type, x.v->x_name, x.v->x_name_len));
}

static bool xattr_cmp_key(struct bkey_s_c _l, const void *_r)
{
	struct bkey_s_c_xattr l = bkey_s_c_to_xattr(_l);
	const struct xattr_search_key *r = _r;

	return l.v->x_type != r->type ||
		l.v->x_name_len != r->name.len ||
		memcmp(l.v->x_name, r->name.name, r->name.len);
}

static bool xattr_cmp_bkey(struct bkey_s_c _l, struct bkey_s_c _r)
{
	struct bkey_s_c_xattr l = bkey_s_c_to_xattr(_l);
	struct bkey_s_c_xattr r = bkey_s_c_to_xattr(_r);

	return l.v->x_type != r.v->x_type ||
		l.v->x_name_len != r.v->x_name_len ||
		memcmp(l.v->x_name, r.v->x_name, r.v->x_name_len);
}

const struct bch_hash_desc bch2_xattr_hash_desc = {
	.btree_id	= BTREE_ID_XATTRS,
	.key_type	= BCH_XATTR,
	.whiteout_type	= BCH_XATTR_WHITEOUT,
	.hash_key	= xattr_hash_key,
	.hash_bkey	= xattr_hash_bkey,
	.cmp_key	= xattr_cmp_key,
	.cmp_bkey	= xattr_cmp_bkey,
};

const char *bch2_xattr_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	const struct xattr_handler *handler;
	struct bkey_s_c_xattr xattr;

	switch (k.k->type) {
	case BCH_XATTR:
		if (bkey_val_bytes(k.k) < sizeof(struct bch_xattr))
			return "value too small";

		xattr = bkey_s_c_to_xattr(k);

		if (bkey_val_u64s(k.k) <
			xattr_val_u64s(xattr.v->x_name_len,
				       le16_to_cpu(xattr.v->x_val_len)))
			return "value too small";

		if (bkey_val_u64s(k.k) >
			xattr_val_u64s(xattr.v->x_name_len,
				       le16_to_cpu(xattr.v->x_val_len) + 4))
			return "value too big";

		handler = bch2_xattr_type_to_handler(xattr.v->x_type);
		if (!handler)
			return "invalid type";

		if (memchr(xattr.v->x_name, '\0', xattr.v->x_name_len))
			return "xattr name has invalid characters";

		return NULL;
	case BCH_XATTR_WHITEOUT:
		return bkey_val_bytes(k.k) != 0
			? "value size should be zero"
			: NULL;

	default:
		return "invalid type";
	}
}

void bch2_xattr_to_text(struct bch_fs *c, char *buf,
			size_t size, struct bkey_s_c k)
{
	const struct xattr_handler *handler;
	struct bkey_s_c_xattr xattr;
	size_t n = 0;

	switch (k.k->type) {
	case BCH_XATTR:
		xattr = bkey_s_c_to_xattr(k);

		handler = bch2_xattr_type_to_handler(xattr.v->x_type);
		if (handler && handler->prefix)
			n += scnprintf(buf + n, size - n, "%s", handler->prefix);
		else if (handler)
			n += scnprintf(buf + n, size - n, "(type %u)",
				       xattr.v->x_type);
		else
			n += scnprintf(buf + n, size - n, "(unknown type %u)",
				       xattr.v->x_type);

		n += bch_scnmemcpy(buf + n, size - n, xattr.v->x_name,
				   xattr.v->x_name_len);
		n += scnprintf(buf + n, size - n, ":");
		n += bch_scnmemcpy(buf + n, size - n, xattr_val(xattr.v),
				   le16_to_cpu(xattr.v->x_val_len));
		break;
	case BCH_XATTR_WHITEOUT:
		scnprintf(buf, size, "whiteout");
		break;
	}
}

int bch2_xattr_get(struct bch_fs *c, struct bch_inode_info *inode,
		   const char *name, void *buffer, size_t size, int type)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c_xattr xattr;
	int ret;

	bch2_trans_init(&trans, c);

	iter = bch2_hash_lookup(&trans, bch2_xattr_hash_desc,
				&inode->ei_str_hash, inode->v.i_ino,
				&X_SEARCH(type, name, strlen(name)),
				0);
	if (IS_ERR(iter)) {
		bch2_trans_exit(&trans);
		BUG_ON(PTR_ERR(iter) == -EINTR);

		return PTR_ERR(iter) == -ENOENT ? -ENODATA : PTR_ERR(iter);
	}

	xattr = bkey_s_c_to_xattr(bch2_btree_iter_peek_slot(iter));
	ret = le16_to_cpu(xattr.v->x_val_len);
	if (buffer) {
		if (ret > size)
			ret = -ERANGE;
		else
			memcpy(buffer, xattr_val(xattr.v), ret);
	}

	bch2_trans_exit(&trans);
	return ret;
}

int bch2_xattr_set(struct btree_trans *trans, u64 inum,
		   const struct bch_hash_info *hash_info,
		   const char *name, const void *value, size_t size,
		   int type, int flags)
{
	int ret;

	if (value) {
		struct bkey_i_xattr *xattr;
		unsigned namelen = strlen(name);
		unsigned u64s = BKEY_U64s +
			xattr_val_u64s(namelen, size);

		if (u64s > U8_MAX)
			return -ERANGE;

		xattr = bch2_trans_kmalloc(trans, u64s * sizeof(u64));
		if (IS_ERR(xattr))
			return PTR_ERR(xattr);

		bkey_xattr_init(&xattr->k_i);
		xattr->k.u64s		= u64s;
		xattr->v.x_type		= type;
		xattr->v.x_name_len	= namelen;
		xattr->v.x_val_len	= cpu_to_le16(size);
		memcpy(xattr->v.x_name, name, namelen);
		memcpy(xattr_val(&xattr->v), value, size);

		ret = __bch2_hash_set(trans, bch2_xattr_hash_desc, hash_info,
			      inum, &xattr->k_i,
			      (flags & XATTR_CREATE ? BCH_HASH_SET_MUST_CREATE : 0)|
			      (flags & XATTR_REPLACE ? BCH_HASH_SET_MUST_REPLACE : 0));
	} else {
		struct xattr_search_key search =
			X_SEARCH(type, name, strlen(name));

		ret = bch2_hash_delete(trans, bch2_xattr_hash_desc,
				       hash_info, inum, &search);
	}

	if (ret == -ENOENT)
		ret = flags & XATTR_REPLACE ? -ENODATA : 0;

	return ret;
}

static size_t bch2_xattr_emit(struct dentry *dentry,
			     const struct bch_xattr *xattr,
			     char *buffer, size_t buffer_size)
{
	const struct xattr_handler *handler =
		bch2_xattr_type_to_handler(xattr->x_type);

	if (handler && (!handler->list || handler->list(dentry))) {
		const char *prefix = handler->prefix ?: handler->name;
		const size_t prefix_len = strlen(prefix);
		const size_t total_len = prefix_len + xattr->x_name_len + 1;

		if (buffer && total_len <= buffer_size) {
			memcpy(buffer, prefix, prefix_len);
			memcpy(buffer + prefix_len,
			       xattr->x_name, xattr->x_name_len);
			buffer[prefix_len + xattr->x_name_len] = '\0';
		}

		return total_len;
	} else {
		return 0;
	}
}

ssize_t bch2_xattr_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct bch_fs *c = dentry->d_sb->s_fs_info;
	struct btree_iter iter;
	struct bkey_s_c k;
	const struct bch_xattr *xattr;
	u64 inum = dentry->d_inode->i_ino;
	ssize_t ret = 0;
	size_t len;

	for_each_btree_key(&iter, c, BTREE_ID_XATTRS, POS(inum, 0), 0, k) {
		BUG_ON(k.k->p.inode < inum);

		if (k.k->p.inode > inum)
			break;

		if (k.k->type != BCH_XATTR)
			continue;

		xattr = bkey_s_c_to_xattr(k).v;

		len = bch2_xattr_emit(dentry, xattr, buffer, buffer_size);
		if (buffer) {
			if (len > buffer_size) {
				bch2_btree_iter_unlock(&iter);
				return -ERANGE;
			}

			buffer += len;
			buffer_size -= len;
		}

		ret += len;

	}
	bch2_btree_iter_unlock(&iter);

	return ret;
}

static int bch2_xattr_get_handler(const struct xattr_handler *handler,
				  struct dentry *dentry, struct inode *vinode,
				  const char *name, void *buffer, size_t size)
{
	struct bch_inode_info *inode = to_bch_ei(vinode);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	return bch2_xattr_get(c, inode, name, buffer, size, handler->flags);
}

static int bch2_xattr_set_handler(const struct xattr_handler *handler,
				  struct mnt_idmap *idmap,
				  struct dentry *dentry, struct inode *vinode,
				  const char *name, const void *value,
				  size_t size, int flags)
{
	struct bch_inode_info *inode = to_bch_ei(vinode);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	return bch2_trans_do(c, &inode->ei_journal_seq, BTREE_INSERT_ATOMIC,
			bch2_xattr_set(&trans, inode->v.i_ino,
				       &inode->ei_str_hash,
				       name, value, size,
				       handler->flags, flags));
}

static const struct xattr_handler bch_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= bch2_xattr_get_handler,
	.set	= bch2_xattr_set_handler,
	.flags	= BCH_XATTR_INDEX_USER,
};

static bool bch2_xattr_trusted_list(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
}

static const struct xattr_handler bch_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.list	= bch2_xattr_trusted_list,
	.get	= bch2_xattr_get_handler,
	.set	= bch2_xattr_set_handler,
	.flags	= BCH_XATTR_INDEX_TRUSTED,
};

static const struct xattr_handler bch_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= bch2_xattr_get_handler,
	.set	= bch2_xattr_set_handler,
	.flags	= BCH_XATTR_INDEX_SECURITY,
};

#ifndef NO_BCACHEFS_FS

static int bch2_xattr_bcachefs_get(const struct xattr_handler *handler,
				   struct dentry *dentry, struct inode *vinode,
				   const char *name, void *buffer, size_t size)
{
	struct bch_inode_info *inode = to_bch_ei(vinode);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_opts opts =
		bch2_inode_opts_to_opts(bch2_inode_opts_get(&inode->ei_inode));
	const struct bch_option *opt;
	int ret, id;
	u64 v;

	id = bch2_opt_lookup(name);
	if (id < 0 || !bch2_opt_is_inode_opt(id))
		return -EINVAL;

	opt = bch2_opt_table + id;

	if (!bch2_opt_defined_by_id(&opts, id))
		return -ENODATA;

	v = bch2_opt_get_by_id(&opts, id);

	ret = bch2_opt_to_text(c, buffer, size, opt, v, 0);

	return ret < size || !buffer ? ret : -ERANGE;
}

struct inode_opt_set {
	int			id;
	u64			v;
	bool			defined;
};

static int inode_opt_set_fn(struct bch_inode_info *inode,
			    struct bch_inode_unpacked *bi,
			    void *p)
{
	struct inode_opt_set *s = p;

	if (s->defined)
		bch2_inode_opt_set(bi, s->id, s->v);
	else
		bch2_inode_opt_clear(bi, s->id);
	return 0;
}

static int bch2_xattr_bcachefs_set(const struct xattr_handler *handler,
				   struct mnt_idmap *idmap,
				   struct dentry *dentry, struct inode *vinode,
				   const char *name, const void *value,
				   size_t size, int flags)
{
	struct bch_inode_info *inode = to_bch_ei(vinode);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	const struct bch_option *opt;
	char *buf;
	struct inode_opt_set s;
	int ret;

	s.id = bch2_opt_lookup(name);
	if (s.id < 0 || !bch2_opt_is_inode_opt(s.id))
		return -EINVAL;

	opt = bch2_opt_table + s.id;

	if (value) {
		buf = kmalloc(size + 1, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		memcpy(buf, value, size);
		buf[size] = '\0';

		ret = bch2_opt_parse(c, opt, buf, &s.v);
		kfree(buf);

		if (ret < 0)
			return ret;

		if (s.id == Opt_compression ||
		    s.id == Opt_background_compression) {
			ret = bch2_check_set_has_compressed_data(c, s.v);
			if (ret)
				return ret;
		}

		s.defined = true;
	} else {
		s.defined = false;
	}

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode(c, inode, inode_opt_set_fn, &s, 0);
	mutex_unlock(&inode->ei_update_lock);

	if (value &&
	    (s.id == Opt_background_compression ||
	     s.id == Opt_background_target))
		bch2_rebalance_add_work(c, inode->v.i_blocks);

	return ret;
}

static const struct xattr_handler bch_xattr_bcachefs_handler = {
	.prefix	= "bcachefs.",
	.get	= bch2_xattr_bcachefs_get,
	.set	= bch2_xattr_bcachefs_set,
};

#endif /* NO_BCACHEFS_FS */

const struct xattr_handler *bch2_xattr_handlers[] = {
	&bch_xattr_user_handler,
	&nop_posix_acl_access,
	&nop_posix_acl_default,
	&bch_xattr_trusted_handler,
	&bch_xattr_security_handler,
#ifndef NO_BCACHEFS_FS
	&bch_xattr_bcachefs_handler,
#endif
	NULL
};

static const struct xattr_handler *bch_xattr_handler_map[] = {
	[BCH_XATTR_INDEX_USER]			= &bch_xattr_user_handler,
	[BCH_XATTR_INDEX_POSIX_ACL_ACCESS]	=
		&nop_posix_acl_access,
	[BCH_XATTR_INDEX_POSIX_ACL_DEFAULT]	=
		&nop_posix_acl_default,
	[BCH_XATTR_INDEX_TRUSTED]		= &bch_xattr_trusted_handler,
	[BCH_XATTR_INDEX_SECURITY]		= &bch_xattr_security_handler,
};

static const struct xattr_handler *bch2_xattr_type_to_handler(unsigned type)
{
	return type < ARRAY_SIZE(bch_xattr_handler_map)
		? bch_xattr_handler_map[type]
		: NULL;
}
