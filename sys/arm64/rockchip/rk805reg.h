/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#ifndef _RK805REG_H_
#define	 _RK805REG_H_

#define	RK805_CHIP_NAME		0x17
#define	RK805_CHIP_VER		0x18
#define	RK805_OTP_VER		0x19

#define	RK805_DCDC_EN		0x23
#define	RK808_LDO_EN		0x24
#define	RK805_SLEEP_DCDC_EN	0x25
#define	RK805_SLEEP_LDO_EN	0x26
#define	RK805_LDO_EN		0x27
#define	RK805_SLEEP_LDO_LP_EN	0x2A

#define	RK805_DCDC1_CONFIG	0x2E
#define	RK805_DCDC1_ON_VSEL	0x2F
#define	RK805_DCDC1_SLEEP_VSEL	0x30
#define	RK805_DCDC2_CONFIG	0x32
#define	RK805_DCDC2_ON_VSEL	0x33
#define	RK805_DCDC2_SLEEP_VSEL	0x34
#define	RK805_DCDC3_CONFIG	0x36
#define	RK805_DCDC4_CONFIG	0x37
#define	RK805_DCDC4_ON_VSEL	0x38
#define	RK805_DCDC4_SLEEP_VSEL	0x39
#define	RK805_LDO1_ON_VSEL	0x3B
#define	RK805_LDO1_SLEEP_VSEL	0x3C
#define	RK805_LDO2_ON_VSEL	0x3D
#define	RK805_LDO2_SLEEP_VSEL	0x3E
#define	RK805_LDO3_ON_VSEL	0x3F
#define	RK805_LDO3_SLEEP_VSEL	0x40

enum rk805_regulator {
	RK805_DCDC1 = 0,
	RK805_DCDC2,
	RK805_DCDC3,
	RK805_DCDC4,
	RK805_LDO1,
	RK805_LDO2,
	RK805_LDO3,
};

enum rk808_regulator {
	RK808_DCDC1 = 0,
	RK808_DCDC2,
	RK808_DCDC3,
	RK808_DCDC4,
	RK808_LDO1,
	RK808_LDO2,
	RK808_LDO3,
	RK808_LDO4,
	RK808_LDO5,
	RK808_LDO6,
	RK808_LDO7,
	RK808_LDO8,
	RK808_SWITCH1,
	RK808_SWITCH2,
};

#endif /* _RK805REG_H_ */
