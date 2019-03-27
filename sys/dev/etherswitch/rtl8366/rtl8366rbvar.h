/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Hiroki Mori.
 * Copyright (c) 2011-2012 Stefan Bethke.
 * All rights reserved.
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

#ifndef _DEV_ETHERSWITCH_RTL8366RBVAR_H_
#define	_DEV_ETHERSWITCH_RTL8366RBVAR_H_

#define	RTL8366RB		0
#define	RTL8366SR		1

#define RTL8366_IIC_ADDR	0xa8
#define RTL_IICBUS_TIMEOUT	100	/* us */
#define RTL_IICBUS_READ		1
#define	RTL_IICBUS_WRITE	0
/* number of times to try and select the chip on the I2C bus */
#define RTL_IICBUS_RETRIES	3
#define RTL_IICBUS_RETRY_SLEEP	(hz/1000)

/* Register definitions */

/* Switch Global Configuration */
#define RTL8366_SGCR				0x0000
#define RTL8366_SGCR_EN_BC_STORM_CTRL		0x0001
#define RTL8366_SGCR_MAX_LENGTH_MASK		0x0030
#define RTL8366_SGCR_MAX_LENGTH_1522		0x0000
#define RTL8366_SGCR_MAX_LENGTH_1536		0x0010
#define RTL8366_SGCR_MAX_LENGTH_1552		0x0020
#define RTL8366_SGCR_MAX_LENGTH_9216		0x0030
#define RTL8366_SGCR_EN_VLAN			0x2000
#define RTL8366_SGCR_EN_VLAN_4KTB		0x4000
#define RTL8366_SGCR_EN_QOS			0x8000

/* Port Enable Control: DISABLE_PORT[5:0] */
#define RTL8366_PECR				0x0001

/* Switch Security Control 0: DIS_LEARN[5:0] */
#define RTL8366_SSCR0				0x0002

/* Switch Security Control 1: DIS_AGE[5:0] */
#define RTL8366_SSCR1				0x0003

/* Switch Security Control 2 */
#define RTL8366_SSCR2				0x0004
#define RTL8366_SSCR2_DROP_UNKNOWN_DA		0x0001

/* Port Link Status: two ports per register */
#define RTL8366_PLSR_BASE			(sc->chip_type == 0 ? 0x0014 : 0x0060)
#define RTL8366_PLSR_SPEED_MASK	0x03
#define RTL8366_PLSR_SPEED_10		0x00
#define RTL8366_PLSR_SPEED_100	0x01
#define RTL8366_PLSR_SPEED_1000	0x02
#define RTL8366_PLSR_FULLDUPLEX	0x04
#define RTL8366_PLSR_LINK		0x10
#define RTL8366_PLSR_TXPAUSE		0x20
#define RTL8366_PLSR_RXPAUSE		0x40
#define RTL8366_PLSR_NO_AUTO		0x80

/* VLAN Member Configuration, 3 or 2 registers per VLAN */
#define RTL8366_VMCR_BASE			(sc->chip_type == 0 ? 0x0020 : 0x0016)
#define RTL8366_VMCR_MULT		(sc->chip_type == 0 ? 3 : 2)
#define RTL8366_VMCR_DOT1Q_REG	0
#define RTL8366_VMCR_DOT1Q_VID_SHIFT	0
#define RTL8366_VMCR_DOT1Q_VID_MASK	0x0fff
#define RTL8366_VMCR_DOT1Q_PCP_SHIFT	12
#define RTL8366_VMCR_DOT1Q_PCP_MASK	0x7000
#define RTL8366_VMCR_MU_REG		1
#define RTL8366_VMCR_MU_MEMBER_SHIFT	0
#define RTL8366_VMCR_MU_MEMBER_MASK	(sc->chip_type == 0 ? 0x00ff : 0x003f)
#define RTL8366_VMCR_MU_UNTAG_SHIFT	(sc->chip_type == 0 ? 8 : 6)
#define RTL8366_VMCR_MU_UNTAG_MASK	(sc->chip_type == 0 ? 0xff00 : 0x0fc0)
#define RTL8366_VMCR_FID_REG		(sc->chip_type == 0 ? 2 : 1)
#define RTL8366_VMCR_FID_FID_SHIFT	(sc->chip_type == 0 ? 0 : 12)
#define RTL8366_VMCR_FID_FID_MASK	(sc->chip_type == 0 ? 0x0007 : 0x7000)
#define RTL8366_VMCR(_reg, _vlan) \
	(RTL8366_VMCR_BASE + _reg + _vlan * RTL8366_VMCR_MULT)
/* VLAN Identifier */
#define RTL8366_VMCR_VID(_r) \
	(_r[RTL8366_VMCR_DOT1Q_REG] & RTL8366_VMCR_DOT1Q_VID_MASK)
/* Priority Code Point */
#define RTL8366_VMCR_PCP(_r) \
	((_r[RTL8366_VMCR_DOT1Q_REG] & RTL8366_VMCR_DOT1Q_PCP_MASK) \
	>> RTL8366_VMCR_DOT1Q_PCP_SHIFT)
/* Member ports */
#define RTL8366_VMCR_MEMBER(_r) \
	(_r[RTL8366_VMCR_MU_REG] & RTL8366_VMCR_MU_MEMBER_MASK)
/* Untagged ports */
#define RTL8366_VMCR_UNTAG(_r) \
	((_r[RTL8366_VMCR_MU_REG] & RTL8366_VMCR_MU_UNTAG_MASK) \
	>> RTL8366_VMCR_MU_UNTAG_SHIFT)
/* Forwarding ID */
#define RTL8366_VMCR_FID(_r) \
	(sc->chip_type == 0 ? (_r[RTL8366_VMCR_FID_REG] & RTL8366_VMCR_FID_FID_MASK) : \
		((_r[RTL8366_VMCR_FID_REG] & RTL8366_VMCR_FID_FID_MASK) \
		>> RTL8366_VMCR_FID_FID_SHIFT))

/*
 * Port VLAN Control, 4 ports per register
 * Determines the VID for untagged ingress frames through
 * index into VMC.
 */
#define RTL8366_PVCR_BASE			(sc->chip_type == 0 ? 0x0063 : 0x0058)
#define RTL8366_PVCR_PORT_SHIFT	4
#define RTL8366_PVCR_PORT_PERREG	(16 / RTL8366_PVCR_PORT_SHIFT)
#define RTL8366_PVCR_PORT_MASK	0x000f
#define RTL8366_PVCR_REG(_port) \
	(RTL8366_PVCR_BASE + _port / (RTL8366_PVCR_PORT_PERREG))
#define RTL8366_PVCR_VAL(_port, _pvlan) \
	((_pvlan & RTL8366_PVCR_PORT_MASK) << \
	((_port % RTL8366_PVCR_PORT_PERREG) * RTL8366_PVCR_PORT_SHIFT))
#define RTL8366_PVCR_GET(_port, _val) \
	(((_val) >> ((_port % RTL8366_PVCR_PORT_PERREG) * RTL8366_PVCR_PORT_SHIFT)) & RTL8366_PVCR_PORT_MASK)

/* Reset Control */
#define RTL8366_RCR				0x0100
#define RTL8366_RCR_HARD_RESET	0x0001
#define RTL8366_RCR_SOFT_RESET	0x0002

/* Chip Version Control: CHIP_VER[3:0] */
#define RTL8366_CVCR				(sc->chip_type == 0 ? 0x050A : 0x0104)
/* Chip Identifier */
#define RTL8366RB_CIR				0x0509
#define RTL8366RB_CIR_ID8366RB		0x5937
#define RTL8366SR_CIR				0x0105
#define RTL8366SR_CIR_ID8366SR		0x8366

/* VLAN Ingress Control 2: [5:0] */
#define RTL8366_VIC2R				0x037f

/* MIB registers */
#define RTL8366_MCNT_BASE			0x1000
#define RTL8366_MCTLR				(sc->chip_type == 0 ? 0x13f0 : 0x11F0)
#define RTL8366_MCTLR_BUSY		0x0001
#define RTL8366_MCTLR_RESET		0x0002
#define RTL8366_MCTLR_RESET_PORT_MASK	0x00fc
#define RTL8366_MCTLR_RESET_ALL	0x0800

#define RTL8366_MCNT(_port, _r) \
	(RTL8366_MCNT_BASE + 0x50 * (_port) + (_r))
#define RTL8366_MCTLR_RESET_PORT(_p) \
	(1 << ((_p) + 2))

/* PHY Access Control */
#define RTL8366_PACR				(sc->chip_type == 0 ? 0x8000 : 0x8028)
#define RTL8366_PACR_WRITE		0x0000
#define RTL8366_PACR_READ			0x0001

/* PHY Access Data */
#define	RTL8366_PADR				(sc->chip_type == 0 ? 0x8002 : 0x8029)

#define RTL8366_PHYREG(phy, page, reg) \
	(0x8000 | (1 << (((phy) & 0x1f) + 9)) | (((page) & (sc->chip_type == 0 ? 0xf : 0x7)) << 5) | ((reg) & 0x1f))

/* general characteristics of the chip */
#define	RTL8366_NUM_PHYS			5
#define	RTL8366_NUM_VLANS			16
#define	RTL8366_NUM_PHY_REG			32

#endif
