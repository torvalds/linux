/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2012 Juli Mallett <jmallett@FreeBSD.org>
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
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_vlan_var.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <machine/cpuregs.h>

#include <dev/gxemul/ether/gxreg.h>

struct gx_softc {
	struct ifnet *sc_ifp;
	device_t sc_dev;
	unsigned sc_port;
	int sc_flags;
	struct ifmedia sc_ifmedia;
	struct resource *sc_intr;
	void *sc_intr_cookie;
	struct mtx sc_mtx;
};

#define	GXEMUL_ETHER_LOCK(sc)	mtx_lock(&(sc)->sc_mtx)
#define	GXEMUL_ETHER_UNLOCK(sc)	mtx_unlock(&(sc)->sc_mtx)

static void	gx_identify(driver_t *, device_t);
static int	gx_probe(device_t);
static int	gx_attach(device_t);
static int	gx_detach(device_t);
static int	gx_shutdown(device_t);

static void	gx_init(void *);
static int	gx_transmit(struct ifnet *, struct mbuf *);

static int	gx_medchange(struct ifnet *);
static void	gx_medstat(struct ifnet *, struct ifmediareq *);

static int	gx_ioctl(struct ifnet *, u_long, caddr_t);

static void	gx_rx_intr(void *);

static device_method_t gx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	gx_identify),
	DEVMETHOD(device_probe,		gx_probe),
	DEVMETHOD(device_attach,	gx_attach),
	DEVMETHOD(device_detach,	gx_detach),
	DEVMETHOD(device_shutdown,	gx_shutdown),

	{ 0, 0 }
};

static driver_t gx_driver = {
	"gx",
	gx_methods,
	sizeof (struct gx_softc),
};

static devclass_t gx_devclass;

DRIVER_MODULE(gx, nexus, gx_driver, gx_devclass, 0, 0);

static void
gx_identify(driver_t *drv, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "gx", 0);
}

static int
gx_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "GXemul test Ethernet");

	return (BUS_PROBE_NOWILDCARD);
}

static int
gx_attach(device_t dev)
{
	struct ifnet *ifp;
	struct gx_softc *sc;
	uint8_t mac[6];
	int error;
	int rid;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_port = device_get_unit(dev);

	/* Read MAC address.  */
	GXEMUL_ETHER_DEV_WRITE(GXEMUL_ETHER_DEV_MAC, (uintptr_t)mac);

	/* Allocate and establish interrupt.  */
	rid = 0;
	sc->sc_intr = bus_alloc_resource(sc->sc_dev, SYS_RES_IRQ, &rid,
	    GXEMUL_ETHER_DEV_IRQ - 2, GXEMUL_ETHER_DEV_IRQ - 2, 1, RF_ACTIVE);
	if (sc->sc_intr == NULL) {
		device_printf(dev, "unable to allocate IRQ.\n");
		return (ENXIO);
	}

	error = bus_setup_intr(sc->sc_dev, sc->sc_intr, INTR_TYPE_NET, NULL,
	    gx_rx_intr, sc, &sc->sc_intr_cookie);
	if (error != 0) {
		device_printf(dev, "unable to setup interrupt.\n");
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr);
		return (ENXIO);
	}

	bus_describe_intr(sc->sc_dev, sc->sc_intr, sc->sc_intr_cookie, "rx");

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet.\n");
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr);
		return (ENOMEM);
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_mtu = ETHERMTU;
	ifp->if_init = gx_init;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_ALLMULTI;
	ifp->if_ioctl = gx_ioctl;

	sc->sc_ifp = ifp;
	sc->sc_flags = ifp->if_flags;

	ifmedia_init(&sc->sc_ifmedia, 0, gx_medchange, gx_medstat);

	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO);

	mtx_init(&sc->sc_mtx, "GXemul Ethernet", NULL, MTX_DEF);

	ether_ifattach(ifp, mac);

	ifp->if_transmit = gx_transmit;

	return (bus_generic_attach(dev));
}

static int
gx_detach(device_t dev)
{
	struct gx_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr);
	/* XXX Incomplete.  */

	return (0);
}

static int
gx_shutdown(device_t dev)
{
	return (gx_detach(dev));
}

static void
gx_init(void *arg)
{
	struct ifnet *ifp;
	struct gx_softc *sc;

	sc = arg;
	ifp = sc->sc_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
}

static int
gx_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct gx_softc *sc;

	sc = ifp->if_softc;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != IFF_DRV_RUNNING) {
		m_freem(m);
		return (0);
	}

	GXEMUL_ETHER_LOCK(sc);
	GXEMUL_ETHER_DEV_WRITE(GXEMUL_ETHER_DEV_LENGTH, m->m_pkthdr.len);
	m_copydata(m, 0, m->m_pkthdr.len, (void *)(uintptr_t)GXEMUL_ETHER_DEV_FUNCTION(GXEMUL_ETHER_DEV_BUFFER));
	GXEMUL_ETHER_DEV_WRITE(GXEMUL_ETHER_DEV_COMMAND, GXEMUL_ETHER_DEV_COMMAND_TX);
	GXEMUL_ETHER_UNLOCK(sc);

	ETHER_BPF_MTAP(ifp, m);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);

	m_freem(m);

	return (0);
}

static int
gx_medchange(struct ifnet *ifp)
{
	return (ENOTSUP);
}

static void
gx_medstat(struct ifnet *ifp, struct ifmediareq *ifm)
{
	struct gx_softc *sc;

	sc = ifp->if_softc;

	/* Lie amazingly.  */
	ifm->ifm_status = IFM_AVALID | IFM_ACTIVE;
	ifm->ifm_active = IFT_ETHER | IFM_1000_T | IFM_FDX;
}

static int
gx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct gx_softc *sc;
	struct ifreq *ifr;
#ifdef INET
	struct ifaddr *ifa;
#endif
	int error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
#ifdef INET
	ifa = (struct ifaddr *)data;
#endif

	switch (cmd) {
	case SIOCSIFADDR:
#ifdef INET
		/*
		 * Avoid reinitialization unless it's necessary.
		 */
		if (ifa->ifa_addr->sa_family == AF_INET) {
			ifp->if_flags |= IFF_UP;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
				gx_init(sc);
			arp_ifinit(ifp, ifa);

			return (0);
		}
#endif
		error = ether_ioctl(ifp, cmd, data);
		if (error != 0)
			return (error);
		return (0);

	case SIOCSIFFLAGS:
		if (ifp->if_flags == sc->sc_flags)
			return (0);
		if ((ifp->if_flags & IFF_UP) != 0) {
			gx_init(sc);
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			}
		}
		sc->sc_flags = ifp->if_flags;
		return (0);

	case SIOCSIFMTU:
		if (ifr->ifr_mtu + ifp->if_hdrlen > GXEMUL_ETHER_DEV_MTU)
			return (ENOTSUP);
		return (0);

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_ifmedia, cmd);
		if (error != 0)
			return (error);
		return (0);
	
	default:
		error = ether_ioctl(ifp, cmd, data);
		if (error != 0)
			return (error);
		return (0);
	}
}

static void
gx_rx_intr(void *arg)
{
	struct gx_softc *sc = arg;

	GXEMUL_ETHER_LOCK(sc);
	for (;;) {
		uint64_t status, length;
		struct mbuf *m;

		/*
		 * XXX
		 * Limit number of packets received at once?
		 */
		status = GXEMUL_ETHER_DEV_READ(GXEMUL_ETHER_DEV_STATUS);
		if (status == GXEMUL_ETHER_DEV_STATUS_RX_MORE) {
			GXEMUL_ETHER_DEV_WRITE(GXEMUL_ETHER_DEV_COMMAND, GXEMUL_ETHER_DEV_COMMAND_RX);
			continue;
		}
		if (status != GXEMUL_ETHER_DEV_STATUS_RX_OK)
			break;
		length = GXEMUL_ETHER_DEV_READ(GXEMUL_ETHER_DEV_LENGTH);
		if (length > MCLBYTES - ETHER_ALIGN) {
			if_inc_counter(sc->sc_ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}

		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			device_printf(sc->sc_dev, "no memory for receive mbuf.\n");
			if_inc_counter(sc->sc_ifp, IFCOUNTER_IQDROPS, 1);
			GXEMUL_ETHER_UNLOCK(sc);
			return;
		}

		/* Align incoming frame so IP headers are aligned.  */
		m->m_data += ETHER_ALIGN;

		memcpy(m->m_data, (const void *)(uintptr_t)GXEMUL_ETHER_DEV_FUNCTION(GXEMUL_ETHER_DEV_BUFFER), length);

		m->m_pkthdr.rcvif = sc->sc_ifp;
		m->m_pkthdr.len = m->m_len = length;

		if_inc_counter(sc->sc_ifp, IFCOUNTER_IPACKETS, 1);

		GXEMUL_ETHER_UNLOCK(sc);

		(*sc->sc_ifp->if_input)(sc->sc_ifp, m);

		GXEMUL_ETHER_LOCK(sc);
	}
	GXEMUL_ETHER_UNLOCK(sc);
}
