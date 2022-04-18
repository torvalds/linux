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

bool help;

const char *objname;
static struct objtool_file file;

static bool objtool_create_backup(const char *_objname)
{
	int len = strlen(_objname);
	char *buf, *base, *name = malloc(len+6);
	int s, d, l, t;

	if (!name) {
		perror("failed backup name malloc");
		return false;
	}

	strcpy(name, _objname);
	strcpy(name + len, ".orig");

	d = open(name, O_CREAT|O_WRONLY|O_TRUNC, 0644);
	if (d < 0) {
		perror("failed to create backup file");
		return false;
	}

	s = open(_objname, O_RDONLY);
	if (s < 0) {
		perror("failed to open orig file");
		return false;
	}

	buf = malloc(4096);
	if (!buf) {
		perror("failed backup data malloc");
		return false;
	}

	while ((l = read(s, buf, 4096)) > 0) {
		base = buf;
		do {
			t = write(d, base, l);
			if (t < 0) {
				perror("failed backup write");
				return false;
			}
			base += t;
			l -= t;
		} while (l);
	}

	if (l < 0) {
		perror("failed backup read");
		return false;
	}

	free(name);
	free(buf);
	close(d);
	close(s);

	return true;
}

struct objtool_file *objtool_open_read(const char *_objname)
{
	if (objname) {
		if (strcmp(objname, _objname)) {
			WARN("won't handle more than one file at a time");
			return NULL;
		}
		return &file;
	}
	objname = _objname;

	file.elf = elf_open_read(objname, O_RDWR);
	if (!file.elf)
		return NULL;

	if (opts.backup && !objtool_create_backup(objname)) {
		WARN("can't create backup file");
		return NULL;
	}

	INIT_LIST_HEAD(&file.insn_list);
	hash_init(file.insn_hash);
	INIT_LIST_HEAD(&file.retpoline_call_list);
	INIT_LIST_HEAD(&file.static_call_list);
	INIT_LIST_HEAD(&file.mcount_loc_list);
	INIT_LIST_HEAD(&file.endbr_list);
	file.ignore_unreachables = opts.no_unreachable;
	file.hints = false;

	return &file;
}

void objtool_pv_add(struct objtool_file *f, int idx, struct symbol *func)
{
	if (!opts.noinstr)
		return;

	if (!f->pv_ops) {
		WARN("paravirt confusion");
		return;
	}

	/*
	 * These functions will be patched into native code,
	 * see paravirt_patch().
	 */
	if (!strcmp(func->name, "_paravirt_nop") ||
	    !strcmp(func->name, "_paravirt_ident_64"))
		return;

	/* already added this function */
	if (!list_empty(&func->pv_target))
		return;

	list_add(&func->pv_target, &f->pv_ops[idx].targets);
	f->pv_ops[idx].clean = false;
}

int main(int argc, const char **argv)
{
	static const char *UNUSED = "OBJTOOL_NOT_IMPLEMENTED";

	/* libsubcmd init */
	exec_cmd_init("objtool", UNUSED, UNUSED, UNUSED);
	pager_init(UNUSED);

	objtool_run(argc, argv);

	return 0;
}
