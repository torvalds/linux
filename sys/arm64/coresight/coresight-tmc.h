/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#ifndef	_ARM64_CORESIGHT_CORESIGHT_TMC_H_
#define	_ARM64_CORESIGHT_CORESIGHT_TMC_H_

#define	TMC_RSZ		0x004 /* RAM Size Register */
#define	TMC_STS		0x00C /* Status Register */
#define	 STS_MEMERR	(1 << 5)
#define	 STS_EMPTY	(1 << 4)
#define	 STS_FTEMPTY	(1 << 3)
#define	 STS_TMCREADY	(1 << 2)
#define	 STS_TRIGGERED	(1 << 1)
#define	 STS_FULL	(1 << 0)
#define	TMC_RRD		0x010 /* RAM Read Data Register */
#define	TMC_RRP		0x014 /* RAM Read Pointer Register */
#define	TMC_RWP		0x018 /* RAM Write Pointer Register */
#define	TMC_TRG		0x01C /* Trigger Counter Register */
#define	TMC_CTL		0x020 /* Control Register */
#define	 CTL_TRACECAPTEN	(1 << 0)	/* Controls trace capture. */
#define	TMC_RWD		0x024 /* RAM Write Data Register */
#define	TMC_MODE	0x028 /* Mode Register */
#define	 MODE_HW_FIFO		2
#define	 MODE_SW_FIFO		1
#define	 MODE_CIRCULAR_BUFFER	0
#define	TMC_LBUFLEVEL	0x02C /* Latched Buffer Fill Level */
#define	TMC_CBUFLEVEL	0x030 /* Current Buffer Fill Level */
#define	TMC_BUFWM	0x034 /* Buffer Level Water Mark */
#define	TMC_RRPHI	0x038 /* RAM Read Pointer High Register */
#define	TMC_RWPHI	0x03C /* RAM Write Pointer High Register */
#define	TMC_AXICTL	0x110 /* AXI Control Register */
#define	 AXICTL_WRBURSTLEN_S	8
#define	 AXICTL_WRBURSTLEN_M	(0xf << AXICTL_WRBURSTLEN_S)
#define	 AXICTL_WRBURSTLEN_16	(0xf << AXICTL_WRBURSTLEN_S)
#define	 AXICTL_SG_MODE		(1 << 7)	/* Scatter Gather Mode */
#define	 AXICTL_CACHE_CTRL_BIT3	(1 << 5)
#define	 AXICTL_CACHE_CTRL_BIT2	(1 << 4)
#define	 AXICTL_CACHE_CTRL_BIT1	(1 << 3)
#define	 AXICTL_CACHE_CTRL_BIT0	(1 << 2)
#define	 AXICTL_AXCACHE_OS	(0xf << 2)
#define	 AXICTL_PROT_CTRL_BIT1	(1 << 1)
#define	 AXICTL_PROT_CTRL_BIT0	(1 << 0)
#define	TMC_DBALO	0x118 /* Data Buffer Address Low Register */
#define	TMC_DBAHI	0x11C /* Data Buffer Address High Register */
#define	TMC_FFSR	0x300 /* Formatter and Flush Status Register */
#define	TMC_FFCR	0x304 /* Formatter and Flush Control Register */
#define	 FFCR_EN_FMT		(1 << 0)
#define	 FFCR_EN_TI		(1 << 1)
#define	 FFCR_FON_FLIN		(1 << 4)
#define	 FFCR_FON_TRIG_EVT	(1 << 5)
#define	 FFCR_FLUSH_MAN		(1 << 6)
#define	 FFCR_TRIGON_TRIGIN	(1 << 8)
#define	TMC_PSCR	0x308 /* Periodic Synchronization Counter Register */
#define	TMC_ITATBMDATA0	0xED0 /* Integration Test ATB Master Data Register 0 */
#define	TMC_ITATBMCTR2	0xED4 /* Integration Test ATB Master Interface Control 2 Register */
#define	TMC_ITATBMCTR1	0xED8 /* Integration Test ATB Master Control Register 1 */
#define	TMC_ITATBMCTR0	0xEDC /* Integration Test ATB Master Interface Control 0 Register */
#define	TMC_ITMISCOP0	0xEE0 /* Integration Test Miscellaneous Output Register 0 */
#define	TMC_ITTRFLIN	0xEE8 /* Integration Test Trigger In and Flush In Register */
#define	TMC_ITATBDATA0	0xEEC /* Integration Test ATB Data Register 0 */
#define	TMC_ITATBCTR2	0xEF0 /* Integration Test ATB Control 2 Register */
#define	TMC_ITATBCTR1	0xEF4 /* Integration Test ATB Control 1 Register */
#define	TMC_ITATBCTR0	0xEF8 /* Integration Test ATB Control 0 Register */
#define	TMC_ITCTRL	0xF00 /* Integration Mode Control Register */
#define	TMC_CLAIMSET	0xFA0 /* Claim Tag Set Register */
#define	TMC_CLAIMCLR	0xFA4 /* Claim Tag Clear Register */
#define	TMC_LAR		0xFB0 /* Lock Access Register */
#define	TMC_LSR		0xFB4 /* Lock Status Register */
#define	TMC_AUTHSTATUS	0xFB8 /* Authentication Status Register */
#define	TMC_DEVID	0xFC8 /* Device Configuration Register */
#define	 DEVID_CONFIGTYPE_S	6
#define	 DEVID_CONFIGTYPE_M	(0x3 << DEVID_CONFIGTYPE_S)
#define	 DEVID_CONFIGTYPE_ETB	(0 << DEVID_CONFIGTYPE_S)
#define	 DEVID_CONFIGTYPE_ETR	(1 << DEVID_CONFIGTYPE_S)
#define	 DEVID_CONFIGTYPE_ETF	(2 << DEVID_CONFIGTYPE_S)
#define	TMC_DEVTYPE	0xFCC /* Device Type Identifier Register */
#define	TMC_PERIPHID4	0xFD0 /* Peripheral ID4 Register */
#define	TMC_PERIPHID5	0xFD4 /* Peripheral ID5 Register */
#define	TMC_PERIPHID6	0xFD8 /* Peripheral ID6 Register */
#define	TMC_PERIPHID7	0xFDC /* Peripheral ID7 Register */
#define	TMC_PERIPHID0	0xFE0 /* Peripheral ID0 Register */
#define	TMC_PERIPHID1	0xFE4 /* Peripheral ID1 Register */
#define	TMC_PERIPHID2	0xFE8 /* Peripheral ID2 Register */
#define	TMC_PERIPHID3	0xFEC /* Peripheral ID3 Register */
#define	TMC_COMPID0	0xFF0 /* Component ID0 Register */
#define	TMC_COMPID1	0xFF4 /* Component ID1 Register */
#define	TMC_COMPID2	0xFF8 /* Component ID2 Register */
#define	TMC_COMPID3	0xFFC /* Component ID3 Register */

#endif /* !_ARM64_CORESIGHT_CORESIGHT_TMC_H_ */
