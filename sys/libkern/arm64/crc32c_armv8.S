/*-
 * Copyright (c) 2017 Michael Tuexen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");
.arch armv8-a+crc

/*
 * uint32_t
 * armv8_crc32c(uint32_t crc, const unsigned char *buf, unsigned int len)
 */

ENTRY(armv8_crc32c)
	cbz	w2, end
	tbz	x1, #0x0, half_word_aligned
	sub	w2, w2, 0x1
	ldr	w10, [x1], #0x1
	crc32cb	w0, w0, w10
half_word_aligned:
	cmp	w2, #0x2
	b.lo	last_byte
	tbz	x1, #0x1, word_aligned
	sub	w2, w2, 0x2
	ldr	w10, [x1], #0x2
	crc32ch	w0, w0, w10
word_aligned:
	cmp	w2, #0x4
	b.lo	last_half_word
	tbz	x1, #0x2, double_word_aligned
	sub	w2, w2, 0x4
	ldr	w10, [x1], #0x4
	crc32cw	w0, w0, w10
double_word_aligned:
	lsr	w9, w2, #0x3
	cbz	w9, last_word
loop:
	ldr	x10, [x1], #0x8
	crc32cx	w0, w0, x10
	subs	w9, w9, #1
	b.ne	loop
last_word:
	tbz	w2, #0x2, last_half_word
	ldr	w10, [x1], #0x4
	crc32cw	w0, w0, w10
last_half_word:
	tbz	w2, #0x1, last_byte
	ldr	w10, [x1], #0x2
	crc32ch	w0, w0, w10
last_byte:
	tbz	w2, #0x0, end 
	ldr	w10, [x1], #0x1
	crc32cb	w0, w0, w10
end:
	ret
END(armv8_crc32c)
