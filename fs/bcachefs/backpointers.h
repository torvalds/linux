/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BACKPOINTERS_BACKGROUND_H
#define _BCACHEFS_BACKPOINTERS_BACKGROUND_H

#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "buckets.h"
#include "error.h"
#include "super.h"

static inline u64 swab40(u64 x)
{
	return (((x & 0x00000000ffULL) << 32)|
		((x & 0x000000ff00ULL) << 16)|
		((x & 0x0000ff0000ULL) >>  0)|
		((x & 0x00ff000000ULL) >> 16)|
		((x & 0xff00000000ULL) >> 32));
}

int bch2_backpointer_validate(struct bch_fs *, struct bkey_s_c k, enum bch_validate_flags);
void bch2_backpointer_to_text(struct printbuf *, const struct bch_backpointer *);
void bch2_backpointer_k_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
void bch2_backpointer_swab(struct bkey_s);

#define bch2_bkey_ops_backpointer ((struct bkey_ops) {	\
	.key_validate	= bch2_backpointer_validate,	\
	.val_to_text	= bch2_backpointer_k_to_text,	\
	.swab		= bch2_backpointer_swab,	\
	.min_val_size	= 32,				\
})

#define MAX_EXTENT_COMPRESS_RATIO_SHIFT		10

/*
 * Convert from pos in backpointer btree to pos of corresponding bucket in alloc
 * btree:
 */
static inline struct bpos bp_pos_to_bucket(const struct bch_dev *ca, struct bpos bp_pos)
{
	u64 bucket_sector = bp_pos.offset >> MAX_EXTENT_COMPRESS_RATIO_SHIFT;

	return POS(bp_pos.inode, sector_to_bucket(ca, bucket_sector));
}

static inline bool bp_pos_to_bucket_nodev_noerror(struct bch_fs *c, struct bpos bp_pos, struct bpos *bucket)
{
	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu(c, bp_pos.inode);
	if (ca)
		*bucket = bp_pos_to_bucket(ca, bp_pos);
	rcu_read_unlock();
	return ca != NULL;
}

static inline bool bp_pos_to_bucket_nodev(struct bch_fs *c, struct bpos bp_pos, struct bpos *bucket)
{
	return !bch2_fs_inconsistent_on(!bp_pos_to_bucket_nodev_noerror(c, bp_pos, bucket),
					c, "backpointer for missing device %llu", bp_pos.inode);
}

static inline struct bpos bucket_pos_to_bp_noerror(const struct bch_dev *ca,
						   struct bpos bucket,
						   u64 bucket_offset)
{
	return POS(bucket.inode,
		   (bucket_to_sector(ca, bucket.offset) <<
		    MAX_EXTENT_COMPRESS_RATIO_SHIFT) + bucket_offset);
}

/*
 * Convert from pos in alloc btree + bucket offset to pos in backpointer btree:
 */
static inline struct bpos bucket_pos_to_bp(const struct bch_dev *ca,
					   struct bpos bucket,
					   u64 bucket_offset)
{
	struct bpos ret = bucket_pos_to_bp_noerror(ca, bucket, bucket_offset);
	EBUG_ON(!bkey_eq(bucket, bp_pos_to_bucket(ca, ret)));
	return ret;
}

int bch2_bucket_backpointer_mod_nowritebuffer(struct btree_trans *, struct bch_dev *,
				struct bpos bucket, struct bch_backpointer, struct bkey_s_c, bool);

static inline int bch2_bucket_backpointer_mod(struct btree_trans *trans,
				struct bch_dev *ca,
				struct bpos bucket,
				struct bch_backpointer bp,
				struct bkey_s_c orig_k,
				bool insert)
{
	if (unlikely(bch2_backpointers_no_use_write_buffer))
		return bch2_bucket_backpointer_mod_nowritebuffer(trans, ca, bucket, bp, orig_k, insert);

	struct bkey_i_backpointer bp_k;

	bkey_backpointer_init(&bp_k.k_i);
	bp_k.k.p = bucket_pos_to_bp(ca, bucket, bp.bucket_offset);
	bp_k.v = bp;

	if (!insert) {
		bp_k.k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&bp_k.k, 0);
	}

	return bch2_trans_update_buffered(trans, BTREE_ID_backpointers, &bp_k.k_i);
}

static inline enum bch_data_type bch2_bkey_ptr_data_type(struct bkey_s_c k,
							 struct extent_ptr_decoded p,
							 const union bch_extent_entry *entry)
{
	switch (k.k->type) {
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_btree_ptr_v2:
		return BCH_DATA_btree;
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		return p.has_ec ? BCH_DATA_stripe : BCH_DATA_user;
	case KEY_TYPE_stripe: {
		const struct bch_extent_ptr *ptr = &entry->ptr;
		struct bkey_s_c_stripe s = bkey_s_c_to_stripe(k);

		BUG_ON(ptr < s.v->ptrs ||
		       ptr >= s.v->ptrs + s.v->nr_blocks);

		return ptr >= s.v->ptrs + s.v->nr_blocks - s.v->nr_redundant
			? BCH_DATA_parity
			: BCH_DATA_user;
	}
	default:
		BUG();
	}
}

static inline void __bch2_extent_ptr_to_bp(struct bch_fs *c, struct bch_dev *ca,
			   enum btree_id btree_id, unsigned level,
			   struct bkey_s_c k, struct extent_ptr_decoded p,
			   const union bch_extent_entry *entry,
			   struct bpos *bucket_pos, struct bch_backpointer *bp,
			   u64 sectors)
{
	u32 bucket_offset;
	*bucket_pos = PTR_BUCKET_POS_OFFSET(ca, &p.ptr, &bucket_offset);
	*bp = (struct bch_backpointer) {
		.btree_id	= btree_id,
		.level		= level,
		.data_type	= bch2_bkey_ptr_data_type(k, p, entry),
		.bucket_offset	= ((u64) bucket_offset << MAX_EXTENT_COMPRESS_RATIO_SHIFT) +
			p.crc.offset,
		.bucket_len	= sectors,
		.pos		= k.k->p,
	};
}

static inline void bch2_extent_ptr_to_bp(struct bch_fs *c, struct bch_dev *ca,
			   enum btree_id btree_id, unsigned level,
			   struct bkey_s_c k, struct extent_ptr_decoded p,
			   const union bch_extent_entry *entry,
			   struct bpos *bucket_pos, struct bch_backpointer *bp)
{
	u64 sectors = ptr_disk_sectors(level ? btree_sectors(c) : k.k->size, p);

	__bch2_extent_ptr_to_bp(c, ca, btree_id, level, k, p, entry, bucket_pos, bp, sectors);
}

int bch2_get_next_backpointer(struct btree_trans *, struct bch_dev *ca, struct bpos, int,
			      struct bpos *, struct bch_backpointer *, unsigned);
struct bkey_s_c bch2_backpointer_get_key(struct btree_trans *, struct btree_iter *,
					 struct bpos, struct bch_backpointer,
					 unsigned);
struct btree *bch2_backpointer_get_node(struct btree_trans *, struct btree_iter *,
					struct bpos, struct bch_backpointer);

int bch2_check_btree_backpointers(struct bch_fs *);
int bch2_check_extents_to_backpointers(struct bch_fs *);
int bch2_check_backpointers_to_extents(struct bch_fs *);

#endif /* _BCACHEFS_BACKPOINTERS_BACKGROUND_H */
