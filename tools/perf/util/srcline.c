#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kernel.h>

#include "util/dso.h"
#include "util/util.h"
#include "util/debug.h"
#include "util/callchain.h"
#include "srcline.h"

#include "symbol.h"

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

static int inline_list__append(char *filename, char *funcname, int line_nr,
			       struct inline_node *node, struct dso *dso)
{
	struct inline_list *ilist;
	char *demangled;

	ilist = zalloc(sizeof(*ilist));
	if (ilist == NULL)
		return -1;

	ilist->filename = filename;
	ilist->line_nr = line_nr;

	if (dso != NULL) {
		demangled = dso__demangle_sym(dso, 0, funcname);
		if (demangled == NULL) {
			ilist->funcname = funcname;
		} else {
			ilist->funcname = demangled;
			free(funcname);
		}
	}

	if (callchain_param.order == ORDER_CALLEE)
		list_add_tail(&ilist->list, &node->val);
	else
		list_add(&ilist->list, &node->val);

	return 0;
}

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

	if (a2l->found)
		return;

	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
		return;

	pc = a2l->addr;
	vma = bfd_get_section_vma(abfd, section);
	size = bfd_get_section_size(section);

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

#define MAX_INLINE_NEST 1024

static int inline_list__append_dso_a2l(struct dso *dso,
				       struct inline_node *node)
{
	struct a2l_data *a2l = dso->a2l;
	char *funcname = a2l->funcname ? strdup(a2l->funcname) : NULL;
	char *filename = a2l->filename ? strdup(a2l->filename) : NULL;

	return inline_list__append(filename, funcname, a2l->line, node, dso);
}

static int addr2line(const char *dso_name, u64 addr,
		     char **file, unsigned int *line, struct dso *dso,
		     bool unwind_inlines, struct inline_node *node)
{
	int ret = 0;
	struct a2l_data *a2l = dso->a2l;

	if (!a2l) {
		dso->a2l = addr2line_init(dso_name);
		a2l = dso->a2l;
	}

	if (a2l == NULL) {
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

		if (node && inline_list__append_dso_a2l(dso, node))
			return 0;

		while (bfd_find_inliner_info(a2l->abfd, &a2l->filename,
					     &a2l->funcname, &a2l->line) &&
		       cnt++ < MAX_INLINE_NEST) {

			if (a2l->filename && !strlen(a2l->filename))
				a2l->filename = NULL;

			if (node != NULL) {
				if (inline_list__append_dso_a2l(dso, node))
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

static struct inline_node *addr2inlines(const char *dso_name, u64 addr,
	struct dso *dso)
{
	struct inline_node *node;

	node = zalloc(sizeof(*node));
	if (node == NULL) {
		perror("not enough memory for the inline node");
		return NULL;
	}

	INIT_LIST_HEAD(&node->val);
	node->addr = addr;

	if (!addr2line(dso_name, addr, NULL, NULL, dso, TRUE, node))
		goto out_free_inline_node;

	if (list_empty(&node->val))
		goto out_free_inline_node;

	return node;

out_free_inline_node:
	inline_node__delete(node);
	return NULL;
}

#else /* HAVE_LIBBFD_SUPPORT */

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

static int addr2line(const char *dso_name, u64 addr,
		     char **file, unsigned int *line_nr,
		     struct dso *dso __maybe_unused,
		     bool unwind_inlines __maybe_unused,
		     struct inline_node *node __maybe_unused)
{
	FILE *fp;
	char cmd[PATH_MAX];
	char *filename = NULL;
	size_t len;
	int ret = 0;

	scnprintf(cmd, sizeof(cmd), "addr2line -e %s %016"PRIx64,
		  dso_name, addr);

	fp = popen(cmd, "r");
	if (fp == NULL) {
		pr_warning("popen failed for %s\n", dso_name);
		return 0;
	}

	if (getline(&filename, &len, fp) < 0 || !len) {
		pr_warning("addr2line has no output for %s\n", dso_name);
		goto out;
	}

	ret = filename_split(filename, line_nr);
	if (ret != 1) {
		free(filename);
		goto out;
	}

	*file = filename;

out:
	pclose(fp);
	return ret;
}

void dso__free_a2l(struct dso *dso __maybe_unused)
{
}

static struct inline_node *addr2inlines(const char *dso_name, u64 addr,
	struct dso *dso __maybe_unused)
{
	FILE *fp;
	char cmd[PATH_MAX];
	struct inline_node *node;
	char *filename = NULL;
	size_t len;
	unsigned int line_nr = 0;

	scnprintf(cmd, sizeof(cmd), "addr2line -e %s -i %016"PRIx64,
		  dso_name, addr);

	fp = popen(cmd, "r");
	if (fp == NULL) {
		pr_err("popen failed for %s\n", dso_name);
		return NULL;
	}

	node = zalloc(sizeof(*node));
	if (node == NULL) {
		perror("not enough memory for the inline node");
		goto out;
	}

	INIT_LIST_HEAD(&node->val);
	node->addr = addr;

	while (getline(&filename, &len, fp) != -1) {
		if (filename_split(filename, &line_nr) != 1) {
			free(filename);
			goto out;
		}

		if (inline_list__append(filename, NULL, line_nr, node,
					NULL) != 0)
			goto out;

		filename = NULL;
	}

out:
	pclose(fp);

	if (list_empty(&node->val)) {
		inline_node__delete(node);
		return NULL;
	}

	return node;
}

#endif /* HAVE_LIBBFD_SUPPORT */

/*
 * Number of addr2line failures (without success) before disabling it for that
 * dso.
 */
#define A2L_FAIL_LIMIT 123

char *__get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr, bool unwind_inlines)
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

	if (!addr2line(dso_name, addr, &file, &line, dso, unwind_inlines, NULL))
		goto out;

	if (asprintf(&srcline, "%s:%u",
				srcline_full_filename ? file : basename(file),
				line) < 0) {
		free(file);
		goto out;
	}

	dso->a2l_fails = 0;

	free(file);
	return srcline;

out:
	if (dso->a2l_fails && ++dso->a2l_fails > A2L_FAIL_LIMIT) {
		dso->has_srcline = 0;
		dso__free_a2l(dso);
	}

	if (!show_addr)
		return (show_sym && sym) ?
			    strndup(sym->name, sym->namelen) : NULL;

	if (sym) {
		if (asprintf(&srcline, "%s+%" PRIu64, show_sym ? sym->name : "",
					addr - sym->start) < 0)
			return SRCLINE_UNKNOWN;
	} else if (asprintf(&srcline, "%s[%" PRIx64 "]", dso->short_name, addr) < 0)
		return SRCLINE_UNKNOWN;
	return srcline;
}

void free_srcline(char *srcline)
{
	if (srcline && strcmp(srcline, SRCLINE_UNKNOWN) != 0)
		free(srcline);
}

char *get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool show_addr)
{
	return __get_srcline(dso, addr, sym, show_sym, show_addr, false);
}

struct inline_node *dso__parse_addr_inlines(struct dso *dso, u64 addr)
{
	const char *dso_name;

	dso_name = dso__name(dso);
	if (dso_name == NULL)
		return NULL;

	return addr2inlines(dso_name, addr, dso);
}

void inline_node__delete(struct inline_node *node)
{
	struct inline_list *ilist, *tmp;

	list_for_each_entry_safe(ilist, tmp, &node->val, list) {
		list_del_init(&ilist->list);
		zfree(&ilist->filename);
		zfree(&ilist->funcname);
		free(ilist);
	}

	free(node);
}
