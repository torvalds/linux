/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __IF_MGE_H__
#define __IF_MGE_H__

#include <arm/mv/mvvar.h>

#define MGE_INTR_COUNT		5	/* ETH controller occupies 5 IRQ lines */
#define MGE_TX_DESC_NUM		256
#define MGE_RX_DESC_NUM		256
#define MGE_RX_QUEUE_NUM	8
#define MGE_RX_DEFAULT_QUEUE	0

#define MGE_CHECKSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

/* Interrupt Coalescing types */
#define MGE_IC_RX		0
#define MGE_IC_TX		1

struct mge_desc {
	uint32_t	cmd_status;
	uint16_t	buff_size;
	uint16_t	byte_count;
	bus_addr_t	buffer;
	bus_addr_t	next_desc;
};

struct mge_desc_wrapper {
	bus_dmamap_t		desc_dmap;
	struct mge_desc*	mge_desc;
	bus_addr_t		mge_desc_paddr;
	bus_dmamap_t		buffer_dmap;
	struct mbuf*		buffer;
};

struct mge_softc {
	struct ifnet	*ifp;		/* per-interface network data */

	phandle_t	node;

	device_t	dev;
	device_t	miibus;

	struct mii_data	*mii;
	struct ifmedia	mge_ifmedia;
	struct resource	*res[1 + MGE_INTR_COUNT];	/* resources */
	void		*ih_cookie[MGE_INTR_COUNT];	/* interrupt handlers cookies */
	struct mtx	transmit_lock;			/* transmitter lock */
	struct mtx	receive_lock;			/* receiver lock */

	uint32_t	mge_if_flags;
	uint32_t	mge_media_status;

	struct callout	wd_callout;
	int		wd_timer;

	bus_dma_tag_t	mge_desc_dtag;
	bus_dma_tag_t	mge_tx_dtag;
	bus_dma_tag_t	mge_rx_dtag;
	bus_addr_t	tx_desc_start;
	bus_addr_t	rx_desc_start;
	uint32_t	tx_desc_curr;
	uint32_t	rx_desc_curr;
	uint32_t	tx_desc_used_idx;
	uint32_t	tx_desc_used_count;
	uint32_t	rx_ic_time;
	uint32_t	tx_ic_time;
	struct mge_desc_wrapper mge_tx_desc[MGE_TX_DESC_NUM];
	struct mge_desc_wrapper mge_rx_desc[MGE_RX_DESC_NUM];

	uint32_t	mge_tfut_ipg_max;		/* TX FIFO Urgent Threshold */
	uint32_t	mge_rx_ipg_max;
	uint32_t	mge_tx_arb_cfg;
	uint32_t	mge_tx_tok_cfg;
	uint32_t	mge_tx_tok_cnt;
	uint16_t	mge_mtu;
	int		mge_ver;
	int		mge_intr_cnt;
	uint8_t		mge_hw_csum;

	int		phy_attached;
	int		switch_attached;
	struct mge_softc *phy_sc;
};


/* bus access macros */
#define MGE_READ(sc,reg)	bus_read_4((sc)->res[0], (reg))
#define MGE_WRITE(sc,reg,val)	bus_write_4((sc)->res[0], (reg), (val))

/* Locking macros */
#define MGE_TRANSMIT_LOCK(sc) do {						\
			mtx_assert(&(sc)->receive_lock, MA_NOTOWNED);		\
			mtx_lock(&(sc)->transmit_lock);				\
} while (0)

#define MGE_TRANSMIT_UNLOCK(sc)		mtx_unlock(&(sc)->transmit_lock)
#define MGE_TRANSMIT_LOCK_ASSERT(sc)	mtx_assert(&(sc)->transmit_lock, MA_OWNED)

#define MGE_RECEIVE_LOCK(sc) do {						\
			mtx_assert(&(sc)->transmit_lock, MA_NOTOWNED);		\
			mtx_lock(&(sc)->receive_lock);				\
} while (0)

#define MGE_RECEIVE_UNLOCK(sc)		mtx_unlock(&(sc)->receive_lock)
#define MGE_RECEIVE_LOCK_ASSERT(sc)	mtx_assert(&(sc)->receive_lock, MA_OWNED)

#define MGE_GLOBAL_LOCK(sc) do {						\
			mtx_assert(&(sc)->transmit_lock, MA_NOTOWNED);		\
			mtx_assert(&(sc)->receive_lock, MA_NOTOWNED);		\
			mtx_lock(&(sc)->transmit_lock);				\
			mtx_lock(&(sc)->receive_lock);				\
} while (0)

#define MGE_GLOBAL_UNLOCK(sc) do {						\
			MGE_RECEIVE_UNLOCK(sc);					\
			MGE_TRANSMIT_UNLOCK(sc);				\
} while (0)

#define MGE_GLOBAL_LOCK_ASSERT(sc) do {						\
			MGE_TRANSMIT_LOCK_ASSERT(sc);				\
			MGE_RECEIVE_LOCK_ASSERT(sc); 				\
} while (0)

#define MGE_SMI_LOCK() do {				\
    sx_assert(&sx_smi, SA_UNLOCKED);			\
    sx_xlock(&sx_smi);					\
} while (0)

#define MGE_SMI_UNLOCK()		sx_unlock(&sx_smi)
#define MGE_SMI_LOCK_ASSERT()		sx_assert(&sx_smi, SA_XLOCKED)

/* SMI-related macros */
#define MGE_REG_PHYDEV		0x000
#define MGE_REG_SMI		0x004
#define MGE_SMI_READ		(1 << 26)
#define MGE_SMI_WRITE		(0 << 26)
#define MGE_SMI_READVALID	(1 << 27)
#define MGE_SMI_BUSY		(1 << 28)

#define	MGE_SMI_MASK		0x1fffffff
#define	MGE_SMI_DATA_MASK	0xffff
#define	MGE_SMI_DELAY		1000

#define	MGE_SWITCH_PHYDEV	6

/* Internal Switch SMI Command */

#define SW_SMI_READ_CMD(phy, reg)		((1 << 15) | (1 << 12) | (1 << 11) | (phy << 5) | reg)
#define SW_SMI_WRITE_CMD(phy, reg)		((1 << 15) | (1 << 12) | (1 << 10) | (phy << 5) | reg)

/* TODO verify the timings and retries count w/specs */
#define MGE_SMI_READ_RETRIES		1000
#define MGE_SMI_READ_DELAY		100
#define MGE_SMI_WRITE_RETRIES		1000
#define MGE_SMI_WRITE_DELAY		100

/* MGE registers */
#define MGE_INT_CAUSE		0x080
#define MGE_INT_MASK		0x084

#define MGE_PORT_CONFIG			0x400
#define PORT_CONFIG_UPM			(1 << 0)		/* promiscuous */
#define PORT_CONFIG_DFLT_RXQ(val)	(((val) & 7) << 1)	/* default RX queue */
#define PORT_CONFIG_ARO_RXQ(val)	(((val) & 7) << 4)	/* ARP RX queue */
#define PORT_CONFIG_REJECT_BCAST	(1 << 7) /* reject non-ip and non-arp bcast */
#define PORT_CONFIG_REJECT_IP_BCAST	(1 << 8) /* reject ip bcast */
#define PORT_CONFIG_REJECT_ARP__BCAST	(1 << 9) /* reject arp bcast */
#define PORT_CONFIG_AMNoTxES		(1 << 12) /* Automatic mode not updating Error Summary in Tx descriptor */
#define PORT_CONFIG_TCP_CAP		(1 << 14) /* capture tcp to a different queue */
#define PORT_CONFIG_UDP_CAP		(1 << 15) /* capture udp to a different queue */
#define PORT_CONFIG_TCPQ		(7 << 16) /* queue to capture tcp */
#define PORT_CONFIG_UDPQ		(7 << 19) /* queue to capture udp */
#define PORT_CONFIG_BPDUQ		(7 << 22) /* queue to capture bpdu */
#define PORT_CONFIG_RXCS		(1 << 25) /* calculation Rx TCP checksum include pseudo header */

#define MGE_PORT_EXT_CONFIG	0x404
#define MGE_MAC_ADDR_L		0x414
#define MGE_MAC_ADDR_H		0x418

#define MGE_SDMA_CONFIG			0x41c
#define MGE_SDMA_INT_ON_FRAME_BOUND	(1 << 0)
#define MGE_SDMA_RX_BURST_SIZE(val)	(((val) & 7) << 1)
#define MGE_SDMA_TX_BURST_SIZE(val)	(((val) & 7) << 22)
#define MGE_SDMA_BURST_1_WORD		0x0
#define MGE_SDMA_BURST_2_WORD		0x1
#define MGE_SDMA_BURST_4_WORD		0x2
#define MGE_SDMA_BURST_8_WORD		0x3
#define MGE_SDMA_BURST_16_WORD		0x4
#define MGE_SDMA_RX_BYTE_SWAP		(1 << 4)
#define MGE_SDMA_TX_BYTE_SWAP		(1 << 5)
#define MGE_SDMA_DESC_SWAP_MODE		(1 << 6)

#define MGE_PORT_SERIAL_CTRL		0x43c
#define PORT_SERIAL_ENABLE		(1 << 0) /* serial port enable */
#define PORT_SERIAL_FORCE_LINKUP	(1 << 1) /* force link status to up */
#define PORT_SERIAL_AUTONEG		(1 << 2) /* enable autoneg for duplex mode */
#define PORT_SERIAL_AUTONEG_FC		(1 << 3) /* enable autoneg for FC */
#define PORT_SERIAL_PAUSE_ADV		(1 << 4) /* advertise symmetric FC in autoneg */
#define PORT_SERIAL_FORCE_FC(val)	(((val) & 3) << 5) /* pause enable & disable frames conf */
#define PORT_SERIAL_NO_PAUSE_DIS	0x00
#define PORT_SERIAL_PAUSE_DIS		0x01
#define PORT_SERIAL_FORCE_BP(val)	(((val) & 3) << 7) /* transmitting JAM configuration */
#define PORT_SERIAL_NO_JAM		0x00
#define PORT_SERIAL_JAM			0x01
#define PORT_SERIAL_RES_BIT9		(1 << 9)
#define PORT_SERIAL_FORCE_LINK_FAIL	(1 << 10)
#define PORT_SERIAL_SPEED_AUTONEG	(1 << 13)
#define PORT_SERIAL_FORCE_DTE_ADV	(1 << 14)
#define PORT_SERIAL_MRU(val)		(((val) & 7) << 17)
#define PORT_SERIAL_MRU_1518		0x0
#define PORT_SERIAL_MRU_1522		0x1
#define PORT_SERIAL_MRU_1552		0x2
#define PORT_SERIAL_MRU_9022		0x3
#define PORT_SERIAL_MRU_9192		0x4
#define PORT_SERIAL_MRU_9700		0x5
#define PORT_SERIAL_FULL_DUPLEX		(1 << 21)
#define PORT_SERIAL_FULL_DUPLEX_FC	(1 << 22)
#define PORT_SERIAL_GMII_SPEED_1000	(1 << 23)
#define PORT_SERIAL_MII_SPEED_100	(1 << 24)

#define MGE_PORT_STATUS			0x444
#define MGE_STATUS_LINKUP		(1 << 1)
#define MGE_STATUS_FULL_DUPLEX		(1 << 2)
#define MGE_STATUS_FLOW_CONTROL		(1 << 3)
#define MGE_STATUS_1000MB		(1 << 4)
#define MGE_STATUS_100MB		(1 << 5)
#define MGE_STATUS_TX_IN_PROG		(1 << 7)
#define MGE_STATUS_TX_FIFO_EMPTY	(1 << 10)

#define MGE_TX_QUEUE_CMD	0x448
#define MGE_ENABLE_TXQ		(1 << 0)
#define MGE_DISABLE_TXQ		(1 << 8)

/* 88F6281 only */
#define MGE_PORT_SERIAL_CTRL1		0x44c
#define MGE_PCS_LOOPBACK		(1 << 1)
#define MGE_RGMII_EN			(1 << 3)
#define MGE_PORT_RESET			(1 << 4)
#define MGE_CLK125_BYPASS		(1 << 5)
#define MGE_INBAND_AUTONEG		(1 << 6)
#define MGE_INBAND_AUTONEG_BYPASS	(1 << 6)
#define MGE_INBAND_AUTONEG_RESTART	(1 << 7)
#define MGE_1000BASEX			(1 << 11)
#define MGE_BP_COLLISION_COUNT		(1 << 15)
#define MGE_COLLISION_LIMIT(val)	(((val) & 0x3f) << 16)
#define MGE_DROP_ODD_PREAMBLE		(1 << 22)

#define MGE_PORT_INT_CAUSE	0x460
#define MGE_PORT_INT_MASK	0x468
#define MGE_PORT_INT_RX		(1 << 0)
#define MGE_PORT_INT_EXTEND	(1 << 1)
#define MGE_PORT_INT_RXQ0	(1 << 2)
#define MGE_PORT_INT_RXERR	(1 << 10)
#define MGE_PORT_INT_RXERRQ0	(1 << 11)
#define MGE_PORT_INT_SUM	(1U << 31)

#define MGE_PORT_INT_CAUSE_EXT	0x464
#define MGE_PORT_INT_MASK_EXT	0x46C
#define MGE_PORT_INT_EXT_TXBUF0	(1 << 0)
#define MGE_PORT_INT_EXT_TXERR0	(1 << 8)
#define MGE_PORT_INT_EXT_PHYSC	(1 << 16)
#define MGE_PORT_INT_EXT_RXOR	(1 << 18)
#define MGE_PORT_INT_EXT_TXUR	(1 << 19)
#define MGE_PORT_INT_EXT_LC	(1 << 20)
#define MGE_PORT_INT_EXT_IAR	(1 << 23)
#define MGE_PORT_INT_EXT_SUM	(1U << 31)

#define MGE_RX_FIFO_URGENT_TRSH		0x470
#define MGE_TX_FIFO_URGENT_TRSH		0x474

#define MGE_FIXED_PRIO_CONF		0x4dc
#define MGE_FIXED_PRIO_EN(q)		(1 << (q))

#define MGE_RX_CUR_DESC_PTR(q)		(0x60c + ((q)<<4))

#define MGE_RX_QUEUE_CMD		0x680
#define MGE_ENABLE_RXQ(q)		(1 << ((q) & 0x7))
#define MGE_ENABLE_RXQ_ALL		(0xff)
#define MGE_DISABLE_RXQ(q)		(1 << (((q) & 0x7) + 8))
#define MGE_DISABLE_RXQ_ALL		(0xff00)

#define MGE_TX_CUR_DESC_PTR		0x6c0

#define MGE_TX_TOKEN_COUNT(q)		(0x700 + ((q)<<4))
#define MGE_TX_TOKEN_CONF(q)		(0x704 + ((q)<<4))
#define MGE_TX_ARBITER_CONF(q)		(0x704 + ((q)<<4))

#define MGE_MCAST_REG_NUMBER		64
#define MGE_DA_FILTER_SPEC_MCAST(i)	(0x1400 + ((i) << 2))
#define MGE_DA_FILTER_OTH_MCAST(i)	(0x1500 + ((i) << 2))

#define MGE_UCAST_REG_NUMBER		4
#define MGE_DA_FILTER_UCAST(i)		(0x1600 + ((i) << 2))
	

/* TX descriptor bits */
#define MGE_TX_LLC_SNAP		(1 << 9)
#define MGE_TX_NOT_FRAGMENT	(1 << 10)
#define MGE_TX_VLAN_TAGGED	(1 << 15)
#define MGE_TX_UDP		(1 << 16)
#define MGE_TX_GEN_L4_CSUM	(1 << 17)
#define MGE_TX_GEN_IP_CSUM	(1 << 18)
#define MGE_TX_PADDING		(1 << 19)
#define MGE_TX_LAST		(1 << 20)
#define MGE_TX_FIRST		(1 << 21)
#define MGE_TX_ETH_CRC		(1 << 22)
#define MGE_TX_EN_INT		(1 << 23)

#define MGE_TX_IP_HDR_SIZE(size)	((size << 11) & 0xFFFF)

/* RX descriptor bits */
#define MGE_ERR_SUMMARY		(1 << 0)
#define MGE_ERR_MASK		(3 << 1)
#define MGE_RX_L4_PROTO_MASK	(3 << 21)
#define MGE_RX_L4_PROTO_TCP	(0 << 21)
#define MGE_RX_L4_PROTO_UDP	(1 << 21)
#define MGE_RX_L3_IS_IP		(1 << 24)
#define MGE_RX_IP_OK		(1 << 25)
#define MGE_RX_DESC_LAST	(1 << 26)
#define MGE_RX_DESC_FIRST	(1 << 27)
#define MGE_RX_ENABLE_INT	(1 << 29)
#define MGE_RX_L4_CSUM_OK	(1 << 30)
#define MGE_DMA_OWNED		(1U << 31)

#define MGE_RX_IP_FRAGMENT	(1 << 2)

#define MGE_RX_L4_IS_TCP(status)	((status & MGE_RX_L4_PROTO_MASK) \
					    == MGE_RX_L4_PROTO_TCP)

#define MGE_RX_L4_IS_UDP(status)	((status & MGE_RX_L4_PROTO_MASK) \
					    == MGE_RX_L4_PROTO_UDP)

/* TX error codes */
#define MGE_TX_ERROR_LC		(0 << 1)	/* Late collision */
#define MGE_TX_ERROR_UR		(1 << 1)	/* Underrun error */
#define MGE_TX_ERROR_RL		(2 << 1)	/* Excessive collision */

/* RX error codes */
#define MGE_RX_ERROR_CE		(0 << 1)	/* CRC error */
#define MGE_RX_ERROR_OR		(1 << 1)	/* Overrun error */
#define	MGE_RX_ERROR_MF		(2 << 1)	/* Max frame length error */
#define MGE_RX_ERROR_RE		(3 << 1)	/* Resource error */

#endif /* __IF_MGE_H__ */
