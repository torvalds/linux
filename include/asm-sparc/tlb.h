#ifndef ___ASM_SPARC_TLB_H
#define ___ASM_SPARC_TLB_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/tlb_64.h>
#else
#include <asm-sparc/tlb_32.h>
#endif
#endif
