/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_STR_HASH_H
#define _BCACHEFS_STR_HASH_H

#include "btree_iter.h"
#include "btree_update.h"
#include "checksum.h"
#include "error.h"
#include "inode.h"
#include "siphash.h"
#include "subvolume.h"
#include "super.h"

#include <linux/crc32c.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>

static inline enum bch_str_hash_type
bch2_str_hash_opt_to_type(struct bch_fs *c, enum bch_str_hash_opts opt)
{
	switch (opt) {
	case BCH_STR_HASH_OPT_crc32c:
		return BCH_STR_HASH_crc32c;
	case BCH_STR_HASH_OPT_crc64:
		return BCH_STR_HASH_crc64;
	case BCH_STR_HASH_OPT_siphash:
		return c->sb.features & (1ULL << BCH_FEATURE_new_siphash)
			? BCH_STR_HASH_siphash
			: BCH_STR_HASH_siphash_old;
	default:
	     BUG();
	}
}

struct bch_hash_info {
	u8			type;
	/*
	 * For crc32 or crc64 string hashes the first key value of
	 * the siphash_key (k0) is used as the key.
	 */
	SIPHASH_KEY	siphash_key;
};

static inline struct bch_hash_info
bch2_hash_info_init(struct bch_fs *c, const struct bch_inode_unpacked *bi)
{
	/* XXX ick */
	struct bch_hash_info info = {
		.type = (bi->bi_flags >> INODE_STR_HASH_OFFSET) &
			~(~0U << INODE_STR_HASH_BITS),
		.siphash_key = { .k0 = bi->bi_hash_seed }
	};

	if (unlikely(info.type == BCH_STR_HASH_siphash_old)) {
		SHASH_DESC_ON_STACK(desc, c->sha256);
		u8 digest[SHA256_DIGEST_SIZE];

		desc->tfm = c->sha256;

		crypto_shash_digest(desc, (void *) &bi->bi_hash_seed,
				    sizeof(bi->bi_hash_seed), digest);
		memcpy(&info.siphash_key, digest, sizeof(info.siphash_key));
	}

	return info;
}

struct bch_str_hash_ctx {
	union {
		u32		crc32c;
		u64		crc64;
		SIPHASH_CTX	siphash;
	};
};

static inline void bch2_str_hash_init(struct bch_str_hash_ctx *ctx,
				     const struct bch_hash_info *info)
{
	switch (info->type) {
	case BCH_STR_HASH_crc32c:
		ctx->crc32c = crc32c(~0, &info->siphash_key.k0,
				     sizeof(info->siphash_key.k0));
		break;
	case BCH_STR_HASH_crc64:
		ctx->crc64 = crc64_be(~0, &info->siphash_key.k0,
				      sizeof(info->siphash_key.k0));
		break;
	case BCH_STR_HASH_siphash_old:
	case BCH_STR_HASH_siphash:
		SipHash24_Init(&ctx->siphash, &info->siphash_key);
		break;
	default:
		BUG();
	}
}

static inline void bch2_str_hash_update(struct bch_str_hash_ctx *ctx,
				       const struct bch_hash_info *info,
				       const void *data, size_t len)
{
	switch (info->type) {
	case BCH_STR_HASH_crc32c:
		ctx->crc32c = crc32c(ctx->crc32c, data, len);
		break;
	case BCH_STR_HASH_crc64:
		ctx->crc64 = crc64_be(ctx->crc64, data, len);
		break;
	case BCH_STR_HASH_siphash_old:
	case BCH_STR_HASH_siphash:
		SipHash24_Update(&ctx->siphash, data, len);
		break;
	default:
		BUG();
	}
}

static inline u64 bch2_str_hash_end(struct bch_str_hash_ctx *ctx,
				   const struct bch_hash_info *info)
{
	switch (info->type) {
	case BCH_STR_HASH_crc32c:
		return ctx->crc32c;
	case BCH_STR_HASH_crc64:
		return ctx->crc64 >> 1;
	case BCH_STR_HASH_siphash_old:
	case BCH_STR_HASH_siphash:
		return SipHash24_End(&ctx->siphash) >> 1;
	default:
		BUG();
	}
}

struct bch_hash_desc {
	enum btree_id	btree_id;
	u8		key_type;

	u64		(*hash_key)(const struct bch_hash_info *, const void *);
	u64		(*hash_bkey)(const struct bch_hash_info *, struct bkey_s_c);
	bool		(*cmp_key)(struct bkey_s_c, const void *);
	bool		(*cmp_bkey)(struct bkey_s_c, struct bkey_s_c);
	bool		(*is_visible)(subvol_inum inum, struct bkey_s_c);
};

static inline bool is_visible_key(struct bch_hash_desc desc, subvol_inum inum, struct bkey_s_c k)
{
	return k.k->type == desc.key_type &&
		(!desc.is_visible ||
		 !inum.inum ||
		 desc.is_visible(inum, k));
}

static __always_inline int
bch2_hash_lookup(struct btree_trans *trans,
		 struct btree_iter *iter,
		 const struct bch_hash_desc desc,
		 const struct bch_hash_info *info,
		 subvol_inum inum, const void *key,
		 unsigned flags)
{
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		return ret;

	for_each_btree_key_upto_norestart(trans, *iter, desc.btree_id,
			   SPOS(inum.inum, desc.hash_key(info, key), snapshot),
			   POS(inum.inum, U64_MAX),
			   BTREE_ITER_SLOTS|flags, k, ret) {
		if (is_visible_key(desc, inum, k)) {
			if (!desc.cmp_key(k, key))
				return 0;
		} else if (k.k->type == KEY_TYPE_hash_whiteout) {
			;
		} else {
			/* hole, not found */
			break;
		}
	}
	bch2_trans_iter_exit(trans, iter);

	return ret ?: -ENOENT;
}

static __always_inline int
bch2_hash_hole(struct btree_trans *trans,
	       struct btree_iter *iter,
	       const struct bch_hash_desc desc,
	       const struct bch_hash_info *info,
	       subvol_inum inum, const void *key)
{
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		return ret;

	for_each_btree_key_upto_norestart(trans, *iter, desc.btree_id,
			   SPOS(inum.inum, desc.hash_key(info, key), snapshot),
			   POS(inum.inum, U64_MAX),
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k, ret)
		if (!is_visible_key(desc, inum, k))
			return 0;
	bch2_trans_iter_exit(trans, iter);

	return ret ?: -BCH_ERR_ENOSPC_str_hash_create;
}

static __always_inline
int bch2_hash_needs_whiteout(struct btree_trans *trans,
			     const struct bch_hash_desc desc,
			     const struct bch_hash_info *info,
			     struct btree_iter *start)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_copy_iter(&iter, start);

	bch2_btree_iter_advance(&iter);

	for_each_btree_key_continue_norestart(iter, BTREE_ITER_SLOTS, k, ret) {
		if (k.k->type != desc.key_type &&
		    k.k->type != KEY_TYPE_hash_whiteout)
			break;

		if (k.k->type == desc.key_type &&
		    desc.hash_bkey(info, k) <= start->pos.offset) {
			ret = 1;
			break;
		}
	}

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static __always_inline
int bch2_hash_set_snapshot(struct btree_trans *trans,
			   const struct bch_hash_desc desc,
			   const struct bch_hash_info *info,
			   subvol_inum inum, u32 snapshot,
			   struct bkey_i *insert,
			   int flags,
			   int update_flags)
{
	struct btree_iter iter, slot = { NULL };
	struct bkey_s_c k;
	bool found = false;
	int ret;

	for_each_btree_key_upto_norestart(trans, iter, desc.btree_id,
			   SPOS(insert->k.p.inode,
				desc.hash_bkey(info, bkey_i_to_s_c(insert)),
				snapshot),
			   POS(insert->k.p.inode, U64_MAX),
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k, ret) {
		if (is_visible_key(desc, inum, k)) {
			if (!desc.cmp_bkey(k, bkey_i_to_s_c(insert)))
				goto found;

			/* hash collision: */
			continue;
		}

		if (!slot.path &&
		    !(flags & BCH_HASH_SET_MUST_REPLACE))
			bch2_trans_copy_iter(&slot, &iter);

		if (k.k->type != KEY_TYPE_hash_whiteout)
			goto not_found;
	}

	if (!ret)
		ret = -BCH_ERR_ENOSPC_str_hash_create;
out:
	bch2_trans_iter_exit(trans, &slot);
	bch2_trans_iter_exit(trans, &iter);

	return ret;
found:
	found = true;
not_found:

	if (!found && (flags & BCH_HASH_SET_MUST_REPLACE)) {
		ret = -ENOENT;
	} else if (found && (flags & BCH_HASH_SET_MUST_CREATE)) {
		ret = -EEXIST;
	} else {
		if (!found && slot.path)
			swap(iter, slot);

		insert->k.p = iter.pos;
		ret = bch2_trans_update(trans, &iter, insert, 0);
	}

	goto out;
}

static __always_inline
int bch2_hash_set(struct btree_trans *trans,
		  const struct bch_hash_desc desc,
		  const struct bch_hash_info *info,
		  subvol_inum inum,
		  struct bkey_i *insert, int flags)
{
	u32 snapshot;
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		return ret;

	insert->k.p.inode = inum.inum;

	return bch2_hash_set_snapshot(trans, desc, info, inum,
				      snapshot, insert, flags, 0);
}

static __always_inline
int bch2_hash_delete_at(struct btree_trans *trans,
			const struct bch_hash_desc desc,
			const struct bch_hash_info *info,
			struct btree_iter *iter,
			unsigned update_flags)
{
	struct bkey_i *delete;
	int ret;

	delete = bch2_trans_kmalloc(trans, sizeof(*delete));
	ret = PTR_ERR_OR_ZERO(delete);
	if (ret)
		return ret;

	ret = bch2_hash_needs_whiteout(trans, desc, info, iter);
	if (ret < 0)
		return ret;

	bkey_init(&delete->k);
	delete->k.p = iter->pos;
	delete->k.type = ret ? KEY_TYPE_hash_whiteout : KEY_TYPE_deleted;

	return bch2_trans_update(trans, iter, delete, update_flags);
}

static __always_inline
int bch2_hash_delete(struct btree_trans *trans,
		     const struct bch_hash_desc desc,
		     const struct bch_hash_info *info,
		     subvol_inum inum, const void *key)
{
	struct btree_iter iter;
	int ret;

	ret = bch2_hash_lookup(trans, &iter, desc, info, inum, key,
				BTREE_ITER_INTENT);
	if (ret)
		return ret;

	ret = bch2_hash_delete_at(trans, desc, info, &iter, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

#endif /* _BCACHEFS_STR_HASH_H */
