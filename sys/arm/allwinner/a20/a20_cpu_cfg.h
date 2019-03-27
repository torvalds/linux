/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _A20_CPU_CFG_H_
#define _A20_CPU_CFG_H_

#define CPU_CFG_BASE		0xe1c25c00

#define CPU0_RST_CTRL		0x0040
#define CPU0_CTRL_REG		0x0044
#define CPU0_STATUS_REG 	0x0048

#define CPU1_RST_CTRL		0x0080
#define CPU1_CTRL_REG		0x0084
#define CPU1_STATUS_REG 	0x0088

#define GENER_CTRL_REG		0x0184

#define EVENT_IN_REG		0x0190
#define PRIVATE_REG		0x01a4

#define IDLE_CNT0_LOW_REG	0x0200
#define IDLE_CNT0_HIGH_REG	0x0204
#define IDLE_CNT0_CTRL_REG	0x0208

#define IDLE_CNT1_LOW_REG	0x0210
#define IDLE_CNT1_HIGH_REG	0x0214
#define IDLE_CNT1_CTRL_REG	0x0218

#define OSC24M_CNT64_CTRL_REG	0x0280
#define OSC24M_CNT64_LOW_REG	0x0284
#define OSC24M_CNT64_HIGH_REG	0x0288

#define LOSC_CNT64_CTRL_REG	0x0290
#define LOSC_CNT64_LOW_REG	0x0294
#define LOSC_CNT64_HIGH_REG	0x0298

#define CNT64_RL_EN		0x02 /* read latch enable */

uint64_t a20_read_counter64(void);

#endif /* _A20_CPU_CFG_H_ */
