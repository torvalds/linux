/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_WORDPART_H
#define _LINUX_WORDPART_H

/**
 * upper_32_bits - return bits 32-63 of a number
 * @n: the number we're accessing
 *
 * A basic shift-right of a 64- or 32-bit quantity.  Use this to suppress
 * the "right shift count >= width of type" warning when that quantity is
 * 32-bits.
 */
#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))

/**
 * lower_32_bits - return bits 0-31 of a number
 * @n: the number we're accessing
 */
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))

/**
 * upper_16_bits - return bits 16-31 of a number
 * @n: the number we're accessing
 */
#define upper_16_bits(n) ((u16)((n) >> 16))

/**
 * lower_16_bits - return bits 0-15 of a number
 * @n: the number we're accessing
 */
#define lower_16_bits(n) ((u16)((n) & 0xffff))

/**
 * REPEAT_BYTE - repeat the value @x multiple times as an unsigned long value
 * @x: value to repeat
 *
 * NOTE: @x is not checked for > 0xff; larger values produce odd results.
 */
#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#endif // _LINUX_WORDPART_H
