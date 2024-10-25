/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_INODE_H
#define _BCACHEFS_INODE_H

#include "bkey.h"
#include "bkey_methods.h"
#include "opts.h"
#include "snapshot.h"

enum bch_validate_flags;
extern const char * const bch2_inode_opts[];

int bch2_inode_validate(struct bch_fs *, struct bkey_s_c,
		       enum bch_validate_flags);
int bch2_inode_v2_validate(struct bch_fs *, struct bkey_s_c,
			  enum bch_validate_flags);
int bch2_inode_v3_validate(struct bch_fs *, struct bkey_s_c,
			  enum bch_validate_flags);
void bch2_inode_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

int __bch2_inode_has_child_snapshots(struct btree_trans *, struct bpos);

static inline int bch2_inode_has_child_snapshots(struct btree_trans *trans, struct bpos pos)
{
	return bch2_snapshot_is_leaf(trans->c, pos.snapshot) <= 0
		? __bch2_inode_has_child_snapshots(trans, pos)
		: 0;
}

int bch2_trigger_inode(struct btree_trans *, enum btree_id, unsigned,
		       struct bkey_s_c, struct bkey_s,
		       enum btree_iter_update_trigger_flags);

#define bch2_bkey_ops_inode ((struct bkey_ops) {	\
	.key_validate	= bch2_inode_validate,		\
	.val_to_text	= bch2_inode_to_text,		\
	.trigger	= bch2_trigger_inode,		\
	.min_val_size	= 16,				\
})

#define bch2_bkey_ops_inode_v2 ((struct bkey_ops) {	\
	.key_validate	= bch2_inode_v2_validate,	\
	.val_to_text	= bch2_inode_to_text,		\
	.trigger	= bch2_trigger_inode,		\
	.min_val_size	= 32,				\
})

#define bch2_bkey_ops_inode_v3 ((struct bkey_ops) {	\
	.key_validate	= bch2_inode_v3_validate,	\
	.val_to_text	= bch2_inode_to_text,		\
	.trigger	= bch2_trigger_inode,		\
	.min_val_size	= 48,				\
})

static inline bool bkey_is_inode(const struct bkey *k)
{
	return  k->type == KEY_TYPE_inode ||
		k->type == KEY_TYPE_inode_v2 ||
		k->type == KEY_TYPE_inode_v3;
}

int bch2_inode_generation_validate(struct bch_fs *, struct bkey_s_c,
				  enum bch_validate_flags);
void bch2_inode_generation_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_inode_generation ((struct bkey_ops) {	\
	.key_validate	= bch2_inode_generation_validate,	\
	.val_to_text	= bch2_inode_generation_to_text,	\
	.min_val_size	= 8,					\
})

#if 0
typedef struct {
	u64			lo;
	u32			hi;
} __packed __aligned(4) u96;
#endif
typedef u64 u96;

struct bch_inode_unpacked {
	u64			bi_inum;
	u32			bi_snapshot;
	u64			bi_journal_seq;
	__le64			bi_hash_seed;
	u64			bi_size;
	u64			bi_sectors;
	u64			bi_version;
	u32			bi_flags;
	u16			bi_mode;

#define x(_name, _bits)	u##_bits _name;
	BCH_INODE_FIELDS_v3()
#undef  x
};
BITMASK(INODE_STR_HASH,	struct bch_inode_unpacked, bi_flags, 20, 24);

struct bkey_inode_buf {
	struct bkey_i_inode_v3	inode;

#define x(_name, _bits)		+ 8 + _bits / 8
	u8		_pad[0 + BCH_INODE_FIELDS_v3()];
#undef  x
};

void bch2_inode_pack(struct bkey_inode_buf *, const struct bch_inode_unpacked *);
int bch2_inode_unpack(struct bkey_s_c, struct bch_inode_unpacked *);
struct bkey_i *bch2_inode_to_v3(struct btree_trans *, struct bkey_i *);

void bch2_inode_unpacked_to_text(struct printbuf *, struct bch_inode_unpacked *);

int __bch2_inode_peek(struct btree_trans *, struct btree_iter *,
		      struct bch_inode_unpacked *, subvol_inum, unsigned, bool);

static inline int bch2_inode_peek_nowarn(struct btree_trans *trans,
					 struct btree_iter *iter,
					 struct bch_inode_unpacked *inode,
					 subvol_inum inum, unsigned flags)
{
	return __bch2_inode_peek(trans, iter, inode, inum, flags, false);
}

static inline int bch2_inode_peek(struct btree_trans *trans,
				  struct btree_iter *iter,
				  struct bch_inode_unpacked *inode,
				  subvol_inum inum, unsigned flags)
{
	return __bch2_inode_peek(trans, iter, inode, inum, flags, true);
	int ret = bch2_inode_peek_nowarn(trans, iter, inode, inum, flags);
	return ret;
}

int bch2_inode_write_flags(struct btree_trans *, struct btree_iter *,
		     struct bch_inode_unpacked *, enum btree_iter_update_trigger_flags);

static inline int bch2_inode_write(struct btree_trans *trans,
		     struct btree_iter *iter,
		     struct bch_inode_unpacked *inode)
{
	return bch2_inode_write_flags(trans, iter, inode, 0);
}

int __bch2_fsck_write_inode(struct btree_trans *, struct bch_inode_unpacked *);
int bch2_fsck_write_inode(struct btree_trans *, struct bch_inode_unpacked *);

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

int bch2_inode_find_by_inum_nowarn_trans(struct btree_trans *,
				  subvol_inum,
				  struct bch_inode_unpacked *);
int bch2_inode_find_by_inum_trans(struct btree_trans *, subvol_inum,
				  struct bch_inode_unpacked *);
int bch2_inode_find_by_inum(struct bch_fs *, subvol_inum,
			    struct bch_inode_unpacked *);

#define inode_opt_get(_c, _inode, _name)			\
	((_inode)->bi_##_name ? (_inode)->bi_##_name - 1 : (_c)->opts._name)

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

static inline u8 mode_to_type(umode_t mode)
{
	return (mode >> 12) & 15;
}

static inline u8 inode_d_type(struct bch_inode_unpacked *inode)
{
	return inode->bi_subvol ? DT_SUBVOL : mode_to_type(inode->bi_mode);
}

static inline u32 bch2_inode_flags(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_inode:
		return le32_to_cpu(bkey_s_c_to_inode(k).v->bi_flags);
	case KEY_TYPE_inode_v2:
		return le64_to_cpu(bkey_s_c_to_inode_v2(k).v->bi_flags);
	case KEY_TYPE_inode_v3:
		return le64_to_cpu(bkey_s_c_to_inode_v3(k).v->bi_flags);
	default:
		return 0;
	}
}

/* i_nlink: */

static inline unsigned nlink_bias(umode_t mode)
{
	return S_ISDIR(mode) ? 2 : 1;
}

static inline unsigned bch2_inode_nlink_get(struct bch_inode_unpacked *bi)
{
	return bi->bi_flags & BCH_INODE_unlinked
		  ? 0
		  : bi->bi_nlink + nlink_bias(bi->bi_mode);
}

static inline void bch2_inode_nlink_set(struct bch_inode_unpacked *bi,
					unsigned nlink)
{
	if (nlink) {
		bi->bi_nlink = nlink - nlink_bias(bi->bi_mode);
		bi->bi_flags &= ~BCH_INODE_unlinked;
	} else {
		bi->bi_nlink = 0;
		bi->bi_flags |= BCH_INODE_unlinked;
	}
}

int bch2_inode_nlink_inc(struct bch_inode_unpacked *);
void bch2_inode_nlink_dec(struct btree_trans *, struct bch_inode_unpacked *);

static inline bool bch2_inode_should_have_bp(struct bch_inode_unpacked *inode)
{
	bool inode_has_bp = inode->bi_dir || inode->bi_dir_offset;

	return S_ISDIR(inode->bi_mode) ||
		(!inode->bi_nlink && inode_has_bp);
}

struct bch_opts bch2_inode_opts_to_opts(struct bch_inode_unpacked *);
void bch2_inode_opts_get(struct bch_io_opts *, struct bch_fs *,
			 struct bch_inode_unpacked *);
int bch2_inum_opts_get(struct btree_trans*, subvol_inum, struct bch_io_opts *);

int bch2_inode_rm_snapshot(struct btree_trans *, u64, u32);
int bch2_delete_dead_inodes(struct bch_fs *);

#endif /* _BCACHEFS_INODE_H */
