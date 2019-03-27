/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/*
 * BERI Virtio Networking Frontend
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/mdioctl.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_vlan_var.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/beri/virtio/virtio.h>
#include <dev/beri/virtio/virtio_mmio_platform.h>

#include <dev/altera/pio/pio.h>

#include <dev/virtio/mmio/virtio_mmio.h>
#include <dev/virtio/network/virtio_net.h>
#include <dev/virtio/virtio_ids.h>
#include <dev/virtio/virtio_config.h>
#include <dev/virtio/virtio_ring.h>

#include "pio_if.h"

#define	DPRINTF(fmt, args...)	printf(fmt, ##args)

#define	READ4(_sc, _reg) \
	bus_read_4((_sc)->res[0], _reg)
#define	WRITE4(_sc, _reg, _val) \
	bus_write_4((_sc)->res[0], _reg, _val)

#define	VTBE_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	VTBE_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	VTBE_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->mtx, MA_OWNED);
#define	VTBE_ASSERT_UNLOCKED(sc)	mtx_assert(&(sc)->mtx, MA_NOTOWNED);

/*
 * Driver data and defines.
 */
#define	DESC_COUNT	256

struct vtbe_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	struct ifnet		*ifp;
	int			if_flags;
	struct mtx		mtx;
	boolean_t		is_attached;

	int			beri_mem_offset;
	device_t		pio_send;
	device_t		pio_recv;
	int			opened;

	struct vqueue_info	vs_queues[2];
	int			vs_curq;
	int			hdrsize;
};

static struct resource_spec vtbe_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static void vtbe_txfinish_locked(struct vtbe_softc *sc);
static void vtbe_rxfinish_locked(struct vtbe_softc *sc);
static void vtbe_stop_locked(struct vtbe_softc *sc);
static int pio_enable_irq(struct vtbe_softc *sc, int enable);

static void
vtbe_txstart_locked(struct vtbe_softc *sc)
{
	struct iovec iov[DESC_COUNT];
	struct virtio_net_hdr *vnh;
	struct vqueue_info *vq;
	struct iovec *tiov;
	struct ifnet *ifp;
	struct mbuf *m;
	struct uio uio;
	int enqueued;
	int iolen;
	int error;
	int reg;
	int len;
	int n;

	VTBE_ASSERT_LOCKED(sc);

	/* RX queue */
	vq = &sc->vs_queues[0];
	if (!vq_has_descs(vq)) {
		return;
	}

	ifp = sc->ifp;
	if (ifp->if_drv_flags & IFF_DRV_OACTIVE) {
		return;
	}

	enqueued = 0;

	if (!vq_ring_ready(vq))
		return;

	vq->vq_save_used = be16toh(vq->vq_used->idx);

	for (;;) {
		if (!vq_has_descs(vq)) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL) {
			break;
		}

		n = vq_getchain(sc->beri_mem_offset, vq, iov,
			DESC_COUNT, NULL);
		KASSERT(n == 2,
			("Unexpected amount of descriptors (%d)", n));

		tiov = getcopy(iov, n);
		vnh = iov[0].iov_base;
		memset(vnh, 0, sc->hdrsize);

		len = iov[1].iov_len;
		uio.uio_resid = len;
		uio.uio_iov = &tiov[1];
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_iovcnt = 1;
		uio.uio_offset = 0;
		uio.uio_rw = UIO_READ;

		error = m_mbuftouio(&uio, m, 0);
		if (error)
			panic("m_mbuftouio failed\n");

		iolen = (len - uio.uio_resid + sc->hdrsize);

		free(tiov, M_DEVBUF);
		vq_relchain(vq, iov, n, iolen);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		BPF_MTAP(ifp, m);
		m_freem(m);

		++enqueued;
	}

	if (enqueued != 0) {
		reg = htobe32(VIRTIO_MMIO_INT_VRING);
		WRITE4(sc, VIRTIO_MMIO_INTERRUPT_STATUS, reg);

		PIO_SET(sc->pio_send, Q_INTR, 1);
	}
}

static void
vtbe_txstart(struct ifnet *ifp)
{
	struct vtbe_softc *sc = ifp->if_softc;

	VTBE_LOCK(sc);
	vtbe_txstart_locked(sc);
	VTBE_UNLOCK(sc);
}

static void
vtbe_stop_locked(struct vtbe_softc *sc)
{
	struct ifnet *ifp;

	VTBE_ASSERT_LOCKED(sc);

	ifp = sc->ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

static void
vtbe_init_locked(struct vtbe_softc *sc)
{
	struct ifnet *ifp = sc->ifp;

	VTBE_ASSERT_LOCKED(sc);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
}

static void
vtbe_init(void *if_softc)
{
	struct vtbe_softc *sc = if_softc;

	VTBE_LOCK(sc);
	vtbe_init_locked(sc);
	VTBE_UNLOCK(sc);
}

static int
vtbe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifmediareq *ifmr;
	struct vtbe_softc *sc;
	struct ifreq *ifr;
	int mask, error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	error = 0;
	switch (cmd) {
	case SIOCSIFFLAGS:
		VTBE_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			pio_enable_irq(sc, 1);

			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
				vtbe_init_locked(sc);
			}
		} else {
			pio_enable_irq(sc, 0);

			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				vtbe_stop_locked(sc);
			}
		}
		sc->if_flags = ifp->if_flags;
		VTBE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		ifmr = (struct ifmediareq *)data;
		ifmr->ifm_count = 1;
		ifmr->ifm_status = (IFM_AVALID | IFM_ACTIVE);
		ifmr->ifm_active = (IFM_ETHER | IFM_10G_T | IFM_FDX);
		ifmr->ifm_current = ifmr->ifm_active;
		break;
	case SIOCSIFCAP:
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (mask & IFCAP_VLAN_MTU) {
			ifp->if_capenable ^= IFCAP_VLAN_MTU;
		}
		break;

	case SIOCSIFADDR:
		pio_enable_irq(sc, 1);
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
vtbe_txfinish_locked(struct vtbe_softc *sc)
{
	struct ifnet *ifp;

	VTBE_ASSERT_LOCKED(sc);

	ifp = sc->ifp;
}

static int
vq_init(struct vtbe_softc *sc)
{
	struct vqueue_info *vq;
	uint8_t *base;
	int size;
	int reg;
	int pfn;

	vq = &sc->vs_queues[sc->vs_curq];
	vq->vq_qsize = DESC_COUNT;

	reg = READ4(sc, VIRTIO_MMIO_QUEUE_PFN);
	pfn = be32toh(reg);
	vq->vq_pfn = pfn;

	size = vring_size(vq->vq_qsize, VRING_ALIGN);
	base = paddr_map(sc->beri_mem_offset,
		(pfn << PAGE_SHIFT), size);

	/* First pages are descriptors */
	vq->vq_desc = (struct vring_desc *)base;
	base += vq->vq_qsize * sizeof(struct vring_desc);

	/* Then avail ring */
	vq->vq_avail = (struct vring_avail *)base;
	base += (2 + vq->vq_qsize + 1) * sizeof(uint16_t);

	/* Then it's rounded up to the next page */
	base = (uint8_t *)roundup2((uintptr_t)base, VRING_ALIGN);

	/* And the last pages are the used ring */
	vq->vq_used = (struct vring_used *)base;

	/* Mark queue as allocated, and start at 0 when we use it. */
	vq->vq_flags = VQ_ALLOC;
	vq->vq_last_avail = 0;

	return (0);
}

static void
vtbe_proc_rx(struct vtbe_softc *sc, struct vqueue_info *vq)
{
	struct iovec iov[DESC_COUNT];
	struct iovec *tiov;
	struct ifnet *ifp;
	struct uio uio;
	struct mbuf *m;
	int iolen;
	int i;
	int n;

	ifp = sc->ifp;

	n = vq_getchain(sc->beri_mem_offset, vq, iov,
		DESC_COUNT, NULL);

	KASSERT(n >= 1 && n <= DESC_COUNT,
		("wrong n %d", n));

	tiov = getcopy(iov, n);

	iolen = 0;
	for (i = 1; i < n; i++) {
		iolen += iov[i].iov_len;
	}

	uio.uio_resid = iolen;
	uio.uio_iov = &tiov[1];
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_iovcnt = (n - 1);
	uio.uio_rw = UIO_WRITE;

	if ((m = m_uiotombuf(&uio, M_NOWAIT, 0, ETHER_ALIGN,
	    M_PKTHDR)) == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		goto done;
	}

	m->m_pkthdr.rcvif = ifp;

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

	CURVNET_SET(ifp->if_vnet);
	VTBE_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	VTBE_LOCK(sc);
	CURVNET_RESTORE();

done:
	free(tiov, M_DEVBUF);
	vq_relchain(vq, iov, n, iolen + sc->hdrsize);
}

static void
vtbe_rxfinish_locked(struct vtbe_softc *sc)
{
	struct vqueue_info *vq;
	int reg;

	/* TX queue */
	vq = &sc->vs_queues[1];
	if (!vq_ring_ready(vq))
		return;

	/* Process new descriptors */
	vq->vq_save_used = be16toh(vq->vq_used->idx);

	while (vq_has_descs(vq)) {
		vtbe_proc_rx(sc, vq);
	}

	/* Interrupt the other side */
	reg = htobe32(VIRTIO_MMIO_INT_VRING);
	WRITE4(sc, VIRTIO_MMIO_INTERRUPT_STATUS, reg);

	PIO_SET(sc->pio_send, Q_INTR, 1);
}

static void
vtbe_intr(void *arg)
{
	struct vtbe_softc *sc;
	int pending;
	uint32_t reg;

	sc = arg;

	VTBE_LOCK(sc);

	reg = PIO_READ(sc->pio_recv);

	/* Ack */
	PIO_SET(sc->pio_recv, reg, 0);

	pending = htobe32(reg);
	if (pending & Q_SEL) {
		reg = READ4(sc, VIRTIO_MMIO_QUEUE_SEL);
		sc->vs_curq = be32toh(reg);
	}

	if (pending & Q_PFN) {
		vq_init(sc);
	}

	if (pending & Q_NOTIFY) {
		/* beri rx / arm tx notify */
		vtbe_txfinish_locked(sc);
	}

	if (pending & Q_NOTIFY1) {
		vtbe_rxfinish_locked(sc);
	}

	VTBE_UNLOCK(sc);
}

static int
vtbe_get_hwaddr(struct vtbe_softc *sc, uint8_t *hwaddr)
{
	int rnd;

	/*
	 * Generate MAC address, use 'bsd' + random 24 low-order bits.
	 */

	rnd = arc4random() & 0x00ffffff;

	hwaddr[0] = 'b';
	hwaddr[1] = 's';
	hwaddr[2] = 'd';
	hwaddr[3] = rnd >> 16;
	hwaddr[4] = rnd >>  8;
	hwaddr[5] = rnd >>  0;

	return (0);
}

static int
pio_enable_irq(struct vtbe_softc *sc, int enable)
{

	/*
	 * IRQ lines should be disabled while reprogram FPGA core.
	 */

	if (enable) {
		if (sc->opened == 0) {
			sc->opened = 1;
			PIO_SETUP_IRQ(sc->pio_recv, vtbe_intr, sc);
		}
	} else {
		if (sc->opened == 1) {
			PIO_TEARDOWN_IRQ(sc->pio_recv);
			sc->opened = 0;
		}
	}

	return (0);
}

static int
vtbe_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "sri-cambridge,beri-vtnet"))
		return (ENXIO);

	device_set_desc(dev, "Virtio BERI Ethernet Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
vtbe_attach(device_t dev)
{
	uint8_t macaddr[ETHER_ADDR_LEN];
	struct vtbe_softc *sc;
	struct ifnet *ifp;
	int reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->hdrsize = sizeof(struct virtio_net_hdr);

	if (bus_alloc_resources(dev, vtbe_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	mtx_init(&sc->mtx, device_get_nameunit(sc->dev),
	    MTX_NETWORK_LOCK, MTX_DEF);

	if (setup_offset(dev, &sc->beri_mem_offset) != 0)
		return (ENXIO);
	if (setup_pio(dev, "pio-send", &sc->pio_send) != 0)
		return (ENXIO);
	if (setup_pio(dev, "pio-recv", &sc->pio_recv) != 0)
		return (ENXIO);

	/* Setup MMIO */

	/* Specify that we provide network device */
	reg = htobe32(VIRTIO_ID_NETWORK);
	WRITE4(sc, VIRTIO_MMIO_DEVICE_ID, reg);

	/* The number of desc we support */
	reg = htobe32(DESC_COUNT);
	WRITE4(sc, VIRTIO_MMIO_QUEUE_NUM_MAX, reg);

	/* Our features */
	reg = htobe32(VIRTIO_NET_F_MAC |
    			VIRTIO_F_NOTIFY_ON_EMPTY);
	WRITE4(sc, VIRTIO_MMIO_HOST_FEATURES, reg);

	/* Get MAC */
	if (vtbe_get_hwaddr(sc, macaddr)) {
		device_printf(sc->dev, "can't get mac\n");
		return (ENXIO);
	}

	/* Set up the ethernet interface. */
	sc->ifp = ifp = if_alloc(IFT_ETHER);
	ifp->if_baudrate = IF_Gbps(10);
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = (IFF_BROADCAST | IFF_SIMPLEX |
			 IFF_MULTICAST | IFF_PROMISC);
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_start = vtbe_txstart;
	ifp->if_ioctl = vtbe_ioctl;
	ifp->if_init = vtbe_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, DESC_COUNT - 1);
	ifp->if_snd.ifq_drv_maxlen = DESC_COUNT - 1;
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* All ready to run, attach the ethernet interface. */
	ether_ifattach(ifp, macaddr);

	sc->is_attached = true;

	return (0);
}

static device_method_t vtbe_methods[] = {
	DEVMETHOD(device_probe,		vtbe_probe),
	DEVMETHOD(device_attach,	vtbe_attach),

	{ 0, 0 }
};

static driver_t vtbe_driver = {
	"vtbe",
	vtbe_methods,
	sizeof(struct vtbe_softc),
};

static devclass_t vtbe_devclass;

DRIVER_MODULE(vtbe, simplebus, vtbe_driver, vtbe_devclass, 0, 0);
MODULE_DEPEND(vtbe, ether, 1, 1, 1);
