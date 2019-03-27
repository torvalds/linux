/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon
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

#ifndef	_DEV_MII_ATPHYREG_H_
#define	_DEV_MII_ATPHYREG_H_

/*
 * Registers for the Attansic/Atheros Gigabit PHY.
 */

/* Special Control Register */
#define ATPHY_SCR			0x10
#define ATPHY_SCR_JABBER_DISABLE	0x0001
#define ATPHY_SCR_POLARITY_REVERSAL	0x0002
#define ATPHY_SCR_SQE_TEST		0x0004
#define ATPHY_SCR_MAC_PDOWN		0x0008
#define ATPHY_SCR_CLK125_DISABLE	0x0010
#define ATPHY_SCR_MDI_MANUAL_MODE	0x0000
#define ATPHY_SCR_MDIX_MANUAL_MODE	0x0020
#define ATPHY_SCR_AUTO_X_1000T		0x0040
#define ATPHY_SCR_AUTO_X_MODE		0x0060
#define ATPHY_SCR_10BT_EXT_ENABLE	0x0080
#define ATPHY_SCR_MII_5BIT_ENABLE	0x0100
#define ATPHY_SCR_SCRAMBLER_DISABLE	0x0200
#define ATPHY_SCR_FORCE_LINK_GOOD	0x0400
#define ATPHY_SCR_ASSERT_CRS_ON_TX	0x0800

/* Special Status Register. */
#define ATPHY_SSR			0x11
#define ATPHY_SSR_SPD_DPLX_RESOLVED	0x0800
#define ATPHY_SSR_DUPLEX		0x2000
#define ATPHY_SSR_SPEED_MASK		0xC000
#define ATPHY_SSR_10MBS			0x0000
#define ATPHY_SSR_100MBS		0x4000
#define ATPHY_SSR_1000MBS		0x8000

#endif	/* _DEV_MII_ATPHYREG_H_ */
