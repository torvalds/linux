/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_LRU_H
#define _BCACHEFS_LRU_H

#define LRU_TIME_BITS	48
#define LRU_TIME_MAX	((1ULL << LRU_TIME_BITS) - 1)

static inline struct bpos lru_pos(u16 lru_id, u64 dev_bucket, u64 time)
{
	EBUG_ON(time > LRU_TIME_MAX);

	return POS(((u64) lru_id << LRU_TIME_BITS)|time, dev_bucket);
}

static inline u64 lru_pos_id(struct bpos pos)
{
	return pos.inode >> LRU_TIME_BITS;
}

static inline u64 lru_pos_time(struct bpos pos)
{
	return pos.inode & ~(~0ULL << LRU_TIME_BITS);
}

int bch2_lru_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
void bch2_lru_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_lru ((struct bkey_ops) {	\
	.key_invalid	= bch2_lru_invalid,	\
	.val_to_text	= bch2_lru_to_text,	\
})

int bch2_lru_del(struct btree_trans *, u16, u64, u64);
int bch2_lru_set(struct btree_trans *, u16, u64, u64);
int bch2_lru_change(struct btree_trans *, u16, u64, u64, u64);

int bch2_check_lrus(struct bch_fs *);

#endif /* _BCACHEFS_LRU_H */
