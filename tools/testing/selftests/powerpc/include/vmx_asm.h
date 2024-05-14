/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 */

#include "basic_asm.h"

/* POS MUST BE 16 ALIGNED! */
#define PUSH_VMX(pos,reg) \
	li	reg,pos; \
	stvx	v20,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v21,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v22,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v23,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v24,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v25,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v26,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v27,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v28,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v29,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v30,reg,%r1; \
	addi	reg,reg,16; \
	stvx	v31,reg,%r1;

/* POS MUST BE 16 ALIGNED! */
#define POP_VMX(pos,reg) \
	li	reg,pos; \
	lvx	v20,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v21,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v22,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v23,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v24,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v25,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v26,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v27,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v28,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v29,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v30,reg,%r1; \
	addi	reg,reg,16; \
	lvx	v31,reg,%r1;

/*
 * Careful this will 'clobber' vmx (by design)
 * Don't call this from C
 */
FUNC_START(load_vmx)
	li	r5,0
	lvx	v20,r5,r3
	addi	r5,r5,16
	lvx	v21,r5,r3
	addi	r5,r5,16
	lvx	v22,r5,r3
	addi	r5,r5,16
	lvx	v23,r5,r3
	addi	r5,r5,16
	lvx	v24,r5,r3
	addi	r5,r5,16
	lvx	v25,r5,r3
	addi	r5,r5,16
	lvx	v26,r5,r3
	addi	r5,r5,16
	lvx	v27,r5,r3
	addi	r5,r5,16
	lvx	v28,r5,r3
	addi	r5,r5,16
	lvx	v29,r5,r3
	addi	r5,r5,16
	lvx	v30,r5,r3
	addi	r5,r5,16
	lvx	v31,r5,r3
	blr
FUNC_END(load_vmx)
