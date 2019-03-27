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

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <machine/bus.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mdio/mdio.h>

#include <dev/etherswitch/etherswitch.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

MALLOC_DECLARE(M_UKSWITCH);
MALLOC_DEFINE(M_UKSWITCH, "ukswitch", "ukswitch data structures");

struct ukswitch_softc {
	struct mtx	sc_mtx;		/* serialize access to softc */
	device_t	sc_dev;
	int		media;		/* cpu port media */
	int		cpuport;	/* which PHY is connected to the CPU */
	int		phymask;	/* PHYs we manage */
	int		phyoffset;	/* PHYs register offset */
	int		numports;	/* number of ports */
	int		ifpport[MII_NPHY];
	int		*portphy;
	char		**ifname;
	device_t	**miibus;
	struct ifnet	**ifp;
	struct callout	callout_tick;
	etherswitch_info_t	info;
};

#define UKSWITCH_LOCK(_sc)			\
	    mtx_lock(&(_sc)->sc_mtx)
#define UKSWITCH_UNLOCK(_sc)			\
	    mtx_unlock(&(_sc)->sc_mtx)
#define UKSWITCH_LOCK_ASSERT(_sc, _what)	\
	    mtx_assert(&(_sc)->sc_mtx, (_what))
#define UKSWITCH_TRYLOCK(_sc)			\
	    mtx_trylock(&(_sc)->sc_mtx)

#if defined(DEBUG)
#define	DPRINTF(dev, args...) device_printf(dev, args)
#else
#define	DPRINTF(dev, args...)
#endif

static inline int ukswitch_portforphy(struct ukswitch_softc *, int);
static void ukswitch_tick(void *);
static int ukswitch_ifmedia_upd(struct ifnet *);
static void ukswitch_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int
ukswitch_probe(device_t dev)
{
	struct ukswitch_softc *sc;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));

	device_set_desc_copy(dev, "Generic MDIO switch driver");
	return (BUS_PROBE_DEFAULT);
}

static int
ukswitch_attach_phys(struct ukswitch_softc *sc)
{
	int phy, port = 0, err = 0;
	char name[IFNAMSIZ];

	/* PHYs need an interface, so we generate a dummy one */
	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->sc_dev));
	for (phy = 0; phy < MII_NPHY; phy++) {
		if (((1 << phy) & sc->phymask) == 0)
			continue;
		sc->ifpport[phy] = port;
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
		sc->ifname[port] = malloc(strlen(name)+1, M_UKSWITCH, M_WAITOK);
		bcopy(name, sc->ifname[port], strlen(name)+1);
		if_initname(sc->ifp[port], sc->ifname[port], port);
		sc->miibus[port] = malloc(sizeof(device_t), M_UKSWITCH,
		    M_WAITOK | M_ZERO);
		err = mii_attach(sc->sc_dev, sc->miibus[port], sc->ifp[port],
		    ukswitch_ifmedia_upd, ukswitch_ifmedia_sts, \
		    BMSR_DEFCAPMASK, phy + sc->phyoffset, MII_OFFSET_ANY, 0);
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
ukswitch_attach(device_t dev)
{
	struct ukswitch_softc *sc;
	int err = 0;

	sc = device_get_softc(dev);

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "ukswitch", NULL, MTX_DEF);
	strlcpy(sc->info.es_name, device_get_desc(dev),
	    sizeof(sc->info.es_name));

	/* XXX Defaults */
	sc->numports = 6;
	sc->phymask = 0x0f;
	sc->phyoffset = 0;
	sc->cpuport = -1;
	sc->media = 100;

	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "numports", &sc->numports);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "phymask", &sc->phymask);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "phyoffset", &sc->phyoffset);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "cpuport", &sc->cpuport);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "media", &sc->media);

	/* Support only fast and giga ethernet. */
	if (sc->media != 100 && sc->media != 1000)
		sc->media = 100;

	if (sc->cpuport != -1)
		/* Always attach the cpu port. */
		sc->phymask |= (1 << sc->cpuport);

	/* We do not support any vlan groups. */
	sc->info.es_nvlangroups = 0;

	sc->ifp = malloc(sizeof(struct ifnet *) * sc->numports, M_UKSWITCH,
	    M_WAITOK | M_ZERO);
	sc->ifname = malloc(sizeof(char *) * sc->numports, M_UKSWITCH,
	    M_WAITOK | M_ZERO);
	sc->miibus = malloc(sizeof(device_t *) * sc->numports, M_UKSWITCH,
	    M_WAITOK | M_ZERO);
	sc->portphy = malloc(sizeof(int) * sc->numports, M_UKSWITCH,
	    M_WAITOK | M_ZERO);

	/*
	 * Attach the PHYs and complete the bus enumeration.
	 */
	err = ukswitch_attach_phys(sc);
	if (err != 0)
		return (err);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0)
		return (err);
	
	callout_init(&sc->callout_tick, 0);

	ukswitch_tick(sc);
	
	return (err);
}

static int
ukswitch_detach(device_t dev)
{
	struct ukswitch_softc *sc = device_get_softc(dev);
	int i, port;

	callout_drain(&sc->callout_tick);

	for (i=0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = ukswitch_portforphy(sc, i);
		if (sc->miibus[port] != NULL)
			device_delete_child(dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		free(sc->ifname[port], M_UKSWITCH);
		free(sc->miibus[port], M_UKSWITCH);
	}

	free(sc->portphy, M_UKSWITCH);
	free(sc->miibus, M_UKSWITCH);
	free(sc->ifname, M_UKSWITCH);
	free(sc->ifp, M_UKSWITCH);

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/*
 * Convert PHY number to port number.
 */
static inline int
ukswitch_portforphy(struct ukswitch_softc *sc, int phy)
{

	return (sc->ifpport[phy]);
}

static inline struct mii_data *
ukswitch_miiforport(struct ukswitch_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (device_get_softc(*sc->miibus[port]));
}

static inline struct ifnet *
ukswitch_ifpforport(struct ukswitch_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (sc->ifp[port]);
}

/*
 * Poll the status for all PHYs.
 */
static void
ukswitch_miipollstat(struct ukswitch_softc *sc)
{
	int i, port;
	struct mii_data *mii;
	struct mii_softc *miisc;

	UKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);

	for (i = 0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = ukswitch_portforphy(sc, i);
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
ukswitch_tick(void *arg)
{
	struct ukswitch_softc *sc = arg;

	ukswitch_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, ukswitch_tick, sc);
}

static void
ukswitch_lock(device_t dev)
{
	struct ukswitch_softc *sc = device_get_softc(dev);

	UKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	UKSWITCH_LOCK(sc);
}

static void
ukswitch_unlock(device_t dev)
{
	struct ukswitch_softc *sc = device_get_softc(dev);

	UKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	UKSWITCH_UNLOCK(sc);
}

static etherswitch_info_t *
ukswitch_getinfo(device_t dev)
{
	struct ukswitch_softc *sc = device_get_softc(dev);
	
	return (&sc->info);
}

static int
ukswitch_getport(device_t dev, etherswitch_port_t *p)
{
	struct ukswitch_softc *sc = device_get_softc(dev);
	struct mii_data *mii;
	struct ifmediareq *ifmr = &p->es_ifmr;
	int err, phy;

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);
	p->es_pvid = 0;

	phy = sc->portphy[p->es_port];
	mii = ukswitch_miiforport(sc, p->es_port);
	if (sc->cpuport != -1 && phy == sc->cpuport) {
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
	} else if (mii != NULL) {
		err = ifmedia_ioctl(mii->mii_ifp, &p->es_ifr,
		    &mii->mii_media, SIOCGIFMEDIA);
		if (err)
			return (err);
	} else {
		return (ENXIO);
	}
	return (0);
}

static int
ukswitch_setport(device_t dev, etherswitch_port_t *p)
{
	struct ukswitch_softc *sc = device_get_softc(dev);
	struct ifmedia *ifm;
	struct mii_data *mii;
	struct ifnet *ifp;
	int err;

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	if (sc->portphy[p->es_port] == sc->cpuport)
		return (ENXIO);

	mii = ukswitch_miiforport(sc, p->es_port);
	if (mii == NULL)
		return (ENXIO);

	ifp = ukswitch_ifpforport(sc, p->es_port);

	ifm = &mii->mii_media;
	err = ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA);
	return (err);
}

static int
ukswitch_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{

	/* Not supported. */
	vg->es_vid = 0;
	vg->es_member_ports = 0;
	vg->es_untagged_ports = 0;
	vg->es_fid = 0;
	return (0);
}

static int
ukswitch_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{

	/* Not supported. */
	return (0);
}

static void
ukswitch_statchg(device_t dev)
{

	DPRINTF(dev, "%s\n", __func__);
}

static int
ukswitch_ifmedia_upd(struct ifnet *ifp)
{
	struct ukswitch_softc *sc = ifp->if_softc;
	struct mii_data *mii = ukswitch_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);
	return (0);
}

static void
ukswitch_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ukswitch_softc *sc = ifp->if_softc;
	struct mii_data *mii = ukswitch_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);

	if (mii == NULL)
		return;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
ukswitch_readphy(device_t dev, int phy, int reg)
{
	struct ukswitch_softc *sc;
	int data;

	sc = device_get_softc(dev);
	UKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	UKSWITCH_LOCK(sc);
	data = MDIO_READREG(device_get_parent(dev), phy, reg);
	UKSWITCH_UNLOCK(sc);

	return (data);
}

static int
ukswitch_writephy(device_t dev, int phy, int reg, int data)
{
	struct ukswitch_softc *sc;
	int err;

	sc = device_get_softc(dev);
	UKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	UKSWITCH_LOCK(sc);
	err = MDIO_WRITEREG(device_get_parent(dev), phy, reg, data);
	UKSWITCH_UNLOCK(sc);

	return (err);
}

static int
ukswitch_readreg(device_t dev, int addr)
{
	struct ukswitch_softc *sc;

	sc = device_get_softc(dev);
	UKSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	/* Not supported. */
	return (0);
}

static int
ukswitch_writereg(device_t dev, int addr, int value)
{
	struct ukswitch_softc *sc;

	sc = device_get_softc(dev);
	UKSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	/* Not supported. */
	return (0);
}

static device_method_t ukswitch_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ukswitch_probe),
	DEVMETHOD(device_attach,	ukswitch_attach),
	DEVMETHOD(device_detach,	ukswitch_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,	ukswitch_readphy),
	DEVMETHOD(miibus_writereg,	ukswitch_writephy),
	DEVMETHOD(miibus_statchg,	ukswitch_statchg),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		ukswitch_readphy),
	DEVMETHOD(mdio_writereg,	ukswitch_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_lock,	ukswitch_lock),
	DEVMETHOD(etherswitch_unlock,	ukswitch_unlock),
	DEVMETHOD(etherswitch_getinfo,	ukswitch_getinfo),
	DEVMETHOD(etherswitch_readreg,	ukswitch_readreg),
	DEVMETHOD(etherswitch_writereg,	ukswitch_writereg),
	DEVMETHOD(etherswitch_readphyreg,	ukswitch_readphy),
	DEVMETHOD(etherswitch_writephyreg,	ukswitch_writephy),
	DEVMETHOD(etherswitch_getport,	ukswitch_getport),
	DEVMETHOD(etherswitch_setport,	ukswitch_setport),
	DEVMETHOD(etherswitch_getvgroup,	ukswitch_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	ukswitch_setvgroup),

	DEVMETHOD_END
};

DEFINE_CLASS_0(ukswitch, ukswitch_driver, ukswitch_methods,
    sizeof(struct ukswitch_softc));
static devclass_t ukswitch_devclass;

DRIVER_MODULE(ukswitch, mdio, ukswitch_driver, ukswitch_devclass, 0, 0);
DRIVER_MODULE(miibus, ukswitch, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, ukswitch, mdio_driver, mdio_devclass, 0, 0);
DRIVER_MODULE(etherswitch, ukswitch, etherswitch_driver, etherswitch_devclass, 0, 0);
MODULE_VERSION(ukswitch, 1);
MODULE_DEPEND(ukswitch, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(ukswitch, etherswitch, 1, 1, 1); /* XXX which versions? */
