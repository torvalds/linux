#ifndef ___ASM_SPARC_FUTEX_H
#define ___ASM_SPARC_FUTEX_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/futex_64.h>
#else
#include <asm-sparc/futex_32.h>
#endif
#endif
