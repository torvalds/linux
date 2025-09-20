// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2015-2021 ARM Limited.
// Original author: Dave Martin <Dave.Martin@arm.com>
//
// Utility functions for assembly code.

#include <asm/unistd.h>
#include "assembler.h"

// Print a single character x0 to stdout
// Clobbers x0-x2,x8
function putc
	str	x0, [sp, #-16]!

	mov	x0, #1			// STDOUT_FILENO
	mov	x1, sp
	mov	x2, #1
	mov	x8, #__NR_write
	svc	#0

	add	sp, sp, #16
	ret
endfunction
.globl	putc
	
// Print a NUL-terminated string starting at address x0 to stdout
// Clobbers x0-x3,x8
function puts
	mov	x1, x0

	mov	x2, #0
0:	ldrb	w3, [x0], #1
	cbz	w3, 1f
	add	x2, x2, #1
	b	0b

1:	mov	w0, #1			// STDOUT_FILENO
	mov	x8, #__NR_write
	svc	#0

	ret
endfunction
.globl	puts

// Print an unsigned decimal number x0 to stdout
// Clobbers x0-x4,x8
function putdec
	mov	x1, sp
	str	x30, [sp, #-32]!	// Result can't be > 20 digits

	mov	x2, #0
	strb	w2, [x1, #-1]!		// Write the NUL terminator

	mov	x2, #10
0:	udiv	x3, x0, x2		// div-mod loop to generate the digits
	msub	x0, x3, x2, x0
	add	w0, w0, #'0'
	strb	w0, [x1, #-1]!
	mov	x0, x3
	cbnz	x3, 0b

	ldrb	w0, [x1]
	cbnz	w0, 1f
	mov	w0, #'0'		// Print "0" for 0, not ""
	strb	w0, [x1, #-1]!

1:	mov	x0, x1
	bl	puts

	ldr	x30, [sp], #32
	ret
endfunction
.globl	putdec

// Print an unsigned decimal number x0 to stdout, followed by a newline
// Clobbers x0-x5,x8
function putdecn
	mov	x5, x30

	bl	putdec
	mov	x0, #'\n'
	bl	putc

	ret	x5
endfunction
.globl	putdecn

// Clobbers x0-x3,x8
function puthexb
	str	x30, [sp, #-0x10]!

	mov	w3, w0
	lsr	w0, w0, #4
	bl	puthexnibble
	mov	w0, w3

	ldr	x30, [sp], #0x10
	// fall through to puthexnibble
endfunction
.globl	puthexb

// Clobbers x0-x2,x8
function puthexnibble
	and	w0, w0, #0xf
	cmp	w0, #10
	blo	1f
	add	w0, w0, #'a' - ('9' + 1)
1:	add	w0, w0, #'0'
	b	putc
endfunction
.globl	puthexnibble

// x0=data in, x1=size in, clobbers x0-x5,x8
function dumphex
	str	x30, [sp, #-0x10]!

	mov	x4, x0
	mov	x5, x1

0:	subs	x5, x5, #1
	b.lo	1f
	ldrb	w0, [x4], #1
	bl	puthexb
	b	0b

1:	ldr	x30, [sp], #0x10
	ret
endfunction
.globl	dumphex

	// Trivial memory copy: copy x2 bytes, starting at address x1, to address x0.
// Clobbers x0-x3
function memcpy
	cmp	x2, #0
	b.eq	1f
0:	ldrb	w3, [x1], #1
	strb	w3, [x0], #1
	subs	x2, x2, #1
	b.ne	0b
1:	ret
endfunction
.globl	memcpy

// Fill x1 bytes starting at x0 with 0xae (for canary purposes)
// Clobbers x1, x2.
function memfill_ae
	mov	w2, #0xae
	b	memfill
endfunction
.globl	memfill_ae
	
// Fill x1 bytes starting at x0 with 0.
// Clobbers x1, x2.
function memclr
	mov	w2, #0
endfunction
.globl	memclr
	// fall through to memfill

// Trivial memory fill: fill x1 bytes starting at address x0 with byte w2
// Clobbers x1
function memfill
	cmp	x1, #0
	b.eq	1f

0:	strb	w2, [x0], #1
	subs	x1, x1, #1
	b.ne	0b

1:	ret
endfunction
.globl	memfill
