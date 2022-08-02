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
	struct list_head reloc_list;
	struct section *base, *reloc;
	struct symbol *sym;
	Elf_Data *data;
	char *name;
	int idx;
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
	u8 uaccess_safe      : 1;
	u8 static_call_tramp : 1;
	u8 retpoline_thunk   : 1;
	u8 fentry            : 1;
	u8 profiling_func    : 1;
	struct list_head pv_target;
};

struct reloc {
	struct list_head list;
	struct hlist_node hash;
	union {
		GElf_Rela rela;
		GElf_Rel  rel;
	};
	struct section *sec;
	struct symbol *sym;
	unsigned long offset;
	unsigned int type;
	s64 addend;
	int idx;
	bool jump_table_start;
};

#define ELF_HASH_BITS	20

struct elf {
	Elf *elf;
	GElf_Ehdr ehdr;
	int fd;
	bool changed;
	char *name;
	unsigned int text_size, num_files;
	struct list_head sections;

	int symbol_bits;
	int symbol_name_bits;
	int section_bits;
	int section_name_bits;
	int reloc_bits;

	struct hlist_head *symbol_hash;
	struct hlist_head *symbol_name_hash;
	struct hlist_head *section_hash;
	struct hlist_head *section_name_hash;
	struct hlist_head *reloc_hash;
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

static inline u32 reloc_hash(struct reloc *reloc)
{
	return sec_offset_hash(reloc->sec, reloc->offset);
}

/*
 * Try to see if it's a whole archive (vmlinux.o or module).
 *
 * Note this will miss the case where a module only has one source file.
 */
static inline bool has_multiple_files(struct elf *elf)
{
	return elf->num_files > 1;
}

struct elf *elf_open_read(const char *name, int flags);
struct section *elf_create_section(struct elf *elf, const char *name, unsigned int sh_flags, size_t entsize, int nr);

int elf_add_reloc(struct elf *elf, struct section *sec, unsigned long offset,
		  unsigned int type, struct symbol *sym, s64 addend);
int elf_add_reloc_to_insn(struct elf *elf, struct section *sec,
			  unsigned long offset, unsigned int type,
			  struct section *insn_sec, unsigned long insn_off);

int elf_write_insn(struct elf *elf, struct section *sec,
		   unsigned long offset, unsigned int len,
		   const char *insn);
int elf_write_reloc(struct elf *elf, struct reloc *reloc);
int elf_write(struct elf *elf);
void elf_close(struct elf *elf);

struct section *find_section_by_name(const struct elf *elf, const char *name);
struct symbol *find_func_by_offset(struct section *sec, unsigned long offset);
struct symbol *find_symbol_by_offset(struct section *sec, unsigned long offset);
struct symbol *find_symbol_by_name(const struct elf *elf, const char *name);
struct symbol *find_symbol_containing(const struct section *sec, unsigned long offset);
int find_symbol_hole_containing(const struct section *sec, unsigned long offset);
struct reloc *find_reloc_by_dest(const struct elf *elf, struct section *sec, unsigned long offset);
struct reloc *find_reloc_by_dest_range(const struct elf *elf, struct section *sec,
				     unsigned long offset, unsigned int len);
struct symbol *find_func_containing(struct section *sec, unsigned long offset);

#define for_each_sec(file, sec)						\
	list_for_each_entry(sec, &file->elf->sections, list)

#endif /* _OBJTOOL_ELF_H */
