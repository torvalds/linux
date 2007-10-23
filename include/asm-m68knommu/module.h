#ifndef ASM_M68KNOMMU_MODULE_H
#define ASM_M68KNOMMU_MODULE_H

struct mod_arch_specific {
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

#endif /* ASM_M68KNOMMU_MODULE_H */
