// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021-2 ARM Limited.
// Original author: Mark Brown <broonie@kernel.org>
//
// Scalable Matrix Extension ZT context switch test
// Repeatedly writes unique test patterns into ZT0
// and reads them back to verify integrity.

#include <asm/unistd.h>
#include "assembler.h"
#include "asm-offsets.h"
#include "sme-inst.h"

.arch_extension sve

#define ZT_SZ	512
#define ZT_B	(ZT_SZ / 8)

// Declare some storage space to shadow ZT register contents and a
// scratch buffer.
.pushsection .text
.data
.align 4
ztref:
	.space	ZT_B
scratch:
	.space	ZT_B
.popsection


// Generate a test pattern for storage in ZT
// x0: pid
// x1: generation

// These values are used to construct a 32-bit pattern that is repeated in the
// scratch buffer as many times as will fit:
// bits 31:24	generation number (increments once per test_loop)
// bits 23: 8	pid
// bits  7: 0	32-bit lane index

function pattern
	mov	w3, wzr
	bfi	w3, w0, #8, #16		// PID
	bfi	w3, w1, #24, #8		// Generation

	ldr	x0, =scratch
	mov	w1, #ZT_B / 4

0:	str	w3, [x0], #4
	add	w3, w3, #1		// Lane
	subs	w1, w1, #1
	b.ne	0b

	ret
endfunction

// Set up test pattern in a ZT horizontal vector
// x0: pid
// x1: generation
function setup_zt
	mov	x4, x30

	bl	pattern			// Get pattern in scratch buffer
	ldr	x0, =ztref
	ldr	x1, =scratch
	mov	x2, #ZT_B
	bl	memcpy

	ldr	x0, =ztref
	_ldr_zt 0			// load zt0 from pointer x0

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

// Verify that a ZT vector matches its shadow in memory, else abort
// Clobbers x0-x3
function check_zt
	mov	x3, x30

	ldr	x0, =scratch		// Poison scratch
	mov	x1, #ZT_B
	bl	memfill_ae

	ldr	x0, =scratch
	_str_zt 0

	ldr	x0, =ztref
	ldr	x1, =scratch
	mov	x2, #ZT_B
	mov	x30, x3
	b	memcmp
endfunction

// Modify the live SME register state, signal return will undo our changes
function irritator_handler
	// Increment the irritation signal count (x23):
	ldr	x0, [x2, #ucontext_regs + 8 * 23]
	add	x0, x0, #1
	str	x0, [x2, #ucontext_regs + 8 * 23]

	// This will reset ZT to all bits 0
	smstop
	smstart_za

	ret
endfunction

function tickle_handler
	// Increment the signal count (x23):
	ldr	x0, [x2, #ucontext_regs + 8 * 23]
	add	x0, x0, #1
	str	x0, [x2, #ucontext_regs + 8 * 23]

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
	enable_gcs

	mov	x23, #0		// signal count

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

	mov	w0, #SIGUSR2
	adr	x1, tickle_handler
	mov	w2, #SA_SIGINFO
	orr	w2, w2, #SA_NODEFER
	bl	setsignal

	smstart_za

	// Obtain our PID, to ensure test pattern uniqueness between processes
	mov	x8, #__NR_getpid
	svc	#0
	mov	x20, x0

	puts	"PID:\t"
	mov	x0, x20
	bl	putdecn

	mov	x22, #0		// generation number, increments per iteration
.Ltest_loop:
	mov	x0, x20
	mov	x1, x22
	bl	setup_zt

	mov	x8, #__NR_sched_yield	// Encourage preemption
	svc	#0

	mrs	x0, S3_3_C4_C2_2	// SVCR should have ZA=1,SM=0
	and	x1, x0, #3
	cmp	x1, #2
	b.ne	svcr_barf

	bl	check_zt

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

	mrs	x13, S3_3_C4_C2_2
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
	puts	"\tExpected ["
	mov	x0, x10
	mov	x1, x12
	bl	dumphex
	puts	"]\n\tGot      ["
	mov	x0, x11
	mov	x1, x12
	bl	dumphex
	puts	"]\n"
	puts	"\tSVCR: "
	mov	x0, x13
	bl	putdecn

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

function svcr_barf
	mov	x10, x0

	puts	"Bad SVCR: "
	mov	x0, x10
	bl	putdecn

	mov	x8, #__NR_exit
	mov	x1, #1
	svc	#0
endfunction
