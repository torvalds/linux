/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_WORDPART_H
#define _LINUX_WORDPART_H
/**
 * REPEAT_BYTE - repeat the value @x multiple times as an unsigned long value
 * @x: value to repeat
 *
 * NOTE: @x is not checked for > 0xff; larger values produce odd results.
 */
#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#endif // _LINUX_WORDPART_H
