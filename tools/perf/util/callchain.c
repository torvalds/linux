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

#ifdef HAVE_DWARF_UNWIND_SUPPORT
static int get_stack_size(const char *str, unsigned long *_size)
{
	char *endptr;
	unsigned long size;
	unsigned long max_size = round_down(USHRT_MAX, sizeof(u64));

	size = strtoul(str, &endptr, 0);

	do {
		if (*endptr)
			break;

		size = round_up(size, sizeof(u64));
		if (!size || size > max_size)
			break;

		*_size = size;
		return 0;

	} while (0);

	pr_err("callchain: Incorrect stack dump size (max %ld): %s\n",
	       max_size, str);
	return -1;
}
#endif /* HAVE_DWARF_UNWIND_SUPPORT */

int parse_callchain_record_opt(const char *arg)
{
	char *tok, *name, *saveptr = NULL;
	char *buf;
	int ret = -1;

	/* We need buffer that we know we can write to. */
	buf = malloc(strlen(arg) + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, arg);

	tok = strtok_r((char *)buf, ",", &saveptr);
	name = tok ? : (char *)buf;

	do {
		/* Framepointer style */
		if (!strncmp(name, "fp", sizeof("fp"))) {
			if (!strtok_r(NULL, ",", &saveptr)) {
				callchain_param.record_mode = CALLCHAIN_FP;
				ret = 0;
			} else
				pr_err("callchain: No more arguments "
				       "needed for -g fp\n");
			break;

#ifdef HAVE_DWARF_UNWIND_SUPPORT
		/* Dwarf style */
		} else if (!strncmp(name, "dwarf", sizeof("dwarf"))) {
			const unsigned long default_stack_dump_size = 8192;

			ret = 0;
			callchain_param.record_mode = CALLCHAIN_DWARF;
			callchain_param.dump_size = default_stack_dump_size;

			tok = strtok_r(NULL, ",", &saveptr);
			if (tok) {
				unsigned long size = 0;

				ret = get_stack_size(tok, &size);
				callchain_param.dump_size = size;
			}
#endif /* HAVE_DWARF_UNWIND_SUPPORT */
		} else {
			pr_err("callchain: Unknown --call-graph option "
			       "value: %s\n", arg);
			break;
		}

	} while (0);

	free(buf);
	return ret;
}

int
parse_callchain_report_opt(const char *arg)
{
	char *tok;
	char *endptr;
	bool minpcnt_set = false;

	symbol_conf.use_callchain = true;

	if (!arg)
		return 0;

	while ((tok = strtok((char *)arg, ",")) != NULL) {
		if (!strncmp(tok, "none", strlen(tok))) {
			callchain_param.mode = CHAIN_NONE;
			symbol_conf.use_callchain = false;
			return 0;
		}

		/* try to get the output mode */
		if (!strncmp(tok, "graph", strlen(tok)))
			callchain_param.mode = CHAIN_GRAPH_ABS;
		else if (!strncmp(tok, "flat", strlen(tok)))
			callchain_param.mode = CHAIN_FLAT;
		else if (!strncmp(tok, "fractal", strlen(tok)))
			callchain_param.mode = CHAIN_GRAPH_REL;
		/* try to get the call chain order */
		else if (!strncmp(tok, "caller", strlen(tok)))
			callchain_param.order = ORDER_CALLER;
		else if (!strncmp(tok, "callee", strlen(tok)))
			callchain_param.order = ORDER_CALLEE;
		/* try to get the sort key */
		else if (!strncmp(tok, "function", strlen(tok)))
			callchain_param.key = CCKEY_FUNCTION;
		else if (!strncmp(tok, "address", strlen(tok)))
			callchain_param.key = CCKEY_ADDRESS;
		/* try to get the min percent */
		else if (!minpcnt_set) {
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

		arg = NULL;
	}

	if (callchain_register_param(&callchain_param) < 0) {
		pr_err("Can't register callchain params\n");
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
static void
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
			return;
		}
		call->ip = cursor_node->ip;
		call->ms.sym = cursor_node->sym;
		call->ms.map = cursor_node->map;
		list_add_tail(&call->list, &node->val);

		callchain_cursor_advance(cursor);
		cursor_node = callchain_cursor_current(cursor);
	}
}

static struct callchain_node *
add_child(struct callchain_node *parent,
	  struct callchain_cursor *cursor,
	  u64 period)
{
	struct callchain_node *new;

	new = create_child(parent, false);
	fill_node(new, cursor);

	new->children_hit = 0;
	new->hit = period;
	return new;
}

static s64 match_chain(struct callchain_cursor_node *node,
		      struct callchain_list *cnode)
{
	struct symbol *sym = node->sym;

	if (cnode->ms.sym && sym &&
	    callchain_param.key == CCKEY_FUNCTION)
		return cnode->ms.sym->start - sym->start;
	else
		return cnode->ip - node->ip;
}

/*
 * Split the parent in two parts (a new child is created) and
 * give a part of its callchain to the created child.
 * Then create another child to host the given callchain of new branch
 */
static void
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

	/* create a new child for the new branch if any */
	if (idx_total < cursor->nr) {
		struct callchain_node *first;
		struct callchain_list *cnode;
		struct callchain_cursor_node *node;
		struct rb_node *p, **pp;

		parent->hit = 0;
		parent->children_hit += period;

		node = callchain_cursor_current(cursor);
		new = add_child(parent, cursor, period);

		/*
		 * This is second child since we moved parent's children
		 * to new (first) child above.
		 */
		p = parent->rb_root_in.rb_node;
		first = rb_entry(p, struct callchain_node, rb_node_in);
		cnode = list_first_entry(&first->val, struct callchain_list,
					 list);

		if (match_chain(node, cnode) < 0)
			pp = &p->rb_left;
		else
			pp = &p->rb_right;

		rb_link_node(&new->rb_node_in, p, pp);
		rb_insert_color(&new->rb_node_in, &parent->rb_root_in);
	} else {
		parent->hit = period;
	}
}

static int
append_chain(struct callchain_node *root,
	     struct callchain_cursor *cursor,
	     u64 period);

static void
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
		return;

	/* lookup in childrens */
	while (*p) {
		s64 ret;

		parent = *p;
		rnode = rb_entry(parent, struct callchain_node, rb_node_in);

		/* If at least first entry matches, rely to children */
		ret = append_chain(rnode, cursor, period);
		if (ret == 0)
			goto inc_children_hit;

		if (ret < 0)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	/* nothing in children, add to the current node */
	rnode = add_child(root, cursor, period);
	rb_link_node(&rnode->rb_node_in, parent, p);
	rb_insert_color(&rnode->rb_node_in, &root->rb_root_in);

inc_children_hit:
	root->children_hit += period;
}

static int
append_chain(struct callchain_node *root,
	     struct callchain_cursor *cursor,
	     u64 period)
{
	struct callchain_list *cnode;
	u64 start = cursor->pos;
	bool found = false;
	u64 matches;
	int cmp = 0;

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
		if (cmp)
			break;

		found = true;

		callchain_cursor_advance(cursor);
	}

	/* matches not, relay no the parent */
	if (!found) {
		WARN_ONCE(!cmp, "Chain comparison error\n");
		return cmp;
	}

	matches = cursor->pos - start;

	/* we match only a part of the node. Split it and add the new chain */
	if (matches < root->val_nr) {
		split_add_child(root, cursor, cnode, start, matches, period);
		return 0;
	}

	/* we match 100% of the path, increment the hit */
	if (matches == root->val_nr && cursor->pos == cursor->nr) {
		root->hit += period;
		return 0;
	}

	/* We match the node and still have a part remaining */
	append_chain_children(root, cursor, period);

	return 0;
}

int callchain_append(struct callchain_root *root,
		     struct callchain_cursor *cursor,
		     u64 period)
{
	if (!cursor->nr)
		return 0;

	callchain_cursor_commit(cursor);

	append_chain_children(&root->node, cursor, period);

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
					list->ms.map, list->ms.sym);
		list_del(&list->list);
		free(list);
	}

	if (src->hit) {
		callchain_cursor_commit(cursor);
		append_chain_children(dst, cursor, src->hit);
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
			    u64 ip, struct map *map, struct symbol *sym)
{
	struct callchain_cursor_node *node = *cursor->last;

	if (!node) {
		node = calloc(1, sizeof(*node));
		if (!node)
			return -ENOMEM;

		*cursor->last = node;
	}

	node->ip = ip;
	node->map = map;
	node->sym = sym;

	cursor->nr++;

	cursor->last = &node->next;

	return 0;
}

int sample__resolve_callchain(struct perf_sample *sample, struct symbol **parent,
			      struct perf_evsel *evsel, struct addr_location *al,
			      int max_stack)
{
	if (sample->callchain == NULL)
		return 0;

	if (symbol_conf.use_callchain || symbol_conf.cumulate_callchain ||
	    sort__has_parent) {
		return machine__resolve_callchain(al->machine, evsel, al->thread,
						  sample, parent, al, max_stack);
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
