/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_IO_H
#define _BCACHEFS_BTREE_IO_H

#include "bkey_methods.h"
#include "bset.h"
#include "btree_locking.h"
#include "checksum.h"
#include "extents.h"
#include "io_types.h"

struct bch_fs;
struct btree_write;
struct btree;
struct btree_iter;

static inline bool btree_node_dirty(struct btree *b)
{
	return test_bit(BTREE_NODE_dirty, &b->flags);
}

static inline void set_btree_node_dirty(struct bch_fs *c, struct btree *b)
{
	if (!test_and_set_bit(BTREE_NODE_dirty, &b->flags))
		atomic_inc(&c->btree_cache.dirty);
}

static inline void clear_btree_node_dirty(struct bch_fs *c, struct btree *b)
{
	if (test_and_clear_bit(BTREE_NODE_dirty, &b->flags))
		atomic_dec(&c->btree_cache.dirty);
}

struct btree_read_bio {
	struct bch_fs		*c;
	u64			start_time;
	unsigned		have_ioref:1;
	struct extent_ptr_decoded	pick;
	struct work_struct	work;
	struct bio		bio;
};

struct btree_write_bio {
	struct work_struct	work;
	void			*data;
	unsigned		bytes;
	struct bch_write_bio	wbio;
};

static inline void btree_node_io_unlock(struct btree *b)
{
	EBUG_ON(!btree_node_write_in_flight(b));
	clear_btree_node_write_in_flight(b);
	wake_up_bit(&b->flags, BTREE_NODE_write_in_flight);
}

static inline void btree_node_io_lock(struct btree *b)
{
	wait_on_bit_lock_io(&b->flags, BTREE_NODE_write_in_flight,
			    TASK_UNINTERRUPTIBLE);
}

static inline void btree_node_wait_on_io(struct btree *b)
{
	wait_on_bit_io(&b->flags, BTREE_NODE_write_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

static inline bool btree_node_may_write(struct btree *b)
{
	return list_empty_careful(&b->write_blocked) &&
		(!b->written || !b->will_make_reachable);
}

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
	struct bset_tree *t;

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

static inline void bset_encrypt(struct bch_fs *c, struct bset *i, unsigned offset)
{
	struct nonce nonce = btree_nonce(i, offset);

	if (!offset) {
		struct btree_node *bn = container_of(i, struct btree_node, keys);
		unsigned bytes = (void *) &bn->keys - (void *) &bn->flags;

		bch2_encrypt(c, BSET_CSUM_TYPE(i), nonce, &bn->flags,
			     bytes);

		nonce = nonce_add(nonce, round_up(bytes, CHACHA_BLOCK_SIZE));
	}

	bch2_encrypt(c, BSET_CSUM_TYPE(i), nonce, i->_data,
		     vstruct_end(i) - (void *) i->_data);
}

void bch2_btree_sort_into(struct bch_fs *, struct btree *, struct btree *);

void bch2_btree_build_aux_trees(struct btree *);
void bch2_btree_init_next(struct bch_fs *, struct btree *,
			 struct btree_iter *);

int bch2_btree_node_read_done(struct bch_fs *, struct bch_dev *,
			      struct btree *, bool);
void bch2_btree_node_read(struct bch_fs *, struct btree *, bool);
int bch2_btree_root_read(struct bch_fs *, enum btree_id,
			 const struct bkey_i *, unsigned);

void bch2_btree_complete_write(struct bch_fs *, struct btree *,
			      struct btree_write *);
void bch2_btree_write_error_work(struct work_struct *);

void __bch2_btree_node_write(struct bch_fs *, struct btree *,
			    enum six_lock_type);
bool bch2_btree_post_write_cleanup(struct bch_fs *, struct btree *);

void bch2_btree_node_write(struct bch_fs *, struct btree *,
			  enum six_lock_type);

static inline void btree_node_write_if_need(struct bch_fs *c, struct btree *b,
					    enum six_lock_type lock_held)
{
	while (b->written &&
	       btree_node_need_write(b) &&
	       btree_node_may_write(b)) {
		if (!btree_node_write_in_flight(b)) {
			bch2_btree_node_write(c, b, lock_held);
			break;
		}

		six_unlock_type(&b->c.lock, lock_held);
		btree_node_wait_on_io(b);
		btree_node_lock_type(c, b, lock_held);
	}
}

#define bch2_btree_node_write_cond(_c, _b, cond)			\
do {									\
	unsigned long old, new, v = READ_ONCE((_b)->flags);		\
									\
	do {								\
		old = new = v;						\
									\
		if (!(old & (1 << BTREE_NODE_dirty)) || !(cond))	\
			break;						\
									\
		new |= (1 << BTREE_NODE_need_write);			\
	} while ((v = cmpxchg(&(_b)->flags, old, new)) != old);		\
									\
	btree_node_write_if_need(_c, _b, SIX_LOCK_read);		\
} while (0)

void bch2_btree_flush_all_reads(struct bch_fs *);
void bch2_btree_flush_all_writes(struct bch_fs *);
void bch2_dirty_btree_nodes_to_text(struct printbuf *, struct bch_fs *);

static inline void compat_bformat(unsigned level, enum btree_id btree_id,
				 unsigned version, unsigned big_endian,
				 int write, struct bkey_format *f)
{
	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id == BTREE_ID_INODES) {
		swap(f->bits_per_field[BKEY_FIELD_INODE],
		     f->bits_per_field[BKEY_FIELD_OFFSET]);
		swap(f->field_offset[BKEY_FIELD_INODE],
		     f->field_offset[BKEY_FIELD_OFFSET]);
	}
}

static inline void compat_bpos(unsigned level, enum btree_id btree_id,
			       unsigned version, unsigned big_endian,
			       int write, struct bpos *p)
{
	if (big_endian != CPU_BIG_ENDIAN)
		bch2_bpos_swab(p);

	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id == BTREE_ID_INODES)
		swap(p->inode, p->offset);
}

static inline void compat_btree_node(unsigned level, enum btree_id btree_id,
				     unsigned version, unsigned big_endian,
				     int write,
				     struct btree_node *bn)
{
	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_node_type_is_extents(btree_id) &&
	    bkey_cmp(bn->min_key, POS_MIN) &&
	    write)
		bn->min_key = bkey_predecessor(bn->min_key);

	compat_bpos(level, btree_id, version, big_endian, write, &bn->min_key);
	compat_bpos(level, btree_id, version, big_endian, write, &bn->max_key);

	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_node_type_is_extents(btree_id) &&
	    bkey_cmp(bn->min_key, POS_MIN) &&
	    !write)
		bn->min_key = bkey_successor(bn->min_key);
}

#endif /* _BCACHEFS_BTREE_IO_H */
