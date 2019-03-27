/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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
 */

#ifndef IF_FFECREG_H
#define IF_FFECREG_H

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Hardware defines for Freescale Fast Ethernet Controller.
 */

/*
 * MAC registers.
 */
#define	FEC_IER_REG			0x0004
#define	FEC_IEM_REG			0x0008
#define	  FEC_IER_HBERR			  (1U << 31)
#define	  FEC_IER_BABR			  (1 << 30)
#define	  FEC_IER_BABT			  (1 << 29)
#define	  FEC_IER_GRA			  (1 << 28)
#define	  FEC_IER_TXF			  (1 << 27)
#define	  FEC_IER_TXB			  (1 << 26)
#define	  FEC_IER_RXF			  (1 << 25)
#define	  FEC_IER_RXB			  (1 << 24)
#define	  FEC_IER_MII			  (1 << 23)
#define	  FEC_IER_EBERR			  (1 << 22)
#define	  FEC_IER_LC			  (1 << 21)
#define	  FEC_IER_RL			  (1 << 20)
#define	  FEC_IER_UN			  (1 << 19)
#define	  FEC_IER_PLR			  (1 << 18)
#define	  FEC_IER_WAKEUP		  (1 << 17)
#define	  FEC_IER_AVAIL			  (1 << 16)
#define	  FEC_IER_TIMER			  (1 << 15)

#define	FEC_RDAR_REG			0x0010
#define	  FEC_RDAR_RDAR			  (1 << 24)

#define	FEC_TDAR_REG			0x0014
#define	  FEC_TDAR_TDAR			  (1 << 24)

#define	FEC_ECR_REG			0x0024
#define	  FEC_ECR_DBSWP			  (1 <<  8)
#define	  FEC_ECR_STOPEN		  (1 <<  7)
#define	  FEC_ECR_DBGEN			  (1 <<  6)
#define	  FEC_ECR_SPEED			  (1 <<  5)
#define	  FEC_ECR_EN1588		  (1 <<  4)
#define	  FEC_ECR_SLEEP			  (1 <<  3)
#define	  FEC_ECR_MAGICEN		  (1 <<  2)
#define	  FEC_ECR_ETHEREN		  (1 <<  1)
#define	  FEC_ECR_RESET			  (1 <<  0)

#define	FEC_MMFR_REG			0x0040
#define	  FEC_MMFR_ST_SHIFT		  30
#define	  FEC_MMFR_ST_VALUE		  (0x01 << FEC_MMFR_ST_SHIFT)
#define	  FEC_MMFR_OP_SHIFT		  28
#define	  FEC_MMFR_OP_WRITE		  (0x01 << FEC_MMFR_OP_SHIFT)
#define	  FEC_MMFR_OP_READ		  (0x02 << FEC_MMFR_OP_SHIFT)
#define	  FEC_MMFR_PA_SHIFT		  23
#define	  FEC_MMFR_PA_MASK		  (0x1f << FEC_MMFR_PA_SHIFT)
#define	  FEC_MMFR_RA_SHIFT		  18
#define	  FEC_MMFR_RA_MASK		  (0x1f << FEC_MMFR_RA_SHIFT)
#define	  FEC_MMFR_TA_SHIFT		  16
#define	  FEC_MMFR_TA_VALUE		  (0x02 << FEC_MMFR_TA_SHIFT)
#define	  FEC_MMFR_DATA_SHIFT		  0
#define	  FEC_MMFR_DATA_MASK		  (0xffff << FEC_MMFR_DATA_SHIFT)

#define	FEC_MSCR_REG			0x0044
#define	  FEC_MSCR_HOLDTIME_SHIFT	  8
#define	  FEC_MSCR_HOLDTIME_MASK	  (0x07 << FEC_MSCR_HOLDTIME_SHIFT)
#define	  FEC_MSCR_DIS_PRE      	  (1 <<  7)
#define	  FEC_MSCR_MII_SPEED_SHIFT	  1
#define	  FEC_MSCR_MII_SPEED_MASk	  (0x3f << FEC_MSCR_MII_SPEED_SHIFT)

#define	FEC_MIBC_REG			0x0064
#define	  FEC_MIBC_DIS			  (1U << 31)
#define	  FEC_MIBC_IDLE			  (1 << 30)
#define	  FEC_MIBC_CLEAR		  (1 << 29) /* imx6 only */

#define	FEC_RCR_REG			0x0084
#define	  FEC_RCR_GRS			  (1U << 31)
#define	  FEC_RCR_NLC			  (1 << 30)
#define	  FEC_RCR_MAX_FL_SHIFT		  16
#define	  FEC_RCR_MAX_FL_MASK		  (0x3fff << FEC_RCR_MAX_FL_SHIFT)
#define	  FEC_RCR_CFEN			  (1 << 15)
#define	  FEC_RCR_CRCFWD		  (1 << 14)
#define	  FEC_RCR_PAUFWD		  (1 << 13)
#define	  FEC_RCR_PADEN			  (1 << 12)
#define	  FEC_RCR_RMII_10T		  (1 <<  9)
#define	  FEC_RCR_RMII_MODE		  (1 <<  8)
#define	  FEC_RCR_RGMII_EN		  (1 <<  6)
#define	  FEC_RCR_FCE			  (1 <<  5)
#define	  FEC_RCR_BC_REJ		  (1 <<  4)
#define	  FEC_RCR_PROM			  (1 <<  3)
#define	  FEC_RCR_MII_MODE		  (1 <<  2)
#define	  FEC_RCR_DRT			  (1 <<  1)
#define	  FEC_RCR_LOOP			  (1 <<  0)

#define	FEC_TCR_REG			0x00c4
#define	  FEC_TCR_ADDINS		  (1 <<  9)
#define	  FEC_TCR_ADDSEL_SHIFT		  5
#define	  FEC_TCR_ADDSEL_MASK		  (0x07 << FEC_TCR_ADDSEL_SHIFT)
#define	  FEC_TCR_RFC_PAUSE		  (1 <<  4)
#define	  FEC_TCR_TFC_PAUSE		  (1 <<  3)
#define	  FEC_TCR_FDEN			  (1 <<  2)
#define	  FEC_TCR_GTS			  (1 <<  0)

#define	FEC_PALR_REG			0x00e4
#define	  FEC_PALR_PADDR1_SHIFT		  0
#define	  FEC_PALR_PADDR1_MASK		  (0xffffffff << FEC_PALR_PADDR1_SHIFT)

#define	FEC_PAUR_REG			0x00e8
#define	  FEC_PAUR_PADDR2_SHIFT		  16
#define	  FEC_PAUR_PADDR2_MASK		  (0xffff << FEC_PAUR_PADDR2_SHIFT)
#define	  FEC_PAUR_TYPE_VALUE		  (0x8808)

#define	FEC_OPD_REG			0x00ec
#define	  FEC_OPD_PAUSE_DUR_SHIFT	  0
#define	  FEC_OPD_PAUSE_DUR_MASK	  (0xffff << FEC_OPD_PAUSE_DUR_SHIFT)

#define	FEC_IAUR_REG			0x0118
#define	FEC_IALR_REG			0x011c

#define	FEC_GAUR_REG			0x0120
#define	FEC_GALR_REG			0x0124

#define	FEC_TFWR_REG			0x0144
#define	  FEC_TFWR_STRFWD		  (1 <<  8)
#define	  FEC_TFWR_TWFR_SHIFT		  0
#define	  FEC_TFWR_TWFR_MASK		  (0x3f << FEC_TFWR_TWFR_SHIFT)
#define	  FEC_TFWR_TWFR_128BYTE		  (0x02 << FEC_TFWR_TWFR_SHIFT)

#define	FEC_RDSR_REG			0x0180

#define	FEC_TDSR_REG			0x0184

#define	FEC_MRBR_REG			0x0188
#define	  FEC_MRBR_R_BUF_SIZE_SHIFT	  0
#define	  FEC_MRBR_R_BUF_SIZE_MASK	  (0x3fff << FEC_MRBR_R_BUF_SIZE_SHIFT)

#define	FEC_RSFL_REG			0x0190
#define	FEC_RSEM_REG			0x0194
#define	FEC_RAEM_REG			0x0198
#define	FEC_RAFL_REG			0x019c
#define	FEC_TSEM_REG			0x01a0
#define	FEC_TAEM_REG			0x01a4
#define	FEC_TAFL_REG			0x01a8
#define	FEC_TIPG_REG			0x01ac
#define	FEC_FTRL_REG			0x01b0

#define	FEC_TACC_REG			0x01c0
#define	  FEC_TACC_PROCHK		  (1 <<  4)
#define	  FEC_TACC_IPCHK		  (1 <<  3)
#define	  FEC_TACC_SHIFT16		  (1 <<  0)

#define	FEC_RACC_REG			0x01c4
#define	  FEC_RACC_SHIFT16		  (1 <<  7)
#define	  FEC_RACC_LINEDIS		  (1 <<  6)
#define	  FEC_RACC_PRODIS		  (1 <<  2)
#define	  FEC_RACC_IPDIS		  (1 <<  1)
#define	  FEC_RACC_PADREM		  (1 <<  0)

/*
 * IEEE-1588 timer registers
 */

#define	FEC_ATCR_REG			0x0400
#define	  FEC_ATCR_SLAVE		  (1u << 13)
#define	  FEC_ATCR_CAPTURE		  (1u << 11)
#define	  FEC_ATCR_RESTART		  (1u << 9)
#define	  FEC_ATCR_PINPER		  (1u << 7)
#define	  FEC_ATCR_PEREN		  (1u << 4)
#define	  FEC_ATCR_OFFRST		  (1u << 3)
#define	  FEC_ATCR_OFFEN		  (1u << 2)
#define	  FEC_ATCR_EN			  (1u << 0)

#define	FEC_ATVR_REG			0x0404
#define	FEC_ATOFF_REG			0x0408
#define	FEC_ATPER_REG			0x040c
#define	FEC_ATCOR_REG			0x0410
#define	FEC_ATINC_REG			0x0414
#define	FEC_ATSTMP_REG			0x0418

/*
 * Statistics registers
 */
#define	FEC_RMON_T_DROP			0x200
#define	FEC_RMON_T_PACKETS		0x204
#define	FEC_RMON_T_BC_PKT		0x208
#define	FEC_RMON_T_MC_PKT		0x20C
#define	FEC_RMON_T_CRC_ALIGN		0x210
#define	FEC_RMON_T_UNDERSIZE		0x214
#define	FEC_RMON_T_OVERSIZE		0x218
#define	FEC_RMON_T_FRAG			0x21C
#define	FEC_RMON_T_JAB			0x220
#define	FEC_RMON_T_COL			0x224
#define	FEC_RMON_T_P64			0x228
#define	FEC_RMON_T_P65TO127		0x22C
#define	FEC_RMON_T_P128TO255		0x230
#define	FEC_RMON_T_P256TO511		0x234
#define	FEC_RMON_T_P512TO1023		0x238
#define	FEC_RMON_T_P1024TO2047		0x23C
#define	FEC_RMON_T_P_GTE2048		0x240
#define	FEC_RMON_T_OCTECTS		0x240
#define	FEC_IEEE_T_DROP			0x248
#define	FEC_IEEE_T_FRAME_OK		0x24C
#define	FEC_IEEE_T_1COL			0x250
#define	FEC_IEEE_T_MCOL			0x254
#define	FEC_IEEE_T_DEF			0x258
#define	FEC_IEEE_T_LCOL			0x25C
#define	FEC_IEEE_T_EXCOL		0x260
#define	FEC_IEEE_T_MACERR		0x264
#define	FEC_IEEE_T_CSERR		0x268
#define	FEC_IEEE_T_SQE			0x26C
#define	FEC_IEEE_T_FDXFC		0x270
#define	FEC_IEEE_T_OCTETS_OK		0x274
#define	FEC_RMON_R_PACKETS		0x284
#define	FEC_RMON_R_BC_PKT		0x288
#define	FEC_RMON_R_MC_PKT		0x28C
#define	FEC_RMON_R_CRC_ALIGN		0x290
#define	FEC_RMON_R_UNDERSIZE		0x294
#define	FEC_RMON_R_OVERSIZE		0x298
#define	FEC_RMON_R_FRAG			0x29C
#define	FEC_RMON_R_JAB			0x2A0
#define	FEC_RMON_R_RESVD_0		0x2A4
#define	FEC_RMON_R_P64			0x2A8
#define	FEC_RMON_R_P65TO127		0x2AC
#define	FEC_RMON_R_P128TO255		0x2B0
#define	FEC_RMON_R_P256TO511		0x2B4
#define	FEC_RMON_R_P512TO1023		0x2B8
#define	FEC_RMON_R_P1024TO2047		0x2BC
#define	FEC_RMON_R_P_GTE2048		0x2C0
#define	FEC_RMON_R_OCTETS		0x2C4
#define	FEC_IEEE_R_DROP			0x2C8
#define	FEC_IEEE_R_FRAME_OK		0x2CC
#define	FEC_IEEE_R_CRC			0x2D0
#define	FEC_IEEE_R_ALIGN		0x2D4
#define	FEC_IEEE_R_MACERR		0x2D8
#define	FEC_IEEE_R_FDXFC		0x2DC
#define	FEC_IEEE_R_OCTETS_OK		0x2E0

#define	FEC_MIIGSK_CFGR			0x300
#define	FEC_MIIGSK_CFGR_FRCONT		(1 << 6)   /* Freq: 0=50MHz, 1=5MHz */
#define	FEC_MIIGSK_CFGR_LBMODE		(1 << 4)   /* loopback mode */
#define	FEC_MIIGSK_CFGR_EMODE		(1 << 3)   /* echo mode */
#define	FEC_MIIGSK_CFGR_IF_MODE_MASK	(0x3 << 0)
#define	FEC_MIIGSK_CFGR_IF_MODE_MII	  (0 << 0)
#define	FEC_MIIGSK_CFGR_IF_MODE_RMII	  (1 << 0)

#define	FEC_MIIGSK_ENR			0x308
#define	FEC_MIIGSK_ENR_READY		(1 << 2)
#define	FEC_MIIGSK_ENR_EN		(1 << 1)

/*
 * A hardware buffer descriptor.  Rx and Tx buffers have the same descriptor
 * layout, but the bits in the flags field have different meanings.
 */
struct ffec_hwdesc
{
	uint32_t	flags_len;
	uint32_t	buf_paddr;
};

#define	FEC_TXDESC_READY		(1U << 31)
#define	FEC_TXDESC_T01			(1 << 30)
#define	FEC_TXDESC_WRAP			(1 << 29)
#define	FEC_TXDESC_T02			(1 << 28)
#define	FEC_TXDESC_L			(1 << 27)
#define	FEC_TXDESC_TC			(1 << 26)
#define	FEC_TXDESC_ABC			(1 << 25)
#define	FEC_TXDESC_LEN_MASK		(0xffff)

#define	FEC_RXDESC_EMPTY		(1U << 31)
#define	FEC_RXDESC_R01			(1 << 30)
#define	FEC_RXDESC_WRAP			(1 << 29)
#define	FEC_RXDESC_R02			(1 << 28)
#define	FEC_RXDESC_L			(1 << 27)
#define	FEC_RXDESC_M			(1 << 24)
#define	FEC_RXDESC_BC			(1 << 23)
#define	FEC_RXDESC_MC			(1 << 22)
#define	FEC_RXDESC_LG			(1 << 21)
#define	FEC_RXDESC_NO			(1 << 20)
#define	FEC_RXDESC_CR			(1 << 18)
#define	FEC_RXDESC_OV			(1 << 17)
#define	FEC_RXDESC_TR			(1 << 16)
#define	FEC_RXDESC_LEN_MASK		(0xffff)

#define	FEC_RXDESC_ERROR_BITS	(FEC_RXDESC_LG | FEC_RXDESC_NO | \
    FEC_RXDESC_OV | FEC_RXDESC_TR)

/*
 * The hardware imposes alignment restrictions on various objects involved in
 * DMA transfers.  These values are expressed in bytes (not bits).
 */
#define	FEC_DESC_RING_ALIGN		64

#endif	/* IF_FFECREG_H */
