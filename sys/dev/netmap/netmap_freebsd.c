/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2013-2014 Universita` di Pisa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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

/* $FreeBSD$ */
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/jail.h>
#include <sys/poll.h>  /* POLLIN, POLLOUT */
#include <sys/kernel.h> /* types used in module initialization */
#include <sys/conf.h>	/* DEV_MODULE_ORDERED */
#include <sys/endian.h>
#include <sys/syscallsubr.h> /* kern_ioctl() */

#include <sys/rwlock.h>

#include <vm/vm.h>      /* vtophys */
#include <vm/pmap.h>    /* vtophys */
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/uma.h>


#include <sys/malloc.h>
#include <sys/socket.h> /* sockaddrs */
#include <sys/selinfo.h>
#include <sys/kthread.h> /* kthread_add() */
#include <sys/proc.h> /* PROC_LOCK() */
#include <sys/unistd.h> /* RFNOWAIT */
#include <sys/sched.h> /* sched_bind() */
#include <sys/smp.h> /* mp_maxid */
#include <sys/taskqueue.h> /* taskqueue_enqueue(), taskqueue_create(), ... */
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h> /* IFT_ETHER */
#include <net/ethernet.h> /* ether_ifdetach */
#include <net/if_dl.h> /* LLADDR */
#include <machine/bus.h>        /* bus_dmamap_* */
#include <netinet/in.h>		/* in6_cksum_pseudo() */
#include <machine/in_cksum.h>  /* in_pseudo(), in_cksum_hdr() */

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <net/netmap_virt.h>
#include <dev/netmap/netmap_mem2.h>


/* ======================== FREEBSD-SPECIFIC ROUTINES ================== */

static void
nm_kqueue_notify(void *opaque, int pending)
{
	struct nm_selinfo *si = opaque;

	/* We use a non-zero hint to distinguish this notification call
	 * from the call done in kqueue_scan(), which uses hint=0.
	 */
	KNOTE_UNLOCKED(&si->si.si_note, /*hint=*/0x100);
}

int nm_os_selinfo_init(NM_SELINFO_T *si, const char *name) {
	int err;

	TASK_INIT(&si->ntfytask, 0, nm_kqueue_notify, si);
	si->ntfytq = taskqueue_create(name, M_NOWAIT,
	    taskqueue_thread_enqueue, &si->ntfytq);
	if (si->ntfytq == NULL)
		return -ENOMEM;
	err = taskqueue_start_threads(&si->ntfytq, 1, PI_NET, "tq %s", name);
	if (err) {
		taskqueue_free(si->ntfytq);
		si->ntfytq = NULL;
		return err;
	}

	snprintf(si->mtxname, sizeof(si->mtxname), "nmkl%s", name);
	mtx_init(&si->m, si->mtxname, NULL, MTX_DEF);
	knlist_init_mtx(&si->si.si_note, &si->m);
	si->kqueue_users = 0;

	return (0);
}

void
nm_os_selinfo_uninit(NM_SELINFO_T *si)
{
	if (si->ntfytq == NULL) {
		return;	/* si was not initialized */
	}
	taskqueue_drain(si->ntfytq, &si->ntfytask);
	taskqueue_free(si->ntfytq);
	si->ntfytq = NULL;
	knlist_delete(&si->si.si_note, curthread, /*islocked=*/0);
	knlist_destroy(&si->si.si_note);
	/* now we don't need the mutex anymore */
	mtx_destroy(&si->m);
}

void *
nm_os_malloc(size_t size)
{
	return malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
}

void *
nm_os_realloc(void *addr, size_t new_size, size_t old_size __unused)
{
	return realloc(addr, new_size, M_DEVBUF, M_NOWAIT | M_ZERO);
}

void
nm_os_free(void *addr)
{
	free(addr, M_DEVBUF);
}

void
nm_os_ifnet_lock(void)
{
	IFNET_RLOCK();
}

void
nm_os_ifnet_unlock(void)
{
	IFNET_RUNLOCK();
}

static int netmap_use_count = 0;

void
nm_os_get_module(void)
{
	netmap_use_count++;
}

void
nm_os_put_module(void)
{
	netmap_use_count--;
}

static void
netmap_ifnet_arrival_handler(void *arg __unused, struct ifnet *ifp)
{
	netmap_undo_zombie(ifp);
}

static void
netmap_ifnet_departure_handler(void *arg __unused, struct ifnet *ifp)
{
	netmap_make_zombie(ifp);
}

static eventhandler_tag nm_ifnet_ah_tag;
static eventhandler_tag nm_ifnet_dh_tag;

int
nm_os_ifnet_init(void)
{
	nm_ifnet_ah_tag =
		EVENTHANDLER_REGISTER(ifnet_arrival_event,
				netmap_ifnet_arrival_handler,
				NULL, EVENTHANDLER_PRI_ANY);
	nm_ifnet_dh_tag =
		EVENTHANDLER_REGISTER(ifnet_departure_event,
				netmap_ifnet_departure_handler,
				NULL, EVENTHANDLER_PRI_ANY);
	return 0;
}

void
nm_os_ifnet_fini(void)
{
	EVENTHANDLER_DEREGISTER(ifnet_arrival_event,
			nm_ifnet_ah_tag);
	EVENTHANDLER_DEREGISTER(ifnet_departure_event,
			nm_ifnet_dh_tag);
}

unsigned
nm_os_ifnet_mtu(struct ifnet *ifp)
{
#if __FreeBSD_version < 1100030
	return ifp->if_data.ifi_mtu;
#else /* __FreeBSD_version >= 1100030 */
	return ifp->if_mtu;
#endif
}

rawsum_t
nm_os_csum_raw(uint8_t *data, size_t len, rawsum_t cur_sum)
{
	/* TODO XXX please use the FreeBSD implementation for this. */
	uint16_t *words = (uint16_t *)data;
	int nw = len / 2;
	int i;

	for (i = 0; i < nw; i++)
		cur_sum += be16toh(words[i]);

	if (len & 1)
		cur_sum += (data[len-1] << 8);

	return cur_sum;
}

/* Fold a raw checksum: 'cur_sum' is in host byte order, while the
 * return value is in network byte order.
 */
uint16_t
nm_os_csum_fold(rawsum_t cur_sum)
{
	/* TODO XXX please use the FreeBSD implementation for this. */
	while (cur_sum >> 16)
		cur_sum = (cur_sum & 0xFFFF) + (cur_sum >> 16);

	return htobe16((~cur_sum) & 0xFFFF);
}

uint16_t nm_os_csum_ipv4(struct nm_iphdr *iph)
{
#if 0
	return in_cksum_hdr((void *)iph);
#else
	return nm_os_csum_fold(nm_os_csum_raw((uint8_t*)iph, sizeof(struct nm_iphdr), 0));
#endif
}

void
nm_os_csum_tcpudp_ipv4(struct nm_iphdr *iph, void *data,
					size_t datalen, uint16_t *check)
{
#ifdef INET
	uint16_t pseudolen = datalen + iph->protocol;

	/* Compute and insert the pseudo-header cheksum. */
	*check = in_pseudo(iph->saddr, iph->daddr,
				 htobe16(pseudolen));
	/* Compute the checksum on TCP/UDP header + payload
	 * (includes the pseudo-header).
	 */
	*check = nm_os_csum_fold(nm_os_csum_raw(data, datalen, 0));
#else
	static int notsupported = 0;
	if (!notsupported) {
		notsupported = 1;
		nm_prerr("inet4 segmentation not supported");
	}
#endif
}

void
nm_os_csum_tcpudp_ipv6(struct nm_ipv6hdr *ip6h, void *data,
					size_t datalen, uint16_t *check)
{
#ifdef INET6
	*check = in6_cksum_pseudo((void*)ip6h, datalen, ip6h->nexthdr, 0);
	*check = nm_os_csum_fold(nm_os_csum_raw(data, datalen, 0));
#else
	static int notsupported = 0;
	if (!notsupported) {
		notsupported = 1;
		nm_prerr("inet6 segmentation not supported");
	}
#endif
}

/* on FreeBSD we send up one packet at a time */
void *
nm_os_send_up(struct ifnet *ifp, struct mbuf *m, struct mbuf *prev)
{
	NA(ifp)->if_input(ifp, m);
	return NULL;
}

int
nm_os_mbuf_has_csum_offld(struct mbuf *m)
{
	return m->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP | CSUM_SCTP |
					 CSUM_TCP_IPV6 | CSUM_UDP_IPV6 |
					 CSUM_SCTP_IPV6);
}

int
nm_os_mbuf_has_seg_offld(struct mbuf *m)
{
	return m->m_pkthdr.csum_flags & CSUM_TSO;
}

static void
freebsd_generic_rx_handler(struct ifnet *ifp, struct mbuf *m)
{
	int stolen;

	if (unlikely(!NM_NA_VALID(ifp))) {
		nm_prlim(1, "Warning: RX packet intercepted, but no"
				" emulated adapter");
		return;
	}

	stolen = generic_rx_handler(ifp, m);
	if (!stolen) {
		struct netmap_generic_adapter *gna =
				(struct netmap_generic_adapter *)NA(ifp);
		gna->save_if_input(ifp, m);
	}
}

/*
 * Intercept the rx routine in the standard device driver.
 * Second argument is non-zero to intercept, 0 to restore
 */
int
nm_os_catch_rx(struct netmap_generic_adapter *gna, int intercept)
{
	struct netmap_adapter *na = &gna->up.up;
	struct ifnet *ifp = na->ifp;
	int ret = 0;

	nm_os_ifnet_lock();
	if (intercept) {
		if (gna->save_if_input) {
			nm_prerr("RX on %s already intercepted", na->name);
			ret = EBUSY; /* already set */
			goto out;
		}
		gna->save_if_input = ifp->if_input;
		ifp->if_input = freebsd_generic_rx_handler;
	} else {
		if (!gna->save_if_input) {
			nm_prerr("Failed to undo RX intercept on %s",
				na->name);
			ret = EINVAL;  /* not saved */
			goto out;
		}
		ifp->if_input = gna->save_if_input;
		gna->save_if_input = NULL;
	}
out:
	nm_os_ifnet_unlock();

	return ret;
}


/*
 * Intercept the packet steering routine in the tx path,
 * so that we can decide which queue is used for an mbuf.
 * Second argument is non-zero to intercept, 0 to restore.
 * On freebsd we just intercept if_transmit.
 */
int
nm_os_catch_tx(struct netmap_generic_adapter *gna, int intercept)
{
	struct netmap_adapter *na = &gna->up.up;
	struct ifnet *ifp = netmap_generic_getifp(gna);

	nm_os_ifnet_lock();
	if (intercept) {
		na->if_transmit = ifp->if_transmit;
		ifp->if_transmit = netmap_transmit;
	} else {
		ifp->if_transmit = na->if_transmit;
	}
	nm_os_ifnet_unlock();

	return 0;
}


/*
 * Transmit routine used by generic_netmap_txsync(). Returns 0 on success
 * and non-zero on error (which may be packet drops or other errors).
 * addr and len identify the netmap buffer, m is the (preallocated)
 * mbuf to use for transmissions.
 *
 * We should add a reference to the mbuf so the m_freem() at the end
 * of the transmission does not consume resources.
 *
 * On FreeBSD, and on multiqueue cards, we can force the queue using
 *      if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
 *              i = m->m_pkthdr.flowid % adapter->num_queues;
 *      else
 *              i = curcpu % adapter->num_queues;
 *
 */
int
nm_os_generic_xmit_frame(struct nm_os_gen_arg *a)
{
	int ret;
	u_int len = a->len;
	struct ifnet *ifp = a->ifp;
	struct mbuf *m = a->m;

#if __FreeBSD_version < 1100000
	/*
	 * Old FreeBSD versions. The mbuf has a cluster attached,
	 * we need to copy from the cluster to the netmap buffer.
	 */
	if (MBUF_REFCNT(m) != 1) {
		nm_prerr("invalid refcnt %d for %p", MBUF_REFCNT(m), m);
		panic("in generic_xmit_frame");
	}
	if (m->m_ext.ext_size < len) {
		nm_prlim(2, "size %d < len %d", m->m_ext.ext_size, len);
		len = m->m_ext.ext_size;
	}
	bcopy(a->addr, m->m_data, len);
#else  /* __FreeBSD_version >= 1100000 */
	/* New FreeBSD versions. Link the external storage to
	 * the netmap buffer, so that no copy is necessary. */
	m->m_ext.ext_buf = m->m_data = a->addr;
	m->m_ext.ext_size = len;
#endif /* __FreeBSD_version >= 1100000 */

	m->m_len = m->m_pkthdr.len = len;

	/* mbuf refcnt is not contended, no need to use atomic
	 * (a memory barrier is enough). */
	SET_MBUF_REFCNT(m, 2);
	M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE);
	m->m_pkthdr.flowid = a->ring_nr;
	m->m_pkthdr.rcvif = ifp; /* used for tx notification */
	ret = NA(ifp)->if_transmit(ifp, m);
	return ret ? -1 : 0;
}


#if __FreeBSD_version >= 1100005
struct netmap_adapter *
netmap_getna(if_t ifp)
{
	return (NA((struct ifnet *)ifp));
}
#endif /* __FreeBSD_version >= 1100005 */

/*
 * The following two functions are empty until we have a generic
 * way to extract the info from the ifp
 */
int
nm_os_generic_find_num_desc(struct ifnet *ifp, unsigned int *tx, unsigned int *rx)
{
	return 0;
}


void
nm_os_generic_find_num_queues(struct ifnet *ifp, u_int *txq, u_int *rxq)
{
	unsigned num_rings = netmap_generic_rings ? netmap_generic_rings : 1;

	*txq = num_rings;
	*rxq = num_rings;
}

void
nm_os_generic_set_features(struct netmap_generic_adapter *gna)
{

	gna->rxsg = 1; /* Supported through m_copydata. */
	gna->txqdisc = 0; /* Not supported. */
}

void
nm_os_mitigation_init(struct nm_generic_mit *mit, int idx, struct netmap_adapter *na)
{
	mit->mit_pending = 0;
	mit->mit_ring_idx = idx;
	mit->mit_na = na;
}


void
nm_os_mitigation_start(struct nm_generic_mit *mit)
{
}


void
nm_os_mitigation_restart(struct nm_generic_mit *mit)
{
}


int
nm_os_mitigation_active(struct nm_generic_mit *mit)
{

	return 0;
}


void
nm_os_mitigation_cleanup(struct nm_generic_mit *mit)
{
}

static int
nm_vi_dummy(struct ifnet *ifp, u_long cmd, caddr_t addr)
{

	return EINVAL;
}

static void
nm_vi_start(struct ifnet *ifp)
{
	panic("nm_vi_start() must not be called");
}

/*
 * Index manager of persistent virtual interfaces.
 * It is used to decide the lowest byte of the MAC address.
 * We use the same algorithm with management of bridge port index.
 */
#define NM_VI_MAX	255
static struct {
	uint8_t index[NM_VI_MAX]; /* XXX just for a reasonable number */
	uint8_t active;
	struct mtx lock;
} nm_vi_indices;

void
nm_os_vi_init_index(void)
{
	int i;
	for (i = 0; i < NM_VI_MAX; i++)
		nm_vi_indices.index[i] = i;
	nm_vi_indices.active = 0;
	mtx_init(&nm_vi_indices.lock, "nm_vi_indices_lock", NULL, MTX_DEF);
}

/* return -1 if no index available */
static int
nm_vi_get_index(void)
{
	int ret;

	mtx_lock(&nm_vi_indices.lock);
	ret = nm_vi_indices.active == NM_VI_MAX ? -1 :
		nm_vi_indices.index[nm_vi_indices.active++];
	mtx_unlock(&nm_vi_indices.lock);
	return ret;
}

static void
nm_vi_free_index(uint8_t val)
{
	int i, lim;

	mtx_lock(&nm_vi_indices.lock);
	lim = nm_vi_indices.active;
	for (i = 0; i < lim; i++) {
		if (nm_vi_indices.index[i] == val) {
			/* swap index[lim-1] and j */
			int tmp = nm_vi_indices.index[lim-1];
			nm_vi_indices.index[lim-1] = val;
			nm_vi_indices.index[i] = tmp;
			nm_vi_indices.active--;
			break;
		}
	}
	if (lim == nm_vi_indices.active)
		nm_prerr("Index %u not found", val);
	mtx_unlock(&nm_vi_indices.lock);
}
#undef NM_VI_MAX

/*
 * Implementation of a netmap-capable virtual interface that
 * registered to the system.
 * It is based on if_tap.c and ip_fw_log.c in FreeBSD 9.
 *
 * Note: Linux sets refcount to 0 on allocation of net_device,
 * then increments it on registration to the system.
 * FreeBSD sets refcount to 1 on if_alloc(), and does not
 * increment this refcount on if_attach().
 */
int
nm_os_vi_persist(const char *name, struct ifnet **ret)
{
	struct ifnet *ifp;
	u_short macaddr_hi;
	uint32_t macaddr_mid;
	u_char eaddr[6];
	int unit = nm_vi_get_index(); /* just to decide MAC address */

	if (unit < 0)
		return EBUSY;
	/*
	 * We use the same MAC address generation method with tap
	 * except for the highest octet is 00:be instead of 00:bd
	 */
	macaddr_hi = htons(0x00be); /* XXX tap + 1 */
	macaddr_mid = (uint32_t) ticks;
	bcopy(&macaddr_hi, eaddr, sizeof(short));
	bcopy(&macaddr_mid, &eaddr[2], sizeof(uint32_t));
	eaddr[5] = (uint8_t)unit;

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		nm_prerr("if_alloc failed");
		return ENOMEM;
	}
	if_initname(ifp, name, IF_DUNIT_NONE);
	ifp->if_mtu = 65536;
	ifp->if_flags = IFF_UP | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = (void *)nm_vi_dummy;
	ifp->if_ioctl = nm_vi_dummy;
	ifp->if_start = nm_vi_start;
	ifp->if_mtu = ETHERMTU;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_capabilities |= IFCAP_LINKSTATE;
	ifp->if_capenable |= IFCAP_LINKSTATE;

	ether_ifattach(ifp, eaddr);
	*ret = ifp;
	return 0;
}

/* unregister from the system and drop the final refcount */
void
nm_os_vi_detach(struct ifnet *ifp)
{
	nm_vi_free_index(((char *)IF_LLADDR(ifp))[5]);
	ether_ifdetach(ifp);
	if_free(ifp);
}

#ifdef WITH_EXTMEM
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
struct nm_os_extmem {
	vm_object_t obj;
	vm_offset_t kva;
	vm_offset_t size;
	uintptr_t scan;
};

void
nm_os_extmem_delete(struct nm_os_extmem *e)
{
	nm_prinf("freeing %zx bytes", (size_t)e->size);
	vm_map_remove(kernel_map, e->kva, e->kva + e->size);
	nm_os_free(e);
}

char *
nm_os_extmem_nextpage(struct nm_os_extmem *e)
{
	char *rv = NULL;
	if (e->scan < e->kva + e->size) {
		rv = (char *)e->scan;
		e->scan += PAGE_SIZE;
	}
	return rv;
}

int
nm_os_extmem_isequal(struct nm_os_extmem *e1, struct nm_os_extmem *e2)
{
	return (e1->obj == e2->obj);
}

int
nm_os_extmem_nr_pages(struct nm_os_extmem *e)
{
	return e->size >> PAGE_SHIFT;
}

struct nm_os_extmem *
nm_os_extmem_create(unsigned long p, struct nmreq_pools_info *pi, int *perror)
{
	vm_map_t map;
	vm_map_entry_t entry;
	vm_object_t obj;
	vm_prot_t prot;
	vm_pindex_t index;
	boolean_t wired;
	struct nm_os_extmem *e = NULL;
	int rv, error = 0;

	e = nm_os_malloc(sizeof(*e));
	if (e == NULL) {
		error = ENOMEM;
		goto out;
	}

	map = &curthread->td_proc->p_vmspace->vm_map;
	rv = vm_map_lookup(&map, p, VM_PROT_RW, &entry,
			&obj, &index, &prot, &wired);
	if (rv != KERN_SUCCESS) {
		nm_prerr("address %lx not found", p);
		goto out_free;
	}
	/* check that we are given the whole vm_object ? */
	vm_map_lookup_done(map, entry);

	// XXX can we really use obj after releasing the map lock?
	e->obj = obj;
	vm_object_reference(obj);
	/* wire the memory and add the vm_object to the kernel map,
	 * to make sure that it is not fred even if the processes that
	 * are mmap()ing it all exit
	 */
	e->kva = vm_map_min(kernel_map);
	e->size = obj->size << PAGE_SHIFT;
	rv = vm_map_find(kernel_map, obj, 0, &e->kva, e->size, 0,
			VMFS_OPTIMAL_SPACE, VM_PROT_READ | VM_PROT_WRITE,
			VM_PROT_READ | VM_PROT_WRITE, 0);
	if (rv != KERN_SUCCESS) {
		nm_prerr("vm_map_find(%zx) failed", (size_t)e->size);
		goto out_rel;
	}
	rv = vm_map_wire(kernel_map, e->kva, e->kva + e->size,
			VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);
	if (rv != KERN_SUCCESS) {
		nm_prerr("vm_map_wire failed");
		goto out_rem;
	}

	e->scan = e->kva;

	return e;

out_rem:
	vm_map_remove(kernel_map, e->kva, e->kva + e->size);
	e->obj = NULL;
out_rel:
	vm_object_deallocate(e->obj);
out_free:
	nm_os_free(e);
out:
	if (perror)
		*perror = error;
	return NULL;
}
#endif /* WITH_EXTMEM */

/* ================== PTNETMAP GUEST SUPPORT ==================== */

#ifdef WITH_PTNETMAP
#include <sys/bus.h>
#include <sys/rman.h>
#include <machine/bus.h>        /* bus_dmamap_* */
#include <machine/resource.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
/*
 * ptnetmap memory device (memdev) for freebsd guest,
 * ssed to expose host netmap memory to the guest through a PCI BAR.
 */

/*
 * ptnetmap memdev private data structure
 */
struct ptnetmap_memdev {
	device_t dev;
	struct resource *pci_io;
	struct resource *pci_mem;
	struct netmap_mem_d *nm_mem;
};

static int	ptn_memdev_probe(device_t);
static int	ptn_memdev_attach(device_t);
static int	ptn_memdev_detach(device_t);
static int	ptn_memdev_shutdown(device_t);

static device_method_t ptn_memdev_methods[] = {
	DEVMETHOD(device_probe, ptn_memdev_probe),
	DEVMETHOD(device_attach, ptn_memdev_attach),
	DEVMETHOD(device_detach, ptn_memdev_detach),
	DEVMETHOD(device_shutdown, ptn_memdev_shutdown),
	DEVMETHOD_END
};

static driver_t ptn_memdev_driver = {
	PTNETMAP_MEMDEV_NAME,
	ptn_memdev_methods,
	sizeof(struct ptnetmap_memdev),
};

/* We use (SI_ORDER_MIDDLE+1) here, see DEV_MODULE_ORDERED() invocation
 * below. */
static devclass_t ptnetmap_devclass;
DRIVER_MODULE_ORDERED(ptn_memdev, pci, ptn_memdev_driver, ptnetmap_devclass,
		      NULL, NULL, SI_ORDER_MIDDLE + 1);

/*
 * Map host netmap memory through PCI-BAR in the guest OS,
 * returning physical (nm_paddr) and virtual (nm_addr) addresses
 * of the netmap memory mapped in the guest.
 */
int
nm_os_pt_memdev_iomap(struct ptnetmap_memdev *ptn_dev, vm_paddr_t *nm_paddr,
		      void **nm_addr, uint64_t *mem_size)
{
	int rid;

	nm_prinf("ptn_memdev_driver iomap");

	rid = PCIR_BAR(PTNETMAP_MEM_PCI_BAR);
	*mem_size = bus_read_4(ptn_dev->pci_io, PTNET_MDEV_IO_MEMSIZE_HI);
	*mem_size = bus_read_4(ptn_dev->pci_io, PTNET_MDEV_IO_MEMSIZE_LO) |
			(*mem_size << 32);

	/* map memory allocator */
	ptn_dev->pci_mem = bus_alloc_resource(ptn_dev->dev, SYS_RES_MEMORY,
			&rid, 0, ~0, *mem_size, RF_ACTIVE);
	if (ptn_dev->pci_mem == NULL) {
		*nm_paddr = 0;
		*nm_addr = NULL;
		return ENOMEM;
	}

	*nm_paddr = rman_get_start(ptn_dev->pci_mem);
	*nm_addr = rman_get_virtual(ptn_dev->pci_mem);

	nm_prinf("=== BAR %d start %lx len %lx mem_size %lx ===",
			PTNETMAP_MEM_PCI_BAR,
			(unsigned long)(*nm_paddr),
			(unsigned long)rman_get_size(ptn_dev->pci_mem),
			(unsigned long)*mem_size);
	return (0);
}

uint32_t
nm_os_pt_memdev_ioread(struct ptnetmap_memdev *ptn_dev, unsigned int reg)
{
	return bus_read_4(ptn_dev->pci_io, reg);
}

/* Unmap host netmap memory. */
void
nm_os_pt_memdev_iounmap(struct ptnetmap_memdev *ptn_dev)
{
	nm_prinf("ptn_memdev_driver iounmap");

	if (ptn_dev->pci_mem) {
		bus_release_resource(ptn_dev->dev, SYS_RES_MEMORY,
			PCIR_BAR(PTNETMAP_MEM_PCI_BAR), ptn_dev->pci_mem);
		ptn_dev->pci_mem = NULL;
	}
}

/* Device identification routine, return BUS_PROBE_DEFAULT on success,
 * positive on failure */
static int
ptn_memdev_probe(device_t dev)
{
	char desc[256];

	if (pci_get_vendor(dev) != PTNETMAP_PCI_VENDOR_ID)
		return (ENXIO);
	if (pci_get_device(dev) != PTNETMAP_PCI_DEVICE_ID)
		return (ENXIO);

	snprintf(desc, sizeof(desc), "%s PCI adapter",
			PTNETMAP_MEMDEV_NAME);
	device_set_desc_copy(dev, desc);

	return (BUS_PROBE_DEFAULT);
}

/* Device initialization routine. */
static int
ptn_memdev_attach(device_t dev)
{
	struct ptnetmap_memdev *ptn_dev;
	int rid;
	uint16_t mem_id;

	ptn_dev = device_get_softc(dev);
	ptn_dev->dev = dev;

	pci_enable_busmaster(dev);

	rid = PCIR_BAR(PTNETMAP_IO_PCI_BAR);
	ptn_dev->pci_io = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
						 RF_ACTIVE);
	if (ptn_dev->pci_io == NULL) {
	        device_printf(dev, "cannot map I/O space\n");
	        return (ENXIO);
	}

	mem_id = bus_read_4(ptn_dev->pci_io, PTNET_MDEV_IO_MEMID);

	/* create guest allocator */
	ptn_dev->nm_mem = netmap_mem_pt_guest_attach(ptn_dev, mem_id);
	if (ptn_dev->nm_mem == NULL) {
		ptn_memdev_detach(dev);
	        return (ENOMEM);
	}
	netmap_mem_get(ptn_dev->nm_mem);

	nm_prinf("ptnetmap memdev attached, host memid: %u", mem_id);

	return (0);
}

/* Device removal routine. */
static int
ptn_memdev_detach(device_t dev)
{
	struct ptnetmap_memdev *ptn_dev;

	ptn_dev = device_get_softc(dev);

	if (ptn_dev->nm_mem) {
		nm_prinf("ptnetmap memdev detached, host memid %u",
			netmap_mem_get_id(ptn_dev->nm_mem));
		netmap_mem_put(ptn_dev->nm_mem);
		ptn_dev->nm_mem = NULL;
	}
	if (ptn_dev->pci_mem) {
		bus_release_resource(dev, SYS_RES_MEMORY,
			PCIR_BAR(PTNETMAP_MEM_PCI_BAR), ptn_dev->pci_mem);
		ptn_dev->pci_mem = NULL;
	}
	if (ptn_dev->pci_io) {
		bus_release_resource(dev, SYS_RES_IOPORT,
			PCIR_BAR(PTNETMAP_IO_PCI_BAR), ptn_dev->pci_io);
		ptn_dev->pci_io = NULL;
	}

	return (0);
}

static int
ptn_memdev_shutdown(device_t dev)
{
	return bus_generic_shutdown(dev);
}

#endif /* WITH_PTNETMAP */

/*
 * In order to track whether pages are still mapped, we hook into
 * the standard cdev_pager and intercept the constructor and
 * destructor.
 */

struct netmap_vm_handle_t {
	struct cdev 		*dev;
	struct netmap_priv_d	*priv;
};


static int
netmap_dev_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
		vm_ooffset_t foff, struct ucred *cred, u_short *color)
{
	struct netmap_vm_handle_t *vmh = handle;

	if (netmap_verbose)
		nm_prinf("handle %p size %jd prot %d foff %jd",
			handle, (intmax_t)size, prot, (intmax_t)foff);
	if (color)
		*color = 0;
	dev_ref(vmh->dev);
	return 0;
}


static void
netmap_dev_pager_dtor(void *handle)
{
	struct netmap_vm_handle_t *vmh = handle;
	struct cdev *dev = vmh->dev;
	struct netmap_priv_d *priv = vmh->priv;

	if (netmap_verbose)
		nm_prinf("handle %p", handle);
	netmap_dtor(priv);
	free(vmh, M_DEVBUF);
	dev_rel(dev);
}


static int
netmap_dev_pager_fault(vm_object_t object, vm_ooffset_t offset,
	int prot, vm_page_t *mres)
{
	struct netmap_vm_handle_t *vmh = object->handle;
	struct netmap_priv_d *priv = vmh->priv;
	struct netmap_adapter *na = priv->np_na;
	vm_paddr_t paddr;
	vm_page_t page;
	vm_memattr_t memattr;
	vm_pindex_t pidx;

	nm_prdis("object %p offset %jd prot %d mres %p",
			object, (intmax_t)offset, prot, mres);
	memattr = object->memattr;
	pidx = OFF_TO_IDX(offset);
	paddr = netmap_mem_ofstophys(na->nm_mem, offset);
	if (paddr == 0)
		return VM_PAGER_FAIL;

	if (((*mres)->flags & PG_FICTITIOUS) != 0) {
		/*
		 * If the passed in result page is a fake page, update it with
		 * the new physical address.
		 */
		page = *mres;
		vm_page_updatefake(page, paddr, memattr);
	} else {
		/*
		 * Replace the passed in reqpage page with our own fake page and
		 * free up the all of the original pages.
		 */
#ifndef VM_OBJECT_WUNLOCK	/* FreeBSD < 10.x */
#define VM_OBJECT_WUNLOCK VM_OBJECT_UNLOCK
#define VM_OBJECT_WLOCK	VM_OBJECT_LOCK
#endif /* VM_OBJECT_WUNLOCK */

		VM_OBJECT_WUNLOCK(object);
		page = vm_page_getfake(paddr, memattr);
		VM_OBJECT_WLOCK(object);
		vm_page_lock(*mres);
		vm_page_free(*mres);
		vm_page_unlock(*mres);
		*mres = page;
		vm_page_insert(page, object, pidx);
	}
	page->valid = VM_PAGE_BITS_ALL;
	return (VM_PAGER_OK);
}


static struct cdev_pager_ops netmap_cdev_pager_ops = {
	.cdev_pg_ctor = netmap_dev_pager_ctor,
	.cdev_pg_dtor = netmap_dev_pager_dtor,
	.cdev_pg_fault = netmap_dev_pager_fault,
};


static int
netmap_mmap_single(struct cdev *cdev, vm_ooffset_t *foff,
	vm_size_t objsize,  vm_object_t *objp, int prot)
{
	int error;
	struct netmap_vm_handle_t *vmh;
	struct netmap_priv_d *priv;
	vm_object_t obj;

	if (netmap_verbose)
		nm_prinf("cdev %p foff %jd size %jd objp %p prot %d", cdev,
		    (intmax_t )*foff, (intmax_t )objsize, objp, prot);

	vmh = malloc(sizeof(struct netmap_vm_handle_t), M_DEVBUF,
			      M_NOWAIT | M_ZERO);
	if (vmh == NULL)
		return ENOMEM;
	vmh->dev = cdev;

	NMG_LOCK();
	error = devfs_get_cdevpriv((void**)&priv);
	if (error)
		goto err_unlock;
	if (priv->np_nifp == NULL) {
		error = EINVAL;
		goto err_unlock;
	}
	vmh->priv = priv;
	priv->np_refs++;
	NMG_UNLOCK();

	obj = cdev_pager_allocate(vmh, OBJT_DEVICE,
		&netmap_cdev_pager_ops, objsize, prot,
		*foff, NULL);
	if (obj == NULL) {
		nm_prerr("cdev_pager_allocate failed");
		error = EINVAL;
		goto err_deref;
	}

	*objp = obj;
	return 0;

err_deref:
	NMG_LOCK();
	priv->np_refs--;
err_unlock:
	NMG_UNLOCK();
// err:
	free(vmh, M_DEVBUF);
	return error;
}

/*
 * On FreeBSD the close routine is only called on the last close on
 * the device (/dev/netmap) so we cannot do anything useful.
 * To track close() on individual file descriptors we pass netmap_dtor() to
 * devfs_set_cdevpriv() on open(). The FreeBSD kernel will call the destructor
 * when the last fd pointing to the device is closed.
 *
 * Note that FreeBSD does not even munmap() on close() so we also have
 * to track mmap() ourselves, and postpone the call to
 * netmap_dtor() is called when the process has no open fds and no active
 * memory maps on /dev/netmap, as in linux.
 */
static int
netmap_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	if (netmap_verbose)
		nm_prinf("dev %p fflag 0x%x devtype %d td %p",
			dev, fflag, devtype, td);
	return 0;
}


static int
netmap_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct netmap_priv_d *priv;
	int error;

	(void)dev;
	(void)oflags;
	(void)devtype;
	(void)td;

	NMG_LOCK();
	priv = netmap_priv_new();
	if (priv == NULL) {
		error = ENOMEM;
		goto out;
	}
	error = devfs_set_cdevpriv(priv, netmap_dtor);
	if (error) {
		netmap_priv_delete(priv);
	}
out:
	NMG_UNLOCK();
	return error;
}

/******************** kthread wrapper ****************/
#include <sys/sysproto.h>
u_int
nm_os_ncpus(void)
{
	return mp_maxid + 1;
}

struct nm_kctx_ctx {
	/* Userspace thread (kthread creator). */
	struct thread *user_td;

	/* worker function and parameter */
	nm_kctx_worker_fn_t worker_fn;
	void *worker_private;

	struct nm_kctx *nmk;

	/* integer to manage multiple worker contexts (e.g., RX or TX on ptnetmap) */
	long type;
};

struct nm_kctx {
	struct thread *worker;
	struct mtx worker_lock;
	struct nm_kctx_ctx worker_ctx;
	int run;			/* used to stop kthread */
	int attach_user;		/* kthread attached to user_process */
	int affinity;
};

static void
nm_kctx_worker(void *data)
{
	struct nm_kctx *nmk = data;
	struct nm_kctx_ctx *ctx = &nmk->worker_ctx;

	if (nmk->affinity >= 0) {
		thread_lock(curthread);
		sched_bind(curthread, nmk->affinity);
		thread_unlock(curthread);
	}

	while (nmk->run) {
		/*
		 * check if the parent process dies
		 * (when kthread is attached to user process)
		 */
		if (ctx->user_td) {
			PROC_LOCK(curproc);
			thread_suspend_check(0);
			PROC_UNLOCK(curproc);
		} else {
			kthread_suspend_check();
		}

		/* Continuously execute worker process. */
		ctx->worker_fn(ctx->worker_private); /* worker body */
	}

	kthread_exit();
}

void
nm_os_kctx_worker_setaff(struct nm_kctx *nmk, int affinity)
{
	nmk->affinity = affinity;
}

struct nm_kctx *
nm_os_kctx_create(struct nm_kctx_cfg *cfg, void *opaque)
{
	struct nm_kctx *nmk = NULL;

	nmk = malloc(sizeof(*nmk),  M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!nmk)
		return NULL;

	mtx_init(&nmk->worker_lock, "nm_kthread lock", NULL, MTX_DEF);
	nmk->worker_ctx.worker_fn = cfg->worker_fn;
	nmk->worker_ctx.worker_private = cfg->worker_private;
	nmk->worker_ctx.type = cfg->type;
	nmk->affinity = -1;

	/* attach kthread to user process (ptnetmap) */
	nmk->attach_user = cfg->attach_user;

	return nmk;
}

int
nm_os_kctx_worker_start(struct nm_kctx *nmk)
{
	struct proc *p = NULL;
	int error = 0;

	/* Temporarily disable this function as it is currently broken
	 * and causes kernel crashes. The failure can be triggered by
	 * the "vale_polling_enable_disable" test in ctrl-api-test.c. */
	return EOPNOTSUPP;

	if (nmk->worker)
		return EBUSY;

	/* check if we want to attach kthread to user process */
	if (nmk->attach_user) {
		nmk->worker_ctx.user_td = curthread;
		p = curthread->td_proc;
	}

	/* enable kthread main loop */
	nmk->run = 1;
	/* create kthread */
	if((error = kthread_add(nm_kctx_worker, nmk, p,
			&nmk->worker, RFNOWAIT /* to be checked */, 0, "nm-kthread-%ld",
			nmk->worker_ctx.type))) {
		goto err;
	}

	nm_prinf("nm_kthread started td %p", nmk->worker);

	return 0;
err:
	nm_prerr("nm_kthread start failed err %d", error);
	nmk->worker = NULL;
	return error;
}

void
nm_os_kctx_worker_stop(struct nm_kctx *nmk)
{
	if (!nmk->worker)
		return;

	/* tell to kthread to exit from main loop */
	nmk->run = 0;

	/* wake up kthread if it sleeps */
	kthread_resume(nmk->worker);

	nmk->worker = NULL;
}

void
nm_os_kctx_destroy(struct nm_kctx *nmk)
{
	if (!nmk)
		return;

	if (nmk->worker)
		nm_os_kctx_worker_stop(nmk);

	free(nmk, M_DEVBUF);
}

/******************** kqueue support ****************/

/*
 * In addition to calling selwakeuppri(), nm_os_selwakeup() also
 * needs to call knote() to wake up kqueue listeners.
 * This operation is deferred to a taskqueue in order to avoid possible
 * lock order reversals; these may happen because knote() grabs a
 * private lock associated to the 'si' (see struct selinfo,
 * struct nm_selinfo, and nm_os_selinfo_init), and nm_os_selwakeup()
 * can be called while holding the lock associated to a different
 * 'si'.
 * When calling knote() we use a non-zero 'hint' argument to inform
 * the netmap_knrw() function that it is being called from
 * 'nm_os_selwakeup'; this is necessary because when netmap_knrw() is
 * called by the kevent subsystem (i.e. kevent_scan()) we also need to
 * call netmap_poll().
 *
 * The netmap_kqfilter() function registers one or another f_event
 * depending on read or write mode. A pointer to the struct
 * 'netmap_priv_d' is stored into kn->kn_hook, so that it can later
 * be passed to netmap_poll(). We pass NULL as a third argument to
 * netmap_poll(), so that the latter only runs the txsync/rxsync
 * (if necessary), and skips the nm_os_selrecord() calls.
 */


void
nm_os_selwakeup(struct nm_selinfo *si)
{
	selwakeuppri(&si->si, PI_NET);
	if (si->kqueue_users > 0) {
		taskqueue_enqueue(si->ntfytq, &si->ntfytask);
	}
}

void
nm_os_selrecord(struct thread *td, struct nm_selinfo *si)
{
	selrecord(td, &si->si);
}

static void
netmap_knrdetach(struct knote *kn)
{
	struct netmap_priv_d *priv = (struct netmap_priv_d *)kn->kn_hook;
	struct nm_selinfo *si = priv->np_si[NR_RX];

	knlist_remove(&si->si.si_note, kn, /*islocked=*/0);
	NMG_LOCK();
	KASSERT(si->kqueue_users > 0, ("kqueue_user underflow on %s",
	    si->mtxname));
	si->kqueue_users--;
	nm_prinf("kqueue users for %s: %d", si->mtxname, si->kqueue_users);
	NMG_UNLOCK();
}

static void
netmap_knwdetach(struct knote *kn)
{
	struct netmap_priv_d *priv = (struct netmap_priv_d *)kn->kn_hook;
	struct nm_selinfo *si = priv->np_si[NR_TX];

	knlist_remove(&si->si.si_note, kn, /*islocked=*/0);
	NMG_LOCK();
	si->kqueue_users--;
	nm_prinf("kqueue users for %s: %d", si->mtxname, si->kqueue_users);
	NMG_UNLOCK();
}

/*
 * Callback triggered by netmap notifications (see netmap_notify()),
 * and by the application calling kevent(). In the former case we
 * just return 1 (events ready), since we are not able to do better.
 * In the latter case we use netmap_poll() to see which events are
 * ready.
 */
static int
netmap_knrw(struct knote *kn, long hint, int events)
{
	struct netmap_priv_d *priv;
	int revents;

	if (hint != 0) {
		/* Called from netmap_notify(), typically from a
		 * thread different from the one issuing kevent().
		 * Assume we are ready. */
		return 1;
	}

	/* Called from kevent(). */
	priv = kn->kn_hook;
	revents = netmap_poll(priv, events, /*thread=*/NULL);

	return (events & revents) ? 1 : 0;
}

static int
netmap_knread(struct knote *kn, long hint)
{
	return netmap_knrw(kn, hint, POLLIN);
}

static int
netmap_knwrite(struct knote *kn, long hint)
{
	return netmap_knrw(kn, hint, POLLOUT);
}

static struct filterops netmap_rfiltops = {
	.f_isfd = 1,
	.f_detach = netmap_knrdetach,
	.f_event = netmap_knread,
};

static struct filterops netmap_wfiltops = {
	.f_isfd = 1,
	.f_detach = netmap_knwdetach,
	.f_event = netmap_knwrite,
};


/*
 * This is called when a thread invokes kevent() to record
 * a change in the configuration of the kqueue().
 * The 'priv' is the one associated to the open netmap device.
 */
static int
netmap_kqfilter(struct cdev *dev, struct knote *kn)
{
	struct netmap_priv_d *priv;
	int error;
	struct netmap_adapter *na;
	struct nm_selinfo *si;
	int ev = kn->kn_filter;

	if (ev != EVFILT_READ && ev != EVFILT_WRITE) {
		nm_prerr("bad filter request %d", ev);
		return 1;
	}
	error = devfs_get_cdevpriv((void**)&priv);
	if (error) {
		nm_prerr("device not yet setup");
		return 1;
	}
	na = priv->np_na;
	if (na == NULL) {
		nm_prerr("no netmap adapter for this file descriptor");
		return 1;
	}
	/* the si is indicated in the priv */
	si = priv->np_si[(ev == EVFILT_WRITE) ? NR_TX : NR_RX];
	kn->kn_fop = (ev == EVFILT_WRITE) ?
		&netmap_wfiltops : &netmap_rfiltops;
	kn->kn_hook = priv;
	NMG_LOCK();
	si->kqueue_users++;
	nm_prinf("kqueue users for %s: %d", si->mtxname, si->kqueue_users);
	NMG_UNLOCK();
	knlist_add(&si->si.si_note, kn, /*islocked=*/0);

	return 0;
}

static int
freebsd_netmap_poll(struct cdev *cdevi __unused, int events, struct thread *td)
{
	struct netmap_priv_d *priv;
	if (devfs_get_cdevpriv((void **)&priv)) {
		return POLLERR;
	}
	return netmap_poll(priv, events, td);
}

static int
freebsd_netmap_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
		int ffla __unused, struct thread *td)
{
	int error;
	struct netmap_priv_d *priv;

	CURVNET_SET(TD_TO_VNET(td));
	error = devfs_get_cdevpriv((void **)&priv);
	if (error) {
		/* XXX ENOENT should be impossible, since the priv
		 * is now created in the open */
		if (error == ENOENT)
			error = ENXIO;
		goto out;
	}
	error = netmap_ioctl(priv, cmd, data, td, /*nr_body_is_user=*/1);
out:
	CURVNET_RESTORE();

	return error;
}

void
nm_os_onattach(struct ifnet *ifp)
{
	ifp->if_capabilities |= IFCAP_NETMAP;
}

void
nm_os_onenter(struct ifnet *ifp)
{
	struct netmap_adapter *na = NA(ifp);

	na->if_transmit = ifp->if_transmit;
	ifp->if_transmit = netmap_transmit;
	ifp->if_capenable |= IFCAP_NETMAP;
}

void
nm_os_onexit(struct ifnet *ifp)
{
	struct netmap_adapter *na = NA(ifp);

	ifp->if_transmit = na->if_transmit;
	ifp->if_capenable &= ~IFCAP_NETMAP;
}

extern struct cdevsw netmap_cdevsw; /* XXX used in netmap.c, should go elsewhere */
struct cdevsw netmap_cdevsw = {
	.d_version = D_VERSION,
	.d_name = "netmap",
	.d_open = netmap_open,
	.d_mmap_single = netmap_mmap_single,
	.d_ioctl = freebsd_netmap_ioctl,
	.d_poll = freebsd_netmap_poll,
	.d_kqfilter = netmap_kqfilter,
	.d_close = netmap_close,
};
/*--- end of kqueue support ----*/

/*
 * Kernel entry point.
 *
 * Initialize/finalize the module and return.
 *
 * Return 0 on success, errno on failure.
 */
static int
netmap_loader(__unused struct module *module, int event, __unused void *arg)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		error = netmap_init();
		break;

	case MOD_UNLOAD:
		/*
		 * if some one is still using netmap,
		 * then the module can not be unloaded.
		 */
		if (netmap_use_count) {
			nm_prerr("netmap module can not be unloaded - netmap_use_count: %d",
					netmap_use_count);
			error = EBUSY;
			break;
		}
		netmap_fini();
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

#ifdef DEV_MODULE_ORDERED
/*
 * The netmap module contains three drivers: (i) the netmap character device
 * driver; (ii) the ptnetmap memdev PCI device driver, (iii) the ptnet PCI
 * device driver. The attach() routines of both (ii) and (iii) need the
 * lock of the global allocator, and such lock is initialized in netmap_init(),
 * which is part of (i).
 * Therefore, we make sure that (i) is loaded before (ii) and (iii), using
 * the 'order' parameter of driver declaration macros. For (i), we specify
 * SI_ORDER_MIDDLE, while higher orders are used with the DRIVER_MODULE_ORDERED
 * macros for (ii) and (iii).
 */
DEV_MODULE_ORDERED(netmap, netmap_loader, NULL, SI_ORDER_MIDDLE);
#else /* !DEV_MODULE_ORDERED */
DEV_MODULE(netmap, netmap_loader, NULL);
#endif /* DEV_MODULE_ORDERED  */
MODULE_DEPEND(netmap, pci, 1, 1, 1);
MODULE_VERSION(netmap, 1);
/* reduce conditional code */
// linux API, use for the knlist in FreeBSD
/* use a private mutex for the knlist */
