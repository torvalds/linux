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

#ifndef __NLM_SGMII_H__
#define	__NLM_SGMII_H__

/**
* @file_name sgmii.h
* @author Netlogic Microsystems
* @brief Basic definitions of XLP SGMII ports
*/

#define	SGMII_MAC_CONF1(block, i)		NAE_REG(block, i, 0x00)
#define	SGMII_MAC_CONF2(block, i)		NAE_REG(block, i, 0x01)
#define	SGMII_IPG_IFG(block, i)			NAE_REG(block, i, 0x02)
#define	SGMII_HLF_DUP(block, i)			NAE_REG(block, i, 0x03)
#define	SGMII_MAX_FRAME(block, i)		NAE_REG(block, i, 0x04)
#define	SGMII_TEST(block, i)			NAE_REG(block, i, 0x07)
#define	SGMII_MIIM_CONF(block, i)		NAE_REG(block, i, 0x08)
#define	SGMII_MIIM_CMD(block, i)		NAE_REG(block, i, 0x09)
#define	SGMII_MIIM_ADDR(block, i)		NAE_REG(block, i, 0x0a)
#define	SGMII_MIIM_CTRL(block, i)		NAE_REG(block, i, 0x0b)
#define	SGMII_MIIM_STAT(block, i)		NAE_REG(block, i, 0x0c)
#define	SGMII_MIIM_IND(block, i)		NAE_REG(block, i, 0x0d)
#define	SGMII_IO_CTRL(block, i)			NAE_REG(block, i, 0x0e)
#define	SGMII_IO_STAT(block, i)			NAE_REG(block, i, 0x0f)
#define	SGMII_STATS_MLR(block, i)		NAE_REG(block, i, 0x1f)
#define	SGMII_STATS_TR64(block, i)		NAE_REG(block, i, 0x20)
#define	SGMII_STATS_TR127(block, i)		NAE_REG(block, i, 0x21)
#define	SGMII_STATS_TR255(block, i)		NAE_REG(block, i, 0x22)
#define	SGMII_STATS_TR511(block, i)		NAE_REG(block, i, 0x23)
#define	SGMII_STATS_TR1K(block, i)		NAE_REG(block, i, 0x24)
#define	SGMII_STATS_TRMAX(block, i)		NAE_REG(block, i, 0x25)
#define	SGMII_STATS_TRMGV(block, i)		NAE_REG(block, i, 0x26)
#define	SGMII_STATS_RBYT(block, i)		NAE_REG(block, i, 0x27)
#define	SGMII_STATS_RPKT(block, i)		NAE_REG(block, i, 0x28)
#define	SGMII_STATS_RFCS(block, i)		NAE_REG(block, i, 0x29)
#define	SGMII_STATS_RMCA(block, i)		NAE_REG(block, i, 0x2a)
#define	SGMII_STATS_RBCA(block, i)		NAE_REG(block, i, 0x2b)
#define	SGMII_STATS_RXCF(block, i)		NAE_REG(block, i, 0x2c)
#define	SGMII_STATS_RXPF(block, i)		NAE_REG(block, i, 0x2d)
#define	SGMII_STATS_RXUO(block, i)		NAE_REG(block, i, 0x2e)
#define	SGMII_STATS_RALN(block, i)		NAE_REG(block, i, 0x2f)
#define	SGMII_STATS_RFLR(block, i)		NAE_REG(block, i, 0x30)
#define	SGMII_STATS_RCDE(block, i)		NAE_REG(block, i, 0x31)
#define	SGMII_STATS_RCSE(block, i)		NAE_REG(block, i, 0x32)
#define	SGMII_STATS_RUND(block, i)		NAE_REG(block, i, 0x33)
#define	SGMII_STATS_ROVR(block, i)		NAE_REG(block, i, 0x34)
#define	SGMII_STATS_RFRG(block, i)		NAE_REG(block, i, 0x35)
#define	SGMII_STATS_RJBR(block, i)		NAE_REG(block, i, 0x36)
#define	SGMII_STATS_TBYT(block, i)		NAE_REG(block, i, 0x38)
#define	SGMII_STATS_TPKT(block, i)		NAE_REG(block, i, 0x39)
#define	SGMII_STATS_TMCA(block, i)		NAE_REG(block, i, 0x3a)
#define	SGMII_STATS_TBCA(block, i)		NAE_REG(block, i, 0x3b)
#define	SGMII_STATS_TXPF(block, i)		NAE_REG(block, i, 0x3c)
#define	SGMII_STATS_TDFR(block, i)		NAE_REG(block, i, 0x3d)
#define	SGMII_STATS_TEDF(block, i)		NAE_REG(block, i, 0x3e)
#define	SGMII_STATS_TSCL(block, i)		NAE_REG(block, i, 0x3f)
#define	SGMII_STATS_TMCL(block, i)		NAE_REG(block, i, 0x40)
#define	SGMII_STATS_TLCL(block, i)		NAE_REG(block, i, 0x41)
#define	SGMII_STATS_TXCL(block, i)		NAE_REG(block, i, 0x42)
#define	SGMII_STATS_TNCL(block, i)		NAE_REG(block, i, 0x43)
#define	SGMII_STATS_TJBR(block, i)		NAE_REG(block, i, 0x46)
#define	SGMII_STATS_TFCS(block, i)		NAE_REG(block, i, 0x47)
#define	SGMII_STATS_TXCF(block, i)		NAE_REG(block, i, 0x48)
#define	SGMII_STATS_TOVR(block, i)		NAE_REG(block, i, 0x49)
#define	SGMII_STATS_TUND(block, i)		NAE_REG(block, i, 0x4a)
#define	SGMII_STATS_TFRG(block, i)		NAE_REG(block, i, 0x4b)
#define	SGMII_STATS_CAR1(block, i)		NAE_REG(block, i, 0x4c)
#define	SGMII_STATS_CAR2(block, i)		NAE_REG(block, i, 0x4d)
#define	SGMII_STATS_CAM1(block, i)		NAE_REG(block, i, 0x4e)
#define	SGMII_STATS_CAM2(block, i)		NAE_REG(block, i, 0x4f)
#define	SGMII_MAC_ADDR0_LO(block, i)		NAE_REG(block, i, 0x50)
#define	SGMII_MAC_ADDR0_HI(block, i)		NAE_REG(block, i, 0x51)
#define	SGMII_MAC_ADDR1_LO(block, i)		NAE_REG(block, i, 0x52)
#define	SGMII_MAC_ADDR1_HI(block, i)		NAE_REG(block, i, 0x53)
#define	SGMII_MAC_ADDR2_LO(block, i)		NAE_REG(block, i, 0x54)
#define	SGMII_MAC_ADDR2_HI(block, i)		NAE_REG(block, i, 0x55)
#define	SGMII_MAC_ADDR3_LO(block, i)		NAE_REG(block, i, 0x56)
#define	SGMII_MAC_ADDR3_HI(block, i)		NAE_REG(block, i, 0x57)
#define	SGMII_MAC_ADDR_MASK0_LO(block, i)	NAE_REG(block, i, 0x58)
#define	SGMII_MAC_ADDR_MASK0_HI(block, i)	NAE_REG(block, i, 0x59)
#define	SGMII_MAC_ADDR_MASK1_LO(block, i)	NAE_REG(block, i, 0x5a)
#define	SGMII_MAC_ADDR_MASK1_HI(block, i)	NAE_REG(block, i, 0x5b)
#define	SGMII_MAC_FILTER_CONFIG(block, i)	NAE_REG(block, i, 0x5c)
#define	SGMII_HASHTBL_VEC_B31_0(block, i)	NAE_REG(block, i, 0x60)
#define	SGMII_HASHTBL_VEC_B63_32(block, i)	NAE_REG(block, i, 0x61)
#define	SGMII_HASHTBL_VEC_B95_64(block, i)	NAE_REG(block, i, 0x62)
#define	SGMII_HASHTBL_VEC_B127_96(block, i)	NAE_REG(block, i, 0x63)
#define	SGMII_HASHTBL_VEC_B159_128(block, i)	NAE_REG(block, i, 0x64)
#define	SGMII_HASHTBL_VEC_B191_160(block, i)	NAE_REG(block, i, 0x65)
#define	SGMII_HASHTBL_VEC_B223_192(block, i)	NAE_REG(block, i, 0x66)
#define	SGMII_HASHTBL_VEC_B255_224(block, i)	NAE_REG(block, i, 0x67)
#define	SGMII_HASHTBL_VEC_B287_256(block, i)	NAE_REG(block, i, 0x68)
#define	SGMII_HASHTBL_VEC_B319_288(block, i)	NAE_REG(block, i, 0x69)
#define	SGMII_HASHTBL_VEC_B351_320(block, i)	NAE_REG(block, i, 0x6a)
#define	SGMII_HASHTBL_VEC_B383_352(block, i)	NAE_REG(block, i, 0x6b)
#define	SGMII_HASHTBL_VEC_B415_384(block, i)	NAE_REG(block, i, 0x6c)
#define	SGMII_HASHTBL_VEC_B447_416(block, i)	NAE_REG(block, i, 0x6d)
#define	SGMII_HASHTBL_VEC_B479_448(block, i)	NAE_REG(block, i, 0x6e)
#define	SGMII_HASHTBL_VEC_B511_480(block, i)	NAE_REG(block, i, 0x6f)

#define	SGMII_NETIOR_VLANTYPE_FILTER(block, i)	NAE_REG(block, i, 0x76)
#define	SGMII_NETIOR_RXDROP_CNTR(block, i)	NAE_REG(block, i, 0x77)
#define	SGMII_NETIOR_PAUSE_QUANTAMULT(block, i)	NAE_REG(block, i, 0x78)
#define	SGMII_NETIOR_MAC_CTRL_OPCODE(block, i)	NAE_REG(block, i, 0x79)
#define	SGMII_NETIOR_MAC_DA_H(block, i)		NAE_REG(block, i, 0x7a)
#define	SGMII_NETIOR_MAC_DA_L(block, i)		NAE_REG(block, i, 0x7b)
#define	SGMII_NET_IFACE_CTRL3(block, i)		NAE_REG(block, i, 0x7c)
#define	SGMII_NETIOR_GMAC_STAT(block, i)	NAE_REG(block, i, 0x7d)
#define	SGMII_NET_IFACE_CTRL2(block, i)		NAE_REG(block, i, 0x7e)
#define	SGMII_NET_IFACE_CTRL(block, i)		NAE_REG(block, i, 0x7f)

#if !defined(LOCORE) && !defined(__ASSEMBLY__)
/* speed */
enum nlm_sgmii_speed {
	NLM_SGMII_SPEED_10,
	NLM_SGMII_SPEED_100,
	NLM_SGMII_SPEED_1000,
	NLM_SGMII_SPEED_RSVD
};

/* duplexity */
enum nlm_sgmii_duplex_mode {
	NLM_SGMII_DUPLEX_AUTO,
	NLM_SGMII_DUPLEX_HALF,
	NLM_SGMII_DUPLEX_FULL
};

/* stats */
enum {
	nlm_sgmii_stats_mlr,
	nlm_sgmii_stats_tr64,
	nlm_sgmii_stats_tr127,
	nlm_sgmii_stats_tr255,
	nlm_sgmii_stats_tr511,
	nlm_sgmii_stats_tr1k,
	nlm_sgmii_stats_trmax,
	nlm_sgmii_stats_trmgv,
	nlm_sgmii_stats_rbyt,
	nlm_sgmii_stats_rpkt,
	nlm_sgmii_stats_rfcs,
	nlm_sgmii_stats_rmca,
	nlm_sgmii_stats_rbca,
	nlm_sgmii_stats_rxcf,
	nlm_sgmii_stats_rxpf,
	nlm_sgmii_stats_rxuo,
	nlm_sgmii_stats_raln,
	nlm_sgmii_stats_rflr,
	nlm_sgmii_stats_rcde,
	nlm_sgmii_stats_rcse,
	nlm_sgmii_stats_rund,
	nlm_sgmii_stats_rovr,
	nlm_sgmii_stats_rfrg,
	nlm_sgmii_stats_rjbr,
	nlm_sgmii_stats_rdummy, /* not used */
	nlm_sgmii_stats_tbyt,
	nlm_sgmii_stats_tpkt,
	nlm_sgmii_stats_tmca,
	nlm_sgmii_stats_tbca,
	nlm_sgmii_stats_txpf,
	nlm_sgmii_stats_tdfr,
	nlm_sgmii_stats_tedf,
	nlm_sgmii_stats_tscl,
	nlm_sgmii_stats_tmcl,
	nlm_sgmii_stats_tlcl,
	nlm_sgmii_stats_txcl,
	nlm_sgmii_stats_tncl,
	nlm_sgmii_stats_tjbr,
	nlm_sgmii_stats_tfcs,
	nlm_sgmii_stats_txcf,
	nlm_sgmii_stats_tovr,
	nlm_sgmii_stats_tund,
	nlm_sgmii_stats_tfrg,
	nlm_sgmii_stats_car1,
	nlm_sgmii_stats_car2,
	nlm_sgmii_stats_cam1,
	nlm_sgmii_stats_cam2
};

void nlm_configure_sgmii_interface(uint64_t, int, int, int, int);
void nlm_sgmii_pcs_init(uint64_t, uint32_t);
void nlm_nae_setup_mac(uint64_t, int, int, int, int, int, int, int);
void nlm_nae_setup_rx_mode_sgmii(uint64_t, int, int, int, int, int,
    int, int);
void nlm_nae_setup_mac_addr_sgmii(uint64_t, int, int, int, uint8_t *);

#endif /* !(LOCORE) && !(__ASSEMBLY__) */

#endif
