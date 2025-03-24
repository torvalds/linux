// Program that loops for ever doing lots of recursions and system calls,
// intended to be used as part of a stress test for GCS context switching.
//
// Copyright 2015-2023 Arm Ltd

#include <asm/unistd.h>

#define sa_sz 32
#define sa_flags 8
#define sa_handler 0
#define sa_mask_sz 8

#define si_code 8

#define SIGINT 2
#define SIGABRT 6
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGTERM 15
#define SEGV_CPERR 10

#define SA_NODEFER 1073741824
#define SA_SIGINFO 4
#define ucontext_regs 184

#define PR_SET_SHADOW_STACK_STATUS      75
# define PR_SHADOW_STACK_ENABLE         (1UL << 0)

#define	GCSPR_EL0 S3_3_C2_C5_1

.macro function name
	.macro endfunction
		.type \name, @function
		.purgem endfunction
	.endm
\name:
.endm

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

// Utility macro to print a literal string
// Clobbers x0-x4,x8
.macro puts string
	.pushsection .rodata.str1.1, "aMS", @progbits, 1
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
	b	abort

1:	ldr	x30, [sp], #((sa_sz + 15) / 16 * 16 + 16)
	ret
endfunction


function tickle_handler
	// Perhaps collect GCSPR_EL0 here in future?
	ret
endfunction

function terminate_handler
	mov	w21, w0
	mov	x20, x2

	puts	"Terminated by signal "
	mov	w0, w21
	bl	putdec
	puts	", no error\n"

	mov	x0, #0
	mov	x8, #__NR_exit
	svc	#0
endfunction

function segv_handler
	// stash the siginfo_t *
	mov	x20, x1

	// Disable GCS, we don't want additional faults logging things
	mov	x0, PR_SET_SHADOW_STACK_STATUS
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr
	mov	x5, xzr
	mov	x8, #__NR_prctl
	svc	#0

	puts	"Got SIGSEGV code "

	ldr	x21, [x20, #si_code]
	mov	x0, x21
	bl	putdec

	// GCS faults should have si_code SEGV_CPERR
	cmp	x21, #SEGV_CPERR
	bne	1f

	puts	" (GCS violation)"
1:
	mov	x0, '\n'
	bl	putc
	b	abort
endfunction

// Recurse x20 times
.macro recurse id
function recurse\id
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	cmp	x20, 0
	beq	1f
	sub	x20, x20, 1
	bl	recurse\id

1:
	ldp	x29, x30, [sp], #16

	// Do a syscall immediately prior to returning to try to provoke
	// scheduling and migration at a point where coherency issues
	// might trigger.
	mov	x8, #__NR_getpid
	svc	#0

	ret
endfunction
.endm

// Generate and use two copies so we're changing the GCS contents
recurse 1
recurse 2

.globl _start
function _start
	// Run with GCS
	mov	x0, PR_SET_SHADOW_STACK_STATUS
	mov	x1, PR_SHADOW_STACK_ENABLE
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr
	mov	x5, xzr
	mov	x8, #__NR_prctl
	svc	#0
	cbz	x0, 1f
	puts	"Failed to enable GCS\n"
	b	abort
1:

	mov	w0, #SIGTERM
	adr	x1, terminate_handler
	mov	w2, #SA_SIGINFO
	bl	setsignal

	mov	w0, #SIGUSR1
	adr	x1, tickle_handler
	mov	w2, #SA_SIGINFO
	orr	w2, w2, #SA_NODEFER
	bl	setsignal

	mov	w0, #SIGSEGV
	adr	x1, segv_handler
	mov	w2, #SA_SIGINFO
	orr	w2, w2, #SA_NODEFER
	bl	setsignal

	puts	"Running\n"

loop:
	// Small recursion depth so we're frequently flipping between
	// the two recursors and changing what's on the stack
	mov	x20, #5
	bl	recurse1
	mov	x20, #5
	bl	recurse2
	b	loop
endfunction

abort:
	mov	x0, #255
	mov	x8, #__NR_exit
	svc	#0
