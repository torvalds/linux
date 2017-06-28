/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
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

#ifndef _CHECK_H
#define _CHECK_H

#include <stdbool.h>
#include "elf.h"
#include "arch.h"
#include <linux/hashtable.h>

struct instruction {
	struct list_head list;
	struct hlist_node hash;
	struct section *sec;
	unsigned long offset;
	unsigned int len, state;
	unsigned char type;
	unsigned long immediate;
	bool alt_group, visited, dead_end;
	struct symbol *call_dest;
	struct instruction *jump_dest;
	struct list_head alts;
	struct symbol *func;
};

struct objtool_file {
	struct elf *elf;
	struct list_head insn_list;
	DECLARE_HASHTABLE(insn_hash, 16);
	struct section *rodata, *whitelist;
	bool ignore_unreachables, c_file;
};

int check(const char *objname, bool nofp);

#endif /* _CHECK_H */
