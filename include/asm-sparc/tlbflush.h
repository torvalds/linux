#ifndef ___ASM_SPARC_TLBFLUSH_H
#define ___ASM_SPARC_TLBFLUSH_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/tlbflush_64.h>
#else
#include <asm-sparc/tlbflush_32.h>
#endif
#endif
