/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_SRCLINE_H
#define PERF_SRCLINE_H

#include <linux/list.h>
#include <linux/types.h>

struct dso;
struct symbol;

extern bool srcline_full_filename;
char *get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr);
char *__get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr, bool unwind_inlines);
void free_srcline(char *srcline);

#define SRCLINE_UNKNOWN  ((char *) "??:0")

struct inline_list {
	char			*filename;
	char			*funcname;
	unsigned int		line_nr;
	struct list_head	list;
};

struct inline_node {
	u64			addr;
	struct list_head	val;
};

struct inline_node *dso__parse_addr_inlines(struct dso *dso, u64 addr);
void inline_node__delete(struct inline_node *node);

#endif /* PERF_SRCLINE_H */
