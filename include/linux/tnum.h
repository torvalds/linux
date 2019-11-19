/* tnum: tracked (or tristate) numbers
 *
 * A tnum tracks knowledge about the bits of a value.  Each bit can be either
 * known (0 or 1), or unknown (x).  Arithmetic operations on tnums will
 * propagate the unknown bits such that the tnum result represents all the
 * possible results for possible values of the operands.
 */

#ifndef _LINUX_TNUM_H
#define _LINUX_TNUM_H

#include <linux/types.h>

struct tnum {
	u64 value;
	u64 mask;
};

/* Constructors */
/* Represent a known constant as a tnum. */
struct tnum tnum_const(u64 value);
/* A completely unknown value */
extern const struct tnum tnum_unknown;
/* A value that's unknown except that @min <= value <= @max */
struct tnum tnum_range(u64 min, u64 max);

/* Arithmetic and logical ops */
/* Shift a tnum left (by a fixed shift) */
struct tnum tnum_lshift(struct tnum a, u8 shift);
/* Shift (rsh) a tnum right (by a fixed shift) */
struct tnum tnum_rshift(struct tnum a, u8 shift);
/* Shift (arsh) a tnum right (by a fixed min_shift) */
struct tnum tnum_arshift(struct tnum a, u8 min_shift);
/* Add two tnums, return @a + @b */
struct tnum tnum_add(struct tnum a, struct tnum b);
/* Subtract two tnums, return @a - @b */
struct tnum tnum_sub(struct tnum a, struct tnum b);
/* Bitwise-AND, return @a & @b */
struct tnum tnum_and(struct tnum a, struct tnum b);
/* Bitwise-OR, return @a | @b */
struct tnum tnum_or(struct tnum a, struct tnum b);
/* Bitwise-XOR, return @a ^ @b */
struct tnum tnum_xor(struct tnum a, struct tnum b);
/* Multiply two tnums, return @a * @b */
struct tnum tnum_mul(struct tnum a, struct tnum b);

/* Return a tnum representing numbers satisfying both @a and @b */
struct tnum tnum_intersect(struct tnum a, struct tnum b);

/* Return @a with all but the lowest @size bytes cleared */
struct tnum tnum_cast(struct tnum a, u8 size);

/* Returns true if @a is a known constant */
static inline bool tnum_is_const(struct tnum a)
{
	return !a.mask;
}

/* Returns true if @a == tnum_const(@b) */
static inline bool tnum_equals_const(struct tnum a, u64 b)
{
	return tnum_is_const(a) && a.value == b;
}

/* Returns true if @a is completely unknown */
static inline bool tnum_is_unknown(struct tnum a)
{
	return !~a.mask;
}

/* Returns true if @a is known to be a multiple of @size.
 * @size must be a power of two.
 */
bool tnum_is_aligned(struct tnum a, u64 size);

/* Returns true if @b represents a subset of @a. */
bool tnum_in(struct tnum a, struct tnum b);

/* Formatting functions.  These have snprintf-like semantics: they will write
 * up to @size bytes (including the terminating NUL byte), and return the number
 * of bytes (excluding the terminating NUL) which would have been written had
 * sufficient space been available.  (Thus tnum_sbin always returns 64.)
 */
/* Format a tnum as a pair of hex numbers (value; mask) */
int tnum_strn(char *str, size_t size, struct tnum a);
/* Format a tnum as tristate binary expansion */
int tnum_sbin(char *str, size_t size, struct tnum a);

#endif /* _LINUX_TNUM_H */
