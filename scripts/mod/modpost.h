/* SPDX-License-Identifier: GPL-2.0 */
#include <byteswap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include "../../include/linux/module_symbol.h"

#include <list_types.h>
#include "elfconfig.h"

/* On BSD-alike OSes elf.h defines these according to host's word size */
#undef ELF_ST_BIND
#undef ELF_ST_TYPE
#undef ELF_R_SYM
#undef ELF_R_TYPE

#if KERNEL_ELFCLASS == ELFCLASS32

#define Elf_Ehdr    Elf32_Ehdr
#define Elf_Shdr    Elf32_Shdr
#define Elf_Sym     Elf32_Sym
#define Elf_Addr    Elf32_Addr
#define Elf_Section Elf32_Half
#define ELF_ST_BIND ELF32_ST_BIND
#define ELF_ST_TYPE ELF32_ST_TYPE

#define Elf_Rel     Elf32_Rel
#define Elf_Rela    Elf32_Rela
#define ELF_R_SYM   ELF32_R_SYM
#define ELF_R_TYPE  ELF32_R_TYPE
#else

#define Elf_Ehdr    Elf64_Ehdr
#define Elf_Shdr    Elf64_Shdr
#define Elf_Sym     Elf64_Sym
#define Elf_Addr    Elf64_Addr
#define Elf_Section Elf64_Half
#define ELF_ST_BIND ELF64_ST_BIND
#define ELF_ST_TYPE ELF64_ST_TYPE

#define Elf_Rel     Elf64_Rel
#define Elf_Rela    Elf64_Rela
#define ELF_R_SYM   ELF64_R_SYM
#define ELF_R_TYPE  ELF64_R_TYPE
#endif

#define bswap(x) \
({ \
	_Static_assert(sizeof(x) == 1 || sizeof(x) == 2 || \
		       sizeof(x) == 4 || sizeof(x) == 8, "bug"); \
	(typeof(x))(sizeof(x) == 2 ? bswap_16(x) : \
		    sizeof(x) == 4 ? bswap_32(x) : \
		    sizeof(x) == 8 ? bswap_64(x) : \
		    x); \
})

#define TO_NATIVE(x)	\
	(target_is_big_endian == host_is_big_endian ? x : bswap(x))

#define __get_unaligned_t(type, ptr) ({					\
	const struct { type x; } __attribute__((__packed__)) *__pptr =	\
						(typeof(__pptr))(ptr);	\
	__pptr->x;							\
})

#define get_unaligned(ptr)	__get_unaligned_t(typeof(*(ptr)), (ptr))

#define get_unaligned_native(ptr) \
({ \
	typeof(*(ptr)) _val = get_unaligned(ptr); \
	TO_NATIVE(_val); \
})

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define strstarts(str, prefix) (strncmp(str, prefix, strlen(prefix)) == 0)

struct buffer {
	char *p;
	int pos;
	int size;
};

void __attribute__((format(printf, 2, 3)))
buf_printf(struct buffer *buf, const char *fmt, ...);

void
buf_write(struct buffer *buf, const char *s, int len);

/**
 * struct module_alias - auto-generated MODULE_ALIAS()
 *
 * @node: linked to module::aliases
 * @modname: name of the builtin module (only for vmlinux)
 * @str: a string for MODULE_ALIAS()
 */
struct module_alias {
	struct list_head node;
	char *builtin_modname;
	char str[];
};

/**
 * struct module - represent a module (vmlinux or *.ko)
 *
 * @dump_file: path to the .symvers file if loaded from a file
 * @aliases: list head for module_aliases
 * @no_trim_symbol: .no_trim_symbol section data
 * @no_trim_symbol_len: length of the .no_trim_symbol section
 */
struct module {
	struct list_head list;
	struct list_head exported_symbols;
	struct list_head unresolved_symbols;
	const char *dump_file;
	bool is_gpl_compatible;
	bool is_vmlinux;
	bool seen;
	bool has_init;
	bool has_cleanup;
	char	     srcversion[25];
	// Missing namespace dependencies
	struct list_head missing_namespaces;
	// Actual imported namespaces
	struct list_head imported_namespaces;
	struct list_head aliases;
	char *no_trim_symbol;
	unsigned int no_trim_symbol_len;
	char name[];
};

struct elf_info {
	size_t size;
	Elf_Ehdr     *hdr;
	Elf_Shdr     *sechdrs;
	Elf_Sym      *symtab_start;
	Elf_Sym      *symtab_stop;
	unsigned int export_symbol_secndx;	/* .export_symbol section */
	char         *strtab;
	char	     *modinfo;
	unsigned int modinfo_len;
	char         *no_trim_symbol;
	unsigned int no_trim_symbol_len;

	/* support for 32bit section numbers */

	unsigned int num_sections; /* max_secindex + 1 */
	unsigned int secindex_strings;
	/* if Nth symbol table entry has .st_shndx = SHN_XINDEX,
	 * take shndx from symtab_shndx_start[N] instead */
	Elf32_Word   *symtab_shndx_start;
	Elf32_Word   *symtab_shndx_stop;

	struct symsearch *symsearch;
};

/* Accessor for sym->st_shndx, hides ugliness of "64k sections" */
static inline unsigned int get_secindex(const struct elf_info *info,
					const Elf_Sym *sym)
{
	unsigned int index = sym->st_shndx;

	/*
	 * Elf{32,64}_Sym::st_shndx is 2 byte. Big section numbers are available
	 * in the .symtab_shndx section.
	 */
	if (index == SHN_XINDEX)
		return info->symtab_shndx_start[sym - info->symtab_start];

	/*
	 * Move reserved section indices SHN_LORESERVE..SHN_HIRESERVE out of
	 * the way to UINT_MAX-255..UINT_MAX, to avoid conflicting with real
	 * section indices.
	 */
	if (index >= SHN_LORESERVE && index <= SHN_HIRESERVE)
		return index - SHN_HIRESERVE - 1;

	return index;
}

/*
 * If there's no name there, ignore it; likewise, ignore it if it's
 * one of the magic symbols emitted used by current tools.
 *
 * Internal symbols created by tools should be ignored by modpost.
 */
static inline bool is_valid_name(struct elf_info *elf, Elf_Sym *sym)
{
	const char *name = elf->strtab + sym->st_name;

	if (!name || !strlen(name))
		return false;
	return !is_mapping_symbol(name);
}

/* symsearch.c */
void symsearch_init(struct elf_info *elf);
void symsearch_finish(struct elf_info *elf);
Elf_Sym *symsearch_find_nearest(struct elf_info *elf, Elf_Addr addr,
				unsigned int secndx, bool allow_negative,
				Elf_Addr min_distance);

/* file2alias.c */
void handle_moddevtable(struct module *mod, struct elf_info *info,
			Elf_Sym *sym, const char *symname);

/* sumversion.c */
void get_src_version(const char *modname, char sum[], unsigned sumlen);

/* from modpost.c */
extern bool target_is_big_endian;
extern bool host_is_big_endian;
const char *get_basename(const char *path);
char *read_text_file(const char *filename);
char *get_line(char **stringp);
void *sym_get_data(const struct elf_info *info, const Elf_Sym *sym);

void __attribute__((format(printf, 2, 3)))
modpost_log(bool is_error, const char *fmt, ...);

/*
 * warn - show the given message, then let modpost continue running, still
 *        allowing modpost to exit successfully. This should be used when
 *        we still allow to generate vmlinux and modules.
 *
 * error - show the given message, then let modpost continue running, but fail
 *         in the end. This should be used when we should stop building vmlinux
 *         or modules, but we can continue running modpost to catch as many
 *         issues as possible.
 *
 * fatal - show the given message, and bail out immediately. This should be
 *         used when there is no point to continue running modpost.
 */
#define warn(fmt, args...)	modpost_log(false, fmt, ##args)
#define error(fmt, args...)	modpost_log(true, fmt, ##args)
#define fatal(fmt, args...)	do { error(fmt, ##args); exit(1); } while (1)
