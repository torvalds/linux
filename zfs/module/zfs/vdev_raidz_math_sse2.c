/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/isa_defs.h>

#if defined(__x86_64) && defined(HAVE_SSE2)

#include <sys/types.h>
#include <linux/simd_x86.h>

#define	__asm __asm__ __volatile__

#define	_REG_CNT(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define	REG_CNT(r...) _REG_CNT(r, 8, 7, 6, 5, 4, 3, 2, 1)

#define	VR0_(REG, ...) "xmm"#REG
#define	VR1_(_1, REG, ...) "xmm"#REG
#define	VR2_(_1, _2, REG, ...) "xmm"#REG
#define	VR3_(_1, _2, _3, REG, ...) "xmm"#REG
#define	VR4_(_1, _2, _3, _4, REG, ...) "xmm"#REG
#define	VR5_(_1, _2, _3, _4, _5, REG, ...) "xmm"#REG
#define	VR6_(_1, _2, _3, _4, _5, _6, REG, ...) "xmm"#REG
#define	VR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) "xmm"#REG

#define	VR0(r...) VR0_(r, 1, 2, 3, 4, 5, 6)
#define	VR1(r...) VR1_(r, 1, 2, 3, 4, 5, 6)
#define	VR2(r...) VR2_(r, 1, 2, 3, 4, 5, 6)
#define	VR3(r...) VR3_(r, 1, 2, 3, 4, 5, 6)
#define	VR4(r...) VR4_(r, 1, 2, 3, 4, 5, 6)
#define	VR5(r...) VR5_(r, 1, 2, 3, 4, 5, 6)
#define	VR6(r...) VR6_(r, 1, 2, 3, 4, 5, 6)
#define	VR7(r...) VR7_(r, 1, 2, 3, 4, 5, 6)

#define	ELEM_SIZE 16

typedef struct v {
	uint8_t b[ELEM_SIZE] __attribute__((aligned(ELEM_SIZE)));
} v_t;

#define	XOR_ACC(src, r...) 						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "pxor 0x00(%[SRC]), %%" VR0(r) "\n"			\
		    "pxor 0x10(%[SRC]), %%" VR1(r) "\n"			\
		    "pxor 0x20(%[SRC]), %%" VR2(r) "\n"			\
		    "pxor 0x30(%[SRC]), %%" VR3(r) "\n"			\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "pxor 0x00(%[SRC]), %%" VR0(r) "\n"			\
		    "pxor 0x10(%[SRC]), %%" VR1(r) "\n"			\
		    : : [SRC] "r" (src));				\
		break;							\
	case 1:								\
		__asm("pxor 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	}								\
}

#define	XOR(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "pxor %" VR0(r) ", %" VR4(r) "\n"			\
		    "pxor %" VR1(r) ", %" VR5(r) "\n"			\
		    "pxor %" VR2(r) ", %" VR6(r) "\n"			\
		    "pxor %" VR3(r) ", %" VR7(r));			\
		break;							\
	case 4:								\
		__asm(							\
		    "pxor %" VR0(r) ", %" VR2(r) "\n"			\
		    "pxor %" VR1(r) ", %" VR3(r));			\
		break;							\
	case 2:								\
		__asm(							\
		    "pxor %" VR0(r) ", %" VR1(r));			\
		break;							\
	}								\
}

#define	ZERO(r...)	XOR(r, r)

#define	COPY(r...) 							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "movdqa %" VR0(r) ", %" VR4(r) "\n"			\
		    "movdqa %" VR1(r) ", %" VR5(r) "\n"			\
		    "movdqa %" VR2(r) ", %" VR6(r) "\n"			\
		    "movdqa %" VR3(r) ", %" VR7(r));			\
		break;							\
	case 4:								\
		__asm(							\
		    "movdqa %" VR0(r) ", %" VR2(r) "\n"			\
		    "movdqa %" VR1(r) ", %" VR3(r));			\
		break;							\
	case 2:								\
		__asm(							\
		    "movdqa %" VR0(r) ", %" VR1(r));			\
		break;							\
	}								\
}

#define	LOAD(src, r...) 						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "movdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "movdqa 0x10(%[SRC]), %%" VR1(r) "\n"		\
		    "movdqa 0x20(%[SRC]), %%" VR2(r) "\n"		\
		    "movdqa 0x30(%[SRC]), %%" VR3(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "movdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "movdqa 0x10(%[SRC]), %%" VR1(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	case 1:								\
		__asm(							\
		    "movdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	}								\
}

#define	STORE(dst, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "movdqa %%" VR0(r)", 0x00(%[DST])\n"		\
		    "movdqa %%" VR1(r)", 0x10(%[DST])\n"		\
		    "movdqa %%" VR2(r)", 0x20(%[DST])\n"		\
		    "movdqa %%" VR3(r)", 0x30(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	case 2:								\
		__asm(							\
		    "movdqa %%" VR0(r)", 0x00(%[DST])\n"		\
		    "movdqa %%" VR1(r)", 0x10(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	case 1:								\
		__asm(							\
		    "movdqa %%" VR0(r)", 0x00(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	}								\
}

#define	MUL2_SETUP()							\
{   									\
	__asm(								\
	    "movd %[mask], %%xmm15\n"					\
	    "pshufd $0x0, %%xmm15, %%xmm15\n"				\
	    : : [mask] "r" (0x1d1d1d1d));				\
}

#define	_MUL2_x1(a0) 							\
{									\
	__asm(								\
	    "pxor    %xmm14,      %xmm14\n"				\
	    "pcmpgtb %" a0",  %xmm14\n"					\
	    "pand    %xmm15,      %xmm14\n"				\
	    "paddb   %" a0",  %" a0 "\n"				\
	    "pxor    %xmm14,      %" a0);				\
}

#define	_MUL2_x2(a0, a1) 						\
{									\
	__asm(								\
	    "pxor    %xmm14,      %xmm14\n"				\
	    "pxor    %xmm13,      %xmm13\n"				\
	    "pcmpgtb %" a0",  %xmm14\n"					\
	    "pcmpgtb %" a1",  %xmm13\n"					\
	    "pand    %xmm15,      %xmm14\n"				\
	    "pand    %xmm15,      %xmm13\n"				\
	    "paddb   %" a0",  %" a0 "\n"				\
	    "paddb   %" a1",  %" a1 "\n"				\
	    "pxor    %xmm14,      %" a0 "\n"				\
	    "pxor    %xmm13,      %" a1);				\
}

#define	MUL2(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		_MUL2_x2(VR0(r), VR1(r));				\
		_MUL2_x2(VR2(r), VR3(r));				\
		break;							\
	case 2:								\
		_MUL2_x2(VR0(r), VR1(r));				\
		break;							\
	case 1:								\
		_MUL2_x1(VR0(r));					\
		break;							\
	}								\
}

#define	MUL4(r...)							\
{									\
	MUL2(r);							\
	MUL2(r);							\
}

/* General multiplication by adding powers of two */

#define	_MUL_PARAM(x, in, acc)						\
{									\
	if (x & 0x01) {	COPY(in, acc); } else { ZERO(acc); }	\
	if (x & 0xfe) { MUL2(in); }					\
	if (x & 0x02) { XOR(in, acc); }					\
	if (x & 0xfc) { MUL2(in); }					\
	if (x & 0x04) { XOR(in, acc); }					\
	if (x & 0xf8) { MUL2(in); }					\
	if (x & 0x08) { XOR(in, acc); }					\
	if (x & 0xf0) { MUL2(in); }					\
	if (x & 0x10) { XOR(in, acc); }					\
	if (x & 0xe0) { MUL2(in); }					\
	if (x & 0x20) { XOR(in, acc); }					\
	if (x & 0xc0) { MUL2(in); }					\
	if (x & 0x40) { XOR(in, acc); }					\
	if (x & 0x80) { MUL2(in); XOR(in, acc); }			\
}

#define	_mul_x1_in	11
#define	_mul_x1_acc	12

#define	MUL_x1_DEFINE(x)						\
static void 								\
mul_x1_ ## x(void) { _MUL_PARAM(x, _mul_x1_in, _mul_x1_acc); }

#define	_mul_x2_in	9, 10
#define	_mul_x2_acc	11, 12

#define	MUL_x2_DEFINE(x)						\
static void 								\
mul_x2_ ## x(void) { _MUL_PARAM(x, _mul_x2_in, _mul_x2_acc); }

MUL_x1_DEFINE(0); MUL_x1_DEFINE(1); MUL_x1_DEFINE(2); MUL_x1_DEFINE(3);
MUL_x1_DEFINE(4); MUL_x1_DEFINE(5); MUL_x1_DEFINE(6); MUL_x1_DEFINE(7);
MUL_x1_DEFINE(8); MUL_x1_DEFINE(9); MUL_x1_DEFINE(10); MUL_x1_DEFINE(11);
MUL_x1_DEFINE(12); MUL_x1_DEFINE(13); MUL_x1_DEFINE(14); MUL_x1_DEFINE(15);
MUL_x1_DEFINE(16); MUL_x1_DEFINE(17); MUL_x1_DEFINE(18); MUL_x1_DEFINE(19);
MUL_x1_DEFINE(20); MUL_x1_DEFINE(21); MUL_x1_DEFINE(22); MUL_x1_DEFINE(23);
MUL_x1_DEFINE(24); MUL_x1_DEFINE(25); MUL_x1_DEFINE(26); MUL_x1_DEFINE(27);
MUL_x1_DEFINE(28); MUL_x1_DEFINE(29); MUL_x1_DEFINE(30); MUL_x1_DEFINE(31);
MUL_x1_DEFINE(32); MUL_x1_DEFINE(33); MUL_x1_DEFINE(34); MUL_x1_DEFINE(35);
MUL_x1_DEFINE(36); MUL_x1_DEFINE(37); MUL_x1_DEFINE(38); MUL_x1_DEFINE(39);
MUL_x1_DEFINE(40); MUL_x1_DEFINE(41); MUL_x1_DEFINE(42); MUL_x1_DEFINE(43);
MUL_x1_DEFINE(44); MUL_x1_DEFINE(45); MUL_x1_DEFINE(46); MUL_x1_DEFINE(47);
MUL_x1_DEFINE(48); MUL_x1_DEFINE(49); MUL_x1_DEFINE(50); MUL_x1_DEFINE(51);
MUL_x1_DEFINE(52); MUL_x1_DEFINE(53); MUL_x1_DEFINE(54); MUL_x1_DEFINE(55);
MUL_x1_DEFINE(56); MUL_x1_DEFINE(57); MUL_x1_DEFINE(58); MUL_x1_DEFINE(59);
MUL_x1_DEFINE(60); MUL_x1_DEFINE(61); MUL_x1_DEFINE(62); MUL_x1_DEFINE(63);
MUL_x1_DEFINE(64); MUL_x1_DEFINE(65); MUL_x1_DEFINE(66); MUL_x1_DEFINE(67);
MUL_x1_DEFINE(68); MUL_x1_DEFINE(69); MUL_x1_DEFINE(70); MUL_x1_DEFINE(71);
MUL_x1_DEFINE(72); MUL_x1_DEFINE(73); MUL_x1_DEFINE(74); MUL_x1_DEFINE(75);
MUL_x1_DEFINE(76); MUL_x1_DEFINE(77); MUL_x1_DEFINE(78); MUL_x1_DEFINE(79);
MUL_x1_DEFINE(80); MUL_x1_DEFINE(81); MUL_x1_DEFINE(82); MUL_x1_DEFINE(83);
MUL_x1_DEFINE(84); MUL_x1_DEFINE(85); MUL_x1_DEFINE(86); MUL_x1_DEFINE(87);
MUL_x1_DEFINE(88); MUL_x1_DEFINE(89); MUL_x1_DEFINE(90); MUL_x1_DEFINE(91);
MUL_x1_DEFINE(92); MUL_x1_DEFINE(93); MUL_x1_DEFINE(94); MUL_x1_DEFINE(95);
MUL_x1_DEFINE(96); MUL_x1_DEFINE(97); MUL_x1_DEFINE(98); MUL_x1_DEFINE(99);
MUL_x1_DEFINE(100); MUL_x1_DEFINE(101); MUL_x1_DEFINE(102); MUL_x1_DEFINE(103);
MUL_x1_DEFINE(104); MUL_x1_DEFINE(105); MUL_x1_DEFINE(106); MUL_x1_DEFINE(107);
MUL_x1_DEFINE(108); MUL_x1_DEFINE(109); MUL_x1_DEFINE(110); MUL_x1_DEFINE(111);
MUL_x1_DEFINE(112); MUL_x1_DEFINE(113); MUL_x1_DEFINE(114); MUL_x1_DEFINE(115);
MUL_x1_DEFINE(116); MUL_x1_DEFINE(117); MUL_x1_DEFINE(118); MUL_x1_DEFINE(119);
MUL_x1_DEFINE(120); MUL_x1_DEFINE(121); MUL_x1_DEFINE(122); MUL_x1_DEFINE(123);
MUL_x1_DEFINE(124); MUL_x1_DEFINE(125); MUL_x1_DEFINE(126); MUL_x1_DEFINE(127);
MUL_x1_DEFINE(128); MUL_x1_DEFINE(129); MUL_x1_DEFINE(130); MUL_x1_DEFINE(131);
MUL_x1_DEFINE(132); MUL_x1_DEFINE(133); MUL_x1_DEFINE(134); MUL_x1_DEFINE(135);
MUL_x1_DEFINE(136); MUL_x1_DEFINE(137); MUL_x1_DEFINE(138); MUL_x1_DEFINE(139);
MUL_x1_DEFINE(140); MUL_x1_DEFINE(141); MUL_x1_DEFINE(142); MUL_x1_DEFINE(143);
MUL_x1_DEFINE(144); MUL_x1_DEFINE(145); MUL_x1_DEFINE(146); MUL_x1_DEFINE(147);
MUL_x1_DEFINE(148); MUL_x1_DEFINE(149); MUL_x1_DEFINE(150); MUL_x1_DEFINE(151);
MUL_x1_DEFINE(152); MUL_x1_DEFINE(153); MUL_x1_DEFINE(154); MUL_x1_DEFINE(155);
MUL_x1_DEFINE(156); MUL_x1_DEFINE(157); MUL_x1_DEFINE(158); MUL_x1_DEFINE(159);
MUL_x1_DEFINE(160); MUL_x1_DEFINE(161); MUL_x1_DEFINE(162); MUL_x1_DEFINE(163);
MUL_x1_DEFINE(164); MUL_x1_DEFINE(165); MUL_x1_DEFINE(166); MUL_x1_DEFINE(167);
MUL_x1_DEFINE(168); MUL_x1_DEFINE(169); MUL_x1_DEFINE(170); MUL_x1_DEFINE(171);
MUL_x1_DEFINE(172); MUL_x1_DEFINE(173); MUL_x1_DEFINE(174); MUL_x1_DEFINE(175);
MUL_x1_DEFINE(176); MUL_x1_DEFINE(177); MUL_x1_DEFINE(178); MUL_x1_DEFINE(179);
MUL_x1_DEFINE(180); MUL_x1_DEFINE(181); MUL_x1_DEFINE(182); MUL_x1_DEFINE(183);
MUL_x1_DEFINE(184); MUL_x1_DEFINE(185); MUL_x1_DEFINE(186); MUL_x1_DEFINE(187);
MUL_x1_DEFINE(188); MUL_x1_DEFINE(189); MUL_x1_DEFINE(190); MUL_x1_DEFINE(191);
MUL_x1_DEFINE(192); MUL_x1_DEFINE(193); MUL_x1_DEFINE(194); MUL_x1_DEFINE(195);
MUL_x1_DEFINE(196); MUL_x1_DEFINE(197); MUL_x1_DEFINE(198); MUL_x1_DEFINE(199);
MUL_x1_DEFINE(200); MUL_x1_DEFINE(201); MUL_x1_DEFINE(202); MUL_x1_DEFINE(203);
MUL_x1_DEFINE(204); MUL_x1_DEFINE(205); MUL_x1_DEFINE(206); MUL_x1_DEFINE(207);
MUL_x1_DEFINE(208); MUL_x1_DEFINE(209); MUL_x1_DEFINE(210); MUL_x1_DEFINE(211);
MUL_x1_DEFINE(212); MUL_x1_DEFINE(213); MUL_x1_DEFINE(214); MUL_x1_DEFINE(215);
MUL_x1_DEFINE(216); MUL_x1_DEFINE(217); MUL_x1_DEFINE(218); MUL_x1_DEFINE(219);
MUL_x1_DEFINE(220); MUL_x1_DEFINE(221); MUL_x1_DEFINE(222); MUL_x1_DEFINE(223);
MUL_x1_DEFINE(224); MUL_x1_DEFINE(225); MUL_x1_DEFINE(226); MUL_x1_DEFINE(227);
MUL_x1_DEFINE(228); MUL_x1_DEFINE(229); MUL_x1_DEFINE(230); MUL_x1_DEFINE(231);
MUL_x1_DEFINE(232); MUL_x1_DEFINE(233); MUL_x1_DEFINE(234); MUL_x1_DEFINE(235);
MUL_x1_DEFINE(236); MUL_x1_DEFINE(237); MUL_x1_DEFINE(238); MUL_x1_DEFINE(239);
MUL_x1_DEFINE(240); MUL_x1_DEFINE(241); MUL_x1_DEFINE(242); MUL_x1_DEFINE(243);
MUL_x1_DEFINE(244); MUL_x1_DEFINE(245); MUL_x1_DEFINE(246); MUL_x1_DEFINE(247);
MUL_x1_DEFINE(248); MUL_x1_DEFINE(249); MUL_x1_DEFINE(250); MUL_x1_DEFINE(251);
MUL_x1_DEFINE(252); MUL_x1_DEFINE(253); MUL_x1_DEFINE(254); MUL_x1_DEFINE(255);

MUL_x2_DEFINE(0); MUL_x2_DEFINE(1); MUL_x2_DEFINE(2); MUL_x2_DEFINE(3);
MUL_x2_DEFINE(4); MUL_x2_DEFINE(5); MUL_x2_DEFINE(6); MUL_x2_DEFINE(7);
MUL_x2_DEFINE(8); MUL_x2_DEFINE(9); MUL_x2_DEFINE(10); MUL_x2_DEFINE(11);
MUL_x2_DEFINE(12); MUL_x2_DEFINE(13); MUL_x2_DEFINE(14); MUL_x2_DEFINE(15);
MUL_x2_DEFINE(16); MUL_x2_DEFINE(17); MUL_x2_DEFINE(18); MUL_x2_DEFINE(19);
MUL_x2_DEFINE(20); MUL_x2_DEFINE(21); MUL_x2_DEFINE(22); MUL_x2_DEFINE(23);
MUL_x2_DEFINE(24); MUL_x2_DEFINE(25); MUL_x2_DEFINE(26); MUL_x2_DEFINE(27);
MUL_x2_DEFINE(28); MUL_x2_DEFINE(29); MUL_x2_DEFINE(30); MUL_x2_DEFINE(31);
MUL_x2_DEFINE(32); MUL_x2_DEFINE(33); MUL_x2_DEFINE(34); MUL_x2_DEFINE(35);
MUL_x2_DEFINE(36); MUL_x2_DEFINE(37); MUL_x2_DEFINE(38); MUL_x2_DEFINE(39);
MUL_x2_DEFINE(40); MUL_x2_DEFINE(41); MUL_x2_DEFINE(42); MUL_x2_DEFINE(43);
MUL_x2_DEFINE(44); MUL_x2_DEFINE(45); MUL_x2_DEFINE(46); MUL_x2_DEFINE(47);
MUL_x2_DEFINE(48); MUL_x2_DEFINE(49); MUL_x2_DEFINE(50); MUL_x2_DEFINE(51);
MUL_x2_DEFINE(52); MUL_x2_DEFINE(53); MUL_x2_DEFINE(54); MUL_x2_DEFINE(55);
MUL_x2_DEFINE(56); MUL_x2_DEFINE(57); MUL_x2_DEFINE(58); MUL_x2_DEFINE(59);
MUL_x2_DEFINE(60); MUL_x2_DEFINE(61); MUL_x2_DEFINE(62); MUL_x2_DEFINE(63);
MUL_x2_DEFINE(64); MUL_x2_DEFINE(65); MUL_x2_DEFINE(66); MUL_x2_DEFINE(67);
MUL_x2_DEFINE(68); MUL_x2_DEFINE(69); MUL_x2_DEFINE(70); MUL_x2_DEFINE(71);
MUL_x2_DEFINE(72); MUL_x2_DEFINE(73); MUL_x2_DEFINE(74); MUL_x2_DEFINE(75);
MUL_x2_DEFINE(76); MUL_x2_DEFINE(77); MUL_x2_DEFINE(78); MUL_x2_DEFINE(79);
MUL_x2_DEFINE(80); MUL_x2_DEFINE(81); MUL_x2_DEFINE(82); MUL_x2_DEFINE(83);
MUL_x2_DEFINE(84); MUL_x2_DEFINE(85); MUL_x2_DEFINE(86); MUL_x2_DEFINE(87);
MUL_x2_DEFINE(88); MUL_x2_DEFINE(89); MUL_x2_DEFINE(90); MUL_x2_DEFINE(91);
MUL_x2_DEFINE(92); MUL_x2_DEFINE(93); MUL_x2_DEFINE(94); MUL_x2_DEFINE(95);
MUL_x2_DEFINE(96); MUL_x2_DEFINE(97); MUL_x2_DEFINE(98); MUL_x2_DEFINE(99);
MUL_x2_DEFINE(100); MUL_x2_DEFINE(101); MUL_x2_DEFINE(102); MUL_x2_DEFINE(103);
MUL_x2_DEFINE(104); MUL_x2_DEFINE(105); MUL_x2_DEFINE(106); MUL_x2_DEFINE(107);
MUL_x2_DEFINE(108); MUL_x2_DEFINE(109); MUL_x2_DEFINE(110); MUL_x2_DEFINE(111);
MUL_x2_DEFINE(112); MUL_x2_DEFINE(113); MUL_x2_DEFINE(114); MUL_x2_DEFINE(115);
MUL_x2_DEFINE(116); MUL_x2_DEFINE(117); MUL_x2_DEFINE(118); MUL_x2_DEFINE(119);
MUL_x2_DEFINE(120); MUL_x2_DEFINE(121); MUL_x2_DEFINE(122); MUL_x2_DEFINE(123);
MUL_x2_DEFINE(124); MUL_x2_DEFINE(125); MUL_x2_DEFINE(126); MUL_x2_DEFINE(127);
MUL_x2_DEFINE(128); MUL_x2_DEFINE(129); MUL_x2_DEFINE(130); MUL_x2_DEFINE(131);
MUL_x2_DEFINE(132); MUL_x2_DEFINE(133); MUL_x2_DEFINE(134); MUL_x2_DEFINE(135);
MUL_x2_DEFINE(136); MUL_x2_DEFINE(137); MUL_x2_DEFINE(138); MUL_x2_DEFINE(139);
MUL_x2_DEFINE(140); MUL_x2_DEFINE(141); MUL_x2_DEFINE(142); MUL_x2_DEFINE(143);
MUL_x2_DEFINE(144); MUL_x2_DEFINE(145); MUL_x2_DEFINE(146); MUL_x2_DEFINE(147);
MUL_x2_DEFINE(148); MUL_x2_DEFINE(149); MUL_x2_DEFINE(150); MUL_x2_DEFINE(151);
MUL_x2_DEFINE(152); MUL_x2_DEFINE(153); MUL_x2_DEFINE(154); MUL_x2_DEFINE(155);
MUL_x2_DEFINE(156); MUL_x2_DEFINE(157); MUL_x2_DEFINE(158); MUL_x2_DEFINE(159);
MUL_x2_DEFINE(160); MUL_x2_DEFINE(161); MUL_x2_DEFINE(162); MUL_x2_DEFINE(163);
MUL_x2_DEFINE(164); MUL_x2_DEFINE(165); MUL_x2_DEFINE(166); MUL_x2_DEFINE(167);
MUL_x2_DEFINE(168); MUL_x2_DEFINE(169); MUL_x2_DEFINE(170); MUL_x2_DEFINE(171);
MUL_x2_DEFINE(172); MUL_x2_DEFINE(173); MUL_x2_DEFINE(174); MUL_x2_DEFINE(175);
MUL_x2_DEFINE(176); MUL_x2_DEFINE(177); MUL_x2_DEFINE(178); MUL_x2_DEFINE(179);
MUL_x2_DEFINE(180); MUL_x2_DEFINE(181); MUL_x2_DEFINE(182); MUL_x2_DEFINE(183);
MUL_x2_DEFINE(184); MUL_x2_DEFINE(185); MUL_x2_DEFINE(186); MUL_x2_DEFINE(187);
MUL_x2_DEFINE(188); MUL_x2_DEFINE(189); MUL_x2_DEFINE(190); MUL_x2_DEFINE(191);
MUL_x2_DEFINE(192); MUL_x2_DEFINE(193); MUL_x2_DEFINE(194); MUL_x2_DEFINE(195);
MUL_x2_DEFINE(196); MUL_x2_DEFINE(197); MUL_x2_DEFINE(198); MUL_x2_DEFINE(199);
MUL_x2_DEFINE(200); MUL_x2_DEFINE(201); MUL_x2_DEFINE(202); MUL_x2_DEFINE(203);
MUL_x2_DEFINE(204); MUL_x2_DEFINE(205); MUL_x2_DEFINE(206); MUL_x2_DEFINE(207);
MUL_x2_DEFINE(208); MUL_x2_DEFINE(209); MUL_x2_DEFINE(210); MUL_x2_DEFINE(211);
MUL_x2_DEFINE(212); MUL_x2_DEFINE(213); MUL_x2_DEFINE(214); MUL_x2_DEFINE(215);
MUL_x2_DEFINE(216); MUL_x2_DEFINE(217); MUL_x2_DEFINE(218); MUL_x2_DEFINE(219);
MUL_x2_DEFINE(220); MUL_x2_DEFINE(221); MUL_x2_DEFINE(222); MUL_x2_DEFINE(223);
MUL_x2_DEFINE(224); MUL_x2_DEFINE(225); MUL_x2_DEFINE(226); MUL_x2_DEFINE(227);
MUL_x2_DEFINE(228); MUL_x2_DEFINE(229); MUL_x2_DEFINE(230); MUL_x2_DEFINE(231);
MUL_x2_DEFINE(232); MUL_x2_DEFINE(233); MUL_x2_DEFINE(234); MUL_x2_DEFINE(235);
MUL_x2_DEFINE(236); MUL_x2_DEFINE(237); MUL_x2_DEFINE(238); MUL_x2_DEFINE(239);
MUL_x2_DEFINE(240); MUL_x2_DEFINE(241); MUL_x2_DEFINE(242); MUL_x2_DEFINE(243);
MUL_x2_DEFINE(244); MUL_x2_DEFINE(245); MUL_x2_DEFINE(246); MUL_x2_DEFINE(247);
MUL_x2_DEFINE(248); MUL_x2_DEFINE(249); MUL_x2_DEFINE(250); MUL_x2_DEFINE(251);
MUL_x2_DEFINE(252); MUL_x2_DEFINE(253); MUL_x2_DEFINE(254); MUL_x2_DEFINE(255);



typedef void (*mul_fn_ptr_t)(void);

static const mul_fn_ptr_t __attribute__((aligned(256)))
gf_x1_mul_fns[256] = {
	mul_x1_0, mul_x1_1, mul_x1_2, mul_x1_3, mul_x1_4, mul_x1_5,
	mul_x1_6, mul_x1_7, mul_x1_8, mul_x1_9, mul_x1_10, mul_x1_11,
	mul_x1_12, mul_x1_13, mul_x1_14, mul_x1_15, mul_x1_16, mul_x1_17,
	mul_x1_18, mul_x1_19, mul_x1_20, mul_x1_21, mul_x1_22, mul_x1_23,
	mul_x1_24, mul_x1_25, mul_x1_26, mul_x1_27, mul_x1_28, mul_x1_29,
	mul_x1_30, mul_x1_31, mul_x1_32, mul_x1_33, mul_x1_34, mul_x1_35,
	mul_x1_36, mul_x1_37, mul_x1_38, mul_x1_39, mul_x1_40, mul_x1_41,
	mul_x1_42, mul_x1_43, mul_x1_44, mul_x1_45, mul_x1_46, mul_x1_47,
	mul_x1_48, mul_x1_49, mul_x1_50, mul_x1_51, mul_x1_52, mul_x1_53,
	mul_x1_54, mul_x1_55, mul_x1_56, mul_x1_57, mul_x1_58, mul_x1_59,
	mul_x1_60, mul_x1_61, mul_x1_62, mul_x1_63, mul_x1_64, mul_x1_65,
	mul_x1_66, mul_x1_67, mul_x1_68, mul_x1_69, mul_x1_70, mul_x1_71,
	mul_x1_72, mul_x1_73, mul_x1_74, mul_x1_75, mul_x1_76, mul_x1_77,
	mul_x1_78, mul_x1_79, mul_x1_80, mul_x1_81, mul_x1_82, mul_x1_83,
	mul_x1_84, mul_x1_85, mul_x1_86, mul_x1_87, mul_x1_88, mul_x1_89,
	mul_x1_90, mul_x1_91, mul_x1_92, mul_x1_93, mul_x1_94, mul_x1_95,
	mul_x1_96, mul_x1_97, mul_x1_98, mul_x1_99, mul_x1_100, mul_x1_101,
	mul_x1_102, mul_x1_103, mul_x1_104, mul_x1_105, mul_x1_106, mul_x1_107,
	mul_x1_108, mul_x1_109, mul_x1_110, mul_x1_111, mul_x1_112, mul_x1_113,
	mul_x1_114, mul_x1_115, mul_x1_116, mul_x1_117, mul_x1_118, mul_x1_119,
	mul_x1_120, mul_x1_121, mul_x1_122, mul_x1_123, mul_x1_124, mul_x1_125,
	mul_x1_126, mul_x1_127, mul_x1_128, mul_x1_129, mul_x1_130, mul_x1_131,
	mul_x1_132, mul_x1_133, mul_x1_134, mul_x1_135, mul_x1_136, mul_x1_137,
	mul_x1_138, mul_x1_139, mul_x1_140, mul_x1_141, mul_x1_142, mul_x1_143,
	mul_x1_144, mul_x1_145, mul_x1_146, mul_x1_147, mul_x1_148, mul_x1_149,
	mul_x1_150, mul_x1_151, mul_x1_152, mul_x1_153, mul_x1_154, mul_x1_155,
	mul_x1_156, mul_x1_157, mul_x1_158, mul_x1_159, mul_x1_160, mul_x1_161,
	mul_x1_162, mul_x1_163, mul_x1_164, mul_x1_165, mul_x1_166, mul_x1_167,
	mul_x1_168, mul_x1_169, mul_x1_170, mul_x1_171, mul_x1_172, mul_x1_173,
	mul_x1_174, mul_x1_175, mul_x1_176, mul_x1_177, mul_x1_178, mul_x1_179,
	mul_x1_180, mul_x1_181, mul_x1_182, mul_x1_183, mul_x1_184, mul_x1_185,
	mul_x1_186, mul_x1_187, mul_x1_188, mul_x1_189, mul_x1_190, mul_x1_191,
	mul_x1_192, mul_x1_193, mul_x1_194, mul_x1_195, mul_x1_196, mul_x1_197,
	mul_x1_198, mul_x1_199, mul_x1_200, mul_x1_201, mul_x1_202, mul_x1_203,
	mul_x1_204, mul_x1_205, mul_x1_206, mul_x1_207, mul_x1_208, mul_x1_209,
	mul_x1_210, mul_x1_211, mul_x1_212, mul_x1_213, mul_x1_214, mul_x1_215,
	mul_x1_216, mul_x1_217, mul_x1_218, mul_x1_219, mul_x1_220, mul_x1_221,
	mul_x1_222, mul_x1_223, mul_x1_224, mul_x1_225, mul_x1_226, mul_x1_227,
	mul_x1_228, mul_x1_229, mul_x1_230, mul_x1_231, mul_x1_232, mul_x1_233,
	mul_x1_234, mul_x1_235, mul_x1_236, mul_x1_237, mul_x1_238, mul_x1_239,
	mul_x1_240, mul_x1_241, mul_x1_242, mul_x1_243, mul_x1_244, mul_x1_245,
	mul_x1_246, mul_x1_247, mul_x1_248, mul_x1_249, mul_x1_250, mul_x1_251,
	mul_x1_252, mul_x1_253, mul_x1_254, mul_x1_255
};

static const mul_fn_ptr_t __attribute__((aligned(256)))
gf_x2_mul_fns[256] = {
	mul_x2_0, mul_x2_1, mul_x2_2, mul_x2_3, mul_x2_4, mul_x2_5,
	mul_x2_6, mul_x2_7, mul_x2_8, mul_x2_9, mul_x2_10, mul_x2_11,
	mul_x2_12, mul_x2_13, mul_x2_14, mul_x2_15, mul_x2_16, mul_x2_17,
	mul_x2_18, mul_x2_19, mul_x2_20, mul_x2_21, mul_x2_22, mul_x2_23,
	mul_x2_24, mul_x2_25, mul_x2_26, mul_x2_27, mul_x2_28, mul_x2_29,
	mul_x2_30, mul_x2_31, mul_x2_32, mul_x2_33, mul_x2_34, mul_x2_35,
	mul_x2_36, mul_x2_37, mul_x2_38, mul_x2_39, mul_x2_40, mul_x2_41,
	mul_x2_42, mul_x2_43, mul_x2_44, mul_x2_45, mul_x2_46, mul_x2_47,
	mul_x2_48, mul_x2_49, mul_x2_50, mul_x2_51, mul_x2_52, mul_x2_53,
	mul_x2_54, mul_x2_55, mul_x2_56, mul_x2_57, mul_x2_58, mul_x2_59,
	mul_x2_60, mul_x2_61, mul_x2_62, mul_x2_63, mul_x2_64, mul_x2_65,
	mul_x2_66, mul_x2_67, mul_x2_68, mul_x2_69, mul_x2_70, mul_x2_71,
	mul_x2_72, mul_x2_73, mul_x2_74, mul_x2_75, mul_x2_76, mul_x2_77,
	mul_x2_78, mul_x2_79, mul_x2_80, mul_x2_81, mul_x2_82, mul_x2_83,
	mul_x2_84, mul_x2_85, mul_x2_86, mul_x2_87, mul_x2_88, mul_x2_89,
	mul_x2_90, mul_x2_91, mul_x2_92, mul_x2_93, mul_x2_94, mul_x2_95,
	mul_x2_96, mul_x2_97, mul_x2_98, mul_x2_99, mul_x2_100, mul_x2_101,
	mul_x2_102, mul_x2_103, mul_x2_104, mul_x2_105, mul_x2_106, mul_x2_107,
	mul_x2_108, mul_x2_109, mul_x2_110, mul_x2_111, mul_x2_112, mul_x2_113,
	mul_x2_114, mul_x2_115, mul_x2_116, mul_x2_117, mul_x2_118, mul_x2_119,
	mul_x2_120, mul_x2_121, mul_x2_122, mul_x2_123, mul_x2_124, mul_x2_125,
	mul_x2_126, mul_x2_127, mul_x2_128, mul_x2_129, mul_x2_130, mul_x2_131,
	mul_x2_132, mul_x2_133, mul_x2_134, mul_x2_135, mul_x2_136, mul_x2_137,
	mul_x2_138, mul_x2_139, mul_x2_140, mul_x2_141, mul_x2_142, mul_x2_143,
	mul_x2_144, mul_x2_145, mul_x2_146, mul_x2_147, mul_x2_148, mul_x2_149,
	mul_x2_150, mul_x2_151, mul_x2_152, mul_x2_153, mul_x2_154, mul_x2_155,
	mul_x2_156, mul_x2_157, mul_x2_158, mul_x2_159, mul_x2_160, mul_x2_161,
	mul_x2_162, mul_x2_163, mul_x2_164, mul_x2_165, mul_x2_166, mul_x2_167,
	mul_x2_168, mul_x2_169, mul_x2_170, mul_x2_171, mul_x2_172, mul_x2_173,
	mul_x2_174, mul_x2_175, mul_x2_176, mul_x2_177, mul_x2_178, mul_x2_179,
	mul_x2_180, mul_x2_181, mul_x2_182, mul_x2_183, mul_x2_184, mul_x2_185,
	mul_x2_186, mul_x2_187, mul_x2_188, mul_x2_189, mul_x2_190, mul_x2_191,
	mul_x2_192, mul_x2_193, mul_x2_194, mul_x2_195, mul_x2_196, mul_x2_197,
	mul_x2_198, mul_x2_199, mul_x2_200, mul_x2_201, mul_x2_202, mul_x2_203,
	mul_x2_204, mul_x2_205, mul_x2_206, mul_x2_207, mul_x2_208, mul_x2_209,
	mul_x2_210, mul_x2_211, mul_x2_212, mul_x2_213, mul_x2_214, mul_x2_215,
	mul_x2_216, mul_x2_217, mul_x2_218, mul_x2_219, mul_x2_220, mul_x2_221,
	mul_x2_222, mul_x2_223, mul_x2_224, mul_x2_225, mul_x2_226, mul_x2_227,
	mul_x2_228, mul_x2_229, mul_x2_230, mul_x2_231, mul_x2_232, mul_x2_233,
	mul_x2_234, mul_x2_235, mul_x2_236, mul_x2_237, mul_x2_238, mul_x2_239,
	mul_x2_240, mul_x2_241, mul_x2_242, mul_x2_243, mul_x2_244, mul_x2_245,
	mul_x2_246, mul_x2_247, mul_x2_248, mul_x2_249, mul_x2_250, mul_x2_251,
	mul_x2_252, mul_x2_253, mul_x2_254, mul_x2_255
};

#define	MUL(c, r...) 							\
{									\
	switch (REG_CNT(r)) {						\
	case 2:								\
		COPY(r, _mul_x2_in);					\
		gf_x2_mul_fns[c]();					\
		COPY(_mul_x2_acc, r);					\
		break;							\
	case 1:								\
		COPY(r, _mul_x1_in);					\
		gf_x1_mul_fns[c]();					\
		COPY(_mul_x1_acc, r);					\
		break;							\
	}								\
}


#define	raidz_math_begin()	kfpu_begin()
#define	raidz_math_end()	kfpu_end()

#define	SYN_STRIDE		4

#define	ZERO_STRIDE		4
#define	ZERO_DEFINE()		{}
#define	ZERO_D			0, 1, 2, 3

#define	COPY_STRIDE		4
#define	COPY_DEFINE()		{}
#define	COPY_D			0, 1, 2, 3

#define	ADD_STRIDE		4
#define	ADD_DEFINE()		{}
#define	ADD_D 			0, 1, 2, 3

#define	MUL_STRIDE		2
#define	MUL_DEFINE() 		MUL2_SETUP()
#define	MUL_D			0, 1

#define	GEN_P_STRIDE		4
#define	GEN_P_DEFINE()		{}
#define	GEN_P_P			0, 1, 2, 3

#define	GEN_PQ_STRIDE		4
#define	GEN_PQ_DEFINE() 	{}
#define	GEN_PQ_D		0, 1, 2, 3
#define	GEN_PQ_C		4, 5, 6, 7

#define	GEN_PQR_STRIDE		4
#define	GEN_PQR_DEFINE() 	{}
#define	GEN_PQR_D		0, 1, 2, 3
#define	GEN_PQR_C		4, 5, 6, 7

#define	SYN_Q_DEFINE()		{}
#define	SYN_Q_D			0, 1, 2, 3
#define	SYN_Q_X			4, 5, 6, 7

#define	SYN_R_DEFINE()		{}
#define	SYN_R_D			0, 1, 2, 3
#define	SYN_R_X			4, 5, 6, 7

#define	SYN_PQ_DEFINE() 	{}
#define	SYN_PQ_D		0, 1, 2, 3
#define	SYN_PQ_X		4, 5, 6, 7

#define	REC_PQ_STRIDE		2
#define	REC_PQ_DEFINE() 	MUL2_SETUP()
#define	REC_PQ_X		0, 1
#define	REC_PQ_Y		2, 3
#define	REC_PQ_T		4, 5

#define	SYN_PR_DEFINE() 	{}
#define	SYN_PR_D		0, 1, 2, 3
#define	SYN_PR_X		4, 5, 6, 7

#define	REC_PR_STRIDE		2
#define	REC_PR_DEFINE() 	MUL2_SETUP()
#define	REC_PR_X		0, 1
#define	REC_PR_Y		2, 3
#define	REC_PR_T		4, 5

#define	SYN_QR_DEFINE() 	{}
#define	SYN_QR_D		0, 1, 2, 3
#define	SYN_QR_X		4, 5, 6, 7

#define	REC_QR_STRIDE		2
#define	REC_QR_DEFINE() 	MUL2_SETUP()
#define	REC_QR_X		0, 1
#define	REC_QR_Y		2, 3
#define	REC_QR_T		4, 5

#define	SYN_PQR_DEFINE() 	{}
#define	SYN_PQR_D		0, 1, 2, 3
#define	SYN_PQR_X		4, 5, 6, 7

#define	REC_PQR_STRIDE		1
#define	REC_PQR_DEFINE() 	MUL2_SETUP()
#define	REC_PQR_X		0
#define	REC_PQR_Y		1
#define	REC_PQR_Z		2
#define	REC_PQR_XS		3
#define	REC_PQR_YS		4


#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(sse2);
DEFINE_REC_METHODS(sse2);

static boolean_t
raidz_will_sse2_work(void)
{
	return (zfs_sse_available() && zfs_sse2_available());
}

const raidz_impl_ops_t vdev_raidz_sse2_impl = {
	.init = NULL,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(sse2),
	.rec = RAIDZ_REC_METHODS(sse2),
	.is_supported = &raidz_will_sse2_work,
	.name = "sse2"
};

#endif /* defined(__x86_64) && defined(HAVE_SSE2) */
