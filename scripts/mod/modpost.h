/* SPDX-License-Identifier: GPL-2.0 */
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
#define Elf_Sword   Elf64_Sword
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
#define Elf_Sword   Elf64_Sxword
#define Elf_Section Elf64_Half
#define ELF_ST_BIND ELF64_ST_BIND
#define ELF_ST_TYPE ELF64_ST_TYPE

#define Elf_Rel     Elf64_Rel
#define Elf_Rela    Elf64_Rela
#define ELF_R_SYM   ELF64_R_SYM
#define ELF_R_TYPE  ELF64_R_TYPE
#endif

/* The 64-bit MIPS ELF ABI uses an unusual reloc format. */
typedef struct
{
	Elf32_Word    r_sym;	/* Symbol index */
	unsigned char r_ssym;	/* Special symbol for 2nd relocation */
	unsigned char r_type3;	/* 3rd relocation type */
	unsigned char r_type2;	/* 2nd relocation type */
	unsigned char r_type1;	/* 1st relocation type */
} _Elf64_Mips_R_Info;

typedef union
{
	Elf64_Xword		r_info_number;
	_Elf64_Mips_R_Info	r_info_fields;
} _Elf64_Mips_R_Info_union;

#define ELF64_MIPS_R_SYM(i) \
  ((__extension__ (_Elf64_Mips_R_Info_union)(i)).r_info_fields.r_sym)

#define ELF64_MIPS_R_TYPE(i) \
  ((__extension__ (_Elf64_Mips_R_Info_union)(i)).r_info_fields.r_type1)

#if KERNEL_ELFDATA != HOST_ELFDATA

static inline void __endian(const void *src, void *dest, unsigned int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
		((unsigned char*)dest)[i] = ((unsigned char*)src)[size - i-1];
}

#define TO_NATIVE(x)						\
({								\
	typeof(x) __x;						\
	__endian(&(x), &(__x), sizeof(__x));			\
	__x;							\
})

#else /* endianness matches */

#define TO_NATIVE(x) (x)

#endif

#define NOFAIL(ptr)   do_nofail((ptr), #ptr)
void *do_nofail(void *ptr, const char *expr);

struct buffer {
	char *p;
	int pos;
	int size;
};

void __attribute__((format(printf, 2, 3)))
buf_printf(struct buffer *buf, const char *fmt, ...);

void
buf_write(struct buffer *buf, const char *s, int len);

struct module {
	struct module *next;
	const char *name;
	int gpl_compatible;
	struct symbol *unres;
	int seen;
	int skip;
	int has_init;
	int has_cleanup;
	struct buffer dev_table_buf;
	char	     srcversion[25];
	int is_dot_o;
};

struct elf_info {
	unsigned long size;
	Elf_Ehdr     *hdr;
	Elf_Shdr     *sechdrs;
	Elf_Sym      *symtab_start;
	Elf_Sym      *symtab_stop;
	Elf_Section  export_sec;
	Elf_Section  export_unused_sec;
	Elf_Section  export_gpl_sec;
	Elf_Section  export_unused_gpl_sec;
	Elf_Section  export_gpl_future_sec;
	char	     *ksymtab_strings;
	char         *strtab;
	char	     *modinfo;
	unsigned int modinfo_len;

	/* support for 32bit section numbers */

	unsigned int num_sections; /* max_secindex + 1 */
	unsigned int secindex_strings;
	/* if Nth symbol table entry has .st_shndx = SHN_XINDEX,
	 * take shndx from symtab_shndx_start[N] instead */
	Elf32_Word   *symtab_shndx_start;
	Elf32_Word   *symtab_shndx_stop;
};

static inline int is_shndx_special(unsigned int i)
{
	return i != SHN_XINDEX && i >= SHN_LORESERVE && i <= SHN_HIRESERVE;
}

/*
 * Move reserved section indices SHN_LORESERVE..SHN_HIRESERVE out of
 * the way to -256..-1, to avoid conflicting with real section
 * indices.
 */
#define SPECIAL(i) ((i) - (SHN_HIRESERVE + 1))

/* Accessor for sym->st_shndx, hides ugliness of "64k sections" */
static inline unsigned int get_secindex(const struct elf_info *info,
					const Elf_Sym *sym)
{
	if (is_shndx_special(sym->st_shndx))
		return SPECIAL(sym->st_shndx);
	if (sym->st_shndx != SHN_XINDEX)
		return sym->st_shndx;
	return info->symtab_shndx_start[sym - info->symtab_start];
}

/* file2alias.c */
extern unsigned int cross_build;
void handle_moddevtable(struct module *mod, struct elf_info *info,
			Elf_Sym *sym, const char *symname);
void add_moddevtable(struct buffer *buf, struct module *mod);

/* sumversion.c */
void maybe_frob_rcs_version(const char *modfilename,
			    char *version,
			    void *modinfo,
			    unsigned long modinfo_offset);
void get_src_version(const char *modname, char sum[], unsigned sumlen);

/* from modpost.c */
void *grab_file(const char *filename, unsigned long *size);
char* get_next_line(unsigned long *pos, void *file, unsigned long size);
void release_file(void *file, unsigned long size);

void fatal(const char *fmt, ...);
void warn(const char *fmt, ...);
void merror(const char *fmt, ...);
