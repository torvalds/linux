/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
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

#ifndef	_IF_JMEVAR_H
#define	_IF_JMEVAR_H

#include <sys/queue.h>
#include <sys/callout.h>
#include <sys/taskqueue.h>

/*
 * JMC250 supports up to 1024 descriptors and the number of
 * descriptors should be multiple of 16.
 */
#define	JME_TX_RING_CNT		384
#define	JME_RX_RING_CNT		256
/*
 * Tx/Rx descriptor queue base should be 16bytes aligned and
 * should not cross 4G bytes boundary on the 64bits address
 * mode.
 */
#define	JME_TX_RING_ALIGN	16
#define	JME_RX_RING_ALIGN	16
#define	JME_TSO_MAXSEGSIZE	4096
#define	JME_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))
#define	JME_MAXTXSEGS		35
#define	JME_RX_BUF_ALIGN	sizeof(uint64_t)
#define	JME_SSB_ALIGN		16

#define	JME_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	JME_ADDR_HI(x)		((uint64_t) (x) >> 32)

#define	JME_MSI_MESSAGES	8
#define	JME_MSIX_MESSAGES	8

/* Water mark to kick reclaiming Tx buffers. */
#define	JME_TX_DESC_HIWAT	(JME_TX_RING_CNT - (((JME_TX_RING_CNT) * 3) / 10))

/*
 * JMC250 can send 9K jumbo frame on Tx path and can receive
 * 65535 bytes.
 */
#define	JME_JUMBO_FRAMELEN	9216
#define	JME_JUMBO_MTU							\
	(JME_JUMBO_FRAMELEN - sizeof(struct ether_vlan_header) -	\
	 ETHER_HDR_LEN - ETHER_CRC_LEN)
#define	JME_MAX_MTU							\
	(ETHER_MAX_LEN + sizeof(struct ether_vlan_header) -		\
	 ETHER_HDR_LEN - ETHER_CRC_LEN)
/*
 * JMC250 can't handle Tx checksum offload/TSO if frame length
 * is larger than its FIFO size(2K). It's also good idea to not
 * use jumbo frame if hardware is running at half-duplex media.
 * Because the jumbo frame may not fit into the Tx FIFO,
 * collisions make hardware fetch frame from host memory with
 * DMA again which in turn slows down Tx performance
 * significantly.
 */
#define	JME_TX_FIFO_SIZE	2000
/*
 * JMC250 has just 4K Rx FIFO. To support jumbo frame that is
 * larger than 4K bytes in length, Rx FIFO threshold should be
 * adjusted to minimize Rx FIFO overrun.
 */
#define	JME_RX_FIFO_SIZE	4000

#define	JME_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

#define	JME_PROC_MIN		10
#define	JME_PROC_DEFAULT	(JME_RX_RING_CNT / 2)
#define	JME_PROC_MAX		(JME_RX_RING_CNT - 1)

struct jme_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	int			tx_ndesc;
	struct jme_desc		*tx_desc;
};

struct jme_rxdesc {
	struct mbuf 		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct jme_desc		*rx_desc;
};

struct jme_chain_data{
	bus_dma_tag_t		jme_ring_tag;
	bus_dma_tag_t		jme_buffer_tag;
	bus_dma_tag_t		jme_ssb_tag;
	bus_dmamap_t		jme_ssb_map;
	bus_dma_tag_t		jme_tx_tag;
	struct jme_txdesc	jme_txdesc[JME_TX_RING_CNT];
	bus_dma_tag_t		jme_rx_tag;
	struct jme_rxdesc	jme_rxdesc[JME_RX_RING_CNT];
	bus_dma_tag_t		jme_tx_ring_tag;
	bus_dmamap_t		jme_tx_ring_map;
	bus_dma_tag_t		jme_rx_ring_tag;
	bus_dmamap_t		jme_rx_ring_map;
	bus_dmamap_t		jme_rx_sparemap;

	int			jme_tx_prod;
	int			jme_tx_cons;
	int			jme_tx_cnt;
	int			jme_rx_cons;
	int			jme_rxlen;

	struct mbuf		*jme_rxhead;
	struct mbuf		*jme_rxtail;
};

struct jme_ring_data {
	struct jme_desc		*jme_tx_ring;
	bus_addr_t		jme_tx_ring_paddr;
	struct jme_desc		*jme_rx_ring;
	bus_addr_t		jme_rx_ring_paddr;
	struct jme_ssb		*jme_ssb_block;
	bus_addr_t		jme_ssb_block_paddr;
};

#define	JME_TX_RING_ADDR(sc, i)	\
    ((sc)->jme_rdata.jme_tx_ring_paddr + sizeof(struct jme_desc) * (i))
#define	JME_RX_RING_ADDR(sc, i)	\
    ((sc)->jme_rdata.jme_rx_ring_paddr + sizeof(struct jme_desc) * (i))

#define	JME_TX_RING_SIZE	\
    (sizeof(struct jme_desc) * JME_TX_RING_CNT)
#define	JME_RX_RING_SIZE	\
    (sizeof(struct jme_desc) * JME_RX_RING_CNT)
#define	JME_SSB_SIZE		sizeof(struct jme_ssb)

/* Statistics counters. */
struct jme_hw_stats {
	uint32_t		rx_good_frames;
	uint32_t		rx_crc_errs;
	uint32_t		rx_mii_errs;
	uint32_t		rx_fifo_oflows;
	uint32_t		rx_desc_empty;
	uint32_t		rx_bad_frames;
	uint32_t		tx_good_frames;
	uint32_t		tx_bad_frames;
};

/*
 * Software state per device.
 */
struct jme_softc {
	struct ifnet 		*jme_ifp;
	device_t		jme_dev;
	device_t		jme_miibus;
	struct resource		*jme_res[1];
	struct resource_spec	*jme_res_spec;
	struct resource		*jme_irq[JME_MSI_MESSAGES];
	struct resource_spec	*jme_irq_spec;
	void			*jme_intrhand[JME_MSI_MESSAGES];
	int			jme_rev;
	int			jme_chip_rev;
	int			jme_phyaddr;
	uint8_t			jme_eaddr[ETHER_ADDR_LEN];
	uint32_t		jme_tx_dma_size;
	uint32_t		jme_rx_dma_size;
	int			jme_flags;
#define	JME_FLAG_FPGA		0x00000001
#define	JME_FLAG_PCIE		0x00000002
#define	JME_FLAG_PCIX		0x00000004
#define	JME_FLAG_MSI		0x00000008
#define	JME_FLAG_MSIX		0x00000010
#define	JME_FLAG_PMCAP		0x00000020
#define	JME_FLAG_FASTETH	0x00000040
#define	JME_FLAG_NOJUMBO	0x00000080
#define	JME_FLAG_RXCLK		0x00000100
#define	JME_FLAG_TXCLK		0x00000200
#define	JME_FLAG_DMA32BIT	0x00000400
#define	JME_FLAG_HWMIB		0x00000800
#define	JME_FLAG_EFUSE		0x00001000
#define	JME_FLAG_PCCPCD		0x00002000
#define	JME_FLAG_DETACH		0x40000000
#define	JME_FLAG_LINK		0x80000000

	struct jme_hw_stats	jme_ostats;
	struct jme_hw_stats	jme_stats;
	struct callout		jme_tick_ch;
	struct jme_chain_data	jme_cdata;
	struct jme_ring_data	jme_rdata;
	int			jme_if_flags;
	int			jme_watchdog_timer;
	uint32_t		jme_txcsr;
	uint32_t		jme_rxcsr;
	int			jme_process_limit;
	int			jme_tx_coal_to;
	int			jme_tx_pcd_to;
	int			jme_tx_coal_pkt;
	int			jme_rx_coal_to;
	int			jme_rx_pcd_to;
	int			jme_rx_coal_pkt;
	volatile int		jme_morework;

	struct task		jme_int_task;
	struct task		jme_link_task;
	struct taskqueue	*jme_tq;
	struct mtx		jme_mtx;
};

/* Register access macros. */
#define	CSR_WRITE_4(_sc, reg, val)	\
	bus_write_4((_sc)->jme_res[0], (reg), (val))
#define	CSR_READ_4(_sc, reg)		\
	bus_read_4((_sc)->jme_res[0], (reg))

#define	JME_LOCK(_sc)		mtx_lock(&(_sc)->jme_mtx)
#define	JME_UNLOCK(_sc)		mtx_unlock(&(_sc)->jme_mtx)
#define	JME_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->jme_mtx, MA_OWNED)

#define	JME_MAXERR	5

#define	JME_RXCHAIN_RESET(_sc)						\
do {									\
	(_sc)->jme_cdata.jme_rxhead = NULL;				\
	(_sc)->jme_cdata.jme_rxtail = NULL;				\
	(_sc)->jme_cdata.jme_rxlen = 0;					\
} while (0)

#define	JME_TX_TIMEOUT		5
#define	JME_TIMEOUT		1000
#define	JME_PHY_TIMEOUT		1000
#define	JME_EEPROM_TIMEOUT	1000

#endif
