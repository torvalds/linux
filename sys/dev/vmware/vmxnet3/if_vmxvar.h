/*-
 * Copyright (c) 2013 Tsubai Masanari
 * Copyright (c) 2013 Bryan Venteicher <bryanv@FreeBSD.org>
 * Copyright (c) 2018 Patrick Kelsey
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

#ifndef _IF_VMXVAR_H
#define _IF_VMXVAR_H

struct vmxnet3_softc;

/*
 * The number of Rx/Tx queues this driver prefers.
 */
#define VMXNET3_DEF_RX_QUEUES	8
#define VMXNET3_DEF_TX_QUEUES	8

/*
 * The number of Rx rings in each Rx queue.
 */
#define VMXNET3_RXRINGS_PERQ	2

/*
 * The number of descriptors in each Rx/Tx ring.
 */
#define VMXNET3_DEF_TX_NDESC		512
#define VMXNET3_MAX_TX_NDESC		4096
#define VMXNET3_MIN_TX_NDESC		32
#define VMXNET3_MASK_TX_NDESC		0x1F
#define VMXNET3_DEF_RX_NDESC		256
#define VMXNET3_MAX_RX_NDESC		2048
#define VMXNET3_MIN_RX_NDESC		32
#define VMXNET3_MASK_RX_NDESC		0x1F

#define VMXNET3_MAX_TX_NCOMPDESC	VMXNET3_MAX_TX_NDESC
#define VMXNET3_MAX_RX_NCOMPDESC \
    (VMXNET3_MAX_RX_NDESC * VMXNET3_RXRINGS_PERQ)

struct vmxnet3_txring {
	u_int			 vxtxr_next;
	u_int			 vxtxr_ndesc;
	int			 vxtxr_gen;
	struct vmxnet3_txdesc	*vxtxr_txd;
	bus_addr_t		 vxtxr_paddr;
};

struct vmxnet3_rxring {
	struct vmxnet3_rxdesc	*vxrxr_rxd;
	u_int			 vxrxr_ndesc;
	int			 vxrxr_gen;
	bus_addr_t		 vxrxr_paddr;
};

struct vmxnet3_comp_ring {
	union {
		struct vmxnet3_txcompdesc *txcd;
		struct vmxnet3_rxcompdesc *rxcd;
	}			 vxcr_u;
	/*
	 * vxcr_next is used on the transmit side to track the next index to
	 * begin cleaning at.  It is not used on the receive side.
	 */
	u_int			 vxcr_next;
	u_int			 vxcr_ndesc;
	int			 vxcr_gen;
	bus_addr_t		 vxcr_paddr;
};

struct vmxnet3_txqueue {
	struct vmxnet3_softc		*vxtxq_sc;
	int				 vxtxq_id;
	int				 vxtxq_last_flush;
	int				 vxtxq_intr_idx;
	struct vmxnet3_txring		 vxtxq_cmd_ring;
	struct vmxnet3_comp_ring	 vxtxq_comp_ring;
	struct vmxnet3_txq_shared	*vxtxq_ts;
	struct sysctl_oid_list		*vxtxq_sysctl;
	char				 vxtxq_name[16];
} __aligned(CACHE_LINE_SIZE);

struct vmxnet3_rxqueue {
	struct vmxnet3_softc		*vxrxq_sc;
	int				 vxrxq_id;
	int				 vxrxq_intr_idx;
	struct if_irq			 vxrxq_irq;
	struct vmxnet3_rxring		 vxrxq_cmd_ring[VMXNET3_RXRINGS_PERQ];
	struct vmxnet3_comp_ring	 vxrxq_comp_ring;
	struct vmxnet3_rxq_shared	*vxrxq_rs;
	struct sysctl_oid_list		*vxrxq_sysctl;
	char				 vxrxq_name[16];
} __aligned(CACHE_LINE_SIZE);

struct vmxnet3_softc {
	device_t			 vmx_dev;
	if_ctx_t			 vmx_ctx;
	if_shared_ctx_t			 vmx_sctx;
	if_softc_ctx_t			 vmx_scctx;
	struct ifnet			*vmx_ifp;
	struct vmxnet3_driver_shared	*vmx_ds;
	uint32_t			 vmx_flags;
#define VMXNET3_FLAG_RSS	0x0002

	struct vmxnet3_rxqueue		*vmx_rxq;
	struct vmxnet3_txqueue		*vmx_txq;

	struct resource			*vmx_res0;
	bus_space_tag_t			 vmx_iot0;
	bus_space_handle_t		 vmx_ioh0;
	struct resource			*vmx_res1;
	bus_space_tag_t			 vmx_iot1;
	bus_space_handle_t		 vmx_ioh1;

	int				 vmx_link_active;

	int				 vmx_intr_mask_mode;
	int				 vmx_event_intr_idx;
	struct if_irq			 vmx_event_intr_irq;

	uint8_t				*vmx_mcast;
	struct vmxnet3_rss_shared	*vmx_rss;
	struct iflib_dma_info		 vmx_ds_dma;
	struct iflib_dma_info		 vmx_qs_dma;
	struct iflib_dma_info		 vmx_mcast_dma;
	struct iflib_dma_info		 vmx_rss_dma;
	struct ifmedia			*vmx_media;
	uint32_t			 vmx_vlan_filter[4096/32];
	uint8_t				 vmx_lladdr[ETHER_ADDR_LEN];
};

/*
 * Our driver version we report to the hypervisor; we just keep
 * this value constant.
 */
#define VMXNET3_DRIVER_VERSION 0x00010000

/*
 * Max descriptors per Tx packet. We must limit the size of the
 * any TSO packets based on the number of segments.
 */
#define VMXNET3_TX_MAXSEGS		32  /* 64K @ 2K segment size */
#define VMXNET3_TX_MAXSIZE		(VMXNET3_TX_MAXSEGS * MCLBYTES)
#define VMXNET3_TSO_MAXSIZE		(VMXNET3_TX_MAXSIZE - ETHER_VLAN_ENCAP_LEN) 

/*
 * Maximum supported Tx segment size. The length field in the
 * Tx descriptor is 14 bits.
 *
 * XXX It's possible a descriptor length field of 0 means 2^14, but this
 * isn't confirmed, so limit to 2^14 - 1 for now.
 */
#define VMXNET3_TX_MAXSEGSIZE		((1 << 14) - 1)

/*
 * Maximum supported Rx segment size. The length field in the
 * Rx descriptor is 14 bits.
 *
 * The reference drivers skip zero-length descriptors, which seems to be a
 * strong indication that on the receive side, a descriptor length field of
 * zero does not mean 2^14.
 */
#define VMXNET3_RX_MAXSEGSIZE		((1 << 14) - 1)

/*
 * Predetermined size of the multicast MACs filter table. If the
 * number of multicast addresses exceeds this size, then the
 * ALL_MULTI mode is use instead.
 */
#define VMXNET3_MULTICAST_MAX		32

/*
 * IP protocols that we can perform Tx checksum offloading of.
 */
#define VMXNET3_CSUM_OFFLOAD		(CSUM_TCP | CSUM_UDP)
#define VMXNET3_CSUM_OFFLOAD_IPV6	(CSUM_TCP_IPV6 | CSUM_UDP_IPV6)

#define VMXNET3_CSUM_ALL_OFFLOAD	\
    (VMXNET3_CSUM_OFFLOAD | VMXNET3_CSUM_OFFLOAD_IPV6 | CSUM_TSO)

#endif /* _IF_VMXVAR_H */
