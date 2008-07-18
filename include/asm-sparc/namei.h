#ifndef ___ASM_SPARC_NAMEI_H
#define ___ASM_SPARC_NAMEI_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/namei_64.h>
#else
#include <asm-sparc/namei_32.h>
#endif
#endif
