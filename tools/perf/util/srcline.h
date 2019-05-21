/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_SRCLINE_H
#define PERF_SRCLINE_H

#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct dso;
struct symbol;

extern bool srcline_full_filename;
char *get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr, u64 ip);
char *__get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr, bool unwind_inlines,
		  u64 ip);
void free_srcline(char *srcline);
char *get_srcline_split(struct dso *dso, u64 addr, unsigned *line);

/* insert the srcline into the DSO, which will take ownership */
void srcline__tree_insert(struct rb_root_cached *tree, u64 addr, char *srcline);
/* find previously inserted srcline */
char *srcline__tree_find(struct rb_root_cached *tree, u64 addr);
/* delete all srclines within the tree */
void srcline__tree_delete(struct rb_root_cached *tree);

#define SRCLINE_UNKNOWN  ((char *) "??:0")

struct inline_list {
	struct symbol		*symbol;
	char			*srcline;
	struct list_head	list;
};

struct inline_node {
	u64			addr;
	struct list_head	val;
	struct rb_node		rb_node;
};

/* parse inlined frames for the given address */
struct inline_node *dso__parse_addr_inlines(struct dso *dso, u64 addr,
					    struct symbol *sym);
/* free resources associated to the inline node list */
void inline_node__delete(struct inline_node *node);

/* insert the inline node list into the DSO, which will take ownership */
void inlines__tree_insert(struct rb_root_cached *tree,
			  struct inline_node *inlines);
/* find previously inserted inline node list */
struct inline_node *inlines__tree_find(struct rb_root_cached *tree, u64 addr);
/* delete all nodes within the tree of inline_node s */
void inlines__tree_delete(struct rb_root_cached *tree);

#endif /* PERF_SRCLINE_H */
