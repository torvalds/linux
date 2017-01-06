/*
 * Copyright (C) 2009-2011, Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Handle the callchains from the stream in an ad-hoc radix tree and then
 * sort them in an rbtree.
 *
 * Using a radix for code path provides a fast retrieval and factorizes
 * memory use. Also that lets us use the paths in a hierarchical graph view.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#include "asm/bug.h"

#include "hist.h"
#include "util.h"
#include "sort.h"
#include "machine.h"
#include "callchain.h"

__thread struct callchain_cursor callchain_cursor;

int parse_callchain_record_opt(const char *arg, struct callchain_param *param)
{
	return parse_callchain_record(arg, param);
}

static int parse_callchain_mode(const char *value)
{
	if (!strncmp(value, "graph", strlen(value))) {
		callchain_param.mode = CHAIN_GRAPH_ABS;
		return 0;
	}
	if (!strncmp(value, "flat", strlen(value))) {
		callchain_param.mode = CHAIN_FLAT;
		return 0;
	}
	if (!strncmp(value, "fractal", strlen(value))) {
		callchain_param.mode = CHAIN_GRAPH_REL;
		return 0;
	}
	if (!strncmp(value, "folded", strlen(value))) {
		callchain_param.mode = CHAIN_FOLDED;
		return 0;
	}
	return -1;
}

static int parse_callchain_order(const char *value)
{
	if (!strncmp(value, "caller", strlen(value))) {
		callchain_param.order = ORDER_CALLER;
		callchain_param.order_set = true;
		return 0;
	}
	if (!strncmp(value, "callee", strlen(value))) {
		callchain_param.order = ORDER_CALLEE;
		callchain_param.order_set = true;
		return 0;
	}
	return -1;
}

static int parse_callchain_sort_key(const char *value)
{
	if (!strncmp(value, "function", strlen(value))) {
		callchain_param.key = CCKEY_FUNCTION;
		return 0;
	}
	if (!strncmp(value, "address", strlen(value))) {
		callchain_param.key = CCKEY_ADDRESS;
		return 0;
	}
	if (!strncmp(value, "branch", strlen(value))) {
		callchain_param.branch_callstack = 1;
		return 0;
	}
	return -1;
}

static int parse_callchain_value(const char *value)
{
	if (!strncmp(value, "percent", strlen(value))) {
		callchain_param.value = CCVAL_PERCENT;
		return 0;
	}
	if (!strncmp(value, "period", strlen(value))) {
		callchain_param.value = CCVAL_PERIOD;
		return 0;
	}
	if (!strncmp(value, "count", strlen(value))) {
		callchain_param.value = CCVAL_COUNT;
		return 0;
	}
	return -1;
}

static int
__parse_callchain_report_opt(const char *arg, bool allow_record_opt)
{
	char *tok;
	char *endptr;
	bool minpcnt_set = false;
	bool record_opt_set = false;
	bool try_stack_size = false;

	callchain_param.enabled = true;
	symbol_conf.use_callchain = true;

	if (!arg)
		return 0;

	while ((tok = strtok((char *)arg, ",")) != NULL) {
		if (!strncmp(tok, "none", strlen(tok))) {
			callchain_param.mode = CHAIN_NONE;
			callchain_param.enabled = false;
			symbol_conf.use_callchain = false;
			return 0;
		}

		if (!parse_callchain_mode(tok) ||
		    !parse_callchain_order(tok) ||
		    !parse_callchain_sort_key(tok) ||
		    !parse_callchain_value(tok)) {
			/* parsing ok - move on to the next */
			try_stack_size = false;
			goto next;
		} else if (allow_record_opt && !record_opt_set) {
			if (parse_callchain_record(tok, &callchain_param))
				goto try_numbers;

			/* assume that number followed by 'dwarf' is stack size */
			if (callchain_param.record_mode == CALLCHAIN_DWARF)
				try_stack_size = true;

			record_opt_set = true;
			goto next;
		}

try_numbers:
		if (try_stack_size) {
			unsigned long size = 0;

			if (get_stack_size(tok, &size) < 0)
				return -1;
			callchain_param.dump_size = size;
			try_stack_size = false;
		} else if (!minpcnt_set) {
			/* try to get the min percent */
			callchain_param.min_percent = strtod(tok, &endptr);
			if (tok == endptr)
				return -1;
			minpcnt_set = true;
		} else {
			/* try print limit at last */
			callchain_param.print_limit = strtoul(tok, &endptr, 0);
			if (tok == endptr)
				return -1;
		}
next:
		arg = NULL;
	}

	if (callchain_register_param(&callchain_param) < 0) {
		pr_err("Can't register callchain params\n");
		return -1;
	}
	return 0;
}

int parse_callchain_report_opt(const char *arg)
{
	return __parse_callchain_report_opt(arg, false);
}

int parse_callchain_top_opt(const char *arg)
{
	return __parse_callchain_report_opt(arg, true);
}

int perf_callchain_config(const char *var, const char *value)
{
	char *endptr;

	if (prefixcmp(var, "call-graph."))
		return 0;
	var += sizeof("call-graph.") - 1;

	if (!strcmp(var, "record-mode"))
		return parse_callchain_record_opt(value, &callchain_param);
	if (!strcmp(var, "dump-size")) {
		unsigned long size = 0;
		int ret;

		ret = get_stack_size(value, &size);
		callchain_param.dump_size = size;

		return ret;
	}
	if (!strcmp(var, "print-type"))
		return parse_callchain_mode(value);
	if (!strcmp(var, "order"))
		return parse_callchain_order(value);
	if (!strcmp(var, "sort-key"))
		return parse_callchain_sort_key(value);
	if (!strcmp(var, "threshold")) {
		callchain_param.min_percent = strtod(value, &endptr);
		if (value == endptr)
			return -1;
	}
	if (!strcmp(var, "print-limit")) {
		callchain_param.print_limit = strtod(value, &endptr);
		if (value == endptr)
			return -1;
	}

	return 0;
}

static void
rb_insert_callchain(struct rb_root *root, struct callchain_node *chain,
		    enum chain_mode mode)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct callchain_node *rnode;
	u64 chain_cumul = callchain_cumul_hits(chain);

	while (*p) {
		u64 rnode_cumul;

		parent = *p;
		rnode = rb_entry(parent, struct callchain_node, rb_node);
		rnode_cumul = callchain_cumul_hits(rnode);

		switch (mode) {
		case CHAIN_FLAT:
		case CHAIN_FOLDED:
			if (rnode->hit < chain->hit)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
			break;
		case CHAIN_GRAPH_ABS: /* Falldown */
		case CHAIN_GRAPH_REL:
			if (rnode_cumul < chain_cumul)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
			break;
		case CHAIN_NONE:
		default:
			break;
		}
	}

	rb_link_node(&chain->rb_node, parent, p);
	rb_insert_color(&chain->rb_node, root);
}

static void
__sort_chain_flat(struct rb_root *rb_root, struct callchain_node *node,
		  u64 min_hit)
{
	struct rb_node *n;
	struct callchain_node *child;

	n = rb_first(&node->rb_root_in);
	while (n) {
		child = rb_entry(n, struct callchain_node, rb_node_in);
		n = rb_next(n);

		__sort_chain_flat(rb_root, child, min_hit);
	}

	if (node->hit && node->hit >= min_hit)
		rb_insert_callchain(rb_root, node, CHAIN_FLAT);
}

/*
 * Once we get every callchains from the stream, we can now
 * sort them by hit
 */
static void
sort_chain_flat(struct rb_root *rb_root, struct callchain_root *root,
		u64 min_hit, struct callchain_param *param __maybe_unused)
{
	*rb_root = RB_ROOT;
	__sort_chain_flat(rb_root, &root->node, min_hit);
}

static void __sort_chain_graph_abs(struct callchain_node *node,
				   u64 min_hit)
{
	struct rb_node *n;
	struct callchain_node *child;

	node->rb_root = RB_ROOT;
	n = rb_first(&node->rb_root_in);

	while (n) {
		child = rb_entry(n, struct callchain_node, rb_node_in);
		n = rb_next(n);

		__sort_chain_graph_abs(child, min_hit);
		if (callchain_cumul_hits(child) >= min_hit)
			rb_insert_callchain(&node->rb_root, child,
					    CHAIN_GRAPH_ABS);
	}
}

static void
sort_chain_graph_abs(struct rb_root *rb_root, struct callchain_root *chain_root,
		     u64 min_hit, struct callchain_param *param __maybe_unused)
{
	__sort_chain_graph_abs(&chain_root->node, min_hit);
	rb_root->rb_node = chain_root->node.rb_root.rb_node;
}

static void __sort_chain_graph_rel(struct callchain_node *node,
				   double min_percent)
{
	struct rb_node *n;
	struct callchain_node *child;
	u64 min_hit;

	node->rb_root = RB_ROOT;
	min_hit = ceil(node->children_hit * min_percent);

	n = rb_first(&node->rb_root_in);
	while (n) {
		child = rb_entry(n, struct callchain_node, rb_node_in);
		n = rb_next(n);

		__sort_chain_graph_rel(child, min_percent);
		if (callchain_cumul_hits(child) >= min_hit)
			rb_insert_callchain(&node->rb_root, child,
					    CHAIN_GRAPH_REL);
	}
}

static void
sort_chain_graph_rel(struct rb_root *rb_root, struct callchain_root *chain_root,
		     u64 min_hit __maybe_unused, struct callchain_param *param)
{
	__sort_chain_graph_rel(&chain_root->node, param->min_percent / 100.0);
	rb_root->rb_node = chain_root->node.rb_root.rb_node;
}

int callchain_register_param(struct callchain_param *param)
{
	switch (param->mode) {
	case CHAIN_GRAPH_ABS:
		param->sort = sort_chain_graph_abs;
		break;
	case CHAIN_GRAPH_REL:
		param->sort = sort_chain_graph_rel;
		break;
	case CHAIN_FLAT:
	case CHAIN_FOLDED:
		param->sort = sort_chain_flat;
		break;
	case CHAIN_NONE:
	default:
		return -1;
	}
	return 0;
}

/*
 * Create a child for a parent. If inherit_children, then the new child
 * will become the new parent of it's parent children
 */
static struct callchain_node *
create_child(struct callchain_node *parent, bool inherit_children)
{
	struct callchain_node *new;

	new = zalloc(sizeof(*new));
	if (!new) {
		perror("not enough memory to create child for code path tree");
		return NULL;
	}
	new->parent = parent;
	INIT_LIST_HEAD(&new->val);
	INIT_LIST_HEAD(&new->parent_val);

	if (inherit_children) {
		struct rb_node *n;
		struct callchain_node *child;

		new->rb_root_in = parent->rb_root_in;
		parent->rb_root_in = RB_ROOT;

		n = rb_first(&new->rb_root_in);
		while (n) {
			child = rb_entry(n, struct callchain_node, rb_node_in);
			child->parent = new;
			n = rb_next(n);
		}

		/* make it the first child */
		rb_link_node(&new->rb_node_in, NULL, &parent->rb_root_in.rb_node);
		rb_insert_color(&new->rb_node_in, &parent->rb_root_in);
	}

	return new;
}


/*
 * Fill the node with callchain values
 */
static int
fill_node(struct callchain_node *node, struct callchain_cursor *cursor)
{
	struct callchain_cursor_node *cursor_node;

	node->val_nr = cursor->nr - cursor->pos;
	if (!node->val_nr)
		pr_warning("Warning: empty node in callchain tree\n");

	cursor_node = callchain_cursor_current(cursor);

	while (cursor_node) {
		struct callchain_list *call;

		call = zalloc(sizeof(*call));
		if (!call) {
			perror("not enough memory for the code path tree");
			return -1;
		}
		call->ip = cursor_node->ip;
		call->ms.sym = cursor_node->sym;
		call->ms.map = map__get(cursor_node->map);

		if (cursor_node->branch) {
			call->branch_count = 1;

			if (cursor_node->branch_flags.predicted)
				call->predicted_count = 1;

			if (cursor_node->branch_flags.abort)
				call->abort_count = 1;

			call->cycles_count = cursor_node->branch_flags.cycles;
			call->iter_count = cursor_node->nr_loop_iter;
			call->samples_count = cursor_node->samples;
		}

		list_add_tail(&call->list, &node->val);

		callchain_cursor_advance(cursor);
		cursor_node = callchain_cursor_current(cursor);
	}
	return 0;
}

static struct callchain_node *
add_child(struct callchain_node *parent,
	  struct callchain_cursor *cursor,
	  u64 period)
{
	struct callchain_node *new;

	new = create_child(parent, false);
	if (new == NULL)
		return NULL;

	if (fill_node(new, cursor) < 0) {
		struct callchain_list *call, *tmp;

		list_for_each_entry_safe(call, tmp, &new->val, list) {
			list_del(&call->list);
			map__zput(call->ms.map);
			free(call);
		}
		free(new);
		return NULL;
	}

	new->children_hit = 0;
	new->hit = period;
	new->children_count = 0;
	new->count = 1;
	return new;
}

enum match_result {
	MATCH_ERROR  = -1,
	MATCH_EQ,
	MATCH_LT,
	MATCH_GT,
};

static enum match_result match_chain(struct callchain_cursor_node *node,
				     struct callchain_list *cnode)
{
	struct symbol *sym = node->sym;
	u64 left, right;

	if (cnode->ms.sym && sym &&
	    callchain_param.key == CCKEY_FUNCTION) {
		left = cnode->ms.sym->start;
		right = sym->start;
	} else {
		left = cnode->ip;
		right = node->ip;
	}

	if (left == right) {
		if (node->branch) {
			cnode->branch_count++;

			if (node->branch_flags.predicted)
				cnode->predicted_count++;

			if (node->branch_flags.abort)
				cnode->abort_count++;

			cnode->cycles_count += node->branch_flags.cycles;
			cnode->iter_count += node->nr_loop_iter;
			cnode->samples_count += node->samples;
		}

		return MATCH_EQ;
	}

	return left > right ? MATCH_GT : MATCH_LT;
}

/*
 * Split the parent in two parts (a new child is created) and
 * give a part of its callchain to the created child.
 * Then create another child to host the given callchain of new branch
 */
static int
split_add_child(struct callchain_node *parent,
		struct callchain_cursor *cursor,
		struct callchain_list *to_split,
		u64 idx_parents, u64 idx_local, u64 period)
{
	struct callchain_node *new;
	struct list_head *old_tail;
	unsigned int idx_total = idx_parents + idx_local;

	/* split */
	new = create_child(parent, true);
	if (new == NULL)
		return -1;

	/* split the callchain and move a part to the new child */
	old_tail = parent->val.prev;
	list_del_range(&to_split->list, old_tail);
	new->val.next = &to_split->list;
	new->val.prev = old_tail;
	to_split->list.prev = &new->val;
	old_tail->next = &new->val;

	/* split the hits */
	new->hit = parent->hit;
	new->children_hit = parent->children_hit;
	parent->children_hit = callchain_cumul_hits(new);
	new->val_nr = parent->val_nr - idx_local;
	parent->val_nr = idx_local;
	new->count = parent->count;
	new->children_count = parent->children_count;
	parent->children_count = callchain_cumul_counts(new);

	/* create a new child for the new branch if any */
	if (idx_total < cursor->nr) {
		struct callchain_node *first;
		struct callchain_list *cnode;
		struct callchain_cursor_node *node;
		struct rb_node *p, **pp;

		parent->hit = 0;
		parent->children_hit += period;
		parent->count = 0;
		parent->children_count += 1;

		node = callchain_cursor_current(cursor);
		new = add_child(parent, cursor, period);
		if (new == NULL)
			return -1;

		/*
		 * This is second child since we moved parent's children
		 * to new (first) child above.
		 */
		p = parent->rb_root_in.rb_node;
		first = rb_entry(p, struct callchain_node, rb_node_in);
		cnode = list_first_entry(&first->val, struct callchain_list,
					 list);

		if (match_chain(node, cnode) == MATCH_LT)
			pp = &p->rb_left;
		else
			pp = &p->rb_right;

		rb_link_node(&new->rb_node_in, p, pp);
		rb_insert_color(&new->rb_node_in, &parent->rb_root_in);
	} else {
		parent->hit = period;
		parent->count = 1;
	}
	return 0;
}

static enum match_result
append_chain(struct callchain_node *root,
	     struct callchain_cursor *cursor,
	     u64 period);

static int
append_chain_children(struct callchain_node *root,
		      struct callchain_cursor *cursor,
		      u64 period)
{
	struct callchain_node *rnode;
	struct callchain_cursor_node *node;
	struct rb_node **p = &root->rb_root_in.rb_node;
	struct rb_node *parent = NULL;

	node = callchain_cursor_current(cursor);
	if (!node)
		return -1;

	/* lookup in childrens */
	while (*p) {
		enum match_result ret;

		parent = *p;
		rnode = rb_entry(parent, struct callchain_node, rb_node_in);

		/* If at least first entry matches, rely to children */
		ret = append_chain(rnode, cursor, period);
		if (ret == MATCH_EQ)
			goto inc_children_hit;
		if (ret == MATCH_ERROR)
			return -1;

		if (ret == MATCH_LT)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	/* nothing in children, add to the current node */
	rnode = add_child(root, cursor, period);
	if (rnode == NULL)
		return -1;

	rb_link_node(&rnode->rb_node_in, parent, p);
	rb_insert_color(&rnode->rb_node_in, &root->rb_root_in);

inc_children_hit:
	root->children_hit += period;
	root->children_count++;
	return 0;
}

static enum match_result
append_chain(struct callchain_node *root,
	     struct callchain_cursor *cursor,
	     u64 period)
{
	struct callchain_list *cnode;
	u64 start = cursor->pos;
	bool found = false;
	u64 matches;
	enum match_result cmp = MATCH_ERROR;

	/*
	 * Lookup in the current node
	 * If we have a symbol, then compare the start to match
	 * anywhere inside a function, unless function
	 * mode is disabled.
	 */
	list_for_each_entry(cnode, &root->val, list) {
		struct callchain_cursor_node *node;

		node = callchain_cursor_current(cursor);
		if (!node)
			break;

		cmp = match_chain(node, cnode);
		if (cmp != MATCH_EQ)
			break;

		found = true;

		callchain_cursor_advance(cursor);
	}

	/* matches not, relay no the parent */
	if (!found) {
		WARN_ONCE(cmp == MATCH_ERROR, "Chain comparison error\n");
		return cmp;
	}

	matches = cursor->pos - start;

	/* we match only a part of the node. Split it and add the new chain */
	if (matches < root->val_nr) {
		if (split_add_child(root, cursor, cnode, start, matches,
				    period) < 0)
			return MATCH_ERROR;

		return MATCH_EQ;
	}

	/* we match 100% of the path, increment the hit */
	if (matches == root->val_nr && cursor->pos == cursor->nr) {
		root->hit += period;
		root->count++;
		return MATCH_EQ;
	}

	/* We match the node and still have a part remaining */
	if (append_chain_children(root, cursor, period) < 0)
		return MATCH_ERROR;

	return MATCH_EQ;
}

int callchain_append(struct callchain_root *root,
		     struct callchain_cursor *cursor,
		     u64 period)
{
	if (!cursor->nr)
		return 0;

	callchain_cursor_commit(cursor);

	if (append_chain_children(&root->node, cursor, period) < 0)
		return -1;

	if (cursor->nr > root->max_depth)
		root->max_depth = cursor->nr;

	return 0;
}

static int
merge_chain_branch(struct callchain_cursor *cursor,
		   struct callchain_node *dst, struct callchain_node *src)
{
	struct callchain_cursor_node **old_last = cursor->last;
	struct callchain_node *child;
	struct callchain_list *list, *next_list;
	struct rb_node *n;
	int old_pos = cursor->nr;
	int err = 0;

	list_for_each_entry_safe(list, next_list, &src->val, list) {
		callchain_cursor_append(cursor, list->ip,
					list->ms.map, list->ms.sym,
					false, NULL, 0, 0);
		list_del(&list->list);
		map__zput(list->ms.map);
		free(list);
	}

	if (src->hit) {
		callchain_cursor_commit(cursor);
		if (append_chain_children(dst, cursor, src->hit) < 0)
			return -1;
	}

	n = rb_first(&src->rb_root_in);
	while (n) {
		child = container_of(n, struct callchain_node, rb_node_in);
		n = rb_next(n);
		rb_erase(&child->rb_node_in, &src->rb_root_in);

		err = merge_chain_branch(cursor, dst, child);
		if (err)
			break;

		free(child);
	}

	cursor->nr = old_pos;
	cursor->last = old_last;

	return err;
}

int callchain_merge(struct callchain_cursor *cursor,
		    struct callchain_root *dst, struct callchain_root *src)
{
	return merge_chain_branch(cursor, &dst->node, &src->node);
}

int callchain_cursor_append(struct callchain_cursor *cursor,
			    u64 ip, struct map *map, struct symbol *sym,
			    bool branch, struct branch_flags *flags,
			    int nr_loop_iter, int samples)
{
	struct callchain_cursor_node *node = *cursor->last;

	if (!node) {
		node = calloc(1, sizeof(*node));
		if (!node)
			return -ENOMEM;

		*cursor->last = node;
	}

	node->ip = ip;
	map__zput(node->map);
	node->map = map__get(map);
	node->sym = sym;
	node->branch = branch;
	node->nr_loop_iter = nr_loop_iter;
	node->samples = samples;

	if (flags)
		memcpy(&node->branch_flags, flags,
			sizeof(struct branch_flags));

	cursor->nr++;

	cursor->last = &node->next;

	return 0;
}

int sample__resolve_callchain(struct perf_sample *sample,
			      struct callchain_cursor *cursor, struct symbol **parent,
			      struct perf_evsel *evsel, struct addr_location *al,
			      int max_stack)
{
	if (sample->callchain == NULL)
		return 0;

	if (symbol_conf.use_callchain || symbol_conf.cumulate_callchain ||
	    perf_hpp_list.parent) {
		return thread__resolve_callchain(al->thread, cursor, evsel, sample,
						 parent, al, max_stack);
	}
	return 0;
}

int hist_entry__append_callchain(struct hist_entry *he, struct perf_sample *sample)
{
	if (!symbol_conf.use_callchain || sample->callchain == NULL)
		return 0;
	return callchain_append(he->callchain, &callchain_cursor, sample->period);
}

int fill_callchain_info(struct addr_location *al, struct callchain_cursor_node *node,
			bool hide_unresolved)
{
	al->map = node->map;
	al->sym = node->sym;
	if (node->map)
		al->addr = node->map->map_ip(node->map, node->ip);
	else
		al->addr = node->ip;

	if (al->sym == NULL) {
		if (hide_unresolved)
			return 0;
		if (al->map == NULL)
			goto out;
	}

	if (al->map->groups == &al->machine->kmaps) {
		if (machine__is_host(al->machine)) {
			al->cpumode = PERF_RECORD_MISC_KERNEL;
			al->level = 'k';
		} else {
			al->cpumode = PERF_RECORD_MISC_GUEST_KERNEL;
			al->level = 'g';
		}
	} else {
		if (machine__is_host(al->machine)) {
			al->cpumode = PERF_RECORD_MISC_USER;
			al->level = '.';
		} else if (perf_guest) {
			al->cpumode = PERF_RECORD_MISC_GUEST_USER;
			al->level = 'u';
		} else {
			al->cpumode = PERF_RECORD_MISC_HYPERVISOR;
			al->level = 'H';
		}
	}

out:
	return 1;
}

char *callchain_list__sym_name(struct callchain_list *cl,
			       char *bf, size_t bfsize, bool show_dso)
{
	int printed;

	if (cl->ms.sym) {
		if (callchain_param.key == CCKEY_ADDRESS &&
		    cl->ms.map && !cl->srcline)
			cl->srcline = get_srcline(cl->ms.map->dso,
						  map__rip_2objdump(cl->ms.map,
								    cl->ip),
						  cl->ms.sym, false);
		if (cl->srcline)
			printed = scnprintf(bf, bfsize, "%s %s",
					cl->ms.sym->name, cl->srcline);
		else
			printed = scnprintf(bf, bfsize, "%s", cl->ms.sym->name);
	} else
		printed = scnprintf(bf, bfsize, "%#" PRIx64, cl->ip);

	if (show_dso)
		scnprintf(bf + printed, bfsize - printed, " %s",
			  cl->ms.map ?
			  cl->ms.map->dso->short_name :
			  "unknown");

	return bf;
}

char *callchain_node__scnprintf_value(struct callchain_node *node,
				      char *bf, size_t bfsize, u64 total)
{
	double percent = 0.0;
	u64 period = callchain_cumul_hits(node);
	unsigned count = callchain_cumul_counts(node);

	if (callchain_param.mode == CHAIN_FOLDED) {
		period = node->hit;
		count = node->count;
	}

	switch (callchain_param.value) {
	case CCVAL_PERIOD:
		scnprintf(bf, bfsize, "%"PRIu64, period);
		break;
	case CCVAL_COUNT:
		scnprintf(bf, bfsize, "%u", count);
		break;
	case CCVAL_PERCENT:
	default:
		if (total)
			percent = period * 100.0 / total;
		scnprintf(bf, bfsize, "%.2f%%", percent);
		break;
	}
	return bf;
}

int callchain_node__fprintf_value(struct callchain_node *node,
				 FILE *fp, u64 total)
{
	double percent = 0.0;
	u64 period = callchain_cumul_hits(node);
	unsigned count = callchain_cumul_counts(node);

	if (callchain_param.mode == CHAIN_FOLDED) {
		period = node->hit;
		count = node->count;
	}

	switch (callchain_param.value) {
	case CCVAL_PERIOD:
		return fprintf(fp, "%"PRIu64, period);
	case CCVAL_COUNT:
		return fprintf(fp, "%u", count);
	case CCVAL_PERCENT:
	default:
		if (total)
			percent = period * 100.0 / total;
		return percent_color_fprintf(fp, "%.2f%%", percent);
	}
	return 0;
}

static void callchain_counts_value(struct callchain_node *node,
				   u64 *branch_count, u64 *predicted_count,
				   u64 *abort_count, u64 *cycles_count)
{
	struct callchain_list *clist;

	list_for_each_entry(clist, &node->val, list) {
		if (branch_count)
			*branch_count += clist->branch_count;

		if (predicted_count)
			*predicted_count += clist->predicted_count;

		if (abort_count)
			*abort_count += clist->abort_count;

		if (cycles_count)
			*cycles_count += clist->cycles_count;
	}
}

static int callchain_node_branch_counts_cumul(struct callchain_node *node,
					      u64 *branch_count,
					      u64 *predicted_count,
					      u64 *abort_count,
					      u64 *cycles_count)
{
	struct callchain_node *child;
	struct rb_node *n;

	n = rb_first(&node->rb_root_in);
	while (n) {
		child = rb_entry(n, struct callchain_node, rb_node_in);
		n = rb_next(n);

		callchain_node_branch_counts_cumul(child, branch_count,
						   predicted_count,
						   abort_count,
						   cycles_count);

		callchain_counts_value(child, branch_count,
				       predicted_count, abort_count,
				       cycles_count);
	}

	return 0;
}

int callchain_branch_counts(struct callchain_root *root,
			    u64 *branch_count, u64 *predicted_count,
			    u64 *abort_count, u64 *cycles_count)
{
	if (branch_count)
		*branch_count = 0;

	if (predicted_count)
		*predicted_count = 0;

	if (abort_count)
		*abort_count = 0;

	if (cycles_count)
		*cycles_count = 0;

	return callchain_node_branch_counts_cumul(&root->node,
						  branch_count,
						  predicted_count,
						  abort_count,
						  cycles_count);
}

static int callchain_counts_printf(FILE *fp, char *bf, int bfsize,
				   u64 branch_count, u64 predicted_count,
				   u64 abort_count, u64 cycles_count,
				   u64 iter_count, u64 samples_count)
{
	double predicted_percent = 0.0;
	const char *null_str = "";
	char iter_str[32];
	char *str;
	u64 cycles = 0;

	if (branch_count == 0) {
		if (fp)
			return fprintf(fp, " (calltrace)");

		return scnprintf(bf, bfsize, " (calltrace)");
	}

	if (iter_count && samples_count) {
		scnprintf(iter_str, sizeof(iter_str),
			 ", iterations:%" PRId64 "",
			 iter_count / samples_count);
		str = iter_str;
	} else
		str = (char *)null_str;

	predicted_percent = predicted_count * 100.0 / branch_count;
	cycles = cycles_count / branch_count;

	if ((predicted_percent >= 100.0) && (abort_count == 0)) {
		if (fp)
			return fprintf(fp, " (cycles:%" PRId64 "%s)",
				       cycles, str);

		return scnprintf(bf, bfsize, " (cycles:%" PRId64 "%s)",
				 cycles, str);
	}

	if ((predicted_percent < 100.0) && (abort_count == 0)) {
		if (fp)
			return fprintf(fp,
				" (predicted:%.1f%%, cycles:%" PRId64 "%s)",
				predicted_percent, cycles, str);

		return scnprintf(bf, bfsize,
			" (predicted:%.1f%%, cycles:%" PRId64 "%s)",
			predicted_percent, cycles, str);
	}

	if (fp)
		return fprintf(fp,
		" (predicted:%.1f%%, abort:%" PRId64 ", cycles:%" PRId64 "%s)",
			predicted_percent, abort_count, cycles, str);

	return scnprintf(bf, bfsize,
		" (predicted:%.1f%%, abort:%" PRId64 ", cycles:%" PRId64 "%s)",
		predicted_percent, abort_count, cycles, str);
}

int callchain_list_counts__printf_value(struct callchain_node *node,
					struct callchain_list *clist,
					FILE *fp, char *bf, int bfsize)
{
	u64 branch_count, predicted_count;
	u64 abort_count, cycles_count;
	u64 iter_count = 0, samples_count = 0;

	branch_count = clist->branch_count;
	predicted_count = clist->predicted_count;
	abort_count = clist->abort_count;
	cycles_count = clist->cycles_count;

	if (node) {
		struct callchain_list *call;

		list_for_each_entry(call, &node->val, list) {
			iter_count += call->iter_count;
			samples_count += call->samples_count;
		}
	}

	return callchain_counts_printf(fp, bf, bfsize, branch_count,
				       predicted_count, abort_count,
				       cycles_count, iter_count, samples_count);
}

static void free_callchain_node(struct callchain_node *node)
{
	struct callchain_list *list, *tmp;
	struct callchain_node *child;
	struct rb_node *n;

	list_for_each_entry_safe(list, tmp, &node->parent_val, list) {
		list_del(&list->list);
		map__zput(list->ms.map);
		free(list);
	}

	list_for_each_entry_safe(list, tmp, &node->val, list) {
		list_del(&list->list);
		map__zput(list->ms.map);
		free(list);
	}

	n = rb_first(&node->rb_root_in);
	while (n) {
		child = container_of(n, struct callchain_node, rb_node_in);
		n = rb_next(n);
		rb_erase(&child->rb_node_in, &node->rb_root_in);

		free_callchain_node(child);
		free(child);
	}
}

void free_callchain(struct callchain_root *root)
{
	if (!symbol_conf.use_callchain)
		return;

	free_callchain_node(&root->node);
}

static u64 decay_callchain_node(struct callchain_node *node)
{
	struct callchain_node *child;
	struct rb_node *n;
	u64 child_hits = 0;

	n = rb_first(&node->rb_root_in);
	while (n) {
		child = container_of(n, struct callchain_node, rb_node_in);

		child_hits += decay_callchain_node(child);
		n = rb_next(n);
	}

	node->hit = (node->hit * 7) / 8;
	node->children_hit = child_hits;

	return node->hit;
}

void decay_callchain(struct callchain_root *root)
{
	if (!symbol_conf.use_callchain)
		return;

	decay_callchain_node(&root->node);
}

int callchain_node__make_parent_list(struct callchain_node *node)
{
	struct callchain_node *parent = node->parent;
	struct callchain_list *chain, *new;
	LIST_HEAD(head);

	while (parent) {
		list_for_each_entry_reverse(chain, &parent->val, list) {
			new = malloc(sizeof(*new));
			if (new == NULL)
				goto out;
			*new = *chain;
			new->has_children = false;
			map__get(new->ms.map);
			list_add_tail(&new->list, &head);
		}
		parent = parent->parent;
	}

	list_for_each_entry_safe_reverse(chain, new, &head, list)
		list_move_tail(&chain->list, &node->parent_val);

	if (!list_empty(&node->parent_val)) {
		chain = list_first_entry(&node->parent_val, struct callchain_list, list);
		chain->has_children = rb_prev(&node->rb_node) || rb_next(&node->rb_node);

		chain = list_first_entry(&node->val, struct callchain_list, list);
		chain->has_children = false;
	}
	return 0;

out:
	list_for_each_entry_safe(chain, new, &head, list) {
		list_del(&chain->list);
		map__zput(chain->ms.map);
		free(chain);
	}
	return -ENOMEM;
}

int callchain_cursor__copy(struct callchain_cursor *dst,
			   struct callchain_cursor *src)
{
	int rc = 0;

	callchain_cursor_reset(dst);
	callchain_cursor_commit(src);

	while (true) {
		struct callchain_cursor_node *node;

		node = callchain_cursor_current(src);
		if (node == NULL)
			break;

		rc = callchain_cursor_append(dst, node->ip, node->map, node->sym,
					     node->branch, &node->branch_flags,
					     node->nr_loop_iter, node->samples);
		if (rc)
			break;

		callchain_cursor_advance(src);
	}

	return rc;
}
