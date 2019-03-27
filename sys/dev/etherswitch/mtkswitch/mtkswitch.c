/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#include <dev/etherswitch/mtkswitch/mtkswitchvar.h>

#include <dev/ofw/ofw_bus_subr.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

#define DEBUG

#if defined(DEBUG)
static SYSCTL_NODE(_debug, OID_AUTO, mtkswitch, CTLFLAG_RD, 0, "mtkswitch");
#endif

static inline int mtkswitch_portforphy(int phy);
static int mtkswitch_ifmedia_upd(struct ifnet *ifp);
static void mtkswitch_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr);
static void mtkswitch_tick(void *arg);

static const struct ofw_compat_data compat_data[] = {
	{ "ralink,rt3050-esw",		MTK_SWITCH_RT3050 },
	{ "ralink,rt3352-esw",		MTK_SWITCH_RT3352 },
	{ "ralink,rt5350-esw",		MTK_SWITCH_RT5350 },
	{ "mediatek,mt7620-gsw",	MTK_SWITCH_MT7620 },
	{ "mediatek,mt7621-gsw",	MTK_SWITCH_MT7621 },
	{ "mediatek,mt7628-esw",	MTK_SWITCH_MT7628 },

	/* Sentinel */
	{ NULL,				MTK_SWITCH_NONE }
};

static int
mtkswitch_probe(device_t dev)
{
	struct mtkswitch_softc *sc;
	mtk_switch_type switch_type;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	switch_type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (switch_type == MTK_SWITCH_NONE)
		return (ENXIO);

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));
	sc->sc_switchtype = switch_type;

	device_set_desc_copy(dev, "MTK Switch Driver");

	return (0);
}

static int
mtkswitch_attach_phys(struct mtkswitch_softc *sc)
{
	int phy, err = 0;
	char name[IFNAMSIZ];

	/* PHYs need an interface, so we generate a dummy one */
	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->sc_dev));
	for (phy = 0; phy < sc->numphys; phy++) {
		if ((sc->phymap & (1u << phy)) == 0) {
			sc->ifp[phy] = NULL;
			sc->ifname[phy] = NULL;
			sc->miibus[phy] = NULL;
			continue;
		}
		sc->ifp[phy] = if_alloc(IFT_ETHER);
		if (sc->ifp[phy] == NULL) {
			device_printf(sc->sc_dev, "couldn't allocate ifnet structure\n");
			err = ENOMEM;
			break;
		}

		sc->ifp[phy]->if_softc = sc;
		sc->ifp[phy]->if_flags |= IFF_UP | IFF_BROADCAST |
		    IFF_DRV_RUNNING | IFF_SIMPLEX;
		sc->ifname[phy] = malloc(strlen(name) + 1, M_DEVBUF, M_WAITOK);
		bcopy(name, sc->ifname[phy], strlen(name) + 1);
		if_initname(sc->ifp[phy], sc->ifname[phy],
		    mtkswitch_portforphy(phy));
		err = mii_attach(sc->sc_dev, &sc->miibus[phy], sc->ifp[phy],
		    mtkswitch_ifmedia_upd, mtkswitch_ifmedia_sts,
		    BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
		if (err != 0) {
			device_printf(sc->sc_dev,
			    "attaching PHY %d failed\n",
			    phy);
		} else {
			DPRINTF(sc->sc_dev, "%s attached to pseudo interface "
			    "%s\n", device_get_nameunit(sc->miibus[phy]),
			    sc->ifp[phy]->if_xname);
		}
	}
	return (err);
}

static int
mtkswitch_set_vlan_mode(struct mtkswitch_softc *sc, uint32_t mode)
{

	/* Check for invalid modes. */
	if ((mode & sc->info.es_vlan_caps) != mode)
		return (EINVAL);

	sc->vlan_mode = mode;

	/* Reset VLANs. */
	sc->hal.mtkswitch_vlan_init_hw(sc);

	return (0);
}

static int
mtkswitch_attach(device_t dev)
{
	struct mtkswitch_softc *sc;
	int err = 0;
	int port, rid;

	sc = device_get_softc(dev);

	/* sc->sc_switchtype is already decided in mtkswitch_probe() */
	sc->numports = MTKSWITCH_MAX_PORTS;
	sc->numphys = MTKSWITCH_MAX_PHYS;
	sc->cpuport = MTKSWITCH_CPU_PORT;
	sc->sc_dev = dev;

	/* Attach switch related functions */
	if (sc->sc_switchtype == MTK_SWITCH_NONE) {
		device_printf(dev, "Unknown switch type\n");
		return (ENXIO);
	}

	if (sc->sc_switchtype == MTK_SWITCH_MT7620 ||
	    sc->sc_switchtype == MTK_SWITCH_MT7621)
		mtk_attach_switch_mt7620(sc);
	else
		mtk_attach_switch_rt3050(sc);

	/* Allocate resources */
	rid = 0;
	sc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_res == NULL) {
		device_printf(dev, "could not map memory\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, "mtkswitch", NULL, MTX_DEF);

	/* Reset the switch */
	if (sc->hal.mtkswitch_reset(sc)) {
		DPRINTF(dev, "%s: mtkswitch_reset: failed\n", __func__);
		return (ENXIO);
	}

	err = sc->hal.mtkswitch_hw_setup(sc);
	DPRINTF(dev, "%s: hw_setup: err=%d\n", __func__, err);
	if (err != 0)
		return (err);

	err = sc->hal.mtkswitch_hw_global_setup(sc);
	DPRINTF(dev, "%s: hw_global_setup: err=%d\n", __func__, err);
	if (err != 0)
		return (err);

	/* Initialize the switch ports */
	for (port = 0; port < sc->numports; port++) {
		sc->hal.mtkswitch_port_init(sc, port);
	}

	/* Attach the PHYs and complete the bus enumeration */
	err = mtkswitch_attach_phys(sc);
	DPRINTF(dev, "%s: attach_phys: err=%d\n", __func__, err);
	if (err != 0)
		return (err);

	/* Default to ingress filters off. */
	err = mtkswitch_set_vlan_mode(sc, ETHERSWITCH_VLAN_DOT1Q);
	DPRINTF(dev, "%s: set_vlan_mode: err=%d\n", __func__, err);
	if (err != 0)
		return (err);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	DPRINTF(dev, "%s: bus_generic_attach: err=%d\n", __func__, err);
	if (err != 0)
		return (err);

	callout_init_mtx(&sc->callout_tick, &sc->sc_mtx, 0);

	MTKSWITCH_LOCK(sc);
	mtkswitch_tick(sc);
	MTKSWITCH_UNLOCK(sc);

	return (0);
}

static int
mtkswitch_detach(device_t dev)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	int phy;

	callout_drain(&sc->callout_tick);

	for (phy = 0; phy < MTKSWITCH_MAX_PHYS; phy++) {
		if (sc->miibus[phy] != NULL)
			device_delete_child(dev, sc->miibus[phy]);
		if (sc->ifp[phy] != NULL)
			if_free(sc->ifp[phy]);
		free(sc->ifname[phy], M_DEVBUF);
	}

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/* PHY <-> port mapping is currently 1:1 */
static inline int
mtkswitch_portforphy(int phy)
{

	return (phy);
}

static inline int
mtkswitch_phyforport(int port)
{

	return (port);
}

static inline struct mii_data *
mtkswitch_miiforport(struct mtkswitch_softc *sc, int port)
{
	int phy = mtkswitch_phyforport(port);

	if (phy < 0 || phy >= MTKSWITCH_MAX_PHYS || sc->miibus[phy] == NULL)
		return (NULL);

	return (device_get_softc(sc->miibus[phy]));
}

static inline struct ifnet *
mtkswitch_ifpforport(struct mtkswitch_softc *sc, int port)
{
	int phy = mtkswitch_phyforport(port);

	if (phy < 0 || phy >= MTKSWITCH_MAX_PHYS)
		return (NULL);

	return (sc->ifp[phy]);
}

/*
 * Convert port status to ifmedia.
 */
static void
mtkswitch_update_ifmedia(uint32_t portstatus, u_int *media_status,
    u_int *media_active)
{
	*media_active = IFM_ETHER;
	*media_status = IFM_AVALID;

	if ((portstatus & MTKSWITCH_LINK_UP) != 0)
		*media_status |= IFM_ACTIVE;
	else {
		*media_active |= IFM_NONE;
		return;
	}

	switch (portstatus & MTKSWITCH_SPEED_MASK) {
	case MTKSWITCH_SPEED_10:
		*media_active |= IFM_10_T;
		break;
	case MTKSWITCH_SPEED_100:
		*media_active |= IFM_100_TX;
		break;
	case MTKSWITCH_SPEED_1000:
		*media_active |= IFM_1000_T;
		break;
	}

	if ((portstatus & MTKSWITCH_DUPLEX) != 0)
		*media_active |= IFM_FDX;
	else
		*media_active |= IFM_HDX;

	if ((portstatus & MTKSWITCH_TXFLOW) != 0)
		*media_active |= IFM_ETH_TXPAUSE;
	if ((portstatus & MTKSWITCH_RXFLOW) != 0)
		*media_active |= IFM_ETH_RXPAUSE;
}

static void
mtkswitch_miipollstat(struct mtkswitch_softc *sc)
{
	struct mii_data *mii;
	struct mii_softc *miisc;
	uint32_t portstatus;
	int i, port_flap = 0;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	for (i = 0; i < sc->numphys; i++) {
		if (sc->miibus[i] == NULL)
			continue;
		mii = device_get_softc(sc->miibus[i]);
		portstatus = sc->hal.mtkswitch_get_port_status(sc,
		    mtkswitch_portforphy(i));

		/* If a port has flapped - mark it so we can flush the ATU */
		if (((mii->mii_media_status & IFM_ACTIVE) == 0 &&
		    (portstatus & MTKSWITCH_LINK_UP) != 0) ||
		    ((mii->mii_media_status & IFM_ACTIVE) != 0 &&
		    (portstatus & MTKSWITCH_LINK_UP) == 0)) {
			port_flap = 1;
		}

		mtkswitch_update_ifmedia(portstatus, &mii->mii_media_status,
		    &mii->mii_media_active);
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
			if (IFM_INST(mii->mii_media.ifm_cur->ifm_media) !=
			    miisc->mii_inst)
				continue;
			mii_phy_update(miisc, MII_POLLSTAT);
		}
	}

	if (port_flap)
		sc->hal.mtkswitch_atu_flush(sc);
}

static void
mtkswitch_tick(void *arg)
{
	struct mtkswitch_softc *sc = arg;

	mtkswitch_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, mtkswitch_tick, sc);
}

static void
mtkswitch_lock(device_t dev)
{
        struct mtkswitch_softc *sc = device_get_softc(dev);

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
}

static void
mtkswitch_unlock(device_t dev)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	MTKSWITCH_UNLOCK(sc);
}

static etherswitch_info_t *
mtkswitch_getinfo(device_t dev)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	return (&sc->info);
}

static inline int
mtkswitch_is_cpuport(struct mtkswitch_softc *sc, int port)
{

	return (sc->cpuport == port);
}

static int
mtkswitch_getport(device_t dev, etherswitch_port_t *p)
{       
	struct mtkswitch_softc *sc;
	struct mii_data *mii;
	struct ifmediareq *ifmr;
	int err;

	sc = device_get_softc(dev);
	if (p->es_port < 0 || p->es_port > sc->info.es_nports)
		return (ENXIO);

	err = sc->hal.mtkswitch_port_vlan_get(sc, p);
	if (err != 0)
		return (err);

	mii = mtkswitch_miiforport(sc, p->es_port);
	if (mtkswitch_is_cpuport(sc, p->es_port)) {
		/* fill in fixed values for CPU port */
		/* XXX is this valid in all cases? */
		p->es_flags |= ETHERSWITCH_PORT_CPU;
		ifmr = &p->es_ifmr;
		ifmr->ifm_count = 0;
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
		ifmr = &p->es_ifmr;
		ifmr->ifm_count = 0;
		ifmr->ifm_current = ifmr->ifm_active = IFM_NONE;
		ifmr->ifm_mask = 0;
		ifmr->ifm_status = 0;
	}
	return (0);
}

static int
mtkswitch_setport(device_t dev, etherswitch_port_t *p)
{       
	int err;
	struct mtkswitch_softc *sc;
	struct ifmedia *ifm;
	struct mii_data *mii;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	if (p->es_port < 0 || p->es_port > sc->info.es_nports)
		return (ENXIO);
        
	/* Port flags. */ 
	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		err = sc->hal.mtkswitch_port_vlan_setup(sc, p);
		if (err)
			return (err);
	}

	/* Do not allow media changes on CPU port. */
	if (mtkswitch_is_cpuport(sc, p->es_port))
		return (0);

	mii = mtkswitch_miiforport(sc, p->es_port);
	if (mii == NULL)
		return (ENXIO);

	ifp = mtkswitch_ifpforport(sc, p->es_port);

	ifm = &mii->mii_media;
	return (ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA));
}

static void
mtkswitch_statchg(device_t dev)
{

	DPRINTF(dev, "%s\n", __func__);
}

static int
mtkswitch_ifmedia_upd(struct ifnet *ifp)
{
	struct mtkswitch_softc *sc = ifp->if_softc;
	struct mii_data *mii = mtkswitch_miiforport(sc, ifp->if_dunit);
        
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);
	return (0);
}

static void
mtkswitch_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mtkswitch_softc *sc = ifp->if_softc;
	struct mii_data *mii = mtkswitch_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);

	if (mii == NULL)
		return;
	mii_pollstat(mii); 
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
mtkswitch_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct mtkswitch_softc *sc;

	sc = device_get_softc(dev);

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->vlan_mode;

	return (0);
}

static int
mtkswitch_setconf(device_t dev, etherswitch_conf_t *conf)
{
	struct mtkswitch_softc *sc;
	int err;
        
	sc = device_get_softc(dev);
        
	/* Set the VLAN mode. */
	if (conf->cmd & ETHERSWITCH_CONF_VLAN_MODE) {
		err = mtkswitch_set_vlan_mode(sc, conf->vlan_mode);
		if (err != 0)
			return (err);
	}

	return (0);
}

static int
mtkswitch_getvgroup(device_t dev, etherswitch_vlangroup_t *e)
{
        struct mtkswitch_softc *sc = device_get_softc(dev);

        return (sc->hal.mtkswitch_vlan_getvgroup(sc, e));
}

static int
mtkswitch_setvgroup(device_t dev, etherswitch_vlangroup_t *e)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.mtkswitch_vlan_setvgroup(sc, e));
}

static int
mtkswitch_readphy(device_t dev, int phy, int reg)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.mtkswitch_phy_read(dev, phy, reg));
}

static int
mtkswitch_writephy(device_t dev, int phy, int reg, int val)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.mtkswitch_phy_write(dev, phy, reg, val));
}

static int
mtkswitch_readreg(device_t dev, int addr)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.mtkswitch_reg_read(dev, addr));
}

static int
mtkswitch_writereg(device_t dev, int addr, int value)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.mtkswitch_reg_write(dev, addr, value));
}

static device_method_t mtkswitch_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mtkswitch_probe),
	DEVMETHOD(device_attach,	mtkswitch_attach),
	DEVMETHOD(device_detach,	mtkswitch_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	mtkswitch_readphy),
	DEVMETHOD(miibus_writereg,	mtkswitch_writephy),
	DEVMETHOD(miibus_statchg,	mtkswitch_statchg),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		mtkswitch_readphy),
	DEVMETHOD(mdio_writereg,	mtkswitch_writephy),

	/* ehterswitch interface */
	DEVMETHOD(etherswitch_lock,	mtkswitch_lock),
	DEVMETHOD(etherswitch_unlock,	mtkswitch_unlock),
	DEVMETHOD(etherswitch_getinfo,	mtkswitch_getinfo),
	DEVMETHOD(etherswitch_readreg,	mtkswitch_readreg),
	DEVMETHOD(etherswitch_writereg,	mtkswitch_writereg),
	DEVMETHOD(etherswitch_readphyreg,	mtkswitch_readphy),
	DEVMETHOD(etherswitch_writephyreg,	mtkswitch_writephy),
	DEVMETHOD(etherswitch_getport,	mtkswitch_getport),
	DEVMETHOD(etherswitch_setport,	mtkswitch_setport),
	DEVMETHOD(etherswitch_getvgroup,	mtkswitch_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	mtkswitch_setvgroup),
	DEVMETHOD(etherswitch_getconf,	mtkswitch_getconf),
	DEVMETHOD(etherswitch_setconf,	mtkswitch_setconf),

	DEVMETHOD_END
};

DEFINE_CLASS_0(mtkswitch, mtkswitch_driver, mtkswitch_methods,
    sizeof(struct mtkswitch_softc));
static devclass_t mtkswitch_devclass;

DRIVER_MODULE(mtkswitch, simplebus, mtkswitch_driver, mtkswitch_devclass, 0, 0);
DRIVER_MODULE(miibus, mtkswitch, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, mtkswitch, mdio_driver, mdio_devclass, 0, 0);
DRIVER_MODULE(etherswitch, mtkswitch, etherswitch_driver, etherswitch_devclass,
    0, 0);
MODULE_VERSION(mtkswitch, 1);
MODULE_DEPEND(mtkswitch, miibus, 1, 1, 1);
MODULE_DEPEND(mtkswitch, etherswitch, 1, 1, 1);
