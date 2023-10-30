/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BBPOS_H
#define _BCACHEFS_BBPOS_H

#include "bkey_methods.h"

struct bbpos {
	enum btree_id		btree;
	struct bpos		pos;
};

static inline struct bbpos BBPOS(enum btree_id btree, struct bpos pos)
{
	return (struct bbpos) { btree, pos };
}

#define BBPOS_MIN	BBPOS(0, POS_MIN)
#define BBPOS_MAX	BBPOS(BTREE_ID_NR - 1, POS_MAX)

static inline int bbpos_cmp(struct bbpos l, struct bbpos r)
{
	return cmp_int(l.btree, r.btree) ?: bpos_cmp(l.pos, r.pos);
}

static inline struct bbpos bbpos_successor(struct bbpos pos)
{
	if (bpos_cmp(pos.pos, SPOS_MAX)) {
		pos.pos = bpos_successor(pos.pos);
		return pos;
	}

	if (pos.btree != BTREE_ID_NR) {
		pos.btree++;
		pos.pos = POS_MIN;
		return pos;
	}

	BUG();
}

static inline void bch2_bbpos_to_text(struct printbuf *out, struct bbpos pos)
{
	prt_str(out, bch2_btree_ids[pos.btree]);
	prt_char(out, ':');
	bch2_bpos_to_text(out, pos.pos);
}

#endif /* _BCACHEFS_BBPOS_H */
