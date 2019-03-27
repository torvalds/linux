/*-
 * Copyright (c) 2015 Semihalf
 * Copyright (c) 2015 Stormshield
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
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/etherswitch/etherswitch.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "e6000swreg.h"
#include "etherswitch_if.h"
#include "miibus_if.h"
#include "mdio_if.h"

MALLOC_DECLARE(M_E6000SW);
MALLOC_DEFINE(M_E6000SW, "e6000sw", "e6000sw switch");

#define	E6000SW_LOCK(_sc)		sx_xlock(&(_sc)->sx)
#define	E6000SW_UNLOCK(_sc)		sx_unlock(&(_sc)->sx)
#define	E6000SW_LOCK_ASSERT(_sc, _what)	sx_assert(&(_sc)->sx, (_what))
#define	E6000SW_TRYLOCK(_sc)		sx_tryxlock(&(_sc)->sx)

typedef struct e6000sw_softc {
	device_t		dev;
	phandle_t		node;

	struct sx		sx;
	struct ifnet		*ifp[E6000SW_MAX_PORTS];
	char			*ifname[E6000SW_MAX_PORTS];
	device_t		miibus[E6000SW_MAX_PORTS];
	struct proc		*kproc;

	uint32_t		swid;
	uint32_t		vlan_mode;
	uint32_t		cpuports_mask;
	uint32_t		fixed_mask;
	uint32_t		fixed25_mask;
	uint32_t		ports_mask;
	int			phy_base;
	int			sw_addr;
	int			num_ports;
	boolean_t		multi_chip;
} e6000sw_softc_t;

static etherswitch_info_t etherswitch_info = {
	.es_nports =		0,
	.es_nvlangroups =	0,
	.es_vlan_caps =		ETHERSWITCH_VLAN_PORT,
	.es_name =		"Marvell 6000 series switch"
};

static void e6000sw_identify(driver_t *, device_t);
static int e6000sw_probe(device_t);
static int e6000sw_parse_fixed_link(e6000sw_softc_t *, phandle_t, uint32_t);
static int e6000sw_parse_ethernet(e6000sw_softc_t *, phandle_t, uint32_t);
static int e6000sw_attach(device_t);
static int e6000sw_detach(device_t);
static int e6000sw_readphy(device_t, int, int);
static int e6000sw_writephy(device_t, int, int, int);
static etherswitch_info_t* e6000sw_getinfo(device_t);
static int e6000sw_getconf(device_t, etherswitch_conf_t *);
static void e6000sw_lock(device_t);
static void e6000sw_unlock(device_t);
static int e6000sw_getport(device_t, etherswitch_port_t *);
static int e6000sw_setport(device_t, etherswitch_port_t *);
static int e6000sw_readreg_wrapper(device_t, int);
static int e6000sw_writereg_wrapper(device_t, int, int);
static int e6000sw_readphy_wrapper(device_t, int, int);
static int e6000sw_writephy_wrapper(device_t, int, int, int);
static int e6000sw_getvgroup_wrapper(device_t, etherswitch_vlangroup_t *);
static int e6000sw_setvgroup_wrapper(device_t, etherswitch_vlangroup_t *);
static int e6000sw_setvgroup(device_t, etherswitch_vlangroup_t *);
static int e6000sw_getvgroup(device_t, etherswitch_vlangroup_t *);
static void e6000sw_setup(device_t, e6000sw_softc_t *);
static void e6000sw_port_vlan_conf(e6000sw_softc_t *);
static void e6000sw_tick(void *);
static void e6000sw_set_atustat(device_t, e6000sw_softc_t *, int, int);
static int e6000sw_atu_flush(device_t, e6000sw_softc_t *, int);
static __inline void e6000sw_writereg(e6000sw_softc_t *, int, int, int);
static __inline uint32_t e6000sw_readreg(e6000sw_softc_t *, int, int);
static int e6000sw_ifmedia_upd(struct ifnet *);
static void e6000sw_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int e6000sw_atu_mac_table(device_t, e6000sw_softc_t *, struct atu_opt *,
    int);
static int e6000sw_get_pvid(e6000sw_softc_t *, int, int *);
static int e6000sw_set_pvid(e6000sw_softc_t *, int, int);
static __inline bool e6000sw_is_cpuport(e6000sw_softc_t *, int);
static __inline bool e6000sw_is_fixedport(e6000sw_softc_t *, int);
static __inline bool e6000sw_is_fixed25port(e6000sw_softc_t *, int);
static __inline bool e6000sw_is_phyport(e6000sw_softc_t *, int);
static __inline bool e6000sw_is_portenabled(e6000sw_softc_t *, int);
static __inline struct mii_data *e6000sw_miiforphy(e6000sw_softc_t *,
    unsigned int);

static device_method_t e6000sw_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,		e6000sw_identify),
	DEVMETHOD(device_probe,			e6000sw_probe),
	DEVMETHOD(device_attach,		e6000sw_attach),
	DEVMETHOD(device_detach,		e6000sw_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,		device_add_child_ordered),

	/* mii interface */
	DEVMETHOD(miibus_readreg,		e6000sw_readphy),
	DEVMETHOD(miibus_writereg,		e6000sw_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_getinfo,		e6000sw_getinfo),
	DEVMETHOD(etherswitch_getconf,		e6000sw_getconf),
	DEVMETHOD(etherswitch_lock,		e6000sw_lock),
	DEVMETHOD(etherswitch_unlock,		e6000sw_unlock),
	DEVMETHOD(etherswitch_getport,		e6000sw_getport),
	DEVMETHOD(etherswitch_setport,		e6000sw_setport),
	DEVMETHOD(etherswitch_readreg,		e6000sw_readreg_wrapper),
	DEVMETHOD(etherswitch_writereg,		e6000sw_writereg_wrapper),
	DEVMETHOD(etherswitch_readphyreg,	e6000sw_readphy_wrapper),
	DEVMETHOD(etherswitch_writephyreg,	e6000sw_writephy_wrapper),
	DEVMETHOD(etherswitch_setvgroup,	e6000sw_setvgroup_wrapper),
	DEVMETHOD(etherswitch_getvgroup,	e6000sw_getvgroup_wrapper),

	DEVMETHOD_END
};

static devclass_t e6000sw_devclass;

DEFINE_CLASS_0(e6000sw, e6000sw_driver, e6000sw_methods,
    sizeof(e6000sw_softc_t));

DRIVER_MODULE(e6000sw, mdio, e6000sw_driver, e6000sw_devclass, 0, 0);
DRIVER_MODULE(etherswitch, e6000sw, etherswitch_driver, etherswitch_devclass, 0,
    0);
DRIVER_MODULE(miibus, e6000sw, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(e6000sw, mdio, 1, 1, 1);

#define	SMI_CMD			0
#define	SMI_CMD_BUSY		(1 << 15)
#define	SMI_CMD_OP_READ		((2 << 10) | SMI_CMD_BUSY | (1 << 12))
#define	SMI_CMD_OP_WRITE	((1 << 10) | SMI_CMD_BUSY | (1 << 12))
#define	SMI_DATA		1

#define	MDIO_READ(dev, addr, reg)					\
	MDIO_READREG(device_get_parent(dev), (addr), (reg))
#define	MDIO_WRITE(dev, addr, reg, val)					\
	MDIO_WRITEREG(device_get_parent(dev), (addr), (reg), (val))

static void
e6000sw_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "e6000sw", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "e6000sw", -1);
}

static int
e6000sw_probe(device_t dev)
{
	e6000sw_softc_t *sc;
	const char *description;
	phandle_t switch_node;

	switch_node = ofw_bus_find_compatible(OF_finddevice("/"),
	    "marvell,mv88e6085");

	if (switch_node == 0)
		return (ENXIO);

	if (bootverbose)
		device_printf(dev, "Found switch_node: 0x%x\n", switch_node);

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->node = switch_node;

	if (OF_getencprop(sc->node, "reg", &sc->sw_addr,
	    sizeof(sc->sw_addr)) < 0)
		return (ENXIO);

	/*
	 * According to the Linux source code, all of the Switch IDs we support
	 * are multi_chip capable, and should go into multi-chip mode if the
	 * sw_addr != 0.
	 */
	if (!OF_hasprop(sc->node, "single-chip-addressing") && sc->sw_addr != 0)
		sc->multi_chip = true;

	/*
	 * Create temporary lock, just to satisfy assertions,
	 * when obtaining the switch ID. Destroy immediately afterwards.
	 */
	sx_init(&sc->sx, "e6000sw_tmp");
	E6000SW_LOCK(sc);
	sc->swid = e6000sw_readreg(sc, REG_PORT(0), SWITCH_ID) & 0xfff0;
	E6000SW_UNLOCK(sc);
	sx_destroy(&sc->sx);

	switch (sc->swid) {
	case MV88E6141:
		description = "Marvell 88E6141";
		sc->phy_base = 0x10;
		sc->num_ports = 6;
		break;
	case MV88E6341:
		description = "Marvell 88E6341";
		sc->phy_base = 0x10;
		sc->num_ports = 6;
		break;
	case MV88E6352:
		description = "Marvell 88E6352";
		sc->num_ports = 7;
		break;
	case MV88E6172:
		description = "Marvell 88E6172";
		sc->num_ports = 7;
		break;
	case MV88E6176:
		description = "Marvell 88E6176";
		sc->num_ports = 7;
		break;
	default:
		device_printf(dev, "Unrecognized device, id 0x%x.\n", sc->swid);
		return (ENXIO);
	}

	device_set_desc(dev, description);

	return (BUS_PROBE_DEFAULT);
}

static int
e6000sw_parse_fixed_link(e6000sw_softc_t *sc, phandle_t node, uint32_t port)
{
	int speed;
	phandle_t fixed_link;

	fixed_link = ofw_bus_find_child(node, "fixed-link");

	if (fixed_link != 0) {
		sc->fixed_mask |= (1 << port);

		if (OF_getencprop(fixed_link, "speed", &speed, sizeof(speed))> 0) {
			if (speed == 2500 &&
			    (MVSWITCH(sc, MV88E6141) ||
			     MVSWITCH(sc, MV88E6341)))
				sc->fixed25_mask |= (1 << port);
		} else {
		    device_printf(sc->dev,
			    "Port %d has a fixed-link node without a speed "
			    "property\n", port);

		    return (ENXIO);
		}
	}

	return (0);
}

static int
e6000sw_parse_ethernet(e6000sw_softc_t *sc, phandle_t port_handle, uint32_t port) {
	phandle_t switch_eth, switch_eth_handle;

	if (OF_getencprop(port_handle, "ethernet", (void*)&switch_eth_handle,
	    sizeof(switch_eth_handle)) > 0) {
		if (switch_eth_handle > 0) {
			switch_eth = OF_node_from_xref(switch_eth_handle);

			device_printf(sc->dev, "CPU port at %d\n", port);
			sc->cpuports_mask |= (1 << port);

			return (e6000sw_parse_fixed_link(sc, switch_eth, port));
		} else
			device_printf(sc->dev,
				"Port %d has ethernet property but it points "
				"to an invalid location\n", port);
	}

	return (0);
}

static int
e6000sw_parse_child_fdt(e6000sw_softc_t *sc, phandle_t child, int *pport)
{
	uint32_t port;

	if (pport == NULL)
		return (ENXIO);

	if (OF_getencprop(child, "reg", (void *)&port, sizeof(port)) < 0)
		return (ENXIO);
	if (port >= sc->num_ports)
		return (ENXIO);
	*pport = port;

	if (e6000sw_parse_fixed_link(sc, child, port) != 0)
		return (ENXIO);

	if (e6000sw_parse_ethernet(sc, child, port) != 0)
		return (ENXIO);

	if ((sc->fixed_mask & (1 << port)) != 0)
		device_printf(sc->dev, "fixed port at %d\n", port);
	else
		device_printf(sc->dev, "PHY at port %d\n", port);

	return (0);
}

static int
e6000sw_init_interface(e6000sw_softc_t *sc, int port)
{
	char name[IFNAMSIZ];

	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->dev));

	sc->ifp[port] = if_alloc(IFT_ETHER);
	if (sc->ifp[port] == NULL)
		return (ENOMEM);
	sc->ifp[port]->if_softc = sc;
	sc->ifp[port]->if_flags |= IFF_UP | IFF_BROADCAST |
	    IFF_DRV_RUNNING | IFF_SIMPLEX;
	sc->ifname[port] = malloc(strlen(name) + 1, M_E6000SW, M_NOWAIT);
	if (sc->ifname[port] == NULL) {
		if_free(sc->ifp[port]);
		return (ENOMEM);
	}
	memcpy(sc->ifname[port], name, strlen(name) + 1);
	if_initname(sc->ifp[port], sc->ifname[port], port);

	return (0);
}

static int
e6000sw_attach_miibus(e6000sw_softc_t *sc, int port)
{
	int err;

	err = mii_attach(sc->dev, &sc->miibus[port], sc->ifp[port],
	    e6000sw_ifmedia_upd, e6000sw_ifmedia_sts, BMSR_DEFCAPMASK,
	    port + sc->phy_base, MII_OFFSET_ANY, 0);
	if (err != 0)
		return (err);

	return (0);
}

static int
e6000sw_attach(device_t dev)
{
	e6000sw_softc_t *sc;
	phandle_t child, ports;
	int err, port;
	uint32_t reg;

	err = 0;
	sc = device_get_softc(dev);

	if (sc->multi_chip)
		device_printf(dev, "multi-chip addressing mode\n");
	else
		device_printf(dev, "single-chip addressing mode\n");

	sx_init(&sc->sx, "e6000sw");

	E6000SW_LOCK(sc);
	e6000sw_setup(dev, sc);

	ports = ofw_bus_find_child(sc->node, "ports");

	if (ports == 0) {
		device_printf(dev, "failed to parse DTS: no ports found for "
		    "switch\n");
		return (ENXIO);
	}

	for (child = OF_child(ports); child != 0; child = OF_peer(child)) {
		err = e6000sw_parse_child_fdt(sc, child, &port);
		if (err != 0) {
			device_printf(sc->dev, "failed to parse DTS\n");
			goto out_fail;
		}

		/* Port is in use. */
		sc->ports_mask |= (1 << port);

		err = e6000sw_init_interface(sc, port);
		if (err != 0) {
			device_printf(sc->dev, "failed to init interface\n");
			goto out_fail;
		}

		if (e6000sw_is_fixedport(sc, port)) {
			/* Link must be down to change speed force value. */
			reg = e6000sw_readreg(sc, REG_PORT(port), PSC_CONTROL);
			reg &= ~PSC_CONTROL_LINK_UP;
			reg |= PSC_CONTROL_FORCED_LINK;
			e6000sw_writereg(sc, REG_PORT(port), PSC_CONTROL, reg);

			/*
			 * Force speed, full-duplex, EEE off and flow-control
			 * on.
			 */
			if (e6000sw_is_fixed25port(sc, port))
				reg = PSC_CONTROL_SPD2500;
			else
				reg = PSC_CONTROL_SPD1000;
			reg |= PSC_CONTROL_FORCED_DPX | PSC_CONTROL_FULLDPX |
			    PSC_CONTROL_FORCED_LINK | PSC_CONTROL_LINK_UP |
			    PSC_CONTROL_FORCED_FC | PSC_CONTROL_FC_ON |
			    PSC_CONTROL_FORCED_SPD;
			if (MVSWITCH(sc, MV88E6141) || MVSWITCH(sc, MV88E6341))
			    reg |= PSC_CONTROL_FORCED_EEE;
			e6000sw_writereg(sc, REG_PORT(port), PSC_CONTROL, reg);
		}

		/* Don't attach miibus at CPU/fixed ports */
		if (!e6000sw_is_phyport(sc, port))
			continue;

		err = e6000sw_attach_miibus(sc, port);
		if (err != 0) {
			device_printf(sc->dev, "failed to attach miibus\n");
			goto out_fail;
		}
	}

	etherswitch_info.es_nports = sc->num_ports;

	/* Default to port vlan. */
	e6000sw_port_vlan_conf(sc);
	E6000SW_UNLOCK(sc);

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	kproc_create(e6000sw_tick, sc, &sc->kproc, 0, 0, "e6000sw tick kproc");

	return (0);

out_fail:
	E6000SW_UNLOCK(sc);
	e6000sw_detach(dev);

	return (err);
}

static __inline int
e6000sw_poll_done(e6000sw_softc_t *sc)
{
	int i;

	for (i = 0; i < E6000SW_SMI_TIMEOUT; i++) {

		if ((e6000sw_readreg(sc, REG_GLOBAL2, SMI_PHY_CMD_REG) &
		    (1 << PHY_CMD_SMI_BUSY)) == 0)
			return (0);

		pause("e6000sw PHY poll", hz/1000);
	}

	return (ETIMEDOUT);
}

/*
 * PHY registers are paged. Put page index in reg 22 (accessible from every
 * page), then access specific register.
 */
static int
e6000sw_readphy(device_t dev, int phy, int reg)
{
	e6000sw_softc_t *sc;
	uint32_t val;
	int err;

	sc = device_get_softc(dev);
	if (!e6000sw_is_phyport(sc, phy) || reg >= E6000SW_NUM_PHY_REGS) {
		device_printf(dev, "Wrong register address.\n");
		return (EINVAL);
	}

	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);

	err = e6000sw_poll_done(sc);
	if (err != 0) {
		device_printf(dev, "Timeout while waiting for switch\n");
		return (err);
	}

	val = 1 << PHY_CMD_SMI_BUSY;
	val |= PHY_CMD_MODE_MDIO << PHY_CMD_MODE;
	val |= PHY_CMD_OPCODE_READ << PHY_CMD_OPCODE;
	val |= (reg << PHY_CMD_REG_ADDR) & PHY_CMD_REG_ADDR_MASK;
	val |= (phy << PHY_CMD_DEV_ADDR) & PHY_CMD_DEV_ADDR_MASK;
	e6000sw_writereg(sc, REG_GLOBAL2, SMI_PHY_CMD_REG, val);

	err = e6000sw_poll_done(sc);
	if (err != 0) {
		device_printf(dev, "Timeout while waiting for switch\n");
		return (err);
	}

	val = e6000sw_readreg(sc, REG_GLOBAL2, SMI_PHY_DATA_REG);

	return (val & PHY_DATA_MASK);
}

static int
e6000sw_writephy(device_t dev, int phy, int reg, int data)
{
	e6000sw_softc_t *sc;
	uint32_t val;
	int err;

	sc = device_get_softc(dev);
	if (!e6000sw_is_phyport(sc, phy) || reg >= E6000SW_NUM_PHY_REGS) {
		device_printf(dev, "Wrong register address.\n");
		return (EINVAL);
	}

	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);

	err = e6000sw_poll_done(sc);
	if (err != 0) {
		device_printf(dev, "Timeout while waiting for switch\n");
		return (err);
	}

	val = 1 << PHY_CMD_SMI_BUSY;
	val |= PHY_CMD_MODE_MDIO << PHY_CMD_MODE;
	val |= PHY_CMD_OPCODE_WRITE << PHY_CMD_OPCODE;
	val |= (reg << PHY_CMD_REG_ADDR) & PHY_CMD_REG_ADDR_MASK;
	val |= (phy << PHY_CMD_DEV_ADDR) & PHY_CMD_DEV_ADDR_MASK;
	e6000sw_writereg(sc, REG_GLOBAL2, SMI_PHY_DATA_REG,
	    data & PHY_DATA_MASK);
	e6000sw_writereg(sc, REG_GLOBAL2, SMI_PHY_CMD_REG, val);

	err = e6000sw_poll_done(sc);
	if (err != 0)
		device_printf(dev, "Timeout while waiting for switch\n");

	return (err);
}

static int
e6000sw_detach(device_t dev)
{
	int phy;
	e6000sw_softc_t *sc;

	sc = device_get_softc(dev);
	bus_generic_detach(dev);
	sx_destroy(&sc->sx);
	for (phy = 0; phy < sc->num_ports; phy++) {
		if (sc->miibus[phy] != NULL)
			device_delete_child(dev, sc->miibus[phy]);
		if (sc->ifp[phy] != NULL)
			if_free(sc->ifp[phy]);
		if (sc->ifname[phy] != NULL)
			free(sc->ifname[phy], M_E6000SW);
	}

	return (0);
}

static etherswitch_info_t*
e6000sw_getinfo(device_t dev)
{

	return (&etherswitch_info);
}

static int
e6000sw_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct e6000sw_softc *sc;

	/* Return the VLAN mode. */
	sc = device_get_softc(dev);
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->vlan_mode;

	return (0);
}

static void
e6000sw_lock(device_t dev)
{
	struct e6000sw_softc *sc;

	sc = device_get_softc(dev);

	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);
	E6000SW_LOCK(sc);
}

static void
e6000sw_unlock(device_t dev)
{
	struct e6000sw_softc *sc;

	sc = device_get_softc(dev);

	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);
	E6000SW_UNLOCK(sc);
}

static int
e6000sw_getport(device_t dev, etherswitch_port_t *p)
{
	struct mii_data *mii;
	int err;
	struct ifmediareq *ifmr;

	e6000sw_softc_t *sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	if (p->es_port >= sc->num_ports || p->es_port < 0)
		return (EINVAL);
	if (!e6000sw_is_portenabled(sc, p->es_port))
		return (0);

	err = 0;
	E6000SW_LOCK(sc);
	e6000sw_get_pvid(sc, p->es_port, &p->es_pvid);

	if (e6000sw_is_fixedport(sc, p->es_port)) {
		if (e6000sw_is_cpuport(sc, p->es_port))
			p->es_flags |= ETHERSWITCH_PORT_CPU;
		ifmr = &p->es_ifmr;
		ifmr->ifm_status = IFM_ACTIVE | IFM_AVALID;
		ifmr->ifm_count = 0;
		if (e6000sw_is_fixed25port(sc, p->es_port))
			ifmr->ifm_active = IFM_2500_T;
		else
			ifmr->ifm_active = IFM_1000_T;
		ifmr->ifm_active |= IFM_ETHER | IFM_FDX;
		ifmr->ifm_current = ifmr->ifm_active;
		ifmr->ifm_mask = 0;
	} else {
		mii = e6000sw_miiforphy(sc, p->es_port);
		err = ifmedia_ioctl(mii->mii_ifp, &p->es_ifr,
		    &mii->mii_media, SIOCGIFMEDIA);
	}
	E6000SW_UNLOCK(sc);

	return (err);
}

static int
e6000sw_setport(device_t dev, etherswitch_port_t *p)
{
	e6000sw_softc_t *sc;
	int err;
	struct mii_data *mii;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	if (p->es_port >= sc->num_ports || p->es_port < 0)
		return (EINVAL);
	if (!e6000sw_is_portenabled(sc, p->es_port))
		return (0);

	err = 0;
	E6000SW_LOCK(sc);
	if (p->es_pvid != 0)
		e6000sw_set_pvid(sc, p->es_port, p->es_pvid);
	if (e6000sw_is_phyport(sc, p->es_port)) {
		mii = e6000sw_miiforphy(sc, p->es_port);
		err = ifmedia_ioctl(mii->mii_ifp, &p->es_ifr, &mii->mii_media,
		    SIOCSIFMEDIA);
	}
	E6000SW_UNLOCK(sc);

	return (err);
}

/*
 * Registers in this switch are divided into sections, specified in
 * documentation. So as to access any of them, section index and reg index
 * is necessary. etherswitchcfg uses only one variable, so indexes were
 * compressed into addr_reg: 32 * section_index + reg_index.
 */
static int
e6000sw_readreg_wrapper(device_t dev, int addr_reg)
{

	if ((addr_reg > (REG_GLOBAL2 * 32 + REG_NUM_MAX)) ||
	    (addr_reg < (REG_PORT(0) * 32))) {
		device_printf(dev, "Wrong register address.\n");
		return (EINVAL);
	}

	return (e6000sw_readreg(device_get_softc(dev), addr_reg / 32,
	    addr_reg % 32));
}

static int
e6000sw_writereg_wrapper(device_t dev, int addr_reg, int val)
{

	if ((addr_reg > (REG_GLOBAL2 * 32 + REG_NUM_MAX)) ||
	    (addr_reg < (REG_PORT(0) * 32))) {
		device_printf(dev, "Wrong register address.\n");
		return (EINVAL);
	}
	e6000sw_writereg(device_get_softc(dev), addr_reg / 5,
	    addr_reg % 32, val);

	return (0);
}

/*
 * These wrappers are necessary because PHY accesses from etherswitchcfg
 * need to be synchronized with locks, while miibus PHY accesses do not.
 */
static int
e6000sw_readphy_wrapper(device_t dev, int phy, int reg)
{
	e6000sw_softc_t *sc;
	int ret;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	E6000SW_LOCK(sc);
	ret = e6000sw_readphy(dev, phy, reg);
	E6000SW_UNLOCK(sc);

	return (ret);
}

static int
e6000sw_writephy_wrapper(device_t dev, int phy, int reg, int data)
{
	e6000sw_softc_t *sc;
	int ret;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	E6000SW_LOCK(sc);
	ret = e6000sw_writephy(dev, phy, reg, data);
	E6000SW_UNLOCK(sc);

	return (ret);
}

/*
 * setvgroup/getvgroup called from etherswitchfcg need to be locked,
 * while internal calls do not.
 */
static int
e6000sw_setvgroup_wrapper(device_t dev, etherswitch_vlangroup_t *vg)
{
	e6000sw_softc_t *sc;
	int ret;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	E6000SW_LOCK(sc);
	ret = e6000sw_setvgroup(dev, vg);
	E6000SW_UNLOCK(sc);

	return (ret);
}

static int
e6000sw_getvgroup_wrapper(device_t dev, etherswitch_vlangroup_t *vg)
{
	e6000sw_softc_t *sc;
	int ret;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	E6000SW_LOCK(sc);
	ret = e6000sw_getvgroup(dev, vg);
	E6000SW_UNLOCK(sc);

	return (ret);
}

static __inline void
e6000sw_port_vlan_assign(e6000sw_softc_t *sc, int port, uint32_t fid,
    uint32_t members)
{
	uint32_t reg;

	reg = e6000sw_readreg(sc, REG_PORT(port), PORT_VLAN_MAP);
	reg &= ~PORT_VLAN_MAP_TABLE_MASK;
	reg &= ~PORT_VLAN_MAP_FID_MASK;
	reg |= members & PORT_VLAN_MAP_TABLE_MASK & ~(1 << port);
	reg |= (fid << PORT_VLAN_MAP_FID) & PORT_VLAN_MAP_FID_MASK;
	e6000sw_writereg(sc, REG_PORT(port), PORT_VLAN_MAP, reg);
	reg = e6000sw_readreg(sc, REG_PORT(port), PORT_CONTROL_1);
	reg &= ~PORT_CONTROL_1_FID_MASK;
	reg |= (fid >> 4) & PORT_CONTROL_1_FID_MASK;
	e6000sw_writereg(sc, REG_PORT(port), PORT_CONTROL_1, reg);
}

static int
e6000sw_set_port_vlan(e6000sw_softc_t *sc, etherswitch_vlangroup_t *vg)
{
	uint32_t port;

	port = vg->es_vlangroup;
	if (port > sc->num_ports)
		return (EINVAL);

	if (vg->es_member_ports != vg->es_untagged_ports) {
		device_printf(sc->dev, "Tagged ports not supported.\n");
		return (EINVAL);
	}

	e6000sw_port_vlan_assign(sc, port, port + 1, vg->es_untagged_ports);
	vg->es_vid = port | ETHERSWITCH_VID_VALID;

	return (0);
}

static int
e6000sw_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	e6000sw_softc_t *sc;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT)
		return (e6000sw_set_port_vlan(sc, vg));

	return (EINVAL);
}

static int
e6000sw_get_port_vlan(e6000sw_softc_t *sc, etherswitch_vlangroup_t *vg)
{
	uint32_t port, reg;

	port = vg->es_vlangroup;
	if (port > sc->num_ports)
		return (EINVAL);

	if (!e6000sw_is_portenabled(sc, port)) {
		vg->es_vid = port;
		return (0);
	}

	reg = e6000sw_readreg(sc, REG_PORT(port), PORT_VLAN_MAP);
	vg->es_untagged_ports = vg->es_member_ports =
	    reg & PORT_VLAN_MAP_TABLE_MASK;
	vg->es_vid = port | ETHERSWITCH_VID_VALID;
	vg->es_fid = (reg & PORT_VLAN_MAP_FID_MASK) >> PORT_VLAN_MAP_FID;
	reg = e6000sw_readreg(sc, REG_PORT(port), PORT_CONTROL_1);
	vg->es_fid |= (reg & PORT_CONTROL_1_FID_MASK) << 4;

	return (0);
}

static int
e6000sw_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	e6000sw_softc_t *sc;

	sc = device_get_softc(dev);
	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);

	if (sc->vlan_mode == ETHERSWITCH_VLAN_PORT)
		return (e6000sw_get_port_vlan(sc, vg));

	return (EINVAL);
}

static __inline struct mii_data*
e6000sw_miiforphy(e6000sw_softc_t *sc, unsigned int phy)
{

	if (!e6000sw_is_phyport(sc, phy))
		return (NULL);

	return (device_get_softc(sc->miibus[phy]));
}

static int
e6000sw_ifmedia_upd(struct ifnet *ifp)
{
	e6000sw_softc_t *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = e6000sw_miiforphy(sc, ifp->if_dunit);
	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);

	return (0);
}

static void
e6000sw_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	e6000sw_softc_t *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = e6000sw_miiforphy(sc, ifp->if_dunit);

	if (mii == NULL)
		return;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
e6000sw_smi_waitready(e6000sw_softc_t *sc, int phy)
{
	int i;

	for (i = 0; i < E6000SW_SMI_TIMEOUT; i++) {
		if ((MDIO_READ(sc->dev, phy, SMI_CMD) & SMI_CMD_BUSY) == 0)
			return (0);
		DELAY(1);
	}

	return (1);
}

static __inline uint32_t
e6000sw_readreg(e6000sw_softc_t *sc, int addr, int reg)
{

	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);

	if (!sc->multi_chip)
		return (MDIO_READ(sc->dev, addr, reg) & 0xffff);

	if (e6000sw_smi_waitready(sc, sc->sw_addr)) {
		printf("e6000sw: readreg timeout\n");
		return (0xffff);
	}
	MDIO_WRITE(sc->dev, sc->sw_addr, SMI_CMD,
	    SMI_CMD_OP_READ | (addr << 5) | reg);
	if (e6000sw_smi_waitready(sc, sc->sw_addr)) {
		printf("e6000sw: readreg timeout\n");
		return (0xffff);
	}

	return (MDIO_READ(sc->dev, sc->sw_addr, SMI_DATA) & 0xffff);
}

static __inline void
e6000sw_writereg(e6000sw_softc_t *sc, int addr, int reg, int val)
{

	E6000SW_LOCK_ASSERT(sc, SA_XLOCKED);

	if (!sc->multi_chip) {
		MDIO_WRITE(sc->dev, addr, reg, val);
		return;
	}

	if (e6000sw_smi_waitready(sc, sc->sw_addr)) {
		printf("e6000sw: readreg timeout\n");
		return;
	}
	MDIO_WRITE(sc->dev, sc->sw_addr, SMI_DATA, val);
	MDIO_WRITE(sc->dev, sc->sw_addr, SMI_CMD,
	    SMI_CMD_OP_WRITE | (addr << 5) | reg);
	if (e6000sw_smi_waitready(sc, sc->sw_addr)) {
		printf("e6000sw: readreg timeout\n");
		return;
	}
}

static __inline bool
e6000sw_is_cpuport(e6000sw_softc_t *sc, int port)
{

	return ((sc->cpuports_mask & (1 << port)) ? true : false);
}

static __inline bool
e6000sw_is_fixedport(e6000sw_softc_t *sc, int port)
{

	return ((sc->fixed_mask & (1 << port)) ? true : false);
}

static __inline bool
e6000sw_is_fixed25port(e6000sw_softc_t *sc, int port)
{

	return ((sc->fixed25_mask & (1 << port)) ? true : false);
}

static __inline bool
e6000sw_is_phyport(e6000sw_softc_t *sc, int port)
{
	uint32_t phy_mask;
	phy_mask = ~(sc->fixed_mask | sc->cpuports_mask);

	return ((phy_mask & (1 << port)) ? true : false);
}

static __inline bool
e6000sw_is_portenabled(e6000sw_softc_t *sc, int port)
{

	return ((sc->ports_mask & (1 << port)) ? true : false);
}

static __inline int
e6000sw_set_pvid(e6000sw_softc_t *sc, int port, int pvid)
{

	e6000sw_writereg(sc, REG_PORT(port), PORT_VID, pvid &
	    PORT_VID_DEF_VID_MASK);

	return (0);
}

static __inline int
e6000sw_get_pvid(e6000sw_softc_t *sc, int port, int *pvid)
{

	if (pvid == NULL)
		return (ENXIO);

	*pvid = e6000sw_readreg(sc, REG_PORT(port), PORT_VID) &
	    PORT_VID_DEF_VID_MASK;

	return (0);
}

/*
 * Convert port status to ifmedia.
 */
static void
e6000sw_update_ifmedia(uint16_t portstatus, u_int *media_status, u_int *media_active)
{
	*media_active = IFM_ETHER;
	*media_status = IFM_AVALID;

	if ((portstatus & PORT_STATUS_LINK_MASK) != 0)
		*media_status |= IFM_ACTIVE;
	else {
		*media_active |= IFM_NONE;
		return;
	}

	switch (portstatus & PORT_STATUS_SPEED_MASK) {
	case PORT_STATUS_SPEED_10:
		*media_active |= IFM_10_T;
		break;
	case PORT_STATUS_SPEED_100:
		*media_active |= IFM_100_TX;
		break;
	case PORT_STATUS_SPEED_1000:
		*media_active |= IFM_1000_T;
		break;
	}

	if ((portstatus & PORT_STATUS_DUPLEX_MASK) == 0)
		*media_active |= IFM_FDX;
	else
		*media_active |= IFM_HDX;
}

static void
e6000sw_tick (void *arg)
{
	e6000sw_softc_t *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	uint16_t portstatus;
	int port;

	sc = arg;

	E6000SW_LOCK_ASSERT(sc, SA_UNLOCKED);

	for (;;) {
		E6000SW_LOCK(sc);
		for (port = 0; port < sc->num_ports; port++) {
			/* Tick only on PHY ports */
			if (!e6000sw_is_portenabled(sc, port) ||
			    !e6000sw_is_phyport(sc, port))
				continue;

			mii = e6000sw_miiforphy(sc, port);
			if (mii == NULL)
				continue;

			portstatus = e6000sw_readreg(sc, REG_PORT(port),
			    PORT_STATUS);

			e6000sw_update_ifmedia(portstatus,
			    &mii->mii_media_status, &mii->mii_media_active);

			LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
				if (IFM_INST(mii->mii_media.ifm_cur->ifm_media)
				    != miisc->mii_inst)
					continue;
				mii_phy_update(miisc, MII_POLLSTAT);
			}
		}
		E6000SW_UNLOCK(sc);
		pause("e6000sw tick", 1000);
	}
}

static void
e6000sw_setup(device_t dev, e6000sw_softc_t *sc)
{
	uint16_t atu_ctrl, atu_age;

	/* Set aging time */
	e6000sw_writereg(sc, REG_GLOBAL, ATU_CONTROL,
	    (E6000SW_DEFAULT_AGETIME << ATU_CONTROL_AGETIME) |
	    (1 << ATU_CONTROL_LEARN2ALL));

	/* Send all with specific mac address to cpu port */
	e6000sw_writereg(sc, REG_GLOBAL2, MGMT_EN_2x, MGMT_EN_ALL);
	e6000sw_writereg(sc, REG_GLOBAL2, MGMT_EN_0x, MGMT_EN_ALL);

	/* Disable Remote Management */
	e6000sw_writereg(sc, REG_GLOBAL, SWITCH_GLOBAL_CONTROL2, 0);

	/* Disable loopback filter and flow control messages */
	e6000sw_writereg(sc, REG_GLOBAL2, SWITCH_MGMT,
	    SWITCH_MGMT_PRI_MASK |
	    (1 << SWITCH_MGMT_RSVD2CPU) |
	    SWITCH_MGMT_FC_PRI_MASK |
	    (1 << SWITCH_MGMT_FORCEFLOW));

	e6000sw_atu_flush(dev, sc, NO_OPERATION);
	e6000sw_atu_mac_table(dev, sc, NULL, NO_OPERATION);
	e6000sw_set_atustat(dev, sc, 0, COUNT_ALL);

	/* Set ATU AgeTime to 15 seconds */
	atu_age = 1;

	atu_ctrl = e6000sw_readreg(sc, REG_GLOBAL, ATU_CONTROL);

	/* Set new AgeTime field */
	atu_ctrl &= ~ATU_CONTROL_AGETIME_MASK;
	e6000sw_writereg(sc, REG_GLOBAL, ATU_CONTROL, atu_ctrl |
	    (atu_age << ATU_CONTROL_AGETIME));
}

static void
e6000sw_port_vlan_conf(e6000sw_softc_t *sc)
{
	int i, port, ret;
	uint32_t members;

	/* Disable all ports */
	for (port = 0; port < sc->num_ports; port++) {
		ret = e6000sw_readreg(sc, REG_PORT(port), PORT_CONTROL);
		e6000sw_writereg(sc, REG_PORT(port), PORT_CONTROL,
		    (ret & ~PORT_CONTROL_ENABLE));
	}

	/* Set port priority */
	for (port = 0; port < sc->num_ports; port++) {
		if (!e6000sw_is_portenabled(sc, port))
			continue;
		ret = e6000sw_readreg(sc, REG_PORT(port), PORT_VID);
		ret &= ~PORT_VID_PRIORITY_MASK;
		e6000sw_writereg(sc, REG_PORT(port), PORT_VID, ret);
	}

	/* Set VID map */
	for (port = 0; port < sc->num_ports; port++) {
		if (!e6000sw_is_portenabled(sc, port))
			continue;
		ret = e6000sw_readreg(sc, REG_PORT(port), PORT_VID);
		ret &= ~PORT_VID_DEF_VID_MASK;
		ret |= (port + 1);
		e6000sw_writereg(sc, REG_PORT(port), PORT_VID, ret);
	}

	/* Enable all ports */
	for (port = 0; port < sc->num_ports; port++) {
		if (!e6000sw_is_portenabled(sc, port))
			continue;
		ret = e6000sw_readreg(sc, REG_PORT(port), PORT_CONTROL);
		e6000sw_writereg(sc, REG_PORT(port), PORT_CONTROL,
		    (ret | PORT_CONTROL_ENABLE));
	}

	/* Set VLAN mode. */
	sc->vlan_mode = ETHERSWITCH_VLAN_PORT;
	etherswitch_info.es_nvlangroups = sc->num_ports;
	for (port = 0; port < sc->num_ports; port++) {
		members = 0;
		if (e6000sw_is_portenabled(sc, port)) {
			for (i = 0; i < sc->num_ports; i++) {
				if (i == port || !e6000sw_is_portenabled(sc, i))
					continue;
				members |= (1 << i);
			}
		}
		e6000sw_port_vlan_assign(sc, port, port + 1, members);
	}
}

static void
e6000sw_set_atustat(device_t dev, e6000sw_softc_t *sc, int bin, int flag)
{
	uint16_t ret;

	ret = e6000sw_readreg(sc, REG_GLOBAL2, ATU_STATS);
	e6000sw_writereg(sc, REG_GLOBAL2, ATU_STATS, (bin << ATU_STATS_BIN ) |
	    (flag << ATU_STATS_FLAG));
}

static int
e6000sw_atu_mac_table(device_t dev, e6000sw_softc_t *sc, struct atu_opt *atu,
    int flag)
{
	uint16_t ret_opt;
	uint16_t ret_data;
	int retries;

	if (flag == NO_OPERATION)
		return (0);
	else if ((flag & (LOAD_FROM_FIB | PURGE_FROM_FIB | GET_NEXT_IN_FIB |
	    GET_VIOLATION_DATA | CLEAR_VIOLATION_DATA)) == 0) {
		device_printf(dev, "Wrong Opcode for ATU operation\n");
		return (EINVAL);
	}

	ret_opt = e6000sw_readreg(sc, REG_GLOBAL, ATU_OPERATION);

	if (ret_opt & ATU_UNIT_BUSY) {
		device_printf(dev, "ATU unit is busy, cannot access"
		    "register\n");
		return (EBUSY);
	} else {
		if(flag & LOAD_FROM_FIB) {
			ret_data = e6000sw_readreg(sc, REG_GLOBAL, ATU_DATA);
			e6000sw_writereg(sc, REG_GLOBAL2, ATU_DATA, (ret_data &
			    ~ENTRY_STATE));
		}
		e6000sw_writereg(sc, REG_GLOBAL, ATU_MAC_ADDR01, atu->mac_01);
		e6000sw_writereg(sc, REG_GLOBAL, ATU_MAC_ADDR23, atu->mac_23);
		e6000sw_writereg(sc, REG_GLOBAL, ATU_MAC_ADDR45, atu->mac_45);
		e6000sw_writereg(sc, REG_GLOBAL, ATU_FID, atu->fid);

		e6000sw_writereg(sc, REG_GLOBAL, ATU_OPERATION, (ret_opt |
		    ATU_UNIT_BUSY | flag));

		retries = E6000SW_RETRIES;
		while (--retries & (e6000sw_readreg(sc, REG_GLOBAL,
		    ATU_OPERATION) & ATU_UNIT_BUSY))
			DELAY(1);

		if (retries == 0)
			device_printf(dev, "Timeout while flushing\n");
		else if (flag & GET_NEXT_IN_FIB) {
			atu->mac_01 = e6000sw_readreg(sc, REG_GLOBAL,
			    ATU_MAC_ADDR01);
			atu->mac_23 = e6000sw_readreg(sc, REG_GLOBAL,
			    ATU_MAC_ADDR23);
			atu->mac_45 = e6000sw_readreg(sc, REG_GLOBAL,
			    ATU_MAC_ADDR45);
		}
	}

	return (0);
}

static int
e6000sw_atu_flush(device_t dev, e6000sw_softc_t *sc, int flag)
{
	uint16_t ret;
	int retries;

	if (flag == NO_OPERATION)
		return (0);

	ret = e6000sw_readreg(sc, REG_GLOBAL, ATU_OPERATION);
	if (ret & ATU_UNIT_BUSY) {
		device_printf(dev, "Atu unit is busy, cannot flush\n");
		return (EBUSY);
	} else {
		e6000sw_writereg(sc, REG_GLOBAL, ATU_OPERATION, (ret |
		    ATU_UNIT_BUSY | flag));
		retries = E6000SW_RETRIES;
		while (--retries & (e6000sw_readreg(sc, REG_GLOBAL,
		    ATU_OPERATION) & ATU_UNIT_BUSY))
			DELAY(1);

		if (retries == 0)
			device_printf(dev, "Timeout while flushing\n");
	}

	return (0);
}
