/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_WRITE_BUFFER_H
#define _BCACHEFS_BTREE_WRITE_BUFFER_H

#include "bkey.h"
#include "disk_accounting.h"

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
bool bch2_btree_write_buffer_flush_going_ro(struct bch_fs *);
int bch2_btree_write_buffer_flush_nocheck_rw(struct btree_trans *);
int bch2_btree_write_buffer_tryflush(struct btree_trans *);

struct bkey_buf;
int bch2_btree_write_buffer_maybe_flush(struct btree_trans *, struct bkey_s_c, struct bkey_buf *);

struct journal_keys_to_wb {
	struct btree_write_buffer_keys	*wb;
	size_t				room;
	u64				seq;
};

static inline int wb_key_cmp(const void *_l, const void *_r)
{
	const struct btree_write_buffered_key *l = _l;
	const struct btree_write_buffered_key *r = _r;

	return cmp_int(l->btree, r->btree) ?: bpos_cmp(l->k.k.p, r->k.k.p);
}

int bch2_accounting_key_to_wb_slowpath(struct bch_fs *,
			      enum btree_id, struct bkey_i_accounting *);

static inline int bch2_accounting_key_to_wb(struct bch_fs *c,
			     enum btree_id btree, struct bkey_i_accounting *k)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	struct btree_write_buffered_key search;
	search.btree = btree;
	search.k.k.p = k->k.p;

	unsigned idx = eytzinger0_find(wb->accounting.data, wb->accounting.nr,
			sizeof(wb->accounting.data[0]),
			wb_key_cmp, &search);

	if (idx >= wb->accounting.nr)
		return bch2_accounting_key_to_wb_slowpath(c, btree, k);

	struct bkey_i_accounting *dst = bkey_i_to_accounting(&wb->accounting.data[idx].k);
	bch2_accounting_accumulate(dst, accounting_i_to_s_c(k));
	return 0;
}

int bch2_journal_key_to_wb_slowpath(struct bch_fs *,
			     struct journal_keys_to_wb *,
			     enum btree_id, struct bkey_i *);

static inline int __bch2_journal_key_to_wb(struct bch_fs *c,
			     struct journal_keys_to_wb *dst,
			     enum btree_id btree, struct bkey_i *k)
{
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

static inline int bch2_journal_key_to_wb(struct bch_fs *c,
			     struct journal_keys_to_wb *dst,
			     enum btree_id btree, struct bkey_i *k)
{
	EBUG_ON(!dst->seq);

	return k->k.type == KEY_TYPE_accounting
		? bch2_accounting_key_to_wb(c, btree, bkey_i_to_accounting(k))
		: __bch2_journal_key_to_wb(c, dst, btree, k);
}

void bch2_journal_keys_to_write_buffer_start(struct bch_fs *, struct journal_keys_to_wb *, u64);
int bch2_journal_keys_to_write_buffer_end(struct bch_fs *, struct journal_keys_to_wb *);

int bch2_btree_write_buffer_resize(struct bch_fs *, size_t);
void bch2_fs_btree_write_buffer_exit(struct bch_fs *);
int bch2_fs_btree_write_buffer_init(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_WRITE_BUFFER_H */
