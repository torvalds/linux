/*-
 * Copyright 2015 John Wehle <john@feith.com>
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

#ifndef	_ARM_AMLOGIC_AML8726_SOC_H
#define	_ARM_AMLOGIC_AML8726_SOC_H

#define	AML_SOC_AOBUS_BASE_ADDR		0xc8100000
#define	AML_SOC_CBUS_BASE_ADDR		0xc1100000

void aml8726_identify_soc(void);

/* cbus */
#define	AML_SOC_HW_REV_REG		0x7d4c
#define	AML_SOC_HW_REV_UNKNOWN		0xffffffff
#define	AML_SOC_HW_REV_M3		0x15
#define	AML_SOC_HW_REV_M6		0x16
#define	AML_SOC_HW_REV_M6TV		0x17
#define	AML_SOC_HW_REV_M6TVL		0x18
#define	AML_SOC_HW_REV_M8		0x19
#define	AML_SOC_HW_REV_M8B		0x1b

#define	AML_SOC_METAL_REV_REG		0x81a8
#define	AML_SOC_METAL_REV_UNKNOWN	0xffffffff
#define	AML_SOC_M8_METAL_REV_A		0x11111111
#define	AML_SOC_M8_METAL_REV_M2_A	0x11111112
#define	AML_SOC_M8_METAL_REV_B		0x11111113
#define	AML_SOC_M8_METAL_REV_C		0x11111133
#define	AML_SOC_M8B_METAL_REV_A		0x11111111

extern uint32_t aml8726_soc_hw_rev;
extern uint32_t aml8726_soc_metal_rev;

#endif /* _ARM_AMLOGIC_AML8726_SOC_H */
