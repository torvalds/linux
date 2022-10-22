/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_INODE_H
#define _BCACHEFS_INODE_H

#include "bkey.h"
#include "opts.h"

extern const char * const bch2_inode_opts[];

int bch2_inode_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
int bch2_inode_v2_invalid(const struct bch_fs *, struct bkey_s_c, int, struct printbuf *);
void bch2_inode_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_inode (struct bkey_ops) {		\
	.key_invalid	= bch2_inode_invalid,		\
	.val_to_text	= bch2_inode_to_text,		\
	.trans_trigger	= bch2_trans_mark_inode,	\
	.atomic_trigger	= bch2_mark_inode,		\
}

#define bch2_bkey_ops_inode_v2 (struct bkey_ops) {	\
	.key_invalid	= bch2_inode_v2_invalid,	\
	.val_to_text	= bch2_inode_to_text,		\
	.trans_trigger	= bch2_trans_mark_inode,	\
	.atomic_trigger	= bch2_mark_inode,		\
}

static inline bool bkey_is_inode(const struct bkey *k)
{
	return  k->type == KEY_TYPE_inode ||
		k->type == KEY_TYPE_inode_v2;
}

int bch2_inode_generation_invalid(const struct bch_fs *, struct bkey_s_c,
				  int, struct printbuf *);
void bch2_inode_generation_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_inode_generation (struct bkey_ops) {	\
	.key_invalid	= bch2_inode_generation_invalid,	\
	.val_to_text	= bch2_inode_generation_to_text,	\
}

#if 0
typedef struct {
	u64			lo;
	u32			hi;
} __packed __aligned(4) u96;
#endif
typedef u64 u96;

struct bch_inode_unpacked {
	u64			bi_inum;
	u64			bi_journal_seq;
	__le64			bi_hash_seed;
	u32			bi_flags;
	u16			bi_mode;

#define x(_name, _bits)	u##_bits _name;
	BCH_INODE_FIELDS()
#undef  x
};

struct bkey_inode_buf {
	struct bkey_i_inode_v2	inode;

#define x(_name, _bits)		+ 8 + _bits / 8
	u8		_pad[0 + BCH_INODE_FIELDS()];
#undef  x
} __packed __aligned(8);

void bch2_inode_pack(struct bch_fs *, struct bkey_inode_buf *,
		     const struct bch_inode_unpacked *);
int bch2_inode_unpack(struct bkey_s_c, struct bch_inode_unpacked *);

void bch2_inode_unpacked_to_text(struct printbuf *, struct bch_inode_unpacked *);

int bch2_inode_peek(struct btree_trans *, struct btree_iter *,
		    struct bch_inode_unpacked *, subvol_inum, unsigned);
int bch2_inode_write(struct btree_trans *, struct btree_iter *,
		     struct bch_inode_unpacked *);

void bch2_inode_init_early(struct bch_fs *,
			   struct bch_inode_unpacked *);
void bch2_inode_init_late(struct bch_inode_unpacked *, u64,
			  uid_t, gid_t, umode_t, dev_t,
			  struct bch_inode_unpacked *);
void bch2_inode_init(struct bch_fs *, struct bch_inode_unpacked *,
		     uid_t, gid_t, umode_t, dev_t,
		     struct bch_inode_unpacked *);

int bch2_inode_create(struct btree_trans *, struct btree_iter *,
		      struct bch_inode_unpacked *, u32, u64);

int bch2_inode_rm(struct bch_fs *, subvol_inum);

int bch2_inode_find_by_inum_trans(struct btree_trans *, subvol_inum,
				  struct bch_inode_unpacked *);
int bch2_inode_find_by_inum(struct bch_fs *, subvol_inum,
			    struct bch_inode_unpacked *);

static inline struct bch_io_opts bch2_inode_opts_get(struct bch_inode_unpacked *inode)
{
	struct bch_io_opts ret = { 0 };

#define x(_name, _bits)					\
	if (inode->bi_##_name)						\
		opt_set(ret, _name, inode->bi_##_name - 1);
	BCH_INODE_OPTS()
#undef x
	return ret;
}

static inline void bch2_inode_opt_set(struct bch_inode_unpacked *inode,
				      enum inode_opt_id id, u64 v)
{
	switch (id) {
#define x(_name, ...)							\
	case Inode_opt_##_name:						\
		inode->bi_##_name = v;					\
		break;
	BCH_INODE_OPTS()
#undef x
	default:
		BUG();
	}
}

static inline u64 bch2_inode_opt_get(struct bch_inode_unpacked *inode,
				     enum inode_opt_id id)
{
	switch (id) {
#define x(_name, ...)							\
	case Inode_opt_##_name:						\
		return inode->bi_##_name;
	BCH_INODE_OPTS()
#undef x
	default:
		BUG();
	}
}

static inline struct bch_io_opts
io_opts(struct bch_fs *c, struct bch_inode_unpacked *inode)
{
	struct bch_io_opts opts = bch2_opts_to_inode_opts(c->opts);

	bch2_io_opts_apply(&opts, bch2_inode_opts_get(inode));
	return opts;
}

static inline u8 mode_to_type(umode_t mode)
{
	return (mode >> 12) & 15;
}

static inline u8 inode_d_type(struct bch_inode_unpacked *inode)
{
	return inode->bi_subvol ? DT_SUBVOL : mode_to_type(inode->bi_mode);
}

/* i_nlink: */

static inline unsigned nlink_bias(umode_t mode)
{
	return S_ISDIR(mode) ? 2 : 1;
}

static inline unsigned bch2_inode_nlink_get(struct bch_inode_unpacked *bi)
{
	return bi->bi_flags & BCH_INODE_UNLINKED
		  ? 0
		  : bi->bi_nlink + nlink_bias(bi->bi_mode);
}

static inline void bch2_inode_nlink_set(struct bch_inode_unpacked *bi,
					unsigned nlink)
{
	if (nlink) {
		bi->bi_nlink = nlink - nlink_bias(bi->bi_mode);
		bi->bi_flags &= ~BCH_INODE_UNLINKED;
	} else {
		bi->bi_nlink = 0;
		bi->bi_flags |= BCH_INODE_UNLINKED;
	}
}

int bch2_inode_nlink_inc(struct bch_inode_unpacked *);
void bch2_inode_nlink_dec(struct btree_trans *, struct bch_inode_unpacked *);

#endif /* _BCACHEFS_INODE_H */
