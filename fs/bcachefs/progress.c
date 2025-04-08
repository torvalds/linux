// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bbpos.h"
#include "disk_accounting.h"
#include "progress.h"

void bch2_progress_init(struct progress_indicator_state *s,
			struct bch_fs *c,
			u64 btree_id_mask)
{
	memset(s, 0, sizeof(*s));

	s->next_print = jiffies + HZ * 10;

	for (unsigned i = 0; i < BTREE_ID_NR; i++) {
		if (!(btree_id_mask & BIT_ULL(i)))
			continue;

		struct disk_accounting_pos acc;
		disk_accounting_key_init(acc, btree, .id = i);

		u64 v;
		bch2_accounting_mem_read(c, disk_accounting_pos_to_bpos(&acc), &v, 1);
		s->nodes_total += div64_ul(v, btree_sectors(c));
	}
}

static inline bool progress_update_p(struct progress_indicator_state *s)
{
	bool ret = time_after_eq(jiffies, s->next_print);

	if (ret)
		s->next_print = jiffies + HZ * 10;
	return ret;
}

void bch2_progress_update_iter(struct btree_trans *trans,
			       struct progress_indicator_state *s,
			       struct btree_iter *iter,
			       const char *msg)
{
	struct bch_fs *c = trans->c;
	struct btree *b = path_l(btree_iter_path(trans, iter))->b;

	s->nodes_seen += b != s->last_node;
	s->last_node = b;

	if (progress_update_p(s)) {
		struct printbuf buf = PRINTBUF;
		unsigned percent = s->nodes_total
			? div64_u64(s->nodes_seen * 100, s->nodes_total)
			: 0;

		prt_printf(&buf, "%s: %d%%, done %llu/%llu nodes, at ",
			   msg, percent, s->nodes_seen, s->nodes_total);
		bch2_bbpos_to_text(&buf, BBPOS(iter->btree_id, iter->pos));

		bch_info(c, "%s", buf.buf);
		printbuf_exit(&buf);
	}
}
