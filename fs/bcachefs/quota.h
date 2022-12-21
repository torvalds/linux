/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_QUOTA_H
#define _BCACHEFS_QUOTA_H

#include "inode.h"
#include "quota_types.h"

extern const struct bch_sb_field_ops bch_sb_field_ops_quota;

int bch2_quota_invalid(const struct bch_fs *, struct bkey_s_c, unsigned, struct printbuf *);
void bch2_quota_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_quota ((struct bkey_ops) {	\
	.key_invalid	= bch2_quota_invalid,		\
	.val_to_text	= bch2_quota_to_text,		\
})

static inline struct bch_qid bch_qid(struct bch_inode_unpacked *u)
{
	return (struct bch_qid) {
		.q[QTYP_USR] = u->bi_uid,
		.q[QTYP_GRP] = u->bi_gid,
		.q[QTYP_PRJ] = u->bi_project ? u->bi_project - 1 : 0,
	};
}

static inline unsigned enabled_qtypes(struct bch_fs *c)
{
	return ((c->opts.usrquota << QTYP_USR)|
		(c->opts.grpquota << QTYP_GRP)|
		(c->opts.prjquota << QTYP_PRJ));
}

#ifdef CONFIG_BCACHEFS_QUOTA

int bch2_quota_acct(struct bch_fs *, struct bch_qid, enum quota_counters,
		    s64, enum quota_acct_mode);

int bch2_quota_transfer(struct bch_fs *, unsigned, struct bch_qid,
			struct bch_qid, u64, enum quota_acct_mode);

void bch2_fs_quota_exit(struct bch_fs *);
void bch2_fs_quota_init(struct bch_fs *);
int bch2_fs_quota_read(struct bch_fs *);

extern const struct quotactl_ops bch2_quotactl_operations;

#else

static inline int bch2_quota_acct(struct bch_fs *c, struct bch_qid qid,
				  enum quota_counters counter, s64 v,
				  enum quota_acct_mode mode)
{
	return 0;
}

static inline int bch2_quota_transfer(struct bch_fs *c, unsigned qtypes,
				      struct bch_qid dst,
				      struct bch_qid src, u64 space,
				      enum quota_acct_mode mode)
{
	return 0;
}

static inline void bch2_fs_quota_exit(struct bch_fs *c) {}
static inline void bch2_fs_quota_init(struct bch_fs *c) {}
static inline int bch2_fs_quota_read(struct bch_fs *c) { return 0; }

#endif

#endif /* _BCACHEFS_QUOTA_H */
