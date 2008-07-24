#ifndef ___ASM_SPARC_OPLIB_H
#define ___ASM_SPARC_OPLIB_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/oplib_64.h>
#else
#include <asm-sparc/oplib_32.h>
#endif
#endif
