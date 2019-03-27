/*-
 * Copyright (c) 2016-2017 Hiroki Mori
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
 * This code is Marvell 88E6060 ethernet switch support code on etherswitch
 * framework. 
 * 88E6060 support is only port vlan support. Not support ingress/egress
 * trailer.
 * 88E6065 support is port and dot1q vlan. Also group base tag support.
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

#define	CORE_REGISTER	0x8
#define	SWITCH_ID	3

#define	PORT_CONTROL	4
#define	ENGRESSFSHIFT	2
#define	ENGRESSFMASK	3
#define	ENGRESSTAGSHIFT	12
#define	ENGRESSTAGMASK	3

#define	PORT_VLAN_MAP	6
#define	FORCEMAPSHIFT	8
#define	FORCEMAPMASK	1

#define	PORT_DEFVLAN	7
#define	DEFVIDMASK	0xfff
#define	DEFPRIMASK	7

#define	PORT_CONTROL2	8
#define	DOT1QMODESHIFT	10
#define	DOT1QMODEMASK	3
#define	DOT1QNONE	0
#define	DOT1QFALLBACK	1
#define	DOT1QCHECK	2
#define	DOT1QSECURE	3

#define	GLOBAL_REGISTER	0xf

#define	VTU_OPERATION	5
#define	VTU_VID_REG	6
#define	VTU_DATA1_REG	7
#define	VTU_DATA2_REG	8
#define	VTU_DATA3_REG	9
#define	VTU_BUSY	0x8000
#define	VTU_FLASH	1
#define	VTU_LOAD_PURGE	3
#define	VTU_GET_NEXT	4
#define	VTU_VIOLATION	7

MALLOC_DECLARE(M_E6060SW);
MALLOC_DEFINE(M_E6060SW, "e6060sw", "e6060sw data structures");

struct e6060sw_softc {
	struct mtx	sc_mtx;		/* serialize access to softc */
	device_t	sc_dev;
	int		vlan_mode;
	int		media;		/* cpu port media */
	int		cpuport;	/* which PHY is connected to the CPU */
	int		phymask;	/* PHYs we manage */
	int		numports;	/* number of ports */
	int		ifpport[MII_NPHY];
	int		*portphy;
	char		**ifname;
	device_t	**miibus;
	struct ifnet	**ifp;
	struct callout	callout_tick;
	etherswitch_info_t	info;
	int		smi_offset;
	int		sw_model;
};

/* Switch Identifier DeviceID */

#define	E6060		0x60
#define	E6063		0x63		
#define	E6065		0x65		

#define	E6060SW_LOCK(_sc)			\
	    mtx_lock(&(_sc)->sc_mtx)
#define	E6060SW_UNLOCK(_sc)			\
	    mtx_unlock(&(_sc)->sc_mtx)
#define	E6060SW_LOCK_ASSERT(_sc, _what)	\
	    mtx_assert(&(_sc)->sc_mtx, (_what))
#define	E6060SW_TRYLOCK(_sc)			\
	    mtx_trylock(&(_sc)->sc_mtx)

#if defined(DEBUG)
#define	DPRINTF(dev, args...) device_printf(dev, args)
#else
#define	DPRINTF(dev, args...)
#endif

static inline int e6060sw_portforphy(struct e6060sw_softc *, int);
static void e6060sw_tick(void *);
static int e6060sw_ifmedia_upd(struct ifnet *);
static void e6060sw_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void e6060sw_setup(device_t dev);
static int e6060sw_read_vtu(device_t dev, int num, int *data1, int *data2);
static void e6060sw_set_vtu(device_t dev, int num, int data1, int data2);

static int
e6060sw_probe(device_t dev)
{
	int data;
	struct e6060sw_softc *sc;
	int devid, i;
	char *devname;
	char desc[80];

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));

	devid = 0;
	for (i = 0; i < 2; ++i) {
		data = MDIO_READREG(device_get_parent(dev), 
		    CORE_REGISTER + i * 0x10, SWITCH_ID);
		if (bootverbose)
			device_printf(dev,"Switch Identifier Register %x\n",
			    data);

		devid = data >> 4;
		if (devid == E6060 || 
		    devid == E6063 || devid == E6065) {
			sc->sw_model = devid;
			sc->smi_offset = i * 0x10;
			break;
		}
	}

	if (devid == E6060)
		devname = "88E6060";
	else if (devid == E6063)
		devname = "88E6063";
	else if (devid == E6065)
		devname = "88E6065";
	else
		return (ENXIO);

	sprintf(desc, "Marvell %s MDIO switch driver at 0x%02x",
	    devname, sc->smi_offset);
	device_set_desc_copy(dev, desc);

	return (BUS_PROBE_DEFAULT);
}

static int
e6060sw_attach_phys(struct e6060sw_softc *sc)
{
	int phy, port, err;
	char name[IFNAMSIZ];

	port = 0;
	err = 0;
	/* PHYs need an interface, so we generate a dummy one */
	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->sc_dev));
	for (phy = 0; phy < sc->numports; phy++) {
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
		sc->miibus[port] = malloc(sizeof(device_t), M_E6060SW,
		    M_WAITOK | M_ZERO);
		err = mii_attach(sc->sc_dev, sc->miibus[port], sc->ifp[port],
		    e6060sw_ifmedia_upd, e6060sw_ifmedia_sts, \
		    BMSR_DEFCAPMASK, phy + sc->smi_offset, MII_OFFSET_ANY, 0);
		DPRINTF(sc->sc_dev, "%s attached to pseudo interface %s\n",
		    device_get_nameunit(*sc->miibus[port]),
		    sc->ifp[port]->if_xname);
		if (err != 0) {
			device_printf(sc->sc_dev,
			    "attaching PHY %d failed\n",
			    phy);
			break;
		}
		++port;
	}
	sc->info.es_nports = port;
	if (sc->cpuport != -1) {
		/* assume cpuport is last one */
		sc->ifpport[sc->cpuport] = port;
		sc->portphy[port] = sc->cpuport;
		++sc->info.es_nports;
	}
	return (err);
}

static int
e6060sw_attach(device_t dev)
{
	struct e6060sw_softc *sc;
	int err;

	sc = device_get_softc(dev);
	err = 0;

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "e6060sw", NULL, MTX_DEF);
	strlcpy(sc->info.es_name, device_get_desc(dev),
	    sizeof(sc->info.es_name));

	/* XXX Defaults */
	if (sc->sw_model == E6063) {
		sc->numports = 3;
		sc->phymask = 0x07;
		sc->cpuport = 2;
	} else {
		sc->numports = 6;
		sc->phymask = 0x1f;
		sc->cpuport = 5;
	}
	sc->media = 100;

	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "numports", &sc->numports);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "phymask", &sc->phymask);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "cpuport", &sc->cpuport);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "media", &sc->media);

	if (sc->sw_model == E6060) {
		sc->info.es_nvlangroups = sc->numports;
		sc->info.es_vlan_caps = ETHERSWITCH_VLAN_PORT;
	} else {
		sc->info.es_nvlangroups = 64;
		sc->info.es_vlan_caps = ETHERSWITCH_VLAN_PORT | 
		    ETHERSWITCH_VLAN_DOT1Q;
	}

	e6060sw_setup(dev);

	sc->ifp = malloc(sizeof(struct ifnet *) * sc->numports, M_E6060SW,
	    M_WAITOK | M_ZERO);
	sc->ifname = malloc(sizeof(char *) * sc->numports, M_E6060SW,
	    M_WAITOK | M_ZERO);
	sc->miibus = malloc(sizeof(device_t *) * sc->numports, M_E6060SW,
	    M_WAITOK | M_ZERO);
	sc->portphy = malloc(sizeof(int) * sc->numports, M_E6060SW,
	    M_WAITOK | M_ZERO);

	/*
	 * Attach the PHYs and complete the bus enumeration.
	 */
	err = e6060sw_attach_phys(sc);
	if (err != 0)
		return (err);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0)
		return (err);
	
	callout_init(&sc->callout_tick, 0);

	e6060sw_tick(sc);
	
	return (err);
}

static int
e6060sw_detach(device_t dev)
{
	struct e6060sw_softc *sc;
	int i, port;

	sc = device_get_softc(dev);

	callout_drain(&sc->callout_tick);

	for (i = 0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = e6060sw_portforphy(sc, i);
		if (sc->miibus[port] != NULL)
			device_delete_child(dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		free(sc->ifname[port], M_E6060SW);
		free(sc->miibus[port], M_E6060SW);
	}

	free(sc->portphy, M_E6060SW);
	free(sc->miibus, M_E6060SW);
	free(sc->ifname, M_E6060SW);
	free(sc->ifp, M_E6060SW);

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/*
 * Convert PHY number to port number.
 */
static inline int
e6060sw_portforphy(struct e6060sw_softc *sc, int phy)
{

	return (sc->ifpport[phy]);
}

static inline struct mii_data *
e6060sw_miiforport(struct e6060sw_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	if (port == sc->cpuport)
		return (NULL);
	return (device_get_softc(*sc->miibus[port]));
}

static inline struct ifnet *
e6060sw_ifpforport(struct e6060sw_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (sc->ifp[port]);
}

/*
 * Poll the status for all PHYs.
 */
static void
e6060sw_miipollstat(struct e6060sw_softc *sc)
{
	int i, port;
	struct mii_data *mii;
	struct mii_softc *miisc;

	E6060SW_LOCK_ASSERT(sc, MA_NOTOWNED);

	for (i = 0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = e6060sw_portforphy(sc, i);
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
e6060sw_tick(void *arg)
{
	struct e6060sw_softc *sc;

	sc = arg;

	e6060sw_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, e6060sw_tick, sc);
}

static void
e6060sw_lock(device_t dev)
{
	struct e6060sw_softc *sc;

	sc = device_get_softc(dev);

	E6060SW_LOCK_ASSERT(sc, MA_NOTOWNED);
	E6060SW_LOCK(sc);
}

static void
e6060sw_unlock(device_t dev)
{
	struct e6060sw_softc *sc;

	sc = device_get_softc(dev);

	E6060SW_LOCK_ASSERT(sc, MA_OWNED);
	E6060SW_UNLOCK(sc);
}

static etherswitch_info_t *
e6060sw_getinfo(device_t dev)
{
	struct e6060sw_softc *sc;
	
	sc = device_get_softc(dev);

	return (&sc->info);
}

static int
e6060sw_getport(device_t dev, etherswitch_port_t *p)
{
	struct e6060sw_softc *sc;
	struct mii_data *mii;
	struct ifmediareq *ifmr;
	int err, phy;

	sc = device_get_softc(dev);
	ifmr = &p->es_ifmr;

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	p->es_pvid = 0;
	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		p->es_pvid = MDIO_READREG(device_get_parent(dev), 
		    CORE_REGISTER + sc->smi_offset + p->es_port,
		    PORT_DEFVLAN) & 0xfff;
	}

	phy = sc->portphy[p->es_port];
	mii = e6060sw_miiforport(sc, p->es_port);
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
e6060sw_setport(device_t dev, etherswitch_port_t *p)
{
	struct e6060sw_softc *sc;
	struct ifmedia *ifm;
	struct mii_data *mii;
	struct ifnet *ifp;
	int err;
	int data;

	sc = device_get_softc(dev);

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		data = MDIO_READREG(device_get_parent(dev), 
		    CORE_REGISTER + sc->smi_offset + p->es_port,
		    PORT_DEFVLAN);
		data &= ~0xfff;
		data |= p->es_pvid;
		data |= 1 << 12;
		MDIO_WRITEREG(device_get_parent(dev), 
		    CORE_REGISTER + sc->smi_offset + p->es_port,
		    PORT_DEFVLAN, data);
	}

	if (sc->portphy[p->es_port] == sc->cpuport)
		return(0);

	mii = e6060sw_miiforport(sc, p->es_port);
	if (mii == NULL)
		return (ENXIO);

	ifp = e6060sw_ifpforport(sc, p->es_port);

	ifm = &mii->mii_media;
	err = ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA);
	return (err);
}

static int
e6060sw_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct e6060sw_softc *sc;
	int data1, data2;
	int vid;
	int i, tag;

	sc = device_get_softc(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		vg->es_vid = ETHERSWITCH_VID_VALID;
		vg->es_vid |= vg->es_vlangroup;
		data1 = MDIO_READREG(device_get_parent(dev), 
		    CORE_REGISTER + sc->smi_offset + vg->es_vlangroup,
		    PORT_VLAN_MAP);
		vg->es_member_ports = data1 & 0x3f;
		vg->es_untagged_ports = vg->es_member_ports;
		vg->es_fid = 0;
	} else if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		if (vg->es_vlangroup == 0)
			return (0);
		vid = e6060sw_read_vtu(dev, vg->es_vlangroup, &data1, &data2);
		if (vid > 0) {
			vg->es_vid = ETHERSWITCH_VID_VALID;
			vg->es_vid |= vid;
			vg->es_member_ports = 0;
			vg->es_untagged_ports = 0;
			for (i = 0; i < 4; ++i) {
				tag = data1 >> (i * 4) & 3;
				if (tag == 0 || tag == 1) {
					vg->es_member_ports |= 1 << i;
					vg->es_untagged_ports |= 1 << i;
				} else if (tag == 2) {
					vg->es_member_ports |= 1 << i;
				}
			}
			for (i = 0; i < 2; ++i) {
				tag = data2 >> (i * 4) & 3;
				if (tag == 0 || tag == 1) {
					vg->es_member_ports |= 1 << (i + 4);
					vg->es_untagged_ports |= 1 << (i + 4);
				} else if (tag == 2) {
					vg->es_member_ports |= 1 << (i + 4);
				}
			}

		}
	} else {
		vg->es_vid = 0;
	}
	return (0);
}

static int
e6060sw_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct e6060sw_softc *sc;
	int data1, data2;
	int i;

	sc = device_get_softc(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		data1 = MDIO_READREG(device_get_parent(dev),
		    CORE_REGISTER + sc->smi_offset + vg->es_vlangroup,
		    PORT_VLAN_MAP);
		data1 &= ~0x3f;
		data1 |= vg->es_member_ports;
		MDIO_WRITEREG(device_get_parent(dev),
		    CORE_REGISTER + sc->smi_offset + vg->es_vlangroup,
		    PORT_VLAN_MAP, data1); 
	} else if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		if (vg->es_vlangroup == 0)
			return (0);
		data1 = 0;
		data2 = 0;
		for (i = 0; i < 6; ++i) {
			if (vg->es_member_ports & 
			    vg->es_untagged_ports & (1 << i)) {
				if (i < 4) {
					data1 |= (0xd << i * 4);
				} else {
					data2 |= (0xd << (i - 4) * 4);
				}
			} else if (vg->es_member_ports & (1 << i)) {
				if (i < 4) {
					data1 |= (0xe << i * 4);
				} else {
					data2 |= (0xe << (i - 4) * 4);
				}
			} else {
				if (i < 4) {
					data1 |= (0x3 << i * 4);
				} else {
					data2 |= (0x3 << (i - 4) * 4);
				}
			}
		}
		e6060sw_set_vtu(dev, vg->es_vlangroup, data1, data2);
	}
	return (0);
}

static void
e6060sw_reset_vlans(device_t dev)
{
	struct e6060sw_softc *sc;
	uint32_t ports;
	int i;
	int data;

	sc = device_get_softc(dev);

	for (i = 0; i <= sc->numports; i++) {
		ports = (1 << (sc->numports + 1)) - 1;
		ports &= ~(1 << i);
		if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
			data = i << 12;
		} else if (sc->vlan_mode == 0) {
			data = 1 << 8;
		} else {
			data = 0;
		}
		data |= ports;
		MDIO_WRITEREG(device_get_parent(dev),
		    CORE_REGISTER + sc->smi_offset + i, PORT_VLAN_MAP, data);
	}
}

static void
e6060sw_setup(device_t dev)
{
	struct e6060sw_softc *sc;
	int i;
	int data;

	sc = device_get_softc(dev);

	for (i = 0; i <= sc->numports; i++) {
		if (sc->sw_model == E6063 || sc->sw_model == E6065) {
			data = MDIO_READREG(device_get_parent(dev),
			    CORE_REGISTER + sc->smi_offset + i, PORT_VLAN_MAP);
			data &= ~(FORCEMAPMASK << FORCEMAPSHIFT);
			MDIO_WRITEREG(device_get_parent(dev),
			    CORE_REGISTER + sc->smi_offset + i,
			    PORT_VLAN_MAP, data);

			data = MDIO_READREG(device_get_parent(dev),
			    CORE_REGISTER + sc->smi_offset + i, PORT_CONTROL);
			data |= 3 << ENGRESSFSHIFT;
			MDIO_WRITEREG(device_get_parent(dev),
			    CORE_REGISTER + sc->smi_offset + i, 
			    PORT_CONTROL, data);
		}
	}
}

static void
e6060sw_dot1q_mode(device_t dev, int mode)
{
	struct e6060sw_softc *sc;
	int i;
	int data;

	sc = device_get_softc(dev);

	for (i = 0; i <= sc->numports; i++) {
		data = MDIO_READREG(device_get_parent(dev),
		    CORE_REGISTER + sc->smi_offset + i, PORT_CONTROL2);
		data &= ~(DOT1QMODEMASK << DOT1QMODESHIFT);
		data |= mode << DOT1QMODESHIFT;
		MDIO_WRITEREG(device_get_parent(dev),
		    CORE_REGISTER + sc->smi_offset + i, PORT_CONTROL2, data);

		data = MDIO_READREG(device_get_parent(dev), 
		    CORE_REGISTER + sc->smi_offset + i,
		    PORT_DEFVLAN);
		data &= ~0xfff;
		data |= 1;
		MDIO_WRITEREG(device_get_parent(dev), 
		    CORE_REGISTER + sc->smi_offset + i,
		    PORT_DEFVLAN, data);
	}
}

static int
e6060sw_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct e6060sw_softc *sc;
	
	sc = device_get_softc(dev);

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->vlan_mode;

	return (0);
}

static void
e6060sw_init_vtu(device_t dev)
{
	struct e6060sw_softc *sc;
	int busy;

	sc = device_get_softc(dev);

	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_OPERATION, VTU_BUSY | (VTU_FLASH << 12));
	while (1) {
		busy = MDIO_READREG(device_get_parent(dev),
		    GLOBAL_REGISTER + sc->smi_offset, VTU_OPERATION);
		if ((busy & VTU_BUSY) == 0)
			break;
	}

	/* initial member set at vlan 1*/
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_DATA1_REG, 0xcccc);
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_DATA2_REG, 0x00cc);
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_VID_REG, 0x1000 | 1);
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_OPERATION, VTU_BUSY | (VTU_LOAD_PURGE << 12) | 1);
	while (1) {
		busy = MDIO_READREG(device_get_parent(dev),
		    GLOBAL_REGISTER + sc->smi_offset, VTU_OPERATION);
		if ((busy & VTU_BUSY) == 0)
			break;
	}
}

static void
e6060sw_set_vtu(device_t dev, int num, int data1, int data2)
{
	struct e6060sw_softc *sc;
	int busy;

	sc = device_get_softc(dev);

	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_DATA1_REG, data1);
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_DATA2_REG, data2);
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_VID_REG, 0x1000 | num);
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_OPERATION, VTU_BUSY | (VTU_LOAD_PURGE << 12) | num);
	while (1) {
		busy = MDIO_READREG(device_get_parent(dev),
		    GLOBAL_REGISTER + sc->smi_offset, VTU_OPERATION);
		if ((busy & VTU_BUSY) == 0)
			break;
	}

}

static int
e6060sw_read_vtu(device_t dev, int num, int *data1, int *data2)
{
	struct e6060sw_softc *sc;
	int busy;

	sc = device_get_softc(dev);

	num = num - 1;

	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_VID_REG, num & 0xfff);
	/* Get Next */
	MDIO_WRITEREG(device_get_parent(dev), GLOBAL_REGISTER + sc->smi_offset,
	    VTU_OPERATION, VTU_BUSY | (VTU_GET_NEXT << 12));
	while (1) {
		busy = MDIO_READREG(device_get_parent(dev),
		    GLOBAL_REGISTER + sc->smi_offset, VTU_OPERATION);
		if ((busy & VTU_BUSY) == 0)
			break;
	}

	int vid = MDIO_READREG(device_get_parent(dev),
	    GLOBAL_REGISTER + sc->smi_offset, VTU_VID_REG);
	if (vid & 0x1000) {
		*data1 = MDIO_READREG(device_get_parent(dev),
		    GLOBAL_REGISTER + sc->smi_offset, VTU_DATA1_REG);
		*data2 = MDIO_READREG(device_get_parent(dev),
		    GLOBAL_REGISTER + sc->smi_offset, VTU_DATA2_REG);
		    
		return (vid & 0xfff);
	}

	return (-1);
}

static int
e6060sw_setconf(device_t dev, etherswitch_conf_t *conf)
{
	struct e6060sw_softc *sc;

	sc = device_get_softc(dev);

	/* Set the VLAN mode. */
	if (conf->cmd & ETHERSWITCH_CONF_VLAN_MODE) {
		if (conf->vlan_mode == ETHERSWITCH_VLAN_PORT) {
			sc->vlan_mode = ETHERSWITCH_VLAN_PORT;
			e6060sw_dot1q_mode(dev, DOT1QNONE);
			e6060sw_reset_vlans(dev);
		} else if ((sc->sw_model == E6063 || sc->sw_model == E6065) &&
		    conf->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
			sc->vlan_mode = ETHERSWITCH_VLAN_DOT1Q;
			e6060sw_dot1q_mode(dev, DOT1QSECURE);
			e6060sw_init_vtu(dev);
		} else {
			sc->vlan_mode = 0;
			/* Reset VLANs. */
			e6060sw_dot1q_mode(dev, DOT1QNONE);
			e6060sw_reset_vlans(dev);
		}
	}

	return (0);
}

static void
e6060sw_statchg(device_t dev)
{

	DPRINTF(dev, "%s\n", __func__);
}

static int
e6060sw_ifmedia_upd(struct ifnet *ifp)
{
	struct e6060sw_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = e6060sw_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);
	return (0);
}

static void
e6060sw_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct e6060sw_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = e6060sw_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);

	if (mii == NULL)
		return;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
e6060sw_readphy(device_t dev, int phy, int reg)
{
	struct e6060sw_softc *sc;
	int data;

	sc = device_get_softc(dev);
	E6060SW_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	E6060SW_LOCK(sc);
	data = MDIO_READREG(device_get_parent(dev), phy, reg);
	E6060SW_UNLOCK(sc);

	return (data);
}

static int
e6060sw_writephy(device_t dev, int phy, int reg, int data)
{
	struct e6060sw_softc *sc;
	int err;

	sc = device_get_softc(dev);
	E6060SW_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	E6060SW_LOCK(sc);
	err = MDIO_WRITEREG(device_get_parent(dev), phy, reg, data);
	E6060SW_UNLOCK(sc);

	return (err);
}

/* addr is 5-8 bit is SMI Device Addres, 0-4 bit is SMI Register Address */

static int
e6060sw_readreg(device_t dev, int addr)
{
	int devaddr, regaddr;

	devaddr = (addr >> 5) & 0x1f;
	regaddr = addr & 0x1f;

	return MDIO_READREG(device_get_parent(dev), devaddr, regaddr);
}

/* addr is 5-8 bit is SMI Device Addres, 0-4 bit is SMI Register Address */

static int
e6060sw_writereg(device_t dev, int addr, int value)
{
	int devaddr, regaddr;

	devaddr = (addr >> 5) & 0x1f;
	regaddr = addr & 0x1f;

	return (MDIO_WRITEREG(device_get_parent(dev), devaddr, regaddr, value));
}

static device_method_t e6060sw_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		e6060sw_probe),
	DEVMETHOD(device_attach,	e6060sw_attach),
	DEVMETHOD(device_detach,	e6060sw_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,	e6060sw_readphy),
	DEVMETHOD(miibus_writereg,	e6060sw_writephy),
	DEVMETHOD(miibus_statchg,	e6060sw_statchg),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		e6060sw_readphy),
	DEVMETHOD(mdio_writereg,	e6060sw_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_lock,	e6060sw_lock),
	DEVMETHOD(etherswitch_unlock,	e6060sw_unlock),
	DEVMETHOD(etherswitch_getinfo,	e6060sw_getinfo),
	DEVMETHOD(etherswitch_readreg,	e6060sw_readreg),
	DEVMETHOD(etherswitch_writereg,	e6060sw_writereg),
	DEVMETHOD(etherswitch_readphyreg,	e6060sw_readphy),
	DEVMETHOD(etherswitch_writephyreg,	e6060sw_writephy),
	DEVMETHOD(etherswitch_getport,	e6060sw_getport),
	DEVMETHOD(etherswitch_setport,	e6060sw_setport),
	DEVMETHOD(etherswitch_getvgroup,	e6060sw_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	e6060sw_setvgroup),
	DEVMETHOD(etherswitch_setconf,	e6060sw_setconf),
	DEVMETHOD(etherswitch_getconf,	e6060sw_getconf),

	DEVMETHOD_END
};

DEFINE_CLASS_0(e6060sw, e6060sw_driver, e6060sw_methods,
    sizeof(struct e6060sw_softc));
static devclass_t e6060sw_devclass;

DRIVER_MODULE(e6060sw, mdio, e6060sw_driver, e6060sw_devclass, 0, 0);
DRIVER_MODULE(miibus, e6060sw, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, e6060sw, mdio_driver, mdio_devclass, 0, 0);
DRIVER_MODULE(etherswitch, e6060sw, etherswitch_driver, etherswitch_devclass, 0, 0);
MODULE_VERSION(e6060sw, 1);
MODULE_DEPEND(e6060sw, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(e6060sw, etherswitch, 1, 1, 1); /* XXX which versions? */
