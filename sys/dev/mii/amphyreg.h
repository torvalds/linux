/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_MII_AMTPHYREG_H_
#define	_DEV_MII_AMTPHYREG_H_

/*
 * AMD Am79C873 registers.
 */


#define MII_AMPHY_DSCR		0x10	/* Specified configuration register */a
#define DSCR_BP4B5B		0x8000	/* Bypass 4B5B encoding */
#define DSCR_BPSCR		0x4000	/* Bypass scrambler */
#define DSCR_BPALIGN		0x2000	/* Bypass symbol alignment */
#define DSCR_REPEATER		0x0800	/* Repeater mode */
#define DSCR_TX			0x0400	/* TX/FX mode control */
#define DSCR_UTP		0x0200	/* UTP/STP mode control */
#define DSCR_CLK25MDIS		0x0100	/* CLK25M disable */
#define DSCR_FGLNKTX		0x0080	/* Force good link at 100baseTX */
#define DSCR_LINKLEDCTL		0x0020	/* Link LED control */
#define DSCR_FDXLEDCTL		0x0010	/* FDX LED control */
#define DSCR_SMRTS		0x0008	/* Reset state machine */
#define DSCR_MFPSC		0x0004	/* Preamble surpression control */
#define DSCR_SLEEP		0x0002	/* Sleep mode */
#define DSCR_RLOUT		0x0001	/* Remote loopout control */

#define MII_AMPHY_DSCSR		0x11	/* Specified configuration and status */
#define DSCSR_100FDX		0x8000	/* 100MBps full duplex */
#define DSCSR_100HDX		0x4000	/* 100Mbps half duplex */
#define DSCSR_10FDX		0x2000	/* 10Mbps full duplex */
#define DSCSR_10HDX		0x1000	/* 10Mbps half duplex */
#define DSCSR_PADDR		0x01F0	/* PHY address */
#define DSCSR_ASTAT		0x000F	/* Autonegotiation status */

#define ASTAT_COMPLETE		0x8
#define ASTAT_PDLINK_READY_FAIL	0x7
#define ASTAT_PDLINK_READY	0x6
#define ASTAT_CONSTMATCH_FAIL	0x5
#define ASTAT_CONSTMATCH	0x4
#define ASTAT_ACKMATCH_FAIL	0x3
#define ASTAT_ACKMATCH		0x2
#define ASTAT_ABILITYMATCH	0x1
#define ASTAT_IDLE		0x0

#define MII_AMPHY_T10CSRSCR	0x12	/* 10baseT configuration/status */
#define T10CSRSCR_LPEN		0x4000	/* Link pulse enable */
#define T10CSRSCR_HBE		0x2000	/* Heartbeat enable */
#define T10CSRSCR_JABEN		0x0800	/* Jabber enable */
#define T10CSRSCR_SER		0x0400	/* Serial mode enable */
#define T10CSRSCR_POLR		0x0001	/* Polarity reversed */

#endif /* _DEV_MII_AMTPHYREG_H_ */
