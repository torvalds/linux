#ifndef ___ASM_SPARC_CHECKSUM_H
#define ___ASM_SPARC_CHECKSUM_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/checksum_64.h>
#else
#include <asm-sparc/checksum_32.h>
#endif
#endif
