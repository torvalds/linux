/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Luiz Otavio O Souza.
 * Copyright (c) 2011-2012 Stefan Bethke.
 * Copyright (c) 2012 Adrian Chadd.
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

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/types.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <machine/bus.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mdio/mdio.h>

#include <dev/etherswitch/etherswitch.h>
#include <dev/etherswitch/ip17x/ip17x_phy.h>
#include <dev/etherswitch/ip17x/ip17x_reg.h>
#include <dev/etherswitch/ip17x/ip17x_var.h>
#include <dev/etherswitch/ip17x/ip17x_vlans.h>
#include <dev/etherswitch/ip17x/ip175c.h>
#include <dev/etherswitch/ip17x/ip175d.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

MALLOC_DECLARE(M_IP17X);
MALLOC_DEFINE(M_IP17X, "ip17x", "ip17x data structures");

static void ip17x_tick(void *);
static int ip17x_ifmedia_upd(struct ifnet *);
static void ip17x_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void
ip17x_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, "ip17x", -1) == NULL)
	    BUS_ADD_CHILD(parent, 0, "ip17x", -1);
}

static int
ip17x_probe(device_t dev)
{
	struct ip17x_softc *sc;
	uint32_t oui, model, phy_id1, phy_id2;
#ifdef FDT
	phandle_t ip17x_node;
	pcell_t cell;

	ip17x_node = fdt_find_compatible(OF_finddevice("/"),
	    "icplus,ip17x", 0);

	if (ip17x_node == 0)
		return (ENXIO);
#endif

	sc = device_get_softc(dev);

	/* Read ID from PHY 0. */
	phy_id1 = MDIO_READREG(device_get_parent(dev), 0, MII_PHYIDR1);
	phy_id2 = MDIO_READREG(device_get_parent(dev), 0, MII_PHYIDR2);

	oui = MII_OUI(phy_id1, phy_id2);
	model = MII_MODEL(phy_id2);
	/* We only care about IC+ devices. */
	if (oui != IP17X_OUI) {
		device_printf(dev,
		    "Unsupported IC+ switch. Unknown OUI: %#x\n", oui);
		return (ENXIO);
	}

	switch (model) {
	case IP17X_IP175A:
		sc->sc_switchtype = IP17X_SWITCH_IP175A;
		break;
	case IP17X_IP175C:
		sc->sc_switchtype = IP17X_SWITCH_IP175C;
		break;
	default:
		device_printf(dev, "Unsupported IC+ switch model: %#x\n",
		    model);
		return (ENXIO);
	}

	/* IP175D has a specific ID register. */
	model = MDIO_READREG(device_get_parent(dev), IP175D_ID_PHY,
	    IP175D_ID_REG);
	if (model == 0x175d)
		sc->sc_switchtype = IP17X_SWITCH_IP175D;
	else {
		/* IP178 has more PHYs.  Try it. */
		model = MDIO_READREG(device_get_parent(dev), 5, MII_PHYIDR1);
		if (phy_id1 == model)
			sc->sc_switchtype = IP17X_SWITCH_IP178C;
	}

	sc->miipoll = 1;
#ifdef FDT
	if ((OF_getencprop(ip17x_node, "mii-poll",
	    &cell, sizeof(cell))) > 0)
		sc->miipoll = cell ? 1 : 0;
#else
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "mii-poll", &sc->miipoll);
#endif
	device_set_desc_copy(dev, "IC+ IP17x switch driver");
	return (BUS_PROBE_DEFAULT);
}

static int
ip17x_attach_phys(struct ip17x_softc *sc)
{
	int err, phy, port;
	char name[IFNAMSIZ];

	port = err = 0;

	/* PHYs need an interface, so we generate a dummy one */
	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->sc_dev));
	for (phy = 0; phy < MII_NPHY; phy++) {
		if (((1 << phy) & sc->phymask) == 0)
			continue;
		sc->phyport[phy] = port;
		sc->portphy[port] = phy;
		sc->ifp[port] = if_alloc(IFT_ETHER);
		if (sc->ifp[port] == NULL) {
			device_printf(sc->sc_dev, "couldn't allocate ifnet structure\n");
			err = ENOMEM;
			break;
		}

		sc->ifp[port]->if_softc = sc;
		sc->ifp[port]->if_flags |= IFF_UP | IFF_BROADCAST |
		    IFF_DRV_RUNNING | IFF_SIMPLEX;
		if_initname(sc->ifp[port], name, port);
		sc->miibus[port] = malloc(sizeof(device_t), M_IP17X,
		    M_WAITOK | M_ZERO);
		err = mii_attach(sc->sc_dev, sc->miibus[port], sc->ifp[port],
		    ip17x_ifmedia_upd, ip17x_ifmedia_sts, \
		    BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
		DPRINTF(sc->sc_dev, "%s attached to pseudo interface %s\n",
		    device_get_nameunit(*sc->miibus[port]),
		    sc->ifp[port]->if_xname);
		if (err != 0) {
			device_printf(sc->sc_dev,
			    "attaching PHY %d failed\n",
			    phy);
			break;
		}
		sc->info.es_nports = port + 1;
		if (++port >= sc->numports)
			break;
	}
	return (err);
}

static int
ip17x_attach(device_t dev)
{
	struct ip17x_softc *sc;
	int err;

	sc = device_get_softc(dev);

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "ip17x", NULL, MTX_DEF);
	strlcpy(sc->info.es_name, device_get_desc(dev),
	    sizeof(sc->info.es_name));

	/* XXX Defaults */
	sc->phymask = 0x0f;
	sc->media = 100;

	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "phymask", &sc->phymask);

	/* Number of vlans supported by the switch. */
	sc->info.es_nvlangroups = IP17X_MAX_VLANS;

	/* Attach the switch related functions. */
	if (IP17X_IS_SWITCH(sc, IP175C))
		ip175c_attach(sc);
	else if (IP17X_IS_SWITCH(sc, IP175D))
		ip175d_attach(sc);
	else
		/* We don't have support to all the models yet :-/ */
		return (ENXIO);

	/* Always attach the cpu port. */
	sc->phymask |= (1 << sc->cpuport);

	sc->ifp = malloc(sizeof(struct ifnet *) * sc->numports, M_IP17X,
	    M_WAITOK | M_ZERO);
	sc->pvid = malloc(sizeof(uint32_t) * sc->numports, M_IP17X,
	    M_WAITOK | M_ZERO);
	sc->miibus = malloc(sizeof(device_t *) * sc->numports, M_IP17X,
	    M_WAITOK | M_ZERO);
	sc->portphy = malloc(sizeof(int) * sc->numports, M_IP17X,
	    M_WAITOK | M_ZERO);

	/* Initialize the switch. */
	sc->hal.ip17x_reset(sc);

	/*
	 * Attach the PHYs and complete the bus enumeration.
	 */
	err = ip17x_attach_phys(sc);
	if (err != 0)
		return (err);

	/*
	 * Set the switch to port based vlans or disabled (if not supported
	 * on this model).
	 */
	sc->hal.ip17x_set_vlan_mode(sc, ETHERSWITCH_VLAN_PORT);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0)
		return (err);
	
	if (sc->miipoll) {
		callout_init(&sc->callout_tick, 0);

		ip17x_tick(sc);
	}
	
	return (0);
}

static int
ip17x_detach(device_t dev)
{
	struct ip17x_softc *sc;
	int i, port;

	sc = device_get_softc(dev);
	if (sc->miipoll)
		callout_drain(&sc->callout_tick);

	for (i=0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = sc->phyport[i];
		if (sc->miibus[port] != NULL)
			device_delete_child(dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		free(sc->miibus[port], M_IP17X);
	}

	free(sc->portphy, M_IP17X);
	free(sc->miibus, M_IP17X);
	free(sc->pvid, M_IP17X);
	free(sc->ifp, M_IP17X);

	/* Reset the switch. */
	sc->hal.ip17x_reset(sc);

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static inline struct mii_data *
ip17x_miiforport(struct ip17x_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (device_get_softc(*sc->miibus[port]));
}

static inline struct ifnet *
ip17x_ifpforport(struct ip17x_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (sc->ifp[port]);
}

/*
 * Poll the status for all PHYs.
 */
static void
ip17x_miipollstat(struct ip17x_softc *sc)
{
	struct mii_softc *miisc;
	struct mii_data *mii;
	int i, port;

	IP17X_LOCK_ASSERT(sc, MA_NOTOWNED);

	for (i = 0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = sc->phyport[i];
		if ((*sc->miibus[port]) == NULL)
			continue;
		mii = device_get_softc(*sc->miibus[port]);
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
			if (IFM_INST(mii->mii_media.ifm_cur->ifm_media) !=
			    miisc->mii_inst)
				continue;
			ukphy_status(miisc);
			mii_phy_update(miisc, MII_POLLSTAT);
		}
	}
}

static void
ip17x_tick(void *arg)
{
	struct ip17x_softc *sc;

	sc = arg;
	ip17x_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, ip17x_tick, sc);
}

static void
ip17x_lock(device_t dev)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);
	IP17X_LOCK_ASSERT(sc, MA_NOTOWNED);
	IP17X_LOCK(sc);
}

static void
ip17x_unlock(device_t dev)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);
	IP17X_LOCK_ASSERT(sc, MA_OWNED);
	IP17X_UNLOCK(sc);
}

static etherswitch_info_t *
ip17x_getinfo(device_t dev)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);
	return (&sc->info);
}

static int
ip17x_getport(device_t dev, etherswitch_port_t *p)
{
	struct ip17x_softc *sc;
	struct ifmediareq *ifmr;
	struct mii_data *mii;
	int err, phy;

	sc = device_get_softc(dev);
	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	phy = sc->portphy[p->es_port];

	/* Retrieve the PVID. */
	p->es_pvid = sc->pvid[phy];

	/* Port flags. */
	if (sc->addtag & (1 << phy))
		p->es_flags |= ETHERSWITCH_PORT_ADDTAG;
	if (sc->striptag & (1 << phy))
		p->es_flags |= ETHERSWITCH_PORT_STRIPTAG;

	ifmr = &p->es_ifmr;

	/* No media settings ? */
	if (p->es_ifmr.ifm_count == 0)
		return (0);

	mii = ip17x_miiforport(sc, p->es_port);
	if (mii == NULL)
		return (ENXIO);
	if (phy == sc->cpuport) {
		/* fill in fixed values for CPU port */
		p->es_flags |= ETHERSWITCH_PORT_CPU;
		ifmr->ifm_count = 0;
		if (sc->media == 100)
			ifmr->ifm_current = ifmr->ifm_active =
			    IFM_ETHER | IFM_100_TX | IFM_FDX;
		else
			ifmr->ifm_current = ifmr->ifm_active =
			    IFM_ETHER | IFM_1000_T | IFM_FDX;
		ifmr->ifm_mask = 0;
		ifmr->ifm_status = IFM_ACTIVE | IFM_AVALID;
	} else {
		err = ifmedia_ioctl(mii->mii_ifp, &p->es_ifr,
		    &mii->mii_media, SIOCGIFMEDIA);
		if (err)
			return (err);
	}
	return (0);
}

static int
ip17x_setport(device_t dev, etherswitch_port_t *p)
{
	struct ip17x_softc *sc;
	struct ifmedia *ifm;
	struct ifnet *ifp;
	struct mii_data *mii;
	int phy;

 	sc = device_get_softc(dev);
	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	phy = sc->portphy[p->es_port];
	ifp = ip17x_ifpforport(sc, p->es_port);
	mii = ip17x_miiforport(sc, p->es_port);
	if (ifp == NULL || mii == NULL)
		return (ENXIO);

	/* Port flags. */
	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {

		/* Set the PVID. */
		if (p->es_pvid != 0) {
			if (IP17X_IS_SWITCH(sc, IP175C) &&
			    p->es_pvid > IP175C_LAST_VLAN)
				return (ENXIO);
			sc->pvid[phy] = p->es_pvid;
		}

		/* Mutually exclusive. */
		if (p->es_flags & ETHERSWITCH_PORT_ADDTAG &&
		    p->es_flags & ETHERSWITCH_PORT_STRIPTAG)
			return (EINVAL);

		/* Reset the settings for this port. */
		sc->addtag &= ~(1 << phy);
		sc->striptag &= ~(1 << phy);

		/* And then set it to the new value. */
		if (p->es_flags & ETHERSWITCH_PORT_ADDTAG)
			sc->addtag |= (1 << phy);
		if (p->es_flags & ETHERSWITCH_PORT_STRIPTAG)
			sc->striptag |= (1 << phy);
	}

	/* Update the switch configuration. */
	if (sc->hal.ip17x_hw_setup(sc))
		return (ENXIO);

	/* Do not allow media changes on CPU port. */
	if (phy == sc->cpuport)
		return (0);

	/* No media settings ? */
	if (p->es_ifmr.ifm_count == 0)
		return (0);

	ifm = &mii->mii_media;
	return (ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA));
}

static void
ip17x_statchg(device_t dev)
{

	DPRINTF(dev, "%s\n", __func__);
}

static int
ip17x_ifmedia_upd(struct ifnet *ifp)
{
	struct ip17x_softc *sc;
	struct mii_data *mii;

 	sc = ifp->if_softc;
	DPRINTF(sc->sc_dev, "%s\n", __func__);
 	mii = ip17x_miiforport(sc, ifp->if_dunit);
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);

	return (0);
}

static void
ip17x_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ip17x_softc *sc;
	struct mii_data *mii;

 	sc = ifp->if_softc;
	DPRINTF(sc->sc_dev, "%s\n", __func__);
	mii = ip17x_miiforport(sc, ifp->if_dunit);
	if (mii == NULL)
		return;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
ip17x_readreg(device_t dev, int addr)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);
	IP17X_LOCK_ASSERT(sc, MA_OWNED);

	/* Not supported. */
	return (0);
}

static int
ip17x_writereg(device_t dev, int addr, int value)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);
	IP17X_LOCK_ASSERT(sc, MA_OWNED);

	/* Not supported. */
	return (0);
}

static int
ip17x_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->hal.ip17x_get_vlan_mode(sc);

	return (0);
}

static int
ip17x_setconf(device_t dev, etherswitch_conf_t *conf)
{
	struct ip17x_softc *sc;

	sc = device_get_softc(dev);

	/* Set the VLAN mode. */
	if (conf->cmd & ETHERSWITCH_CONF_VLAN_MODE)
		sc->hal.ip17x_set_vlan_mode(sc, conf->vlan_mode);

	return (0);
}

static device_method_t ip17x_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	ip17x_identify),
	DEVMETHOD(device_probe,		ip17x_probe),
	DEVMETHOD(device_attach,	ip17x_attach),
	DEVMETHOD(device_detach,	ip17x_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,	ip17x_readphy),
	DEVMETHOD(miibus_writereg,	ip17x_writephy),
	DEVMETHOD(miibus_statchg,	ip17x_statchg),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		ip17x_readphy),
	DEVMETHOD(mdio_writereg,	ip17x_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_lock,	ip17x_lock),
	DEVMETHOD(etherswitch_unlock,	ip17x_unlock),
	DEVMETHOD(etherswitch_getinfo,	ip17x_getinfo),
	DEVMETHOD(etherswitch_readreg,	ip17x_readreg),
	DEVMETHOD(etherswitch_writereg,	ip17x_writereg),
	DEVMETHOD(etherswitch_readphyreg,	ip17x_readphy),
	DEVMETHOD(etherswitch_writephyreg,	ip17x_writephy),
	DEVMETHOD(etherswitch_getport,	ip17x_getport),
	DEVMETHOD(etherswitch_setport,	ip17x_setport),
	DEVMETHOD(etherswitch_getvgroup,	ip17x_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	ip17x_setvgroup),
	DEVMETHOD(etherswitch_getconf,	ip17x_getconf),
	DEVMETHOD(etherswitch_setconf,	ip17x_setconf),

	DEVMETHOD_END
};

DEFINE_CLASS_0(ip17x, ip17x_driver, ip17x_methods,
    sizeof(struct ip17x_softc));
static devclass_t ip17x_devclass;

DRIVER_MODULE(ip17x, mdio, ip17x_driver, ip17x_devclass, 0, 0);
DRIVER_MODULE(miibus, ip17x, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(etherswitch, ip17x, etherswitch_driver, etherswitch_devclass, 0, 0);
MODULE_VERSION(ip17x, 1);

#ifdef FDT
MODULE_DEPEND(ip17x, mdio, 1, 1, 1); /* XXX which versions? */
#else
DRIVER_MODULE(mdio, ip17x, mdio_driver, mdio_devclass, 0, 0);
MODULE_DEPEND(ip17x, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(ip17x, etherswitch, 1, 1, 1); /* XXX which versions? */
#endif
