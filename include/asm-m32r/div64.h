#ifndef _ASM_M32R_DIV64
#define _ASM_M32R_DIV64

/* $Id$ */

/* unsigned long long division.
 * Input:
 *  unsigned long long  n
 *  unsigned long  base
 * Output:
 *  n = n / base;
 *  return value = n % base;
 */
#define do_div(n, base)						\
({								\
	unsigned long _res, _high, _mid, _low;			\
								\
	_low = (n) & 0xffffffffUL;				\
	_high = (n) >> 32;					\
	if (_high) {						\
		_mid = (_high % (unsigned long)(base)) << 16;	\
		_high = _high / (unsigned long)(base);		\
		_mid += _low >> 16;				\
		_low &= 0x0000ffffUL;				\
		_low += (_mid % (unsigned long)(base)) << 16;	\
		_mid = _mid / (unsigned long)(base);		\
		_res = _low % (unsigned long)(base);		\
		_low = _low / (unsigned long)(base);		\
		n = _low + ((long long)_mid << 16) +		\
			((long long)_high << 32);		\
	} else {						\
		_res = _low % (unsigned long)(base);		\
		n = (_low / (unsigned long)(base));		\
	}							\
	_res;							\
})

#endif  /* _ASM_M32R_DIV64 */
