/*	$OpenBSD: ifq.h,v 1.44 2025/03/04 01:13:37 dlg Exp $ */

/*
 * Copyright (c) 2015 David Gwynne <dlg@openbsd.org>
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
 */

#ifndef _NET_IFQ_H_
#define _NET_IFQ_H_

struct ifnet;
struct kstat;

struct ifq_ops;

struct ifqueue {
	struct ifnet		*ifq_if;
	struct taskq		*ifq_softnet;
	union {
		void			*_ifq_softc;
		/*
		 * a rings sndq is found by looking up an array of pointers.
		 * by default we only have one sndq and the default drivers
		 * dont use ifq_softc, so we can borrow it for the map until
		 * we need to allocate a proper map.
		 */
		struct ifqueue		*_ifq_ifqs[1];
	} _ifq_ptr;
#define ifq_softc		 _ifq_ptr._ifq_softc
#define ifq_ifqs		 _ifq_ptr._ifq_ifqs

	/* mbuf handling */
	struct mutex		 ifq_mtx;
	const struct ifq_ops	*ifq_ops;
	void			*ifq_q;
	struct mbuf_list	 ifq_free;
	unsigned int		 ifq_len;
	unsigned int		 ifq_oactive;

	/* statistics */
	uint64_t		 ifq_packets;
	uint64_t		 ifq_bytes;
	uint64_t		 ifq_qdrops;
	uint64_t		 ifq_errors;
	uint64_t		 ifq_mcasts;
	uint32_t		 ifq_oactives;

	struct kstat		*ifq_kstat;

	/* work serialisation */
	struct mutex		 ifq_task_mtx;
	struct task_list	 ifq_task_list;
	void			*ifq_serializer;
	struct task		 ifq_bundle;

	/* work to be serialised */
	struct task		 ifq_start;
	struct task		 ifq_restart;

	/* properties */
	unsigned int		 ifq_maxlen;
	unsigned int		 ifq_idx;
};

struct ifiqueue {
	struct ifnet		*ifiq_if;
	caddr_t			*ifiq_bpfp;
	struct taskq		*ifiq_softnet;
	union {
		void			*_ifiq_softc;
		struct ifiqueue		*_ifiq_ifiqs[1];
	} _ifiq_ptr;
#define ifiq_softc		 _ifiq_ptr._ifiq_softc
#define ifiq_ifiqs		 _ifiq_ptr._ifiq_ifiqs

	struct mutex		 ifiq_mtx;
	struct mbuf_list	 ifiq_ml;
	struct task		 ifiq_task;
	unsigned int		 ifiq_pressure;

	/* counters */
	uint64_t		 ifiq_packets;
	uint64_t		 ifiq_bytes;
	uint64_t		 ifiq_fdrops;
	uint64_t		 ifiq_qdrops;
	uint64_t		 ifiq_errors;
	uint64_t		 ifiq_mcasts;
	uint64_t		 ifiq_noproto;

	/* number of times a list of packets were put on ifiq_ml */
	uint64_t		 ifiq_enqueues;
	/* number of times a list of packets were pulled off ifiq_ml */
	uint64_t		 ifiq_dequeues;

	struct kstat		*ifiq_kstat;

	/* properties */
	unsigned int		 ifiq_idx;
};

#ifdef _KERNEL

#define IFQ_MAXLEN		256

/*
 *
 * Interface Send Queues
 *
 * struct ifqueue sits between the network stack and a drivers
 * transmission of packets. The high level view is that when the stack
 * has finished generating a packet it hands it to a driver for
 * transmission. It does this by queueing the packet on an ifqueue and
 * notifying the driver to start transmission of the queued packets.
 *
 * A network device may have multiple contexts for the transmission
 * of packets, ie, independent transmit rings. Such a network device,
 * represented by a struct ifnet, would then have multiple ifqueue
 * structures, each of which maps to an independent transmit ring.
 *
 * struct ifqueue also provides the point where conditioning of
 * traffic (ie, priq and hfsc) is implemented, and provides some
 * infrastructure to assist in the implementation of network drivers.
 *
 * = ifq API
 *
 * The ifq API provides functions for three distinct consumers:
 *
 * 1. The network stack
 * 2. Traffic QoS/conditioning implementations
 * 3. Network drivers
 *
 * == Network Stack API
 *
 * The network stack is responsible for initialising and destroying
 * the ifqueue structures, changing the traffic conditioner on an
 * interface, enqueuing packets for transmission, and notifying
 * the driver to start transmission of a particular ifqueue.
 *
 * === ifq_init()
 *
 * During if_attach(), the network stack calls ifq_init to initialise
 * the ifqueue structure. By default it configures the priq traffic
 * conditioner.
 *
 * === ifq_destroy()
 *
 * The network stack calls ifq_destroy() during if_detach to tear down
 * the ifqueue structure. It frees the traffic conditioner state, and
 * frees any mbufs that were left queued.
 *
 * === ifq_attach()
 *
 * ifq_attach() is used to replace the current traffic conditioner on
 * the ifqueue. All the pending mbufs are removed from the previous
 * conditioner and requeued on the new.
 *
 * === ifq_idx()
 *
 * ifq_idx() selects a specific ifqueue from the current ifnet
 * structure for use in the transmission of the mbuf.
 *
 * === ifq_enqueue()
 *
 * ifq_enqueue() attempts to fit an mbuf onto the ifqueue. The
 * current traffic conditioner may drop a packet to make space on the
 * queue.
 *
 * === ifq_start()
 *
 * Once a packet has been successfully queued with ifq_enqueue(),
 * the network card is notified with a call to ifq_start().
 * Calls to ifq_start() run in the ifqueue serialisation context,
 * guaranteeing that only one instance of ifp->if_qstart() will be
 * running on behalf of a specific ifqueue in the system at any point
 * in time.
 *
 * == Traffic conditioners API
 *
 * The majority of interaction between struct ifqueue and a traffic
 * conditioner occurs via the callbacks a traffic conditioner provides
 * in an instance of struct ifq_ops.
 *
 * XXX document ifqop_*
 *
 * The ifqueue API implements the locking on behalf of the conditioning
 * implementations so conditioners only have to reject or keep mbufs.
 * If something needs to inspect a conditioners internals, the queue lock
 * needs to be taken to allow for a consistent or safe view. The queue
 * lock may be taken and released with ifq_q_enter() and ifq_q_leave().
 *
 * === ifq_q_enter()
 *
 * Code wishing to access a conditioners internals may take the queue
 * lock with ifq_q_enter(). The caller must pass a reference to the
 * conditioners ifq_ops structure so the infrastructure can ensure the
 * caller is able to understand the internals. ifq_q_enter() returns
 * a pointer to the conditioners internal structures, or NULL if the
 * ifq_ops did not match the current conditioner.
 *
 * === ifq_q_leave()
 *
 * The queue lock acquired with ifq_q_enter() is released with
 * ifq_q_leave().
 *
 * === ifq_mfreem() and ifq_mfreeml()
 *
 * A goal of the API is to avoid freeing an mbuf while mutexes are
 * held. Because the ifq API manages the lock on behalf of the backend
 * ifqops, the backend should not directly free mbufs. If a conditioner
 * backend needs to drop a packet during the handling of ifqop_deq_begin,
 * it may free it by calling ifq_mfreem(). This accounts for the drop,
 * and schedules the free of the mbuf outside the hold of ifq_mtx.
 * ifq_mfreeml() takes an mbuf list as an argument instead.
 *
 *
 * == Network Driver API
 *
 * The API used by network drivers is mostly documented in the
 * ifq_dequeue(9) manpage except for ifq_serialize().
 *
 * === ifq_serialize()
 *
 * A driver may run arbitrary work in the ifqueue serialiser context
 * via ifq_serialize(). The work to be done is represented by a task
 * that has been prepared with task_set.
 *
 * The work will be run in series with any other work dispatched by
 * ifq_start(), ifq_restart(), or other ifq_serialize() calls.
 *
 * Because the work may be run on another CPU, the lifetime of the
 * task and the work it represents can extend beyond the end of the
 * call to ifq_serialize() that dispatched it.
 *
 *
 * = ifqueue work serialisation
 *
 * ifqueues provide a mechanism to dispatch work to be run in a single
 * context. Work in this mechanism is represented by task structures.
 *
 * The tasks are run in a context similar to a taskq serviced by a
 * single kernel thread, except the work is run immediately by the
 * first CPU that dispatches work. If a second CPU attempts to dispatch
 * additional tasks while the first is still running, it will be queued
 * to be run by the first CPU. The second CPU will return immediately.
 *
 * = MP Safe Network Drivers
 *
 * An MP safe network driver is one in which its start routine can be
 * called by the network stack without holding the big kernel lock.
 *
 * == Attach
 *
 * A driver advertises its ability to run its start routine without
 * the kernel lock by setting the IFXF_MPSAFE flag in ifp->if_xflags
 * before calling if_attach(). Advertising an MPSAFE start routine
 * also implies that the driver understands that a network card can
 * have multiple rings or transmit queues, and therefore provides
 * if_qstart function (which takes an ifqueue pointer) instead of an
 * if_start function (which takes an ifnet pointer).
 *
 * If the hardware supports multiple transmit rings, it advertises
 * support for multiple rings to the network stack with if_attach_queues()
 * after the call to if_attach(). if_attach_queues allocates a struct
 * ifqueue for each hardware ring, which can then be initialised by
 * the driver with data for each ring.
 *
 *	void	drv_start(struct ifqueue *);
 *
 *	void
 *	drv_attach()
 *	{
 *	...
 *		ifp->if_xflags = IFXF_MPSAFE;
 *		ifp->if_qstart = drv_start;
 *		if_attach(ifp);
 *
 *		if_attach_queues(ifp, DRV_NUM_TX_RINGS);
 *		for (i = 0; i < DRV_NUM_TX_RINGS; i++) {
 *			struct ifqueue *ifq = ifp->if_ifqs[i];
 *			struct drv_tx_ring *ring = &sc->sc_tx_rings[i];
 *
 *			ifq->ifq_softc = ring;
 *			ring->ifq = ifq;
 *		}
 *	}
 *
 * The network stack will then call ifp->if_qstart via ifq_start()
 * to guarantee there is only one instance of that function running
 * for each ifq in the system, and to serialise it with other work
 * the driver may provide.
 *
 * == Initialise
 *
 * When the stack requests an interface be brought up (ie, drv_ioctl()
 * is called to handle SIOCSIFFLAGS with IFF_UP set in ifp->if_flags)
 * drivers should set IFF_RUNNING in ifp->if_flags, and then call
 * ifq_clr_oactive() against each ifq.
 *
 * == if_start
 *
 * ifq_start() checks that IFF_RUNNING is set in ifp->if_flags, that
 * ifq_is_oactive() does not return true, and that there are pending
 * packets to transmit via a call to ifq_len(). Therefore, drivers are
 * no longer responsible for doing this themselves.
 *
 * If a driver should not transmit packets while its link is down, use
 * ifq_purge() to flush pending packets from the transmit queue.
 *
 * Drivers for hardware should use the following pattern to transmit
 * packets:
 *
 *	void
 *	drv_start(struct ifqueue *ifq)
 *	{
 *		struct drv_tx_ring *ring = ifq->ifq_softc;
 *		struct ifnet *ifp = ifq->ifq_if;
 *		struct drv_softc *sc = ifp->if_softc;
 *		struct mbuf *m;
 *		int kick = 0;
 *
 *		if (NO_LINK) {
 *			ifq_purge(ifq);
 *			return;
 *		}
 *
 *		for (;;) {
 *			if (NO_SPACE(ring)) {
 *				ifq_set_oactive(ifq);
 *				break;
 *			}
 *
 *			m = ifq_dequeue(ifq);
 *			if (m == NULL)
 *				break;
 *
 *			if (drv_encap(sc, ring, m) != 0) { // map and fill ring
 *				m_freem(m);
 *				continue;
 *			}
 *
 *			bpf_mtap();
 *		}
 *
 *		drv_kick(ring); // notify hw of new descriptors on the ring
 *	 }
 *
 * == Transmission completion
 *
 * The following pattern should be used for transmit queue interrupt
 * processing:
 *
 *	void
 *	drv_txeof(struct drv_tx_ring *ring)
 *	{
 *		struct ifqueue *ifq = ring->ifq;
 *
 *		while (COMPLETED_PKTS(ring)) {
 *			// unmap packets, m_freem() the mbufs.
 *		}
 *
 *		if (ifq_is_oactive(ifq))
 *			ifq_restart(ifq);
 *	}
 *
 * == Stop
 *
 * Bringing an interface down (ie, IFF_UP was cleared in ifp->if_flags)
 * should clear IFF_RUNNING in ifp->if_flags, and guarantee the start
 * routine is not running before freeing any resources it uses:
 *
 *	void
 *	drv_down(struct drv_softc *sc)
 *	{
 *		struct ifnet *ifp = &sc->sc_if;
 *		struct ifqueue *ifq;
 *		int i;
 *
 *		CLR(ifp->if_flags, IFF_RUNNING);
 *		DISABLE_INTERRUPTS();
 *
 *		for (i = 0; i < sc->sc_num_queues; i++) {
 *			ifq = ifp->if_ifqs[i];
 *			ifq_barrier(ifq);
 *		}
 *
 *		intr_barrier(sc->sc_ih);
 *
 *		FREE_RESOURCES();
 *
 *		for (i = 0; i < sc->sc_num_queues; i++) {
 *			ifq = ifp->if_ifqs[i];
 *			ifq_clr_oactive(ifq);
 *		}
 *	}
 *
 */

struct ifq_ops {
	unsigned int		 (*ifqop_idx)(unsigned int,
				    const struct mbuf *);
	struct mbuf		*(*ifqop_enq)(struct ifqueue *, struct mbuf *);
	struct mbuf		*(*ifqop_deq_begin)(struct ifqueue *, void **);
	void			 (*ifqop_deq_commit)(struct ifqueue *,
				    struct mbuf *, void *);
	void			 (*ifqop_purge)(struct ifqueue *,
				    struct mbuf_list *);
	void			*(*ifqop_alloc)(unsigned int, void *);
	void			 (*ifqop_free)(unsigned int, void *);
};

extern const struct ifq_ops * const ifq_priq_ops;

/*
 * Interface send queues.
 */

void		 ifq_init(struct ifqueue *, struct ifnet *, unsigned int);
void		 ifq_attach(struct ifqueue *, const struct ifq_ops *, void *);
void		 ifq_destroy(struct ifqueue *);
void		 ifq_add_data(struct ifqueue *, struct if_data *);
int		 ifq_enqueue(struct ifqueue *, struct mbuf *);
void		 ifq_start(struct ifqueue *);
struct mbuf	*ifq_deq_begin(struct ifqueue *);
void		 ifq_deq_commit(struct ifqueue *, struct mbuf *);
void		 ifq_deq_rollback(struct ifqueue *, struct mbuf *);
struct mbuf	*ifq_dequeue(struct ifqueue *);
int		 ifq_hdatalen(struct ifqueue *);
void		 ifq_init_maxlen(struct ifqueue *, unsigned int);
void		 ifq_mfreem(struct ifqueue *, struct mbuf *);
void		 ifq_mfreeml(struct ifqueue *, struct mbuf_list *);
unsigned int	 ifq_purge(struct ifqueue *);
void		*ifq_q_enter(struct ifqueue *, const struct ifq_ops *);
void		 ifq_q_leave(struct ifqueue *, void *);
void		 ifq_serialize(struct ifqueue *, struct task *);
void		 ifq_barrier(struct ifqueue *);
void		 ifq_set_oactive(struct ifqueue *);
void		 ifq_deq_set_oactive(struct ifqueue *);

int		 ifq_deq_sleep(struct ifqueue *, struct mbuf **, int, int,
		     const char *, volatile unsigned int *,
		     volatile unsigned int *);

#define ifq_len(_ifq)		READ_ONCE((_ifq)->ifq_len)
#define ifq_empty(_ifq)		(ifq_len(_ifq) == 0)

static inline int
ifq_is_priq(struct ifqueue *ifq)
{
	return (ifq->ifq_ops == ifq_priq_ops);
}

static inline void
ifq_clr_oactive(struct ifqueue *ifq)
{
	ifq->ifq_oactive = 0;
}

static inline unsigned int
ifq_is_oactive(struct ifqueue *ifq)
{
	return (ifq->ifq_oactive);
}

static inline void
ifq_restart(struct ifqueue *ifq)
{
	ifq_serialize(ifq, &ifq->ifq_restart);
}

static inline unsigned int
ifq_idx(struct ifqueue *ifq, unsigned int nifqs, const struct mbuf *m)
{
	return ((*ifq->ifq_ops->ifqop_idx)(nifqs, m));
}

/* ifiq */

void		 ifiq_init(struct ifiqueue *, struct ifnet *, unsigned int);
void		 ifiq_destroy(struct ifiqueue *);
int		 ifiq_input(struct ifiqueue *, struct mbuf_list *);
int		 ifiq_enqueue_qlim(struct ifiqueue *, struct mbuf *,
		     unsigned int);
void		 ifiq_add_data(struct ifiqueue *, struct if_data *);

#define ifiq_len(_ifiq)		READ_ONCE(ml_len(&(_ifiq)->ifiq_ml))
#define ifiq_empty(_ifiq)	(ifiq_len(_ifiq) == 0)

static inline int
ifiq_enqueue(struct ifiqueue *ifiq, struct mbuf *m)
{
	return ifiq_enqueue_qlim(ifiq, m, 0);
}

#endif /* _KERNEL */

#endif /* _NET_IFQ_H_ */
