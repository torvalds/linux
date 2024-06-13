/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <ppc-asm.h>
#include "reg.h"


/* ppc-asm.h defines most of the reg aliases, but not r1/r2. */
#define r1 1
#define r2 2

#define RFEBB   .long 0x4c000924

/* Stack layout:
 *
 *                   ^
 *  User stack       |
 *  Back chain ------+	<- r1		<-------+
 *  ...						|
 *  Red zone / ABI Gap				|
 *  ...						|
 *  vr63	<+				|
 *  vr0		 |				|
 *  VSCR	 |				|
 *  FSCR	 |				|
 *  r31		 | Save area			|
 *  r0		 |				|
 *  XER		 |				|
 *  CTR		 |				|
 *  LR		 |				|
 *  CCR		<+				|
 *  ...		<+				|
 *  LR		 | Caller frame			|
 *  CCR		 |				|
 *  Back chain	<+	<- updated r1	--------+
 *
 */

#if defined(_CALL_ELF) && _CALL_ELF == 2
#define ABIGAP		512
#else
#define ABIGAP		288
#endif

#define NR_GPR		32
#define NR_SPR		6
#define NR_VSR		64

#define SAVE_AREA	((NR_GPR + NR_SPR) * 8 + (NR_VSR * 16))
#define CALLER_FRAME	112

#define STACK_FRAME	(ABIGAP + SAVE_AREA + CALLER_FRAME)

#define CCR_SAVE	(CALLER_FRAME)
#define LR_SAVE		(CCR_SAVE + 8)
#define CTR_SAVE	(LR_SAVE  + 8)
#define XER_SAVE	(CTR_SAVE + 8)
#define GPR_SAVE(n)	(XER_SAVE + 8 + (8 * n))
#define FSCR_SAVE	(GPR_SAVE(31) + 8)
#define VSCR_SAVE	(FSCR_SAVE + 8)
#define VSR_SAVE(n)	(VSCR_SAVE + 8 + (16 * n))

#define SAVE_GPR(n)	std n,GPR_SAVE(n)(r1)
#define REST_GPR(n)	ld  n,GPR_SAVE(n)(r1)
#define TRASH_GPR(n)	lis n,0xaaaa

#define SAVE_VSR(n, b)	li b, VSR_SAVE(n); stxvd2x n,b,r1
#define LOAD_VSR(n, b)	li b, VSR_SAVE(n); lxvd2x  n,b,r1

#define LOAD_REG_IMMEDIATE(reg,expr)	\
	lis     reg,(expr)@highest;	\
	ori     reg,reg,(expr)@higher;	\
	rldicr  reg,reg,32,31;		\
	oris    reg,reg,(expr)@h;	\
	ori     reg,reg,(expr)@l;


#if defined(_CALL_ELF) && _CALL_ELF == 2
#define ENTRY_POINT(name) \
	.type FUNC_NAME(name),@function; \
	.globl FUNC_NAME(name); \
	FUNC_NAME(name):

#define RESTORE_TOC(name)	\
	/* Restore our TOC pointer using our entry point */	\
	LOAD_REG_IMMEDIATE(r12, name)				\
0:	addis	r2,r12,(.TOC.-0b)@ha;				\
	addi	r2,r2,(.TOC.-0b)@l;

#else
#define ENTRY_POINT(name) FUNC_START(name)
#define RESTORE_TOC(name)	\
	/* Restore our TOC pointer via our opd entry */	\
	LOAD_REG_IMMEDIATE(r2, name)			\
	ld      r2,8(r2);
#endif

    .text

ENTRY_POINT(ebb_handler)
    stdu    r1,-STACK_FRAME(r1)
    SAVE_GPR(0)
    mflr    r0
    std     r0,LR_SAVE(r1)
    mfcr    r0
    std     r0,CCR_SAVE(r1)
    mfctr   r0
    std     r0,CTR_SAVE(r1)
    mfxer   r0
    std     r0,XER_SAVE(r1)
    SAVE_GPR(2)
    SAVE_GPR(3)
    SAVE_GPR(4)
    SAVE_GPR(5)
    SAVE_GPR(6)
    SAVE_GPR(7)
    SAVE_GPR(8)
    SAVE_GPR(9)
    SAVE_GPR(10)
    SAVE_GPR(11)
    SAVE_GPR(12)
    SAVE_GPR(13)
    SAVE_GPR(14)
    SAVE_GPR(15)
    SAVE_GPR(16)
    SAVE_GPR(17)
    SAVE_GPR(18)
    SAVE_GPR(19)
    SAVE_GPR(20)
    SAVE_GPR(21)
    SAVE_GPR(22)
    SAVE_GPR(23)
    SAVE_GPR(24)
    SAVE_GPR(25)
    SAVE_GPR(26)
    SAVE_GPR(27)
    SAVE_GPR(28)
    SAVE_GPR(29)
    SAVE_GPR(30)
    SAVE_GPR(31)
    SAVE_VSR(0, r3)
    mffs     f0
    stfd     f0, FSCR_SAVE(r1)
    mfvscr   f0
    stfd     f0, VSCR_SAVE(r1)
    SAVE_VSR(1,  r3)
    SAVE_VSR(2,  r3)
    SAVE_VSR(3,  r3)
    SAVE_VSR(4,  r3)
    SAVE_VSR(5,  r3)
    SAVE_VSR(6,  r3)
    SAVE_VSR(7,  r3)
    SAVE_VSR(8,  r3)
    SAVE_VSR(9,  r3)
    SAVE_VSR(10, r3)
    SAVE_VSR(11, r3)
    SAVE_VSR(12, r3)
    SAVE_VSR(13, r3)
    SAVE_VSR(14, r3)
    SAVE_VSR(15, r3)
    SAVE_VSR(16, r3)
    SAVE_VSR(17, r3)
    SAVE_VSR(18, r3)
    SAVE_VSR(19, r3)
    SAVE_VSR(20, r3)
    SAVE_VSR(21, r3)
    SAVE_VSR(22, r3)
    SAVE_VSR(23, r3)
    SAVE_VSR(24, r3)
    SAVE_VSR(25, r3)
    SAVE_VSR(26, r3)
    SAVE_VSR(27, r3)
    SAVE_VSR(28, r3)
    SAVE_VSR(29, r3)
    SAVE_VSR(30, r3)
    SAVE_VSR(31, r3)
    SAVE_VSR(32, r3)
    SAVE_VSR(33, r3)
    SAVE_VSR(34, r3)
    SAVE_VSR(35, r3)
    SAVE_VSR(36, r3)
    SAVE_VSR(37, r3)
    SAVE_VSR(38, r3)
    SAVE_VSR(39, r3)
    SAVE_VSR(40, r3)
    SAVE_VSR(41, r3)
    SAVE_VSR(42, r3)
    SAVE_VSR(43, r3)
    SAVE_VSR(44, r3)
    SAVE_VSR(45, r3)
    SAVE_VSR(46, r3)
    SAVE_VSR(47, r3)
    SAVE_VSR(48, r3)
    SAVE_VSR(49, r3)
    SAVE_VSR(50, r3)
    SAVE_VSR(51, r3)
    SAVE_VSR(52, r3)
    SAVE_VSR(53, r3)
    SAVE_VSR(54, r3)
    SAVE_VSR(55, r3)
    SAVE_VSR(56, r3)
    SAVE_VSR(57, r3)
    SAVE_VSR(58, r3)
    SAVE_VSR(59, r3)
    SAVE_VSR(60, r3)
    SAVE_VSR(61, r3)
    SAVE_VSR(62, r3)
    SAVE_VSR(63, r3)

    TRASH_GPR(2)
    TRASH_GPR(3)
    TRASH_GPR(4)
    TRASH_GPR(5)
    TRASH_GPR(6)
    TRASH_GPR(7)
    TRASH_GPR(8)
    TRASH_GPR(9)
    TRASH_GPR(10)
    TRASH_GPR(11)
    TRASH_GPR(12)
    TRASH_GPR(14)
    TRASH_GPR(15)
    TRASH_GPR(16)
    TRASH_GPR(17)
    TRASH_GPR(18)
    TRASH_GPR(19)
    TRASH_GPR(20)
    TRASH_GPR(21)
    TRASH_GPR(22)
    TRASH_GPR(23)
    TRASH_GPR(24)
    TRASH_GPR(25)
    TRASH_GPR(26)
    TRASH_GPR(27)
    TRASH_GPR(28)
    TRASH_GPR(29)
    TRASH_GPR(30)
    TRASH_GPR(31)

    RESTORE_TOC(ebb_handler)

    /*
     * r13 is our TLS pointer. We leave whatever value was in there when the
     * EBB fired. That seems to be OK because once set the TLS pointer is not
     * changed - but presumably that could change in future.
     */

    bl      ebb_hook
    nop

    /* r2 may be changed here but we don't care */

    lfd      f0, FSCR_SAVE(r1)
    mtfsf    0xff,f0
    lfd      f0, VSCR_SAVE(r1)
    mtvscr   f0
    LOAD_VSR(0, r3)
    LOAD_VSR(1,  r3)
    LOAD_VSR(2,  r3)
    LOAD_VSR(3,  r3)
    LOAD_VSR(4,  r3)
    LOAD_VSR(5,  r3)
    LOAD_VSR(6,  r3)
    LOAD_VSR(7,  r3)
    LOAD_VSR(8,  r3)
    LOAD_VSR(9,  r3)
    LOAD_VSR(10, r3)
    LOAD_VSR(11, r3)
    LOAD_VSR(12, r3)
    LOAD_VSR(13, r3)
    LOAD_VSR(14, r3)
    LOAD_VSR(15, r3)
    LOAD_VSR(16, r3)
    LOAD_VSR(17, r3)
    LOAD_VSR(18, r3)
    LOAD_VSR(19, r3)
    LOAD_VSR(20, r3)
    LOAD_VSR(21, r3)
    LOAD_VSR(22, r3)
    LOAD_VSR(23, r3)
    LOAD_VSR(24, r3)
    LOAD_VSR(25, r3)
    LOAD_VSR(26, r3)
    LOAD_VSR(27, r3)
    LOAD_VSR(28, r3)
    LOAD_VSR(29, r3)
    LOAD_VSR(30, r3)
    LOAD_VSR(31, r3)
    LOAD_VSR(32, r3)
    LOAD_VSR(33, r3)
    LOAD_VSR(34, r3)
    LOAD_VSR(35, r3)
    LOAD_VSR(36, r3)
    LOAD_VSR(37, r3)
    LOAD_VSR(38, r3)
    LOAD_VSR(39, r3)
    LOAD_VSR(40, r3)
    LOAD_VSR(41, r3)
    LOAD_VSR(42, r3)
    LOAD_VSR(43, r3)
    LOAD_VSR(44, r3)
    LOAD_VSR(45, r3)
    LOAD_VSR(46, r3)
    LOAD_VSR(47, r3)
    LOAD_VSR(48, r3)
    LOAD_VSR(49, r3)
    LOAD_VSR(50, r3)
    LOAD_VSR(51, r3)
    LOAD_VSR(52, r3)
    LOAD_VSR(53, r3)
    LOAD_VSR(54, r3)
    LOAD_VSR(55, r3)
    LOAD_VSR(56, r3)
    LOAD_VSR(57, r3)
    LOAD_VSR(58, r3)
    LOAD_VSR(59, r3)
    LOAD_VSR(60, r3)
    LOAD_VSR(61, r3)
    LOAD_VSR(62, r3)
    LOAD_VSR(63, r3)

    ld      r0,XER_SAVE(r1)
    mtxer   r0
    ld      r0,CTR_SAVE(r1)
    mtctr   r0
    ld      r0,LR_SAVE(r1)
    mtlr    r0
    ld      r0,CCR_SAVE(r1)
    mtcr    r0
    REST_GPR(0)
    REST_GPR(2)
    REST_GPR(3)
    REST_GPR(4)
    REST_GPR(5)
    REST_GPR(6)
    REST_GPR(7)
    REST_GPR(8)
    REST_GPR(9)
    REST_GPR(10)
    REST_GPR(11)
    REST_GPR(12)
    REST_GPR(13)
    REST_GPR(14)
    REST_GPR(15)
    REST_GPR(16)
    REST_GPR(17)
    REST_GPR(18)
    REST_GPR(19)
    REST_GPR(20)
    REST_GPR(21)
    REST_GPR(22)
    REST_GPR(23)
    REST_GPR(24)
    REST_GPR(25)
    REST_GPR(26)
    REST_GPR(27)
    REST_GPR(28)
    REST_GPR(29)
    REST_GPR(30)
    REST_GPR(31)
    addi    r1,r1,STACK_FRAME
    RFEBB
FUNC_END(ebb_handler)
