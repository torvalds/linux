/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REFLINK_H
#define _BCACHEFS_REFLINK_H

const char *bch2_reflink_p_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_reflink_p_to_text(struct printbuf *, struct bch_fs *,
			    struct bkey_s_c);
enum merge_result bch2_reflink_p_merge(struct bch_fs *,
				       struct bkey_s, struct bkey_s);

#define bch2_bkey_ops_reflink_p (struct bkey_ops) {		\
	.key_invalid	= bch2_reflink_p_invalid,		\
	.val_to_text	= bch2_reflink_p_to_text,		\
	.key_merge	= bch2_reflink_p_merge,		\
}

const char *bch2_reflink_v_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_reflink_v_to_text(struct printbuf *, struct bch_fs *,
			    struct bkey_s_c);


#define bch2_bkey_ops_reflink_v (struct bkey_ops) {		\
	.key_invalid	= bch2_reflink_v_invalid,		\
	.val_to_text	= bch2_reflink_v_to_text,		\
}

#ifndef NO_BCACHEFS_FS
s64 bch2_remap_range(struct bch_fs *, struct bch_inode_info *,
		     struct bpos, struct bpos, u64, u64);
#endif /* NO_BCACHEFS_FS */

#endif /* _BCACHEFS_REFLINK_H */
