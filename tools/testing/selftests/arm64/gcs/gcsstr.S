// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright 2024 Arm Limited
//
// Give ourselves GCS write permissions then use them

#include <asm/unistd.h>

/* Shadow Stack/Guarded Control Stack interface */
#define PR_GET_SHADOW_STACK_STATUS	74
#define PR_SET_SHADOW_STACK_STATUS      75
#define PR_LOCK_SHADOW_STACK_STATUS     76

# define PR_SHADOW_STACK_ENABLE         (1UL << 0)
# define PR_SHADOW_STACK_WRITE		(1UL << 1)
# define PR_SHADOW_STACK_PUSH		(1UL << 2)

#define	GCSPR_EL0 S3_3_C2_C5_1

#define KSFT_SKIP 4

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

.globl _start
function _start
	// Run with GCS
	mov	x0, PR_SET_SHADOW_STACK_STATUS
	mov	x1, PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_WRITE
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr
	mov	x5, xzr
	mov	x8, #__NR_prctl
	svc	#0
	cbz	x0, 1f
	puts	"Failed to enable GCS with write permission\n"
	mov	x0, #KSFT_SKIP
	b	2f
1:
	mrs	x0, GCSPR_EL0
	sub	x0, x0, #8
	.inst	0xd91f1c01	// GCSSTR x1, x0

	mov	x0, #0
2:
	mov	x8, #__NR_exit
	svc	#0
