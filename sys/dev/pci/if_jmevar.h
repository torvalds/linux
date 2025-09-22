/*	$OpenBSD: if_jmevar.h,v 1.6 2013/12/07 07:22:37 brad Exp $	*/
/*-
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
 * $FreeBSD: src/sys/dev/jme/if_jmevar.h,v 1.1 2008/05/27 01:42:01 yongari Exp $
 * $DragonFly: src/sys/dev/netif/jme/if_jmevar.h,v 1.4 2008/09/13 04:04:39 sephe Exp $
 */

#ifndef	_IF_JMEVAR_H
#define	_IF_JMEVAR_H

/*
 * JMC250 supports upto 1024 descriptors and the number of
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
#define	JME_MAXTXSEGS		32
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
#define JME_JUMBO_FRAMELEN	9216
#define JME_JUMBO_MTU							\
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
	struct jme_txdesc	jme_txdesc[JME_TX_RING_CNT];
	bus_dma_tag_t		jme_rx_tag;
	struct jme_rxdesc	jme_rxdesc[JME_RX_RING_CNT];
	bus_dmamap_t		jme_tx_ring_map;
	bus_dma_segment_t	jme_tx_ring_seg;
	bus_dmamap_t		jme_rx_ring_map;
	bus_dma_segment_t	jme_rx_ring_seg;
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
	bus_dma_segment_t	jme_tx_ring_seg;
	bus_addr_t		jme_tx_ring_paddr;
	struct jme_desc		*jme_rx_ring;
	bus_dma_segment_t	jme_rx_ring_seg;
	bus_addr_t		jme_rx_ring_paddr;
	struct jme_ssb		*jme_ssb_block;
	bus_dma_segment_t	jme_ssb_block_seg;
	bus_addr_t		jme_ssb_block_paddr;
};

#define JME_TX_RING_ADDR(sc, i)	\
    ((sc)->jme_rdata.jme_tx_ring_paddr + sizeof(struct jme_desc) * (i))
#define JME_RX_RING_ADDR(sc, i)	\
    ((sc)->jme_rdata.jme_rx_ring_paddr + sizeof(struct jme_desc) * (i))

#define JME_TX_RING_SIZE	\
    (sizeof(struct jme_desc) * JME_TX_RING_CNT)
#define JME_RX_RING_SIZE	\
    (sizeof(struct jme_desc) * JME_RX_RING_CNT)
#define	JME_SSB_SIZE		sizeof(struct jme_ssb)

struct jme_dmamap_ctx {
	int			nsegs;
	bus_dma_segment_t	*segs;
};

/*
 * Software state per device.
 */
struct jme_softc {
	struct device		sc_dev;
	struct arpcom		sc_arpcom;

	int			jme_mem_rid;
	struct resource		*jme_mem_res;
	bus_space_tag_t		jme_mem_bt;
	bus_space_handle_t	jme_mem_bh;
	bus_size_t		jme_mem_size;
	bus_dma_tag_t		sc_dmat;
	pci_chipset_tag_t	jme_pct;
	pcitag_t		jme_pcitag;
	uint8_t			jme_revfm;

	int			jme_irq_rid;
	struct resource		*jme_irq_res;
	void			*sc_irq_handle;

	struct mii_data		sc_miibus;
	int			jme_phyaddr;

	uint32_t		jme_tx_dma_size;
	uint32_t		jme_rx_dma_size;

	uint32_t		jme_caps;
#define	JME_CAP_FPGA		0x0001
#define	JME_CAP_PCIE		0x0002
#define	JME_CAP_PMCAP		0x0004
#define	JME_CAP_FASTETH		0x0008
#define	JME_CAP_JUMBO		0x0010

	uint32_t		jme_workaround;
#define JME_WA_CRCERRORS	0x0001
#define JME_WA_PACKETLOSS	0x0002

	uint32_t		jme_flags;
#define	JME_FLAG_MSI		0x0001
#define	JME_FLAG_MSIX		0x0002
#define	JME_FLAG_DETACH		0x0004
#define	JME_FLAG_LINK		0x0008

	struct timeout		jme_tick_ch;
	struct jme_chain_data	jme_cdata;
	struct jme_ring_data	jme_rdata;
	uint32_t		jme_txcsr;
	uint32_t		jme_rxcsr;

	/*
	 * Sysctl variables
	 */
	int			jme_process_limit;
	int			jme_tx_coal_to;
	int			jme_tx_coal_pkt;
	int			jme_rx_coal_to;
	int			jme_rx_coal_pkt;
};

/* Register access macros. */
#define CSR_WRITE_4(_sc, reg, val)	\
	bus_space_write_4((_sc)->jme_mem_bt, (_sc)->jme_mem_bh, (reg), (val))
#define CSR_READ_4(_sc, reg)		\
	bus_space_read_4((_sc)->jme_mem_bt, (_sc)->jme_mem_bh, (reg))

#define	JME_MAXERR	5

#define	JME_RXCHAIN_RESET(_sc)						\
do {									\
	(_sc)->jme_cdata.jme_rxhead = NULL;				\
	(_sc)->jme_cdata.jme_rxtail = NULL;				\
	(_sc)->jme_cdata.jme_rxlen = 0;					\
} while (0)

#define	JME_TX_TIMEOUT		5
#define JME_TIMEOUT		1000
#define JME_PHY_TIMEOUT		1000
#define JME_EEPROM_TIMEOUT	1000

#define JME_TXD_RSVD		1

#endif
