#ifndef ___ASM_SPARC_PGALLOC_H
#define ___ASM_SPARC_PGALLOC_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/pgalloc_64.h>
#else
#include <asm-sparc/pgalloc_32.h>
#endif
#endif
