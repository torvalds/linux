#ifndef __UM_ELF_H
#define __UM_ELF_H

#include "linux/config.h"
#include "asm/archparam.h"

extern long elf_aux_hwcap;
#define ELF_HWCAP (elf_aux_hwcap)

#define SET_PERSONALITY(ex, ibcs2) do ; while(0)

#define ELF_EXEC_PAGESIZE 4096

#define elf_check_arch(x) (1)

#ifdef CONFIG_64_BIT
#define ELF_CLASS ELFCLASS64
#else
#define ELF_CLASS ELFCLASS32
#endif

#define USE_ELF_CORE_DUMP

#define R_386_NONE	0
#define R_386_32	1
#define R_386_PC32	2
#define R_386_GOT32	3
#define R_386_PLT32	4
#define R_386_COPY	5
#define R_386_GLOB_DAT	6
#define R_386_JMP_SLOT	7
#define R_386_RELATIVE	8
#define R_386_GOTOFF	9
#define R_386_GOTPC	10
#define R_386_NUM	11

#endif
