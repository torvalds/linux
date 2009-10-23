#include "hist.h"

struct rb_root hist;
struct rb_root collapse_hists;
struct rb_root output_hists;
int callchain;

struct callchain_param	callchain_param = {
	.mode	= CHAIN_GRAPH_REL,
	.min_percent = 0.5
};

unsigned long total;
unsigned long total_mmap;
unsigned long total_comm;
unsigned long total_fork;
unsigned long total_unknown;
unsigned long total_lost;

/*
 * histogram, sorted on item, collects counts
 */

struct hist_entry *__hist_entry__add(struct thread *thread, struct map *map,
				     struct symbol *sym,
				     struct symbol *sym_parent,
				     u64 ip, u64 count, char level, bool *hit)
{
	struct rb_node **p = &hist.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	struct hist_entry entry = {
		.thread	= thread,
		.map	= map,
		.sym	= sym,
		.ip	= ip,
		.level	= level,
		.count	= count,
		.parent = sym_parent,
	};
	int cmp;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node);

		cmp = hist_entry__cmp(&entry, he);

		if (!cmp) {
			*hit = true;
			return he;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	he = malloc(sizeof(*he));
	if (!he)
		return NULL;
	*he = entry;
	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &hist);
	*hit = false;
	return he;
}

int64_t
hist_entry__cmp(struct hist_entry *left, struct hist_entry *right)
{
	struct sort_entry *se;
	int64_t cmp = 0;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		cmp = se->cmp(left, right);
		if (cmp)
			break;
	}

	return cmp;
}

int64_t
hist_entry__collapse(struct hist_entry *left, struct hist_entry *right)
{
	struct sort_entry *se;
	int64_t cmp = 0;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		int64_t (*f)(struct hist_entry *, struct hist_entry *);

		f = se->collapse ?: se->cmp;

		cmp = f(left, right);
		if (cmp)
			break;
	}

	return cmp;
}

void hist_entry__free(struct hist_entry *he)
{
	free(he);
}

/*
 * collapse the histogram
 */

void collapse__insert_entry(struct hist_entry *he)
{
	struct rb_node **p = &collapse_hists.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;
	int64_t cmp;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		cmp = hist_entry__collapse(iter, he);

		if (!cmp) {
			iter->count += he->count;
			hist_entry__free(he);
			return;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &collapse_hists);
}

void collapse__resort(void)
{
	struct rb_node *next;
	struct hist_entry *n;

	if (!sort__need_collapse)
		return;

	next = rb_first(&hist);
	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		rb_erase(&n->rb_node, &hist);
		collapse__insert_entry(n);
	}
}

/*
 * reverse the map, sort on count.
 */

void output__insert_entry(struct hist_entry *he, u64 min_callchain_hits)
{
	struct rb_node **p = &output_hists.rb_node;
	struct rb_node *parent = NULL;
	struct hist_entry *iter;

	if (callchain)
		callchain_param.sort(&he->sorted_chain, &he->callchain,
				      min_callchain_hits, &callchain_param);

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hist_entry, rb_node);

		if (he->count > iter->count)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&he->rb_node, parent, p);
	rb_insert_color(&he->rb_node, &output_hists);
}

void output__resort(u64 total_samples)
{
	struct rb_node *next;
	struct hist_entry *n;
	struct rb_root *tree = &hist;
	u64 min_callchain_hits;

	min_callchain_hits =
		total_samples * (callchain_param.min_percent / 100);

	if (sort__need_collapse)
		tree = &collapse_hists;

	next = rb_first(tree);

	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		next = rb_next(&n->rb_node);

		rb_erase(&n->rb_node, tree);
		output__insert_entry(n, min_callchain_hits);
	}
}
