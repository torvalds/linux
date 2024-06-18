/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * test helper assembly functions
 *
 * Copyright (C) 2016 Simon Guo, IBM Corporation.
 */
#include <ppc-asm.h>
#include "reg.h"


/* Non volatile GPR - unsigned long buf[18] */
FUNC_START(load_gpr)
	ld	14, 0*8(3)
	ld	15, 1*8(3)
	ld	16, 2*8(3)
	ld	17, 3*8(3)
	ld	18, 4*8(3)
	ld	19, 5*8(3)
	ld	20, 6*8(3)
	ld	21, 7*8(3)
	ld	22, 8*8(3)
	ld	23, 9*8(3)
	ld	24, 10*8(3)
	ld	25, 11*8(3)
	ld	26, 12*8(3)
	ld	27, 13*8(3)
	ld	28, 14*8(3)
	ld	29, 15*8(3)
	ld	30, 16*8(3)
	ld	31, 17*8(3)
	blr
FUNC_END(load_gpr)

FUNC_START(store_gpr)
	std	14, 0*8(3)
	std	15, 1*8(3)
	std	16, 2*8(3)
	std	17, 3*8(3)
	std	18, 4*8(3)
	std	19, 5*8(3)
	std	20, 6*8(3)
	std	21, 7*8(3)
	std	22, 8*8(3)
	std	23, 9*8(3)
	std	24, 10*8(3)
	std	25, 11*8(3)
	std	26, 12*8(3)
	std	27, 13*8(3)
	std	28, 14*8(3)
	std	29, 15*8(3)
	std	30, 16*8(3)
	std	31, 17*8(3)
	blr
FUNC_END(store_gpr)

/* Double Precision Float - double buf[32] */
FUNC_START(store_fpr)
	stfd  0,  0*8(3)
	stfd  1,  1*8(3)
	stfd  2,  2*8(3)
	stfd  3,  3*8(3)
	stfd  4,  4*8(3)
	stfd  5,  5*8(3)
	stfd  6,  6*8(3)
	stfd  7,  7*8(3)
	stfd  8,  8*8(3)
	stfd  9,  9*8(3)
	stfd 10, 10*8(3)
	stfd 11, 11*8(3)
	stfd 12, 12*8(3)
	stfd 13, 13*8(3)
	stfd 14, 14*8(3)
	stfd 15, 15*8(3)
	stfd 16, 16*8(3)
	stfd 17, 17*8(3)
	stfd 18, 18*8(3)
	stfd 19, 19*8(3)
	stfd 20, 20*8(3)
	stfd 21, 21*8(3)
	stfd 22, 22*8(3)
	stfd 23, 23*8(3)
	stfd 24, 24*8(3)
	stfd 25, 25*8(3)
	stfd 26, 26*8(3)
	stfd 27, 27*8(3)
	stfd 28, 28*8(3)
	stfd 29, 29*8(3)
	stfd 30, 30*8(3)
	stfd 31, 31*8(3)
	blr
FUNC_END(store_fpr)

/* VMX/VSX registers - unsigned long buf[128] */
FUNC_START(loadvsx)
	lis	4, 0
	LXVD2X	(0,(4),(3))
	addi	4, 4, 16
	LXVD2X	(1,(4),(3))
	addi	4, 4, 16
	LXVD2X	(2,(4),(3))
	addi	4, 4, 16
	LXVD2X	(3,(4),(3))
	addi	4, 4, 16
	LXVD2X	(4,(4),(3))
	addi	4, 4, 16
	LXVD2X	(5,(4),(3))
	addi	4, 4, 16
	LXVD2X	(6,(4),(3))
	addi	4, 4, 16
	LXVD2X	(7,(4),(3))
	addi	4, 4, 16
	LXVD2X	(8,(4),(3))
	addi	4, 4, 16
	LXVD2X	(9,(4),(3))
	addi	4, 4, 16
	LXVD2X	(10,(4),(3))
	addi	4, 4, 16
	LXVD2X	(11,(4),(3))
	addi	4, 4, 16
	LXVD2X	(12,(4),(3))
	addi	4, 4, 16
	LXVD2X	(13,(4),(3))
	addi	4, 4, 16
	LXVD2X	(14,(4),(3))
	addi	4, 4, 16
	LXVD2X	(15,(4),(3))
	addi	4, 4, 16
	LXVD2X	(16,(4),(3))
	addi	4, 4, 16
	LXVD2X	(17,(4),(3))
	addi	4, 4, 16
	LXVD2X	(18,(4),(3))
	addi	4, 4, 16
	LXVD2X	(19,(4),(3))
	addi	4, 4, 16
	LXVD2X	(20,(4),(3))
	addi	4, 4, 16
	LXVD2X	(21,(4),(3))
	addi	4, 4, 16
	LXVD2X	(22,(4),(3))
	addi	4, 4, 16
	LXVD2X	(23,(4),(3))
	addi	4, 4, 16
	LXVD2X	(24,(4),(3))
	addi	4, 4, 16
	LXVD2X	(25,(4),(3))
	addi	4, 4, 16
	LXVD2X	(26,(4),(3))
	addi	4, 4, 16
	LXVD2X	(27,(4),(3))
	addi	4, 4, 16
	LXVD2X	(28,(4),(3))
	addi	4, 4, 16
	LXVD2X	(29,(4),(3))
	addi	4, 4, 16
	LXVD2X	(30,(4),(3))
	addi	4, 4, 16
	LXVD2X	(31,(4),(3))
	addi	4, 4, 16
	LXVD2X	(32,(4),(3))
	addi	4, 4, 16
	LXVD2X	(33,(4),(3))
	addi	4, 4, 16
	LXVD2X	(34,(4),(3))
	addi	4, 4, 16
	LXVD2X	(35,(4),(3))
	addi	4, 4, 16
	LXVD2X	(36,(4),(3))
	addi	4, 4, 16
	LXVD2X	(37,(4),(3))
	addi	4, 4, 16
	LXVD2X	(38,(4),(3))
	addi	4, 4, 16
	LXVD2X	(39,(4),(3))
	addi	4, 4, 16
	LXVD2X	(40,(4),(3))
	addi	4, 4, 16
	LXVD2X	(41,(4),(3))
	addi	4, 4, 16
	LXVD2X	(42,(4),(3))
	addi	4, 4, 16
	LXVD2X	(43,(4),(3))
	addi	4, 4, 16
	LXVD2X	(44,(4),(3))
	addi	4, 4, 16
	LXVD2X	(45,(4),(3))
	addi	4, 4, 16
	LXVD2X	(46,(4),(3))
	addi	4, 4, 16
	LXVD2X	(47,(4),(3))
	addi	4, 4, 16
	LXVD2X	(48,(4),(3))
	addi	4, 4, 16
	LXVD2X	(49,(4),(3))
	addi	4, 4, 16
	LXVD2X	(50,(4),(3))
	addi	4, 4, 16
	LXVD2X	(51,(4),(3))
	addi	4, 4, 16
	LXVD2X	(52,(4),(3))
	addi	4, 4, 16
	LXVD2X	(53,(4),(3))
	addi	4, 4, 16
	LXVD2X	(54,(4),(3))
	addi	4, 4, 16
	LXVD2X	(55,(4),(3))
	addi	4, 4, 16
	LXVD2X	(56,(4),(3))
	addi	4, 4, 16
	LXVD2X	(57,(4),(3))
	addi	4, 4, 16
	LXVD2X	(58,(4),(3))
	addi	4, 4, 16
	LXVD2X	(59,(4),(3))
	addi	4, 4, 16
	LXVD2X	(60,(4),(3))
	addi	4, 4, 16
	LXVD2X	(61,(4),(3))
	addi	4, 4, 16
	LXVD2X	(62,(4),(3))
	addi	4, 4, 16
	LXVD2X	(63,(4),(3))
	blr
FUNC_END(loadvsx)

FUNC_START(storevsx)
	lis	4, 0
	STXVD2X	(0,(4),(3))
	addi	4, 4, 16
	STXVD2X	(1,(4),(3))
	addi	4, 4, 16
	STXVD2X	(2,(4),(3))
	addi	4, 4, 16
	STXVD2X	(3,(4),(3))
	addi	4, 4, 16
	STXVD2X	(4,(4),(3))
	addi	4, 4, 16
	STXVD2X	(5,(4),(3))
	addi	4, 4, 16
	STXVD2X	(6,(4),(3))
	addi	4, 4, 16
	STXVD2X	(7,(4),(3))
	addi	4, 4, 16
	STXVD2X	(8,(4),(3))
	addi	4, 4, 16
	STXVD2X	(9,(4),(3))
	addi	4, 4, 16
	STXVD2X	(10,(4),(3))
	addi	4, 4, 16
	STXVD2X	(11,(4),(3))
	addi	4, 4, 16
	STXVD2X	(12,(4),(3))
	addi	4, 4, 16
	STXVD2X	(13,(4),(3))
	addi	4, 4, 16
	STXVD2X	(14,(4),(3))
	addi	4, 4, 16
	STXVD2X	(15,(4),(3))
	addi	4, 4, 16
	STXVD2X	(16,(4),(3))
	addi	4, 4, 16
	STXVD2X	(17,(4),(3))
	addi	4, 4, 16
	STXVD2X	(18,(4),(3))
	addi	4, 4, 16
	STXVD2X	(19,(4),(3))
	addi	4, 4, 16
	STXVD2X	(20,(4),(3))
	addi	4, 4, 16
	STXVD2X	(21,(4),(3))
	addi	4, 4, 16
	STXVD2X	(22,(4),(3))
	addi	4, 4, 16
	STXVD2X	(23,(4),(3))
	addi	4, 4, 16
	STXVD2X	(24,(4),(3))
	addi	4, 4, 16
	STXVD2X	(25,(4),(3))
	addi	4, 4, 16
	STXVD2X	(26,(4),(3))
	addi	4, 4, 16
	STXVD2X	(27,(4),(3))
	addi	4, 4, 16
	STXVD2X	(28,(4),(3))
	addi	4, 4, 16
	STXVD2X	(29,(4),(3))
	addi	4, 4, 16
	STXVD2X	(30,(4),(3))
	addi	4, 4, 16
	STXVD2X	(31,(4),(3))
	addi	4, 4, 16
	STXVD2X	(32,(4),(3))
	addi	4, 4, 16
	STXVD2X	(33,(4),(3))
	addi	4, 4, 16
	STXVD2X	(34,(4),(3))
	addi	4, 4, 16
	STXVD2X	(35,(4),(3))
	addi	4, 4, 16
	STXVD2X	(36,(4),(3))
	addi	4, 4, 16
	STXVD2X	(37,(4),(3))
	addi	4, 4, 16
	STXVD2X	(38,(4),(3))
	addi	4, 4, 16
	STXVD2X	(39,(4),(3))
	addi	4, 4, 16
	STXVD2X	(40,(4),(3))
	addi	4, 4, 16
	STXVD2X	(41,(4),(3))
	addi	4, 4, 16
	STXVD2X	(42,(4),(3))
	addi	4, 4, 16
	STXVD2X	(43,(4),(3))
	addi	4, 4, 16
	STXVD2X	(44,(4),(3))
	addi	4, 4, 16
	STXVD2X	(45,(4),(3))
	addi	4, 4, 16
	STXVD2X	(46,(4),(3))
	addi	4, 4, 16
	STXVD2X	(47,(4),(3))
	addi	4, 4, 16
	STXVD2X	(48,(4),(3))
	addi	4, 4, 16
	STXVD2X	(49,(4),(3))
	addi	4, 4, 16
	STXVD2X	(50,(4),(3))
	addi	4, 4, 16
	STXVD2X	(51,(4),(3))
	addi	4, 4, 16
	STXVD2X	(52,(4),(3))
	addi	4, 4, 16
	STXVD2X	(53,(4),(3))
	addi	4, 4, 16
	STXVD2X	(54,(4),(3))
	addi	4, 4, 16
	STXVD2X	(55,(4),(3))
	addi	4, 4, 16
	STXVD2X	(56,(4),(3))
	addi	4, 4, 16
	STXVD2X	(57,(4),(3))
	addi	4, 4, 16
	STXVD2X	(58,(4),(3))
	addi	4, 4, 16
	STXVD2X	(59,(4),(3))
	addi	4, 4, 16
	STXVD2X	(60,(4),(3))
	addi	4, 4, 16
	STXVD2X	(61,(4),(3))
	addi	4, 4, 16
	STXVD2X	(62,(4),(3))
	addi	4, 4, 16
	STXVD2X	(63,(4),(3))
	blr
FUNC_END(storevsx)
