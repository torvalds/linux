/*	$OpenBSD: bitstring.h,v 1.7 2024/08/26 11:52:54 bluhm Exp $	*/
/*	$NetBSD: bitstring.h,v 1.5 1997/05/14 15:49:55 pk Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Vixie.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bitstring.h	8.1 (Berkeley) 7/19/93
 */

#ifndef _BITSTRING_H_
#define	_BITSTRING_H_

/* modified for SV/AT and bitstring bugfix by M.R.Murphy, 11oct91
 * bitstr_size changed gratuitously, but shorter
 * bit_alloc   spelling error fixed
 * the following were efficient, but didn't work, they've been made to
 * work, but are no longer as efficient :-)
 * bit_nclear, bit_nset, bit_ffc, bit_ffs
 */
typedef	unsigned char bitstr_t;

/* internal macros */
				/* byte of the bitstring bit is in */
#define	_bit_byte(bit) \
	((bit) >> 3)

				/* mask for the bit within its byte */
#define	_bit_mask(bit) \
	(1 << ((bit)&0x7))

/* external macros */
				/* bytes in a bitstring of nbits bits */
#define	bitstr_size(nbits) \
	(((nbits) + 7) >> 3)

				/* allocate a bitstring */
#define	bit_alloc(nbits) \
	(bitstr_t *)calloc((size_t)bitstr_size(nbits), sizeof(bitstr_t))

				/* allocate a bitstring on the stack */
#define	bit_decl(name, nbits) \
	((name)[bitstr_size(nbits)])

				/* is bit N of bitstring name set? */
#define	bit_test(name, bit) ({ \
	register int __tbit = (bit); \
	((name)[_bit_byte(__tbit)] & _bit_mask(__tbit)); \
})

				/* set bit N of bitstring name */
#define	bit_set(name, bit) do { \
	register int __sbit = (bit); \
	((name)[_bit_byte(__sbit)] |= _bit_mask(__sbit)); \
} while(0)

				/* clear bit N of bitstring name */
#define	bit_clear(name, bit) do { \
	register int __cbit = (bit); \
	((name)[_bit_byte(__cbit)] &= ~_bit_mask(__cbit)); \
} while(0)

				/* clear bits start ... stop in bitstring */
#define	bit_nclear(name, start, stop) do { \
	register bitstr_t *__name = (name); \
	register int __start = (start), __stop = (stop); \
	while (__start <= __stop) { \
		bit_clear(__name, __start); \
		__start++; \
		} \
} while(0)

				/* set bits start ... stop in bitstring */
#define	bit_nset(name, start, stop) do { \
	register bitstr_t *__name = (name); \
	register int __start = (start), __stop = (stop); \
	while (__start <= __stop) { \
		bit_set(__name, __start); \
		__start++; \
		} \
} while(0)

				/* find first bit clear in name */
#define	bit_ffc(name, nbits, value) do { \
	register bitstr_t *__name = (name); \
	register int __bit, __nbits = (nbits), __value = -1; \
	for (__bit = 0; __bit < __nbits; ++__bit) \
		if (!bit_test(__name, __bit)) { \
			__value = __bit; \
			break; \
		} \
	*(value) = __value; \
} while(0)

				/* find first bit set in name */
#define	bit_ffs(name, nbits, value) do { \
	register bitstr_t *__name = (name); \
	register int __bit, __nbits = (nbits), __value = -1; \
	for (__bit = 0; __bit < __nbits; ++__bit) \
		if (bit_test(__name, __bit)) { \
			__value = __bit; \
			break; \
		} \
	*(value) = __value; \
} while(0)

#endif /* !_BITSTRING_H_ */
