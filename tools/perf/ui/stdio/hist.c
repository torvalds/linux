// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <linux/string.h>

#include "../../util/callchain.h"
#include "../../util/util.h"
#include "../../util/hist.h"
#include "../../util/map.h"
#include "../../util/map_groups.h"
#include "../../util/symbol.h"
#include "../../util/sort.h"
#include "../../util/evsel.h"
#include "../../util/srcline.h"
#include "../../util/string2.h"
#include "../../util/thread.h"
#include "../../util/sane_ctype.h"

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

static size_t ipchain__fprintf_graph(FILE *fp, struct callchain_node *node,
				     struct callchain_list *chain,
				     int depth, int depth_mask, int period,
				     u64 total_samples, int left_margin)
{
	int i;
	size_t ret = 0;
	char bf[1024], *alloc_str = NULL;
	char buf[64];
	const char *str;

	ret += callchain__fprintf_left_margin(fp, left_margin);
	for (i = 0; i < depth; i++) {
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|");
		else
			ret += fprintf(fp, " ");
		if (!period && i == depth - 1) {
			ret += fprintf(fp, "--");
			ret += callchain_node__fprintf_value(node, fp, total_samples);
			ret += fprintf(fp, "--");
		} else
			ret += fprintf(fp, "%s", "          ");
	}

	str = callchain_list__sym_name(chain, bf, sizeof(bf), false);

	if (symbol_conf.show_branchflag_count) {
		callchain_list_counts__printf_value(chain, NULL,
						    buf, sizeof(buf));

		if (asprintf(&alloc_str, "%s%s", str, buf) < 0)
			str = "Not enough memory!";
		else
			str = alloc_str;
	}

	fputs(str, fp);
	fputc('\n', fp);
	free(alloc_str);

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
	struct callchain_node *child = NULL;
	struct callchain_list *chain;
	int new_depth_mask = depth_mask;
	u64 remaining;
	size_t ret = 0;
	int i;
	uint entries_printed = 0;
	int cumul_count = 0;

	remaining = total_samples;

	node = rb_first(root);
	while (node) {
		u64 new_total;
		u64 cumul;

		child = rb_entry(node, struct callchain_node, rb_node);
		cumul = callchain_cumul_hits(child);
		remaining -= cumul;
		cumul_count += callchain_cumul_counts(child);

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
			ret += ipchain__fprintf_graph(fp, child, chain, depth,
						      new_depth_mask, i++,
						      total_samples,
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
		struct callchain_node rem_node = {
			.hit = remaining,
		};

		if (!rem_sq_bracket)
			return ret;

		if (callchain_param.value == CCVAL_COUNT && child && child->parent) {
			rem_node.count = child->parent->children_count - cumul_count;
			if (rem_node.count <= 0)
				return ret;
		}

		new_depth_mask &= ~(1 << (depth - 1));
		ret += ipchain__fprintf_graph(fp, &rem_node, &rem_hits, depth,
					      new_depth_mask, 0, total_samples,
					      left_margin);
	}

	return ret;
}

/*
 * If have one single callchain root, don't bother printing
 * its percentage (100 % in fractal mode and the same percentage
 * than the hist in graph mode). This also avoid one level of column.
 *
 * However when percent-limit applied, it's possible that single callchain
 * node have different (non-100% in fractal mode) percentage.
 */
static bool need_percent_display(struct rb_node *node, u64 parent_samples)
{
	struct callchain_node *cnode;

	if (rb_next(node))
		return true;

	cnode = rb_entry(node, struct callchain_node, rb_node);
	return callchain_cumul_hits(cnode) != parent_samples;
}

static size_t callchain__fprintf_graph(FILE *fp, struct rb_root *root,
				       u64 total_samples, u64 parent_samples,
				       int left_margin)
{
	struct callchain_node *cnode;
	struct callchain_list *chain;
	u32 entries_printed = 0;
	bool printed = false;
	struct rb_node *node;
	int i = 0;
	int ret = 0;
	char bf[1024];

	node = rb_first(root);
	if (node && !need_percent_display(node, parent_samples)) {
		cnode = rb_entry(node, struct callchain_node, rb_node);
		list_for_each_entry(chain, &cnode->val, list) {
			/*
			 * If we sort by symbol, the first entry is the same than
			 * the symbol. No need to print it otherwise it appears as
			 * displayed twice.
			 */
			if (!i++ && field_order == NULL &&
			    sort_order && strstarts(sort_order, "sym"))
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

			ret += fprintf(fp, "%s",
				       callchain_list__sym_name(chain, bf,
								sizeof(bf),
								false));

			if (symbol_conf.show_branchflag_count)
				ret += callchain_list_counts__printf_value(
						chain, fp, NULL, 0);
			ret += fprintf(fp, "\n");

			if (++entries_printed == callchain_param.print_limit)
				break;
		}
		root = &cnode->rb_root;
	}

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		total_samples = parent_samples;

	ret += __callchain__fprintf_graph(fp, root, total_samples,
					  1, 1, left_margin);
	if (ret) {
		/* do not add a blank line if it printed nothing */
		ret += fprintf(fp, "\n");
	}

	return ret;
}

static size_t __callchain__fprintf_flat(FILE *fp, struct callchain_node *node,
					u64 total_samples)
{
	struct callchain_list *chain;
	size_t ret = 0;
	char bf[1024];

	if (!node)
		return 0;

	ret += __callchain__fprintf_flat(fp, node->parent, total_samples);


	list_for_each_entry(chain, &node->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		ret += fprintf(fp, "                %s\n", callchain_list__sym_name(chain,
					bf, sizeof(bf), false));
	}

	return ret;
}

static size_t callchain__fprintf_flat(FILE *fp, struct rb_root *tree,
				      u64 total_samples)
{
	size_t ret = 0;
	u32 entries_printed = 0;
	struct callchain_node *chain;
	struct rb_node *rb_node = rb_first(tree);

	while (rb_node) {
		chain = rb_entry(rb_node, struct callchain_node, rb_node);

		ret += fprintf(fp, "           ");
		ret += callchain_node__fprintf_value(chain, fp, total_samples);
		ret += fprintf(fp, "\n");
		ret += __callchain__fprintf_flat(fp, chain, total_samples);
		ret += fprintf(fp, "\n");
		if (++entries_printed == callchain_param.print_limit)
			break;

		rb_node = rb_next(rb_node);
	}

	return ret;
}

static size_t __callchain__fprintf_folded(FILE *fp, struct callchain_node *node)
{
	const char *sep = symbol_conf.field_sep ?: ";";
	struct callchain_list *chain;
	size_t ret = 0;
	char bf[1024];
	bool first;

	if (!node)
		return 0;

	ret += __callchain__fprintf_folded(fp, node->parent);

	first = (ret == 0);
	list_for_each_entry(chain, &node->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		ret += fprintf(fp, "%s%s", first ? "" : sep,
			       callchain_list__sym_name(chain,
						bf, sizeof(bf), false));
		first = false;
	}

	return ret;
}

static size_t callchain__fprintf_folded(FILE *fp, struct rb_root *tree,
					u64 total_samples)
{
	size_t ret = 0;
	u32 entries_printed = 0;
	struct callchain_node *chain;
	struct rb_node *rb_node = rb_first(tree);

	while (rb_node) {

		chain = rb_entry(rb_node, struct callchain_node, rb_node);

		ret += callchain_node__fprintf_value(chain, fp, total_samples);
		ret += fprintf(fp, " ");
		ret += __callchain__fprintf_folded(fp, chain);
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
	u64 parent_samples = he->stat.period;

	if (symbol_conf.cumulate_callchain)
		parent_samples = he->stat_acc->period;

	switch (callchain_param.mode) {
	case CHAIN_GRAPH_REL:
		return callchain__fprintf_graph(fp, &he->sorted_chain, total_samples,
						parent_samples, left_margin);
		break;
	case CHAIN_GRAPH_ABS:
		return callchain__fprintf_graph(fp, &he->sorted_chain, total_samples,
						parent_samples, left_margin);
		break;
	case CHAIN_FLAT:
		return callchain__fprintf_flat(fp, &he->sorted_chain, total_samples);
		break;
	case CHAIN_FOLDED:
		return callchain__fprintf_folded(fp, &he->sorted_chain, total_samples);
		break;
	case CHAIN_NONE:
		break;
	default:
		pr_err("Bad callchain mode\n");
	}

	return 0;
}

int __hist_entry__snprintf(struct hist_entry *he, struct perf_hpp *hpp,
			   struct perf_hpp_list *hpp_list)
{
	const char *sep = symbol_conf.field_sep;
	struct perf_hpp_fmt *fmt;
	char *start = hpp->buf;
	int ret;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	perf_hpp_list__for_each_format(hpp_list, fmt) {
		if (perf_hpp__should_skip(fmt, he->hists))
			continue;

		/*
		 * If there's no field_sep, we still need
		 * to display initial '  '.
		 */
		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
		} else
			first = false;

		if (perf_hpp__use_color() && fmt->color)
			ret = fmt->color(fmt, hpp, he);
		else
			ret = fmt->entry(fmt, hpp, he);

		ret = hist_entry__snprintf_alignment(he, hpp, fmt, ret);
		advance_hpp(hpp, ret);
	}

	return hpp->buf - start;
}

static int hist_entry__snprintf(struct hist_entry *he, struct perf_hpp *hpp)
{
	return __hist_entry__snprintf(he, hpp, he->hists->hpp_list);
}

static int hist_entry__hierarchy_fprintf(struct hist_entry *he,
					 struct perf_hpp *hpp,
					 struct hists *hists,
					 FILE *fp)
{
	const char *sep = symbol_conf.field_sep;
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *fmt_node;
	char *buf = hpp->buf;
	size_t size = hpp->size;
	int ret, printed = 0;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	ret = scnprintf(hpp->buf, hpp->size, "%*s", he->depth * HIERARCHY_INDENT, "");
	advance_hpp(hpp, ret);

	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&hists->hpp_formats,
				    struct perf_hpp_list_node, list);
	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		/*
		 * If there's no field_sep, we still need
		 * to display initial '  '.
		 */
		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
		} else
			first = false;

		if (perf_hpp__use_color() && fmt->color)
			ret = fmt->color(fmt, hpp, he);
		else
			ret = fmt->entry(fmt, hpp, he);

		ret = hist_entry__snprintf_alignment(he, hpp, fmt, ret);
		advance_hpp(hpp, ret);
	}

	if (!sep)
		ret = scnprintf(hpp->buf, hpp->size, "%*s",
				(hists->nr_hpp_node - 2) * HIERARCHY_INDENT, "");
	advance_hpp(hpp, ret);

	printed += fprintf(fp, "%s", buf);

	perf_hpp_list__for_each_format(he->hpp_list, fmt) {
		hpp->buf  = buf;
		hpp->size = size;

		/*
		 * No need to call hist_entry__snprintf_alignment() since this
		 * fmt is always the last column in the hierarchy mode.
		 */
		if (perf_hpp__use_color() && fmt->color)
			fmt->color(fmt, hpp, he);
		else
			fmt->entry(fmt, hpp, he);

		/*
		 * dynamic entries are right-aligned but we want left-aligned
		 * in the hierarchy mode
		 */
		printed += fprintf(fp, "%s%s", sep ?: "  ", ltrim(buf));
	}
	printed += putc('\n', fp);

	if (he->leaf && hist_entry__has_callchains(he) && symbol_conf.use_callchain) {
		u64 total = hists__total_period(hists);

		printed += hist_entry_callchain__fprintf(he, total, 0, fp);
		goto out;
	}

out:
	return printed;
}

static int hist_entry__fprintf(struct hist_entry *he, size_t size,
			       char *bf, size_t bfsz, FILE *fp,
			       bool ignore_callchains)
{
	int ret;
	int callchain_ret = 0;
	struct perf_hpp hpp = {
		.buf		= bf,
		.size		= size,
	};
	struct hists *hists = he->hists;
	u64 total_period = hists->stats.total_period;

	if (size == 0 || size > bfsz)
		size = hpp.size = bfsz;

	if (symbol_conf.report_hierarchy)
		return hist_entry__hierarchy_fprintf(he, &hpp, hists, fp);

	hist_entry__snprintf(he, &hpp);

	ret = fprintf(fp, "%s\n", bf);

	if (hist_entry__has_callchains(he) && !ignore_callchains)
		callchain_ret = hist_entry_callchain__fprintf(he, total_period,
							      0, fp);

	ret += callchain_ret;

	return ret;
}

static int print_hierarchy_indent(const char *sep, int indent,
				  const char *line, FILE *fp)
{
	int width;

	if (sep != NULL || indent < 2)
		return 0;

	width = (indent - 2) * HIERARCHY_INDENT;

	return fprintf(fp, "%-*.*s", width, width, line);
}

static int hists__fprintf_hierarchy_headers(struct hists *hists,
					    struct perf_hpp *hpp, FILE *fp)
{
	bool first_node, first_col;
	int indent;
	int depth;
	unsigned width = 0;
	unsigned header_width = 0;
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *fmt_node;
	const char *sep = symbol_conf.field_sep;

	indent = hists->nr_hpp_node;

	/* preserve max indent depth for column headers */
	print_hierarchy_indent(sep, indent, " ", fp);

	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&hists->hpp_formats,
				    struct perf_hpp_list_node, list);

	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		fmt->header(fmt, hpp, hists, 0, NULL);
		fprintf(fp, "%s%s", hpp->buf, sep ?: "  ");
	}

	/* combine sort headers with ' / ' */
	first_node = true;
	list_for_each_entry_continue(fmt_node, &hists->hpp_formats, list) {
		if (!first_node)
			header_width += fprintf(fp, " / ");
		first_node = false;

		first_col = true;
		perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
			if (perf_hpp__should_skip(fmt, hists))
				continue;

			if (!first_col)
				header_width += fprintf(fp, "+");
			first_col = false;

			fmt->header(fmt, hpp, hists, 0, NULL);

			header_width += fprintf(fp, "%s", trim(hpp->buf));
		}
	}

	fprintf(fp, "\n# ");

	/* preserve max indent depth for initial dots */
	print_hierarchy_indent(sep, indent, dots, fp);

	/* the first hpp_list_node is for overhead columns */
	fmt_node = list_first_entry(&hists->hpp_formats,
				    struct perf_hpp_list_node, list);

	first_col = true;
	perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
		if (!first_col)
			fprintf(fp, "%s", sep ?: "..");
		first_col = false;

		width = fmt->width(fmt, hpp, hists);
		fprintf(fp, "%.*s", width, dots);
	}

	depth = 0;
	list_for_each_entry_continue(fmt_node, &hists->hpp_formats, list) {
		first_col = true;
		width = depth * HIERARCHY_INDENT;

		perf_hpp_list__for_each_format(&fmt_node->hpp, fmt) {
			if (perf_hpp__should_skip(fmt, hists))
				continue;

			if (!first_col)
				width++;  /* for '+' sign between column header */
			first_col = false;

			width += fmt->width(fmt, hpp, hists);
		}

		if (width > header_width)
			header_width = width;

		depth++;
	}

	fprintf(fp, "%s%-.*s", sep ?: "  ", header_width, dots);

	fprintf(fp, "\n#\n");

	return 2;
}

static void fprintf_line(struct hists *hists, struct perf_hpp *hpp,
			 int line, FILE *fp)
{
	struct perf_hpp_fmt *fmt;
	const char *sep = symbol_conf.field_sep;
	bool first = true;
	int span = 0;

	hists__for_each_format(hists, fmt) {
		if (perf_hpp__should_skip(fmt, hists))
			continue;

		if (!first && !span)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		fmt->header(fmt, hpp, hists, line, &span);

		if (!span)
			fprintf(fp, "%s", hpp->buf);
	}
}

static int
hists__fprintf_standard_headers(struct hists *hists,
				struct perf_hpp *hpp,
				FILE *fp)
{
	struct perf_hpp_list *hpp_list = hists->hpp_list;
	struct perf_hpp_fmt *fmt;
	unsigned int width;
	const char *sep = symbol_conf.field_sep;
	bool first = true;
	int line;

	for (line = 0; line < hpp_list->nr_header_lines; line++) {
		/* first # is displayed one level up */
		if (line)
			fprintf(fp, "# ");
		fprintf_line(hists, hpp, line, fp);
		fprintf(fp, "\n");
	}

	if (sep)
		return hpp_list->nr_header_lines;

	first = true;

	fprintf(fp, "# ");

	hists__for_each_format(hists, fmt) {
		unsigned int i;

		if (perf_hpp__should_skip(fmt, hists))
			continue;

		if (!first)
			fprintf(fp, "%s", sep ?: "  ");
		else
			first = false;

		width = fmt->width(fmt, hpp, hists);
		for (i = 0; i < width; i++)
			fprintf(fp, ".");
	}

	fprintf(fp, "\n");
	fprintf(fp, "#\n");
	return hpp_list->nr_header_lines + 2;
}

int hists__fprintf_headers(struct hists *hists, FILE *fp)
{
	char bf[1024];
	struct perf_hpp dummy_hpp = {
		.buf	= bf,
		.size	= sizeof(bf),
	};

	fprintf(fp, "# ");

	if (symbol_conf.report_hierarchy)
		return hists__fprintf_hierarchy_headers(hists, &dummy_hpp, fp);
	else
		return hists__fprintf_standard_headers(hists, &dummy_hpp, fp);

}

size_t hists__fprintf(struct hists *hists, bool show_header, int max_rows,
		      int max_cols, float min_pcnt, FILE *fp,
		      bool ignore_callchains)
{
	struct rb_node *nd;
	size_t ret = 0;
	const char *sep = symbol_conf.field_sep;
	int nr_rows = 0;
	size_t linesz;
	char *line = NULL;
	unsigned indent;

	init_rem_hits();

	hists__reset_column_width(hists);

	if (symbol_conf.col_width_list_str)
		perf_hpp__set_user_width(symbol_conf.col_width_list_str);

	if (show_header)
		nr_rows += hists__fprintf_headers(hists, fp);

	if (max_rows && nr_rows >= max_rows)
		goto out;

	linesz = hists__sort_list_width(hists) + 3 + 1;
	linesz += perf_hpp__color_overhead();
	line = malloc(linesz);
	if (line == NULL) {
		ret = -1;
		goto out;
	}

	indent = hists__overhead_width(hists) + 4;

	for (nd = rb_first_cached(&hists->entries); nd;
	     nd = __rb_hierarchy_next(nd, HMD_FORCE_CHILD)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		float percent;

		if (h->filtered)
			continue;

		percent = hist_entry__get_percent_limit(h);
		if (percent < min_pcnt)
			continue;

		ret += hist_entry__fprintf(h, max_cols, line, linesz, fp, ignore_callchains);

		if (max_rows && ++nr_rows >= max_rows)
			break;

		/*
		 * If all children are filtered out or percent-limited,
		 * display "no entry >= x.xx%" message.
		 */
		if (!h->leaf && !hist_entry__has_hierarchy_children(h, min_pcnt)) {
			int depth = hists->nr_hpp_node + h->depth + 1;

			print_hierarchy_indent(sep, depth, " ", fp);
			fprintf(fp, "%*sno entry >= %.2f%%\n", indent, "", min_pcnt);

			if (max_rows && ++nr_rows >= max_rows)
				break;
		}

		if (h->ms.map == NULL && verbose > 1) {
			map_groups__fprintf(h->thread->mg, fp);
			fprintf(fp, "%.10s end\n", graph_dotted_line);
		}
	}

	free(line);
out:
	zfree(&rem_sq_bracket);

	return ret;
}

size_t events_stats__fprintf(struct events_stats *stats, FILE *fp)
{
	int i;
	size_t ret = 0;

	for (i = 0; i < PERF_RECORD_HEADER_MAX; ++i) {
		const char *name;

		name = perf_event__name(i);
		if (!strcmp(name, "UNKNOWN"))
			continue;

		ret += fprintf(fp, "%16s events: %10d\n", name, stats->nr_events[i]);
	}

	return ret;
}
