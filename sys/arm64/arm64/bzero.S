/*-
 * Copyright (C) 2016 Cavium Inc.
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");


#include "assym.inc"

	/*
	 * void bzero(void *p, size_t size)
	 *
	 *  x0 - p
	 *  x1 - size
	 */
ENTRY(bzero)
	cbz	x1, ending

	/*
	 * x5 is number of cache lines to zero - calculated later and
	 * will become non-zero if  buffer is long enough to zero by
	 * cache lines (and if it is allowed.)
	 * We need to zero it before proceeding with buffers of size
	 * smaller than 16 bytes - otherwise the x5 will not be
	 * calculated and will retain random value.
	 * "normal" is used for buffers <= 16 bytes and to align buffer
	 * to cache line for buffers bigger than cache line; non-0 x5
	 * after "normal" has completed indicates that it has been used
	 * to align buffer to cache line and now zero by cache lines will
	 * be performed, and x5 is amount of cache lines to loop through.
	 */
	mov	x5, xzr

	/* No use of cache assisted zero for buffers with size <= 16 */
	cmp	x1, #0x10
	b.le	normal

	/*
	 * Load size of line that will be cleaned by dc zva call.
	 * 0 means that the instruction is not allowed
	 */
	ldr	x7, =dczva_line_size
	ldr	x7, [x7]
	cbz	x7, normal

	/*
	 * Buffer must be larger than cache line for using cache zeroing
	 * (and cache line aligned but this is checked after jump)
	 */
	cmp	x1, x7
	b.lt	normal

	/*
	 * Calculate number of bytes to cache aligned address (x4) nad
	 * number of full cache lines (x5). x6 is final address to zero.
	 */
	sub	x2, x7, #0x01
	mov	x3, -1
	eor	x3, x3, x2
	add	x4, x0, x2
	and	x4, x4, x3
	subs	x4, x4, x0
	b.eq	normal

	/* Calculate number of "lines" in buffer */
	sub	x5, x1, x4
	rbit	x2, x7
	clz	x2, x2
	lsr	x5, x5, x2

	/*
	 * If number of cache lines is 0, we will not be able to zero
	 * by cache lines, so go normal way.
	 */
	cbz	x5, normal
	/* x6 is final address to zero */
	add	x6, x0, x1

	/*
	 * We are here because x5 is non-0 so normal will be used to
	 * align buffer before cache zeroing. x4 holds number of bytes
	 * needed for alignment.
	 */
	mov	x1, x4

	/* When jumping here: x0 holds pointer, x1 holds size */
normal:
	/*
	 * Get buffer offset into 16 byte aligned address; 0 means pointer
	 * is aligned.
	 */
	ands	x2, x0, #0x0f
	b.eq	aligned_to_16
	/* Calculate one-byte loop runs to 8 byte aligned address. */
	ands	x2, x2, #0x07
	mov	x3, #0x08
	sub	x2, x3, x2
	/* x2 is number of bytes missing for alignment, x1 is buffer size */
	cmp	x1, x2
	csel	x2, x1, x2, le
	sub	x1, x1, x2

	/*
	 * Byte by byte copy will copy at least enough bytes to align
	 * pointer and at most "size".
	 */
align:
	strb	wzr, [x0], #0x01
	subs	x2, x2, #0x01
	b.ne	align

	/* Now pointer is aligned to 8 bytes */
	cmp	x1, #0x10
	b.lt	lead_out
	/* 
	 * Check if copy of another 8 bytes is needed to align to 16 byte
	 * address and do it
	 */
	tbz	x0, #0x03, aligned_to_16
	str	xzr, [x0], #0x08
	sub	x1, x1, #0x08

	/* While jumping here: x0 is 16 byte alligned address, x1 is size */
aligned_to_16:
	/* If size is less than 16 bytes, use lead_out to copy what remains */
	cmp	x1, #0x10
	b.lt	lead_out

	lsr	x2, x1, #0x04
zero_by_16:
	stp	xzr, xzr, [x0], #0x10
	subs	x2, x2, #0x01
	b.ne	zero_by_16

	/*
	 * Lead out requires addresses to be aligned to 8 bytes. It is used to
	 * zero buffers with sizes < 16 and what can not be zeroed by
	 * zero_by_16 loop.
	 */
	ands	x1, x1, #0x0f
	b.eq	lead_out_end
lead_out:
	tbz	x1, #0x03, lead_out_dword
	str	xzr, [x0], #0x08
lead_out_dword:
	tbz	x1, #0x02, lead_out_word
	str	wzr, [x0], #0x04
lead_out_word:
	tbz	x1, #0x01, lead_out_byte
	strh	wzr, [x0], #0x02
lead_out_byte:
	tbz	x1, #0x00, lead_out_end
	strb	wzr, [x0], #0x01

lead_out_end:
	/*
	 * If x5 is non-zero, this means that normal has been used as
	 * a lead in to align buffer address to cache size
	 */
	cbz	x5, ending

	/*
	 * Here x5 holds number of lines to zero; x6 is final address of
	 * buffer. x0 is cache line aligned pointer. x7 is cache line size
	 * in bytes
	 */
cache_line_zero:
	dc	zva, x0
	add	x0, x0, x7
	subs	x5, x5, #0x01
	b.ne	cache_line_zero

	/* Need to zero remaining bytes? */
	subs	x1, x6, x0
	b.ne	normal

ending:
	ret

END(bzero)

