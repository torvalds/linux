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

#include <sys/isa_defs.h>

#if defined(__aarch64__)

#include "vdev_raidz_math_aarch64_neon_common.h"

#define	SYN_STRIDE		4

#define	ZERO_STRIDE		8
#define	ZERO_DEFINE()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()
#define	ZERO_D			0, 1, 2, 3, 4, 5, 6, 7

#define	COPY_STRIDE		8
#define	COPY_DEFINE()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()
#define	COPY_D			0, 1, 2, 3, 4, 5, 6, 7

#define	ADD_STRIDE		8
#define	ADD_DEFINE()	\
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()
#define	ADD_D			0, 1, 2, 3, 4, 5, 6, 7

#define	MUL_STRIDE		4
#define	MUL_DEFINE()	\
	GEN_X_DEFINE_0_3()  \
	GEN_X_DEFINE_33_36()
#define	MUL_D			0, 1, 2, 3

#define	GEN_P_DEFINE() \
	GEN_X_DEFINE_0_3() \
	GEN_X_DEFINE_33_36()
#define	GEN_P_STRIDE		4
#define	GEN_P_P			0, 1, 2, 3

#define	GEN_PQ_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	GEN_PQ_STRIDE		4
#define	GEN_PQ_D		0, 1, 2, 3
#define	GEN_PQ_C		4, 5, 6, 7

#define	GEN_PQR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	GEN_PQR_STRIDE		4
#define	GEN_PQR_D		0, 1, 2, 3
#define	GEN_PQR_C		4, 5, 6, 7

#define	SYN_Q_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	SYN_Q_STRIDE		4
#define	SYN_Q_D			0, 1, 2, 3
#define	SYN_Q_X			4, 5, 6, 7

#define	SYN_R_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	SYN_R_STRIDE		4
#define	SYN_R_D			0, 1, 2, 3
#define	SYN_R_X			4, 5, 6, 7

#define	SYN_PQ_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	SYN_PQ_STRIDE		4
#define	SYN_PQ_D		0, 1, 2, 3
#define	SYN_PQ_X		4, 5, 6, 7

#define	REC_PQ_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_33_36()
#define	REC_PQ_STRIDE		4
#define	REC_PQ_X		0, 1, 2, 3
#define	REC_PQ_Y		4, 5, 6, 7
#define	REC_PQ_T		8, 9, 22, 23

#define	SYN_PR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	SYN_PR_STRIDE		4
#define	SYN_PR_D		0, 1, 2, 3
#define	SYN_PR_X		4, 5, 6, 7

#define	REC_PR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_33_36()
#define	REC_PR_STRIDE		4
#define	REC_PR_X		0, 1, 2, 3
#define	REC_PR_Y		4, 5, 6, 7
#define	REC_PR_T		8, 9, 22, 23

#define	SYN_QR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	SYN_QR_STRIDE		4
#define	SYN_QR_D		0, 1, 2, 3
#define	SYN_QR_X		4, 5, 6, 7

#define	REC_QR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_22_23()	\
	GEN_X_DEFINE_33_36()
#define	REC_QR_STRIDE		4
#define	REC_QR_X		0, 1, 2, 3
#define	REC_QR_Y		4, 5, 6, 7
#define	REC_QR_T		8, 9, 22, 23

#define	SYN_PQR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_16()	\
	GEN_X_DEFINE_17()	\
	GEN_X_DEFINE_33_36()
#define	SYN_PQR_STRIDE		 4
#define	SYN_PQR_D		 0, 1, 2, 3
#define	SYN_PQR_X		 4, 5, 6, 7

#define	REC_PQR_DEFINE() \
	GEN_X_DEFINE_0_3()	\
	GEN_X_DEFINE_4_5()	\
	GEN_X_DEFINE_6_7()	\
	GEN_X_DEFINE_8_9()	\
	GEN_X_DEFINE_31()	\
	GEN_X_DEFINE_32()	\
	GEN_X_DEFINE_33_36()
#define	REC_PQR_STRIDE		2
#define	REC_PQR_X		0, 1
#define	REC_PQR_Y		2, 3
#define	REC_PQR_Z		4, 5
#define	REC_PQR_XS		6, 7
#define	REC_PQR_YS		8, 9

#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(aarch64_neonx2);
/*
 * If compiled with -O0, gcc doesn't do any stack frame coalescing
 * and -Wframe-larger-than=1024 is triggered in debug mode.
 */
#pragma GCC diagnostic ignored "-Wframe-larger-than="
DEFINE_REC_METHODS(aarch64_neonx2);
#pragma GCC diagnostic pop

static boolean_t
raidz_will_aarch64_neonx2_work(void)
{
	return (B_TRUE); // __arch64__ requires NEON
}

const raidz_impl_ops_t vdev_raidz_aarch64_neonx2_impl = {
	.init = NULL,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(aarch64_neonx2),
	.rec = RAIDZ_REC_METHODS(aarch64_neonx2),
	.is_supported = &raidz_will_aarch64_neonx2_work,
	.name = "aarch64_neonx2"
};

#endif /* defined(__aarch64__) */
