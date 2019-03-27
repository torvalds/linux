
#ifdef HAVE_AVX_ASM

#define IN_SANDY2X

#include "consts.S"
#include "fe51_mul.S"
#include "fe51_nsquare.S"
#include "fe51_pack.S"
#include "ladder.S"
#include "ladder_base.S"

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

#endif
