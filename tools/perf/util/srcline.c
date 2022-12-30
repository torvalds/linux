// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>

#include "util/dso.h"
#include "util/debug.h"
#include "util/callchain.h"
#include "util/symbol_conf.h"
#include "srcline.h"
#include "string2.h"
#include "symbol.h"
#include "subcmd/run-command.h"

bool srcline_full_filename;

static const char *dso__name(struct dso *dso)
{
	const char *dso_name;

	if (dso->symsrc_filename)
		dso_name = dso->symsrc_filename;
	else
		dso_name = dso->long_name;

	if (dso_name[0] == '[')
		return NULL;

	if (!strncmp(dso_name, "/tmp/perf-", 10))
		return NULL;

	return dso_name;
}

static int inline_list__append(struct symbol *symbol, char *srcline,
			       struct inline_node *node)
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

static char *srcline_from_fileline(const char *file, unsigned int line)
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

static struct symbol *new_inline_sym(struct dso *dso,
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

#define MAX_INLINE_NEST 1024

#ifdef HAVE_LIBBFD_SUPPORT

/*
 * Implement addr2line using libbfd.
 */
#define PACKAGE "perf"
#include <bfd.h>

struct a2l_data {
	const char 	*input;
	u64	 	addr;

	bool 		found;
	const char 	*filename;
	const char 	*funcname;
	unsigned 	line;

	bfd 		*abfd;
	asymbol 	**syms;
};

static int bfd_error(const char *string)
{
	const char *errmsg;

	errmsg = bfd_errmsg(bfd_get_error());
	fflush(stdout);

	if (string)
		pr_debug("%s: %s\n", string, errmsg);
	else
		pr_debug("%s\n", errmsg);

	return -1;
}

static int slurp_symtab(bfd *abfd, struct a2l_data *a2l)
{
	long storage;
	long symcount;
	asymbol **syms;
	bfd_boolean dynamic = FALSE;

	if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
		return bfd_error(bfd_get_filename(abfd));

	storage = bfd_get_symtab_upper_bound(abfd);
	if (storage == 0L) {
		storage = bfd_get_dynamic_symtab_upper_bound(abfd);
		dynamic = TRUE;
	}
	if (storage < 0L)
		return bfd_error(bfd_get_filename(abfd));

	syms = malloc(storage);
	if (dynamic)
		symcount = bfd_canonicalize_dynamic_symtab(abfd, syms);
	else
		symcount = bfd_canonicalize_symtab(abfd, syms);

	if (symcount < 0) {
		free(syms);
		return bfd_error(bfd_get_filename(abfd));
	}

	a2l->syms = syms;
	return 0;
}

static void find_address_in_section(bfd *abfd, asection *section, void *data)
{
	bfd_vma pc, vma;
	bfd_size_type size;
	struct a2l_data *a2l = data;
	flagword flags;

	if (a2l->found)
		return;

#ifdef bfd_get_section_flags
	flags = bfd_get_section_flags(abfd, section);
#else
	flags = bfd_section_flags(section);
#endif
	if ((flags & SEC_ALLOC) == 0)
		return;

	pc = a2l->addr;
#ifdef bfd_get_section_vma
	vma = bfd_get_section_vma(abfd, section);
#else
	vma = bfd_section_vma(section);
#endif
#ifdef bfd_get_section_size
	size = bfd_get_section_size(section);
#else
	size = bfd_section_size(section);
#endif

	if (pc < vma || pc >= vma + size)
		return;

	a2l->found = bfd_find_nearest_line(abfd, section, a2l->syms, pc - vma,
					   &a2l->filename, &a2l->funcname,
					   &a2l->line);

	if (a2l->filename && !strlen(a2l->filename))
		a2l->filename = NULL;
}

static struct a2l_data *addr2line_init(const char *path)
{
	bfd *abfd;
	struct a2l_data *a2l = NULL;

	abfd = bfd_openr(path, NULL);
	if (abfd == NULL)
		return NULL;

	if (!bfd_check_format(abfd, bfd_object))
		goto out;

	a2l = zalloc(sizeof(*a2l));
	if (a2l == NULL)
		goto out;

	a2l->abfd = abfd;
	a2l->input = strdup(path);
	if (a2l->input == NULL)
		goto out;

	if (slurp_symtab(abfd, a2l))
		goto out;

	return a2l;

out:
	if (a2l) {
		zfree((char **)&a2l->input);
		free(a2l);
	}
	bfd_close(abfd);
	return NULL;
}

static void addr2line_cleanup(struct a2l_data *a2l)
{
	if (a2l->abfd)
		bfd_close(a2l->abfd);
	zfree((char **)&a2l->input);
	zfree(&a2l->syms);
	free(a2l);
}

static int inline_list__append_dso_a2l(struct dso *dso,
				       struct inline_node *node,
				       struct symbol *sym)
{
	struct a2l_data *a2l = dso->a2l;
	struct symbol *inline_sym = new_inline_sym(dso, sym, a2l->funcname);
	char *srcline = NULL;

	if (a2l->filename)
		srcline = srcline_from_fileline(a2l->filename, a2l->line);

	return inline_list__append(inline_sym, srcline, node);
}

static int addr2line(const char *dso_name, u64 addr,
		     char **file, unsigned int *line, struct dso *dso,
		     bool unwind_inlines, struct inline_node *node,
		     struct symbol *sym)
{
	int ret = 0;
	struct a2l_data *a2l = dso->a2l;

	if (!a2l) {
		dso->a2l = addr2line_init(dso_name);
		a2l = dso->a2l;
	}

	if (a2l == NULL) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("addr2line_init failed for %s\n", dso_name);
		return 0;
	}

	a2l->addr = addr;
	a2l->found = false;

	bfd_map_over_sections(a2l->abfd, find_address_in_section, a2l);

	if (!a2l->found)
		return 0;

	if (unwind_inlines) {
		int cnt = 0;

		if (node && inline_list__append_dso_a2l(dso, node, sym))
			return 0;

		while (bfd_find_inliner_info(a2l->abfd, &a2l->filename,
					     &a2l->funcname, &a2l->line) &&
		       cnt++ < MAX_INLINE_NEST) {

			if (a2l->filename && !strlen(a2l->filename))
				a2l->filename = NULL;

			if (node != NULL) {
				if (inline_list__append_dso_a2l(dso, node, sym))
					return 0;
				// found at least one inline frame
				ret = 1;
			}
		}
	}

	if (file) {
		*file = a2l->filename ? strdup(a2l->filename) : NULL;
		ret = *file ? 1 : 0;
	}

	if (line)
		*line = a2l->line;

	return ret;
}

void dso__free_a2l(struct dso *dso)
{
	struct a2l_data *a2l = dso->a2l;

	if (!a2l)
		return;

	addr2line_cleanup(a2l);

	dso->a2l = NULL;
}

#else /* HAVE_LIBBFD_SUPPORT */

struct a2l_subprocess {
	struct child_process addr2line;
	FILE *to_child;
	FILE *from_child;
};

static int filename_split(char *filename, unsigned int *line_nr)
{
	char *sep;

	sep = strchr(filename, '\n');
	if (sep)
		*sep = '\0';

	if (!strcmp(filename, "??:0"))
		return 0;

	sep = strchr(filename, ':');
	if (sep) {
		*sep++ = '\0';
		*line_nr = strtoul(sep, NULL, 0);
		return 1;
	}

	return 0;
}

static void addr2line_subprocess_cleanup(struct a2l_subprocess *a2l)
{
	if (a2l->addr2line.pid != -1) {
		kill(a2l->addr2line.pid, SIGKILL);
		finish_command(&a2l->addr2line); /* ignore result, we don't care */
		a2l->addr2line.pid = -1;
	}

	if (a2l->to_child != NULL) {
		fclose(a2l->to_child);
		a2l->to_child = NULL;
	}

	if (a2l->from_child != NULL) {
		fclose(a2l->from_child);
		a2l->from_child = NULL;
	}

	free(a2l);
}

static struct a2l_subprocess *addr2line_subprocess_init(const char *path)
{
	const char *argv[] = { "addr2line", "-e", path, "-i", "-f", NULL };
	struct a2l_subprocess *a2l = zalloc(sizeof(*a2l));
	int start_command_status = 0;

	if (a2l == NULL)
		goto out;

	a2l->to_child = NULL;
	a2l->from_child = NULL;

	a2l->addr2line.pid = -1;
	a2l->addr2line.in = -1;
	a2l->addr2line.out = -1;
	a2l->addr2line.no_stderr = 1;

	a2l->addr2line.argv = argv;
	start_command_status = start_command(&a2l->addr2line);
	a2l->addr2line.argv = NULL; /* it's not used after start_command; avoid dangling pointers */

	if (start_command_status != 0) {
		pr_warning("could not start addr2line for %s: start_command return code %d\n",
			   path,
			   start_command_status);
		goto out;
	}

	a2l->to_child = fdopen(a2l->addr2line.in, "w");
	if (a2l->to_child == NULL) {
		pr_warning("could not open write-stream to addr2line of %s\n", path);
		goto out;
	}

	a2l->from_child = fdopen(a2l->addr2line.out, "r");
	if (a2l->from_child == NULL) {
		pr_warning("could not open read-stream from addr2line of %s\n", path);
		goto out;
	}

	return a2l;

out:
	if (a2l)
		addr2line_subprocess_cleanup(a2l);

	return NULL;
}

static int read_addr2line_record(struct a2l_subprocess *a2l,
				 char **function,
				 char **filename,
				 unsigned int *line_nr)
{
	/*
	 * Returns:
	 * -1 ==> error
	 * 0 ==> sentinel (or other ill-formed) record read
	 * 1 ==> a genuine record read
	 */
	char *line = NULL;
	size_t line_len = 0;
	unsigned int dummy_line_nr = 0;
	int ret = -1;

	if (function != NULL)
		zfree(function);

	if (filename != NULL)
		zfree(filename);

	if (line_nr != NULL)
		*line_nr = 0;

	if (getline(&line, &line_len, a2l->from_child) < 0 || !line_len)
		goto error;

	if (function != NULL)
		*function = strdup(strim(line));

	zfree(&line);
	line_len = 0;

	if (getline(&line, &line_len, a2l->from_child) < 0 || !line_len)
		goto error;

	if (filename_split(line, line_nr == NULL ? &dummy_line_nr : line_nr) == 0) {
		ret = 0;
		goto error;
	}

	if (filename != NULL)
		*filename = strdup(line);

	zfree(&line);
	line_len = 0;

	return 1;

error:
	free(line);
	if (function != NULL)
		zfree(function);
	if (filename != NULL)
		zfree(filename);
	return ret;
}

static int inline_list__append_record(struct dso *dso,
				      struct inline_node *node,
				      struct symbol *sym,
				      const char *function,
				      const char *filename,
				      unsigned int line_nr)
{
	struct symbol *inline_sym = new_inline_sym(dso, sym, function);

	return inline_list__append(inline_sym, srcline_from_fileline(filename, line_nr), node);
}

static int addr2line(const char *dso_name, u64 addr,
		     char **file, unsigned int *line_nr,
		     struct dso *dso,
		     bool unwind_inlines,
		     struct inline_node *node,
		     struct symbol *sym __maybe_unused)
{
	struct a2l_subprocess *a2l = dso->a2l;
	char *record_function = NULL;
	char *record_filename = NULL;
	unsigned int record_line_nr = 0;
	int record_status = -1;
	int ret = 0;
	size_t inline_count = 0;

	if (!a2l) {
		if (!filename__has_section(dso_name, ".debug_line"))
			goto out;

		dso->a2l = addr2line_subprocess_init(dso_name);
		a2l = dso->a2l;
	}

	if (a2l == NULL) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: addr2line_subprocess_init failed\n", __func__, dso_name);
		goto out;
	}

	/*
	 * Send our request and then *deliberately* send something that can't be interpreted as
	 * a valid address to ask addr2line about (namely, ","). This causes addr2line to first
	 * write out the answer to our request, in an unbounded/unknown number of records, and
	 * then to write out the lines "??" and "??:0", so that we can detect when it has
	 * finished giving us anything useful. We have to be careful about the first record,
	 * though, because it may be genuinely unknown, in which case we'll get two sets of
	 * "??"/"??:0" lines.
	 */
	if (fprintf(a2l->to_child, "%016"PRIx64"\n,\n", addr) < 0 || fflush(a2l->to_child) != 0) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: could not send request\n", __func__, dso_name);
		goto out;
	}

	switch (read_addr2line_record(a2l, &record_function, &record_filename, &record_line_nr)) {
	case -1:
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: could not read first record\n", __func__, dso_name);
		goto out;
	case 0:
		/*
		 * The first record was invalid, so return failure, but first read another
		 * record, since we asked a junk question and have to clear the answer out.
		 */
		switch (read_addr2line_record(a2l, NULL, NULL, NULL)) {
		case -1:
			if (!symbol_conf.disable_add2line_warn)
				pr_warning("%s %s: could not read delimiter record\n",
					   __func__, dso_name);
			break;
		case 0:
			/* As expected. */
			break;
		default:
			if (!symbol_conf.disable_add2line_warn)
				pr_warning("%s %s: unexpected record instead of sentinel",
					   __func__, dso_name);
			break;
		}
		goto out;
	default:
		break;
	}

	if (file) {
		*file = strdup(record_filename);
		ret = 1;
	}
	if (line_nr)
		*line_nr = record_line_nr;

	if (unwind_inlines) {
		if (node && inline_list__append_record(dso, node, sym,
						       record_function,
						       record_filename,
						       record_line_nr)) {
			ret = 0;
			goto out;
		}
	}

	/* We have to read the records even if we don't care about the inline info. */
	while ((record_status = read_addr2line_record(a2l,
						      &record_function,
						      &record_filename,
						      &record_line_nr)) == 1) {
		if (unwind_inlines && node && inline_count++ < MAX_INLINE_NEST) {
			if (inline_list__append_record(dso, node, sym,
						       record_function,
						       record_filename,
						       record_line_nr)) {
				ret = 0;
				goto out;
			}
			ret = 1; /* found at least one inline frame */
		}
	}

out:
	free(record_function);
	free(record_filename);
	return ret;
}

void dso__free_a2l(struct dso *dso)
{
	struct a2l_subprocess *a2l = dso->a2l;

	if (!a2l)
		return;

	addr2line_subprocess_cleanup(a2l);

	dso->a2l = NULL;
}

#endif /* HAVE_LIBBFD_SUPPORT */

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

	addr2line(dso_name, addr, NULL, NULL, dso, true, node, sym);
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

	if (!dso->has_srcline)
		goto out;

	dso_name = dso__name(dso);
	if (dso_name == NULL)
		goto out;

	if (!addr2line(dso_name, addr, &file, &line, dso,
		       unwind_inlines, NULL, sym))
		goto out;

	srcline = srcline_from_fileline(file, line);
	free(file);

	if (!srcline)
		goto out;

	dso->a2l_fails = 0;

	return srcline;

out:
	if (dso->a2l_fails && ++dso->a2l_fails > A2L_FAIL_LIMIT) {
		dso->has_srcline = 0;
		dso__free_a2l(dso);
	}

	if (!show_addr)
		return (show_sym && sym) ?
			    strndup(sym->name, sym->namelen) : SRCLINE_UNKNOWN;

	if (sym) {
		if (asprintf(&srcline, "%s+%" PRIu64, show_sym ? sym->name : "",
					ip - sym->start) < 0)
			return SRCLINE_UNKNOWN;
	} else if (asprintf(&srcline, "%s[%" PRIx64 "]", dso->short_name, addr) < 0)
		return SRCLINE_UNKNOWN;
	return srcline;
}

/* Returns filename and fills in line number in line */
char *get_srcline_split(struct dso *dso, u64 addr, unsigned *line)
{
	char *file = NULL;
	const char *dso_name;

	if (!dso->has_srcline)
		goto out;

	dso_name = dso__name(dso);
	if (dso_name == NULL)
		goto out;

	if (!addr2line(dso_name, addr, &file, line, dso, true, NULL, NULL))
		goto out;

	dso->a2l_fails = 0;
	return file;

out:
	if (dso->a2l_fails && ++dso->a2l_fails > A2L_FAIL_LIMIT) {
		dso->has_srcline = 0;
		dso__free_a2l(dso);
	}

	return NULL;
}

void free_srcline(char *srcline)
{
	if (srcline && strcmp(srcline, SRCLINE_UNKNOWN) != 0)
		free(srcline);
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
		free_srcline(pos->srcline);
		zfree(&pos);
	}
}

struct inline_node *dso__parse_addr_inlines(struct dso *dso, u64 addr,
					    struct symbol *sym)
{
	const char *dso_name;

	dso_name = dso__name(dso);
	if (dso_name == NULL)
		return NULL;

	return addr2inlines(dso_name, addr, dso, sym);
}

void inline_node__delete(struct inline_node *node)
{
	struct inline_list *ilist, *tmp;

	list_for_each_entry_safe(ilist, tmp, &node->val, list) {
		list_del_init(&ilist->list);
		free_srcline(ilist->srcline);
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
