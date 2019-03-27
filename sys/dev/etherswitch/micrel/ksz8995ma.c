/*-
 * Copyright (c) 2016 Hiroki Mori
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

/*
 * This is Micrel KSZ8995MA driver code. KSZ8995MA use SPI bus on control.
 * This code development on @SRCHACK's ksz8995ma board and FON2100 with
 * gpiospi.
 * etherswitchcfg command port option support addtag, ingress, striptag, 
 * dropuntagged.
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

#include <dev/etherswitch/etherswitch.h>

#include <dev/spibus/spi.h>

#include "spibus_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

#define	KSZ8995MA_SPI_READ		0x03
#define	KSZ8995MA_SPI_WRITE		0x02

#define	KSZ8995MA_CID0			0x00
#define	KSZ8995MA_CID1			0x01

#define	KSZ8995MA_GC0			0x02
#define	KSZ8995MA_GC1			0x03
#define	KSZ8995MA_GC2			0x04
#define	KSZ8995MA_GC3			0x05

#define	KSZ8995MA_PORT_SIZE		0x10

#define	KSZ8995MA_PC0_BASE		0x10
#define	KSZ8995MA_PC1_BASE		0x11
#define	KSZ8995MA_PC2_BASE		0x12
#define	KSZ8995MA_PC3_BASE		0x13
#define	KSZ8995MA_PC4_BASE		0x14
#define	KSZ8995MA_PC5_BASE		0x15
#define	KSZ8995MA_PC6_BASE		0x16
#define	KSZ8995MA_PC7_BASE		0x17
#define	KSZ8995MA_PC8_BASE		0x18
#define	KSZ8995MA_PC9_BASE		0x19
#define	KSZ8995MA_PC10_BASE		0x1a
#define	KSZ8995MA_PC11_BASE		0x1b
#define	KSZ8995MA_PC12_BASE		0x1c
#define	KSZ8995MA_PC13_BASE		0x1d

#define	KSZ8995MA_PS0_BASE		0x1e

#define	KSZ8995MA_PC14_BASE		0x1f

#define	KSZ8995MA_IAC0			0x6e
#define	KSZ8995MA_IAC1			0x6f
#define	KSZ8995MA_IDR8			0x70
#define	KSZ8995MA_IDR7			0x71
#define	KSZ8995MA_IDR6			0x72
#define	KSZ8995MA_IDR5			0x73
#define	KSZ8995MA_IDR4			0x74
#define	KSZ8995MA_IDR3			0x75
#define	KSZ8995MA_IDR2			0x76
#define	KSZ8995MA_IDR1			0x77
#define	KSZ8995MA_IDR0			0x78

#define	KSZ8995MA_FAMILI_ID		0x95
#define	KSZ8995MA_CHIP_ID		0x00
#define	KSZ8995MA_CHIP_ID_MASK		0xf0
#define	KSZ8995MA_START			0x01
#define	KSZ8995MA_VLAN_ENABLE		0x80
#define	KSZ8995MA_TAG_INS		0x04
#define	KSZ8995MA_TAG_RM		0x02
#define	KSZ8995MA_INGR_FILT		0x40
#define	KSZ8995MA_DROP_NONPVID		0x20

#define	KSZ8995MA_PDOWN			0x08
#define	KSZ8995MA_STARTNEG		0x20

#define	KSZ8995MA_MII_STAT		0x7808
#define	KSZ8995MA_MII_PHYID_H		0x0022
#define	KSZ8995MA_MII_PHYID_L		0x1450
#define	KSZ8995MA_MII_AA		0x0401

#define	KSZ8995MA_VLAN_TABLE_VALID	0x20
#define	KSZ8995MA_VLAN_TABLE_READ	0x14
#define	KSZ8995MA_VLAN_TABLE_WRITE	0x04

#define	KSZ8995MA_MAX_PORT		5

MALLOC_DECLARE(M_KSZ8995MA);
MALLOC_DEFINE(M_KSZ8995MA, "ksz8995ma", "ksz8995ma data structures");

struct ksz8995ma_softc {
	struct mtx	sc_mtx;		/* serialize access to softc */
	device_t	sc_dev;
	int		vlan_mode;
	int		media;		/* cpu port media */
	int		cpuport;	/* which PHY is connected to the CPU */
	int		phymask;	/* PHYs we manage */
	int		numports;	/* number of ports */
	int		ifpport[KSZ8995MA_MAX_PORT];
	int		*portphy;
	char		**ifname;
	device_t	**miibus;
	struct ifnet	**ifp;
	struct callout	callout_tick;
	etherswitch_info_t	info;
};

#define	KSZ8995MA_LOCK(_sc)			\
	    mtx_lock(&(_sc)->sc_mtx)
#define	KSZ8995MA_UNLOCK(_sc)			\
	    mtx_unlock(&(_sc)->sc_mtx)
#define	KSZ8995MA_LOCK_ASSERT(_sc, _what)	\
	    mtx_assert(&(_sc)->sc_mtx, (_what))
#define	KSZ8995MA_TRYLOCK(_sc)			\
	    mtx_trylock(&(_sc)->sc_mtx)

#if defined(DEBUG)
#define	DPRINTF(dev, args...) device_printf(dev, args)
#else
#define	DPRINTF(dev, args...)
#endif

static inline int ksz8995ma_portforphy(struct ksz8995ma_softc *, int);
static void ksz8995ma_tick(void *);
static int ksz8995ma_ifmedia_upd(struct ifnet *);
static void ksz8995ma_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int ksz8995ma_readreg(device_t dev, int addr);
static int ksz8995ma_writereg(device_t dev, int addr, int value);
static void ksz8995ma_portvlanreset(device_t dev);

static int
ksz8995ma_probe(device_t dev)
{
	int id0, id1;
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));

	id0 = ksz8995ma_readreg(dev, KSZ8995MA_CID0);
	id1 = ksz8995ma_readreg(dev, KSZ8995MA_CID1);
	if (bootverbose)
		device_printf(dev,"Chip Identifier Register %x %x\n", id0, id1);

	/* check Product Code */
	if (id0 != KSZ8995MA_FAMILI_ID || (id1 & KSZ8995MA_CHIP_ID_MASK) !=
	    KSZ8995MA_CHIP_ID) {
		return (ENXIO);
	}

	device_set_desc_copy(dev, "Micrel KSZ8995MA SPI switch driver");
	return (BUS_PROBE_DEFAULT);
}

static int
ksz8995ma_attach_phys(struct ksz8995ma_softc *sc)
{
	int phy, port, err;
	char name[IFNAMSIZ];

	port = 0;
	err = 0;
	/* PHYs need an interface, so we generate a dummy one */
	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->sc_dev));
	for (phy = 0; phy < sc->numports; phy++) {
		if (phy == sc->cpuport)
			continue;
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
		if_initname(sc->ifp[port], name, port);
		sc->miibus[port] = malloc(sizeof(device_t), M_KSZ8995MA,
		    M_WAITOK | M_ZERO);
		if (sc->miibus[port] == NULL) {
			err = ENOMEM;
			goto failed;
		}
		err = mii_attach(sc->sc_dev, sc->miibus[port], sc->ifp[port],
		    ksz8995ma_ifmedia_upd, ksz8995ma_ifmedia_sts, \
		    BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
		DPRINTF(sc->sc_dev, "%s attached to pseudo interface %s\n",
		    device_get_nameunit(*sc->miibus[port]),
		    sc->ifp[port]->if_xname);
		if (err != 0) {
			device_printf(sc->sc_dev,
			    "attaching PHY %d failed\n",
			    phy);
			goto failed;
		}
		++port;
	}
	sc->info.es_nports = port;
	if (sc->cpuport != -1) {
		/* cpu port is MAC5 on ksz8995ma */ 
		sc->ifpport[sc->cpuport] = port;
		sc->portphy[port] = sc->cpuport;
		++sc->info.es_nports;
	}

	return (0);

failed:
	for (phy = 0; phy < sc->numports; phy++) {
		if (((1 << phy) & sc->phymask) == 0)
			continue;
		port = ksz8995ma_portforphy(sc, phy);
		if (sc->miibus[port] != NULL)
			device_delete_child(sc->sc_dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		if (sc->ifname[port] != NULL)
			free(sc->ifname[port], M_KSZ8995MA);
		if (sc->miibus[port] != NULL)
			free(sc->miibus[port], M_KSZ8995MA);
	}
	return (err);
}

static int
ksz8995ma_attach(device_t dev)
{
	struct ksz8995ma_softc	*sc;
	int			 err, reg;

	err = 0;
	sc = device_get_softc(dev);

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "ksz8995ma", NULL, MTX_DEF);
	strlcpy(sc->info.es_name, device_get_desc(dev),
	    sizeof(sc->info.es_name));

	/* KSZ8995MA Defaults */
	sc->numports = KSZ8995MA_MAX_PORT;
	sc->phymask = (1 << (KSZ8995MA_MAX_PORT + 1)) - 1;
	sc->cpuport = -1;
	sc->media = 100;

	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "cpuport", &sc->cpuport);

	sc->info.es_nvlangroups = 16;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_PORT | ETHERSWITCH_VLAN_DOT1Q;

	sc->ifp = malloc(sizeof(struct ifnet *) * sc->numports, M_KSZ8995MA,
	    M_WAITOK | M_ZERO);
	sc->ifname = malloc(sizeof(char *) * sc->numports, M_KSZ8995MA,
	    M_WAITOK | M_ZERO);
	sc->miibus = malloc(sizeof(device_t *) * sc->numports, M_KSZ8995MA,
	    M_WAITOK | M_ZERO);
	sc->portphy = malloc(sizeof(int) * sc->numports, M_KSZ8995MA,
	    M_WAITOK | M_ZERO);

	if (sc->ifp == NULL || sc->ifname == NULL || sc->miibus == NULL ||
	    sc->portphy == NULL) {
		err = ENOMEM;
		goto failed;
	}

	/*
	 * Attach the PHYs and complete the bus enumeration.
	 */
	err = ksz8995ma_attach_phys(sc);
	if (err != 0)
		goto failed;

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0)
		goto failed;
	
	callout_init(&sc->callout_tick, 0);

	ksz8995ma_tick(sc);
	
	/* start switch */
	sc->vlan_mode = 0;
	reg = ksz8995ma_readreg(dev, KSZ8995MA_GC3);
	ksz8995ma_writereg(dev, KSZ8995MA_GC3, 
	    reg & ~KSZ8995MA_VLAN_ENABLE);
	ksz8995ma_portvlanreset(dev);
	ksz8995ma_writereg(dev, KSZ8995MA_CID1, KSZ8995MA_START);

	return (0);

failed:
	if (sc->portphy != NULL)
		free(sc->portphy, M_KSZ8995MA);
	if (sc->miibus != NULL)
		free(sc->miibus, M_KSZ8995MA);
	if (sc->ifname != NULL)
		free(sc->ifname, M_KSZ8995MA);
	if (sc->ifp != NULL)
		free(sc->ifp, M_KSZ8995MA);

	return (err);
}

static int
ksz8995ma_detach(device_t dev)
{
	struct ksz8995ma_softc	*sc;
	int			 i, port;

	sc = device_get_softc(dev);

	callout_drain(&sc->callout_tick);

	for (i = 0; i < KSZ8995MA_MAX_PORT; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = ksz8995ma_portforphy(sc, i);
		if (sc->miibus[port] != NULL)
			device_delete_child(dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		free(sc->ifname[port], M_KSZ8995MA);
		free(sc->miibus[port], M_KSZ8995MA);
	}

	free(sc->portphy, M_KSZ8995MA);
	free(sc->miibus, M_KSZ8995MA);
	free(sc->ifname, M_KSZ8995MA);
	free(sc->ifp, M_KSZ8995MA);

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/*
 * Convert PHY number to port number.
 */
static inline int
ksz8995ma_portforphy(struct ksz8995ma_softc *sc, int phy)
{

	return (sc->ifpport[phy]);
}

static inline struct mii_data *
ksz8995ma_miiforport(struct ksz8995ma_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	if (port == sc->cpuport)
		return (NULL);
	return (device_get_softc(*sc->miibus[port]));
}

static inline struct ifnet *
ksz8995ma_ifpforport(struct ksz8995ma_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (sc->ifp[port]);
}

/*
 * Poll the status for all PHYs.
 */
static void
ksz8995ma_miipollstat(struct ksz8995ma_softc *sc)
{
	int i, port;
	struct mii_data *mii;
	struct mii_softc *miisc;

	KSZ8995MA_LOCK_ASSERT(sc, MA_NOTOWNED);

	for (i = 0; i < KSZ8995MA_MAX_PORT; i++) {
		if (i == sc->cpuport)
			continue;
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = ksz8995ma_portforphy(sc, i);
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
ksz8995ma_tick(void *arg)
{
	struct ksz8995ma_softc *sc;

	sc = arg;

	ksz8995ma_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, ksz8995ma_tick, sc);
}

static void
ksz8995ma_lock(device_t dev)
{
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);

	KSZ8995MA_LOCK_ASSERT(sc, MA_NOTOWNED);
	KSZ8995MA_LOCK(sc);
}

static void
ksz8995ma_unlock(device_t dev)
{
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);

	KSZ8995MA_LOCK_ASSERT(sc, MA_OWNED);
	KSZ8995MA_UNLOCK(sc);
}

static etherswitch_info_t *
ksz8995ma_getinfo(device_t dev)
{
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);
	
	return (&sc->info);
}

static int
ksz8995ma_getport(device_t dev, etherswitch_port_t *p)
{
	struct ksz8995ma_softc *sc;
	struct mii_data *mii;
	struct ifmediareq *ifmr;
	int phy, err;
	int tag1, tag2, portreg;

	sc = device_get_softc(dev);
	ifmr = &p->es_ifmr;

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		tag1 = ksz8995ma_readreg(dev, KSZ8995MA_PC3_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		tag2 = ksz8995ma_readreg(dev, KSZ8995MA_PC4_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		p->es_pvid = (tag1 & 0x0f) << 8 | tag2;

		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC0_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		if (portreg & KSZ8995MA_TAG_INS)
			p->es_flags |= ETHERSWITCH_PORT_ADDTAG;
		if (portreg & KSZ8995MA_TAG_RM)
			p->es_flags |= ETHERSWITCH_PORT_STRIPTAG;

		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC2_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		if (portreg & KSZ8995MA_DROP_NONPVID)
			p->es_flags |= ETHERSWITCH_PORT_DROPUNTAGGED;
		if (portreg & KSZ8995MA_INGR_FILT)
			p->es_flags |= ETHERSWITCH_PORT_INGRESS;
	}

	phy = sc->portphy[p->es_port];
	mii = ksz8995ma_miiforport(sc, p->es_port);
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
ksz8995ma_setport(device_t dev, etherswitch_port_t *p)
{
	struct ksz8995ma_softc *sc;
	struct mii_data *mii;
        struct ifmedia *ifm;
        struct ifnet *ifp;
	int phy, err;
	int portreg;

	sc = device_get_softc(dev);

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		ksz8995ma_writereg(dev, KSZ8995MA_PC4_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port, p->es_pvid & 0xff);
		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC3_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		ksz8995ma_writereg(dev, KSZ8995MA_PC3_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port,
		    (portreg & 0xf0) | ((p->es_pvid >> 8) & 0x0f));

		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC0_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		if (p->es_flags & ETHERSWITCH_PORT_ADDTAG)
			portreg |= KSZ8995MA_TAG_INS;
		else
			portreg &= ~KSZ8995MA_TAG_INS;
		if (p->es_flags & ETHERSWITCH_PORT_STRIPTAG) 
			portreg |= KSZ8995MA_TAG_RM;
		else
			portreg &= ~KSZ8995MA_TAG_RM;
		ksz8995ma_writereg(dev, KSZ8995MA_PC0_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port, portreg);

		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC2_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port);
		if (p->es_flags & ETHERSWITCH_PORT_DROPUNTAGGED)
			portreg |= KSZ8995MA_DROP_NONPVID;
		else
			portreg &= ~KSZ8995MA_DROP_NONPVID;
		if (p->es_flags & ETHERSWITCH_PORT_INGRESS)
			portreg |= KSZ8995MA_INGR_FILT;
		else
			portreg &= ~KSZ8995MA_INGR_FILT;
		ksz8995ma_writereg(dev, KSZ8995MA_PC2_BASE + 
		    KSZ8995MA_PORT_SIZE * p->es_port, portreg);
	}

	phy = sc->portphy[p->es_port];
	mii = ksz8995ma_miiforport(sc, p->es_port);
	if (phy != sc->cpuport) {
		if (mii == NULL)
			return (ENXIO);
		ifp = ksz8995ma_ifpforport(sc, p->es_port);
		ifm = &mii->mii_media;
		err = ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA);
	}
	return (0);
}

static int
ksz8995ma_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	int data0, data1, data2;
	int vlantab;
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		if (vg->es_vlangroup < sc->numports) {
			vg->es_vid = ETHERSWITCH_VID_VALID;
			vg->es_vid |= vg->es_vlangroup;
			data0 = ksz8995ma_readreg(dev, KSZ8995MA_PC1_BASE +
			    KSZ8995MA_PORT_SIZE * vg->es_vlangroup);
			vg->es_member_ports = data0 & 0x1f;
			vg->es_untagged_ports = vg->es_member_ports;
			vg->es_fid = 0;
		} else {
			vg->es_vid = 0;
		}
	} else if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		ksz8995ma_writereg(dev, KSZ8995MA_IAC0,
		    KSZ8995MA_VLAN_TABLE_READ);
		ksz8995ma_writereg(dev, KSZ8995MA_IAC1, vg->es_vlangroup);
		data2 = ksz8995ma_readreg(dev, KSZ8995MA_IDR2);
		data1 = ksz8995ma_readreg(dev, KSZ8995MA_IDR1);
		data0 = ksz8995ma_readreg(dev, KSZ8995MA_IDR0);
		vlantab = data2 << 16 | data1 << 8 | data0;
		if (data2 & KSZ8995MA_VLAN_TABLE_VALID) {
			vg->es_vid = ETHERSWITCH_VID_VALID;
			vg->es_vid |= vlantab & 0xfff;
			vg->es_member_ports = (vlantab >> 16) & 0x1f;
			vg->es_untagged_ports = vg->es_member_ports;
			vg->es_fid = (vlantab >> 12) & 0x0f;
		} else {
			vg->es_fid = 0;
		}
	}
	
	return (0);
}

static int
ksz8995ma_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct ksz8995ma_softc *sc;
	int data0;

	sc = device_get_softc(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		data0 = ksz8995ma_readreg(dev, KSZ8995MA_PC1_BASE +
		    KSZ8995MA_PORT_SIZE * vg->es_vlangroup);
		ksz8995ma_writereg(dev, KSZ8995MA_PC1_BASE +
		    KSZ8995MA_PORT_SIZE * vg->es_vlangroup,
		    (data0 & 0xe0) | (vg->es_member_ports & 0x1f));
	} else if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		if (vg->es_member_ports != 0) {
			ksz8995ma_writereg(dev, KSZ8995MA_IDR2,
			    KSZ8995MA_VLAN_TABLE_VALID |
			    (vg->es_member_ports & 0x1f));
			ksz8995ma_writereg(dev, KSZ8995MA_IDR1,
			    vg->es_fid << 4 | vg->es_vid >> 8);
			ksz8995ma_writereg(dev, KSZ8995MA_IDR0,
			    vg->es_vid & 0xff);
		} else {
			ksz8995ma_writereg(dev, KSZ8995MA_IDR2, 0);
			ksz8995ma_writereg(dev, KSZ8995MA_IDR1, 0);
			ksz8995ma_writereg(dev, KSZ8995MA_IDR0, 0);
		}
		ksz8995ma_writereg(dev, KSZ8995MA_IAC0,
		    KSZ8995MA_VLAN_TABLE_WRITE);
		ksz8995ma_writereg(dev, KSZ8995MA_IAC1, vg->es_vlangroup);
	}

	return (0);
}

static int
ksz8995ma_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->vlan_mode;

	return (0);
}

static void 
ksz8995ma_portvlanreset(device_t dev)
{
	int i, data;
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->numports; ++i) {
		data = ksz8995ma_readreg(dev, KSZ8995MA_PC1_BASE +
		    KSZ8995MA_PORT_SIZE * i);
		ksz8995ma_writereg(dev, KSZ8995MA_PC1_BASE +
		    KSZ8995MA_PORT_SIZE * i, (data & 0xe0) | 0x1f);
	}
}

static int
ksz8995ma_setconf(device_t dev, etherswitch_conf_t *conf)
{
	int reg;
	struct ksz8995ma_softc *sc;

	sc = device_get_softc(dev);

	if ((conf->cmd & ETHERSWITCH_CONF_VLAN_MODE) == 0)
		return (0);

	if (conf->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		sc->vlan_mode = ETHERSWITCH_VLAN_PORT;
		reg = ksz8995ma_readreg(dev, KSZ8995MA_GC3);
		ksz8995ma_writereg(dev, KSZ8995MA_GC3, 
		    reg & ~KSZ8995MA_VLAN_ENABLE);
		ksz8995ma_portvlanreset(dev);
	} else if (conf->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		sc->vlan_mode = ETHERSWITCH_VLAN_DOT1Q;
		reg = ksz8995ma_readreg(dev, KSZ8995MA_GC3);
		ksz8995ma_writereg(dev, KSZ8995MA_GC3, 
		    reg | KSZ8995MA_VLAN_ENABLE);
	} else {
		sc->vlan_mode = 0;
		reg = ksz8995ma_readreg(dev, KSZ8995MA_GC3);
		ksz8995ma_writereg(dev, KSZ8995MA_GC3, 
		    reg & ~KSZ8995MA_VLAN_ENABLE);
		ksz8995ma_portvlanreset(dev);
	}
	return (0);
}

static void
ksz8995ma_statchg(device_t dev)
{

	DPRINTF(dev, "%s\n", __func__);
}

static int
ksz8995ma_ifmedia_upd(struct ifnet *ifp)
{
	struct ksz8995ma_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = ksz8995ma_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);
	return (0);
}

static void
ksz8995ma_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ksz8995ma_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = ksz8995ma_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);

	if (mii == NULL)
		return;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
ksz8995ma_readphy(device_t dev, int phy, int reg)
{
int portreg;

	/* 
	 * This is no mdio/mdc connection code.
         * simulate MIIM Registers via the SPI interface
	 */
	if (reg == MII_BMSR) {
		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PS0_BASE + 
			KSZ8995MA_PORT_SIZE * phy);
		return (KSZ8995MA_MII_STAT | 
		    (portreg & 0x20 ? BMSR_LINK : 0x00) |
		    (portreg & 0x40 ? BMSR_ACOMP : 0x00));
	} else if (reg == MII_PHYIDR1) {
		return (KSZ8995MA_MII_PHYID_H);
	} else if (reg == MII_PHYIDR2) {
		return (KSZ8995MA_MII_PHYID_L);
	} else if (reg == MII_ANAR) {
		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC12_BASE + 
			KSZ8995MA_PORT_SIZE * phy);
		return (KSZ8995MA_MII_AA | (portreg & 0x0f) << 5);
	} else if (reg == MII_ANLPAR) {
		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PS0_BASE + 
			KSZ8995MA_PORT_SIZE * phy);
		return (((portreg & 0x0f) << 5) | 0x01);
	}

	return (0);
}

static int
ksz8995ma_writephy(device_t dev, int phy, int reg, int data)
{
int portreg;

	/* 
	 * This is no mdio/mdc connection code.
         * simulate MIIM Registers via the SPI interface
	 */
	if (reg == MII_BMCR) {
		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC13_BASE + 
			KSZ8995MA_PORT_SIZE * phy);
		if (data & BMCR_PDOWN)
			portreg |= KSZ8995MA_PDOWN;
		else
			portreg &= ~KSZ8995MA_PDOWN;
		if (data & BMCR_STARTNEG)
			portreg |= KSZ8995MA_STARTNEG;
		else
			portreg &= ~KSZ8995MA_STARTNEG;
		ksz8995ma_writereg(dev, KSZ8995MA_PC13_BASE + 
			KSZ8995MA_PORT_SIZE * phy, portreg);
	} else if (reg == MII_ANAR) {
		portreg = ksz8995ma_readreg(dev, KSZ8995MA_PC12_BASE + 
			KSZ8995MA_PORT_SIZE * phy);
		portreg &= 0xf;
		portreg |= ((data >> 5) & 0x0f);
		ksz8995ma_writereg(dev, KSZ8995MA_PC12_BASE + 
			KSZ8995MA_PORT_SIZE * phy, portreg);
	}
	return (0);
}

static int
ksz8995ma_readreg(device_t dev, int addr)
{
	uint8_t txBuf[8], rxBuf[8];
	struct spi_command cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(txBuf, 0, sizeof(txBuf));
	memset(rxBuf, 0, sizeof(rxBuf));

	/* read spi */
	txBuf[0] = KSZ8995MA_SPI_READ;
	txBuf[1] = addr;
	cmd.tx_cmd = &txBuf;
	cmd.rx_cmd = &rxBuf;
	cmd.tx_cmd_sz = 3;
	cmd.rx_cmd_sz = 3;
        err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	if (err)
		return(0);

	return (rxBuf[2]);
}

static int
ksz8995ma_writereg(device_t dev, int addr, int value)
{
	uint8_t txBuf[8], rxBuf[8];
	struct spi_command cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(txBuf, 0, sizeof(txBuf));
	memset(rxBuf, 0, sizeof(rxBuf));

	/* write spi */
	txBuf[0] = KSZ8995MA_SPI_WRITE;
	txBuf[1] = addr;
	txBuf[2] = value;
	cmd.tx_cmd = &txBuf;
	cmd.rx_cmd = &rxBuf;
	cmd.tx_cmd_sz = 3;
	cmd.rx_cmd_sz = 3;
        err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	if (err)
		return(0);

	return (0);
}

static device_method_t ksz8995ma_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			ksz8995ma_probe),
	DEVMETHOD(device_attach,		ksz8995ma_attach),
	DEVMETHOD(device_detach,		ksz8995ma_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,		device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,		ksz8995ma_readphy),
	DEVMETHOD(miibus_writereg,		ksz8995ma_writephy),
	DEVMETHOD(miibus_statchg,		ksz8995ma_statchg),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_lock,		ksz8995ma_lock),
	DEVMETHOD(etherswitch_unlock,		ksz8995ma_unlock),
	DEVMETHOD(etherswitch_getinfo,		ksz8995ma_getinfo),
	DEVMETHOD(etherswitch_readreg,		ksz8995ma_readreg),
	DEVMETHOD(etherswitch_writereg,		ksz8995ma_writereg),
	DEVMETHOD(etherswitch_readphyreg,	ksz8995ma_readphy),
	DEVMETHOD(etherswitch_writephyreg,	ksz8995ma_writephy),
	DEVMETHOD(etherswitch_getport,		ksz8995ma_getport),
	DEVMETHOD(etherswitch_setport,		ksz8995ma_setport),
	DEVMETHOD(etherswitch_getvgroup,	ksz8995ma_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	ksz8995ma_setvgroup),
	DEVMETHOD(etherswitch_setconf,		ksz8995ma_setconf),
	DEVMETHOD(etherswitch_getconf,		ksz8995ma_getconf),

	DEVMETHOD_END
};

DEFINE_CLASS_0(ksz8995ma, ksz8995ma_driver, ksz8995ma_methods,
    sizeof(struct ksz8995ma_softc));
static devclass_t ksz8995ma_devclass;

DRIVER_MODULE(ksz8995ma, spibus, ksz8995ma_driver, ksz8995ma_devclass, 0, 0);
DRIVER_MODULE(miibus, ksz8995ma, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(etherswitch, ksz8995ma, etherswitch_driver, etherswitch_devclass,
    0, 0);
MODULE_VERSION(ksz8995ma, 1);
MODULE_DEPEND(ksz8995ma, spibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(ksz8995ma, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(ksz8995ma, etherswitch, 1, 1, 1); /* XXX which versions? */
