#ifndef __UM_MODULE_I386_H
#define __UM_MODULE_I386_H

/* UML is simple */
struct mod_arch_specific
{
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

#endif
