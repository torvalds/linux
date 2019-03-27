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

#ifndef _RK30_PMU_H_
#define	_RK30_PMU_H_

#define	RK30_PMU_BASE			0xF0004000

#define	PMU_WAKEUP_CFG0			0x00
#define	PMU_WAKEUP_CFG1			0x04
#define	PMU_PWRDN_CON			0x08
#define	PMU_PWRDN_ST			0x0c
#define	PMU_INT_CON			0x10
#define	PMU_INT_ST			0x14
#define	PMU_MISC_CON			0x18
#define	PMU_OSC_CNT			0x1c
#define	PMU_PLL_CNT			0x20
#define	PMU_PMU_CNT			0x24
#define	PMU_DDRIO_PWRON_CNT		0x28
#define	PMU_WAKEUP_RST_CLR_CNT		0x2c
#define	PMU_SCU_PWRDWN_CNT		0x30
#define	PMU_SCU_PWRUP_CNT		0x34
#define	PMU_MISC_CON1			0x38
#define	PMU_GPIO0_CON			0x3c
#define	PMU_SYS_REG0			0x40
#define	PMU_SYS_REG1			0x44
#define	PMU_SYS_REG2			0x48
#define	PMU_SYS_REG3			0x4c
#define	PMU_STOP_INT_DLY		0x60
#define	PMU_GPIO0A_PULL			0x64
#define	PMU_GPIO0B_PULL			0x68

void rk30_pmu_gpio_pud(uint32_t pin, uint32_t state);

#endif /* _RK30_PMU_H_ */
