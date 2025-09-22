/*	$OpenBSD: ipgphyreg.h,v 1.3 2015/07/19 06:28:12 yuo Exp $	*/

/*-
 * Copyright (c) 2006, Pyun YongHyeon
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
 */

#ifndef _DEV_MII_IPGPHYREG_H_
#define _DEV_MII_IPGPHYREG_H_

/*
 * Registers for the IC Plus IPGA internal PHY.
 */

/* PHY specific control & status register. IP1001 only. */
#define IPGPHY_SCSR			0x10
#define IPGPHY_SCSR_RXPHASE_SEL	0x0001
#define IPGPHY_SCSR_TXPHASE_SEL	0x0002
#define IPGPHY_SCSR_REPEATOR_MODE	0x0004
#define IPGPHY_SCSR_RESERVED1_DEF	0x0008
#define IPGPHY_SCSR_RXCLK_DRV_MASK	0x0060
#define IPGPHY_SCSR_RXCLK_DRV_DEF	0x0040
#define IPGPHY_SCSR_RXD_DRV_MASK	0x0180
#define IPGPHY_SCSR_RXD_DRV_DEF	0x0100
#define IPGPHY_SCSR_JABBER_ENB	0x0200
#define IPGPHY_SCSR_HEART_BEAT_ENB	0x0400
#define IPGPHY_SCSR_DOWNSHIFT_ENB	0x0800
#define IPGPHY_SCSR_RESERVED2_DEF	0x1000
#define IPGPHY_SCSR_LED_DRV_4MA	0x0000
#define IPGPHY_SCSR_LED_DRV_8MA	0x2000
#define IPGPHY_SCSR_LED_MODE_MASK	0xC000
#define IPGPHY_SCSR_LED_MODE_DEF	0x0000

/* PHY link status register. IP1001 only. */
#define IPGPHY_LSR			0x11
#define IPGPHY_LSR_JABBER_DET	0x0200
#define IPGPHY_LSR_APS_SLEEP		0x0400
#define IPGPHY_LSR_MDIX		0x0800
#define IPGPHY_LSR_FULL_DUPLEX	0x1000
#define IPGPHY_LSR_SPEED_10		0x0000
#define IPGPHY_LSR_SPEED_100		0x2000
#define IPGPHY_LSR_SPEED_1000	0x4000
#define IPGPHY_LSR_SPEED_MASK	0x6000
#define IPGPHY_LSR_LINKUP		0x8000

/* PHY specific control register 2. IP1001 only. */
#define IPGPHY_SCR
#define IPGPHY_SCR_SEW_RATE_MASK	0x0003
#define IPGPHY_SCR_SEW_RATE_DEF	0x0003
#define IPGPHY_SCR_AUTO_XOVER	0x0004
#define IPGPHY_SCR_SPEED_10_100_ENB	0x0040
#define IPGPHY_SCR_FIFO_LATENCY_2	0x0000
#define IPGPHY_SCR_FIFO_LATENCY_3	0x0080
#define IPGPHY_SCR_FIFO_LATENCY_4	0x0100
#define IPGPHY_SCR_FIFO_LATENCY_5	0x0180
#define IPGPHY_SCR_MDIX_ENB		0x0200
#define IPGPHY_SCR_RESERVED_DEF	0x0400
#define IPGPHY_SCR_APS_ON		0x0800

#endif /* _DEV_MII_IPGPHYREG_H_ */
