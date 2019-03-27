/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2013 Nathan Whitehorn
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <powerpc/pseries/phyp-hvcall.h>

#define LLAN_MAX_RX_PACKETS	100
#define LLAN_MAX_TX_PACKETS	100
#define LLAN_RX_BUF_LEN		8*PAGE_SIZE

#define LLAN_BUFDESC_VALID	(1ULL << 63)
#define LLAN_ADD_MULTICAST	0x1
#define LLAN_DEL_MULTICAST	0x2
#define LLAN_CLEAR_MULTICAST	0x3

struct llan_xfer {
	struct mbuf *rx_mbuf;
	bus_dmamap_t rx_dmamap;
	uint64_t rx_bufdesc;
};

struct llan_receive_queue_entry { /* PAPR page 539 */
	uint8_t control;
	uint8_t reserved;
	uint16_t offset;
	uint32_t length;
	uint64_t handle;
} __packed;

struct llan_softc {
	device_t	dev;
	struct mtx	io_lock;

	cell_t		unit;
	uint8_t		mac_address[8];

	struct ifmedia	media;

	int		irqid;
	struct resource	*irq;
	void		*irq_cookie;

	bus_dma_tag_t	rx_dma_tag;
	bus_dma_tag_t	rxbuf_dma_tag;
	bus_dma_tag_t	tx_dma_tag;

	bus_dmamap_t	tx_dma_map;

	struct llan_receive_queue_entry *rx_buf;
	int		rx_dma_slot;
	int		rx_valid_val;
	bus_dmamap_t	rx_buf_map;
	bus_addr_t	rx_buf_phys;
	bus_size_t	rx_buf_len;
	bus_addr_t	input_buf_phys;
	bus_addr_t	filter_buf_phys;
	struct llan_xfer rx_xfer[LLAN_MAX_RX_PACKETS];

	struct ifnet	*ifp;
};

static int	llan_probe(device_t);
static int	llan_attach(device_t);
static void	llan_intr(void *xsc);
static void	llan_init(void *xsc);
static void	llan_start(struct ifnet *ifp);
static int	llan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static void	llan_media_status(struct ifnet *ifp, struct ifmediareq *ifmr);
static int	llan_media_change(struct ifnet *ifp);
static void	llan_rx_load_cb(void *xsc, bus_dma_segment_t *segs, int nsegs,
		    int err);
static int	llan_add_rxbuf(struct llan_softc *sc, struct llan_xfer *rx);
static int	llan_set_multicast(struct llan_softc *sc);

static devclass_t       llan_devclass;
static device_method_t  llan_methods[] = {
        DEVMETHOD(device_probe,         llan_probe),
        DEVMETHOD(device_attach,        llan_attach),
        
        DEVMETHOD_END
};
static driver_t llan_driver = {
        "llan",
        llan_methods,
        sizeof(struct llan_softc)
};
DRIVER_MODULE(llan, vdevice, llan_driver, llan_devclass, 0, 0);

static int
llan_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev,"IBM,l-lan"))
		return (ENXIO);

	device_set_desc(dev, "POWER Hypervisor Virtual Ethernet");
	return (0);
}

static int
llan_attach(device_t dev)
{
	struct llan_softc *sc;
	phandle_t node;
	int error, i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Get firmware properties */
	node = ofw_bus_get_node(dev);
	OF_getprop(node, "local-mac-address", sc->mac_address,
	    sizeof(sc->mac_address));
	OF_getencprop(node, "reg", &sc->unit, sizeof(sc->unit));

	mtx_init(&sc->io_lock, "llan", NULL, MTX_DEF);

        /* Setup interrupt */
	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
	    RF_ACTIVE);

	if (!sc->irq) {
		device_printf(dev, "Could not allocate IRQ\n");
		mtx_destroy(&sc->io_lock);
		return (ENXIO);
	}

	bus_setup_intr(dev, sc->irq, INTR_TYPE_MISC | INTR_MPSAFE |
	    INTR_ENTROPY, NULL, llan_intr, sc, &sc->irq_cookie);

	/* Setup DMA */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 16, 0,
            BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    LLAN_RX_BUF_LEN, 1, BUS_SPACE_MAXSIZE_32BIT,
	    0, NULL, NULL, &sc->rx_dma_tag);
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
            BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE, 1, BUS_SPACE_MAXSIZE_32BIT,
	    0, NULL, NULL, &sc->rxbuf_dma_tag);
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
            BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE, 6, BUS_SPACE_MAXSIZE_32BIT, 0,
	    busdma_lock_mutex, &sc->io_lock, &sc->tx_dma_tag);

	error = bus_dmamem_alloc(sc->rx_dma_tag, (void **)&sc->rx_buf,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->rx_buf_map);
	error = bus_dmamap_load(sc->rx_dma_tag, sc->rx_buf_map, sc->rx_buf,
	    LLAN_RX_BUF_LEN, llan_rx_load_cb, sc, 0);

	/* TX DMA maps */
	bus_dmamap_create(sc->tx_dma_tag, 0, &sc->tx_dma_map);

	/* RX DMA */
	for (i = 0; i < LLAN_MAX_RX_PACKETS; i++) {
		error = bus_dmamap_create(sc->rxbuf_dma_tag, 0,
		    &sc->rx_xfer[i].rx_dmamap);
		sc->rx_xfer[i].rx_mbuf = NULL;
	}

	/* Attach to network stack */
	sc->ifp = if_alloc(IFT_ETHER);
	sc->ifp->if_softc = sc;

	if_initname(sc->ifp, device_get_name(dev), device_get_unit(dev));
	sc->ifp->if_mtu = ETHERMTU; /* XXX max-frame-size from OF? */
	sc->ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	sc->ifp->if_hwassist = 0; /* XXX: ibm,illan-options */
	sc->ifp->if_capabilities = 0;
	sc->ifp->if_capenable = 0;
	sc->ifp->if_start = llan_start;
	sc->ifp->if_ioctl = llan_ioctl;
	sc->ifp->if_init = llan_init;

	ifmedia_init(&sc->media, IFM_IMASK, llan_media_change,
	    llan_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	IFQ_SET_MAXLEN(&sc->ifp->if_snd, LLAN_MAX_TX_PACKETS);
	sc->ifp->if_snd.ifq_drv_maxlen = LLAN_MAX_TX_PACKETS;
	IFQ_SET_READY(&sc->ifp->if_snd);

	ether_ifattach(sc->ifp, &sc->mac_address[2]);

	/* We don't have link state reporting, so make it always up */
	if_link_state_change(sc->ifp, LINK_STATE_UP);

	return (0);
}

static int
llan_media_change(struct ifnet *ifp)
{
	struct llan_softc *sc = ifp->if_softc;

	if (IFM_TYPE(sc->media.ifm_media) != IFM_ETHER)
		return (EINVAL);

	if (IFM_SUBTYPE(sc->media.ifm_media) != IFM_AUTO)
		return (EINVAL);

	return (0);
}

static void
llan_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{

	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE | IFM_UNKNOWN | IFM_FDX;
	ifmr->ifm_active = IFM_ETHER;
}

static void
llan_rx_load_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int err)
{
	struct llan_softc *sc = xsc;

	sc->rx_buf_phys = segs[0].ds_addr;
	sc->rx_buf_len = segs[0].ds_len - 2*PAGE_SIZE;
	sc->input_buf_phys = segs[0].ds_addr + segs[0].ds_len - PAGE_SIZE;
	sc->filter_buf_phys = segs[0].ds_addr + segs[0].ds_len - 2*PAGE_SIZE;
}

static void
llan_init(void *xsc)
{
	struct llan_softc *sc = xsc;
	uint64_t rx_buf_desc;
	uint64_t macaddr;
	int err, i;

	mtx_lock(&sc->io_lock);

	phyp_hcall(H_FREE_LOGICAL_LAN, sc->unit);

	/* Create buffers (page 539) */
	sc->rx_dma_slot = 0;
	sc->rx_valid_val = 1;

	rx_buf_desc = LLAN_BUFDESC_VALID;
	rx_buf_desc |= (sc->rx_buf_len << 32);
	rx_buf_desc |= sc->rx_buf_phys;
	memcpy(&macaddr, sc->mac_address, 8);
	err = phyp_hcall(H_REGISTER_LOGICAL_LAN, sc->unit, sc->input_buf_phys,
	    rx_buf_desc, sc->filter_buf_phys, macaddr);

	for (i = 0; i < LLAN_MAX_RX_PACKETS; i++)
		llan_add_rxbuf(sc, &sc->rx_xfer[i]);

	phyp_hcall(H_VIO_SIGNAL, sc->unit, 1); /* Enable interrupts */

	/* Tell stack we're up */
	sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	mtx_unlock(&sc->io_lock);

	/* Check for pending receives scheduled before interrupt enable */
	llan_intr(sc);
}

static int
llan_add_rxbuf(struct llan_softc *sc, struct llan_xfer *rx)
{
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	int error, nsegs;

	mtx_assert(&sc->io_lock, MA_OWNED);

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);

	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;
	if (rx->rx_mbuf != NULL) {
		bus_dmamap_sync(sc->rxbuf_dma_tag, rx->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->rxbuf_dma_tag, rx->rx_dmamap);
	}

	/* Save pointer to buffer structure */
	m_copyback(m, 0, 8, (void *)&rx);

	error = bus_dmamap_load_mbuf_sg(sc->rxbuf_dma_tag, rx->rx_dmamap, m,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->dev,
		    "cannot load RX DMA map %p, error = %d\n", rx, error);
		m_freem(m);
		return (error);
	}

	/* If nsegs is wrong then the stack is corrupt. */
	KASSERT(nsegs == 1,
	    ("%s: too many DMA segments (%d)", __func__, nsegs));
	rx->rx_mbuf = m;

	bus_dmamap_sync(sc->rxbuf_dma_tag, rx->rx_dmamap, BUS_DMASYNC_PREREAD);

	rx->rx_bufdesc = LLAN_BUFDESC_VALID;
	rx->rx_bufdesc |= (((uint64_t)segs[0].ds_len) << 32);
	rx->rx_bufdesc |= segs[0].ds_addr;
	error = phyp_hcall(H_ADD_LOGICAL_LAN_BUFFER, sc->unit, rx->rx_bufdesc);
	if (error != 0) {
		m_freem(m);
		rx->rx_mbuf = NULL;
		return (ENOBUFS);
	}

        return (0);
}

static void
llan_intr(void *xsc)
{
	struct llan_softc *sc = xsc;
	struct llan_xfer *rx;
	struct mbuf *m;

	mtx_lock(&sc->io_lock);
restart:
	phyp_hcall(H_VIO_SIGNAL, sc->unit, 0);

	while ((sc->rx_buf[sc->rx_dma_slot].control >> 7) == sc->rx_valid_val) {
		rx = (struct llan_xfer *)sc->rx_buf[sc->rx_dma_slot].handle;
		m = rx->rx_mbuf;
		m_adj(m, sc->rx_buf[sc->rx_dma_slot].offset - 8);
		m->m_len = sc->rx_buf[sc->rx_dma_slot].length;

		/* llan_add_rxbuf does DMA sync and unload as well as requeue */
		if (llan_add_rxbuf(sc, rx) != 0) {
			if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}

		if_inc_counter(sc->ifp, IFCOUNTER_IPACKETS, 1);
		m_adj(m, sc->rx_buf[sc->rx_dma_slot].offset);
		m->m_len = sc->rx_buf[sc->rx_dma_slot].length;
		m->m_pkthdr.rcvif = sc->ifp;
		m->m_pkthdr.len = m->m_len;
		sc->rx_dma_slot++;

		if (sc->rx_dma_slot >= sc->rx_buf_len/sizeof(sc->rx_buf[0])) {
			sc->rx_dma_slot = 0;
			sc->rx_valid_val = !sc->rx_valid_val;
		}

		mtx_unlock(&sc->io_lock);
		(*sc->ifp->if_input)(sc->ifp, m);
		mtx_lock(&sc->io_lock);
	}

	phyp_hcall(H_VIO_SIGNAL, sc->unit, 1);

	/*
	 * H_VIO_SIGNAL enables interrupts for future packets only.
	 * Make sure none were queued between the end of the loop and the
	 * enable interrupts call.
	 */
	if ((sc->rx_buf[sc->rx_dma_slot].control >> 7) == sc->rx_valid_val)
		goto restart;

	mtx_unlock(&sc->io_lock);
}

static void
llan_send_packet(void *xsc, bus_dma_segment_t *segs, int nsegs,
    bus_size_t mapsize, int error)
{
	struct llan_softc *sc = xsc;
	uint64_t bufdescs[6];
	int i;

	bzero(bufdescs, sizeof(bufdescs));

	for (i = 0; i < nsegs; i++) {
		bufdescs[i] = LLAN_BUFDESC_VALID;
		bufdescs[i] |= (((uint64_t)segs[i].ds_len) << 32);
		bufdescs[i] |= segs[i].ds_addr;
	}

	phyp_hcall(H_SEND_LOGICAL_LAN, sc->unit, bufdescs[0],
	    bufdescs[1], bufdescs[2], bufdescs[3], bufdescs[4], bufdescs[5], 0);
	/*
	 * The hypercall returning implies completion -- or that the call will
	 * not complete. In principle, we should try a few times if we get back
	 * H_BUSY based on the continuation token in R4. For now, just drop
	 * the packet in such cases.
	 */
}

static void
llan_start_locked(struct ifnet *ifp)
{
	struct llan_softc *sc = ifp->if_softc;
	bus_addr_t first;
	int nsegs;
	struct mbuf *mb_head, *m;

	mtx_assert(&sc->io_lock, MA_OWNED);
	first = 0;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, mb_head);

		if (mb_head == NULL)
			break;

		BPF_MTAP(ifp, mb_head);

		for (m = mb_head, nsegs = 0; m != NULL; m = m->m_next)
			nsegs++;
		if (nsegs > 6) {
			m = m_collapse(mb_head, M_NOWAIT, 6);
			if (m == NULL) {
				m_freem(mb_head);
				continue;
			}
		}

		bus_dmamap_load_mbuf(sc->tx_dma_tag, sc->tx_dma_map,
			mb_head, llan_send_packet, sc, 0);
		bus_dmamap_unload(sc->tx_dma_tag, sc->tx_dma_map);
		m_freem(mb_head);
	}
}

static void
llan_start(struct ifnet *ifp)
{
	struct llan_softc *sc = ifp->if_softc;

	mtx_lock(&sc->io_lock);
	llan_start_locked(ifp);
	mtx_unlock(&sc->io_lock);
}

static int
llan_set_multicast(struct llan_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct ifmultiaddr *inm;
	uint64_t macaddr;

	mtx_assert(&sc->io_lock, MA_OWNED);

	phyp_hcall(H_MULTICAST_CTRL, sc->unit, LLAN_CLEAR_MULTICAST, 0);

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(inm, &ifp->if_multiaddrs, ifma_link) {
		if (inm->ifma_addr->sa_family != AF_LINK)
			continue;

		memcpy((uint8_t *)&macaddr + 2,
		    LLADDR((struct sockaddr_dl *)inm->ifma_addr), 6);
		phyp_hcall(H_MULTICAST_CTRL, sc->unit, LLAN_ADD_MULTICAST,
		    macaddr);
	}
	if_maddr_runlock(ifp);

	return (0);
}

static int
llan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	int err = 0;
	struct llan_softc *sc = ifp->if_softc;

	switch (cmd) {
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		mtx_lock(&sc->io_lock);
		if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			llan_set_multicast(sc);
		mtx_unlock(&sc->io_lock);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		err = ifmedia_ioctl(ifp, (struct ifreq *)data, &sc->media, cmd);
		break;
	case SIOCSIFFLAGS:
	default:
		err = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (err);
}

