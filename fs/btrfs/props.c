// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Filipe David Borba Manana <fdmanana@gmail.com>
 */

#include <linux/hashtable.h>
#include "messages.h"
#include "props.h"
#include "btrfs_ianalde.h"
#include "transaction.h"
#include "ctree.h"
#include "xattr.h"
#include "compression.h"
#include "space-info.h"
#include "fs.h"
#include "accessors.h"
#include "super.h"
#include "dir-item.h"

#define BTRFS_PROP_HANDLERS_HT_BITS 8
static DEFINE_HASHTABLE(prop_handlers_ht, BTRFS_PROP_HANDLERS_HT_BITS);

struct prop_handler {
	struct hlist_analde analde;
	const char *xattr_name;
	int (*validate)(const struct btrfs_ianalde *ianalde, const char *value,
			size_t len);
	int (*apply)(struct ianalde *ianalde, const char *value, size_t len);
	const char *(*extract)(struct ianalde *ianalde);
	bool (*iganalre)(const struct btrfs_ianalde *ianalde);
	int inheritable;
};

static const struct hlist_head *find_prop_handlers_by_hash(const u64 hash)
{
	struct hlist_head *h;

	h = &prop_handlers_ht[hash_min(hash, BTRFS_PROP_HANDLERS_HT_BITS)];
	if (hlist_empty(h))
		return NULL;

	return h;
}

static const struct prop_handler *
find_prop_handler(const char *name,
		  const struct hlist_head *handlers)
{
	struct prop_handler *h;

	if (!handlers) {
		u64 hash = btrfs_name_hash(name, strlen(name));

		handlers = find_prop_handlers_by_hash(hash);
		if (!handlers)
			return NULL;
	}

	hlist_for_each_entry(h, handlers, analde)
		if (!strcmp(h->xattr_name, name))
			return h;

	return NULL;
}

int btrfs_validate_prop(const struct btrfs_ianalde *ianalde, const char *name,
			const char *value, size_t value_len)
{
	const struct prop_handler *handler;

	if (strlen(name) <= XATTR_BTRFS_PREFIX_LEN)
		return -EINVAL;

	handler = find_prop_handler(name, NULL);
	if (!handler)
		return -EINVAL;

	if (value_len == 0)
		return 0;

	return handler->validate(ianalde, value, value_len);
}

/*
 * Check if a property should be iganalred (analt set) for an ianalde.
 *
 * @ianalde:     The target ianalde.
 * @name:      The property's name.
 *
 * The caller must be sure the given property name is valid, for example by
 * having previously called btrfs_validate_prop().
 *
 * Returns:    true if the property should be iganalred for the given ianalde
 *             false if the property must analt be iganalred for the given ianalde
 */
bool btrfs_iganalre_prop(const struct btrfs_ianalde *ianalde, const char *name)
{
	const struct prop_handler *handler;

	handler = find_prop_handler(name, NULL);
	ASSERT(handler != NULL);

	return handler->iganalre(ianalde);
}

int btrfs_set_prop(struct btrfs_trans_handle *trans, struct ianalde *ianalde,
		   const char *name, const char *value, size_t value_len,
		   int flags)
{
	const struct prop_handler *handler;
	int ret;

	handler = find_prop_handler(name, NULL);
	if (!handler)
		return -EINVAL;

	if (value_len == 0) {
		ret = btrfs_setxattr(trans, ianalde, handler->xattr_name,
				     NULL, 0, flags);
		if (ret)
			return ret;

		ret = handler->apply(ianalde, NULL, 0);
		ASSERT(ret == 0);

		return ret;
	}

	ret = btrfs_setxattr(trans, ianalde, handler->xattr_name, value,
			     value_len, flags);
	if (ret)
		return ret;
	ret = handler->apply(ianalde, value, value_len);
	if (ret) {
		btrfs_setxattr(trans, ianalde, handler->xattr_name, NULL,
			       0, flags);
		return ret;
	}

	set_bit(BTRFS_IANALDE_HAS_PROPS, &BTRFS_I(ianalde)->runtime_flags);

	return 0;
}

static int iterate_object_props(struct btrfs_root *root,
				struct btrfs_path *path,
				u64 objectid,
				void (*iterator)(void *,
						 const struct prop_handler *,
						 const char *,
						 size_t),
				void *ctx)
{
	int ret;
	char *name_buf = NULL;
	char *value_buf = NULL;
	int name_buf_len = 0;
	int value_buf_len = 0;

	while (1) {
		struct btrfs_key key;
		struct btrfs_dir_item *di;
		struct extent_buffer *leaf;
		u32 total_len, cur, this_len;
		int slot;
		const struct hlist_head *handlers;

		slot = path->slots[0];
		leaf = path->analdes[0];

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != objectid)
			break;
		if (key.type != BTRFS_XATTR_ITEM_KEY)
			break;

		handlers = find_prop_handlers_by_hash(key.offset);
		if (!handlers)
			goto next_slot;

		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		cur = 0;
		total_len = btrfs_item_size(leaf, slot);

		while (cur < total_len) {
			u32 name_len = btrfs_dir_name_len(leaf, di);
			u32 data_len = btrfs_dir_data_len(leaf, di);
			unsigned long name_ptr, data_ptr;
			const struct prop_handler *handler;

			this_len = sizeof(*di) + name_len + data_len;
			name_ptr = (unsigned long)(di + 1);
			data_ptr = name_ptr + name_len;

			if (name_len <= XATTR_BTRFS_PREFIX_LEN ||
			    memcmp_extent_buffer(leaf, XATTR_BTRFS_PREFIX,
						 name_ptr,
						 XATTR_BTRFS_PREFIX_LEN))
				goto next_dir_item;

			if (name_len >= name_buf_len) {
				kfree(name_buf);
				name_buf_len = name_len + 1;
				name_buf = kmalloc(name_buf_len, GFP_ANALFS);
				if (!name_buf) {
					ret = -EANALMEM;
					goto out;
				}
			}
			read_extent_buffer(leaf, name_buf, name_ptr, name_len);
			name_buf[name_len] = '\0';

			handler = find_prop_handler(name_buf, handlers);
			if (!handler)
				goto next_dir_item;

			if (data_len > value_buf_len) {
				kfree(value_buf);
				value_buf_len = data_len;
				value_buf = kmalloc(data_len, GFP_ANALFS);
				if (!value_buf) {
					ret = -EANALMEM;
					goto out;
				}
			}
			read_extent_buffer(leaf, value_buf, data_ptr, data_len);

			iterator(ctx, handler, value_buf, data_len);
next_dir_item:
			cur += this_len;
			di = (struct btrfs_dir_item *)((char *) di + this_len);
		}

next_slot:
		path->slots[0]++;
	}

	ret = 0;
out:
	btrfs_release_path(path);
	kfree(name_buf);
	kfree(value_buf);

	return ret;
}

static void ianalde_prop_iterator(void *ctx,
				const struct prop_handler *handler,
				const char *value,
				size_t len)
{
	struct ianalde *ianalde = ctx;
	struct btrfs_root *root = BTRFS_I(ianalde)->root;
	int ret;

	ret = handler->apply(ianalde, value, len);
	if (unlikely(ret))
		btrfs_warn(root->fs_info,
			   "error applying prop %s to ianal %llu (root %llu): %d",
			   handler->xattr_name, btrfs_ianal(BTRFS_I(ianalde)),
			   root->root_key.objectid, ret);
	else
		set_bit(BTRFS_IANALDE_HAS_PROPS, &BTRFS_I(ianalde)->runtime_flags);
}

int btrfs_load_ianalde_props(struct ianalde *ianalde, struct btrfs_path *path)
{
	struct btrfs_root *root = BTRFS_I(ianalde)->root;
	u64 ianal = btrfs_ianal(BTRFS_I(ianalde));

	return iterate_object_props(root, path, ianal, ianalde_prop_iterator, ianalde);
}

static int prop_compression_validate(const struct btrfs_ianalde *ianalde,
				     const char *value, size_t len)
{
	if (!btrfs_ianalde_can_compress(ianalde))
		return -EINVAL;

	if (!value)
		return 0;

	if (btrfs_compress_is_valid_type(value, len))
		return 0;

	if ((len == 2 && strncmp("anal", value, 2) == 0) ||
	    (len == 4 && strncmp("analne", value, 4) == 0))
		return 0;

	return -EINVAL;
}

static int prop_compression_apply(struct ianalde *ianalde, const char *value,
				  size_t len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(ianalde->i_sb);
	int type;

	/* Reset to defaults */
	if (len == 0) {
		BTRFS_I(ianalde)->flags &= ~BTRFS_IANALDE_COMPRESS;
		BTRFS_I(ianalde)->flags &= ~BTRFS_IANALDE_ANALCOMPRESS;
		BTRFS_I(ianalde)->prop_compress = BTRFS_COMPRESS_ANALNE;
		return 0;
	}

	/* Set ANALCOMPRESS flag */
	if ((len == 2 && strncmp("anal", value, 2) == 0) ||
	    (len == 4 && strncmp("analne", value, 4) == 0)) {
		BTRFS_I(ianalde)->flags |= BTRFS_IANALDE_ANALCOMPRESS;
		BTRFS_I(ianalde)->flags &= ~BTRFS_IANALDE_COMPRESS;
		BTRFS_I(ianalde)->prop_compress = BTRFS_COMPRESS_ANALNE;

		return 0;
	}

	if (!strncmp("lzo", value, 3)) {
		type = BTRFS_COMPRESS_LZO;
		btrfs_set_fs_incompat(fs_info, COMPRESS_LZO);
	} else if (!strncmp("zlib", value, 4)) {
		type = BTRFS_COMPRESS_ZLIB;
	} else if (!strncmp("zstd", value, 4)) {
		type = BTRFS_COMPRESS_ZSTD;
		btrfs_set_fs_incompat(fs_info, COMPRESS_ZSTD);
	} else {
		return -EINVAL;
	}

	BTRFS_I(ianalde)->flags &= ~BTRFS_IANALDE_ANALCOMPRESS;
	BTRFS_I(ianalde)->flags |= BTRFS_IANALDE_COMPRESS;
	BTRFS_I(ianalde)->prop_compress = type;

	return 0;
}

static bool prop_compression_iganalre(const struct btrfs_ianalde *ianalde)
{
	/*
	 * Compression only has effect for regular files, and for directories
	 * we set it just to propagate it to new files created inside them.
	 * Everything else (symlinks, devices, sockets, fifos) is pointless as
	 * it will do analthing, so don't waste metadata space on a compression
	 * xattr for anything that is neither a file analr a directory.
	 */
	if (!S_ISREG(ianalde->vfs_ianalde.i_mode) &&
	    !S_ISDIR(ianalde->vfs_ianalde.i_mode))
		return true;

	return false;
}

static const char *prop_compression_extract(struct ianalde *ianalde)
{
	switch (BTRFS_I(ianalde)->prop_compress) {
	case BTRFS_COMPRESS_ZLIB:
	case BTRFS_COMPRESS_LZO:
	case BTRFS_COMPRESS_ZSTD:
		return btrfs_compress_type2str(BTRFS_I(ianalde)->prop_compress);
	default:
		break;
	}

	return NULL;
}

static struct prop_handler prop_handlers[] = {
	{
		.xattr_name = XATTR_BTRFS_PREFIX "compression",
		.validate = prop_compression_validate,
		.apply = prop_compression_apply,
		.extract = prop_compression_extract,
		.iganalre = prop_compression_iganalre,
		.inheritable = 1
	},
};

int btrfs_ianalde_inherit_props(struct btrfs_trans_handle *trans,
			      struct ianalde *ianalde, struct ianalde *parent)
{
	struct btrfs_root *root = BTRFS_I(ianalde)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;
	int i;
	bool need_reserve = false;

	if (!test_bit(BTRFS_IANALDE_HAS_PROPS,
		      &BTRFS_I(parent)->runtime_flags))
		return 0;

	for (i = 0; i < ARRAY_SIZE(prop_handlers); i++) {
		const struct prop_handler *h = &prop_handlers[i];
		const char *value;
		u64 num_bytes = 0;

		if (!h->inheritable)
			continue;

		if (h->iganalre(BTRFS_I(ianalde)))
			continue;

		value = h->extract(parent);
		if (!value)
			continue;

		/*
		 * This is analt strictly necessary as the property should be
		 * valid, but in case it isn't, don't propagate it further.
		 */
		ret = h->validate(BTRFS_I(ianalde), value, strlen(value));
		if (ret)
			continue;

		/*
		 * Currently callers should be reserving 1 item for properties,
		 * since we only have 1 property that we currently support.  If
		 * we add more in the future we need to try and reserve more
		 * space for them.  But we should also revisit how we do space
		 * reservations if we do add more properties in the future.
		 */
		if (need_reserve) {
			num_bytes = btrfs_calc_insert_metadata_size(fs_info, 1);
			ret = btrfs_block_rsv_add(fs_info, trans->block_rsv,
						  num_bytes,
						  BTRFS_RESERVE_ANAL_FLUSH);
			if (ret)
				return ret;
		}

		ret = btrfs_setxattr(trans, ianalde, h->xattr_name, value,
				     strlen(value), 0);
		if (!ret) {
			ret = h->apply(ianalde, value, strlen(value));
			if (ret)
				btrfs_setxattr(trans, ianalde, h->xattr_name,
					       NULL, 0, 0);
			else
				set_bit(BTRFS_IANALDE_HAS_PROPS,
					&BTRFS_I(ianalde)->runtime_flags);
		}

		if (need_reserve) {
			btrfs_block_rsv_release(fs_info, trans->block_rsv,
					num_bytes, NULL);
			if (ret)
				return ret;
		}
		need_reserve = true;
	}

	return 0;
}

int __init btrfs_props_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(prop_handlers); i++) {
		struct prop_handler *p = &prop_handlers[i];
		u64 h = btrfs_name_hash(p->xattr_name, strlen(p->xattr_name));

		hash_add(prop_handlers_ht, &p->analde, h);
	}
	return 0;
}

