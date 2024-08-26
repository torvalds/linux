/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_IO_H
#define _BCACHEFS_BTREE_IO_H

#include "bkey_methods.h"
#include "bset.h"
#include "btree_locking.h"
#include "checksum.h"
#include "extents.h"
#include "io_write_types.h"

struct bch_fs;
struct btree_write;
struct btree;
struct btree_iter;
struct btree_node_read_all;

static inline void set_btree_node_dirty_acct(struct bch_fs *c, struct btree *b)
{
	if (!test_and_set_bit(BTREE_NODE_dirty, &b->flags))
		atomic_inc(&c->btree_cache.dirty);
}

static inline void clear_btree_node_dirty_acct(struct bch_fs *c, struct btree *b)
{
	if (test_and_clear_bit(BTREE_NODE_dirty, &b->flags))
		atomic_dec(&c->btree_cache.dirty);
}

static inline unsigned btree_ptr_sectors_written(struct bkey_s_c k)
{
	return k.k->type == KEY_TYPE_btree_ptr_v2
		? le16_to_cpu(bkey_s_c_to_btree_ptr_v2(k).v->sectors_written)
		: 0;
}

struct btree_read_bio {
	struct bch_fs		*c;
	struct btree		*b;
	struct btree_node_read_all *ra;
	u64			start_time;
	unsigned		have_ioref:1;
	unsigned		idx:7;
	struct extent_ptr_decoded	pick;
	struct work_struct	work;
	struct bio		bio;
};

struct btree_write_bio {
	struct work_struct	work;
	__BKEY_PADDED(key, BKEY_BTREE_PTR_VAL_U64s_MAX);
	void			*data;
	unsigned		data_bytes;
	unsigned		sector_offset;
	struct bch_write_bio	wbio;
};

void bch2_btree_node_io_unlock(struct btree *);
void bch2_btree_node_io_lock(struct btree *);
void __bch2_btree_node_wait_on_read(struct btree *);
void __bch2_btree_node_wait_on_write(struct btree *);
void bch2_btree_node_wait_on_read(struct btree *);
void bch2_btree_node_wait_on_write(struct btree *);

enum compact_mode {
	COMPACT_LAZY,
	COMPACT_ALL,
};

bool bch2_compact_whiteouts(struct bch_fs *, struct btree *,
			    enum compact_mode);

static inline bool should_compact_bset_lazy(struct btree *b,
					    struct bset_tree *t)
{
	unsigned total_u64s = bset_u64s(t);
	unsigned dead_u64s = bset_dead_u64s(b, t);

	return dead_u64s > 64 && dead_u64s * 3 > total_u64s;
}

static inline bool bch2_maybe_compact_whiteouts(struct bch_fs *c, struct btree *b)
{
	for_each_bset(b, t)
		if (should_compact_bset_lazy(b, t))
			return bch2_compact_whiteouts(c, b, COMPACT_LAZY);

	return false;
}

static inline struct nonce btree_nonce(struct bset *i, unsigned offset)
{
	return (struct nonce) {{
		[0] = cpu_to_le32(offset),
		[1] = ((__le32 *) &i->seq)[0],
		[2] = ((__le32 *) &i->seq)[1],
		[3] = ((__le32 *) &i->journal_seq)[0]^BCH_NONCE_BTREE,
	}};
}

static inline int bset_encrypt(struct bch_fs *c, struct bset *i, unsigned offset)
{
	struct nonce nonce = btree_nonce(i, offset);
	int ret;

	if (!offset) {
		struct btree_node *bn = container_of(i, struct btree_node, keys);
		unsigned bytes = (void *) &bn->keys - (void *) &bn->flags;

		ret = bch2_encrypt(c, BSET_CSUM_TYPE(i), nonce,
				   &bn->flags, bytes);
		if (ret)
			return ret;

		nonce = nonce_add(nonce, round_up(bytes, CHACHA_BLOCK_SIZE));
	}

	return bch2_encrypt(c, BSET_CSUM_TYPE(i), nonce, i->_data,
			    vstruct_end(i) - (void *) i->_data);
}

void bch2_btree_sort_into(struct bch_fs *, struct btree *, struct btree *);

void bch2_btree_node_drop_keys_outside_node(struct btree *);

void bch2_btree_build_aux_trees(struct btree *);
void bch2_btree_init_next(struct btree_trans *, struct btree *);

int bch2_btree_node_read_done(struct bch_fs *, struct bch_dev *,
			      struct btree *, bool, bool *);
void bch2_btree_node_read(struct btree_trans *, struct btree *, bool);
int bch2_btree_root_read(struct bch_fs *, enum btree_id,
			 const struct bkey_i *, unsigned);

bool bch2_btree_post_write_cleanup(struct bch_fs *, struct btree *);

enum btree_write_flags {
	__BTREE_WRITE_ONLY_IF_NEED = BTREE_WRITE_TYPE_BITS,
	__BTREE_WRITE_ALREADY_STARTED,
};
#define BTREE_WRITE_ONLY_IF_NEED	BIT(__BTREE_WRITE_ONLY_IF_NEED)
#define BTREE_WRITE_ALREADY_STARTED	BIT(__BTREE_WRITE_ALREADY_STARTED)

void __bch2_btree_node_write(struct bch_fs *, struct btree *, unsigned);
void bch2_btree_node_write(struct bch_fs *, struct btree *,
			   enum six_lock_type, unsigned);

static inline void btree_node_write_if_need(struct bch_fs *c, struct btree *b,
					    enum six_lock_type lock_held)
{
	bch2_btree_node_write(c, b, lock_held, BTREE_WRITE_ONLY_IF_NEED);
}

bool bch2_btree_flush_all_reads(struct bch_fs *);
bool bch2_btree_flush_all_writes(struct bch_fs *);

static inline void compat_bformat(unsigned level, enum btree_id btree_id,
				  unsigned version, unsigned big_endian,
				  int write, struct bkey_format *f)
{
	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id == BTREE_ID_inodes) {
		swap(f->bits_per_field[BKEY_FIELD_INODE],
		     f->bits_per_field[BKEY_FIELD_OFFSET]);
		swap(f->field_offset[BKEY_FIELD_INODE],
		     f->field_offset[BKEY_FIELD_OFFSET]);
	}

	if (version < bcachefs_metadata_version_snapshot &&
	    (level || btree_type_has_snapshots(btree_id))) {
		u64 max_packed =
			~(~0ULL << f->bits_per_field[BKEY_FIELD_SNAPSHOT]);

		f->field_offset[BKEY_FIELD_SNAPSHOT] = write
			? 0
			: cpu_to_le64(U32_MAX - max_packed);
	}
}

static inline void compat_bpos(unsigned level, enum btree_id btree_id,
			       unsigned version, unsigned big_endian,
			       int write, struct bpos *p)
{
	if (big_endian != CPU_BIG_ENDIAN)
		bch2_bpos_swab(p);

	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id == BTREE_ID_inodes)
		swap(p->inode, p->offset);
}

static inline void compat_btree_node(unsigned level, enum btree_id btree_id,
				     unsigned version, unsigned big_endian,
				     int write,
				     struct btree_node *bn)
{
	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id_is_extents(btree_id) &&
	    !bpos_eq(bn->min_key, POS_MIN) &&
	    write)
		bn->min_key = bpos_nosnap_predecessor(bn->min_key);

	if (version < bcachefs_metadata_version_snapshot &&
	    write)
		bn->max_key.snapshot = 0;

	compat_bpos(level, btree_id, version, big_endian, write, &bn->min_key);
	compat_bpos(level, btree_id, version, big_endian, write, &bn->max_key);

	if (version < bcachefs_metadata_version_snapshot &&
	    !write)
		bn->max_key.snapshot = U32_MAX;

	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id_is_extents(btree_id) &&
	    !bpos_eq(bn->min_key, POS_MIN) &&
	    !write)
		bn->min_key = bpos_nosnap_successor(bn->min_key);
}

void bch2_btree_write_stats_to_text(struct printbuf *, struct bch_fs *);

#endif /* _BCACHEFS_BTREE_IO_H */
