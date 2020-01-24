/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CALLCHAIN_H
#define __PERF_CALLCHAIN_H

#include <linux/list.h>
#include <linux/rbtree.h>
#include "map_symbol.h"
#include "branch.h"

struct addr_location;
struct evsel;
struct ip_callchain;
struct map;
struct perf_sample;
struct thread;

#define HELP_PAD "\t\t\t\t"

#define CALLCHAIN_HELP "setup and enables call-graph (stack chain/backtrace):\n\n"

# define RECORD_MODE_HELP  HELP_PAD "record_mode:\tcall graph recording mode (fp|dwarf|lbr)\n"

#define RECORD_SIZE_HELP						\
	HELP_PAD "record_size:\tif record_mode is 'dwarf', max size of stack recording (<bytes>)\n" \
	HELP_PAD "\t\tdefault: 8192 (bytes)\n"

#define CALLCHAIN_RECORD_HELP  CALLCHAIN_HELP RECORD_MODE_HELP RECORD_SIZE_HELP

#define CALLCHAIN_REPORT_HELP						\
	HELP_PAD "print_type:\tcall graph printing style (graph|flat|fractal|folded|none)\n" \
	HELP_PAD "threshold:\tminimum call graph inclusion threshold (<percent>)\n" \
	HELP_PAD "print_limit:\tmaximum number of call graph entry (<number>)\n" \
	HELP_PAD "order:\t\tcall graph order (caller|callee)\n" \
	HELP_PAD "sort_key:\tcall graph sort key (function|address)\n"	\
	HELP_PAD "branch:\t\tinclude last branch info to call graph (branch)\n" \
	HELP_PAD "value:\t\tcall graph value (percent|period|count)\n"

enum perf_call_graph_mode {
	CALLCHAIN_NONE,
	CALLCHAIN_FP,
	CALLCHAIN_DWARF,
	CALLCHAIN_LBR,
	CALLCHAIN_MAX
};

enum chain_mode {
	CHAIN_NONE,
	CHAIN_FLAT,
	CHAIN_GRAPH_ABS,
	CHAIN_GRAPH_REL,
	CHAIN_FOLDED,
};

enum chain_order {
	ORDER_CALLER,
	ORDER_CALLEE
};

struct callchain_node {
	struct callchain_node	*parent;
	struct list_head	val;
	struct list_head	parent_val;
	struct rb_node		rb_node_in; /* to insert nodes in an rbtree */
	struct rb_node		rb_node;    /* to sort nodes in an output tree */
	struct rb_root		rb_root_in; /* input tree of children */
	struct rb_root		rb_root;    /* sorted output tree of children */
	unsigned int		val_nr;
	unsigned int		count;
	unsigned int		children_count;
	u64			hit;
	u64			children_hit;
};

struct callchain_root {
	u64			max_depth;
	struct callchain_node	node;
};

struct callchain_param;

typedef void (*sort_chain_func_t)(struct rb_root *, struct callchain_root *,
				 u64, struct callchain_param *);

enum chain_key {
	CCKEY_FUNCTION,
	CCKEY_ADDRESS,
	CCKEY_SRCLINE
};

enum chain_value {
	CCVAL_PERCENT,
	CCVAL_PERIOD,
	CCVAL_COUNT,
};

extern bool dwarf_callchain_users;

struct callchain_param {
	bool			enabled;
	enum perf_call_graph_mode record_mode;
	u32			dump_size;
	enum chain_mode 	mode;
	u16			max_stack;
	u32			print_limit;
	double			min_percent;
	sort_chain_func_t	sort;
	enum chain_order	order;
	bool			order_set;
	enum chain_key		key;
	bool			branch_callstack;
	enum chain_value	value;
};

extern struct callchain_param callchain_param;
extern struct callchain_param callchain_param_default;

struct callchain_list {
	u64			ip;
	struct map_symbol	ms;
	struct /* for TUI */ {
		bool		unfolded;
		bool		has_children;
	};
	u64			branch_count;
	u64			from_count;
	u64			predicted_count;
	u64			abort_count;
	u64			cycles_count;
	u64			iter_count;
	u64			iter_cycles;
	struct branch_type_stat brtype_stat;
	const char		*srcline;
	struct list_head	list;
};

/*
 * A callchain cursor is a single linked list that
 * let one feed a callchain progressively.
 * It keeps persistent allocated entries to minimize
 * allocations.
 */
struct callchain_cursor_node {
	u64				ip;
	struct map_symbol		ms;
	const char			*srcline;
	bool				branch;
	struct branch_flags		branch_flags;
	u64				branch_from;
	int				nr_loop_iter;
	u64				iter_cycles;
	struct callchain_cursor_node	*next;
};

struct callchain_cursor {
	u64				nr;
	struct callchain_cursor_node	*first;
	struct callchain_cursor_node	**last;
	u64				pos;
	struct callchain_cursor_node	*curr;
};

extern __thread struct callchain_cursor callchain_cursor;

static inline void callchain_init(struct callchain_root *root)
{
	INIT_LIST_HEAD(&root->node.val);
	INIT_LIST_HEAD(&root->node.parent_val);

	root->node.parent = NULL;
	root->node.hit = 0;
	root->node.children_hit = 0;
	root->node.rb_root_in = RB_ROOT;
	root->max_depth = 0;
}

static inline u64 callchain_cumul_hits(struct callchain_node *node)
{
	return node->hit + node->children_hit;
}

static inline unsigned callchain_cumul_counts(struct callchain_node *node)
{
	return node->count + node->children_count;
}

int callchain_register_param(struct callchain_param *param);
int callchain_append(struct callchain_root *root,
		     struct callchain_cursor *cursor,
		     u64 period);

int callchain_merge(struct callchain_cursor *cursor,
		    struct callchain_root *dst, struct callchain_root *src);

void callchain_cursor_reset(struct callchain_cursor *cursor);

int callchain_cursor_append(struct callchain_cursor *cursor, u64 ip,
			    struct map_symbol *ms,
			    bool branch, struct branch_flags *flags,
			    int nr_loop_iter, u64 iter_cycles, u64 branch_from,
			    const char *srcline);

/* Close a cursor writing session. Initialize for the reader */
static inline void callchain_cursor_commit(struct callchain_cursor *cursor)
{
	cursor->curr = cursor->first;
	cursor->pos = 0;
}

/* Cursor reading iteration helpers */
static inline struct callchain_cursor_node *
callchain_cursor_current(struct callchain_cursor *cursor)
{
	if (cursor->pos == cursor->nr)
		return NULL;

	return cursor->curr;
}

static inline void callchain_cursor_advance(struct callchain_cursor *cursor)
{
	cursor->curr = cursor->curr->next;
	cursor->pos++;
}

int callchain_cursor__copy(struct callchain_cursor *dst,
			   struct callchain_cursor *src);

struct option;
struct hist_entry;

int record_parse_callchain_opt(const struct option *opt, const char *arg, int unset);
int record_callchain_opt(const struct option *opt, const char *arg, int unset);

struct record_opts;

int record_opts__parse_callchain(struct record_opts *record,
				 struct callchain_param *callchain,
				 const char *arg, bool unset);

int sample__resolve_callchain(struct perf_sample *sample,
			      struct callchain_cursor *cursor, struct symbol **parent,
			      struct evsel *evsel, struct addr_location *al,
			      int max_stack);
int hist_entry__append_callchain(struct hist_entry *he, struct perf_sample *sample);
int fill_callchain_info(struct addr_location *al, struct callchain_cursor_node *node,
			bool hide_unresolved);

extern const char record_callchain_help[];
int parse_callchain_record(const char *arg, struct callchain_param *param);
int parse_callchain_record_opt(const char *arg, struct callchain_param *param);
int parse_callchain_report_opt(const char *arg);
int parse_callchain_top_opt(const char *arg);
int perf_callchain_config(const char *var, const char *value);

static inline void callchain_cursor_snapshot(struct callchain_cursor *dest,
					     struct callchain_cursor *src)
{
	*dest = *src;

	dest->first = src->curr;
	dest->nr -= src->pos;
}

#ifdef HAVE_SKIP_CALLCHAIN_IDX
int arch_skip_callchain_idx(struct thread *thread, struct ip_callchain *chain);
#else
static inline int arch_skip_callchain_idx(struct thread *thread __maybe_unused,
			struct ip_callchain *chain __maybe_unused)
{
	return -1;
}
#endif

char *callchain_list__sym_name(struct callchain_list *cl,
			       char *bf, size_t bfsize, bool show_dso);
char *callchain_node__scnprintf_value(struct callchain_node *node,
				      char *bf, size_t bfsize, u64 total);
int callchain_node__fprintf_value(struct callchain_node *node,
				  FILE *fp, u64 total);

int callchain_list_counts__printf_value(struct callchain_list *clist,
					FILE *fp, char *bf, int bfsize);

void free_callchain(struct callchain_root *root);
void decay_callchain(struct callchain_root *root);
int callchain_node__make_parent_list(struct callchain_node *node);

int callchain_branch_counts(struct callchain_root *root,
			    u64 *branch_count, u64 *predicted_count,
			    u64 *abort_count, u64 *cycles_count);

#endif	/* __PERF_CALLCHAIN_H */
