/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __NLM_XAUI_H__
#define	__NLM_XAUI_H__

/**
* @file_name xaui.h
* @author Netlogic Microsystems
* @brief Basic definitions of XLP XAUI ports
*/
#define	XAUI_CONFIG0(block)			NAE_REG(block, 4, 0x00)
#define	XAUI_CONFIG1(block)			NAE_REG(block, 4, 0x01)
#define	XAUI_CONFIG2(block)			NAE_REG(block, 4, 0x02)
#define	XAUI_CONFIG3(block)			NAE_REG(block, 4, 0x03)
/*
#define	XAUI_MAC_ADDR0_LO(block)		NAE_REG(block, 4, 0x04)
#define	XAUI_MAC_ADDR0_HI(block)		NAE_REG(block, 4, 0x05)
*/
#define	XAUI_MAX_FRAME_LEN(block)		NAE_REG(block, 4, 0x08)
#define	XAUI_REVISION_LVL(block)		NAE_REG(block, 4, 0x0b)
#define	XAUI_MII_MGMT_CMD(block)		NAE_REG(block, 4, 0x10)
#define	XAUI_MII_MGMT_FIELD(block)		NAE_REG(block, 4, 0x11)
#define	XAUI_MII_MGMT_CFG(block)		NAE_REG(block, 4, 0x12)
#define	XAUI_MIIM_LINK_FALL_VEC(block)		NAE_REG(block, 4, 0x13)
#define	XAUI_MII_MGMT_IND(block)		NAE_REG(block, 4, 0x14)
#define	XAUI_STATS_MLR(block)			NAE_REG(block, 4, 0x1f)
#define	XAUI_STATS_TR64(block)			NAE_REG(block, 4, 0x20)
#define	XAUI_STATS_TR127(block)			NAE_REG(block, 4, 0x21)
#define	XAUI_STATS_TR255(block)			NAE_REG(block, 4, 0x22)
#define	XAUI_STATS_TR511(block)			NAE_REG(block, 4, 0x23)
#define	XAUI_STATS_TR1K(block)			NAE_REG(block, 4, 0x24)
#define	XAUI_STATS_TRMAX(block)			NAE_REG(block, 4, 0x25)
#define	XAUI_STATS_TRMGV(block)			NAE_REG(block, 4, 0x26)
#define	XAUI_STATS_RBYT(block)			NAE_REG(block, 4, 0x27)
#define	XAUI_STATS_RPKT(block)			NAE_REG(block, 4, 0x28)
#define	XAUI_STATS_RFCS(block)			NAE_REG(block, 4, 0x29)
#define	XAUI_STATS_RMCA(block)			NAE_REG(block, 4, 0x2a)
#define	XAUI_STATS_RBCA(block)			NAE_REG(block, 4, 0x2b)
#define	XAUI_STATS_RXCF(block)			NAE_REG(block, 4, 0x2c)
#define	XAUI_STATS_RXPF(block)			NAE_REG(block, 4, 0x2d)
#define	XAUI_STATS_RXUO(block)			NAE_REG(block, 4, 0x2e)
#define	XAUI_STATS_RALN(block)			NAE_REG(block, 4, 0x2f)
#define	XAUI_STATS_RFLR(block)			NAE_REG(block, 4, 0x30)
#define	XAUI_STATS_RCDE(block)			NAE_REG(block, 4, 0x31)
#define	XAUI_STATS_RCSE(block)			NAE_REG(block, 4, 0x32)
#define	XAUI_STATS_RUND(block)			NAE_REG(block, 4, 0x33)
#define	XAUI_STATS_ROVR(block)			NAE_REG(block, 4, 0x34)
#define	XAUI_STATS_RFRG(block)			NAE_REG(block, 4, 0x35)
#define	XAUI_STATS_RJBR(block)			NAE_REG(block, 4, 0x36)
#define	XAUI_STATS_TBYT(block)			NAE_REG(block, 4, 0x38)
#define	XAUI_STATS_TPKT(block)			NAE_REG(block, 4, 0x39)
#define	XAUI_STATS_TMCA(block)			NAE_REG(block, 4, 0x3a)
#define	XAUI_STATS_TBCA(block)			NAE_REG(block, 4, 0x3b)
#define	XAUI_STATS_TXPF(block)			NAE_REG(block, 4, 0x3c)
#define	XAUI_STATS_TDFR(block)			NAE_REG(block, 4, 0x3d)
#define	XAUI_STATS_TEDF(block)			NAE_REG(block, 4, 0x3e)
#define	XAUI_STATS_TSCL(block)			NAE_REG(block, 4, 0x3f)
#define	XAUI_STATS_TMCL(block)			NAE_REG(block, 4, 0x40)
#define	XAUI_STATS_TLCL(block)			NAE_REG(block, 4, 0x41)
#define	XAUI_STATS_TXCL(block)			NAE_REG(block, 4, 0x42)
#define	XAUI_STATS_TNCL(block)			NAE_REG(block, 4, 0x43)
#define	XAUI_STATS_TJBR(block)			NAE_REG(block, 4, 0x46)
#define	XAUI_STATS_TFCS(block)			NAE_REG(block, 4, 0x47)
#define	XAUI_STATS_TXCF(block)			NAE_REG(block, 4, 0x48)
#define	XAUI_STATS_TOVR(block)			NAE_REG(block, 4, 0x49)
#define	XAUI_STATS_TUND(block)			NAE_REG(block, 4, 0x4a)
#define	XAUI_STATS_TFRG(block)			NAE_REG(block, 4, 0x4b)
#define	XAUI_STATS_CAR1(block)			NAE_REG(block, 4, 0x4c)
#define	XAUI_STATS_CAR2(block)			NAE_REG(block, 4, 0x4d)
#define	XAUI_STATS_CAM1(block)			NAE_REG(block, 4, 0x4e)
#define	XAUI_STATS_CAM2(block)			NAE_REG(block, 4, 0x4f)
#define	XAUI_MAC_ADDR0_LO(block)		NAE_REG(block, 4, 0x50)
#define	XAUI_MAC_ADDR0_HI(block)		NAE_REG(block, 4, 0x51)
#define	XAUI_MAC_ADDR1_LO(block)		NAE_REG(block, 4, 0x52)
#define	XAUI_MAC_ADDR1_HI(block)		NAE_REG(block, 4, 0x53)
#define	XAUI_MAC_ADDR2_LO(block)		NAE_REG(block, 4, 0x54)
#define	XAUI_MAC_ADDR2_HI(block)		NAE_REG(block, 4, 0x55)
#define	XAUI_MAC_ADDR3_LO(block)		NAE_REG(block, 4, 0x56)
#define	XAUI_MAC_ADDR3_HI(block)		NAE_REG(block, 4, 0x57)
#define	XAUI_MAC_ADDR_MASK0_LO(block)		NAE_REG(block, 4, 0x58)
#define	XAUI_MAC_ADDR_MASK0_HI(block)		NAE_REG(block, 4, 0x59)
#define	XAUI_MAC_ADDR_MASK1_LO(block)		NAE_REG(block, 4, 0x5a)
#define	XAUI_MAC_ADDR_MASK1_HI(block)		NAE_REG(block, 4, 0x5b)
#define	XAUI_MAC_FILTER_CFG(block)		NAE_REG(block, 4, 0x5c)
#define	XAUI_HASHTBL_VEC_B31_0(block)		NAE_REG(block, 4, 0x60)
#define	XAUI_HASHTBL_VEC_B63_32(block)		NAE_REG(block, 4, 0x61)
#define	XAUI_HASHTBL_VEC_B95_64(block)		NAE_REG(block, 4, 0x62)
#define	XAUI_HASHTBL_VEC_B127_96(block)		NAE_REG(block, 4, 0x63)
#define	XAUI_HASHTBL_VEC_B159_128(block)	NAE_REG(block, 4, 0x64)
#define	XAUI_HASHTBL_VEC_B191_160(block)	NAE_REG(block, 4, 0x65)
#define	XAUI_HASHTBL_VEC_B223_192(block)	NAE_REG(block, 4, 0x66)
#define	XAUI_HASHTBL_VEC_B255_224(block)	NAE_REG(block, 4, 0x67)
#define	XAUI_HASHTBL_VEC_B287_256(block)	NAE_REG(block, 4, 0x68)
#define	XAUI_HASHTBL_VEC_B319_288(block)	NAE_REG(block, 4, 0x69)
#define	XAUI_HASHTBL_VEC_B351_320(block)	NAE_REG(block, 4, 0x6a)
#define	XAUI_HASHTBL_VEC_B383_352(block)	NAE_REG(block, 4, 0x6b)
#define	XAUI_HASHTBL_VEC_B415_384(block)	NAE_REG(block, 4, 0x6c)
#define	XAUI_HASHTBL_VEC_B447_416(block)	NAE_REG(block, 4, 0x6d)
#define	XAUI_HASHTBL_VEC_B479_448(block)	NAE_REG(block, 4, 0x6e)
#define	XAUI_HASHTBL_VEC_B511_480(block)	NAE_REG(block, 4, 0x6f)

#define	XAUI_NETIOR_XGMAC_MISC0(block)		NAE_REG(block, 4, 0x76)
#define	XAUI_NETIOR_RX_ABORT_DROP_COUNT(block)	NAE_REG(block, 4, 0x77)
#define	XAUI_NETIOR_MACCTRL_PAUSE_QUANTA(block)	NAE_REG(block, 4, 0x78)
#define	XAUI_NETIOR_MACCTRL_OPCODE(block)	NAE_REG(block, 4, 0x79)
#define	XAUI_NETIOR_MAC_DA_H(block)		NAE_REG(block, 4, 0x7a)
#define	XAUI_NETIOR_MAC_DA_L(block)		NAE_REG(block, 4, 0x7b)
#define	XAUI_NETIOR_XGMAC_STAT(block)		NAE_REG(block, 4, 0x7c)
#define	XAUI_NETIOR_XGMAC_CTRL3(block)		NAE_REG(block, 4, 0x7d)
#define	XAUI_NETIOR_XGMAC_CTRL2(block)		NAE_REG(block, 4, 0x7e)
#define	XAUI_NETIOR_XGMAC_CTRL1(block)		NAE_REG(block, 4, 0x7f)

#define	LANE_RX_CLK				(1 << 0)
#define	LANE_TX_CLK				(1 << 6)

#define	XAUI_LANE_FAULT				0x400
#define	XAUI_CONFIG_0				0

#define	XAUI_CONFIG_MACRST			0x80000000
#define	XAUI_CONFIG_RSTRCTL			0x00400000
#define	XAUI_CONFIG_RSTRFN			0x00200000
#define	XAUI_CONFIG_RSTTCTL			0x00040000
#define	XAUI_CONFIG_RSTTFN			0x00020000
#define	XAUI_CONFIG_RSTMIIM			0x00010000

#define	XAUI_CONFIG_1				1

#define	XAUI_CONFIG_TCTLEN			0x80000000
#define	XAUI_CONFIG_TFEN			0x40000000
#define	XAUI_CONFIG_RCTLEN			0x20000000
#define	XAUI_CONFIG_RFEN			0x10000000
#define	XAUI_CONFIG_DRPLT64			0x00000020
#define	XAUI_CONFIG_LENCHK			0x00000008
#define	XAUI_CONFIG_GENFCS			0x00000004
#define	XAUI_CONFIG_PAD_0			0x00000000
#define	XAUI_CONFIG_PAD_64			0x00000001
#define	XAUI_CONFIG_PAD_COND			0x00000002
#define	XAUI_CONFIG_PAD_68			0x00000003

#define	XAUI_PHY_CTRL_1				0x00

#define	NETIOR_XGMAC_CTRL1			0x7F
#define	NETIOR_XGMAC_CTRL3			0x7D

#define	NETIOR_XGMAC_VLAN_DC_POS		28
#define	NETIOR_XGMAC_PHYADDR_POS		23
#define	NETIOR_XGMAC_DEVID_POS			18
#define	NETIOR_XGMAC_STATS_EN_POS		17
#define	NETIOR_XGMAC_TX_PFC_EN_POS		14
#define	NETIOR_XGMAC_RX_PFC_EN_POS		13
#define	NETIOR_XGMAC_SOFT_RST_POS		11
#define	NETIOR_XGMAC_TX_PAUSE_POS		10

#define	NETIOR_XGMAC_STATS_CLR_POS		16

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

void nlm_xaui_pcs_init(uint64_t, int);
void nlm_nae_setup_rx_mode_xaui(uint64_t, int, int, int, int, int, int, int);
void nlm_nae_setup_mac_addr_xaui(uint64_t, int, int, int, unsigned char *);
void nlm_config_xaui_mtu(uint64_t, int, int, int);
void nlm_config_xaui(uint64_t, int, int, int, int);

#endif /* !(LOCORE) && !(__ASSEMBLY__) */

#endif
