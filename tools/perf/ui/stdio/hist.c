#include <stdio.h>

#include "../../util/util.h"
#include "../../util/hist.h"
#include "../../util/sort.h"
#include "../../util/evsel.h"


static size_t callchain__fprintf_left_margin(FILE *fp, int left_margin)
{
	int i;
	int ret = fprintf(fp, "            ");

	for (i = 0; i < left_margin; i++)
		ret += fprintf(fp, " ");

	return ret;
}

static size_t ipchain__fprintf_graph_line(FILE *fp, int depth, int depth_mask,
					  int left_margin)
{
	int i;
	size_t ret = callchain__fprintf_left_margin(fp, left_margin);

	for (i = 0; i < depth; i++)
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|          ");
		else
			ret += fprintf(fp, "           ");

	ret += fprintf(fp, "\n");

	return ret;
}

static size_t ipchain__fprintf_graph(FILE *fp, struct callchain_list *chain,
				     int depth, int depth_mask, int period,
				     u64 total_samples, u64 hits,
				     int left_margin)
{
	int i;
	size_t ret = 0;

	ret += callchain__fprintf_left_margin(fp, left_margin);
	for (i = 0; i < depth; i++) {
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|");
		else
			ret += fprintf(fp, " ");
		if (!period && i == depth - 1) {
			double percent;

			percent = hits * 100.0 / total_samples;
			ret += percent_color_fprintf(fp, "--%2.2f%%-- ", percent);
		} else
			ret += fprintf(fp, "%s", "          ");
	}
	if (chain->ms.sym)
		ret += fprintf(fp, "%s\n", chain->ms.sym->name);
	else
		ret += fprintf(fp, "0x%0" PRIx64 "\n", chain->ip);

	return ret;
}

static struct symbol *rem_sq_bracket;
static struct callchain_list rem_hits;

static void init_rem_hits(void)
{
	rem_sq_bracket = malloc(sizeof(*rem_sq_bracket) + 6);
	if (!rem_sq_bracket) {
		fprintf(stderr, "Not enough memory to display remaining hits\n");
		return;
	}

	strcpy(rem_sq_bracket->name, "[...]");
	rem_hits.ms.sym = rem_sq_bracket;
}

static size_t __callchain__fprintf_graph(FILE *fp, struct rb_root *root,
					 u64 total_samples, int depth,
					 int depth_mask, int left_margin)
{
	struct rb_node *node, *next;
	struct callchain_node *child;
	struct callchain_list *chain;
	int new_depth_mask = depth_mask;
	u64 remaining;
	size_t ret = 0;
	int i;
	uint entries_printed = 0;

	remaining = total_samples;

	node = rb_first(root);
	while (node) {
		u64 new_total;
		u64 cumul;

		child = rb_entry(node, struct callchain_node, rb_node);
		cumul = callchain_cumul_hits(child);
		remaining -= cumul;

		/*
		 * The depth mask manages the output of pipes that show
		 * the depth. We don't want to keep the pipes of the current
		 * level for the last child of this depth.
		 * Except if we have remaining filtered hits. They will
		 * supersede the last child
		 */
		next = rb_next(node);
		if (!next && (callchain_param.mode != CHAIN_GRAPH_REL || !remaining))
			new_depth_mask &= ~(1 << (depth - 1));

		/*
		 * But we keep the older depth mask for the line separator
		 * to keep the level link until we reach the last child
		 */
		ret += ipchain__fprintf_graph_line(fp, depth, depth_mask,
						   left_margin);
		i = 0;
		list_for_each_entry(chain, &child->val, list) {
			ret += ipchain__fprintf_graph(fp, chain, depth,
						      new_depth_mask, i++,
						      total_samples,
						      cumul,
						      left_margin);
		}

		if (callchain_param.mode == CHAIN_GRAPH_REL)
			new_total = child->children_hit;
		else
			new_total = total_samples;

		ret += __callchain__fprintf_graph(fp, &child->rb_root, new_total,
						  depth + 1,
						  new_depth_mask | (1 << depth),
						  left_margin);
		node = next;
		if (++entries_printed == callchain_param.print_limit)
			break;
	}

	if (callchain_param.mode == CHAIN_GRAPH_REL &&
		remaining && remaining != total_samples) {

		if (!rem_sq_bracket)
			return ret;

		new_depth_mask &= ~(1 << (depth - 1));
		ret += ipchain__fprintf_graph(fp, &rem_hits, depth,
					      new_depth_mask, 0, total_samples,
					      remaining, left_margin);
	}

	return ret;
}

static size_t callchain__fprintf_graph(FILE *fp, struct rb_root *root,
				       u64 total_samples, int left_margin)
{
	struct callchain_node *cnode;
	struct callchain_list *chain;
	u32 entries_printed = 0;
	bool printed = false;
	struct rb_node *node;
	int i = 0;
	int ret = 0;

	/*
	 * If have one single callchain root, don't bother printing
	 * its percentage (100 % in fractal mode and the same percentage
	 * than the hist in graph mode). This also avoid one level of column.
	 */
	node = rb_first(root);
	if (node && !rb_next(node)) {
		cnode = rb_entry(node, struct callchain_node, rb_node);
		list_for_each_entry(chain, &cnode->val, list) {
			/*
			 * If we sort by symbol, the first entry is the same than
			 * the symbol. No need to print it otherwise it appears as
			 * displayed twice.
			 */
			if (!i++ && sort__first_dimension == SORT_SYM)
				continue;
			if (!printed) {
				ret += callchain__fprintf_left_margin(fp, left_margin);
				ret += fprintf(fp, "|\n");
				ret += callchain__fprintf_left_margin(fp, left_margin);
				ret += fprintf(fp, "---");
				left_margin += 3;
				printed = true;
			} else
				ret += callchain__fprintf_left_margin(fp, left_margin);

			if (chain->ms.sym)
				ret += fprintf(fp, " %s\n", chain->ms.sym->name);
			else
				ret += fprintf(fp, " %p\n", (void *)(long)chain->ip);

			if (++entries_printed == callchain_param.print_limit)
				break;
		}
		root = &cnode->rb_root;
	}

	ret += __callchain__fprintf_graph(fp, root, total_samples,
					  1, 1, left_margin);
	ret += fprintf(fp, "\n");

	return ret;
}

static size_t __callchain__fprintf_flat(FILE *fp,
					struct callchain_node *self,
					u64 total_samples)
{
	struct callchain_list *chain;
	size_t ret = 0;

	if (!self)
		return 0;

	ret += __callchain__fprintf_flat(fp, self->parent, total_samples);


	list_for_each_entry(chain, &self->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		if (chain->ms.sym)
			ret += fprintf(fp, "                %s\n", chain->ms.sym->name);
		else
			ret += fprintf(fp, "                %p\n",
					(void *)(long)chain->ip);
	}

	return ret;
}

static size_t callchain__fprintf_flat(FILE *fp, struct rb_root *self,
				      u64 total_samples)
{
	size_t ret = 0;
	u32 entries_printed = 0;
	struct rb_node *rb_node;
	struct callchain_node *chain;

	rb_node = rb_first(self);
	while (rb_node) {
		double percent;

		chain = rb_entry(rb_node, struct callchain_node, rb_node);
		percent = chain->hit * 100.0 / total_samples;

		ret = percent_color_fprintf(fp, "           %6.2f%%\n", percent);
		ret += __callchain__fprintf_flat(fp, chain, total_samples);
		ret += fprintf(fp, "\n");
		if (++entries_printed == callchain_param.print_limit)
			break;

		rb_node = rb_next(rb_node);
	}

	return ret;
}

static size_t hist_entry_callchain__fprintf(struct hist_entry *he,
					    u64 total_samples, int left_margin,
					    FILE *fp)
{
	switch (callchain_param.mode) {
	case CHAIN_GRAPH_REL:
		return callchain__fprintf_graph(fp, &he->sorted_chain, he->stat.period,
						left_margin);
		break;
	case CHAIN_GRAPH_ABS:
		return callchain__fprintf_graph(fp, &he->sorted_chain, total_samples,
						left_margin);
		break;
	case CHAIN_FLAT:
		return callchain__fprintf_flat(fp, &he->sorted_chain, total_samples);
		break;
	case CHAIN_NONE:
		break;
	default:
		pr_err("Bad callchain mode\n");
	}

	return 0;
}

static size_t hist_entry__callchain_fprintf(struct hist_entry *he,
					    struct hists *hists,
					    FILE *fp)
{
	int left_margin = 0;
	u64 total_period = hists->stats.total_period;

	if (sort__first_dimension == SORT_COMM) {
		struct sort_entry *se = list_first_entry(&hist_entry__sort_list,
							 typeof(*se), list);
		left_margin = hists__col_len(hists, se->se_width_idx);
		left_margin -= thread__comm_len(he->thread);
	}

	return hist_entry_callchain__fprintf(he, total_period, left_margin, fp);
}

static inline void advance_hpp(struct perf_hpp *hpp, int inc)
{
	hpp->buf  += inc;
	hpp->size -= inc;
}

static int hist_entry__period_snprintf(struct perf_hpp *hpp,
				       struct hist_entry *he,
				       bool color)
{
	const char *sep = symbol_conf.field_sep;
	struct perf_hpp_fmt *fmt;
	char *start = hpp->buf;
	int ret;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	perf_hpp__for_each_format(fmt) {
		/*
		 * If there's no field_sep, we still need
		 * to display initial '  '.
		 */
		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
		} else
			first = false;

		if (color && fmt->color)
			ret = fmt->color(fmt, hpp, he);
		else
			ret = fmt->entry(fmt, hpp, he);

		advance_hpp(hpp, ret);
	}

	return hpp->buf - start;
}

static int hist_entry__fprintf(struct hist_entry *he, size_t size,
			       struct hists *hists,
			       char *bf, size_t bfsz, FILE *fp)
{
	int ret;
	struct perf_hpp hpp = {
		.buf		= bf,
		.size		= size,
	};
	bool color = !symbol_conf.field_sep;

	if (size == 0 || size > bfsz)
		size = hpp.size = bfsz;

	ret = hist_entry__period_snprintf(&hpp, he, color);
	hist_entry__sort_snprintf(he, bf + ret, size - ret, hists);

	ret = fprintf(fp, "%s\n", bf);

	if (symbol_conf.use_callchain)
		ret += hist_entry__callchain_fprintf(he, hists, fp);

	return ret;
}

size_t hists__fprintf(struct hists *hists, bool show_header, int max_rows,
		      int max_cols, float min_pcnt, FILE *fp)
{
	struct perf_hpp_fmt *fmt;
	struct sort_entry *se;
	struct rb_node *nd;
	size_t ret = 0;
	unsigned int width;
	const char *sep = symbol_conf.field_sep;
	const char *col_width = symbol_conf.col_width_list_str;
	int nr_rows = 0;
	char bf[96];
	struct perf_hpp dummy_hpp = {
		.buf	= bf,
		.size	= sizeof(bf),
		.ptr	= hists_to_evsel(hists),
	};
	bool first = true;
	size_t linesz;
	char *line = NULL;

	init_rem_hits();

	if (!show_header)
		goto print_entries;

	fprintf(fp, "# ");

	perf_hpp__for_each_format(fmt) {
		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		fmt->header(fmt, &dummy_hpp);
		fprintf(fp, "%s", bf);
	}

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;
		if (sep) {
			fprintf(fp, "%c%s", *sep, se->se_header);
			continue;
		}
		width = strlen(se->se_header);
		if (symbol_conf.col_width_list_str) {
			if (col_width) {
				hists__set_col_len(hists, se->se_width_idx,
						   atoi(col_width));
				col_width = strchr(col_width, ',');
				if (col_width)
					++col_width;
			}
		}
		if (!hists__new_col_len(hists, se->se_width_idx, width))
			width = hists__col_len(hists, se->se_width_idx);
		fprintf(fp, "  %*s", width, se->se_header);
	}

	fprintf(fp, "\n");
	if (max_rows && ++nr_rows >= max_rows)
		goto out;

	if (sep)
		goto print_entries;

	first = true;

	fprintf(fp, "# ");

	perf_hpp__for_each_format(fmt) {
		unsigned int i;

		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		width = fmt->width(fmt, &dummy_hpp);
		for (i = 0; i < width; i++)
			fprintf(fp, ".");
	}

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		unsigned int i;

		if (se->elide)
			continue;

		fprintf(fp, "  ");
		width = hists__col_len(hists, se->se_width_idx);
		if (width == 0)
			width = strlen(se->se_header);
		for (i = 0; i < width; i++)
			fprintf(fp, ".");
	}

	fprintf(fp, "\n");
	if (max_rows && ++nr_rows >= max_rows)
		goto out;

	fprintf(fp, "#\n");
	if (max_rows && ++nr_rows >= max_rows)
		goto out;

print_entries:
	linesz = hists__sort_list_width(hists) + 3 + 1;
	line = malloc(linesz);
	if (line == NULL) {
		ret = -1;
		goto out;
	}

	for (nd = rb_first(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent = h->stat.period * 100.0 /
					hists->stats.total_period;

		if (h->filtered)
			continue;

		if (percent < min_pcnt)
			continue;

		ret += hist_entry__fprintf(h, max_cols, hists, line, linesz, fp);

		if (max_rows && ++nr_rows >= max_rows)
			break;

		if (h->ms.map == NULL && verbose > 1) {
			__map_groups__fprintf_maps(&h->thread->mg,
						   MAP__FUNCTION, verbose, fp);
			fprintf(fp, "%.10s end\n", graph_dotted_line);
		}
	}

	free(line);
out:
	free(rem_sq_bracket);

	return ret;
}

size_t events_stats__fprintf(struct events_stats *stats, FILE *fp)
{
	int i;
	size_t ret = 0;

	for (i = 0; i < PERF_RECORD_HEADER_MAX; ++i) {
		const char *name;

		if (stats->nr_events[i] == 0)
			continue;

		name = perf_event__name(i);
		if (!strcmp(name, "UNKNOWN"))
			continue;

		ret += fprintf(fp, "%16s events: %10d\n", name,
			       stats->nr_events[i]);
	}

	return ret;
}
