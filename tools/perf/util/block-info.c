// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <linux/zalloc.h>
#include "block-info.h"
#include "sort.h"
#include "annotate.h"
#include "symbol.h"

struct block_info *block_info__get(struct block_info *bi)
{
	if (bi)
		refcount_inc(&bi->refcnt);
	return bi;
}

void block_info__put(struct block_info *bi)
{
	if (bi && refcount_dec_and_test(&bi->refcnt))
		free(bi);
}

struct block_info *block_info__new(void)
{
	struct block_info *bi = zalloc(sizeof(*bi));

	if (bi)
		refcount_set(&bi->refcnt, 1);
	return bi;
}

int64_t block_info__cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			struct hist_entry *left, struct hist_entry *right)
{
	struct block_info *bi_l = left->block_info;
	struct block_info *bi_r = right->block_info;
	int cmp;

	if (!bi_l->sym || !bi_r->sym) {
		if (!bi_l->sym && !bi_r->sym)
			return 0;
		else if (!bi_l->sym)
			return -1;
		else
			return 1;
	}

	if (bi_l->sym == bi_r->sym) {
		if (bi_l->start == bi_r->start) {
			if (bi_l->end == bi_r->end)
				return 0;
			else
				return (int64_t)(bi_r->end - bi_l->end);
		} else
			return (int64_t)(bi_r->start - bi_l->start);
	} else {
		cmp = strcmp(bi_l->sym->name, bi_r->sym->name);
		return cmp;
	}

	if (bi_l->sym->start != bi_r->sym->start)
		return (int64_t)(bi_r->sym->start - bi_l->sym->start);

	return (int64_t)(bi_r->sym->end - bi_l->sym->end);
}

static void init_block_info(struct block_info *bi, struct symbol *sym,
			    struct cyc_hist *ch, int offset,
			    u64 total_cycles)
{
	bi->sym = sym;
	bi->start = ch->start;
	bi->end = offset;
	bi->cycles = ch->cycles;
	bi->cycles_aggr = ch->cycles_aggr;
	bi->num = ch->num;
	bi->num_aggr = ch->num_aggr;
	bi->total_cycles = total_cycles;

	memcpy(bi->cycles_spark, ch->cycles_spark,
	       NUM_SPARKS * sizeof(u64));
}

int block_info__process_sym(struct hist_entry *he, struct block_hist *bh,
			    u64 *block_cycles_aggr, u64 total_cycles)
{
	struct annotation *notes;
	struct cyc_hist *ch;
	static struct addr_location al;
	u64 cycles = 0;

	if (!he->ms.map || !he->ms.sym)
		return 0;

	memset(&al, 0, sizeof(al));
	al.map = he->ms.map;
	al.sym = he->ms.sym;

	notes = symbol__annotation(he->ms.sym);
	if (!notes || !notes->src || !notes->src->cycles_hist)
		return 0;
	ch = notes->src->cycles_hist;
	for (unsigned int i = 0; i < symbol__size(he->ms.sym); i++) {
		if (ch[i].num_aggr) {
			struct block_info *bi;
			struct hist_entry *he_block;

			bi = block_info__new();
			if (!bi)
				return -1;

			init_block_info(bi, he->ms.sym, &ch[i], i,
					total_cycles);
			cycles += bi->cycles_aggr / bi->num_aggr;

			he_block = hists__add_entry_block(&bh->block_hists,
							  &al, bi);
			if (!he_block) {
				block_info__put(bi);
				return -1;
			}
		}
	}

	if (block_cycles_aggr)
		*block_cycles_aggr += cycles;

	return 0;
}
