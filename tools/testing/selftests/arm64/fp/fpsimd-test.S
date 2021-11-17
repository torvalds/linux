// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2015-2019 ARM Limited.
// Original author: Dave Martin <Dave.Martin@arm.com>
//
// Simple FPSIMD context switch test
// Repeatedly writes unique test patterns into each FPSIMD register
// and reads them back to verify integrity.
//
// for x in `seq 1 NR_CPUS`; do fpsimd-test & pids=$pids\ $! ; done
// (leave it running for as long as you want...)
// kill $pids

#include <asm/unistd.h>
#include "assembler.h"
#include "asm-offsets.h"

#define NVR	32
#define MAXVL_B	(128 / 8)

.macro _vldr Vn:req, Xt:req
	ld1	{v\Vn\().2d}, [x\Xt]
.endm

.macro _vstr Vn:req, Xt:req
	st1	{v\Vn\().2d}, [x\Xt]
.endm

// Generate accessor functions to read/write programmatically selected
// FPSIMD registers.
// x0 is the register index to access
// x1 is the memory address to read from (getv,setp) or store to (setv,setp)
// All clobber x0-x2
define_accessor setv, NVR, _vldr
define_accessor getv, NVR, _vstr

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

// Utility macro to print a literal string
// Clobbers x0-x4,x8
.macro puts string
	.pushsection .rodata.str1.1, "aMS", 1
.L__puts_literal\@: .string "\string"
	.popsection

	ldr	x0, =.L__puts_literal\@
	bl	puts
.endm

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

// Print an unsigned decimal number x0 to stdout, followed by a newline
// Clobbers x0-x5,x8
function putdecn
	mov	x5, x30

	bl	putdec
	mov	x0, #'\n'
	bl	putc

	ret	x5
endfunction


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
// Clobbers x0-x2,x8
function puthexnibble
	and	w0, w0, #0xf
	cmp	w0, #10
	blo	1f
	add	w0, w0, #'a' - ('9' + 1)
1:	add	w0, w0, #'0'
	b	putc
endfunction

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

// Declare some storate space to shadow the SVE register contents:
.pushsection .text
.data
.align 4
vref:
	.space	MAXVL_B * NVR
scratch:
	.space	MAXVL_B
.popsection

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

// Generate a test pattern for storage in SVE registers
// x0: pid	(16 bits)
// x1: register number (6 bits)
// x2: generation (4 bits)
function pattern
	orr	w1, w0, w1, lsl #16
	orr	w2, w1, w2, lsl #28

	ldr	x0, =scratch
	mov	w1, #MAXVL_B / 4

0:	str	w2, [x0], #4
	add	w2, w2, #(1 << 22)
	subs	w1, w1, #1
	bne	0b

	ret
endfunction

// Get the address of shadow data for FPSIMD V-register V<xn>
.macro _adrv xd, xn, nrtmp
	ldr	\xd, =vref
	mov	x\nrtmp, #16
	madd	\xd, x\nrtmp, \xn, \xd
.endm

// Set up test pattern in a FPSIMD V-register
// x0: pid
// x1: register number
// x2: generation
function setup_vreg
	mov	x4, x30

	mov	x6, x1
	bl	pattern
	_adrv	x0, x6, 2
	mov	x5, x0
	ldr	x1, =scratch
	bl	memcpy

	mov	x0, x6
	mov	x1, x5
	bl	setv

	ret	x4
endfunction

// Fill x1 bytes starting at x0 with 0xae (for canary purposes)
// Clobbers x1, x2.
function memfill_ae
	mov	w2, #0xae
	b	memfill
endfunction

// Fill x1 bytes starting at x0 with 0.
// Clobbers x1, x2.
function memclr
	mov	w2, #0
endfunction
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

// Trivial memory compare: compare x2 bytes starting at address x0 with
// bytes starting at address x1.
// Returns only if all bytes match; otherwise, the program is aborted.
// Clobbers x0-x5.
function memcmp
	cbz	x2, 1f

	mov	x5, #0
0:	ldrb	w3, [x0, x5]
	ldrb	w4, [x1, x5]
	add	x5, x5, #1
	cmp	w3, w4
	b.ne	barf
	subs	x2, x2, #1
	b.ne	0b

1:	ret
endfunction

// Verify that a FPSIMD V-register matches its shadow in memory, else abort
// x0: reg number
// Clobbers x0-x5.
function check_vreg
	mov	x3, x30

	_adrv	x5, x0, 6
	mov	x4, x0
	ldr	x7, =scratch

	mov	x0, x7
	mov	x1, x6
	bl	memfill_ae

	mov	x0, x4
	mov	x1, x7
	bl	getv

	mov	x0, x5
	mov	x1, x7
	mov	x2, x6
	mov	x30, x3
	b	memcmp
endfunction

// Any SVE register modified here can cause corruption in the main
// thread -- but *only* the registers modified here.
function irritator_handler
	// Increment the irritation signal count (x23):
	ldr	x0, [x2, #ucontext_regs + 8 * 23]
	add	x0, x0, #1
	str	x0, [x2, #ucontext_regs + 8 * 23]

	// Corrupt some random V-regs
	adr	x0, .text + (irritator_handler - .text) / 16 * 16
	movi	v0.8b, #7
	movi	v9.16b, #9
	movi	v31.8b, #31

	ret
endfunction

function terminate_handler
	mov	w21, w0
	mov	x20, x2

	puts	"Terminated by signal "
	mov	w0, w21
	bl	putdec
	puts	", no error, iterations="
	ldr	x0, [x20, #ucontext_regs + 8 * 22]
	bl	putdec
	puts	", signals="
	ldr	x0, [x20, #ucontext_regs + 8 * 23]
	bl	putdecn

	mov	x0, #0
	mov	x8, #__NR_exit
	svc	#0
endfunction

// w0: signal number
// x1: sa_action
// w2: sa_flags
// Clobbers x0-x6,x8
function setsignal
	str	x30, [sp, #-((sa_sz + 15) / 16 * 16 + 16)]!

	mov	w4, w0
	mov	x5, x1
	mov	w6, w2

	add	x0, sp, #16
	mov	x1, #sa_sz
	bl	memclr

	mov	w0, w4
	add	x1, sp, #16
	str	w6, [x1, #sa_flags]
	str	x5, [x1, #sa_handler]
	mov	x2, #0
	mov	x3, #sa_mask_sz
	mov	x8, #__NR_rt_sigaction
	svc	#0

	cbz	w0, 1f

	puts	"sigaction failure\n"
	b	.Labort

1:	ldr	x30, [sp], #((sa_sz + 15) / 16 * 16 + 16)
	ret
endfunction

// Main program entry point
.globl _start
function _start
_start:
	// Sanity-check and report the vector length

	mov	x19, #128
	cmp	x19, #128
	b.lo	1f
	cmp	x19, #2048
	b.hi	1f
	tst	x19, #(8 - 1)
	b.eq	2f

1:	puts	"Bad vector length: "
	mov	x0, x19
	bl	putdecn
	b	.Labort

2:	puts	"Vector length:\t"
	mov	x0, x19
	bl	putdec
	puts	" bits\n"

	// Obtain our PID, to ensure test pattern uniqueness between processes

	mov	x8, #__NR_getpid
	svc	#0
	mov	x20, x0

	puts	"PID:\t"
	mov	x0, x20
	bl	putdecn

	mov	x23, #0		// Irritation signal count

	mov	w0, #SIGINT
	adr	x1, terminate_handler
	mov	w2, #SA_SIGINFO
	bl	setsignal

	mov	w0, #SIGTERM
	adr	x1, terminate_handler
	mov	w2, #SA_SIGINFO
	bl	setsignal

	mov	w0, #SIGUSR1
	adr	x1, irritator_handler
	mov	w2, #SA_SIGINFO
	orr	w2, w2, #SA_NODEFER
	bl	setsignal

	mov	x22, #0		// generation number, increments per iteration
.Ltest_loop:

	mov	x21, #0		// Set up V-regs & shadow with test pattern
0:	mov	x0, x20
	mov	x1, x21
	and	x2, x22, #0xf
	bl	setup_vreg
	add	x21, x21, #1
	cmp	x21, #NVR
	b.lo	0b

// Can't do this when SVE state is volatile across SVC:
	mov	x8, #__NR_sched_yield	// Encourage preemption
	svc	#0

	mov	x21, #0
0:	mov	x0, x21
	bl	check_vreg
	add	x21, x21, #1
	cmp	x21, #NVR
	b.lo	0b

	add	x22, x22, #1
	b	.Ltest_loop

.Labort:
	mov	x0, #0
	mov	x1, #SIGABRT
	mov	x8, #__NR_kill
	svc	#0
endfunction

function barf
	mov	x10, x0	// expected data
	mov	x11, x1	// actual data
	mov	x12, x2	// data size

	puts	"Mismatch: PID="
	mov	x0, x20
	bl	putdec
	puts	", iteration="
	mov	x0, x22
	bl	putdec
	puts	", reg="
	mov	x0, x21
	bl	putdecn
	puts	"\tExpected ["
	mov	x0, x10
	mov	x1, x12
	bl	dumphex
	puts	"]\n\tGot      ["
	mov	x0, x11
	mov	x1, x12
	bl	dumphex
	puts	"]\n"

	mov	x8, #__NR_exit
	mov	x1, #1
	svc	#0
endfunction
