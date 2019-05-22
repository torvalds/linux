/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
