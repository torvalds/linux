// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <subcmd/exec-cmd.h>
#include <subcmd/pager.h>
#include <linux/kernel.h>

#include <objtool/builtin.h>
#include <objtool/objtool.h>
#include <objtool/warn.h>

bool debug;
int indent;

static struct objtool_file file;

struct objtool_file *objtool_open_read(const char *filename)
{
	if (file.elf) {
		ERROR("won't handle more than one file at a time");
		return NULL;
	}

	file.elf = elf_open_read(filename, O_RDWR);
	if (!file.elf)
		return NULL;

	hash_init(file.insn_hash);
	INIT_LIST_HEAD(&file.retpoline_call_list);
	INIT_LIST_HEAD(&file.return_thunk_list);
	INIT_LIST_HEAD(&file.static_call_list);
	INIT_LIST_HEAD(&file.mcount_loc_list);
	INIT_LIST_HEAD(&file.endbr_list);
	INIT_LIST_HEAD(&file.call_list);
	file.ignore_unreachables = opts.no_unreachable;
	file.hints = false;

	return &file;
}

int objtool_pv_add(struct objtool_file *f, int idx, struct symbol *func)
{
	if (!opts.noinstr)
		return 0;

	if (!f->pv_ops) {
		ERROR("paravirt confusion");
		return -1;
	}

	/*
	 * These functions will be patched into native code,
	 * see paravirt_patch().
	 */
	if (!strcmp(func->name, "_paravirt_nop") ||
	    !strcmp(func->name, "_paravirt_ident_64"))
		return 0;

	/* already added this function */
	if (!list_empty(&func->pv_target))
		return 0;

	list_add(&func->pv_target, &f->pv_ops[idx].targets);
	f->pv_ops[idx].clean = false;
	return 0;
}

char *top_level_dir(const char *file)
{
	ssize_t len, self_len, file_len;
	char self[PATH_MAX], *str;
	int i;

	len = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (len <= 0)
		return NULL;
	self[len] = '\0';

	for (i = 0; i < 3; i++) {
		char *s = strrchr(self, '/');
		if (!s)
			return NULL;
		*s = '\0';
	}

	self_len = strlen(self);
	file_len = strlen(file);

	str = malloc(self_len + file_len + 2);
	if (!str)
		return NULL;

	memcpy(str, self, self_len);
	str[self_len] = '/';
	strcpy(str + self_len + 1, file);

	return str;
}

int main(int argc, const char **argv)
{
	static const char *UNUSED = "OBJTOOL_NOT_IMPLEMENTED";

	if (init_signal_handler())
		return -1;

	/* libsubcmd init */
	exec_cmd_init("objtool", UNUSED, UNUSED, UNUSED);
	pager_init(UNUSED);

	if (argc > 1 && !strcmp(argv[1], "klp")) {
		argc--;
		argv++;
		return cmd_klp(argc, argv);
	}

	return objtool_run(argc, argv);
}
