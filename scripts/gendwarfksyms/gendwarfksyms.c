// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "gendwarfksyms.h"

/*
 * Options
 */

/* Print debugging information to stderr */
int debug;
/* Dump DIE contents */
int dump_dies;
/* Print debugging information about die_map changes */
int dump_die_map;
/* Print out type strings (i.e. type_map) */
int dump_types;
/* Print out expanded type strings used for symbol versions */
int dump_versions;
/* Support kABI stability features */
int stable;
/* Write a symtypes file */
int symtypes;
static const char *symtypes_file;

static void usage(void)
{
	fputs("Usage: gendwarfksyms [options] elf-object-file ... < symbol-list\n\n"
	      "Options:\n"
	      "  -d, --debug          Print debugging information\n"
	      "      --dump-dies      Dump DWARF DIE contents\n"
	      "      --dump-die-map   Print debugging information about die_map changes\n"
	      "      --dump-types     Dump type strings\n"
	      "      --dump-versions  Dump expanded type strings used for symbol versions\n"
	      "  -s, --stable         Support kABI stability features\n"
	      "  -T, --symtypes file  Write a symtypes file\n"
	      "  -h, --help           Print this message\n"
	      "\n",
	      stderr);
}

static int process_module(Dwfl_Module *mod, void **userdata, const char *name,
			  Dwarf_Addr base, void *arg)
{
	Dwarf_Addr dwbias;
	Dwarf_Die cudie;
	Dwarf_CU *cu = NULL;
	Dwarf *dbg;
	FILE *symfile = arg;
	int res;

	debug("%s", name);
	dbg = dwfl_module_getdwarf(mod, &dwbias);

	/*
	 * Look for exported symbols in each CU, follow the DIE tree, and add
	 * the entries to die_map.
	 */
	do {
		res = dwarf_get_units(dbg, cu, &cu, NULL, NULL, &cudie, NULL);
		if (res < 0)
			error("dwarf_get_units failed: no debugging information?");
		if (res == 1)
			break; /* No more units */

		process_cu(&cudie);
	} while (cu);

	/*
	 * Use die_map to expand type strings, write them to `symfile`, and
	 * calculate symbol versions.
	 */
	generate_symtypes_and_versions(symfile);
	die_map_free();

	return DWARF_CB_OK;
}

static const Dwfl_Callbacks callbacks = {
	.section_address = dwfl_offline_section_address,
	.find_debuginfo = dwfl_standard_find_debuginfo,
};

int main(int argc, char **argv)
{
	FILE *symfile = NULL;
	unsigned int n;
	int opt;

	static const struct option opts[] = {
		{ "debug", 0, NULL, 'd' },
		{ "dump-dies", 0, &dump_dies, 1 },
		{ "dump-die-map", 0, &dump_die_map, 1 },
		{ "dump-types", 0, &dump_types, 1 },
		{ "dump-versions", 0, &dump_versions, 1 },
		{ "stable", 0, NULL, 's' },
		{ "symtypes", 1, NULL, 'T' },
		{ "help", 0, NULL, 'h' },
		{ 0, 0, NULL, 0 }
	};

	while ((opt = getopt_long(argc, argv, "dsT:h", opts, NULL)) != EOF) {
		switch (opt) {
		case 0:
			break;
		case 'd':
			debug = 1;
			break;
		case 's':
			stable = 1;
			break;
		case 'T':
			symtypes = 1;
			symtypes_file = optarg;
			break;
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return 1;
		}
	}

	if (dump_die_map)
		dump_dies = 1;

	if (optind >= argc) {
		usage();
		error("no input files?");
	}

	if (!symbol_read_exports(stdin))
		return 0;

	if (symtypes_file) {
		symfile = fopen(symtypes_file, "w");
		if (!symfile)
			error("fopen failed for '%s': %s", symtypes_file,
			      strerror(errno));
	}

	for (n = optind; n < argc; n++) {
		Dwfl *dwfl;
		int fd;

		fd = open(argv[n], O_RDONLY);
		if (fd == -1)
			error("open failed for '%s': %s", argv[n],
			      strerror(errno));

		symbol_read_symtab(fd);
		kabi_read_rules(fd);

		dwfl = dwfl_begin(&callbacks);
		if (!dwfl)
			error("dwfl_begin failed for '%s': %s", argv[n],
			      dwarf_errmsg(-1));

		if (!dwfl_report_offline(dwfl, argv[n], argv[n], fd))
			error("dwfl_report_offline failed for '%s': %s",
			      argv[n], dwarf_errmsg(-1));

		dwfl_report_end(dwfl, NULL, NULL);

		if (dwfl_getmodules(dwfl, &process_module, symfile, 0))
			error("dwfl_getmodules failed for '%s'", argv[n]);

		dwfl_end(dwfl);
		kabi_free();
	}

	if (symfile)
		check(fclose(symfile));

	symbol_print_versions();
	symbol_free();

	return 0;
}
