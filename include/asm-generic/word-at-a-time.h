#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

/*
 * This says "generic", but it's actually big-endian only.
 * Little-endian can use more efficient versions of these
 * interfaces, see for example
 *	 arch/x86/include/asm/word-at-a-time.h
 * for those.
 */

#include <linux/kernel.h>

struct word_at_a_time {
	const unsigned long high_bits, low_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0xfe) + 1, REPEAT_BYTE(0x7f) }

/* Bit set in the bytes that have a zero */
static inline long prep_zero_mask(unsigned long val, unsigned long rhs, const struct word_at_a_time *c)
{
	unsigned long mask = (val & c->low_bits) + c->low_bits;
	return ~(mask | rhs);
}

#define create_zero_mask(mask) (mask)

static inline long find_zero(unsigned long mask)
{
	long byte = 0;
#ifdef CONFIG_64BIT
	if (mask >> 32)
		mask >>= 32;
	else
		byte = 4;
#endif
	if (mask >> 16)
		mask >>= 16;
	else
		byte += 2;
	return (mask >> 8) ? byte : byte + 1;
}

static inline bool has_zero(unsigned long val, unsigned long *data, const struct word_at_a_time *c)
{
	unsigned long rhs = val | c->low_bits;
	*data = rhs;
	return (val + c->high_bits) & ~rhs;
}

#ifndef zero_bytemask
#define zero_bytemask(mask) (~1ul << __fls(mask))
#endif

#endif /* _ASM_WORD_AT_A_TIME_H */
