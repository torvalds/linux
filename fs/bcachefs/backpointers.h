/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BACKPOINTERS_BACKGROUND_H
#define _BCACHEFS_BACKPOINTERS_BACKGROUND_H

#include "btree_iter.h"
#include "btree_update.h"
#include "buckets.h"
#include "super.h"

int bch2_backpointer_invalid(const struct bch_fs *, struct bkey_s_c k,
			     unsigned, struct printbuf *);
void bch2_backpointer_to_text(struct printbuf *, const struct bch_backpointer *);
void bch2_backpointer_k_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
void bch2_backpointer_swab(struct bkey_s);

#define bch2_bkey_ops_backpointer ((struct bkey_ops) {	\
	.key_invalid	= bch2_backpointer_invalid,	\
	.val_to_text	= bch2_backpointer_k_to_text,	\
	.swab		= bch2_backpointer_swab,	\
})

#define MAX_EXTENT_COMPRESS_RATIO_SHIFT		10

/*
 * Convert from pos in backpointer btree to pos of corresponding bucket in alloc
 * btree:
 */
static inline struct bpos bp_pos_to_bucket(const struct bch_fs *c,
					   struct bpos bp_pos)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, bp_pos.inode);
	u64 bucket_sector = bp_pos.offset >> MAX_EXTENT_COMPRESS_RATIO_SHIFT;

	return POS(bp_pos.inode, sector_to_bucket(ca, bucket_sector));
}

/*
 * Convert from pos in alloc btree + bucket offset to pos in backpointer btree:
 */
static inline struct bpos bucket_pos_to_bp(const struct bch_fs *c,
					   struct bpos bucket,
					   u64 bucket_offset)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, bucket.inode);
	struct bpos ret;

	ret = POS(bucket.inode,
		  (bucket_to_sector(ca, bucket.offset) <<
		   MAX_EXTENT_COMPRESS_RATIO_SHIFT) + bucket_offset);

	EBUG_ON(!bkey_eq(bucket, bp_pos_to_bucket(c, ret)));

	return ret;
}

bool bch2_bucket_backpointer_del(struct btree_trans *,
				struct bkey_i_alloc_v4 *,
				struct bch_backpointer);

int bch2_bucket_backpointer_mod_nowritebuffer(struct btree_trans *,
				struct bkey_i_alloc_v4 *,
				struct bch_backpointer, struct bkey_s_c, bool);

static inline int bch2_bucket_backpointer_mod(struct btree_trans *trans,
				struct bkey_i_alloc_v4 *a,
				struct bch_backpointer bp,
				struct bkey_s_c orig_k,
				bool insert)
{
	struct bch_fs *c = trans->c;
	struct bkey_i_backpointer *bp_k;
	int ret;

	if (!insert &&
	    unlikely(BCH_ALLOC_V4_NR_BACKPOINTERS(&a->v)) &&
	    bch2_bucket_backpointer_del(trans, a, bp))
		return 0;

	if (unlikely(bch2_backpointers_no_use_write_buffer))
		return bch2_bucket_backpointer_mod_nowritebuffer(trans, a, bp, orig_k, insert);

	bp_k = bch2_trans_kmalloc_nomemzero(trans, sizeof(struct bkey_i_backpointer));
	ret = PTR_ERR_OR_ZERO(bp_k);
	if (ret)
		return ret;

	bkey_backpointer_init(&bp_k->k_i);
	bp_k->k.p = bucket_pos_to_bp(c, a->k.p, bp.bucket_offset);
	bp_k->v = bp;

	if (!insert) {
		bp_k->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&bp_k->k, 0);
	}

	return bch2_trans_update_buffered(trans, BTREE_ID_backpointers, &bp_k->k_i);
}

static inline enum bch_data_type bkey_ptr_data_type(enum btree_id btree_id, unsigned level,
						    struct bkey_s_c k, struct extent_ptr_decoded p)
{
	return  level		? BCH_DATA_btree :
		p.has_ec	? BCH_DATA_stripe :
				  BCH_DATA_user;
}

static inline void bch2_extent_ptr_to_bp(struct bch_fs *c,
			   enum btree_id btree_id, unsigned level,
			   struct bkey_s_c k, struct extent_ptr_decoded p,
			   struct bpos *bucket_pos, struct bch_backpointer *bp)
{
	enum bch_data_type data_type = bkey_ptr_data_type(btree_id, level, k, p);
	s64 sectors = level ? btree_sectors(c) : k.k->size;
	u32 bucket_offset;

	*bucket_pos = PTR_BUCKET_POS_OFFSET(c, &p.ptr, &bucket_offset);
	*bp = (struct bch_backpointer) {
		.btree_id	= btree_id,
		.level		= level,
		.data_type	= data_type,
		.bucket_offset	= ((u64) bucket_offset << MAX_EXTENT_COMPRESS_RATIO_SHIFT) +
			p.crc.offset,
		.bucket_len	= ptr_disk_sectors(sectors, p),
		.pos		= k.k->p,
	};
}

int bch2_get_next_backpointer(struct btree_trans *, struct bpos, int,
			      u64 *, struct bch_backpointer *, unsigned);
struct bkey_s_c bch2_backpointer_get_key(struct btree_trans *, struct btree_iter *,
					 struct bpos, u64, struct bch_backpointer);
struct btree *bch2_backpointer_get_node(struct btree_trans *, struct btree_iter *,
					struct bpos, u64, struct bch_backpointer);

int bch2_check_btree_backpointers(struct bch_fs *);
int bch2_check_extents_to_backpointers(struct bch_fs *);
int bch2_check_backpointers_to_extents(struct bch_fs *);

#endif /* _BCACHEFS_BACKPOINTERS_BACKGROUND_H */
