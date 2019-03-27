/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Juli Mallett <jmallett@FreeBSD.org>
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
 * Cavium Octeon Ethernet devices.
 *
 * XXX This file should be moved to if_octe.c
 * XXX The driver may have sufficient locking but we need locking to protect
 *     the interfaces presented here, right?
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

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "wrapper-cvmx-includes.h"
#include "cavium-ethernet.h"

#include "ethernet-common.h"
#include "ethernet-defines.h"
#include "ethernet-mdio.h"
#include "ethernet-tx.h"

#include "miibus_if.h"

#define	OCTE_TX_LOCK(priv)	mtx_lock(&(priv)->tx_mtx)
#define	OCTE_TX_UNLOCK(priv)	mtx_unlock(&(priv)->tx_mtx)

static int		octe_probe(device_t);
static int		octe_attach(device_t);
static int		octe_detach(device_t);
static int		octe_shutdown(device_t);

static int		octe_miibus_readreg(device_t, int, int);
static int		octe_miibus_writereg(device_t, int, int, int);

static void		octe_init(void *);
static void		octe_stop(void *);
static int		octe_transmit(struct ifnet *, struct mbuf *);

static int		octe_mii_medchange(struct ifnet *);
static void		octe_mii_medstat(struct ifnet *, struct ifmediareq *);

static int		octe_medchange(struct ifnet *);
static void		octe_medstat(struct ifnet *, struct ifmediareq *);

static int		octe_ioctl(struct ifnet *, u_long, caddr_t);

static device_method_t octe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		octe_probe),
	DEVMETHOD(device_attach,	octe_attach),
	DEVMETHOD(device_detach,	octe_detach),
	DEVMETHOD(device_shutdown,	octe_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	octe_miibus_readreg),
	DEVMETHOD(miibus_writereg,	octe_miibus_writereg),

	{ 0, 0 }
};

static driver_t octe_driver = {
	"octe",
	octe_methods,
	sizeof (cvm_oct_private_t),
};

static devclass_t octe_devclass;

DRIVER_MODULE(octe, octebus, octe_driver, octe_devclass, 0, 0);
DRIVER_MODULE(miibus, octe, miibus_driver, miibus_devclass, 0, 0);

static int
octe_probe(device_t dev)
{
	return (0);
}

static int
octe_attach(device_t dev)
{
	struct ifnet *ifp;
	cvm_oct_private_t *priv;
	device_t child;
	unsigned qos;
	int error;

	priv = device_get_softc(dev);
	ifp = priv->ifp;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	if (priv->phy_id != -1) {
		if (priv->phy_device == NULL) {
			error = mii_attach(dev, &priv->miibus, ifp,
			    octe_mii_medchange, octe_mii_medstat,
			    BMSR_DEFCAPMASK, priv->phy_id, MII_OFFSET_ANY, 0);
			if (error != 0)
				device_printf(dev, "attaching PHYs failed\n");
		} else {
			child = device_add_child(dev, priv->phy_device, -1);
			if (child == NULL)
				device_printf(dev, "missing phy %u device %s\n", priv->phy_id, priv->phy_device);
		}
	}

	if (priv->miibus == NULL) {
		ifmedia_init(&priv->media, 0, octe_medchange, octe_medstat);

		ifmedia_add(&priv->media, IFM_ETHER | IFM_AUTO, 0, NULL);
		ifmedia_set(&priv->media, IFM_ETHER | IFM_AUTO);
	}

	/*
	 * XXX
	 * We don't support programming the multicast filter right now, although it
	 * ought to be easy enough.  (Presumably it's just a matter of putting
	 * multicast addresses in the CAM?)
	 */
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_ALLMULTI;
	ifp->if_init = octe_init;
	ifp->if_ioctl = octe_ioctl;

	priv->if_flags = ifp->if_flags;

	mtx_init(&priv->tx_mtx, ifp->if_xname, "octe tx send queue", MTX_DEF);

	for (qos = 0; qos < 16; qos++) {
		mtx_init(&priv->tx_free_queue[qos].ifq_mtx, ifp->if_xname, "octe tx free queue", MTX_DEF);
		IFQ_SET_MAXLEN(&priv->tx_free_queue[qos], MAX_OUT_QUEUE_DEPTH);
	}

	ether_ifattach(ifp, priv->mac);

	ifp->if_transmit = octe_transmit;

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_HWCSUM;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP;

	OCTE_TX_LOCK(priv);
	IFQ_SET_MAXLEN(&ifp->if_snd, MAX_OUT_QUEUE_DEPTH);
	ifp->if_snd.ifq_drv_maxlen = MAX_OUT_QUEUE_DEPTH;
	IFQ_SET_READY(&ifp->if_snd);
	OCTE_TX_UNLOCK(priv);

	return (bus_generic_attach(dev));
}

static int
octe_detach(device_t dev)
{
	return (0);
}

static int
octe_shutdown(device_t dev)
{
	return (octe_detach(dev));
}

static int
octe_miibus_readreg(device_t dev, int phy, int reg)
{
	cvm_oct_private_t *priv;

	priv = device_get_softc(dev);

	/*
	 * Try interface-specific MII routine.
	 */
	if (priv->mdio_read != NULL)
		return (priv->mdio_read(priv->ifp, phy, reg));

	/*
	 * Try generic MII routine.
	 */
	KASSERT(phy == priv->phy_id,
	    ("read from phy %u but our phy is %u", phy, priv->phy_id));
	return (cvm_oct_mdio_read(priv->ifp, phy, reg));
}

static int
octe_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	cvm_oct_private_t *priv;

	priv = device_get_softc(dev);

	/*
	 * Try interface-specific MII routine.
	 */
	if (priv->mdio_write != NULL) {
		priv->mdio_write(priv->ifp, phy, reg, val);
		return (0);
	}

	/*
	 * Try generic MII routine.
	 */
	KASSERT(phy == priv->phy_id,
	    ("write to phy %u but our phy is %u", phy, priv->phy_id));
	cvm_oct_mdio_write(priv->ifp, phy, reg, val);

	return (0);
}

static void
octe_init(void *arg)
{
	struct ifnet *ifp;
	cvm_oct_private_t *priv;

	priv = arg;
	ifp = priv->ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		octe_stop(priv);

	if (priv->open != NULL)
		priv->open(ifp);

	if (((ifp->if_flags ^ priv->if_flags) & (IFF_ALLMULTI | IFF_MULTICAST | IFF_PROMISC)) != 0)
		cvm_oct_common_set_multicast_list(ifp);

	cvm_oct_common_set_mac_address(ifp, IF_LLADDR(ifp));

	cvm_oct_common_poll(ifp);

	if (priv->miibus != NULL)
		mii_mediachg(device_get_softc(priv->miibus));

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static void
octe_stop(void *arg)
{
	struct ifnet *ifp;
	cvm_oct_private_t *priv;

	priv = arg;
	ifp = priv->ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	if (priv->stop != NULL)
		priv->stop(ifp);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
}

static int
octe_transmit(struct ifnet *ifp, struct mbuf *m)
{
	cvm_oct_private_t *priv;

	priv = ifp->if_softc;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING) {
		m_freem(m);
		return (0);
	}

	return (cvm_oct_xmit(m, ifp));
}

static int
octe_mii_medchange(struct ifnet *ifp)
{
	cvm_oct_private_t *priv;
	struct mii_data *mii;
	struct mii_softc *miisc;

	priv = ifp->if_softc;
	mii = device_get_softc(priv->miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	mii_mediachg(mii);

	return (0);
}

static void
octe_mii_medstat(struct ifnet *ifp, struct ifmediareq *ifm)
{
	cvm_oct_private_t *priv;
	struct mii_data *mii;

	priv = ifp->if_softc;
	mii = device_get_softc(priv->miibus);

	mii_pollstat(mii);
	ifm->ifm_active = mii->mii_media_active;
	ifm->ifm_status = mii->mii_media_status;
}

static int
octe_medchange(struct ifnet *ifp)
{
	return (ENOTSUP);
}

static void
octe_medstat(struct ifnet *ifp, struct ifmediareq *ifm)
{
	cvm_oct_private_t *priv;
	cvmx_helper_link_info_t link_info;

	priv = ifp->if_softc;

	ifm->ifm_status = IFM_AVALID;
	ifm->ifm_active = IFT_ETHER;

	if (priv->poll == NULL)
		return;
	priv->poll(ifp);

	link_info.u64 = priv->link_info;

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
octe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	cvm_oct_private_t *priv;
	struct mii_data *mii;
	struct ifreq *ifr;
#ifdef INET
	struct ifaddr *ifa;
#endif
	int error;

	priv = ifp->if_softc;
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
				octe_init(priv);
			arp_ifinit(ifp, ifa);

			return (0);
		}
#endif
		error = ether_ioctl(ifp, cmd, data);
		if (error != 0)
			return (error);
		return (0);

	case SIOCSIFFLAGS:
		if (ifp->if_flags == priv->if_flags)
			return (0);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
				octe_init(priv);
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				octe_stop(priv);
		}
		priv->if_flags = ifp->if_flags;
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
		error = cvm_oct_common_change_mtu(ifp, ifr->ifr_mtu);
		if (error != 0)
			return (EINVAL);
		return (0);

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (priv->miibus != NULL) {
			mii = device_get_softc(priv->miibus);
			error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
			if (error != 0)
				return (error);
			return (0);
		}
		error = ifmedia_ioctl(ifp, ifr, &priv->media, cmd);
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
