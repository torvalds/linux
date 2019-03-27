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
 * This is Infineon ADM6996FC/M/MX driver code on etherswitch framework.
 * Support PORT and DOT1Q VLAN.
 * This code suppose ADM6996FC SDC/SDIO connect to SOC network interface
 * MDC/MDIO.
 * This code development on Netgear WGR614Cv7.
 * etherswitchcfg command port option support addtag.
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

#define	ADM6996FC_PRODUCT_CODE	0x7102

#define	ADM6996FC_SC3		0x11
#define	ADM6996FC_VF0L		0x40
#define	ADM6996FC_VF0H		0x41
#define	ADM6996FC_CI0		0xa0
#define	ADM6996FC_CI1		0xa1
#define	ADM6996FC_PHY_C0	0x200

#define	ADM6996FC_PC_SHIFT	4
#define	ADM6996FC_TBV_SHIFT	5
#define	ADM6996FC_PVID_SHIFT	10
#define	ADM6996FC_OPTE_SHIFT	4
#define	ADM6996FC_VV_SHIFT	15

#define	ADM6996FC_PHY_SIZE	0x20

MALLOC_DECLARE(M_ADM6996FC);
MALLOC_DEFINE(M_ADM6996FC, "adm6996fc", "adm6996fc data structures");

struct adm6996fc_softc {
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
};

#define	ADM6996FC_LOCK(_sc)			\
	    mtx_lock(&(_sc)->sc_mtx)
#define	ADM6996FC_UNLOCK(_sc)			\
	    mtx_unlock(&(_sc)->sc_mtx)
#define	ADM6996FC_LOCK_ASSERT(_sc, _what)	\
	    mtx_assert(&(_sc)->sc_mtx, (_what))
#define	ADM6996FC_TRYLOCK(_sc)			\
	    mtx_trylock(&(_sc)->sc_mtx)

#if defined(DEBUG)
#define	DPRINTF(dev, args...) device_printf(dev, args)
#else
#define	DPRINTF(dev, args...)
#endif

static inline int adm6996fc_portforphy(struct adm6996fc_softc *, int);
static void adm6996fc_tick(void *);
static int adm6996fc_ifmedia_upd(struct ifnet *);
static void adm6996fc_ifmedia_sts(struct ifnet *, struct ifmediareq *);

#define	ADM6996FC_READREG(dev, x)					\
	MDIO_READREG(dev, ((x) >> 5), ((x) & 0x1f));
#define	ADM6996FC_WRITEREG(dev, x, v)					\
	MDIO_WRITEREG(dev, ((x) >> 5), ((x) & 0x1f), v);

#define	ADM6996FC_PVIDBYDATA(data1, data2)				\
	((((data1) >> ADM6996FC_PVID_SHIFT) & 0x0f) | ((data2) << 4))

static int
adm6996fc_probe(device_t dev)
{
	int data1, data2;
	int pc;
	struct adm6996fc_softc *sc;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));

	data1 = ADM6996FC_READREG(device_get_parent(dev), ADM6996FC_CI0);
	data2 = ADM6996FC_READREG(device_get_parent(dev), ADM6996FC_CI1);
	pc = ((data2 << 16) | data1) >> ADM6996FC_PC_SHIFT;
	if (bootverbose)
		device_printf(dev,"Chip Identifier Register %x %x\n", data1,
		    data2);

	/* check Product Code */
	if (pc != ADM6996FC_PRODUCT_CODE) {
		return (ENXIO);
	}

	device_set_desc_copy(dev, "Infineon ADM6996FC/M/MX MDIO switch driver");
	return (BUS_PROBE_DEFAULT);
}

static int
adm6996fc_attach_phys(struct adm6996fc_softc *sc)
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
		sc->miibus[port] = malloc(sizeof(device_t), M_ADM6996FC,
		    M_WAITOK | M_ZERO);
		if (sc->miibus[port] == NULL) {
			err = ENOMEM;
			goto failed;
		}
		err = mii_attach(sc->sc_dev, sc->miibus[port], sc->ifp[port],
		    adm6996fc_ifmedia_upd, adm6996fc_ifmedia_sts, \
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
		/* assume cpuport is last one */
		sc->ifpport[sc->cpuport] = port;
		sc->portphy[port] = sc->cpuport;
		++sc->info.es_nports;
	}
	return (0);

failed:
	for (phy = 0; phy < sc->numports; phy++) {
		if (((1 << phy) & sc->phymask) == 0)
			continue;
		port = adm6996fc_portforphy(sc, phy);
		if (sc->miibus[port] != NULL)
			device_delete_child(sc->sc_dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		if (sc->ifname[port] != NULL)
			free(sc->ifname[port], M_ADM6996FC);
		if (sc->miibus[port] != NULL)
			free(sc->miibus[port], M_ADM6996FC);
	}
	return (err);
}

static int
adm6996fc_attach(device_t dev)
{
	struct adm6996fc_softc	*sc;
	int			 err;

	err = 0;
	sc = device_get_softc(dev);

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "adm6996fc", NULL, MTX_DEF);
	strlcpy(sc->info.es_name, device_get_desc(dev),
	    sizeof(sc->info.es_name));

	/* ADM6996FC Defaults */
	sc->numports = 6;
	sc->phymask = 0x1f;
	sc->cpuport = 5;
	sc->media = 100;

	sc->info.es_nvlangroups = 16;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_PORT | ETHERSWITCH_VLAN_DOT1Q;

	sc->ifp = malloc(sizeof(struct ifnet *) * sc->numports, M_ADM6996FC,
	    M_WAITOK | M_ZERO);
	sc->ifname = malloc(sizeof(char *) * sc->numports, M_ADM6996FC,
	    M_WAITOK | M_ZERO);
	sc->miibus = malloc(sizeof(device_t *) * sc->numports, M_ADM6996FC,
	    M_WAITOK | M_ZERO);
	sc->portphy = malloc(sizeof(int) * sc->numports, M_ADM6996FC,
	    M_WAITOK | M_ZERO);

	if (sc->ifp == NULL || sc->ifname == NULL || sc->miibus == NULL ||
	    sc->portphy == NULL) {
		err = ENOMEM;
		goto failed;
	}

	/*
	 * Attach the PHYs and complete the bus enumeration.
	 */
	err = adm6996fc_attach_phys(sc);
	if (err != 0)
		goto failed;

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0)
		goto failed;
	
	callout_init(&sc->callout_tick, 0);

	adm6996fc_tick(sc);
	
	return (0);

failed:
	if (sc->portphy != NULL)
		free(sc->portphy, M_ADM6996FC);
	if (sc->miibus != NULL)
		free(sc->miibus, M_ADM6996FC);
	if (sc->ifname != NULL)
		free(sc->ifname, M_ADM6996FC);
	if (sc->ifp != NULL)
		free(sc->ifp, M_ADM6996FC);

	return (err);
}

static int
adm6996fc_detach(device_t dev)
{
	struct adm6996fc_softc	*sc;
	int			 i, port;

	sc = device_get_softc(dev);

	callout_drain(&sc->callout_tick);

	for (i = 0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = adm6996fc_portforphy(sc, i);
		if (sc->miibus[port] != NULL)
			device_delete_child(dev, (*sc->miibus[port]));
		if (sc->ifp[port] != NULL)
			if_free(sc->ifp[port]);
		free(sc->ifname[port], M_ADM6996FC);
		free(sc->miibus[port], M_ADM6996FC);
	}

	free(sc->portphy, M_ADM6996FC);
	free(sc->miibus, M_ADM6996FC);
	free(sc->ifname, M_ADM6996FC);
	free(sc->ifp, M_ADM6996FC);

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/*
 * Convert PHY number to port number.
 */
static inline int
adm6996fc_portforphy(struct adm6996fc_softc *sc, int phy)
{

	return (sc->ifpport[phy]);
}

static inline struct mii_data *
adm6996fc_miiforport(struct adm6996fc_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	if (port == sc->cpuport)
		return (NULL);
	return (device_get_softc(*sc->miibus[port]));
}

static inline struct ifnet *
adm6996fc_ifpforport(struct adm6996fc_softc *sc, int port)
{

	if (port < 0 || port > sc->numports)
		return (NULL);
	return (sc->ifp[port]);
}

/*
 * Poll the status for all PHYs.
 */
static void
adm6996fc_miipollstat(struct adm6996fc_softc *sc)
{
	int i, port;
	struct mii_data *mii;
	struct mii_softc *miisc;

	ADM6996FC_LOCK_ASSERT(sc, MA_NOTOWNED);

	for (i = 0; i < MII_NPHY; i++) {
		if (((1 << i) & sc->phymask) == 0)
			continue;
		port = adm6996fc_portforphy(sc, i);
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
adm6996fc_tick(void *arg)
{
	struct adm6996fc_softc *sc;

	sc = arg;

	adm6996fc_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, adm6996fc_tick, sc);
}

static void
adm6996fc_lock(device_t dev)
{
	struct adm6996fc_softc *sc;

	sc = device_get_softc(dev);

	ADM6996FC_LOCK_ASSERT(sc, MA_NOTOWNED);
	ADM6996FC_LOCK(sc);
}

static void
adm6996fc_unlock(device_t dev)
{
	struct adm6996fc_softc *sc;

	sc = device_get_softc(dev);

	ADM6996FC_LOCK_ASSERT(sc, MA_OWNED);
	ADM6996FC_UNLOCK(sc);
}

static etherswitch_info_t *
adm6996fc_getinfo(device_t dev)
{
	struct adm6996fc_softc *sc;

	sc = device_get_softc(dev);
	
	return (&sc->info);
}

static int
adm6996fc_getport(device_t dev, etherswitch_port_t *p)
{
	struct adm6996fc_softc	*sc;
	struct mii_data		*mii;
	struct ifmediareq	*ifmr;
	device_t		 parent;
	int 			 err, phy;
	int			 data1, data2;

	int	bcaddr[6] = {0x01, 0x03, 0x05, 0x07, 0x08, 0x09};
	int	vidaddr[6] = {0x28, 0x29, 0x2a, 0x2b, 0x2b, 0x2c};

	sc = device_get_softc(dev);
	ifmr = &p->es_ifmr;

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	parent = device_get_parent(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		data1 = ADM6996FC_READREG(parent, bcaddr[p->es_port]);
		data2 = ADM6996FC_READREG(parent, vidaddr[p->es_port]);
		/* only port 4 is hi bit */
		if (p->es_port == 4)
			data2 = (data2 >> 8) & 0xff;
		else
			data2 = data2 & 0xff;

		p->es_pvid = ADM6996FC_PVIDBYDATA(data1, data2);
		if (((data1 >> ADM6996FC_OPTE_SHIFT) & 0x01) == 1)
			p->es_flags |= ETHERSWITCH_PORT_ADDTAG;
	} else {
		p->es_pvid = 0;
	}

	phy = sc->portphy[p->es_port];
	mii = adm6996fc_miiforport(sc, p->es_port);
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
adm6996fc_setport(device_t dev, etherswitch_port_t *p)
{
	struct adm6996fc_softc	*sc;
	struct ifmedia		*ifm;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	device_t		 parent;
	int 			 err;
	int			 data;

	int	bcaddr[6] = {0x01, 0x03, 0x05, 0x07, 0x08, 0x09};
	int	vidaddr[6] = {0x28, 0x29, 0x2a, 0x2b, 0x2b, 0x2c};

	sc = device_get_softc(dev);
	parent = device_get_parent(dev);

	if (p->es_port < 0 || p->es_port >= sc->numports)
		return (ENXIO);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		data = ADM6996FC_READREG(parent, bcaddr[p->es_port]);
		data &= ~(0xf << 10);
		data |= (p->es_pvid & 0xf) << ADM6996FC_PVID_SHIFT;
		if (p->es_flags & ETHERSWITCH_PORT_ADDTAG)
			data |= 1 << ADM6996FC_OPTE_SHIFT;
		else
			data &= ~(1 << ADM6996FC_OPTE_SHIFT);
		ADM6996FC_WRITEREG(parent, bcaddr[p->es_port], data);
		data = ADM6996FC_READREG(parent, vidaddr[p->es_port]);
		/* only port 4 is hi bit */
		if (p->es_port == 4) {
			data &= ~(0xff << 8);
			data = data | (((p->es_pvid >> 4) & 0xff) << 8);
		} else {
			data &= ~0xff;
			data = data | ((p->es_pvid >> 4) & 0xff);
		}
		ADM6996FC_WRITEREG(parent, vidaddr[p->es_port], data);
		err = 0;
	} else {
		if (sc->portphy[p->es_port] == sc->cpuport)
			return (ENXIO);
	} 

	if (sc->portphy[p->es_port] != sc->cpuport) {
		mii = adm6996fc_miiforport(sc, p->es_port);
		if (mii == NULL)
			return (ENXIO);

		ifp = adm6996fc_ifpforport(sc, p->es_port);

		ifm = &mii->mii_media;
		err = ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA);
	}
	return (err);
}

static int
adm6996fc_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct adm6996fc_softc	*sc;
	device_t		 parent;
	int			 datahi, datalo;

	sc = device_get_softc(dev);
	parent = device_get_parent(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		if (vg->es_vlangroup <= 5) {
			vg->es_vid = ETHERSWITCH_VID_VALID;
			vg->es_vid |= vg->es_vlangroup;
			datalo = ADM6996FC_READREG(parent,
			    ADM6996FC_VF0L + 2 * vg->es_vlangroup);
			datahi = ADM6996FC_READREG(parent,
			    ADM6996FC_VF0H + 2 * vg->es_vlangroup);

			vg->es_member_ports = datalo & 0x3f;
			vg->es_untagged_ports = vg->es_member_ports;
			vg->es_fid = 0;
		} else {
			vg->es_vid = 0;
		}
	} else if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		datalo = ADM6996FC_READREG(parent,
		    ADM6996FC_VF0L + 2 * vg->es_vlangroup);
		datahi = ADM6996FC_READREG(parent,
		    ADM6996FC_VF0H + 2 * vg->es_vlangroup);
		
		if (datahi & (1 << ADM6996FC_VV_SHIFT)) {
			vg->es_vid = ETHERSWITCH_VID_VALID;
			vg->es_vid |= datahi & 0xfff;
			vg->es_member_ports = datalo & 0x3f;
			vg->es_untagged_ports = (~datalo >> 6) & 0x3f;
			vg->es_fid = 0;
		} else {
			vg->es_fid = 0;
		}
	} else {
		vg->es_fid = 0;
	}

	return (0);
}

static int
adm6996fc_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct adm6996fc_softc	*sc;
	device_t		 parent;

	sc = device_get_softc(dev);
	parent = device_get_parent(dev);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		ADM6996FC_WRITEREG(parent, ADM6996FC_VF0L + 2 * vg->es_vlangroup,
		    vg->es_member_ports);
	} else if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		ADM6996FC_WRITEREG(parent, ADM6996FC_VF0L + 2 * vg->es_vlangroup,
		    vg->es_member_ports | ((~vg->es_untagged_ports & 0x3f)<< 6));
		ADM6996FC_WRITEREG(parent, ADM6996FC_VF0H + 2 * vg->es_vlangroup,
		    (1 << ADM6996FC_VV_SHIFT) | vg->es_vid);
	}

	return (0);
}

static int
adm6996fc_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct adm6996fc_softc *sc;

	sc = device_get_softc(dev);

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->vlan_mode;

	return (0);
}

static int
adm6996fc_setconf(device_t dev, etherswitch_conf_t *conf)
{
	struct adm6996fc_softc	*sc;
	device_t		 parent;
	int 			 i;
	int 			 data;
	int	bcaddr[6] = {0x01, 0x03, 0x05, 0x07, 0x08, 0x09};

	sc = device_get_softc(dev);
	parent = device_get_parent(dev);

	if ((conf->cmd & ETHERSWITCH_CONF_VLAN_MODE) == 0)
		return (0);

	if (conf->vlan_mode == ETHERSWITCH_VLAN_PORT) {
		sc->vlan_mode = ETHERSWITCH_VLAN_PORT;
		data = ADM6996FC_READREG(parent, ADM6996FC_SC3);
		data &= ~(1 << ADM6996FC_TBV_SHIFT);
		ADM6996FC_WRITEREG(parent, ADM6996FC_SC3, data);
		for (i = 0;i <= 5; ++i) {
			data = ADM6996FC_READREG(parent, bcaddr[i]);
			data &= ~(0xf << 10);
			data |= (i << 10);
			ADM6996FC_WRITEREG(parent, bcaddr[i], data);
			ADM6996FC_WRITEREG(parent, ADM6996FC_VF0L + 2 * i,
			    0x003f);
			ADM6996FC_WRITEREG(parent, ADM6996FC_VF0H + 2 * i,
			    (1 << ADM6996FC_VV_SHIFT) | 1);
		}
	} else if (conf->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		sc->vlan_mode = ETHERSWITCH_VLAN_DOT1Q;
		data = ADM6996FC_READREG(parent, ADM6996FC_SC3);
		data |= (1 << ADM6996FC_TBV_SHIFT);
		ADM6996FC_WRITEREG(parent, ADM6996FC_SC3, data);
		for (i = 0;i <= 5; ++i) {
			data = ADM6996FC_READREG(parent, bcaddr[i]);
			/* Private VID set 1 */
			data &= ~(0xf << 10);
			data |= (1 << 10);
			ADM6996FC_WRITEREG(parent, bcaddr[i], data);
		}
		for (i = 2;i <= 15; ++i) {
			ADM6996FC_WRITEREG(parent, ADM6996FC_VF0H + 2 * i,
			    0x0000);
		}
	} else {
		/*
		 ADM6996FC have no VLAN off. Then set Port base and
		 add all port to member. Use VLAN Filter 1 is reset
		 default.
		 */
		sc->vlan_mode = 0;
		data = ADM6996FC_READREG(parent, ADM6996FC_SC3);
		data &= ~(1 << ADM6996FC_TBV_SHIFT);
		ADM6996FC_WRITEREG(parent, ADM6996FC_SC3, data);
		for (i = 0;i <= 5; ++i) {
			data = ADM6996FC_READREG(parent, bcaddr[i]);
			data &= ~(0xf << 10);
			data |= (1 << 10);
			if (i == 5)
				data &= ~(1 << 4);
			ADM6996FC_WRITEREG(parent, bcaddr[i], data);
		}
		/* default setting */
		ADM6996FC_WRITEREG(parent, ADM6996FC_VF0L + 2, 0x003f);
		ADM6996FC_WRITEREG(parent, ADM6996FC_VF0H + 2,
		    (1 << ADM6996FC_VV_SHIFT) | 1);
	}


	return (0);
}

static void
adm6996fc_statchg(device_t dev)
{

	DPRINTF(dev, "%s\n", __func__);
}

static int
adm6996fc_ifmedia_upd(struct ifnet *ifp)
{
	struct adm6996fc_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = adm6996fc_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);
	return (0);
}

static void
adm6996fc_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct adm6996fc_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = adm6996fc_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc->sc_dev, "%s\n", __func__);

	if (mii == NULL)
		return;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
adm6996fc_readphy(device_t dev, int phy, int reg)
{
	struct adm6996fc_softc	*sc;
	int			 data;

	sc = device_get_softc(dev);
	ADM6996FC_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	ADM6996FC_LOCK(sc);
	data = ADM6996FC_READREG(device_get_parent(dev),
	    (ADM6996FC_PHY_C0 + ADM6996FC_PHY_SIZE * phy) + reg);
	ADM6996FC_UNLOCK(sc);

	return (data);
}

static int
adm6996fc_writephy(device_t dev, int phy, int reg, int data)
{
	struct adm6996fc_softc *sc;
	int err;

	sc = device_get_softc(dev);
	ADM6996FC_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	ADM6996FC_LOCK(sc);
	err = ADM6996FC_WRITEREG(device_get_parent(dev),
	    (ADM6996FC_PHY_C0 + ADM6996FC_PHY_SIZE * phy) + reg, data);
	ADM6996FC_UNLOCK(sc);

	return (err);
}

static int
adm6996fc_readreg(device_t dev, int addr)
{

	return ADM6996FC_READREG(device_get_parent(dev),  addr);
}

static int
adm6996fc_writereg(device_t dev, int addr, int value)
{
	int err;

	err = ADM6996FC_WRITEREG(device_get_parent(dev), addr, value);
	return (err);
}

static device_method_t adm6996fc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		adm6996fc_probe),
	DEVMETHOD(device_attach,	adm6996fc_attach),
	DEVMETHOD(device_detach,	adm6996fc_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,	adm6996fc_readphy),
	DEVMETHOD(miibus_writereg,	adm6996fc_writephy),
	DEVMETHOD(miibus_statchg,	adm6996fc_statchg),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		adm6996fc_readphy),
	DEVMETHOD(mdio_writereg,	adm6996fc_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_lock,	adm6996fc_lock),
	DEVMETHOD(etherswitch_unlock,	adm6996fc_unlock),
	DEVMETHOD(etherswitch_getinfo,	adm6996fc_getinfo),
	DEVMETHOD(etherswitch_readreg,	adm6996fc_readreg),
	DEVMETHOD(etherswitch_writereg,	adm6996fc_writereg),
	DEVMETHOD(etherswitch_readphyreg,	adm6996fc_readphy),
	DEVMETHOD(etherswitch_writephyreg,	adm6996fc_writephy),
	DEVMETHOD(etherswitch_getport,	adm6996fc_getport),
	DEVMETHOD(etherswitch_setport,	adm6996fc_setport),
	DEVMETHOD(etherswitch_getvgroup,	adm6996fc_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	adm6996fc_setvgroup),
	DEVMETHOD(etherswitch_setconf,	adm6996fc_setconf),
	DEVMETHOD(etherswitch_getconf,	adm6996fc_getconf),

	DEVMETHOD_END
};

DEFINE_CLASS_0(adm6996fc, adm6996fc_driver, adm6996fc_methods,
    sizeof(struct adm6996fc_softc));
static devclass_t adm6996fc_devclass;

DRIVER_MODULE(adm6996fc, mdio, adm6996fc_driver, adm6996fc_devclass, 0, 0);
DRIVER_MODULE(miibus, adm6996fc, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, adm6996fc, mdio_driver, mdio_devclass, 0, 0);
DRIVER_MODULE(etherswitch, adm6996fc, etherswitch_driver, etherswitch_devclass,
    0, 0);
MODULE_VERSION(adm6996fc, 1);
MODULE_DEPEND(adm6996fc, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(adm6996fc, etherswitch, 1, 1, 1); /* XXX which versions? */
