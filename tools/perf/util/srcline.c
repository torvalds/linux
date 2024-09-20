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

#include <api/io.h>

#include "util/dso.h"
#include "util/debug.h"
#include "util/callchain.h"
#include "util/symbol_conf.h"
#include "srcline.h"
#include "string2.h"
#include "symbol.h"
#include "subcmd/run-command.h"

/* If addr2line doesn't return data for 1 second then timeout. */
int addr2line_timeout_ms = 1 * 1000;
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
	struct a2l_data *a2l = dso__a2l(dso);
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
	struct a2l_data *a2l = dso__a2l(dso);

	if (!a2l) {
		a2l = addr2line_init(dso_name);
		dso__set_a2l(dso, a2l);
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
	struct a2l_data *a2l = dso__a2l(dso);

	if (!a2l)
		return;

	addr2line_cleanup(a2l);

	dso__set_a2l(dso, NULL);
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
	pr_debug("addr2line missing ':' in filename split\n");
	return 0;
}

static void addr2line_subprocess_cleanup(struct child_process *a2l)
{
	if (a2l->pid != -1) {
		kill(a2l->pid, SIGKILL);
		finish_command(a2l); /* ignore result, we don't care */
		a2l->pid = -1;
		close(a2l->in);
		close(a2l->out);
	}

	free(a2l);
}

static struct child_process *addr2line_subprocess_init(const char *addr2line_path,
							const char *binary_path)
{
	const char *argv[] = {
		addr2line_path ?: "addr2line",
		"-e", binary_path,
		"-a", "-i", "-f", NULL
	};
	struct child_process *a2l = zalloc(sizeof(*a2l));
	int start_command_status = 0;

	if (a2l == NULL) {
		pr_err("Failed to allocate memory for addr2line");
		return NULL;
	}

	a2l->pid = -1;
	a2l->in = -1;
	a2l->out = -1;
	a2l->no_stderr = 1;

	a2l->argv = argv;
	start_command_status = start_command(a2l);
	a2l->argv = NULL; /* it's not used after start_command; avoid dangling pointers */

	if (start_command_status != 0) {
		pr_warning("could not start addr2line (%s) for %s: start_command return code %d\n",
			addr2line_path, binary_path, start_command_status);
		addr2line_subprocess_cleanup(a2l);
		return NULL;
	}

	return a2l;
}

enum a2l_style {
	BROKEN,
	GNU_BINUTILS,
	LLVM,
};

static enum a2l_style addr2line_configure(struct child_process *a2l, const char *dso_name)
{
	static bool cached;
	static enum a2l_style style;

	if (!cached) {
		char buf[128];
		struct io io;
		int ch;
		int lines;

		if (write(a2l->in, ",\n", 2) != 2)
			return BROKEN;

		io__init(&io, a2l->out, buf, sizeof(buf));
		ch = io__get_char(&io);
		if (ch == ',') {
			style = LLVM;
			cached = true;
			lines = 1;
			pr_debug("Detected LLVM addr2line style\n");
		} else if (ch == '0') {
			style = GNU_BINUTILS;
			cached = true;
			lines = 3;
			pr_debug("Detected binutils addr2line style\n");
		} else {
			if (!symbol_conf.disable_add2line_warn) {
				char *output = NULL;
				size_t output_len;

				io__getline(&io, &output, &output_len);
				pr_warning("%s %s: addr2line configuration failed\n",
					   __func__, dso_name);
				pr_warning("\t%c%s", ch, output);
			}
			pr_debug("Unknown/broken addr2line style\n");
			return BROKEN;
		}
		while (lines) {
			ch = io__get_char(&io);
			if (ch <= 0)
				break;
			if (ch == '\n')
				lines--;
		}
		/* Ignore SIGPIPE in the event addr2line exits. */
		signal(SIGPIPE, SIG_IGN);
	}
	return style;
}

static int read_addr2line_record(struct io *io,
				 enum a2l_style style,
				 const char *dso_name,
				 u64 addr,
				 bool first,
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

	/*
	 * Read the first line. Without an error this will be:
	 * - for the first line an address like 0x1234,
	 * - the binutils sentinel 0x0000000000000000,
	 * - the llvm-addr2line the sentinel ',' character,
	 * - the function name line for an inlined function.
	 */
	if (io__getline(io, &line, &line_len) < 0 || !line_len)
		goto error;

	pr_debug("%s %s: addr2line read address for sentinel: %s", __func__, dso_name, line);
	if (style == LLVM && line_len == 2 && line[0] == ',') {
		/* Found the llvm-addr2line sentinel character. */
		zfree(&line);
		return 0;
	} else if (style == GNU_BINUTILS && (!first || addr != 0)) {
		int zero_count = 0, non_zero_count = 0;
		/*
		 * Check for binutils sentinel ignoring it for the case the
		 * requested address is 0.
		 */

		/* A given address should always start 0x. */
		if (line_len >= 2 || line[0] != '0' || line[1] != 'x') {
			for (size_t i = 2; i < line_len; i++) {
				if (line[i] == '0')
					zero_count++;
				else if (line[i] != '\n')
					non_zero_count++;
			}
			if (!non_zero_count) {
				int ch;

				if (first && !zero_count) {
					/* Line was erroneous just '0x'. */
					goto error;
				}
				/*
				 * Line was 0x0..0, the sentinel for binutils. Remove
				 * the function and filename lines.
				 */
				zfree(&line);
				do {
					ch = io__get_char(io);
				} while (ch > 0 && ch != '\n');
				do {
					ch = io__get_char(io);
				} while (ch > 0 && ch != '\n');
				return 0;
			}
		}
	}
	/* Read the second function name line (if inline data then this is the first line). */
	if (first && (io__getline(io, &line, &line_len) < 0 || !line_len))
		goto error;

	pr_debug("%s %s: addr2line read line: %s", __func__, dso_name, line);
	if (function != NULL)
		*function = strdup(strim(line));

	zfree(&line);
	line_len = 0;

	/* Read the third filename and line number line. */
	if (io__getline(io, &line, &line_len) < 0 || !line_len)
		goto error;

	pr_debug("%s %s: addr2line filename:number : %s", __func__, dso_name, line);
	if (filename_split(line, line_nr == NULL ? &dummy_line_nr : line_nr) == 0 &&
	    style == GNU_BINUTILS) {
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
	struct child_process *a2l = dso__a2l(dso);
	char *record_function = NULL;
	char *record_filename = NULL;
	unsigned int record_line_nr = 0;
	int record_status = -1;
	int ret = 0;
	size_t inline_count = 0;
	int len;
	char buf[128];
	ssize_t written;
	struct io io = { .eof = false };
	enum a2l_style a2l_style;

	if (!a2l) {
		if (!filename__has_section(dso_name, ".debug_line"))
			goto out;

		dso__set_a2l(dso,
			     addr2line_subprocess_init(symbol_conf.addr2line_path, dso_name));
		a2l = dso__a2l(dso);
	}

	if (a2l == NULL) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: addr2line_subprocess_init failed\n", __func__, dso_name);
		goto out;
	}
	a2l_style = addr2line_configure(a2l, dso_name);
	if (a2l_style == BROKEN)
		goto out;

	/*
	 * Send our request and then *deliberately* send something that can't be
	 * interpreted as a valid address to ask addr2line about (namely,
	 * ","). This causes addr2line to first write out the answer to our
	 * request, in an unbounded/unknown number of records, and then to write
	 * out the lines "0x0...0", "??" and "??:0", for GNU binutils, or ","
	 * for llvm-addr2line, so that we can detect when it has finished giving
	 * us anything useful.
	 */
	len = snprintf(buf, sizeof(buf), "%016"PRIx64"\n,\n", addr);
	written = len > 0 ? write(a2l->in, buf, len) : -1;
	if (written != len) {
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: could not send request\n", __func__, dso_name);
		goto out;
	}
	io__init(&io, a2l->out, buf, sizeof(buf));
	io.timeout_ms = addr2line_timeout_ms;
	switch (read_addr2line_record(&io, a2l_style, dso_name, addr, /*first=*/true,
				      &record_function, &record_filename, &record_line_nr)) {
	case -1:
		if (!symbol_conf.disable_add2line_warn)
			pr_warning("%s %s: could not read first record\n", __func__, dso_name);
		goto out;
	case 0:
		/*
		 * The first record was invalid, so return failure, but first
		 * read another record, since we sent a sentinel ',' for the
		 * sake of detected the last inlined function. Treat this as the
		 * first of a record as the ',' generates a new start with GNU
		 * binutils, also force a non-zero address as we're no longer
		 * reading that record.
		 */
		switch (read_addr2line_record(&io, a2l_style, dso_name,
					      /*addr=*/1, /*first=*/true,
					      NULL, NULL, NULL)) {
		case -1:
			if (!symbol_conf.disable_add2line_warn)
				pr_warning("%s %s: could not read sentinel record\n",
					   __func__, dso_name);
			break;
		case 0:
			/* The sentinel as expected. */
			break;
		default:
			if (!symbol_conf.disable_add2line_warn)
				pr_warning("%s %s: unexpected record instead of sentinel",
					   __func__, dso_name);
			break;
		}
		goto out;
	default:
		/* First record as expected. */
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

	/*
	 * We have to read the records even if we don't care about the inline
	 * info. This isn't the first record and force the address to non-zero
	 * as we're reading records beyond the first.
	 */
	while ((record_status = read_addr2line_record(&io,
						      a2l_style,
						      dso_name,
						      /*addr=*/1,
						      /*first=*/false,
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
	if (io.eof) {
		dso__set_a2l(dso, NULL);
		addr2line_subprocess_cleanup(a2l);
	}
	return ret;
}

void dso__free_a2l(struct dso *dso)
{
	struct child_process *a2l = dso__a2l(dso);

	if (!a2l)
		return;

	addr2line_subprocess_cleanup(a2l);

	dso__set_a2l(dso, NULL);
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

	if (!dso__has_srcline(dso))
		goto out;

	dso_name = srcline_dso_name(dso);
	if (dso_name == NULL)
		goto out_err;

	if (!addr2line(dso_name, addr, &file, &line, dso,
		       unwind_inlines, NULL, sym))
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

	if (!addr2line(dso_name, addr, &file, line, dso, true, NULL, NULL))
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
