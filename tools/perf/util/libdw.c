// SPDX-License-Identifier: GPL-2.0
#include "dso.h"
#include "libdw.h"
#include "srcline.h"
#include "symbol.h"
#include "dwarf-aux.h"
#include "callchain.h"
#include <fcntl.h>
#include <unistd.h>
#include <elfutils/libdwfl.h>

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.section_address = dwfl_offline_section_address,
	.find_elf = dwfl_build_id_find_elf,
};

void dso__free_libdw(struct dso *dso)
{
	Dwfl *dwfl = dso__libdw(dso);

	if (dwfl) {
		dwfl_end(dwfl);
		dso__set_libdw(dso, NULL);
	}
}

struct Dwfl *dso__libdw_dwfl(struct dso *dso)
{
	Dwfl *dwfl = dso__libdw(dso);
	const char *dso_name;
	Dwfl_Module *mod;
	int fd;

	if (dwfl)
		return dwfl;

	dso_name = dso__long_name(dso);
	/*
	 * Initialize Dwfl session.
	 * We need to open the DSO file to report it to libdw.
	 */
	fd = open(dso_name, O_RDONLY);
	if (fd < 0)
		return NULL;

	dwfl = dwfl_begin(&offline_callbacks);
	if (!dwfl) {
		close(fd);
		return NULL;
	}

	/*
	 * If the report is successful, the file descriptor fd is consumed
	 * and closed by the Dwfl. If not, it is not closed.
	 */
	mod = dwfl_report_offline(dwfl, dso_name, dso_name, fd);
	if (!mod) {
		dwfl_end(dwfl);
		close(fd);
		return NULL;
	}

	if (dwfl_report_end(dwfl, /*removed=*/NULL, /*arg=*/NULL) != 0) {
		dwfl_end(dwfl);
		return NULL;
	}
	dso__set_libdw(dso, dwfl);

	return dwfl;
}

struct libdw_a2l_cb_args {
	struct dso *dso;
	struct symbol *sym;
	struct inline_node *node;
	char *leaf_srcline;
	bool leaf_srcline_used;
	int err;
};

static int libdw_a2l_cb(Dwarf_Die *die, void *_args)
{
	struct libdw_a2l_cb_args *args  = _args;
	struct symbol *inline_sym = new_inline_sym(args->dso, args->sym, die_name(die));
	const char *call_fname = die_get_call_file(die);
	int call_lineno = die_get_call_lineno(die);
	char *call_srcline = srcline__unknown;

	if (!inline_sym)
		goto abort_enomem;

	/* Assign caller information to the parent. */
	if (call_fname)
		call_srcline = srcline_from_fileline(call_fname, call_lineno >= 0 ? call_lineno : 0);

	if (!list_empty(&args->node->val)) {
		struct inline_list *parent;

		if (callchain_param.order == ORDER_CALLEE)
			parent = list_first_entry(&args->node->val, struct inline_list, list);
		else
			parent = list_last_entry(&args->node->val, struct inline_list, list);

		if (args->leaf_srcline == parent->srcline)
			args->leaf_srcline_used = false;
		else if (parent->srcline != srcline__unknown)
			free(parent->srcline);
		parent->srcline = call_srcline;
		call_srcline = NULL;
	}
	if (call_srcline && call_srcline != srcline__unknown)
		free(call_srcline);

	/* Add this symbol to the chain as the leaf. */
	if (!args->leaf_srcline_used) {
		if (inline_list__append_tail(inline_sym, args->leaf_srcline, args->node) != 0)
			goto abort_delete_sym;
		args->leaf_srcline_used = true;
	} else {
		char *srcline = strdup(args->leaf_srcline);

		if (!srcline)
			goto abort_delete_sym;
		if (inline_list__append_tail(inline_sym, srcline, args->node) != 0) {
			free(srcline);
			goto abort_delete_sym;
		}
	}
	return 0;

abort_delete_sym:
	if (symbol__inlined(inline_sym))
		symbol__delete(inline_sym);
abort_enomem:
	args->err = -ENOMEM;
	return DWARF_CB_ABORT;
}

int libdw__addr2line(u64 addr, char **file, unsigned int *line_nr,
		     struct dso *dso, bool unwind_inlines,
		     struct inline_node *node, struct symbol *sym)
{
	Dwfl *dwfl = dso__libdw_dwfl(dso);
	Dwfl_Module *mod;
	Dwfl_Line *dwline;
	Dwarf_Addr bias;
	const char *src;
	int lineno = 0;

	if (!dwfl)
		return 0;

	mod = dwfl_addrmodule(dwfl, addr);
	if (!mod)
		return 0;

	/*
	 * Get/ignore the dwarf information. Determine the bias, difference
	 * between the regular ELF addr2line addresses and those to use with
	 * libdw.
	 */
	if (!dwfl_module_getdwarf(mod, &bias))
		return 0;

	/* Find source line information for the address. */
	dwline = dwfl_module_getsrc(mod, addr + bias);
	if (!dwline)
		return 0;

	/* Get line information. */
	src = dwfl_lineinfo(dwline, /*addr=*/NULL, &lineno, /*col=*/NULL, /*mtime=*/NULL,
			    /*length=*/NULL);

	if (file)
		*file = src ? strdup(src) : NULL;
	if (line_nr)
		*line_nr = lineno;

	/* Optionally unwind inline function call chain. */
	if (unwind_inlines && node) {
		Dwarf_Addr unused_bias;
		Dwarf_Die *cudie = dwfl_module_addrdie(mod, addr + bias, &unused_bias);
		struct libdw_a2l_cb_args args = {
			.dso = dso,
			.sym = sym,
			.node = node,
			.leaf_srcline = srcline_from_fileline(src ?: "<unknown>", lineno),
		};

		if (!args.leaf_srcline) {
			if (file && *file) {
				free(*file);
				*file = NULL;
			}
			return 0;
		}

		/* Walk from the parent down to the leaf. */
		if (cudie)
			cu_walk_functions_at(cudie, addr, libdw_a2l_cb, &args);

		if (!args.leaf_srcline_used)
			free(args.leaf_srcline);

		if (args.err) {
			if (file && *file) {
				free(*file);
				*file = NULL;
			}
			inline_node__clear_frames(node);
			return 0;
		}
	}
	return 1;
}
