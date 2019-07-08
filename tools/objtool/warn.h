/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _WARN_H
#define _WARN_H

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "elf.h"

extern const char *objname;

static inline char *offstr(struct section *sec, unsigned long offset)
{
	struct symbol *func;
	char *name, *str;
	unsigned long name_off;

	func = find_containing_func(sec, offset);
	if (func) {
		name = func->name;
		name_off = offset - func->offset;
	} else {
		name = sec->name;
		name_off = offset;
	}

	str = malloc(strlen(name) + 20);

	if (func)
		sprintf(str, "%s()+0x%lx", name, name_off);
	else
		sprintf(str, "%s+0x%lx", name, name_off);

	return str;
}

#define WARN(format, ...)				\
	fprintf(stderr,					\
		"%s: warning: objtool: " format "\n",	\
		objname, ##__VA_ARGS__)

#define WARN_FUNC(format, sec, offset, ...)		\
({							\
	char *_str = offstr(sec, offset);		\
	WARN("%s: " format, _str, ##__VA_ARGS__);	\
	free(_str);					\
})

#define BT_FUNC(format, insn, ...)			\
({							\
	struct instruction *_insn = (insn);		\
	char *_str = offstr(_insn->sec, _insn->offset); \
	WARN("  %s: " format, _str, ##__VA_ARGS__);	\
	free(_str);					\
})

#define WARN_ELF(format, ...)				\
	WARN(format ": %s", ##__VA_ARGS__, elf_errmsg(-1))

#endif /* _WARN_H */
