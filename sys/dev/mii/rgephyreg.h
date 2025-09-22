/*	$OpenBSD: rgephyreg.h,v 1.10 2023/04/05 10:45:07 kettenis Exp $	*/
/*
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * $FreeBSD: rgephyreg.h,v 1.1 2003/09/11 03:53:46 wpaul Exp $
 */

#ifndef _DEV_MII_RGEPHYREG_H_
#define	_DEV_MII_RGEPHYREG_H_

#define RGEPHY_8211B		2
#define RGEPHY_8211C		3
#define RGEPHY_8211F		6

/*
 * Realtek 8169S/8110S gigE PHY registers
 */

/* RTL8211B(L)/RTL8211C(L) */
#define RGEPHY_CR		0x10	/* PHY Specific Control */
#define RGEPHY_CR_ASSERT_CRS	0x0800
#define RGEPHY_CR_FORCE_LINK	0x0400
#define RGEPHY_CR_MDI_MASK	0x0060
#define RGEPHY_CR_MDIX_AUTO	0x0040
#define RGEPHY_CR_MDIX_MANUAL	0x0020
#define RGEPHY_CR_MDI_MANUAL	0x0000
#define RGEPHY_CR_CLK125_DIS	0x0010
#define RGEPHY_CR_ALDPS		0x0004	/* RTL8251 only */
#define RGEPHY_CR_JABBER_DIS	0x0001

/* RTL8211B(L)/RTL8211C(L) */
#define RGEPHY_SR		0x11	/* PHY Specific Status */
#define RGEPHY_SR_SPEED_1000MBPS	0x8000
#define RGEPHY_SR_SPEED_100MBPS		0x4000
#define RGEPHY_SR_SPEED_10MBPS		0x0000
#define RGEPHY_SR_SPEED_MASK		0xc000
#define RGEPHY_SR_FDX			0x2000	/* full duplex */
#define RGEPHY_SR_PAGE_RECEIVED		0x1000	/* new page received */
#define RGEPHY_SR_SPD_DPLX_RESOLVED	0x0800	/* speed/duplex resolved */
#define RGEPHY_SR_LINK			0x0400	/* link up */
#define RGEPHY_SR_MDI_XOVER		0x0040	/* MDI crossover */
#define RGEPHY_SR_ALDPS			0x0008	/* RTL8211C(L) only */
#define RGEPHY_SR_JABBER		0x0001	/* Jabber */
#define RGEPHY_SR_SPEED(X)		((X) & RGEPHY_SR_SPEED_MASK)

/* RTL8211F */
#define RGEPHY_F_SR		0x1A	/* PHY Specific Status */
#define RGEPHY_F_SR_SPEED_1000MBPS	0x0020
#define RGEPHY_F_SR_SPEED_100MBPS	0x0010
#define RGEPHY_F_SR_SPEED_10MBPS	0x0000
#define RGEPHY_F_SR_SPEED_MASK		0x0030
#define RGEPHY_F_SR_FDX			0x0008
#define RGEPHY_F_SR_LINK		0x0004
#define RGEPHY_F_SR_SPEED(X)		((X) & RGEPHY_F_SR_SPEED_MASK)

#define RGEPHY_LC		0x18	/* PHY LED Control Register */
#define RGEPHY_LC_P2		0x1A	/* PHY LED Control Register, Page 2 */
#define RGEPHY_LC_DISABLE	0x8000	/* disable leds */
/* Led pusle strething */
#define RGEPHY_LC_PULSE_1_3S	0x7000
#define RGEPHY_LC_PULSE_670MS	0x6000	
#define RGEPHY_LC_PULSE_340MS	0x5000	
#define RGEPHY_LC_PULSE_170MS	0x4000	
#define RGEPHY_LC_PULSE_84MS	0x3000	
#define RGEPHY_LC_PULSE_42MS	0x2000	
#define RGEPHY_LC_PULSE_21MS	0x1000	
#define RGEPHY_LC_PULSE_0MS	0x0000	
#define RGEPHY_LC_LINK		0x0008 /* Link and speed indicated by combination of leds */
#define RGEPHY_LC_DUPLEX	0x0004
#define RGEPHY_LC_RX		0x0002
#define RGEPHY_LC_TX		0x0001

#define RGEPHY_PS		0x1F	/* Page Select Register */
#define RGEPHY_PS_PAGE_0	0x0000
#define RGEPHY_PS_PAGE_1	0x0001
#define RGEPHY_PS_PAGE_2	0x0002
#define RGEPHY_PS_PAGE_3	0x0003
#define RGEPHY_PS_PAGE_4	0x0004

/* RTL8211F */
#define RGEPHY_PS_PAGE_MII	0x0d08
#define RGEPHY_MIICR1		0x11
#define RGEPHY_MIICR1_TXDLY_EN	0x0100
#define RGEPHY_MIICR2		0x15
#define RGEPHY_MIICR2_RXDLY_EN	0x0008

#endif /* _DEV_RGEPHY_MIIREG_H_ */
