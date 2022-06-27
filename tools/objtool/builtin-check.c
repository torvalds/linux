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
#include "builtin.h"
#include "objtool.h"

bool no_fp, no_unreachable, retpoline, module, backtrace, uaccess, stats,
     validate_dup, vmlinux, sls, unret, rethunk;

static const char * const check_usage[] = {
	"objtool check [<options>] file.o",
	NULL,
};

const struct option check_options[] = {
	OPT_BOOLEAN('f', "no-fp", &no_fp, "Skip frame pointer validation"),
	OPT_BOOLEAN('u', "no-unreachable", &no_unreachable, "Skip 'unreachable instruction' warnings"),
	OPT_BOOLEAN('r', "retpoline", &retpoline, "Validate retpoline assumptions"),
	OPT_BOOLEAN(0,   "rethunk", &rethunk, "validate and annotate rethunk usage"),
	OPT_BOOLEAN(0,   "unret", &unret, "validate entry unret placement"),
	OPT_BOOLEAN('m', "module", &module, "Indicates the object will be part of a kernel module"),
	OPT_BOOLEAN('b', "backtrace", &backtrace, "unwind on error"),
	OPT_BOOLEAN('a', "uaccess", &uaccess, "enable uaccess checking"),
	OPT_BOOLEAN('s', "stats", &stats, "print statistics"),
	OPT_BOOLEAN('d', "duplicate", &validate_dup, "duplicate validation for vmlinux.o"),
	OPT_BOOLEAN('l', "vmlinux", &vmlinux, "vmlinux.o validation"),
	OPT_BOOLEAN('S', "sls", &sls, "validate straight-line-speculation"),
	OPT_END(),
};

int cmd_check(int argc, const char **argv)
{
	const char *objname, *s;
	struct objtool_file *file;
	int ret;

	argc = parse_options(argc, argv, check_options, check_usage, 0);

	if (argc != 1)
		usage_with_options(check_usage, check_options);

	objname = argv[0];

	s = strstr(objname, "vmlinux.o");
	if (s && !s[9])
		vmlinux = true;

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
