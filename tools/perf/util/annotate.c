// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-annotate.c, see those files for further
 * copyright notes.
 */

#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdlib.h>
#include "util.h" // hex_width()
#include "ui/ui.h"
#include "sort.h"
#include "build-id.h"
#include "color.h"
#include "config.h"
#include "disasm.h"
#include "dso.h"
#include "env.h"
#include "map.h"
#include "maps.h"
#include "symbol.h"
#include "srcline.h"
#include "units.h"
#include "debug.h"
#include "debuginfo.h"
#include "annotate.h"
#include "annotate-data.h"
#include "evsel.h"
#include "evlist.h"
#include "bpf-event.h"
#include "bpf-utils.h"
#include "block-range.h"
#include "string2.h"
#include "dwarf-regs.h"
#include "util/event.h"
#include "util/sharded_mutex.h"
#include "arch/common.h"
#include "namespaces.h"
#include "thread.h"
#include "hashmap.h"
#include "strbuf.h"
#include <regex.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <subcmd/parse-options.h>
#include <subcmd/run-command.h>
#include <math.h>

/* FIXME: For the HE_COLORSET */
#include "ui/browser.h"

/*
 * FIXME: Using the same values as slang.h,
 * but that header may not be available everywhere
 */
#define LARROW_CHAR	((unsigned char)',')
#define RARROW_CHAR	((unsigned char)'+')
#define DARROW_CHAR	((unsigned char)'.')
#define UARROW_CHAR	((unsigned char)'-')

#include <linux/ctype.h>

/* global annotation options */
struct annotation_options annotate_opts;

/* Data type collection debug statistics */
struct annotated_data_stat ann_data_stat;
LIST_HEAD(ann_insn_stat);

/* Pseudo data types */
struct annotated_data_type stackop_type = {
	.self = {
		.type_name = (char *)"(stack operation)",
		.children = LIST_HEAD_INIT(stackop_type.self.children),
	},
};

struct annotated_data_type canary_type = {
	.self = {
		.type_name = (char *)"(stack canary)",
		.children = LIST_HEAD_INIT(canary_type.self.children),
	},
};

/* symbol histogram: key = offset << 16 | evsel->core.idx */
static size_t sym_hist_hash(long key, void *ctx __maybe_unused)
{
	return (key >> 16) + (key & 0xffff);
}

static bool sym_hist_equal(long key1, long key2, void *ctx __maybe_unused)
{
	return key1 == key2;
}

static struct annotated_source *annotated_source__new(void)
{
	struct annotated_source *src = zalloc(sizeof(*src));

	if (src != NULL)
		INIT_LIST_HEAD(&src->source);

	return src;
}

static __maybe_unused void annotated_source__delete(struct annotated_source *src)
{
	struct hashmap_entry *cur;
	size_t bkt;

	if (src == NULL)
		return;

	if (src->samples) {
		hashmap__for_each_entry(src->samples, cur, bkt)
			zfree(&cur->pvalue);
		hashmap__free(src->samples);
	}
	zfree(&src->histograms);
	free(src);
}

static int annotated_source__alloc_histograms(struct annotated_source *src,
					      int nr_hists)
{
	src->nr_histograms   = nr_hists;
	src->histograms	     = calloc(nr_hists, sizeof(*src->histograms));

	if (src->histograms == NULL)
		return -1;

	src->samples = hashmap__new(sym_hist_hash, sym_hist_equal, NULL);
	if (src->samples == NULL)
		zfree(&src->histograms);

	return src->histograms ? 0 : -1;
}

void symbol__annotate_zero_histograms(struct symbol *sym)
{
	struct annotation *notes = symbol__annotation(sym);

	annotation__lock(notes);
	if (notes->src != NULL) {
		memset(notes->src->histograms, 0,
		       notes->src->nr_histograms * sizeof(*notes->src->histograms));
		hashmap__clear(notes->src->samples);
	}
	if (notes->branch && notes->branch->cycles_hist) {
		memset(notes->branch->cycles_hist, 0,
		       symbol__size(sym) * sizeof(struct cyc_hist));
	}
	annotation__unlock(notes);
}

static int __symbol__account_cycles(struct cyc_hist *ch,
				    u64 start,
				    unsigned offset, unsigned cycles,
				    unsigned have_start)
{
	/*
	 * For now we can only account one basic block per
	 * final jump. But multiple could be overlapping.
	 * Always account the longest one. So when
	 * a shorter one has been already seen throw it away.
	 *
	 * We separately always account the full cycles.
	 */
	ch[offset].num_aggr++;
	ch[offset].cycles_aggr += cycles;

	if (cycles > ch[offset].cycles_max)
		ch[offset].cycles_max = cycles;

	if (ch[offset].cycles_min) {
		if (cycles && cycles < ch[offset].cycles_min)
			ch[offset].cycles_min = cycles;
	} else
		ch[offset].cycles_min = cycles;

	if (!have_start && ch[offset].have_start)
		return 0;
	if (ch[offset].num) {
		if (have_start && (!ch[offset].have_start ||
				   ch[offset].start > start)) {
			ch[offset].have_start = 0;
			ch[offset].cycles = 0;
			ch[offset].num = 0;
			if (ch[offset].reset < 0xffff)
				ch[offset].reset++;
		} else if (have_start &&
			   ch[offset].start < start)
			return 0;
	}

	if (ch[offset].num < NUM_SPARKS)
		ch[offset].cycles_spark[ch[offset].num] = cycles;

	ch[offset].have_start = have_start;
	ch[offset].start = start;
	ch[offset].cycles += cycles;
	ch[offset].num++;
	return 0;
}

static int __symbol__inc_addr_samples(struct map_symbol *ms,
				      struct annotated_source *src, struct evsel *evsel, u64 addr,
				      struct perf_sample *sample)
{
	struct symbol *sym = ms->sym;
	long hash_key;
	u64 offset;
	struct sym_hist *h;
	struct sym_hist_entry *entry;

	pr_debug3("%s: addr=%#" PRIx64 "\n", __func__, map__unmap_ip(ms->map, addr));

	if ((addr < sym->start || addr >= sym->end) &&
	    (addr != sym->end || sym->start != sym->end)) {
		pr_debug("%s(%d): ERANGE! sym->name=%s, start=%#" PRIx64 ", addr=%#" PRIx64 ", end=%#" PRIx64 "\n",
		       __func__, __LINE__, sym->name, sym->start, addr, sym->end);
		return -ERANGE;
	}

	offset = addr - sym->start;
	h = annotated_source__histogram(src, evsel);
	if (h == NULL) {
		pr_debug("%s(%d): ENOMEM! sym->name=%s, start=%#" PRIx64 ", addr=%#" PRIx64 ", end=%#" PRIx64 ", func: %d\n",
			 __func__, __LINE__, sym->name, sym->start, addr, sym->end, sym->type == STT_FUNC);
		return -ENOMEM;
	}

	hash_key = offset << 16 | evsel->core.idx;
	if (!hashmap__find(src->samples, hash_key, &entry)) {
		entry = zalloc(sizeof(*entry));
		if (entry == NULL)
			return -ENOMEM;

		if (hashmap__add(src->samples, hash_key, entry) < 0)
			return -ENOMEM;
	}

	h->nr_samples++;
	h->period += sample->period;
	entry->nr_samples++;
	entry->period += sample->period;

	pr_debug3("%#" PRIx64 " %s: period++ [addr: %#" PRIx64 ", %#" PRIx64
		  ", evidx=%d] => nr_samples: %" PRIu64 ", period: %" PRIu64 "\n",
		  sym->start, sym->name, addr, addr - sym->start, evsel->core.idx,
		  entry->nr_samples, entry->period);
	return 0;
}

struct annotated_branch *annotation__get_branch(struct annotation *notes)
{
	if (notes == NULL)
		return NULL;

	if (notes->branch == NULL)
		notes->branch = zalloc(sizeof(*notes->branch));

	return notes->branch;
}

static struct annotated_branch *symbol__find_branch_hist(struct symbol *sym,
							 unsigned int br_cntr_nr)
{
	struct annotation *notes = symbol__annotation(sym);
	struct annotated_branch *branch;
	const size_t size = symbol__size(sym);

	branch = annotation__get_branch(notes);
	if (branch == NULL)
		return NULL;

	if (branch->cycles_hist == NULL) {
		branch->cycles_hist = calloc(size, sizeof(struct cyc_hist));
		if (!branch->cycles_hist)
			return NULL;
	}

	if (br_cntr_nr && branch->br_cntr == NULL) {
		branch->br_cntr = calloc(br_cntr_nr * size, sizeof(u64));
		if (!branch->br_cntr)
			return NULL;
	}

	return branch;
}

struct annotated_source *symbol__hists(struct symbol *sym, int nr_hists)
{
	struct annotation *notes = symbol__annotation(sym);

	if (notes->src == NULL) {
		notes->src = annotated_source__new();
		if (notes->src == NULL)
			return NULL;
		goto alloc_histograms;
	}

	if (notes->src->histograms == NULL) {
alloc_histograms:
		annotated_source__alloc_histograms(notes->src, nr_hists);
	}

	return notes->src;
}

static int symbol__inc_addr_samples(struct map_symbol *ms,
				    struct evsel *evsel, u64 addr,
				    struct perf_sample *sample)
{
	struct symbol *sym = ms->sym;
	struct annotated_source *src;

	if (sym == NULL)
		return 0;
	src = symbol__hists(sym, evsel->evlist->core.nr_entries);
	return src ? __symbol__inc_addr_samples(ms, src, evsel, addr, sample) : 0;
}

static int symbol__account_br_cntr(struct annotated_branch *branch,
				   struct evsel *evsel,
				   unsigned offset,
				   u64 br_cntr)
{
	unsigned int br_cntr_nr = evsel__leader(evsel)->br_cntr_nr;
	unsigned int base = evsel__leader(evsel)->br_cntr_idx;
	unsigned int off = offset * evsel->evlist->nr_br_cntr;
	u64 *branch_br_cntr = branch->br_cntr;
	unsigned int i, mask, width;

	if (!br_cntr || !branch_br_cntr)
		return 0;

	perf_env__find_br_cntr_info(evsel__env(evsel), NULL, &width);
	mask = (1L << width) - 1;
	for (i = 0; i < br_cntr_nr; i++) {
		u64 cntr = (br_cntr >> i * width) & mask;

		branch_br_cntr[off + i + base] += cntr;
		if (cntr == mask)
			branch_br_cntr[off + i + base] |= ANNOTATION__BR_CNTR_SATURATED_FLAG;
	}

	return 0;
}

static int symbol__account_cycles(u64 addr, u64 start, struct symbol *sym,
				  unsigned cycles, struct evsel *evsel,
				  u64 br_cntr)
{
	struct annotated_branch *branch;
	unsigned offset;
	int ret;

	if (sym == NULL)
		return 0;
	branch = symbol__find_branch_hist(sym, evsel->evlist->nr_br_cntr);
	if (!branch)
		return -ENOMEM;
	if (addr < sym->start || addr >= sym->end)
		return -ERANGE;

	if (start) {
		if (start < sym->start || start >= sym->end)
			return -ERANGE;
		if (start >= addr)
			start = 0;
	}
	offset = addr - sym->start;
	ret = __symbol__account_cycles(branch->cycles_hist,
					start ? start - sym->start : 0,
					offset, cycles,
					!!start);

	if (ret)
		return ret;

	return symbol__account_br_cntr(branch, evsel, offset, br_cntr);
}

int addr_map_symbol__account_cycles(struct addr_map_symbol *ams,
				    struct addr_map_symbol *start,
				    unsigned cycles,
				    struct evsel *evsel,
				    u64 br_cntr)
{
	u64 saddr = 0;
	int err;

	if (!cycles)
		return 0;

	/*
	 * Only set start when IPC can be computed. We can only
	 * compute it when the basic block is completely in a single
	 * function.
	 * Special case the case when the jump is elsewhere, but
	 * it starts on the function start.
	 */
	if (start &&
		(start->ms.sym == ams->ms.sym ||
		 (ams->ms.sym &&
		  start->addr == ams->ms.sym->start + map__start(ams->ms.map))))
		saddr = start->al_addr;
	if (saddr == 0)
		pr_debug2("BB with bad start: addr %"PRIx64" start %"PRIx64" sym %"PRIx64" saddr %"PRIx64"\n",
			ams->addr,
			start ? start->addr : 0,
			ams->ms.sym ? ams->ms.sym->start + map__start(ams->ms.map) : 0,
			saddr);
	err = symbol__account_cycles(ams->al_addr, saddr, ams->ms.sym, cycles, evsel, br_cntr);
	if (err)
		pr_debug2("account_cycles failed %d\n", err);
	return err;
}

struct annotation_line *annotated_source__get_line(struct annotated_source *src,
						   s64 offset)
{
	struct annotation_line *al;

	list_for_each_entry(al, &src->source, node) {
		if (al->offset == offset)
			return al;
	}
	return NULL;
}

static unsigned annotation__count_insn(struct annotation *notes, u64 start, u64 end)
{
	struct annotation_line *al;
	unsigned n_insn = 0;

	al = annotated_source__get_line(notes->src, start);
	if (al == NULL)
		return 0;

	list_for_each_entry_from(al, &notes->src->source, node) {
		if (al->offset == -1)
			continue;
		if ((u64)al->offset > end)
			break;
		n_insn++;
	}
	return n_insn;
}

static void annotated_branch__delete(struct annotated_branch *branch)
{
	if (branch) {
		zfree(&branch->cycles_hist);
		free(branch->br_cntr);
		free(branch);
	}
}

static void annotation__count_and_fill(struct annotation *notes, u64 start, u64 end, struct cyc_hist *ch)
{
	unsigned n_insn;
	unsigned int cover_insn = 0;

	n_insn = annotation__count_insn(notes, start, end);
	if (n_insn && ch->num && ch->cycles) {
		struct annotation_line *al;
		struct annotated_branch *branch;
		float ipc = n_insn / ((double)ch->cycles / (double)ch->num);

		/* Hide data when there are too many overlaps. */
		if (ch->reset >= 0x7fff)
			return;

		al = annotated_source__get_line(notes->src, start);
		if (al == NULL)
			return;

		list_for_each_entry_from(al, &notes->src->source, node) {
			if (al->offset == -1)
				continue;
			if ((u64)al->offset > end)
				break;
			if (al->cycles && al->cycles->ipc == 0.0) {
				al->cycles->ipc = ipc;
				cover_insn++;
			}
		}

		branch = annotation__get_branch(notes);
		if (cover_insn && branch) {
			branch->hit_cycles += ch->cycles;
			branch->hit_insn += n_insn * ch->num;
			branch->cover_insn += cover_insn;
		}
	}
}

static int annotation__compute_ipc(struct annotation *notes, size_t size,
				   struct evsel *evsel)
{
	unsigned int br_cntr_nr = evsel->evlist->nr_br_cntr;
	int err = 0;
	s64 offset;

	if (!notes->branch || !notes->branch->cycles_hist)
		return 0;

	notes->branch->total_insn = annotation__count_insn(notes, 0, size - 1);
	notes->branch->hit_cycles = 0;
	notes->branch->hit_insn = 0;
	notes->branch->cover_insn = 0;

	annotation__lock(notes);
	for (offset = size - 1; offset >= 0; --offset) {
		struct cyc_hist *ch;

		ch = &notes->branch->cycles_hist[offset];
		if (ch && ch->cycles) {
			struct annotation_line *al;

			al = annotated_source__get_line(notes->src, offset);
			if (al && al->cycles == NULL) {
				al->cycles = zalloc(sizeof(*al->cycles));
				if (al->cycles == NULL) {
					err = ENOMEM;
					break;
				}
			}
			if (ch->have_start)
				annotation__count_and_fill(notes, ch->start, offset, ch);
			if (al && ch->num_aggr) {
				al->cycles->avg = ch->cycles_aggr / ch->num_aggr;
				al->cycles->max = ch->cycles_max;
				al->cycles->min = ch->cycles_min;
			}
			if (al && notes->branch->br_cntr) {
				if (!al->br_cntr) {
					al->br_cntr = calloc(br_cntr_nr, sizeof(u64));
					if (!al->br_cntr) {
						err = ENOMEM;
						break;
					}
				}
				al->num_aggr = ch->num_aggr;
				al->br_cntr_nr = br_cntr_nr;
				al->evsel = evsel;
				memcpy(al->br_cntr, &notes->branch->br_cntr[offset * br_cntr_nr],
				       br_cntr_nr * sizeof(u64));
			}
		}
	}

	if (err) {
		while (++offset < (s64)size) {
			struct cyc_hist *ch = &notes->branch->cycles_hist[offset];

			if (ch && ch->cycles) {
				struct annotation_line *al;

				al = annotated_source__get_line(notes->src, offset);
				if (al) {
					zfree(&al->cycles);
					zfree(&al->br_cntr);
				}
			}
		}
	}

	annotation__unlock(notes);
	return 0;
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, struct perf_sample *sample,
				 struct evsel *evsel)
{
	return symbol__inc_addr_samples(&ams->ms, evsel, ams->al_addr, sample);
}

int hist_entry__inc_addr_samples(struct hist_entry *he, struct perf_sample *sample,
				 struct evsel *evsel, u64 ip)
{
	return symbol__inc_addr_samples(&he->ms, evsel, ip, sample);
}


void annotation__exit(struct annotation *notes)
{
	annotated_source__delete(notes->src);
	annotated_branch__delete(notes->branch);
}

static struct sharded_mutex *sharded_mutex;

static void annotation__init_sharded_mutex(void)
{
	/* As many mutexes as there are CPUs. */
	sharded_mutex = sharded_mutex__new(cpu__max_present_cpu().cpu);
}

static size_t annotation__hash(const struct annotation *notes)
{
	return (size_t)notes;
}

static struct mutex *annotation__get_mutex(const struct annotation *notes)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, annotation__init_sharded_mutex);
	if (!sharded_mutex)
		return NULL;

	return sharded_mutex__get_mutex(sharded_mutex, annotation__hash(notes));
}

void annotation__lock(struct annotation *notes)
	NO_THREAD_SAFETY_ANALYSIS
{
	struct mutex *mutex = annotation__get_mutex(notes);

	if (mutex)
		mutex_lock(mutex);
}

void annotation__unlock(struct annotation *notes)
	NO_THREAD_SAFETY_ANALYSIS
{
	struct mutex *mutex = annotation__get_mutex(notes);

	if (mutex)
		mutex_unlock(mutex);
}

bool annotation__trylock(struct annotation *notes)
{
	struct mutex *mutex = annotation__get_mutex(notes);

	if (!mutex)
		return false;

	return mutex_trylock(mutex);
}

void annotation_line__add(struct annotation_line *al, struct list_head *head)
{
	list_add_tail(&al->node, head);
}

struct annotation_line *
annotation_line__next(struct annotation_line *pos, struct list_head *head)
{
	list_for_each_entry_continue(pos, head, node)
		if (pos->offset >= 0)
			return pos;

	return NULL;
}

static const char *annotate__address_color(struct block_range *br)
{
	double cov = block_range__coverage(br);

	if (cov >= 0) {
		/* mark red for >75% coverage */
		if (cov > 0.75)
			return PERF_COLOR_RED;

		/* mark dull for <1% coverage */
		if (cov < 0.01)
			return PERF_COLOR_NORMAL;
	}

	return PERF_COLOR_MAGENTA;
}

static const char *annotate__asm_color(struct block_range *br)
{
	double cov = block_range__coverage(br);

	if (cov >= 0) {
		/* mark dull for <1% coverage */
		if (cov < 0.01)
			return PERF_COLOR_NORMAL;
	}

	return PERF_COLOR_BLUE;
}

static void annotate__branch_printf(struct block_range *br, u64 addr)
{
	bool emit_comment = true;

	if (!br)
		return;

#if 1
	if (br->is_target && br->start == addr) {
		struct block_range *branch = br;
		double p;

		/*
		 * Find matching branch to our target.
		 */
		while (!branch->is_branch)
			branch = block_range__next(branch);

		p = 100 *(double)br->entry / branch->coverage;

		if (p > 0.1) {
			if (emit_comment) {
				emit_comment = false;
				printf("\t#");
			}

			/*
			 * The percentage of coverage joined at this target in relation
			 * to the next branch.
			 */
			printf(" +%.2f%%", p);
		}
	}
#endif
	if (br->is_branch && br->end == addr) {
		double p = 100*(double)br->taken / br->coverage;

		if (p > 0.1) {
			if (emit_comment) {
				emit_comment = false;
				printf("\t#");
			}

			/*
			 * The percentage of coverage leaving at this branch, and
			 * its prediction ratio.
			 */
			printf(" -%.2f%% (p:%.2f%%)", p, 100*(double)br->pred  / br->taken);
		}
	}
}

static int disasm_line__print(struct disasm_line *dl, u64 start, int addr_fmt_width)
{
	s64 offset = dl->al.offset;
	const u64 addr = start + offset;
	struct block_range *br;

	br = block_range__find(addr);
	color_fprintf(stdout, annotate__address_color(br), "  %*" PRIx64 ":", addr_fmt_width, addr);
	color_fprintf(stdout, annotate__asm_color(br), "%s", dl->al.line);
	annotate__branch_printf(br, addr);
	return 0;
}

static int
annotation_line__print(struct annotation_line *al, struct symbol *sym, u64 start,
		       struct evsel *evsel, u64 len, int min_pcnt, int printed,
		       int max_lines, struct annotation_line *queue, int addr_fmt_width,
		       int percent_type)
{
	struct disasm_line *dl = container_of(al, struct disasm_line, al);
	struct annotation *notes = symbol__annotation(sym);
	static const char *prev_line;

	if (al->offset != -1) {
		double max_percent = 0.0;
		int i, nr_percent = 1;
		const char *color;

		for (i = 0; i < al->data_nr; i++) {
			double percent;

			percent = annotation_data__percent(&al->data[i],
							   percent_type);

			if (percent > max_percent)
				max_percent = percent;
		}

		if (al->data_nr > nr_percent)
			nr_percent = al->data_nr;

		if (max_percent < min_pcnt)
			return -1;

		if (max_lines && printed >= max_lines)
			return 1;

		if (queue != NULL) {
			list_for_each_entry_from(queue, &notes->src->source, node) {
				if (queue == al)
					break;
				annotation_line__print(queue, sym, start, evsel, len,
						       0, 0, 1, NULL, addr_fmt_width,
						       percent_type);
			}
		}

		color = get_percent_color(max_percent);

		for (i = 0; i < nr_percent; i++) {
			struct annotation_data *data = &al->data[i];
			double percent;

			percent = annotation_data__percent(data, percent_type);
			color = get_percent_color(percent);

			if (symbol_conf.show_total_period)
				color_fprintf(stdout, color, " %11" PRIu64,
					      data->he.period);
			else if (symbol_conf.show_nr_samples)
				color_fprintf(stdout, color, " %7" PRIu64,
					      data->he.nr_samples);
			else
				color_fprintf(stdout, color, " %7.2f", percent);
		}

		printf(" : ");

		disasm_line__print(dl, start, addr_fmt_width);

		/*
		 * Also color the filename and line if needed, with
		 * the same color than the percentage. Don't print it
		 * twice for close colored addr with the same filename:line
		 */
		if (al->path) {
			if (!prev_line || strcmp(prev_line, al->path)) {
				color_fprintf(stdout, color, " // %s", al->path);
				prev_line = al->path;
			}
		}

		printf("\n");
	} else if (max_lines && printed >= max_lines)
		return 1;
	else {
		int width = annotation__pcnt_width(notes);

		if (queue)
			return -1;

		if (!*al->line)
			printf(" %*s:\n", width, " ");
		else
			printf(" %*s: %-*d %s\n", width, " ", addr_fmt_width, al->line_nr, al->line);
	}

	return 0;
}

static void calc_percent(struct annotation *notes,
			 struct evsel *evsel,
			 struct annotation_data *data,
			 s64 offset, s64 end)
{
	struct hists *hists = evsel__hists(evsel);
	struct sym_hist *sym_hist = annotation__histogram(notes, evsel);
	unsigned int hits = 0;
	u64 period = 0;

	while (offset < end) {
		struct sym_hist_entry *entry;

		entry = annotated_source__hist_entry(notes->src, evsel, offset);
		if (entry) {
			hits   += entry->nr_samples;
			period += entry->period;
		}
		++offset;
	}

	if (sym_hist->nr_samples) {
		data->he.period     = period;
		data->he.nr_samples = hits;
		data->percent[PERCENT_HITS_LOCAL] = 100.0 * hits / sym_hist->nr_samples;
	}

	if (hists->stats.nr_non_filtered_samples)
		data->percent[PERCENT_HITS_GLOBAL] = 100.0 * hits / hists->stats.nr_non_filtered_samples;

	if (sym_hist->period)
		data->percent[PERCENT_PERIOD_LOCAL] = 100.0 * period / sym_hist->period;

	if (hists->stats.total_period)
		data->percent[PERCENT_PERIOD_GLOBAL] = 100.0 * period / hists->stats.total_period;
}

static void annotation__calc_percent(struct annotation *notes,
				     struct evsel *leader, s64 len)
{
	struct annotation_line *al, *next;
	struct evsel *evsel;

	list_for_each_entry(al, &notes->src->source, node) {
		s64 end;
		int i = 0;

		if (al->offset == -1)
			continue;

		next = annotation_line__next(al, &notes->src->source);
		end  = next ? next->offset : len;

		for_each_group_evsel(evsel, leader) {
			struct annotation_data *data;

			BUG_ON(i >= al->data_nr);

			if (symbol_conf.skip_empty &&
			    evsel__hists(evsel)->stats.nr_samples == 0)
				continue;

			data = &al->data[i++];

			calc_percent(notes, evsel, data, al->offset, end);
		}
	}
}

void symbol__calc_percent(struct symbol *sym, struct evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);

	annotation__calc_percent(notes, evsel, symbol__size(sym));
}

static int evsel__get_arch(struct evsel *evsel, struct arch **parch)
{
	struct perf_env *env = evsel__env(evsel);
	const char *arch_name = perf_env__arch(env);
	struct arch *arch;
	int err;

	if (!arch_name) {
		*parch = NULL;
		return errno;
	}

	*parch = arch = arch__find(arch_name);
	if (arch == NULL) {
		pr_err("%s: unsupported arch %s\n", __func__, arch_name);
		return ENOTSUP;
	}

	if (arch->init) {
		err = arch->init(arch, env ? env->cpuid : NULL);
		if (err) {
			pr_err("%s: failed to initialize %s arch priv area\n",
			       __func__, arch->name);
			return err;
		}
	}
	return 0;
}

int symbol__annotate(struct map_symbol *ms, struct evsel *evsel,
		     struct arch **parch)
{
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	struct annotate_args args = {
		.evsel		= evsel,
		.options	= &annotate_opts,
	};
	struct arch *arch = NULL;
	int err, nr;

	err = evsel__get_arch(evsel, &arch);
	if (err < 0)
		return err;

	if (parch)
		*parch = arch;

	if (notes->src && !list_empty(&notes->src->source))
		return 0;

	args.arch = arch;
	args.ms = *ms;

	if (notes->src == NULL) {
		notes->src = annotated_source__new();
		if (notes->src == NULL)
			return -1;
	}

	nr = 0;
	if (evsel__is_group_event(evsel)) {
		struct evsel *pos;

		for_each_group_evsel(pos, evsel) {
			if (symbol_conf.skip_empty &&
			    evsel__hists(pos)->stats.nr_samples == 0)
				continue;
			nr++;
		}
	}
	notes->src->nr_events = nr ? nr : 1;

	if (annotate_opts.full_addr)
		notes->src->start = map__objdump_2mem(ms->map, ms->sym->start);
	else
		notes->src->start = map__rip_2objdump(ms->map, ms->sym->start);

	return symbol__disassemble(sym, &args);
}

static void insert_source_line(struct rb_root *root, struct annotation_line *al)
{
	struct annotation_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	unsigned int percent_type = annotate_opts.percent_type;
	int i, ret;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct annotation_line, rb_node);

		ret = strcmp(iter->path, al->path);
		if (ret == 0) {
			for (i = 0; i < al->data_nr; i++) {
				iter->data[i].percent_sum += annotation_data__percent(&al->data[i],
										      percent_type);
			}
			return;
		}

		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	for (i = 0; i < al->data_nr; i++) {
		al->data[i].percent_sum = annotation_data__percent(&al->data[i],
								   percent_type);
	}

	rb_link_node(&al->rb_node, parent, p);
	rb_insert_color(&al->rb_node, root);
}

static int cmp_source_line(struct annotation_line *a, struct annotation_line *b)
{
	int i;

	for (i = 0; i < a->data_nr; i++) {
		if (a->data[i].percent_sum == b->data[i].percent_sum)
			continue;
		return a->data[i].percent_sum > b->data[i].percent_sum;
	}

	return 0;
}

static void __resort_source_line(struct rb_root *root, struct annotation_line *al)
{
	struct annotation_line *iter;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct annotation_line, rb_node);

		if (cmp_source_line(al, iter))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&al->rb_node, parent, p);
	rb_insert_color(&al->rb_node, root);
}

static void resort_source_line(struct rb_root *dest_root, struct rb_root *src_root)
{
	struct annotation_line *al;
	struct rb_node *node;

	node = rb_first(src_root);
	while (node) {
		struct rb_node *next;

		al = rb_entry(node, struct annotation_line, rb_node);
		next = rb_next(node);
		rb_erase(node, src_root);

		__resort_source_line(dest_root, al);
		node = next;
	}
}

static void print_summary(struct rb_root *root, const char *filename)
{
	struct annotation_line *al;
	struct rb_node *node;

	printf("\nSorted summary for file %s\n", filename);
	printf("----------------------------------------------\n\n");

	if (RB_EMPTY_ROOT(root)) {
		printf(" Nothing higher than %1.1f%%\n", MIN_GREEN);
		return;
	}

	node = rb_first(root);
	while (node) {
		double percent, percent_max = 0.0;
		const char *color;
		char *path;
		int i;

		al = rb_entry(node, struct annotation_line, rb_node);
		for (i = 0; i < al->data_nr; i++) {
			percent = al->data[i].percent_sum;
			color = get_percent_color(percent);
			color_fprintf(stdout, color, " %7.2f", percent);

			if (percent > percent_max)
				percent_max = percent;
		}

		path = al->path;
		color = get_percent_color(percent_max);
		color_fprintf(stdout, color, " %s\n", path);

		node = rb_next(node);
	}
}

static void symbol__annotate_hits(struct symbol *sym, struct evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel);
	u64 len = symbol__size(sym), offset;

	for (offset = 0; offset < len; ++offset) {
		struct sym_hist_entry *entry;

		entry = annotated_source__hist_entry(notes->src, evsel, offset);
		if (entry && entry->nr_samples != 0)
			printf("%*" PRIx64 ": %" PRIu64 "\n", BITS_PER_LONG / 2,
			       sym->start + offset, entry->nr_samples);
	}
	printf("%*s: %" PRIu64 "\n", BITS_PER_LONG / 2, "h->nr_samples", h->nr_samples);
}

static int annotated_source__addr_fmt_width(struct list_head *lines, u64 start)
{
	char bf[32];
	struct annotation_line *line;

	list_for_each_entry_reverse(line, lines, node) {
		if (line->offset != -1)
			return scnprintf(bf, sizeof(bf), "%" PRIx64, start + line->offset);
	}

	return 0;
}

int symbol__annotate_printf(struct map_symbol *ms, struct evsel *evsel)
{
	struct map *map = ms->map;
	struct symbol *sym = ms->sym;
	struct dso *dso = map__dso(map);
	char *filename;
	const char *d_filename;
	const char *evsel_name = evsel__name(evsel);
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel);
	struct annotation_line *pos, *queue = NULL;
	struct annotation_options *opts = &annotate_opts;
	u64 start = map__rip_2objdump(map, sym->start);
	int printed = 2, queue_len = 0, addr_fmt_width;
	int more = 0;
	bool context = opts->context;
	u64 len;
	int width = annotation__pcnt_width(notes);
	int graph_dotted_len;
	char buf[512];

	filename = strdup(dso__long_name(dso));
	if (!filename)
		return -ENOMEM;

	if (opts->full_path)
		d_filename = filename;
	else
		d_filename = basename(filename);

	len = symbol__size(sym);

	if (evsel__is_group_event(evsel)) {
		evsel__group_desc(evsel, buf, sizeof(buf));
		evsel_name = buf;
	}

	graph_dotted_len = printf(" %-*.*s|	Source code & Disassembly of %s for %s (%" PRIu64 " samples, "
				  "percent: %s)\n",
				  width, width, symbol_conf.show_total_period ? "Period" :
				  symbol_conf.show_nr_samples ? "Samples" : "Percent",
				  d_filename, evsel_name, h->nr_samples,
				  percent_type_str(opts->percent_type));

	printf("%-*.*s----\n",
	       graph_dotted_len, graph_dotted_len, graph_dotted_line);

	if (verbose > 0)
		symbol__annotate_hits(sym, evsel);

	addr_fmt_width = annotated_source__addr_fmt_width(&notes->src->source, start);

	list_for_each_entry(pos, &notes->src->source, node) {
		int err;

		if (context && queue == NULL) {
			queue = pos;
			queue_len = 0;
		}

		err = annotation_line__print(pos, sym, start, evsel, len,
					     opts->min_pcnt, printed, opts->max_lines,
					     queue, addr_fmt_width, opts->percent_type);

		switch (err) {
		case 0:
			++printed;
			if (context) {
				printed += queue_len;
				queue = NULL;
				queue_len = 0;
			}
			break;
		case 1:
			/* filtered by max_lines */
			++more;
			break;
		case -1:
		default:
			/*
			 * Filtered by min_pcnt or non IP lines when
			 * context != 0
			 */
			if (!context)
				break;
			if (queue_len == context)
				queue = list_entry(queue->node.next, typeof(*queue), node);
			else
				++queue_len;
			break;
		}
	}

	free(filename);

	return more;
}

static void FILE__set_percent_color(void *fp __maybe_unused,
				    double percent __maybe_unused,
				    bool current __maybe_unused)
{
}

static int FILE__set_jumps_percent_color(void *fp __maybe_unused,
					 int nr __maybe_unused, bool current __maybe_unused)
{
	return 0;
}

static int FILE__set_color(void *fp __maybe_unused, int color __maybe_unused)
{
	return 0;
}

static void FILE__printf(void *fp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(fp, fmt, args);
	va_end(args);
}

static void FILE__write_graph(void *fp, int graph)
{
	const char *s;
	switch (graph) {

	case DARROW_CHAR: s = "↓"; break;
	case UARROW_CHAR: s = "↑"; break;
	case LARROW_CHAR: s = "←"; break;
	case RARROW_CHAR: s = "→"; break;
	default:		s = "?"; break;
	}

	fputs(s, fp);
}

static int symbol__annotate_fprintf2(struct symbol *sym, FILE *fp)
{
	struct annotation *notes = symbol__annotation(sym);
	struct annotation_write_ops wops = {
		.first_line		 = true,
		.obj			 = fp,
		.set_color		 = FILE__set_color,
		.set_percent_color	 = FILE__set_percent_color,
		.set_jumps_percent_color = FILE__set_jumps_percent_color,
		.printf			 = FILE__printf,
		.write_graph		 = FILE__write_graph,
	};
	struct annotation_line *al;

	list_for_each_entry(al, &notes->src->source, node) {
		if (annotation_line__filter(al))
			continue;
		annotation_line__write(al, notes, &wops);
		fputc('\n', fp);
		wops.first_line = false;
	}

	return 0;
}

int map_symbol__annotation_dump(struct map_symbol *ms, struct evsel *evsel)
{
	const char *ev_name = evsel__name(evsel);
	char buf[1024];
	char *filename;
	int err = -1;
	FILE *fp;

	if (asprintf(&filename, "%s.annotation", ms->sym->name) < 0)
		return -1;

	fp = fopen(filename, "w");
	if (fp == NULL)
		goto out_free_filename;

	if (evsel__is_group_event(evsel)) {
		evsel__group_desc(evsel, buf, sizeof(buf));
		ev_name = buf;
	}

	fprintf(fp, "%s() %s\nEvent: %s\n\n",
		ms->sym->name, dso__long_name(map__dso(ms->map)), ev_name);
	symbol__annotate_fprintf2(ms->sym, fp);

	fclose(fp);
	err = 0;
out_free_filename:
	free(filename);
	return err;
}

void symbol__annotate_zero_histogram(struct symbol *sym, struct evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel);

	memset(h, 0, sizeof(*notes->src->histograms) * notes->src->nr_histograms);
}

void symbol__annotate_decay_histogram(struct symbol *sym, struct evsel *evsel)
{
	struct annotation *notes = symbol__annotation(sym);
	struct sym_hist *h = annotation__histogram(notes, evsel);
	struct annotation_line *al;

	h->nr_samples = 0;
	list_for_each_entry(al, &notes->src->source, node) {
		struct sym_hist_entry *entry;

		if (al->offset == -1)
			continue;

		entry = annotated_source__hist_entry(notes->src, evsel, al->offset);
		if (entry == NULL)
			continue;

		entry->nr_samples = entry->nr_samples * 7 / 8;
		h->nr_samples += entry->nr_samples;
	}
}

void annotated_source__purge(struct annotated_source *as)
{
	struct annotation_line *al, *n;

	list_for_each_entry_safe(al, n, &as->source, node) {
		list_del_init(&al->node);
		disasm_line__free(disasm_line(al));
	}
}

static size_t disasm_line__fprintf(struct disasm_line *dl, FILE *fp)
{
	size_t printed;

	if (dl->al.offset == -1)
		return fprintf(fp, "%s\n", dl->al.line);

	printed = fprintf(fp, "%#" PRIx64 " %s", dl->al.offset, dl->ins.name);

	if (dl->ops.raw[0] != '\0') {
		printed += fprintf(fp, "%.*s %s\n", 6 - (int)printed, " ",
				   dl->ops.raw);
	}

	return printed + fprintf(fp, "\n");
}

size_t disasm__fprintf(struct list_head *head, FILE *fp)
{
	struct disasm_line *pos;
	size_t printed = 0;

	list_for_each_entry(pos, head, al.node)
		printed += disasm_line__fprintf(pos, fp);

	return printed;
}

bool disasm_line__is_valid_local_jump(struct disasm_line *dl, struct symbol *sym)
{
	if (!dl || !dl->ins.ops || !ins__is_jump(&dl->ins) ||
	    !disasm_line__has_local_offset(dl) || dl->ops.target.offset < 0 ||
	    dl->ops.target.offset >= (s64)symbol__size(sym))
		return false;

	return true;
}

static void
annotation__mark_jump_targets(struct annotation *notes, struct symbol *sym)
{
	struct annotation_line *al;

	/* PLT symbols contain external offsets */
	if (strstr(sym->name, "@plt"))
		return;

	list_for_each_entry(al, &notes->src->source, node) {
		struct disasm_line *dl;
		struct annotation_line *target;

		dl = disasm_line(al);

		if (!disasm_line__is_valid_local_jump(dl, sym))
			continue;

		target = annotated_source__get_line(notes->src,
						    dl->ops.target.offset);
		/*
		 * FIXME: Oops, no jump target? Buggy disassembler? Or do we
		 * have to adjust to the previous offset?
		 */
		if (target == NULL)
			continue;

		if (++target->jump_sources > notes->src->max_jump_sources)
			notes->src->max_jump_sources = target->jump_sources;
	}
}

static void annotation__set_index(struct annotation *notes)
{
	struct annotation_line *al;
	struct annotated_source *src = notes->src;

	src->widths.max_line_len = 0;
	src->nr_entries = 0;
	src->nr_asm_entries = 0;

	list_for_each_entry(al, &src->source, node) {
		size_t line_len = strlen(al->line);

		if (src->widths.max_line_len < line_len)
			src->widths.max_line_len = line_len;
		al->idx = src->nr_entries++;
		if (al->offset != -1)
			al->idx_asm = src->nr_asm_entries++;
		else
			al->idx_asm = -1;
	}
}

static inline int width_jumps(int n)
{
	if (n >= 100)
		return 5;
	if (n / 10)
		return 2;
	return 1;
}

static int annotation__max_ins_name(struct annotation *notes)
{
	int max_name = 0, len;
	struct annotation_line *al;

        list_for_each_entry(al, &notes->src->source, node) {
		if (al->offset == -1)
			continue;

		len = strlen(disasm_line(al)->ins.name);
		if (max_name < len)
			max_name = len;
	}

	return max_name;
}

static void
annotation__init_column_widths(struct annotation *notes, struct symbol *sym)
{
	notes->src->widths.addr = notes->src->widths.target =
		notes->src->widths.min_addr = hex_width(symbol__size(sym));
	notes->src->widths.max_addr = hex_width(sym->end);
	notes->src->widths.jumps = width_jumps(notes->src->max_jump_sources);
	notes->src->widths.max_ins_name = annotation__max_ins_name(notes);
}

void annotation__update_column_widths(struct annotation *notes)
{
	if (annotate_opts.use_offset)
		notes->src->widths.target = notes->src->widths.min_addr;
	else if (annotate_opts.full_addr)
		notes->src->widths.target = BITS_PER_LONG / 4;
	else
		notes->src->widths.target = notes->src->widths.max_addr;

	notes->src->widths.addr = notes->src->widths.target;

	if (annotate_opts.show_nr_jumps)
		notes->src->widths.addr += notes->src->widths.jumps + 1;
}

void annotation__toggle_full_addr(struct annotation *notes, struct map_symbol *ms)
{
	annotate_opts.full_addr = !annotate_opts.full_addr;

	if (annotate_opts.full_addr)
		notes->src->start = map__objdump_2mem(ms->map, ms->sym->start);
	else
		notes->src->start = map__rip_2objdump(ms->map, ms->sym->start);

	annotation__update_column_widths(notes);
}

static void annotation__calc_lines(struct annotation *notes, struct map_symbol *ms,
				   struct rb_root *root)
{
	struct annotation_line *al;
	struct rb_root tmp_root = RB_ROOT;

	list_for_each_entry(al, &notes->src->source, node) {
		double percent_max = 0.0;
		u64 addr;
		int i;

		for (i = 0; i < al->data_nr; i++) {
			double percent;

			percent = annotation_data__percent(&al->data[i],
							   annotate_opts.percent_type);

			if (percent > percent_max)
				percent_max = percent;
		}

		if (percent_max <= 0.5)
			continue;

		addr = map__rip_2objdump(ms->map, ms->sym->start);
		al->path = get_srcline(map__dso(ms->map), addr + al->offset, NULL,
				       false, true, ms->sym->start + al->offset);
		insert_source_line(&tmp_root, al);
	}

	resort_source_line(root, &tmp_root);
}

static void symbol__calc_lines(struct map_symbol *ms, struct rb_root *root)
{
	struct annotation *notes = symbol__annotation(ms->sym);

	annotation__calc_lines(notes, ms, root);
}

int symbol__tty_annotate2(struct map_symbol *ms, struct evsel *evsel)
{
	struct dso *dso = map__dso(ms->map);
	struct symbol *sym = ms->sym;
	struct rb_root source_line = RB_ROOT;
	struct hists *hists = evsel__hists(evsel);
	char buf[1024];
	int err;

	err = symbol__annotate2(ms, evsel, NULL);
	if (err) {
		char msg[BUFSIZ];

		dso__set_annotate_warned(dso);
		symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
		ui__error("Couldn't annotate %s:\n%s", sym->name, msg);
		return -1;
	}

	if (annotate_opts.print_lines) {
		srcline_full_filename = annotate_opts.full_path;
		symbol__calc_lines(ms, &source_line);
		print_summary(&source_line, dso__long_name(dso));
	}

	hists__scnprintf_title(hists, buf, sizeof(buf));
	fprintf(stdout, "%s, [percent: %s]\n%s() %s\n",
		buf, percent_type_str(annotate_opts.percent_type), sym->name, dso__long_name(dso));
	symbol__annotate_fprintf2(sym, stdout);

	annotated_source__purge(symbol__annotation(sym)->src);

	return 0;
}

int symbol__tty_annotate(struct map_symbol *ms, struct evsel *evsel)
{
	struct dso *dso = map__dso(ms->map);
	struct symbol *sym = ms->sym;
	struct rb_root source_line = RB_ROOT;
	int err;

	err = symbol__annotate(ms, evsel, NULL);
	if (err) {
		char msg[BUFSIZ];

		dso__set_annotate_warned(dso);
		symbol__strerror_disassemble(ms, err, msg, sizeof(msg));
		ui__error("Couldn't annotate %s:\n%s", sym->name, msg);
		return -1;
	}

	symbol__calc_percent(sym, evsel);

	if (annotate_opts.print_lines) {
		srcline_full_filename = annotate_opts.full_path;
		symbol__calc_lines(ms, &source_line);
		print_summary(&source_line, dso__long_name(dso));
	}

	symbol__annotate_printf(ms, evsel);

	annotated_source__purge(symbol__annotation(sym)->src);

	return 0;
}

bool ui__has_annotation(void)
{
	return use_browser == 1 && perf_hpp_list.sym;
}


static double annotation_line__max_percent(struct annotation_line *al,
					   unsigned int percent_type)
{
	double percent_max = 0.0;
	int i;

	for (i = 0; i < al->data_nr; i++) {
		double percent;

		percent = annotation_data__percent(&al->data[i],
						   percent_type);

		if (percent > percent_max)
			percent_max = percent;
	}

	return percent_max;
}

static void disasm_line__write(struct disasm_line *dl, struct annotation *notes,
			       void *obj, char *bf, size_t size,
			       void (*obj__printf)(void *obj, const char *fmt, ...),
			       void (*obj__write_graph)(void *obj, int graph))
{
	if (dl->ins.ops && dl->ins.ops->scnprintf) {
		if (ins__is_jump(&dl->ins)) {
			bool fwd;

			if (dl->ops.target.outside)
				goto call_like;
			fwd = dl->ops.target.offset > dl->al.offset;
			obj__write_graph(obj, fwd ? DARROW_CHAR : UARROW_CHAR);
			obj__printf(obj, " ");
		} else if (ins__is_call(&dl->ins)) {
call_like:
			obj__write_graph(obj, RARROW_CHAR);
			obj__printf(obj, " ");
		} else if (ins__is_ret(&dl->ins)) {
			obj__write_graph(obj, LARROW_CHAR);
			obj__printf(obj, " ");
		} else {
			obj__printf(obj, "  ");
		}
	} else {
		obj__printf(obj, "  ");
	}

	disasm_line__scnprintf(dl, bf, size, !annotate_opts.use_offset,
			       notes->src->widths.max_ins_name);
}

static void ipc_coverage_string(char *bf, int size, struct annotation *notes)
{
	double ipc = 0.0, coverage = 0.0;
	struct annotated_branch *branch = annotation__get_branch(notes);

	if (branch && branch->hit_cycles)
		ipc = branch->hit_insn / ((double)branch->hit_cycles);

	if (branch && branch->total_insn) {
		coverage = branch->cover_insn * 100.0 /
			((double)branch->total_insn);
	}

	scnprintf(bf, size, "(Average IPC: %.2f, IPC Coverage: %.1f%%)",
		  ipc, coverage);
}

int annotation_br_cntr_abbr_list(char **str, struct evsel *evsel, bool header)
{
	struct evsel *pos;
	struct strbuf sb;

	if (evsel->evlist->nr_br_cntr <= 0)
		return -ENOTSUP;

	strbuf_init(&sb, /*hint=*/ 0);

	if (header && strbuf_addf(&sb, "# Branch counter abbr list:\n"))
		goto err;

	evlist__for_each_entry(evsel->evlist, pos) {
		if (!(pos->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_COUNTERS))
			continue;
		if (header && strbuf_addf(&sb, "#"))
			goto err;

		if (strbuf_addf(&sb, " %s = %s\n", pos->name, pos->abbr_name))
			goto err;
	}

	if (header && strbuf_addf(&sb, "#"))
		goto err;
	if (strbuf_addf(&sb, " '-' No event occurs\n"))
		goto err;

	if (header && strbuf_addf(&sb, "#"))
		goto err;
	if (strbuf_addf(&sb, " '+' Event occurrences may be lost due to branch counter saturated\n"))
		goto err;

	*str = strbuf_detach(&sb, NULL);

	return 0;
err:
	strbuf_release(&sb);
	return -ENOMEM;
}

/* Assume the branch counter saturated at 3 */
#define ANNOTATION_BR_CNTR_SATURATION		3

int annotation_br_cntr_entry(char **str, int br_cntr_nr,
			     u64 *br_cntr, int num_aggr,
			     struct evsel *evsel)
{
	struct evsel *pos = evsel ? evlist__first(evsel->evlist) : NULL;
	bool saturated = false;
	int i, j, avg, used;
	struct strbuf sb;

	strbuf_init(&sb, /*hint=*/ 0);
	for (i = 0; i < br_cntr_nr; i++) {
		used = 0;
		avg = ceil((double)(br_cntr[i] & ~ANNOTATION__BR_CNTR_SATURATED_FLAG) /
			   (double)num_aggr);

		/*
		 * A histogram with the abbr name is displayed by default.
		 * With -v, the exact number of branch counter is displayed.
		 */
		if (verbose) {
			evlist__for_each_entry_from(evsel->evlist, pos) {
				if ((pos->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_COUNTERS) &&
				    (pos->br_cntr_idx == i))
				break;
			}
			if (strbuf_addstr(&sb, pos->abbr_name))
				goto err;

			if (!br_cntr[i]) {
				if (strbuf_addstr(&sb, "=-"))
					goto err;
			} else {
				if (strbuf_addf(&sb, "=%d", avg))
					goto err;
			}
			if (br_cntr[i] & ANNOTATION__BR_CNTR_SATURATED_FLAG) {
				if (strbuf_addch(&sb, '+'))
					goto err;
			} else {
				if (strbuf_addch(&sb, ' '))
					goto err;
			}

			if ((i < br_cntr_nr - 1) && strbuf_addch(&sb, ','))
				goto err;
			continue;
		}

		if (strbuf_addch(&sb, '|'))
			goto err;

		if (!br_cntr[i]) {
			if (strbuf_addch(&sb, '-'))
				goto err;
			used++;
		} else {
			evlist__for_each_entry_from(evsel->evlist, pos) {
				if ((pos->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_COUNTERS) &&
				    (pos->br_cntr_idx == i))
					break;
			}
			if (br_cntr[i] & ANNOTATION__BR_CNTR_SATURATED_FLAG)
				saturated = true;

			for (j = 0; j < avg; j++, used++) {
				/* Print + if the number of logged events > 3 */
				if (j >= ANNOTATION_BR_CNTR_SATURATION) {
					saturated = true;
					break;
				}
				if (strbuf_addstr(&sb, pos->abbr_name))
					goto err;
			}

			if (saturated) {
				if (strbuf_addch(&sb, '+'))
					goto err;
				used++;
			}
			pos = list_next_entry(pos, core.node);
		}

		for (j = used; j < ANNOTATION_BR_CNTR_SATURATION + 1; j++) {
			if (strbuf_addch(&sb, ' '))
				goto err;
		}
	}

	if (!verbose && strbuf_addch(&sb, br_cntr_nr ? '|' : ' '))
		goto err;

	*str = strbuf_detach(&sb, NULL);

	return 0;
err:
	strbuf_release(&sb);
	return -ENOMEM;
}

static void __annotation_line__write(struct annotation_line *al, struct annotation *notes,
				     bool first_line, bool current_entry, bool change_color, int width,
				     void *obj, unsigned int percent_type,
				     int  (*obj__set_color)(void *obj, int color),
				     void (*obj__set_percent_color)(void *obj, double percent, bool current),
				     int  (*obj__set_jumps_percent_color)(void *obj, int nr, bool current),
				     void (*obj__printf)(void *obj, const char *fmt, ...),
				     void (*obj__write_graph)(void *obj, int graph))

{
	double percent_max = annotation_line__max_percent(al, percent_type);
	int pcnt_width = annotation__pcnt_width(notes),
	    cycles_width = annotation__cycles_width(notes);
	bool show_title = false;
	char bf[256];
	int printed;

	if (first_line && (al->offset == -1 || percent_max == 0.0)) {
		if (notes->branch && al->cycles) {
			if (al->cycles->ipc == 0.0 && al->cycles->avg == 0)
				show_title = true;
		} else
			show_title = true;
	}

	if (al->offset != -1 && percent_max != 0.0) {
		int i;

		for (i = 0; i < al->data_nr; i++) {
			double percent;

			percent = annotation_data__percent(&al->data[i], percent_type);

			obj__set_percent_color(obj, percent, current_entry);
			if (symbol_conf.show_total_period) {
				obj__printf(obj, "%11" PRIu64 " ", al->data[i].he.period);
			} else if (symbol_conf.show_nr_samples) {
				obj__printf(obj, "%7" PRIu64 " ",
						   al->data[i].he.nr_samples);
			} else {
				obj__printf(obj, "%7.2f ", percent);
			}
		}
	} else {
		obj__set_percent_color(obj, 0, current_entry);

		if (!show_title)
			obj__printf(obj, "%-*s", pcnt_width, " ");
		else {
			obj__printf(obj, "%-*s", pcnt_width,
					   symbol_conf.show_total_period ? "Period" :
					   symbol_conf.show_nr_samples ? "Samples" : "Percent");
		}
	}

	if (notes->branch) {
		if (al->cycles && al->cycles->ipc)
			obj__printf(obj, "%*.2f ", ANNOTATION__IPC_WIDTH - 1, al->cycles->ipc);
		else if (!show_title)
			obj__printf(obj, "%*s", ANNOTATION__IPC_WIDTH, " ");
		else
			obj__printf(obj, "%*s ", ANNOTATION__IPC_WIDTH - 1, "IPC");

		if (!annotate_opts.show_minmax_cycle) {
			if (al->cycles && al->cycles->avg)
				obj__printf(obj, "%*" PRIu64 " ",
					   ANNOTATION__CYCLES_WIDTH - 1, al->cycles->avg);
			else if (!show_title)
				obj__printf(obj, "%*s",
					    ANNOTATION__CYCLES_WIDTH, " ");
			else
				obj__printf(obj, "%*s ",
					    ANNOTATION__CYCLES_WIDTH - 1,
					    "Cycle");
		} else {
			if (al->cycles) {
				char str[32];

				scnprintf(str, sizeof(str),
					"%" PRIu64 "(%" PRIu64 "/%" PRIu64 ")",
					al->cycles->avg, al->cycles->min,
					al->cycles->max);

				obj__printf(obj, "%*s ",
					    ANNOTATION__MINMAX_CYCLES_WIDTH - 1,
					    str);
			} else if (!show_title)
				obj__printf(obj, "%*s",
					    ANNOTATION__MINMAX_CYCLES_WIDTH,
					    " ");
			else
				obj__printf(obj, "%*s ",
					    ANNOTATION__MINMAX_CYCLES_WIDTH - 1,
					    "Cycle(min/max)");
		}

		if (annotate_opts.show_br_cntr) {
			if (show_title) {
				obj__printf(obj, "%*s ",
					    ANNOTATION__BR_CNTR_WIDTH,
					    "Branch Counter");
			} else {
				char *buf;

				if (!annotation_br_cntr_entry(&buf, al->br_cntr_nr, al->br_cntr,
							      al->num_aggr, al->evsel)) {
					obj__printf(obj, "%*s ", ANNOTATION__BR_CNTR_WIDTH, buf);
					free(buf);
				}
			}
		}

		if (show_title && !*al->line) {
			ipc_coverage_string(bf, sizeof(bf), notes);
			obj__printf(obj, "%*s", ANNOTATION__AVG_IPC_WIDTH, bf);
		}
	}

	obj__printf(obj, " ");

	if (!*al->line)
		obj__printf(obj, "%-*s", width - pcnt_width - cycles_width, " ");
	else if (al->offset == -1) {
		if (al->line_nr && annotate_opts.show_linenr)
			printed = scnprintf(bf, sizeof(bf), "%-*d ",
					    notes->src->widths.addr + 1, al->line_nr);
		else
			printed = scnprintf(bf, sizeof(bf), "%-*s  ",
					    notes->src->widths.addr, " ");
		obj__printf(obj, bf);
		obj__printf(obj, "%-*s", width - printed - pcnt_width - cycles_width + 1, al->line);
	} else {
		u64 addr = al->offset;
		int color = -1;

		if (!annotate_opts.use_offset)
			addr += notes->src->start;

		if (!annotate_opts.use_offset) {
			printed = scnprintf(bf, sizeof(bf), "%" PRIx64 ": ", addr);
		} else {
			if (al->jump_sources &&
			    annotate_opts.offset_level >= ANNOTATION__OFFSET_JUMP_TARGETS) {
				if (annotate_opts.show_nr_jumps) {
					int prev;
					printed = scnprintf(bf, sizeof(bf), "%*d ",
							    notes->src->widths.jumps,
							    al->jump_sources);
					prev = obj__set_jumps_percent_color(obj, al->jump_sources,
									    current_entry);
					obj__printf(obj, bf);
					obj__set_color(obj, prev);
				}
print_addr:
				printed = scnprintf(bf, sizeof(bf), "%*" PRIx64 ": ",
						    notes->src->widths.target, addr);
			} else if (ins__is_call(&disasm_line(al)->ins) &&
				   annotate_opts.offset_level >= ANNOTATION__OFFSET_CALL) {
				goto print_addr;
			} else if (annotate_opts.offset_level == ANNOTATION__MAX_OFFSET_LEVEL) {
				goto print_addr;
			} else {
				printed = scnprintf(bf, sizeof(bf), "%-*s  ",
						    notes->src->widths.addr, " ");
			}
		}

		if (change_color)
			color = obj__set_color(obj, HE_COLORSET_ADDR);
		obj__printf(obj, bf);
		if (change_color)
			obj__set_color(obj, color);

		disasm_line__write(disasm_line(al), notes, obj, bf, sizeof(bf), obj__printf, obj__write_graph);

		obj__printf(obj, "%-*s", width - pcnt_width - cycles_width - 3 - printed, bf);
	}

}

void annotation_line__write(struct annotation_line *al, struct annotation *notes,
			    struct annotation_write_ops *wops)
{
	__annotation_line__write(al, notes, wops->first_line, wops->current_entry,
				 wops->change_color, wops->width, wops->obj,
				 annotate_opts.percent_type,
				 wops->set_color, wops->set_percent_color,
				 wops->set_jumps_percent_color, wops->printf,
				 wops->write_graph);
}

int symbol__annotate2(struct map_symbol *ms, struct evsel *evsel,
		      struct arch **parch)
{
	struct symbol *sym = ms->sym;
	struct annotation *notes = symbol__annotation(sym);
	size_t size = symbol__size(sym);
	int err;

	err = symbol__annotate(ms, evsel, parch);
	if (err)
		return err;

	symbol__calc_percent(sym, evsel);

	annotation__set_index(notes);
	annotation__mark_jump_targets(notes, sym);

	err = annotation__compute_ipc(notes, size, evsel);
	if (err)
		return err;

	annotation__init_column_widths(notes, sym);
	annotation__update_column_widths(notes);
	sym->annotate2 = 1;

	return 0;
}

const char * const perf_disassembler__strs[] = {
	[PERF_DISASM_UNKNOWN]  = "unknown",
	[PERF_DISASM_LLVM]     = "llvm",
	[PERF_DISASM_CAPSTONE] = "capstone",
	[PERF_DISASM_OBJDUMP]  = "objdump",
};


static void annotation_options__add_disassembler(struct annotation_options *options,
						 enum perf_disassembler dis)
{
	for (u8 i = 0; i < ARRAY_SIZE(options->disassemblers); i++) {
		if (options->disassemblers[i] == dis) {
			/* Disassembler is already present then don't add again. */
			return;
		}
		if (options->disassemblers[i] == PERF_DISASM_UNKNOWN) {
			/* Found a free slot. */
			options->disassemblers[i] = dis;
			return;
		}
	}
	pr_err("Failed to add disassembler %d\n", dis);
}

static int annotation_options__add_disassemblers_str(struct annotation_options *options,
						const char *str)
{
	while (str && *str != '\0') {
		const char *comma = strchr(str, ',');
		int len = comma ? comma - str : (int)strlen(str);
		bool match = false;

		for (u8 i = 0; i < ARRAY_SIZE(perf_disassembler__strs); i++) {
			const char *dis_str = perf_disassembler__strs[i];

			if (len == (int)strlen(dis_str) && !strncmp(str, dis_str, len)) {
				annotation_options__add_disassembler(options, i);
				match = true;
				break;
			}
		}
		if (!match) {
			pr_err("Invalid disassembler '%.*s'\n", len, str);
			return -1;
		}
		str = comma ? comma + 1 : NULL;
	}
	return 0;
}

static int annotation__config(const char *var, const char *value, void *data)
{
	struct annotation_options *opt = data;

	if (!strstarts(var, "annotate."))
		return 0;

	if (!strcmp(var, "annotate.offset_level")) {
		perf_config_u8(&opt->offset_level, "offset_level", value);

		if (opt->offset_level > ANNOTATION__MAX_OFFSET_LEVEL)
			opt->offset_level = ANNOTATION__MAX_OFFSET_LEVEL;
		else if (opt->offset_level < ANNOTATION__MIN_OFFSET_LEVEL)
			opt->offset_level = ANNOTATION__MIN_OFFSET_LEVEL;
	} else if (!strcmp(var, "annotate.disassemblers")) {
		int err = annotation_options__add_disassemblers_str(opt, value);

		if (err)
			return err;
	} else if (!strcmp(var, "annotate.hide_src_code")) {
		opt->hide_src_code = perf_config_bool("hide_src_code", value);
	} else if (!strcmp(var, "annotate.jump_arrows")) {
		opt->jump_arrows = perf_config_bool("jump_arrows", value);
	} else if (!strcmp(var, "annotate.show_linenr")) {
		opt->show_linenr = perf_config_bool("show_linenr", value);
	} else if (!strcmp(var, "annotate.show_nr_jumps")) {
		opt->show_nr_jumps = perf_config_bool("show_nr_jumps", value);
	} else if (!strcmp(var, "annotate.show_nr_samples")) {
		symbol_conf.show_nr_samples = perf_config_bool("show_nr_samples",
								value);
	} else if (!strcmp(var, "annotate.show_total_period")) {
		symbol_conf.show_total_period = perf_config_bool("show_total_period",
								value);
	} else if (!strcmp(var, "annotate.use_offset")) {
		opt->use_offset = perf_config_bool("use_offset", value);
	} else if (!strcmp(var, "annotate.disassembler_style")) {
		opt->disassembler_style = strdup(value);
		if (!opt->disassembler_style) {
			pr_err("Not enough memory for annotate.disassembler_style\n");
			return -1;
		}
	} else if (!strcmp(var, "annotate.objdump")) {
		opt->objdump_path = strdup(value);
		if (!opt->objdump_path) {
			pr_err("Not enough memory for annotate.objdump\n");
			return -1;
		}
	} else if (!strcmp(var, "annotate.addr2line")) {
		symbol_conf.addr2line_path = strdup(value);
		if (!symbol_conf.addr2line_path) {
			pr_err("Not enough memory for annotate.addr2line\n");
			return -1;
		}
	} else if (!strcmp(var, "annotate.demangle")) {
		symbol_conf.demangle = perf_config_bool("demangle", value);
	} else if (!strcmp(var, "annotate.demangle_kernel")) {
		symbol_conf.demangle_kernel = perf_config_bool("demangle_kernel", value);
	} else {
		pr_debug("%s variable unknown, ignoring...", var);
	}

	return 0;
}

void annotation_options__init(void)
{
	struct annotation_options *opt = &annotate_opts;

	memset(opt, 0, sizeof(*opt));

	/* Default values. */
	opt->use_offset = true;
	opt->jump_arrows = true;
	opt->annotate_src = true;
	opt->offset_level = ANNOTATION__OFFSET_JUMP_TARGETS;
	opt->percent_type = PERCENT_PERIOD_LOCAL;
}

void annotation_options__exit(void)
{
	zfree(&annotate_opts.disassembler_style);
	zfree(&annotate_opts.objdump_path);
}

static void annotation_options__default_init_disassemblers(struct annotation_options *options)
{
	if (options->disassemblers[0] != PERF_DISASM_UNKNOWN) {
		/* Already initialized. */
		return;
	}
#ifdef HAVE_LIBLLVM_SUPPORT
	annotation_options__add_disassembler(options, PERF_DISASM_LLVM);
#endif
#ifdef HAVE_LIBCAPSTONE_SUPPORT
	annotation_options__add_disassembler(options, PERF_DISASM_CAPSTONE);
#endif
	annotation_options__add_disassembler(options, PERF_DISASM_OBJDUMP);
}

void annotation_config__init(void)
{
	perf_config(annotation__config, &annotate_opts);
	annotation_options__default_init_disassemblers(&annotate_opts);
}

static unsigned int parse_percent_type(char *str1, char *str2)
{
	unsigned int type = (unsigned int) -1;

	if (!strcmp("period", str1)) {
		if (!strcmp("local", str2))
			type = PERCENT_PERIOD_LOCAL;
		else if (!strcmp("global", str2))
			type = PERCENT_PERIOD_GLOBAL;
	}

	if (!strcmp("hits", str1)) {
		if (!strcmp("local", str2))
			type = PERCENT_HITS_LOCAL;
		else if (!strcmp("global", str2))
			type = PERCENT_HITS_GLOBAL;
	}

	return type;
}

int annotate_parse_percent_type(const struct option *opt __maybe_unused, const char *_str,
				int unset __maybe_unused)
{
	unsigned int type;
	char *str1, *str2;
	int err = -1;

	str1 = strdup(_str);
	if (!str1)
		return -ENOMEM;

	str2 = strchr(str1, '-');
	if (!str2)
		goto out;

	*str2++ = 0;

	type = parse_percent_type(str1, str2);
	if (type == (unsigned int) -1)
		type = parse_percent_type(str2, str1);
	if (type != (unsigned int) -1) {
		annotate_opts.percent_type = type;
		err = 0;
	}

out:
	free(str1);
	return err;
}

int annotate_check_args(void)
{
	struct annotation_options *args = &annotate_opts;

	if (args->prefix_strip && !args->prefix) {
		pr_err("--prefix-strip requires --prefix\n");
		return -1;
	}
	return 0;
}

/*
 * Get register number and access offset from the given instruction.
 * It assumes AT&T x86 asm format like OFFSET(REG).  Maybe it needs
 * to revisit the format when it handles different architecture.
 * Fills @reg and @offset when return 0.
 */
static int extract_reg_offset(struct arch *arch, const char *str,
			      struct annotated_op_loc *op_loc)
{
	char *p;
	char *regname;

	if (arch->objdump.register_char == 0)
		return -1;

	/*
	 * It should start from offset, but it's possible to skip 0
	 * in the asm.  So 0(%rax) should be same as (%rax).
	 *
	 * However, it also start with a segment select register like
	 * %gs:0x18(%rbx).  In that case it should skip the part.
	 */
	if (*str == arch->objdump.register_char) {
		if (arch__is(arch, "x86")) {
			/* FIXME: Handle other segment registers */
			if (!strncmp(str, "%gs:", 4))
				op_loc->segment = INSN_SEG_X86_GS;
		}

		while (*str && !isdigit(*str) &&
		       *str != arch->objdump.memory_ref_char)
			str++;
	}

	op_loc->offset = strtol(str, &p, 0);

	p = strchr(p, arch->objdump.register_char);
	if (p == NULL)
		return -1;

	regname = strdup(p);
	if (regname == NULL)
		return -1;

	op_loc->reg1 = get_dwarf_regnum(regname, arch->e_machine, arch->e_flags);
	free(regname);

	/* Get the second register */
	if (op_loc->multi_regs) {
		p = strchr(p + 1, arch->objdump.register_char);
		if (p == NULL)
			return -1;

		regname = strdup(p);
		if (regname == NULL)
			return -1;

		op_loc->reg2 = get_dwarf_regnum(regname, arch->e_machine, arch->e_flags);
		free(regname);
	}
	return 0;
}

/**
 * annotate_get_insn_location - Get location of instruction
 * @arch: the architecture info
 * @dl: the target instruction
 * @loc: a buffer to save the data
 *
 * Get detailed location info (register and offset) in the instruction.
 * It needs both source and target operand and whether it accesses a
 * memory location.  The offset field is meaningful only when the
 * corresponding mem flag is set.  The reg2 field is meaningful only
 * when multi_regs flag is set.
 *
 * Some examples on x86:
 *
 *   mov  (%rax), %rcx   # src_reg1 = rax, src_mem = 1, src_offset = 0
 *                       # dst_reg1 = rcx, dst_mem = 0
 *
 *   mov  0x18, %r8      # src_reg1 = -1, src_mem = 0
 *                       # dst_reg1 = r8, dst_mem = 0
 *
 *   mov  %rsi, 8(%rbx,%rcx,4)  # src_reg1 = rsi, src_mem = 0, src_multi_regs = 0
 *                              # dst_reg1 = rbx, dst_reg2 = rcx, dst_mem = 1
 *                              # dst_multi_regs = 1, dst_offset = 8
 */
int annotate_get_insn_location(struct arch *arch, struct disasm_line *dl,
			       struct annotated_insn_loc *loc)
{
	struct ins_operands *ops;
	struct annotated_op_loc *op_loc;
	int i;

	if (ins__is_lock(&dl->ins))
		ops = dl->ops.locked.ops;
	else
		ops = &dl->ops;

	if (ops == NULL)
		return -1;

	memset(loc, 0, sizeof(*loc));

	for_each_insn_op_loc(loc, i, op_loc) {
		const char *insn_str = ops->source.raw;
		bool multi_regs = ops->source.multi_regs;
		bool mem_ref = ops->source.mem_ref;

		if (i == INSN_OP_TARGET) {
			insn_str = ops->target.raw;
			multi_regs = ops->target.multi_regs;
			mem_ref = ops->target.mem_ref;
		}

		/* Invalidate the register by default */
		op_loc->reg1 = -1;
		op_loc->reg2 = -1;

		if (insn_str == NULL) {
			if (!arch__is(arch, "powerpc"))
				continue;
		}

		/*
		 * For powerpc, call get_powerpc_regs function which extracts the
		 * required fields for op_loc, ie reg1, reg2, offset from the
		 * raw instruction.
		 */
		if (arch__is(arch, "powerpc")) {
			op_loc->mem_ref = mem_ref;
			op_loc->multi_regs = multi_regs;
			get_powerpc_regs(dl->raw.raw_insn, !i, op_loc);
		} else if (strchr(insn_str, arch->objdump.memory_ref_char)) {
			op_loc->mem_ref = true;
			op_loc->multi_regs = multi_regs;
			extract_reg_offset(arch, insn_str, op_loc);
		} else {
			char *s, *p = NULL;

			if (arch__is(arch, "x86")) {
				/* FIXME: Handle other segment registers */
				if (!strncmp(insn_str, "%gs:", 4)) {
					op_loc->segment = INSN_SEG_X86_GS;
					op_loc->offset = strtol(insn_str + 4,
								&p, 0);
					if (p && p != insn_str + 4)
						op_loc->imm = true;
					continue;
				}
			}

			s = strdup(insn_str);
			if (s == NULL)
				return -1;

			if (*s == arch->objdump.register_char)
				op_loc->reg1 = get_dwarf_regnum(s, arch->e_machine, arch->e_flags);
			else if (*s == arch->objdump.imm_char) {
				op_loc->offset = strtol(s + 1, &p, 0);
				if (p && p != s + 1)
					op_loc->imm = true;
			}
			free(s);
		}
	}

	return 0;
}

static struct disasm_line *find_disasm_line(struct symbol *sym, u64 ip,
					    bool allow_update)
{
	struct disasm_line *dl;
	struct annotation *notes;

	notes = symbol__annotation(sym);

	list_for_each_entry(dl, &notes->src->source, al.node) {
		if (dl->al.offset == -1)
			continue;

		if (sym->start + dl->al.offset == ip) {
			/*
			 * llvm-objdump places "lock" in a separate line and
			 * in that case, we want to get the next line.
			 */
			if (ins__is_lock(&dl->ins) &&
			    *dl->ops.raw == '\0' && allow_update) {
				ip++;
				continue;
			}
			return dl;
		}
	}
	return NULL;
}

static struct annotated_item_stat *annotate_data_stat(struct list_head *head,
						      const char *name)
{
	struct annotated_item_stat *istat;

	list_for_each_entry(istat, head, list) {
		if (!strcmp(istat->name, name))
			return istat;
	}

	istat = zalloc(sizeof(*istat));
	if (istat == NULL)
		return NULL;

	istat->name = strdup(name);
	if ((istat->name == NULL) || (!strlen(istat->name))) {
		free(istat);
		return NULL;
	}

	list_add_tail(&istat->list, head);
	return istat;
}

static bool is_stack_operation(struct arch *arch, struct disasm_line *dl)
{
	if (arch__is(arch, "x86")) {
		if (!strncmp(dl->ins.name, "push", 4) ||
		    !strncmp(dl->ins.name, "pop", 3) ||
		    !strncmp(dl->ins.name, "call", 4) ||
		    !strncmp(dl->ins.name, "ret", 3))
			return true;
	}

	return false;
}

static bool is_stack_canary(struct arch *arch, struct annotated_op_loc *loc)
{
	/* On x86_64, %gs:40 is used for stack canary */
	if (arch__is(arch, "x86")) {
		if (loc->segment == INSN_SEG_X86_GS && loc->imm &&
		    loc->offset == 40)
			return true;
	}

	return false;
}

static struct disasm_line *
annotation__prev_asm_line(struct annotation *notes, struct disasm_line *curr)
{
	struct list_head *sources = &notes->src->source;
	struct disasm_line *prev;

	if (curr == list_first_entry(sources, struct disasm_line, al.node))
		return NULL;

	prev = list_prev_entry(curr, al.node);
	while (prev->al.offset == -1 &&
	       prev != list_first_entry(sources, struct disasm_line, al.node))
		prev = list_prev_entry(prev, al.node);

	if (prev->al.offset == -1)
		return NULL;

	return prev;
}

static struct disasm_line *
annotation__next_asm_line(struct annotation *notes, struct disasm_line *curr)
{
	struct list_head *sources = &notes->src->source;
	struct disasm_line *next;

	if (curr == list_last_entry(sources, struct disasm_line, al.node))
		return NULL;

	next = list_next_entry(curr, al.node);
	while (next->al.offset == -1 &&
	       next != list_last_entry(sources, struct disasm_line, al.node))
		next = list_next_entry(next, al.node);

	if (next->al.offset == -1)
		return NULL;

	return next;
}

u64 annotate_calc_pcrel(struct map_symbol *ms, u64 ip, int offset,
			struct disasm_line *dl)
{
	struct annotation *notes;
	struct disasm_line *next;
	u64 addr;

	notes = symbol__annotation(ms->sym);
	/*
	 * PC-relative addressing starts from the next instruction address
	 * But the IP is for the current instruction.  Since disasm_line
	 * doesn't have the instruction size, calculate it using the next
	 * disasm_line.  If it's the last one, we can use symbol's end
	 * address directly.
	 */
	next = annotation__next_asm_line(notes, dl);
	if (next == NULL)
		addr = ms->sym->end + offset;
	else
		addr = ip + (next->al.offset - dl->al.offset) + offset;

	return map__rip_2objdump(ms->map, addr);
}

static struct debuginfo_cache {
	struct dso *dso;
	struct debuginfo *dbg;
} di_cache;

void debuginfo_cache__delete(void)
{
	dso__put(di_cache.dso);
	di_cache.dso = NULL;

	debuginfo__delete(di_cache.dbg);
	di_cache.dbg = NULL;
}

/**
 * hist_entry__get_data_type - find data type for given hist entry
 * @he: hist entry
 *
 * This function first annotates the instruction at @he->ip and extracts
 * register and offset info from it.  Then it searches the DWARF debug
 * info to get a variable and type information using the address, register,
 * and offset.
 */
struct annotated_data_type *hist_entry__get_data_type(struct hist_entry *he)
{
	struct map_symbol *ms = &he->ms;
	struct evsel *evsel = hists_to_evsel(he->hists);
	struct arch *arch;
	struct disasm_line *dl;
	struct annotated_insn_loc loc;
	struct annotated_op_loc *op_loc;
	struct annotated_data_type *mem_type;
	struct annotated_item_stat *istat;
	u64 ip = he->ip;
	int i;

	ann_data_stat.total++;

	if (ms->map == NULL || ms->sym == NULL) {
		ann_data_stat.no_sym++;
		return NULL;
	}

	if (!symbol_conf.init_annotation) {
		ann_data_stat.no_sym++;
		return NULL;
	}

	/*
	 * di_cache holds a pair of values, but code below assumes
	 * di_cache.dso can be compared/updated and di_cache.dbg can be
	 * read/updated independently from each other. That assumption only
	 * holds in single threaded code.
	 */
	assert(perf_singlethreaded);

	if (map__dso(ms->map) != di_cache.dso) {
		dso__put(di_cache.dso);
		di_cache.dso = dso__get(map__dso(ms->map));

		debuginfo__delete(di_cache.dbg);
		di_cache.dbg = debuginfo__new(dso__long_name(di_cache.dso));
	}

	if (di_cache.dbg == NULL) {
		ann_data_stat.no_dbginfo++;
		return NULL;
	}

	/* Make sure it has the disasm of the function */
	if (symbol__annotate(ms, evsel, &arch) < 0) {
		ann_data_stat.no_insn++;
		return NULL;
	}

	/*
	 * Get a disasm to extract the location from the insn.
	 * This is too slow...
	 */
	dl = find_disasm_line(ms->sym, ip, /*allow_update=*/true);
	if (dl == NULL) {
		ann_data_stat.no_insn++;
		return NULL;
	}

retry:
	istat = annotate_data_stat(&ann_insn_stat, dl->ins.name);
	if (istat == NULL) {
		ann_data_stat.no_insn++;
		return NULL;
	}

	if (annotate_get_insn_location(arch, dl, &loc) < 0) {
		ann_data_stat.no_insn_ops++;
		istat->bad++;
		return NULL;
	}

	if (is_stack_operation(arch, dl)) {
		istat->good++;
		he->mem_type_off = 0;
		return &stackop_type;
	}

	for_each_insn_op_loc(&loc, i, op_loc) {
		struct data_loc_info dloc = {
			.arch = arch,
			.thread = he->thread,
			.ms = ms,
			/* Recalculate IP for LOCK prefix or insn fusion */
			.ip = ms->sym->start + dl->al.offset,
			.cpumode = he->cpumode,
			.op = op_loc,
			.di = di_cache.dbg,
		};

		if (!op_loc->mem_ref && op_loc->segment == INSN_SEG_NONE)
			continue;

		/* Recalculate IP because of LOCK prefix or insn fusion */
		ip = ms->sym->start + dl->al.offset;

		/* PC-relative addressing */
		if (op_loc->reg1 == DWARF_REG_PC) {
			dloc.var_addr = annotate_calc_pcrel(ms, dloc.ip,
							    op_loc->offset, dl);
		}

		/* This CPU access in kernel - pretend PC-relative addressing */
		if (dso__kernel(map__dso(ms->map)) && arch__is(arch, "x86") &&
		    op_loc->segment == INSN_SEG_X86_GS && op_loc->imm) {
			dloc.var_addr = op_loc->offset;
			op_loc->reg1 = DWARF_REG_PC;
		}

		mem_type = find_data_type(&dloc);

		if (mem_type == NULL && is_stack_canary(arch, op_loc)) {
			istat->good++;
			he->mem_type_off = 0;
			return &canary_type;
		}

		if (mem_type)
			istat->good++;
		else
			istat->bad++;

		if (symbol_conf.annotate_data_sample) {
			annotated_data_type__update_samples(mem_type, evsel,
							    dloc.type_offset,
							    he->stat.nr_events,
							    he->stat.period);
		}
		he->mem_type_off = dloc.type_offset;
		return mem_type;
	}

	/*
	 * Some instructions can be fused and the actual memory access came
	 * from the previous instruction.
	 */
	if (dl->al.offset > 0) {
		struct annotation *notes;
		struct disasm_line *prev_dl;

		notes = symbol__annotation(ms->sym);
		prev_dl = annotation__prev_asm_line(notes, dl);

		if (prev_dl && ins__is_fused(arch, prev_dl->ins.name, dl->ins.name)) {
			dl = prev_dl;
			goto retry;
		}
	}

	ann_data_stat.no_mem_ops++;
	istat->bad++;
	return NULL;
}

/* Basic block traversal (BFS) data structure */
struct basic_block_data {
	struct list_head queue;
	struct list_head visited;
};

/*
 * During the traversal, it needs to know the parent block where the current
 * block block started from.  Note that single basic block can be parent of
 * two child basic blocks (in case of condition jump).
 */
struct basic_block_link {
	struct list_head node;
	struct basic_block_link *parent;
	struct annotated_basic_block *bb;
};

/* Check any of basic block in the list already has the offset */
static bool basic_block_has_offset(struct list_head *head, s64 offset)
{
	struct basic_block_link *link;

	list_for_each_entry(link, head, node) {
		s64 begin_offset = link->bb->begin->al.offset;
		s64 end_offset = link->bb->end->al.offset;

		if (begin_offset <= offset && offset <= end_offset)
			return true;
	}
	return false;
}

static bool is_new_basic_block(struct basic_block_data *bb_data,
			       struct disasm_line *dl)
{
	s64 offset = dl->al.offset;

	if (basic_block_has_offset(&bb_data->visited, offset))
		return false;
	if (basic_block_has_offset(&bb_data->queue, offset))
		return false;
	return true;
}

/* Add a basic block starting from dl and link it to the parent */
static int add_basic_block(struct basic_block_data *bb_data,
			   struct basic_block_link *parent,
			   struct disasm_line *dl)
{
	struct annotated_basic_block *bb;
	struct basic_block_link *link;

	if (dl == NULL)
		return -1;

	if (!is_new_basic_block(bb_data, dl))
		return 0;

	bb = zalloc(sizeof(*bb));
	if (bb == NULL)
		return -1;

	bb->begin = dl;
	bb->end = dl;
	INIT_LIST_HEAD(&bb->list);

	link = malloc(sizeof(*link));
	if (link == NULL) {
		free(bb);
		return -1;
	}

	link->bb = bb;
	link->parent = parent;
	list_add_tail(&link->node, &bb_data->queue);
	return 0;
}

/* Returns true when it finds the target in the current basic block */
static bool process_basic_block(struct basic_block_data *bb_data,
				struct basic_block_link *link,
				struct symbol *sym, u64 target)
{
	struct disasm_line *dl, *next_dl, *last_dl;
	struct annotation *notes = symbol__annotation(sym);
	bool found = false;

	dl = link->bb->begin;
	/* Check if it's already visited */
	if (basic_block_has_offset(&bb_data->visited, dl->al.offset))
		return false;

	last_dl = list_last_entry(&notes->src->source,
				  struct disasm_line, al.node);
	if (last_dl->al.offset == -1)
		last_dl = annotation__prev_asm_line(notes, last_dl);

	if (last_dl == NULL)
		return false;

	list_for_each_entry_from(dl, &notes->src->source, al.node) {
		/* Skip comment or debug info line */
		if (dl->al.offset == -1)
			continue;
		/* Found the target instruction */
		if (sym->start + dl->al.offset == target) {
			found = true;
			break;
		}
		/* End of the function, finish the block */
		if (dl == last_dl)
			break;
		/* 'return' instruction finishes the block */
		if (ins__is_ret(&dl->ins))
			break;
		/* normal instructions are part of the basic block */
		if (!ins__is_jump(&dl->ins))
			continue;
		/* jump to a different function, tail call or return */
		if (dl->ops.target.outside)
			break;
		/* jump instruction creates new basic block(s) */
		next_dl = find_disasm_line(sym, sym->start + dl->ops.target.offset,
					   /*allow_update=*/false);
		if (next_dl)
			add_basic_block(bb_data, link, next_dl);

		/*
		 * FIXME: determine conditional jumps properly.
		 * Conditional jumps create another basic block with the
		 * next disasm line.
		 */
		if (!strstr(dl->ins.name, "jmp")) {
			next_dl = annotation__next_asm_line(notes, dl);
			if (next_dl)
				add_basic_block(bb_data, link, next_dl);
		}
		break;

	}
	link->bb->end = dl;
	return found;
}

/*
 * It founds a target basic block, build a proper linked list of basic blocks
 * by following the link recursively.
 */
static void link_found_basic_blocks(struct basic_block_link *link,
				    struct list_head *head)
{
	while (link) {
		struct basic_block_link *parent = link->parent;

		list_move(&link->bb->list, head);
		list_del(&link->node);
		free(link);

		link = parent;
	}
}

static void delete_basic_blocks(struct basic_block_data *bb_data)
{
	struct basic_block_link *link, *tmp;

	list_for_each_entry_safe(link, tmp, &bb_data->queue, node) {
		list_del(&link->node);
		zfree(&link->bb);
		free(link);
	}

	list_for_each_entry_safe(link, tmp, &bb_data->visited, node) {
		list_del(&link->node);
		zfree(&link->bb);
		free(link);
	}
}

/**
 * annotate_get_basic_blocks - Get basic blocks for given address range
 * @sym: symbol to annotate
 * @src: source address
 * @dst: destination address
 * @head: list head to save basic blocks
 *
 * This function traverses disasm_lines from @src to @dst and save them in a
 * list of annotated_basic_block to @head.  It uses BFS to find the shortest
 * path between two.  The basic_block_link is to maintain parent links so
 * that it can build a list of blocks from the start.
 */
int annotate_get_basic_blocks(struct symbol *sym, s64 src, s64 dst,
			      struct list_head *head)
{
	struct basic_block_data bb_data = {
		.queue = LIST_HEAD_INIT(bb_data.queue),
		.visited = LIST_HEAD_INIT(bb_data.visited),
	};
	struct basic_block_link *link;
	struct disasm_line *dl;
	int ret = -1;

	dl = find_disasm_line(sym, src, /*allow_update=*/false);
	if (dl == NULL)
		return -1;

	if (add_basic_block(&bb_data, /*parent=*/NULL, dl) < 0)
		return -1;

	/* Find shortest path from src to dst using BFS */
	while (!list_empty(&bb_data.queue)) {
		link = list_first_entry(&bb_data.queue, struct basic_block_link, node);

		if (process_basic_block(&bb_data, link, sym, dst)) {
			link_found_basic_blocks(link, head);
			ret = 0;
			break;
		}
		list_move(&link->node, &bb_data.visited);
	}
	delete_basic_blocks(&bb_data);
	return ret;
}
