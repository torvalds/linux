#ifndef __GENELF_H__
#define __GENELF_H__

/* genelf.c */
int jit_write_elf(int fd, uint64_t code_addr, const char *sym,
		  const void *code, int csize, void *debug, int nr_debug_entries);
#ifdef HAVE_DWARF_SUPPORT
/* genelf_debug.c */
int jit_add_debug_info(Elf *e, uint64_t code_addr, void *debug, int nr_debug_entries);
#endif

#if   defined(__arm__)
#define GEN_ELF_ARCH	EM_ARM
#define GEN_ELF_CLASS	ELFCLASS32
#elif defined(__aarch64__)
#define GEN_ELF_ARCH	EM_AARCH64
#define GEN_ELF_CLASS	ELFCLASS64
#elif defined(__x86_64__)
#define GEN_ELF_ARCH	EM_X86_64
#define GEN_ELF_CLASS	ELFCLASS64
#elif defined(__i386__)
#define GEN_ELF_ARCH	EM_386
#define GEN_ELF_CLASS	ELFCLASS32
#elif defined(__powerpc64__)
#define GEN_ELF_ARCH	EM_PPC64
#define GEN_ELF_CLASS	ELFCLASS64
#elif defined(__powerpc__)
#define GEN_ELF_ARCH	EM_PPC
#define GEN_ELF_CLASS	ELFCLASS32
#else
#error "unsupported architecture"
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
#define GEN_ELF_ENDIAN	ELFDATA2MSB
#else
#define GEN_ELF_ENDIAN	ELFDATA2LSB
#endif

#if GEN_ELF_CLASS == ELFCLASS64
#define elf_newehdr	elf64_newehdr
#define elf_getshdr	elf64_getshdr
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Shdr	Elf64_Shdr
#define Elf_Sym		Elf64_Sym
#define ELF_ST_TYPE(a)	ELF64_ST_TYPE(a)
#define ELF_ST_BIND(a)	ELF64_ST_BIND(a)
#define ELF_ST_VIS(a)	ELF64_ST_VISIBILITY(a)
#else
#define elf_newehdr	elf32_newehdr
#define elf_getshdr	elf32_getshdr
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Shdr	Elf32_Shdr
#define Elf_Sym		Elf32_Sym
#define ELF_ST_TYPE(a)	ELF32_ST_TYPE(a)
#define ELF_ST_BIND(a)	ELF32_ST_BIND(a)
#define ELF_ST_VIS(a)	ELF32_ST_VISIBILITY(a)
#endif

/* The .text section is directly after the ELF header */
#define GEN_ELF_TEXT_OFFSET sizeof(Elf_Ehdr)

#endif
