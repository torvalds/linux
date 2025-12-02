// SPDX-License-Identifier: GPL-2.0
#include "srcline.h"
#include "addr2line.h"
#include "dso.h"
#include "callchain.h"
#include "libbfd.h"
#include "llvm.h"
#include "symbol.h"

#include <inttypes.h>
#include <string.h>

bool srcline_full_filename;

char *srcline__unknown = (char *)"??:0";

static const char *srcline_dso_name(struct dso *dso)
{
	const char *dso_name;

	if (dso__symsrc_filename(dso))
		dso_name = dso__symsrc_filename(dso);
	else
		dso_name = dso__long_name(dso);

	if (dso_name[0] == '[')
		return NULL;

	if (is_perf_pid_map_name(dso_name))
		return NULL;

	return dso_name;
}

int inline_list__append(struct symbol *symbol, char *srcline, struct inline_node *node)
{
	struct inline_list *ilist;

	ilist = zalloc(sizeof(*ilist));
	if (ilist == NULL)
		return -1;

	ilist->symbol = symbol;
	ilist->srcline = srcline;

	if (callchain_param.order == ORDER_CALLEE)
		list_add_tail(&ilist->list, &node->val);
	else
		list_add(&ilist->list, &node->val);

	return 0;
}

/* basename version that takes a const input string */
static const char *gnu_basename(const char *path)
{
	const char *base = strrchr(path, '/');

	return base ? base + 1 : path;
}

char *srcline_from_fileline(const char *file, unsigned int line)
{
	char *srcline;

	if (!file)
		return NULL;

	if (!srcline_full_filename)
		file = gnu_basename(file);

	if (asprintf(&srcline, "%s:%u", file, line) < 0)
		return NULL;

	return srcline;
}

struct symbol *new_inline_sym(struct dso *dso,
			      struct symbol *base_sym,
			      const char *funcname)
{
	struct symbol *inline_sym;
	char *demangled = NULL;

	if (!funcname)
		funcname = "??";

	if (dso) {
		demangled = dso__demangle_sym(dso, 0, funcname);
		if (demangled)
			funcname = demangled;
	}

	if (base_sym && strcmp(funcname, base_sym->name) == 0) {
		/* reuse the real, existing symbol */
		inline_sym = base_sym;
		/* ensure that we don't alias an inlined symbol, which could
		 * lead to double frees in inline_node__delete
		 */
		assert(!base_sym->inlined);
	} else {
		/* create a fake symbol for the inline frame */
		inline_sym = symbol__new(base_sym ? base_sym->start : 0,
					 base_sym ? (base_sym->end - base_sym->start) : 0,
					 base_sym ? base_sym->binding : 0,
					 base_sym ? base_sym->type : 0,
					 funcname);
		if (inline_sym)
			inline_sym->inlined = 1;
	}

	free(demangled);

	return inline_sym;
}

static int addr2line(const char *dso_name, u64 addr, char **file, unsigned int *line_nr,
		     struct dso *dso, bool unwind_inlines, struct inline_node *node,
		     struct symbol *sym)
{
	int ret;

	ret = llvm__addr2line(dso_name, addr, file, line_nr, dso, unwind_inlines, node, sym);
	if (ret > 0)
		return ret;

	ret = libbfd__addr2line(dso_name, addr, file, line_nr, dso, unwind_inlines, node, sym);
	if (ret > 0)
		return ret;

	return cmd__addr2line(dso_name, addr, file, line_nr, dso, unwind_inlines, node, sym);
}

static struct inline_node *addr2inlines(const char *dso_name, u64 addr,
					struct dso *dso, struct symbol *sym)
{
	struct inline_node *node;

	node = zalloc(sizeof(*node));
	if (node == NULL) {
		perror("not enough memory for the inline node");
		return NULL;
	}

	INIT_LIST_HEAD(&node->val);
	node->addr = addr;

	addr2line(dso_name, addr, /*file=*/NULL, /*line_nr=*/NULL, dso,
		  /*unwind_inlines=*/true, node, sym);

	return node;
}

/*
 * Number of addr2line failures (without success) before disabling it for that
 * dso.
 */
#define A2L_FAIL_LIMIT 123

char *__get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr, bool unwind_inlines,
		  u64 ip)
{
	char *file = NULL;
	unsigned line = 0;
	char *srcline;
	const char *dso_name;

	if (!dso__has_srcline(dso))
		goto out;

	dso_name = srcline_dso_name(dso);
	if (dso_name == NULL)
		goto out_err;

	if (!addr2line(dso_name, addr, &file, &line, dso,
		       unwind_inlines, /*node=*/NULL, sym))
		goto out_err;

	srcline = srcline_from_fileline(file, line);
	free(file);

	if (!srcline)
		goto out_err;

	dso__set_a2l_fails(dso, 0);

	return srcline;

out_err:
	dso__set_a2l_fails(dso, dso__a2l_fails(dso) + 1);
	if (dso__a2l_fails(dso) > A2L_FAIL_LIMIT) {
		dso__set_has_srcline(dso, false);
		dso__free_a2l(dso);
	}
out:
	if (!show_addr)
		return (show_sym && sym) ?
			    strndup(sym->name, sym->namelen) : SRCLINE_UNKNOWN;

	if (sym) {
		if (asprintf(&srcline, "%s+%" PRIu64, show_sym ? sym->name : "",
					ip - sym->start) < 0)
			return SRCLINE_UNKNOWN;
	} else if (asprintf(&srcline, "%s[%" PRIx64 "]", dso__short_name(dso), addr) < 0)
		return SRCLINE_UNKNOWN;
	return srcline;
}

/* Returns filename and fills in line number in line */
char *get_srcline_split(struct dso *dso, u64 addr, unsigned *line)
{
	char *file = NULL;
	const char *dso_name;

	if (!dso__has_srcline(dso))
		return NULL;

	dso_name = srcline_dso_name(dso);
	if (dso_name == NULL)
		goto out_err;

	if (!addr2line(dso_name, addr, &file, line, dso, /*unwind_inlines=*/true,
			/*node=*/NULL, /*sym=*/NULL))
		goto out_err;

	dso__set_a2l_fails(dso, 0);
	return file;

out_err:
	dso__set_a2l_fails(dso, dso__a2l_fails(dso) + 1);
	if (dso__a2l_fails(dso) > A2L_FAIL_LIMIT) {
		dso__set_has_srcline(dso, false);
		dso__free_a2l(dso);
	}

	return NULL;
}

void zfree_srcline(char **srcline)
{
	if (*srcline == NULL)
		return;

	if (*srcline != SRCLINE_UNKNOWN)
		free(*srcline);

	*srcline = NULL;
}

char *get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr, u64 ip)
{
	return __get_srcline(dso, addr, sym, show_sym, show_addr, false, ip);
}

struct srcline_node {
	u64			addr;
	char			*srcline;
	struct rb_node		rb_node;
};

void srcline__tree_insert(struct rb_root_cached *tree, u64 addr, char *srcline)
{
	struct rb_node **p = &tree->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct srcline_node *i, *node;
	bool leftmost = true;

	node = zalloc(sizeof(struct srcline_node));
	if (!node) {
		perror("not enough memory for the srcline node");
		return;
	}

	node->addr = addr;
	node->srcline = srcline;

	while (*p != NULL) {
		parent = *p;
		i = rb_entry(parent, struct srcline_node, rb_node);
		if (addr < i->addr)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}
	rb_link_node(&node->rb_node, parent, p);
	rb_insert_color_cached(&node->rb_node, tree, leftmost);
}

char *srcline__tree_find(struct rb_root_cached *tree, u64 addr)
{
	struct rb_node *n = tree->rb_root.rb_node;

	while (n) {
		struct srcline_node *i = rb_entry(n, struct srcline_node,
						  rb_node);

		if (addr < i->addr)
			n = n->rb_left;
		else if (addr > i->addr)
			n = n->rb_right;
		else
			return i->srcline;
	}

	return NULL;
}

void srcline__tree_delete(struct rb_root_cached *tree)
{
	struct srcline_node *pos;
	struct rb_node *next = rb_first_cached(tree);

	while (next) {
		pos = rb_entry(next, struct srcline_node, rb_node);
		next = rb_next(&pos->rb_node);
		rb_erase_cached(&pos->rb_node, tree);
		zfree_srcline(&pos->srcline);
		zfree(&pos);
	}
}

struct inline_node *dso__parse_addr_inlines(struct dso *dso, u64 addr,
					    struct symbol *sym)
{
	const char *dso_name;

	dso_name = srcline_dso_name(dso);
	if (dso_name == NULL)
		return NULL;

	return addr2inlines(dso_name, addr, dso, sym);
}

void inline_node__delete(struct inline_node *node)
{
	struct inline_list *ilist, *tmp;

	list_for_each_entry_safe(ilist, tmp, &node->val, list) {
		list_del_init(&ilist->list);
		zfree_srcline(&ilist->srcline);
		/* only the inlined symbols are owned by the list */
		if (ilist->symbol && ilist->symbol->inlined)
			symbol__delete(ilist->symbol);
		free(ilist);
	}

	free(node);
}

void inlines__tree_insert(struct rb_root_cached *tree,
			  struct inline_node *inlines)
{
	struct rb_node **p = &tree->rb_root.rb_node;
	struct rb_node *parent = NULL;
	const u64 addr = inlines->addr;
	struct inline_node *i;
	bool leftmost = true;

	while (*p != NULL) {
		parent = *p;
		i = rb_entry(parent, struct inline_node, rb_node);
		if (addr < i->addr)
			p = &(*p)->rb_left;
		else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}
	rb_link_node(&inlines->rb_node, parent, p);
	rb_insert_color_cached(&inlines->rb_node, tree, leftmost);
}

struct inline_node *inlines__tree_find(struct rb_root_cached *tree, u64 addr)
{
	struct rb_node *n = tree->rb_root.rb_node;

	while (n) {
		struct inline_node *i = rb_entry(n, struct inline_node,
						 rb_node);

		if (addr < i->addr)
			n = n->rb_left;
		else if (addr > i->addr)
			n = n->rb_right;
		else
			return i;
	}

	return NULL;
}

void inlines__tree_delete(struct rb_root_cached *tree)
{
	struct inline_node *pos;
	struct rb_node *next = rb_first_cached(tree);

	while (next) {
		pos = rb_entry(next, struct inline_node, rb_node);
		next = rb_next(&pos->rb_node);
		rb_erase_cached(&pos->rb_node, tree);
		inline_node__delete(pos);
	}
}
