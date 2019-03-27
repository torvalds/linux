/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, Pyun YongHyeon <yongari@FreeBSD.org>
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

#ifndef _DEV_MII_RDCPHYREG_H_
#define	_DEV_MII_RDCPHYREG_H_

#define	MII_RDCPHY_DEBUG	0x11
#define	DEBUG_JABBER_DIS	0x0040
#define	DEBUG_LOOP_BACK_10MBPS	0x0400

#define	MII_RDCPHY_CTRL		0x14
#define	CTRL_SQE_ENB		0x0100
#define	CTRL_NEG_POLARITY	0x0400
#define	CTRL_AUTO_POLARITY	0x0800
#define	CTRL_MDIXSEL_RX		0x2000
#define	CTRL_MDIXSEL_TX		0x4000
#define	CTRL_AUTO_MDIX_DIS	0x8000

#define	MII_RDCPHY_CTRL2	0x15
#define	CTRL2_LED_DUPLEX	0x0000
#define	CTRL2_LED_DUPLEX_COL	0x0008
#define	CTRL2_LED_ACT		0x0010
#define	CTRL2_LED_SPEED_ACT	0x0018
#define	CTRL2_LED_BLK_100MBPS_DIS	0x0020
#define	CTRL2_LED_BLK_10MBPS_DIS	0x0040
#define	CTRL2_LED_BLK_LINK_ACT_DIS	0x0080
#define	CTRL2_SDT_THRESH_MASK	0x3E00
#define	CTRL2_TIMING_ERR_SEL	0x4000
#define	CTRL2_LED_BLK_80MS	0x8000
#define	CTRL2_LED_BLK_160MS	0x0000
#define	CTRL2_LED_MASK		0x0018

#define	MII_RDCPHY_STATUS	0x16
#define	STATUS_AUTO_MDIX_RX	0x0200
#define	STATUS_AUTO_MDIX_TX	0x0400
#define	STATUS_NEG_POLARITY	0x0800
#define	STATUS_FULL_DUPLEX	0x1000
#define	STATUS_SPEED_10		0x0000
#define	STATUS_SPEED_100	0x2000
#define	STATUS_SPEED_MASK	0x6000
#define	STATUS_LINK_UP		0x8000

/* Analog test register 2 */
#define	MII_RDCPHY_TEST2	0x1A
#define	TEST2_PWR_DOWN		0x0200

#endif /* _DEV_MII_RDCPHYREG_H_ */
