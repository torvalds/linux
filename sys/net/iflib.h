/*-
 * Copyright (c) 2014-2017, Matthew Macy (mmacy@mattmacy.io)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Neither the name of Matthew Macy nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __IFLIB_H_
#define __IFLIB_H_

#include <sys/kobj.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#include <machine/bus.h>
#include <sys/nv.h>
#include <sys/gtaskqueue.h>

struct if_clone;

/*
 * The value type for indexing, limits max descriptors
 * to 65535 can be conditionally redefined to uint32_t
 * in the future if the need arises.
 */
typedef uint16_t qidx_t;
#define QIDX_INVALID 0xFFFF

struct iflib_ctx;
typedef struct iflib_ctx *if_ctx_t;
struct if_shared_ctx;
typedef struct if_shared_ctx *if_shared_ctx_t;
struct if_int_delay_info;
typedef struct if_int_delay_info  *if_int_delay_info_t;
struct if_pseudo;
typedef struct if_pseudo *if_pseudo_t;

/*
 * File organization:
 *  - public structures
 *  - iflib accessors
 *  - iflib utility functions
 *  - iflib core functions
 */

typedef struct if_rxd_frag {
	uint8_t irf_flid;
	qidx_t irf_idx;
	uint16_t irf_len;
} *if_rxd_frag_t;

/* bnxt supports 64 with hardware LRO enabled */
#define IFLIB_MAX_RX_SEGS		64

typedef struct if_rxd_info {
	/* set by iflib */
	uint16_t iri_qsidx;		/* qset index */
	uint16_t iri_vtag;		/* vlan tag - if flag set */
	/* XXX redundant with the new irf_len field */
	uint16_t iri_len;		/* packet length */
	qidx_t iri_cidx;		/* consumer index of cq */
	struct ifnet *iri_ifp;		/* some drivers >1 interface per softc */

	/* updated by driver */
	if_rxd_frag_t iri_frags;
	uint32_t iri_flowid;		/* RSS hash for packet */
	uint32_t iri_csum_flags;	/* m_pkthdr csum flags */

	uint32_t iri_csum_data;		/* m_pkthdr csum data */
	uint8_t iri_flags;		/* mbuf flags for packet */
	uint8_t	 iri_nfrags;		/* number of fragments in packet */
	uint8_t	 iri_rsstype;		/* RSS hash type */
	uint8_t	 iri_pad;		/* any padding in the received data */
} *if_rxd_info_t;

typedef struct if_rxd_update {
	uint64_t	*iru_paddrs;
	caddr_t		*iru_vaddrs;
	qidx_t		*iru_idxs;
	qidx_t		iru_pidx;
	uint16_t	iru_qsidx;
	uint16_t	iru_count;
	uint16_t	iru_buf_size;
	uint8_t		iru_flidx;
} *if_rxd_update_t;

#define IPI_TX_INTR	0x1		/* send an interrupt when this packet is sent */
#define IPI_TX_IPV4	0x2		/* ethertype IPv4 */
#define IPI_TX_IPV6	0x4		/* ethertype IPv6 */

typedef struct if_pkt_info {
	bus_dma_segment_t	*ipi_segs;	/* physical addresses */
	uint32_t		ipi_len;	/* packet length */
	uint16_t		ipi_qsidx;	/* queue set index */
	qidx_t			ipi_nsegs;	/* number of segments */

	qidx_t			ipi_ndescs;	/* number of descriptors used by encap */
	uint16_t		ipi_flags;	/* iflib per-packet flags */
	qidx_t			ipi_pidx;	/* start pidx for encap */
	qidx_t			ipi_new_pidx;	/* next available pidx post-encap */
	/* offload handling */
	uint8_t			ipi_ehdrlen;	/* ether header length */
	uint8_t			ipi_ip_hlen;	/* ip header length */
	uint8_t			ipi_tcp_hlen;	/* tcp header length */
	uint8_t			ipi_ipproto;	/* ip protocol */

	uint32_t		ipi_csum_flags;	/* packet checksum flags */
	uint16_t		ipi_tso_segsz;	/* tso segment size */
	uint16_t		ipi_vtag;	/* VLAN tag */
	uint16_t		ipi_etype;	/* ether header type */
	uint8_t			ipi_tcp_hflags;	/* tcp header flags */
	uint8_t			ipi_mflags;	/* packet mbuf flags */

	uint32_t		ipi_tcp_seq;	/* tcp seqno */
	uint32_t		ipi_tcp_sum;	/* tcp csum */
} *if_pkt_info_t;

typedef struct if_irq {
	struct resource  *ii_res;
	int               ii_rid;
	void             *ii_tag;
} *if_irq_t;

struct if_int_delay_info {
	if_ctx_t iidi_ctx;	/* Back-pointer to the iflib ctx (softc) */
	int iidi_offset;			/* Register offset to read/write */
	int iidi_value;			/* Current value in usecs */
	struct sysctl_oid *iidi_oidp;
	struct sysctl_req *iidi_req;
};

typedef enum {
	IFLIB_INTR_LEGACY,
	IFLIB_INTR_MSI,
	IFLIB_INTR_MSIX
} iflib_intr_mode_t;

/*
 * This really belongs in pciio.h or some place more general
 * but this is the only consumer for now.
 */
typedef struct pci_vendor_info {
	uint32_t	pvi_vendor_id;
	uint32_t	pvi_device_id;
	uint32_t	pvi_subvendor_id;
	uint32_t	pvi_subdevice_id;
	uint32_t	pvi_rev_id;
	uint32_t	pvi_class_mask;
	caddr_t		pvi_name;
} pci_vendor_info_t;

#define PVID(vendor, devid, name) {vendor, devid, 0, 0, 0, 0, name}
#define PVID_OEM(vendor, devid, svid, sdevid, revid, name) {vendor, devid, svid, sdevid, revid, 0, name}
#define PVID_END {0, 0, 0, 0, 0, 0, NULL}

#define IFLIB_PNP_DESCR "U32:vendor;U32:device;U32:subvendor;U32:subdevice;" \
    "U32:revision;U32:class;D:#"
#define IFLIB_PNP_INFO(b, u, t) \
    MODULE_PNP_INFO(IFLIB_PNP_DESCR, b, u, t, nitems(t) - 1)

typedef struct if_txrx {
	int (*ift_txd_encap) (void *, if_pkt_info_t);
	void (*ift_txd_flush) (void *, uint16_t, qidx_t pidx);
	int (*ift_txd_credits_update) (void *, uint16_t qsidx, bool clear);

	int (*ift_rxd_available) (void *, uint16_t qsidx, qidx_t pidx, qidx_t budget);
	int (*ift_rxd_pkt_get) (void *, if_rxd_info_t ri);
	void (*ift_rxd_refill) (void * , if_rxd_update_t iru);
	void (*ift_rxd_flush) (void *, uint16_t qsidx, uint8_t flidx, qidx_t pidx);
	int (*ift_legacy_intr) (void *);
} *if_txrx_t;

typedef struct if_softc_ctx {
	int isc_vectors;
	int isc_nrxqsets;
	int isc_ntxqsets;
	uint8_t isc_min_tx_latency; /* disable doorbell update batching */
	uint8_t isc_rx_mvec_enable; /* generate mvecs on rx */
	uint32_t isc_txrx_budget_bytes_max;
	int isc_msix_bar;		/* can be model specific - initialize in attach_pre */
	int isc_tx_nsegments;		/* can be model specific - initialize in attach_pre */
	int isc_ntxd[8];
	int isc_nrxd[8];

	uint32_t isc_txqsizes[8];
	uint32_t isc_rxqsizes[8];
	/* is there such thing as a descriptor that is more than 248 bytes ? */
	uint8_t isc_txd_size[8];
	uint8_t isc_rxd_size[8];

	int isc_tx_tso_segments_max;
	int isc_tx_tso_size_max;
	int isc_tx_tso_segsize_max;
	int isc_tx_csum_flags;
	int isc_capabilities;
	int isc_capenable;
	int isc_rss_table_size;
	int isc_rss_table_mask;
	int isc_nrxqsets_max;
	int isc_ntxqsets_max;
	uint32_t isc_tx_qdepth;

	iflib_intr_mode_t isc_intr;
	uint16_t isc_max_frame_size; /* set at init time by driver */
	uint16_t isc_min_frame_size; /* set at init time by driver, only used if
					IFLIB_NEED_ETHER_PAD is set. */
	uint32_t isc_pause_frames;   /* set by driver for iflib_timer to detect */
	pci_vendor_info_t isc_vendor_info;	/* set by iflib prior to attach_pre */
	int isc_disable_msix;
	if_txrx_t isc_txrx;
} *if_softc_ctx_t;

/*
 * Initialization values for device
 */
struct if_shared_ctx {
	unsigned isc_magic;
	driver_t *isc_driver;
	bus_size_t isc_q_align;
	bus_size_t isc_tx_maxsize;
	bus_size_t isc_tx_maxsegsize;
	bus_size_t isc_tso_maxsize;
	bus_size_t isc_tso_maxsegsize;
	bus_size_t isc_rx_maxsize;
	bus_size_t isc_rx_maxsegsize;
	int isc_rx_nsegments;
	int isc_admin_intrcnt;		/* # of admin/link interrupts */

	/* fields necessary for probe */
	pci_vendor_info_t *isc_vendor_info;
	const char *isc_driver_version;
	/* optional function to transform the read values to match the table*/
	void (*isc_parse_devinfo) (uint16_t *device_id, uint16_t *subvendor_id,
				   uint16_t *subdevice_id, uint16_t *rev_id);
	int isc_nrxd_min[8];
	int isc_nrxd_default[8];
	int isc_nrxd_max[8];
	int isc_ntxd_min[8];
	int isc_ntxd_default[8];
	int isc_ntxd_max[8];

	/* actively used during operation */
	int isc_nfl __aligned(CACHE_LINE_SIZE);
	int isc_ntxqs;			/* # of tx queues per tx qset - usually 1 */
	int isc_nrxqs;			/* # of rx queues per rx qset - intel 1, chelsio 2, broadcom 3 */
	int isc_rx_process_limit;
	int isc_tx_reclaim_thresh;
	int isc_flags;
	const char *isc_name;
};

typedef struct iflib_dma_info {
	bus_addr_t		idi_paddr;
	caddr_t			idi_vaddr;
	bus_dma_tag_t		idi_tag;
	bus_dmamap_t		idi_map;
	uint32_t		idi_size;
} *iflib_dma_info_t;

#define IFLIB_MAGIC 0xCAFEF00D

typedef enum {
	IFLIB_INTR_RX,
	IFLIB_INTR_TX,
	IFLIB_INTR_RXTX,
	IFLIB_INTR_ADMIN,
	IFLIB_INTR_IOV,
} iflib_intr_type_t;

#ifndef ETH_ADDR_LEN
#define ETH_ADDR_LEN 6
#endif


/*
 * Interface has a separate command queue for RX
 */
#define IFLIB_HAS_RXCQ		0x01
/*
 * Driver has already allocated vectors
 */
#define IFLIB_SKIP_MSIX		0x02
/*
 * Interface is a virtual function
 */
#define IFLIB_IS_VF		0x04
/*
 * Interface has a separate command queue for TX
 */
#define IFLIB_HAS_TXCQ		0x08
/*
 * Interface does checksum in place
 */
#define IFLIB_NEED_SCRATCH	0x10
/*
 * Interface doesn't expect in_pseudo for th_sum
 */
#define IFLIB_TSO_INIT_IP	0x20
/*
 * Interface doesn't align IP header
 */
#define IFLIB_DO_RX_FIXUP	0x40
/*
 * Driver needs csum zeroed for offloading
 */
#define IFLIB_NEED_ZERO_CSUM	0x80
/*
 * Driver needs frames padded to some minimum length
 */
#define IFLIB_NEED_ETHER_PAD	0x100
/*
 * Packets can be freed immediately after encap
 */
#define IFLIB_TXD_ENCAP_PIO	0x00200
/*
 * Use RX completion handler
 */
#define IFLIB_RX_COMPLETION	0x00400
/*
 * Skip refilling cluster free lists
 */
#define IFLIB_SKIP_CLREFILL	0x00800
/*
 * Don't reset on hang
 */
#define IFLIB_NO_HANG_RESET	0x01000
/*
 * Don't need/want most of the niceties of
 * queue management
 */
#define IFLIB_PSEUDO	0x02000
/*
 * No DMA support needed / wanted
 */
#define IFLIB_VIRTUAL	0x04000
/*
 * autogenerate a MAC address
 */
#define IFLIB_GEN_MAC	0x08000
/*
 * Interface needs admin task to ignore interface up/down status
 */
#define IFLIB_ADMIN_ALWAYS_RUN	0x10000


/*
 * field accessors
 */
void *iflib_get_softc(if_ctx_t ctx);

device_t iflib_get_dev(if_ctx_t ctx);

if_t iflib_get_ifp(if_ctx_t ctx);

struct ifmedia *iflib_get_media(if_ctx_t ctx);

if_softc_ctx_t iflib_get_softc_ctx(if_ctx_t ctx);
if_shared_ctx_t iflib_get_sctx(if_ctx_t ctx);

void iflib_set_mac(if_ctx_t ctx, uint8_t mac[ETHER_ADDR_LEN]);
void iflib_request_reset(if_ctx_t ctx);
uint8_t iflib_in_detach(if_ctx_t ctx);

uint32_t iflib_get_rx_mbuf_sz(if_ctx_t ctx);

/*
 * If the driver can plug cleanly in to newbus use these
 */
int iflib_device_probe(device_t);
int iflib_device_attach(device_t);
int iflib_device_detach(device_t);
int iflib_device_suspend(device_t);
int iflib_device_resume(device_t);
int iflib_device_shutdown(device_t);


int iflib_device_iov_init(device_t, uint16_t, const nvlist_t *);
void iflib_device_iov_uninit(device_t);
int iflib_device_iov_add_vf(device_t, uint16_t, const nvlist_t *);

/*
 * If the driver can't plug cleanly in to newbus
 * use these
 */
int iflib_device_register(device_t dev, void *softc, if_shared_ctx_t sctx, if_ctx_t *ctxp);
int iflib_device_deregister(if_ctx_t);



int iflib_irq_alloc(if_ctx_t, if_irq_t, int, driver_filter_t, void *filter_arg, driver_intr_t, void *arg, const char *name);
int iflib_irq_alloc_generic(if_ctx_t ctx, if_irq_t irq, int rid,
			    iflib_intr_type_t type, driver_filter_t *filter,
			    void *filter_arg, int qid, const char *name);
void iflib_softirq_alloc_generic(if_ctx_t ctx, if_irq_t irq, iflib_intr_type_t type,  void *arg, int qid, const char *name);

void iflib_irq_free(if_ctx_t ctx, if_irq_t irq);

void iflib_io_tqg_attach(struct grouptask *gt, void *uniq, int cpu, char *name);

void iflib_config_gtask_init(void *ctx, struct grouptask *gtask,
			     gtask_fn_t *fn, const char *name);

void iflib_config_gtask_deinit(struct grouptask *gtask);



void iflib_tx_intr_deferred(if_ctx_t ctx, int txqid);
void iflib_rx_intr_deferred(if_ctx_t ctx, int rxqid);
void iflib_admin_intr_deferred(if_ctx_t ctx);
void iflib_iov_intr_deferred(if_ctx_t ctx);


void iflib_link_state_change(if_ctx_t ctx, int linkstate, uint64_t baudrate);

int iflib_dma_alloc(if_ctx_t ctx, int size, iflib_dma_info_t dma, int mapflags);
int iflib_dma_alloc_align(if_ctx_t ctx, int size, int align, iflib_dma_info_t dma, int mapflags);
void iflib_dma_free(iflib_dma_info_t dma);

int iflib_dma_alloc_multi(if_ctx_t ctx, int *sizes, iflib_dma_info_t *dmalist, int mapflags, int count);

void iflib_dma_free_multi(iflib_dma_info_t *dmalist, int count);


struct sx *iflib_ctx_lock_get(if_ctx_t);
struct mtx *iflib_qset_lock_get(if_ctx_t, uint16_t);

void iflib_led_create(if_ctx_t ctx);

void iflib_add_int_delay_sysctl(if_ctx_t, const char *, const char *,
								if_int_delay_info_t, int, int);

/*
 * Pseudo device support
 */
if_pseudo_t iflib_clone_register(if_shared_ctx_t);
void iflib_clone_deregister(if_pseudo_t);
#endif /*  __IFLIB_H_ */
