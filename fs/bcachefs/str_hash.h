/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_STR_HASH_H
#define _BCACHEFS_STR_HASH_H

#include "btree_iter.h"
#include "btree_update.h"
#include "checksum.h"
#include "error.h"
#include "inode.h"
#include "siphash.h"
#include "super.h"

#include <linux/crc32c.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>

struct bch_hash_info {
	u8			type;
	union {
		__le64		crc_key;
		SIPHASH_KEY	siphash_key;
	};
};

static inline struct bch_hash_info
bch2_hash_info_init(struct bch_fs *c,
		   const struct bch_inode_unpacked *bi)
{
	/* XXX ick */
	struct bch_hash_info info = {
		.type = (bi->bi_flags >> INODE_STR_HASH_OFFSET) &
			~(~0U << INODE_STR_HASH_BITS)
	};

	switch (info.type) {
	case BCH_STR_HASH_CRC32C:
	case BCH_STR_HASH_CRC64:
		info.crc_key = bi->bi_hash_seed;
		break;
	case BCH_STR_HASH_SIPHASH: {
		SHASH_DESC_ON_STACK(desc, c->sha256);
		u8 digest[SHA256_DIGEST_SIZE];

		desc->tfm = c->sha256;

		crypto_shash_digest(desc, (void *) &bi->bi_hash_seed,
				    sizeof(bi->bi_hash_seed), digest);
		memcpy(&info.siphash_key, digest, sizeof(info.siphash_key));
		break;
	}
	default:
		BUG();
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
	case BCH_STR_HASH_CRC32C:
		ctx->crc32c = crc32c(~0, &info->crc_key, sizeof(info->crc_key));
		break;
	case BCH_STR_HASH_CRC64:
		ctx->crc64 = bch2_crc64_update(~0, &info->crc_key, sizeof(info->crc_key));
		break;
	case BCH_STR_HASH_SIPHASH:
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
	case BCH_STR_HASH_CRC32C:
		ctx->crc32c = crc32c(ctx->crc32c, data, len);
		break;
	case BCH_STR_HASH_CRC64:
		ctx->crc64 = bch2_crc64_update(ctx->crc64, data, len);
		break;
	case BCH_STR_HASH_SIPHASH:
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
	case BCH_STR_HASH_CRC32C:
		return ctx->crc32c;
	case BCH_STR_HASH_CRC64:
		return ctx->crc64 >> 1;
	case BCH_STR_HASH_SIPHASH:
		return SipHash24_End(&ctx->siphash) >> 1;
	default:
		BUG();
	}
}

struct bch_hash_desc {
	enum btree_id	btree_id;
	u8		key_type;
	u8		whiteout_type;

	u64		(*hash_key)(const struct bch_hash_info *, const void *);
	u64		(*hash_bkey)(const struct bch_hash_info *, struct bkey_s_c);
	bool		(*cmp_key)(struct bkey_s_c, const void *);
	bool		(*cmp_bkey)(struct bkey_s_c, struct bkey_s_c);
};

static inline struct btree_iter *
bch2_hash_lookup(struct btree_trans *trans,
		 const struct bch_hash_desc desc,
		 const struct bch_hash_info *info,
		 u64 inode, const void *key,
		 unsigned flags)
{
	struct btree_iter *iter;
	struct bkey_s_c k;

	iter = bch2_trans_get_iter(trans, desc.btree_id,
				   POS(inode, desc.hash_key(info, key)),
				   BTREE_ITER_SLOTS|flags);
	if (IS_ERR(iter))
		return iter;

	for_each_btree_key_continue(iter, BTREE_ITER_SLOTS, k) {
		if (iter->pos.inode != inode)
			break;

		if (k.k->type == desc.key_type) {
			if (!desc.cmp_key(k, key))
				return iter;
		} else if (k.k->type == desc.whiteout_type) {
			;
		} else {
			/* hole, not found */
			break;
		}
	}

	return IS_ERR(k.k) ? ERR_CAST(k.k) : ERR_PTR(-ENOENT);
}

static inline struct btree_iter *
bch2_hash_hole(struct btree_trans *trans,
	       const struct bch_hash_desc desc,
	       const struct bch_hash_info *info,
	       u64 inode, const void *key)
{
	struct btree_iter *iter;
	struct bkey_s_c k;

	iter = bch2_trans_get_iter(trans, desc.btree_id,
				   POS(inode, desc.hash_key(info, key)),
				   BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
	if (IS_ERR(iter))
		return iter;

	for_each_btree_key_continue(iter, BTREE_ITER_SLOTS, k) {
		if (iter->pos.inode != inode)
			break;

		if (k.k->type != desc.key_type)
			return iter;
	}

	return IS_ERR(k.k) ? ERR_CAST(k.k) : ERR_PTR(-ENOSPC);
}

static inline int bch2_hash_needs_whiteout(struct btree_trans *trans,
					   const struct bch_hash_desc desc,
					   const struct bch_hash_info *info,
					   struct btree_iter *start)
{
	struct btree_iter *iter;
	struct bkey_s_c k;

	iter = bch2_trans_copy_iter(trans, start);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	bch2_btree_iter_next_slot(iter);

	for_each_btree_key_continue(iter, BTREE_ITER_SLOTS, k) {
		if (k.k->type != desc.key_type &&
		    k.k->type != desc.whiteout_type)
			return false;

		if (k.k->type == desc.key_type &&
		    desc.hash_bkey(info, k) <= start->pos.offset)
			return true;
	}
	return btree_iter_err(k);
}

static inline int __bch2_hash_set(struct btree_trans *trans,
				  const struct bch_hash_desc desc,
				  const struct bch_hash_info *info,
				  u64 inode, struct bkey_i *insert, int flags)
{
	struct btree_iter *iter, *slot = NULL;
	struct bkey_s_c k;

	iter = bch2_trans_get_iter(trans, desc.btree_id,
			POS(inode, desc.hash_bkey(info, bkey_i_to_s_c(insert))),
			BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	for_each_btree_key_continue(iter, BTREE_ITER_SLOTS, k) {
		if (iter->pos.inode != inode)
			break;

		if (k.k->type == desc.key_type) {
			if (!desc.cmp_bkey(k, bkey_i_to_s_c(insert)))
				goto found;

			/* hash collision: */
			continue;
		}

		if (!slot &&
		    !(flags & BCH_HASH_SET_MUST_REPLACE)) {
			slot = bch2_trans_copy_iter(trans, iter);
			if (IS_ERR(slot))
				return PTR_ERR(slot);
		}

		if (k.k->type != desc.whiteout_type)
			goto not_found;
	}

	return btree_iter_err(k) ?: -ENOSPC;
not_found:
	if (flags & BCH_HASH_SET_MUST_REPLACE)
		return -ENOENT;

	insert->k.p = slot->pos;
	bch2_trans_update(trans, BTREE_INSERT_ENTRY(slot, insert));
	return 0;
found:
	if (flags & BCH_HASH_SET_MUST_CREATE)
		return -EEXIST;

	insert->k.p = iter->pos;
	bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, insert));
	return 0;
}

static inline int bch2_hash_set(const struct bch_hash_desc desc,
			       const struct bch_hash_info *info,
			       struct bch_fs *c, u64 inode,
			       u64 *journal_seq,
			       struct bkey_i *insert, int flags)
{
	return bch2_trans_do(c, journal_seq, flags|BTREE_INSERT_ATOMIC,
			__bch2_hash_set(&trans, desc, info,
					inode, insert, flags));
}

static inline int bch2_hash_delete_at(struct btree_trans *trans,
				      const struct bch_hash_desc desc,
				      const struct bch_hash_info *info,
				      struct btree_iter *iter)
{
	struct bkey_i *delete;
	int ret;

	ret = bch2_hash_needs_whiteout(trans, desc, info, iter);
	if (ret < 0)
		return ret;

	delete = bch2_trans_kmalloc(trans, sizeof(*delete));
	if (IS_ERR(delete))
		return PTR_ERR(delete);

	bkey_init(&delete->k);
	delete->k.p = iter->pos;
	delete->k.type = ret ? desc.whiteout_type : KEY_TYPE_DELETED;

	bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, delete));
	return 0;
}

static inline int bch2_hash_delete(struct btree_trans *trans,
				   const struct bch_hash_desc desc,
				   const struct bch_hash_info *info,
				   u64 inode, const void *key)
{
	struct btree_iter *iter;

	iter = bch2_hash_lookup(trans, desc, info, inode, key,
				BTREE_ITER_INTENT);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	return bch2_hash_delete_at(trans, desc, info, iter);
}

#endif /* _BCACHEFS_STR_HASH_H */
