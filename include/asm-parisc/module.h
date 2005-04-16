#ifndef _ASM_PARISC_MODULE_H
#define _ASM_PARISC_MODULE_H
/*
 * This file contains the parisc architecture specific module code.
 */
#ifdef __LP64__
#define Elf_Shdr Elf64_Shdr
#define Elf_Sym Elf64_Sym
#define Elf_Ehdr Elf64_Ehdr
#define Elf_Addr Elf64_Addr
#define Elf_Rela Elf64_Rela
#else
#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr
#define Elf_Addr Elf32_Addr
#define Elf_Rela Elf32_Rela
#endif

struct unwind_table;

struct mod_arch_specific
{
	unsigned long got_offset, got_count, got_max;
	unsigned long fdesc_offset, fdesc_count, fdesc_max;
	unsigned long stub_offset, stub_count, stub_max;
	unsigned long init_stub_offset, init_stub_count, init_stub_max;
	int unwind_section;
	struct unwind_table *unwind;
};

#endif /* _ASM_PARISC_MODULE_H */
