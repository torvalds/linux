/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Kip Macy
 * Copyright (c) 2015 Wei Liu <wei.liu2@citrix.com>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/limits.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/tcp_lro.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/bus.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_intr.h>
#include <xen/gnttab.h>
#include <xen/interface/memory.h>
#include <xen/interface/io/netif.h>
#include <xen/xenbus/xenbusvar.h>

#include "xenbus_if.h"

/* Features supported by all backends.  TSO and LRO can be negotiated */
#define XN_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)

#define NET_TX_RING_SIZE __CONST_RING_SIZE(netif_tx, PAGE_SIZE)
#define NET_RX_RING_SIZE __CONST_RING_SIZE(netif_rx, PAGE_SIZE)

#define NET_RX_SLOTS_MIN (XEN_NETIF_NR_SLOTS_MIN + 1)

/*
 * Should the driver do LRO on the RX end
 *  this can be toggled on the fly, but the
 *  interface must be reset (down/up) for it
 *  to take effect.
 */
static int xn_enable_lro = 1;
TUNABLE_INT("hw.xn.enable_lro", &xn_enable_lro);

/*
 * Number of pairs of queues.
 */
static unsigned long xn_num_queues = 4;
TUNABLE_ULONG("hw.xn.num_queues", &xn_num_queues);

/**
 * \brief The maximum allowed data fragments in a single transmit
 *        request.
 *
 * This limit is imposed by the backend driver.  We assume here that
 * we are dealing with a Linux driver domain and have set our limit
 * to mirror the Linux MAX_SKB_FRAGS constant.
 */
#define	MAX_TX_REQ_FRAGS (65536 / PAGE_SIZE + 2)

#define RX_COPY_THRESHOLD 256

#define net_ratelimit() 0

struct netfront_rxq;
struct netfront_txq;
struct netfront_info;
struct netfront_rx_info;

static void xn_txeof(struct netfront_txq *);
static void xn_rxeof(struct netfront_rxq *);
static void xn_alloc_rx_buffers(struct netfront_rxq *);
static void xn_alloc_rx_buffers_callout(void *arg);

static void xn_release_rx_bufs(struct netfront_rxq *);
static void xn_release_tx_bufs(struct netfront_txq *);

static void xn_rxq_intr(struct netfront_rxq *);
static void xn_txq_intr(struct netfront_txq *);
static void xn_intr(void *);
static inline int xn_count_frags(struct mbuf *m);
static int xn_assemble_tx_request(struct netfront_txq *, struct mbuf *);
static int xn_ioctl(struct ifnet *, u_long, caddr_t);
static void xn_ifinit_locked(struct netfront_info *);
static void xn_ifinit(void *);
static void xn_stop(struct netfront_info *);
static void xn_query_features(struct netfront_info *np);
static int xn_configure_features(struct netfront_info *np);
static void netif_free(struct netfront_info *info);
static int netfront_detach(device_t dev);

static int xn_txq_mq_start_locked(struct netfront_txq *, struct mbuf *);
static int xn_txq_mq_start(struct ifnet *, struct mbuf *);

static int talk_to_backend(device_t dev, struct netfront_info *info);
static int create_netdev(device_t dev);
static void netif_disconnect_backend(struct netfront_info *info);
static int setup_device(device_t dev, struct netfront_info *info,
    unsigned long);
static int xn_ifmedia_upd(struct ifnet *ifp);
static void xn_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr);

static int xn_connect(struct netfront_info *);
static void xn_kick_rings(struct netfront_info *);

static int xn_get_responses(struct netfront_rxq *,
    struct netfront_rx_info *, RING_IDX, RING_IDX *,
    struct mbuf **);

#define virt_to_mfn(x) (vtophys(x) >> PAGE_SHIFT)

#define INVALID_P2M_ENTRY (~0UL)
#define XN_QUEUE_NAME_LEN  8	/* xn{t,r}x_%u, allow for two digits */
struct netfront_rxq {
	struct netfront_info 	*info;
	u_int			id;
	char			name[XN_QUEUE_NAME_LEN];
	struct mtx		lock;

	int			ring_ref;
	netif_rx_front_ring_t 	ring;
	xen_intr_handle_t	xen_intr_handle;

	grant_ref_t 		gref_head;
	grant_ref_t 		grant_ref[NET_RX_RING_SIZE + 1];

	struct mbuf		*mbufs[NET_RX_RING_SIZE + 1];

	struct lro_ctrl		lro;

	struct callout		rx_refill;
};

struct netfront_txq {
	struct netfront_info 	*info;
	u_int 			id;
	char			name[XN_QUEUE_NAME_LEN];
	struct mtx		lock;

	int			ring_ref;
	netif_tx_front_ring_t	ring;
	xen_intr_handle_t 	xen_intr_handle;

	grant_ref_t		gref_head;
	grant_ref_t		grant_ref[NET_TX_RING_SIZE + 1];

	struct mbuf		*mbufs[NET_TX_RING_SIZE + 1];
	int			mbufs_cnt;
	struct buf_ring		*br;

	struct taskqueue 	*tq;
	struct task       	defrtask;

	bool			full;
};

struct netfront_info {
	struct ifnet 		*xn_ifp;

	struct mtx   		sc_lock;

	u_int  num_queues;
	struct netfront_rxq 	*rxq;
	struct netfront_txq 	*txq;

	u_int			carrier;
	u_int			maxfrags;

	device_t		xbdev;
	uint8_t			mac[ETHER_ADDR_LEN];

	int			xn_if_flags;

	struct ifmedia		sc_media;

	bool			xn_reset;
};

struct netfront_rx_info {
	struct netif_rx_response rx;
	struct netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX - 1];
};

#define XN_RX_LOCK(_q)         mtx_lock(&(_q)->lock)
#define XN_RX_UNLOCK(_q)       mtx_unlock(&(_q)->lock)

#define XN_TX_LOCK(_q)         mtx_lock(&(_q)->lock)
#define XN_TX_TRYLOCK(_q)      mtx_trylock(&(_q)->lock)
#define XN_TX_UNLOCK(_q)       mtx_unlock(&(_q)->lock)

#define XN_LOCK(_sc)           mtx_lock(&(_sc)->sc_lock);
#define XN_UNLOCK(_sc)         mtx_unlock(&(_sc)->sc_lock);

#define XN_LOCK_ASSERT(_sc)    mtx_assert(&(_sc)->sc_lock, MA_OWNED);
#define XN_RX_LOCK_ASSERT(_q)  mtx_assert(&(_q)->lock, MA_OWNED);
#define XN_TX_LOCK_ASSERT(_q)  mtx_assert(&(_q)->lock, MA_OWNED);

#define netfront_carrier_on(netif)	((netif)->carrier = 1)
#define netfront_carrier_off(netif)	((netif)->carrier = 0)
#define netfront_carrier_ok(netif)	((netif)->carrier)

/* Access macros for acquiring freeing slots in xn_free_{tx,rx}_idxs[]. */

static inline void
add_id_to_freelist(struct mbuf **list, uintptr_t id)
{

	KASSERT(id != 0,
		("%s: the head item (0) must always be free.", __func__));
	list[id] = list[0];
	list[0]  = (struct mbuf *)id;
}

static inline unsigned short
get_id_from_freelist(struct mbuf **list)
{
	uintptr_t id;

	id = (uintptr_t)list[0];
	KASSERT(id != 0,
		("%s: the head item (0) must always remain free.", __func__));
	list[0] = list[id];
	return (id);
}

static inline int
xn_rxidx(RING_IDX idx)
{

	return idx & (NET_RX_RING_SIZE - 1);
}

static inline struct mbuf *
xn_get_rx_mbuf(struct netfront_rxq *rxq, RING_IDX ri)
{
	int i;
	struct mbuf *m;

	i = xn_rxidx(ri);
	m = rxq->mbufs[i];
	rxq->mbufs[i] = NULL;
	return (m);
}

static inline grant_ref_t
xn_get_rx_ref(struct netfront_rxq *rxq, RING_IDX ri)
{
	int i = xn_rxidx(ri);
	grant_ref_t ref = rxq->grant_ref[i];

	KASSERT(ref != GRANT_REF_INVALID, ("Invalid grant reference!\n"));
	rxq->grant_ref[i] = GRANT_REF_INVALID;
	return (ref);
}

#define IPRINTK(fmt, args...) \
    printf("[XEN] " fmt, ##args)
#ifdef INVARIANTS
#define WPRINTK(fmt, args...) \
    printf("[XEN] " fmt, ##args)
#else
#define WPRINTK(fmt, args...)
#endif
#ifdef DEBUG
#define DPRINTK(fmt, args...) \
    printf("[XEN] %s: " fmt, __func__, ##args)
#else
#define DPRINTK(fmt, args...)
#endif

/**
 * Read the 'mac' node at the given device's node in the store, and parse that
 * as colon-separated octets, placing result the given mac array.  mac must be
 * a preallocated array of length ETH_ALEN (as declared in linux/if_ether.h).
 * Return 0 on success, or errno on error.
 */
static int
xen_net_read_mac(device_t dev, uint8_t mac[])
{
	int error, i;
	char *s, *e, *macstr;
	const char *path;

	path = xenbus_get_node(dev);
	error = xs_read(XST_NIL, path, "mac", NULL, (void **) &macstr);
	if (error == ENOENT) {
		/*
		 * Deal with missing mac XenStore nodes on devices with
		 * HVM emulation (the 'ioemu' configuration attribute)
		 * enabled.
		 *
		 * The HVM emulator may execute in a stub device model
		 * domain which lacks the permission, only given to Dom0,
		 * to update the guest's XenStore tree.  For this reason,
		 * the HVM emulator doesn't even attempt to write the
		 * front-side mac node, even when operating in Dom0.
		 * However, there should always be a mac listed in the
		 * backend tree.  Fallback to this version if our query
		 * of the front side XenStore location doesn't find
		 * anything.
		 */
		path = xenbus_get_otherend_path(dev);
		error = xs_read(XST_NIL, path, "mac", NULL, (void **) &macstr);
	}
	if (error != 0) {
		xenbus_dev_fatal(dev, error, "parsing %s/mac", path);
		return (error);
	}

	s = macstr;
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		mac[i] = strtoul(s, &e, 16);
		if (s == e || (e[0] != ':' && e[0] != 0)) {
			free(macstr, M_XENBUS);
			return (ENOENT);
		}
		s = &e[1];
	}
	free(macstr, M_XENBUS);
	return (0);
}

/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures and the ring buffers for communication with the backend, and
 * inform the backend of the appropriate details for those.  Switch to
 * Connected state.
 */
static int
netfront_probe(device_t dev)
{

	if (xen_hvm_domain() && xen_disable_pv_nics != 0)
		return (ENXIO);

	if (!strcmp(xenbus_get_type(dev), "vif")) {
		device_set_desc(dev, "Virtual Network Interface");
		return (0);
	}

	return (ENXIO);
}

static int
netfront_attach(device_t dev)
{
	int err;

	err = create_netdev(dev);
	if (err != 0) {
		xenbus_dev_fatal(dev, err, "creating netdev");
		return (err);
	}

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "enable_lro", CTLFLAG_RW,
	    &xn_enable_lro, 0, "Large Receive Offload");

	SYSCTL_ADD_ULONG(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "num_queues", CTLFLAG_RD,
	    &xn_num_queues, "Number of pairs of queues");

	return (0);
}

static int
netfront_suspend(device_t dev)
{
	struct netfront_info *np = device_get_softc(dev);
	u_int i;

	for (i = 0; i < np->num_queues; i++) {
		XN_RX_LOCK(&np->rxq[i]);
		XN_TX_LOCK(&np->txq[i]);
	}
	netfront_carrier_off(np);
	for (i = 0; i < np->num_queues; i++) {
		XN_RX_UNLOCK(&np->rxq[i]);
		XN_TX_UNLOCK(&np->txq[i]);
	}
	return (0);
}

/**
 * We are reconnecting to the backend, due to a suspend/resume, or a backend
 * driver restart.  We tear down our netif structure and recreate it, but
 * leave the device-layer structures intact so that this is transparent to the
 * rest of the kernel.
 */
static int
netfront_resume(device_t dev)
{
	struct netfront_info *info = device_get_softc(dev);
	u_int i;

	if (xen_suspend_cancelled) {
		for (i = 0; i < info->num_queues; i++) {
			XN_RX_LOCK(&info->rxq[i]);
			XN_TX_LOCK(&info->txq[i]);
		}
		netfront_carrier_on(info);
		for (i = 0; i < info->num_queues; i++) {
			XN_RX_UNLOCK(&info->rxq[i]);
			XN_TX_UNLOCK(&info->txq[i]);
		}
		return (0);
	}

	netif_disconnect_backend(info);
	return (0);
}

static int
write_queue_xenstore_keys(device_t dev,
    struct netfront_rxq *rxq,
    struct netfront_txq *txq,
    struct xs_transaction *xst, bool hierarchy)
{
	int err;
	const char *message;
	const char *node = xenbus_get_node(dev);
	char *path;
	size_t path_size;

	KASSERT(rxq->id == txq->id, ("Mismatch between RX and TX queue ids"));
	/* Split event channel support is not yet there. */
	KASSERT(rxq->xen_intr_handle == txq->xen_intr_handle,
	    ("Split event channels are not supported"));

	if (hierarchy) {
		path_size = strlen(node) + 10;
		path = malloc(path_size, M_DEVBUF, M_WAITOK|M_ZERO);
		snprintf(path, path_size, "%s/queue-%u", node, rxq->id);
	} else {
		path_size = strlen(node) + 1;
		path = malloc(path_size, M_DEVBUF, M_WAITOK|M_ZERO);
		snprintf(path, path_size, "%s", node);
	}

	err = xs_printf(*xst, path, "tx-ring-ref","%u", txq->ring_ref);
	if (err != 0) {
		message = "writing tx ring-ref";
		goto error;
	}
	err = xs_printf(*xst, path, "rx-ring-ref","%u", rxq->ring_ref);
	if (err != 0) {
		message = "writing rx ring-ref";
		goto error;
	}
	err = xs_printf(*xst, path, "event-channel", "%u",
	    xen_intr_port(rxq->xen_intr_handle));
	if (err != 0) {
		message = "writing event-channel";
		goto error;
	}

	free(path, M_DEVBUF);

	return (0);

error:
	free(path, M_DEVBUF);
	xenbus_dev_fatal(dev, err, "%s", message);

	return (err);
}

/* Common code used when first setting up, and when resuming. */
static int
talk_to_backend(device_t dev, struct netfront_info *info)
{
	const char *message;
	struct xs_transaction xst;
	const char *node = xenbus_get_node(dev);
	int err;
	unsigned long num_queues, max_queues = 0;
	unsigned int i;

	err = xen_net_read_mac(dev, info->mac);
	if (err != 0) {
		xenbus_dev_fatal(dev, err, "parsing %s/mac", node);
		goto out;
	}

	err = xs_scanf(XST_NIL, xenbus_get_otherend_path(info->xbdev),
	    "multi-queue-max-queues", NULL, "%lu", &max_queues);
	if (err != 0)
		max_queues = 1;
	num_queues = xn_num_queues;
	if (num_queues > max_queues)
		num_queues = max_queues;

	err = setup_device(dev, info, num_queues);
	if (err != 0)
		goto out;

 again:
	err = xs_transaction_start(&xst);
	if (err != 0) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto free;
	}

	if (info->num_queues == 1) {
		err = write_queue_xenstore_keys(dev, &info->rxq[0],
		    &info->txq[0], &xst, false);
		if (err != 0)
			goto abort_transaction_no_def_error;
	} else {
		err = xs_printf(xst, node, "multi-queue-num-queues",
		    "%u", info->num_queues);
		if (err != 0) {
			message = "writing multi-queue-num-queues";
			goto abort_transaction;
		}

		for (i = 0; i < info->num_queues; i++) {
			err = write_queue_xenstore_keys(dev, &info->rxq[i],
			    &info->txq[i], &xst, true);
			if (err != 0)
				goto abort_transaction_no_def_error;
		}
	}

	err = xs_printf(xst, node, "request-rx-copy", "%u", 1);
	if (err != 0) {
		message = "writing request-rx-copy";
		goto abort_transaction;
	}
	err = xs_printf(xst, node, "feature-rx-notify", "%d", 1);
	if (err != 0) {
		message = "writing feature-rx-notify";
		goto abort_transaction;
	}
	err = xs_printf(xst, node, "feature-sg", "%d", 1);
	if (err != 0) {
		message = "writing feature-sg";
		goto abort_transaction;
	}
	if ((info->xn_ifp->if_capenable & IFCAP_LRO) != 0) {
		err = xs_printf(xst, node, "feature-gso-tcpv4", "%d", 1);
		if (err != 0) {
			message = "writing feature-gso-tcpv4";
			goto abort_transaction;
		}
	}
	if ((info->xn_ifp->if_capenable & IFCAP_RXCSUM) == 0) {
		err = xs_printf(xst, node, "feature-no-csum-offload", "%d", 1);
		if (err != 0) {
			message = "writing feature-no-csum-offload";
			goto abort_transaction;
		}
	}

	err = xs_transaction_end(xst, 0);
	if (err != 0) {
		if (err == EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto free;
	}

	return 0;

 abort_transaction:
	xenbus_dev_fatal(dev, err, "%s", message);
 abort_transaction_no_def_error:
	xs_transaction_end(xst, 1);
 free:
	netif_free(info);
 out:
	return (err);
}

static void
xn_rxq_intr(struct netfront_rxq *rxq)
{

	XN_RX_LOCK(rxq);
	xn_rxeof(rxq);
	XN_RX_UNLOCK(rxq);
}

static void
xn_txq_start(struct netfront_txq *txq)
{
	struct netfront_info *np = txq->info;
	struct ifnet *ifp = np->xn_ifp;

	XN_TX_LOCK_ASSERT(txq);
	if (!drbr_empty(ifp, txq->br))
		xn_txq_mq_start_locked(txq, NULL);
}

static void
xn_txq_intr(struct netfront_txq *txq)
{

	XN_TX_LOCK(txq);
	if (RING_HAS_UNCONSUMED_RESPONSES(&txq->ring))
		xn_txeof(txq);
	xn_txq_start(txq);
	XN_TX_UNLOCK(txq);
}

static void
xn_txq_tq_deferred(void *xtxq, int pending)
{
	struct netfront_txq *txq = xtxq;

	XN_TX_LOCK(txq);
	xn_txq_start(txq);
	XN_TX_UNLOCK(txq);
}

static void
disconnect_rxq(struct netfront_rxq *rxq)
{

	xn_release_rx_bufs(rxq);
	gnttab_free_grant_references(rxq->gref_head);
	gnttab_end_foreign_access(rxq->ring_ref, NULL);
	/*
	 * No split event channel support at the moment, handle will
	 * be unbound in tx. So no need to call xen_intr_unbind here,
	 * but we do want to reset the handler to 0.
	 */
	rxq->xen_intr_handle = 0;
}

static void
destroy_rxq(struct netfront_rxq *rxq)
{

	callout_drain(&rxq->rx_refill);
	free(rxq->ring.sring, M_DEVBUF);
}

static void
destroy_rxqs(struct netfront_info *np)
{
	int i;

	for (i = 0; i < np->num_queues; i++)
		destroy_rxq(&np->rxq[i]);

	free(np->rxq, M_DEVBUF);
	np->rxq = NULL;
}

static int
setup_rxqs(device_t dev, struct netfront_info *info,
	   unsigned long num_queues)
{
	int q, i;
	int error;
	netif_rx_sring_t *rxs;
	struct netfront_rxq *rxq;

	info->rxq = malloc(sizeof(struct netfront_rxq) * num_queues,
	    M_DEVBUF, M_WAITOK|M_ZERO);

	for (q = 0; q < num_queues; q++) {
		rxq = &info->rxq[q];

		rxq->id = q;
		rxq->info = info;
		rxq->ring_ref = GRANT_REF_INVALID;
		rxq->ring.sring = NULL;
		snprintf(rxq->name, XN_QUEUE_NAME_LEN, "xnrx_%u", q);
		mtx_init(&rxq->lock, rxq->name, "netfront receive lock",
		    MTX_DEF);

		for (i = 0; i <= NET_RX_RING_SIZE; i++) {
			rxq->mbufs[i] = NULL;
			rxq->grant_ref[i] = GRANT_REF_INVALID;
		}

		/* Start resources allocation */

		if (gnttab_alloc_grant_references(NET_RX_RING_SIZE,
		    &rxq->gref_head) != 0) {
			device_printf(dev, "allocating rx gref");
			error = ENOMEM;
			goto fail;
		}

		rxs = (netif_rx_sring_t *)malloc(PAGE_SIZE, M_DEVBUF,
		    M_WAITOK|M_ZERO);
		SHARED_RING_INIT(rxs);
		FRONT_RING_INIT(&rxq->ring, rxs, PAGE_SIZE);

		error = xenbus_grant_ring(dev, virt_to_mfn(rxs),
		    &rxq->ring_ref);
		if (error != 0) {
			device_printf(dev, "granting rx ring page");
			goto fail_grant_ring;
		}

		callout_init(&rxq->rx_refill, 1);
	}

	return (0);

fail_grant_ring:
	gnttab_free_grant_references(rxq->gref_head);
	free(rxq->ring.sring, M_DEVBUF);
fail:
	for (; q >= 0; q--) {
		disconnect_rxq(&info->rxq[q]);
		destroy_rxq(&info->rxq[q]);
	}

	free(info->rxq, M_DEVBUF);
	return (error);
}

static void
disconnect_txq(struct netfront_txq *txq)
{

	xn_release_tx_bufs(txq);
	gnttab_free_grant_references(txq->gref_head);
	gnttab_end_foreign_access(txq->ring_ref, NULL);
	xen_intr_unbind(&txq->xen_intr_handle);
}

static void
destroy_txq(struct netfront_txq *txq)
{

	free(txq->ring.sring, M_DEVBUF);
	buf_ring_free(txq->br, M_DEVBUF);
	taskqueue_drain_all(txq->tq);
	taskqueue_free(txq->tq);
}

static void
destroy_txqs(struct netfront_info *np)
{
	int i;

	for (i = 0; i < np->num_queues; i++)
		destroy_txq(&np->txq[i]);

	free(np->txq, M_DEVBUF);
	np->txq = NULL;
}

static int
setup_txqs(device_t dev, struct netfront_info *info,
	   unsigned long num_queues)
{
	int q, i;
	int error;
	netif_tx_sring_t *txs;
	struct netfront_txq *txq;

	info->txq = malloc(sizeof(struct netfront_txq) * num_queues,
	    M_DEVBUF, M_WAITOK|M_ZERO);

	for (q = 0; q < num_queues; q++) {
		txq = &info->txq[q];

		txq->id = q;
		txq->info = info;

		txq->ring_ref = GRANT_REF_INVALID;
		txq->ring.sring = NULL;

		snprintf(txq->name, XN_QUEUE_NAME_LEN, "xntx_%u", q);

		mtx_init(&txq->lock, txq->name, "netfront transmit lock",
		    MTX_DEF);

		for (i = 0; i <= NET_TX_RING_SIZE; i++) {
			txq->mbufs[i] = (void *) ((u_long) i+1);
			txq->grant_ref[i] = GRANT_REF_INVALID;
		}
		txq->mbufs[NET_TX_RING_SIZE] = (void *)0;

		/* Start resources allocation. */

		if (gnttab_alloc_grant_references(NET_TX_RING_SIZE,
		    &txq->gref_head) != 0) {
			device_printf(dev, "failed to allocate tx grant refs\n");
			error = ENOMEM;
			goto fail;
		}

		txs = (netif_tx_sring_t *)malloc(PAGE_SIZE, M_DEVBUF,
		    M_WAITOK|M_ZERO);
		SHARED_RING_INIT(txs);
		FRONT_RING_INIT(&txq->ring, txs, PAGE_SIZE);

		error = xenbus_grant_ring(dev, virt_to_mfn(txs),
		    &txq->ring_ref);
		if (error != 0) {
			device_printf(dev, "failed to grant tx ring\n");
			goto fail_grant_ring;
		}

		txq->br = buf_ring_alloc(NET_TX_RING_SIZE, M_DEVBUF,
		    M_WAITOK, &txq->lock);
		TASK_INIT(&txq->defrtask, 0, xn_txq_tq_deferred, txq);

		txq->tq = taskqueue_create(txq->name, M_WAITOK,
		    taskqueue_thread_enqueue, &txq->tq);

		error = taskqueue_start_threads(&txq->tq, 1, PI_NET,
		    "%s txq %d", device_get_nameunit(dev), txq->id);
		if (error != 0) {
			device_printf(dev, "failed to start tx taskq %d\n",
			    txq->id);
			goto fail_start_thread;
		}

		error = xen_intr_alloc_and_bind_local_port(dev,
		    xenbus_get_otherend_id(dev), /* filter */ NULL, xn_intr,
		    &info->txq[q], INTR_TYPE_NET | INTR_MPSAFE | INTR_ENTROPY,
		    &txq->xen_intr_handle);

		if (error != 0) {
			device_printf(dev, "xen_intr_alloc_and_bind_local_port failed\n");
			goto fail_bind_port;
		}
	}

	return (0);

fail_bind_port:
	taskqueue_drain_all(txq->tq);
fail_start_thread:
	buf_ring_free(txq->br, M_DEVBUF);
	taskqueue_free(txq->tq);
	gnttab_end_foreign_access(txq->ring_ref, NULL);
fail_grant_ring:
	gnttab_free_grant_references(txq->gref_head);
	free(txq->ring.sring, M_DEVBUF);
fail:
	for (; q >= 0; q--) {
		disconnect_txq(&info->txq[q]);
		destroy_txq(&info->txq[q]);
	}

	free(info->txq, M_DEVBUF);
	return (error);
}

static int
setup_device(device_t dev, struct netfront_info *info,
    unsigned long num_queues)
{
	int error;
	int q;

	if (info->txq)
		destroy_txqs(info);

	if (info->rxq)
		destroy_rxqs(info);

	info->num_queues = 0;

	error = setup_rxqs(dev, info, num_queues);
	if (error != 0)
		goto out;
	error = setup_txqs(dev, info, num_queues);
	if (error != 0)
		goto out;

	info->num_queues = num_queues;

	/* No split event channel at the moment. */
	for (q = 0; q < num_queues; q++)
		info->rxq[q].xen_intr_handle = info->txq[q].xen_intr_handle;

	return (0);

out:
	KASSERT(error != 0, ("Error path taken without providing an error code"));
	return (error);
}

#ifdef INET
/**
 * If this interface has an ipv4 address, send an arp for it. This
 * helps to get the network going again after migrating hosts.
 */
static void
netfront_send_fake_arp(device_t dev, struct netfront_info *info)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;

	ifp = info->xn_ifp;
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family == AF_INET) {
			arp_ifinit(ifp, ifa);
		}
	}
}
#endif

/**
 * Callback received when the backend's state changes.
 */
static void
netfront_backend_changed(device_t dev, XenbusState newstate)
{
	struct netfront_info *sc = device_get_softc(dev);

	DPRINTK("newstate=%d\n", newstate);

	CURVNET_SET(sc->xn_ifp->if_vnet);

	switch (newstate) {
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateUnknown:
	case XenbusStateReconfigured:
	case XenbusStateReconfiguring:
		break;
	case XenbusStateInitWait:
		if (xenbus_get_state(dev) != XenbusStateInitialising)
			break;
		if (xn_connect(sc) != 0)
			break;
		/* Switch to connected state before kicking the rings. */
		xenbus_set_state(sc->xbdev, XenbusStateConnected);
		xn_kick_rings(sc);
		break;
	case XenbusStateClosing:
		xenbus_set_state(dev, XenbusStateClosed);
		break;
	case XenbusStateClosed:
		if (sc->xn_reset) {
			netif_disconnect_backend(sc);
			xenbus_set_state(dev, XenbusStateInitialising);
			sc->xn_reset = false;
		}
		break;
	case XenbusStateConnected:
#ifdef INET
		netfront_send_fake_arp(dev, sc);
#endif
		break;
	}

	CURVNET_RESTORE();
}

/**
 * \brief Verify that there is sufficient space in the Tx ring
 *        buffer for a maximally sized request to be enqueued.
 *
 * A transmit request requires a transmit descriptor for each packet
 * fragment, plus up to 2 entries for "options" (e.g. TSO).
 */
static inline int
xn_tx_slot_available(struct netfront_txq *txq)
{

	return (RING_FREE_REQUESTS(&txq->ring) > (MAX_TX_REQ_FRAGS + 2));
}

static void
xn_release_tx_bufs(struct netfront_txq *txq)
{
	int i;

	for (i = 1; i <= NET_TX_RING_SIZE; i++) {
		struct mbuf *m;

		m = txq->mbufs[i];

		/*
		 * We assume that no kernel addresses are
		 * less than NET_TX_RING_SIZE.  Any entry
		 * in the table that is below this number
		 * must be an index from free-list tracking.
		 */
		if (((uintptr_t)m) <= NET_TX_RING_SIZE)
			continue;
		gnttab_end_foreign_access_ref(txq->grant_ref[i]);
		gnttab_release_grant_reference(&txq->gref_head,
		    txq->grant_ref[i]);
		txq->grant_ref[i] = GRANT_REF_INVALID;
		add_id_to_freelist(txq->mbufs, i);
		txq->mbufs_cnt--;
		if (txq->mbufs_cnt < 0) {
			panic("%s: tx_chain_cnt must be >= 0", __func__);
		}
		m_free(m);
	}
}

static struct mbuf *
xn_alloc_one_rx_buffer(struct netfront_rxq *rxq)
{
	struct mbuf *m;

	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
	if (m == NULL)
		return NULL;
	m->m_len = m->m_pkthdr.len = MJUMPAGESIZE;

	return (m);
}

static void
xn_alloc_rx_buffers(struct netfront_rxq *rxq)
{
	RING_IDX req_prod;
	int notify;

	XN_RX_LOCK_ASSERT(rxq);

	if (__predict_false(rxq->info->carrier == 0))
		return;

	for (req_prod = rxq->ring.req_prod_pvt;
	     req_prod - rxq->ring.rsp_cons < NET_RX_RING_SIZE;
	     req_prod++) {
		struct mbuf *m;
		unsigned short id;
		grant_ref_t ref;
		struct netif_rx_request *req;
		unsigned long pfn;

		m = xn_alloc_one_rx_buffer(rxq);
		if (m == NULL)
			break;

		id = xn_rxidx(req_prod);

		KASSERT(rxq->mbufs[id] == NULL, ("non-NULL xn_rx_chain"));
		rxq->mbufs[id] = m;

		ref = gnttab_claim_grant_reference(&rxq->gref_head);
		KASSERT(ref != GNTTAB_LIST_END,
		    ("reserved grant references exhuasted"));
		rxq->grant_ref[id] = ref;

		pfn = atop(vtophys(mtod(m, vm_offset_t)));
		req = RING_GET_REQUEST(&rxq->ring, req_prod);

		gnttab_grant_foreign_access_ref(ref,
		    xenbus_get_otherend_id(rxq->info->xbdev), pfn, 0);
		req->id = id;
		req->gref = ref;
	}

	rxq->ring.req_prod_pvt = req_prod;

	/* Not enough requests? Try again later. */
	if (req_prod - rxq->ring.rsp_cons < NET_RX_SLOTS_MIN) {
		callout_reset_curcpu(&rxq->rx_refill, hz/10,
		    xn_alloc_rx_buffers_callout, rxq);
		return;
	}

	wmb();		/* barrier so backend seens requests */

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&rxq->ring, notify);
	if (notify)
		xen_intr_signal(rxq->xen_intr_handle);
}

static void xn_alloc_rx_buffers_callout(void *arg)
{
	struct netfront_rxq *rxq;

	rxq = (struct netfront_rxq *)arg;
	XN_RX_LOCK(rxq);
	xn_alloc_rx_buffers(rxq);
	XN_RX_UNLOCK(rxq);
}

static void
xn_release_rx_bufs(struct netfront_rxq *rxq)
{
	int i,  ref;
	struct mbuf *m;

	for (i = 0; i < NET_RX_RING_SIZE; i++) {
		m = rxq->mbufs[i];

		if (m == NULL)
			continue;

		ref = rxq->grant_ref[i];
		if (ref == GRANT_REF_INVALID)
			continue;

		gnttab_end_foreign_access_ref(ref);
		gnttab_release_grant_reference(&rxq->gref_head, ref);
		rxq->mbufs[i] = NULL;
		rxq->grant_ref[i] = GRANT_REF_INVALID;
		m_freem(m);
	}
}

static void
xn_rxeof(struct netfront_rxq *rxq)
{
	struct ifnet *ifp;
	struct netfront_info *np = rxq->info;
#if (defined(INET) || defined(INET6))
	struct lro_ctrl *lro = &rxq->lro;
#endif
	struct netfront_rx_info rinfo;
	struct netif_rx_response *rx = &rinfo.rx;
	struct netif_extra_info *extras = rinfo.extras;
	RING_IDX i, rp;
	struct mbuf *m;
	struct mbufq mbufq_rxq, mbufq_errq;
	int err, work_to_do;

	XN_RX_LOCK_ASSERT(rxq);

	if (!netfront_carrier_ok(np))
		return;

	/* XXX: there should be some sane limit. */
	mbufq_init(&mbufq_errq, INT_MAX);
	mbufq_init(&mbufq_rxq, INT_MAX);

	ifp = np->xn_ifp;

	do {
		rp = rxq->ring.sring->rsp_prod;
		rmb();	/* Ensure we see queued responses up to 'rp'. */

		i = rxq->ring.rsp_cons;
		while ((i != rp)) {
			memcpy(rx, RING_GET_RESPONSE(&rxq->ring, i), sizeof(*rx));
			memset(extras, 0, sizeof(rinfo.extras));

			m = NULL;
			err = xn_get_responses(rxq, &rinfo, rp, &i, &m);

			if (__predict_false(err)) {
				if (m)
					(void )mbufq_enqueue(&mbufq_errq, m);
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				continue;
			}

			m->m_pkthdr.rcvif = ifp;
			if (rx->flags & NETRXF_data_validated) {
				/*
				 * According to mbuf(9) the correct way to tell
				 * the stack that the checksum of an inbound
				 * packet is correct, without it actually being
				 * present (because the underlying interface
				 * doesn't provide it), is to set the
				 * CSUM_DATA_VALID and CSUM_PSEUDO_HDR flags,
				 * and the csum_data field to 0xffff.
				 */
				m->m_pkthdr.csum_flags |= (CSUM_DATA_VALID
				    | CSUM_PSEUDO_HDR);
				m->m_pkthdr.csum_data = 0xffff;
			}
			if ((rx->flags & NETRXF_extra_info) != 0 &&
			    (extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].type ==
			    XEN_NETIF_EXTRA_TYPE_GSO)) {
				m->m_pkthdr.tso_segsz =
				extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].u.gso.size;
				m->m_pkthdr.csum_flags |= CSUM_TSO;
			}

			(void )mbufq_enqueue(&mbufq_rxq, m);
		}

		rxq->ring.rsp_cons = i;

		xn_alloc_rx_buffers(rxq);

		RING_FINAL_CHECK_FOR_RESPONSES(&rxq->ring, work_to_do);
	} while (work_to_do);

	mbufq_drain(&mbufq_errq);
	/*
	 * Process all the mbufs after the remapping is complete.
	 * Break the mbuf chain first though.
	 */
	while ((m = mbufq_dequeue(&mbufq_rxq)) != NULL) {
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
#if (defined(INET) || defined(INET6))
		/* Use LRO if possible */
		if ((ifp->if_capenable & IFCAP_LRO) == 0 ||
		    lro->lro_cnt == 0 || tcp_lro_rx(lro, m, 0)) {
			/*
			 * If LRO fails, pass up to the stack
			 * directly.
			 */
			(*ifp->if_input)(ifp, m);
		}
#else
		(*ifp->if_input)(ifp, m);
#endif
	}

#if (defined(INET) || defined(INET6))
	/*
	 * Flush any outstanding LRO work
	 */
	tcp_lro_flush_all(lro);
#endif
}

static void
xn_txeof(struct netfront_txq *txq)
{
	RING_IDX i, prod;
	unsigned short id;
	struct ifnet *ifp;
	netif_tx_response_t *txr;
	struct mbuf *m;
	struct netfront_info *np = txq->info;

	XN_TX_LOCK_ASSERT(txq);

	if (!netfront_carrier_ok(np))
		return;

	ifp = np->xn_ifp;

	do {
		prod = txq->ring.sring->rsp_prod;
		rmb(); /* Ensure we see responses up to 'rp'. */

		for (i = txq->ring.rsp_cons; i != prod; i++) {
			txr = RING_GET_RESPONSE(&txq->ring, i);
			if (txr->status == NETIF_RSP_NULL)
				continue;

			if (txr->status != NETIF_RSP_OKAY) {
				printf("%s: WARNING: response is %d!\n",
				       __func__, txr->status);
			}
			id = txr->id;
			m = txq->mbufs[id];
			KASSERT(m != NULL, ("mbuf not found in chain"));
			KASSERT((uintptr_t)m > NET_TX_RING_SIZE,
				("mbuf already on the free list, but we're "
				"trying to free it again!"));
			M_ASSERTVALID(m);

			if (__predict_false(gnttab_query_foreign_access(
			    txq->grant_ref[id]) != 0)) {
				panic("%s: grant id %u still in use by the "
				    "backend", __func__, id);
			}
			gnttab_end_foreign_access_ref(txq->grant_ref[id]);
			gnttab_release_grant_reference(
				&txq->gref_head, txq->grant_ref[id]);
			txq->grant_ref[id] = GRANT_REF_INVALID;

			txq->mbufs[id] = NULL;
			add_id_to_freelist(txq->mbufs, id);
			txq->mbufs_cnt--;
			m_free(m);
			/* Only mark the txq active if we've freed up at least one slot to try */
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		}
		txq->ring.rsp_cons = prod;

		/*
		 * Set a new event, then check for race with update of
		 * tx_cons. Note that it is essential to schedule a
		 * callback, no matter how few buffers are pending. Even if
		 * there is space in the transmit ring, higher layers may
		 * be blocked because too much data is outstanding: in such
		 * cases notification from Xen is likely to be the only kick
		 * that we'll get.
		 */
		txq->ring.sring->rsp_event =
		    prod + ((txq->ring.sring->req_prod - prod) >> 1) + 1;

		mb();
	} while (prod != txq->ring.sring->rsp_prod);

	if (txq->full &&
	    ((txq->ring.sring->req_prod - prod) < NET_TX_RING_SIZE)) {
		txq->full = false;
		xn_txq_start(txq);
	}
}

static void
xn_intr(void *xsc)
{
	struct netfront_txq *txq = xsc;
	struct netfront_info *np = txq->info;
	struct netfront_rxq *rxq = &np->rxq[txq->id];

	/* kick both tx and rx */
	xn_rxq_intr(rxq);
	xn_txq_intr(txq);
}

static void
xn_move_rx_slot(struct netfront_rxq *rxq, struct mbuf *m,
    grant_ref_t ref)
{
	int new = xn_rxidx(rxq->ring.req_prod_pvt);

	KASSERT(rxq->mbufs[new] == NULL, ("mbufs != NULL"));
	rxq->mbufs[new] = m;
	rxq->grant_ref[new] = ref;
	RING_GET_REQUEST(&rxq->ring, rxq->ring.req_prod_pvt)->id = new;
	RING_GET_REQUEST(&rxq->ring, rxq->ring.req_prod_pvt)->gref = ref;
	rxq->ring.req_prod_pvt++;
}

static int
xn_get_extras(struct netfront_rxq *rxq,
    struct netif_extra_info *extras, RING_IDX rp, RING_IDX *cons)
{
	struct netif_extra_info *extra;

	int err = 0;

	do {
		struct mbuf *m;
		grant_ref_t ref;

		if (__predict_false(*cons + 1 == rp)) {
			err = EINVAL;
			break;
		}

		extra = (struct netif_extra_info *)
		RING_GET_RESPONSE(&rxq->ring, ++(*cons));

		if (__predict_false(!extra->type ||
			extra->type >= XEN_NETIF_EXTRA_TYPE_MAX)) {
			err = EINVAL;
		} else {
			memcpy(&extras[extra->type - 1], extra, sizeof(*extra));
		}

		m = xn_get_rx_mbuf(rxq, *cons);
		ref = xn_get_rx_ref(rxq,  *cons);
		xn_move_rx_slot(rxq, m, ref);
	} while (extra->flags & XEN_NETIF_EXTRA_FLAG_MORE);

	return err;
}

static int
xn_get_responses(struct netfront_rxq *rxq,
    struct netfront_rx_info *rinfo, RING_IDX rp, RING_IDX *cons,
    struct mbuf  **list)
{
	struct netif_rx_response *rx = &rinfo->rx;
	struct netif_extra_info *extras = rinfo->extras;
	struct mbuf *m, *m0, *m_prev;
	grant_ref_t ref = xn_get_rx_ref(rxq, *cons);
	RING_IDX ref_cons = *cons;
	int frags = 1;
	int err = 0;
	u_long ret;

	m0 = m = m_prev = xn_get_rx_mbuf(rxq, *cons);

	if (rx->flags & NETRXF_extra_info) {
		err = xn_get_extras(rxq, extras, rp, cons);
	}

	if (m0 != NULL) {
		m0->m_pkthdr.len = 0;
		m0->m_next = NULL;
	}

	for (;;) {
#if 0
		DPRINTK("rx->status=%hd rx->offset=%hu frags=%u\n",
			rx->status, rx->offset, frags);
#endif
		if (__predict_false(rx->status < 0 ||
			rx->offset + rx->status > PAGE_SIZE)) {

			xn_move_rx_slot(rxq, m, ref);
			if (m0 == m)
				m0 = NULL;
			m = NULL;
			err = EINVAL;
			goto next_skip_queue;
		}

		/*
		 * This definitely indicates a bug, either in this driver or in
		 * the backend driver. In future this should flag the bad
		 * situation to the system controller to reboot the backed.
		 */
		if (ref == GRANT_REF_INVALID) {
			printf("%s: Bad rx response id %d.\n", __func__, rx->id);
			err = EINVAL;
			goto next;
		}

		ret = gnttab_end_foreign_access_ref(ref);
		KASSERT(ret, ("Unable to end access to grant references"));

		gnttab_release_grant_reference(&rxq->gref_head, ref);

next:
		if (m == NULL)
			break;

		m->m_len = rx->status;
		m->m_data += rx->offset;
		m0->m_pkthdr.len += rx->status;

next_skip_queue:
		if (!(rx->flags & NETRXF_more_data))
			break;

		if (*cons + frags == rp) {
			if (net_ratelimit())
				WPRINTK("Need more frags\n");
			err = ENOENT;
			printf("%s: cons %u frags %u rp %u, not enough frags\n",
			       __func__, *cons, frags, rp);
			break;
		}
		/*
		 * Note that m can be NULL, if rx->status < 0 or if
		 * rx->offset + rx->status > PAGE_SIZE above.
		 */
		m_prev = m;

		rx = RING_GET_RESPONSE(&rxq->ring, *cons + frags);
		m = xn_get_rx_mbuf(rxq, *cons + frags);

		/*
		 * m_prev == NULL can happen if rx->status < 0 or if
		 * rx->offset + * rx->status > PAGE_SIZE above.
		 */
		if (m_prev != NULL)
			m_prev->m_next = m;

		/*
		 * m0 can be NULL if rx->status < 0 or if * rx->offset +
		 * rx->status > PAGE_SIZE above.
		 */
		if (m0 == NULL)
			m0 = m;
		m->m_next = NULL;
		ref = xn_get_rx_ref(rxq, *cons + frags);
		ref_cons = *cons + frags;
		frags++;
	}
	*list = m0;
	*cons += frags;

	return (err);
}

/**
 * \brief Count the number of fragments in an mbuf chain.
 *
 * Surprisingly, there isn't an M* macro for this.
 */
static inline int
xn_count_frags(struct mbuf *m)
{
	int nfrags;

	for (nfrags = 0; m != NULL; m = m->m_next)
		nfrags++;

	return (nfrags);
}

/**
 * Given an mbuf chain, make sure we have enough room and then push
 * it onto the transmit ring.
 */
static int
xn_assemble_tx_request(struct netfront_txq *txq, struct mbuf *m_head)
{
	struct mbuf *m;
	struct netfront_info *np = txq->info;
	struct ifnet *ifp = np->xn_ifp;
	u_int nfrags;
	int otherend_id;

	/**
	 * Defragment the mbuf if necessary.
	 */
	nfrags = xn_count_frags(m_head);

	/*
	 * Check to see whether this request is longer than netback
	 * can handle, and try to defrag it.
	 */
	/**
	 * It is a bit lame, but the netback driver in Linux can't
	 * deal with nfrags > MAX_TX_REQ_FRAGS, which is a quirk of
	 * the Linux network stack.
	 */
	if (nfrags > np->maxfrags) {
		m = m_defrag(m_head, M_NOWAIT);
		if (!m) {
			/*
			 * Defrag failed, so free the mbuf and
			 * therefore drop the packet.
			 */
			m_freem(m_head);
			return (EMSGSIZE);
		}
		m_head = m;
	}

	/* Determine how many fragments now exist */
	nfrags = xn_count_frags(m_head);

	/*
	 * Check to see whether the defragmented packet has too many
	 * segments for the Linux netback driver.
	 */
	/**
	 * The FreeBSD TCP stack, with TSO enabled, can produce a chain
	 * of mbufs longer than Linux can handle.  Make sure we don't
	 * pass a too-long chain over to the other side by dropping the
	 * packet.  It doesn't look like there is currently a way to
	 * tell the TCP stack to generate a shorter chain of packets.
	 */
	if (nfrags > MAX_TX_REQ_FRAGS) {
#ifdef DEBUG
		printf("%s: nfrags %d > MAX_TX_REQ_FRAGS %d, netback "
		       "won't be able to handle it, dropping\n",
		       __func__, nfrags, MAX_TX_REQ_FRAGS);
#endif
		m_freem(m_head);
		return (EMSGSIZE);
	}

	/*
	 * This check should be redundant.  We've already verified that we
	 * have enough slots in the ring to handle a packet of maximum
	 * size, and that our packet is less than the maximum size.  Keep
	 * it in here as an assert for now just to make certain that
	 * chain_cnt is accurate.
	 */
	KASSERT((txq->mbufs_cnt + nfrags) <= NET_TX_RING_SIZE,
		("%s: chain_cnt (%d) + nfrags (%d) > NET_TX_RING_SIZE "
		 "(%d)!", __func__, (int) txq->mbufs_cnt,
                    (int) nfrags, (int) NET_TX_RING_SIZE));

	/*
	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	otherend_id = xenbus_get_otherend_id(np->xbdev);
	for (m = m_head; m; m = m->m_next) {
		netif_tx_request_t *tx;
		uintptr_t id;
		grant_ref_t ref;
		u_long mfn; /* XXX Wrong type? */

		tx = RING_GET_REQUEST(&txq->ring, txq->ring.req_prod_pvt);
		id = get_id_from_freelist(txq->mbufs);
		if (id == 0)
			panic("%s: was allocated the freelist head!\n",
			    __func__);
		txq->mbufs_cnt++;
		if (txq->mbufs_cnt > NET_TX_RING_SIZE)
			panic("%s: tx_chain_cnt must be <= NET_TX_RING_SIZE\n",
			    __func__);
		txq->mbufs[id] = m;
		tx->id = id;
		ref = gnttab_claim_grant_reference(&txq->gref_head);
		KASSERT((short)ref >= 0, ("Negative ref"));
		mfn = virt_to_mfn(mtod(m, vm_offset_t));
		gnttab_grant_foreign_access_ref(ref, otherend_id,
		    mfn, GNTMAP_readonly);
		tx->gref = txq->grant_ref[id] = ref;
		tx->offset = mtod(m, vm_offset_t) & (PAGE_SIZE - 1);
		tx->flags = 0;
		if (m == m_head) {
			/*
			 * The first fragment has the entire packet
			 * size, subsequent fragments have just the
			 * fragment size. The backend works out the
			 * true size of the first fragment by
			 * subtracting the sizes of the other
			 * fragments.
			 */
			tx->size = m->m_pkthdr.len;

			/*
			 * The first fragment contains the checksum flags
			 * and is optionally followed by extra data for
			 * TSO etc.
			 */
			/**
			 * CSUM_TSO requires checksum offloading.
			 * Some versions of FreeBSD fail to
			 * set CSUM_TCP in the CSUM_TSO case,
			 * so we have to test for CSUM_TSO
			 * explicitly.
			 */
			if (m->m_pkthdr.csum_flags
			    & (CSUM_DELAY_DATA | CSUM_TSO)) {
				tx->flags |= (NETTXF_csum_blank
				    | NETTXF_data_validated);
			}
			if (m->m_pkthdr.csum_flags & CSUM_TSO) {
				struct netif_extra_info *gso =
					(struct netif_extra_info *)
					RING_GET_REQUEST(&txq->ring,
							 ++txq->ring.req_prod_pvt);

				tx->flags |= NETTXF_extra_info;

				gso->u.gso.size = m->m_pkthdr.tso_segsz;
				gso->u.gso.type =
					XEN_NETIF_GSO_TYPE_TCPV4;
				gso->u.gso.pad = 0;
				gso->u.gso.features = 0;

				gso->type = XEN_NETIF_EXTRA_TYPE_GSO;
				gso->flags = 0;
			}
		} else {
			tx->size = m->m_len;
		}
		if (m->m_next)
			tx->flags |= NETTXF_more_data;

		txq->ring.req_prod_pvt++;
	}
	BPF_MTAP(ifp, m_head);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, m_head->m_pkthdr.len);
	if (m_head->m_flags & M_MCAST)
		if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);

	xn_txeof(txq);

	return (0);
}

/* equivalent of network_open() in Linux */
static void
xn_ifinit_locked(struct netfront_info *np)
{
	struct ifnet *ifp;
	int i;
	struct netfront_rxq *rxq;

	XN_LOCK_ASSERT(np);

	ifp = np->xn_ifp;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING || !netfront_carrier_ok(np))
		return;

	xn_stop(np);

	for (i = 0; i < np->num_queues; i++) {
		rxq = &np->rxq[i];
		XN_RX_LOCK(rxq);
		xn_alloc_rx_buffers(rxq);
		rxq->ring.sring->rsp_event = rxq->ring.rsp_cons + 1;
		if (RING_HAS_UNCONSUMED_RESPONSES(&rxq->ring))
			xn_rxeof(rxq);
		XN_RX_UNLOCK(rxq);
	}

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	if_link_state_change(ifp, LINK_STATE_UP);
}

static void
xn_ifinit(void *xsc)
{
	struct netfront_info *sc = xsc;

	XN_LOCK(sc);
	xn_ifinit_locked(sc);
	XN_UNLOCK(sc);
}

static int
xn_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct netfront_info *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	device_t dev;
#ifdef INET
	struct ifaddr *ifa = (struct ifaddr *)data;
#endif
	int mask, error = 0, reinit;

	dev = sc->xbdev;

	switch(cmd) {
	case SIOCSIFADDR:
#ifdef INET
		XN_LOCK(sc);
		if (ifa->ifa_addr->sa_family == AF_INET) {
			ifp->if_flags |= IFF_UP;
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
				xn_ifinit_locked(sc);
			arp_ifinit(ifp, ifa);
			XN_UNLOCK(sc);
		} else {
			XN_UNLOCK(sc);
#endif
			error = ether_ioctl(ifp, cmd, data);
#ifdef INET
		}
#endif
		break;
	case SIOCSIFMTU:
		if (ifp->if_mtu == ifr->ifr_mtu)
			break;

		ifp->if_mtu = ifr->ifr_mtu;
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		xn_ifinit(sc);
		break;
	case SIOCSIFFLAGS:
		XN_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the state of the PROMISC flag changed,
			 * then just use the 'set promisc mode' command
			 * instead of reinitializing the entire NIC. Doing
			 * a full re-init means reloading the firmware and
			 * waiting for it to start up, which may take a
			 * second or two.
			 */
			xn_ifinit_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				xn_stop(sc);
			}
		}
		sc->xn_if_flags = ifp->if_flags;
		XN_UNLOCK(sc);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		reinit = 0;

		if (mask & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			ifp->if_hwassist ^= XN_CSUM_FEATURES;
		}
		if (mask & IFCAP_TSO4) {
			ifp->if_capenable ^= IFCAP_TSO4;
			ifp->if_hwassist ^= CSUM_TSO;
		}

		if (mask & (IFCAP_RXCSUM | IFCAP_LRO)) {
			/* These Rx features require us to renegotiate. */
			reinit = 1;

			if (mask & IFCAP_RXCSUM)
				ifp->if_capenable ^= IFCAP_RXCSUM;
			if (mask & IFCAP_LRO)
				ifp->if_capenable ^= IFCAP_LRO;
		}

		if (reinit == 0)
			break;

		/*
		 * We must reset the interface so the backend picks up the
		 * new features.
		 */
		device_printf(sc->xbdev,
		    "performing interface reset due to feature change\n");
		XN_LOCK(sc);
		netfront_carrier_off(sc);
		sc->xn_reset = true;
		/*
		 * NB: the pending packet queue is not flushed, since
		 * the interface should still support the old options.
		 */
		XN_UNLOCK(sc);
		/*
		 * Delete the xenstore nodes that export features.
		 *
		 * NB: There's a xenbus state called
		 * "XenbusStateReconfiguring", which is what we should set
		 * here. Sadly none of the backends know how to handle it,
		 * and simply disconnect from the frontend, so we will just
		 * switch back to XenbusStateInitialising in order to force
		 * a reconnection.
		 */
		xs_rm(XST_NIL, xenbus_get_node(dev), "feature-gso-tcpv4");
		xs_rm(XST_NIL, xenbus_get_node(dev), "feature-no-csum-offload");
		xenbus_set_state(dev, XenbusStateClosing);

		/*
		 * Wait for the frontend to reconnect before returning
		 * from the ioctl. 30s should be more than enough for any
		 * sane backend to reconnect.
		 */
		error = tsleep(sc, 0, "xn_rst", 30*hz);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
	}

	return (error);
}

static void
xn_stop(struct netfront_info *sc)
{
	struct ifnet *ifp;

	XN_LOCK_ASSERT(sc);

	ifp = sc->xn_ifp;

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	if_link_state_change(ifp, LINK_STATE_DOWN);
}

static void
xn_rebuild_rx_bufs(struct netfront_rxq *rxq)
{
	int requeue_idx, i;
	grant_ref_t ref;
	netif_rx_request_t *req;

	for (requeue_idx = 0, i = 0; i < NET_RX_RING_SIZE; i++) {
		struct mbuf *m;
		u_long pfn;

		if (rxq->mbufs[i] == NULL)
			continue;

		m = rxq->mbufs[requeue_idx] = xn_get_rx_mbuf(rxq, i);
		ref = rxq->grant_ref[requeue_idx] = xn_get_rx_ref(rxq, i);

		req = RING_GET_REQUEST(&rxq->ring, requeue_idx);
		pfn = vtophys(mtod(m, vm_offset_t)) >> PAGE_SHIFT;

		gnttab_grant_foreign_access_ref(ref,
		    xenbus_get_otherend_id(rxq->info->xbdev),
		    pfn, 0);

		req->gref = ref;
		req->id   = requeue_idx;

		requeue_idx++;
	}

	rxq->ring.req_prod_pvt = requeue_idx;
}

/* START of Xenolinux helper functions adapted to FreeBSD */
static int
xn_connect(struct netfront_info *np)
{
	int i, error;
	u_int feature_rx_copy;
	struct netfront_rxq *rxq;
	struct netfront_txq *txq;

	error = xs_scanf(XST_NIL, xenbus_get_otherend_path(np->xbdev),
	    "feature-rx-copy", NULL, "%u", &feature_rx_copy);
	if (error != 0)
		feature_rx_copy = 0;

	/* We only support rx copy. */
	if (!feature_rx_copy)
		return (EPROTONOSUPPORT);

	/* Recovery procedure: */
	error = talk_to_backend(np->xbdev, np);
	if (error != 0)
		return (error);

	/* Step 1: Reinitialise variables. */
	xn_query_features(np);
	xn_configure_features(np);

	/* Step 2: Release TX buffer */
	for (i = 0; i < np->num_queues; i++) {
		txq = &np->txq[i];
		xn_release_tx_bufs(txq);
	}

	/* Step 3: Rebuild the RX buffer freelist and the RX ring itself. */
	for (i = 0; i < np->num_queues; i++) {
		rxq = &np->rxq[i];
		xn_rebuild_rx_bufs(rxq);
	}

	/* Step 4: All public and private state should now be sane.  Get
	 * ready to start sending and receiving packets and give the driver
	 * domain a kick because we've probably just requeued some
	 * packets.
	 */
	netfront_carrier_on(np);
	wakeup(np);

	return (0);
}

static void
xn_kick_rings(struct netfront_info *np)
{
	struct netfront_rxq *rxq;
	struct netfront_txq *txq;
	int i;

	for (i = 0; i < np->num_queues; i++) {
		txq = &np->txq[i];
		rxq = &np->rxq[i];
		xen_intr_signal(txq->xen_intr_handle);
		XN_TX_LOCK(txq);
		xn_txeof(txq);
		XN_TX_UNLOCK(txq);
		XN_RX_LOCK(rxq);
		xn_alloc_rx_buffers(rxq);
		XN_RX_UNLOCK(rxq);
	}
}

static void
xn_query_features(struct netfront_info *np)
{
	int val;

	device_printf(np->xbdev, "backend features:");

	if (xs_scanf(XST_NIL, xenbus_get_otherend_path(np->xbdev),
		"feature-sg", NULL, "%d", &val) != 0)
		val = 0;

	np->maxfrags = 1;
	if (val) {
		np->maxfrags = MAX_TX_REQ_FRAGS;
		printf(" feature-sg");
	}

	if (xs_scanf(XST_NIL, xenbus_get_otherend_path(np->xbdev),
		"feature-gso-tcpv4", NULL, "%d", &val) != 0)
		val = 0;

	np->xn_ifp->if_capabilities &= ~(IFCAP_TSO4|IFCAP_LRO);
	if (val) {
		np->xn_ifp->if_capabilities |= IFCAP_TSO4|IFCAP_LRO;
		printf(" feature-gso-tcp4");
	}

	/*
	 * HW CSUM offload is assumed to be available unless
	 * feature-no-csum-offload is set in xenstore.
	 */
	if (xs_scanf(XST_NIL, xenbus_get_otherend_path(np->xbdev),
		"feature-no-csum-offload", NULL, "%d", &val) != 0)
		val = 0;

	np->xn_ifp->if_capabilities |= IFCAP_HWCSUM;
	if (val) {
		np->xn_ifp->if_capabilities &= ~(IFCAP_HWCSUM);
		printf(" feature-no-csum-offload");
	}

	printf("\n");
}

static int
xn_configure_features(struct netfront_info *np)
{
	int err, cap_enabled;
#if (defined(INET) || defined(INET6))
	int i;
#endif
	struct ifnet *ifp;

	ifp = np->xn_ifp;
	err = 0;

	if ((ifp->if_capenable & ifp->if_capabilities) == ifp->if_capenable) {
		/* Current options are available, no need to do anything. */
		return (0);
	}

	/* Try to preserve as many options as possible. */
	cap_enabled = ifp->if_capenable;
	ifp->if_capenable = ifp->if_hwassist = 0;

#if (defined(INET) || defined(INET6))
	if ((cap_enabled & IFCAP_LRO) != 0)
		for (i = 0; i < np->num_queues; i++)
			tcp_lro_free(&np->rxq[i].lro);
	if (xn_enable_lro &&
	    (ifp->if_capabilities & cap_enabled & IFCAP_LRO) != 0) {
	    	ifp->if_capenable |= IFCAP_LRO;
		for (i = 0; i < np->num_queues; i++) {
			err = tcp_lro_init(&np->rxq[i].lro);
			if (err != 0) {
				device_printf(np->xbdev,
				    "LRO initialization failed\n");
				ifp->if_capenable &= ~IFCAP_LRO;
				break;
			}
			np->rxq[i].lro.ifp = ifp;
		}
	}
	if ((ifp->if_capabilities & cap_enabled & IFCAP_TSO4) != 0) {
		ifp->if_capenable |= IFCAP_TSO4;
		ifp->if_hwassist |= CSUM_TSO;
	}
#endif
	if ((ifp->if_capabilities & cap_enabled & IFCAP_TXCSUM) != 0) {
		ifp->if_capenable |= IFCAP_TXCSUM;
		ifp->if_hwassist |= XN_CSUM_FEATURES;
	}
	if ((ifp->if_capabilities & cap_enabled & IFCAP_RXCSUM) != 0)
		ifp->if_capenable |= IFCAP_RXCSUM;

	return (err);
}

static int
xn_txq_mq_start_locked(struct netfront_txq *txq, struct mbuf *m)
{
	struct netfront_info *np;
	struct ifnet *ifp;
	struct buf_ring *br;
	int error, notify;

	np = txq->info;
	br = txq->br;
	ifp = np->xn_ifp;
	error = 0;

	XN_TX_LOCK_ASSERT(txq);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    !netfront_carrier_ok(np)) {
		if (m != NULL)
			error = drbr_enqueue(ifp, br, m);
		return (error);
	}

	if (m != NULL) {
		error = drbr_enqueue(ifp, br, m);
		if (error != 0)
			return (error);
	}

	while ((m = drbr_peek(ifp, br)) != NULL) {
		if (!xn_tx_slot_available(txq)) {
			drbr_putback(ifp, br, m);
			break;
		}

		error = xn_assemble_tx_request(txq, m);
		/* xn_assemble_tx_request always consumes the mbuf*/
		if (error != 0) {
			drbr_advance(ifp, br);
			break;
		}

		RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&txq->ring, notify);
		if (notify)
			xen_intr_signal(txq->xen_intr_handle);

		drbr_advance(ifp, br);
	}

	if (RING_FULL(&txq->ring))
		txq->full = true;

	return (0);
}

static int
xn_txq_mq_start(struct ifnet *ifp, struct mbuf *m)
{
	struct netfront_info *np;
	struct netfront_txq *txq;
	int i, npairs, error;

	np = ifp->if_softc;
	npairs = np->num_queues;

	if (!netfront_carrier_ok(np))
		return (ENOBUFS);

	KASSERT(npairs != 0, ("called with 0 available queues"));

	/* check if flowid is set */
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		i = m->m_pkthdr.flowid % npairs;
	else
		i = curcpu % npairs;

	txq = &np->txq[i];

	if (XN_TX_TRYLOCK(txq) != 0) {
		error = xn_txq_mq_start_locked(txq, m);
		XN_TX_UNLOCK(txq);
	} else {
		error = drbr_enqueue(ifp, txq->br, m);
		taskqueue_enqueue(txq->tq, &txq->defrtask);
	}

	return (error);
}

static void
xn_qflush(struct ifnet *ifp)
{
	struct netfront_info *np;
	struct netfront_txq *txq;
	struct mbuf *m;
	int i;

	np = ifp->if_softc;

	for (i = 0; i < np->num_queues; i++) {
		txq = &np->txq[i];

		XN_TX_LOCK(txq);
		while ((m = buf_ring_dequeue_sc(txq->br)) != NULL)
			m_freem(m);
		XN_TX_UNLOCK(txq);
	}

	if_qflush(ifp);
}

/**
 * Create a network device.
 * @param dev  Newbus device representing this virtual NIC.
 */
int
create_netdev(device_t dev)
{
	struct netfront_info *np;
	int err;
	struct ifnet *ifp;

	np = device_get_softc(dev);

	np->xbdev         = dev;

	mtx_init(&np->sc_lock, "xnsc", "netfront softc lock", MTX_DEF);

	ifmedia_init(&np->sc_media, 0, xn_ifmedia_upd, xn_ifmedia_sts);
	ifmedia_add(&np->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
	ifmedia_set(&np->sc_media, IFM_ETHER|IFM_MANUAL);

	err = xen_net_read_mac(dev, np->mac);
	if (err != 0)
		goto error;

	/* Set up ifnet structure */
	ifp = np->xn_ifp = if_alloc(IFT_ETHER);
    	ifp->if_softc = np;
    	if_initname(ifp, "xn",  device_get_unit(dev));
    	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    	ifp->if_ioctl = xn_ioctl;

	ifp->if_transmit = xn_txq_mq_start;
	ifp->if_qflush = xn_qflush;

    	ifp->if_init = xn_ifinit;

    	ifp->if_hwassist = XN_CSUM_FEATURES;
	/* Enable all supported features at device creation. */
	ifp->if_capenable = ifp->if_capabilities =
	    IFCAP_HWCSUM|IFCAP_TSO4|IFCAP_LRO;
	ifp->if_hw_tsomax = 65536 - (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN);
	ifp->if_hw_tsomaxsegcount = MAX_TX_REQ_FRAGS;
	ifp->if_hw_tsomaxsegsize = PAGE_SIZE;

    	ether_ifattach(ifp, np->mac);
	netfront_carrier_off(np);

	return (0);

error:
	KASSERT(err != 0, ("Error path with no error code specified"));
	return (err);
}

static int
netfront_detach(device_t dev)
{
	struct netfront_info *info = device_get_softc(dev);

	DPRINTK("%s\n", xenbus_get_node(dev));

	netif_free(info);

	return 0;
}

static void
netif_free(struct netfront_info *np)
{

	XN_LOCK(np);
	xn_stop(np);
	XN_UNLOCK(np);
	netif_disconnect_backend(np);
	ether_ifdetach(np->xn_ifp);
	free(np->rxq, M_DEVBUF);
	free(np->txq, M_DEVBUF);
	if_free(np->xn_ifp);
	np->xn_ifp = NULL;
	ifmedia_removeall(&np->sc_media);
}

static void
netif_disconnect_backend(struct netfront_info *np)
{
	u_int i;

	for (i = 0; i < np->num_queues; i++) {
		XN_RX_LOCK(&np->rxq[i]);
		XN_TX_LOCK(&np->txq[i]);
	}
	netfront_carrier_off(np);
	for (i = 0; i < np->num_queues; i++) {
		XN_RX_UNLOCK(&np->rxq[i]);
		XN_TX_UNLOCK(&np->txq[i]);
	}

	for (i = 0; i < np->num_queues; i++) {
		disconnect_rxq(&np->rxq[i]);
		disconnect_txq(&np->txq[i]);
	}
}

static int
xn_ifmedia_upd(struct ifnet *ifp)
{

	return (0);
}

static void
xn_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{

	ifmr->ifm_status = IFM_AVALID|IFM_ACTIVE;
	ifmr->ifm_active = IFM_ETHER|IFM_MANUAL;
}

/* ** Driver registration ** */
static device_method_t netfront_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         netfront_probe),
	DEVMETHOD(device_attach,        netfront_attach),
	DEVMETHOD(device_detach,        netfront_detach),
	DEVMETHOD(device_shutdown,      bus_generic_shutdown),
	DEVMETHOD(device_suspend,       netfront_suspend),
	DEVMETHOD(device_resume,        netfront_resume),

	/* Xenbus interface */
	DEVMETHOD(xenbus_otherend_changed, netfront_backend_changed),

	DEVMETHOD_END
};

static driver_t netfront_driver = {
	"xn",
	netfront_methods,
	sizeof(struct netfront_info),
};
devclass_t netfront_devclass;

DRIVER_MODULE(xe, xenbusb_front, netfront_driver, netfront_devclass, NULL,
    NULL);
