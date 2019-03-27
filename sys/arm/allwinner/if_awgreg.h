/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Allwinner Gigabit Ethernet
 */

#ifndef __IF_AWGREG_H__
#define __IF_AWGREG_H__

#define	EMAC_BASIC_CTL_0	0x00
#define	 BASIC_CTL_SPEED	(0x3 << 2)
#define	 BASIC_CTL_SPEED_SHIFT	2
#define	 BASIC_CTL_SPEED_1000	0
#define	 BASIC_CTL_SPEED_10	2
#define	 BASIC_CTL_SPEED_100	3
#define	 BASIC_CTL_LOOPBACK	(1 << 1)
#define	 BASIC_CTL_DUPLEX	(1 << 0)
#define	EMAC_BASIC_CTL_1	0x04
#define	 BASIC_CTL_BURST_LEN	(0x3f << 24)
#define	 BASIC_CTL_BURST_LEN_SHIFT 24
#define	 BASIC_CTL_RX_TX_PRI	(1 << 1)
#define	 BASIC_CTL_SOFT_RST	(1 << 0)
#define	EMAC_INT_STA		0x08
#define	 RX_BUF_UA_INT		(1 << 10)
#define	 RX_INT			(1 << 8)
#define	 TX_UNDERFLOW_INT	(1 << 4)
#define	 TX_BUF_UA_INT		(1 << 2)
#define	 TX_DMA_STOPPED_INT	(1 << 1)
#define	 TX_INT			(1 << 0)
#define	EMAC_INT_EN		0x0c
#define	 RX_BUF_UA_INT_EN	(1 << 10)
#define	 RX_INT_EN		(1 << 8)
#define	 TX_UNDERFLOW_INT_EN	(1 << 4)
#define	 TX_BUF_UA_INT_EN	(1 << 2)
#define	 TX_DMA_STOPPED_INT_EN	(1 << 1)
#define	 TX_INT_EN		(1 << 0)
#define	EMAC_TX_CTL_0		0x10
#define	 TX_EN			(1 << 31)
#define	EMAC_TX_CTL_1		0x14
#define	 TX_DMA_START		(1 << 31)
#define	 TX_DMA_EN		(1 << 30)
#define	 TX_NEXT_FRAME		(1 << 2)
#define	 TX_MD			(1 << 1)
#define	 FLUSH_TX_FIFO		(1 << 0)
#define	EMAC_TX_FLOW_CTL	0x1c
#define	 PAUSE_TIME		(0xffff << 4)
#define	 PAUSE_TIME_SHIFT	4
#define	 TX_FLOW_CTL_EN		(1 << 0)
#define	EMAC_TX_DMA_LIST	0x20
#define	EMAC_RX_CTL_0		0x24
#define	 RX_EN			(1 << 31)
#define	 JUMBO_FRM_EN		(1 << 29)
#define	 STRIP_FCS		(1 << 28)
#define	 CHECK_CRC		(1 << 27)
#define	 RX_FLOW_CTL_EN		(1 << 16)
#define	EMAC_RX_CTL_1		0x28
#define	 RX_DMA_START		(1 << 31)
#define	 RX_DMA_EN		(1 << 30)
#define	 RX_MD			(1 << 1)
#define	EMAC_RX_DMA_LIST	0x34
#define	EMAC_RX_FRM_FLT		0x38
#define	 DIS_ADDR_FILTER	(1 << 31)
#define	 DIS_BROADCAST		(1 << 17)
#define	 RX_ALL_MULTICAST	(1 << 16)
#define	 CTL_FRM_FILTER		(0x3 << 12)
#define	 CTL_FRM_FILTER_SHIFT	12
#define	 HASH_MULTICAST		(1 << 9)
#define	 HASH_UNICAST		(1 << 8)
#define	 SA_FILTER_EN		(1 << 6)
#define	 SA_INV_FILTER		(1 << 5)
#define	 DA_INV_FILTER		(1 << 4)
#define	 FLT_MD			(1 << 1)
#define	 RX_ALL			(1 << 0)
#define	EMAC_RX_HASH_0		0x40
#define	EMAC_RX_HASH_1		0x44
#define	EMAC_MII_CMD		0x48
#define	 MDC_DIV_RATIO_M	(0x7 << 20)
#define	 MDC_DIV_RATIO_M_16	0
#define	 MDC_DIV_RATIO_M_32	1
#define	 MDC_DIV_RATIO_M_64	2
#define	 MDC_DIV_RATIO_M_128	3
#define	 MDC_DIV_RATIO_M_SHIFT	20
#define	 PHY_ADDR		(0x1f << 12)
#define	 PHY_ADDR_SHIFT		12
#define	 PHY_REG_ADDR		(0x1f << 4)
#define	 PHY_REG_ADDR_SHIFT	4
#define	 MII_WR			(1 << 1)
#define	 MII_BUSY		(1 << 0)
#define	EMAC_MII_DATA		0x4c
#define	EMAC_ADDR_HIGH(n)	(0x50 + (n) * 8)
#define	EMAC_ADDR_LOW(n)	(0x54 + (n) * 8)
#define	EMAC_TX_DMA_STA		0xb0
#define	EMAC_TX_DMA_CUR_DESC	0xb4
#define	EMAC_TX_DMA_CUR_BUF	0xb8
#define	EMAC_RX_DMA_STA		0xc0
#define	EMAC_RX_DMA_CUR_DESC	0xc4
#define	EMAC_RX_DMA_CUR_BUF	0xc8
#define	EMAC_RGMII_STA		0xd0

struct emac_desc {
	uint32_t	status;
/* Transmit */
#define	TX_DESC_CTL		(1 << 31)
#define	TX_HEADER_ERR		(1 << 16)
#define	TX_LENGTH_ERR		(1 << 14)
#define	TX_PAYLOAD_ERR		(1 << 12)
#define	TX_CRS_ERR		(1 << 10)
#define	TX_COL_ERR_0		(1 << 9)
#define	TX_COL_ERR_1		(1 << 8)
#define	TX_COL_CNT		(0xf << 3)
#define	TX_COL_CNT_SHIFT	3
#define	TX_DEFER_ERR		(1 << 2)
#define	TX_UNDERFLOW_ERR	(1 << 1)
#define	TX_DEFER		(1 << 0)
/* Receive */
#define	RX_DESC_CTL		(1 << 31)
#define	RX_DAF_FAIL		(1 << 30)
#define	RX_FRM_LEN		(0x3fff << 16)
#define	RX_FRM_LEN_SHIFT	16
#define	RX_NO_ENOUGH_BUF_ERR	(1 << 14)
#define	RX_SAF_FAIL		(1 << 13)
#define	RX_OVERFLOW_ERR		(1 << 11)
#define	RX_FIR_DESC		(1 << 9)
#define	RX_LAST_DESC		(1 << 8)
#define	RX_HEADER_ERR		(1 << 7)
#define	RX_COL_ERR		(1 << 6)
#define	RX_FRM_TYPE		(1 << 5)
#define	RX_LENGTH_ERR		(1 << 4)
#define	RX_PHY_ERR		(1 << 3)
#define	RX_CRC_ERR		(1 << 1)
#define	RX_PAYLOAD_ERR		(1 << 0)

	uint32_t	size;
/* Transmit */
#define	TX_INT_CTL		(1 << 31)
#define	TX_LAST_DESC		(1 << 30)
#define	TX_FIR_DESC		(1 << 29)
#define	TX_CHECKSUM_CTL		(0x3 << 27)
#define	TX_CHECKSUM_CTL_IP	1
#define	TX_CHECKSUM_CTL_NO_PSE	2
#define	TX_CHECKSUM_CTL_FULL	3
#define	TX_CHECKSUM_CTL_SHIFT	27
#define	TX_CRC_CTL		(1 << 26)
#define	TX_BUF_SIZE		(0xfff << 0)
#define	TX_BUF_SIZE_SHIFT	0
/* Receive */
#define	RX_INT_CTL		(1 << 31)
#define	RX_BUF_SIZE		(0xfff << 0)
#define	RX_BUF_SIZE_SHIFT	0

	uint32_t	addr;

	uint32_t	next;
} __packed;

#endif /* !__IF_AWGREG_H__ */
