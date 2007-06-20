#ifndef __ASM_AVR32_UNALIGNED_H
#define __ASM_AVR32_UNALIGNED_H

/*
 * AVR32 can handle some unaligned accesses, depending on the
 * implementation.  The AVR32 AP implementation can handle unaligned
 * words, but halfwords must be halfword-aligned, and doublewords must
 * be word-aligned.
 *
 * However, swapped word loads must be word-aligned so we can't
 * optimize word loads in general.
 */

#include <asm-generic/unaligned.h>

#endif /* __ASM_AVR32_UNALIGNED_H */
