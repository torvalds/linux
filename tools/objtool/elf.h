/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _OBJTOOL_ELF_H
#define _OBJTOOL_ELF_H

#include <stdio.h>
#include <gelf.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/rbtree.h>
#include <linux/jhash.h>

#ifdef LIBELF_USE_DEPRECATED
# define elf_getshdrnum    elf_getshnum
# define elf_getshdrstrndx elf_getshstrndx
#endif

/*
 * Fallback for systems without this "read, mmaping if possible" cmd.
 */
#ifndef ELF_C_READ_MMAP
#define ELF_C_READ_MMAP ELF_C_READ
#endif

struct section {
	struct list_head list;
	struct hlist_node hash;
	struct hlist_node name_hash;
	GElf_Shdr sh;
	struct rb_root symbol_tree;
	struct list_head symbol_list;
	struct list_head rela_list;
	struct section *base, *rela;
	struct symbol *sym;
	Elf_Data *data;
	char *name;
	int idx;
	unsigned int len;
	bool changed, text, rodata, noinstr;
};

struct symbol {
	struct list_head list;
	struct rb_node node;
	struct hlist_node hash;
	struct hlist_node name_hash;
	GElf_Sym sym;
	struct section *sec;
	char *name;
	unsigned int idx;
	unsigned char bind, type;
	unsigned long offset;
	unsigned int len;
	struct symbol *pfunc, *cfunc, *alias;
	bool uaccess_safe;
};

struct rela {
	struct list_head list;
	struct hlist_node hash;
	GElf_Rela rela;
	struct section *sec;
	struct symbol *sym;
	unsigned int type;
	unsigned long offset;
	int addend;
	bool jump_table_start;
};

#define ELF_HASH_BITS	20

struct elf {
	Elf *elf;
	GElf_Ehdr ehdr;
	int fd;
	char *name;
	struct list_head sections;
	DECLARE_HASHTABLE(symbol_hash, ELF_HASH_BITS);
	DECLARE_HASHTABLE(symbol_name_hash, ELF_HASH_BITS);
	DECLARE_HASHTABLE(section_hash, ELF_HASH_BITS);
	DECLARE_HASHTABLE(section_name_hash, ELF_HASH_BITS);
	DECLARE_HASHTABLE(rela_hash, ELF_HASH_BITS);
};

#define OFFSET_STRIDE_BITS	4
#define OFFSET_STRIDE		(1UL << OFFSET_STRIDE_BITS)
#define OFFSET_STRIDE_MASK	(~(OFFSET_STRIDE - 1))

#define for_offset_range(_offset, _start, _end)			\
	for (_offset = ((_start) & OFFSET_STRIDE_MASK);		\
	     _offset >= ((_start) & OFFSET_STRIDE_MASK) &&	\
	     _offset <= ((_end) & OFFSET_STRIDE_MASK);		\
	     _offset += OFFSET_STRIDE)

static inline u32 sec_offset_hash(struct section *sec, unsigned long offset)
{
	u32 ol, oh, idx = sec->idx;

	offset &= OFFSET_STRIDE_MASK;

	ol = offset;
	oh = (offset >> 16) >> 16;

	__jhash_mix(ol, oh, idx);

	return ol;
}

static inline u32 rela_hash(struct rela *rela)
{
	return sec_offset_hash(rela->sec, rela->offset);
}

struct elf *elf_open_read(const char *name, int flags);
struct section *elf_create_section(struct elf *elf, const char *name, size_t entsize, int nr);
struct section *elf_create_rela_section(struct elf *elf, struct section *base);
void elf_add_rela(struct elf *elf, struct rela *rela);
int elf_write(const struct elf *elf);
void elf_close(struct elf *elf);

struct section *find_section_by_name(const struct elf *elf, const char *name);
struct symbol *find_func_by_offset(struct section *sec, unsigned long offset);
struct symbol *find_symbol_by_offset(struct section *sec, unsigned long offset);
struct symbol *find_symbol_by_name(const struct elf *elf, const char *name);
struct symbol *find_symbol_containing(const struct section *sec, unsigned long offset);
struct rela *find_rela_by_dest(const struct elf *elf, struct section *sec, unsigned long offset);
struct rela *find_rela_by_dest_range(const struct elf *elf, struct section *sec,
				     unsigned long offset, unsigned int len);
struct symbol *find_func_containing(struct section *sec, unsigned long offset);
int elf_rebuild_rela_section(struct section *sec);

#define for_each_sec(file, sec)						\
	list_for_each_entry(sec, &file->elf->sections, list)

#endif /* _OBJTOOL_ELF_H */
