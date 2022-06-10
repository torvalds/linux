// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021 ARM Limited.
// Original author: Mark Brown <broonie@kernel.org>
//
// Scalable Matrix Extension ZA context switch test
// Repeatedly writes unique test patterns into each ZA tile
// and reads them back to verify integrity.
//
// for x in `seq 1 NR_CPUS`; do sve-test & pids=$pids\ $! ; done
// (leave it running for as long as you want...)
// kill $pids

#include <asm/unistd.h>
#include "assembler.h"
#include "asm-offsets.h"
#include "sme-inst.h"

.arch_extension sve

#define MAXVL     2048
#define MAXVL_B   (MAXVL / 8)

// Declare some storage space to shadow ZA register contents and a
// scratch buffer for a vector.
.pushsection .text
.data
.align 4
zaref:
	.space	MAXVL_B * MAXVL_B
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

// Generate a test pattern for storage in ZA
// x0: pid
// x1: row in ZA
// x2: generation

// These values are used to constuct a 32-bit pattern that is repeated in the
// scratch buffer as many times as will fit:
// bits 31:28	generation number (increments once per test_loop)
// bits 27:16	pid
// bits 15: 8	row number
// bits  7: 0	32-bit lane index

function pattern
	mov	w3, wzr
	bfi	w3, w0, #16, #12	// PID
	bfi	w3, w1, #8, #8		// Row
	bfi	w3, w2, #28, #4		// Generation

	ldr	x0, =scratch
	mov	w1, #MAXVL_B / 4

0:	str	w3, [x0], #4
	add	w3, w3, #1		// Lane
	subs	w1, w1, #1
	b.ne	0b

	ret
endfunction

// Get the address of shadow data for ZA horizontal vector xn
.macro _adrza xd, xn, nrtmp
	ldr	\xd, =zaref
	rdsvl	\nrtmp, 1
	madd	\xd, x\nrtmp, \xn, \xd
.endm

// Set up test pattern in a ZA horizontal vector
// x0: pid
// x1: row number
// x2: generation
function setup_za
	mov	x4, x30
	mov	x12, x1			// Use x12 for vector select

	bl	pattern			// Get pattern in scratch buffer
	_adrza	x0, x12, 2		// Shadow buffer pointer to x0 and x5
	mov	x5, x0
	ldr	x1, =scratch
	bl	memcpy			// length set up in x2 by _adrza

	_ldr_za 12, 5			// load vector w12 from pointer x5

	ret	x4
endfunction

// Trivial memory compare: compare x2 bytes starting at address x0 with
// bytes starting at address x1.
// Returns only if all bytes match; otherwise, the program is aborted.
// Clobbers x0-x5.
function memcmp
	cbz	x2, 2f

	stp	x0, x1, [sp, #-0x20]!
	str	x2, [sp, #0x10]

	mov	x5, #0
0:	ldrb	w3, [x0, x5]
	ldrb	w4, [x1, x5]
	add	x5, x5, #1
	cmp	w3, w4
	b.ne	1f
	subs	x2, x2, #1
	b.ne	0b

1:	ldr	x2, [sp, #0x10]
	ldp	x0, x1, [sp], #0x20
	b.ne	barf

2:	ret
endfunction

// Verify that a ZA vector matches its shadow in memory, else abort
// x0: row number
// Clobbers x0-x7 and x12.
function check_za
	mov	x3, x30

	mov	x12, x0
	_adrza	x5, x0, 6		// pointer to expected value in x5
	mov	x4, x0
	ldr	x7, =scratch		// x7 is scratch

	mov	x0, x7			// Poison scratch
	mov	x1, x6
	bl	memfill_ae

	_str_za 12, 7			// save vector w12 to pointer x7

	mov	x0, x5
	mov	x1, x7
	mov	x2, x6
	mov	x30, x3
	b	memcmp
endfunction

// Any SME register modified here can cause corruption in the main
// thread -- but *only* the locations modified here.
function irritator_handler
	// Increment the irritation signal count (x23):
	ldr	x0, [x2, #ucontext_regs + 8 * 23]
	add	x0, x0, #1
	str	x0, [x2, #ucontext_regs + 8 * 23]

	// Corrupt some random ZA data
#if 0
	adr	x0, .text + (irritator_handler - .text) / 16 * 16
	movi	v0.8b, #1
	movi	v9.16b, #2
	movi	v31.8b, #3
#endif

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
	puts	"Streaming mode "
	smstart_za

	// Sanity-check and report the vector length

	rdsvl	19, 8
	cmp	x19, #128
	b.lo	1f
	cmp	x19, #2048
	b.hi	1f
	tst	x19, #(8 - 1)
	b.eq	2f

1:	puts	"bad vector length: "
	mov	x0, x19
	bl	putdecn
	b	.Labort

2:	puts	"vector length:\t"
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
	rdsvl	0, 8
	cmp	x0, x19
	b.ne	vl_barf

	rdsvl	21, 1		// Set up ZA & shadow with test pattern
0:	mov	x0, x20
	sub	x1, x21, #1
	mov	x2, x22
	bl	setup_za
	subs	x21, x21, #1
	b.ne	0b

	and	x8, x22, #127		// Every 128 interations...
	cbz	x8, 0f
	mov	x8, #__NR_getpid	// (otherwise minimal syscall)
	b	1f
0:
	mov	x8, #__NR_sched_yield	// ...encourage preemption
1:
	svc	#0

	mrs	x0, S3_3_C4_C2_2	// SVCR should have ZA=1,SM=0
	and	x1, x0, #3
	cmp	x1, #2
	b.ne	svcr_barf

	rdsvl	21, 1			// Verify that the data made it through
	rdsvl	24, 1			// Verify that the data made it through
0:	sub	x0, x24, x21
	bl	check_za
	subs	x21, x21, #1
	bne	0b

	add	x22, x22, #1	// Everything still working
	b	.Ltest_loop

.Labort:
	mov	x0, #0
	mov	x1, #SIGABRT
	mov	x8, #__NR_kill
	svc	#0
endfunction

function barf
// fpsimd.c acitivty log dump hack
//	ldr	w0, =0xdeadc0de
//	mov	w8, #__NR_exit
//	svc	#0
// end hack
	smstop
	mov	x10, x0	// expected data
	mov	x11, x1	// actual data
	mov	x12, x2	// data size

	puts	"Mismatch: PID="
	mov	x0, x20
	bl	putdec
	puts	", iteration="
	mov	x0, x22
	bl	putdec
	puts	", row="
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

	mov	x8, #__NR_getpid
	svc	#0
// fpsimd.c acitivty log dump hack
//	ldr	w0, =0xdeadc0de
//	mov	w8, #__NR_exit
//	svc	#0
// ^ end of hack
	mov	x1, #SIGABRT
	mov	x8, #__NR_kill
	svc	#0
//	mov	x8, #__NR_exit
//	mov	x1, #1
//	svc	#0
endfunction

function vl_barf
	mov	x10, x0

	puts	"Bad active VL: "
	mov	x0, x10
	bl	putdecn

	mov	x8, #__NR_exit
	mov	x1, #1
	svc	#0
endfunction

function svcr_barf
	mov	x10, x0

	puts	"Bad SVCR: "
	mov	x0, x10
	bl	putdecn

	mov	x8, #__NR_exit
	mov	x1, #1
	svc	#0
endfunction
