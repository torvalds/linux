#ifndef ___ASM_SPARC_TIMEX_H
#define ___ASM_SPARC_TIMEX_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/timex_64.h>
#else
#include <asm-sparc/timex_32.h>
#endif
#endif
