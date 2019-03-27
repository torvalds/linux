/*	$OpenBSD: if_nfevar.h,v 1.11 2006/02/19 13:57:02 damien Exp $	*/

/*-
 * Copyright (c) 2005 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

struct nfe_tx_data {
	bus_dmamap_t	tx_data_map;
	struct mbuf	*m;
};

struct nfe_tx_ring {
	bus_dma_tag_t		tx_desc_tag;
	bus_dmamap_t		tx_desc_map;
	bus_addr_t		physaddr;
	struct nfe_desc32	*desc32;
	struct nfe_desc64	*desc64;
	bus_dma_tag_t		tx_data_tag;
	struct nfe_tx_data	data[NFE_TX_RING_COUNT];
	int			queued;
	int			cur;
	int			next;
};

struct nfe_rx_data {
	bus_dmamap_t	rx_data_map;
	bus_addr_t	paddr;
	struct mbuf	*m;
};

struct nfe_rx_ring {
	bus_dma_tag_t		rx_desc_tag;
	bus_dmamap_t		rx_desc_map;
	bus_addr_t		physaddr;
	struct nfe_desc32	*desc32;
	struct nfe_desc64	*desc64;
	bus_dma_tag_t		rx_data_tag;
	bus_dmamap_t		rx_spare_map;
	struct nfe_rx_data	data[NFE_RX_RING_COUNT];
	int			cur;
	int			next;
};

struct nfe_jrx_ring {
	bus_dma_tag_t		jrx_desc_tag;
	bus_dmamap_t		jrx_desc_map;
	bus_dma_tag_t		jrx_jumbo_tag;
	bus_dmamap_t		jrx_jumbo_map;
	bus_addr_t		jphysaddr;
	struct nfe_desc32	*jdesc32;
	struct nfe_desc64	*jdesc64;
	bus_dma_tag_t		jrx_data_tag;
	bus_dmamap_t		jrx_spare_map;
	struct nfe_rx_data	jdata[NFE_JUMBO_RX_RING_COUNT];
	int			jcur;
	int			jnext;
};

struct nfe_hw_stats {
	uint64_t		rx_octets;
	uint32_t		rx_frame_errors;
	uint32_t		rx_extra_bytes;
	uint32_t		rx_late_cols;
	uint32_t		rx_runts;
	uint32_t		rx_jumbos;
	uint32_t		rx_fifo_overuns;
	uint32_t		rx_crc_errors;
	uint32_t		rx_fae;
	uint32_t		rx_len_errors;
	uint32_t		rx_unicast;
	uint32_t		rx_multicast;
	uint32_t		rx_broadcast;
	uint32_t		rx_pause;
	uint32_t		rx_drops;
	uint64_t		tx_octets;
	uint32_t		tx_zero_rexmits;
	uint32_t		tx_one_rexmits;
	uint32_t		tx_multi_rexmits;
	uint32_t		tx_late_cols;
	uint32_t		tx_fifo_underuns;
	uint32_t		tx_carrier_losts;
	uint32_t		tx_excess_deferals;
	uint32_t		tx_retry_errors;
	uint32_t		tx_deferals;
	uint32_t		tx_frames;
	uint32_t		tx_pause;
	uint32_t		tx_unicast;
	uint32_t		tx_multicast;
	uint32_t		tx_broadcast;
};

struct nfe_softc {
	struct ifnet		*nfe_ifp;
	device_t		nfe_dev;
	uint16_t		nfe_devid;
	uint16_t		nfe_revid;
	device_t		nfe_miibus;
	struct mtx		nfe_mtx;
	struct resource		*nfe_res[1];
	struct resource		*nfe_msix_res;
	struct resource		*nfe_msix_pba_res;
	struct resource		*nfe_irq[NFE_MSI_MESSAGES];
	void			*nfe_intrhand[NFE_MSI_MESSAGES];
	struct callout		nfe_stat_ch;
	int			nfe_watchdog_timer;

	bus_dma_tag_t		nfe_parent_tag;

	int			nfe_if_flags;
	uint32_t		nfe_flags;
#define	NFE_JUMBO_SUP		0x0001
#define	NFE_40BIT_ADDR		0x0002
#define	NFE_HW_CSUM		0x0004
#define	NFE_HW_VLAN		0x0008
#define	NFE_PWR_MGMT		0x0010
#define	NFE_CORRECT_MACADDR	0x0020
#define	NFE_TX_FLOW_CTRL	0x0040
#define	NFE_MIB_V1		0x0080
#define	NFE_MIB_V2		0x0100
#define	NFE_MIB_V3		0x0200
	int			nfe_jumbo_disable;
	uint32_t		rxtxctl;
	uint8_t			mii_phyaddr;
	uint8_t			eaddr[ETHER_ADDR_LEN];
	struct nfe_hw_stats	nfe_stats;
	struct taskqueue	*nfe_tq;
	struct task		nfe_int_task;
	int			nfe_link;
	int			nfe_suspended;
	int			nfe_framesize;
	int			nfe_process_limit;
	int			nfe_force_tx;
	uint32_t		nfe_irq_status;
	uint32_t		nfe_irq_mask;
	uint32_t		nfe_intrs;
	uint32_t		nfe_nointrs;
	uint32_t		nfe_msi;
	uint32_t		nfe_msix;

	struct nfe_tx_ring	txq;
	struct nfe_rx_ring	rxq;
	struct nfe_jrx_ring	jrxq;
};

struct nfe_type {
	uint16_t	vid_id;
	uint16_t	dev_id;
	char		*name;
};
