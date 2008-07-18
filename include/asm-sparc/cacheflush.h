#ifndef ___ASM_SPARC_CACHEFLUSH_H
#define ___ASM_SPARC_CACHEFLUSH_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/cacheflush_64.h>
#else
#include <asm-sparc/cacheflush_32.h>
#endif
#endif
