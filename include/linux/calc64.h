#ifndef _LINUX_CALC64_H
#define _LINUX_CALC64_H

#include <linux/types.h>
#include <asm/div64.h>

/*
 * This is a generic macro which is used when the architecture
 * specific div64.h does not provide a optimized one.
 *
 * The 64bit dividend is divided by the divisor (data type long), the
 * result is returned and the remainder stored in the variable
 * referenced by remainder (data type long *). In contrast to the
 * do_div macro the dividend is kept intact.
 */
#ifndef div_long_long_rem
#define div_long_long_rem(dividend, divisor, remainder)	\
	do_div_llr((dividend), divisor, remainder)

static inline unsigned long do_div_llr(const long long dividend,
				       const long divisor, long *remainder)
{
	u64 result = dividend;

	*(remainder) = do_div(result, divisor);
	return (unsigned long) result;
}
#endif

/*
 * Sign aware variation of the above. On some architectures a
 * negative dividend leads to an divide overflow exception, which
 * is avoided by the sign check.
 */
static inline long div_long_long_rem_signed(const long long dividend,
					    const long divisor, long *remainder)
{
	long res;

	if (unlikely(dividend < 0)) {
		res = -div_long_long_rem(-dividend, divisor, remainder);
		*remainder = -(*remainder);
	} else
		res = div_long_long_rem(dividend, divisor, remainder);

	return res;
}

#endif
