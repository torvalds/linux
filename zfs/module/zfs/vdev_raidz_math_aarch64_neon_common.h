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
 * Copyright (C) 2016 Romain Dolbeau. All rights reserved.
 */

#include <sys/types.h>
#include <linux/simd_aarch64.h>

#define	__asm __asm__ __volatile__

#define	_REG_CNT(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define	REG_CNT(r...) _REG_CNT(r, 8, 7, 6, 5, 4, 3, 2, 1)

#define	VR0_(REG, ...) "%[w"#REG"]"
#define	VR1_(_1, REG, ...) "%[w"#REG"]"
#define	VR2_(_1, _2, REG, ...) "%[w"#REG"]"
#define	VR3_(_1, _2, _3, REG, ...) "%[w"#REG"]"
#define	VR4_(_1, _2, _3, _4, REG, ...) "%[w"#REG"]"
#define	VR5_(_1, _2, _3, _4, _5, REG, ...) "%[w"#REG"]"
#define	VR6_(_1, _2, _3, _4, _5, _6, REG, ...) "%[w"#REG"]"
#define	VR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) "%[w"#REG"]"

/*
 * Here we need registers not used otherwise.
 * They will be used in unused ASM for the case
 * with more registers than required... but GGC
 * will still need to make sure the constraints
 * are correct, and duplicate constraints are illegal
 * ... and we use the "register" number as a name
 */

#define	VR0(r...) VR0_(r)
#define	VR1(r...) VR1_(r)
#define	VR2(r...) VR2_(r, 36)
#define	VR3(r...) VR3_(r, 36, 35)
#define	VR4(r...) VR4_(r, 36, 35, 34, 33)
#define	VR5(r...) VR5_(r, 36, 35, 34, 33, 32)
#define	VR6(r...) VR6_(r, 36, 35, 34, 33, 32, 31)
#define	VR7(r...) VR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	VR(X) "%[w"#X"]"

#define	RVR0_(REG, ...) [w##REG] "w" (w##REG)
#define	RVR1_(_1, REG, ...) [w##REG] "w" (w##REG)
#define	RVR2_(_1, _2, REG, ...) [w##REG] "w" (w##REG)
#define	RVR3_(_1, _2, _3, REG, ...) [w##REG] "w" (w##REG)
#define	RVR4_(_1, _2, _3, _4, REG, ...) [w##REG] "w" (w##REG)
#define	RVR5_(_1, _2, _3, _4, _5, REG, ...) [w##REG] "w" (w##REG)
#define	RVR6_(_1, _2, _3, _4, _5, _6, REG, ...) [w##REG] "w" (w##REG)
#define	RVR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) [w##REG] "w" (w##REG)

#define	RVR0(r...) RVR0_(r)
#define	RVR1(r...) RVR1_(r)
#define	RVR2(r...) RVR2_(r, 36)
#define	RVR3(r...) RVR3_(r, 36, 35)
#define	RVR4(r...) RVR4_(r, 36, 35, 34, 33)
#define	RVR5(r...) RVR5_(r, 36, 35, 34, 33, 32)
#define	RVR6(r...) RVR6_(r, 36, 35, 34, 33, 32, 31)
#define	RVR7(r...) RVR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	RVR(X) [w##X] "w" (w##X)

#define	WVR0_(REG, ...) [w##REG] "=w" (w##REG)
#define	WVR1_(_1, REG, ...) [w##REG] "=w" (w##REG)
#define	WVR2_(_1, _2, REG, ...) [w##REG] "=w" (w##REG)
#define	WVR3_(_1, _2, _3, REG, ...) [w##REG] "=w" (w##REG)
#define	WVR4_(_1, _2, _3, _4, REG, ...) [w##REG] "=w" (w##REG)
#define	WVR5_(_1, _2, _3, _4, _5, REG, ...) [w##REG] "=w" (w##REG)
#define	WVR6_(_1, _2, _3, _4, _5, _6, REG, ...) [w##REG] "=w" (w##REG)
#define	WVR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) [w##REG] "=w" (w##REG)

#define	WVR0(r...) WVR0_(r)
#define	WVR1(r...) WVR1_(r)
#define	WVR2(r...) WVR2_(r, 36)
#define	WVR3(r...) WVR3_(r, 36, 35)
#define	WVR4(r...) WVR4_(r, 36, 35, 34, 33)
#define	WVR5(r...) WVR5_(r, 36, 35, 34, 33, 32)
#define	WVR6(r...) WVR6_(r, 36, 35, 34, 33, 32, 31)
#define	WVR7(r...) WVR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	WVR(X) [w##X] "=w" (w##X)

#define	UVR0_(REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR1_(_1, REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR2_(_1, _2, REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR3_(_1, _2, _3, REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR4_(_1, _2, _3, _4, REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR5_(_1, _2, _3, _4, _5, REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR6_(_1, _2, _3, _4, _5, _6, REG, ...) [w##REG] "+&w" (w##REG)
#define	UVR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) [w##REG] "+&w" (w##REG)

#define	UVR0(r...) UVR0_(r)
#define	UVR1(r...) UVR1_(r)
#define	UVR2(r...) UVR2_(r, 36)
#define	UVR3(r...) UVR3_(r, 36, 35)
#define	UVR4(r...) UVR4_(r, 36, 35, 34, 33)
#define	UVR5(r...) UVR5_(r, 36, 35, 34, 33, 32)
#define	UVR6(r...) UVR6_(r, 36, 35, 34, 33, 32, 31)
#define	UVR7(r...) UVR7_(r, 36, 35, 34, 33, 32, 31, 30)

#define	UVR(X) [w##X] "+&w" (w##X)

#define	R_01(REG1, REG2, ...) REG1, REG2
#define	_R_23(_0, _1, REG2, REG3, ...) REG2, REG3
#define	R_23(REG...) _R_23(REG, 1, 2, 3)

#define	ASM_BUG()	ASSERT(0)

#define	OFFSET(ptr, val)	(((unsigned char *)(ptr))+val)

extern const uint8_t gf_clmul_mod_lt[4*256][16];

#define	ELEM_SIZE 16

typedef struct v {
	uint8_t b[ELEM_SIZE] __attribute__((aligned(ELEM_SIZE)));
} v_t;

#define	XOR_ACC(src, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		"ld1 { v21.4s },%[SRC0]\n"				\
		"ld1 { v20.4s },%[SRC1]\n"				\
		"ld1 { v19.4s },%[SRC2]\n"				\
		"ld1 { v18.4s },%[SRC3]\n"				\
		"eor " VR0(r) ".16b," VR0(r) ".16b,v21.16b\n"		\
		"eor " VR1(r) ".16b," VR1(r) ".16b,v20.16b\n"		\
		"eor " VR2(r) ".16b," VR2(r) ".16b,v19.16b\n"		\
		"eor " VR3(r) ".16b," VR3(r) ".16b,v18.16b\n"		\
		"ld1 { v21.4s },%[SRC4]\n"				\
		"ld1 { v20.4s },%[SRC5]\n"				\
		"ld1 { v19.4s },%[SRC6]\n"				\
		"ld1 { v18.4s },%[SRC7]\n"				\
		"eor " VR4(r) ".16b," VR4(r) ".16b,v21.16b\n"		\
		"eor " VR5(r) ".16b," VR5(r) ".16b,v20.16b\n"		\
		"eor " VR6(r) ".16b," VR6(r) ".16b,v19.16b\n"		\
		"eor " VR7(r) ".16b," VR7(r) ".16b,v18.16b\n"		\
		:	UVR0(r), UVR1(r), UVR2(r), UVR3(r),		\
			UVR4(r), UVR5(r), UVR6(r), UVR7(r)		\
		:	[SRC0] "Q" (*(OFFSET(src, 0))),			\
		[SRC1] "Q" (*(OFFSET(src, 16))),			\
		[SRC2] "Q" (*(OFFSET(src, 32))),			\
		[SRC3] "Q" (*(OFFSET(src, 48))),			\
		[SRC4] "Q" (*(OFFSET(src, 64))),			\
		[SRC5] "Q" (*(OFFSET(src, 80))),			\
		[SRC6] "Q" (*(OFFSET(src, 96))),			\
		[SRC7] "Q" (*(OFFSET(src, 112)))			\
		:	"v18", "v19", "v20", "v21");			\
		break;							\
	case 4:								\
		__asm(							\
		"ld1 { v21.4s },%[SRC0]\n"				\
		"ld1 { v20.4s },%[SRC1]\n"				\
		"ld1 { v19.4s },%[SRC2]\n"				\
		"ld1 { v18.4s },%[SRC3]\n"				\
		"eor " VR0(r) ".16b," VR0(r) ".16b,v21.16b\n"		\
		"eor " VR1(r) ".16b," VR1(r) ".16b,v20.16b\n"		\
		"eor " VR2(r) ".16b," VR2(r) ".16b,v19.16b\n"		\
		"eor " VR3(r) ".16b," VR3(r) ".16b,v18.16b\n"		\
		:	UVR0(r), UVR1(r), UVR2(r), UVR3(r)		\
		:	[SRC0] "Q" (*(OFFSET(src, 0))),			\
		[SRC1] "Q" (*(OFFSET(src, 16))),			\
		[SRC2] "Q" (*(OFFSET(src, 32))),			\
		[SRC3] "Q" (*(OFFSET(src, 48)))				\
		:	"v18", "v19", "v20", "v21");			\
		break;							\
	case 2:								\
		__asm(							\
		"ld1 { v21.4s },%[SRC0]\n"				\
		"ld1 { v20.4s },%[SRC1]\n"				\
		"eor " VR0(r) ".16b," VR0(r) ".16b,v21.16b\n"		\
		"eor " VR1(r) ".16b," VR1(r) ".16b,v20.16b\n"		\
		:	UVR0(r), UVR1(r)				\
		:	[SRC0] "Q" (*(OFFSET(src, 0))),			\
		[SRC1] "Q" (*(OFFSET(src, 16)))				\
		:	"v20", "v21");					\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	XOR(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		"eor " VR4(r) ".16b," VR4(r) ".16b," VR0(r) ".16b\n"	\
		"eor " VR5(r) ".16b," VR5(r) ".16b," VR1(r) ".16b\n"	\
		"eor " VR6(r) ".16b," VR6(r) ".16b," VR2(r) ".16b\n"	\
		"eor " VR7(r) ".16b," VR7(r) ".16b," VR3(r) ".16b\n"	\
		:	UVR4(r), UVR5(r), UVR6(r), UVR7(r)		\
		:	RVR0(r), RVR1(r), RVR2(r), RVR3(r));		\
		break;							\
	case 4:								\
		__asm(							\
		"eor " VR2(r) ".16b," VR2(r) ".16b," VR0(r) ".16b\n"	\
		"eor " VR3(r) ".16b," VR3(r) ".16b," VR1(r) ".16b\n"	\
		:	UVR2(r), UVR3(r)				\
		:	RVR0(r), RVR1(r));				\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	ZERO(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		"eor " VR0(r) ".16b," VR0(r) ".16b," VR0(r) ".16b\n"	\
		"eor " VR1(r) ".16b," VR1(r) ".16b," VR1(r) ".16b\n"	\
		"eor " VR2(r) ".16b," VR2(r) ".16b," VR2(r) ".16b\n"	\
		"eor " VR3(r) ".16b," VR3(r) ".16b," VR3(r) ".16b\n"	\
		"eor " VR4(r) ".16b," VR4(r) ".16b," VR4(r) ".16b\n"	\
		"eor " VR5(r) ".16b," VR5(r) ".16b," VR5(r) ".16b\n"	\
		"eor " VR6(r) ".16b," VR6(r) ".16b," VR6(r) ".16b\n"	\
		"eor " VR7(r) ".16b," VR7(r) ".16b," VR7(r) ".16b\n"	\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r),		\
			WVR4(r), WVR5(r), WVR6(r), WVR7(r));		\
		break;							\
	case 4:								\
		__asm(							\
		"eor " VR0(r) ".16b," VR0(r) ".16b," VR0(r) ".16b\n"	\
		"eor " VR1(r) ".16b," VR1(r) ".16b," VR1(r) ".16b\n"	\
		"eor " VR2(r) ".16b," VR2(r) ".16b," VR2(r) ".16b\n"	\
		"eor " VR3(r) ".16b," VR3(r) ".16b," VR3(r) ".16b\n"	\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r));		\
		break;							\
	case 2:								\
		__asm(							\
		"eor " VR0(r) ".16b," VR0(r) ".16b," VR0(r) ".16b\n"	\
		"eor " VR1(r) ".16b," VR1(r) ".16b," VR1(r) ".16b\n"	\
		:	WVR0(r), WVR1(r));				\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	COPY(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		"mov " VR4(r) ".16b," VR0(r) ".16b\n"			\
		"mov " VR5(r) ".16b," VR1(r) ".16b\n"			\
		"mov " VR6(r) ".16b," VR2(r) ".16b\n"			\
		"mov " VR7(r) ".16b," VR3(r) ".16b\n"			\
		:	WVR4(r), WVR5(r), WVR6(r), WVR7(r)		\
		:	RVR0(r), RVR1(r), RVR2(r), RVR3(r));		\
		break;							\
	case 4:								\
		__asm(							\
		"mov " VR2(r) ".16b," VR0(r) ".16b\n"			\
		"mov " VR3(r) ".16b," VR1(r) ".16b\n"			\
		:	WVR2(r), WVR3(r)				\
		:	RVR0(r), RVR1(r));				\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	LOAD(src, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		"ld1 { " VR0(r) ".4s },%[SRC0]\n"			\
		"ld1 { " VR1(r) ".4s },%[SRC1]\n"			\
		"ld1 { " VR2(r) ".4s },%[SRC2]\n"			\
		"ld1 { " VR3(r) ".4s },%[SRC3]\n"			\
		"ld1 { " VR4(r) ".4s },%[SRC4]\n"			\
		"ld1 { " VR5(r) ".4s },%[SRC5]\n"			\
		"ld1 { " VR6(r) ".4s },%[SRC6]\n"			\
		"ld1 { " VR7(r) ".4s },%[SRC7]\n"			\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r),		\
			WVR4(r), WVR5(r), WVR6(r), WVR7(r)		\
		:	[SRC0] "Q" (*(OFFSET(src, 0))),			\
		[SRC1] "Q" (*(OFFSET(src, 16))),			\
		[SRC2] "Q" (*(OFFSET(src, 32))),			\
		[SRC3] "Q" (*(OFFSET(src, 48))),			\
		[SRC4] "Q" (*(OFFSET(src, 64))),			\
		[SRC5] "Q" (*(OFFSET(src, 80))),			\
		[SRC6] "Q" (*(OFFSET(src, 96))),			\
		[SRC7] "Q" (*(OFFSET(src, 112))));			\
		break;							\
	case 4:								\
		__asm(							\
		"ld1 { " VR0(r) ".4s },%[SRC0]\n"			\
		"ld1 { " VR1(r) ".4s },%[SRC1]\n"			\
		"ld1 { " VR2(r) ".4s },%[SRC2]\n"			\
		"ld1 { " VR3(r) ".4s },%[SRC3]\n"			\
		:	WVR0(r), WVR1(r), WVR2(r), WVR3(r)		\
		:	[SRC0] "Q" (*(OFFSET(src, 0))),			\
		[SRC1] "Q" (*(OFFSET(src, 16))),			\
		[SRC2] "Q" (*(OFFSET(src, 32))),			\
		[SRC3] "Q" (*(OFFSET(src, 48))));			\
		break;							\
	case 2:								\
		__asm(							\
		"ld1 { " VR0(r) ".4s },%[SRC0]\n"			\
		"ld1 { " VR1(r) ".4s },%[SRC1]\n"			\
		:	WVR0(r), WVR1(r)				\
		:	[SRC0] "Q" (*(OFFSET(src, 0))),			\
		[SRC1] "Q" (*(OFFSET(src, 16))));			\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	STORE(dst, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		"st1 { " VR0(r) ".4s },%[DST0]\n"			\
		"st1 { " VR1(r) ".4s },%[DST1]\n"			\
		"st1 { " VR2(r) ".4s },%[DST2]\n"			\
		"st1 { " VR3(r) ".4s },%[DST3]\n"			\
		"st1 { " VR4(r) ".4s },%[DST4]\n"			\
		"st1 { " VR5(r) ".4s },%[DST5]\n"			\
		"st1 { " VR6(r) ".4s },%[DST6]\n"			\
		"st1 { " VR7(r) ".4s },%[DST7]\n"			\
		:	[DST0] "=Q" (*(OFFSET(dst, 0))),		\
		[DST1] "=Q" (*(OFFSET(dst, 16))),			\
		[DST2] "=Q" (*(OFFSET(dst, 32))),			\
		[DST3] "=Q" (*(OFFSET(dst, 48))),			\
		[DST4] "=Q" (*(OFFSET(dst, 64))),			\
		[DST5] "=Q" (*(OFFSET(dst, 80))),			\
		[DST6] "=Q" (*(OFFSET(dst, 96))),			\
		[DST7] "=Q" (*(OFFSET(dst, 112)))			\
		:	RVR0(r), RVR1(r), RVR2(r), RVR3(r),		\
			RVR4(r), RVR5(r), RVR6(r), RVR7(r));		\
		break;							\
	case 4:								\
		__asm(							\
		"st1 { " VR0(r) ".4s },%[DST0]\n"			\
		"st1 { " VR1(r) ".4s },%[DST1]\n"			\
		"st1 { " VR2(r) ".4s },%[DST2]\n"			\
		"st1 { " VR3(r) ".4s },%[DST3]\n"			\
		:	[DST0] "=Q" (*(OFFSET(dst, 0))),		\
		[DST1] "=Q" (*(OFFSET(dst, 16))),			\
		[DST2] "=Q" (*(OFFSET(dst, 32))),			\
		[DST3] "=Q" (*(OFFSET(dst, 48)))			\
		:	RVR0(r), RVR1(r), RVR2(r), RVR3(r));		\
		break;							\
	case 2:								\
		__asm(							\
		"st1 { " VR0(r) ".4s },%[DST0]\n"			\
		"st1 { " VR1(r) ".4s },%[DST1]\n"			\
		:	[DST0] "=Q" (*(OFFSET(dst, 0))),		\
		[DST1] "=Q" (*(OFFSET(dst, 16)))			\
		:	RVR0(r), RVR1(r));				\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

/*
 * Unfortunately cannot use the macro, because GCC
 * will try to use the macro name and not value
 * later on...
 * Kept as a reference to what a numbered variable is
 */
#define	_00	"v17"
#define	_1d	"v16"
#define	_temp0	"v19"
#define	_temp1	"v18"

#define	MUL2_SETUP()							\
{									\
	__asm(								\
	"eor " VR(17) ".16b," VR(17) ".16b," VR(17) ".16b\n"		\
	"movi " VR(16) ".16b,#0x1d\n"					\
	:	WVR(16), WVR(17));					\
}

#define	MUL2(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		"cmgt v19.16b," VR(17) ".16b," VR0(r) ".16b\n"		\
		"cmgt v18.16b," VR(17) ".16b," VR1(r) ".16b\n"		\
		"cmgt v21.16b," VR(17) ".16b," VR2(r) ".16b\n"		\
		"cmgt v20.16b," VR(17) ".16b," VR3(r) ".16b\n"		\
		"and v19.16b,v19.16b," VR(16) ".16b\n"			\
		"and v18.16b,v18.16b," VR(16) ".16b\n"			\
		"and v21.16b,v21.16b," VR(16) ".16b\n"			\
		"and v20.16b,v20.16b," VR(16) ".16b\n"			\
		"shl " VR0(r) ".16b," VR0(r) ".16b,#1\n"		\
		"shl " VR1(r) ".16b," VR1(r) ".16b,#1\n"		\
		"shl " VR2(r) ".16b," VR2(r) ".16b,#1\n"		\
		"shl " VR3(r) ".16b," VR3(r) ".16b,#1\n"		\
		"eor " VR0(r) ".16b,v19.16b," VR0(r) ".16b\n"		\
		"eor " VR1(r) ".16b,v18.16b," VR1(r) ".16b\n"		\
		"eor " VR2(r) ".16b,v21.16b," VR2(r) ".16b\n"		\
		"eor " VR3(r) ".16b,v20.16b," VR3(r) ".16b\n"		\
		:	UVR0(r), UVR1(r), UVR2(r), UVR3(r)		\
		:	RVR(17), RVR(16)				\
		:	"v18", "v19", "v20", "v21");			\
		break;							\
	case 2:								\
		__asm(							\
		"cmgt v19.16b," VR(17) ".16b," VR0(r) ".16b\n"		\
		"cmgt v18.16b," VR(17) ".16b," VR1(r) ".16b\n"		\
		"and v19.16b,v19.16b," VR(16) ".16b\n"			\
		"and v18.16b,v18.16b," VR(16) ".16b\n"			\
		"shl " VR0(r) ".16b," VR0(r) ".16b,#1\n"		\
		"shl " VR1(r) ".16b," VR1(r) ".16b,#1\n"		\
		"eor " VR0(r) ".16b,v19.16b," VR0(r) ".16b\n"		\
		"eor " VR1(r) ".16b,v18.16b," VR1(r) ".16b\n"		\
		:	UVR0(r), UVR1(r)				\
		:	RVR(17), RVR(16)				\
		:	"v18", "v19");					\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	MUL4(r...)							\
{									\
	MUL2(r);							\
	MUL2(r);							\
}

/*
 * Unfortunately cannot use the macro, because GCC
 * will try to use the macro name and not value
 * later on...
 * Kept as a reference to what a register is
 * (here we're using actual registers for the
 * clobbered ones)
 */
#define	_0f		"v15"
#define	_a_save		"v14"
#define	_b_save		"v13"
#define	_lt_mod_a	"v12"
#define	_lt_clmul_a	"v11"
#define	_lt_mod_b	"v10"
#define	_lt_clmul_b	"v15"

#define	_MULx2(c, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 2:								\
		__asm(							\
		/* lts for upper part */				\
		"movi v15.16b,#0x0f\n"					\
		"ld1 { v10.4s },%[lt0]\n"				\
		"ld1 { v11.4s },%[lt1]\n"				\
		/* upper part */					\
		"and v14.16b," VR0(r) ".16b,v15.16b\n"			\
		"and v13.16b," VR1(r) ".16b,v15.16b\n"			\
		"sshr " VR0(r) ".8h," VR0(r) ".8h,#4\n"			\
		"sshr " VR1(r) ".8h," VR1(r) ".8h,#4\n"			\
		"and " VR0(r) ".16b," VR0(r) ".16b,v15.16b\n"		\
		"and " VR1(r) ".16b," VR1(r) ".16b,v15.16b\n"		\
									\
		"tbl v12.16b,{v10.16b}," VR0(r) ".16b\n"		\
		"tbl v10.16b,{v10.16b}," VR1(r) ".16b\n"		\
		"tbl v15.16b,{v11.16b}," VR0(r) ".16b\n"		\
		"tbl v11.16b,{v11.16b}," VR1(r) ".16b\n"		\
									\
		"eor " VR0(r) ".16b,v15.16b,v12.16b\n"			\
		"eor " VR1(r) ".16b,v11.16b,v10.16b\n"			\
		/* lts for lower part */				\
		"ld1 { v10.4s },%[lt2]\n"				\
		"ld1 { v15.4s },%[lt3]\n"				\
		/* lower part */					\
		"tbl v12.16b,{v10.16b},v14.16b\n"			\
		"tbl v10.16b,{v10.16b},v13.16b\n"			\
		"tbl v11.16b,{v15.16b},v14.16b\n"			\
		"tbl v15.16b,{v15.16b},v13.16b\n"			\
									\
		"eor " VR0(r) ".16b," VR0(r) ".16b,v12.16b\n"		\
		"eor " VR1(r) ".16b," VR1(r) ".16b,v10.16b\n"		\
		"eor " VR0(r) ".16b," VR0(r) ".16b,v11.16b\n"		\
		"eor " VR1(r) ".16b," VR1(r) ".16b,v15.16b\n"		\
		:	UVR0(r), UVR1(r)				\
		:	[lt0] "Q" ((gf_clmul_mod_lt[4*(c)+0][0])),	\
		[lt1] "Q" ((gf_clmul_mod_lt[4*(c)+1][0])),		\
		[lt2] "Q" ((gf_clmul_mod_lt[4*(c)+2][0])),		\
		[lt3] "Q" ((gf_clmul_mod_lt[4*(c)+3][0]))		\
		:	"v10", "v11", "v12", "v13", "v14", "v15");	\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	MUL(c, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		_MULx2(c, R_23(r));					\
		_MULx2(c, R_01(r));					\
		break;							\
	case 2:								\
		_MULx2(c, R_01(r));					\
		break;							\
	default:							\
		ASM_BUG();						\
	}								\
}

#define	raidz_math_begin()	kfpu_begin()
#define	raidz_math_end()	kfpu_end()

/* Overkill... */
#if defined(_KERNEL)
#define	GEN_X_DEFINE_0_3()	\
register unsigned char w0 asm("v0") __attribute__((vector_size(16)));	\
register unsigned char w1 asm("v1") __attribute__((vector_size(16)));	\
register unsigned char w2 asm("v2") __attribute__((vector_size(16)));	\
register unsigned char w3 asm("v3") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_4_5()	\
register unsigned char w4 asm("v4") __attribute__((vector_size(16)));	\
register unsigned char w5 asm("v5") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_6_7()	\
register unsigned char w6 asm("v6") __attribute__((vector_size(16)));	\
register unsigned char w7 asm("v7") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_8_9()	\
register unsigned char w8 asm("v8") __attribute__((vector_size(16)));	\
register unsigned char w9 asm("v9") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_10_11()	\
register unsigned char w10 asm("v10") __attribute__((vector_size(16)));	\
register unsigned char w11 asm("v11") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_12_15()	\
register unsigned char w12 asm("v12") __attribute__((vector_size(16)));	\
register unsigned char w13 asm("v13") __attribute__((vector_size(16)));	\
register unsigned char w14 asm("v14") __attribute__((vector_size(16)));	\
register unsigned char w15 asm("v15") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_16()	\
register unsigned char w16 asm("v16") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_17()	\
register unsigned char w17 asm("v17") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_18_21()	\
register unsigned char w18 asm("v18") __attribute__((vector_size(16)));	\
register unsigned char w19 asm("v19") __attribute__((vector_size(16)));	\
register unsigned char w20 asm("v20") __attribute__((vector_size(16)));	\
register unsigned char w21 asm("v21") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_22_23()	\
register unsigned char w22 asm("v22") __attribute__((vector_size(16)));	\
register unsigned char w23 asm("v23") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_24_27()	\
register unsigned char w24 asm("v24") __attribute__((vector_size(16)));	\
register unsigned char w25 asm("v25") __attribute__((vector_size(16)));	\
register unsigned char w26 asm("v26") __attribute__((vector_size(16)));	\
register unsigned char w27 asm("v27") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_28_30()	\
register unsigned char w28 asm("v28") __attribute__((vector_size(16)));	\
register unsigned char w29 asm("v29") __attribute__((vector_size(16)));	\
register unsigned char w30 asm("v30") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_31()	\
register unsigned char w31 asm("v31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_32()	\
register unsigned char w32 asm("v31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_33_36()	\
register unsigned char w33 asm("v31") __attribute__((vector_size(16)));	\
register unsigned char w34 asm("v31") __attribute__((vector_size(16)));	\
register unsigned char w35 asm("v31") __attribute__((vector_size(16)));	\
register unsigned char w36 asm("v31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_37_38()	\
register unsigned char w37 asm("v31") __attribute__((vector_size(16)));	\
register unsigned char w38 asm("v31") __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_ALL()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_10_11()	\
	GEN_X_DEFINE_12_15()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_18_21()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_24_27()	\
	GEN_X_DEFINE_28_30()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36() 	\
	GEN_X_DEFINE_37_38()
#else
#define	GEN_X_DEFINE_0_3()	\
	unsigned char w0 __attribute__((vector_size(16)));	\
	unsigned char w1 __attribute__((vector_size(16)));	\
	unsigned char w2 __attribute__((vector_size(16)));	\
	unsigned char w3 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_4_5()	\
	unsigned char w4 __attribute__((vector_size(16)));	\
	unsigned char w5 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_6_7()	\
	unsigned char w6 __attribute__((vector_size(16)));	\
	unsigned char w7 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_8_9()	\
	unsigned char w8 __attribute__((vector_size(16)));	\
	unsigned char w9 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_10_11()	\
	unsigned char w10 __attribute__((vector_size(16)));	\
	unsigned char w11 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_12_15()	\
	unsigned char w12 __attribute__((vector_size(16)));	\
	unsigned char w13 __attribute__((vector_size(16)));	\
	unsigned char w14 __attribute__((vector_size(16)));	\
	unsigned char w15 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_16()	\
	unsigned char w16 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_17()	\
	unsigned char w17 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_18_21()	\
	unsigned char w18 __attribute__((vector_size(16)));	\
	unsigned char w19 __attribute__((vector_size(16)));	\
	unsigned char w20 __attribute__((vector_size(16)));	\
	unsigned char w21 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_22_23()	\
	unsigned char w22 __attribute__((vector_size(16)));	\
	unsigned char w23 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_24_27()	\
	unsigned char w24 __attribute__((vector_size(16)));	\
	unsigned char w25 __attribute__((vector_size(16)));	\
	unsigned char w26 __attribute__((vector_size(16)));	\
	unsigned char w27 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_28_30()	\
	unsigned char w28 __attribute__((vector_size(16)));	\
	unsigned char w29 __attribute__((vector_size(16)));	\
	unsigned char w30 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_31()	\
	unsigned char w31 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_32()	\
	unsigned char w32 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_33_36()	\
	unsigned char w33 __attribute__((vector_size(16)));	\
	unsigned char w34 __attribute__((vector_size(16)));	\
	unsigned char w35 __attribute__((vector_size(16)));	\
	unsigned char w36 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_37_38()	\
	unsigned char w37 __attribute__((vector_size(16)));	\
	unsigned char w38 __attribute__((vector_size(16)));
#define	GEN_X_DEFINE_ALL()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_10_11()	\
	GEN_X_DEFINE_12_15()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_18_21()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_24_27()	\
	GEN_X_DEFINE_28_30()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()	\
	GEN_X_DEFINE_37_38()
#endif
