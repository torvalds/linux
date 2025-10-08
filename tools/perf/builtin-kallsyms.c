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
#include "env.h"
#include "machine.h"
#include "map.h"
#include "symbol.h"

static int __cmd_kallsyms(int argc, const char **argv)
{
	int i, err;
	struct perf_env host_env;
	struct machine *machine = NULL;


	perf_env__init(&host_env);
	err = perf_env__set_cmdline(&host_env, argc, argv);
	if (err)
		goto out;

	machine = machine__new_kallsyms(&host_env);
	if (machine == NULL) {
		pr_err("Couldn't read /proc/kallsyms\n");
		err = -1;
		goto out;
	}

	for (i = 0; i < argc; ++i) {
		struct map *map;
		const struct dso *dso;
		struct symbol *symbol = machine__find_kernel_symbol_by_name(machine, argv[i], &map);

		if (symbol == NULL) {
			printf("%s: not found\n", argv[i]);
			continue;
		}

		dso = map__dso(map);
		printf("%s: %s %s %#" PRIx64 "-%#" PRIx64 " (%#" PRIx64 "-%#" PRIx64")\n",
			symbol->name, dso__short_name(dso), dso__long_name(dso),
			map__unmap_ip(map, symbol->start), map__unmap_ip(map, symbol->end),
			symbol->start, symbol->end);
	}
out:
	machine__delete(machine);
	perf_env__exit(&host_env);
	return err;
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

	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);
	if (symbol__init(NULL) < 0)
		return -1;

	return __cmd_kallsyms(argc, argv);
}
