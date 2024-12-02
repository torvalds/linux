// SPDX-License-Identifier: GPL-2.0-only
/*
 * builtin-kallsyms.c
 *
 * Builtin command: Look for a symbol in the running kernel and its modules
 *
 * Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include <inttypes.h>
#include "builtin.h"
#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include "debug.h"
#include "dso.h"
#include "machine.h"
#include "map.h"
#include "symbol.h"

static int __cmd_kallsyms(int argc, const char **argv)
{
	int i;
	struct machine *machine = machine__new_kallsyms();

	if (machine == NULL) {
		pr_err("Couldn't read /proc/kallsyms\n");
		return -1;
	}

	for (i = 0; i < argc; ++i) {
		struct map *map;
		struct symbol *symbol = machine__find_kernel_symbol_by_name(machine, argv[i], &map);

		if (symbol == NULL) {
			printf("%s: not found\n", argv[i]);
			continue;
		}

		printf("%s: %s %s %#" PRIx64 "-%#" PRIx64 " (%#" PRIx64 "-%#" PRIx64")\n",
			symbol->name, map->dso->short_name, map->dso->long_name,
			map->unmap_ip(map, symbol->start), map->unmap_ip(map, symbol->end),
			symbol->start, symbol->end);
	}

	machine__delete(machine);
	return 0;
}

int cmd_kallsyms(int argc, const char **argv)
{
	const struct option options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose (show counter open errors, etc)"),
	OPT_END()
	};
	const char * const kallsyms_usage[] = {
		"perf kallsyms [<options>] symbol_name",
		NULL
	};

	argc = parse_options(argc, argv, options, kallsyms_usage, 0);
	if (argc < 1)
		usage_with_options(kallsyms_usage, options);

	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);
	if (symbol__init(NULL) < 0)
		return -1;

	return __cmd_kallsyms(argc, argv);
}
