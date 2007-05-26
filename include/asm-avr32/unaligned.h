#ifndef __ASM_AVR32_UNALIGNED_H
#define __ASM_AVR32_UNALIGNED_H

/*
 * AVR32 can handle some unaligned accesses, depending on the
 * implementation.  The AVR32 AP implementation can handle unaligned
 * words, but halfwords must be halfword-aligned, and doublewords must
 * be word-aligned.
 */

#include <asm-generic/unaligned.h>

#ifdef CONFIG_CPU_AT32AP7000

/* REVISIT calling memmove() may be smaller for 64-bit values ... */

#undef get_unaligned
#define get_unaligned(ptr) \
	___get_unaligned(ptr, sizeof((*ptr)))
#define ___get_unaligned(ptr, size) \
	((size == 4) ? *(ptr) : __get_unaligned(ptr, size))

#undef put_unaligned
#define put_unaligned(val, ptr) \
	___put_unaligned((__u64)(val), ptr, sizeof((*ptr)))
#define ___put_unaligned(val, ptr, size)		\
do {							\
	if (size == 4)					\
		*(ptr) = (val);				\
	else						\
		__put_unaligned(val, ptr, size);	\
} while (0)

#endif

#endif /* __ASM_AVR32_UNALIGNED_H */
