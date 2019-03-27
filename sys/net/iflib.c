/*-
 * Copyright (c) 2014-2018, Matthew Macy <mmacy@mattmacy.io>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_acpi.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/md5.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/kobj.h>
#include <sys/rman.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>
#include <sys/limits.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/mp_ring.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp_lro.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/ip_var.h>
#include <netinet/netdump/netdump.h>
#include <netinet6/ip6_var.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/led/led.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>

#include <net/iflib.h>
#include <net/iflib_private.h>

#include "ifdi_if.h"

#ifdef PCI_IOV
#include <dev/pci/pci_iov.h>
#endif

#include <sys/bitstring.h>
/*
 * enable accounting of every mbuf as it comes in to and goes out of
 * iflib's software descriptor references
 */
#define MEMORY_LOGGING 0
/*
 * Enable mbuf vectors for compressing long mbuf chains
 */

/*
 * NB:
 * - Prefetching in tx cleaning should perhaps be a tunable. The distance ahead
 *   we prefetch needs to be determined by the time spent in m_free vis a vis
 *   the cost of a prefetch. This will of course vary based on the workload:
 *      - NFLX's m_free path is dominated by vm-based M_EXT manipulation which
 *        is quite expensive, thus suggesting very little prefetch.
 *      - small packet forwarding which is just returning a single mbuf to
 *        UMA will typically be very fast vis a vis the cost of a memory
 *        access.
 */


/*
 * File organization:
 *  - private structures
 *  - iflib private utility functions
 *  - ifnet functions
 *  - vlan registry and other exported functions
 *  - iflib public core functions
 *
 *
 */
MALLOC_DEFINE(M_IFLIB, "iflib", "ifnet library");

struct iflib_txq;
typedef struct iflib_txq *iflib_txq_t;
struct iflib_rxq;
typedef struct iflib_rxq *iflib_rxq_t;
struct iflib_fl;
typedef struct iflib_fl *iflib_fl_t;

struct iflib_ctx;

static void iru_init(if_rxd_update_t iru, iflib_rxq_t rxq, uint8_t flid);
static void iflib_timer(void *arg);

typedef struct iflib_filter_info {
	driver_filter_t *ifi_filter;
	void *ifi_filter_arg;
	struct grouptask *ifi_task;
	void *ifi_ctx;
} *iflib_filter_info_t;

struct iflib_ctx {
	KOBJ_FIELDS;
	/*
	 * Pointer to hardware driver's softc
	 */
	void *ifc_softc;
	device_t ifc_dev;
	if_t ifc_ifp;

	cpuset_t ifc_cpus;
	if_shared_ctx_t ifc_sctx;
	struct if_softc_ctx ifc_softc_ctx;

	struct sx ifc_ctx_sx;
	struct mtx ifc_state_mtx;

	iflib_txq_t ifc_txqs;
	iflib_rxq_t ifc_rxqs;
	uint32_t ifc_if_flags;
	uint32_t ifc_flags;
	uint32_t ifc_max_fl_buf_size;
	uint32_t ifc_rx_mbuf_sz;

	int ifc_link_state;
	int ifc_link_irq;
	int ifc_watchdog_events;
	struct cdev *ifc_led_dev;
	struct resource *ifc_msix_mem;

	struct if_irq ifc_legacy_irq;
	struct grouptask ifc_admin_task;
	struct grouptask ifc_vflr_task;
	struct iflib_filter_info ifc_filter_info;
	struct ifmedia	ifc_media;

	struct sysctl_oid *ifc_sysctl_node;
	uint16_t ifc_sysctl_ntxqs;
	uint16_t ifc_sysctl_nrxqs;
	uint16_t ifc_sysctl_qs_eq_override;
	uint16_t ifc_sysctl_rx_budget;
	uint16_t ifc_sysctl_tx_abdicate;

	qidx_t ifc_sysctl_ntxds[8];
	qidx_t ifc_sysctl_nrxds[8];
	struct if_txrx ifc_txrx;
#define isc_txd_encap  ifc_txrx.ift_txd_encap
#define isc_txd_flush  ifc_txrx.ift_txd_flush
#define isc_txd_credits_update  ifc_txrx.ift_txd_credits_update
#define isc_rxd_available ifc_txrx.ift_rxd_available
#define isc_rxd_pkt_get ifc_txrx.ift_rxd_pkt_get
#define isc_rxd_refill ifc_txrx.ift_rxd_refill
#define isc_rxd_flush ifc_txrx.ift_rxd_flush
#define isc_rxd_refill ifc_txrx.ift_rxd_refill
#define isc_rxd_refill ifc_txrx.ift_rxd_refill
#define isc_legacy_intr ifc_txrx.ift_legacy_intr
	eventhandler_tag ifc_vlan_attach_event;
	eventhandler_tag ifc_vlan_detach_event;
	uint8_t ifc_mac[ETHER_ADDR_LEN];
	char ifc_mtx_name[16];
};


void *
iflib_get_softc(if_ctx_t ctx)
{

	return (ctx->ifc_softc);
}

device_t
iflib_get_dev(if_ctx_t ctx)
{

	return (ctx->ifc_dev);
}

if_t
iflib_get_ifp(if_ctx_t ctx)
{

	return (ctx->ifc_ifp);
}

struct ifmedia *
iflib_get_media(if_ctx_t ctx)
{

	return (&ctx->ifc_media);
}

uint32_t
iflib_get_flags(if_ctx_t ctx)
{
	return (ctx->ifc_flags);
}

void
iflib_set_mac(if_ctx_t ctx, uint8_t mac[ETHER_ADDR_LEN])
{

	bcopy(mac, ctx->ifc_mac, ETHER_ADDR_LEN);
}

if_softc_ctx_t
iflib_get_softc_ctx(if_ctx_t ctx)
{

	return (&ctx->ifc_softc_ctx);
}

if_shared_ctx_t
iflib_get_sctx(if_ctx_t ctx)
{

	return (ctx->ifc_sctx);
}

#define IP_ALIGNED(m) ((((uintptr_t)(m)->m_data) & 0x3) == 0x2)
#define CACHE_PTR_INCREMENT (CACHE_LINE_SIZE/sizeof(void*))
#define CACHE_PTR_NEXT(ptr) ((void *)(((uintptr_t)(ptr)+CACHE_LINE_SIZE-1) & (CACHE_LINE_SIZE-1)))

#define LINK_ACTIVE(ctx) ((ctx)->ifc_link_state == LINK_STATE_UP)
#define CTX_IS_VF(ctx) ((ctx)->ifc_sctx->isc_flags & IFLIB_IS_VF)

typedef struct iflib_sw_rx_desc_array {
	bus_dmamap_t	*ifsd_map;         /* bus_dma maps for packet */
	struct mbuf	**ifsd_m;           /* pkthdr mbufs */
	caddr_t		*ifsd_cl;          /* direct cluster pointer for rx */
	bus_addr_t	*ifsd_ba;          /* bus addr of cluster for rx */
} iflib_rxsd_array_t;

typedef struct iflib_sw_tx_desc_array {
	bus_dmamap_t    *ifsd_map;         /* bus_dma maps for packet */
	bus_dmamap_t	*ifsd_tso_map;     /* bus_dma maps for TSO packet */
	struct mbuf    **ifsd_m;           /* pkthdr mbufs */
} if_txsd_vec_t;


/* magic number that should be high enough for any hardware */
#define IFLIB_MAX_TX_SEGS		128
#define IFLIB_RX_COPY_THRESH		128
#define IFLIB_MAX_RX_REFRESH		32
/* The minimum descriptors per second before we start coalescing */
#define IFLIB_MIN_DESC_SEC		16384
#define IFLIB_DEFAULT_TX_UPDATE_FREQ	16
#define IFLIB_QUEUE_IDLE		0
#define IFLIB_QUEUE_HUNG		1
#define IFLIB_QUEUE_WORKING		2
/* maximum number of txqs that can share an rx interrupt */
#define IFLIB_MAX_TX_SHARED_INTR	4

/* this should really scale with ring size - this is a fairly arbitrary value */
#define TX_BATCH_SIZE			32

#define IFLIB_RESTART_BUDGET		8


#define CSUM_OFFLOAD		(CSUM_IP_TSO|CSUM_IP6_TSO|CSUM_IP| \
				 CSUM_IP_UDP|CSUM_IP_TCP|CSUM_IP_SCTP| \
				 CSUM_IP6_UDP|CSUM_IP6_TCP|CSUM_IP6_SCTP)
struct iflib_txq {
	qidx_t		ift_in_use;
	qidx_t		ift_cidx;
	qidx_t		ift_cidx_processed;
	qidx_t		ift_pidx;
	uint8_t		ift_gen;
	uint8_t		ift_br_offset;
	uint16_t	ift_npending;
	uint16_t	ift_db_pending;
	uint16_t	ift_rs_pending;
	/* implicit pad */
	uint8_t		ift_txd_size[8];
	uint64_t	ift_processed;
	uint64_t	ift_cleaned;
	uint64_t	ift_cleaned_prev;
#if MEMORY_LOGGING
	uint64_t	ift_enqueued;
	uint64_t	ift_dequeued;
#endif
	uint64_t	ift_no_tx_dma_setup;
	uint64_t	ift_no_desc_avail;
	uint64_t	ift_mbuf_defrag_failed;
	uint64_t	ift_mbuf_defrag;
	uint64_t	ift_map_failed;
	uint64_t	ift_txd_encap_efbig;
	uint64_t	ift_pullups;
	uint64_t	ift_last_timer_tick;

	struct mtx	ift_mtx;
	struct mtx	ift_db_mtx;

	/* constant values */
	if_ctx_t	ift_ctx;
	struct ifmp_ring        *ift_br;
	struct grouptask	ift_task;
	qidx_t		ift_size;
	uint16_t	ift_id;
	struct callout	ift_timer;

	if_txsd_vec_t	ift_sds;
	uint8_t		ift_qstatus;
	uint8_t		ift_closed;
	uint8_t		ift_update_freq;
	struct iflib_filter_info ift_filter_info;
	bus_dma_tag_t	ift_buf_tag;
	bus_dma_tag_t	ift_tso_buf_tag;
	iflib_dma_info_t	ift_ifdi;
#define MTX_NAME_LEN 16
	char                    ift_mtx_name[MTX_NAME_LEN];
	char                    ift_db_mtx_name[MTX_NAME_LEN];
	bus_dma_segment_t	ift_segs[IFLIB_MAX_TX_SEGS]  __aligned(CACHE_LINE_SIZE);
#ifdef IFLIB_DIAGNOSTICS
	uint64_t ift_cpu_exec_count[256];
#endif
} __aligned(CACHE_LINE_SIZE);

struct iflib_fl {
	qidx_t		ifl_cidx;
	qidx_t		ifl_pidx;
	qidx_t		ifl_credits;
	uint8_t		ifl_gen;
	uint8_t		ifl_rxd_size;
#if MEMORY_LOGGING
	uint64_t	ifl_m_enqueued;
	uint64_t	ifl_m_dequeued;
	uint64_t	ifl_cl_enqueued;
	uint64_t	ifl_cl_dequeued;
#endif
	/* implicit pad */

	bitstr_t 	*ifl_rx_bitmap;
	qidx_t		ifl_fragidx;
	/* constant */
	qidx_t		ifl_size;
	uint16_t	ifl_buf_size;
	uint16_t	ifl_cltype;
	uma_zone_t	ifl_zone;
	iflib_rxsd_array_t	ifl_sds;
	iflib_rxq_t	ifl_rxq;
	uint8_t		ifl_id;
	bus_dma_tag_t	ifl_buf_tag;
	iflib_dma_info_t	ifl_ifdi;
	uint64_t	ifl_bus_addrs[IFLIB_MAX_RX_REFRESH] __aligned(CACHE_LINE_SIZE);
	caddr_t		ifl_vm_addrs[IFLIB_MAX_RX_REFRESH];
	qidx_t	ifl_rxd_idxs[IFLIB_MAX_RX_REFRESH];
}  __aligned(CACHE_LINE_SIZE);

static inline qidx_t
get_inuse(int size, qidx_t cidx, qidx_t pidx, uint8_t gen)
{
	qidx_t used;

	if (pidx > cidx)
		used = pidx - cidx;
	else if (pidx < cidx)
		used = size - cidx + pidx;
	else if (gen == 0 && pidx == cidx)
		used = 0;
	else if (gen == 1 && pidx == cidx)
		used = size;
	else
		panic("bad state");

	return (used);
}

#define TXQ_AVAIL(txq) (txq->ift_size - get_inuse(txq->ift_size, txq->ift_cidx, txq->ift_pidx, txq->ift_gen))

#define IDXDIFF(head, tail, wrap) \
	((head) >= (tail) ? (head) - (tail) : (wrap) - (tail) + (head))

struct iflib_rxq {
	/* If there is a separate completion queue -
	 * these are the cq cidx and pidx. Otherwise
	 * these are unused.
	 */
	qidx_t		ifr_size;
	qidx_t		ifr_cq_cidx;
	qidx_t		ifr_cq_pidx;
	uint8_t		ifr_cq_gen;
	uint8_t		ifr_fl_offset;

	if_ctx_t	ifr_ctx;
	iflib_fl_t	ifr_fl;
	uint64_t	ifr_rx_irq;
	uint16_t	ifr_id;
	uint8_t		ifr_lro_enabled;
	uint8_t		ifr_nfl;
	uint8_t		ifr_ntxqirq;
	uint8_t		ifr_txqid[IFLIB_MAX_TX_SHARED_INTR];
	struct lro_ctrl			ifr_lc;
	struct grouptask        ifr_task;
	struct iflib_filter_info ifr_filter_info;
	iflib_dma_info_t		ifr_ifdi;

	/* dynamically allocate if any drivers need a value substantially larger than this */
	struct if_rxd_frag	ifr_frags[IFLIB_MAX_RX_SEGS] __aligned(CACHE_LINE_SIZE);
#ifdef IFLIB_DIAGNOSTICS
	uint64_t ifr_cpu_exec_count[256];
#endif
}  __aligned(CACHE_LINE_SIZE);

typedef struct if_rxsd {
	caddr_t *ifsd_cl;
	struct mbuf **ifsd_m;
	iflib_fl_t ifsd_fl;
	qidx_t ifsd_cidx;
} *if_rxsd_t;

/* multiple of word size */
#ifdef __LP64__
#define PKT_INFO_SIZE	6
#define RXD_INFO_SIZE	5
#define PKT_TYPE uint64_t
#else
#define PKT_INFO_SIZE	11
#define RXD_INFO_SIZE	8
#define PKT_TYPE uint32_t
#endif
#define PKT_LOOP_BOUND  ((PKT_INFO_SIZE/3)*3)
#define RXD_LOOP_BOUND  ((RXD_INFO_SIZE/4)*4)

typedef struct if_pkt_info_pad {
	PKT_TYPE pkt_val[PKT_INFO_SIZE];
} *if_pkt_info_pad_t;
typedef struct if_rxd_info_pad {
	PKT_TYPE rxd_val[RXD_INFO_SIZE];
} *if_rxd_info_pad_t;

CTASSERT(sizeof(struct if_pkt_info_pad) == sizeof(struct if_pkt_info));
CTASSERT(sizeof(struct if_rxd_info_pad) == sizeof(struct if_rxd_info));


static inline void
pkt_info_zero(if_pkt_info_t pi)
{
	if_pkt_info_pad_t pi_pad;

	pi_pad = (if_pkt_info_pad_t)pi;
	pi_pad->pkt_val[0] = 0; pi_pad->pkt_val[1] = 0; pi_pad->pkt_val[2] = 0;
	pi_pad->pkt_val[3] = 0; pi_pad->pkt_val[4] = 0; pi_pad->pkt_val[5] = 0;
#ifndef __LP64__
	pi_pad->pkt_val[6] = 0; pi_pad->pkt_val[7] = 0; pi_pad->pkt_val[8] = 0;
	pi_pad->pkt_val[9] = 0; pi_pad->pkt_val[10] = 0;
#endif	
}

static device_method_t iflib_pseudo_methods[] = {
	DEVMETHOD(device_attach, noop_attach),
	DEVMETHOD(device_detach, iflib_pseudo_detach),
	DEVMETHOD_END
};

driver_t iflib_pseudodriver = {
	"iflib_pseudo", iflib_pseudo_methods, sizeof(struct iflib_ctx),
};

static inline void
rxd_info_zero(if_rxd_info_t ri)
{
	if_rxd_info_pad_t ri_pad;
	int i;

	ri_pad = (if_rxd_info_pad_t)ri;
	for (i = 0; i < RXD_LOOP_BOUND; i += 4) {
		ri_pad->rxd_val[i] = 0;
		ri_pad->rxd_val[i+1] = 0;
		ri_pad->rxd_val[i+2] = 0;
		ri_pad->rxd_val[i+3] = 0;
	}
#ifdef __LP64__
	ri_pad->rxd_val[RXD_INFO_SIZE-1] = 0;
#endif
}

/*
 * Only allow a single packet to take up most 1/nth of the tx ring
 */
#define MAX_SINGLE_PACKET_FRACTION 12
#define IF_BAD_DMA (bus_addr_t)-1

#define CTX_ACTIVE(ctx) ((if_getdrvflags((ctx)->ifc_ifp) & IFF_DRV_RUNNING))

#define CTX_LOCK_INIT(_sc)  sx_init(&(_sc)->ifc_ctx_sx, "iflib ctx lock")
#define CTX_LOCK(ctx) sx_xlock(&(ctx)->ifc_ctx_sx)
#define CTX_UNLOCK(ctx) sx_xunlock(&(ctx)->ifc_ctx_sx)
#define CTX_LOCK_DESTROY(ctx) sx_destroy(&(ctx)->ifc_ctx_sx)


#define STATE_LOCK_INIT(_sc, _name)  mtx_init(&(_sc)->ifc_state_mtx, _name, "iflib state lock", MTX_DEF)
#define STATE_LOCK(ctx) mtx_lock(&(ctx)->ifc_state_mtx)
#define STATE_UNLOCK(ctx) mtx_unlock(&(ctx)->ifc_state_mtx)
#define STATE_LOCK_DESTROY(ctx) mtx_destroy(&(ctx)->ifc_state_mtx)



#define CALLOUT_LOCK(txq)	mtx_lock(&txq->ift_mtx)
#define CALLOUT_UNLOCK(txq) 	mtx_unlock(&txq->ift_mtx)

void
iflib_set_detach(if_ctx_t ctx)
{
	STATE_LOCK(ctx);
	ctx->ifc_flags |= IFC_IN_DETACH;
	STATE_UNLOCK(ctx);
}

/* Our boot-time initialization hook */
static int	iflib_module_event_handler(module_t, int, void *);

static moduledata_t iflib_moduledata = {
	"iflib",
	iflib_module_event_handler,
	NULL
};

DECLARE_MODULE(iflib, iflib_moduledata, SI_SUB_INIT_IF, SI_ORDER_ANY);
MODULE_VERSION(iflib, 1);

MODULE_DEPEND(iflib, pci, 1, 1, 1);
MODULE_DEPEND(iflib, ether, 1, 1, 1);

TASKQGROUP_DEFINE(if_io_tqg, mp_ncpus, 1);
TASKQGROUP_DEFINE(if_config_tqg, 1, 1);

#ifndef IFLIB_DEBUG_COUNTERS
#ifdef INVARIANTS
#define IFLIB_DEBUG_COUNTERS 1
#else
#define IFLIB_DEBUG_COUNTERS 0
#endif /* !INVARIANTS */
#endif

static SYSCTL_NODE(_net, OID_AUTO, iflib, CTLFLAG_RD, 0,
                   "iflib driver parameters");

/*
 * XXX need to ensure that this can't accidentally cause the head to be moved backwards 
 */
static int iflib_min_tx_latency = 0;
SYSCTL_INT(_net_iflib, OID_AUTO, min_tx_latency, CTLFLAG_RW,
		   &iflib_min_tx_latency, 0, "minimize transmit latency at the possible expense of throughput");
static int iflib_no_tx_batch = 0;
SYSCTL_INT(_net_iflib, OID_AUTO, no_tx_batch, CTLFLAG_RW,
		   &iflib_no_tx_batch, 0, "minimize transmit latency at the possible expense of throughput");


#if IFLIB_DEBUG_COUNTERS

static int iflib_tx_seen;
static int iflib_tx_sent;
static int iflib_tx_encap;
static int iflib_rx_allocs;
static int iflib_fl_refills;
static int iflib_fl_refills_large;
static int iflib_tx_frees;

SYSCTL_INT(_net_iflib, OID_AUTO, tx_seen, CTLFLAG_RD,
		   &iflib_tx_seen, 0, "# tx mbufs seen");
SYSCTL_INT(_net_iflib, OID_AUTO, tx_sent, CTLFLAG_RD,
		   &iflib_tx_sent, 0, "# tx mbufs sent");
SYSCTL_INT(_net_iflib, OID_AUTO, tx_encap, CTLFLAG_RD,
		   &iflib_tx_encap, 0, "# tx mbufs encapped");
SYSCTL_INT(_net_iflib, OID_AUTO, tx_frees, CTLFLAG_RD,
		   &iflib_tx_frees, 0, "# tx frees");
SYSCTL_INT(_net_iflib, OID_AUTO, rx_allocs, CTLFLAG_RD,
		   &iflib_rx_allocs, 0, "# rx allocations");
SYSCTL_INT(_net_iflib, OID_AUTO, fl_refills, CTLFLAG_RD,
		   &iflib_fl_refills, 0, "# refills");
SYSCTL_INT(_net_iflib, OID_AUTO, fl_refills_large, CTLFLAG_RD,
		   &iflib_fl_refills_large, 0, "# large refills");


static int iflib_txq_drain_flushing;
static int iflib_txq_drain_oactive;
static int iflib_txq_drain_notready;

SYSCTL_INT(_net_iflib, OID_AUTO, txq_drain_flushing, CTLFLAG_RD,
		   &iflib_txq_drain_flushing, 0, "# drain flushes");
SYSCTL_INT(_net_iflib, OID_AUTO, txq_drain_oactive, CTLFLAG_RD,
		   &iflib_txq_drain_oactive, 0, "# drain oactives");
SYSCTL_INT(_net_iflib, OID_AUTO, txq_drain_notready, CTLFLAG_RD,
		   &iflib_txq_drain_notready, 0, "# drain notready");


static int iflib_encap_load_mbuf_fail;
static int iflib_encap_pad_mbuf_fail;
static int iflib_encap_txq_avail_fail;
static int iflib_encap_txd_encap_fail;

SYSCTL_INT(_net_iflib, OID_AUTO, encap_load_mbuf_fail, CTLFLAG_RD,
		   &iflib_encap_load_mbuf_fail, 0, "# busdma load failures");
SYSCTL_INT(_net_iflib, OID_AUTO, encap_pad_mbuf_fail, CTLFLAG_RD,
		   &iflib_encap_pad_mbuf_fail, 0, "# runt frame pad failures");
SYSCTL_INT(_net_iflib, OID_AUTO, encap_txq_avail_fail, CTLFLAG_RD,
		   &iflib_encap_txq_avail_fail, 0, "# txq avail failures");
SYSCTL_INT(_net_iflib, OID_AUTO, encap_txd_encap_fail, CTLFLAG_RD,
		   &iflib_encap_txd_encap_fail, 0, "# driver encap failures");

static int iflib_task_fn_rxs;
static int iflib_rx_intr_enables;
static int iflib_fast_intrs;
static int iflib_rx_unavail;
static int iflib_rx_ctx_inactive;
static int iflib_rx_if_input;
static int iflib_rx_mbuf_null;
static int iflib_rxd_flush;

static int iflib_verbose_debug;

SYSCTL_INT(_net_iflib, OID_AUTO, task_fn_rx, CTLFLAG_RD,
		   &iflib_task_fn_rxs, 0, "# task_fn_rx calls");
SYSCTL_INT(_net_iflib, OID_AUTO, rx_intr_enables, CTLFLAG_RD,
		   &iflib_rx_intr_enables, 0, "# rx intr enables");
SYSCTL_INT(_net_iflib, OID_AUTO, fast_intrs, CTLFLAG_RD,
		   &iflib_fast_intrs, 0, "# fast_intr calls");
SYSCTL_INT(_net_iflib, OID_AUTO, rx_unavail, CTLFLAG_RD,
		   &iflib_rx_unavail, 0, "# times rxeof called with no available data");
SYSCTL_INT(_net_iflib, OID_AUTO, rx_ctx_inactive, CTLFLAG_RD,
		   &iflib_rx_ctx_inactive, 0, "# times rxeof called with inactive context");
SYSCTL_INT(_net_iflib, OID_AUTO, rx_if_input, CTLFLAG_RD,
		   &iflib_rx_if_input, 0, "# times rxeof called if_input");
SYSCTL_INT(_net_iflib, OID_AUTO, rx_mbuf_null, CTLFLAG_RD,
		   &iflib_rx_mbuf_null, 0, "# times rxeof got null mbuf");
SYSCTL_INT(_net_iflib, OID_AUTO, rxd_flush, CTLFLAG_RD,
	         &iflib_rxd_flush, 0, "# times rxd_flush called");
SYSCTL_INT(_net_iflib, OID_AUTO, verbose_debug, CTLFLAG_RW,
		   &iflib_verbose_debug, 0, "enable verbose debugging");

#define DBG_COUNTER_INC(name) atomic_add_int(&(iflib_ ## name), 1)
static void
iflib_debug_reset(void)
{
	iflib_tx_seen = iflib_tx_sent = iflib_tx_encap = iflib_rx_allocs =
		iflib_fl_refills = iflib_fl_refills_large = iflib_tx_frees =
		iflib_txq_drain_flushing = iflib_txq_drain_oactive =
		iflib_txq_drain_notready =
		iflib_encap_load_mbuf_fail = iflib_encap_pad_mbuf_fail =
		iflib_encap_txq_avail_fail = iflib_encap_txd_encap_fail =
		iflib_task_fn_rxs = iflib_rx_intr_enables = iflib_fast_intrs =
		iflib_rx_unavail =
		iflib_rx_ctx_inactive = iflib_rx_if_input =
		iflib_rx_mbuf_null = iflib_rxd_flush = 0;
}

#else
#define DBG_COUNTER_INC(name)
static void iflib_debug_reset(void) {}
#endif

#define IFLIB_DEBUG 0

static void iflib_tx_structures_free(if_ctx_t ctx);
static void iflib_rx_structures_free(if_ctx_t ctx);
static int iflib_queues_alloc(if_ctx_t ctx);
static int iflib_tx_credits_update(if_ctx_t ctx, iflib_txq_t txq);
static int iflib_rxd_avail(if_ctx_t ctx, iflib_rxq_t rxq, qidx_t cidx, qidx_t budget);
static int iflib_qset_structures_setup(if_ctx_t ctx);
static int iflib_msix_init(if_ctx_t ctx);
static int iflib_legacy_setup(if_ctx_t ctx, driver_filter_t filter, void *filterarg, int *rid, const char *str);
static void iflib_txq_check_drain(iflib_txq_t txq, int budget);
static uint32_t iflib_txq_can_drain(struct ifmp_ring *);
#ifdef ALTQ
static void iflib_altq_if_start(if_t ifp);
static int iflib_altq_if_transmit(if_t ifp, struct mbuf *m);
#endif
static int iflib_register(if_ctx_t);
static void iflib_init_locked(if_ctx_t ctx);
static void iflib_add_device_sysctl_pre(if_ctx_t ctx);
static void iflib_add_device_sysctl_post(if_ctx_t ctx);
static void iflib_ifmp_purge(iflib_txq_t txq);
static void _iflib_pre_assert(if_softc_ctx_t scctx);
static void iflib_if_init_locked(if_ctx_t ctx);
static void iflib_free_intr_mem(if_ctx_t ctx);
#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf * iflib_fixup_rx(struct mbuf *m);
#endif

NETDUMP_DEFINE(iflib);

#ifdef DEV_NETMAP
#include <sys/selinfo.h>
#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>

MODULE_DEPEND(iflib, netmap, 1, 1, 1);

static int netmap_fl_refill(iflib_rxq_t rxq, struct netmap_kring *kring, uint32_t nm_i, bool init);

/*
 * device-specific sysctl variables:
 *
 * iflib_crcstrip: 0: keep CRC in rx frames (default), 1: strip it.
 *	During regular operations the CRC is stripped, but on some
 *	hardware reception of frames not multiple of 64 is slower,
 *	so using crcstrip=0 helps in benchmarks.
 *
 * iflib_rx_miss, iflib_rx_miss_bufs:
 *	count packets that might be missed due to lost interrupts.
 */
SYSCTL_DECL(_dev_netmap);
/*
 * The xl driver by default strips CRCs and we do not override it.
 */

int iflib_crcstrip = 1;
SYSCTL_INT(_dev_netmap, OID_AUTO, iflib_crcstrip,
    CTLFLAG_RW, &iflib_crcstrip, 1, "strip CRC on rx frames");

int iflib_rx_miss, iflib_rx_miss_bufs;
SYSCTL_INT(_dev_netmap, OID_AUTO, iflib_rx_miss,
    CTLFLAG_RW, &iflib_rx_miss, 0, "potentially missed rx intr");
SYSCTL_INT(_dev_netmap, OID_AUTO, iflib_rx_miss_bufs,
    CTLFLAG_RW, &iflib_rx_miss_bufs, 0, "potentially missed rx intr bufs");

/*
 * Register/unregister. We are already under netmap lock.
 * Only called on the first register or the last unregister.
 */
static int
iflib_netmap_register(struct netmap_adapter *na, int onoff)
{
	struct ifnet *ifp = na->ifp;
	if_ctx_t ctx = ifp->if_softc;
	int status;

	CTX_LOCK(ctx);
	IFDI_INTR_DISABLE(ctx);

	/* Tell the stack that the interface is no longer active */
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	if (!CTX_IS_VF(ctx))
		IFDI_CRCSTRIP_SET(ctx, onoff, iflib_crcstrip);

	/* enable or disable flags and callbacks in na and ifp */
	if (onoff) {
		nm_set_native_flags(na);
	} else {
		nm_clear_native_flags(na);
	}
	iflib_stop(ctx);
	iflib_init_locked(ctx);
	IFDI_CRCSTRIP_SET(ctx, onoff, iflib_crcstrip); // XXX why twice ?
	status = ifp->if_drv_flags & IFF_DRV_RUNNING ? 0 : 1;
	if (status)
		nm_clear_native_flags(na);
	CTX_UNLOCK(ctx);
	return (status);
}

static int
netmap_fl_refill(iflib_rxq_t rxq, struct netmap_kring *kring, uint32_t nm_i, bool init)
{
	struct netmap_adapter *na = kring->na;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int head = kring->rhead;
	struct netmap_ring *ring = kring->ring;
	bus_dmamap_t *map;
	struct if_rxd_update iru;
	if_ctx_t ctx = rxq->ifr_ctx;
	iflib_fl_t fl = &rxq->ifr_fl[0];
	uint32_t refill_pidx, nic_i;
#if IFLIB_DEBUG_COUNTERS
	int rf_count = 0;
#endif

	if (nm_i == head && __predict_true(!init))
		return 0;
	iru_init(&iru, rxq, 0 /* flid */);
	map = fl->ifl_sds.ifsd_map;
	refill_pidx = netmap_idx_k2n(kring, nm_i);
	/*
	 * IMPORTANT: we must leave one free slot in the ring,
	 * so move head back by one unit
	 */
	head = nm_prev(head, lim);
	nic_i = UINT_MAX;
	DBG_COUNTER_INC(fl_refills);
	while (nm_i != head) {
#if IFLIB_DEBUG_COUNTERS
		if (++rf_count == 9)
			DBG_COUNTER_INC(fl_refills_large);
#endif
		for (int tmp_pidx = 0; tmp_pidx < IFLIB_MAX_RX_REFRESH && nm_i != head; tmp_pidx++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			void *addr = PNMB(na, slot, &fl->ifl_bus_addrs[tmp_pidx]);
			uint32_t nic_i_dma = refill_pidx;
			nic_i = netmap_idx_k2n(kring, nm_i);

			MPASS(tmp_pidx < IFLIB_MAX_RX_REFRESH);

			if (addr == NETMAP_BUF_BASE(na)) /* bad buf */
			        return netmap_ring_reinit(kring);

			fl->ifl_vm_addrs[tmp_pidx] = addr;
			if (__predict_false(init)) {
				netmap_load_map(na, fl->ifl_buf_tag,
				    map[nic_i], addr);
			} else if (slot->flags & NS_BUF_CHANGED) {
				/* buffer has changed, reload map */
				netmap_reload_map(na, fl->ifl_buf_tag,
				    map[nic_i], addr);
			}
			slot->flags &= ~NS_BUF_CHANGED;

			nm_i = nm_next(nm_i, lim);
			fl->ifl_rxd_idxs[tmp_pidx] = nic_i = nm_next(nic_i, lim);
			if (nm_i != head && tmp_pidx < IFLIB_MAX_RX_REFRESH-1)
				continue;

			iru.iru_pidx = refill_pidx;
			iru.iru_count = tmp_pidx+1;
			ctx->isc_rxd_refill(ctx->ifc_softc, &iru);
			refill_pidx = nic_i;
			for (int n = 0; n < iru.iru_count; n++) {
				bus_dmamap_sync(fl->ifl_buf_tag, map[nic_i_dma],
						BUS_DMASYNC_PREREAD);
				/* XXX - change this to not use the netmap func*/
				nic_i_dma = nm_next(nic_i_dma, lim);
			}
		}
	}
	kring->nr_hwcur = head;

	bus_dmamap_sync(fl->ifl_ifdi->idi_tag, fl->ifl_ifdi->idi_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	if (__predict_true(nic_i != UINT_MAX)) {
		ctx->isc_rxd_flush(ctx->ifc_softc, rxq->ifr_id, fl->ifl_id, nic_i);
		DBG_COUNTER_INC(rxd_flush);
	}
	return (0);
}

/*
 * Reconcile kernel and user view of the transmit ring.
 *
 * All information is in the kring.
 * Userspace wants to send packets up to the one before kring->rhead,
 * kernel knows kring->nr_hwcur is the first unsent packet.
 *
 * Here we push packets out (as many as possible), and possibly
 * reclaim buffers from previously completed transmission.
 *
 * The caller (netmap) guarantees that there is only one instance
 * running at any time. Any interference with other driver
 * methods should be handled by the individual drivers.
 */
static int
iflib_netmap_txsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct ifnet *ifp = na->ifp;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap kring */
	u_int nic_i;	/* index into the NIC ring */
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	struct if_pkt_info pi;

	/*
	 * interrupts on every tx packet are expensive so request
	 * them every half ring, or where NS_REPORT is set
	 */
	u_int report_frequency = kring->nkr_num_slots >> 1;
	/* device-specific */
	if_ctx_t ctx = ifp->if_softc;
	iflib_txq_t txq = &ctx->ifc_txqs[kring->ring_id];

	bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * First part: process new packets to send.
	 * nm_i is the current index in the netmap kring,
	 * nic_i is the corresponding index in the NIC ring.
	 *
	 * If we have packets to send (nm_i != head)
	 * iterate over the netmap ring, fetch length and update
	 * the corresponding slot in the NIC ring. Some drivers also
	 * need to update the buffer's physical address in the NIC slot
	 * even NS_BUF_CHANGED is not set (PNMB computes the addresses).
	 *
	 * The netmap_reload_map() calls is especially expensive,
	 * even when (as in this case) the tag is 0, so do only
	 * when the buffer has actually changed.
	 *
	 * If possible do not set the report/intr bit on all slots,
	 * but only a few times per ring or when NS_REPORT is set.
	 *
	 * Finally, on 10G and faster drivers, it might be useful
	 * to prefetch the next slot and txr entry.
	 */

	nm_i = kring->nr_hwcur;
	if (nm_i != head) {	/* we have new packets to send */
		pkt_info_zero(&pi);
		pi.ipi_segs = txq->ift_segs;
		pi.ipi_qsidx = kring->ring_id;
		nic_i = netmap_idx_k2n(kring, nm_i);

		__builtin_prefetch(&ring->slot[nm_i]);
		__builtin_prefetch(&txq->ift_sds.ifsd_m[nic_i]);
		__builtin_prefetch(&txq->ift_sds.ifsd_map[nic_i]);

		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			u_int len = slot->len;
			uint64_t paddr;
			void *addr = PNMB(na, slot, &paddr);
			int flags = (slot->flags & NS_REPORT ||
				nic_i == 0 || nic_i == report_frequency) ?
				IPI_TX_INTR : 0;

			/* device-specific */
			pi.ipi_len = len;
			pi.ipi_segs[0].ds_addr = paddr;
			pi.ipi_segs[0].ds_len = len;
			pi.ipi_nsegs = 1;
			pi.ipi_ndescs = 0;
			pi.ipi_pidx = nic_i;
			pi.ipi_flags = flags;

			/* Fill the slot in the NIC ring. */
			ctx->isc_txd_encap(ctx->ifc_softc, &pi);
			DBG_COUNTER_INC(tx_encap);

			/* prefetch for next round */
			__builtin_prefetch(&ring->slot[nm_i + 1]);
			__builtin_prefetch(&txq->ift_sds.ifsd_m[nic_i + 1]);
			__builtin_prefetch(&txq->ift_sds.ifsd_map[nic_i + 1]);

			NM_CHECK_ADDR_LEN(na, addr, len);

			if (slot->flags & NS_BUF_CHANGED) {
				/* buffer has changed, reload map */
				netmap_reload_map(na, txq->ift_buf_tag,
				    txq->ift_sds.ifsd_map[nic_i], addr);
			}
			/* make sure changes to the buffer are synced */
			bus_dmamap_sync(txq->ift_buf_tag,
			    txq->ift_sds.ifsd_map[nic_i],
			    BUS_DMASYNC_PREWRITE);

			slot->flags &= ~(NS_REPORT | NS_BUF_CHANGED);
			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		kring->nr_hwcur = nm_i;

		/* synchronize the NIC ring */
		bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* (re)start the tx unit up to slot nic_i (excluded) */
		ctx->isc_txd_flush(ctx->ifc_softc, txq->ift_id, nic_i);
	}

	/*
	 * Second part: reclaim buffers for completed transmissions.
	 *
	 * If there are unclaimed buffers, attempt to reclaim them.
	 * If none are reclaimed, and TX IRQs are not in use, do an initial
	 * minimal delay, then trigger the tx handler which will spin in the
	 * group task queue.
	 */
	if (kring->nr_hwtail != nm_prev(kring->nr_hwcur, lim)) {
		if (iflib_tx_credits_update(ctx, txq)) {
			/* some tx completed, increment avail */
			nic_i = txq->ift_cidx_processed;
			kring->nr_hwtail = nm_prev(netmap_idx_n2k(kring, nic_i), lim);
		}
	}
	if (!(ctx->ifc_flags & IFC_NETMAP_TX_IRQ))
		if (kring->nr_hwtail != nm_prev(kring->nr_hwcur, lim)) {
			callout_reset_on(&txq->ift_timer, hz < 2000 ? 1 : hz / 1000,
			    iflib_timer, txq, txq->ift_timer.c_cpu);
	}
	return (0);
}

/*
 * Reconcile kernel and user view of the receive ring.
 * Same as for the txsync, this routine must be efficient.
 * The caller guarantees a single invocations, but races against
 * the rest of the driver should be handled here.
 *
 * On call, kring->rhead is the first packet that userspace wants
 * to keep, and kring->rcur is the wakeup point.
 * The kernel has previously reported packets up to kring->rtail.
 *
 * If (flags & NAF_FORCE_READ) also check for incoming packets irrespective
 * of whether or not we received an interrupt.
 */
static int
iflib_netmap_rxsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_ring *ring = kring->ring;
	iflib_fl_t fl;
	uint32_t nm_i;	/* index into the netmap ring */
	uint32_t nic_i;	/* index into the NIC ring */
	u_int i, n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	int force_update = (flags & NAF_FORCE_READ) || kring->nr_kflags & NKR_PENDINTR;
	struct if_rxd_info ri;

	struct ifnet *ifp = na->ifp;
	if_ctx_t ctx = ifp->if_softc;
	iflib_rxq_t rxq = &ctx->ifc_rxqs[kring->ring_id];
	if (head > lim)
		return netmap_ring_reinit(kring);

	/*
	 * XXX netmap_fl_refill() only ever (re)fills free list 0 so far.
	 */

	for (i = 0, fl = rxq->ifr_fl; i < rxq->ifr_nfl; i++, fl++) {
		bus_dmamap_sync(fl->ifl_ifdi->idi_tag, fl->ifl_ifdi->idi_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	}

	/*
	 * First part: import newly received packets.
	 *
	 * nm_i is the index of the next free slot in the netmap ring,
	 * nic_i is the index of the next received packet in the NIC ring,
	 * and they may differ in case if_init() has been called while
	 * in netmap mode. For the receive ring we have
	 *
	 *	nic_i = rxr->next_check;
	 *	nm_i = kring->nr_hwtail (previous)
	 * and
	 *	nm_i == (nic_i + kring->nkr_hwofs) % ring_size
	 *
	 * rxr->next_check is set to 0 on a ring reinit
	 */
	if (netmap_no_pendintr || force_update) {
		int crclen = iflib_crcstrip ? 0 : 4;
		int error, avail;

		for (i = 0; i < rxq->ifr_nfl; i++) {
			fl = &rxq->ifr_fl[i];
			nic_i = fl->ifl_cidx;
			nm_i = netmap_idx_n2k(kring, nic_i);
			avail = ctx->isc_rxd_available(ctx->ifc_softc,
			    rxq->ifr_id, nic_i, USHRT_MAX);
			for (n = 0; avail > 0; n++, avail--) {
				rxd_info_zero(&ri);
				ri.iri_frags = rxq->ifr_frags;
				ri.iri_qsidx = kring->ring_id;
				ri.iri_ifp = ctx->ifc_ifp;
				ri.iri_cidx = nic_i;

				error = ctx->isc_rxd_pkt_get(ctx->ifc_softc, &ri);
				ring->slot[nm_i].len = error ? 0 : ri.iri_len - crclen;
				ring->slot[nm_i].flags = 0;
				bus_dmamap_sync(fl->ifl_buf_tag,
				    fl->ifl_sds.ifsd_map[nic_i], BUS_DMASYNC_POSTREAD);
				nm_i = nm_next(nm_i, lim);
				nic_i = nm_next(nic_i, lim);
			}
			if (n) { /* update the state variables */
				if (netmap_no_pendintr && !force_update) {
					/* diagnostics */
					iflib_rx_miss ++;
					iflib_rx_miss_bufs += n;
				}
				fl->ifl_cidx = nic_i;
				kring->nr_hwtail = nm_i;
			}
			kring->nr_kflags &= ~NKR_PENDINTR;
		}
	}
	/*
	 * Second part: skip past packets that userspace has released.
	 * (kring->nr_hwcur to head excluded),
	 * and make the buffers available for reception.
	 * As usual nm_i is the index in the netmap ring,
	 * nic_i is the index in the NIC ring, and
	 * nm_i == (nic_i + kring->nkr_hwofs) % ring_size
	 */
	/* XXX not sure how this will work with multiple free lists */
	nm_i = kring->nr_hwcur;

	return (netmap_fl_refill(rxq, kring, nm_i, false));
}

static void
iflib_netmap_intr(struct netmap_adapter *na, int onoff)
{
	struct ifnet *ifp = na->ifp;
	if_ctx_t ctx = ifp->if_softc;

	CTX_LOCK(ctx);
	if (onoff) {
		IFDI_INTR_ENABLE(ctx);
	} else {
		IFDI_INTR_DISABLE(ctx);
	}
	CTX_UNLOCK(ctx);
}


static int
iflib_netmap_attach(if_ctx_t ctx)
{
	struct netmap_adapter na;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;

	bzero(&na, sizeof(na));

	na.ifp = ctx->ifc_ifp;
	na.na_flags = NAF_BDG_MAYSLEEP;
	MPASS(ctx->ifc_softc_ctx.isc_ntxqsets);
	MPASS(ctx->ifc_softc_ctx.isc_nrxqsets);

	na.num_tx_desc = scctx->isc_ntxd[0];
	na.num_rx_desc = scctx->isc_nrxd[0];
	na.nm_txsync = iflib_netmap_txsync;
	na.nm_rxsync = iflib_netmap_rxsync;
	na.nm_register = iflib_netmap_register;
	na.nm_intr = iflib_netmap_intr;
	na.num_tx_rings = ctx->ifc_softc_ctx.isc_ntxqsets;
	na.num_rx_rings = ctx->ifc_softc_ctx.isc_nrxqsets;
	return (netmap_attach(&na));
}

static void
iflib_netmap_txq_init(if_ctx_t ctx, iflib_txq_t txq)
{
	struct netmap_adapter *na = NA(ctx->ifc_ifp);
	struct netmap_slot *slot;

	slot = netmap_reset(na, NR_TX, txq->ift_id, 0);
	if (slot == NULL)
		return;
	for (int i = 0; i < ctx->ifc_softc_ctx.isc_ntxd[0]; i++) {

		/*
		 * In netmap mode, set the map for the packet buffer.
		 * NOTE: Some drivers (not this one) also need to set
		 * the physical buffer address in the NIC ring.
		 * netmap_idx_n2k() maps a nic index, i, into the corresponding
		 * netmap slot index, si
		 */
		int si = netmap_idx_n2k(na->tx_rings[txq->ift_id], i);
		netmap_load_map(na, txq->ift_buf_tag, txq->ift_sds.ifsd_map[i],
		    NMB(na, slot + si));
	}
}

static void
iflib_netmap_rxq_init(if_ctx_t ctx, iflib_rxq_t rxq)
{
	struct netmap_adapter *na = NA(ctx->ifc_ifp);
	struct netmap_kring *kring = na->rx_rings[rxq->ifr_id];
	struct netmap_slot *slot;
	uint32_t nm_i;

	slot = netmap_reset(na, NR_RX, rxq->ifr_id, 0);
	if (slot == NULL)
		return;
	nm_i = netmap_idx_n2k(kring, 0);
	netmap_fl_refill(rxq, kring, nm_i, true);
}

static void
iflib_netmap_timer_adjust(if_ctx_t ctx, iflib_txq_t txq, uint32_t *reset_on)
{
	struct netmap_kring *kring;
	uint16_t txqid;

	txqid = txq->ift_id;
	kring = NA(ctx->ifc_ifp)->tx_rings[txqid];

	if (kring->nr_hwcur != nm_next(kring->nr_hwtail, kring->nkr_num_slots - 1)) {
		bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
		    BUS_DMASYNC_POSTREAD);
		if (ctx->isc_txd_credits_update(ctx->ifc_softc, txqid, false))
			netmap_tx_irq(ctx->ifc_ifp, txqid);
		if (!(ctx->ifc_flags & IFC_NETMAP_TX_IRQ)) {
			if (hz < 2000)
				*reset_on = 1;
			else
				*reset_on = hz / 1000;
		}
	}
}

#define iflib_netmap_detach(ifp) netmap_detach(ifp)

#else
#define iflib_netmap_txq_init(ctx, txq)
#define iflib_netmap_rxq_init(ctx, rxq)
#define iflib_netmap_detach(ifp)

#define iflib_netmap_attach(ctx) (0)
#define netmap_rx_irq(ifp, qid, budget) (0)
#define netmap_tx_irq(ifp, qid) do {} while (0)
#define iflib_netmap_timer_adjust(ctx, txq, reset_on)

#endif

#if defined(__i386__) || defined(__amd64__)
static __inline void
prefetch(void *x)
{
	__asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}
static __inline void
prefetch2cachelines(void *x)
{
	__asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
#if (CACHE_LINE_SIZE < 128)
	__asm volatile("prefetcht0 %0" :: "m" (*(((unsigned long *)x)+CACHE_LINE_SIZE/(sizeof(unsigned long)))));
#endif
}
#else
#define prefetch(x)
#define prefetch2cachelines(x)
#endif

static void
iflib_gen_mac(if_ctx_t ctx)
{
	struct thread *td;
	MD5_CTX mdctx;
	char uuid[HOSTUUIDLEN+1];
	char buf[HOSTUUIDLEN+16];
	uint8_t *mac;
	unsigned char digest[16];

	td = curthread;
	mac = ctx->ifc_mac;
	uuid[HOSTUUIDLEN] = 0;
	bcopy(td->td_ucred->cr_prison->pr_hostuuid, uuid, HOSTUUIDLEN);
	snprintf(buf, HOSTUUIDLEN+16, "%s-%s", uuid, device_get_nameunit(ctx->ifc_dev));
	/*
	 * Generate a pseudo-random, deterministic MAC
	 * address based on the UUID and unit number.
	 * The FreeBSD Foundation OUI of 58-9C-FC is used.
	 */
	MD5Init(&mdctx);
	MD5Update(&mdctx, buf, strlen(buf));
	MD5Final(digest, &mdctx);

	mac[0] = 0x58;
	mac[1] = 0x9C;
	mac[2] = 0xFC;
	mac[3] = digest[0];
	mac[4] = digest[1];
	mac[5] = digest[2];
}

static void
iru_init(if_rxd_update_t iru, iflib_rxq_t rxq, uint8_t flid)
{
	iflib_fl_t fl;

	fl = &rxq->ifr_fl[flid];
	iru->iru_paddrs = fl->ifl_bus_addrs;
	iru->iru_vaddrs = &fl->ifl_vm_addrs[0];
	iru->iru_idxs = fl->ifl_rxd_idxs;
	iru->iru_qsidx = rxq->ifr_id;
	iru->iru_buf_size = fl->ifl_buf_size;
	iru->iru_flidx = fl->ifl_id;
}

static void
_iflib_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	if (err)
		return;
	*(bus_addr_t *) arg = segs[0].ds_addr;
}

int
iflib_dma_alloc_align(if_ctx_t ctx, int size, int align, iflib_dma_info_t dma, int mapflags)
{
	int err;
	device_t dev = ctx->ifc_dev;

	err = bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
				align, 0,		/* alignment, bounds */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				size,			/* maxsize */
				1,			/* nsegments */
				size,			/* maxsegsize */
				BUS_DMA_ALLOCNOW,	/* flags */
				NULL,			/* lockfunc */
				NULL,			/* lockarg */
				&dma->idi_tag);
	if (err) {
		device_printf(dev,
		    "%s: bus_dma_tag_create failed: %d\n",
		    __func__, err);
		goto fail_0;
	}

	err = bus_dmamem_alloc(dma->idi_tag, (void**) &dma->idi_vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT | BUS_DMA_ZERO, &dma->idi_map);
	if (err) {
		device_printf(dev,
		    "%s: bus_dmamem_alloc(%ju) failed: %d\n",
		    __func__, (uintmax_t)size, err);
		goto fail_1;
	}

	dma->idi_paddr = IF_BAD_DMA;
	err = bus_dmamap_load(dma->idi_tag, dma->idi_map, dma->idi_vaddr,
	    size, _iflib_dmamap_cb, &dma->idi_paddr, mapflags | BUS_DMA_NOWAIT);
	if (err || dma->idi_paddr == IF_BAD_DMA) {
		device_printf(dev,
		    "%s: bus_dmamap_load failed: %d\n",
		    __func__, err);
		goto fail_2;
	}

	dma->idi_size = size;
	return (0);

fail_2:
	bus_dmamem_free(dma->idi_tag, dma->idi_vaddr, dma->idi_map);
fail_1:
	bus_dma_tag_destroy(dma->idi_tag);
fail_0:
	dma->idi_tag = NULL;

	return (err);
}

int
iflib_dma_alloc(if_ctx_t ctx, int size, iflib_dma_info_t dma, int mapflags)
{
	if_shared_ctx_t sctx = ctx->ifc_sctx;

	KASSERT(sctx->isc_q_align != 0, ("alignment value not initialized"));

	return (iflib_dma_alloc_align(ctx, size, sctx->isc_q_align, dma, mapflags));
}

int
iflib_dma_alloc_multi(if_ctx_t ctx, int *sizes, iflib_dma_info_t *dmalist, int mapflags, int count)
{
	int i, err;
	iflib_dma_info_t *dmaiter;

	dmaiter = dmalist;
	for (i = 0; i < count; i++, dmaiter++) {
		if ((err = iflib_dma_alloc(ctx, sizes[i], *dmaiter, mapflags)) != 0)
			break;
	}
	if (err)
		iflib_dma_free_multi(dmalist, i);
	return (err);
}

void
iflib_dma_free(iflib_dma_info_t dma)
{
	if (dma->idi_tag == NULL)
		return;
	if (dma->idi_paddr != IF_BAD_DMA) {
		bus_dmamap_sync(dma->idi_tag, dma->idi_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->idi_tag, dma->idi_map);
		dma->idi_paddr = IF_BAD_DMA;
	}
	if (dma->idi_vaddr != NULL) {
		bus_dmamem_free(dma->idi_tag, dma->idi_vaddr, dma->idi_map);
		dma->idi_vaddr = NULL;
	}
	bus_dma_tag_destroy(dma->idi_tag);
	dma->idi_tag = NULL;
}

void
iflib_dma_free_multi(iflib_dma_info_t *dmalist, int count)
{
	int i;
	iflib_dma_info_t *dmaiter = dmalist;

	for (i = 0; i < count; i++, dmaiter++)
		iflib_dma_free(*dmaiter);
}

#ifdef EARLY_AP_STARTUP
static const int iflib_started = 1;
#else
/*
 * We used to abuse the smp_started flag to decide if the queues have been
 * fully initialized (by late taskqgroup_adjust() calls in a SYSINIT()).
 * That gave bad races, since the SYSINIT() runs strictly after smp_started
 * is set.  Run a SYSINIT() strictly after that to just set a usable
 * completion flag.
 */

static int iflib_started;

static void
iflib_record_started(void *arg)
{
	iflib_started = 1;
}

SYSINIT(iflib_record_started, SI_SUB_SMP + 1, SI_ORDER_FIRST,
	iflib_record_started, NULL);
#endif

static int
iflib_fast_intr(void *arg)
{
	iflib_filter_info_t info = arg;
	struct grouptask *gtask = info->ifi_task;
	int result;

	if (!iflib_started)
		return (FILTER_STRAY);

	DBG_COUNTER_INC(fast_intrs);
	if (info->ifi_filter != NULL) {
		result = info->ifi_filter(info->ifi_filter_arg);
		if ((result & FILTER_SCHEDULE_THREAD) == 0)
			return (result);
	}

	GROUPTASK_ENQUEUE(gtask);
	return (FILTER_HANDLED);
}

static int
iflib_fast_intr_rxtx(void *arg)
{
	iflib_filter_info_t info = arg;
	struct grouptask *gtask = info->ifi_task;
	if_ctx_t ctx;
	iflib_rxq_t rxq = (iflib_rxq_t)info->ifi_ctx;
	iflib_txq_t txq;
	void *sc;
	int i, cidx, result;
	qidx_t txqid;

	if (!iflib_started)
		return (FILTER_STRAY);

	DBG_COUNTER_INC(fast_intrs);
	if (info->ifi_filter != NULL) {
		result = info->ifi_filter(info->ifi_filter_arg);
		if ((result & FILTER_SCHEDULE_THREAD) == 0)
			return (result);
	}

	ctx = rxq->ifr_ctx;
	sc = ctx->ifc_softc;
	MPASS(rxq->ifr_ntxqirq);
	for (i = 0; i < rxq->ifr_ntxqirq; i++) {
		txqid = rxq->ifr_txqid[i];
		txq = &ctx->ifc_txqs[txqid];
		bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
		    BUS_DMASYNC_POSTREAD);
		if (!ctx->isc_txd_credits_update(sc, txqid, false)) {
			IFDI_TX_QUEUE_INTR_ENABLE(ctx, txqid);
			continue;
		}
		GROUPTASK_ENQUEUE(&txq->ift_task);
	}
	if (ctx->ifc_sctx->isc_flags & IFLIB_HAS_RXCQ)
		cidx = rxq->ifr_cq_cidx;
	else
		cidx = rxq->ifr_fl[0].ifl_cidx;
	if (iflib_rxd_avail(ctx, rxq, cidx, 1))
		GROUPTASK_ENQUEUE(gtask);
	else {
		IFDI_RX_QUEUE_INTR_ENABLE(ctx, rxq->ifr_id);
		DBG_COUNTER_INC(rx_intr_enables);
	}
	return (FILTER_HANDLED);
}


static int
iflib_fast_intr_ctx(void *arg)
{
	iflib_filter_info_t info = arg;
	struct grouptask *gtask = info->ifi_task;
	int result;

	if (!iflib_started)
		return (FILTER_STRAY);

	DBG_COUNTER_INC(fast_intrs);
	if (info->ifi_filter != NULL) {
		result = info->ifi_filter(info->ifi_filter_arg);
		if ((result & FILTER_SCHEDULE_THREAD) == 0)
			return (result);
	}

	GROUPTASK_ENQUEUE(gtask);
	return (FILTER_HANDLED);
}

static int
_iflib_irq_alloc(if_ctx_t ctx, if_irq_t irq, int rid,
		 driver_filter_t filter, driver_intr_t handler, void *arg,
		 const char *name)
{
	int rc, flags;
	struct resource *res;
	void *tag = NULL;
	device_t dev = ctx->ifc_dev;

	flags = RF_ACTIVE;
	if (ctx->ifc_flags & IFC_LEGACY)
		flags |= RF_SHAREABLE;
	MPASS(rid < 512);
	irq->ii_rid = rid;
	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &irq->ii_rid, flags);
	if (res == NULL) {
		device_printf(dev,
		    "failed to allocate IRQ for rid %d, name %s.\n", rid, name);
		return (ENOMEM);
	}
	irq->ii_res = res;
	KASSERT(filter == NULL || handler == NULL, ("filter and handler can't both be non-NULL"));
	rc = bus_setup_intr(dev, res, INTR_MPSAFE | INTR_TYPE_NET,
						filter, handler, arg, &tag);
	if (rc != 0) {
		device_printf(dev,
		    "failed to setup interrupt for rid %d, name %s: %d\n",
					  rid, name ? name : "unknown", rc);
		return (rc);
	} else if (name)
		bus_describe_intr(dev, res, tag, "%s", name);

	irq->ii_tag = tag;
	return (0);
}


/*********************************************************************
 *
 *  Allocate DMA resources for TX buffers as well as memory for the TX
 *  mbuf map.  TX DMA maps (non-TSO/TSO) and TX mbuf map are kept in a
 *  iflib_sw_tx_desc_array structure, storing all the information that
 *  is needed to transmit a packet on the wire.  This is called only
 *  once at attach, setup is done every reset.
 *
 **********************************************************************/
static int
iflib_txsd_alloc(iflib_txq_t txq)
{
	if_ctx_t ctx = txq->ift_ctx;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	device_t dev = ctx->ifc_dev;
	bus_size_t tsomaxsize;
	int err, nsegments, ntsosegments;
	bool tso;

	nsegments = scctx->isc_tx_nsegments;
	ntsosegments = scctx->isc_tx_tso_segments_max;
	tsomaxsize = scctx->isc_tx_tso_size_max;
	if (if_getcapabilities(ctx->ifc_ifp) & IFCAP_VLAN_MTU)
		tsomaxsize += sizeof(struct ether_vlan_header);
	MPASS(scctx->isc_ntxd[0] > 0);
	MPASS(scctx->isc_ntxd[txq->ift_br_offset] > 0);
	MPASS(nsegments > 0);
	if (if_getcapabilities(ctx->ifc_ifp) & IFCAP_TSO) {
		MPASS(ntsosegments > 0);
		MPASS(sctx->isc_tso_maxsize >= tsomaxsize);
	}

	/*
	 * Set up DMA tags for TX buffers.
	 */
	if ((err = bus_dma_tag_create(bus_get_dma_tag(dev),
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       sctx->isc_tx_maxsize,		/* maxsize */
			       nsegments,	/* nsegments */
			       sctx->isc_tx_maxsegsize,	/* maxsegsize */
			       0,			/* flags */
			       NULL,			/* lockfunc */
			       NULL,			/* lockfuncarg */
			       &txq->ift_buf_tag))) {
		device_printf(dev,"Unable to allocate TX DMA tag: %d\n", err);
		device_printf(dev,"maxsize: %ju nsegments: %d maxsegsize: %ju\n",
		    (uintmax_t)sctx->isc_tx_maxsize, nsegments, (uintmax_t)sctx->isc_tx_maxsegsize);
		goto fail;
	}
	tso = (if_getcapabilities(ctx->ifc_ifp) & IFCAP_TSO) != 0;
	if (tso && (err = bus_dma_tag_create(bus_get_dma_tag(dev),
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       tsomaxsize,		/* maxsize */
			       ntsosegments,	/* nsegments */
			       sctx->isc_tso_maxsegsize,/* maxsegsize */
			       0,			/* flags */
			       NULL,			/* lockfunc */
			       NULL,			/* lockfuncarg */
			       &txq->ift_tso_buf_tag))) {
		device_printf(dev, "Unable to allocate TSO TX DMA tag: %d\n",
		    err);
		goto fail;
	}

	/* Allocate memory for the TX mbuf map. */
	if (!(txq->ift_sds.ifsd_m =
	    (struct mbuf **) malloc(sizeof(struct mbuf *) *
	    scctx->isc_ntxd[txq->ift_br_offset], M_IFLIB, M_NOWAIT | M_ZERO))) {
		device_printf(dev, "Unable to allocate TX mbuf map memory\n");
		err = ENOMEM;
		goto fail;
	}

	/*
	 * Create the DMA maps for TX buffers.
	 */
	if ((txq->ift_sds.ifsd_map = (bus_dmamap_t *)malloc(
	    sizeof(bus_dmamap_t) * scctx->isc_ntxd[txq->ift_br_offset],
	    M_IFLIB, M_NOWAIT | M_ZERO)) == NULL) {
		device_printf(dev,
		    "Unable to allocate TX buffer DMA map memory\n");
		err = ENOMEM;
		goto fail;
	}
	if (tso && (txq->ift_sds.ifsd_tso_map = (bus_dmamap_t *)malloc(
	    sizeof(bus_dmamap_t) * scctx->isc_ntxd[txq->ift_br_offset],
	    M_IFLIB, M_NOWAIT | M_ZERO)) == NULL) {
		device_printf(dev,
		    "Unable to allocate TSO TX buffer map memory\n");
		err = ENOMEM;
		goto fail;
	}
	for (int i = 0; i < scctx->isc_ntxd[txq->ift_br_offset]; i++) {
		err = bus_dmamap_create(txq->ift_buf_tag, 0,
		    &txq->ift_sds.ifsd_map[i]);
		if (err != 0) {
			device_printf(dev, "Unable to create TX DMA map\n");
			goto fail;
		}
		if (!tso)
			continue;
		err = bus_dmamap_create(txq->ift_tso_buf_tag, 0,
		    &txq->ift_sds.ifsd_tso_map[i]);
		if (err != 0) {
			device_printf(dev, "Unable to create TSO TX DMA map\n");
			goto fail;
		}
	}
	return (0);
fail:
	/* We free all, it handles case where we are in the middle */
	iflib_tx_structures_free(ctx);
	return (err);
}

static void
iflib_txsd_destroy(if_ctx_t ctx, iflib_txq_t txq, int i)
{
	bus_dmamap_t map;

	map = NULL;
	if (txq->ift_sds.ifsd_map != NULL)
		map = txq->ift_sds.ifsd_map[i];
	if (map != NULL) {
		bus_dmamap_sync(txq->ift_buf_tag, map, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txq->ift_buf_tag, map);
		bus_dmamap_destroy(txq->ift_buf_tag, map);
		txq->ift_sds.ifsd_map[i] = NULL;
	}

	map = NULL;
	if (txq->ift_sds.ifsd_tso_map != NULL)
		map = txq->ift_sds.ifsd_tso_map[i];
	if (map != NULL) {
		bus_dmamap_sync(txq->ift_tso_buf_tag, map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txq->ift_tso_buf_tag, map);
		bus_dmamap_destroy(txq->ift_tso_buf_tag, map);
		txq->ift_sds.ifsd_tso_map[i] = NULL;
	}
}

static void
iflib_txq_destroy(iflib_txq_t txq)
{
	if_ctx_t ctx = txq->ift_ctx;

	for (int i = 0; i < txq->ift_size; i++)
		iflib_txsd_destroy(ctx, txq, i);
	if (txq->ift_sds.ifsd_map != NULL) {
		free(txq->ift_sds.ifsd_map, M_IFLIB);
		txq->ift_sds.ifsd_map = NULL;
	}
	if (txq->ift_sds.ifsd_tso_map != NULL) {
		free(txq->ift_sds.ifsd_tso_map, M_IFLIB);
		txq->ift_sds.ifsd_tso_map = NULL;
	}
	if (txq->ift_sds.ifsd_m != NULL) {
		free(txq->ift_sds.ifsd_m, M_IFLIB);
		txq->ift_sds.ifsd_m = NULL;
	}
	if (txq->ift_buf_tag != NULL) {
		bus_dma_tag_destroy(txq->ift_buf_tag);
		txq->ift_buf_tag = NULL;
	}
	if (txq->ift_tso_buf_tag != NULL) {
		bus_dma_tag_destroy(txq->ift_tso_buf_tag);
		txq->ift_tso_buf_tag = NULL;
	}
}

static void
iflib_txsd_free(if_ctx_t ctx, iflib_txq_t txq, int i)
{
	struct mbuf **mp;

	mp = &txq->ift_sds.ifsd_m[i];
	if (*mp == NULL)
		return;

	if (txq->ift_sds.ifsd_map != NULL) {
		bus_dmamap_sync(txq->ift_buf_tag,
		    txq->ift_sds.ifsd_map[i], BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txq->ift_buf_tag, txq->ift_sds.ifsd_map[i]);
	}
	if (txq->ift_sds.ifsd_tso_map != NULL) {
		bus_dmamap_sync(txq->ift_tso_buf_tag,
		    txq->ift_sds.ifsd_tso_map[i], BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txq->ift_tso_buf_tag,
		    txq->ift_sds.ifsd_tso_map[i]);
	}
	m_free(*mp);
	DBG_COUNTER_INC(tx_frees);
	*mp = NULL;
}

static int
iflib_txq_setup(iflib_txq_t txq)
{
	if_ctx_t ctx = txq->ift_ctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	iflib_dma_info_t di;
	int i;

	/* Set number of descriptors available */
	txq->ift_qstatus = IFLIB_QUEUE_IDLE;
	/* XXX make configurable */
	txq->ift_update_freq = IFLIB_DEFAULT_TX_UPDATE_FREQ;

	/* Reset indices */
	txq->ift_cidx_processed = 0;
	txq->ift_pidx = txq->ift_cidx = txq->ift_npending = 0;
	txq->ift_size = scctx->isc_ntxd[txq->ift_br_offset];

	for (i = 0, di = txq->ift_ifdi; i < sctx->isc_ntxqs; i++, di++)
		bzero((void *)di->idi_vaddr, di->idi_size);

	IFDI_TXQ_SETUP(ctx, txq->ift_id);
	for (i = 0, di = txq->ift_ifdi; i < sctx->isc_ntxqs; i++, di++)
		bus_dmamap_sync(di->idi_tag, di->idi_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	return (0);
}

/*********************************************************************
 *
 *  Allocate DMA resources for RX buffers as well as memory for the RX
 *  mbuf map, direct RX cluster pointer map and RX cluster bus address
 *  map.  RX DMA map, RX mbuf map, direct RX cluster pointer map and
 *  RX cluster map are kept in a iflib_sw_rx_desc_array structure.
 *  Since we use use one entry in iflib_sw_rx_desc_array per received
 *  packet, the maximum number of entries we'll need is equal to the
 *  number of hardware receive descriptors that we've allocated.
 *
 **********************************************************************/
static int
iflib_rxsd_alloc(iflib_rxq_t rxq)
{
	if_ctx_t ctx = rxq->ifr_ctx;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	device_t dev = ctx->ifc_dev;
	iflib_fl_t fl;
	int			err;

	MPASS(scctx->isc_nrxd[0] > 0);
	MPASS(scctx->isc_nrxd[rxq->ifr_fl_offset] > 0);

	fl = rxq->ifr_fl;
	for (int i = 0; i <  rxq->ifr_nfl; i++, fl++) {
		fl->ifl_size = scctx->isc_nrxd[rxq->ifr_fl_offset]; /* this isn't necessarily the same */
		/* Set up DMA tag for RX buffers. */
		err = bus_dma_tag_create(bus_get_dma_tag(dev), /* parent */
					 1, 0,			/* alignment, bounds */
					 BUS_SPACE_MAXADDR,	/* lowaddr */
					 BUS_SPACE_MAXADDR,	/* highaddr */
					 NULL, NULL,		/* filter, filterarg */
					 sctx->isc_rx_maxsize,	/* maxsize */
					 sctx->isc_rx_nsegments,	/* nsegments */
					 sctx->isc_rx_maxsegsize,	/* maxsegsize */
					 0,			/* flags */
					 NULL,			/* lockfunc */
					 NULL,			/* lockarg */
					 &fl->ifl_buf_tag);
		if (err) {
			device_printf(dev,
			    "Unable to allocate RX DMA tag: %d\n", err);
			goto fail;
		}

		/* Allocate memory for the RX mbuf map. */
		if (!(fl->ifl_sds.ifsd_m =
		      (struct mbuf **) malloc(sizeof(struct mbuf *) *
					      scctx->isc_nrxd[rxq->ifr_fl_offset], M_IFLIB, M_NOWAIT | M_ZERO))) {
			device_printf(dev,
			    "Unable to allocate RX mbuf map memory\n");
			err = ENOMEM;
			goto fail;
		}

		/* Allocate memory for the direct RX cluster pointer map. */
		if (!(fl->ifl_sds.ifsd_cl =
		      (caddr_t *) malloc(sizeof(caddr_t) *
					      scctx->isc_nrxd[rxq->ifr_fl_offset], M_IFLIB, M_NOWAIT | M_ZERO))) {
			device_printf(dev,
			    "Unable to allocate RX cluster map memory\n");
			err = ENOMEM;
			goto fail;
		}

		/* Allocate memory for the RX cluster bus address map. */
		if (!(fl->ifl_sds.ifsd_ba =
		      (bus_addr_t *) malloc(sizeof(bus_addr_t) *
					      scctx->isc_nrxd[rxq->ifr_fl_offset], M_IFLIB, M_NOWAIT | M_ZERO))) {
			device_printf(dev,
			    "Unable to allocate RX bus address map memory\n");
			err = ENOMEM;
			goto fail;
		}

		/*
		 * Create the DMA maps for RX buffers.
		 */
		if (!(fl->ifl_sds.ifsd_map =
		      (bus_dmamap_t *) malloc(sizeof(bus_dmamap_t) * scctx->isc_nrxd[rxq->ifr_fl_offset], M_IFLIB, M_NOWAIT | M_ZERO))) {
			device_printf(dev,
			    "Unable to allocate RX buffer DMA map memory\n");
			err = ENOMEM;
			goto fail;
		}
		for (int i = 0; i < scctx->isc_nrxd[rxq->ifr_fl_offset]; i++) {
			err = bus_dmamap_create(fl->ifl_buf_tag, 0,
			    &fl->ifl_sds.ifsd_map[i]);
			if (err != 0) {
				device_printf(dev, "Unable to create RX buffer DMA map\n");
				goto fail;
			}
		}
	}
	return (0);

fail:
	iflib_rx_structures_free(ctx);
	return (err);
}


/*
 * Internal service routines
 */

struct rxq_refill_cb_arg {
	int               error;
	bus_dma_segment_t seg;
	int               nseg;
};

static void
_rxq_refill_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct rxq_refill_cb_arg *cb_arg = arg;

	cb_arg->error = error;
	cb_arg->seg = segs[0];
	cb_arg->nseg = nseg;
}

/**
 *	rxq_refill - refill an rxq  free-buffer list
 *	@ctx: the iflib context
 *	@rxq: the free-list to refill
 *	@n: the number of new buffers to allocate
 *
 *	(Re)populate an rxq free-buffer list with up to @n new packet buffers.
 *	The caller must assure that @n does not exceed the queue's capacity.
 */
static void
_iflib_fl_refill(if_ctx_t ctx, iflib_fl_t fl, int count)
{
	struct if_rxd_update iru;
	struct rxq_refill_cb_arg cb_arg;
	struct mbuf *m;
	caddr_t cl, *sd_cl;
	struct mbuf **sd_m;
	bus_dmamap_t *sd_map;
	bus_addr_t bus_addr, *sd_ba;
	int err, frag_idx, i, idx, n, pidx;
	qidx_t credits;

	sd_m = fl->ifl_sds.ifsd_m;
	sd_map = fl->ifl_sds.ifsd_map;
	sd_cl = fl->ifl_sds.ifsd_cl;
	sd_ba = fl->ifl_sds.ifsd_ba;
	pidx = fl->ifl_pidx;
	idx = pidx;
	frag_idx = fl->ifl_fragidx;
	credits = fl->ifl_credits;

	i = 0;
	n = count;
	MPASS(n > 0);
	MPASS(credits + n <= fl->ifl_size);

	if (pidx < fl->ifl_cidx)
		MPASS(pidx + n <= fl->ifl_cidx);
	if (pidx == fl->ifl_cidx && (credits < fl->ifl_size))
		MPASS(fl->ifl_gen == 0);
	if (pidx > fl->ifl_cidx)
		MPASS(n <= fl->ifl_size - pidx + fl->ifl_cidx);

	DBG_COUNTER_INC(fl_refills);
	if (n > 8)
		DBG_COUNTER_INC(fl_refills_large);
	iru_init(&iru, fl->ifl_rxq, fl->ifl_id);
	while (n--) {
		/*
		 * We allocate an uninitialized mbuf + cluster, mbuf is
		 * initialized after rx.
		 *
		 * If the cluster is still set then we know a minimum sized packet was received
		 */
		bit_ffc_at(fl->ifl_rx_bitmap, frag_idx, fl->ifl_size,
		    &frag_idx);
		if (frag_idx < 0)
			bit_ffc(fl->ifl_rx_bitmap, fl->ifl_size, &frag_idx);
		MPASS(frag_idx >= 0);
		if ((cl = sd_cl[frag_idx]) == NULL) {
			if ((cl = m_cljget(NULL, M_NOWAIT, fl->ifl_buf_size)) == NULL)
				break;

			cb_arg.error = 0;
			MPASS(sd_map != NULL);
			err = bus_dmamap_load(fl->ifl_buf_tag, sd_map[frag_idx],
			    cl, fl->ifl_buf_size, _rxq_refill_cb, &cb_arg,
			    BUS_DMA_NOWAIT);
			if (err != 0 || cb_arg.error) {
				/*
				 * !zone_pack ?
				 */
				if (fl->ifl_zone == zone_pack)
					uma_zfree(fl->ifl_zone, cl);
				break;
			}

			sd_ba[frag_idx] =  bus_addr = cb_arg.seg.ds_addr;
			sd_cl[frag_idx] = cl;
#if MEMORY_LOGGING
			fl->ifl_cl_enqueued++;
#endif
		} else {
			bus_addr = sd_ba[frag_idx];
		}
		bus_dmamap_sync(fl->ifl_buf_tag, sd_map[frag_idx],
		    BUS_DMASYNC_PREREAD);

		MPASS(sd_m[frag_idx] == NULL);
		if ((m = m_gethdr(M_NOWAIT, MT_NOINIT)) == NULL) {
			break;
		}
		sd_m[frag_idx] = m;
		bit_set(fl->ifl_rx_bitmap, frag_idx);
#if MEMORY_LOGGING
		fl->ifl_m_enqueued++;
#endif

		DBG_COUNTER_INC(rx_allocs);
		fl->ifl_rxd_idxs[i] = frag_idx;
		fl->ifl_bus_addrs[i] = bus_addr;
		fl->ifl_vm_addrs[i] = cl;
		credits++;
		i++;
		MPASS(credits <= fl->ifl_size);
		if (++idx == fl->ifl_size) {
			fl->ifl_gen = 1;
			idx = 0;
		}
		if (n == 0 || i == IFLIB_MAX_RX_REFRESH) {
			iru.iru_pidx = pidx;
			iru.iru_count = i;
			ctx->isc_rxd_refill(ctx->ifc_softc, &iru);
			i = 0;
			pidx = idx;
			fl->ifl_pidx = idx;
			fl->ifl_credits = credits;
		}
	}

	if (i) {
		iru.iru_pidx = pidx;
		iru.iru_count = i;
		ctx->isc_rxd_refill(ctx->ifc_softc, &iru);
		fl->ifl_pidx = idx;
		fl->ifl_credits = credits;
	}
	DBG_COUNTER_INC(rxd_flush);
	if (fl->ifl_pidx == 0)
		pidx = fl->ifl_size - 1;
	else
		pidx = fl->ifl_pidx - 1;

	bus_dmamap_sync(fl->ifl_ifdi->idi_tag, fl->ifl_ifdi->idi_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	ctx->isc_rxd_flush(ctx->ifc_softc, fl->ifl_rxq->ifr_id, fl->ifl_id, pidx);
	fl->ifl_fragidx = frag_idx;
}

static __inline void
__iflib_fl_refill_lt(if_ctx_t ctx, iflib_fl_t fl, int max)
{
	/* we avoid allowing pidx to catch up with cidx as it confuses ixl */
	int32_t reclaimable = fl->ifl_size - fl->ifl_credits - 1;
#ifdef INVARIANTS
	int32_t delta = fl->ifl_size - get_inuse(fl->ifl_size, fl->ifl_cidx, fl->ifl_pidx, fl->ifl_gen) - 1;
#endif

	MPASS(fl->ifl_credits <= fl->ifl_size);
	MPASS(reclaimable == delta);

	if (reclaimable > 0)
		_iflib_fl_refill(ctx, fl, min(max, reclaimable));
}

uint8_t
iflib_in_detach(if_ctx_t ctx)
{
	bool in_detach;
	STATE_LOCK(ctx);
	in_detach = !!(ctx->ifc_flags & IFC_IN_DETACH);
	STATE_UNLOCK(ctx);
	return (in_detach);
}

static void
iflib_fl_bufs_free(iflib_fl_t fl)
{
	iflib_dma_info_t idi = fl->ifl_ifdi;
	bus_dmamap_t sd_map;
	uint32_t i;

	for (i = 0; i < fl->ifl_size; i++) {
		struct mbuf **sd_m = &fl->ifl_sds.ifsd_m[i];
		caddr_t *sd_cl = &fl->ifl_sds.ifsd_cl[i];

		if (*sd_cl != NULL) {
			sd_map = fl->ifl_sds.ifsd_map[i];
			bus_dmamap_sync(fl->ifl_buf_tag, sd_map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(fl->ifl_buf_tag, sd_map);
			if (*sd_cl != NULL)
				uma_zfree(fl->ifl_zone, *sd_cl);
			// XXX: Should this get moved out?
			if (iflib_in_detach(fl->ifl_rxq->ifr_ctx))
				bus_dmamap_destroy(fl->ifl_buf_tag, sd_map);
			if (*sd_m != NULL) {
				m_init(*sd_m, M_NOWAIT, MT_DATA, 0);
				uma_zfree(zone_mbuf, *sd_m);
			}
		} else {
			MPASS(*sd_cl == NULL);
			MPASS(*sd_m == NULL);
		}
#if MEMORY_LOGGING
		fl->ifl_m_dequeued++;
		fl->ifl_cl_dequeued++;
#endif
		*sd_cl = NULL;
		*sd_m = NULL;
	}
#ifdef INVARIANTS
	for (i = 0; i < fl->ifl_size; i++) {
		MPASS(fl->ifl_sds.ifsd_cl[i] == NULL);
		MPASS(fl->ifl_sds.ifsd_m[i] == NULL);
	}
#endif
	/*
	 * Reset free list values
	 */
	fl->ifl_credits = fl->ifl_cidx = fl->ifl_pidx = fl->ifl_gen = fl->ifl_fragidx = 0;
	bzero(idi->idi_vaddr, idi->idi_size);
}

/*********************************************************************
 *
 *  Initialize a receive ring and its buffers.
 *
 **********************************************************************/
static int
iflib_fl_setup(iflib_fl_t fl)
{
	iflib_rxq_t rxq = fl->ifl_rxq;
	if_ctx_t ctx = rxq->ifr_ctx;

	bit_nclear(fl->ifl_rx_bitmap, 0, fl->ifl_size - 1);
	/*
	** Free current RX buffer structs and their mbufs
	*/
	iflib_fl_bufs_free(fl);
	/* Now replenish the mbufs */
	MPASS(fl->ifl_credits == 0);
	fl->ifl_buf_size = ctx->ifc_rx_mbuf_sz;
	if (fl->ifl_buf_size > ctx->ifc_max_fl_buf_size)
		ctx->ifc_max_fl_buf_size = fl->ifl_buf_size;
	fl->ifl_cltype = m_gettype(fl->ifl_buf_size);
	fl->ifl_zone = m_getzone(fl->ifl_buf_size);


	/* avoid pre-allocating zillions of clusters to an idle card
	 * potentially speeding up attach
	 */
	_iflib_fl_refill(ctx, fl, min(128, fl->ifl_size));
	MPASS(min(128, fl->ifl_size) == fl->ifl_credits);
	if (min(128, fl->ifl_size) != fl->ifl_credits)
		return (ENOBUFS);
	/*
	 * handle failure
	 */
	MPASS(rxq != NULL);
	MPASS(fl->ifl_ifdi != NULL);
	bus_dmamap_sync(fl->ifl_ifdi->idi_tag, fl->ifl_ifdi->idi_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	return (0);
}

/*********************************************************************
 *
 *  Free receive ring data structures
 *
 **********************************************************************/
static void
iflib_rx_sds_free(iflib_rxq_t rxq)
{
	iflib_fl_t fl;
	int i, j;

	if (rxq->ifr_fl != NULL) {
		for (i = 0; i < rxq->ifr_nfl; i++) {
			fl = &rxq->ifr_fl[i];
			if (fl->ifl_buf_tag != NULL) {
				if (fl->ifl_sds.ifsd_map != NULL) {
					for (j = 0; j < fl->ifl_size; j++) {
						if (fl->ifl_sds.ifsd_map[j] ==
						    NULL)
							continue;
						bus_dmamap_sync(
						    fl->ifl_buf_tag,
						    fl->ifl_sds.ifsd_map[j],
						    BUS_DMASYNC_POSTREAD);
						bus_dmamap_unload(
						    fl->ifl_buf_tag,
						    fl->ifl_sds.ifsd_map[j]);
					}
				}
				bus_dma_tag_destroy(fl->ifl_buf_tag);
				fl->ifl_buf_tag = NULL;
			}
			free(fl->ifl_sds.ifsd_m, M_IFLIB);
			free(fl->ifl_sds.ifsd_cl, M_IFLIB);
			free(fl->ifl_sds.ifsd_ba, M_IFLIB);
			free(fl->ifl_sds.ifsd_map, M_IFLIB);
			fl->ifl_sds.ifsd_m = NULL;
			fl->ifl_sds.ifsd_cl = NULL;
			fl->ifl_sds.ifsd_ba = NULL;
			fl->ifl_sds.ifsd_map = NULL;
		}
		free(rxq->ifr_fl, M_IFLIB);
		rxq->ifr_fl = NULL;
		rxq->ifr_cq_gen = rxq->ifr_cq_cidx = rxq->ifr_cq_pidx = 0;
	}
}

/*
 * MI independent logic
 *
 */
static void
iflib_timer(void *arg)
{
	iflib_txq_t txq = arg;
	if_ctx_t ctx = txq->ift_ctx;
	if_softc_ctx_t sctx = &ctx->ifc_softc_ctx;
	uint64_t this_tick = ticks;
	uint32_t reset_on = hz / 2;

	if (!(if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING))
		return;
	/*
	** Check on the state of the TX queue(s), this
	** can be done without the lock because its RO
	** and the HUNG state will be static if set.
	*/
	if (this_tick - txq->ift_last_timer_tick >= hz / 2) {
		txq->ift_last_timer_tick = this_tick;
		IFDI_TIMER(ctx, txq->ift_id);
		if ((txq->ift_qstatus == IFLIB_QUEUE_HUNG) &&
		    ((txq->ift_cleaned_prev == txq->ift_cleaned) ||
		     (sctx->isc_pause_frames == 0)))
			goto hung;

		if (ifmp_ring_is_stalled(txq->ift_br))
			txq->ift_qstatus = IFLIB_QUEUE_HUNG;
		txq->ift_cleaned_prev = txq->ift_cleaned;
	}
#ifdef DEV_NETMAP
	if (if_getcapenable(ctx->ifc_ifp) & IFCAP_NETMAP)
		iflib_netmap_timer_adjust(ctx, txq, &reset_on);
#endif
	/* handle any laggards */
	if (txq->ift_db_pending)
		GROUPTASK_ENQUEUE(&txq->ift_task);

	sctx->isc_pause_frames = 0;
	if (if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING) 
		callout_reset_on(&txq->ift_timer, reset_on, iflib_timer, txq, txq->ift_timer.c_cpu);
	return;
 hung:
	device_printf(ctx->ifc_dev,  "TX(%d) desc avail = %d, pidx = %d\n",
				  txq->ift_id, TXQ_AVAIL(txq), txq->ift_pidx);
	STATE_LOCK(ctx);
	if_setdrvflagbits(ctx->ifc_ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);
	ctx->ifc_flags |= (IFC_DO_WATCHDOG|IFC_DO_RESET);
	iflib_admin_intr_deferred(ctx);
	STATE_UNLOCK(ctx);
}

static void
iflib_calc_rx_mbuf_sz(if_ctx_t ctx)
{
	if_softc_ctx_t sctx = &ctx->ifc_softc_ctx;

	/*
	 * XXX don't set the max_frame_size to larger
	 * than the hardware can handle
	 */
	if (sctx->isc_max_frame_size <= MCLBYTES)
		ctx->ifc_rx_mbuf_sz = MCLBYTES;
	else
		ctx->ifc_rx_mbuf_sz = MJUMPAGESIZE;
}

uint32_t
iflib_get_rx_mbuf_sz(if_ctx_t ctx)
{
	return (ctx->ifc_rx_mbuf_sz);
}

static void
iflib_init_locked(if_ctx_t ctx)
{
	if_softc_ctx_t sctx = &ctx->ifc_softc_ctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	if_t ifp = ctx->ifc_ifp;
	iflib_fl_t fl;
	iflib_txq_t txq;
	iflib_rxq_t rxq;
	int i, j, tx_ip_csum_flags, tx_ip6_csum_flags;


	if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);
	IFDI_INTR_DISABLE(ctx);

	tx_ip_csum_flags = scctx->isc_tx_csum_flags & (CSUM_IP | CSUM_TCP | CSUM_UDP | CSUM_SCTP);
	tx_ip6_csum_flags = scctx->isc_tx_csum_flags & (CSUM_IP6_TCP | CSUM_IP6_UDP | CSUM_IP6_SCTP);
	/* Set hardware offload abilities */
	if_clearhwassist(ifp);
	if (if_getcapenable(ifp) & IFCAP_TXCSUM)
		if_sethwassistbits(ifp, tx_ip_csum_flags, 0);
	if (if_getcapenable(ifp) & IFCAP_TXCSUM_IPV6)
		if_sethwassistbits(ifp,  tx_ip6_csum_flags, 0);
	if (if_getcapenable(ifp) & IFCAP_TSO4)
		if_sethwassistbits(ifp, CSUM_IP_TSO, 0);
	if (if_getcapenable(ifp) & IFCAP_TSO6)
		if_sethwassistbits(ifp, CSUM_IP6_TSO, 0);

	for (i = 0, txq = ctx->ifc_txqs; i < sctx->isc_ntxqsets; i++, txq++) {
		CALLOUT_LOCK(txq);
		callout_stop(&txq->ift_timer);
		CALLOUT_UNLOCK(txq);
		iflib_netmap_txq_init(ctx, txq);
	}

	/*
	 * Calculate a suitable Rx mbuf size prior to calling IFDI_INIT, so
	 * that drivers can use the value when setting up the hardware receive
	 * buffers.
	 */
	iflib_calc_rx_mbuf_sz(ctx);

#ifdef INVARIANTS
	i = if_getdrvflags(ifp);
#endif
	IFDI_INIT(ctx);
	MPASS(if_getdrvflags(ifp) == i);
	for (i = 0, rxq = ctx->ifc_rxqs; i < sctx->isc_nrxqsets; i++, rxq++) {
		/* XXX this should really be done on a per-queue basis */
		if (if_getcapenable(ifp) & IFCAP_NETMAP) {
			MPASS(rxq->ifr_id == i);
			iflib_netmap_rxq_init(ctx, rxq);
			continue;
		}
		for (j = 0, fl = rxq->ifr_fl; j < rxq->ifr_nfl; j++, fl++) {
			if (iflib_fl_setup(fl)) {
				device_printf(ctx->ifc_dev, "freelist setup failed - check cluster settings\n");
				goto done;
			}
		}
	}
done:
	if_setdrvflagbits(ctx->ifc_ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
	IFDI_INTR_ENABLE(ctx);
	txq = ctx->ifc_txqs;
	for (i = 0; i < sctx->isc_ntxqsets; i++, txq++)
		callout_reset_on(&txq->ift_timer, hz/2, iflib_timer, txq,
			txq->ift_timer.c_cpu);
}

static int
iflib_media_change(if_t ifp)
{
	if_ctx_t ctx = if_getsoftc(ifp);
	int err;

	CTX_LOCK(ctx);
	if ((err = IFDI_MEDIA_CHANGE(ctx)) == 0)
		iflib_init_locked(ctx);
	CTX_UNLOCK(ctx);
	return (err);
}

static void
iflib_media_status(if_t ifp, struct ifmediareq *ifmr)
{
	if_ctx_t ctx = if_getsoftc(ifp);

	CTX_LOCK(ctx);
	IFDI_UPDATE_ADMIN_STATUS(ctx);
	IFDI_MEDIA_STATUS(ctx, ifmr);
	CTX_UNLOCK(ctx);
}

void
iflib_stop(if_ctx_t ctx)
{
	iflib_txq_t txq = ctx->ifc_txqs;
	iflib_rxq_t rxq = ctx->ifc_rxqs;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	iflib_dma_info_t di;
	iflib_fl_t fl;
	int i, j;

	/* Tell the stack that the interface is no longer active */
	if_setdrvflagbits(ctx->ifc_ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);

	IFDI_INTR_DISABLE(ctx);
	DELAY(1000);
	IFDI_STOP(ctx);
	DELAY(1000);

	iflib_debug_reset();
	/* Wait for current tx queue users to exit to disarm watchdog timer. */
	for (i = 0; i < scctx->isc_ntxqsets; i++, txq++) {
		/* make sure all transmitters have completed before proceeding XXX */

		CALLOUT_LOCK(txq);
		callout_stop(&txq->ift_timer);
		CALLOUT_UNLOCK(txq);

		/* clean any enqueued buffers */
		iflib_ifmp_purge(txq);
		/* Free any existing tx buffers. */
		for (j = 0; j < txq->ift_size; j++) {
			iflib_txsd_free(ctx, txq, j);
		}
		txq->ift_processed = txq->ift_cleaned = txq->ift_cidx_processed = 0;
		txq->ift_in_use = txq->ift_gen = txq->ift_cidx = txq->ift_pidx = txq->ift_no_desc_avail = 0;
		txq->ift_closed = txq->ift_mbuf_defrag = txq->ift_mbuf_defrag_failed = 0;
		txq->ift_no_tx_dma_setup = txq->ift_txd_encap_efbig = txq->ift_map_failed = 0;
		txq->ift_pullups = 0;
		ifmp_ring_reset_stats(txq->ift_br);
		for (j = 0, di = txq->ift_ifdi; j < sctx->isc_ntxqs; j++, di++)
			bzero((void *)di->idi_vaddr, di->idi_size);
	}
	for (i = 0; i < scctx->isc_nrxqsets; i++, rxq++) {
		/* make sure all transmitters have completed before proceeding XXX */

		rxq->ifr_cq_gen = rxq->ifr_cq_cidx = rxq->ifr_cq_pidx = 0;
		for (j = 0, di = rxq->ifr_ifdi; j < sctx->isc_nrxqs; j++, di++)
			bzero((void *)di->idi_vaddr, di->idi_size);
		/* also resets the free lists pidx/cidx */
		for (j = 0, fl = rxq->ifr_fl; j < rxq->ifr_nfl; j++, fl++)
			iflib_fl_bufs_free(fl);
	}
}

static inline caddr_t
calc_next_rxd(iflib_fl_t fl, int cidx)
{
	qidx_t size;
	int nrxd;
	caddr_t start, end, cur, next;

	nrxd = fl->ifl_size;
	size = fl->ifl_rxd_size;
	start = fl->ifl_ifdi->idi_vaddr;

	if (__predict_false(size == 0))
		return (start);
	cur = start + size*cidx;
	end = start + size*nrxd;
	next = CACHE_PTR_NEXT(cur);
	return (next < end ? next : start);
}

static inline void
prefetch_pkts(iflib_fl_t fl, int cidx)
{
	int nextptr;
	int nrxd = fl->ifl_size;
	caddr_t next_rxd;


	nextptr = (cidx + CACHE_PTR_INCREMENT) & (nrxd-1);
	prefetch(&fl->ifl_sds.ifsd_m[nextptr]);
	prefetch(&fl->ifl_sds.ifsd_cl[nextptr]);
	next_rxd = calc_next_rxd(fl, cidx);
	prefetch(next_rxd);
	prefetch(fl->ifl_sds.ifsd_m[(cidx + 1) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_m[(cidx + 2) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_m[(cidx + 3) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_m[(cidx + 4) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_cl[(cidx + 1) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_cl[(cidx + 2) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_cl[(cidx + 3) & (nrxd-1)]);
	prefetch(fl->ifl_sds.ifsd_cl[(cidx + 4) & (nrxd-1)]);
}

static void
rxd_frag_to_sd(iflib_rxq_t rxq, if_rxd_frag_t irf, int unload, if_rxsd_t sd)
{
	int flid, cidx;
	bus_dmamap_t map;
	iflib_fl_t fl;
	int next;

	map = NULL;
	flid = irf->irf_flid;
	cidx = irf->irf_idx;
	fl = &rxq->ifr_fl[flid];
	sd->ifsd_fl = fl;
	sd->ifsd_cidx = cidx;
	sd->ifsd_m = &fl->ifl_sds.ifsd_m[cidx];
	sd->ifsd_cl = &fl->ifl_sds.ifsd_cl[cidx];
	fl->ifl_credits--;
#if MEMORY_LOGGING
	fl->ifl_m_dequeued++;
#endif
	if (rxq->ifr_ctx->ifc_flags & IFC_PREFETCH)
		prefetch_pkts(fl, cidx);
	next = (cidx + CACHE_PTR_INCREMENT) & (fl->ifl_size-1);
	prefetch(&fl->ifl_sds.ifsd_map[next]);
	map = fl->ifl_sds.ifsd_map[cidx];
	next = (cidx + CACHE_LINE_SIZE) & (fl->ifl_size-1);

	/* not valid assert if bxe really does SGE from non-contiguous elements */
	MPASS(fl->ifl_cidx == cidx);
	bus_dmamap_sync(fl->ifl_buf_tag, map, BUS_DMASYNC_POSTREAD);
	if (unload)
		bus_dmamap_unload(fl->ifl_buf_tag, map);
	fl->ifl_cidx = (fl->ifl_cidx + 1) & (fl->ifl_size-1);
	if (__predict_false(fl->ifl_cidx == 0))
		fl->ifl_gen = 0;
	bit_clear(fl->ifl_rx_bitmap, cidx);
}

static struct mbuf *
assemble_segments(iflib_rxq_t rxq, if_rxd_info_t ri, if_rxsd_t sd)
{
	int i, padlen , flags;
	struct mbuf *m, *mh, *mt;
	caddr_t cl;

	i = 0;
	mh = NULL;
	do {
		rxd_frag_to_sd(rxq, &ri->iri_frags[i], TRUE, sd);

		MPASS(*sd->ifsd_cl != NULL);
		MPASS(*sd->ifsd_m != NULL);

		/* Don't include zero-length frags */
		if (ri->iri_frags[i].irf_len == 0) {
			/* XXX we can save the cluster here, but not the mbuf */
			m_init(*sd->ifsd_m, M_NOWAIT, MT_DATA, 0);
			m_free(*sd->ifsd_m);
			*sd->ifsd_m = NULL;
			continue;
		}
		m = *sd->ifsd_m;
		*sd->ifsd_m = NULL;
		if (mh == NULL) {
			flags = M_PKTHDR|M_EXT;
			mh = mt = m;
			padlen = ri->iri_pad;
		} else {
			flags = M_EXT;
			mt->m_next = m;
			mt = m;
			/* assuming padding is only on the first fragment */
			padlen = 0;
		}
		cl = *sd->ifsd_cl;
		*sd->ifsd_cl = NULL;

		/* Can these two be made one ? */
		m_init(m, M_NOWAIT, MT_DATA, flags);
		m_cljset(m, cl, sd->ifsd_fl->ifl_cltype);
		/*
		 * These must follow m_init and m_cljset
		 */
		m->m_data += padlen;
		ri->iri_len -= padlen;
		m->m_len = ri->iri_frags[i].irf_len;
	} while (++i < ri->iri_nfrags);

	return (mh);
}

/*
 * Process one software descriptor
 */
static struct mbuf *
iflib_rxd_pkt_get(iflib_rxq_t rxq, if_rxd_info_t ri)
{
	struct if_rxsd sd;
	struct mbuf *m;

	/* should I merge this back in now that the two paths are basically duplicated? */
	if (ri->iri_nfrags == 1 &&
	    ri->iri_frags[0].irf_len <= MIN(IFLIB_RX_COPY_THRESH, MHLEN)) {
		rxd_frag_to_sd(rxq, &ri->iri_frags[0], FALSE, &sd);
		m = *sd.ifsd_m;
		*sd.ifsd_m = NULL;
		m_init(m, M_NOWAIT, MT_DATA, M_PKTHDR);
#ifndef __NO_STRICT_ALIGNMENT
		if (!IP_ALIGNED(m))
			m->m_data += 2;
#endif
		memcpy(m->m_data, *sd.ifsd_cl, ri->iri_len);
		m->m_len = ri->iri_frags[0].irf_len;
       } else {
		m = assemble_segments(rxq, ri, &sd);
	}
	m->m_pkthdr.len = ri->iri_len;
	m->m_pkthdr.rcvif = ri->iri_ifp;
	m->m_flags |= ri->iri_flags;
	m->m_pkthdr.ether_vtag = ri->iri_vtag;
	m->m_pkthdr.flowid = ri->iri_flowid;
	M_HASHTYPE_SET(m, ri->iri_rsstype);
	m->m_pkthdr.csum_flags = ri->iri_csum_flags;
	m->m_pkthdr.csum_data = ri->iri_csum_data;
	return (m);
}

#if defined(INET6) || defined(INET)
static void
iflib_get_ip_forwarding(struct lro_ctrl *lc, bool *v4, bool *v6)
{
	CURVNET_SET(lc->ifp->if_vnet);
#if defined(INET6)
	*v6 = VNET(ip6_forwarding);
#endif
#if defined(INET)
	*v4 = VNET(ipforwarding);
#endif
	CURVNET_RESTORE();
}

/*
 * Returns true if it's possible this packet could be LROed.
 * if it returns false, it is guaranteed that tcp_lro_rx()
 * would not return zero.
 */
static bool
iflib_check_lro_possible(struct mbuf *m, bool v4_forwarding, bool v6_forwarding)
{
	struct ether_header *eh;
	uint16_t eh_type;

	eh = mtod(m, struct ether_header *);
	eh_type = ntohs(eh->ether_type);
	switch (eh_type) {
#if defined(INET6)
		case ETHERTYPE_IPV6:
			return !v6_forwarding;
#endif
#if defined (INET)
		case ETHERTYPE_IP:
			return !v4_forwarding;
#endif
	}

	return false;
}
#else
static void
iflib_get_ip_forwarding(struct lro_ctrl *lc __unused, bool *v4 __unused, bool *v6 __unused)
{
}
#endif

static bool
iflib_rxeof(iflib_rxq_t rxq, qidx_t budget)
{
	if_ctx_t ctx = rxq->ifr_ctx;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	int avail, i;
	qidx_t *cidxp;
	struct if_rxd_info ri;
	int err, budget_left, rx_bytes, rx_pkts;
	iflib_fl_t fl;
	struct ifnet *ifp;
	int lro_enabled;
	bool v4_forwarding, v6_forwarding, lro_possible;

	/*
	 * XXX early demux data packets so that if_input processing only handles
	 * acks in interrupt context
	 */
	struct mbuf *m, *mh, *mt, *mf;

	lro_possible = v4_forwarding = v6_forwarding = false;
	ifp = ctx->ifc_ifp;
	mh = mt = NULL;
	MPASS(budget > 0);
	rx_pkts	= rx_bytes = 0;
	if (sctx->isc_flags & IFLIB_HAS_RXCQ)
		cidxp = &rxq->ifr_cq_cidx;
	else
		cidxp = &rxq->ifr_fl[0].ifl_cidx;
	if ((avail = iflib_rxd_avail(ctx, rxq, *cidxp, budget)) == 0) {
		for (i = 0, fl = &rxq->ifr_fl[0]; i < sctx->isc_nfl; i++, fl++)
			__iflib_fl_refill_lt(ctx, fl, budget + 8);
		DBG_COUNTER_INC(rx_unavail);
		return (false);
	}

	for (budget_left = budget; budget_left > 0 && avail > 0;) {
		if (__predict_false(!CTX_ACTIVE(ctx))) {
			DBG_COUNTER_INC(rx_ctx_inactive);
			break;
		}
		/*
		 * Reset client set fields to their default values
		 */
		rxd_info_zero(&ri);
		ri.iri_qsidx = rxq->ifr_id;
		ri.iri_cidx = *cidxp;
		ri.iri_ifp = ifp;
		ri.iri_frags = rxq->ifr_frags;
		err = ctx->isc_rxd_pkt_get(ctx->ifc_softc, &ri);

		if (err)
			goto err;
		if (sctx->isc_flags & IFLIB_HAS_RXCQ) {
			*cidxp = ri.iri_cidx;
			/* Update our consumer index */
			/* XXX NB: shurd - check if this is still safe */
			while (rxq->ifr_cq_cidx >= scctx->isc_nrxd[0]) {
				rxq->ifr_cq_cidx -= scctx->isc_nrxd[0];
				rxq->ifr_cq_gen = 0;
			}
			/* was this only a completion queue message? */
			if (__predict_false(ri.iri_nfrags == 0))
				continue;
		}
		MPASS(ri.iri_nfrags != 0);
		MPASS(ri.iri_len != 0);

		/* will advance the cidx on the corresponding free lists */
		m = iflib_rxd_pkt_get(rxq, &ri);
		avail--;
		budget_left--;
		if (avail == 0 && budget_left)
			avail = iflib_rxd_avail(ctx, rxq, *cidxp, budget_left);

		if (__predict_false(m == NULL)) {
			DBG_COUNTER_INC(rx_mbuf_null);
			continue;
		}
		/* imm_pkt: -- cxgb */
		if (mh == NULL)
			mh = mt = m;
		else {
			mt->m_nextpkt = m;
			mt = m;
		}
	}
	/* make sure that we can refill faster than drain */
	for (i = 0, fl = &rxq->ifr_fl[0]; i < sctx->isc_nfl; i++, fl++)
		__iflib_fl_refill_lt(ctx, fl, budget + 8);

	lro_enabled = (if_getcapenable(ifp) & IFCAP_LRO);
	if (lro_enabled)
		iflib_get_ip_forwarding(&rxq->ifr_lc, &v4_forwarding, &v6_forwarding);
	mt = mf = NULL;
	while (mh != NULL) {
		m = mh;
		mh = mh->m_nextpkt;
		m->m_nextpkt = NULL;
#ifndef __NO_STRICT_ALIGNMENT
		if (!IP_ALIGNED(m) && (m = iflib_fixup_rx(m)) == NULL)
			continue;
#endif
		rx_bytes += m->m_pkthdr.len;
		rx_pkts++;
#if defined(INET6) || defined(INET)
		if (lro_enabled) {
			if (!lro_possible) {
				lro_possible = iflib_check_lro_possible(m, v4_forwarding, v6_forwarding);
				if (lro_possible && mf != NULL) {
					ifp->if_input(ifp, mf);
					DBG_COUNTER_INC(rx_if_input);
					mt = mf = NULL;
				}
			}
			if ((m->m_pkthdr.csum_flags & (CSUM_L4_CALC|CSUM_L4_VALID)) ==
			    (CSUM_L4_CALC|CSUM_L4_VALID)) {
				if (lro_possible && tcp_lro_rx(&rxq->ifr_lc, m, 0) == 0)
					continue;
			}
		}
#endif
		if (lro_possible) {
			ifp->if_input(ifp, m);
			DBG_COUNTER_INC(rx_if_input);
			continue;
		}

		if (mf == NULL)
			mf = m;
		if (mt != NULL)
			mt->m_nextpkt = m;
		mt = m;
	}
	if (mf != NULL) {
		ifp->if_input(ifp, mf);
		DBG_COUNTER_INC(rx_if_input);
	}

	if_inc_counter(ifp, IFCOUNTER_IBYTES, rx_bytes);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, rx_pkts);

	/*
	 * Flush any outstanding LRO work
	 */
#if defined(INET6) || defined(INET)
	tcp_lro_flush_all(&rxq->ifr_lc);
#endif
	if (avail)
		return true;
	return (iflib_rxd_avail(ctx, rxq, *cidxp, 1));
err:
	STATE_LOCK(ctx);
	ctx->ifc_flags |= IFC_DO_RESET;
	iflib_admin_intr_deferred(ctx);
	STATE_UNLOCK(ctx);
	return (false);
}

#define TXD_NOTIFY_COUNT(txq) (((txq)->ift_size / (txq)->ift_update_freq)-1)
static inline qidx_t
txq_max_db_deferred(iflib_txq_t txq, qidx_t in_use)
{
	qidx_t notify_count = TXD_NOTIFY_COUNT(txq);
	qidx_t minthresh = txq->ift_size / 8;
	if (in_use > 4*minthresh)
		return (notify_count);
	if (in_use > 2*minthresh)
		return (notify_count >> 1);
	if (in_use > minthresh)
		return (notify_count >> 3);
	return (0);
}

static inline qidx_t
txq_max_rs_deferred(iflib_txq_t txq)
{
	qidx_t notify_count = TXD_NOTIFY_COUNT(txq);
	qidx_t minthresh = txq->ift_size / 8;
	if (txq->ift_in_use > 4*minthresh)
		return (notify_count);
	if (txq->ift_in_use > 2*minthresh)
		return (notify_count >> 1);
	if (txq->ift_in_use > minthresh)
		return (notify_count >> 2);
	return (2);
}

#define M_CSUM_FLAGS(m) ((m)->m_pkthdr.csum_flags)
#define M_HAS_VLANTAG(m) (m->m_flags & M_VLANTAG)

#define TXQ_MAX_DB_DEFERRED(txq, in_use) txq_max_db_deferred((txq), (in_use))
#define TXQ_MAX_RS_DEFERRED(txq) txq_max_rs_deferred(txq)
#define TXQ_MAX_DB_CONSUMED(size) (size >> 4)

/* forward compatibility for cxgb */
#define FIRST_QSET(ctx) 0
#define NTXQSETS(ctx) ((ctx)->ifc_softc_ctx.isc_ntxqsets)
#define NRXQSETS(ctx) ((ctx)->ifc_softc_ctx.isc_nrxqsets)
#define QIDX(ctx, m) ((((m)->m_pkthdr.flowid & ctx->ifc_softc_ctx.isc_rss_table_mask) % NTXQSETS(ctx)) + FIRST_QSET(ctx))
#define DESC_RECLAIMABLE(q) ((int)((q)->ift_processed - (q)->ift_cleaned - (q)->ift_ctx->ifc_softc_ctx.isc_tx_nsegments))

/* XXX we should be setting this to something other than zero */
#define RECLAIM_THRESH(ctx) ((ctx)->ifc_sctx->isc_tx_reclaim_thresh)
#define	MAX_TX_DESC(ctx) max((ctx)->ifc_softc_ctx.isc_tx_tso_segments_max, \
    (ctx)->ifc_softc_ctx.isc_tx_nsegments)

static inline bool
iflib_txd_db_check(if_ctx_t ctx, iflib_txq_t txq, int ring, qidx_t in_use)
{
	qidx_t dbval, max;
	bool rang;

	rang = false;
	max = TXQ_MAX_DB_DEFERRED(txq, in_use);
	if (ring || txq->ift_db_pending >= max) {
		dbval = txq->ift_npending ? txq->ift_npending : txq->ift_pidx;
		bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		ctx->isc_txd_flush(ctx->ifc_softc, txq->ift_id, dbval);
		txq->ift_db_pending = txq->ift_npending = 0;
		rang = true;
	}
	return (rang);
}

#ifdef PKT_DEBUG
static void
print_pkt(if_pkt_info_t pi)
{
	printf("pi len:  %d qsidx: %d nsegs: %d ndescs: %d flags: %x pidx: %d\n",
	       pi->ipi_len, pi->ipi_qsidx, pi->ipi_nsegs, pi->ipi_ndescs, pi->ipi_flags, pi->ipi_pidx);
	printf("pi new_pidx: %d csum_flags: %lx tso_segsz: %d mflags: %x vtag: %d\n",
	       pi->ipi_new_pidx, pi->ipi_csum_flags, pi->ipi_tso_segsz, pi->ipi_mflags, pi->ipi_vtag);
	printf("pi etype: %d ehdrlen: %d ip_hlen: %d ipproto: %d\n",
	       pi->ipi_etype, pi->ipi_ehdrlen, pi->ipi_ip_hlen, pi->ipi_ipproto);
}
#endif

#define IS_TSO4(pi) ((pi)->ipi_csum_flags & CSUM_IP_TSO)
#define IS_TX_OFFLOAD4(pi) ((pi)->ipi_csum_flags & (CSUM_IP_TCP | CSUM_IP_TSO))
#define IS_TSO6(pi) ((pi)->ipi_csum_flags & CSUM_IP6_TSO)
#define IS_TX_OFFLOAD6(pi) ((pi)->ipi_csum_flags & (CSUM_IP6_TCP | CSUM_IP6_TSO))

static int
iflib_parse_header(iflib_txq_t txq, if_pkt_info_t pi, struct mbuf **mp)
{
	if_shared_ctx_t sctx = txq->ift_ctx->ifc_sctx;
	struct ether_vlan_header *eh;
	struct mbuf *m;

	m = *mp;
	if ((sctx->isc_flags & IFLIB_NEED_SCRATCH) &&
	    M_WRITABLE(m) == 0) {
		if ((m = m_dup(m, M_NOWAIT)) == NULL) {
			return (ENOMEM);
		} else {
			m_freem(*mp);
			DBG_COUNTER_INC(tx_frees);
			*mp = m;
		}
	}

	/*
	 * Determine where frame payload starts.
	 * Jump over vlan headers if already present,
	 * helpful for QinQ too.
	 */
	if (__predict_false(m->m_len < sizeof(*eh))) {
		txq->ift_pullups++;
		if (__predict_false((m = m_pullup(m, sizeof(*eh))) == NULL))
			return (ENOMEM);
	}
	eh = mtod(m, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		pi->ipi_etype = ntohs(eh->evl_proto);
		pi->ipi_ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		pi->ipi_etype = ntohs(eh->evl_encap_proto);
		pi->ipi_ehdrlen = ETHER_HDR_LEN;
	}

	switch (pi->ipi_etype) {
#ifdef INET
	case ETHERTYPE_IP:
	{
		struct mbuf *n;
		struct ip *ip = NULL;
		struct tcphdr *th = NULL;
		int minthlen;

		minthlen = min(m->m_pkthdr.len, pi->ipi_ehdrlen + sizeof(*ip) + sizeof(*th));
		if (__predict_false(m->m_len < minthlen)) {
			/*
			 * if this code bloat is causing too much of a hit
			 * move it to a separate function and mark it noinline
			 */
			if (m->m_len == pi->ipi_ehdrlen) {
				n = m->m_next;
				MPASS(n);
				if (n->m_len >= sizeof(*ip))  {
					ip = (struct ip *)n->m_data;
					if (n->m_len >= (ip->ip_hl << 2) + sizeof(*th))
						th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
				} else {
					txq->ift_pullups++;
					if (__predict_false((m = m_pullup(m, minthlen)) == NULL))
						return (ENOMEM);
					ip = (struct ip *)(m->m_data + pi->ipi_ehdrlen);
				}
			} else {
				txq->ift_pullups++;
				if (__predict_false((m = m_pullup(m, minthlen)) == NULL))
					return (ENOMEM);
				ip = (struct ip *)(m->m_data + pi->ipi_ehdrlen);
				if (m->m_len >= (ip->ip_hl << 2) + sizeof(*th))
					th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
			}
		} else {
			ip = (struct ip *)(m->m_data + pi->ipi_ehdrlen);
			if (m->m_len >= (ip->ip_hl << 2) + sizeof(*th))
				th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
		}
		pi->ipi_ip_hlen = ip->ip_hl << 2;
		pi->ipi_ipproto = ip->ip_p;
		pi->ipi_flags |= IPI_TX_IPV4;

		/* TCP checksum offload may require TCP header length */
		if (IS_TX_OFFLOAD4(pi)) {
			if (__predict_true(pi->ipi_ipproto == IPPROTO_TCP)) {
				if (__predict_false(th == NULL)) {
					txq->ift_pullups++;
					if (__predict_false((m = m_pullup(m, (ip->ip_hl << 2) + sizeof(*th))) == NULL))
						return (ENOMEM);
					th = (struct tcphdr *)((caddr_t)ip + pi->ipi_ip_hlen);
				}
				pi->ipi_tcp_hflags = th->th_flags;
				pi->ipi_tcp_hlen = th->th_off << 2;
				pi->ipi_tcp_seq = th->th_seq;
			}
			if (IS_TSO4(pi)) {
				if (__predict_false(ip->ip_p != IPPROTO_TCP))
					return (ENXIO);
				/*
				 * TSO always requires hardware checksum offload.
				 */
				pi->ipi_csum_flags |= (CSUM_IP_TCP | CSUM_IP);
				th->th_sum = in_pseudo(ip->ip_src.s_addr,
						       ip->ip_dst.s_addr, htons(IPPROTO_TCP));
				pi->ipi_tso_segsz = m->m_pkthdr.tso_segsz;
				if (sctx->isc_flags & IFLIB_TSO_INIT_IP) {
					ip->ip_sum = 0;
					ip->ip_len = htons(pi->ipi_ip_hlen + pi->ipi_tcp_hlen + pi->ipi_tso_segsz);
				}
			}
		}
		if ((sctx->isc_flags & IFLIB_NEED_ZERO_CSUM) && (pi->ipi_csum_flags & CSUM_IP))
                       ip->ip_sum = 0;

		break;
	}
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
	{
		struct ip6_hdr *ip6 = (struct ip6_hdr *)(m->m_data + pi->ipi_ehdrlen);
		struct tcphdr *th;
		pi->ipi_ip_hlen = sizeof(struct ip6_hdr);

		if (__predict_false(m->m_len < pi->ipi_ehdrlen + sizeof(struct ip6_hdr))) {
			txq->ift_pullups++;
			if (__predict_false((m = m_pullup(m, pi->ipi_ehdrlen + sizeof(struct ip6_hdr))) == NULL))
				return (ENOMEM);
		}
		th = (struct tcphdr *)((caddr_t)ip6 + pi->ipi_ip_hlen);

		/* XXX-BZ this will go badly in case of ext hdrs. */
		pi->ipi_ipproto = ip6->ip6_nxt;
		pi->ipi_flags |= IPI_TX_IPV6;

		/* TCP checksum offload may require TCP header length */
		if (IS_TX_OFFLOAD6(pi)) {
			if (pi->ipi_ipproto == IPPROTO_TCP) {
				if (__predict_false(m->m_len < pi->ipi_ehdrlen + sizeof(struct ip6_hdr) + sizeof(struct tcphdr))) {
					txq->ift_pullups++;
					if (__predict_false((m = m_pullup(m, pi->ipi_ehdrlen + sizeof(struct ip6_hdr) + sizeof(struct tcphdr))) == NULL))
						return (ENOMEM);
				}
				pi->ipi_tcp_hflags = th->th_flags;
				pi->ipi_tcp_hlen = th->th_off << 2;
				pi->ipi_tcp_seq = th->th_seq;
			}
			if (IS_TSO6(pi)) {
				if (__predict_false(ip6->ip6_nxt != IPPROTO_TCP))
					return (ENXIO);
				/*
				 * TSO always requires hardware checksum offload.
				 */
				pi->ipi_csum_flags |= CSUM_IP6_TCP;
				th->th_sum = in6_cksum_pseudo(ip6, 0, IPPROTO_TCP, 0);
				pi->ipi_tso_segsz = m->m_pkthdr.tso_segsz;
			}
		}
		break;
	}
#endif
	default:
		pi->ipi_csum_flags &= ~CSUM_OFFLOAD;
		pi->ipi_ip_hlen = 0;
		break;
	}
	*mp = m;

	return (0);
}

/*
 * If dodgy hardware rejects the scatter gather chain we've handed it
 * we'll need to remove the mbuf chain from ifsg_m[] before we can add the
 * m_defrag'd mbufs
 */
static __noinline struct mbuf *
iflib_remove_mbuf(iflib_txq_t txq)
{
	int ntxd, pidx;
	struct mbuf *m, **ifsd_m;

	ifsd_m = txq->ift_sds.ifsd_m;
	ntxd = txq->ift_size;
	pidx = txq->ift_pidx & (ntxd - 1);
	ifsd_m = txq->ift_sds.ifsd_m;
	m = ifsd_m[pidx];
	ifsd_m[pidx] = NULL;
	bus_dmamap_unload(txq->ift_buf_tag, txq->ift_sds.ifsd_map[pidx]);
	if (txq->ift_sds.ifsd_tso_map != NULL)
		bus_dmamap_unload(txq->ift_tso_buf_tag,
		    txq->ift_sds.ifsd_tso_map[pidx]);
#if MEMORY_LOGGING
	txq->ift_dequeued++;
#endif
	return (m);
}

static inline caddr_t
calc_next_txd(iflib_txq_t txq, int cidx, uint8_t qid)
{
	qidx_t size;
	int ntxd;
	caddr_t start, end, cur, next;

	ntxd = txq->ift_size;
	size = txq->ift_txd_size[qid];
	start = txq->ift_ifdi[qid].idi_vaddr;

	if (__predict_false(size == 0))
		return (start);
	cur = start + size*cidx;
	end = start + size*ntxd;
	next = CACHE_PTR_NEXT(cur);
	return (next < end ? next : start);
}

/*
 * Pad an mbuf to ensure a minimum ethernet frame size.
 * min_frame_size is the frame size (less CRC) to pad the mbuf to
 */
static __noinline int
iflib_ether_pad(device_t dev, struct mbuf **m_head, uint16_t min_frame_size)
{
	/*
	 * 18 is enough bytes to pad an ARP packet to 46 bytes, and
	 * and ARP message is the smallest common payload I can think of
	 */
	static char pad[18];	/* just zeros */
	int n;
	struct mbuf *new_head;

	if (!M_WRITABLE(*m_head)) {
		new_head = m_dup(*m_head, M_NOWAIT);
		if (new_head == NULL) {
			m_freem(*m_head);
			device_printf(dev, "cannot pad short frame, m_dup() failed");
			DBG_COUNTER_INC(encap_pad_mbuf_fail);
			DBG_COUNTER_INC(tx_frees);
			return ENOMEM;
		}
		m_freem(*m_head);
		*m_head = new_head;
	}

	for (n = min_frame_size - (*m_head)->m_pkthdr.len;
	     n > 0; n -= sizeof(pad))
		if (!m_append(*m_head, min(n, sizeof(pad)), pad))
			break;

	if (n > 0) {
		m_freem(*m_head);
		device_printf(dev, "cannot pad short frame\n");
		DBG_COUNTER_INC(encap_pad_mbuf_fail);
		DBG_COUNTER_INC(tx_frees);
		return (ENOBUFS);
	}

	return 0;
}

static int
iflib_encap(iflib_txq_t txq, struct mbuf **m_headp)
{
	if_ctx_t		ctx;
	if_shared_ctx_t		sctx;
	if_softc_ctx_t		scctx;
	bus_dma_tag_t		buf_tag;
	bus_dma_segment_t	*segs;
	struct mbuf		*m_head, **ifsd_m;
	void			*next_txd;
	bus_dmamap_t		map;
	struct if_pkt_info	pi;
	int remap = 0;
	int err, nsegs, ndesc, max_segs, pidx, cidx, next, ntxd;

	ctx = txq->ift_ctx;
	sctx = ctx->ifc_sctx;
	scctx = &ctx->ifc_softc_ctx;
	segs = txq->ift_segs;
	ntxd = txq->ift_size;
	m_head = *m_headp;
	map = NULL;

	/*
	 * If we're doing TSO the next descriptor to clean may be quite far ahead
	 */
	cidx = txq->ift_cidx;
	pidx = txq->ift_pidx;
	if (ctx->ifc_flags & IFC_PREFETCH) {
		next = (cidx + CACHE_PTR_INCREMENT) & (ntxd-1);
		if (!(ctx->ifc_flags & IFLIB_HAS_TXCQ)) {
			next_txd = calc_next_txd(txq, cidx, 0);
			prefetch(next_txd);
		}

		/* prefetch the next cache line of mbuf pointers and flags */
		prefetch(&txq->ift_sds.ifsd_m[next]);
		prefetch(&txq->ift_sds.ifsd_map[next]);
		next = (cidx + CACHE_LINE_SIZE) & (ntxd-1);
	}
	map = txq->ift_sds.ifsd_map[pidx];
	ifsd_m = txq->ift_sds.ifsd_m;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		buf_tag = txq->ift_tso_buf_tag;
		max_segs = scctx->isc_tx_tso_segments_max;
		map = txq->ift_sds.ifsd_tso_map[pidx];
		MPASS(buf_tag != NULL);
		MPASS(max_segs > 0);
	} else {
		buf_tag = txq->ift_buf_tag;
		max_segs = scctx->isc_tx_nsegments;
		map = txq->ift_sds.ifsd_map[pidx];
	}
	if ((sctx->isc_flags & IFLIB_NEED_ETHER_PAD) &&
	    __predict_false(m_head->m_pkthdr.len < scctx->isc_min_frame_size)) {
		err = iflib_ether_pad(ctx->ifc_dev, m_headp, scctx->isc_min_frame_size);
		if (err) {
			DBG_COUNTER_INC(encap_txd_encap_fail);
			return err;
		}
	}
	m_head = *m_headp;

	pkt_info_zero(&pi);
	pi.ipi_mflags = (m_head->m_flags & (M_VLANTAG|M_BCAST|M_MCAST));
	pi.ipi_pidx = pidx;
	pi.ipi_qsidx = txq->ift_id;
	pi.ipi_len = m_head->m_pkthdr.len;
	pi.ipi_csum_flags = m_head->m_pkthdr.csum_flags;
	pi.ipi_vtag = (m_head->m_flags & M_VLANTAG) ? m_head->m_pkthdr.ether_vtag : 0;

	/* deliberate bitwise OR to make one condition */
	if (__predict_true((pi.ipi_csum_flags | pi.ipi_vtag))) {
		if (__predict_false((err = iflib_parse_header(txq, &pi, m_headp)) != 0)) {
			DBG_COUNTER_INC(encap_txd_encap_fail);
			return (err);
		}
		m_head = *m_headp;
	}

retry:
	err = bus_dmamap_load_mbuf_sg(buf_tag, map, m_head, segs, &nsegs,
	    BUS_DMA_NOWAIT);
defrag:
	if (__predict_false(err)) {
		switch (err) {
		case EFBIG:
			/* try collapse once and defrag once */
			if (remap == 0) {
				m_head = m_collapse(*m_headp, M_NOWAIT, max_segs);
				/* try defrag if collapsing fails */
				if (m_head == NULL)
					remap++;
			}
			if (remap == 1) {
				txq->ift_mbuf_defrag++;
				m_head = m_defrag(*m_headp, M_NOWAIT);
			}
			/*
			 * remap should never be >1 unless bus_dmamap_load_mbuf_sg
			 * failed to map an mbuf that was run through m_defrag
			 */
			MPASS(remap <= 1);
			if (__predict_false(m_head == NULL || remap > 1))
				goto defrag_failed;
			remap++;
			*m_headp = m_head;
			goto retry;
			break;
		case ENOMEM:
			txq->ift_no_tx_dma_setup++;
			break;
		default:
			txq->ift_no_tx_dma_setup++;
			m_freem(*m_headp);
			DBG_COUNTER_INC(tx_frees);
			*m_headp = NULL;
			break;
		}
		txq->ift_map_failed++;
		DBG_COUNTER_INC(encap_load_mbuf_fail);
		DBG_COUNTER_INC(encap_txd_encap_fail);
		return (err);
	}
	ifsd_m[pidx] = m_head;
	/*
	 * XXX assumes a 1 to 1 relationship between segments and
	 *        descriptors - this does not hold true on all drivers, e.g.
	 *        cxgb
	 */
	if (__predict_false(nsegs + 2 > TXQ_AVAIL(txq))) {
		txq->ift_no_desc_avail++;
		bus_dmamap_unload(buf_tag, map);
		DBG_COUNTER_INC(encap_txq_avail_fail);
		DBG_COUNTER_INC(encap_txd_encap_fail);
		if ((txq->ift_task.gt_task.ta_flags & TASK_ENQUEUED) == 0)
			GROUPTASK_ENQUEUE(&txq->ift_task);
		return (ENOBUFS);
	}
	/*
	 * On Intel cards we can greatly reduce the number of TX interrupts
	 * we see by only setting report status on every Nth descriptor.
	 * However, this also means that the driver will need to keep track
	 * of the descriptors that RS was set on to check them for the DD bit.
	 */
	txq->ift_rs_pending += nsegs + 1;
	if (txq->ift_rs_pending > TXQ_MAX_RS_DEFERRED(txq) ||
	     iflib_no_tx_batch || (TXQ_AVAIL(txq) - nsegs) <= MAX_TX_DESC(ctx) + 2) {
		pi.ipi_flags |= IPI_TX_INTR;
		txq->ift_rs_pending = 0;
	}

	pi.ipi_segs = segs;
	pi.ipi_nsegs = nsegs;

	MPASS(pidx >= 0 && pidx < txq->ift_size);
#ifdef PKT_DEBUG
	print_pkt(&pi);
#endif
	if ((err = ctx->isc_txd_encap(ctx->ifc_softc, &pi)) == 0) {
		bus_dmamap_sync(buf_tag, map, BUS_DMASYNC_PREWRITE);
		DBG_COUNTER_INC(tx_encap);
		MPASS(pi.ipi_new_pidx < txq->ift_size);

		ndesc = pi.ipi_new_pidx - pi.ipi_pidx;
		if (pi.ipi_new_pidx < pi.ipi_pidx) {
			ndesc += txq->ift_size;
			txq->ift_gen = 1;
		}
		/*
		 * drivers can need as many as 
		 * two sentinels
		 */
		MPASS(ndesc <= pi.ipi_nsegs + 2);
		MPASS(pi.ipi_new_pidx != pidx);
		MPASS(ndesc > 0);
		txq->ift_in_use += ndesc;

		/*
		 * We update the last software descriptor again here because there may
		 * be a sentinel and/or there may be more mbufs than segments
		 */
		txq->ift_pidx = pi.ipi_new_pidx;
		txq->ift_npending += pi.ipi_ndescs;
	} else {
		*m_headp = m_head = iflib_remove_mbuf(txq);
		if (err == EFBIG) {
			txq->ift_txd_encap_efbig++;
			if (remap < 2) {
				remap = 1;
				goto defrag;
			}
		}
		goto defrag_failed;
	}
	/*
	 * err can't possibly be non-zero here, so we don't neet to test it
	 * to see if we need to DBG_COUNTER_INC(encap_txd_encap_fail).
	 */
	return (err);

defrag_failed:
	txq->ift_mbuf_defrag_failed++;
	txq->ift_map_failed++;
	m_freem(*m_headp);
	DBG_COUNTER_INC(tx_frees);
	*m_headp = NULL;
	DBG_COUNTER_INC(encap_txd_encap_fail);
	return (ENOMEM);
}

static void
iflib_tx_desc_free(iflib_txq_t txq, int n)
{
	uint32_t qsize, cidx, mask, gen;
	struct mbuf *m, **ifsd_m;
	bool do_prefetch;

	cidx = txq->ift_cidx;
	gen = txq->ift_gen;
	qsize = txq->ift_size;
	mask = qsize-1;
	ifsd_m = txq->ift_sds.ifsd_m;
	do_prefetch = (txq->ift_ctx->ifc_flags & IFC_PREFETCH);

	while (n-- > 0) {
		if (do_prefetch) {
			prefetch(ifsd_m[(cidx + 3) & mask]);
			prefetch(ifsd_m[(cidx + 4) & mask]);
		}
		if ((m = ifsd_m[cidx]) != NULL) {
			prefetch(&ifsd_m[(cidx + CACHE_PTR_INCREMENT) & mask]);
			if (m->m_pkthdr.csum_flags & CSUM_TSO) {
				bus_dmamap_sync(txq->ift_tso_buf_tag,
				    txq->ift_sds.ifsd_tso_map[cidx],
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(txq->ift_tso_buf_tag,
				    txq->ift_sds.ifsd_tso_map[cidx]);
			} else {
				bus_dmamap_sync(txq->ift_buf_tag,
				    txq->ift_sds.ifsd_map[cidx],
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(txq->ift_buf_tag,
				    txq->ift_sds.ifsd_map[cidx]);
			}
			/* XXX we don't support any drivers that batch packets yet */
			MPASS(m->m_nextpkt == NULL);
			m_freem(m);
			ifsd_m[cidx] = NULL;
#if MEMORY_LOGGING
			txq->ift_dequeued++;
#endif
			DBG_COUNTER_INC(tx_frees);
		}
		if (__predict_false(++cidx == qsize)) {
			cidx = 0;
			gen = 0;
		}
	}
	txq->ift_cidx = cidx;
	txq->ift_gen = gen;
}

static __inline int
iflib_completed_tx_reclaim(iflib_txq_t txq, int thresh)
{
	int reclaim;
	if_ctx_t ctx = txq->ift_ctx;

	KASSERT(thresh >= 0, ("invalid threshold to reclaim"));
	MPASS(thresh /*+ MAX_TX_DESC(txq->ift_ctx) */ < txq->ift_size);

	/*
	 * Need a rate-limiting check so that this isn't called every time
	 */
	iflib_tx_credits_update(ctx, txq);
	reclaim = DESC_RECLAIMABLE(txq);

	if (reclaim <= thresh /* + MAX_TX_DESC(txq->ift_ctx) */) {
#ifdef INVARIANTS
		if (iflib_verbose_debug) {
			printf("%s processed=%ju cleaned=%ju tx_nsegments=%d reclaim=%d thresh=%d\n", __FUNCTION__,
			       txq->ift_processed, txq->ift_cleaned, txq->ift_ctx->ifc_softc_ctx.isc_tx_nsegments,
			       reclaim, thresh);

		}
#endif
		return (0);
	}
	iflib_tx_desc_free(txq, reclaim);
	txq->ift_cleaned += reclaim;
	txq->ift_in_use -= reclaim;

	return (reclaim);
}

static struct mbuf **
_ring_peek_one(struct ifmp_ring *r, int cidx, int offset, int remaining)
{
	int next, size;
	struct mbuf **items;

	size = r->size;
	next = (cidx + CACHE_PTR_INCREMENT) & (size-1);
	items = __DEVOLATILE(struct mbuf **, &r->items[0]);

	prefetch(items[(cidx + offset) & (size-1)]);
	if (remaining > 1) {
		prefetch2cachelines(&items[next]);
		prefetch2cachelines(items[(cidx + offset + 1) & (size-1)]);
		prefetch2cachelines(items[(cidx + offset + 2) & (size-1)]);
		prefetch2cachelines(items[(cidx + offset + 3) & (size-1)]);
	}
	return (__DEVOLATILE(struct mbuf **, &r->items[(cidx + offset) & (size-1)]));
}

static void
iflib_txq_check_drain(iflib_txq_t txq, int budget)
{

	ifmp_ring_check_drainage(txq->ift_br, budget);
}

static uint32_t
iflib_txq_can_drain(struct ifmp_ring *r)
{
	iflib_txq_t txq = r->cookie;
	if_ctx_t ctx = txq->ift_ctx;

	if (TXQ_AVAIL(txq) > MAX_TX_DESC(ctx) + 2)
		return (1);
	bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
	    BUS_DMASYNC_POSTREAD);
	return (ctx->isc_txd_credits_update(ctx->ifc_softc, txq->ift_id,
	    false));
}

static uint32_t
iflib_txq_drain(struct ifmp_ring *r, uint32_t cidx, uint32_t pidx)
{
	iflib_txq_t txq = r->cookie;
	if_ctx_t ctx = txq->ift_ctx;
	struct ifnet *ifp = ctx->ifc_ifp;
	struct mbuf **mp, *m;
	int i, count, consumed, pkt_sent, bytes_sent, mcast_sent, avail;
	int reclaimed, err, in_use_prev, desc_used;
	bool do_prefetch, ring, rang;

	if (__predict_false(!(if_getdrvflags(ifp) & IFF_DRV_RUNNING) ||
			    !LINK_ACTIVE(ctx))) {
		DBG_COUNTER_INC(txq_drain_notready);
		return (0);
	}
	reclaimed = iflib_completed_tx_reclaim(txq, RECLAIM_THRESH(ctx));
	rang = iflib_txd_db_check(ctx, txq, reclaimed, txq->ift_in_use);
	avail = IDXDIFF(pidx, cidx, r->size);
	if (__predict_false(ctx->ifc_flags & IFC_QFLUSH)) {
		DBG_COUNTER_INC(txq_drain_flushing);
		for (i = 0; i < avail; i++) {
			if (__predict_true(r->items[(cidx + i) & (r->size-1)] != (void *)txq))
				m_free(r->items[(cidx + i) & (r->size-1)]);
			r->items[(cidx + i) & (r->size-1)] = NULL;
		}
		return (avail);
	}

	if (__predict_false(if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_OACTIVE)) {
		txq->ift_qstatus = IFLIB_QUEUE_IDLE;
		CALLOUT_LOCK(txq);
		callout_stop(&txq->ift_timer);
		CALLOUT_UNLOCK(txq);
		DBG_COUNTER_INC(txq_drain_oactive);
		return (0);
	}
	if (reclaimed)
		txq->ift_qstatus = IFLIB_QUEUE_IDLE;
	consumed = mcast_sent = bytes_sent = pkt_sent = 0;
	count = MIN(avail, TX_BATCH_SIZE);
#ifdef INVARIANTS
	if (iflib_verbose_debug)
		printf("%s avail=%d ifc_flags=%x txq_avail=%d ", __FUNCTION__,
		       avail, ctx->ifc_flags, TXQ_AVAIL(txq));
#endif
	do_prefetch = (ctx->ifc_flags & IFC_PREFETCH);
	avail = TXQ_AVAIL(txq);
	err = 0;
	for (desc_used = i = 0; i < count && avail > MAX_TX_DESC(ctx) + 2; i++) {
		int rem = do_prefetch ? count - i : 0;

		mp = _ring_peek_one(r, cidx, i, rem);
		MPASS(mp != NULL && *mp != NULL);
		if (__predict_false(*mp == (struct mbuf *)txq)) {
			consumed++;
			reclaimed++;
			continue;
		}
		in_use_prev = txq->ift_in_use;
		err = iflib_encap(txq, mp);
		if (__predict_false(err)) {
			/* no room - bail out */
			if (err == ENOBUFS)
				break;
			consumed++;
			/* we can't send this packet - skip it */
			continue;
		}
		consumed++;
		pkt_sent++;
		m = *mp;
		DBG_COUNTER_INC(tx_sent);
		bytes_sent += m->m_pkthdr.len;
		mcast_sent += !!(m->m_flags & M_MCAST);
		avail = TXQ_AVAIL(txq);

		txq->ift_db_pending += (txq->ift_in_use - in_use_prev);
		desc_used += (txq->ift_in_use - in_use_prev);
		ETHER_BPF_MTAP(ifp, m);
		if (__predict_false(!(ifp->if_drv_flags & IFF_DRV_RUNNING)))
			break;
		rang = iflib_txd_db_check(ctx, txq, false, in_use_prev);
	}

	/* deliberate use of bitwise or to avoid gratuitous short-circuit */
	ring = rang ? false  : (iflib_min_tx_latency | err) || (TXQ_AVAIL(txq) < MAX_TX_DESC(ctx));
	iflib_txd_db_check(ctx, txq, ring, txq->ift_in_use);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, bytes_sent);
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, pkt_sent);
	if (mcast_sent)
		if_inc_counter(ifp, IFCOUNTER_OMCASTS, mcast_sent);
#ifdef INVARIANTS
	if (iflib_verbose_debug)
		printf("consumed=%d\n", consumed);
#endif
	return (consumed);
}

static uint32_t
iflib_txq_drain_always(struct ifmp_ring *r)
{
	return (1);
}

static uint32_t
iflib_txq_drain_free(struct ifmp_ring *r, uint32_t cidx, uint32_t pidx)
{
	int i, avail;
	struct mbuf **mp;
	iflib_txq_t txq;

	txq = r->cookie;

	txq->ift_qstatus = IFLIB_QUEUE_IDLE;
	CALLOUT_LOCK(txq);
	callout_stop(&txq->ift_timer);
	CALLOUT_UNLOCK(txq);

	avail = IDXDIFF(pidx, cidx, r->size);
	for (i = 0; i < avail; i++) {
		mp = _ring_peek_one(r, cidx, i, avail - i);
		if (__predict_false(*mp == (struct mbuf *)txq))
			continue;
		m_freem(*mp);
		DBG_COUNTER_INC(tx_frees);
	}
	MPASS(ifmp_ring_is_stalled(r) == 0);
	return (avail);
}

static void
iflib_ifmp_purge(iflib_txq_t txq)
{
	struct ifmp_ring *r;

	r = txq->ift_br;
	r->drain = iflib_txq_drain_free;
	r->can_drain = iflib_txq_drain_always;

	ifmp_ring_check_drainage(r, r->size);

	r->drain = iflib_txq_drain;
	r->can_drain = iflib_txq_can_drain;
}

static void
_task_fn_tx(void *context)
{
	iflib_txq_t txq = context;
	if_ctx_t ctx = txq->ift_ctx;
#if defined(ALTQ) || defined(DEV_NETMAP)
	if_t ifp = ctx->ifc_ifp;
#endif
	int abdicate = ctx->ifc_sysctl_tx_abdicate;

#ifdef IFLIB_DIAGNOSTICS
	txq->ift_cpu_exec_count[curcpu]++;
#endif
	if (!(if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING))
		return;
#ifdef DEV_NETMAP
	if (if_getcapenable(ifp) & IFCAP_NETMAP) {
		bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
		    BUS_DMASYNC_POSTREAD);
		if (ctx->isc_txd_credits_update(ctx->ifc_softc, txq->ift_id, false))
			netmap_tx_irq(ifp, txq->ift_id);
		IFDI_TX_QUEUE_INTR_ENABLE(ctx, txq->ift_id);
		return;
	}
#endif
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd))
		iflib_altq_if_start(ifp);
#endif
	if (txq->ift_db_pending)
		ifmp_ring_enqueue(txq->ift_br, (void **)&txq, 1, TX_BATCH_SIZE, abdicate);
	else if (!abdicate)
		ifmp_ring_check_drainage(txq->ift_br, TX_BATCH_SIZE);
	/*
	 * When abdicating, we always need to check drainage, not just when we don't enqueue
	 */
	if (abdicate)
		ifmp_ring_check_drainage(txq->ift_br, TX_BATCH_SIZE);
	if (ctx->ifc_flags & IFC_LEGACY)
		IFDI_INTR_ENABLE(ctx);
	else {
#ifdef INVARIANTS
		int rc =
#endif
			IFDI_TX_QUEUE_INTR_ENABLE(ctx, txq->ift_id);
			KASSERT(rc != ENOTSUP, ("MSI-X support requires queue_intr_enable, but not implemented in driver"));
	}
}

static void
_task_fn_rx(void *context)
{
	iflib_rxq_t rxq = context;
	if_ctx_t ctx = rxq->ifr_ctx;
	bool more;
	uint16_t budget;

#ifdef IFLIB_DIAGNOSTICS
	rxq->ifr_cpu_exec_count[curcpu]++;
#endif
	DBG_COUNTER_INC(task_fn_rxs);
	if (__predict_false(!(if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING)))
		return;
	more = true;
#ifdef DEV_NETMAP
	if (if_getcapenable(ctx->ifc_ifp) & IFCAP_NETMAP) {
		u_int work = 0;
		if (netmap_rx_irq(ctx->ifc_ifp, rxq->ifr_id, &work)) {
			more = false;
		}
	}
#endif
	budget = ctx->ifc_sysctl_rx_budget;
	if (budget == 0)
		budget = 16;	/* XXX */
	if (more == false || (more = iflib_rxeof(rxq, budget)) == false) {
		if (ctx->ifc_flags & IFC_LEGACY)
			IFDI_INTR_ENABLE(ctx);
		else {
#ifdef INVARIANTS
			int rc =
#endif
				IFDI_RX_QUEUE_INTR_ENABLE(ctx, rxq->ifr_id);
			KASSERT(rc != ENOTSUP, ("MSI-X support requires queue_intr_enable, but not implemented in driver"));
			DBG_COUNTER_INC(rx_intr_enables);
		}
	}
	if (__predict_false(!(if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING)))
		return;
	if (more)
		GROUPTASK_ENQUEUE(&rxq->ifr_task);
}

static void
_task_fn_admin(void *context)
{
	if_ctx_t ctx = context;
	if_softc_ctx_t sctx = &ctx->ifc_softc_ctx;
	iflib_txq_t txq;
	int i;
	bool oactive, running, do_reset, do_watchdog, in_detach;
	uint32_t reset_on = hz / 2;

	STATE_LOCK(ctx);
	running = (if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING);
	oactive = (if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_OACTIVE);
	do_reset = (ctx->ifc_flags & IFC_DO_RESET);
	do_watchdog = (ctx->ifc_flags & IFC_DO_WATCHDOG);
	in_detach = (ctx->ifc_flags & IFC_IN_DETACH);
	ctx->ifc_flags &= ~(IFC_DO_RESET|IFC_DO_WATCHDOG);
	STATE_UNLOCK(ctx);

	if ((!running && !oactive) && !(ctx->ifc_sctx->isc_flags & IFLIB_ADMIN_ALWAYS_RUN))
		return;
	if (in_detach)
		return;

	CTX_LOCK(ctx);
	for (txq = ctx->ifc_txqs, i = 0; i < sctx->isc_ntxqsets; i++, txq++) {
		CALLOUT_LOCK(txq);
		callout_stop(&txq->ift_timer);
		CALLOUT_UNLOCK(txq);
	}
	if (do_watchdog) {
		ctx->ifc_watchdog_events++;
		IFDI_WATCHDOG_RESET(ctx);
	}
	IFDI_UPDATE_ADMIN_STATUS(ctx);
	for (txq = ctx->ifc_txqs, i = 0; i < sctx->isc_ntxqsets; i++, txq++) {
#ifdef DEV_NETMAP
		reset_on = hz / 2;
		if (if_getcapenable(ctx->ifc_ifp) & IFCAP_NETMAP)
			iflib_netmap_timer_adjust(ctx, txq, &reset_on);
#endif
		callout_reset_on(&txq->ift_timer, reset_on, iflib_timer, txq, txq->ift_timer.c_cpu);
	}
	IFDI_LINK_INTR_ENABLE(ctx);
	if (do_reset)
		iflib_if_init_locked(ctx);
	CTX_UNLOCK(ctx);

	if (LINK_ACTIVE(ctx) == 0)
		return;
	for (txq = ctx->ifc_txqs, i = 0; i < sctx->isc_ntxqsets; i++, txq++)
		iflib_txq_check_drain(txq, IFLIB_RESTART_BUDGET);
}


static void
_task_fn_iov(void *context)
{
	if_ctx_t ctx = context;

	if (!(if_getdrvflags(ctx->ifc_ifp) & IFF_DRV_RUNNING) &&
	    !(ctx->ifc_sctx->isc_flags & IFLIB_ADMIN_ALWAYS_RUN))
		return;

	CTX_LOCK(ctx);
	IFDI_VFLR_HANDLE(ctx);
	CTX_UNLOCK(ctx);
}

static int
iflib_sysctl_int_delay(SYSCTL_HANDLER_ARGS)
{
	int err;
	if_int_delay_info_t info;
	if_ctx_t ctx;

	info = (if_int_delay_info_t)arg1;
	ctx = info->iidi_ctx;
	info->iidi_req = req;
	info->iidi_oidp = oidp;
	CTX_LOCK(ctx);
	err = IFDI_SYSCTL_INT_DELAY(ctx, info);
	CTX_UNLOCK(ctx);
	return (err);
}

/*********************************************************************
 *
 *  IFNET FUNCTIONS
 *
 **********************************************************************/

static void
iflib_if_init_locked(if_ctx_t ctx)
{
	iflib_stop(ctx);
	iflib_init_locked(ctx);
}


static void
iflib_if_init(void *arg)
{
	if_ctx_t ctx = arg;

	CTX_LOCK(ctx);
	iflib_if_init_locked(ctx);
	CTX_UNLOCK(ctx);
}

static int
iflib_if_transmit(if_t ifp, struct mbuf *m)
{
	if_ctx_t	ctx = if_getsoftc(ifp);

	iflib_txq_t txq;
	int err, qidx;
	int abdicate = ctx->ifc_sysctl_tx_abdicate;

	if (__predict_false((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 || !LINK_ACTIVE(ctx))) {
		DBG_COUNTER_INC(tx_frees);
		m_freem(m);
		return (ENOBUFS);
	}

	MPASS(m->m_nextpkt == NULL);
	/* ALTQ-enabled interfaces always use queue 0. */
	qidx = 0;
	if ((NTXQSETS(ctx) > 1) && M_HASHTYPE_GET(m) && !ALTQ_IS_ENABLED(&ifp->if_snd))
		qidx = QIDX(ctx, m);
	/*
	 * XXX calculate buf_ring based on flowid (divvy up bits?)
	 */
	txq = &ctx->ifc_txqs[qidx];

#ifdef DRIVER_BACKPRESSURE
	if (txq->ift_closed) {
		while (m != NULL) {
			next = m->m_nextpkt;
			m->m_nextpkt = NULL;
			m_freem(m);
			DBG_COUNTER_INC(tx_frees);
			m = next;
		}
		return (ENOBUFS);
	}
#endif
#ifdef notyet
	qidx = count = 0;
	mp = marr;
	next = m;
	do {
		count++;
		next = next->m_nextpkt;
	} while (next != NULL);

	if (count > nitems(marr))
		if ((mp = malloc(count*sizeof(struct mbuf *), M_IFLIB, M_NOWAIT)) == NULL) {
			/* XXX check nextpkt */
			m_freem(m);
			/* XXX simplify for now */
			DBG_COUNTER_INC(tx_frees);
			return (ENOBUFS);
		}
	for (next = m, i = 0; next != NULL; i++) {
		mp[i] = next;
		next = next->m_nextpkt;
		mp[i]->m_nextpkt = NULL;
	}
#endif
	DBG_COUNTER_INC(tx_seen);
	err = ifmp_ring_enqueue(txq->ift_br, (void **)&m, 1, TX_BATCH_SIZE, abdicate);

	if (abdicate)
		GROUPTASK_ENQUEUE(&txq->ift_task);
 	if (err) {
		if (!abdicate)
			GROUPTASK_ENQUEUE(&txq->ift_task);
		/* support forthcoming later */
#ifdef DRIVER_BACKPRESSURE
		txq->ift_closed = TRUE;
#endif
		ifmp_ring_check_drainage(txq->ift_br, TX_BATCH_SIZE);
		m_freem(m);
		DBG_COUNTER_INC(tx_frees);
	}

	return (err);
}

#ifdef ALTQ
/*
 * The overall approach to integrating iflib with ALTQ is to continue to use
 * the iflib mp_ring machinery between the ALTQ queue(s) and the hardware
 * ring.  Technically, when using ALTQ, queueing to an intermediate mp_ring
 * is redundant/unnecessary, but doing so minimizes the amount of
 * ALTQ-specific code required in iflib.  It is assumed that the overhead of
 * redundantly queueing to an intermediate mp_ring is swamped by the
 * performance limitations inherent in using ALTQ.
 *
 * When ALTQ support is compiled in, all iflib drivers will use a transmit
 * routine, iflib_altq_if_transmit(), that checks if ALTQ is enabled for the
 * given interface.  If ALTQ is enabled for an interface, then all
 * transmitted packets for that interface will be submitted to the ALTQ
 * subsystem via IFQ_ENQUEUE().  We don't use the legacy if_transmit()
 * implementation because it uses IFQ_HANDOFF(), which will duplicatively
 * update stats that the iflib machinery handles, and which is sensitve to
 * the disused IFF_DRV_OACTIVE flag.  Additionally, iflib_altq_if_start()
 * will be installed as the start routine for use by ALTQ facilities that
 * need to trigger queue drains on a scheduled basis.
 *
 */
static void
iflib_altq_if_start(if_t ifp)
{
	struct ifaltq *ifq = &ifp->if_snd;
	struct mbuf *m;
	
	IFQ_LOCK(ifq);
	IFQ_DEQUEUE_NOLOCK(ifq, m);
	while (m != NULL) {
		iflib_if_transmit(ifp, m);
		IFQ_DEQUEUE_NOLOCK(ifq, m);
	}
	IFQ_UNLOCK(ifq);
}

static int
iflib_altq_if_transmit(if_t ifp, struct mbuf *m)
{
	int err;

	if (ALTQ_IS_ENABLED(&ifp->if_snd)) {
		IFQ_ENQUEUE(&ifp->if_snd, m, err);
		if (err == 0)
			iflib_altq_if_start(ifp);
	} else
		err = iflib_if_transmit(ifp, m);

	return (err);
}
#endif /* ALTQ */

static void
iflib_if_qflush(if_t ifp)
{
	if_ctx_t ctx = if_getsoftc(ifp);
	iflib_txq_t txq = ctx->ifc_txqs;
	int i;

	STATE_LOCK(ctx);
	ctx->ifc_flags |= IFC_QFLUSH;
	STATE_UNLOCK(ctx);
	for (i = 0; i < NTXQSETS(ctx); i++, txq++)
		while (!(ifmp_ring_is_idle(txq->ift_br) || ifmp_ring_is_stalled(txq->ift_br)))
			iflib_txq_check_drain(txq, 0);
	STATE_LOCK(ctx);
	ctx->ifc_flags &= ~IFC_QFLUSH;
	STATE_UNLOCK(ctx);

	/*
	 * When ALTQ is enabled, this will also take care of purging the
	 * ALTQ queue(s).
	 */
	if_qflush(ifp);
}


#define IFCAP_FLAGS (IFCAP_HWCSUM_IPV6 | IFCAP_HWCSUM | IFCAP_LRO | \
		     IFCAP_TSO | IFCAP_VLAN_HWTAGGING | IFCAP_HWSTATS | \
		     IFCAP_VLAN_MTU | IFCAP_VLAN_HWFILTER | \
		     IFCAP_VLAN_HWTSO | IFCAP_VLAN_HWCSUM)

static int
iflib_if_ioctl(if_t ifp, u_long command, caddr_t data)
{
	if_ctx_t ctx = if_getsoftc(ifp);
	struct ifreq	*ifr = (struct ifreq *)data;
#if defined(INET) || defined(INET6)
	struct ifaddr	*ifa = (struct ifaddr *)data;
#endif
	bool		avoid_reset = FALSE;
	int		err = 0, reinit = 0, bits;

	switch (command) {
	case SIOCSIFADDR:
#ifdef INET
		if (ifa->ifa_addr->sa_family == AF_INET)
			avoid_reset = TRUE;
#endif
#ifdef INET6
		if (ifa->ifa_addr->sa_family == AF_INET6)
			avoid_reset = TRUE;
#endif
		/*
		** Calling init results in link renegotiation,
		** so we avoid doing it when possible.
		*/
		if (avoid_reset) {
			if_setflagbits(ifp, IFF_UP,0);
			if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING))
				reinit = 1;
#ifdef INET
			if (!(if_getflags(ifp) & IFF_NOARP))
				arp_ifinit(ifp, ifa);
#endif
		} else
			err = ether_ioctl(ifp, command, data);
		break;
	case SIOCSIFMTU:
		CTX_LOCK(ctx);
		if (ifr->ifr_mtu == if_getmtu(ifp)) {
			CTX_UNLOCK(ctx);
			break;
		}
		bits = if_getdrvflags(ifp);
		/* stop the driver and free any clusters before proceeding */
		iflib_stop(ctx);

		if ((err = IFDI_MTU_SET(ctx, ifr->ifr_mtu)) == 0) {
			STATE_LOCK(ctx);
			if (ifr->ifr_mtu > ctx->ifc_max_fl_buf_size)
				ctx->ifc_flags |= IFC_MULTISEG;
			else
				ctx->ifc_flags &= ~IFC_MULTISEG;
			STATE_UNLOCK(ctx);
			err = if_setmtu(ifp, ifr->ifr_mtu);
		}
		iflib_init_locked(ctx);
		STATE_LOCK(ctx);
		if_setdrvflags(ifp, bits);
		STATE_UNLOCK(ctx);
		CTX_UNLOCK(ctx);
		break;
	case SIOCSIFFLAGS:
		CTX_LOCK(ctx);
		if (if_getflags(ifp) & IFF_UP) {
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
				if ((if_getflags(ifp) ^ ctx->ifc_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) {
					err = IFDI_PROMISC_SET(ctx, if_getflags(ifp));
				}
			} else
				reinit = 1;
		} else if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
			iflib_stop(ctx);
		}
		ctx->ifc_if_flags = if_getflags(ifp);
		CTX_UNLOCK(ctx);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
			CTX_LOCK(ctx);
			IFDI_INTR_DISABLE(ctx);
			IFDI_MULTI_SET(ctx);
			IFDI_INTR_ENABLE(ctx);
			CTX_UNLOCK(ctx);
		}
		break;
	case SIOCSIFMEDIA:
		CTX_LOCK(ctx);
		IFDI_MEDIA_SET(ctx);
		CTX_UNLOCK(ctx);
		/* falls thru */
	case SIOCGIFMEDIA:
	case SIOCGIFXMEDIA:
		err = ifmedia_ioctl(ifp, ifr, &ctx->ifc_media, command);
		break;
	case SIOCGI2C:
	{
		struct ifi2creq i2c;

		err = copyin(ifr_data_get_ptr(ifr), &i2c, sizeof(i2c));
		if (err != 0)
			break;
		if (i2c.dev_addr != 0xA0 && i2c.dev_addr != 0xA2) {
			err = EINVAL;
			break;
		}
		if (i2c.len > sizeof(i2c.data)) {
			err = EINVAL;
			break;
		}

		if ((err = IFDI_I2C_REQ(ctx, &i2c)) == 0)
			err = copyout(&i2c, ifr_data_get_ptr(ifr),
			    sizeof(i2c));
		break;
	}
	case SIOCSIFCAP:
	{
		int mask, setmask, oldmask;

		oldmask = if_getcapenable(ifp);
		mask = ifr->ifr_reqcap ^ oldmask;
		mask &= ctx->ifc_softc_ctx.isc_capabilities;
		setmask = 0;
#ifdef TCP_OFFLOAD
		setmask |= mask & (IFCAP_TOE4|IFCAP_TOE6);
#endif
		setmask |= (mask & IFCAP_FLAGS);
		setmask |= (mask & IFCAP_WOL);

		/*
		 * If any RX csum has changed, change all the ones that
		 * are supported by the driver.
		 */
		if (setmask & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6)) {
			setmask |= ctx->ifc_softc_ctx.isc_capabilities &
			    (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6);
		}

		/*
		 * want to ensure that traffic has stopped before we change any of the flags
		 */
		if (setmask) {
			CTX_LOCK(ctx);
			bits = if_getdrvflags(ifp);
			if (bits & IFF_DRV_RUNNING && setmask & ~IFCAP_WOL)
				iflib_stop(ctx);
			STATE_LOCK(ctx);
			if_togglecapenable(ifp, setmask);
			STATE_UNLOCK(ctx);
			if (bits & IFF_DRV_RUNNING && setmask & ~IFCAP_WOL)
				iflib_init_locked(ctx);
			STATE_LOCK(ctx);
			if_setdrvflags(ifp, bits);
			STATE_UNLOCK(ctx);
			CTX_UNLOCK(ctx);
		}
		if_vlancap(ifp);
		break;
	}
	case SIOCGPRIVATE_0:
	case SIOCSDRVSPEC:
	case SIOCGDRVSPEC:
		CTX_LOCK(ctx);
		err = IFDI_PRIV_IOCTL(ctx, command, data);
		CTX_UNLOCK(ctx);
		break;
	default:
		err = ether_ioctl(ifp, command, data);
		break;
	}
	if (reinit)
		iflib_if_init(ctx);
	return (err);
}

static uint64_t
iflib_if_get_counter(if_t ifp, ift_counter cnt)
{
	if_ctx_t ctx = if_getsoftc(ifp);

	return (IFDI_GET_COUNTER(ctx, cnt));
}

/*********************************************************************
 *
 *  OTHER FUNCTIONS EXPORTED TO THE STACK
 *
 **********************************************************************/

static void
iflib_vlan_register(void *arg, if_t ifp, uint16_t vtag)
{
	if_ctx_t ctx = if_getsoftc(ifp);

	if ((void *)ctx != arg)
		return;

	if ((vtag == 0) || (vtag > 4095))
		return;

	CTX_LOCK(ctx);
	IFDI_VLAN_REGISTER(ctx, vtag);
	/* Re-init to load the changes */
	if (if_getcapenable(ifp) & IFCAP_VLAN_HWFILTER)
		iflib_if_init_locked(ctx);
	CTX_UNLOCK(ctx);
}

static void
iflib_vlan_unregister(void *arg, if_t ifp, uint16_t vtag)
{
	if_ctx_t ctx = if_getsoftc(ifp);

	if ((void *)ctx != arg)
		return;

	if ((vtag == 0) || (vtag > 4095))
		return;

	CTX_LOCK(ctx);
	IFDI_VLAN_UNREGISTER(ctx, vtag);
	/* Re-init to load the changes */
	if (if_getcapenable(ifp) & IFCAP_VLAN_HWFILTER)
		iflib_if_init_locked(ctx);
	CTX_UNLOCK(ctx);
}

static void
iflib_led_func(void *arg, int onoff)
{
	if_ctx_t ctx = arg;

	CTX_LOCK(ctx);
	IFDI_LED_FUNC(ctx, onoff);
	CTX_UNLOCK(ctx);
}

/*********************************************************************
 *
 *  BUS FUNCTION DEFINITIONS
 *
 **********************************************************************/

int
iflib_device_probe(device_t dev)
{
	pci_vendor_info_t *ent;

	uint16_t	pci_vendor_id, pci_device_id;
	uint16_t	pci_subvendor_id, pci_subdevice_id;
	uint16_t	pci_rev_id;
	if_shared_ctx_t sctx;

	if ((sctx = DEVICE_REGISTER(dev)) == NULL || sctx->isc_magic != IFLIB_MAGIC)
		return (ENOTSUP);

	pci_vendor_id = pci_get_vendor(dev);
	pci_device_id = pci_get_device(dev);
	pci_subvendor_id = pci_get_subvendor(dev);
	pci_subdevice_id = pci_get_subdevice(dev);
	pci_rev_id = pci_get_revid(dev);
	if (sctx->isc_parse_devinfo != NULL)
		sctx->isc_parse_devinfo(&pci_device_id, &pci_subvendor_id, &pci_subdevice_id, &pci_rev_id);

	ent = sctx->isc_vendor_info;
	while (ent->pvi_vendor_id != 0) {
		if (pci_vendor_id != ent->pvi_vendor_id) {
			ent++;
			continue;
		}
		if ((pci_device_id == ent->pvi_device_id) &&
		    ((pci_subvendor_id == ent->pvi_subvendor_id) ||
		     (ent->pvi_subvendor_id == 0)) &&
		    ((pci_subdevice_id == ent->pvi_subdevice_id) ||
		     (ent->pvi_subdevice_id == 0)) &&
		    ((pci_rev_id == ent->pvi_rev_id) ||
		     (ent->pvi_rev_id == 0))) {

			device_set_desc_copy(dev, ent->pvi_name);
			/* this needs to be changed to zero if the bus probing code
			 * ever stops re-probing on best match because the sctx
			 * may have its values over written by register calls
			 * in subsequent probes
			 */
			return (BUS_PROBE_DEFAULT);
		}
		ent++;
	}
	return (ENXIO);
}

static void
iflib_reset_qvalues(if_ctx_t ctx)
{
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	device_t dev = ctx->ifc_dev;
	int i;

	scctx->isc_txrx_budget_bytes_max = IFLIB_MAX_TX_BYTES;
	scctx->isc_tx_qdepth = IFLIB_DEFAULT_TX_QDEPTH;
	/*
	 * XXX sanity check that ntxd & nrxd are a power of 2
	 */
	if (ctx->ifc_sysctl_ntxqs != 0)
		scctx->isc_ntxqsets = ctx->ifc_sysctl_ntxqs;
	if (ctx->ifc_sysctl_nrxqs != 0)
		scctx->isc_nrxqsets = ctx->ifc_sysctl_nrxqs;

	for (i = 0; i < sctx->isc_ntxqs; i++) {
		if (ctx->ifc_sysctl_ntxds[i] != 0)
			scctx->isc_ntxd[i] = ctx->ifc_sysctl_ntxds[i];
		else
			scctx->isc_ntxd[i] = sctx->isc_ntxd_default[i];
	}

	for (i = 0; i < sctx->isc_nrxqs; i++) {
		if (ctx->ifc_sysctl_nrxds[i] != 0)
			scctx->isc_nrxd[i] = ctx->ifc_sysctl_nrxds[i];
		else
			scctx->isc_nrxd[i] = sctx->isc_nrxd_default[i];
	}

	for (i = 0; i < sctx->isc_nrxqs; i++) {
		if (scctx->isc_nrxd[i] < sctx->isc_nrxd_min[i]) {
			device_printf(dev, "nrxd%d: %d less than nrxd_min %d - resetting to min\n",
				      i, scctx->isc_nrxd[i], sctx->isc_nrxd_min[i]);
			scctx->isc_nrxd[i] = sctx->isc_nrxd_min[i];
		}
		if (scctx->isc_nrxd[i] > sctx->isc_nrxd_max[i]) {
			device_printf(dev, "nrxd%d: %d greater than nrxd_max %d - resetting to max\n",
				      i, scctx->isc_nrxd[i], sctx->isc_nrxd_max[i]);
			scctx->isc_nrxd[i] = sctx->isc_nrxd_max[i];
		}
	}

	for (i = 0; i < sctx->isc_ntxqs; i++) {
		if (scctx->isc_ntxd[i] < sctx->isc_ntxd_min[i]) {
			device_printf(dev, "ntxd%d: %d less than ntxd_min %d - resetting to min\n",
				      i, scctx->isc_ntxd[i], sctx->isc_ntxd_min[i]);
			scctx->isc_ntxd[i] = sctx->isc_ntxd_min[i];
		}
		if (scctx->isc_ntxd[i] > sctx->isc_ntxd_max[i]) {
			device_printf(dev, "ntxd%d: %d greater than ntxd_max %d - resetting to max\n",
				      i, scctx->isc_ntxd[i], sctx->isc_ntxd_max[i]);
			scctx->isc_ntxd[i] = sctx->isc_ntxd_max[i];
		}
	}
}

int
iflib_device_register(device_t dev, void *sc, if_shared_ctx_t sctx, if_ctx_t *ctxp)
{
	int err, rid, msix;
	if_ctx_t ctx;
	if_t ifp;
	if_softc_ctx_t scctx;
	int i;
	uint16_t main_txq;
	uint16_t main_rxq;


	ctx = malloc(sizeof(* ctx), M_IFLIB, M_WAITOK|M_ZERO);

	if (sc == NULL) {
		sc = malloc(sctx->isc_driver->size, M_IFLIB, M_WAITOK|M_ZERO);
		device_set_softc(dev, ctx);
		ctx->ifc_flags |= IFC_SC_ALLOCATED;
	}

	ctx->ifc_sctx = sctx;
	ctx->ifc_dev = dev;
	ctx->ifc_softc = sc;

	if ((err = iflib_register(ctx)) != 0) {
		device_printf(dev, "iflib_register failed %d\n", err);
		goto fail_ctx_free;
	}
	iflib_add_device_sysctl_pre(ctx);

	scctx = &ctx->ifc_softc_ctx;
	ifp = ctx->ifc_ifp;

	iflib_reset_qvalues(ctx);
	CTX_LOCK(ctx);
	if ((err = IFDI_ATTACH_PRE(ctx)) != 0) {
		device_printf(dev, "IFDI_ATTACH_PRE failed %d\n", err);
		goto fail_unlock;
	}
	_iflib_pre_assert(scctx);
	ctx->ifc_txrx = *scctx->isc_txrx;

#ifdef INVARIANTS
	MPASS(scctx->isc_capabilities);
	if (scctx->isc_capabilities & IFCAP_TXCSUM)
		MPASS(scctx->isc_tx_csum_flags);
#endif

	if_setcapabilities(ifp, scctx->isc_capabilities | IFCAP_HWSTATS);
	if_setcapenable(ifp, scctx->isc_capenable | IFCAP_HWSTATS);

	if (scctx->isc_ntxqsets == 0 || (scctx->isc_ntxqsets_max && scctx->isc_ntxqsets_max < scctx->isc_ntxqsets))
		scctx->isc_ntxqsets = scctx->isc_ntxqsets_max;
	if (scctx->isc_nrxqsets == 0 || (scctx->isc_nrxqsets_max && scctx->isc_nrxqsets_max < scctx->isc_nrxqsets))
		scctx->isc_nrxqsets = scctx->isc_nrxqsets_max;

	main_txq = (sctx->isc_flags & IFLIB_HAS_TXCQ) ? 1 : 0;
	main_rxq = (sctx->isc_flags & IFLIB_HAS_RXCQ) ? 1 : 0;

	/* XXX change for per-queue sizes */
	device_printf(dev, "Using %d tx descriptors and %d rx descriptors\n",
	    scctx->isc_ntxd[main_txq], scctx->isc_nrxd[main_rxq]);
	for (i = 0; i < sctx->isc_nrxqs; i++) {
		if (!powerof2(scctx->isc_nrxd[i])) {
			/* round down instead? */
			device_printf(dev, "# rx descriptors must be a power of 2\n");
			err = EINVAL;
			goto fail_iflib_detach;
		}
	}
	for (i = 0; i < sctx->isc_ntxqs; i++) {
		if (!powerof2(scctx->isc_ntxd[i])) {
			device_printf(dev,
			    "# tx descriptors must be a power of 2");
			err = EINVAL;
			goto fail_iflib_detach;
		}
	}

	if (scctx->isc_tx_nsegments > scctx->isc_ntxd[main_txq] /
	    MAX_SINGLE_PACKET_FRACTION)
		scctx->isc_tx_nsegments = max(1, scctx->isc_ntxd[main_txq] /
		    MAX_SINGLE_PACKET_FRACTION);
	if (scctx->isc_tx_tso_segments_max > scctx->isc_ntxd[main_txq] /
	    MAX_SINGLE_PACKET_FRACTION)
		scctx->isc_tx_tso_segments_max = max(1,
		    scctx->isc_ntxd[main_txq] / MAX_SINGLE_PACKET_FRACTION);

	/* TSO parameters - dig these out of the data sheet - simply correspond to tag setup */
	if (if_getcapabilities(ifp) & IFCAP_TSO) {
		/*
		 * The stack can't handle a TSO size larger than IP_MAXPACKET,
		 * but some MACs do.
		 */
		if_sethwtsomax(ifp, min(scctx->isc_tx_tso_size_max,
		    IP_MAXPACKET));
		/*
		 * Take maximum number of m_pullup(9)'s in iflib_parse_header()
		 * into account.  In the worst case, each of these calls will
		 * add another mbuf and, thus, the requirement for another DMA
		 * segment.  So for best performance, it doesn't make sense to
		 * advertize a maximum of TSO segments that typically will
		 * require defragmentation in iflib_encap().
		 */
		if_sethwtsomaxsegcount(ifp, scctx->isc_tx_tso_segments_max - 3);
		if_sethwtsomaxsegsize(ifp, scctx->isc_tx_tso_segsize_max);
	}
	if (scctx->isc_rss_table_size == 0)
		scctx->isc_rss_table_size = 64;
	scctx->isc_rss_table_mask = scctx->isc_rss_table_size-1;

	GROUPTASK_INIT(&ctx->ifc_admin_task, 0, _task_fn_admin, ctx);
	/* XXX format name */
	taskqgroup_attach(qgroup_if_config_tqg, &ctx->ifc_admin_task, ctx,
	    NULL, NULL, "admin");

	/* Set up cpu set.  If it fails, use the set of all CPUs. */
	if (bus_get_cpus(dev, INTR_CPUS, sizeof(ctx->ifc_cpus), &ctx->ifc_cpus) != 0) {
		device_printf(dev, "Unable to fetch CPU list\n");
		CPU_COPY(&all_cpus, &ctx->ifc_cpus);
	}
	MPASS(CPU_COUNT(&ctx->ifc_cpus) > 0);

	/*
	** Now set up MSI or MSI-X, should return us the number of supported
	** vectors (will be 1 for a legacy interrupt and MSI).
	*/
	if (sctx->isc_flags & IFLIB_SKIP_MSIX) {
		msix = scctx->isc_vectors;
	} else if (scctx->isc_msix_bar != 0)
	       /*
		* The simple fact that isc_msix_bar is not 0 does not mean we
		* we have a good value there that is known to work.
		*/
		msix = iflib_msix_init(ctx);
	else {
		scctx->isc_vectors = 1;
		scctx->isc_ntxqsets = 1;
		scctx->isc_nrxqsets = 1;
		scctx->isc_intr = IFLIB_INTR_LEGACY;
		msix = 0;
	}
	/* Get memory for the station queues */
	if ((err = iflib_queues_alloc(ctx))) {
		device_printf(dev, "Unable to allocate queue memory\n");
		goto fail_intr_free;
	}

	if ((err = iflib_qset_structures_setup(ctx)))
		goto fail_queues;

	/*
	 * Group taskqueues aren't properly set up until SMP is started,
	 * so we disable interrupts until we can handle them post
	 * SI_SUB_SMP.
	 *
	 * XXX: disabling interrupts doesn't actually work, at least for
	 * the non-MSI case.  When they occur before SI_SUB_SMP completes,
	 * we do null handling and depend on this not causing too large an
	 * interrupt storm.
	 */
	IFDI_INTR_DISABLE(ctx);
	if (msix > 1 && (err = IFDI_MSIX_INTR_ASSIGN(ctx, msix)) != 0) {
		device_printf(dev, "IFDI_MSIX_INTR_ASSIGN failed %d\n", err);
		goto fail_queues;
	}
	if (msix <= 1) {
		rid = 0;
		if (scctx->isc_intr == IFLIB_INTR_MSI) {
			MPASS(msix == 1);
			rid = 1;
		}
		if ((err = iflib_legacy_setup(ctx, ctx->isc_legacy_intr, ctx->ifc_softc, &rid, "irq0")) != 0) {
			device_printf(dev, "iflib_legacy_setup failed %d\n", err);
			goto fail_queues;
		}
	}

	ether_ifattach(ctx->ifc_ifp, ctx->ifc_mac);

	if ((err = IFDI_ATTACH_POST(ctx)) != 0) {
		device_printf(dev, "IFDI_ATTACH_POST failed %d\n", err);
		goto fail_detach;
	}

	/*
	 * Tell the upper layer(s) if IFCAP_VLAN_MTU is supported.
	 * This must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets if_hdrlen to the default value.
	 */
	if (if_getcapabilities(ifp) & IFCAP_VLAN_MTU)
		if_setifheaderlen(ifp, sizeof(struct ether_vlan_header));

	if ((err = iflib_netmap_attach(ctx))) {
		device_printf(ctx->ifc_dev, "netmap attach failed: %d\n", err);
		goto fail_detach;
	}
	*ctxp = ctx;

	NETDUMP_SET(ctx->ifc_ifp, iflib);

	if_setgetcounterfn(ctx->ifc_ifp, iflib_if_get_counter);
	iflib_add_device_sysctl_post(ctx);
	ctx->ifc_flags |= IFC_INIT_DONE;
	CTX_UNLOCK(ctx);
	return (0);

fail_detach:
	ether_ifdetach(ctx->ifc_ifp);
fail_intr_free:
	iflib_free_intr_mem(ctx);
fail_queues:
	iflib_tx_structures_free(ctx);
	iflib_rx_structures_free(ctx);
fail_iflib_detach:
	IFDI_DETACH(ctx);
fail_unlock:
	CTX_UNLOCK(ctx);
fail_ctx_free:
        if (ctx->ifc_flags & IFC_SC_ALLOCATED)
                free(ctx->ifc_softc, M_IFLIB);
        free(ctx, M_IFLIB);
	return (err);
}

int
iflib_pseudo_register(device_t dev, if_shared_ctx_t sctx, if_ctx_t *ctxp,
					  struct iflib_cloneattach_ctx *clctx)
{
	int err;
	if_ctx_t ctx;
	if_t ifp;
	if_softc_ctx_t scctx;
	int i;
	void *sc;
	uint16_t main_txq;
	uint16_t main_rxq;

	ctx = malloc(sizeof(*ctx), M_IFLIB, M_WAITOK|M_ZERO);
	sc = malloc(sctx->isc_driver->size, M_IFLIB, M_WAITOK|M_ZERO);
	ctx->ifc_flags |= IFC_SC_ALLOCATED;
	if (sctx->isc_flags & (IFLIB_PSEUDO|IFLIB_VIRTUAL))
		ctx->ifc_flags |= IFC_PSEUDO;

	ctx->ifc_sctx = sctx;
	ctx->ifc_softc = sc;
	ctx->ifc_dev = dev;

	if ((err = iflib_register(ctx)) != 0) {
		device_printf(dev, "%s: iflib_register failed %d\n", __func__, err);
		goto fail_ctx_free;
	}
	iflib_add_device_sysctl_pre(ctx);

	scctx = &ctx->ifc_softc_ctx;
	ifp = ctx->ifc_ifp;

	/*
	 * XXX sanity check that ntxd & nrxd are a power of 2
	 */
	iflib_reset_qvalues(ctx);

	if ((err = IFDI_ATTACH_PRE(ctx)) != 0) {
		device_printf(dev, "IFDI_ATTACH_PRE failed %d\n", err);
		goto fail_ctx_free;
	}
	if (sctx->isc_flags & IFLIB_GEN_MAC)
		iflib_gen_mac(ctx);
	if ((err = IFDI_CLONEATTACH(ctx, clctx->cc_ifc, clctx->cc_name,
								clctx->cc_params)) != 0) {
		device_printf(dev, "IFDI_CLONEATTACH failed %d\n", err);
		goto fail_ctx_free;
	}
	ifmedia_add(&ctx->ifc_media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
	ifmedia_add(&ctx->ifc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&ctx->ifc_media, IFM_ETHER | IFM_AUTO);

#ifdef INVARIANTS
	MPASS(scctx->isc_capabilities);
	if (scctx->isc_capabilities & IFCAP_TXCSUM)
		MPASS(scctx->isc_tx_csum_flags);
#endif

	if_setcapabilities(ifp, scctx->isc_capabilities | IFCAP_HWSTATS | IFCAP_LINKSTATE);
	if_setcapenable(ifp, scctx->isc_capenable | IFCAP_HWSTATS | IFCAP_LINKSTATE);

	ifp->if_flags |= IFF_NOGROUP;
	if (sctx->isc_flags & IFLIB_PSEUDO) {
		ether_ifattach(ctx->ifc_ifp, ctx->ifc_mac);

		if ((err = IFDI_ATTACH_POST(ctx)) != 0) {
			device_printf(dev, "IFDI_ATTACH_POST failed %d\n", err);
			goto fail_detach;
		}
		*ctxp = ctx;

		/*
		 * Tell the upper layer(s) if IFCAP_VLAN_MTU is supported.
		 * This must appear after the call to ether_ifattach() because
		 * ether_ifattach() sets if_hdrlen to the default value.
		 */
		if (if_getcapabilities(ifp) & IFCAP_VLAN_MTU)
			if_setifheaderlen(ifp,
			    sizeof(struct ether_vlan_header));

		if_setgetcounterfn(ctx->ifc_ifp, iflib_if_get_counter);
		iflib_add_device_sysctl_post(ctx);
		ctx->ifc_flags |= IFC_INIT_DONE;
		return (0);
	}
	_iflib_pre_assert(scctx);
	ctx->ifc_txrx = *scctx->isc_txrx;

	if (scctx->isc_ntxqsets == 0 || (scctx->isc_ntxqsets_max && scctx->isc_ntxqsets_max < scctx->isc_ntxqsets))
		scctx->isc_ntxqsets = scctx->isc_ntxqsets_max;
	if (scctx->isc_nrxqsets == 0 || (scctx->isc_nrxqsets_max && scctx->isc_nrxqsets_max < scctx->isc_nrxqsets))
		scctx->isc_nrxqsets = scctx->isc_nrxqsets_max;

	main_txq = (sctx->isc_flags & IFLIB_HAS_TXCQ) ? 1 : 0;
	main_rxq = (sctx->isc_flags & IFLIB_HAS_RXCQ) ? 1 : 0;

	/* XXX change for per-queue sizes */
	device_printf(dev, "Using %d tx descriptors and %d rx descriptors\n",
	    scctx->isc_ntxd[main_txq], scctx->isc_nrxd[main_rxq]);
	for (i = 0; i < sctx->isc_nrxqs; i++) {
		if (!powerof2(scctx->isc_nrxd[i])) {
			/* round down instead? */
			device_printf(dev, "# rx descriptors must be a power of 2\n");
			err = EINVAL;
			goto fail_iflib_detach;
		}
	}
	for (i = 0; i < sctx->isc_ntxqs; i++) {
		if (!powerof2(scctx->isc_ntxd[i])) {
			device_printf(dev,
			    "# tx descriptors must be a power of 2");
			err = EINVAL;
			goto fail_iflib_detach;
		}
	}

	if (scctx->isc_tx_nsegments > scctx->isc_ntxd[main_txq] /
	    MAX_SINGLE_PACKET_FRACTION)
		scctx->isc_tx_nsegments = max(1, scctx->isc_ntxd[main_txq] /
		    MAX_SINGLE_PACKET_FRACTION);
	if (scctx->isc_tx_tso_segments_max > scctx->isc_ntxd[main_txq] /
	    MAX_SINGLE_PACKET_FRACTION)
		scctx->isc_tx_tso_segments_max = max(1,
		    scctx->isc_ntxd[main_txq] / MAX_SINGLE_PACKET_FRACTION);

	/* TSO parameters - dig these out of the data sheet - simply correspond to tag setup */
	if (if_getcapabilities(ifp) & IFCAP_TSO) {
		/*
		 * The stack can't handle a TSO size larger than IP_MAXPACKET,
		 * but some MACs do.
		 */
		if_sethwtsomax(ifp, min(scctx->isc_tx_tso_size_max,
		    IP_MAXPACKET));
		/*
		 * Take maximum number of m_pullup(9)'s in iflib_parse_header()
		 * into account.  In the worst case, each of these calls will
		 * add another mbuf and, thus, the requirement for another DMA
		 * segment.  So for best performance, it doesn't make sense to
		 * advertize a maximum of TSO segments that typically will
		 * require defragmentation in iflib_encap().
		 */
		if_sethwtsomaxsegcount(ifp, scctx->isc_tx_tso_segments_max - 3);
		if_sethwtsomaxsegsize(ifp, scctx->isc_tx_tso_segsize_max);
	}
	if (scctx->isc_rss_table_size == 0)
		scctx->isc_rss_table_size = 64;
	scctx->isc_rss_table_mask = scctx->isc_rss_table_size-1;

	GROUPTASK_INIT(&ctx->ifc_admin_task, 0, _task_fn_admin, ctx);
	/* XXX format name */
	taskqgroup_attach(qgroup_if_config_tqg, &ctx->ifc_admin_task, ctx,
	    NULL, NULL, "admin");

	/* XXX --- can support > 1 -- but keep it simple for now */
	scctx->isc_intr = IFLIB_INTR_LEGACY;

	/* Get memory for the station queues */
	if ((err = iflib_queues_alloc(ctx))) {
		device_printf(dev, "Unable to allocate queue memory\n");
		goto fail_iflib_detach;
	}

	if ((err = iflib_qset_structures_setup(ctx))) {
		device_printf(dev, "qset structure setup failed %d\n", err);
		goto fail_queues;
	}

	/*
	 * XXX What if anything do we want to do about interrupts?
	 */
	ether_ifattach(ctx->ifc_ifp, ctx->ifc_mac);
	if ((err = IFDI_ATTACH_POST(ctx)) != 0) {
		device_printf(dev, "IFDI_ATTACH_POST failed %d\n", err);
		goto fail_detach;
	}

	/*
	 * Tell the upper layer(s) if IFCAP_VLAN_MTU is supported.
	 * This must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets if_hdrlen to the default value.
	 */
	if (if_getcapabilities(ifp) & IFCAP_VLAN_MTU)
		if_setifheaderlen(ifp, sizeof(struct ether_vlan_header));

	/* XXX handle more than one queue */
	for (i = 0; i < scctx->isc_nrxqsets; i++)
		IFDI_RX_CLSET(ctx, 0, i, ctx->ifc_rxqs[i].ifr_fl[0].ifl_sds.ifsd_cl);

	*ctxp = ctx;

	if_setgetcounterfn(ctx->ifc_ifp, iflib_if_get_counter);
	iflib_add_device_sysctl_post(ctx);
	ctx->ifc_flags |= IFC_INIT_DONE;
	return (0);
fail_detach:
	ether_ifdetach(ctx->ifc_ifp);
fail_queues:
	iflib_tx_structures_free(ctx);
	iflib_rx_structures_free(ctx);
fail_iflib_detach:
	IFDI_DETACH(ctx);
fail_ctx_free:
	free(ctx->ifc_softc, M_IFLIB);
	free(ctx, M_IFLIB);
	return (err);
}

int
iflib_pseudo_deregister(if_ctx_t ctx)
{
	if_t ifp = ctx->ifc_ifp;
	iflib_txq_t txq;
	iflib_rxq_t rxq;
	int i, j;
	struct taskqgroup *tqg;
	iflib_fl_t fl;

	/* Unregister VLAN events */
	if (ctx->ifc_vlan_attach_event != NULL)
		EVENTHANDLER_DEREGISTER(vlan_config, ctx->ifc_vlan_attach_event);
	if (ctx->ifc_vlan_detach_event != NULL)
		EVENTHANDLER_DEREGISTER(vlan_unconfig, ctx->ifc_vlan_detach_event);

	ether_ifdetach(ifp);
	/* ether_ifdetach calls if_qflush - lock must be destroy afterwards*/
	CTX_LOCK_DESTROY(ctx);
	/* XXX drain any dependent tasks */
	tqg = qgroup_if_io_tqg;
	for (txq = ctx->ifc_txqs, i = 0; i < NTXQSETS(ctx); i++, txq++) {
		callout_drain(&txq->ift_timer);
		if (txq->ift_task.gt_uniq != NULL)
			taskqgroup_detach(tqg, &txq->ift_task);
	}
	for (i = 0, rxq = ctx->ifc_rxqs; i < NRXQSETS(ctx); i++, rxq++) {
		if (rxq->ifr_task.gt_uniq != NULL)
			taskqgroup_detach(tqg, &rxq->ifr_task);

		for (j = 0, fl = rxq->ifr_fl; j < rxq->ifr_nfl; j++, fl++)
			free(fl->ifl_rx_bitmap, M_IFLIB);
	}
	tqg = qgroup_if_config_tqg;
	if (ctx->ifc_admin_task.gt_uniq != NULL)
		taskqgroup_detach(tqg, &ctx->ifc_admin_task);
	if (ctx->ifc_vflr_task.gt_uniq != NULL)
		taskqgroup_detach(tqg, &ctx->ifc_vflr_task);

	if_free(ifp);

	iflib_tx_structures_free(ctx);
	iflib_rx_structures_free(ctx);
	if (ctx->ifc_flags & IFC_SC_ALLOCATED)
		free(ctx->ifc_softc, M_IFLIB);
	free(ctx, M_IFLIB);
	return (0);
}

int
iflib_device_attach(device_t dev)
{
	if_ctx_t ctx;
	if_shared_ctx_t sctx;

	if ((sctx = DEVICE_REGISTER(dev)) == NULL || sctx->isc_magic != IFLIB_MAGIC)
		return (ENOTSUP);

	pci_enable_busmaster(dev);

	return (iflib_device_register(dev, NULL, sctx, &ctx));
}

int
iflib_device_deregister(if_ctx_t ctx)
{
	if_t ifp = ctx->ifc_ifp;
	iflib_txq_t txq;
	iflib_rxq_t rxq;
	device_t dev = ctx->ifc_dev;
	int i, j;
	struct taskqgroup *tqg;
	iflib_fl_t fl;

	/* Make sure VLANS are not using driver */
	if (if_vlantrunkinuse(ifp)) {
		device_printf(dev, "Vlan in use, detach first\n");
		return (EBUSY);
	}
#ifdef PCI_IOV
	if (!CTX_IS_VF(ctx) && pci_iov_detach(dev) != 0) {
		device_printf(dev, "SR-IOV in use; detach first.\n");
		return (EBUSY);
	}
#endif

	STATE_LOCK(ctx);
	ctx->ifc_flags |= IFC_IN_DETACH;
	STATE_UNLOCK(ctx);

	CTX_LOCK(ctx);
	iflib_stop(ctx);
	CTX_UNLOCK(ctx);

	/* Unregister VLAN events */
	if (ctx->ifc_vlan_attach_event != NULL)
		EVENTHANDLER_DEREGISTER(vlan_config, ctx->ifc_vlan_attach_event);
	if (ctx->ifc_vlan_detach_event != NULL)
		EVENTHANDLER_DEREGISTER(vlan_unconfig, ctx->ifc_vlan_detach_event);

	iflib_netmap_detach(ifp);
	ether_ifdetach(ifp);
	if (ctx->ifc_led_dev != NULL)
		led_destroy(ctx->ifc_led_dev);
	/* XXX drain any dependent tasks */
	tqg = qgroup_if_io_tqg;
	for (txq = ctx->ifc_txqs, i = 0; i < NTXQSETS(ctx); i++, txq++) {
		callout_drain(&txq->ift_timer);
		if (txq->ift_task.gt_uniq != NULL)
			taskqgroup_detach(tqg, &txq->ift_task);
	}
	for (i = 0, rxq = ctx->ifc_rxqs; i < NRXQSETS(ctx); i++, rxq++) {
		if (rxq->ifr_task.gt_uniq != NULL)
			taskqgroup_detach(tqg, &rxq->ifr_task);

		for (j = 0, fl = rxq->ifr_fl; j < rxq->ifr_nfl; j++, fl++)
			free(fl->ifl_rx_bitmap, M_IFLIB);
	}
	tqg = qgroup_if_config_tqg;
	if (ctx->ifc_admin_task.gt_uniq != NULL)
		taskqgroup_detach(tqg, &ctx->ifc_admin_task);
	if (ctx->ifc_vflr_task.gt_uniq != NULL)
		taskqgroup_detach(tqg, &ctx->ifc_vflr_task);
	CTX_LOCK(ctx);
	IFDI_DETACH(ctx);
	CTX_UNLOCK(ctx);

	/* ether_ifdetach calls if_qflush - lock must be destroy afterwards*/
	CTX_LOCK_DESTROY(ctx);
	device_set_softc(ctx->ifc_dev, NULL);
	iflib_free_intr_mem(ctx);

	bus_generic_detach(dev);
	if_free(ifp);

	iflib_tx_structures_free(ctx);
	iflib_rx_structures_free(ctx);
	if (ctx->ifc_flags & IFC_SC_ALLOCATED)
		free(ctx->ifc_softc, M_IFLIB);
	STATE_LOCK_DESTROY(ctx);
	free(ctx, M_IFLIB);
	return (0);
}

static void
iflib_free_intr_mem(if_ctx_t ctx)
{

	if (ctx->ifc_softc_ctx.isc_intr != IFLIB_INTR_MSIX) {
		iflib_irq_free(ctx, &ctx->ifc_legacy_irq);
	}
	if (ctx->ifc_softc_ctx.isc_intr != IFLIB_INTR_LEGACY) {
		pci_release_msi(ctx->ifc_dev);
	}
	if (ctx->ifc_msix_mem != NULL) {
		bus_release_resource(ctx->ifc_dev, SYS_RES_MEMORY,
		    rman_get_rid(ctx->ifc_msix_mem), ctx->ifc_msix_mem);
		ctx->ifc_msix_mem = NULL;
	}
}

int
iflib_device_detach(device_t dev)
{
	if_ctx_t ctx = device_get_softc(dev);

	return (iflib_device_deregister(ctx));
}

int
iflib_device_suspend(device_t dev)
{
	if_ctx_t ctx = device_get_softc(dev);

	CTX_LOCK(ctx);
	IFDI_SUSPEND(ctx);
	CTX_UNLOCK(ctx);

	return bus_generic_suspend(dev);
}
int
iflib_device_shutdown(device_t dev)
{
	if_ctx_t ctx = device_get_softc(dev);

	CTX_LOCK(ctx);
	IFDI_SHUTDOWN(ctx);
	CTX_UNLOCK(ctx);

	return bus_generic_suspend(dev);
}


int
iflib_device_resume(device_t dev)
{
	if_ctx_t ctx = device_get_softc(dev);
	iflib_txq_t txq = ctx->ifc_txqs;

	CTX_LOCK(ctx);
	IFDI_RESUME(ctx);
	iflib_if_init_locked(ctx);
	CTX_UNLOCK(ctx);
	for (int i = 0; i < NTXQSETS(ctx); i++, txq++)
		iflib_txq_check_drain(txq, IFLIB_RESTART_BUDGET);

	return (bus_generic_resume(dev));
}

int
iflib_device_iov_init(device_t dev, uint16_t num_vfs, const nvlist_t *params)
{
	int error;
	if_ctx_t ctx = device_get_softc(dev);

	CTX_LOCK(ctx);
	error = IFDI_IOV_INIT(ctx, num_vfs, params);
	CTX_UNLOCK(ctx);

	return (error);
}

void
iflib_device_iov_uninit(device_t dev)
{
	if_ctx_t ctx = device_get_softc(dev);

	CTX_LOCK(ctx);
	IFDI_IOV_UNINIT(ctx);
	CTX_UNLOCK(ctx);
}

int
iflib_device_iov_add_vf(device_t dev, uint16_t vfnum, const nvlist_t *params)
{
	int error;
	if_ctx_t ctx = device_get_softc(dev);

	CTX_LOCK(ctx);
	error = IFDI_IOV_VF_ADD(ctx, vfnum, params);
	CTX_UNLOCK(ctx);

	return (error);
}

/*********************************************************************
 *
 *  MODULE FUNCTION DEFINITIONS
 *
 **********************************************************************/

/*
 * - Start a fast taskqueue thread for each core
 * - Start a taskqueue for control operations
 */
static int
iflib_module_init(void)
{
	return (0);
}

static int
iflib_module_event_handler(module_t mod, int what, void *arg)
{
	int err;

	switch (what) {
	case MOD_LOAD:
		if ((err = iflib_module_init()) != 0)
			return (err);
		break;
	case MOD_UNLOAD:
		return (EBUSY);
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

/*********************************************************************
 *
 *  PUBLIC FUNCTION DEFINITIONS
 *     ordered as in iflib.h
 *
 **********************************************************************/


static void
_iflib_assert(if_shared_ctx_t sctx)
{
	MPASS(sctx->isc_tx_maxsize);
	MPASS(sctx->isc_tx_maxsegsize);

	MPASS(sctx->isc_rx_maxsize);
	MPASS(sctx->isc_rx_nsegments);
	MPASS(sctx->isc_rx_maxsegsize);

	MPASS(sctx->isc_nrxd_min[0]);
	MPASS(sctx->isc_nrxd_max[0]);
	MPASS(sctx->isc_nrxd_default[0]);
	MPASS(sctx->isc_ntxd_min[0]);
	MPASS(sctx->isc_ntxd_max[0]);
	MPASS(sctx->isc_ntxd_default[0]);
}

static void
_iflib_pre_assert(if_softc_ctx_t scctx)
{

	MPASS(scctx->isc_txrx->ift_txd_encap);
	MPASS(scctx->isc_txrx->ift_txd_flush);
	MPASS(scctx->isc_txrx->ift_txd_credits_update);
	MPASS(scctx->isc_txrx->ift_rxd_available);
	MPASS(scctx->isc_txrx->ift_rxd_pkt_get);
	MPASS(scctx->isc_txrx->ift_rxd_refill);
	MPASS(scctx->isc_txrx->ift_rxd_flush);
}

static int
iflib_register(if_ctx_t ctx)
{
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	driver_t *driver = sctx->isc_driver;
	device_t dev = ctx->ifc_dev;
	if_t ifp;

	_iflib_assert(sctx);

	CTX_LOCK_INIT(ctx);
	STATE_LOCK_INIT(ctx, device_get_nameunit(ctx->ifc_dev));
	ifp = ctx->ifc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not allocate ifnet structure\n");
		return (ENOMEM);
	}

	/*
	 * Initialize our context's device specific methods
	 */
	kobj_init((kobj_t) ctx, (kobj_class_t) driver);
	kobj_class_compile((kobj_class_t) driver);
	driver->refs++;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	if_setsoftc(ifp, ctx);
	if_setdev(ifp, dev);
	if_setinitfn(ifp, iflib_if_init);
	if_setioctlfn(ifp, iflib_if_ioctl);
#ifdef ALTQ
	if_setstartfn(ifp, iflib_altq_if_start);
	if_settransmitfn(ifp, iflib_altq_if_transmit);
	if_setsendqready(ifp);
#else
	if_settransmitfn(ifp, iflib_if_transmit);
#endif
	if_setqflushfn(ifp, iflib_if_qflush);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);

	ctx->ifc_vlan_attach_event =
		EVENTHANDLER_REGISTER(vlan_config, iflib_vlan_register, ctx,
							  EVENTHANDLER_PRI_FIRST);
	ctx->ifc_vlan_detach_event =
		EVENTHANDLER_REGISTER(vlan_unconfig, iflib_vlan_unregister, ctx,
							  EVENTHANDLER_PRI_FIRST);

	ifmedia_init(&ctx->ifc_media, IFM_IMASK,
					 iflib_media_change, iflib_media_status);

	return (0);
}


static int
iflib_queues_alloc(if_ctx_t ctx)
{
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	device_t dev = ctx->ifc_dev;
	int nrxqsets = scctx->isc_nrxqsets;
	int ntxqsets = scctx->isc_ntxqsets;
	iflib_txq_t txq;
	iflib_rxq_t rxq;
	iflib_fl_t fl = NULL;
	int i, j, cpu, err, txconf, rxconf;
	iflib_dma_info_t ifdip;
	uint32_t *rxqsizes = scctx->isc_rxqsizes;
	uint32_t *txqsizes = scctx->isc_txqsizes;
	uint8_t nrxqs = sctx->isc_nrxqs;
	uint8_t ntxqs = sctx->isc_ntxqs;
	int nfree_lists = sctx->isc_nfl ? sctx->isc_nfl : 1;
	caddr_t *vaddrs;
	uint64_t *paddrs;

	KASSERT(ntxqs > 0, ("number of queues per qset must be at least 1"));
	KASSERT(nrxqs > 0, ("number of queues per qset must be at least 1"));

	/* Allocate the TX ring struct memory */
	if (!(ctx->ifc_txqs =
	    (iflib_txq_t) malloc(sizeof(struct iflib_txq) *
	    ntxqsets, M_IFLIB, M_NOWAIT | M_ZERO))) {
		device_printf(dev, "Unable to allocate TX ring memory\n");
		err = ENOMEM;
		goto fail;
	}

	/* Now allocate the RX */
	if (!(ctx->ifc_rxqs =
	    (iflib_rxq_t) malloc(sizeof(struct iflib_rxq) *
	    nrxqsets, M_IFLIB, M_NOWAIT | M_ZERO))) {
		device_printf(dev, "Unable to allocate RX ring memory\n");
		err = ENOMEM;
		goto rx_fail;
	}

	txq = ctx->ifc_txqs;
	rxq = ctx->ifc_rxqs;

	/*
	 * XXX handle allocation failure
	 */
	for (txconf = i = 0, cpu = CPU_FIRST(); i < ntxqsets; i++, txconf++, txq++, cpu = CPU_NEXT(cpu)) {
		/* Set up some basics */

		if ((ifdip = malloc(sizeof(struct iflib_dma_info) * ntxqs,
		    M_IFLIB, M_NOWAIT | M_ZERO)) == NULL) {
			device_printf(dev,
			    "Unable to allocate TX DMA info memory\n");
			err = ENOMEM;
			goto err_tx_desc;
		}
		txq->ift_ifdi = ifdip;
		for (j = 0; j < ntxqs; j++, ifdip++) {
			if (iflib_dma_alloc(ctx, txqsizes[j], ifdip, 0)) {
				device_printf(dev,
				    "Unable to allocate TX descriptors\n");
				err = ENOMEM;
				goto err_tx_desc;
			}
			txq->ift_txd_size[j] = scctx->isc_txd_size[j];
			bzero((void *)ifdip->idi_vaddr, txqsizes[j]);
		}
		txq->ift_ctx = ctx;
		txq->ift_id = i;
		if (sctx->isc_flags & IFLIB_HAS_TXCQ) {
			txq->ift_br_offset = 1;
		} else {
			txq->ift_br_offset = 0;
		}
		/* XXX fix this */
		txq->ift_timer.c_cpu = cpu;

		if (iflib_txsd_alloc(txq)) {
			device_printf(dev, "Critical Failure setting up TX buffers\n");
			err = ENOMEM;
			goto err_tx_desc;
		}

		/* Initialize the TX lock */
		snprintf(txq->ift_mtx_name, MTX_NAME_LEN, "%s:tx(%d):callout",
		    device_get_nameunit(dev), txq->ift_id);
		mtx_init(&txq->ift_mtx, txq->ift_mtx_name, NULL, MTX_DEF);
		callout_init_mtx(&txq->ift_timer, &txq->ift_mtx, 0);

		snprintf(txq->ift_db_mtx_name, MTX_NAME_LEN, "%s:tx(%d):db",
			 device_get_nameunit(dev), txq->ift_id);

		err = ifmp_ring_alloc(&txq->ift_br, 2048, txq, iflib_txq_drain,
				      iflib_txq_can_drain, M_IFLIB, M_WAITOK);
		if (err) {
			/* XXX free any allocated rings */
			device_printf(dev, "Unable to allocate buf_ring\n");
			goto err_tx_desc;
		}
	}

	for (rxconf = i = 0; i < nrxqsets; i++, rxconf++, rxq++) {
		/* Set up some basics */

		if ((ifdip = malloc(sizeof(struct iflib_dma_info) * nrxqs,
		   M_IFLIB, M_NOWAIT | M_ZERO)) == NULL) {
			device_printf(dev,
			    "Unable to allocate RX DMA info memory\n");
			err = ENOMEM;
			goto err_tx_desc;
		}

		rxq->ifr_ifdi = ifdip;
		/* XXX this needs to be changed if #rx queues != #tx queues */
		rxq->ifr_ntxqirq = 1;
		rxq->ifr_txqid[0] = i;
		for (j = 0; j < nrxqs; j++, ifdip++) {
			if (iflib_dma_alloc(ctx, rxqsizes[j], ifdip, 0)) {
				device_printf(dev,
				    "Unable to allocate RX descriptors\n");
				err = ENOMEM;
				goto err_tx_desc;
			}
			bzero((void *)ifdip->idi_vaddr, rxqsizes[j]);
		}
		rxq->ifr_ctx = ctx;
		rxq->ifr_id = i;
		if (sctx->isc_flags & IFLIB_HAS_RXCQ) {
			rxq->ifr_fl_offset = 1;
		} else {
			rxq->ifr_fl_offset = 0;
		}
		rxq->ifr_nfl = nfree_lists;
		if (!(fl =
			  (iflib_fl_t) malloc(sizeof(struct iflib_fl) * nfree_lists, M_IFLIB, M_NOWAIT | M_ZERO))) {
			device_printf(dev, "Unable to allocate free list memory\n");
			err = ENOMEM;
			goto err_tx_desc;
		}
		rxq->ifr_fl = fl;
		for (j = 0; j < nfree_lists; j++) {
			fl[j].ifl_rxq = rxq;
			fl[j].ifl_id = j;
			fl[j].ifl_ifdi = &rxq->ifr_ifdi[j + rxq->ifr_fl_offset];
			fl[j].ifl_rxd_size = scctx->isc_rxd_size[j];
		}
		/* Allocate receive buffers for the ring */
		if (iflib_rxsd_alloc(rxq)) {
			device_printf(dev,
			    "Critical Failure setting up receive buffers\n");
			err = ENOMEM;
			goto err_rx_desc;
		}

		for (j = 0, fl = rxq->ifr_fl; j < rxq->ifr_nfl; j++, fl++) 
			fl->ifl_rx_bitmap = bit_alloc(fl->ifl_size, M_IFLIB,
			    M_WAITOK);
	}

	/* TXQs */
	vaddrs = malloc(sizeof(caddr_t)*ntxqsets*ntxqs, M_IFLIB, M_WAITOK);
	paddrs = malloc(sizeof(uint64_t)*ntxqsets*ntxqs, M_IFLIB, M_WAITOK);
	for (i = 0; i < ntxqsets; i++) {
		iflib_dma_info_t di = ctx->ifc_txqs[i].ift_ifdi;

		for (j = 0; j < ntxqs; j++, di++) {
			vaddrs[i*ntxqs + j] = di->idi_vaddr;
			paddrs[i*ntxqs + j] = di->idi_paddr;
		}
	}
	if ((err = IFDI_TX_QUEUES_ALLOC(ctx, vaddrs, paddrs, ntxqs, ntxqsets)) != 0) {
		device_printf(ctx->ifc_dev,
		    "Unable to allocate device TX queue\n");
		iflib_tx_structures_free(ctx);
		free(vaddrs, M_IFLIB);
		free(paddrs, M_IFLIB);
		goto err_rx_desc;
	}
	free(vaddrs, M_IFLIB);
	free(paddrs, M_IFLIB);

	/* RXQs */
	vaddrs = malloc(sizeof(caddr_t)*nrxqsets*nrxqs, M_IFLIB, M_WAITOK);
	paddrs = malloc(sizeof(uint64_t)*nrxqsets*nrxqs, M_IFLIB, M_WAITOK);
	for (i = 0; i < nrxqsets; i++) {
		iflib_dma_info_t di = ctx->ifc_rxqs[i].ifr_ifdi;

		for (j = 0; j < nrxqs; j++, di++) {
			vaddrs[i*nrxqs + j] = di->idi_vaddr;
			paddrs[i*nrxqs + j] = di->idi_paddr;
		}
	}
	if ((err = IFDI_RX_QUEUES_ALLOC(ctx, vaddrs, paddrs, nrxqs, nrxqsets)) != 0) {
		device_printf(ctx->ifc_dev,
		    "Unable to allocate device RX queue\n");
		iflib_tx_structures_free(ctx);
		free(vaddrs, M_IFLIB);
		free(paddrs, M_IFLIB);
		goto err_rx_desc;
	}
	free(vaddrs, M_IFLIB);
	free(paddrs, M_IFLIB);

	return (0);

/* XXX handle allocation failure changes */
err_rx_desc:
err_tx_desc:
rx_fail:
	if (ctx->ifc_rxqs != NULL)
		free(ctx->ifc_rxqs, M_IFLIB);
	ctx->ifc_rxqs = NULL;
	if (ctx->ifc_txqs != NULL)
		free(ctx->ifc_txqs, M_IFLIB);
	ctx->ifc_txqs = NULL;
fail:
	return (err);
}

static int
iflib_tx_structures_setup(if_ctx_t ctx)
{
	iflib_txq_t txq = ctx->ifc_txqs;
	int i;

	for (i = 0; i < NTXQSETS(ctx); i++, txq++)
		iflib_txq_setup(txq);

	return (0);
}

static void
iflib_tx_structures_free(if_ctx_t ctx)
{
	iflib_txq_t txq = ctx->ifc_txqs;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	int i, j;

	for (i = 0; i < NTXQSETS(ctx); i++, txq++) {
		iflib_txq_destroy(txq);
		for (j = 0; j < sctx->isc_ntxqs; j++)
			iflib_dma_free(&txq->ift_ifdi[j]);
	}
	free(ctx->ifc_txqs, M_IFLIB);
	ctx->ifc_txqs = NULL;
	IFDI_QUEUES_FREE(ctx);
}

/*********************************************************************
 *
 *  Initialize all receive rings.
 *
 **********************************************************************/
static int
iflib_rx_structures_setup(if_ctx_t ctx)
{
	iflib_rxq_t rxq = ctx->ifc_rxqs;
	int q;
#if defined(INET6) || defined(INET)
	int i, err;
#endif

	for (q = 0; q < ctx->ifc_softc_ctx.isc_nrxqsets; q++, rxq++) {
#if defined(INET6) || defined(INET)
		tcp_lro_free(&rxq->ifr_lc);
		if ((err = tcp_lro_init_args(&rxq->ifr_lc, ctx->ifc_ifp,
		    TCP_LRO_ENTRIES, min(1024,
		    ctx->ifc_softc_ctx.isc_nrxd[rxq->ifr_fl_offset]))) != 0) {
			device_printf(ctx->ifc_dev, "LRO Initialization failed!\n");
			goto fail;
		}
		rxq->ifr_lro_enabled = TRUE;
#endif
		IFDI_RXQ_SETUP(ctx, rxq->ifr_id);
	}
	return (0);
#if defined(INET6) || defined(INET)
fail:
	/*
	 * Free RX software descriptors allocated so far, we will only handle
	 * the rings that completed, the failing case will have
	 * cleaned up for itself. 'q' failed, so its the terminus.
	 */
	rxq = ctx->ifc_rxqs;
	for (i = 0; i < q; ++i, rxq++) {
		iflib_rx_sds_free(rxq);
		rxq->ifr_cq_gen = rxq->ifr_cq_cidx = rxq->ifr_cq_pidx = 0;
	}
	return (err);
#endif
}

/*********************************************************************
 *
 *  Free all receive rings.
 *
 **********************************************************************/
static void
iflib_rx_structures_free(if_ctx_t ctx)
{
	iflib_rxq_t rxq = ctx->ifc_rxqs;

	for (int i = 0; i < ctx->ifc_softc_ctx.isc_nrxqsets; i++, rxq++) {
		iflib_rx_sds_free(rxq);
	}
	free(ctx->ifc_rxqs, M_IFLIB);
	ctx->ifc_rxqs = NULL;
}

static int
iflib_qset_structures_setup(if_ctx_t ctx)
{
	int err;

	/*
	 * It is expected that the caller takes care of freeing queues if this
	 * fails.
	 */
	if ((err = iflib_tx_structures_setup(ctx)) != 0) {
		device_printf(ctx->ifc_dev, "iflib_tx_structures_setup failed: %d\n", err);
		return (err);
	}

	if ((err = iflib_rx_structures_setup(ctx)) != 0)
		device_printf(ctx->ifc_dev, "iflib_rx_structures_setup failed: %d\n", err);

	return (err);
}

int
iflib_irq_alloc(if_ctx_t ctx, if_irq_t irq, int rid,
		driver_filter_t filter, void *filter_arg, driver_intr_t handler, void *arg, const char *name)
{

	return (_iflib_irq_alloc(ctx, irq, rid, filter, handler, arg, name));
}

#ifdef SMP
static int
find_nth(if_ctx_t ctx, int qid)
{
	cpuset_t cpus;
	int i, cpuid, eqid, count;

	CPU_COPY(&ctx->ifc_cpus, &cpus);
	count = CPU_COUNT(&cpus);
	eqid = qid % count;
	/* clear up to the qid'th bit */
	for (i = 0; i < eqid; i++) {
		cpuid = CPU_FFS(&cpus);
		MPASS(cpuid != 0);
		CPU_CLR(cpuid-1, &cpus);
	}
	cpuid = CPU_FFS(&cpus);
	MPASS(cpuid != 0);
	return (cpuid-1);
}

#ifdef SCHED_ULE
extern struct cpu_group *cpu_top;              /* CPU topology */

static int
find_child_with_core(int cpu, struct cpu_group *grp)
{
	int i;

	if (grp->cg_children == 0)
		return -1;

	MPASS(grp->cg_child);
	for (i = 0; i < grp->cg_children; i++) {
		if (CPU_ISSET(cpu, &grp->cg_child[i].cg_mask))
			return i;
	}

	return -1;
}

/*
 * Find the nth "close" core to the specified core
 * "close" is defined as the deepest level that shares
 * at least an L2 cache.  With threads, this will be
 * threads on the same core.  If the sahred cache is L3
 * or higher, simply returns the same core.
 */
static int
find_close_core(int cpu, int core_offset)
{
	struct cpu_group *grp;
	int i;
	int fcpu;
	cpuset_t cs;

	grp = cpu_top;
	if (grp == NULL)
		return cpu;
	i = 0;
	while ((i = find_child_with_core(cpu, grp)) != -1) {
		/* If the child only has one cpu, don't descend */
		if (grp->cg_child[i].cg_count <= 1)
			break;
		grp = &grp->cg_child[i];
	}

	/* If they don't share at least an L2 cache, use the same CPU */
	if (grp->cg_level > CG_SHARE_L2 || grp->cg_level == CG_SHARE_NONE)
		return cpu;

	/* Now pick one */
	CPU_COPY(&grp->cg_mask, &cs);

	/* Add the selected CPU offset to core offset. */
	for (i = 0; (fcpu = CPU_FFS(&cs)) != 0; i++) {
		if (fcpu - 1 == cpu)
			break;
		CPU_CLR(fcpu - 1, &cs);
	}
	MPASS(fcpu);

	core_offset += i;

	CPU_COPY(&grp->cg_mask, &cs);
	for (i = core_offset % grp->cg_count; i > 0; i--) {
		MPASS(CPU_FFS(&cs));
		CPU_CLR(CPU_FFS(&cs) - 1, &cs);
	}
	MPASS(CPU_FFS(&cs));
	return CPU_FFS(&cs) - 1;
}
#else
static int
find_close_core(int cpu, int core_offset __unused)
{
	return cpu;
}
#endif

static int
get_core_offset(if_ctx_t ctx, iflib_intr_type_t type, int qid)
{
	switch (type) {
	case IFLIB_INTR_TX:
		/* TX queues get cores which share at least an L2 cache with the corresponding RX queue */
		/* XXX handle multiple RX threads per core and more than two core per L2 group */
		return qid / CPU_COUNT(&ctx->ifc_cpus) + 1;
	case IFLIB_INTR_RX:
	case IFLIB_INTR_RXTX:
		/* RX queues get the specified core */
		return qid / CPU_COUNT(&ctx->ifc_cpus);
	default:
		return -1;
	}
}
#else
#define get_core_offset(ctx, type, qid)	CPU_FIRST()
#define find_close_core(cpuid, tid)	CPU_FIRST()
#define find_nth(ctx, gid)		CPU_FIRST()
#endif

/* Just to avoid copy/paste */
static inline int
iflib_irq_set_affinity(if_ctx_t ctx, if_irq_t irq, iflib_intr_type_t type,
    int qid, struct grouptask *gtask, struct taskqgroup *tqg, void *uniq,
    const char *name)
{
	device_t dev;
	int err, cpuid, tid;

	dev = ctx->ifc_dev;
	cpuid = find_nth(ctx, qid);
	tid = get_core_offset(ctx, type, qid);
	MPASS(tid >= 0);
	cpuid = find_close_core(cpuid, tid);
	err = taskqgroup_attach_cpu(tqg, gtask, uniq, cpuid, dev, irq->ii_res,
	    name);
	if (err) {
		device_printf(dev, "taskqgroup_attach_cpu failed %d\n", err);
		return (err);
	}
#ifdef notyet
	if (cpuid > ctx->ifc_cpuid_highest)
		ctx->ifc_cpuid_highest = cpuid;
#endif
	return 0;
}

int
iflib_irq_alloc_generic(if_ctx_t ctx, if_irq_t irq, int rid,
			iflib_intr_type_t type, driver_filter_t *filter,
			void *filter_arg, int qid, const char *name)
{
	device_t dev;
	struct grouptask *gtask;
	struct taskqgroup *tqg;
	iflib_filter_info_t info;
	gtask_fn_t *fn;
	int tqrid, err;
	driver_filter_t *intr_fast;
	void *q;

	info = &ctx->ifc_filter_info;
	tqrid = rid;

	switch (type) {
	/* XXX merge tx/rx for netmap? */
	case IFLIB_INTR_TX:
		q = &ctx->ifc_txqs[qid];
		info = &ctx->ifc_txqs[qid].ift_filter_info;
		gtask = &ctx->ifc_txqs[qid].ift_task;
		tqg = qgroup_if_io_tqg;
		fn = _task_fn_tx;
		intr_fast = iflib_fast_intr;
		GROUPTASK_INIT(gtask, 0, fn, q);
		ctx->ifc_flags |= IFC_NETMAP_TX_IRQ;
		break;
	case IFLIB_INTR_RX:
		q = &ctx->ifc_rxqs[qid];
		info = &ctx->ifc_rxqs[qid].ifr_filter_info;
		gtask = &ctx->ifc_rxqs[qid].ifr_task;
		tqg = qgroup_if_io_tqg;
		fn = _task_fn_rx;
		intr_fast = iflib_fast_intr;
		GROUPTASK_INIT(gtask, 0, fn, q);
		break;
	case IFLIB_INTR_RXTX:
		q = &ctx->ifc_rxqs[qid];
		info = &ctx->ifc_rxqs[qid].ifr_filter_info;
		gtask = &ctx->ifc_rxqs[qid].ifr_task;
		tqg = qgroup_if_io_tqg;
		fn = _task_fn_rx;
		intr_fast = iflib_fast_intr_rxtx;
		GROUPTASK_INIT(gtask, 0, fn, q);
		break;
	case IFLIB_INTR_ADMIN:
		q = ctx;
		tqrid = -1;
		info = &ctx->ifc_filter_info;
		gtask = &ctx->ifc_admin_task;
		tqg = qgroup_if_config_tqg;
		fn = _task_fn_admin;
		intr_fast = iflib_fast_intr_ctx;
		break;
	default:
		panic("unknown net intr type");
	}

	info->ifi_filter = filter;
	info->ifi_filter_arg = filter_arg;
	info->ifi_task = gtask;
	info->ifi_ctx = q;

	dev = ctx->ifc_dev;
	err = _iflib_irq_alloc(ctx, irq, rid, intr_fast, NULL, info,  name);
	if (err != 0) {
		device_printf(dev, "_iflib_irq_alloc failed %d\n", err);
		return (err);
	}
	if (type == IFLIB_INTR_ADMIN)
		return (0);

	if (tqrid != -1) {
		err = iflib_irq_set_affinity(ctx, irq, type, qid, gtask, tqg,
		    q, name);
		if (err)
			return (err);
	} else {
		taskqgroup_attach(tqg, gtask, q, dev, irq->ii_res, name);
	}

	return (0);
}

void
iflib_softirq_alloc_generic(if_ctx_t ctx, if_irq_t irq, iflib_intr_type_t type, void *arg, int qid, const char *name)
{
	struct grouptask *gtask;
	struct taskqgroup *tqg;
	gtask_fn_t *fn;
	void *q;
	int err;

	switch (type) {
	case IFLIB_INTR_TX:
		q = &ctx->ifc_txqs[qid];
		gtask = &ctx->ifc_txqs[qid].ift_task;
		tqg = qgroup_if_io_tqg;
		fn = _task_fn_tx;
		break;
	case IFLIB_INTR_RX:
		q = &ctx->ifc_rxqs[qid];
		gtask = &ctx->ifc_rxqs[qid].ifr_task;
		tqg = qgroup_if_io_tqg;
		fn = _task_fn_rx;
		break;
	case IFLIB_INTR_IOV:
		q = ctx;
		gtask = &ctx->ifc_vflr_task;
		tqg = qgroup_if_config_tqg;
		fn = _task_fn_iov;
		break;
	default:
		panic("unknown net intr type");
	}
	GROUPTASK_INIT(gtask, 0, fn, q);
	if (irq != NULL) {
		err = iflib_irq_set_affinity(ctx, irq, type, qid, gtask, tqg,
		    q, name);
		if (err)
			taskqgroup_attach(tqg, gtask, q, ctx->ifc_dev,
			    irq->ii_res, name);
	} else {
		taskqgroup_attach(tqg, gtask, q, NULL, NULL, name);
	}
}

void
iflib_irq_free(if_ctx_t ctx, if_irq_t irq)
{

	if (irq->ii_tag)
		bus_teardown_intr(ctx->ifc_dev, irq->ii_res, irq->ii_tag);

	if (irq->ii_res)
		bus_release_resource(ctx->ifc_dev, SYS_RES_IRQ,
		    rman_get_rid(irq->ii_res), irq->ii_res);
}

static int
iflib_legacy_setup(if_ctx_t ctx, driver_filter_t filter, void *filter_arg, int *rid, const char *name)
{
	iflib_txq_t txq = ctx->ifc_txqs;
	iflib_rxq_t rxq = ctx->ifc_rxqs;
	if_irq_t irq = &ctx->ifc_legacy_irq;
	iflib_filter_info_t info;
	device_t dev;
	struct grouptask *gtask;
	struct resource *res;
	struct taskqgroup *tqg;
	gtask_fn_t *fn;
	int tqrid;
	void *q;
	int err;

	q = &ctx->ifc_rxqs[0];
	info = &rxq[0].ifr_filter_info;
	gtask = &rxq[0].ifr_task;
	tqg = qgroup_if_io_tqg;
	tqrid = irq->ii_rid = *rid;
	fn = _task_fn_rx;

	ctx->ifc_flags |= IFC_LEGACY;
	info->ifi_filter = filter;
	info->ifi_filter_arg = filter_arg;
	info->ifi_task = gtask;
	info->ifi_ctx = ctx;

	dev = ctx->ifc_dev;
	/* We allocate a single interrupt resource */
	if ((err = _iflib_irq_alloc(ctx, irq, tqrid, iflib_fast_intr_ctx, NULL, info, name)) != 0)
		return (err);
	GROUPTASK_INIT(gtask, 0, fn, q);
	res = irq->ii_res;
	taskqgroup_attach(tqg, gtask, q, dev, res, name);

	GROUPTASK_INIT(&txq->ift_task, 0, _task_fn_tx, txq);
	taskqgroup_attach(qgroup_if_io_tqg, &txq->ift_task, txq, dev, res,
	    "tx");
	return (0);
}

void
iflib_led_create(if_ctx_t ctx)
{

	ctx->ifc_led_dev = led_create(iflib_led_func, ctx,
	    device_get_nameunit(ctx->ifc_dev));
}

void
iflib_tx_intr_deferred(if_ctx_t ctx, int txqid)
{

	GROUPTASK_ENQUEUE(&ctx->ifc_txqs[txqid].ift_task);
}

void
iflib_rx_intr_deferred(if_ctx_t ctx, int rxqid)
{

	GROUPTASK_ENQUEUE(&ctx->ifc_rxqs[rxqid].ifr_task);
}

void
iflib_admin_intr_deferred(if_ctx_t ctx)
{
#ifdef INVARIANTS
	struct grouptask *gtask;

	gtask = &ctx->ifc_admin_task;
	MPASS(gtask != NULL && gtask->gt_taskqueue != NULL);
#endif

	GROUPTASK_ENQUEUE(&ctx->ifc_admin_task);
}

void
iflib_iov_intr_deferred(if_ctx_t ctx)
{

	GROUPTASK_ENQUEUE(&ctx->ifc_vflr_task);
}

void
iflib_io_tqg_attach(struct grouptask *gt, void *uniq, int cpu, char *name)
{

	taskqgroup_attach_cpu(qgroup_if_io_tqg, gt, uniq, cpu, NULL, NULL,
	    name);
}

void
iflib_config_gtask_init(void *ctx, struct grouptask *gtask, gtask_fn_t *fn,
	const char *name)
{

	GROUPTASK_INIT(gtask, 0, fn, ctx);
	taskqgroup_attach(qgroup_if_config_tqg, gtask, gtask, NULL, NULL,
	    name);
}

void
iflib_config_gtask_deinit(struct grouptask *gtask)
{

	taskqgroup_detach(qgroup_if_config_tqg, gtask);	
}

void
iflib_link_state_change(if_ctx_t ctx, int link_state, uint64_t baudrate)
{
	if_t ifp = ctx->ifc_ifp;
	iflib_txq_t txq = ctx->ifc_txqs;

	if_setbaudrate(ifp, baudrate);
	if (baudrate >= IF_Gbps(10)) {
		STATE_LOCK(ctx);
		ctx->ifc_flags |= IFC_PREFETCH;
		STATE_UNLOCK(ctx);
	}
	/* If link down, disable watchdog */
	if ((ctx->ifc_link_state == LINK_STATE_UP) && (link_state == LINK_STATE_DOWN)) {
		for (int i = 0; i < ctx->ifc_softc_ctx.isc_ntxqsets; i++, txq++)
			txq->ift_qstatus = IFLIB_QUEUE_IDLE;
	}
	ctx->ifc_link_state = link_state;
	if_link_state_change(ifp, link_state);
}

static int
iflib_tx_credits_update(if_ctx_t ctx, iflib_txq_t txq)
{
	int credits;
#ifdef INVARIANTS
	int credits_pre = txq->ift_cidx_processed;
#endif

	if (ctx->isc_txd_credits_update == NULL)
		return (0);

	bus_dmamap_sync(txq->ift_ifdi->idi_tag, txq->ift_ifdi->idi_map,
	    BUS_DMASYNC_POSTREAD);
	if ((credits = ctx->isc_txd_credits_update(ctx->ifc_softc, txq->ift_id, true)) == 0)
		return (0);

	txq->ift_processed += credits;
	txq->ift_cidx_processed += credits;

	MPASS(credits_pre + credits == txq->ift_cidx_processed);
	if (txq->ift_cidx_processed >= txq->ift_size)
		txq->ift_cidx_processed -= txq->ift_size;
	return (credits);
}

static int
iflib_rxd_avail(if_ctx_t ctx, iflib_rxq_t rxq, qidx_t cidx, qidx_t budget)
{
	iflib_fl_t fl;
	u_int i;

	for (i = 0, fl = &rxq->ifr_fl[0]; i < rxq->ifr_nfl; i++, fl++)
		bus_dmamap_sync(fl->ifl_ifdi->idi_tag, fl->ifl_ifdi->idi_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	return (ctx->isc_rxd_available(ctx->ifc_softc, rxq->ifr_id, cidx,
	    budget));
}

void
iflib_add_int_delay_sysctl(if_ctx_t ctx, const char *name,
	const char *description, if_int_delay_info_t info,
	int offset, int value)
{
	info->iidi_ctx = ctx;
	info->iidi_offset = offset;
	info->iidi_value = value;
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(ctx->ifc_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(ctx->ifc_dev)),
	    OID_AUTO, name, CTLTYPE_INT|CTLFLAG_RW,
	    info, 0, iflib_sysctl_int_delay, "I", description);
}

struct sx *
iflib_ctx_lock_get(if_ctx_t ctx)
{

	return (&ctx->ifc_ctx_sx);
}

static int
iflib_msix_init(if_ctx_t ctx)
{
	device_t dev = ctx->ifc_dev;
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
	int vectors, queues, rx_queues, tx_queues, queuemsgs, msgs;
	int iflib_num_tx_queues, iflib_num_rx_queues;
	int err, admincnt, bar;

	iflib_num_tx_queues = ctx->ifc_sysctl_ntxqs;
	iflib_num_rx_queues = ctx->ifc_sysctl_nrxqs;

	if (bootverbose)
		device_printf(dev, "msix_init qsets capped at %d\n",
		    imax(scctx->isc_ntxqsets, scctx->isc_nrxqsets));

	bar = ctx->ifc_softc_ctx.isc_msix_bar;
	admincnt = sctx->isc_admin_intrcnt;
	/* Override by tuneable */
	if (scctx->isc_disable_msix)
		goto msi;

	/* First try MSI-X */
	if ((msgs = pci_msix_count(dev)) == 0) {
		if (bootverbose)
			device_printf(dev, "MSI-X not supported or disabled\n");
		goto msi;
	}
	/*
	 * bar == -1 => "trust me I know what I'm doing"
	 * Some drivers are for hardware that is so shoddily
	 * documented that no one knows which bars are which
	 * so the developer has to map all bars. This hack
	 * allows shoddy garbage to use MSI-X in this framework.
	 */
	if (bar != -1) {
		ctx->ifc_msix_mem = bus_alloc_resource_any(dev,
	            SYS_RES_MEMORY, &bar, RF_ACTIVE);
		if (ctx->ifc_msix_mem == NULL) {
			device_printf(dev, "Unable to map MSI-X table\n");
			goto msi;
		}
	}
#if IFLIB_DEBUG
	/* use only 1 qset in debug mode */
	queuemsgs = min(msgs - admincnt, 1);
#else
	queuemsgs = msgs - admincnt;
#endif
#ifdef RSS
	queues = imin(queuemsgs, rss_getnumbuckets());
#else
	queues = queuemsgs;
#endif
	queues = imin(CPU_COUNT(&ctx->ifc_cpus), queues);
	if (bootverbose)
		device_printf(dev,
		    "intr CPUs: %d queue msgs: %d admincnt: %d\n",
		    CPU_COUNT(&ctx->ifc_cpus), queuemsgs, admincnt);
#ifdef  RSS
	/* If we're doing RSS, clamp at the number of RSS buckets */
	if (queues > rss_getnumbuckets())
		queues = rss_getnumbuckets();
#endif
	if (iflib_num_rx_queues > 0 && iflib_num_rx_queues < queuemsgs - admincnt)
		rx_queues = iflib_num_rx_queues;
	else
		rx_queues = queues;

	if (rx_queues > scctx->isc_nrxqsets)
		rx_queues = scctx->isc_nrxqsets;

	/*
	 * We want this to be all logical CPUs by default
	 */
	if (iflib_num_tx_queues > 0 && iflib_num_tx_queues < queues)
		tx_queues = iflib_num_tx_queues;
	else
		tx_queues = mp_ncpus;

	if (tx_queues > scctx->isc_ntxqsets)
		tx_queues = scctx->isc_ntxqsets;

	if (ctx->ifc_sysctl_qs_eq_override == 0) {
#ifdef INVARIANTS
		if (tx_queues != rx_queues)
			device_printf(dev,
			    "queue equality override not set, capping rx_queues at %d and tx_queues at %d\n",
			    min(rx_queues, tx_queues), min(rx_queues, tx_queues));
#endif
		tx_queues = min(rx_queues, tx_queues);
		rx_queues = min(rx_queues, tx_queues);
	}

	device_printf(dev, "Using %d rx queues %d tx queues\n",
	    rx_queues, tx_queues);

	vectors = rx_queues + admincnt;
	if ((err = pci_alloc_msix(dev, &vectors)) == 0) {
		device_printf(dev, "Using MSI-X interrupts with %d vectors\n",
		    vectors);
		scctx->isc_vectors = vectors;
		scctx->isc_nrxqsets = rx_queues;
		scctx->isc_ntxqsets = tx_queues;
		scctx->isc_intr = IFLIB_INTR_MSIX;

		return (vectors);
	} else {
		device_printf(dev,
		    "failed to allocate %d MSI-X vectors, err: %d - using MSI\n",
		    vectors, err);
		bus_release_resource(dev, SYS_RES_MEMORY, bar,
		    ctx->ifc_msix_mem);
		ctx->ifc_msix_mem = NULL;
	}
msi:
	vectors = pci_msi_count(dev);
	scctx->isc_nrxqsets = 1;
	scctx->isc_ntxqsets = 1;
	scctx->isc_vectors = vectors;
	if (vectors == 1 && pci_alloc_msi(dev, &vectors) == 0) {
		device_printf(dev,"Using an MSI interrupt\n");
		scctx->isc_intr = IFLIB_INTR_MSI;
	} else {
		scctx->isc_vectors = 1;
		device_printf(dev,"Using a Legacy interrupt\n");
		scctx->isc_intr = IFLIB_INTR_LEGACY;
	}

	return (vectors);
}

static const char *ring_states[] = { "IDLE", "BUSY", "STALLED", "ABDICATED" };

static int
mp_ring_state_handler(SYSCTL_HANDLER_ARGS)
{
	int rc;
	uint16_t *state = ((uint16_t *)oidp->oid_arg1);
	struct sbuf *sb;
	const char *ring_state = "UNKNOWN";

	/* XXX needed ? */
	rc = sysctl_wire_old_buffer(req, 0);
	MPASS(rc == 0);
	if (rc != 0)
		return (rc);
	sb = sbuf_new_for_sysctl(NULL, NULL, 80, req);
	MPASS(sb != NULL);
	if (sb == NULL)
		return (ENOMEM);
	if (state[3] <= 3)
		ring_state = ring_states[state[3]];

	sbuf_printf(sb, "pidx_head: %04hd pidx_tail: %04hd cidx: %04hd state: %s",
		    state[0], state[1], state[2], ring_state);
	rc = sbuf_finish(sb);
	sbuf_delete(sb);
        return(rc);
}

enum iflib_ndesc_handler {
	IFLIB_NTXD_HANDLER,
	IFLIB_NRXD_HANDLER,
};

static int
mp_ndesc_handler(SYSCTL_HANDLER_ARGS)
{
	if_ctx_t ctx = (void *)arg1;
	enum iflib_ndesc_handler type = arg2;
	char buf[256] = {0};
	qidx_t *ndesc;
	char *p, *next;
	int nqs, rc, i;

	MPASS(type == IFLIB_NTXD_HANDLER || type == IFLIB_NRXD_HANDLER);

	nqs = 8;
	switch(type) {
	case IFLIB_NTXD_HANDLER:
		ndesc = ctx->ifc_sysctl_ntxds;
		if (ctx->ifc_sctx)
			nqs = ctx->ifc_sctx->isc_ntxqs;
		break;
	case IFLIB_NRXD_HANDLER:
		ndesc = ctx->ifc_sysctl_nrxds;
		if (ctx->ifc_sctx)
			nqs = ctx->ifc_sctx->isc_nrxqs;
		break;
	default:
			panic("unhandled type");
	}
	if (nqs == 0)
		nqs = 8;

	for (i=0; i<8; i++) {
		if (i >= nqs)
			break;
		if (i)
			strcat(buf, ",");
		sprintf(strchr(buf, 0), "%d", ndesc[i]);
	}

	rc = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (rc || req->newptr == NULL)
		return rc;

	for (i = 0, next = buf, p = strsep(&next, " ,"); i < 8 && p;
	    i++, p = strsep(&next, " ,")) {
		ndesc[i] = strtoul(p, NULL, 10);
	}

	return(rc);
}

#define NAME_BUFLEN 32
static void
iflib_add_device_sysctl_pre(if_ctx_t ctx)
{
        device_t dev = iflib_get_dev(ctx);
	struct sysctl_oid_list *child, *oid_list;
	struct sysctl_ctx_list *ctx_list;
	struct sysctl_oid *node;

	ctx_list = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	ctx->ifc_sysctl_node = node = SYSCTL_ADD_NODE(ctx_list, child, OID_AUTO, "iflib",
						      CTLFLAG_RD, NULL, "IFLIB fields");
	oid_list = SYSCTL_CHILDREN(node);

	SYSCTL_ADD_CONST_STRING(ctx_list, oid_list, OID_AUTO, "driver_version",
		       CTLFLAG_RD, ctx->ifc_sctx->isc_driver_version,
		       "driver version");

	SYSCTL_ADD_U16(ctx_list, oid_list, OID_AUTO, "override_ntxqs",
		       CTLFLAG_RWTUN, &ctx->ifc_sysctl_ntxqs, 0,
			"# of txqs to use, 0 => use default #");
	SYSCTL_ADD_U16(ctx_list, oid_list, OID_AUTO, "override_nrxqs",
		       CTLFLAG_RWTUN, &ctx->ifc_sysctl_nrxqs, 0,
			"# of rxqs to use, 0 => use default #");
	SYSCTL_ADD_U16(ctx_list, oid_list, OID_AUTO, "override_qs_enable",
		       CTLFLAG_RWTUN, &ctx->ifc_sysctl_qs_eq_override, 0,
                       "permit #txq != #rxq");
	SYSCTL_ADD_INT(ctx_list, oid_list, OID_AUTO, "disable_msix",
                      CTLFLAG_RWTUN, &ctx->ifc_softc_ctx.isc_disable_msix, 0,
                      "disable MSI-X (default 0)");
	SYSCTL_ADD_U16(ctx_list, oid_list, OID_AUTO, "rx_budget",
		       CTLFLAG_RWTUN, &ctx->ifc_sysctl_rx_budget, 0,
                       "set the rx budget");
	SYSCTL_ADD_U16(ctx_list, oid_list, OID_AUTO, "tx_abdicate",
		       CTLFLAG_RWTUN, &ctx->ifc_sysctl_tx_abdicate, 0,
		       "cause tx to abdicate instead of running to completion");

	/* XXX change for per-queue sizes */
	SYSCTL_ADD_PROC(ctx_list, oid_list, OID_AUTO, "override_ntxds",
		       CTLTYPE_STRING|CTLFLAG_RWTUN, ctx, IFLIB_NTXD_HANDLER,
                       mp_ndesc_handler, "A",
                       "list of # of tx descriptors to use, 0 = use default #");
	SYSCTL_ADD_PROC(ctx_list, oid_list, OID_AUTO, "override_nrxds",
		       CTLTYPE_STRING|CTLFLAG_RWTUN, ctx, IFLIB_NRXD_HANDLER,
                       mp_ndesc_handler, "A",
                       "list of # of rx descriptors to use, 0 = use default #");
}

static void
iflib_add_device_sysctl_post(if_ctx_t ctx)
{
	if_shared_ctx_t sctx = ctx->ifc_sctx;
	if_softc_ctx_t scctx = &ctx->ifc_softc_ctx;
        device_t dev = iflib_get_dev(ctx);
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx_list;
	iflib_fl_t fl;
	iflib_txq_t txq;
	iflib_rxq_t rxq;
	int i, j;
	char namebuf[NAME_BUFLEN];
	char *qfmt;
	struct sysctl_oid *queue_node, *fl_node, *node;
	struct sysctl_oid_list *queue_list, *fl_list;
	ctx_list = device_get_sysctl_ctx(dev);

	node = ctx->ifc_sysctl_node;
	child = SYSCTL_CHILDREN(node);

	if (scctx->isc_ntxqsets > 100)
		qfmt = "txq%03d";
	else if (scctx->isc_ntxqsets > 10)
		qfmt = "txq%02d";
	else
		qfmt = "txq%d";
	for (i = 0, txq = ctx->ifc_txqs; i < scctx->isc_ntxqsets; i++, txq++) {
		snprintf(namebuf, NAME_BUFLEN, qfmt, i);
		queue_node = SYSCTL_ADD_NODE(ctx_list, child, OID_AUTO, namebuf,
					     CTLFLAG_RD, NULL, "Queue Name");
		queue_list = SYSCTL_CHILDREN(queue_node);
#if MEMORY_LOGGING
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "txq_dequeued",
				CTLFLAG_RD,
				&txq->ift_dequeued, "total mbufs freed");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "txq_enqueued",
				CTLFLAG_RD,
				&txq->ift_enqueued, "total mbufs enqueued");
#endif
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "mbuf_defrag",
				   CTLFLAG_RD,
				   &txq->ift_mbuf_defrag, "# of times m_defrag was called");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "m_pullups",
				   CTLFLAG_RD,
				   &txq->ift_pullups, "# of times m_pullup was called");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "mbuf_defrag_failed",
				   CTLFLAG_RD,
				   &txq->ift_mbuf_defrag_failed, "# of times m_defrag failed");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "no_desc_avail",
				   CTLFLAG_RD,
				   &txq->ift_no_desc_avail, "# of times no descriptors were available");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "tx_map_failed",
				   CTLFLAG_RD,
				   &txq->ift_map_failed, "# of times dma map failed");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "txd_encap_efbig",
				   CTLFLAG_RD,
				   &txq->ift_txd_encap_efbig, "# of times txd_encap returned EFBIG");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "no_tx_dma_setup",
				   CTLFLAG_RD,
				   &txq->ift_no_tx_dma_setup, "# of times map failed for other than EFBIG");
		SYSCTL_ADD_U16(ctx_list, queue_list, OID_AUTO, "txq_pidx",
				   CTLFLAG_RD,
				   &txq->ift_pidx, 1, "Producer Index");
		SYSCTL_ADD_U16(ctx_list, queue_list, OID_AUTO, "txq_cidx",
				   CTLFLAG_RD,
				   &txq->ift_cidx, 1, "Consumer Index");
		SYSCTL_ADD_U16(ctx_list, queue_list, OID_AUTO, "txq_cidx_processed",
				   CTLFLAG_RD,
				   &txq->ift_cidx_processed, 1, "Consumer Index seen by credit update");
		SYSCTL_ADD_U16(ctx_list, queue_list, OID_AUTO, "txq_in_use",
				   CTLFLAG_RD,
				   &txq->ift_in_use, 1, "descriptors in use");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "txq_processed",
				   CTLFLAG_RD,
				   &txq->ift_processed, "descriptors procesed for clean");
		SYSCTL_ADD_QUAD(ctx_list, queue_list, OID_AUTO, "txq_cleaned",
				   CTLFLAG_RD,
				   &txq->ift_cleaned, "total cleaned");
		SYSCTL_ADD_PROC(ctx_list, queue_list, OID_AUTO, "ring_state",
				CTLTYPE_STRING | CTLFLAG_RD, __DEVOLATILE(uint64_t *, &txq->ift_br->state),
				0, mp_ring_state_handler, "A", "soft ring state");
		SYSCTL_ADD_COUNTER_U64(ctx_list, queue_list, OID_AUTO, "r_enqueues",
				       CTLFLAG_RD, &txq->ift_br->enqueues,
				       "# of enqueues to the mp_ring for this queue");
		SYSCTL_ADD_COUNTER_U64(ctx_list, queue_list, OID_AUTO, "r_drops",
				       CTLFLAG_RD, &txq->ift_br->drops,
				       "# of drops in the mp_ring for this queue");
		SYSCTL_ADD_COUNTER_U64(ctx_list, queue_list, OID_AUTO, "r_starts",
				       CTLFLAG_RD, &txq->ift_br->starts,
				       "# of normal consumer starts in the mp_ring for this queue");
		SYSCTL_ADD_COUNTER_U64(ctx_list, queue_list, OID_AUTO, "r_stalls",
				       CTLFLAG_RD, &txq->ift_br->stalls,
					       "# of consumer stalls in the mp_ring for this queue");
		SYSCTL_ADD_COUNTER_U64(ctx_list, queue_list, OID_AUTO, "r_restarts",
			       CTLFLAG_RD, &txq->ift_br->restarts,
				       "# of consumer restarts in the mp_ring for this queue");
		SYSCTL_ADD_COUNTER_U64(ctx_list, queue_list, OID_AUTO, "r_abdications",
				       CTLFLAG_RD, &txq->ift_br->abdications,
				       "# of consumer abdications in the mp_ring for this queue");
	}

	if (scctx->isc_nrxqsets > 100)
		qfmt = "rxq%03d";
	else if (scctx->isc_nrxqsets > 10)
		qfmt = "rxq%02d";
	else
		qfmt = "rxq%d";
	for (i = 0, rxq = ctx->ifc_rxqs; i < scctx->isc_nrxqsets; i++, rxq++) {
		snprintf(namebuf, NAME_BUFLEN, qfmt, i);
		queue_node = SYSCTL_ADD_NODE(ctx_list, child, OID_AUTO, namebuf,
					     CTLFLAG_RD, NULL, "Queue Name");
		queue_list = SYSCTL_CHILDREN(queue_node);
		if (sctx->isc_flags & IFLIB_HAS_RXCQ) {
			SYSCTL_ADD_U16(ctx_list, queue_list, OID_AUTO, "rxq_cq_pidx",
				       CTLFLAG_RD,
				       &rxq->ifr_cq_pidx, 1, "Producer Index");
			SYSCTL_ADD_U16(ctx_list, queue_list, OID_AUTO, "rxq_cq_cidx",
				       CTLFLAG_RD,
				       &rxq->ifr_cq_cidx, 1, "Consumer Index");
		}

		for (j = 0, fl = rxq->ifr_fl; j < rxq->ifr_nfl; j++, fl++) {
			snprintf(namebuf, NAME_BUFLEN, "rxq_fl%d", j);
			fl_node = SYSCTL_ADD_NODE(ctx_list, queue_list, OID_AUTO, namebuf,
						     CTLFLAG_RD, NULL, "freelist Name");
			fl_list = SYSCTL_CHILDREN(fl_node);
			SYSCTL_ADD_U16(ctx_list, fl_list, OID_AUTO, "pidx",
				       CTLFLAG_RD,
				       &fl->ifl_pidx, 1, "Producer Index");
			SYSCTL_ADD_U16(ctx_list, fl_list, OID_AUTO, "cidx",
				       CTLFLAG_RD,
				       &fl->ifl_cidx, 1, "Consumer Index");
			SYSCTL_ADD_U16(ctx_list, fl_list, OID_AUTO, "credits",
				       CTLFLAG_RD,
				       &fl->ifl_credits, 1, "credits available");
#if MEMORY_LOGGING
			SYSCTL_ADD_QUAD(ctx_list, fl_list, OID_AUTO, "fl_m_enqueued",
					CTLFLAG_RD,
					&fl->ifl_m_enqueued, "mbufs allocated");
			SYSCTL_ADD_QUAD(ctx_list, fl_list, OID_AUTO, "fl_m_dequeued",
					CTLFLAG_RD,
					&fl->ifl_m_dequeued, "mbufs freed");
			SYSCTL_ADD_QUAD(ctx_list, fl_list, OID_AUTO, "fl_cl_enqueued",
					CTLFLAG_RD,
					&fl->ifl_cl_enqueued, "clusters allocated");
			SYSCTL_ADD_QUAD(ctx_list, fl_list, OID_AUTO, "fl_cl_dequeued",
					CTLFLAG_RD,
					&fl->ifl_cl_dequeued, "clusters freed");
#endif

		}
	}

}

void
iflib_request_reset(if_ctx_t ctx)
{

	STATE_LOCK(ctx);
	ctx->ifc_flags |= IFC_DO_RESET;
	STATE_UNLOCK(ctx);
}

#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *
iflib_fixup_rx(struct mbuf *m)
{
	struct mbuf *n;

	if (m->m_len <= (MCLBYTES - ETHER_HDR_LEN)) {
		bcopy(m->m_data, m->m_data + ETHER_HDR_LEN, m->m_len);
		m->m_data += ETHER_HDR_LEN;
		n = m;
	} else {
		MGETHDR(n, M_NOWAIT, MT_DATA);
		if (n == NULL) {
			m_freem(m);
			return (NULL);
		}
		bcopy(m->m_data, n->m_data, ETHER_HDR_LEN);
		m->m_data += ETHER_HDR_LEN;
		m->m_len -= ETHER_HDR_LEN;
		n->m_len = ETHER_HDR_LEN;
		M_MOVE_PKTHDR(n, m);
		n->m_next = m;
	}
	return (n);
}
#endif

#ifdef NETDUMP
static void
iflib_netdump_init(struct ifnet *ifp, int *nrxr, int *ncl, int *clsize)
{
	if_ctx_t ctx;

	ctx = if_getsoftc(ifp);
	CTX_LOCK(ctx);
	*nrxr = NRXQSETS(ctx);
	*ncl = ctx->ifc_rxqs[0].ifr_fl->ifl_size;
	*clsize = ctx->ifc_rxqs[0].ifr_fl->ifl_buf_size;
	CTX_UNLOCK(ctx);
}

static void
iflib_netdump_event(struct ifnet *ifp, enum netdump_ev event)
{
	if_ctx_t ctx;
	if_softc_ctx_t scctx;
	iflib_fl_t fl;
	iflib_rxq_t rxq;
	int i, j;

	ctx = if_getsoftc(ifp);
	scctx = &ctx->ifc_softc_ctx;

	switch (event) {
	case NETDUMP_START:
		for (i = 0; i < scctx->isc_nrxqsets; i++) {
			rxq = &ctx->ifc_rxqs[i];
			for (j = 0; j < rxq->ifr_nfl; j++) {
				fl = rxq->ifr_fl;
				fl->ifl_zone = m_getzone(fl->ifl_buf_size);
			}
		}
		iflib_no_tx_batch = 1;
		break;
	default:
		break;
	}
}

static int
iflib_netdump_transmit(struct ifnet *ifp, struct mbuf *m)
{
	if_ctx_t ctx;
	iflib_txq_t txq;
	int error;

	ctx = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (EBUSY);

	txq = &ctx->ifc_txqs[0];
	error = iflib_encap(txq, &m);
	if (error == 0)
		(void)iflib_txd_db_check(ctx, txq, true, txq->ift_in_use);
	return (error);
}

static int
iflib_netdump_poll(struct ifnet *ifp, int count)
{
	if_ctx_t ctx;
	if_softc_ctx_t scctx;
	iflib_txq_t txq;
	int i;

	ctx = if_getsoftc(ifp);
	scctx = &ctx->ifc_softc_ctx;

	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (EBUSY);

	txq = &ctx->ifc_txqs[0];
	(void)iflib_completed_tx_reclaim(txq, RECLAIM_THRESH(ctx));

	for (i = 0; i < scctx->isc_nrxqsets; i++)
		(void)iflib_rxeof(&ctx->ifc_rxqs[i], 16 /* XXX */);
	return (0);
}
#endif /* NETDUMP */
