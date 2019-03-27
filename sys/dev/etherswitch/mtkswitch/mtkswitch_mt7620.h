/*-
 * Copyright (c) 2016 Stanislav Galabov.
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

#ifndef	__MTKSWITCH_MT7620_H__
#define	__MTKSWITCH_MT7620_H__

#define	MTKSWITCH_ATC	0x0080
#define		ATC_BUSY		(1u<<15)
#define		ATC_AC_MAT_NON_STATIC_MACS	(4u<<8)
#define		ATC_AC_CMD_CLEAN	(2u<<0)

#define	MTKSWITCH_VTCR	0x0090
#define		VTCR_BUSY		(1u<<31)
#define		VTCR_FUNC_VID_READ	(0u<<12)
#define		VTCR_FUNC_VID_WRITE	(1u<<12)
#define		VTCR_FUNC_VID_INVALID	(2u<<12)
#define		VTCR_FUNC_VID_VALID	(3u<<12)
#define		VTCR_IDX_INVALID	(1u<<16)
#define		VTCR_VID_MASK		0xfff

#define	MTKSWITCH_VAWD1	0x0094
#define		VAWD1_IVL_MAC		(1u<<30)
#define		VAWD1_VTAG_EN		(1u<<28)
#define		VAWD1_PORT_MEMBER(p)	((1u<<16)<<(p))
#define		VAWD1_MEMBER_OFF	16
#define		VAWD1_MEMBER_MASK	0xff
#define		VAWD1_FID_OFFSET	1
#define		VAWD1_VALID		(1u<<0)

#define	MTKSWITCH_VAWD2	0x0098
#define		VAWD2_PORT_UNTAGGED(p)	(0u<<((p)*2))
#define		VAWD2_PORT_TAGGED(p)	(2u<<((p)*2))
#define		VAWD2_PORT_MASK(p)	(3u<<((p)*2))

#define	MTKSWITCH_VTIM(v)	((((v) >> 1) * 4) + 0x100)
#define		VTIM_OFF(v)	(((v) & 1) ? 12 : 0)
#define		VTIM_MASK	0xfff

#define	MTKSWITCH_PIAC	0x7004
#define		PIAC_PHY_ACS_ST		(1u<<31)
#define		PIAC_MDIO_REG_ADDR_OFF	25
#define		PIAC_MDIO_PHY_ADDR_OFF	20
#define		PIAC_MDIO_CMD_WRITE	(1u<<18)
#define		PIAC_MDIO_CMD_READ	(2u<<18)
#define		PIAC_MDIO_ST		(1u<<16)
#define		PIAC_MDIO_RW_DATA_MASK	0xffff

#define	MTKSWITCH_PORTREG(r, p)	((r) + ((p) * 0x100))

#define	MTKSWITCH_PCR(x)	MTKSWITCH_PORTREG(0x2004, (x))
#define		PCR_PORT_VLAN_SECURE	(3u<<0)

#define	MTKSWITCH_PVC(x)	MTKSWITCH_PORTREG(0x2010, (x))
#define		PVC_VLAN_ATTR_MASK	(3u<<6)

#define	MTKSWITCH_PPBV1(x)	MTKSWITCH_PORTREG(0x2014, (x))
#define	MTKSWITCH_PPBV2(x)	MTKSWITCH_PORTREG(0x2018, (x))
#define		PPBV_VID(v)		(((v)<<16) | (v))
#define		PPBV_VID_FROM_REG(x)	((x) & 0xfff)
#define		PPBV_VID_MASK		0xfff

#define	MTKSWITCH_PMCR(x)	MTKSWITCH_PORTREG(0x3000, (x))
#define		PMCR_FORCE_LINK		(1u<<0)
#define		PMCR_FORCE_DPX		(1u<<1)
#define		PMCR_FORCE_SPD_1000	(2u<<2)
#define		PMCR_FORCE_TX_FC	(1u<<4)
#define		PMCR_FORCE_RX_FC	(1u<<5)
#define		PMCR_BACKPR_EN		(1u<<8)
#define		PMCR_BKOFF_EN		(1u<<9)
#define		PMCR_MAC_RX_EN		(1u<<13)
#define		PMCR_MAC_TX_EN		(1u<<14)
#define		PMCR_FORCE_MODE		(1u<<15)
#define		PMCR_RES_1		(1u<<16)
#define		PMCR_IPG_CFG_RND	(1u<<18)
#define		PMCR_CFG_DEFAULT	(PMCR_BACKPR_EN | PMCR_BKOFF_EN | \
		    PMCR_MAC_RX_EN | PMCR_MAC_TX_EN | PMCR_IPG_CFG_RND |  \
		    PMCR_FORCE_RX_FC | PMCR_FORCE_TX_FC | PMCR_RES_1)

#define	MTKSWITCH_PMSR(x)	MTKSWITCH_PORTREG(0x3008, (x))
#define		PMSR_MAC_LINK_STS	(1u<<0)
#define		PMSR_MAC_DPX_STS	(1u<<1)
#define		PMSR_MAC_SPD_STS	(3u<<2)
#define		PMSR_MAC_SPD(x)		(((x)>>2) & 0x3)
#define		PMSR_MAC_SPD_10		0
#define		PMSR_MAC_SPD_100	1
#define		PMSR_MAC_SPD_1000	2
#define		PMSR_TX_FC_STS		(1u<<4)
#define		PMSR_RX_FC_STS		(1u<<5)

#define	MTKSWITCH_REG_ADDR(r)	(((r) >> 6) & 0x3ff)
#define	MTKSWITCH_REG_LO(r)	(((r) >> 2) & 0xf)
#define	MTKSWITCH_REG_HI(r)	(1 << 4)
#define MTKSWITCH_VAL_LO(v)	((v) & 0xffff)
#define MTKSWITCH_VAL_HI(v)	(((v) >> 16) & 0xffff)
#define MTKSWITCH_GLOBAL_PHY	31
#define	MTKSWITCH_GLOBAL_REG	31

#define	MTKSWITCH_LAN_VID	0x001
#define	MTKSWITCH_WAN_VID	0x002
#define	MTKSWITCH_INVALID_VID	0xfff

#define	MTKSWITCH_LAN_FID	1
#define	MTKSWITCH_WAN_FID	2

#endif	/* __MTKSWITCH_MT7620_H__ */
