/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BIND_PROG_H__
#define __BIND_PROG_H__

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define load_byte(src, b, s) \
	(((volatile __u8 *)&(src))[b] << 8 * b)
#define load_word(src, w, s) \
	(((volatile __u16 *)&(src))[w] << 16 * w)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define load_byte(src, b, s) \
	(((volatile __u8 *)&(src))[(b) + (sizeof(src) - (s))] << 8 * ((s) - (b) - 1))
#define load_word(src, w, s) \
	(((volatile __u16 *)&(src))[w] << 16 * (((s) / 2) - (w) - 1))
#else
# error "Fix your compiler's __BYTE_ORDER__?!"
#endif

#endif
