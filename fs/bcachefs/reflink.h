/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REFLINK_H
#define _BCACHEFS_REFLINK_H

enum bch_validate_flags;

int bch2_reflink_p_validate(struct bch_fs *, struct bkey_s_c, enum bch_validate_flags);
void bch2_reflink_p_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
bool bch2_reflink_p_merge(struct bch_fs *, struct bkey_s, struct bkey_s_c);
int bch2_trigger_reflink_p(struct btree_trans *, enum btree_id, unsigned,
			   struct bkey_s_c, struct bkey_s,
			   enum btree_iter_update_trigger_flags);

#define bch2_bkey_ops_reflink_p ((struct bkey_ops) {		\
	.key_validate	= bch2_reflink_p_validate,		\
	.val_to_text	= bch2_reflink_p_to_text,		\
	.key_merge	= bch2_reflink_p_merge,			\
	.trigger	= bch2_trigger_reflink_p,		\
	.min_val_size	= 16,					\
})

int bch2_reflink_v_validate(struct bch_fs *, struct bkey_s_c, enum bch_validate_flags);
void bch2_reflink_v_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
int bch2_trigger_reflink_v(struct btree_trans *, enum btree_id, unsigned,
			   struct bkey_s_c, struct bkey_s,
			   enum btree_iter_update_trigger_flags);

#define bch2_bkey_ops_reflink_v ((struct bkey_ops) {		\
	.key_validate	= bch2_reflink_v_validate,		\
	.val_to_text	= bch2_reflink_v_to_text,		\
	.swab		= bch2_ptr_swab,			\
	.trigger	= bch2_trigger_reflink_v,		\
	.min_val_size	= 8,					\
})

int bch2_indirect_inline_data_validate(struct bch_fs *, struct bkey_s_c,
				      enum bch_validate_flags);
void bch2_indirect_inline_data_to_text(struct printbuf *,
				struct bch_fs *, struct bkey_s_c);
int bch2_trigger_indirect_inline_data(struct btree_trans *,
					 enum btree_id, unsigned,
			      struct bkey_s_c, struct bkey_s,
			      enum btree_iter_update_trigger_flags);

#define bch2_bkey_ops_indirect_inline_data ((struct bkey_ops) {	\
	.key_validate	= bch2_indirect_inline_data_validate,	\
	.val_to_text	= bch2_indirect_inline_data_to_text,	\
	.trigger	= bch2_trigger_indirect_inline_data,	\
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

static inline __le64 *bkey_refcount(struct bkey_s k)
{
	switch (k.k->type) {
	case KEY_TYPE_reflink_v:
		return &bkey_s_to_reflink_v(k).v->refcount;
	case KEY_TYPE_indirect_inline_data:
		return &bkey_s_to_indirect_inline_data(k).v->refcount;
	default:
		return NULL;
	}
}

s64 bch2_remap_range(struct bch_fs *, subvol_inum, u64,
		     subvol_inum, u64, u64, u64, s64 *);

#endif /* _BCACHEFS_REFLINK_H */
