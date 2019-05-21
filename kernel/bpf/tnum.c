// SPDX-License-Identifier: GPL-2.0-only
/* tnum: tracked (or tristate) numbers
 *
 * A tnum tracks knowledge about the bits of a value.  Each bit can be either
 * known (0 or 1), or unknown (x).  Arithmetic operations on tnums will
 * propagate the unknown bits such that the tnum result represents all the
 * possible results for possible values of the operands.
 */
#include <linux/kernel.h>
#include <linux/tnum.h>

#define TNUM(_v, _m)	(struct tnum){.value = _v, .mask = _m}
/* A completely unknown value */
const struct tnum tnum_unknown = { .value = 0, .mask = -1 };

struct tnum tnum_const(u64 value)
{
	return TNUM(value, 0);
}

struct tnum tnum_range(u64 min, u64 max)
{
	u64 chi = min ^ max, delta;
	u8 bits = fls64(chi);

	/* special case, needed because 1ULL << 64 is undefined */
	if (bits > 63)
		return tnum_unknown;
	/* e.g. if chi = 4, bits = 3, delta = (1<<3) - 1 = 7.
	 * if chi = 0, bits = 0, delta = (1<<0) - 1 = 0, so we return
	 *  constant min (since min == max).
	 */
	delta = (1ULL << bits) - 1;
	return TNUM(min & ~delta, delta);
}

struct tnum tnum_lshift(struct tnum a, u8 shift)
{
	return TNUM(a.value << shift, a.mask << shift);
}

struct tnum tnum_rshift(struct tnum a, u8 shift)
{
	return TNUM(a.value >> shift, a.mask >> shift);
}

struct tnum tnum_arshift(struct tnum a, u8 min_shift)
{
	/* if a.value is negative, arithmetic shifting by minimum shift
	 * will have larger negative offset compared to more shifting.
	 * If a.value is nonnegative, arithmetic shifting by minimum shift
	 * will have larger positive offset compare to more shifting.
	 */
	return TNUM((s64)a.value >> min_shift, (s64)a.mask >> min_shift);
}

struct tnum tnum_add(struct tnum a, struct tnum b)
{
	u64 sm, sv, sigma, chi, mu;

	sm = a.mask + b.mask;
	sv = a.value + b.value;
	sigma = sm + sv;
	chi = sigma ^ sv;
	mu = chi | a.mask | b.mask;
	return TNUM(sv & ~mu, mu);
}

struct tnum tnum_sub(struct tnum a, struct tnum b)
{
	u64 dv, alpha, beta, chi, mu;

	dv = a.value - b.value;
	alpha = dv + a.mask;
	beta = dv - b.mask;
	chi = alpha ^ beta;
	mu = chi | a.mask | b.mask;
	return TNUM(dv & ~mu, mu);
}

struct tnum tnum_and(struct tnum a, struct tnum b)
{
	u64 alpha, beta, v;

	alpha = a.value | a.mask;
	beta = b.value | b.mask;
	v = a.value & b.value;
	return TNUM(v, alpha & beta & ~v);
}

struct tnum tnum_or(struct tnum a, struct tnum b)
{
	u64 v, mu;

	v = a.value | b.value;
	mu = a.mask | b.mask;
	return TNUM(v, mu & ~v);
}

struct tnum tnum_xor(struct tnum a, struct tnum b)
{
	u64 v, mu;

	v = a.value ^ b.value;
	mu = a.mask | b.mask;
	return TNUM(v & ~mu, mu);
}

/* half-multiply add: acc += (unknown * mask * value).
 * An intermediate step in the multiply algorithm.
 */
static struct tnum hma(struct tnum acc, u64 value, u64 mask)
{
	while (mask) {
		if (mask & 1)
			acc = tnum_add(acc, TNUM(0, value));
		mask >>= 1;
		value <<= 1;
	}
	return acc;
}

struct tnum tnum_mul(struct tnum a, struct tnum b)
{
	struct tnum acc;
	u64 pi;

	pi = a.value * b.value;
	acc = hma(TNUM(pi, 0), a.mask, b.mask | b.value);
	return hma(acc, b.mask, a.value);
}

/* Note that if a and b disagree - i.e. one has a 'known 1' where the other has
 * a 'known 0' - this will return a 'known 1' for that bit.
 */
struct tnum tnum_intersect(struct tnum a, struct tnum b)
{
	u64 v, mu;

	v = a.value | b.value;
	mu = a.mask & b.mask;
	return TNUM(v & ~mu, mu);
}

struct tnum tnum_cast(struct tnum a, u8 size)
{
	a.value &= (1ULL << (size * 8)) - 1;
	a.mask &= (1ULL << (size * 8)) - 1;
	return a;
}

bool tnum_is_aligned(struct tnum a, u64 size)
{
	if (!size)
		return true;
	return !((a.value | a.mask) & (size - 1));
}

bool tnum_in(struct tnum a, struct tnum b)
{
	if (b.mask & ~a.mask)
		return false;
	b.value &= ~a.mask;
	return a.value == b.value;
}

int tnum_strn(char *str, size_t size, struct tnum a)
{
	return snprintf(str, size, "(%#llx; %#llx)", a.value, a.mask);
}
EXPORT_SYMBOL_GPL(tnum_strn);

int tnum_sbin(char *str, size_t size, struct tnum a)
{
	size_t n;

	for (n = 64; n; n--) {
		if (n < size) {
			if (a.mask & 1)
				str[n - 1] = 'x';
			else if (a.value & 1)
				str[n - 1] = '1';
			else
				str[n - 1] = '0';
		}
		a.mask >>= 1;
		a.value >>= 1;
	}
	str[min(size - 1, (size_t)64)] = 0;
	return 64;
}
