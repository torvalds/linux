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

#ifndef _OBJTOOL_ELF_H
#define _OBJTOOL_ELF_H

#include <stdio.h>
#include <gelf.h>
#include <linux/list.h>
#include <linux/hashtable.h>

struct section {
	struct list_head list;
	GElf_Shdr sh;
	struct list_head symbol_list;
	DECLARE_HASHTABLE(symbol_hash, 8);
	struct list_head rela_list;
	DECLARE_HASHTABLE(rela_hash, 16);
	struct section *base, *rela;
	struct symbol *sym;
	Elf_Data *elf_data;
	char *name;
	int idx;
	unsigned long data;
	unsigned int len;
};

struct symbol {
	struct list_head list;
	struct hlist_node hash;
	GElf_Sym sym;
	struct section *sec;
	char *name;
	unsigned int idx;
	unsigned char bind, type;
	unsigned long offset;
	unsigned int len;
};

struct rela {
	struct list_head list;
	struct hlist_node hash;
	GElf_Rela rela;
	struct symbol *sym;
	unsigned int type;
	unsigned long offset;
	int addend;
};

struct elf {
	Elf *elf;
	GElf_Ehdr ehdr;
	int fd;
	char *name;
	struct list_head sections;
	DECLARE_HASHTABLE(rela_hash, 16);
};


struct elf *elf_open(const char *name);
struct section *find_section_by_name(struct elf *elf, const char *name);
struct symbol *find_symbol_by_offset(struct section *sec, unsigned long offset);
struct rela *find_rela_by_dest(struct section *sec, unsigned long offset);
struct rela *find_rela_by_dest_range(struct section *sec, unsigned long offset,
				     unsigned int len);
struct symbol *find_containing_func(struct section *sec, unsigned long offset);
void elf_close(struct elf *elf);



#endif /* _OBJTOOL_ELF_H */
