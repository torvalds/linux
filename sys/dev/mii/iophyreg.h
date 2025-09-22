/*	$OpenBSD: iophyreg.h,v 1.1 1999/10/12 16:59:29 jason Exp $	*/
/*	$NetBSD: iophyreg.h,v 1.2 1999/09/16 05:58:18 soren Exp $	*/

/*
 * Copyright (c) 1998, 1999 Soren S. Jorvang.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
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
 */

#ifndef _DEV_MII_IOPHYREG_H_
#define	_DEV_MII_IOPHYREG_H_

/*
 * Intel 82553 PHY registers
 */

#define	MII_IOPHY_EXT0		0x10	/* Extended Register 0 */
#define	EXT0_JABDIS		0x8000	/* jabber disabled */
#define	EXT0_LINKDIS		0x4000	/* link integrity disable */
#define	EXT0_TEST4		0x2000
#define	EXT0_TEST3		0x1000
#define	EXT0_TEST2		0x0800
#define	EXT0_TEST1		0x0400
#define	EXT0_TEST0		0x0200
#define	EXT0_FORCE100		0x0100	/* force 100 Mbps operation */
#define	EXT0_REVMASK		0x00e0	/* 82553 chip revision */
#define	EXT0_HSQ		0x0010
#define	EXT0_LSQ		0x0008
#define	EXT0_WAKEUP		0x0004	/* disable auto power-down */
#define	EXT0_SPEED		0x0002	/* current speed 10/100 */
#define	EXT0_DUPLEX		0x0001	/* current duplex setting */

#define	MII_IOPHY_EXT1		0x14	/* Extended Register 1 */
#define	EXT1_PAIR_SKEW_ERR	0x8000	/* pair skew error */
#define	EXT1_DC_BALANCE_ERR	0x4000	/* DC balance error */
#define	EXT1_INVALID_CODE_ERR	0x2000	/* invalid code error */
#define	EXT1_BAD_CODE_ERR	0x1000	/* bad code error */
#define	EXT1_EOP_ERR		0x0800	/* EOP error */
#define	EXT1_MANCHESTER_ERR	0x0400	/* Manchester code error */
#define	EXT1_CH2_EOF_ERR	0x0200	/* channel 2 EOF detection error */
#define	EXT1_DTE_MODE_SEL	0x0100	/* external DTE mode */
#define	EXT1_LINE_RPTR_MODE_SEL	0x0080	/* line repeater mode */
#define	EXT1_EXT_TEST_MODE_SEL	0x0040	/* external test mode */
#define	EXT1_MII_RPTR_MODE_SEL	0x0020	/* MII repeater mode */
#define	EXT1_CH2_POLARITY_ERR	0x0010	/* channel 2 polarity error */
#define	EXT1_CH3_POLARITY_ERR	0x0008	/* channel 3 polarity error */
#define	EXT1_CH4_POLARITY_ERR	0x0004	/* channel 4 polarity error */
#define	EXT1_CH2_SFD_DETECT_ERR	0x0002	/* channel 2 SFD not found */

#define	MII_IOPHY_EXT2		0x15	/* Extended Register 2 (C step only) */
#define	EXT2_AUTONEG_SEL	0x8000	/* autonegotiation selected */
#define	EXT2_CH3_SFD_ERR	0x4000	/* channel 3 SFD not found */
#define	EXT2_CH4_SFD_ERR	0x2000	/* channel 4 SFD not found */

#endif /* _DEV_MII_IOPHYREG_H_ */
