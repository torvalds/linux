// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <objtool/arch.h>
#include <objtool/warn.h>

#include <linux/string.h>

/* 'funcs' is a space-separated list of function names */
static void disas_funcs(const char *funcs)
{
	const char *objdump_str, *cross_compile;
	int size, ret;
	char *cmd;

	cross_compile = getenv("CROSS_COMPILE");
	if (!cross_compile)
		cross_compile = "";

	objdump_str = "%sobjdump -wdr %s | gawk -M -v _funcs='%s' '"
			"BEGIN { split(_funcs, funcs); }"
			"/^$/ { func_match = 0; }"
			"/<.*>:/ { "
				"f = gensub(/.*<(.*)>:/, \"\\\\1\", 1);"
				"for (i in funcs) {"
					"if (funcs[i] == f) {"
						"func_match = 1;"
						"base = strtonum(\"0x\" $1);"
						"break;"
					"}"
				"}"
			"}"
			"{"
				"if (func_match) {"
					"addr = strtonum(\"0x\" $1);"
					"printf(\"%%04x \", addr - base);"
					"print;"
				"}"
			"}' 1>&2";

	/* fake snprintf() to calculate the size */
	size = snprintf(NULL, 0, objdump_str, cross_compile, objname, funcs) + 1;
	if (size <= 0) {
		WARN("objdump string size calculation failed");
		return;
	}

	cmd = malloc(size);

	/* real snprintf() */
	snprintf(cmd, size, objdump_str, cross_compile, objname, funcs);
	ret = system(cmd);
	if (ret) {
		WARN("disassembly failed: %d", ret);
		return;
	}
}

void disas_warned_funcs(struct objtool_file *file)
{
	struct symbol *sym;
	char *funcs = NULL, *tmp;

	for_each_sym(file->elf, sym) {
		if (sym->warned) {
			if (!funcs) {
				funcs = malloc(strlen(sym->name) + 1);
				if (!funcs) {
					ERROR_GLIBC("malloc");
					return;
				}
				strcpy(funcs, sym->name);
			} else {
				tmp = malloc(strlen(funcs) + strlen(sym->name) + 2);
				if (!tmp) {
					ERROR_GLIBC("malloc");
					return;
				}
				sprintf(tmp, "%s %s", funcs, sym->name);
				free(funcs);
				funcs = tmp;
			}
		}
	}

	if (funcs)
		disas_funcs(funcs);
}
