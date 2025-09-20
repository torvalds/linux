// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2015-2019 ARM Limited.
// Original author: Dave Martin <Dave.Martin@arm.com>
//
// Simple Scalable Vector Extension context switch test
// Repeatedly writes unique test patterns into each SVE register
// and reads them back to verify integrity.
//
// for x in `seq 1 NR_CPUS`; do sve-test & pids=$pids\ $! ; done
// (leave it running for as long as you want...)
// kill $pids

#include <asm/unistd.h>
#include "assembler.h"
#include "asm-offsets.h"
#include "sme-inst.h"

#define NZR	32
#define NPR	16
#define MAXVL_B	(2048 / 8)

.arch_extension sve

.macro _sve_ldr_v zt, xn
	ldr	z\zt, [x\xn]
.endm

.macro _sve_str_v zt, xn
	str	z\zt, [x\xn]
.endm

.macro _sve_ldr_p pt, xn
	ldr	p\pt, [x\xn]
.endm

.macro _sve_str_p pt, xn
	str	p\pt, [x\xn]
.endm

// Generate accessor functions to read/write programmatically selected
// SVE registers.
// x0 is the register index to access
// x1 is the memory address to read from (getz,setp) or store to (setz,setp)
// All clobber x0-x2
define_accessor setz, NZR, _sve_ldr_v
define_accessor getz, NZR, _sve_str_v
define_accessor setp, NPR, _sve_ldr_p
define_accessor getp, NPR, _sve_str_p

// Declare some storate space to shadow the SVE register contents:
.pushsection .text
.data
.align 4
zref:
	.space	MAXVL_B * NZR
pref:
	.space	MAXVL_B / 8 * NPR
ffrref:
	.space	MAXVL_B / 8
scratch:
	.space	MAXVL_B
.popsection

// Generate a test pattern for storage in SVE registers
// x0: pid	(16 bits)
// x1: register number (6 bits)
// x2: generation (4 bits)

// These values are used to constuct a 32-bit pattern that is repeated in the
// scratch buffer as many times as will fit:
// bits 31:28	generation number (increments once per test_loop)
// bits 27:22	32-bit lane index
// bits 21:16	register number
// bits 15: 0	pid

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

// Get the address of shadow data for SVE Z-register Z<xn>
.macro _adrz xd, xn, nrtmp
	ldr	\xd, =zref
	rdvl	x\nrtmp, #1
	madd	\xd, x\nrtmp, \xn, \xd
.endm

// Get the address of shadow data for SVE P-register P<xn - NZR>
.macro _adrp xd, xn, nrtmp
	ldr	\xd, =pref
	rdvl	x\nrtmp, #1
	lsr	x\nrtmp, x\nrtmp, #3
	sub	\xn, \xn, #NZR
	madd	\xd, x\nrtmp, \xn, \xd
.endm

// Set up test pattern in a SVE Z-register
// x0: pid
// x1: register number
// x2: generation
function setup_zreg
	mov	x4, x30

	mov	x6, x1
	bl	pattern
	_adrz	x0, x6, 2
	mov	x5, x0
	ldr	x1, =scratch
	bl	memcpy

	mov	x0, x6
	mov	x1, x5
	bl	setz

	ret	x4
endfunction

// Set up test pattern in a SVE P-register
// x0: pid
// x1: register number
// x2: generation
function setup_preg
	mov	x4, x30

	mov	x6, x1
	bl	pattern
	_adrp	x0, x6, 2
	mov	x5, x0
	ldr	x1, =scratch
	bl	memcpy

	mov	x0, x6
	mov	x1, x5
	bl	setp

	ret	x4
endfunction

// Set up test pattern in the FFR
// x0: pid
// x2: generation
//
// We need to generate a canonical FFR value, which consists of a number of
// low "1" bits, followed by a number of zeros. This gives us 17 unique values
// per 16 bits of FFR, so we create a 4 bit signature out of the PID and
// generation, and use that as the initial number of ones in the pattern.
// We fill the upper lanes of FFR with zeros.
// Beware: corrupts P0.
function setup_ffr
#ifndef SSVE
	mov	x4, x30

	and	w0, w0, #0x3
	bfi	w0, w2, #2, #2
	mov	w1, #1
	lsl	w1, w1, w0
	sub	w1, w1, #1

	ldr	x0, =ffrref
	strh	w1, [x0], 2
	rdvl	x1, #1
	lsr	x1, x1, #3
	sub	x1, x1, #2
	bl	memclr

	mov	x0, #0
	ldr	x1, =ffrref
	bl	setp

	wrffr	p0.b

	ret	x4
#else
	ret
#endif
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

// Verify that a SVE Z-register matches its shadow in memory, else abort
// x0: reg number
// Clobbers x0-x7.
function check_zreg
	mov	x3, x30

	_adrz	x5, x0, 6
	mov	x4, x0
	ldr	x7, =scratch

	mov	x0, x7
	mov	x1, x6
	bl	memfill_ae

	mov	x0, x4
	mov	x1, x7
	bl	getz

	mov	x0, x5
	mov	x1, x7
	mov	x2, x6
	mov	x30, x3
	b	memcmp
endfunction

// Verify that a SVE P-register matches its shadow in memory, else abort
// x0: reg number
// Clobbers x0-x7.
function check_preg
	mov	x3, x30

	_adrp	x5, x0, 6
	mov	x4, x0
	ldr	x7, =scratch

	mov	x0, x7
	mov	x1, x6
	bl	memfill_ae

	mov	x0, x4
	mov	x1, x7
	bl	getp

	mov	x0, x5
	mov	x1, x7
	mov	x2, x6
	mov	x30, x3
	b	memcmp
endfunction

// Verify that the FFR matches its shadow in memory, else abort
// Beware -- corrupts P0.
// Clobbers x0-x5.
function check_ffr
#ifndef SSVE
	mov	x3, x30

	ldr	x4, =scratch
	rdvl	x5, #1
	lsr	x5, x5, #3

	mov	x0, x4
	mov	x1, x5
	bl	memfill_ae

	rdffr	p0.b
	mov	x0, #0
	mov	x1, x4
	bl	getp

	ldr	x0, =ffrref
	mov	x1, x4
	mov	x2, x5
	mov	x30, x3
	b	memcmp
#else
	ret
#endif
endfunction

// Modify live register state, the signal return will undo our changes
function irritator_handler
	// Increment the irritation signal count (x23):
	ldr	x0, [x2, #ucontext_regs + 8 * 23]
	add	x0, x0, #1
	str	x0, [x2, #ucontext_regs + 8 * 23]

	// Corrupt some random Z-regs
	movi	v0.8b, #1
	movi	v9.16b, #2
	movi	v31.8b, #3
	// And P0
	ptrue	p0.d
#ifndef SSVE
	// And FFR
	wrffr	p15.b
#endif

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

	mov	w0, #SIGUSR2
	adr	x1, tickle_handler
	mov	w2, #SA_SIGINFO
	orr	w2, w2, #SA_NODEFER
	bl	setsignal

#ifdef SSVE
	puts	"Streaming mode "
	smstart_sm
#endif

	// Sanity-check and report the vector length

	rdvl	x19, #8
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

#ifdef SSVE
	smstart_sm		// syscalls will have exited streaming mode
#endif

	mov	x22, #0		// generation number, increments per iteration
.Ltest_loop:
	rdvl	x0, #8
	cmp	x0, x19
	b.ne	vl_barf

	mov	x21, #0		// Set up Z-regs & shadow with test pattern
0:	mov	x0, x20
	mov	x1, x21
	and	x2, x22, #0xf
	bl	setup_zreg
	add	x21, x21, #1
	cmp	x21, #NZR
	b.lo	0b

	mov	x0, x20		// Set up FFR & shadow with test pattern
	mov	x1, #NZR + NPR
	and	x2, x22, #0xf
	bl	setup_ffr

0:	mov	x0, x20		// Set up P-regs & shadow with test pattern
	mov	x1, x21
	and	x2, x22, #0xf
	bl	setup_preg
	add	x21, x21, #1
	cmp	x21, #NZR + NPR
	b.lo	0b

// Can't do this when SVE state is volatile across SVC:
//	mov	x8, #__NR_sched_yield	// Encourage preemption
//	svc	#0

#ifdef SSVE
	mrs	x0, S3_3_C4_C2_2	// SVCR should have ZA=0,SM=1
	and	x1, x0, #3
	cmp	x1, #1
	b.ne	svcr_barf
#endif

	mov	x21, #0
0:	mov	x0, x21
	bl	check_zreg
	add	x21, x21, #1
	cmp	x21, #NZR
	b.lo	0b

0:	mov	x0, x21
	bl	check_preg
	add	x21, x21, #1
	cmp	x21, #NZR + NPR
	b.lo	0b

	bl	check_ffr

	add	x22, x22, #1
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
	mov	x10, x0	// expected data
	mov	x11, x1	// actual data
	mov	x12, x2	// data size

#ifdef SSVE
	mrs	x13, S3_3_C4_C2_2
#endif

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

#ifdef SSVE
	puts	"\tSVCR: "
	mov	x0, x13
	bl	putdecn
#endif

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
