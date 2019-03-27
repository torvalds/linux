/*
 * Copyright (C) 2015 Cavium Inc.
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
 *
 * $FreeBSD$
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/pciio.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stdatomic.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/tcp_lro.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/dnv.h>
#include <sys/nv.h>
#include <sys/iov_schema.h>

#include <machine/bus.h>

#include "thunder_bgx.h"
#include "nic_reg.h"
#include "nic.h"
#include "nicvf_queues.h"

#define	VNIC_VF_DEVSTR		"Cavium Thunder NIC Virtual Function Driver"

#define	VNIC_VF_REG_RID		PCIR_BAR(PCI_CFG_REG_BAR_NUM)

/* Lock for core interface settings */
#define	NICVF_CORE_LOCK_INIT(nic)				\
    sx_init(&(nic)->core_sx, device_get_nameunit((nic)->dev))

#define	NICVF_CORE_LOCK_DESTROY(nic)				\
    sx_destroy(&(nic)->core_sx)

#define	NICVF_CORE_LOCK(nic)		sx_xlock(&(nic)->core_sx)
#define	NICVF_CORE_UNLOCK(nic)		sx_xunlock(&(nic)->core_sx)

#define	NICVF_CORE_LOCK_ASSERT(nic)	sx_assert(&(nic)->core_sx, SA_XLOCKED)

#define	SPEED_10	10
#define	SPEED_100	100
#define	SPEED_1000	1000
#define	SPEED_10000	10000
#define	SPEED_40000	40000

MALLOC_DEFINE(M_NICVF, "nicvf", "ThunderX VNIC VF dynamic memory");

static int nicvf_probe(device_t);
static int nicvf_attach(device_t);
static int nicvf_detach(device_t);

static device_method_t nicvf_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nicvf_probe),
	DEVMETHOD(device_attach,	nicvf_attach),
	DEVMETHOD(device_detach,	nicvf_detach),

	DEVMETHOD_END,
};

static driver_t nicvf_driver = {
	"vnic",
	nicvf_methods,
	sizeof(struct nicvf),
};

static devclass_t nicvf_devclass;

DRIVER_MODULE(vnicvf, pci, nicvf_driver, nicvf_devclass, 0, 0);
MODULE_VERSION(vnicvf, 1);
MODULE_DEPEND(vnicvf, pci, 1, 1, 1);
MODULE_DEPEND(vnicvf, ether, 1, 1, 1);
MODULE_DEPEND(vnicvf, vnicpf, 1, 1, 1);

static int nicvf_allocate_misc_interrupt(struct nicvf *);
static int nicvf_enable_misc_interrupt(struct nicvf *);
static int nicvf_allocate_net_interrupts(struct nicvf *);
static void nicvf_release_all_interrupts(struct nicvf *);
static int nicvf_update_hw_max_frs(struct nicvf *, int);
static int nicvf_hw_set_mac_addr(struct nicvf *, uint8_t *);
static void nicvf_config_cpi(struct nicvf *);
static int nicvf_rss_init(struct nicvf *);
static int nicvf_init_resources(struct nicvf *);

static int nicvf_setup_ifnet(struct nicvf *);
static int nicvf_setup_ifmedia(struct nicvf *);
static void nicvf_hw_addr_random(uint8_t *);

static int nicvf_if_ioctl(struct ifnet *, u_long, caddr_t);
static void nicvf_if_init(void *);
static void nicvf_if_init_locked(struct nicvf *);
static int nicvf_if_transmit(struct ifnet *, struct mbuf *);
static void nicvf_if_qflush(struct ifnet *);
static uint64_t nicvf_if_getcounter(struct ifnet *, ift_counter);

static int nicvf_stop_locked(struct nicvf *);

static void nicvf_media_status(struct ifnet *, struct ifmediareq *);
static int nicvf_media_change(struct ifnet *);

static void nicvf_tick_stats(void *);

static int
nicvf_probe(device_t dev)
{
	uint16_t vendor_id;
	uint16_t device_id;

	vendor_id = pci_get_vendor(dev);
	device_id = pci_get_device(dev);

	if (vendor_id != PCI_VENDOR_ID_CAVIUM)
		return (ENXIO);

	if (device_id == PCI_DEVICE_ID_THUNDER_NIC_VF ||
	    device_id == PCI_DEVICE_ID_THUNDER_PASS1_NIC_VF) {
		device_set_desc(dev, VNIC_VF_DEVSTR);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
nicvf_attach(device_t dev)
{
	struct nicvf *nic;
	int rid, qcount;
	int err = 0;
	uint8_t hwaddr[ETHER_ADDR_LEN];
	uint8_t zeromac[] = {[0 ... (ETHER_ADDR_LEN - 1)] = 0};

	nic = device_get_softc(dev);
	nic->dev = dev;
	nic->pnicvf = nic;

	NICVF_CORE_LOCK_INIT(nic);
	/* Enable HW TSO on Pass2 */
	if (!pass1_silicon(dev))
		nic->hw_tso = TRUE;

	rid = VNIC_VF_REG_RID;
	nic->reg_base = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (nic->reg_base == NULL) {
		device_printf(dev, "Could not allocate registers memory\n");
		return (ENXIO);
	}

	qcount = MAX_CMP_QUEUES_PER_QS;
	nic->max_queues = qcount;

	err = nicvf_set_qset_resources(nic);
	if (err != 0)
		goto err_free_res;

	/* Check if PF is alive and get MAC address for this VF */
	err = nicvf_allocate_misc_interrupt(nic);
	if (err != 0)
		goto err_free_res;

	NICVF_CORE_LOCK(nic);
	err = nicvf_enable_misc_interrupt(nic);
	NICVF_CORE_UNLOCK(nic);
	if (err != 0)
		goto err_release_intr;

	err = nicvf_allocate_net_interrupts(nic);
	if (err != 0) {
		device_printf(dev,
		    "Could not allocate network interface interrupts\n");
		goto err_free_ifnet;
	}

	/* If no MAC address was obtained we generate random one */
	if (memcmp(nic->hwaddr, zeromac, ETHER_ADDR_LEN) == 0) {
		nicvf_hw_addr_random(hwaddr);
		memcpy(nic->hwaddr, hwaddr, ETHER_ADDR_LEN);
		NICVF_CORE_LOCK(nic);
		nicvf_hw_set_mac_addr(nic, hwaddr);
		NICVF_CORE_UNLOCK(nic);
	}

	/* Configure CPI alorithm */
	nic->cpi_alg = CPI_ALG_NONE;
	NICVF_CORE_LOCK(nic);
	nicvf_config_cpi(nic);
	/* Configure receive side scaling */
	if (nic->qs->rq_cnt > 1)
		nicvf_rss_init(nic);
	NICVF_CORE_UNLOCK(nic);

	err = nicvf_setup_ifnet(nic);
	if (err != 0) {
		device_printf(dev, "Could not set-up ifnet\n");
		goto err_release_intr;
	}

	err = nicvf_setup_ifmedia(nic);
	if (err != 0) {
		device_printf(dev, "Could not set-up ifmedia\n");
		goto err_free_ifnet;
	}

	mtx_init(&nic->stats_mtx, "VNIC stats", NULL, MTX_DEF);
	callout_init_mtx(&nic->stats_callout, &nic->stats_mtx, 0);

	ether_ifattach(nic->ifp, nic->hwaddr);

	return (0);

err_free_ifnet:
	if_free(nic->ifp);
err_release_intr:
	nicvf_release_all_interrupts(nic);
err_free_res:
	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(nic->reg_base),
	    nic->reg_base);

	return (err);
}

static int
nicvf_detach(device_t dev)
{
	struct nicvf *nic;

	nic = device_get_softc(dev);

	NICVF_CORE_LOCK(nic);
	/* Shut down the port and release ring resources */
	nicvf_stop_locked(nic);
	/* Release stats lock */
	mtx_destroy(&nic->stats_mtx);
	/* Release interrupts */
	nicvf_release_all_interrupts(nic);
	/* Release memory resource */
	if (nic->reg_base != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(nic->reg_base), nic->reg_base);
	}

	/* Remove all ifmedia configurations */
	ifmedia_removeall(&nic->if_media);
	/* Free this ifnet */
	if_free(nic->ifp);
	NICVF_CORE_UNLOCK(nic);
	/* Finally destroy the lock */
	NICVF_CORE_LOCK_DESTROY(nic);

	return (0);
}

static void
nicvf_hw_addr_random(uint8_t *hwaddr)
{
	uint32_t rnd;
	uint8_t addr[ETHER_ADDR_LEN];

	/*
	 * Create randomized MAC address.
	 * Set 'bsd' + random 24 low-order bits.
	 */
	rnd = arc4random() & 0x00ffffff;
	addr[0] = 'b';
	addr[1] = 's';
	addr[2] = 'd';
	addr[3] = rnd >> 16;
	addr[4] = rnd >> 8;
	addr[5] = rnd >> 0;

	memcpy(hwaddr, addr, ETHER_ADDR_LEN);
}

static int
nicvf_setup_ifnet(struct nicvf *nic)
{
	struct ifnet *ifp;

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(nic->dev, "Could not allocate ifnet structure\n");
		return (ENOMEM);
	}

	nic->ifp = ifp;

	if_setsoftc(ifp, nic);
	if_initname(ifp, device_get_name(nic->dev), device_get_unit(nic->dev));
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);

	if_settransmitfn(ifp, nicvf_if_transmit);
	if_setqflushfn(ifp, nicvf_if_qflush);
	if_setioctlfn(ifp, nicvf_if_ioctl);
	if_setinitfn(ifp, nicvf_if_init);
	if_setgetcounterfn(ifp, nicvf_if_getcounter);

	if_setmtu(ifp, ETHERMTU);

	/* Reset caps */
	if_setcapabilities(ifp, 0);

	/* Set the default values */
	if_setcapabilitiesbit(ifp, IFCAP_VLAN_MTU | IFCAP_JUMBO_MTU, 0);
	if_setcapabilitiesbit(ifp, IFCAP_LRO, 0);
	if (nic->hw_tso) {
		/* TSO */
		if_setcapabilitiesbit(ifp, IFCAP_TSO4, 0);
		/* TSO parameters */
		if_sethwtsomax(ifp, NICVF_TSO_MAXSIZE);
		if_sethwtsomaxsegcount(ifp, NICVF_TSO_NSEGS);
		if_sethwtsomaxsegsize(ifp, MCLBYTES);
	}
	/* IP/TCP/UDP HW checksums */
	if_setcapabilitiesbit(ifp, IFCAP_HWCSUM, 0);
	if_setcapabilitiesbit(ifp, IFCAP_HWSTATS, 0);
	/*
	 * HW offload enable
	 */
	if_clearhwassist(ifp);
	if_sethwassistbits(ifp, (CSUM_IP | CSUM_TCP | CSUM_UDP | CSUM_SCTP), 0);
	if (nic->hw_tso)
		if_sethwassistbits(ifp, (CSUM_TSO), 0);
	if_setcapenable(ifp, if_getcapabilities(ifp));

	return (0);
}

static int
nicvf_setup_ifmedia(struct nicvf *nic)
{

	ifmedia_init(&nic->if_media, IFM_IMASK, nicvf_media_change,
	    nicvf_media_status);

	/*
	 * Advertise availability of all possible connection types,
	 * even though not all are possible at the same time.
	 */

	ifmedia_add(&nic->if_media, (IFM_ETHER | IFM_10_T | IFM_FDX),
	    0, NULL);
	ifmedia_add(&nic->if_media, (IFM_ETHER | IFM_100_TX | IFM_FDX),
	    0, NULL);
	ifmedia_add(&nic->if_media, (IFM_ETHER | IFM_1000_T | IFM_FDX),
	    0, NULL);
	ifmedia_add(&nic->if_media, (IFM_ETHER | IFM_10G_SR | IFM_FDX),
	    0, NULL);
	ifmedia_add(&nic->if_media, (IFM_ETHER | IFM_40G_CR4 | IFM_FDX),
	    0, NULL);
	ifmedia_add(&nic->if_media, (IFM_ETHER | IFM_AUTO | IFM_FDX),
	    0, NULL);

	ifmedia_set(&nic->if_media, (IFM_ETHER | IFM_AUTO | IFM_FDX));

	return (0);
}

static int
nicvf_if_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct nicvf *nic;
	struct rcv_queue *rq;
	struct ifreq *ifr;
	int mask, err;
	int rq_idx;
#if defined(INET) || defined(INET6)
	struct ifaddr *ifa;
	boolean_t avoid_reset = FALSE;
#endif

	nic = if_getsoftc(ifp);
	ifr = (struct ifreq *)data;
#if defined(INET) || defined(INET6)
	ifa = (struct ifaddr *)data;
#endif
	err = 0;
	switch (cmd) {
	case SIOCSIFADDR:
#ifdef INET
		if (ifa->ifa_addr->sa_family == AF_INET)
			avoid_reset = TRUE;
#endif
#ifdef INET6
		if (ifa->ifa_addr->sa_family == AF_INET6)
			avoid_reset = TRUE;
#endif

#if defined(INET) || defined(INET6)
		/* Avoid reinitialization unless it's necessary */
		if (avoid_reset) {
			if_setflagbits(ifp, IFF_UP, 0);
			if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING))
				nicvf_if_init(nic);
#ifdef INET
			if (!(if_getflags(ifp) & IFF_NOARP))
				arp_ifinit(ifp, ifa);
#endif

			return (0);
		}
#endif
		err = ether_ioctl(ifp, cmd, data);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < NIC_HW_MIN_FRS ||
		    ifr->ifr_mtu > NIC_HW_MAX_FRS) {
			err = EINVAL;
		} else {
			NICVF_CORE_LOCK(nic);
			err = nicvf_update_hw_max_frs(nic, ifr->ifr_mtu);
			if (err == 0)
				if_setmtu(ifp, ifr->ifr_mtu);
			NICVF_CORE_UNLOCK(nic);
		}
		break;
	case SIOCSIFFLAGS:
		NICVF_CORE_LOCK(nic);
		if (if_getflags(ifp) & IFF_UP) {
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
				if ((nic->if_flags & if_getflags(ifp)) &
				    IFF_PROMISC) {
					/* Change promiscous mode */
#if 0
					/* ARM64TODO */
					nicvf_set_promiscous(nic);
#endif
				}

				if ((nic->if_flags ^ if_getflags(ifp)) &
				    IFF_ALLMULTI) {
					/* Change multicasting settings */
#if 0
					/* ARM64TODO */
					nicvf_set_multicast(nic);
#endif
				}
			} else {
				nicvf_if_init_locked(nic);
			}
		} else if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
			nicvf_stop_locked(nic);

		nic->if_flags = if_getflags(ifp);
		NICVF_CORE_UNLOCK(nic);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
#if 0
			NICVF_CORE_LOCK(nic);
			/* ARM64TODO */
			nicvf_set_multicast(nic);
			NICVF_CORE_UNLOCK(nic);
#endif
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		err = ifmedia_ioctl(ifp, ifr, &nic->if_media, cmd);
		break;

	case SIOCSIFCAP:
		mask = if_getcapenable(ifp) ^ ifr->ifr_reqcap;
		if (mask & IFCAP_VLAN_MTU) {
			/* No work to do except acknowledge the change took. */
			if_togglecapenable(ifp, IFCAP_VLAN_MTU);
		}
		if (mask & IFCAP_TXCSUM)
			if_togglecapenable(ifp, IFCAP_TXCSUM);
		if (mask & IFCAP_RXCSUM)
			if_togglecapenable(ifp, IFCAP_RXCSUM);
		if ((mask & IFCAP_TSO4) && nic->hw_tso)
			if_togglecapenable(ifp, IFCAP_TSO4);
		if (mask & IFCAP_LRO) {
			/*
			 * Lock the driver for a moment to avoid
			 * mismatch in per-queue settings.
			 */
			NICVF_CORE_LOCK(nic);
			if_togglecapenable(ifp, IFCAP_LRO);
			if ((if_getdrvflags(nic->ifp) & IFF_DRV_RUNNING) != 0) {
				/*
				 * Now disable LRO for subsequent packets.
				 * Atomicity of this change is not necessary
				 * as we don't need precise toggle of this
				 * feature for all threads processing the
				 * completion queue.
				 */
				for (rq_idx = 0;
				    rq_idx < nic->qs->rq_cnt; rq_idx++) {
					rq = &nic->qs->rq[rq_idx];
					rq->lro_enabled = !rq->lro_enabled;
				}
			}
			NICVF_CORE_UNLOCK(nic);
		}

		break;

	default:
		err = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (err);
}

static void
nicvf_if_init_locked(struct nicvf *nic)
{
	struct queue_set *qs = nic->qs;
	struct ifnet *ifp;
	int qidx;
	int err;
	caddr_t if_addr;

	NICVF_CORE_LOCK_ASSERT(nic);
	ifp = nic->ifp;

	if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0)
		nicvf_stop_locked(nic);

	err = nicvf_enable_misc_interrupt(nic);
	if (err != 0) {
		if_printf(ifp, "Could not reenable Mbox interrupt\n");
		return;
	}

	/* Get the latest MAC address */
	if_addr = if_getlladdr(ifp);
	/* Update MAC address if changed */
	if (memcmp(nic->hwaddr, if_addr, ETHER_ADDR_LEN) != 0) {
		memcpy(nic->hwaddr, if_addr, ETHER_ADDR_LEN);
		nicvf_hw_set_mac_addr(nic, if_addr);
	}

	/* Initialize the queues */
	err = nicvf_init_resources(nic);
	if (err != 0)
		goto error;

	/* Make sure queue initialization is written */
	wmb();

	nicvf_reg_write(nic, NIC_VF_INT, ~0UL);
	/* Enable Qset err interrupt */
	nicvf_enable_intr(nic, NICVF_INTR_QS_ERR, 0);

	/* Enable completion queue interrupt */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++)
		nicvf_enable_intr(nic, NICVF_INTR_CQ, qidx);

	/* Enable RBDR threshold interrupt */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
		nicvf_enable_intr(nic, NICVF_INTR_RBDR, qidx);

	nic->drv_stats.txq_stop = 0;
	nic->drv_stats.txq_wake = 0;

	/* Activate network interface */
	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);

	/* Schedule callout to update stats */
	callout_reset(&nic->stats_callout, hz, nicvf_tick_stats, nic);

	return;

error:
	/* Something went very wrong. Disable this ifnet for good */
	if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);
}

static void
nicvf_if_init(void *if_softc)
{
	struct nicvf *nic = if_softc;

	NICVF_CORE_LOCK(nic);
	nicvf_if_init_locked(nic);
	NICVF_CORE_UNLOCK(nic);
}

static int
nicvf_if_transmit(struct ifnet *ifp, struct mbuf *mbuf)
{
	struct nicvf *nic = if_getsoftc(ifp);
	struct queue_set *qs = nic->qs;
	struct snd_queue *sq;
	struct mbuf *mtmp;
	int qidx;
	int err = 0;


	if (__predict_false(qs == NULL)) {
		panic("%s: missing queue set for %s", __func__,
		    device_get_nameunit(nic->dev));
	}

	/* Select queue */
	if (M_HASHTYPE_GET(mbuf) != M_HASHTYPE_NONE)
		qidx = mbuf->m_pkthdr.flowid % qs->sq_cnt;
	else
		qidx = curcpu % qs->sq_cnt;

	sq = &qs->sq[qidx];

	if (mbuf->m_next != NULL &&
	    (mbuf->m_pkthdr.csum_flags &
	    (CSUM_IP | CSUM_TCP | CSUM_UDP | CSUM_SCTP)) != 0) {
		if (M_WRITABLE(mbuf) == 0) {
			mtmp = m_dup(mbuf, M_NOWAIT);
			m_freem(mbuf);
			if (mtmp == NULL)
				return (ENOBUFS);
			mbuf = mtmp;
		}
	}

	err = drbr_enqueue(ifp, sq->br, mbuf);
	if (((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING) || !nic->link_up || (err != 0)) {
		/*
		 * Try to enqueue packet to the ring buffer.
		 * If the driver is not active, link down or enqueue operation
		 * failed, return with the appropriate error code.
		 */
		return (err);
	}

	if (NICVF_TX_TRYLOCK(sq) != 0) {
		err = nicvf_xmit_locked(sq);
		NICVF_TX_UNLOCK(sq);
		return (err);
	} else
		taskqueue_enqueue(sq->snd_taskq, &sq->snd_task);

	return (0);
}

static void
nicvf_if_qflush(struct ifnet *ifp)
{
	struct nicvf *nic;
	struct queue_set *qs;
	struct snd_queue *sq;
	struct mbuf *mbuf;
	size_t idx;

	nic = if_getsoftc(ifp);
	qs = nic->qs;

	for (idx = 0; idx < qs->sq_cnt; idx++) {
		sq = &qs->sq[idx];
		NICVF_TX_LOCK(sq);
		while ((mbuf = buf_ring_dequeue_sc(sq->br)) != NULL)
			m_freem(mbuf);
		NICVF_TX_UNLOCK(sq);
	}
	if_qflush(ifp);
}

static uint64_t
nicvf_if_getcounter(struct ifnet *ifp, ift_counter cnt)
{
	struct nicvf *nic;
	struct nicvf_hw_stats *hw_stats;
	struct nicvf_drv_stats *drv_stats;

	nic = if_getsoftc(ifp);
	hw_stats = &nic->hw_stats;
	drv_stats = &nic->drv_stats;

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		return (drv_stats->rx_frames_ok);
	case IFCOUNTER_OPACKETS:
		return (drv_stats->tx_frames_ok);
	case IFCOUNTER_IBYTES:
		return (hw_stats->rx_bytes);
	case IFCOUNTER_OBYTES:
		return (hw_stats->tx_bytes_ok);
	case IFCOUNTER_IMCASTS:
		return (hw_stats->rx_mcast_frames);
	case IFCOUNTER_COLLISIONS:
		return (0);
	case IFCOUNTER_IQDROPS:
		return (drv_stats->rx_drops);
	case IFCOUNTER_OQDROPS:
		return (drv_stats->tx_drops);
	default:
		return (if_get_counter_default(ifp, cnt));
	}

}

static void
nicvf_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nicvf *nic = if_getsoftc(ifp);

	NICVF_CORE_LOCK(nic);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (nic->link_up) {
		/* Device attached to working network */
		ifmr->ifm_status |= IFM_ACTIVE;
	}

	switch (nic->speed) {
	case SPEED_10:
		ifmr->ifm_active |= IFM_10_T;
		break;
	case SPEED_100:
		ifmr->ifm_active |= IFM_100_TX;
		break;
	case SPEED_1000:
		ifmr->ifm_active |= IFM_1000_T;
		break;
	case SPEED_10000:
		ifmr->ifm_active |= IFM_10G_SR;
		break;
	case SPEED_40000:
		ifmr->ifm_active |= IFM_40G_CR4;
		break;
	default:
		ifmr->ifm_active |= IFM_AUTO;
		break;
	}

	if (nic->duplex)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;

	NICVF_CORE_UNLOCK(nic);
}

static int
nicvf_media_change(struct ifnet *ifp __unused)
{

	return (0);
}

/* Register read/write APIs */
void
nicvf_reg_write(struct nicvf *nic, bus_space_handle_t offset, uint64_t val)
{

	bus_write_8(nic->reg_base, offset, val);
}

uint64_t
nicvf_reg_read(struct nicvf *nic, uint64_t offset)
{

	return (bus_read_8(nic->reg_base, offset));
}

void
nicvf_queue_reg_write(struct nicvf *nic, bus_space_handle_t offset,
    uint64_t qidx, uint64_t val)
{

	bus_write_8(nic->reg_base, offset + (qidx << NIC_Q_NUM_SHIFT), val);
}

uint64_t
nicvf_queue_reg_read(struct nicvf *nic, bus_space_handle_t offset,
    uint64_t qidx)
{

	return (bus_read_8(nic->reg_base, offset + (qidx << NIC_Q_NUM_SHIFT)));
}

/* VF -> PF mailbox communication */
static void
nicvf_write_to_mbx(struct nicvf *nic, union nic_mbx *mbx)
{
	uint64_t *msg = (uint64_t *)mbx;

	nicvf_reg_write(nic, NIC_VF_PF_MAILBOX_0_1 + 0, msg[0]);
	nicvf_reg_write(nic, NIC_VF_PF_MAILBOX_0_1 + 8, msg[1]);
}

int
nicvf_send_msg_to_pf(struct nicvf *nic, union nic_mbx *mbx)
{
	int timeout = NIC_MBOX_MSG_TIMEOUT * 10;
	int sleep = 2;

	NICVF_CORE_LOCK_ASSERT(nic);

	nic->pf_acked = FALSE;
	nic->pf_nacked = FALSE;

	nicvf_write_to_mbx(nic, mbx);

	/* Wait for previous message to be acked, timeout 2sec */
	while (!nic->pf_acked) {
		if (nic->pf_nacked)
			return (EINVAL);

		DELAY(sleep * 1000);

		if (nic->pf_acked)
			break;
		timeout -= sleep;
		if (!timeout) {
			device_printf(nic->dev,
				   "PF didn't ack to mbox msg %d from VF%d\n",
				   (mbx->msg.msg & 0xFF), nic->vf_id);

			return (EBUSY);
		}
	}
	return (0);
}

/*
 * Checks if VF is able to comminicate with PF
 * and also gets the VNIC number this VF is associated to.
 */
static int
nicvf_check_pf_ready(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.msg.msg = NIC_MBOX_MSG_READY;
	if (nicvf_send_msg_to_pf(nic, &mbx)) {
		device_printf(nic->dev,
			   "PF didn't respond to READY msg\n");
		return 0;
	}

	return 1;
}

static void
nicvf_read_bgx_stats(struct nicvf *nic, struct bgx_stats_msg *bgx)
{

	if (bgx->rx)
		nic->bgx_stats.rx_stats[bgx->idx] = bgx->stats;
	else
		nic->bgx_stats.tx_stats[bgx->idx] = bgx->stats;
}

static void
nicvf_handle_mbx_intr(struct nicvf *nic)
{
	union nic_mbx mbx = {};
	uint64_t *mbx_data;
	uint64_t mbx_addr;
	int i;

	mbx_addr = NIC_VF_PF_MAILBOX_0_1;
	mbx_data = (uint64_t *)&mbx;

	for (i = 0; i < NIC_PF_VF_MAILBOX_SIZE; i++) {
		*mbx_data = nicvf_reg_read(nic, mbx_addr);
		mbx_data++;
		mbx_addr += sizeof(uint64_t);
	}

	switch (mbx.msg.msg) {
	case NIC_MBOX_MSG_READY:
		nic->pf_acked = TRUE;
		nic->vf_id = mbx.nic_cfg.vf_id & 0x7F;
		nic->tns_mode = mbx.nic_cfg.tns_mode & 0x7F;
		nic->node = mbx.nic_cfg.node_id;
		memcpy(nic->hwaddr, mbx.nic_cfg.mac_addr, ETHER_ADDR_LEN);
		nic->loopback_supported = mbx.nic_cfg.loopback_supported;
		nic->link_up = FALSE;
		nic->duplex = 0;
		nic->speed = 0;
		break;
	case NIC_MBOX_MSG_ACK:
		nic->pf_acked = TRUE;
		break;
	case NIC_MBOX_MSG_NACK:
		nic->pf_nacked = TRUE;
		break;
	case NIC_MBOX_MSG_RSS_SIZE:
		nic->rss_info.rss_size = mbx.rss_size.ind_tbl_size;
		nic->pf_acked = TRUE;
		break;
	case NIC_MBOX_MSG_BGX_STATS:
		nicvf_read_bgx_stats(nic, &mbx.bgx_stats);
		nic->pf_acked = TRUE;
		break;
	case NIC_MBOX_MSG_BGX_LINK_CHANGE:
		nic->pf_acked = TRUE;
		nic->link_up = mbx.link_status.link_up;
		nic->duplex = mbx.link_status.duplex;
		nic->speed = mbx.link_status.speed;
		if (nic->link_up) {
			if_setbaudrate(nic->ifp, nic->speed * 1000000);
			if_link_state_change(nic->ifp, LINK_STATE_UP);
		} else {
			if_setbaudrate(nic->ifp, 0);
			if_link_state_change(nic->ifp, LINK_STATE_DOWN);
		}
		break;
	default:
		device_printf(nic->dev,
			   "Invalid message from PF, msg 0x%x\n", mbx.msg.msg);
		break;
	}
	nicvf_clear_intr(nic, NICVF_INTR_MBOX, 0);
}

static int
nicvf_update_hw_max_frs(struct nicvf *nic, int mtu)
{
	union nic_mbx mbx = {};

	mbx.frs.msg = NIC_MBOX_MSG_SET_MAX_FRS;
	mbx.frs.max_frs = mtu;
	mbx.frs.vf_id = nic->vf_id;

	return nicvf_send_msg_to_pf(nic, &mbx);
}

static int
nicvf_hw_set_mac_addr(struct nicvf *nic, uint8_t *hwaddr)
{
	union nic_mbx mbx = {};

	mbx.mac.msg = NIC_MBOX_MSG_SET_MAC;
	mbx.mac.vf_id = nic->vf_id;
	memcpy(mbx.mac.mac_addr, hwaddr, ETHER_ADDR_LEN);

	return (nicvf_send_msg_to_pf(nic, &mbx));
}

static void
nicvf_config_cpi(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.cpi_cfg.msg = NIC_MBOX_MSG_CPI_CFG;
	mbx.cpi_cfg.vf_id = nic->vf_id;
	mbx.cpi_cfg.cpi_alg = nic->cpi_alg;
	mbx.cpi_cfg.rq_cnt = nic->qs->rq_cnt;

	nicvf_send_msg_to_pf(nic, &mbx);
}

static void
nicvf_get_rss_size(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.rss_size.msg = NIC_MBOX_MSG_RSS_SIZE;
	mbx.rss_size.vf_id = nic->vf_id;
	nicvf_send_msg_to_pf(nic, &mbx);
}

static void
nicvf_config_rss(struct nicvf *nic)
{
	union nic_mbx mbx = {};
	struct nicvf_rss_info *rss;
	int ind_tbl_len;
	int i, nextq;

	rss = &nic->rss_info;
	ind_tbl_len = rss->rss_size;
	nextq = 0;

	mbx.rss_cfg.vf_id = nic->vf_id;
	mbx.rss_cfg.hash_bits = rss->hash_bits;
	while (ind_tbl_len != 0) {
		mbx.rss_cfg.tbl_offset = nextq;
		mbx.rss_cfg.tbl_len = MIN(ind_tbl_len,
		    RSS_IND_TBL_LEN_PER_MBX_MSG);
		mbx.rss_cfg.msg = mbx.rss_cfg.tbl_offset ?
		    NIC_MBOX_MSG_RSS_CFG_CONT : NIC_MBOX_MSG_RSS_CFG;

		for (i = 0; i < mbx.rss_cfg.tbl_len; i++)
			mbx.rss_cfg.ind_tbl[i] = rss->ind_tbl[nextq++];

		nicvf_send_msg_to_pf(nic, &mbx);

		ind_tbl_len -= mbx.rss_cfg.tbl_len;
	}
}

static void
nicvf_set_rss_key(struct nicvf *nic)
{
	struct nicvf_rss_info *rss;
	uint64_t key_addr;
	int idx;

	rss = &nic->rss_info;
	key_addr = NIC_VNIC_RSS_KEY_0_4;

	for (idx = 0; idx < RSS_HASH_KEY_SIZE; idx++) {
		nicvf_reg_write(nic, key_addr, rss->key[idx]);
		key_addr += sizeof(uint64_t);
	}
}

static int
nicvf_rss_init(struct nicvf *nic)
{
	struct nicvf_rss_info *rss;
	int idx;

	nicvf_get_rss_size(nic);

	rss = &nic->rss_info;
	if (nic->cpi_alg != CPI_ALG_NONE) {
		rss->enable = FALSE;
		rss->hash_bits = 0;
		return (ENXIO);
	}

	rss->enable = TRUE;

	/* Using the HW reset value for now */
	rss->key[0] = 0xFEED0BADFEED0BADUL;
	rss->key[1] = 0xFEED0BADFEED0BADUL;
	rss->key[2] = 0xFEED0BADFEED0BADUL;
	rss->key[3] = 0xFEED0BADFEED0BADUL;
	rss->key[4] = 0xFEED0BADFEED0BADUL;

	nicvf_set_rss_key(nic);

	rss->cfg = RSS_IP_HASH_ENA | RSS_TCP_HASH_ENA | RSS_UDP_HASH_ENA;
	nicvf_reg_write(nic, NIC_VNIC_RSS_CFG, rss->cfg);

	rss->hash_bits = fls(rss->rss_size) - 1;
	for (idx = 0; idx < rss->rss_size; idx++)
		rss->ind_tbl[idx] = idx % nic->rx_queues;

	nicvf_config_rss(nic);

	return (0);
}

static int
nicvf_init_resources(struct nicvf *nic)
{
	int err;
	union nic_mbx mbx = {};

	mbx.msg.msg = NIC_MBOX_MSG_CFG_DONE;

	/* Enable Qset */
	nicvf_qset_config(nic, TRUE);

	/* Initialize queues and HW for data transfer */
	err = nicvf_config_data_transfer(nic, TRUE);
	if (err) {
		device_printf(nic->dev,
		    "Failed to alloc/config VF's QSet resources\n");
		return (err);
	}

	/* Send VF config done msg to PF */
	nicvf_write_to_mbx(nic, &mbx);

	return (0);
}

static void
nicvf_misc_intr_handler(void *arg)
{
	struct nicvf *nic = (struct nicvf *)arg;
	uint64_t intr;

	intr = nicvf_reg_read(nic, NIC_VF_INT);
	/* Check for spurious interrupt */
	if (!(intr & NICVF_INTR_MBOX_MASK))
		return;

	nicvf_handle_mbx_intr(nic);
}

static int
nicvf_intr_handler(void *arg)
{
	struct nicvf *nic;
	struct cmp_queue *cq;
	int qidx;

	cq = (struct cmp_queue *)arg;
	nic = cq->nic;
	qidx = cq->idx;

	/* Disable interrupts */
	nicvf_disable_intr(nic, NICVF_INTR_CQ, qidx);

	taskqueue_enqueue(cq->cmp_taskq, &cq->cmp_task);

	/* Clear interrupt */
	nicvf_clear_intr(nic, NICVF_INTR_CQ, qidx);

	return (FILTER_HANDLED);
}

static void
nicvf_rbdr_intr_handler(void *arg)
{
	struct nicvf *nic;
	struct queue_set *qs;
	struct rbdr *rbdr;
	int qidx;

	nic = (struct nicvf *)arg;

	/* Disable RBDR interrupt and schedule softirq */
	for (qidx = 0; qidx < nic->qs->rbdr_cnt; qidx++) {
		if (!nicvf_is_intr_enabled(nic, NICVF_INTR_RBDR, qidx))
			continue;
		nicvf_disable_intr(nic, NICVF_INTR_RBDR, qidx);

		qs = nic->qs;
		rbdr = &qs->rbdr[qidx];
		taskqueue_enqueue(rbdr->rbdr_taskq, &rbdr->rbdr_task_nowait);
		/* Clear interrupt */
		nicvf_clear_intr(nic, NICVF_INTR_RBDR, qidx);
	}
}

static void
nicvf_qs_err_intr_handler(void *arg)
{
	struct nicvf *nic = (struct nicvf *)arg;
	struct queue_set *qs = nic->qs;

	/* Disable Qset err interrupt and schedule softirq */
	nicvf_disable_intr(nic, NICVF_INTR_QS_ERR, 0);
	taskqueue_enqueue(qs->qs_err_taskq, &qs->qs_err_task);
	nicvf_clear_intr(nic, NICVF_INTR_QS_ERR, 0);

}

static int
nicvf_enable_msix(struct nicvf *nic)
{
	struct pci_devinfo *dinfo;
	int rid, count;
	int ret;

	dinfo = device_get_ivars(nic->dev);
	rid = dinfo->cfg.msix.msix_table_bar;
	nic->msix_table_res =
	    bus_alloc_resource_any(nic->dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (nic->msix_table_res == NULL) {
		device_printf(nic->dev,
		    "Could not allocate memory for MSI-X table\n");
		return (ENXIO);
	}

	count = nic->num_vec = NIC_VF_MSIX_VECTORS;

	ret = pci_alloc_msix(nic->dev, &count);
	if ((ret != 0) || (count != nic->num_vec)) {
		device_printf(nic->dev,
		    "Request for #%d msix vectors failed, error: %d\n",
		    nic->num_vec, ret);
		return (ret);
	}

	nic->msix_enabled = 1;
	return (0);
}

static void
nicvf_disable_msix(struct nicvf *nic)
{

	if (nic->msix_enabled) {
		pci_release_msi(nic->dev);
		nic->msix_enabled = 0;
		nic->num_vec = 0;
	}
}

static void
nicvf_release_all_interrupts(struct nicvf *nic)
{
	struct resource *res;
	int irq;
	int err;

	/* Free registered interrupts */
	for (irq = 0; irq < nic->num_vec; irq++) {
		res = nic->msix_entries[irq].irq_res;
		if (res == NULL)
			continue;
		/* Teardown interrupt first */
		if (nic->msix_entries[irq].handle != NULL) {
			err = bus_teardown_intr(nic->dev,
			    nic->msix_entries[irq].irq_res,
			    nic->msix_entries[irq].handle);
			KASSERT(err == 0,
			    ("ERROR: Unable to teardown interrupt %d", irq));
			nic->msix_entries[irq].handle = NULL;
		}

		bus_release_resource(nic->dev, SYS_RES_IRQ,
			    rman_get_rid(res), nic->msix_entries[irq].irq_res);
		nic->msix_entries[irq].irq_res = NULL;
	}
	/* Disable MSI-X */
	nicvf_disable_msix(nic);
}

/*
 * Initialize MSIX vectors and register MISC interrupt.
 * Send READY message to PF to check if its alive
 */
static int
nicvf_allocate_misc_interrupt(struct nicvf *nic)
{
	struct resource *res;
	int irq, rid;
	int ret = 0;

	/* Return if mailbox interrupt is already registered */
	if (nic->msix_enabled)
		return (0);

	/* Enable MSI-X */
	if (nicvf_enable_msix(nic) != 0)
		return (ENXIO);

	irq = NICVF_INTR_ID_MISC;
	rid = irq + 1;
	nic->msix_entries[irq].irq_res = bus_alloc_resource_any(nic->dev,
	    SYS_RES_IRQ, &rid, (RF_SHAREABLE | RF_ACTIVE));
	if (nic->msix_entries[irq].irq_res == NULL) {
		device_printf(nic->dev,
		    "Could not allocate Mbox interrupt for VF%d\n",
		    device_get_unit(nic->dev));
		return (ENXIO);
	}

	ret = bus_setup_intr(nic->dev, nic->msix_entries[irq].irq_res,
	    (INTR_MPSAFE | INTR_TYPE_MISC), NULL, nicvf_misc_intr_handler, nic,
	    &nic->msix_entries[irq].handle);
	if (ret != 0) {
		res = nic->msix_entries[irq].irq_res;
		bus_release_resource(nic->dev, SYS_RES_IRQ,
			    rman_get_rid(res), res);
		nic->msix_entries[irq].irq_res = NULL;
		return (ret);
	}

	return (0);
}

static int
nicvf_enable_misc_interrupt(struct nicvf *nic)
{

	/* Enable mailbox interrupt */
	nicvf_enable_intr(nic, NICVF_INTR_MBOX, 0);

	/* Check if VF is able to communicate with PF */
	if (!nicvf_check_pf_ready(nic)) {
		nicvf_disable_intr(nic, NICVF_INTR_MBOX, 0);
		return (ENXIO);
	}

	return (0);
}

static void
nicvf_release_net_interrupts(struct nicvf *nic)
{
	struct resource *res;
	int irq;
	int err;

	for_each_cq_irq(irq) {
		res = nic->msix_entries[irq].irq_res;
		if (res == NULL)
			continue;
		/* Teardown active interrupts first */
		if (nic->msix_entries[irq].handle != NULL) {
			err = bus_teardown_intr(nic->dev,
			    nic->msix_entries[irq].irq_res,
			    nic->msix_entries[irq].handle);
			KASSERT(err == 0,
			    ("ERROR: Unable to teardown CQ interrupt %d",
			    (irq - NICVF_INTR_ID_CQ)));
			if (err != 0)
				continue;
		}

		/* Release resource */
		bus_release_resource(nic->dev, SYS_RES_IRQ, rman_get_rid(res),
		    res);
		nic->msix_entries[irq].irq_res = NULL;
	}

	for_each_rbdr_irq(irq) {
		res = nic->msix_entries[irq].irq_res;
		if (res == NULL)
			continue;
		/* Teardown active interrupts first */
		if (nic->msix_entries[irq].handle != NULL) {
			err = bus_teardown_intr(nic->dev,
			    nic->msix_entries[irq].irq_res,
			    nic->msix_entries[irq].handle);
			KASSERT(err == 0,
			    ("ERROR: Unable to teardown RDBR interrupt %d",
			    (irq - NICVF_INTR_ID_RBDR)));
			if (err != 0)
				continue;
		}

		/* Release resource */
		bus_release_resource(nic->dev, SYS_RES_IRQ, rman_get_rid(res),
		    res);
		nic->msix_entries[irq].irq_res = NULL;
	}

	irq = NICVF_INTR_ID_QS_ERR;
	res = nic->msix_entries[irq].irq_res;
	if (res != NULL) {
		/* Teardown active interrupts first */
		if (nic->msix_entries[irq].handle != NULL) {
			err = bus_teardown_intr(nic->dev,
			    nic->msix_entries[irq].irq_res,
			    nic->msix_entries[irq].handle);
			KASSERT(err == 0,
			    ("ERROR: Unable to teardown QS Error interrupt %d",
			    irq));
			if (err != 0)
				return;
		}

		/* Release resource */
		bus_release_resource(nic->dev, SYS_RES_IRQ, rman_get_rid(res),
		    res);
		nic->msix_entries[irq].irq_res = NULL;
	}
}

static int
nicvf_allocate_net_interrupts(struct nicvf *nic)
{
	u_int cpuid;
	int irq, rid;
	int qidx;
	int ret = 0;

	/* MSI-X must be configured by now */
	if (!nic->msix_enabled) {
		device_printf(nic->dev, "Cannot alloacte queue interrups. "
		    "MSI-X interrupts disabled.\n");
		return (ENXIO);
	}

	/* Register CQ interrupts */
	for_each_cq_irq(irq) {
		if (irq >= (NICVF_INTR_ID_CQ + nic->qs->cq_cnt))
			break;

		qidx = irq - NICVF_INTR_ID_CQ;
		rid = irq + 1;
		nic->msix_entries[irq].irq_res = bus_alloc_resource_any(nic->dev,
		    SYS_RES_IRQ, &rid, (RF_SHAREABLE | RF_ACTIVE));
		if (nic->msix_entries[irq].irq_res == NULL) {
			device_printf(nic->dev,
			    "Could not allocate CQ interrupt %d for VF%d\n",
			    (irq - NICVF_INTR_ID_CQ), device_get_unit(nic->dev));
			ret = ENXIO;
			goto error;
		}
		ret = bus_setup_intr(nic->dev, nic->msix_entries[irq].irq_res,
		    (INTR_MPSAFE | INTR_TYPE_NET), nicvf_intr_handler,
		    NULL, &nic->qs->cq[qidx], &nic->msix_entries[irq].handle);
		if (ret != 0) {
			device_printf(nic->dev,
			    "Could not setup CQ interrupt %d for VF%d\n",
			    (irq - NICVF_INTR_ID_CQ), device_get_unit(nic->dev));
			goto error;
		}
		cpuid = (device_get_unit(nic->dev) * CMP_QUEUE_CNT) + qidx;
		cpuid %= mp_ncpus;
		/*
		 * Save CPU ID for later use when system-wide RSS is enabled.
		 * It will be used to pit the CQ task to the same CPU that got
		 * interrupted.
		 */
		nic->qs->cq[qidx].cmp_cpuid = cpuid;
		if (bootverbose) {
			device_printf(nic->dev, "bind CQ%d IRQ to CPU%d\n",
			    qidx, cpuid);
		}
		/* Bind interrupts to the given CPU */
		bus_bind_intr(nic->dev, nic->msix_entries[irq].irq_res, cpuid);
	}

	/* Register RBDR interrupt */
	for_each_rbdr_irq(irq) {
		if (irq >= (NICVF_INTR_ID_RBDR + nic->qs->rbdr_cnt))
			break;

		rid = irq + 1;
		nic->msix_entries[irq].irq_res = bus_alloc_resource_any(nic->dev,
		    SYS_RES_IRQ, &rid, (RF_SHAREABLE | RF_ACTIVE));
		if (nic->msix_entries[irq].irq_res == NULL) {
			device_printf(nic->dev,
			    "Could not allocate RBDR interrupt %d for VF%d\n",
			    (irq - NICVF_INTR_ID_RBDR),
			    device_get_unit(nic->dev));
			ret = ENXIO;
			goto error;
		}
		ret = bus_setup_intr(nic->dev, nic->msix_entries[irq].irq_res,
		    (INTR_MPSAFE | INTR_TYPE_NET), NULL,
		    nicvf_rbdr_intr_handler, nic,
		    &nic->msix_entries[irq].handle);
		if (ret != 0) {
			device_printf(nic->dev,
			    "Could not setup RBDR interrupt %d for VF%d\n",
			    (irq - NICVF_INTR_ID_RBDR),
			    device_get_unit(nic->dev));
			goto error;
		}
	}

	/* Register QS error interrupt */
	irq = NICVF_INTR_ID_QS_ERR;
	rid = irq + 1;
	nic->msix_entries[irq].irq_res = bus_alloc_resource_any(nic->dev,
	    SYS_RES_IRQ, &rid, (RF_SHAREABLE | RF_ACTIVE));
	if (nic->msix_entries[irq].irq_res == NULL) {
		device_printf(nic->dev,
		    "Could not allocate QS Error interrupt for VF%d\n",
		    device_get_unit(nic->dev));
		ret = ENXIO;
		goto error;
	}
	ret = bus_setup_intr(nic->dev, nic->msix_entries[irq].irq_res,
	    (INTR_MPSAFE | INTR_TYPE_NET), NULL, nicvf_qs_err_intr_handler,
	    nic, &nic->msix_entries[irq].handle);
	if (ret != 0) {
		device_printf(nic->dev,
		    "Could not setup QS Error interrupt for VF%d\n",
		    device_get_unit(nic->dev));
		goto error;
	}

	return (0);
error:
	nicvf_release_net_interrupts(nic);
	return (ret);
}

static int
nicvf_stop_locked(struct nicvf *nic)
{
	struct ifnet *ifp;
	int qidx;
	struct queue_set *qs = nic->qs;
	union nic_mbx mbx = {};

	NICVF_CORE_LOCK_ASSERT(nic);
	/* Stop callout. Can block here since holding SX lock */
	callout_drain(&nic->stats_callout);

	ifp = nic->ifp;

	mbx.msg.msg = NIC_MBOX_MSG_SHUTDOWN;
	nicvf_send_msg_to_pf(nic, &mbx);

	/* Disable RBDR & QS error interrupts */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++) {
		nicvf_disable_intr(nic, NICVF_INTR_RBDR, qidx);
		nicvf_clear_intr(nic, NICVF_INTR_RBDR, qidx);
	}
	nicvf_disable_intr(nic, NICVF_INTR_QS_ERR, 0);
	nicvf_clear_intr(nic, NICVF_INTR_QS_ERR, 0);

	/* Deactivate network interface */
	if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);

	/* Free resources */
	nicvf_config_data_transfer(nic, FALSE);

	/* Disable HW Qset */
	nicvf_qset_config(nic, FALSE);

	/* disable mailbox interrupt */
	nicvf_disable_intr(nic, NICVF_INTR_MBOX, 0);

	return (0);
}

static void
nicvf_update_stats(struct nicvf *nic)
{
	int qidx;
	struct nicvf_hw_stats *stats = &nic->hw_stats;
	struct nicvf_drv_stats *drv_stats = &nic->drv_stats;
	struct queue_set *qs = nic->qs;

#define	GET_RX_STATS(reg) \
    nicvf_reg_read(nic, NIC_VNIC_RX_STAT_0_13 | ((reg) << 3))
#define GET_TX_STATS(reg) \
    nicvf_reg_read(nic, NIC_VNIC_TX_STAT_0_4 | ((reg) << 3))

	stats->rx_bytes = GET_RX_STATS(RX_OCTS);
	stats->rx_ucast_frames = GET_RX_STATS(RX_UCAST);
	stats->rx_bcast_frames = GET_RX_STATS(RX_BCAST);
	stats->rx_mcast_frames = GET_RX_STATS(RX_MCAST);
	stats->rx_fcs_errors = GET_RX_STATS(RX_FCS);
	stats->rx_l2_errors = GET_RX_STATS(RX_L2ERR);
	stats->rx_drop_red = GET_RX_STATS(RX_RED);
	stats->rx_drop_red_bytes = GET_RX_STATS(RX_RED_OCTS);
	stats->rx_drop_overrun = GET_RX_STATS(RX_ORUN);
	stats->rx_drop_overrun_bytes = GET_RX_STATS(RX_ORUN_OCTS);
	stats->rx_drop_bcast = GET_RX_STATS(RX_DRP_BCAST);
	stats->rx_drop_mcast = GET_RX_STATS(RX_DRP_MCAST);
	stats->rx_drop_l3_bcast = GET_RX_STATS(RX_DRP_L3BCAST);
	stats->rx_drop_l3_mcast = GET_RX_STATS(RX_DRP_L3MCAST);

	stats->tx_bytes_ok = GET_TX_STATS(TX_OCTS);
	stats->tx_ucast_frames_ok = GET_TX_STATS(TX_UCAST);
	stats->tx_bcast_frames_ok = GET_TX_STATS(TX_BCAST);
	stats->tx_mcast_frames_ok = GET_TX_STATS(TX_MCAST);
	stats->tx_drops = GET_TX_STATS(TX_DROP);

	drv_stats->tx_frames_ok = stats->tx_ucast_frames_ok +
	    stats->tx_bcast_frames_ok + stats->tx_mcast_frames_ok;
	drv_stats->rx_drops = stats->rx_drop_red + stats->rx_drop_overrun;
	drv_stats->tx_drops = stats->tx_drops;

	/* Update RQ and SQ stats */
	for (qidx = 0; qidx < qs->rq_cnt; qidx++)
		nicvf_update_rq_stats(nic, qidx);
	for (qidx = 0; qidx < qs->sq_cnt; qidx++)
		nicvf_update_sq_stats(nic, qidx);
}

static void
nicvf_tick_stats(void *arg)
{
	struct nicvf *nic;

	nic = (struct nicvf *)arg;

	/* Read the statistics */
	nicvf_update_stats(nic);

	callout_reset(&nic->stats_callout, hz, nicvf_tick_stats, nic);
}
