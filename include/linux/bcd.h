/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCD_H
#define _BCD_H

#include <linux/compiler.h>

#define bcd2bin(x)					\
		(__builtin_constant_p((u8 )(x)) ?	\
		const_bcd2bin(x) :			\
		_bcd2bin(x))

#define bin2bcd(x)					\
		(__builtin_constant_p((u8 )(x)) ?	\
		const_bin2bcd(x) :			\
		_bin2bcd(x))

#define const_bcd2bin(x)	(((x) & 0x0f) + ((x) >> 4) * 10)
#define const_bin2bcd(x)	((((x) / 10) << 4) + (x) % 10)

unsigned _bcd2bin(unsigned char val) __attribute_const__;
unsigned char _bin2bcd(unsigned val) __attribute_const__;

#endif /* _BCD_H */
