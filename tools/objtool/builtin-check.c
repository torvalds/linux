// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

/*
 * objtool check:
 *
 * This command analyzes every .o file and ensures the validity of its stack
 * trace metadata.  It enforces a set of rules on asm code and C inline
 * assembly code so that stack traces can be reliable.
 *
 * For more information, see tools/objtool/Documentation/stack-validation.txt.
 */

#include <subcmd/parse-options.h>
#include <string.h>
#include <stdlib.h>
#include <objtool/builtin.h>
#include <objtool/objtool.h>

bool no_fp, no_unreachable, retpoline, module, backtrace, uaccess, stats,
     lto, vmlinux, mcount, noinstr, backup, sls, dryrun,
     ibt;

static const char * const check_usage[] = {
	"objtool check [<options>] file.o",
	NULL,
};

static const char * const env_usage[] = {
	"OBJTOOL_ARGS=\"<options>\"",
	NULL,
};

const struct option check_options[] = {
	OPT_BOOLEAN('f', "no-fp", &no_fp, "Skip frame pointer validation"),
	OPT_BOOLEAN('u', "no-unreachable", &no_unreachable, "Skip 'unreachable instruction' warnings"),
	OPT_BOOLEAN('r', "retpoline", &retpoline, "Validate retpoline assumptions"),
	OPT_BOOLEAN('m', "module", &module, "Indicates the object will be part of a kernel module"),
	OPT_BOOLEAN('b', "backtrace", &backtrace, "unwind on error"),
	OPT_BOOLEAN('a', "uaccess", &uaccess, "enable uaccess checking"),
	OPT_BOOLEAN('s', "stats", &stats, "print statistics"),
	OPT_BOOLEAN(0, "lto", &lto, "whole-archive like runs"),
	OPT_BOOLEAN('n', "noinstr", &noinstr, "noinstr validation for vmlinux.o"),
	OPT_BOOLEAN('l', "vmlinux", &vmlinux, "vmlinux.o validation"),
	OPT_BOOLEAN('M', "mcount", &mcount, "generate __mcount_loc"),
	OPT_BOOLEAN('B', "backup", &backup, "create .orig files before modification"),
	OPT_BOOLEAN('S', "sls", &sls, "validate straight-line-speculation"),
	OPT_BOOLEAN(0, "dry-run", &dryrun, "don't write the modifications"),
	OPT_BOOLEAN(0, "ibt", &ibt, "validate ENDBR placement"),
	OPT_END(),
};

int cmd_parse_options(int argc, const char **argv, const char * const usage[])
{
	const char *envv[16] = { };
	char *env;
	int envc;

	env = getenv("OBJTOOL_ARGS");
	if (env) {
		envv[0] = "OBJTOOL_ARGS";
		for (envc = 1; envc < ARRAY_SIZE(envv); ) {
			envv[envc++] = env;
			env = strchr(env, ' ');
			if (!env)
				break;
			*env = '\0';
			env++;
		}

		parse_options(envc, envv, check_options, env_usage, 0);
	}

	argc = parse_options(argc, argv, check_options, usage, 0);
	if (argc != 1)
		usage_with_options(usage, check_options);
	return argc;
}

int cmd_check(int argc, const char **argv)
{
	const char *objname;
	struct objtool_file *file;
	int ret;

	argc = cmd_parse_options(argc, argv, check_usage);
	objname = argv[0];

	file = objtool_open_read(objname);
	if (!file)
		return 1;

	ret = check(file);
	if (ret)
		return ret;

	if (file->elf->changed)
		return elf_write(file->elf);

	return 0;
}
