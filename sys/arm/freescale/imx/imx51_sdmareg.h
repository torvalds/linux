/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/* Internal Registers definition for Freescale i.MX515 SDMA Core */

/* SDMA Core Instruction Memory Space */
#define	SDMA_IBUS_ROM_ADDR_BASE	0x0000
#define	SDMA_IBUS_ROM_ADDR_SIZE	0x07ff
#define	SDMA_IBUS_RAM_ADDR_BASE	0x1000
#define	SDMA_IBUS_RAM_ADDR_SIZE	0x1fff

/* SDMA Core Internal Registers */
#define	SDMA_MC0PTR	0x7000 /* AP (MCU) Channel 0 Pointer R */

#define	SDMA_CCPTR	0x7002 /* Current Channel Pointer R */
#define		SDMA_ECTL_CCPTR_MASK	0x0000ffff
#define		SDMA_ECTL_CCPTR_SHIFT	0

#define	SDMA_CCR	0x7003 /* Current Channel Register R */
#define		SDMA_ECTL_CCR_MASK	0x0000001f
#define		SDMA_ECTL_CCR_SHIFT	0

#define	SDMA_NCR	0x7004 /* Highest Pending Channel Register R */
#define		SDMA_ECTL_NCR_MASK	0x0000001f
#define		SDMA_ECTL_NCR_SHIFT	0

#define	SDMA_EVENTS	0x7005 /* External DMA Requests Mirror R */

#define	SDMA_CCPRI	0x7006 /* Current Channel Priority R */
#define		SDMA_ECTL_CCPRI_MASK	0x00000007
#define		SDMA_ECTL_CCPRI_SHIFT	0

#define	SDMA_NCPRI	0x7007 /* Next Channel Priority R */
#define		SDMA_ECTL_NCPRI_MASK	0x00000007
#define		SDMA_ECTL_NCPRI_SHIFT	0

#define	SDMA_ECOUNT	0x7009 /* OnCE Event Cell Counter R/W */
#define		SDMA_ECTL_ECOUNT_MASK	0x0000ffff
#define		SDMA_ECTL_ECOUNT_SHIFT	0

#define	SDMA_ECTL	0x700A /* OnCE Event Cell Control Register R/W */
#define		SDMA_ECTL_EN		(1 << 13)
#define		SDMA_ECTL_CNT		(1 << 12)
#define		SDMA_ECTL_ECTC_MASK	0x00000c00
#define		SDMA_ECTL_ECTC_SHIFT	10
#define		SDMA_ECTL_DTC_MASK	0x00000300
#define		SDMA_ECTL_DTC_SHIFT	8
#define		SDMA_ECTL_ATC_MASK	0x000000c0
#define		SDMA_ECTL_ATC_SHIFT	6
#define		SDMA_ECTL_ABTC_MASK	0x00000030
#define		SDMA_ECTL_ABTC_SHIFT	4
#define		SDMA_ECTL_AATC_MASK	0x0000000c
#define		SDMA_ECTL_AATC_SHIFT	2
#define		SDMA_ECTL_ATS_MASK	0x00000003
#define		SDMA_ECTL_ATS_SHIFT	0

#define	SDMA_EAA	0x700B /* OnCE Event Address Register A R/W */
#define		SDMA_ECTL_EAA_MASK	0x0000ffff
#define		SDMA_ECTL_EAA_SHIFT	0

#define	SDMA_EAB	0x700C /* OnCE Event Cell Address Register B R/W */
#define		SDMA_ECTL_EAB_MASK	0x0000ffff
#define		SDMA_ECTL_EAB_SHIFT	0

#define	SDMA_EAM	0x700D /* OnCE Event Cell Address Mask R/W */
#define		SDMA_ECTL_EAM_MASK	0x0000ffff
#define		SDMA_ECTL_EAM_SHIFT	0

#define	SDMA_ED		0x700E /* OnCE Event Cell Data Register R/W */
#define	SDMA_EDM	0x700F /* OnCE Event Cell Data Mask R/W */
#define	SDMA_RTB	0x7018 /* OnCE Real-Time Buffer R/W */

#define	SDMA_TB		0x7019 /* OnCE Trace Buffer R */
#define		SDMA_TB_TBF		(1 << 28)
#define		SDMA_TB_TADDR_MASK	0x0fffc000
#define		SDMA_TB_TADDR_SHIFT	14
#define		SDMA_TB_CHFADDR_MASK	0x00003fff
#define		SDMA_TB_CHFADDR_SHIFT	0

#define	SDMA_OSTAT	0x701A /* OnCE Status R */
#define		SDMA_OSTAT_PST_MASK	0x0000f000
#define		SDMA_OSTAT_PST_SHIFT	12
#define		SDMA_OSTAT_RCV		(1 << 11)
#define		SDMA_OSTAT_EDR		(1 << 10)
#define		SDMA_OSTAT_ODR		(1 << 9)
#define		SDMA_OSTAT_SWB		(1 << 8)
#define		SDMA_OSTAT_MST		(1 << 7)
#define		SDMA_OSTAT_ECDR_MASK	0x00000007
#define		SDMA_OSTAT_ECDR_SHIFT	0

#define	SDMA_MCHN0ADDR	0x701C /* Channel 0 Boot Address R */
#define		SDMA_MCHN0ADDR_SMS_Z	(1 << 14)
#define		SDMA_MCHN0ADDR_CHN0ADDR_MASK 0x00003fff
#define		SDMA_MCHN0ADDR_CHN0ADDR_SHIFT 0

#define	SDMA_MODE	0x701D /* Mode Status Register R */
#define		SDMA_MODE_DSPCtrl	(1 << 3)
#define		SDMA_MODE_AP_END	(1 << 0)

#define	SDMA_LOCK	0x701E /* Lock Status Register R */
#define		SDMA_LOCK_LOCK		(1 << 0)

#define	SDMA_EVENTS2	0x701F /* External DMA Requests Mirror #2 R */

#define	SDMA_HE		0x7020 /* AP Enable Register R */
#define	SDMA_PRIV	0x7022 /* Current Channel BP Privilege Register R */
#define		SDMA_PRIV_BPPRIV	(1 << 0)
#define	SDMA_PRF_CNT	0x7023 /* Profile Free Running Register R/W */
#define		SDMA_PRF_CNT_SEL_MASK	0xc0000000
#define		SDMA_PRF_CNT_SEL_SHIFT	30
#define		SDMA_PRF_CNT_EN		(1 << 29)
#define		SDMA_PRF_CNT_OFL	(1 << 22)
#define		SDMA_PRF_CNT_COUNTER_MASK 0x003fffff
#define		SDMA_PRF_CNT_COUNTER_SHIFT 0
