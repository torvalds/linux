/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_INODE_H
#define _BCACHEFS_INODE_H

#include "opts.h"

#include <linux/math64.h>

const char *bch2_inode_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_inode_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_inode (struct bkey_ops) {		\
	.key_invalid	= bch2_inode_invalid,		\
	.val_to_text	= bch2_inode_to_text,		\
}

const char *bch2_inode_generation_invalid(const struct bch_fs *,
					  struct bkey_s_c);
void bch2_inode_generation_to_text(struct printbuf *, struct bch_fs *,
				   struct bkey_s_c);

#define bch2_bkey_ops_inode_generation (struct bkey_ops) {	\
	.key_invalid	= bch2_inode_generation_invalid,	\
	.val_to_text	= bch2_inode_generation_to_text,	\
}

struct bch_inode_unpacked {
	u64			bi_inum;
	__le64			bi_hash_seed;
	u32			bi_flags;
	u16			bi_mode;

#define x(_name, _bits)	u##_bits _name;
	BCH_INODE_FIELDS()
#undef  x
};

struct bkey_inode_buf {
	struct bkey_i_inode	inode;

#define x(_name, _bits)		+ 8 + _bits / 8
	u8		_pad[0 + BCH_INODE_FIELDS()];
#undef  x
} __attribute__((packed, aligned(8)));

void bch2_inode_pack(struct bkey_inode_buf *, const struct bch_inode_unpacked *);
int bch2_inode_unpack(struct bkey_s_c_inode, struct bch_inode_unpacked *);

void bch2_inode_init(struct bch_fs *, struct bch_inode_unpacked *,
		     uid_t, gid_t, umode_t, dev_t,
		     struct bch_inode_unpacked *);

int __bch2_inode_create(struct btree_trans *,
			struct bch_inode_unpacked *,
			u64, u64, u64 *);
int bch2_inode_create(struct bch_fs *, struct bch_inode_unpacked *,
		      u64, u64, u64 *);

int bch2_inode_rm(struct bch_fs *, u64);

int bch2_inode_find_by_inum(struct bch_fs *, u64,
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

static inline void __bch2_inode_opt_set(struct bch_inode_unpacked *inode,
					enum bch_opt_id id, u64 v)
{
	switch (id) {
#define x(_name, ...)					\
	case Opt_##_name:						\
		inode->bi_##_name = v;					\
		break;
	BCH_INODE_OPTS()
#undef x
	default:
		BUG();
	}
}

static inline void bch2_inode_opt_set(struct bch_inode_unpacked *inode,
				      enum bch_opt_id id, u64 v)
{
	return __bch2_inode_opt_set(inode, id, v + 1);
}

static inline void bch2_inode_opt_clear(struct bch_inode_unpacked *inode,
					enum bch_opt_id id)
{
	return __bch2_inode_opt_set(inode, id, 0);
}

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_inode_pack_test(void);
#else
static inline void bch2_inode_pack_test(void) {}
#endif

#endif /* _BCACHEFS_INODE_H */
