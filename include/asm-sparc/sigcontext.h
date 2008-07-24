#ifndef ___ASM_SPARC_SIGCONTEXT_H
#define ___ASM_SPARC_SIGCONTEXT_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/sigcontext_64.h>
#else
#include <asm-sparc/sigcontext_32.h>
#endif
#endif
