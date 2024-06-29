/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_WRITE_BUFFER_H
#define _BCACHEFS_BTREE_WRITE_BUFFER_H

#include "bkey.h"

static inline bool bch2_btree_write_buffer_should_flush(struct bch_fs *c)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	return wb->inc.keys.nr + wb->flushing.keys.nr > wb->inc.keys.size / 4;
}

static inline bool bch2_btree_write_buffer_must_wait(struct bch_fs *c)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	return wb->inc.keys.nr > wb->inc.keys.size * 3 / 4;
}

struct btree_trans;
int bch2_btree_write_buffer_flush_sync(struct btree_trans *);
int bch2_btree_write_buffer_flush_nocheck_rw(struct btree_trans *);
int bch2_btree_write_buffer_tryflush(struct btree_trans *);

struct bkey_buf;
int bch2_btree_write_buffer_maybe_flush(struct btree_trans *, struct bkey_s_c, struct bkey_buf *);

struct journal_keys_to_wb {
	struct btree_write_buffer_keys	*wb;
	size_t				room;
	u64				seq;
};

int bch2_journal_key_to_wb_slowpath(struct bch_fs *,
			     struct journal_keys_to_wb *,
			     enum btree_id, struct bkey_i *);

static inline int bch2_journal_key_to_wb(struct bch_fs *c,
			     struct journal_keys_to_wb *dst,
			     enum btree_id btree, struct bkey_i *k)
{
	EBUG_ON(!dst->seq);

	if (unlikely(!dst->room))
		return bch2_journal_key_to_wb_slowpath(c, dst, btree, k);

	struct btree_write_buffered_key *wb_k = &darray_top(dst->wb->keys);
	wb_k->journal_seq	= dst->seq;
	wb_k->btree		= btree;
	bkey_copy(&wb_k->k, k);
	dst->wb->keys.nr++;
	dst->room--;
	return 0;
}

void bch2_journal_keys_to_write_buffer_start(struct bch_fs *, struct journal_keys_to_wb *, u64);
void bch2_journal_keys_to_write_buffer_end(struct bch_fs *, struct journal_keys_to_wb *);

int bch2_btree_write_buffer_resize(struct bch_fs *, size_t);
void bch2_fs_btree_write_buffer_exit(struct bch_fs *);
int bch2_fs_btree_write_buffer_init(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_WRITE_BUFFER_H */
