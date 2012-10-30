#ifndef __ASM_GENERIC_MODULE_H
#define __ASM_GENERIC_MODULE_H

/*
 * Many architectures just need a simple module
 * loader without arch specific data.
 */
#ifndef CONFIG_HAVE_MOD_ARCH_SPECIFIC
struct mod_arch_specific
{
};
#endif

#ifdef CONFIG_64BIT
#define Elf_Shdr	Elf64_Shdr
#define Elf_Phdr	Elf64_Phdr
#define Elf_Sym		Elf64_Sym
#define Elf_Dyn		Elf64_Dyn
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Addr	Elf64_Addr
#ifdef CONFIG_MODULES_USE_ELF_REL
#define Elf_Rel		Elf64_Rel
#endif
#ifdef CONFIG_MODULES_USE_ELF_RELA
#define Elf_Rela	Elf64_Rela
#endif
#define ELF_R_TYPE(X)	ELF64_R_TYPE(X)
#define ELF_R_SYM(X)	ELF64_R_SYM(X)

#else /* CONFIG_64BIT */

#define Elf_Shdr	Elf32_Shdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Sym		Elf32_Sym
#define Elf_Dyn		Elf32_Dyn
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#ifdef CONFIG_MODULES_USE_ELF_REL
#define Elf_Rel		Elf32_Rel
#endif
#ifdef CONFIG_MODULES_USE_ELF_RELA
#define Elf_Rela	Elf32_Rela
#endif
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)
#endif

#endif /* __ASM_GENERIC_MODULE_H */
