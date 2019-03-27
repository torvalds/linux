/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Juli Mallett <jmallett@FreeBSD.org>
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

/*
 * Cavium Octeon management port Ethernet devices.
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

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>
#include <contrib/octeon-sdk/cvmx-mgmt-port.h>

struct octm_softc {
	struct ifnet *sc_ifp;
	device_t sc_dev;
	unsigned sc_port;
	int sc_flags;
	struct ifmedia sc_ifmedia;
	struct resource *sc_intr;
	void *sc_intr_cookie;
};

static void	octm_identify(driver_t *, device_t);
static int	octm_probe(device_t);
static int	octm_attach(device_t);
static int	octm_detach(device_t);
static int	octm_shutdown(device_t);

static void	octm_init(void *);
static int	octm_transmit(struct ifnet *, struct mbuf *);

static int	octm_medchange(struct ifnet *);
static void	octm_medstat(struct ifnet *, struct ifmediareq *);

static int	octm_ioctl(struct ifnet *, u_long, caddr_t);

static void	octm_rx_intr(void *);

static device_method_t octm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	octm_identify),
	DEVMETHOD(device_probe,		octm_probe),
	DEVMETHOD(device_attach,	octm_attach),
	DEVMETHOD(device_detach,	octm_detach),
	DEVMETHOD(device_shutdown,	octm_shutdown),

	{ 0, 0 }
};

static driver_t octm_driver = {
	"octm",
	octm_methods,
	sizeof (struct octm_softc),
};

static devclass_t octm_devclass;

DRIVER_MODULE(octm, ciu, octm_driver, octm_devclass, 0, 0);

static void
octm_identify(driver_t *drv, device_t parent)
{
	unsigned i;

	if (!octeon_has_feature(OCTEON_FEATURE_MGMT_PORT))
		return;

	for (i = 0; i < CVMX_MGMT_PORT_NUM_PORTS; i++)
		BUS_ADD_CHILD(parent, 0, "octm", i);
}

static int
octm_probe(device_t dev)
{
	cvmx_mgmt_port_result_t result;

	result = cvmx_mgmt_port_initialize(device_get_unit(dev));
	switch (result) {
	case CVMX_MGMT_PORT_SUCCESS:
		break;
	case CVMX_MGMT_PORT_NO_MEMORY:
		return (ENOBUFS);
	case CVMX_MGMT_PORT_INVALID_PARAM:
		return (ENXIO);
	case CVMX_MGMT_PORT_INIT_ERROR:
		return (EIO);
	}

	device_set_desc(dev, "Cavium Octeon Management Ethernet");

	return (0);
}

static int
octm_attach(device_t dev)
{
	struct ifnet *ifp;
	struct octm_softc *sc;
	cvmx_mixx_irhwm_t mixx_irhwm;
	cvmx_mixx_intena_t mixx_intena;
	uint64_t mac;
	int error;
	int irq;
	int rid;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_port = device_get_unit(dev);

	switch (sc->sc_port) {
	case 0:
		irq = OCTEON_IRQ_MII;
		break;
	case 1:
		irq = OCTEON_IRQ_MII1;
		break;
	default:
		device_printf(dev, "unsupported management port %u.\n", sc->sc_port);
		return (ENXIO);
	}

	/*
	 * Set MAC address for this management port.
	 */
	mac = 0;
	memcpy((u_int8_t *)&mac + 2, cvmx_sysinfo_get()->mac_addr_base, 6);
	mac += sc->sc_port;

	cvmx_mgmt_port_set_mac(sc->sc_port, mac);

	/* No watermark for input ring.  */
	mixx_irhwm.u64 = 0;
	cvmx_write_csr(CVMX_MIXX_IRHWM(sc->sc_port), mixx_irhwm.u64);

	/* Enable input ring interrupts.  */
	mixx_intena.u64 = 0;
	mixx_intena.s.ithena = 1;
	cvmx_write_csr(CVMX_MIXX_INTENA(sc->sc_port), mixx_intena.u64);

	/* Allocate and establish interrupt.  */
	rid = 0;
	sc->sc_intr = bus_alloc_resource(sc->sc_dev, SYS_RES_IRQ, &rid,
	    irq, irq, 1, RF_ACTIVE);
	if (sc->sc_intr == NULL) {
		device_printf(dev, "unable to allocate IRQ.\n");
		return (ENXIO);
	}

	error = bus_setup_intr(sc->sc_dev, sc->sc_intr, INTR_TYPE_NET, NULL,
	    octm_rx_intr, sc, &sc->sc_intr_cookie);
	if (error != 0) {
		device_printf(dev, "unable to setup interrupt.\n");
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr);
		return (ENXIO);
	}

	bus_describe_intr(sc->sc_dev, sc->sc_intr, sc->sc_intr_cookie, "rx");

	/* XXX Possibly should enable TX interrupts.  */

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet.\n");
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr);
		return (ENOMEM);
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_mtu = ETHERMTU;
	ifp->if_init = octm_init;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_ALLMULTI;
	ifp->if_ioctl = octm_ioctl;

	sc->sc_ifp = ifp;
	sc->sc_flags = ifp->if_flags;

	ifmedia_init(&sc->sc_ifmedia, 0, octm_medchange, octm_medstat);

	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO);

	ether_ifattach(ifp, (const u_int8_t *)&mac + 2);

	ifp->if_transmit = octm_transmit;

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;

	IFQ_SET_MAXLEN(&ifp->if_snd, CVMX_MGMT_PORT_NUM_TX_BUFFERS);
	ifp->if_snd.ifq_drv_maxlen = CVMX_MGMT_PORT_NUM_TX_BUFFERS;
	IFQ_SET_READY(&ifp->if_snd);

	return (bus_generic_attach(dev));
}

static int
octm_detach(device_t dev)
{
	struct octm_softc *sc;
	cvmx_mgmt_port_result_t result;

	sc = device_get_softc(dev);

	result = cvmx_mgmt_port_initialize(sc->sc_port);
	switch (result) {
	case CVMX_MGMT_PORT_SUCCESS:
		break;
	case CVMX_MGMT_PORT_NO_MEMORY:
		return (ENOBUFS);
	case CVMX_MGMT_PORT_INVALID_PARAM:
		return (ENXIO);
	case CVMX_MGMT_PORT_INIT_ERROR:
		return (EIO);
	}

	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_intr);
	/* XXX Incomplete.  */

	return (0);
}

static int
octm_shutdown(device_t dev)
{
	return (octm_detach(dev));
}

static void
octm_init(void *arg)
{
	struct ifnet *ifp;
	struct octm_softc *sc;
	cvmx_mgmt_port_netdevice_flags_t flags;
	uint64_t mac;

	sc = arg;
	ifp = sc->sc_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		cvmx_mgmt_port_disable(sc->sc_port);

		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	}

	/*
	 * NB:
	 * MAC must be set before allmulti and promisc below, as
	 * cvmx_mgmt_port_set_mac will always enable the CAM, and turning on
	 * promiscuous mode only works with the CAM disabled.
	 */
	mac = 0;
	memcpy((u_int8_t *)&mac + 2, IF_LLADDR(ifp), 6);
	cvmx_mgmt_port_set_mac(sc->sc_port, mac);

	/*
	 * This is done unconditionally, rather than only if sc_flags have
	 * changed because of set_mac's effect on the CAM noted above.
	 */
	flags = 0;
	if ((ifp->if_flags & IFF_ALLMULTI) != 0)
		flags |= CVMX_IFF_ALLMULTI;
	if ((ifp->if_flags & IFF_PROMISC) != 0)
		flags |= CVMX_IFF_PROMISC;
	cvmx_mgmt_port_set_multicast_list(sc->sc_port, flags);

	/* XXX link state?  */

	if ((ifp->if_flags & IFF_UP) != 0)
		cvmx_mgmt_port_enable(sc->sc_port);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static int
octm_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct octm_softc *sc;
	cvmx_mgmt_port_result_t result;

	sc = ifp->if_softc;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING) {
		m_freem(m);
		return (0);
	}

	result = cvmx_mgmt_port_sendm(sc->sc_port, m);

	if (result == CVMX_MGMT_PORT_SUCCESS) {
		ETHER_BPF_MTAP(ifp, m);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);
	} else
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	m_freem(m);

	switch (result) {
	case CVMX_MGMT_PORT_SUCCESS:
		return (0);
	case CVMX_MGMT_PORT_NO_MEMORY:
		return (ENOBUFS);
	case CVMX_MGMT_PORT_INVALID_PARAM:
		return (ENXIO);
	case CVMX_MGMT_PORT_INIT_ERROR:
		return (EIO);
	default:
		return (EDOOFUS);
	}
}

static int
octm_medchange(struct ifnet *ifp)
{
	return (ENOTSUP);
}

static void
octm_medstat(struct ifnet *ifp, struct ifmediareq *ifm)
{
	struct octm_softc *sc;
	cvmx_helper_link_info_t link_info;

	sc = ifp->if_softc;

	ifm->ifm_status = IFM_AVALID;
	ifm->ifm_active = IFT_ETHER;

	link_info = cvmx_mgmt_port_link_get(sc->sc_port);
	if (!link_info.s.link_up)
		return;

	ifm->ifm_status |= IFM_ACTIVE;

	switch (link_info.s.speed) {
	case 10:
		ifm->ifm_active |= IFM_10_T;
		break;
	case 100:
		ifm->ifm_active |= IFM_100_TX;
		break;
	case 1000:
		ifm->ifm_active |= IFM_1000_T;
		break;
	case 10000:
		ifm->ifm_active |= IFM_10G_T;
		break;
	}

	if (link_info.s.full_duplex)
		ifm->ifm_active |= IFM_FDX;
	else
		ifm->ifm_active |= IFM_HDX;
}

static int
octm_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct octm_softc *sc;
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
				octm_init(sc);
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
			octm_init(sc);
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				cvmx_mgmt_port_disable(sc->sc_port);

				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			}
		}
		sc->sc_flags = ifp->if_flags;
		return (0);
	
	case SIOCSIFCAP:
		/*
		 * Just change the capabilities in software, currently none
		 * require reprogramming hardware, they just toggle whether we
		 * make use of already-present facilities in software.
		 */
		ifp->if_capenable = ifr->ifr_reqcap;
		return (0);

	case SIOCSIFMTU:
		cvmx_mgmt_port_set_max_packet_size(sc->sc_port, ifr->ifr_mtu + ifp->if_hdrlen);
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
octm_rx_intr(void *arg)
{
	struct octm_softc *sc = arg;
	cvmx_mixx_isr_t mixx_isr;
	int len;

	mixx_isr.u64 = cvmx_read_csr(CVMX_MIXX_ISR(sc->sc_port));
	if (!mixx_isr.s.irthresh) {
		device_printf(sc->sc_dev, "stray interrupt.\n");
		return;
	}

	for (;;) {
		struct mbuf *m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			device_printf(sc->sc_dev, "no memory for receive mbuf.\n");
			return;
		}


		len = cvmx_mgmt_port_receive(sc->sc_port, MCLBYTES, m->m_data);
		if (len > 0) {
			m->m_pkthdr.rcvif = sc->sc_ifp;
			m->m_pkthdr.len = m->m_len = len;

			if_inc_counter(sc->sc_ifp, IFCOUNTER_IPACKETS, 1);

			(*sc->sc_ifp->if_input)(sc->sc_ifp, m);

			continue;
		}

		m_freem(m);

		if (len == 0)
			break;

		if_inc_counter(sc->sc_ifp, IFCOUNTER_IERRORS, 1);
	}

	/* Acknowledge interrupts.  */
	cvmx_write_csr(CVMX_MIXX_ISR(sc->sc_port), mixx_isr.u64);
	cvmx_read_csr(CVMX_MIXX_ISR(sc->sc_port));
}
