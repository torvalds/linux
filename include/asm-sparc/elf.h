#ifndef ___ASM_SPARC_ELF_H
#define ___ASM_SPARC_ELF_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/elf_64.h>
#else
#include <asm-sparc/elf_32.h>
#endif
#endif
