/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BBPOS_TYPES_H
#define _BCACHEFS_BBPOS_TYPES_H

struct bbpos {
	enum btree_id		btree;
	struct bpos		pos;
};

static inline struct bbpos BBPOS(enum btree_id btree, struct bpos pos)
{
	return (struct bbpos) { btree, pos };
}

#define BBPOS_MIN	BBPOS(0, POS_MIN)
#define BBPOS_MAX	BBPOS(BTREE_ID_NR - 1, SPOS_MAX)

#endif /* _BCACHEFS_BBPOS_TYPES_H */
