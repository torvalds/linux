// SPDX-License-Identifier: GPL-2.0
#include "dso.h"
#include "libdw.h"
#include "srcline.h"
#include "symbol.h"
#include "dwarf-aux.h"
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

	dwfl_report_end(dwfl, /*removed=*/NULL, /*arg=*/NULL);
	dso__set_libdw(dso, dwfl);

	return dwfl;
}

struct libdw_a2l_cb_args {
	struct dso *dso;
	struct symbol *sym;
	struct inline_node *node;
	char *leaf_srcline;
	bool leaf_srcline_used;
};

static int libdw_a2l_cb(Dwarf_Die *die, void *_args)
{
	struct libdw_a2l_cb_args *args  = _args;
	struct symbol *inline_sym = new_inline_sym(args->dso, args->sym, dwarf_diename(die));
	const char *call_fname = die_get_call_file(die);
	char *call_srcline = srcline__unknown;
	struct inline_list *ilist;

	if (!inline_sym)
		return -ENOMEM;

	/* Assign caller information to the parent. */
	if (call_fname)
		call_srcline = srcline_from_fileline(call_fname, die_get_call_lineno(die));

	list_for_each_entry(ilist, &args->node->val, list) {
		if (args->leaf_srcline == ilist->srcline)
			args->leaf_srcline_used = false;
		else if (ilist->srcline != srcline__unknown)
			free(ilist->srcline);
		ilist->srcline =  call_srcline;
		call_srcline = NULL;
		break;
	}
	if (call_srcline && call_srcline != srcline__unknown)
		free(call_srcline);

	/* Add this symbol to the chain as the leaf. */
	if (!args->leaf_srcline_used) {
		inline_list__append_tail(inline_sym, args->leaf_srcline, args->node);
		args->leaf_srcline_used = true;
	} else {
		inline_list__append_tail(inline_sym, strdup(args->leaf_srcline), args->node);
	}
	return 0;
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

		/* Walk from the parent down to the leaf. */
		cu_walk_functions_at(cudie, addr, libdw_a2l_cb, &args);

		if (!args.leaf_srcline_used)
			free(args.leaf_srcline);
	}
	return 1;
}
