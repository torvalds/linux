/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_LRU_H
#define _BCACHEFS_LRU_H

int bch2_lru_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
void bch2_lru_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_lru ((struct bkey_ops) {	\
	.key_invalid	= bch2_lru_invalid,	\
	.val_to_text	= bch2_lru_to_text,	\
})

int bch2_lru_delete(struct btree_trans *, u64, u64, u64, struct bkey_s_c);
int bch2_lru_set(struct btree_trans *, u64, u64, u64 *);
int bch2_lru_change(struct btree_trans *, u64, u64, u64, u64 *, struct bkey_s_c);

int bch2_check_lrus(struct bch_fs *);

#endif /* _BCACHEFS_LRU_H */
