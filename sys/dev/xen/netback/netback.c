/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2011 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 *          Alan Somers         (Spectra Logic Corporation)
 *          John Suykerbuyk     (Spectra Logic Corporation)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * \file netback.c
 *
 * \brief Device driver supporting the vending of network access
 * 	  from this FreeBSD domain to other domains.
 */
#include "opt_inet.h"
#include "opt_inet6.h"

#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/kernel.h>

#include <sys/bus.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#if __FreeBSD_version >= 700000
#include <netinet/tcp.h>
#endif
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <machine/in_cksum.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#include <machine/_inttypes.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_intr.h>
#include <xen/interface/io/netif.h>
#include <xen/xenbus/xenbusvar.h>

/*--------------------------- Compile-time Tunables --------------------------*/

/*---------------------------------- Macros ----------------------------------*/
/**
 * Custom malloc type for all driver allocations.
 */
static MALLOC_DEFINE(M_XENNETBACK, "xnb", "Xen Net Back Driver Data");

#define	XNB_SG	1	/* netback driver supports feature-sg */
#define	XNB_GSO_TCPV4 0	/* netback driver supports feature-gso-tcpv4 */
#define	XNB_RX_COPY 1	/* netback driver supports feature-rx-copy */
#define	XNB_RX_FLIP 0	/* netback driver does not support feature-rx-flip */

#undef XNB_DEBUG
#define	XNB_DEBUG /* hardcode on during development */

#ifdef XNB_DEBUG
#define	DPRINTF(fmt, args...) \
	printf("xnb(%s:%d): " fmt, __FUNCTION__, __LINE__, ##args)
#else
#define	DPRINTF(fmt, args...) do {} while (0)
#endif

/* Default length for stack-allocated grant tables */
#define	GNTTAB_LEN	(64)

/* Features supported by all backends.  TSO and LRO can be negotiated */
#define	XNB_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)

#define	NET_TX_RING_SIZE __RING_SIZE((netif_tx_sring_t *)0, PAGE_SIZE)
#define	NET_RX_RING_SIZE __RING_SIZE((netif_rx_sring_t *)0, PAGE_SIZE)

/**
 * Two argument version of the standard macro.  Second argument is a tentative
 * value of req_cons
 */
#define	RING_HAS_UNCONSUMED_REQUESTS_2(_r, cons) ({                     \
	unsigned int req = (_r)->sring->req_prod - cons;          	\
	unsigned int rsp = RING_SIZE(_r) -                              \
	(cons - (_r)->rsp_prod_pvt);                          		\
	req < rsp ? req : rsp;                                          \
})

#define	virt_to_mfn(x) (vtophys(x) >> PAGE_SHIFT)
#define	virt_to_offset(x) ((x) & (PAGE_SIZE - 1))

/**
 * Predefined array type of grant table copy descriptors.  Used to pass around
 * statically allocated memory structures.
 */
typedef struct gnttab_copy gnttab_copy_table[GNTTAB_LEN];

/*--------------------------- Forward Declarations ---------------------------*/
struct xnb_softc;
struct xnb_pkt;

static void	xnb_attach_failed(struct xnb_softc *xnb,
				  int err, const char *fmt, ...)
				  __printflike(3,4);
static int	xnb_shutdown(struct xnb_softc *xnb);
static int	create_netdev(device_t dev);
static int	xnb_detach(device_t dev);
static int	xnb_ifmedia_upd(struct ifnet *ifp);
static void	xnb_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr);
static void 	xnb_intr(void *arg);
static int	xnb_send(netif_rx_back_ring_t *rxb, domid_t otherend,
			 const struct mbuf *mbufc, gnttab_copy_table gnttab);
static int	xnb_recv(netif_tx_back_ring_t *txb, domid_t otherend,
			 struct mbuf **mbufc, struct ifnet *ifnet,
			 gnttab_copy_table gnttab);
static int	xnb_ring2pkt(struct xnb_pkt *pkt,
			     const netif_tx_back_ring_t *tx_ring,
			     RING_IDX start);
static void	xnb_txpkt2rsp(const struct xnb_pkt *pkt,
			      netif_tx_back_ring_t *ring, int error);
static struct mbuf *xnb_pkt2mbufc(const struct xnb_pkt *pkt, struct ifnet *ifp);
static int	xnb_txpkt2gnttab(const struct xnb_pkt *pkt,
				 struct mbuf *mbufc,
				 gnttab_copy_table gnttab,
				 const netif_tx_back_ring_t *txb,
				 domid_t otherend_id);
static void	xnb_update_mbufc(struct mbuf *mbufc,
				 const gnttab_copy_table gnttab, int n_entries);
static int	xnb_mbufc2pkt(const struct mbuf *mbufc,
			      struct xnb_pkt *pkt,
			      RING_IDX start, int space);
static int	xnb_rxpkt2gnttab(const struct xnb_pkt *pkt,
				 const struct mbuf *mbufc,
				 gnttab_copy_table gnttab,
				 const netif_rx_back_ring_t *rxb,
				 domid_t otherend_id);
static int	xnb_rxpkt2rsp(const struct xnb_pkt *pkt,
			      const gnttab_copy_table gnttab, int n_entries,
			      netif_rx_back_ring_t *ring);
static void	xnb_stop(struct xnb_softc*);
static int	xnb_ioctl(struct ifnet*, u_long, caddr_t);
static void	xnb_start_locked(struct ifnet*);
static void	xnb_start(struct ifnet*);
static void	xnb_ifinit_locked(struct xnb_softc*);
static void	xnb_ifinit(void*);
#ifdef XNB_DEBUG
static int	xnb_unit_test_main(SYSCTL_HANDLER_ARGS);
static int	xnb_dump_rings(SYSCTL_HANDLER_ARGS);
#endif
#if defined(INET) || defined(INET6)
static void	xnb_add_mbuf_cksum(struct mbuf *mbufc);
#endif
/*------------------------------ Data Structures -----------------------------*/


/**
 * Representation of a xennet packet.  Simplified version of a packet as
 * stored in the Xen tx ring.  Applicable to both RX and TX packets
 */
struct xnb_pkt{
	/**
	 * Array index of the first data-bearing (eg, not extra info) entry
	 * for this packet
	 */
	RING_IDX	car;

	/**
	 * Array index of the second data-bearing entry for this packet.
	 * Invalid if the packet has only one data-bearing entry.  If the
	 * packet has more than two data-bearing entries, then the second
	 * through the last will be sequential modulo the ring size
	 */
	RING_IDX	cdr;

	/**
	 * Optional extra info.  Only valid if flags contains
	 * NETTXF_extra_info.  Note that extra.type will always be
	 * XEN_NETIF_EXTRA_TYPE_GSO.  Currently, no known netfront or netback
	 * driver will ever set XEN_NETIF_EXTRA_TYPE_MCAST_*
	 */
	netif_extra_info_t extra;

	/** Size of entire packet in bytes.       */
	uint16_t	size;

	/** The size of the first entry's data in bytes */
	uint16_t	car_size;

	/**
	 * Either NETTXF_ or NETRXF_ flags.  Note that the flag values are
	 * not the same for TX and RX packets
	 */
	uint16_t	flags;

	/**
	 * The number of valid data-bearing entries (either netif_tx_request's
	 * or netif_rx_response's) in the packet.  If this is 0, it means the
	 * entire packet is invalid.
	 */
	uint16_t	list_len;

	/** There was an error processing the packet */
	uint8_t		error;
};

/** xnb_pkt method: initialize it */
static inline void
xnb_pkt_initialize(struct xnb_pkt *pxnb)
{
	bzero(pxnb, sizeof(*pxnb));
}

/** xnb_pkt method: mark the packet as valid */
static inline void
xnb_pkt_validate(struct xnb_pkt *pxnb)
{
	pxnb->error = 0;
};

/** xnb_pkt method: mark the packet as invalid */
static inline void
xnb_pkt_invalidate(struct xnb_pkt *pxnb)
{
	pxnb->error = 1;
};

/** xnb_pkt method: Check whether the packet is valid */
static inline int
xnb_pkt_is_valid(const struct xnb_pkt *pxnb)
{
	return (! pxnb->error);
}

#ifdef XNB_DEBUG
/** xnb_pkt method: print the packet's contents in human-readable format*/
static void __unused
xnb_dump_pkt(const struct xnb_pkt *pkt) {
	if (pkt == NULL) {
	  DPRINTF("Was passed a null pointer.\n");
	  return;
	}
	DPRINTF("pkt address= %p\n", pkt);
	DPRINTF("pkt->size=%d\n", pkt->size);
	DPRINTF("pkt->car_size=%d\n", pkt->car_size);
	DPRINTF("pkt->flags=0x%04x\n", pkt->flags);
	DPRINTF("pkt->list_len=%d\n", pkt->list_len);
	/* DPRINTF("pkt->extra");	TODO */
	DPRINTF("pkt->car=%d\n", pkt->car);
	DPRINTF("pkt->cdr=%d\n", pkt->cdr);
	DPRINTF("pkt->error=%d\n", pkt->error);
}
#endif /* XNB_DEBUG */

static void
xnb_dump_txreq(RING_IDX idx, const struct netif_tx_request *txreq)
{
	if (txreq != NULL) {
		DPRINTF("netif_tx_request index =%u\n", idx);
		DPRINTF("netif_tx_request.gref  =%u\n", txreq->gref);
		DPRINTF("netif_tx_request.offset=%hu\n", txreq->offset);
		DPRINTF("netif_tx_request.flags =%hu\n", txreq->flags);
		DPRINTF("netif_tx_request.id    =%hu\n", txreq->id);
		DPRINTF("netif_tx_request.size  =%hu\n", txreq->size);
	}
}


/**
 * \brief Configuration data for a shared memory request ring
 *        used to communicate with the front-end client of this
 *        this driver.
 */
struct xnb_ring_config {
	/**
	 * Runtime structures for ring access.  Unfortunately, TX and RX rings
	 * use different data structures, and that cannot be changed since it
	 * is part of the interdomain protocol.
	 */
	union{
		netif_rx_back_ring_t	  rx_ring;
		netif_tx_back_ring_t	  tx_ring;
	} back_ring;

	/**
	 * The device bus address returned by the hypervisor when
	 * mapping the ring and required to unmap it when a connection
	 * is torn down.
	 */
	uint64_t	bus_addr;

	/** The pseudo-physical address where ring memory is mapped.*/
	uint64_t	gnt_addr;

	/** KVA address where ring memory is mapped. */
	vm_offset_t	va;

	/**
	 * Grant table handles, one per-ring page, returned by the
	 * hyperpervisor upon mapping of the ring and required to
	 * unmap it when a connection is torn down.
	 */
	grant_handle_t	handle;

	/** The number of ring pages mapped for the current connection. */
	unsigned	ring_pages;

	/**
	 * The grant references, one per-ring page, supplied by the
	 * front-end, allowing us to reference the ring pages in the
	 * front-end's domain and to map these pages into our own domain.
	 */
	grant_ref_t	ring_ref;
};

/**
 * Per-instance connection state flags.
 */
typedef enum
{
	/** Communication with the front-end has been established. */
	XNBF_RING_CONNECTED    = 0x01,

	/**
	 * Front-end requests exist in the ring and are waiting for
	 * xnb_xen_req objects to free up.
	 */
	XNBF_RESOURCE_SHORTAGE = 0x02,

	/** Connection teardown has started. */
	XNBF_SHUTDOWN          = 0x04,

	/** A thread is already performing shutdown processing. */
	XNBF_IN_SHUTDOWN       = 0x08
} xnb_flag_t;

/**
 * Types of rings.  Used for array indices and to identify a ring's control
 * data structure type
 */
typedef enum{
	XNB_RING_TYPE_TX = 0,	/* ID of TX rings, used for array indices */
	XNB_RING_TYPE_RX = 1,	/* ID of RX rings, used for array indices */
	XNB_NUM_RING_TYPES
} xnb_ring_type_t;

/**
 * Per-instance configuration data.
 */
struct xnb_softc {
	/** NewBus device corresponding to this instance. */
	device_t		dev;

	/* Media related fields */

	/** Generic network media state */
	struct ifmedia		sc_media;

	/** Media carrier info */
	struct ifnet 		*xnb_ifp;

	/** Our own private carrier state */
	unsigned carrier;

	/** Device MAC Address */
	uint8_t			mac[ETHER_ADDR_LEN];

	/* Xen related fields */

	/**
	 * \brief The netif protocol abi in effect.
	 *
	 * There are situations where the back and front ends can
	 * have a different, native abi (e.g. intel x86_64 and
	 * 32bit x86 domains on the same machine).  The back-end
	 * always accommodates the front-end's native abi.  That
	 * value is pulled from the XenStore and recorded here.
	 */
	int			abi;

	/**
	 * Name of the bridge to which this VIF is connected, if any
	 * This field is dynamically allocated by xenbus and must be free()ed
	 * when no longer needed
	 */
	char			*bridge;

	/** The interrupt driven even channel used to signal ring events. */
	evtchn_port_t		evtchn;

	/** Xen device handle.*/
	long 			handle;

	/** Handle to the communication ring event channel. */
	xen_intr_handle_t	xen_intr_handle;

	/**
	 * \brief Cached value of the front-end's domain id.
	 *
	 * This value is used at once for each mapped page in
	 * a transaction.  We cache it to avoid incuring the
	 * cost of an ivar access every time this is needed.
	 */
	domid_t			otherend_id;

	/**
	 * Undocumented frontend feature.  Has something to do with
	 * scatter/gather IO
	 */
	uint8_t			can_sg;
	/** Undocumented frontend feature */
	uint8_t			gso;
	/** Undocumented frontend feature */
	uint8_t			gso_prefix;
	/** Can checksum TCP/UDP over IPv4 */
	uint8_t			ip_csum;

	/* Implementation related fields */
	/**
	 * Preallocated grant table copy descriptor for RX operations.
	 * Access must be protected by rx_lock
	 */
	gnttab_copy_table	rx_gnttab;

	/**
	 * Preallocated grant table copy descriptor for TX operations.
	 * Access must be protected by tx_lock
	 */
	gnttab_copy_table	tx_gnttab;

	/**
	 * Resource representing allocated physical address space
	 * associated with our per-instance kva region.
	 */
	struct resource		*pseudo_phys_res;

	/** Resource id for allocated physical address space. */
	int			pseudo_phys_res_id;

	/** Ring mapping and interrupt configuration data. */
	struct xnb_ring_config	ring_configs[XNB_NUM_RING_TYPES];

	/**
	 * Global pool of kva used for mapping remote domain ring
	 * and I/O transaction data.
	 */
	vm_offset_t		kva;

	/** Pseudo-physical address corresponding to kva. */
	uint64_t		gnt_base_addr;

	/** Various configuration and state bit flags. */
	xnb_flag_t		flags;

	/** Mutex protecting per-instance data in the receive path. */
	struct mtx		rx_lock;

	/** Mutex protecting per-instance data in the softc structure. */
	struct mtx		sc_lock;

	/** Mutex protecting per-instance data in the transmit path. */
	struct mtx		tx_lock;

	/** The size of the global kva pool. */
	int			kva_size;

	/** Name of the interface */
	char			 if_name[IFNAMSIZ];
};

/*---------------------------- Debugging functions ---------------------------*/
#ifdef XNB_DEBUG
static void __unused
xnb_dump_gnttab_copy(const struct gnttab_copy *entry)
{
	if (entry == NULL) {
		printf("NULL grant table pointer\n");
		return;
	}

	if (entry->flags & GNTCOPY_dest_gref)
		printf("gnttab dest ref=\t%u\n", entry->dest.u.ref);
	else
		printf("gnttab dest gmfn=\t%"PRI_xen_pfn"\n",
		       entry->dest.u.gmfn);
	printf("gnttab dest offset=\t%hu\n", entry->dest.offset);
	printf("gnttab dest domid=\t%hu\n", entry->dest.domid);
	if (entry->flags & GNTCOPY_source_gref)
		printf("gnttab source ref=\t%u\n", entry->source.u.ref);
	else
		printf("gnttab source gmfn=\t%"PRI_xen_pfn"\n",
		       entry->source.u.gmfn);
	printf("gnttab source offset=\t%hu\n", entry->source.offset);
	printf("gnttab source domid=\t%hu\n", entry->source.domid);
	printf("gnttab len=\t%hu\n", entry->len);
	printf("gnttab flags=\t%hu\n", entry->flags);
	printf("gnttab status=\t%hd\n", entry->status);
}

static int
xnb_dump_rings(SYSCTL_HANDLER_ARGS)
{
	static char results[720];
	struct xnb_softc const* xnb = (struct xnb_softc*)arg1;
	netif_rx_back_ring_t const* rxb =
		&xnb->ring_configs[XNB_RING_TYPE_RX].back_ring.rx_ring;
	netif_tx_back_ring_t const* txb =
		&xnb->ring_configs[XNB_RING_TYPE_TX].back_ring.tx_ring;

	/* empty the result strings */
	results[0] = 0;

	if ( !txb || !txb->sring || !rxb || !rxb->sring )
		return (SYSCTL_OUT(req, results, strnlen(results, 720)));

	snprintf(results, 720,
	    "\n\t%35s %18s\n"	/* TX, RX */
	    "\t%16s %18d %18d\n"	/* req_cons */
	    "\t%16s %18d %18d\n"	/* nr_ents */
	    "\t%16s %18d %18d\n"	/* rsp_prod_pvt */
	    "\t%16s %18p %18p\n"	/* sring */
	    "\t%16s %18d %18d\n"	/* req_prod */
	    "\t%16s %18d %18d\n"	/* req_event */
	    "\t%16s %18d %18d\n"	/* rsp_prod */
	    "\t%16s %18d %18d\n",	/* rsp_event */
	    "TX", "RX",
	    "req_cons", txb->req_cons, rxb->req_cons,
	    "nr_ents", txb->nr_ents, rxb->nr_ents,
	    "rsp_prod_pvt", txb->rsp_prod_pvt, rxb->rsp_prod_pvt,
	    "sring", txb->sring, rxb->sring,
	    "sring->req_prod", txb->sring->req_prod, rxb->sring->req_prod,
	    "sring->req_event", txb->sring->req_event, rxb->sring->req_event,
	    "sring->rsp_prod", txb->sring->rsp_prod, rxb->sring->rsp_prod,
	    "sring->rsp_event", txb->sring->rsp_event, rxb->sring->rsp_event);

	return (SYSCTL_OUT(req, results, strnlen(results, 720)));
}

static void __unused
xnb_dump_mbuf(const struct mbuf *m)
{
	int len;
	uint8_t *d;
	if (m == NULL)
		return;

	printf("xnb_dump_mbuf:\n");
	if (m->m_flags & M_PKTHDR) {
		printf("    flowid=%10d, csum_flags=%#8x, csum_data=%#8x, "
		       "tso_segsz=%5hd\n",
		       m->m_pkthdr.flowid, (int)m->m_pkthdr.csum_flags,
		       m->m_pkthdr.csum_data, m->m_pkthdr.tso_segsz);
		printf("    rcvif=%16p,  len=%19d\n",
		       m->m_pkthdr.rcvif, m->m_pkthdr.len);
	}
	printf("    m_next=%16p, m_nextpk=%16p, m_data=%16p\n",
	       m->m_next, m->m_nextpkt, m->m_data);
	printf("    m_len=%17d, m_flags=%#15x, m_type=%18u\n",
	       m->m_len, m->m_flags, m->m_type);

	len = m->m_len;
	d = mtod(m, uint8_t*);
	while (len > 0) {
		int i;
		printf("                ");
		for (i = 0; (i < 16) && (len > 0); i++, len--) {
			printf("%02hhx ", *(d++));
		}
		printf("\n");
	}
}
#endif /* XNB_DEBUG */

/*------------------------ Inter-Domain Communication ------------------------*/
/**
 * Free dynamically allocated KVA or pseudo-physical address allocations.
 *
 * \param xnb  Per-instance xnb configuration structure.
 */
static void
xnb_free_communication_mem(struct xnb_softc *xnb)
{
	if (xnb->kva != 0) {
		if (xnb->pseudo_phys_res != NULL) {
			xenmem_free(xnb->dev, xnb->pseudo_phys_res_id,
			    xnb->pseudo_phys_res);
			xnb->pseudo_phys_res = NULL;
		}
	}
	xnb->kva = 0;
	xnb->gnt_base_addr = 0;
}

/**
 * Cleanup all inter-domain communication mechanisms.
 *
 * \param xnb  Per-instance xnb configuration structure.
 */
static int
xnb_disconnect(struct xnb_softc *xnb)
{
	struct gnttab_unmap_grant_ref gnts[XNB_NUM_RING_TYPES];
	int error;
	int i;

	if (xnb->xen_intr_handle != NULL)
		xen_intr_unbind(&xnb->xen_intr_handle);

	/*
	 * We may still have another thread currently processing requests.  We
	 * must acquire the rx and tx locks to make sure those threads are done,
	 * but we can release those locks as soon as we acquire them, because no
	 * more interrupts will be arriving.
	 */
	mtx_lock(&xnb->tx_lock);
	mtx_unlock(&xnb->tx_lock);
	mtx_lock(&xnb->rx_lock);
	mtx_unlock(&xnb->rx_lock);

	mtx_lock(&xnb->sc_lock);
	/* Free malloc'd softc member variables */
	if (xnb->bridge != NULL) {
		free(xnb->bridge, M_XENSTORE);
		xnb->bridge = NULL;
	}

	/* All request processing has stopped, so unmap the rings */
	for (i=0; i < XNB_NUM_RING_TYPES; i++) {
		gnts[i].host_addr = xnb->ring_configs[i].gnt_addr;
		gnts[i].dev_bus_addr = xnb->ring_configs[i].bus_addr;
		gnts[i].handle = xnb->ring_configs[i].handle;
	}
	error = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, gnts,
					  XNB_NUM_RING_TYPES);
	KASSERT(error == 0, ("Grant table unmap op failed (%d)", error));

	xnb_free_communication_mem(xnb);
	/*
	 * Zero the ring config structs because the pointers, handles, and
	 * grant refs contained therein are no longer valid.
	 */
	bzero(&xnb->ring_configs[XNB_RING_TYPE_TX],
	    sizeof(struct xnb_ring_config));
	bzero(&xnb->ring_configs[XNB_RING_TYPE_RX],
	    sizeof(struct xnb_ring_config));

	xnb->flags &= ~XNBF_RING_CONNECTED;
	mtx_unlock(&xnb->sc_lock);

	return (0);
}

/**
 * Map a single shared memory ring into domain local address space and
 * initialize its control structure
 *
 * \param xnb	Per-instance xnb configuration structure
 * \param ring_type	Array index of this ring in the xnb's array of rings
 * \return 	An errno
 */
static int
xnb_connect_ring(struct xnb_softc *xnb, xnb_ring_type_t ring_type)
{
	struct gnttab_map_grant_ref gnt;
	struct xnb_ring_config *ring = &xnb->ring_configs[ring_type];
	int error;

	/* TX ring type = 0, RX =1 */
	ring->va = xnb->kva + ring_type * PAGE_SIZE;
	ring->gnt_addr = xnb->gnt_base_addr + ring_type * PAGE_SIZE;

	gnt.host_addr = ring->gnt_addr;
	gnt.flags     = GNTMAP_host_map;
	gnt.ref       = ring->ring_ref;
	gnt.dom       = xnb->otherend_id;

	error = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &gnt, 1);
	if (error != 0)
		panic("netback: Ring page grant table op failed (%d)", error);

	if (gnt.status != 0) {
		ring->va = 0;
		error = EACCES;
		xenbus_dev_fatal(xnb->dev, error,
				 "Ring shared page mapping failed. "
				 "Status %d.", gnt.status);
	} else {
		ring->handle = gnt.handle;
		ring->bus_addr = gnt.dev_bus_addr;

		if (ring_type == XNB_RING_TYPE_TX) {
			BACK_RING_INIT(&ring->back_ring.tx_ring,
			    (netif_tx_sring_t*)ring->va,
			    ring->ring_pages * PAGE_SIZE);
		} else if (ring_type == XNB_RING_TYPE_RX) {
			BACK_RING_INIT(&ring->back_ring.rx_ring,
			    (netif_rx_sring_t*)ring->va,
			    ring->ring_pages * PAGE_SIZE);
		} else {
			xenbus_dev_fatal(xnb->dev, error,
				 "Unknown ring type %d", ring_type);
		}
	}

	return error;
}

/**
 * Setup the shared memory rings and bind an interrupt to the event channel
 * used to notify us of ring changes.
 *
 * \param xnb  Per-instance xnb configuration structure.
 */
static int
xnb_connect_comms(struct xnb_softc *xnb)
{
	int	error;
	xnb_ring_type_t i;

	if ((xnb->flags & XNBF_RING_CONNECTED) != 0)
		return (0);

	/*
	 * Kva for our rings are at the tail of the region of kva allocated
	 * by xnb_alloc_communication_mem().
	 */
	for (i=0; i < XNB_NUM_RING_TYPES; i++) {
		error = xnb_connect_ring(xnb, i);
		if (error != 0)
	  		return error;
	}

	xnb->flags |= XNBF_RING_CONNECTED;

	error = xen_intr_bind_remote_port(xnb->dev,
					  xnb->otherend_id,
					  xnb->evtchn,
					  /*filter*/NULL,
					  xnb_intr, /*arg*/xnb,
					  INTR_TYPE_BIO | INTR_MPSAFE,
					  &xnb->xen_intr_handle);
	if (error != 0) {
		(void)xnb_disconnect(xnb);
		xenbus_dev_fatal(xnb->dev, error, "binding event channel");
		return (error);
	}

	DPRINTF("rings connected!\n");

	return (0);
}

/**
 * Size KVA and pseudo-physical address allocations based on negotiated
 * values for the size and number of I/O requests, and the size of our
 * communication ring.
 *
 * \param xnb  Per-instance xnb configuration structure.
 *
 * These address spaces are used to dynamically map pages in the
 * front-end's domain into our own.
 */
static int
xnb_alloc_communication_mem(struct xnb_softc *xnb)
{
	xnb_ring_type_t i;

	xnb->kva_size = 0;
	for (i=0; i < XNB_NUM_RING_TYPES; i++) {
		xnb->kva_size += xnb->ring_configs[i].ring_pages * PAGE_SIZE;
	}

	/*
	 * Reserve a range of pseudo physical memory that we can map
	 * into kva.  These pages will only be backed by machine
	 * pages ("real memory") during the lifetime of front-end requests
	 * via grant table operations.  We will map the netif tx and rx rings
	 * into this space.
	 */
	xnb->pseudo_phys_res_id = 0;
	xnb->pseudo_phys_res = xenmem_alloc(xnb->dev, &xnb->pseudo_phys_res_id,
	    xnb->kva_size);
	if (xnb->pseudo_phys_res == NULL) {
		xnb->kva = 0;
		return (ENOMEM);
	}
	xnb->kva = (vm_offset_t)rman_get_virtual(xnb->pseudo_phys_res);
	xnb->gnt_base_addr = rman_get_start(xnb->pseudo_phys_res);
	return (0);
}

/**
 * Collect information from the XenStore related to our device and its frontend
 *
 * \param xnb  Per-instance xnb configuration structure.
 */
static int
xnb_collect_xenstore_info(struct xnb_softc *xnb)
{
	/**
	 * \todo Linux collects the following info.  We should collect most
	 * of this, too:
	 * "feature-rx-notify"
	 */
	const char *otherend_path;
	const char *our_path;
	int err;
	unsigned int rx_copy, bridge_len;
	uint8_t no_csum_offload;

	otherend_path = xenbus_get_otherend_path(xnb->dev);
	our_path = xenbus_get_node(xnb->dev);

	/* Collect the critical communication parameters */
	err = xs_gather(XST_NIL, otherend_path,
	    "tx-ring-ref", "%l" PRIu32,
	    	&xnb->ring_configs[XNB_RING_TYPE_TX].ring_ref,
	    "rx-ring-ref", "%l" PRIu32,
	    	&xnb->ring_configs[XNB_RING_TYPE_RX].ring_ref,
	    "event-channel", "%" PRIu32, &xnb->evtchn,
	    NULL);
	if (err != 0) {
		xenbus_dev_fatal(xnb->dev, err,
				 "Unable to retrieve ring information from "
				 "frontend %s.  Unable to connect.",
				 otherend_path);
		return (err);
	}

	/* Collect the handle from xenstore */
	err = xs_scanf(XST_NIL, our_path, "handle", NULL, "%li", &xnb->handle);
	if (err != 0) {
		xenbus_dev_fatal(xnb->dev, err,
		    "Error reading handle from frontend %s.  "
		    "Unable to connect.", otherend_path);
	}

	/*
	 * Collect the bridgename, if any.  We do not need bridge_len; we just
	 * throw it away
	 */
	err = xs_read(XST_NIL, our_path, "bridge", &bridge_len,
		      (void**)&xnb->bridge);
	if (err != 0)
		xnb->bridge = NULL;

	/*
	 * Does the frontend request that we use rx copy?  If not, return an
	 * error because this driver only supports rx copy.
	 */
	err = xs_scanf(XST_NIL, otherend_path, "request-rx-copy", NULL,
		       "%" PRIu32, &rx_copy);
	if (err == ENOENT) {
		err = 0;
	 	rx_copy = 0;
	}
	if (err < 0) {
		xenbus_dev_fatal(xnb->dev, err, "reading %s/request-rx-copy",
				 otherend_path);
		return err;
	}
	/**
	 * \todo: figure out the exact meaning of this feature, and when
	 * the frontend will set it to true.  It should be set to true
	 * at some point
	 */
/*        if (!rx_copy)*/
/*          return EOPNOTSUPP;*/

	/** \todo Collect the rx notify feature */

	/*  Collect the feature-sg. */
	if (xs_scanf(XST_NIL, otherend_path, "feature-sg", NULL,
		     "%hhu", &xnb->can_sg) < 0)
		xnb->can_sg = 0;

	/* Collect remaining frontend features */
	if (xs_scanf(XST_NIL, otherend_path, "feature-gso-tcpv4", NULL,
		     "%hhu", &xnb->gso) < 0)
		xnb->gso = 0;

	if (xs_scanf(XST_NIL, otherend_path, "feature-gso-tcpv4-prefix", NULL,
		     "%hhu", &xnb->gso_prefix) < 0)
		xnb->gso_prefix = 0;

	if (xs_scanf(XST_NIL, otherend_path, "feature-no-csum-offload", NULL,
		     "%hhu", &no_csum_offload) < 0)
		no_csum_offload = 0;
	xnb->ip_csum = (no_csum_offload == 0);

	return (0);
}

/**
 * Supply information about the physical device to the frontend
 * via XenBus.
 *
 * \param xnb  Per-instance xnb configuration structure.
 */
static int
xnb_publish_backend_info(struct xnb_softc *xnb)
{
	struct xs_transaction xst;
	const char *our_path;
	int error;

	our_path = xenbus_get_node(xnb->dev);

	do {
		error = xs_transaction_start(&xst);
		if (error != 0) {
			xenbus_dev_fatal(xnb->dev, error,
					 "Error publishing backend info "
					 "(start transaction)");
			break;
		}

		error = xs_printf(xst, our_path, "feature-sg",
				  "%d", XNB_SG);
		if (error != 0)
			break;

		error = xs_printf(xst, our_path, "feature-gso-tcpv4",
				  "%d", XNB_GSO_TCPV4);
		if (error != 0)
			break;

		error = xs_printf(xst, our_path, "feature-rx-copy",
				  "%d", XNB_RX_COPY);
		if (error != 0)
			break;

		error = xs_printf(xst, our_path, "feature-rx-flip",
				  "%d", XNB_RX_FLIP);
		if (error != 0)
			break;

		error = xs_transaction_end(xst, 0);
		if (error != 0 && error != EAGAIN) {
			xenbus_dev_fatal(xnb->dev, error, "ending transaction");
			break;
		}

	} while (error == EAGAIN);

	return (error);
}

/**
 * Connect to our netfront peer now that it has completed publishing
 * its configuration into the XenStore.
 *
 * \param xnb  Per-instance xnb configuration structure.
 */
static void
xnb_connect(struct xnb_softc *xnb)
{
	int	error;

	if (xenbus_get_state(xnb->dev) == XenbusStateConnected)
		return;

	if (xnb_collect_xenstore_info(xnb) != 0)
		return;

	xnb->flags &= ~XNBF_SHUTDOWN;

	/* Read front end configuration. */

	/* Allocate resources whose size depends on front-end configuration. */
	error = xnb_alloc_communication_mem(xnb);
	if (error != 0) {
		xenbus_dev_fatal(xnb->dev, error,
				 "Unable to allocate communication memory");
		return;
	}

	/*
	 * Connect communication channel.
	 */
	error = xnb_connect_comms(xnb);
	if (error != 0) {
		/* Specific errors are reported by xnb_connect_comms(). */
		return;
	}
	xnb->carrier = 1;

	/* Ready for I/O. */
	xenbus_set_state(xnb->dev, XenbusStateConnected);
}

/*-------------------------- Device Teardown Support -------------------------*/
/**
 * Perform device shutdown functions.
 *
 * \param xnb  Per-instance xnb configuration structure.
 *
 * Mark this instance as shutting down, wait for any active requests
 * to drain, disconnect from the front-end, and notify any waiters (e.g.
 * a thread invoking our detach method) that detach can now proceed.
 */
static int
xnb_shutdown(struct xnb_softc *xnb)
{
	/*
	 * Due to the need to drop our mutex during some
	 * xenbus operations, it is possible for two threads
	 * to attempt to close out shutdown processing at
	 * the same time.  Tell the caller that hits this
	 * race to try back later.
	 */
	if ((xnb->flags & XNBF_IN_SHUTDOWN) != 0)
		return (EAGAIN);

	xnb->flags |= XNBF_SHUTDOWN;

	xnb->flags |= XNBF_IN_SHUTDOWN;

	mtx_unlock(&xnb->sc_lock);
	/* Free the network interface */
	xnb->carrier = 0;
	if (xnb->xnb_ifp != NULL) {
		ether_ifdetach(xnb->xnb_ifp);
		if_free(xnb->xnb_ifp);
		xnb->xnb_ifp = NULL;
	}

	xnb_disconnect(xnb);

	if (xenbus_get_state(xnb->dev) < XenbusStateClosing)
		xenbus_set_state(xnb->dev, XenbusStateClosing);
	mtx_lock(&xnb->sc_lock);

	xnb->flags &= ~XNBF_IN_SHUTDOWN;

	/* Indicate to xnb_detach() that is it safe to proceed. */
	wakeup(xnb);

	return (0);
}

/**
 * Report an attach time error to the console and Xen, and cleanup
 * this instance by forcing immediate detach processing.
 *
 * \param xnb  Per-instance xnb configuration structure.
 * \param err  Errno describing the error.
 * \param fmt  Printf style format and arguments
 */
static void
xnb_attach_failed(struct xnb_softc *xnb, int err, const char *fmt, ...)
{
	va_list ap;
	va_list ap_hotplug;

	va_start(ap, fmt);
	va_copy(ap_hotplug, ap);
	xs_vprintf(XST_NIL, xenbus_get_node(xnb->dev),
		  "hotplug-error", fmt, ap_hotplug);
	va_end(ap_hotplug);
	(void)xs_printf(XST_NIL, xenbus_get_node(xnb->dev),
		  "hotplug-status", "error");

	xenbus_dev_vfatal(xnb->dev, err, fmt, ap);
	va_end(ap);

	(void)xs_printf(XST_NIL, xenbus_get_node(xnb->dev), "online", "0");
	xnb_detach(xnb->dev);
}

/*---------------------------- NewBus Entrypoints ----------------------------*/
/**
 * Inspect a XenBus device and claim it if is of the appropriate type.
 *
 * \param dev  NewBus device object representing a candidate XenBus device.
 *
 * \return  0 for success, errno codes for failure.
 */
static int
xnb_probe(device_t dev)
{
	 if (!strcmp(xenbus_get_type(dev), "vif")) {
		DPRINTF("Claiming device %d, %s\n", device_get_unit(dev),
		    devclass_get_name(device_get_devclass(dev)));
		device_set_desc(dev, "Backend Virtual Network Device");
		device_quiet(dev);
		return (0);
	}
	return (ENXIO);
}

/**
 * Setup sysctl variables to control various Network Back parameters.
 *
 * \param xnb  Xen Net Back softc.
 *
 */
static void
xnb_setup_sysctl(struct xnb_softc *xnb)
{
	struct sysctl_ctx_list *sysctl_ctx = NULL;
	struct sysctl_oid      *sysctl_tree = NULL;

	sysctl_ctx = device_get_sysctl_ctx(xnb->dev);
	if (sysctl_ctx == NULL)
		return;

	sysctl_tree = device_get_sysctl_tree(xnb->dev);
	if (sysctl_tree == NULL)
		return;

#ifdef XNB_DEBUG
	SYSCTL_ADD_PROC(sysctl_ctx,
			SYSCTL_CHILDREN(sysctl_tree),
			OID_AUTO,
			"unit_test_results",
			CTLTYPE_STRING | CTLFLAG_RD,
			xnb,
			0,
			xnb_unit_test_main,
			"A",
			"Results of builtin unit tests");

	SYSCTL_ADD_PROC(sysctl_ctx,
			SYSCTL_CHILDREN(sysctl_tree),
			OID_AUTO,
			"dump_rings",
			CTLTYPE_STRING | CTLFLAG_RD,
			xnb,
			0,
			xnb_dump_rings,
			"A",
			"Xennet Back Rings");
#endif /* XNB_DEBUG */
}

/**
 * Create a network device.
 * @param handle device handle
 */
int
create_netdev(device_t dev)
{
	struct ifnet *ifp;
	struct xnb_softc *xnb;
	int err = 0;
	uint32_t handle;

	xnb = device_get_softc(dev);
	mtx_init(&xnb->sc_lock, "xnb_softc", "xen netback softc lock", MTX_DEF);
	mtx_init(&xnb->tx_lock, "xnb_tx", "xen netback tx lock", MTX_DEF);
	mtx_init(&xnb->rx_lock, "xnb_rx", "xen netback rx lock", MTX_DEF);

	xnb->dev = dev;

	ifmedia_init(&xnb->sc_media, 0, xnb_ifmedia_upd, xnb_ifmedia_sts);
	ifmedia_add(&xnb->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
	ifmedia_set(&xnb->sc_media, IFM_ETHER|IFM_MANUAL);

	/*
	 * Set the MAC address to a dummy value (00:00:00:00:00),
	 * if the MAC address of the host-facing interface is set
	 * to the same as the guest-facing one (the value found in
	 * xenstore), the bridge would stop delivering packets to
	 * us because it would see that the destination address of
	 * the packet is the same as the interface, and so the bridge
	 * would expect the packet has already been delivered locally
	 * (and just drop it).
	 */
	bzero(&xnb->mac[0], sizeof(xnb->mac));

	/* The interface will be named using the following nomenclature:
	 *
	 * xnb<domid>.<handle>
	 *
	 * Where handle is the oder of the interface referred to the guest.
	 */
	err = xs_scanf(XST_NIL, xenbus_get_node(xnb->dev), "handle", NULL,
		       "%" PRIu32, &handle);
	if (err != 0)
		return (err);
	snprintf(xnb->if_name, IFNAMSIZ, "xnb%" PRIu16 ".%" PRIu32,
	    xenbus_get_otherend_id(dev), handle);

	if (err == 0) {
		/* Set up ifnet structure */
		ifp = xnb->xnb_ifp = if_alloc(IFT_ETHER);
		ifp->if_softc = xnb;
		if_initname(ifp, xnb->if_name,  IF_DUNIT_NONE);
		ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
		ifp->if_ioctl = xnb_ioctl;
		ifp->if_start = xnb_start;
		ifp->if_init = xnb_ifinit;
		ifp->if_mtu = ETHERMTU;
		ifp->if_snd.ifq_maxlen = NET_RX_RING_SIZE - 1;

		ifp->if_hwassist = XNB_CSUM_FEATURES;
		ifp->if_capabilities = IFCAP_HWCSUM;
		ifp->if_capenable = IFCAP_HWCSUM;

		ether_ifattach(ifp, xnb->mac);
		xnb->carrier = 0;
	}

	return err;
}

/**
 * Attach to a XenBus device that has been claimed by our probe routine.
 *
 * \param dev  NewBus device object representing this Xen Net Back instance.
 *
 * \return  0 for success, errno codes for failure.
 */
static int
xnb_attach(device_t dev)
{
	struct xnb_softc *xnb;
	int	error;
	xnb_ring_type_t	i;

	error = create_netdev(dev);
	if (error != 0) {
		xenbus_dev_fatal(dev, error, "creating netdev");
		return (error);
	}

	DPRINTF("Attaching to %s\n", xenbus_get_node(dev));

	/*
	 * Basic initialization.
	 * After this block it is safe to call xnb_detach()
	 * to clean up any allocated data for this instance.
	 */
	xnb = device_get_softc(dev);
	xnb->otherend_id = xenbus_get_otherend_id(dev);
	for (i=0; i < XNB_NUM_RING_TYPES; i++) {
		xnb->ring_configs[i].ring_pages = 1;
	}

	/*
	 * Setup sysctl variables.
	 */
	xnb_setup_sysctl(xnb);

	/* Update hot-plug status to satisfy xend. */
	error = xs_printf(XST_NIL, xenbus_get_node(xnb->dev),
			  "hotplug-status", "connected");
	if (error != 0) {
		xnb_attach_failed(xnb, error, "writing %s/hotplug-status",
				  xenbus_get_node(xnb->dev));
		return (error);
	}

	if ((error = xnb_publish_backend_info(xnb)) != 0) {
		/*
		 * If we can't publish our data, we cannot participate
		 * in this connection, and waiting for a front-end state
		 * change will not help the situation.
		 */
		xnb_attach_failed(xnb, error,
		    "Publishing backend status for %s",
				  xenbus_get_node(xnb->dev));
		return error;
	}

	/* Tell the front end that we are ready to connect. */
	xenbus_set_state(dev, XenbusStateInitWait);

	return (0);
}

/**
 * Detach from a net back device instance.
 *
 * \param dev  NewBus device object representing this Xen Net Back instance.
 *
 * \return  0 for success, errno codes for failure.
 *
 * \note A net back device may be detached at any time in its life-cycle,
 *       including part way through the attach process.  For this reason,
 *       initialization order and the initialization state checks in this
 *       routine must be carefully coupled so that attach time failures
 *       are gracefully handled.
 */
static int
xnb_detach(device_t dev)
{
	struct xnb_softc *xnb;

	DPRINTF("\n");

	xnb = device_get_softc(dev);
	mtx_lock(&xnb->sc_lock);
	while (xnb_shutdown(xnb) == EAGAIN) {
		msleep(xnb, &xnb->sc_lock, /*wakeup prio unchanged*/0,
		       "xnb_shutdown", 0);
	}
	mtx_unlock(&xnb->sc_lock);
	DPRINTF("\n");

	mtx_destroy(&xnb->tx_lock);
	mtx_destroy(&xnb->rx_lock);
	mtx_destroy(&xnb->sc_lock);
	return (0);
}

/**
 * Prepare this net back device for suspension of this VM.
 *
 * \param dev  NewBus device object representing this Xen net Back instance.
 *
 * \return  0 for success, errno codes for failure.
 */
static int
xnb_suspend(device_t dev)
{
	return (0);
}

/**
 * Perform any processing required to recover from a suspended state.
 *
 * \param dev  NewBus device object representing this Xen Net Back instance.
 *
 * \return  0 for success, errno codes for failure.
 */
static int
xnb_resume(device_t dev)
{
	return (0);
}

/**
 * Handle state changes expressed via the XenStore by our front-end peer.
 *
 * \param dev             NewBus device object representing this Xen
 *                        Net Back instance.
 * \param frontend_state  The new state of the front-end.
 *
 * \return  0 for success, errno codes for failure.
 */
static void
xnb_frontend_changed(device_t dev, XenbusState frontend_state)
{
	struct xnb_softc *xnb;

	xnb = device_get_softc(dev);

	DPRINTF("frontend_state=%s, xnb_state=%s\n",
	        xenbus_strstate(frontend_state),
		xenbus_strstate(xenbus_get_state(xnb->dev)));

	switch (frontend_state) {
	case XenbusStateInitialising:
		break;
	case XenbusStateInitialised:
	case XenbusStateConnected:
		xnb_connect(xnb);
		break;
	case XenbusStateClosing:
	case XenbusStateClosed:
		mtx_lock(&xnb->sc_lock);
		xnb_shutdown(xnb);
		mtx_unlock(&xnb->sc_lock);
		if (frontend_state == XenbusStateClosed)
			xenbus_set_state(xnb->dev, XenbusStateClosed);
		break;
	default:
		xenbus_dev_fatal(xnb->dev, EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}


/*---------------------------- Request Processing ----------------------------*/
/**
 * Interrupt handler bound to the shared ring's event channel.
 * Entry point for the xennet transmit path in netback
 * Transfers packets from the Xen ring to the host's generic networking stack
 *
 * \param arg  Callback argument registerd during event channel
 *             binding - the xnb_softc for this instance.
 */
static void
xnb_intr(void *arg)
{
	struct xnb_softc *xnb;
	struct ifnet *ifp;
	netif_tx_back_ring_t *txb;
	RING_IDX req_prod_local;

	xnb = (struct xnb_softc *)arg;
	ifp = xnb->xnb_ifp;
	txb = &xnb->ring_configs[XNB_RING_TYPE_TX].back_ring.tx_ring;

	mtx_lock(&xnb->tx_lock);
	do {
		int notify;
		req_prod_local = txb->sring->req_prod;
		xen_rmb();

		for (;;) {
			struct mbuf *mbufc;
			int err;

			err = xnb_recv(txb, xnb->otherend_id, &mbufc, ifp,
			    	       xnb->tx_gnttab);
			if (err || (mbufc == NULL))
				break;

			/* Send the packet to the generic network stack */
			(*xnb->xnb_ifp->if_input)(xnb->xnb_ifp, mbufc);
		}

		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(txb, notify);
		if (notify != 0)
			xen_intr_signal(xnb->xen_intr_handle);

		txb->sring->req_event = txb->req_cons + 1;
		xen_mb();
	} while (txb->sring->req_prod != req_prod_local) ;
	mtx_unlock(&xnb->tx_lock);

	xnb_start(ifp);
}


/**
 * Build a struct xnb_pkt based on netif_tx_request's from a netif tx ring.
 * Will read exactly 0 or 1 packets from the ring; never a partial packet.
 * \param[out]	pkt	The returned packet.  If there is an error building
 * 			the packet, pkt.list_len will be set to 0.
 * \param[in]	tx_ring	Pointer to the Ring that is the input to this function
 * \param[in]	start	The ring index of the first potential request
 * \return		The number of requests consumed to build this packet
 */
static int
xnb_ring2pkt(struct xnb_pkt *pkt, const netif_tx_back_ring_t *tx_ring,
	     RING_IDX start)
{
	/*
	 * Outline:
	 * 1) Initialize pkt
	 * 2) Read the first request of the packet
	 * 3) Read the extras
	 * 4) Set cdr
	 * 5) Loop on the remainder of the packet
	 * 6) Finalize pkt (stuff like car_size and list_len)
	 */
	int idx = start;
	int discard = 0;	/* whether to discard the packet */
	int more_data = 0;	/* there are more request past the last one */
	uint16_t cdr_size = 0;	/* accumulated size of requests 2 through n */

	xnb_pkt_initialize(pkt);

	/* Read the first request */
	if (RING_HAS_UNCONSUMED_REQUESTS_2(tx_ring, idx)) {
		netif_tx_request_t *tx = RING_GET_REQUEST(tx_ring, idx);
		pkt->size = tx->size;
		pkt->flags = tx->flags & ~NETTXF_more_data;
		more_data = tx->flags & NETTXF_more_data;
		pkt->list_len++;
		pkt->car = idx;
		idx++;
	}

	/* Read the extra info */
	if ((pkt->flags & NETTXF_extra_info) &&
	    RING_HAS_UNCONSUMED_REQUESTS_2(tx_ring, idx)) {
		netif_extra_info_t *ext =
		    (netif_extra_info_t*) RING_GET_REQUEST(tx_ring, idx);
		pkt->extra.type = ext->type;
		switch (pkt->extra.type) {
			case XEN_NETIF_EXTRA_TYPE_GSO:
				pkt->extra.u.gso = ext->u.gso;
				break;
			default:
				/*
				 * The reference Linux netfront driver will
				 * never set any other extra.type.  So we don't
				 * know what to do with it.  Let's print an
				 * error, then consume and discard the packet
				 */
				printf("xnb(%s:%d): Unknown extra info type %d."
				       "  Discarding packet\n",
				       __func__, __LINE__, pkt->extra.type);
				xnb_dump_txreq(start, RING_GET_REQUEST(tx_ring,
				    start));
				xnb_dump_txreq(idx, RING_GET_REQUEST(tx_ring,
				    idx));
				discard = 1;
				break;
		}

		pkt->extra.flags = ext->flags;
		if (ext->flags & XEN_NETIF_EXTRA_FLAG_MORE) {
			/*
			 * The reference linux netfront driver never sets this
			 * flag (nor does any other known netfront).  So we
			 * will discard the packet.
			 */
			printf("xnb(%s:%d): Request sets "
			    "XEN_NETIF_EXTRA_FLAG_MORE, but we can't handle "
			    "that\n", __func__, __LINE__);
			xnb_dump_txreq(start, RING_GET_REQUEST(tx_ring, start));
			xnb_dump_txreq(idx, RING_GET_REQUEST(tx_ring, idx));
			discard = 1;
		}

		idx++;
	}

	/* Set cdr.  If there is not more data, cdr is invalid */
	pkt->cdr = idx;

	/* Loop on remainder of packet */
	while (more_data && RING_HAS_UNCONSUMED_REQUESTS_2(tx_ring, idx)) {
		netif_tx_request_t *tx = RING_GET_REQUEST(tx_ring, idx);
		pkt->list_len++;
		cdr_size += tx->size;
		if (tx->flags & ~NETTXF_more_data) {
			/* There should be no other flags set at this point */
			printf("xnb(%s:%d): Request sets unknown flags %d "
			    "after the 1st request in the packet.\n",
			    __func__, __LINE__, tx->flags);
			xnb_dump_txreq(start, RING_GET_REQUEST(tx_ring, start));
			xnb_dump_txreq(idx, RING_GET_REQUEST(tx_ring, idx));
		}

		more_data = tx->flags & NETTXF_more_data;
		idx++;
	}

	/* Finalize packet */
	if (more_data != 0) {
		/* The ring ran out of requests before finishing the packet */
		xnb_pkt_invalidate(pkt);
		idx = start;	/* tell caller that we consumed no requests */
	} else {
		/* Calculate car_size */
		pkt->car_size = pkt->size - cdr_size;
	}
	if (discard != 0) {
		xnb_pkt_invalidate(pkt);
	}

	return idx - start;
}


/**
 * Respond to all the requests that constituted pkt.  Builds the responses and
 * writes them to the ring, but doesn't push them to the shared ring.
 * \param[in] pkt	the packet that needs a response
 * \param[in] error	true if there was an error handling the packet, such
 * 			as in the hypervisor copy op or mbuf allocation
 * \param[out] ring	Responses go here
 */
static void
xnb_txpkt2rsp(const struct xnb_pkt *pkt, netif_tx_back_ring_t *ring,
	      int error)
{
	/*
	 * Outline:
	 * 1) Respond to the first request
	 * 2) Respond to the extra info reques
	 * Loop through every remaining request in the packet, generating
	 * responses that copy those requests' ids and sets the status
	 * appropriately.
	 */
	netif_tx_request_t *tx;
	netif_tx_response_t *rsp;
	int i;
	uint16_t status;

	status = (xnb_pkt_is_valid(pkt) == 0) || error ?
		NETIF_RSP_ERROR : NETIF_RSP_OKAY;
	KASSERT((pkt->list_len == 0) || (ring->rsp_prod_pvt == pkt->car),
	    ("Cannot respond to ring requests out of order"));

	if (pkt->list_len >= 1) {
		uint16_t id;
		tx = RING_GET_REQUEST(ring, ring->rsp_prod_pvt);
		id = tx->id;
		rsp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
		rsp->id = id;
		rsp->status = status;
		ring->rsp_prod_pvt++;

		if (pkt->flags & NETRXF_extra_info) {
			rsp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
			rsp->status = NETIF_RSP_NULL;
			ring->rsp_prod_pvt++;
		}
	}

	for (i=0; i < pkt->list_len - 1; i++) {
		uint16_t id;
		tx = RING_GET_REQUEST(ring, ring->rsp_prod_pvt);
		id = tx->id;
		rsp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
		rsp->id = id;
		rsp->status = status;
		ring->rsp_prod_pvt++;
	}
}

/**
 * Create an mbuf chain to represent a packet.  Initializes all of the headers
 * in the mbuf chain, but does not copy the data.  The returned chain must be
 * free()'d when no longer needed
 * \param[in]	pkt	A packet to model the mbuf chain after
 * \return	A newly allocated mbuf chain, possibly with clusters attached.
 * 		NULL on failure
 */
static struct mbuf*
xnb_pkt2mbufc(const struct xnb_pkt *pkt, struct ifnet *ifp)
{
	/**
	 * \todo consider using a memory pool for mbufs instead of
	 * reallocating them for every packet
	 */
	/** \todo handle extra data */
	struct mbuf *m;

	m = m_getm(NULL, pkt->size, M_NOWAIT, MT_DATA);

	if (m != NULL) {
		m->m_pkthdr.rcvif = ifp;
		if (pkt->flags & NETTXF_data_validated) {
			/*
			 * We lie to the host OS and always tell it that the
			 * checksums are ok, because the packet is unlikely to
			 * get corrupted going across domains.
			 */
			m->m_pkthdr.csum_flags = (
				CSUM_IP_CHECKED |
				CSUM_IP_VALID   |
				CSUM_DATA_VALID |
				CSUM_PSEUDO_HDR
				);
			m->m_pkthdr.csum_data = 0xffff;
		}
	}
	return m;
}

/**
 * Build a gnttab_copy table that can be used to copy data from a pkt
 * to an mbufc.  Does not actually perform the copy.  Always uses gref's on
 * the packet side.
 * \param[in]	pkt	pkt's associated requests form the src for
 * 			the copy operation
 * \param[in]	mbufc	mbufc's storage forms the dest for the copy operation
 * \param[out]  gnttab	Storage for the returned grant table
 * \param[in]	txb	Pointer to the backend ring structure
 * \param[in]	otherend_id	The domain ID of the other end of the copy
 * \return 		The number of gnttab entries filled
 */
static int
xnb_txpkt2gnttab(const struct xnb_pkt *pkt, struct mbuf *mbufc,
		 gnttab_copy_table gnttab, const netif_tx_back_ring_t *txb,
		 domid_t otherend_id)
{

	struct mbuf *mbuf = mbufc;/* current mbuf within the chain */
	int gnt_idx = 0;		/* index into grant table */
	RING_IDX r_idx = pkt->car;	/* index into tx ring buffer */
	int r_ofs = 0;	/* offset of next data within tx request's data area */
	int m_ofs = 0;	/* offset of next data within mbuf's data area */
	/* size in bytes that still needs to be represented in the table */
	uint16_t size_remaining = pkt->size;

	while (size_remaining > 0) {
		const netif_tx_request_t *txq = RING_GET_REQUEST(txb, r_idx);
		const size_t mbuf_space = M_TRAILINGSPACE(mbuf) - m_ofs;
		const size_t req_size =
			r_idx == pkt->car ? pkt->car_size : txq->size;
		const size_t pkt_space = req_size - r_ofs;
		/*
		 * space is the largest amount of data that can be copied in the
		 * grant table's next entry
		 */
		const size_t space = MIN(pkt_space, mbuf_space);

		/* TODO: handle this error condition without panicking */
		KASSERT(gnt_idx < GNTTAB_LEN, ("Grant table is too short"));

		gnttab[gnt_idx].source.u.ref = txq->gref;
		gnttab[gnt_idx].source.domid = otherend_id;
		gnttab[gnt_idx].source.offset = txq->offset + r_ofs;
		gnttab[gnt_idx].dest.u.gmfn = virt_to_mfn(
		    mtod(mbuf, vm_offset_t) + m_ofs);
		gnttab[gnt_idx].dest.offset = virt_to_offset(
		    mtod(mbuf, vm_offset_t) + m_ofs);
		gnttab[gnt_idx].dest.domid = DOMID_SELF;
		gnttab[gnt_idx].len = space;
		gnttab[gnt_idx].flags = GNTCOPY_source_gref;

		gnt_idx++;
		r_ofs += space;
		m_ofs += space;
		size_remaining -= space;
		if (req_size - r_ofs <= 0) {
			/* Must move to the next tx request */
			r_ofs = 0;
			r_idx = (r_idx == pkt->car) ? pkt->cdr : r_idx + 1;
		}
		if (M_TRAILINGSPACE(mbuf) - m_ofs <= 0) {
			/* Must move to the next mbuf */
			m_ofs = 0;
			mbuf = mbuf->m_next;
		}
	}

	return gnt_idx;
}

/**
 * Check the status of the grant copy operations, and update mbufs various
 * non-data fields to reflect the data present.
 * \param[in,out] mbufc	mbuf chain to update.  The chain must be valid and of
 * 			the correct length, and data should already be present
 * \param[in] gnttab	A grant table for a just completed copy op
 * \param[in] n_entries The number of valid entries in the grant table
 */
static void
xnb_update_mbufc(struct mbuf *mbufc, const gnttab_copy_table gnttab,
    		 int n_entries)
{
	struct mbuf *mbuf = mbufc;
	int i;
	size_t total_size = 0;

	for (i = 0; i < n_entries; i++) {
		KASSERT(gnttab[i].status == GNTST_okay,
		    ("Some gnttab_copy entry had error status %hd\n",
		    gnttab[i].status));

		mbuf->m_len += gnttab[i].len;
		total_size += gnttab[i].len;
		if (M_TRAILINGSPACE(mbuf) <= 0) {
			mbuf = mbuf->m_next;
		}
	}
	mbufc->m_pkthdr.len = total_size;

#if defined(INET) || defined(INET6)
	xnb_add_mbuf_cksum(mbufc);
#endif
}

/**
 * Dequeue at most one packet from the shared ring
 * \param[in,out] txb	Netif tx ring.  A packet will be removed from it, and
 * 			its private indices will be updated.  But the indices
 * 			will not be pushed to the shared ring.
 * \param[in] ifnet	Interface to which the packet will be sent
 * \param[in] otherend	Domain ID of the other end of the ring
 * \param[out] mbufc	The assembled mbuf chain, ready to send to the generic
 * 			networking stack
 * \param[in,out] gnttab Pointer to enough memory for a grant table.  We make
 * 			this a function parameter so that we will take less
 * 			stack space.
 * \return		An error code
 */
static int
xnb_recv(netif_tx_back_ring_t *txb, domid_t otherend, struct mbuf **mbufc,
	 struct ifnet *ifnet, gnttab_copy_table gnttab)
{
	struct xnb_pkt pkt;
	/* number of tx requests consumed to build the last packet */
	int num_consumed;
	int nr_ents;

	*mbufc = NULL;
	num_consumed = xnb_ring2pkt(&pkt, txb, txb->req_cons);
	if (num_consumed == 0)
		return 0;	/* Nothing to receive */

	/* update statistics independent of errors */
	if_inc_counter(ifnet, IFCOUNTER_IPACKETS, 1);

	/*
	 * if we got here, then 1 or more requests was consumed, but the packet
	 * is not necessarily valid.
	 */
	if (xnb_pkt_is_valid(&pkt) == 0) {
		/* got a garbage packet, respond and drop it */
		xnb_txpkt2rsp(&pkt, txb, 1);
		txb->req_cons += num_consumed;
		DPRINTF("xnb_intr: garbage packet, num_consumed=%d\n",
				num_consumed);
		if_inc_counter(ifnet, IFCOUNTER_IERRORS, 1);
		return EINVAL;
	}

	*mbufc = xnb_pkt2mbufc(&pkt, ifnet);

	if (*mbufc == NULL) {
		/*
		 * Couldn't allocate mbufs.  Respond and drop the packet.  Do
		 * not consume the requests
		 */
		xnb_txpkt2rsp(&pkt, txb, 1);
		DPRINTF("xnb_intr: Couldn't allocate mbufs, num_consumed=%d\n",
		    num_consumed);
		if_inc_counter(ifnet, IFCOUNTER_IQDROPS, 1);
		return ENOMEM;
	}

	nr_ents = xnb_txpkt2gnttab(&pkt, *mbufc, gnttab, txb, otherend);

	if (nr_ents > 0) {
		int __unused hv_ret = HYPERVISOR_grant_table_op(GNTTABOP_copy,
		    gnttab, nr_ents);
		KASSERT(hv_ret == 0,
		    ("HYPERVISOR_grant_table_op returned %d\n", hv_ret));
		xnb_update_mbufc(*mbufc, gnttab, nr_ents);
	}

	xnb_txpkt2rsp(&pkt, txb, 0);
	txb->req_cons += num_consumed;
	return 0;
}

/**
 * Create an xnb_pkt based on the contents of an mbuf chain.
 * \param[in] mbufc	mbuf chain to transform into a packet
 * \param[out] pkt	Storage for the newly generated xnb_pkt
 * \param[in] start	The ring index of the first available slot in the rx
 * 			ring
 * \param[in] space	The number of free slots in the rx ring
 * \retval 0		Success
 * \retval EINVAL	mbufc was corrupt or not convertible into a pkt
 * \retval EAGAIN	There was not enough space in the ring to queue the
 * 			packet
 */
static int
xnb_mbufc2pkt(const struct mbuf *mbufc, struct xnb_pkt *pkt,
	      RING_IDX start, int space)
{

	int retval = 0;

	if ((mbufc == NULL) ||
	     ( (mbufc->m_flags & M_PKTHDR) == 0) ||
	     (mbufc->m_pkthdr.len == 0)) {
		xnb_pkt_invalidate(pkt);
		retval = EINVAL;
	} else {
		int slots_required;

		xnb_pkt_validate(pkt);
		pkt->flags = 0;
		pkt->size = mbufc->m_pkthdr.len;
		pkt->car = start;
		pkt->car_size = mbufc->m_len;

		if (mbufc->m_pkthdr.csum_flags & CSUM_TSO) {
			pkt->flags |= NETRXF_extra_info;
			pkt->extra.u.gso.size = mbufc->m_pkthdr.tso_segsz;
			pkt->extra.u.gso.type = XEN_NETIF_GSO_TYPE_TCPV4;
			pkt->extra.u.gso.pad = 0;
			pkt->extra.u.gso.features = 0;
			pkt->extra.type = XEN_NETIF_EXTRA_TYPE_GSO;
			pkt->extra.flags = 0;
			pkt->cdr = start + 2;
		} else {
			pkt->cdr = start + 1;
		}
		if (mbufc->m_pkthdr.csum_flags & (CSUM_TSO | CSUM_DELAY_DATA)) {
			pkt->flags |=
			    (NETRXF_csum_blank | NETRXF_data_validated);
		}

		/*
		 * Each ring response can have up to PAGE_SIZE of data.
		 * Assume that we can defragment the mbuf chain efficiently
		 * into responses so that each response but the last uses all
		 * PAGE_SIZE bytes.
		 */
		pkt->list_len = howmany(pkt->size, PAGE_SIZE);

		if (pkt->list_len > 1) {
			pkt->flags |= NETRXF_more_data;
		}

		slots_required = pkt->list_len +
			(pkt->flags & NETRXF_extra_info ? 1 : 0);
		if (slots_required > space) {
			xnb_pkt_invalidate(pkt);
			retval = EAGAIN;
		}
	}

	return retval;
}

/**
 * Build a gnttab_copy table that can be used to copy data from an mbuf chain
 * to the frontend's shared buffers.  Does not actually perform the copy.
 * Always uses gref's on the other end's side.
 * \param[in]	pkt	pkt's associated responses form the dest for the copy
 * 			operatoin
 * \param[in]	mbufc	The source for the copy operation
 * \param[out]	gnttab	Storage for the returned grant table
 * \param[in]	rxb	Pointer to the backend ring structure
 * \param[in]	otherend_id	The domain ID of the other end of the copy
 * \return 		The number of gnttab entries filled
 */
static int
xnb_rxpkt2gnttab(const struct xnb_pkt *pkt, const struct mbuf *mbufc,
		 gnttab_copy_table gnttab, const netif_rx_back_ring_t *rxb,
		 domid_t otherend_id)
{

	const struct mbuf *mbuf = mbufc;/* current mbuf within the chain */
	int gnt_idx = 0;		/* index into grant table */
	RING_IDX r_idx = pkt->car;	/* index into rx ring buffer */
	int r_ofs = 0;	/* offset of next data within rx request's data area */
	int m_ofs = 0;	/* offset of next data within mbuf's data area */
	/* size in bytes that still needs to be represented in the table */
	uint16_t size_remaining;

	size_remaining = (xnb_pkt_is_valid(pkt) != 0) ? pkt->size : 0;

	while (size_remaining > 0) {
		const netif_rx_request_t *rxq = RING_GET_REQUEST(rxb, r_idx);
		const size_t mbuf_space = mbuf->m_len - m_ofs;
		/* Xen shared pages have an implied size of PAGE_SIZE */
		const size_t req_size = PAGE_SIZE;
		const size_t pkt_space = req_size - r_ofs;
		/*
		 * space is the largest amount of data that can be copied in the
		 * grant table's next entry
		 */
		const size_t space = MIN(pkt_space, mbuf_space);

		/* TODO: handle this error condition without panicing */
		KASSERT(gnt_idx < GNTTAB_LEN, ("Grant table is too short"));

		gnttab[gnt_idx].dest.u.ref = rxq->gref;
		gnttab[gnt_idx].dest.domid = otherend_id;
		gnttab[gnt_idx].dest.offset = r_ofs;
		gnttab[gnt_idx].source.u.gmfn = virt_to_mfn(
		    mtod(mbuf, vm_offset_t) + m_ofs);
		gnttab[gnt_idx].source.offset = virt_to_offset(
		    mtod(mbuf, vm_offset_t) + m_ofs);
		gnttab[gnt_idx].source.domid = DOMID_SELF;
		gnttab[gnt_idx].len = space;
		gnttab[gnt_idx].flags = GNTCOPY_dest_gref;

		gnt_idx++;

		r_ofs += space;
		m_ofs += space;
		size_remaining -= space;
		if (req_size - r_ofs <= 0) {
			/* Must move to the next rx request */
			r_ofs = 0;
			r_idx = (r_idx == pkt->car) ? pkt->cdr : r_idx + 1;
		}
		if (mbuf->m_len - m_ofs <= 0) {
			/* Must move to the next mbuf */
			m_ofs = 0;
			mbuf = mbuf->m_next;
		}
	}

	return gnt_idx;
}

/**
 * Generates responses for all the requests that constituted pkt.  Builds
 * responses and writes them to the ring, but doesn't push the shared ring
 * indices.
 * \param[in] pkt	the packet that needs a response
 * \param[in] gnttab	The grant copy table corresponding to this packet.
 * 			Used to determine how many rsp->netif_rx_response_t's to
 * 			generate.
 * \param[in] n_entries	Number of relevant entries in the grant table
 * \param[out] ring	Responses go here
 * \return		The number of RX requests that were consumed to generate
 * 			the responses
 */
static int
xnb_rxpkt2rsp(const struct xnb_pkt *pkt, const gnttab_copy_table gnttab,
    	      int n_entries, netif_rx_back_ring_t *ring)
{
	/*
	 * This code makes the following assumptions:
	 *	* All entries in gnttab set GNTCOPY_dest_gref
	 *	* The entries in gnttab are grouped by their grefs: any two
	 *	   entries with the same gref must be adjacent
	 */
	int error = 0;
	int gnt_idx, i;
	int n_responses = 0;
	grant_ref_t last_gref = GRANT_REF_INVALID;
	RING_IDX r_idx;

	KASSERT(gnttab != NULL, ("Received a null granttable copy"));

	/*
	 * In the event of an error, we only need to send one response to the
	 * netfront.  In that case, we musn't write any data to the responses
	 * after the one we send.  So we must loop all the way through gnttab
	 * looking for errors before we generate any responses
	 *
	 * Since we're looping through the grant table anyway, we'll count the
	 * number of different gref's in it, which will tell us how many
	 * responses to generate
	 */
	for (gnt_idx = 0; gnt_idx < n_entries; gnt_idx++) {
		int16_t status = gnttab[gnt_idx].status;
		if (status != GNTST_okay) {
			DPRINTF(
			    "Got error %d for hypervisor gnttab_copy status\n",
			    status);
			error = 1;
			break;
		}
		if (gnttab[gnt_idx].dest.u.ref != last_gref) {
			n_responses++;
			last_gref = gnttab[gnt_idx].dest.u.ref;
		}
	}

	if (error != 0) {
		uint16_t id;
		netif_rx_response_t *rsp;
		
		id = RING_GET_REQUEST(ring, ring->rsp_prod_pvt)->id;
		rsp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
		rsp->id = id;
		rsp->status = NETIF_RSP_ERROR;
		n_responses = 1;
	} else {
		gnt_idx = 0;
		const int has_extra = pkt->flags & NETRXF_extra_info;
		if (has_extra != 0)
			n_responses++;

		for (i = 0; i < n_responses; i++) {
			netif_rx_request_t rxq;
			netif_rx_response_t *rsp;

			r_idx = ring->rsp_prod_pvt + i;
			/*
			 * We copy the structure of rxq instead of making a
			 * pointer because it shares the same memory as rsp.
			 */
			rxq = *(RING_GET_REQUEST(ring, r_idx));
			rsp = RING_GET_RESPONSE(ring, r_idx);
			if (has_extra && (i == 1)) {
				netif_extra_info_t *ext =
					(netif_extra_info_t*)rsp;
				ext->type = XEN_NETIF_EXTRA_TYPE_GSO;
				ext->flags = 0;
				ext->u.gso.size = pkt->extra.u.gso.size;
				ext->u.gso.type = XEN_NETIF_GSO_TYPE_TCPV4;
				ext->u.gso.pad = 0;
				ext->u.gso.features = 0;
			} else {
				rsp->id = rxq.id;
				rsp->status = GNTST_okay;
				rsp->offset = 0;
				rsp->flags = 0;
				if (i < pkt->list_len - 1)
					rsp->flags |= NETRXF_more_data;
				if ((i == 0) && has_extra)
					rsp->flags |= NETRXF_extra_info;
				if ((i == 0) &&
					(pkt->flags & NETRXF_data_validated)) {
					rsp->flags |= NETRXF_data_validated;
					rsp->flags |= NETRXF_csum_blank;
				}
				rsp->status = 0;
				for (; gnttab[gnt_idx].dest.u.ref == rxq.gref;
				    gnt_idx++) {
					rsp->status += gnttab[gnt_idx].len;
				}
			}
		}
	}

	ring->req_cons += n_responses;
	ring->rsp_prod_pvt += n_responses;
	return n_responses;
}

#if defined(INET) || defined(INET6)
/**
 * Add IP, TCP, and/or UDP checksums to every mbuf in a chain.  The first mbuf
 * in the chain must start with a struct ether_header.
 *
 * XXX This function will perform incorrectly on UDP packets that are split up
 * into multiple ethernet frames.
 */
static void
xnb_add_mbuf_cksum(struct mbuf *mbufc)
{
	struct ether_header *eh;
	struct ip *iph;
	uint16_t ether_type;

	eh = mtod(mbufc, struct ether_header*);
	ether_type = ntohs(eh->ether_type);
	if (ether_type != ETHERTYPE_IP) {
		/* Nothing to calculate */
		return;
	}

	iph = (struct ip*)(eh + 1);
	if (mbufc->m_pkthdr.csum_flags & CSUM_IP_VALID) {
		iph->ip_sum = 0;
		iph->ip_sum = in_cksum_hdr(iph);
	}

	switch (iph->ip_p) {
	case IPPROTO_TCP:
		if (mbufc->m_pkthdr.csum_flags & CSUM_IP_VALID) {
			size_t tcplen = ntohs(iph->ip_len) - sizeof(struct ip);
			struct tcphdr *th = (struct tcphdr*)(iph + 1);
			th->th_sum = in_pseudo(iph->ip_src.s_addr,
			    iph->ip_dst.s_addr, htons(IPPROTO_TCP + tcplen));
			th->th_sum = in_cksum_skip(mbufc,
			    sizeof(struct ether_header) + ntohs(iph->ip_len),
			    sizeof(struct ether_header) + (iph->ip_hl << 2));
		}
		break;
	case IPPROTO_UDP:
		if (mbufc->m_pkthdr.csum_flags & CSUM_IP_VALID) {
			size_t udplen = ntohs(iph->ip_len) - sizeof(struct ip);
			struct udphdr *uh = (struct udphdr*)(iph + 1);
			uh->uh_sum = in_pseudo(iph->ip_src.s_addr,
			    iph->ip_dst.s_addr, htons(IPPROTO_UDP + udplen));
			uh->uh_sum = in_cksum_skip(mbufc,
			    sizeof(struct ether_header) + ntohs(iph->ip_len),
			    sizeof(struct ether_header) + (iph->ip_hl << 2));
		}
		break;
	default:
		break;
	}
}
#endif /* INET || INET6 */

static void
xnb_stop(struct xnb_softc *xnb)
{
	struct ifnet *ifp;

	mtx_assert(&xnb->sc_lock, MA_OWNED);
	ifp = xnb->xnb_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	if_link_state_change(ifp, LINK_STATE_DOWN);
}

static int
xnb_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct xnb_softc *xnb = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq*) data;
#ifdef INET
	struct ifaddr *ifa = (struct ifaddr*)data;
#endif
	int error = 0;

	switch (cmd) {
		case SIOCSIFFLAGS:
			mtx_lock(&xnb->sc_lock);
			if (ifp->if_flags & IFF_UP) {
				xnb_ifinit_locked(xnb);
			} else {
				if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
					xnb_stop(xnb);
				}
			}
			/*
			 * Note: netfront sets a variable named xn_if_flags
			 * here, but that variable is never read
			 */
			mtx_unlock(&xnb->sc_lock);
			break;
		case SIOCSIFADDR:
#ifdef INET
			mtx_lock(&xnb->sc_lock);
			if (ifa->ifa_addr->sa_family == AF_INET) {
				ifp->if_flags |= IFF_UP;
				if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
					ifp->if_drv_flags &= ~(IFF_DRV_RUNNING |
							IFF_DRV_OACTIVE);
					if_link_state_change(ifp,
							LINK_STATE_DOWN);
					ifp->if_drv_flags |= IFF_DRV_RUNNING;
					ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
					if_link_state_change(ifp,
					    LINK_STATE_UP);
				}
				arp_ifinit(ifp, ifa);
				mtx_unlock(&xnb->sc_lock);
			} else {
				mtx_unlock(&xnb->sc_lock);
#endif
				error = ether_ioctl(ifp, cmd, data);
#ifdef INET
			}
#endif
			break;
		case SIOCSIFCAP:
			mtx_lock(&xnb->sc_lock);
			if (ifr->ifr_reqcap & IFCAP_TXCSUM) {
				ifp->if_capenable |= IFCAP_TXCSUM;
				ifp->if_hwassist |= XNB_CSUM_FEATURES;
			} else {
				ifp->if_capenable &= ~(IFCAP_TXCSUM);
				ifp->if_hwassist &= ~(XNB_CSUM_FEATURES);
			}
			if ((ifr->ifr_reqcap & IFCAP_RXCSUM)) {
				ifp->if_capenable |= IFCAP_RXCSUM;
			} else {
				ifp->if_capenable &= ~(IFCAP_RXCSUM);
			}
			/*
			 * TODO enable TSO4 and LRO once we no longer need
			 * to calculate checksums in software
			 */
#if 0
			if (ifr->if_reqcap |= IFCAP_TSO4) {
				if (IFCAP_TXCSUM & ifp->if_capenable) {
					printf("xnb: Xen netif requires that "
						"TXCSUM be enabled in order "
						"to use TSO4\n");
					error = EINVAL;
				} else {
					ifp->if_capenable |= IFCAP_TSO4;
					ifp->if_hwassist |= CSUM_TSO;
				}
			} else {
				ifp->if_capenable &= ~(IFCAP_TSO4);
				ifp->if_hwassist &= ~(CSUM_TSO);
			}
			if (ifr->ifreqcap |= IFCAP_LRO) {
				ifp->if_capenable |= IFCAP_LRO;
			} else {
				ifp->if_capenable &= ~(IFCAP_LRO);
			}
#endif
			mtx_unlock(&xnb->sc_lock);
			break;
		case SIOCSIFMTU:
			ifp->if_mtu = ifr->ifr_mtu;
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			xnb_ifinit(xnb);
			break;
		case SIOCADDMULTI:
		case SIOCDELMULTI:
		case SIOCSIFMEDIA:
		case SIOCGIFMEDIA:
			error = ifmedia_ioctl(ifp, ifr, &xnb->sc_media, cmd);
			break;
		default:
			error = ether_ioctl(ifp, cmd, data);
			break;
	}
	return (error);
}

static void
xnb_start_locked(struct ifnet *ifp)
{
	netif_rx_back_ring_t *rxb;
	struct xnb_softc *xnb;
	struct mbuf *mbufc;
	RING_IDX req_prod_local;

	xnb = ifp->if_softc;
	rxb = &xnb->ring_configs[XNB_RING_TYPE_RX].back_ring.rx_ring;

	if (!xnb->carrier)
		return;

	do {
		int out_of_space = 0;
		int notify;
		req_prod_local = rxb->sring->req_prod;
		xen_rmb();
		for (;;) {
			int error;

			IF_DEQUEUE(&ifp->if_snd, mbufc);
			if (mbufc == NULL)
				break;
			error = xnb_send(rxb, xnb->otherend_id, mbufc,
			    		 xnb->rx_gnttab);
			switch (error) {
				case EAGAIN:
					/*
					 * Insufficient space in the ring.
					 * Requeue pkt and send when space is
					 * available.
					 */
					IF_PREPEND(&ifp->if_snd, mbufc);
					/*
					 * Perhaps the frontend missed an IRQ
					 * and went to sleep.  Notify it to wake
					 * it up.
					 */
					out_of_space = 1;
					break;

				case EINVAL:
					/* OS gave a corrupt packet.  Drop it.*/
					if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
					/* FALLTHROUGH */
				default:
					/* Send succeeded, or packet had error.
					 * Free the packet */
					if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
					if (mbufc)
						m_freem(mbufc);
					break;
			}
			if (out_of_space != 0)
				break;
		}

		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(rxb, notify);
		if ((notify != 0) || (out_of_space != 0))
			xen_intr_signal(xnb->xen_intr_handle);
		rxb->sring->req_event = req_prod_local + 1;
		xen_mb();
	} while (rxb->sring->req_prod != req_prod_local) ;
}

/**
 * Sends one packet to the ring.  Blocks until the packet is on the ring
 * \param[in]	mbufc	Contains one packet to send.  Caller must free
 * \param[in,out] rxb	The packet will be pushed onto this ring, but the
 * 			otherend will not be notified.
 * \param[in]	otherend The domain ID of the other end of the connection
 * \retval	EAGAIN	The ring did not have enough space for the packet.
 * 			The ring has not been modified
 * \param[in,out] gnttab Pointer to enough memory for a grant table.  We make
 * 			this a function parameter so that we will take less
 * 			stack space.
 * \retval EINVAL	mbufc was corrupt or not convertible into a pkt
 */
static int
xnb_send(netif_rx_back_ring_t *ring, domid_t otherend, const struct mbuf *mbufc,
	 gnttab_copy_table gnttab)
{
	struct xnb_pkt pkt;
	int error, n_entries, n_reqs;
	RING_IDX space;

	space = ring->sring->req_prod - ring->req_cons;
	error = xnb_mbufc2pkt(mbufc, &pkt, ring->rsp_prod_pvt, space);
	if (error != 0)
		return error;
	n_entries = xnb_rxpkt2gnttab(&pkt, mbufc, gnttab, ring, otherend);
	if (n_entries != 0) {
		int __unused hv_ret = HYPERVISOR_grant_table_op(GNTTABOP_copy,
		    gnttab, n_entries);
		KASSERT(hv_ret == 0, ("HYPERVISOR_grant_table_op returned %d\n",
		    hv_ret));
	}

	n_reqs = xnb_rxpkt2rsp(&pkt, gnttab, n_entries, ring);

	return 0;
}

static void
xnb_start(struct ifnet *ifp)
{
	struct xnb_softc *xnb;

	xnb = ifp->if_softc;
	mtx_lock(&xnb->rx_lock);
	xnb_start_locked(ifp);
	mtx_unlock(&xnb->rx_lock);
}

/* equivalent of network_open() in Linux */
static void
xnb_ifinit_locked(struct xnb_softc *xnb)
{
	struct ifnet *ifp;

	ifp = xnb->xnb_ifp;

	mtx_assert(&xnb->sc_lock, MA_OWNED);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	xnb_stop(xnb);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	if_link_state_change(ifp, LINK_STATE_UP);
}


static void
xnb_ifinit(void *xsc)
{
	struct xnb_softc *xnb = xsc;

	mtx_lock(&xnb->sc_lock);
	xnb_ifinit_locked(xnb);
	mtx_unlock(&xnb->sc_lock);
}

/**
 * Callback used by the generic networking code to tell us when our carrier
 * state has changed.  Since we don't have a physical carrier, we don't care
 */
static int
xnb_ifmedia_upd(struct ifnet *ifp)
{
	return (0);
}

/**
 * Callback used by the generic networking code to ask us what our carrier
 * state is.  Since we don't have a physical carrier, this is very simple
 */
static void
xnb_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	ifmr->ifm_status = IFM_AVALID|IFM_ACTIVE;
	ifmr->ifm_active = IFM_ETHER|IFM_MANUAL;
}


/*---------------------------- NewBus Registration ---------------------------*/
static device_method_t xnb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		xnb_probe),
	DEVMETHOD(device_attach,	xnb_attach),
	DEVMETHOD(device_detach,	xnb_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	xnb_suspend),
	DEVMETHOD(device_resume,	xnb_resume),

	/* Xenbus interface */
	DEVMETHOD(xenbus_otherend_changed, xnb_frontend_changed),

	{ 0, 0 }
};

static driver_t xnb_driver = {
	"xnb",
	xnb_methods,
	sizeof(struct xnb_softc),
};
devclass_t xnb_devclass;

DRIVER_MODULE(xnb, xenbusb_back, xnb_driver, xnb_devclass, 0, 0);


/*-------------------------- Unit Tests -------------------------------------*/
#ifdef XNB_DEBUG
#include "netback_unit_tests.c"
#endif
