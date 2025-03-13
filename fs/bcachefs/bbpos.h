/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BBPOS_H
#define _BCACHEFS_BBPOS_H

#include "bbpos_types.h"
#include "bkey_methods.h"
#include "btree_cache.h"

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
	bch2_btree_id_to_text(out, pos.btree);
	prt_char(out, ':');
	bch2_bpos_to_text(out, pos.pos);
}

#endif /* _BCACHEFS_BBPOS_H */
