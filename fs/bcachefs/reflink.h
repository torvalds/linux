/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REFLINK_H
#define _BCACHEFS_REFLINK_H

int bch2_reflink_p_invalid(const struct bch_fs *, struct bkey_s_c,
			   unsigned, struct printbuf *);
void bch2_reflink_p_to_text(struct printbuf *, struct bch_fs *,
			    struct bkey_s_c);
bool bch2_reflink_p_merge(struct bch_fs *, struct bkey_s, struct bkey_s_c);

#define bch2_bkey_ops_reflink_p ((struct bkey_ops) {		\
	.key_invalid	= bch2_reflink_p_invalid,		\
	.val_to_text	= bch2_reflink_p_to_text,		\
	.key_merge	= bch2_reflink_p_merge,			\
	.trans_trigger	= bch2_trans_mark_reflink_p,		\
	.atomic_trigger	= bch2_mark_reflink_p,			\
	.min_val_size	= 16,					\
})

int bch2_reflink_v_invalid(const struct bch_fs *, struct bkey_s_c,
			   unsigned, struct printbuf *);
void bch2_reflink_v_to_text(struct printbuf *, struct bch_fs *,
			    struct bkey_s_c);
int bch2_trans_mark_reflink_v(struct btree_trans *, enum btree_id, unsigned,
			      struct bkey_s_c, struct bkey_i *, unsigned);

#define bch2_bkey_ops_reflink_v ((struct bkey_ops) {		\
	.key_invalid	= bch2_reflink_v_invalid,		\
	.val_to_text	= bch2_reflink_v_to_text,		\
	.swab		= bch2_ptr_swab,			\
	.trans_trigger	= bch2_trans_mark_reflink_v,		\
	.atomic_trigger	= bch2_mark_extent,			\
	.min_val_size	= 8,					\
})

int bch2_indirect_inline_data_invalid(const struct bch_fs *, struct bkey_s_c,
				      unsigned, struct printbuf *);
void bch2_indirect_inline_data_to_text(struct printbuf *,
				struct bch_fs *, struct bkey_s_c);
int bch2_trans_mark_indirect_inline_data(struct btree_trans *,
					 enum btree_id, unsigned,
			      struct bkey_s_c, struct bkey_i *,
			      unsigned);

#define bch2_bkey_ops_indirect_inline_data ((struct bkey_ops) {	\
	.key_invalid	= bch2_indirect_inline_data_invalid,	\
	.val_to_text	= bch2_indirect_inline_data_to_text,	\
	.trans_trigger	= bch2_trans_mark_indirect_inline_data,	\
	.min_val_size	= 8,					\
})

static inline const __le64 *bkey_refcount_c(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_reflink_v:
		return &bkey_s_c_to_reflink_v(k).v->refcount;
	case KEY_TYPE_indirect_inline_data:
		return &bkey_s_c_to_indirect_inline_data(k).v->refcount;
	default:
		return NULL;
	}
}

static inline __le64 *bkey_refcount(struct bkey_i *k)
{
	switch (k->k.type) {
	case KEY_TYPE_reflink_v:
		return &bkey_i_to_reflink_v(k)->v.refcount;
	case KEY_TYPE_indirect_inline_data:
		return &bkey_i_to_indirect_inline_data(k)->v.refcount;
	default:
		return NULL;
	}
}

s64 bch2_remap_range(struct bch_fs *, subvol_inum, u64,
		     subvol_inum, u64, u64, u64, s64 *);

#endif /* _BCACHEFS_REFLINK_H */
