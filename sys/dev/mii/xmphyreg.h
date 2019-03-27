/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000
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

#ifndef _DEV_MII_XMPHYREG_H_
#define	_DEV_MII_XMPHYREG_H_

/*
 * XaQti XMAC II PHY registers
 */

#define XMPHY_MII_BMCR		0x00
#define XMPHY_BMCR_RESET	0x8000
#define XMPHY_BMCR_LOOP		0x4000
#define XMPHY_BMCR_AUTOEN	0x1000	/* Autoneg enabled */
#define XMPHY_BMCR_PDOWN	0x0800	/* Power down */
#define XMPHY_BMCR_ISO		0x0400	/* Isolate */
#define XMPHY_BMCR_STARTNEG	0x0200	/* Restart autoneg */
#define XMPHY_BMCR_FDX		0x0100	/* Duplex mode */

#define XMPHY_MII_BMSR		0x01
#define XMPHY_BMSR_EXTSTS	0x0100	/* Extended status present */
#define XMPHY_BMSR_ACOMP	0x0020	/* Autoneg complete */
#define XMPHY_BMSR_RFAULT	0x0010	/* Remote fault condition occurred */
#define XMPHY_BMSR_ANEG		0x0008	/* Autoneg capable */
#define XMPHY_BMSR_LINK		0x0004	/* Link status */
#define XMPHY_BMSR_EXT		0x0001	/* Extended capability */

#define XMPHY_MII_ANAR		0x04
#define XMPHY_ANAR_NP		0x8000	/* Next page */
#define XMPHY_ANAR_ACK		0x4000	/* Next page or base received */
#define XMPHY_ANAR_RFBITS	0x3000	/* Remote fault bits */
#define XMPHY_ANAR_PAUSEBITS	0x0180	/* Pause bits */
#define XMPHY_ANAR_HDX		0x0040	/* Select half duplex */
#define XMPHY_ANAR_FDX		0x0020	/* Select full duplex */

#define XMPHY_MII_ANLPAR	0x05
#define XMPHY_ANLPAR_NP		0x8000	/* Next page */
#define XMPHY_ANLPAR_ACK	0x4000	/* Next page or base received */
#define XMPHY_ANLPAR_RFBITS	0x3000	/* Remote fault bits */
#define XMPHY_ANLPAR_PAUSEBITS	0x0180	/* Pause bits */
#define XMPHY_ANLPAR_HDX	0x0040	/* Select half duplex */
#define XMPHY_ANLPAR_FDX	0x0020	/* Select full duplex */

#define XMPHY_RF_OK		0x0000	/* No error -- link is good */
#define XMPHY_RF_LINKFAIL	0x1000	/* Link failure */
#define XMPHY_RF_OFFLINE	0x2000	/* Offline */
#define XMPHY_RF_ANEGFAIL	0x3000	/* Autonegotiation error */

#define XMPHY_PAUSE_NOPAUSE	0x0000	/* No pause possible */
#define XMPHY_PAUSE_ASYMETRIC	0x0080	/* Asymetric pause toward LP */
#define XMPHY_PAUSE_SYMETRIC	0x0100	/* Symetric pause */
#define XMPHY_PAUSE_BOTH	0x0180	/* Both sym and asym pause */

#define XMPHY_MII_ANER		0x06
#define XMPHY_ANER_LPNP		0x0008	/* Link partner can next page */
#define XMPHY_ANER_NP		0x0004	/* Local PHY can next page */
#define XMPHY_ANER_RX		0x0002	/* Next page received */

#define XMPHY_MII_NEXTP		0x07	/* Next page */
#define XMPHY_NEXTP_MORE	0x8000	/* More next pages to follow */
#define XMPHY_NEXTP_ACK1	0x4000	/* Ack bit received OK */
#define XMPHY_NEXTP_MP		0x2000	/* Page is message page */
#define XMPHY_NEXTP_ACK2	0x1000	/* can comply with message (r/o) */
#define XMPHY_NEXTP_TOGGLE	0x0800	/* sync with LP */
#define XMPHY_NEXTP_MESSAGE	0x07FF	/* message */

#define XMPHY_MII_NEXTPLP	0x08	/* Next page of link partner */
#define XMPHY_NEXTPLP_MORE	0x8000	/* More next pages to follow */
#define XMPHY_NEXTPLP_ACK1	0x4000	/* Ack bit received OK */
#define XMPHY_NEXTPLP_MP	0x2000	/* Page is message page */
#define XMPHY_NEXTPLP_ACK2	0x1000	/* can comply with message (r/o) */
#define XMPHY_NEXTPLP_TOGGLE	0x0800	/* sync with LP */
#define XMPHY_NEXTPLP_MESSAGE	0x07FF	/* message */

#define XMPHY_MII_EXTSTS	0x0F	/* Extended status */
#define XMPHY_EXTSTS_FDX	0x8000	/* 1000base-X FD capable */
#define XMPHY_EXTSTS_HDX	0x4000	/* 1000base-X HD capable */

#define XMPHY_MII_RESAB		0x10	/* Resolved ability */
#define XMPHY_RESAB_PAUSEBITS	0x0180	/* Pause bits */
#define XMPHY_RESAB_HDX		0x0040	/* Half duplex selected */
#define XMPHY_RESAB_FDX		0x0020	/* Full duplex selected */
#define XMPHY_RESAB_ABLMIS	0x0010	/* Ability mismatch */
#define XMPHY_RESAB_PAUSEMIS	0x0008	/* Pause mismatch */

#endif /* _DEV_MII_XMPHYREG_H_ */
