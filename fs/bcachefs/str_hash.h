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
	u32			inum_snapshot;
	u8			type;
	struct unicode_map	*cf_encoding;
	/*
	 * For crc32 or crc64 string hashes the first key value of
	 * the siphash_key (k0) is used as the key.
	 */
	SIPHASH_KEY	siphash_key;
};

static inline struct bch_hash_info
bch2_hash_info_init(struct bch_fs *c, const struct bch_inode_unpacked *bi)
{
	struct bch_hash_info info = {
		.inum_snapshot	= bi->bi_snapshot,
		.type		= INODE_STR_HASH(bi),
#ifdef CONFIG_UNICODE
		.cf_encoding	= bch2_inode_casefold(c, bi) ? c->cf_encoding : NULL,
#endif
		.siphash_key	= { .k0 = bi->bi_hash_seed }
	};

	if (unlikely(info.type == BCH_STR_HASH_siphash_old)) {
		u8 digest[SHA256_DIGEST_SIZE];

		sha256((const u8 *)&bi->bi_hash_seed,
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

static __always_inline struct bkey_s_c
bch2_hash_lookup_in_snapshot(struct btree_trans *trans,
		 struct btree_iter *iter,
		 const struct bch_hash_desc desc,
		 const struct bch_hash_info *info,
		 subvol_inum inum, const void *key,
		 enum btree_iter_update_trigger_flags flags,
		 u32 snapshot)
{
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_max_norestart(trans, *iter, desc.btree_id,
			   SPOS(inum.inum, desc.hash_key(info, key), snapshot),
			   POS(inum.inum, U64_MAX),
			   BTREE_ITER_slots|flags, k, ret) {
		if (is_visible_key(desc, inum, k)) {
			if (!desc.cmp_key(k, key))
				return k;
		} else if (k.k->type == KEY_TYPE_hash_whiteout) {
			;
		} else {
			/* hole, not found */
			break;
		}
	}
	bch2_trans_iter_exit(trans, iter);

	return bkey_s_c_err(ret ?: -BCH_ERR_ENOENT_str_hash_lookup);
}

static __always_inline struct bkey_s_c
bch2_hash_lookup(struct btree_trans *trans,
		 struct btree_iter *iter,
		 const struct bch_hash_desc desc,
		 const struct bch_hash_info *info,
		 subvol_inum inum, const void *key,
		 enum btree_iter_update_trigger_flags flags)
{
	u32 snapshot;
	int ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		return bkey_s_c_err(ret);

	return bch2_hash_lookup_in_snapshot(trans, iter, desc, info, inum, key, flags, snapshot);
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

	for_each_btree_key_max_norestart(trans, *iter, desc.btree_id,
			   SPOS(inum.inum, desc.hash_key(info, key), snapshot),
			   POS(inum.inum, U64_MAX),
			   BTREE_ITER_slots|BTREE_ITER_intent, k, ret)
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

	bch2_trans_copy_iter(trans, &iter, start);

	bch2_btree_iter_advance(trans, &iter);

	for_each_btree_key_continue_norestart(trans, iter, BTREE_ITER_slots, k, ret) {
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
struct bkey_s_c bch2_hash_set_or_get_in_snapshot(struct btree_trans *trans,
			   struct btree_iter *iter,
			   const struct bch_hash_desc desc,
			   const struct bch_hash_info *info,
			   subvol_inum inum, u32 snapshot,
			   struct bkey_i *insert,
			   enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter slot = {};
	struct bkey_s_c k;
	bool found = false;
	int ret;

	for_each_btree_key_max_norestart(trans, *iter, desc.btree_id,
			   SPOS(insert->k.p.inode,
				desc.hash_bkey(info, bkey_i_to_s_c(insert)),
				snapshot),
			   POS(insert->k.p.inode, U64_MAX),
			   BTREE_ITER_slots|BTREE_ITER_intent|flags, k, ret) {
		if (is_visible_key(desc, inum, k)) {
			if (!desc.cmp_bkey(k, bkey_i_to_s_c(insert)))
				goto found;

			/* hash collision: */
			continue;
		}

		if (!slot.path && !(flags & STR_HASH_must_replace))
			bch2_trans_copy_iter(trans, &slot, iter);

		if (k.k->type != KEY_TYPE_hash_whiteout)
			goto not_found;
	}

	if (!ret)
		ret = bch_err_throw(c, ENOSPC_str_hash_create);
out:
	bch2_trans_iter_exit(trans, &slot);
	bch2_trans_iter_exit(trans, iter);
	return ret ? bkey_s_c_err(ret) : bkey_s_c_null;
found:
	found = true;
not_found:
	if (found && (flags & STR_HASH_must_create)) {
		bch2_trans_iter_exit(trans, &slot);
		return k;
	} else if (!found && (flags & STR_HASH_must_replace)) {
		ret = bch_err_throw(c, ENOENT_str_hash_set_must_replace);
	} else {
		if (!found && slot.path)
			swap(*iter, slot);

		insert->k.p = iter->pos;
		ret = bch2_trans_update(trans, iter, insert, flags);
	}

	goto out;
}

static __always_inline
int bch2_hash_set_in_snapshot(struct btree_trans *trans,
			   const struct bch_hash_desc desc,
			   const struct bch_hash_info *info,
			   subvol_inum inum, u32 snapshot,
			   struct bkey_i *insert,
			   enum btree_iter_update_trigger_flags flags)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_hash_set_or_get_in_snapshot(trans, &iter, desc, info, inum,
							     snapshot, insert, flags);
	int ret = bkey_err(k);
	if (ret)
		return ret;
	if (k.k) {
		bch2_trans_iter_exit(trans, &iter);
		return bch_err_throw(trans->c, EEXIST_str_hash_set);
	}

	return 0;
}

static __always_inline
int bch2_hash_set(struct btree_trans *trans,
		  const struct bch_hash_desc desc,
		  const struct bch_hash_info *info,
		  subvol_inum inum,
		  struct bkey_i *insert,
		  enum btree_iter_update_trigger_flags flags)
{
	insert->k.p.inode = inum.inum;

	u32 snapshot;
	return  bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot) ?:
		bch2_hash_set_in_snapshot(trans, desc, info, inum,
					  snapshot, insert, flags);
}

static __always_inline
int bch2_hash_delete_at(struct btree_trans *trans,
			const struct bch_hash_desc desc,
			const struct bch_hash_info *info,
			struct btree_iter *iter,
			enum btree_iter_update_trigger_flags flags)
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

	return bch2_trans_update(trans, iter, delete, flags);
}

static __always_inline
int bch2_hash_delete(struct btree_trans *trans,
		     const struct bch_hash_desc desc,
		     const struct bch_hash_info *info,
		     subvol_inum inum, const void *key)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_hash_lookup(trans, &iter, desc, info, inum, key,
					     BTREE_ITER_intent);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	ret = bch2_hash_delete_at(trans, desc, info, &iter, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_repair_inode_hash_info(struct btree_trans *, struct bch_inode_unpacked *);

struct snapshots_seen;
int bch2_str_hash_repair_key(struct btree_trans *,
			     struct snapshots_seen *,
			     const struct bch_hash_desc *,
			     struct bch_hash_info *,
			     struct btree_iter *, struct bkey_s_c,
			     struct btree_iter *, struct bkey_s_c,
			     bool *);

int __bch2_str_hash_check_key(struct btree_trans *,
			      struct snapshots_seen *,
			      const struct bch_hash_desc *,
			      struct bch_hash_info *,
			      struct btree_iter *, struct bkey_s_c,
			      bool *);

static inline int bch2_str_hash_check_key(struct btree_trans *trans,
			    struct snapshots_seen *s,
			    const struct bch_hash_desc *desc,
			    struct bch_hash_info *hash_info,
			    struct btree_iter *k_iter, struct bkey_s_c hash_k,
			    bool *updated_before_k_pos)
{
	if (hash_k.k->type != desc->key_type)
		return 0;

	if (likely(desc->hash_bkey(hash_info, hash_k) == hash_k.k->p.offset))
		return 0;

	return __bch2_str_hash_check_key(trans, s, desc, hash_info, k_iter, hash_k,
					 updated_before_k_pos);
}

#endif /* _BCACHEFS_STR_HASH_H */
