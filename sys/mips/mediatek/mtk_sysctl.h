/*-
 * Copyright (c) 2016 Stanislav Galabov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _MTK_SYSCTL_H_
#define _MTK_SYSCTL_H_

/* System Control */
#define SYSCTL_CHIPID0_3	0x00
#define SYSCTL_CHIPID4_7	0x04

#define SYSCTL_REVID		0x0C
#define SYSCTL_REVID_MASK	0xFFFF
#define SYSCTL_MT7621_REV_E	0x0101

#define SYSCTL_SYSCFG		0x10
#define SYSCTL_SYSCFG1		0x14
#define SYSCTL_CLKCFG0		0x2C
#define SYSCTL_CLKCFG1		0x30
#define SYSCTL_RSTCTRL		0x34
#define SYSCTL_GPIOMODE		0x60

#define SYSCTL_CUR_CLK_STS	0x44

#define SYSCTL_MT7620_CPLL_CFG0	0x54
#define SYSCTL_MT7620_CPLL_CFG1	0x58

#define SYSCFG1_USB_HOST_MODE	(1<<10)

#define RT3350_CHIPID0_3	0x33335452

#define MTK_UNKNOWN_CHIPID0_3	0x6E6B6E75	/* "unkn" */
#define MTK_UNKNOWN_CHIPID4_7	0x206E776F	/* "own " */

extern uint32_t	mtk_sysctl_get(uint32_t);
extern void	mtk_sysctl_set(uint32_t, uint32_t);
extern void	mtk_sysctl_clr_set(uint32_t, uint32_t, uint32_t);

#endif /* _MTK_SYSCTL_H_ */
