/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SCRIPTS_ELF_PARSE_H
#define _SCRIPTS_ELF_PARSE_H

#include <elf.h>

#include <tools/be_byteshift.h>
#include <tools/le_byteshift.h>

typedef union {
	Elf32_Ehdr	e32;
	Elf64_Ehdr	e64;
} Elf_Ehdr;

typedef union {
	Elf32_Shdr	e32;
	Elf64_Shdr	e64;
} Elf_Shdr;

typedef union {
	Elf32_Sym	e32;
	Elf64_Sym	e64;
} Elf_Sym;

typedef union {
	Elf32_Rela	e32;
	Elf64_Rela	e64;
} Elf_Rela;

struct elf_funcs {
	int (*compare_extable)(const void *a, const void *b);
	uint64_t (*ehdr_shoff)(Elf_Ehdr *ehdr);
	uint16_t (*ehdr_shstrndx)(Elf_Ehdr *ehdr);
	uint16_t (*ehdr_shentsize)(Elf_Ehdr *ehdr);
	uint16_t (*ehdr_shnum)(Elf_Ehdr *ehdr);
	uint64_t (*shdr_addr)(Elf_Shdr *shdr);
	uint64_t (*shdr_offset)(Elf_Shdr *shdr);
	uint64_t (*shdr_size)(Elf_Shdr *shdr);
	uint64_t (*shdr_entsize)(Elf_Shdr *shdr);
	uint32_t (*shdr_link)(Elf_Shdr *shdr);
	uint32_t (*shdr_name)(Elf_Shdr *shdr);
	uint32_t (*shdr_type)(Elf_Shdr *shdr);
	uint8_t (*sym_type)(Elf_Sym *sym);
	uint32_t (*sym_name)(Elf_Sym *sym);
	uint64_t (*sym_value)(Elf_Sym *sym);
	uint16_t (*sym_shndx)(Elf_Sym *sym);
	uint64_t (*rela_offset)(Elf_Rela *rela);
	uint64_t (*rela_info)(Elf_Rela *rela);
	uint64_t (*rela_addend)(Elf_Rela *rela);
	void (*rela_write_addend)(Elf_Rela *rela, uint64_t val);
	uint32_t (*r)(const uint32_t *);
	uint16_t (*r2)(const uint16_t *);
	uint64_t (*r8)(const uint64_t *);
	void (*w)(uint32_t, uint32_t *);
	void (*w8)(uint64_t, uint64_t *);
};

extern struct elf_funcs elf_parser;

static inline uint64_t ehdr64_shoff(Elf_Ehdr *ehdr)
{
	return elf_parser.r8(&ehdr->e64.e_shoff);
}

static inline uint64_t ehdr32_shoff(Elf_Ehdr *ehdr)
{
	return elf_parser.r(&ehdr->e32.e_shoff);
}

static inline uint64_t ehdr_shoff(Elf_Ehdr *ehdr)
{
	return elf_parser.ehdr_shoff(ehdr);
}

#define EHDR_HALF(fn_name)				\
static inline uint16_t ehdr64_##fn_name(Elf_Ehdr *ehdr)	\
{							\
	return elf_parser.r2(&ehdr->e64.e_##fn_name);	\
}							\
							\
static inline uint16_t ehdr32_##fn_name(Elf_Ehdr *ehdr)	\
{							\
	return elf_parser.r2(&ehdr->e32.e_##fn_name);	\
}							\
							\
static inline uint16_t ehdr_##fn_name(Elf_Ehdr *ehdr)	\
{							\
	return elf_parser.ehdr_##fn_name(ehdr);		\
}

EHDR_HALF(shentsize)
EHDR_HALF(shstrndx)
EHDR_HALF(shnum)

#define SHDR_WORD(fn_name)				\
static inline uint32_t shdr64_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.r(&shdr->e64.sh_##fn_name);	\
}							\
							\
static inline uint32_t shdr32_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.r(&shdr->e32.sh_##fn_name);	\
}							\
							\
static inline uint32_t shdr_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.shdr_##fn_name(shdr);	\
}

#define SHDR_ADDR(fn_name)				\
static inline uint64_t shdr64_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.r8(&shdr->e64.sh_##fn_name);	\
}							\
							\
static inline uint64_t shdr32_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.r(&shdr->e32.sh_##fn_name);	\
}							\
							\
static inline uint64_t shdr_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.shdr_##fn_name(shdr);		\
}

#define SHDR_WORD(fn_name)				\
static inline uint32_t shdr64_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.r(&shdr->e64.sh_##fn_name);	\
}							\
							\
static inline uint32_t shdr32_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.r(&shdr->e32.sh_##fn_name);	\
}							\
static inline uint32_t shdr_##fn_name(Elf_Shdr *shdr)	\
{							\
	return elf_parser.shdr_##fn_name(shdr);		\
}

SHDR_ADDR(addr)
SHDR_ADDR(offset)
SHDR_ADDR(size)
SHDR_ADDR(entsize)

SHDR_WORD(link)
SHDR_WORD(name)
SHDR_WORD(type)

#define SYM_ADDR(fn_name)				\
static inline uint64_t sym64_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.r8(&sym->e64.st_##fn_name);	\
}							\
							\
static inline uint64_t sym32_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.r(&sym->e32.st_##fn_name);	\
}							\
							\
static inline uint64_t sym_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.sym_##fn_name(sym);		\
}

#define SYM_WORD(fn_name)				\
static inline uint32_t sym64_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.r(&sym->e64.st_##fn_name);	\
}							\
							\
static inline uint32_t sym32_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.r(&sym->e32.st_##fn_name);	\
}							\
							\
static inline uint32_t sym_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.sym_##fn_name(sym);		\
}

#define SYM_HALF(fn_name)				\
static inline uint16_t sym64_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.r2(&sym->e64.st_##fn_name);	\
}							\
							\
static inline uint16_t sym32_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.r2(&sym->e32.st_##fn_name);	\
}							\
							\
static inline uint16_t sym_##fn_name(Elf_Sym *sym)	\
{							\
	return elf_parser.sym_##fn_name(sym);		\
}

static inline uint8_t sym64_type(Elf_Sym *sym)
{
	return ELF64_ST_TYPE(sym->e64.st_info);
}

static inline uint8_t sym32_type(Elf_Sym *sym)
{
	return ELF32_ST_TYPE(sym->e32.st_info);
}

static inline uint8_t sym_type(Elf_Sym *sym)
{
	return elf_parser.sym_type(sym);
}

SYM_ADDR(value)
SYM_WORD(name)
SYM_HALF(shndx)

#define __maybe_unused			__attribute__((__unused__))

#define RELA_ADDR(fn_name)						\
static inline uint64_t rela64_##fn_name(Elf_Rela *rela)			\
{									\
	return elf_parser.r8((uint64_t *)&rela->e64.r_##fn_name);	\
}									\
									\
static inline uint64_t rela32_##fn_name(Elf_Rela *rela)			\
{									\
	return elf_parser.r((uint32_t *)&rela->e32.r_##fn_name);	\
}									\
									\
static inline uint64_t __maybe_unused rela_##fn_name(Elf_Rela *rela)	\
{									\
	return elf_parser.rela_##fn_name(rela);				\
}

RELA_ADDR(offset)
RELA_ADDR(info)
RELA_ADDR(addend)

static inline void rela64_write_addend(Elf_Rela *rela, uint64_t val)
{
	elf_parser.w8(val, (uint64_t *)&rela->e64.r_addend);
}

static inline void rela32_write_addend(Elf_Rela *rela, uint64_t val)
{
	elf_parser.w(val, (uint32_t *)&rela->e32.r_addend);
}

static inline uint32_t rbe(const uint32_t *x)
{
	return get_unaligned_be32(x);
}

static inline uint16_t r2be(const uint16_t *x)
{
	return get_unaligned_be16(x);
}

static inline uint64_t r8be(const uint64_t *x)
{
	return get_unaligned_be64(x);
}

static inline uint32_t rle(const uint32_t *x)
{
	return get_unaligned_le32(x);
}

static inline uint16_t r2le(const uint16_t *x)
{
	return get_unaligned_le16(x);
}

static inline uint64_t r8le(const uint64_t *x)
{
	return get_unaligned_le64(x);
}

static inline void wbe(uint32_t val, uint32_t *x)
{
	put_unaligned_be32(val, x);
}

static inline void wle(uint32_t val, uint32_t *x)
{
	put_unaligned_le32(val, x);
}

static inline void w8be(uint64_t val, uint64_t *x)
{
	put_unaligned_be64(val, x);
}

static inline void w8le(uint64_t val, uint64_t *x)
{
	put_unaligned_le64(val, x);
}

void *elf_map(char const *fname, size_t *size, uint32_t types);
void elf_unmap(void *addr, size_t size);
int elf_map_machine(void *addr);
int elf_map_long_size(void *addr);

#endif /* _SCRIPTS_ELF_PARSE_H */
