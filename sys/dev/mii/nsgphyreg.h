/*	$OpenBSD: nsgphyreg.h,v 1.5 2005/05/27 09:24:01 brad Exp $	*/
/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 2001
 *	Bill Paul <wpaul@bsdi.com>.  All rights reserved.
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
 * $FreeBSD: /usr/local/www/cvsroot/FreeBSD/src/sys/dev/mii/nsgphyreg.h,v 1.3 2002/04/29 11:57:28 phk Exp $
 */

#ifndef _DEV_MII_NSGPHYREG_H_
#define	_DEV_MII_NSGPHYREG_H_

/*
 * NatSemi DP83891 registers
 */

#define NSGPHY_MII_STRAPOPT	0x10	/* Strap options */
#define NSGPHY_STRAPOPT_PHYADDR	0xF800	/* PHY address */
#define NSGPHY_STRAPOPT_COMPAT	0x0400	/* Broadcom compat mode */
#define NSGPHY_STRAPOPT_MMSE	0x0200	/* Manual master/slave enable */
#define NSGPHY_STRAPOPT_ANEG	0x0100	/* Autoneg enable */
#define NSGPHY_STRAPOPT_MMSV	0x0080	/* Manual master/slave setting */
#define NSGPHY_STRAPOPT_1000HDX	0x0010	/* Advertise 1000 half-duplex */
#define NSGPHY_STRAPOPT_1000FDX	0x0008	/* Advertise 1000 full-duplex */
#define NSGPHY_STRAPOPT_100_ADV	0x0004	/* Advertise 100 full/half-duplex */
#define NSGPHY_STRAPOPT_SPEED1	0x0002	/* speed selection */
#define NSGPHY_STRAPOPT_SPEED0	0x0001	/* speed selection */
#define NSGPHY_STRAPOPT_SPDSEL	(NSGPHY_STRAPOPT_SPEED1|NSGPHY_STRAPOPT_SPEED0)

#define NSGPHY_MII_PHYSUP	0x11	/* PHY support/current status */
#define PHY_SUP_SPEED1		0x0010	/* speed bit 1 */
#define PHY_SUP_SPEED0		0x0008	/* speed bit 1 */
#define NSGPHY_PHYSUP_SPEED1	0x0010	/* speed status */
#define NSGPHY_PHYSUP_SPEED0	0x0008	/* speed status */
#define NSGPHY_PHYSUP_SPDSTS	(NSGPHY_PHYSUP_SPEED1|NSGPHY_PHYSUP_SPEED0)
#define NSGPHY_PHYSUP_LNKSTS	0x0004	/* link status */
#define PHY_SUP_LINK		0x0004	/* link status */
#define PHY_SUP_DUPLEX		0x0002	/* 1 == full-duplex */
#define NSGPHY_PHYSUP_DUPSTS	0x0002	/* duplex status 1 == full */
#define NSGPHY_PHYSUP_10BT	0x0001	/* 10baseT resolved */

#define NSGPHY_SPDSTS_1000	0x0010
#define NSGPHY_SPDSTS_100	0x0008
#define NSGPHY_SPDSTS_10	0x0000

#endif /* _DEV_NSGPHY_MIIREG_H_ */
