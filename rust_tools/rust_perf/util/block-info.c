// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <linux/zalloc.h>
#include "block-info.h"
#include "sort.h"
#include "annotate.h"
#include "symbol.h"
#include "dso.h"
#include "map.h"
#include "srcline.h"
#include "evlist.h"
#include "hist.h"
#include "ui/browsers/hists.h"

static struct block_header_column {
	const char *name;
	int width;
} block_columns[PERF_HPP_REPORT__BLOCK_MAX_INDEX] = {
	[PERF_HPP_REPORT__BLOCK_TOTAL_CYCLES_PCT] = {
		.name = "Sampled Cycles%",
		.width = 15,
	},
	[PERF_HPP_REPORT__BLOCK_LBR_CYCLES] = {
		.name = "Sampled Cycles",
		.width = 14,
	},
	[PERF_HPP_REPORT__BLOCK_CYCLES_PCT] = {
		.name = "Avg Cycles%",
		.width = 11,
	},
	[PERF_HPP_REPORT__BLOCK_AVG_CYCLES] = {
		.name = "Avg Cycles",
		.width = 10,
	},
	[PERF_HPP_REPORT__BLOCK_RANGE] = {
		.name = "[Program Block Range]",
		.width = 70,
	},
	[PERF_HPP_REPORT__BLOCK_DSO] = {
		.name = "Shared Object",
		.width = 20,
	},
	[PERF_HPP_REPORT__BLOCK_BRANCH_COUNTER] = {
		.name = "Branch Counter",
		.width = 30,
	}
};

static struct block_info *block_info__new(unsigned int br_cntr_nr)
{
	struct block_info *bi = zalloc(sizeof(struct block_info));

	if (bi && br_cntr_nr) {
		bi->br_cntr = calloc(br_cntr_nr, sizeof(u64));
		if (!bi->br_cntr) {
			free(bi);
			return NULL;
		}
	}

	return bi;
}

void block_info__delete(struct block_info *bi)
{
	if (bi)
		free(bi->br_cntr);
	free(bi);
}

int64_t __block_info__cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct block_info *bi_l = left->block_info;
	struct block_info *bi_r = right->block_info;
	int cmp;

	if (!bi_l->sym || !bi_r->sym) {
		if (!bi_l->sym && !bi_r->sym)
			return -1;
		else if (!bi_l->sym)
			return -1;
		else
			return 1;
	}

	cmp = strcmp(bi_l->sym->name, bi_r->sym->name);
	if (cmp)
		return cmp;

	if (bi_l->start != bi_r->start)
		return (int64_t)(bi_r->start - bi_l->start);

	return (int64_t)(bi_r->end - bi_l->end);
}

int64_t block_info__cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			struct hist_entry *left, struct hist_entry *right)
{
	return __block_info__cmp(left, right);
}

static void init_block_info(struct block_info *bi, struct symbol *sym,
			    struct cyc_hist *ch, int offset,
			    u64 total_cycles, unsigned int br_cntr_nr,
			    u64 *br_cntr, struct evsel *evsel)
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

	if (br_cntr && br_cntr_nr) {
		bi->br_cntr_nr = br_cntr_nr;
		memcpy(bi->br_cntr, &br_cntr[offset * br_cntr_nr],
		       br_cntr_nr * sizeof(u64));
	}
	bi->evsel = evsel;
}

int block_info__process_sym(struct hist_entry *he, struct block_hist *bh,
			    u64 *block_cycles_aggr, u64 total_cycles,
			    unsigned int br_cntr_nr)
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
	if (!notes || !notes->branch || !notes->branch->cycles_hist)
		return 0;
	ch = notes->branch->cycles_hist;
	for (unsigned int i = 0; i < symbol__size(he->ms.sym); i++) {
		if (ch[i].num_aggr) {
			struct block_info *bi;
			struct hist_entry *he_block;

			bi = block_info__new(br_cntr_nr);
			if (!bi)
				return -1;

			init_block_info(bi, he->ms.sym, &ch[i], i,
					total_cycles, br_cntr_nr,
					notes->branch->br_cntr,
					hists_to_evsel(he->hists));
			cycles += bi->cycles_aggr / bi->num_aggr;

			he_block = hists__add_entry_block(&bh->block_hists,
							  &al, bi);
			if (!he_block) {
				block_info__delete(bi);
				return -1;
			}
		}
	}

	if (block_cycles_aggr)
		*block_cycles_aggr += cycles;

	return 0;
}

static int block_column_header(struct perf_hpp_fmt *fmt,
			       struct perf_hpp *hpp,
			       struct hists *hists __maybe_unused,
			       int line __maybe_unused,
			       int *span __maybe_unused)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);

	return scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width,
			 block_fmt->header);
}

static int block_column_width(struct perf_hpp_fmt *fmt,
			      struct perf_hpp *hpp __maybe_unused,
			      struct hists *hists __maybe_unused)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);

	return block_fmt->width;
}

static int color_pct(struct perf_hpp *hpp, int width, double pct)
{
#ifdef HAVE_SLANG_SUPPORT
	if (use_browser) {
		return __hpp__slsmg_color_printf(hpp, "%*.2f%%",
						 width - 1, pct);
	}
#endif
	return hpp_color_scnprintf(hpp, "%*.2f%%", width - 1, pct);
}

static int block_total_cycles_pct_entry(struct perf_hpp_fmt *fmt,
					struct perf_hpp *hpp,
					struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi = he->block_info;
	double ratio = 0.0;

	if (block_fmt->total_cycles)
		ratio = (double)bi->cycles_aggr / (double)block_fmt->total_cycles;

	return color_pct(hpp, block_fmt->width, 100.0 * ratio);
}

static int64_t block_total_cycles_pct_sort(struct perf_hpp_fmt *fmt,
					   struct hist_entry *left,
					   struct hist_entry *right)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi_l = left->block_info;
	struct block_info *bi_r = right->block_info;
	double l, r;

	if (block_fmt->total_cycles) {
		l = ((double)bi_l->cycles_aggr /
			(double)block_fmt->total_cycles) * 100000.0;
		r = ((double)bi_r->cycles_aggr /
			(double)block_fmt->total_cycles) * 100000.0;
		return (int64_t)l - (int64_t)r;
	}

	return 0;
}

static void cycles_string(u64 cycles, char *buf, int size)
{
	if (cycles >= 1000000)
		scnprintf(buf, size, "%.1fM", (double)cycles / 1000000.0);
	else if (cycles >= 1000)
		scnprintf(buf, size, "%.1fK", (double)cycles / 1000.0);
	else
		scnprintf(buf, size, "%1d", cycles);
}

static int block_cycles_lbr_entry(struct perf_hpp_fmt *fmt,
				  struct perf_hpp *hpp, struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi = he->block_info;
	char cycles_buf[16];

	cycles_string(bi->cycles_aggr, cycles_buf, sizeof(cycles_buf));

	return scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width,
			 cycles_buf);
}

static int block_cycles_pct_entry(struct perf_hpp_fmt *fmt,
				  struct perf_hpp *hpp, struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi = he->block_info;
	double ratio = 0.0;
	u64 avg;

	if (block_fmt->block_cycles && bi->num_aggr) {
		avg = bi->cycles_aggr / bi->num_aggr;
		ratio = (double)avg / (double)block_fmt->block_cycles;
	}

	return color_pct(hpp, block_fmt->width, 100.0 * ratio);
}

static int block_avg_cycles_entry(struct perf_hpp_fmt *fmt,
				  struct perf_hpp *hpp,
				  struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi = he->block_info;
	char cycles_buf[16];

	cycles_string(bi->cycles_aggr / bi->num_aggr, cycles_buf,
		      sizeof(cycles_buf));

	return scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width,
			 cycles_buf);
}

static int block_range_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			     struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi = he->block_info;
	char buf[128];
	char *start_line, *end_line;

	symbol_conf.disable_add2line_warn = true;

	start_line = map__srcline(he->ms.map, bi->sym->start + bi->start,
				  he->ms.sym);

	end_line = map__srcline(he->ms.map, bi->sym->start + bi->end,
				he->ms.sym);

	if (start_line != SRCLINE_UNKNOWN &&
	    end_line != SRCLINE_UNKNOWN) {
		scnprintf(buf, sizeof(buf), "[%s -> %s]",
			  start_line, end_line);
	} else {
		scnprintf(buf, sizeof(buf), "[%7lx -> %7lx]",
			  bi->start, bi->end);
	}

	zfree_srcline(&start_line);
	zfree_srcline(&end_line);

	return scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width, buf);
}

static int block_dso_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct map *map = he->ms.map;

	if (map && map__dso(map)) {
		return scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width,
				 dso__short_name(map__dso(map)));
	}

	return scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width,
			 "[unknown]");
}

static void init_block_header(struct block_fmt *block_fmt)
{
	struct perf_hpp_fmt *fmt = &block_fmt->fmt;

	BUG_ON(block_fmt->idx >= PERF_HPP_REPORT__BLOCK_MAX_INDEX);

	block_fmt->header = block_columns[block_fmt->idx].name;
	block_fmt->width = block_columns[block_fmt->idx].width;

	fmt->header = block_column_header;
	fmt->width = block_column_width;
}

static int block_branch_counter_entry(struct perf_hpp_fmt *fmt,
				      struct perf_hpp *hpp,
				      struct hist_entry *he)
{
	struct block_fmt *block_fmt = container_of(fmt, struct block_fmt, fmt);
	struct block_info *bi = he->block_info;
	char *buf;
	int ret;

	if (annotation_br_cntr_entry(&buf, bi->br_cntr_nr, bi->br_cntr,
				     bi->num_aggr, bi->evsel))
		return 0;

	ret = scnprintf(hpp->buf, hpp->size, "%*s", block_fmt->width, buf);
	free(buf);
	return ret;
}

static void hpp_register(struct block_fmt *block_fmt, int idx,
			 struct perf_hpp_list *hpp_list)
{
	struct perf_hpp_fmt *fmt = &block_fmt->fmt;

	block_fmt->idx = idx;
	INIT_LIST_HEAD(&fmt->list);
	INIT_LIST_HEAD(&fmt->sort_list);

	switch (idx) {
	case PERF_HPP_REPORT__BLOCK_TOTAL_CYCLES_PCT:
		fmt->color = block_total_cycles_pct_entry;
		fmt->cmp = block_info__cmp;
		fmt->sort = block_total_cycles_pct_sort;
		break;
	case PERF_HPP_REPORT__BLOCK_LBR_CYCLES:
		fmt->entry = block_cycles_lbr_entry;
		break;
	case PERF_HPP_REPORT__BLOCK_CYCLES_PCT:
		fmt->color = block_cycles_pct_entry;
		break;
	case PERF_HPP_REPORT__BLOCK_AVG_CYCLES:
		fmt->entry = block_avg_cycles_entry;
		break;
	case PERF_HPP_REPORT__BLOCK_RANGE:
		fmt->entry = block_range_entry;
		break;
	case PERF_HPP_REPORT__BLOCK_DSO:
		fmt->entry = block_dso_entry;
		break;
	case PERF_HPP_REPORT__BLOCK_BRANCH_COUNTER:
		fmt->entry = block_branch_counter_entry;
		break;
	default:
		return;
	}

	init_block_header(block_fmt);
	perf_hpp_list__column_register(hpp_list, fmt);
}

static void register_block_columns(struct perf_hpp_list *hpp_list,
				   struct block_fmt *block_fmts,
				   int *block_hpps, int nr_hpps)
{
	for (int i = 0; i < nr_hpps; i++)
		hpp_register(&block_fmts[i], block_hpps[i], hpp_list);
}

static void init_block_hist(struct block_hist *bh, struct block_fmt *block_fmts,
			    int *block_hpps, int nr_hpps)
{
	__hists__init(&bh->block_hists, &bh->block_list);
	perf_hpp_list__init(&bh->block_list);
	bh->block_list.nr_header_lines = 1;

	register_block_columns(&bh->block_list, block_fmts,
			       block_hpps, nr_hpps);

	/* Sort by the first fmt */
	perf_hpp_list__register_sort_field(&bh->block_list, &block_fmts[0].fmt);
}

static int process_block_report(struct hists *hists,
				struct block_report *block_report,
				u64 total_cycles, int *block_hpps,
				int nr_hpps, unsigned int br_cntr_nr)
{
	struct rb_node *next = rb_first_cached(&hists->entries);
	struct block_hist *bh = &block_report->hist;
	struct hist_entry *he;

	if (nr_hpps > PERF_HPP_REPORT__BLOCK_MAX_INDEX)
		return -1;

	block_report->nr_fmts = nr_hpps;
	init_block_hist(bh, block_report->fmts, block_hpps, nr_hpps);

	while (next) {
		he = rb_entry(next, struct hist_entry, rb_node);
		block_info__process_sym(he, bh, &block_report->cycles,
					total_cycles, br_cntr_nr);
		next = rb_next(&he->rb_node);
	}

	for (int i = 0; i < nr_hpps; i++) {
		block_report->fmts[i].total_cycles = total_cycles;
		block_report->fmts[i].block_cycles = block_report->cycles;
	}

	hists__output_resort(&bh->block_hists, NULL);
	return 0;
}

struct block_report *block_info__create_report(struct evlist *evlist,
					       u64 total_cycles,
					       int *block_hpps, int nr_hpps,
					       int *nr_reps)
{
	struct block_report *block_reports;
	int nr_hists = evlist->core.nr_entries, i = 0;
	struct evsel *pos;

	block_reports = calloc(nr_hists, sizeof(struct block_report));
	if (!block_reports)
		return NULL;

	evlist__for_each_entry(evlist, pos) {
		struct hists *hists = evsel__hists(pos);

		process_block_report(hists, &block_reports[i], total_cycles,
				     block_hpps, nr_hpps, evlist->nr_br_cntr);
		i++;
	}

	*nr_reps = nr_hists;
	return block_reports;
}

void block_info__free_report(struct block_report *reps, int nr_reps)
{
	for (int i = 0; i < nr_reps; i++)
		hists__delete_entries(&reps[i].hist.block_hists);

	free(reps);
}

int report__browse_block_hists(struct block_hist *bh, float min_percent,
			       struct evsel *evsel, struct perf_env *env)
{
	int ret;

	switch (use_browser) {
	case 0:
		symbol_conf.report_individual_block = true;
		hists__fprintf(&bh->block_hists, true, 0, 0, min_percent,
			       stdout, true);
		return 0;
	case 1:
		symbol_conf.report_individual_block = true;
		ret = block_hists_tui_browse(bh, evsel, min_percent, env);
		return ret;
	default:
		return -1;
	}

	return 0;
}

float block_info__total_cycles_percent(struct hist_entry *he)
{
	struct block_info *bi = he->block_info;

	if (bi->total_cycles)
		return bi->cycles * 100.0 / bi->total_cycles;

	return 0.0;
}
