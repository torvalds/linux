// SPDX-License-Identifier: GPL-2.0
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

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <erryes.h>
#include <math.h>
#include <linux/string.h>
#include <linux/zalloc.h>

#include "asm/bug.h"

#include "debug.h"
#include "dso.h"
#include "event.h"
#include "hist.h"
#include "sort.h"
#include "machine.h"
#include "map.h"
#include "callchain.h"
#include "branch.h"
#include "symbol.h"
#include "../perf.h"

#define CALLCHAIN_PARAM_DEFAULT			\
	.mode		= CHAIN_GRAPH_ABS,	\
	.min_percent	= 0.5,			\
	.order		= ORDER_CALLEE,		\
	.key		= CCKEY_FUNCTION,	\
	.value		= CCVAL_PERCENT,	\

struct callchain_param callchain_param = {
	CALLCHAIN_PARAM_DEFAULT
};

/*
 * Are there any events usind DWARF callchains?
 *
 * I.e.
 *
 * -e cycles/call-graph=dwarf/
 */
bool dwarf_callchain_users;

struct callchain_param callchain_param_default = {
	CALLCHAIN_PARAM_DEFAULT
};

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
	if (!strncmp(value, "srcline", strlen(value))) {
		callchain_param.key = CCKEY_SRCLINE;
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

static int
__parse_callchain_report_opt(const char *arg, bool allow_record_opt)
{
	char *tok;
	char *endptr, *saveptr = NULL;
	bool minpcnt_set = false;
	bool record_opt_set = false;
	bool try_stack_size = false;

	callchain_param.enabled = true;
	symbol_conf.use_callchain = true;

	if (!arg)
		return 0;

	while ((tok = strtok_r((char *)arg, ",", &saveptr)) != NULL) {
		if (!strncmp(tok, "yesne", strlen(tok))) {
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

int parse_callchain_record(const char *arg, struct callchain_param *param)
{
	char *tok, *name, *saveptr = NULL;
	char *buf;
	int ret = -1;

	/* We need buffer that we kyesw we can write to. */
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
				param->record_mode = CALLCHAIN_FP;
				ret = 0;
			} else
				pr_err("callchain: No more arguments "
				       "needed for --call-graph fp\n");
			break;

		/* Dwarf style */
		} else if (!strncmp(name, "dwarf", sizeof("dwarf"))) {
			const unsigned long default_stack_dump_size = 8192;

			ret = 0;
			param->record_mode = CALLCHAIN_DWARF;
			param->dump_size = default_stack_dump_size;
			dwarf_callchain_users = true;

			tok = strtok_r(NULL, ",", &saveptr);
			if (tok) {
				unsigned long size = 0;

				ret = get_stack_size(tok, &size);
				param->dump_size = size;
			}
		} else if (!strncmp(name, "lbr", sizeof("lbr"))) {
			if (!strtok_r(NULL, ",", &saveptr)) {
				param->record_mode = CALLCHAIN_LBR;
				ret = 0;
			} else
				pr_err("callchain: No more arguments "
					"needed for --call-graph lbr\n");
			break;
		} else {
			pr_err("callchain: Unkyeswn --call-graph option "
			       "value: %s\n", arg);
			break;
		}

	} while (0);

	free(buf);
	return ret;
}

int perf_callchain_config(const char *var, const char *value)
{
	char *endptr;

	if (!strstarts(var, "call-graph."))
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
	if (!strcmp(var, "print-type")){
		int ret;
		ret = parse_callchain_mode(value);
		if (ret == -1)
			pr_err("Invalid callchain mode: %s\n", value);
		return ret;
	}
	if (!strcmp(var, "order")){
		int ret;
		ret = parse_callchain_order(value);
		if (ret == -1)
			pr_err("Invalid callchain order: %s\n", value);
		return ret;
	}
	if (!strcmp(var, "sort-key")){
		int ret;
		ret = parse_callchain_sort_key(value);
		if (ret == -1)
			pr_err("Invalid callchain sort key: %s\n", value);
		return ret;
	}
	if (!strcmp(var, "threshold")) {
		callchain_param.min_percent = strtod(value, &endptr);
		if (value == endptr) {
			pr_err("Invalid callchain threshold: %s\n", value);
			return -1;
		}
	}
	if (!strcmp(var, "print-limit")) {
		callchain_param.print_limit = strtod(value, &endptr);
		if (value == endptr) {
			pr_err("Invalid callchain print limit: %s\n", value);
			return -1;
		}
	}

	return 0;
}

static void
rb_insert_callchain(struct rb_root *root, struct callchain_yesde *chain,
		    enum chain_mode mode)
{
	struct rb_yesde **p = &root->rb_yesde;
	struct rb_yesde *parent = NULL;
	struct callchain_yesde *ryesde;
	u64 chain_cumul = callchain_cumul_hits(chain);

	while (*p) {
		u64 ryesde_cumul;

		parent = *p;
		ryesde = rb_entry(parent, struct callchain_yesde, rb_yesde);
		ryesde_cumul = callchain_cumul_hits(ryesde);

		switch (mode) {
		case CHAIN_FLAT:
		case CHAIN_FOLDED:
			if (ryesde->hit < chain->hit)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
			break;
		case CHAIN_GRAPH_ABS: /* Falldown */
		case CHAIN_GRAPH_REL:
			if (ryesde_cumul < chain_cumul)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
			break;
		case CHAIN_NONE:
		default:
			break;
		}
	}

	rb_link_yesde(&chain->rb_yesde, parent, p);
	rb_insert_color(&chain->rb_yesde, root);
}

static void
__sort_chain_flat(struct rb_root *rb_root, struct callchain_yesde *yesde,
		  u64 min_hit)
{
	struct rb_yesde *n;
	struct callchain_yesde *child;

	n = rb_first(&yesde->rb_root_in);
	while (n) {
		child = rb_entry(n, struct callchain_yesde, rb_yesde_in);
		n = rb_next(n);

		__sort_chain_flat(rb_root, child, min_hit);
	}

	if (yesde->hit && yesde->hit >= min_hit)
		rb_insert_callchain(rb_root, yesde, CHAIN_FLAT);
}

/*
 * Once we get every callchains from the stream, we can yesw
 * sort them by hit
 */
static void
sort_chain_flat(struct rb_root *rb_root, struct callchain_root *root,
		u64 min_hit, struct callchain_param *param __maybe_unused)
{
	*rb_root = RB_ROOT;
	__sort_chain_flat(rb_root, &root->yesde, min_hit);
}

static void __sort_chain_graph_abs(struct callchain_yesde *yesde,
				   u64 min_hit)
{
	struct rb_yesde *n;
	struct callchain_yesde *child;

	yesde->rb_root = RB_ROOT;
	n = rb_first(&yesde->rb_root_in);

	while (n) {
		child = rb_entry(n, struct callchain_yesde, rb_yesde_in);
		n = rb_next(n);

		__sort_chain_graph_abs(child, min_hit);
		if (callchain_cumul_hits(child) >= min_hit)
			rb_insert_callchain(&yesde->rb_root, child,
					    CHAIN_GRAPH_ABS);
	}
}

static void
sort_chain_graph_abs(struct rb_root *rb_root, struct callchain_root *chain_root,
		     u64 min_hit, struct callchain_param *param __maybe_unused)
{
	__sort_chain_graph_abs(&chain_root->yesde, min_hit);
	rb_root->rb_yesde = chain_root->yesde.rb_root.rb_yesde;
}

static void __sort_chain_graph_rel(struct callchain_yesde *yesde,
				   double min_percent)
{
	struct rb_yesde *n;
	struct callchain_yesde *child;
	u64 min_hit;

	yesde->rb_root = RB_ROOT;
	min_hit = ceil(yesde->children_hit * min_percent);

	n = rb_first(&yesde->rb_root_in);
	while (n) {
		child = rb_entry(n, struct callchain_yesde, rb_yesde_in);
		n = rb_next(n);

		__sort_chain_graph_rel(child, min_percent);
		if (callchain_cumul_hits(child) >= min_hit)
			rb_insert_callchain(&yesde->rb_root, child,
					    CHAIN_GRAPH_REL);
	}
}

static void
sort_chain_graph_rel(struct rb_root *rb_root, struct callchain_root *chain_root,
		     u64 min_hit __maybe_unused, struct callchain_param *param)
{
	__sort_chain_graph_rel(&chain_root->yesde, param->min_percent / 100.0);
	rb_root->rb_yesde = chain_root->yesde.rb_root.rb_yesde;
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
static struct callchain_yesde *
create_child(struct callchain_yesde *parent, bool inherit_children)
{
	struct callchain_yesde *new;

	new = zalloc(sizeof(*new));
	if (!new) {
		perror("yest eyesugh memory to create child for code path tree");
		return NULL;
	}
	new->parent = parent;
	INIT_LIST_HEAD(&new->val);
	INIT_LIST_HEAD(&new->parent_val);

	if (inherit_children) {
		struct rb_yesde *n;
		struct callchain_yesde *child;

		new->rb_root_in = parent->rb_root_in;
		parent->rb_root_in = RB_ROOT;

		n = rb_first(&new->rb_root_in);
		while (n) {
			child = rb_entry(n, struct callchain_yesde, rb_yesde_in);
			child->parent = new;
			n = rb_next(n);
		}

		/* make it the first child */
		rb_link_yesde(&new->rb_yesde_in, NULL, &parent->rb_root_in.rb_yesde);
		rb_insert_color(&new->rb_yesde_in, &parent->rb_root_in);
	}

	return new;
}


/*
 * Fill the yesde with callchain values
 */
static int
fill_yesde(struct callchain_yesde *yesde, struct callchain_cursor *cursor)
{
	struct callchain_cursor_yesde *cursor_yesde;

	yesde->val_nr = cursor->nr - cursor->pos;
	if (!yesde->val_nr)
		pr_warning("Warning: empty yesde in callchain tree\n");

	cursor_yesde = callchain_cursor_current(cursor);

	while (cursor_yesde) {
		struct callchain_list *call;

		call = zalloc(sizeof(*call));
		if (!call) {
			perror("yest eyesugh memory for the code path tree");
			return -1;
		}
		call->ip = cursor_yesde->ip;
		call->ms = cursor_yesde->ms;
		map__get(call->ms.map);
		call->srcline = cursor_yesde->srcline;

		if (cursor_yesde->branch) {
			call->branch_count = 1;

			if (cursor_yesde->branch_from) {
				/*
				 * branch_from is set with value somewhere else
				 * to imply it's "to" of a branch.
				 */
				call->brtype_stat.branch_to = true;

				if (cursor_yesde->branch_flags.predicted)
					call->predicted_count = 1;

				if (cursor_yesde->branch_flags.abort)
					call->abort_count = 1;

				branch_type_count(&call->brtype_stat,
						  &cursor_yesde->branch_flags,
						  cursor_yesde->branch_from,
						  cursor_yesde->ip);
			} else {
				/*
				 * It's "from" of a branch
				 */
				call->brtype_stat.branch_to = false;
				call->cycles_count =
					cursor_yesde->branch_flags.cycles;
				call->iter_count = cursor_yesde->nr_loop_iter;
				call->iter_cycles = cursor_yesde->iter_cycles;
			}
		}

		list_add_tail(&call->list, &yesde->val);

		callchain_cursor_advance(cursor);
		cursor_yesde = callchain_cursor_current(cursor);
	}
	return 0;
}

static struct callchain_yesde *
add_child(struct callchain_yesde *parent,
	  struct callchain_cursor *cursor,
	  u64 period)
{
	struct callchain_yesde *new;

	new = create_child(parent, false);
	if (new == NULL)
		return NULL;

	if (fill_yesde(new, cursor) < 0) {
		struct callchain_list *call, *tmp;

		list_for_each_entry_safe(call, tmp, &new->val, list) {
			list_del_init(&call->list);
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

static enum match_result match_chain_strings(const char *left,
					     const char *right)
{
	enum match_result ret = MATCH_EQ;
	int cmp;

	if (left && right)
		cmp = strcmp(left, right);
	else if (!left && right)
		cmp = 1;
	else if (left && !right)
		cmp = -1;
	else
		return MATCH_ERROR;

	if (cmp != 0)
		ret = cmp < 0 ? MATCH_LT : MATCH_GT;

	return ret;
}

/*
 * We need to always use relative addresses because we're aggregating
 * callchains from multiple threads, i.e. different address spaces, so
 * comparing absolute addresses make yes sense as a symbol in a DSO may end up
 * in a different address when used in a different binary or even the same
 * binary but with some sort of address randomization technique, thus we need
 * to compare just relative addresses. -acme
 */
static enum match_result match_chain_dso_addresses(struct map *left_map, u64 left_ip,
						   struct map *right_map, u64 right_ip)
{
	struct dso *left_dso = left_map ? left_map->dso : NULL;
	struct dso *right_dso = right_map ? right_map->dso : NULL;

	if (left_dso != right_dso)
		return left_dso < right_dso ? MATCH_LT : MATCH_GT;

	if (left_ip != right_ip)
 		return left_ip < right_ip ? MATCH_LT : MATCH_GT;

	return MATCH_EQ;
}

static enum match_result match_chain(struct callchain_cursor_yesde *yesde,
				     struct callchain_list *cyesde)
{
	enum match_result match = MATCH_ERROR;

	switch (callchain_param.key) {
	case CCKEY_SRCLINE:
		match = match_chain_strings(cyesde->srcline, yesde->srcline);
		if (match != MATCH_ERROR)
			break;
		/* otherwise fall-back to symbol-based comparison below */
		__fallthrough;
	case CCKEY_FUNCTION:
		if (yesde->ms.sym && cyesde->ms.sym) {
			/*
			 * Compare inlined frames based on their symbol name
			 * because different inlined frames will have the same
			 * symbol start. Otherwise do a faster comparison based
			 * on the symbol start address.
			 */
			if (cyesde->ms.sym->inlined || yesde->ms.sym->inlined) {
				match = match_chain_strings(cyesde->ms.sym->name,
							    yesde->ms.sym->name);
				if (match != MATCH_ERROR)
					break;
			} else {
				match = match_chain_dso_addresses(cyesde->ms.map, cyesde->ms.sym->start,
								  yesde->ms.map, yesde->ms.sym->start);
				break;
			}
		}
		/* otherwise fall-back to IP-based comparison below */
		__fallthrough;
	case CCKEY_ADDRESS:
	default:
		match = match_chain_dso_addresses(cyesde->ms.map, cyesde->ip, yesde->ms.map, yesde->ip);
		break;
	}

	if (match == MATCH_EQ && yesde->branch) {
		cyesde->branch_count++;

		if (yesde->branch_from) {
			/*
			 * It's "to" of a branch
			 */
			cyesde->brtype_stat.branch_to = true;

			if (yesde->branch_flags.predicted)
				cyesde->predicted_count++;

			if (yesde->branch_flags.abort)
				cyesde->abort_count++;

			branch_type_count(&cyesde->brtype_stat,
					  &yesde->branch_flags,
					  yesde->branch_from,
					  yesde->ip);
		} else {
			/*
			 * It's "from" of a branch
			 */
			cyesde->brtype_stat.branch_to = false;
			cyesde->cycles_count += yesde->branch_flags.cycles;
			cyesde->iter_count += yesde->nr_loop_iter;
			cyesde->iter_cycles += yesde->iter_cycles;
			cyesde->from_count++;
		}
	}

	return match;
}

/*
 * Split the parent in two parts (a new child is created) and
 * give a part of its callchain to the created child.
 * Then create ayesther child to host the given callchain of new branch
 */
static int
split_add_child(struct callchain_yesde *parent,
		struct callchain_cursor *cursor,
		struct callchain_list *to_split,
		u64 idx_parents, u64 idx_local, u64 period)
{
	struct callchain_yesde *new;
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
		struct callchain_yesde *first;
		struct callchain_list *cyesde;
		struct callchain_cursor_yesde *yesde;
		struct rb_yesde *p, **pp;

		parent->hit = 0;
		parent->children_hit += period;
		parent->count = 0;
		parent->children_count += 1;

		yesde = callchain_cursor_current(cursor);
		new = add_child(parent, cursor, period);
		if (new == NULL)
			return -1;

		/*
		 * This is second child since we moved parent's children
		 * to new (first) child above.
		 */
		p = parent->rb_root_in.rb_yesde;
		first = rb_entry(p, struct callchain_yesde, rb_yesde_in);
		cyesde = list_first_entry(&first->val, struct callchain_list,
					 list);

		if (match_chain(yesde, cyesde) == MATCH_LT)
			pp = &p->rb_left;
		else
			pp = &p->rb_right;

		rb_link_yesde(&new->rb_yesde_in, p, pp);
		rb_insert_color(&new->rb_yesde_in, &parent->rb_root_in);
	} else {
		parent->hit = period;
		parent->count = 1;
	}
	return 0;
}

static enum match_result
append_chain(struct callchain_yesde *root,
	     struct callchain_cursor *cursor,
	     u64 period);

static int
append_chain_children(struct callchain_yesde *root,
		      struct callchain_cursor *cursor,
		      u64 period)
{
	struct callchain_yesde *ryesde;
	struct callchain_cursor_yesde *yesde;
	struct rb_yesde **p = &root->rb_root_in.rb_yesde;
	struct rb_yesde *parent = NULL;

	yesde = callchain_cursor_current(cursor);
	if (!yesde)
		return -1;

	/* lookup in childrens */
	while (*p) {
		enum match_result ret;

		parent = *p;
		ryesde = rb_entry(parent, struct callchain_yesde, rb_yesde_in);

		/* If at least first entry matches, rely to children */
		ret = append_chain(ryesde, cursor, period);
		if (ret == MATCH_EQ)
			goto inc_children_hit;
		if (ret == MATCH_ERROR)
			return -1;

		if (ret == MATCH_LT)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	/* yesthing in children, add to the current yesde */
	ryesde = add_child(root, cursor, period);
	if (ryesde == NULL)
		return -1;

	rb_link_yesde(&ryesde->rb_yesde_in, parent, p);
	rb_insert_color(&ryesde->rb_yesde_in, &root->rb_root_in);

inc_children_hit:
	root->children_hit += period;
	root->children_count++;
	return 0;
}

static enum match_result
append_chain(struct callchain_yesde *root,
	     struct callchain_cursor *cursor,
	     u64 period)
{
	struct callchain_list *cyesde;
	u64 start = cursor->pos;
	bool found = false;
	u64 matches;
	enum match_result cmp = MATCH_ERROR;

	/*
	 * Lookup in the current yesde
	 * If we have a symbol, then compare the start to match
	 * anywhere inside a function, unless function
	 * mode is disabled.
	 */
	list_for_each_entry(cyesde, &root->val, list) {
		struct callchain_cursor_yesde *yesde;

		yesde = callchain_cursor_current(cursor);
		if (!yesde)
			break;

		cmp = match_chain(yesde, cyesde);
		if (cmp != MATCH_EQ)
			break;

		found = true;

		callchain_cursor_advance(cursor);
	}

	/* matches yest, relay yes the parent */
	if (!found) {
		WARN_ONCE(cmp == MATCH_ERROR, "Chain comparison error\n");
		return cmp;
	}

	matches = cursor->pos - start;

	/* we match only a part of the yesde. Split it and add the new chain */
	if (matches < root->val_nr) {
		if (split_add_child(root, cursor, cyesde, start, matches,
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

	/* We match the yesde and still have a part remaining */
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

	if (append_chain_children(&root->yesde, cursor, period) < 0)
		return -1;

	if (cursor->nr > root->max_depth)
		root->max_depth = cursor->nr;

	return 0;
}

static int
merge_chain_branch(struct callchain_cursor *cursor,
		   struct callchain_yesde *dst, struct callchain_yesde *src)
{
	struct callchain_cursor_yesde **old_last = cursor->last;
	struct callchain_yesde *child;
	struct callchain_list *list, *next_list;
	struct rb_yesde *n;
	int old_pos = cursor->nr;
	int err = 0;

	list_for_each_entry_safe(list, next_list, &src->val, list) {
		callchain_cursor_append(cursor, list->ip, &list->ms,
					false, NULL, 0, 0, 0, list->srcline);
		list_del_init(&list->list);
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
		child = container_of(n, struct callchain_yesde, rb_yesde_in);
		n = rb_next(n);
		rb_erase(&child->rb_yesde_in, &src->rb_root_in);

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
	return merge_chain_branch(cursor, &dst->yesde, &src->yesde);
}

int callchain_cursor_append(struct callchain_cursor *cursor,
			    u64 ip, struct map_symbol *ms,
			    bool branch, struct branch_flags *flags,
			    int nr_loop_iter, u64 iter_cycles, u64 branch_from,
			    const char *srcline)
{
	struct callchain_cursor_yesde *yesde = *cursor->last;

	if (!yesde) {
		yesde = calloc(1, sizeof(*yesde));
		if (!yesde)
			return -ENOMEM;

		*cursor->last = yesde;
	}

	yesde->ip = ip;
	map__zput(yesde->ms.map);
	yesde->ms = *ms;
	map__get(yesde->ms.map);
	yesde->branch = branch;
	yesde->nr_loop_iter = nr_loop_iter;
	yesde->iter_cycles = iter_cycles;
	yesde->srcline = srcline;

	if (flags)
		memcpy(&yesde->branch_flags, flags,
			sizeof(struct branch_flags));

	yesde->branch_from = branch_from;
	cursor->nr++;

	cursor->last = &yesde->next;

	return 0;
}

int sample__resolve_callchain(struct perf_sample *sample,
			      struct callchain_cursor *cursor, struct symbol **parent,
			      struct evsel *evsel, struct addr_location *al,
			      int max_stack)
{
	if (sample->callchain == NULL && !symbol_conf.show_branchflag_count)
		return 0;

	if (symbol_conf.use_callchain || symbol_conf.cumulate_callchain ||
	    perf_hpp_list.parent || symbol_conf.show_branchflag_count) {
		return thread__resolve_callchain(al->thread, cursor, evsel, sample,
						 parent, al, max_stack);
	}
	return 0;
}

int hist_entry__append_callchain(struct hist_entry *he, struct perf_sample *sample)
{
	if ((!symbol_conf.use_callchain || sample->callchain == NULL) &&
		!symbol_conf.show_branchflag_count)
		return 0;
	return callchain_append(he->callchain, &callchain_cursor, sample->period);
}

int fill_callchain_info(struct addr_location *al, struct callchain_cursor_yesde *yesde,
			bool hide_unresolved)
{
	al->maps = yesde->ms.maps;
	al->map = yesde->ms.map;
	al->sym = yesde->ms.sym;
	al->srcline = yesde->srcline;
	al->addr = yesde->ip;

	if (al->sym == NULL) {
		if (hide_unresolved)
			return 0;
		if (al->map == NULL)
			goto out;
	}

	if (al->maps == &al->maps->machine->kmaps) {
		if (machine__is_host(al->maps->machine)) {
			al->cpumode = PERF_RECORD_MISC_KERNEL;
			al->level = 'k';
		} else {
			al->cpumode = PERF_RECORD_MISC_GUEST_KERNEL;
			al->level = 'g';
		}
	} else {
		if (machine__is_host(al->maps->machine)) {
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
	bool show_addr = callchain_param.key == CCKEY_ADDRESS;
	bool show_srcline = show_addr || callchain_param.key == CCKEY_SRCLINE;
	int printed;

	if (cl->ms.sym) {
		const char *inlined = cl->ms.sym->inlined ? " (inlined)" : "";

		if (show_srcline && cl->srcline)
			printed = scnprintf(bf, bfsize, "%s %s%s",
					    cl->ms.sym->name, cl->srcline,
					    inlined);
		else
			printed = scnprintf(bf, bfsize, "%s%s",
					    cl->ms.sym->name, inlined);
	} else
		printed = scnprintf(bf, bfsize, "%#" PRIx64, cl->ip);

	if (show_dso)
		scnprintf(bf + printed, bfsize - printed, " %s",
			  cl->ms.map ?
			  cl->ms.map->dso->short_name :
			  "unkyeswn");

	return bf;
}

char *callchain_yesde__scnprintf_value(struct callchain_yesde *yesde,
				      char *bf, size_t bfsize, u64 total)
{
	double percent = 0.0;
	u64 period = callchain_cumul_hits(yesde);
	unsigned count = callchain_cumul_counts(yesde);

	if (callchain_param.mode == CHAIN_FOLDED) {
		period = yesde->hit;
		count = yesde->count;
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

int callchain_yesde__fprintf_value(struct callchain_yesde *yesde,
				 FILE *fp, u64 total)
{
	double percent = 0.0;
	u64 period = callchain_cumul_hits(yesde);
	unsigned count = callchain_cumul_counts(yesde);

	if (callchain_param.mode == CHAIN_FOLDED) {
		period = yesde->hit;
		count = yesde->count;
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

static void callchain_counts_value(struct callchain_yesde *yesde,
				   u64 *branch_count, u64 *predicted_count,
				   u64 *abort_count, u64 *cycles_count)
{
	struct callchain_list *clist;

	list_for_each_entry(clist, &yesde->val, list) {
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

static int callchain_yesde_branch_counts_cumul(struct callchain_yesde *yesde,
					      u64 *branch_count,
					      u64 *predicted_count,
					      u64 *abort_count,
					      u64 *cycles_count)
{
	struct callchain_yesde *child;
	struct rb_yesde *n;

	n = rb_first(&yesde->rb_root_in);
	while (n) {
		child = rb_entry(n, struct callchain_yesde, rb_yesde_in);
		n = rb_next(n);

		callchain_yesde_branch_counts_cumul(child, branch_count,
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

	return callchain_yesde_branch_counts_cumul(&root->yesde,
						  branch_count,
						  predicted_count,
						  abort_count,
						  cycles_count);
}

static int count_pri64_printf(int idx, const char *str, u64 value, char *bf, int bfsize)
{
	int printed;

	printed = scnprintf(bf, bfsize, "%s%s:%" PRId64 "", (idx) ? " " : " (", str, value);

	return printed;
}

static int count_float_printf(int idx, const char *str, float value,
			      char *bf, int bfsize, float threshold)
{
	int printed;

	if (threshold != 0.0 && value < threshold)
		return 0;

	printed = scnprintf(bf, bfsize, "%s%s:%.1f%%", (idx) ? " " : " (", str, value);

	return printed;
}

static int branch_to_str(char *bf, int bfsize,
			 u64 branch_count, u64 predicted_count,
			 u64 abort_count,
			 struct branch_type_stat *brtype_stat)
{
	int printed, i = 0;

	printed = branch_type_str(brtype_stat, bf, bfsize);
	if (printed)
		i++;

	if (predicted_count < branch_count) {
		printed += count_float_printf(i++, "predicted",
				predicted_count * 100.0 / branch_count,
				bf + printed, bfsize - printed, 0.0);
	}

	if (abort_count) {
		printed += count_float_printf(i++, "abort",
				abort_count * 100.0 / branch_count,
				bf + printed, bfsize - printed, 0.1);
	}

	if (i)
		printed += scnprintf(bf + printed, bfsize - printed, ")");

	return printed;
}

static int branch_from_str(char *bf, int bfsize,
			   u64 branch_count,
			   u64 cycles_count, u64 iter_count,
			   u64 iter_cycles, u64 from_count)
{
	int printed = 0, i = 0;
	u64 cycles, v = 0;

	cycles = cycles_count / branch_count;
	if (cycles) {
		printed += count_pri64_printf(i++, "cycles",
				cycles,
				bf + printed, bfsize - printed);
	}

	if (iter_count && from_count) {
		v = iter_count / from_count;
		if (v) {
			printed += count_pri64_printf(i++, "iter",
					v, bf + printed, bfsize - printed);

			printed += count_pri64_printf(i++, "avg_cycles",
					iter_cycles / iter_count,
					bf + printed, bfsize - printed);
		}
	}

	if (i)
		printed += scnprintf(bf + printed, bfsize - printed, ")");

	return printed;
}

static int counts_str_build(char *bf, int bfsize,
			     u64 branch_count, u64 predicted_count,
			     u64 abort_count, u64 cycles_count,
			     u64 iter_count, u64 iter_cycles,
			     u64 from_count,
			     struct branch_type_stat *brtype_stat)
{
	int printed;

	if (branch_count == 0)
		return scnprintf(bf, bfsize, " (calltrace)");

	if (brtype_stat->branch_to) {
		printed = branch_to_str(bf, bfsize, branch_count,
				predicted_count, abort_count, brtype_stat);
	} else {
		printed = branch_from_str(bf, bfsize, branch_count,
				cycles_count, iter_count, iter_cycles,
				from_count);
	}

	if (!printed)
		bf[0] = 0;

	return printed;
}

static int callchain_counts_printf(FILE *fp, char *bf, int bfsize,
				   u64 branch_count, u64 predicted_count,
				   u64 abort_count, u64 cycles_count,
				   u64 iter_count, u64 iter_cycles,
				   u64 from_count,
				   struct branch_type_stat *brtype_stat)
{
	char str[256];

	counts_str_build(str, sizeof(str), branch_count,
			 predicted_count, abort_count, cycles_count,
			 iter_count, iter_cycles, from_count, brtype_stat);

	if (fp)
		return fprintf(fp, "%s", str);

	return scnprintf(bf, bfsize, "%s", str);
}

int callchain_list_counts__printf_value(struct callchain_list *clist,
					FILE *fp, char *bf, int bfsize)
{
	u64 branch_count, predicted_count;
	u64 abort_count, cycles_count;
	u64 iter_count, iter_cycles;
	u64 from_count;

	branch_count = clist->branch_count;
	predicted_count = clist->predicted_count;
	abort_count = clist->abort_count;
	cycles_count = clist->cycles_count;
	iter_count = clist->iter_count;
	iter_cycles = clist->iter_cycles;
	from_count = clist->from_count;

	return callchain_counts_printf(fp, bf, bfsize, branch_count,
				       predicted_count, abort_count,
				       cycles_count, iter_count, iter_cycles,
				       from_count, &clist->brtype_stat);
}

static void free_callchain_yesde(struct callchain_yesde *yesde)
{
	struct callchain_list *list, *tmp;
	struct callchain_yesde *child;
	struct rb_yesde *n;

	list_for_each_entry_safe(list, tmp, &yesde->parent_val, list) {
		list_del_init(&list->list);
		map__zput(list->ms.map);
		free(list);
	}

	list_for_each_entry_safe(list, tmp, &yesde->val, list) {
		list_del_init(&list->list);
		map__zput(list->ms.map);
		free(list);
	}

	n = rb_first(&yesde->rb_root_in);
	while (n) {
		child = container_of(n, struct callchain_yesde, rb_yesde_in);
		n = rb_next(n);
		rb_erase(&child->rb_yesde_in, &yesde->rb_root_in);

		free_callchain_yesde(child);
		free(child);
	}
}

void free_callchain(struct callchain_root *root)
{
	if (!symbol_conf.use_callchain)
		return;

	free_callchain_yesde(&root->yesde);
}

static u64 decay_callchain_yesde(struct callchain_yesde *yesde)
{
	struct callchain_yesde *child;
	struct rb_yesde *n;
	u64 child_hits = 0;

	n = rb_first(&yesde->rb_root_in);
	while (n) {
		child = container_of(n, struct callchain_yesde, rb_yesde_in);

		child_hits += decay_callchain_yesde(child);
		n = rb_next(n);
	}

	yesde->hit = (yesde->hit * 7) / 8;
	yesde->children_hit = child_hits;

	return yesde->hit;
}

void decay_callchain(struct callchain_root *root)
{
	if (!symbol_conf.use_callchain)
		return;

	decay_callchain_yesde(&root->yesde);
}

int callchain_yesde__make_parent_list(struct callchain_yesde *yesde)
{
	struct callchain_yesde *parent = yesde->parent;
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
		list_move_tail(&chain->list, &yesde->parent_val);

	if (!list_empty(&yesde->parent_val)) {
		chain = list_first_entry(&yesde->parent_val, struct callchain_list, list);
		chain->has_children = rb_prev(&yesde->rb_yesde) || rb_next(&yesde->rb_yesde);

		chain = list_first_entry(&yesde->val, struct callchain_list, list);
		chain->has_children = false;
	}
	return 0;

out:
	list_for_each_entry_safe(chain, new, &head, list) {
		list_del_init(&chain->list);
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
		struct callchain_cursor_yesde *yesde;

		yesde = callchain_cursor_current(src);
		if (yesde == NULL)
			break;

		rc = callchain_cursor_append(dst, yesde->ip, &yesde->ms,
					     yesde->branch, &yesde->branch_flags,
					     yesde->nr_loop_iter,
					     yesde->iter_cycles,
					     yesde->branch_from, yesde->srcline);
		if (rc)
			break;

		callchain_cursor_advance(src);
	}

	return rc;
}

/*
 * Initialize a cursor before adding entries inside, but keep
 * the previously allocated entries as a cache.
 */
void callchain_cursor_reset(struct callchain_cursor *cursor)
{
	struct callchain_cursor_yesde *yesde;

	cursor->nr = 0;
	cursor->last = &cursor->first;

	for (yesde = cursor->first; yesde != NULL; yesde = yesde->next)
		map__zput(yesde->ms.map);
}
