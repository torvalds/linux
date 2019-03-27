/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <machine/bus.h>
#include <dev/iicbus/iic.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mdio/mdio.h>

#include <dev/etherswitch/etherswitch.h>

#include <dev/etherswitch/arswitch/arswitchreg.h>
#include <dev/etherswitch/arswitch/arswitchvar.h>
#include <dev/etherswitch/arswitch/arswitch_reg.h>
#include <dev/etherswitch/arswitch/arswitch_phy.h>
#include <dev/etherswitch/arswitch/arswitch_vlans.h>

#include <dev/etherswitch/arswitch/arswitch_7240.h>
#include <dev/etherswitch/arswitch/arswitch_8216.h>
#include <dev/etherswitch/arswitch/arswitch_8226.h>
#include <dev/etherswitch/arswitch/arswitch_8316.h>
#include <dev/etherswitch/arswitch/arswitch_8327.h>
#include <dev/etherswitch/arswitch/arswitch_9340.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

/* Map ETHERSWITCH_PORT_LED_* to Atheros pattern codes */
static int led_pattern_table[] = {
	[ETHERSWITCH_PORT_LED_DEFAULT] = 0x3,
	[ETHERSWITCH_PORT_LED_ON] = 0x2,
	[ETHERSWITCH_PORT_LED_OFF] = 0x0,
	[ETHERSWITCH_PORT_LED_BLINK] = 0x1
};

static inline int arswitch_portforphy(int phy);
static void arswitch_tick(void *arg);
static int arswitch_ifmedia_upd(struct ifnet *);
static void arswitch_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int ar8xxx_port_vlan_setup(struct arswitch_softc *sc,
    etherswitch_port_t *p);
static int ar8xxx_port_vlan_get(struct arswitch_softc *sc,
    etherswitch_port_t *p);
static int arswitch_setled(struct arswitch_softc *sc, int phy, int led,
    int style);

static int
arswitch_probe(device_t dev)
{
	struct arswitch_softc *sc;
	uint32_t id;
	char *chipname, desc[256];

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));
	sc->page = -1;

	/* AR7240 probe */
	if (ar7240_probe(dev) == 0) {
		chipname = "AR7240";
		sc->sc_switchtype = AR8X16_SWITCH_AR7240;
		sc->is_internal_switch = 1;
		id = 0;
		goto done;
	}

	/* AR9340 probe */
	if (ar9340_probe(dev) == 0) {
		chipname = "AR9340";
		sc->sc_switchtype = AR8X16_SWITCH_AR9340;
		sc->is_internal_switch = 1;
		id = 0;
		goto done;
	}

	/* AR8xxx probe */
	id = arswitch_readreg(dev, AR8X16_REG_MASK_CTRL);
	sc->chip_rev = (id & AR8X16_MASK_CTRL_REV_MASK);
	sc->chip_ver = (id & AR8X16_MASK_CTRL_VER_MASK) > AR8X16_MASK_CTRL_VER_SHIFT;
	switch (id & (AR8X16_MASK_CTRL_VER_MASK | AR8X16_MASK_CTRL_REV_MASK)) {
	case 0x0101:
		chipname = "AR8216";
		sc->sc_switchtype = AR8X16_SWITCH_AR8216;
		break;
	case 0x0201:
		chipname = "AR8226";
		sc->sc_switchtype = AR8X16_SWITCH_AR8226;
		break;
	/* 0x0301 - AR8236 */
	case 0x1000:
	case 0x1001:
		chipname = "AR8316";
		sc->sc_switchtype = AR8X16_SWITCH_AR8316;
		break;
	case 0x1202:
	case 0x1204:
		chipname = "AR8327";
		sc->sc_switchtype = AR8X16_SWITCH_AR8327;
		sc->mii_lo_first = 1;
		break;
	default:
		chipname = NULL;
	}

done:

	DPRINTF(sc, ARSWITCH_DBG_ANY, "chipname=%s, id=%08x\n", chipname, id);
	if (chipname != NULL) {
		snprintf(desc, sizeof(desc),
		    "Atheros %s Ethernet Switch (ver %d rev %d)",
		    chipname,
		    sc->chip_ver,
		    sc->chip_rev);
		device_set_desc_copy(dev, desc);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
arswitch_attach_phys(struct arswitch_softc *sc)
{
	int phy, err = 0;
	char name[IFNAMSIZ];

	/* PHYs need an interface, so we generate a dummy one */
	snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(sc->sc_dev));
	for (phy = 0; phy < sc->numphys; phy++) {
		sc->ifp[phy] = if_alloc(IFT_ETHER);
		if (sc->ifp[phy] == NULL) {
			device_printf(sc->sc_dev, "couldn't allocate ifnet structure\n");
			err = ENOMEM;
			break;
		}

		sc->ifp[phy]->if_softc = sc;
		sc->ifp[phy]->if_flags |= IFF_UP | IFF_BROADCAST |
		    IFF_DRV_RUNNING | IFF_SIMPLEX;
		sc->ifname[phy] = malloc(strlen(name)+1, M_DEVBUF, M_WAITOK);
		bcopy(name, sc->ifname[phy], strlen(name)+1);
		if_initname(sc->ifp[phy], sc->ifname[phy],
		    arswitch_portforphy(phy));
		err = mii_attach(sc->sc_dev, &sc->miibus[phy], sc->ifp[phy],
		    arswitch_ifmedia_upd, arswitch_ifmedia_sts, \
		    BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
#if 0
		DPRINTF(sc->sc_dev, "%s attached to pseudo interface %s\n",
		    device_get_nameunit(sc->miibus[phy]),
		    sc->ifp[phy]->if_xname);
#endif
		if (err != 0) {
			device_printf(sc->sc_dev,
			    "attaching PHY %d failed\n",
			    phy);
			return (err);
		}

		if (AR8X16_IS_SWITCH(sc, AR8327)) {
			int led;
			char ledname[IFNAMSIZ+4];

			for (led = 0; led < 3; led++) {
				sprintf(ledname, "%s%dled%d", name,
				    arswitch_portforphy(phy), led+1);
				sc->dev_led[phy][led].sc = sc;
				sc->dev_led[phy][led].phy = phy;
				sc->dev_led[phy][led].lednum = led;
			}
		}
	}
	return (0);
}

static int
arswitch_reset(device_t dev)
{

	arswitch_writereg(dev, AR8X16_REG_MASK_CTRL,
	    AR8X16_MASK_CTRL_SOFT_RESET);
	DELAY(1000);
	if (arswitch_readreg(dev, AR8X16_REG_MASK_CTRL) &
	    AR8X16_MASK_CTRL_SOFT_RESET) {
		device_printf(dev, "unable to reset switch\n");
		return (-1);
	}
	return (0);
}

static int
arswitch_set_vlan_mode(struct arswitch_softc *sc, uint32_t mode)
{

	/* Check for invalid modes. */
	if ((mode & sc->info.es_vlan_caps) != mode)
		return (EINVAL);

	switch (mode) {
	case ETHERSWITCH_VLAN_DOT1Q:
		sc->vlan_mode = ETHERSWITCH_VLAN_DOT1Q;
		break;
	case ETHERSWITCH_VLAN_PORT:
		sc->vlan_mode = ETHERSWITCH_VLAN_PORT;
		break;
	default:
		sc->vlan_mode = 0;
	}

	/* Reset VLANs. */
	sc->hal.arswitch_vlan_init_hw(sc);

	return (0);
}

static void
ar8xxx_port_init(struct arswitch_softc *sc, int port)
{

	/* Port0 - CPU */
	if (port == AR8X16_PORT_CPU) {
		arswitch_writereg(sc->sc_dev, AR8X16_REG_PORT_STS(0),
		    (AR8X16_IS_SWITCH(sc, AR8216) ?
		    AR8X16_PORT_STS_SPEED_100 : AR8X16_PORT_STS_SPEED_1000) |
		    (AR8X16_IS_SWITCH(sc, AR8216) ? 0 : AR8X16_PORT_STS_RXFLOW) |
		    (AR8X16_IS_SWITCH(sc, AR8216) ? 0 : AR8X16_PORT_STS_TXFLOW) |
		    AR8X16_PORT_STS_RXMAC |
		    AR8X16_PORT_STS_TXMAC |
		    AR8X16_PORT_STS_DUPLEX);
		arswitch_writereg(sc->sc_dev, AR8X16_REG_PORT_CTRL(0),
		    arswitch_readreg(sc->sc_dev, AR8X16_REG_PORT_CTRL(0)) &
		    ~AR8X16_PORT_CTRL_HEADER);
	} else {
		/* Set ports to auto negotiation. */
		arswitch_writereg(sc->sc_dev, AR8X16_REG_PORT_STS(port),
		    AR8X16_PORT_STS_LINK_AUTO);
		arswitch_writereg(sc->sc_dev, AR8X16_REG_PORT_CTRL(port),
		    arswitch_readreg(sc->sc_dev, AR8X16_REG_PORT_CTRL(port)) &
		    ~AR8X16_PORT_CTRL_HEADER);
	}
}

static int
ar8xxx_atu_wait_ready(struct arswitch_softc *sc)
{
	int ret;

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	ret = arswitch_waitreg(sc->sc_dev,
	    AR8216_REG_ATU,
	    AR8216_ATU_ACTIVE,
	    0,
	    1000);

	return (ret);
}

/*
 * Flush all ATU entries.
 */
static int
ar8xxx_atu_flush(struct arswitch_softc *sc)
{
	int ret;

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	DPRINTF(sc, ARSWITCH_DBG_ATU, "%s: flushing all ports\n", __func__);

	ret = ar8xxx_atu_wait_ready(sc);
	if (ret)
		device_printf(sc->sc_dev, "%s: waitreg failed\n", __func__);

	if (!ret)
		arswitch_writereg(sc->sc_dev,
		    AR8216_REG_ATU,
		    AR8216_ATU_OP_FLUSH | AR8216_ATU_ACTIVE);

	return (ret);
}

/*
 * Flush ATU entries for a single port.
 */
static int
ar8xxx_atu_flush_port(struct arswitch_softc *sc, int port)
{
	int ret, val;

	DPRINTF(sc, ARSWITCH_DBG_ATU, "%s: flushing port %d\n", __func__,
	    port);

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	/* Flush unicast entries on port */
	val = AR8216_ATU_OP_FLUSH_UNICAST;

	/* TODO: bit 4 indicates whether to flush dynamic (0) or static (1) */

	/* Which port */
	val |= SM(port, AR8216_ATU_PORT_NUM);

	ret = ar8xxx_atu_wait_ready(sc);
	if (ret)
		device_printf(sc->sc_dev, "%s: waitreg failed\n", __func__);

	if (!ret)
		arswitch_writereg(sc->sc_dev,
		    AR8216_REG_ATU,
		    val | AR8216_ATU_ACTIVE);

	return (ret);
}

/*
 * XXX TODO: flush a single MAC address.
 */

/*
 * Fetch a single entry from the ATU.
 */
static int
ar8xxx_atu_fetch_table(struct arswitch_softc *sc, etherswitch_atu_entry_t *e,
    int atu_fetch_op)
{
	uint32_t ret0, ret1, ret2, val;

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	switch (atu_fetch_op) {
	case 0:
		/* Initialise things for the first fetch */

		DPRINTF(sc, ARSWITCH_DBG_ATU, "%s: initializing\n", __func__);
		(void) ar8xxx_atu_wait_ready(sc);

		arswitch_writereg(sc->sc_dev,
		    AR8216_REG_ATU, AR8216_ATU_OP_GET_NEXT);
		arswitch_writereg(sc->sc_dev,
		    AR8216_REG_ATU_DATA, 0);
		arswitch_writereg(sc->sc_dev,
		    AR8216_REG_ATU_CTRL2, 0);

		return (0);
	case 1:
		DPRINTF(sc, ARSWITCH_DBG_ATU, "%s: reading next\n", __func__);
		/*
		 * Attempt to read the next address entry; don't modify what
		 * is there in AT_ADDR{4,5} as its used for the next fetch
		 */
		(void) ar8xxx_atu_wait_ready(sc);

		/* Begin the next read event; not modifying anything */
		val = arswitch_readreg(sc->sc_dev, AR8216_REG_ATU);
		val |= AR8216_ATU_ACTIVE;
		arswitch_writereg(sc->sc_dev, AR8216_REG_ATU, val);

		/* Wait for it to complete */
		(void) ar8xxx_atu_wait_ready(sc);

		/* Fetch the ethernet address and ATU status */
		ret0 = arswitch_readreg(sc->sc_dev, AR8216_REG_ATU);
		ret1 = arswitch_readreg(sc->sc_dev, AR8216_REG_ATU_DATA);
		ret2 = arswitch_readreg(sc->sc_dev, AR8216_REG_ATU_CTRL2);

		/* If the status is zero, then we're done */
		if (MS(ret2, AR8216_ATU_CTRL2_AT_STATUS) == 0)
			return (-1);

		/* MAC address */
		e->es_macaddr[5] = MS(ret0, AR8216_ATU_ADDR5);
		e->es_macaddr[4] = MS(ret0, AR8216_ATU_ADDR4);
		e->es_macaddr[3] = MS(ret1, AR8216_ATU_ADDR3);
		e->es_macaddr[2] = MS(ret1, AR8216_ATU_ADDR2);
		e->es_macaddr[1] = MS(ret1, AR8216_ATU_ADDR1);
		e->es_macaddr[0] = MS(ret1, AR8216_ATU_ADDR0);

		/* Bitmask of ports this entry is for */
		e->es_portmask = MS(ret2, AR8216_ATU_CTRL2_DESPORT);

		/* TODO: other flags that are interesting */

		DPRINTF(sc, ARSWITCH_DBG_ATU, "%s: MAC %6D portmask 0x%08x\n",
		    __func__,
		    e->es_macaddr, ":", e->es_portmask);
		return (0);
	default:
		return (-1);
	}
	return (-1);
}

/*
 * Configure aging register defaults.
 */
static int
ar8xxx_atu_learn_default(struct arswitch_softc *sc)
{
	int ret;
	uint32_t val;

	DPRINTF(sc, ARSWITCH_DBG_ATU, "%s: resetting learning\n", __func__);

	/*
	 * For now, configure the aging defaults:
	 *
	 * + ARP_EN - enable "acknowledgement" of ARP frames - they are
	 *   forwarded to the CPU port
	 * + LEARN_CHANGE_EN - hash table violations when learning MAC addresses
	 *   will force an entry to be expired/updated and a new one to be
	 *   programmed in.
	 * + AGE_EN - enable address table aging
	 * + AGE_TIME - set to 5 minutes
	 */
	val = 0;
	val |= AR8216_ATU_CTRL_ARP_EN;
	val |= AR8216_ATU_CTRL_LEARN_CHANGE;
	val |= AR8216_ATU_CTRL_AGE_EN;
	val |= 0x2b;	/* 5 minutes; bits 15:0 */

	ret = arswitch_writereg(sc->sc_dev,
	    AR8216_REG_ATU_CTRL,
	    val);

	if (ret)
		device_printf(sc->sc_dev, "%s: writereg failed\n", __func__);

	return (ret);
}

/*
 * XXX TODO: add another routine to configure the leaky behaviour
 * when unknown frames are received.  These must be consistent
 * between ethernet switches.
 */

/*
 * Fetch the configured switch MAC address.
 */
static int
ar8xxx_hw_get_switch_macaddr(struct arswitch_softc *sc, struct ether_addr *ea)
{
	uint32_t ret0, ret1;
	char *s;

	s = (void *) ea;

	ret0 = arswitch_readreg(sc->sc_dev, AR8X16_REG_SW_MAC_ADDR0);
	ret1 = arswitch_readreg(sc->sc_dev, AR8X16_REG_SW_MAC_ADDR1);

	s[5] = MS(ret0, AR8X16_REG_SW_MAC_ADDR0_BYTE5);
	s[4] = MS(ret0, AR8X16_REG_SW_MAC_ADDR0_BYTE4);
	s[3] = MS(ret1, AR8X16_REG_SW_MAC_ADDR1_BYTE3);
	s[2] = MS(ret1, AR8X16_REG_SW_MAC_ADDR1_BYTE2);
	s[1] = MS(ret1, AR8X16_REG_SW_MAC_ADDR1_BYTE1);
	s[0] = MS(ret1, AR8X16_REG_SW_MAC_ADDR1_BYTE0);

	return (0);
}

/*
 * Set the switch mac address.
 */
static int
ar8xxx_hw_set_switch_macaddr(struct arswitch_softc *sc,
    const struct ether_addr *ea)
{

	return (ENXIO);
}

/*
 * XXX TODO: this attach routine does NOT free all memory, resources
 * upon failure!
 */
static int
arswitch_attach(device_t dev)
{
	struct arswitch_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	int err = 0;
	int port;

	/* sc->sc_switchtype is already decided in arswitch_probe() */
	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "arswitch", NULL, MTX_DEF);
	sc->page = -1;
	strlcpy(sc->info.es_name, device_get_desc(dev),
	    sizeof(sc->info.es_name));

	/* Debugging */
	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree = device_get_sysctl_tree(sc->sc_dev);
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "debug", CTLFLAG_RW, &sc->sc_debug, 0,
	    "control debugging printfs");

	/* Allocate a 128 entry ATU table; hopefully its big enough! */
	/* XXX TODO: make this per chip */
	sc->atu.entries = malloc(sizeof(etherswitch_atu_entry_t) * 128,
	    M_DEVBUF, M_NOWAIT);
	if (sc->atu.entries == NULL) {
		device_printf(sc->sc_dev, "%s: failed to allocate ATU table\n",
		    __func__);
		return (ENXIO);
	}
	sc->atu.count = 0;
	sc->atu.size = 128;

	/* Default HAL methods */
	sc->hal.arswitch_port_init = ar8xxx_port_init;
	sc->hal.arswitch_port_vlan_setup = ar8xxx_port_vlan_setup;
	sc->hal.arswitch_port_vlan_get = ar8xxx_port_vlan_get;
	sc->hal.arswitch_vlan_init_hw = ar8xxx_reset_vlans;
	sc->hal.arswitch_hw_get_switch_macaddr = ar8xxx_hw_get_switch_macaddr;
	sc->hal.arswitch_hw_set_switch_macaddr = ar8xxx_hw_set_switch_macaddr;

	sc->hal.arswitch_vlan_getvgroup = ar8xxx_getvgroup;
	sc->hal.arswitch_vlan_setvgroup = ar8xxx_setvgroup;

	sc->hal.arswitch_vlan_get_pvid = ar8xxx_get_pvid;
	sc->hal.arswitch_vlan_set_pvid = ar8xxx_set_pvid;

	sc->hal.arswitch_get_dot1q_vlan = ar8xxx_get_dot1q_vlan;
	sc->hal.arswitch_set_dot1q_vlan = ar8xxx_set_dot1q_vlan;
	sc->hal.arswitch_flush_dot1q_vlan = ar8xxx_flush_dot1q_vlan;
	sc->hal.arswitch_purge_dot1q_vlan = ar8xxx_purge_dot1q_vlan;
	sc->hal.arswitch_get_port_vlan = ar8xxx_get_port_vlan;
	sc->hal.arswitch_set_port_vlan = ar8xxx_set_port_vlan;

	sc->hal.arswitch_atu_flush = ar8xxx_atu_flush;
	sc->hal.arswitch_atu_flush_port = ar8xxx_atu_flush_port;
	sc->hal.arswitch_atu_learn_default = ar8xxx_atu_learn_default;
	sc->hal.arswitch_atu_fetch_table = ar8xxx_atu_fetch_table;

	sc->hal.arswitch_phy_read = arswitch_readphy_internal;
	sc->hal.arswitch_phy_write = arswitch_writephy_internal;

	/*
	 * Attach switch related functions
	 */
	if (AR8X16_IS_SWITCH(sc, AR7240))
		ar7240_attach(sc);
	else if (AR8X16_IS_SWITCH(sc, AR9340))
		ar9340_attach(sc);
	else if (AR8X16_IS_SWITCH(sc, AR8216))
		ar8216_attach(sc);
	else if (AR8X16_IS_SWITCH(sc, AR8226))
		ar8226_attach(sc);
	else if (AR8X16_IS_SWITCH(sc, AR8316))
		ar8316_attach(sc);
	else if (AR8X16_IS_SWITCH(sc, AR8327))
		ar8327_attach(sc);
	else {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: unknown switch (%d)?\n", __func__, sc->sc_switchtype);
		return (ENXIO);
	}

	/* Common defaults. */
	sc->info.es_nports = 5; /* XXX technically 6, but 6th not used */

	/* XXX Defaults for externally connected AR8316 */
	sc->numphys = 4;
	sc->phy4cpu = 1;
	sc->is_rgmii = 1;
	sc->is_gmii = 0;
	sc->is_mii = 0;

	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "numphys", &sc->numphys);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "phy4cpu", &sc->phy4cpu);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "is_rgmii", &sc->is_rgmii);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "is_gmii", &sc->is_gmii);
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "is_mii", &sc->is_mii);

	if (sc->numphys > AR8X16_NUM_PHYS)
		sc->numphys = AR8X16_NUM_PHYS;

	/* Reset the switch. */
	if (arswitch_reset(dev)) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: arswitch_reset: failed\n", __func__);
		return (ENXIO);
	}

	err = sc->hal.arswitch_hw_setup(sc);
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: hw_setup: err=%d\n", __func__, err);
		return (err);
	}

	err = sc->hal.arswitch_hw_global_setup(sc);
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: hw_global_setup: err=%d\n", __func__, err);
		return (err);
	}

	/*
	 * Configure the default address table learning parameters for this
	 * switch.
	 */
	err = sc->hal.arswitch_atu_learn_default(sc);
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: atu_learn_default: err=%d\n", __func__, err);
		return (err);
	}

	/* Initialize the switch ports. */
	for (port = 0; port <= sc->numphys; port++) {
		sc->hal.arswitch_port_init(sc, port);
	}

	/*
	 * Attach the PHYs and complete the bus enumeration.
	 */
	err = arswitch_attach_phys(sc);
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: attach_phys: err=%d\n", __func__, err);
		return (err);
	}

	/* Default to ingress filters off. */
	err = arswitch_set_vlan_mode(sc, 0);
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: set_vlan_mode: err=%d\n", __func__, err);
		return (err);
	}

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: bus_generic_attach: err=%d\n", __func__, err);
		return (err);
	}
	
	callout_init_mtx(&sc->callout_tick, &sc->sc_mtx, 0);

	ARSWITCH_LOCK(sc);
	arswitch_tick(sc);
	ARSWITCH_UNLOCK(sc);
	
	return (err);
}

static int
arswitch_detach(device_t dev)
{
	struct arswitch_softc *sc = device_get_softc(dev);
	int i;

	callout_drain(&sc->callout_tick);

	for (i=0; i < sc->numphys; i++) {
		if (sc->miibus[i] != NULL)
			device_delete_child(dev, sc->miibus[i]);
		if (sc->ifp[i] != NULL)
			if_free(sc->ifp[i]);
		free(sc->ifname[i], M_DEVBUF);
	}

	free(sc->atu.entries, M_DEVBUF);

	bus_generic_detach(dev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/*
 * Convert PHY number to port number. PHY0 is connected to port 1, PHY1 to
 * port 2, etc.
 */
static inline int
arswitch_portforphy(int phy)
{
	return (phy+1);
}

static inline struct mii_data *
arswitch_miiforport(struct arswitch_softc *sc, int port)
{
	int phy = port-1;

	if (phy < 0 || phy >= sc->numphys)
		return (NULL);
	return (device_get_softc(sc->miibus[phy]));
}

static inline struct ifnet *
arswitch_ifpforport(struct arswitch_softc *sc, int port)
{
	int phy = port-1;

	if (phy < 0 || phy >= sc->numphys)
		return (NULL);
	return (sc->ifp[phy]);
}

/*
 * Convert port status to ifmedia.
 */
static void
arswitch_update_ifmedia(int portstatus, u_int *media_status, u_int *media_active)
{
	*media_active = IFM_ETHER;
	*media_status = IFM_AVALID;

	if ((portstatus & AR8X16_PORT_STS_LINK_UP) != 0)
		*media_status |= IFM_ACTIVE;
	else {
		*media_active |= IFM_NONE;
		return;
	}
	switch (portstatus & AR8X16_PORT_STS_SPEED_MASK) {
	case AR8X16_PORT_STS_SPEED_10:
		*media_active |= IFM_10_T;
		break;
	case AR8X16_PORT_STS_SPEED_100:
		*media_active |= IFM_100_TX;
		break;
	case AR8X16_PORT_STS_SPEED_1000:
		*media_active |= IFM_1000_T;
		break;
	}
	if ((portstatus & AR8X16_PORT_STS_DUPLEX) == 0)
		*media_active |= IFM_FDX;
	else
		*media_active |= IFM_HDX;
	if ((portstatus & AR8X16_PORT_STS_TXFLOW) != 0)
		*media_active |= IFM_ETH_TXPAUSE;
	if ((portstatus & AR8X16_PORT_STS_RXFLOW) != 0)
		*media_active |= IFM_ETH_RXPAUSE;
}

/*
 * Poll the status for all PHYs.  We're using the switch port status because
 * thats a lot quicker to read than talking to all the PHYs.  Care must be
 * taken that the resulting ifmedia_active is identical to what the PHY will
 * compute, or gratuitous link status changes will occur whenever the PHYs
 * update function is called.
 */
static void
arswitch_miipollstat(struct arswitch_softc *sc)
{
	int i;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int portstatus;
	int port_flap = 0;

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	for (i = 0; i < sc->numphys; i++) {
		if (sc->miibus[i] == NULL)
			continue;
		mii = device_get_softc(sc->miibus[i]);
		/* XXX This would be nice to have abstracted out to be per-chip */
		/* AR8327/AR8337 has a different register base */
		if (AR8X16_IS_SWITCH(sc, AR8327))
			portstatus = arswitch_readreg(sc->sc_dev,
			    AR8327_REG_PORT_STATUS(arswitch_portforphy(i)));
		else
			portstatus = arswitch_readreg(sc->sc_dev,
			    AR8X16_REG_PORT_STS(arswitch_portforphy(i)));
#if 1
		DPRINTF(sc, ARSWITCH_DBG_POLL, "p[%d]=0x%08x (%b)\n",
		    i,
		    portstatus,
		    portstatus,
		    "\20\3TXMAC\4RXMAC\5TXFLOW\6RXFLOW\7"
		    "DUPLEX\11LINK_UP\12LINK_AUTO\13LINK_PAUSE");
#endif
		/*
		 * If the current status is down, but we have a link
		 * status showing up, we need to do an ATU flush.
		 */
		if ((mii->mii_media_status & IFM_ACTIVE) == 0 &&
		    (portstatus & AR8X16_PORT_STS_LINK_UP) != 0) {
			device_printf(sc->sc_dev, "%s: port %d: port -> UP\n",
			    __func__,
			    i);
			port_flap = 1;
		}
		/*
		 * and maybe if a port goes up->down?
		 */
		if ((mii->mii_media_status & IFM_ACTIVE) != 0 &&
		    (portstatus & AR8X16_PORT_STS_LINK_UP) == 0) {
			device_printf(sc->sc_dev, "%s: port %d: port -> DOWN\n",
			    __func__,
			    i);
			port_flap = 1;
		}
		arswitch_update_ifmedia(portstatus, &mii->mii_media_status,
		    &mii->mii_media_active);
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
			if (IFM_INST(mii->mii_media.ifm_cur->ifm_media) !=
			    miisc->mii_inst)
				continue;
			mii_phy_update(miisc, MII_POLLSTAT);
		}
	}

	/* If a port went from down->up, flush the ATU */
	if (port_flap)
		sc->hal.arswitch_atu_flush(sc);
}

static void
arswitch_tick(void *arg)
{
	struct arswitch_softc *sc = arg;

	arswitch_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, arswitch_tick, sc);
}

static void
arswitch_lock(device_t dev)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	ARSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	ARSWITCH_LOCK(sc);
}

static void
arswitch_unlock(device_t dev)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	ARSWITCH_UNLOCK(sc);
}

static etherswitch_info_t *
arswitch_getinfo(device_t dev)
{
	struct arswitch_softc *sc = device_get_softc(dev);
	
	return (&sc->info);
}

static int
ar8xxx_port_vlan_get(struct arswitch_softc *sc, etherswitch_port_t *p)
{
	uint32_t reg;

	ARSWITCH_LOCK(sc);

	/* Retrieve the PVID. */
	sc->hal.arswitch_vlan_get_pvid(sc, p->es_port, &p->es_pvid);

	/* Port flags. */
	reg = arswitch_readreg(sc->sc_dev, AR8X16_REG_PORT_CTRL(p->es_port));
	if (reg & AR8X16_PORT_CTRL_DOUBLE_TAG)
		p->es_flags |= ETHERSWITCH_PORT_DOUBLE_TAG;
	reg >>= AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_SHIFT;
	if ((reg & 0x3) == AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_ADD)
		p->es_flags |= ETHERSWITCH_PORT_ADDTAG;
	if ((reg & 0x3) == AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_STRIP)
		p->es_flags |= ETHERSWITCH_PORT_STRIPTAG;
	ARSWITCH_UNLOCK(sc);

	return (0);
}

static int
arswitch_is_cpuport(struct arswitch_softc *sc, int port)
{

	return ((port == AR8X16_PORT_CPU) ||
	    ((AR8X16_IS_SWITCH(sc, AR8327) &&
	      port == AR8327_PORT_GMAC6)));
}

static int
arswitch_getport(device_t dev, etherswitch_port_t *p)
{
	struct arswitch_softc *sc;
	struct mii_data *mii;
	struct ifmediareq *ifmr;
	int err;

	sc = device_get_softc(dev);
	/* XXX +1 is for AR8327; should make this configurable! */
	if (p->es_port < 0 || p->es_port > sc->info.es_nports)
		return (ENXIO);

	err = sc->hal.arswitch_port_vlan_get(sc, p);
	if (err != 0)
		return (err);

	mii = arswitch_miiforport(sc, p->es_port);
	if (arswitch_is_cpuport(sc, p->es_port)) {
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
		return (ENXIO);
	}
	
	if (!arswitch_is_cpuport(sc, p->es_port) &&
	    AR8X16_IS_SWITCH(sc, AR8327)) {
		int led;
		p->es_nleds = 3;

		for (led = 0; led < p->es_nleds; led++)
		{
			int style;
			uint32_t val;
			
			/* Find the right style enum for our pattern */
			val = arswitch_readreg(dev,
			    ar8327_led_mapping[p->es_port-1][led].reg);
			val = (val>>ar8327_led_mapping[p->es_port-1][led].shift)&0x03;

			for (style = 0; style < ETHERSWITCH_PORT_LED_MAX; style++)
			{
				if (led_pattern_table[style] == val) break;
			}
			
			/* can't happen */
			if (style == ETHERSWITCH_PORT_LED_MAX)
				style = ETHERSWITCH_PORT_LED_DEFAULT;
			
			p->es_led[led] = style;
		}
	} else
	{
		p->es_nleds = 0;
	}
	
	return (0);
}

static int
ar8xxx_port_vlan_setup(struct arswitch_softc *sc, etherswitch_port_t *p)
{
	uint32_t reg;
	int err;

	ARSWITCH_LOCK(sc);

	/* Set the PVID. */
	if (p->es_pvid != 0)
		sc->hal.arswitch_vlan_set_pvid(sc, p->es_port, p->es_pvid);

	/* Mutually exclusive. */
	if (p->es_flags & ETHERSWITCH_PORT_ADDTAG &&
	    p->es_flags & ETHERSWITCH_PORT_STRIPTAG) {
		ARSWITCH_UNLOCK(sc);
		return (EINVAL);
	}

	reg = 0;
	if (p->es_flags & ETHERSWITCH_PORT_DOUBLE_TAG)
		reg |= AR8X16_PORT_CTRL_DOUBLE_TAG;
	if (p->es_flags & ETHERSWITCH_PORT_ADDTAG)
		reg |= AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_ADD <<
		    AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_SHIFT;
	if (p->es_flags & ETHERSWITCH_PORT_STRIPTAG)
		reg |= AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_STRIP <<
		    AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_SHIFT;

	err = arswitch_modifyreg(sc->sc_dev,
	    AR8X16_REG_PORT_CTRL(p->es_port),
	    0x3 << AR8X16_PORT_CTRL_EGRESS_VLAN_MODE_SHIFT |
	    AR8X16_PORT_CTRL_DOUBLE_TAG, reg);

	ARSWITCH_UNLOCK(sc);
	return (err);
}

static int
arswitch_setport(device_t dev, etherswitch_port_t *p)
{
	int err, i;
	struct arswitch_softc *sc;
	struct ifmedia *ifm;
	struct mii_data *mii;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	if (p->es_port < 0 || p->es_port > sc->info.es_nports)
		return (ENXIO);

	/* Port flags. */
	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		err = sc->hal.arswitch_port_vlan_setup(sc, p);
		if (err)
			return (err);
	}

	/* Do not allow media or led changes on CPU port. */
	if (arswitch_is_cpuport(sc, p->es_port))
		return (0);
	
	if (AR8X16_IS_SWITCH(sc, AR8327))
	{
		for (i = 0; i < 3; i++)
		{	
			int err;
			err = arswitch_setled(sc, p->es_port-1, i, p->es_led[i]);
			if (err)
				return (err);
		}
	}

	mii = arswitch_miiforport(sc, p->es_port);
	if (mii == NULL)
		return (ENXIO);

	ifp = arswitch_ifpforport(sc, p->es_port);

	ifm = &mii->mii_media;
	return (ifmedia_ioctl(ifp, &p->es_ifr, ifm, SIOCSIFMEDIA));
}

static int
arswitch_setled(struct arswitch_softc *sc, int phy, int led, int style)
{
	int shift;
	int err;

	if (phy < 0 || phy > sc->numphys)
		return EINVAL;

	if (style < 0 || style > ETHERSWITCH_PORT_LED_MAX)
		return (EINVAL);

	ARSWITCH_LOCK(sc);

	shift = ar8327_led_mapping[phy][led].shift;
	err = (arswitch_modifyreg(sc->sc_dev,
	    ar8327_led_mapping[phy][led].reg,
	    0x03 << shift, led_pattern_table[style] << shift));
	ARSWITCH_UNLOCK(sc);

	return (err);
}

static void
arswitch_statchg(device_t dev)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	DPRINTF(sc, ARSWITCH_DBG_POLL, "%s\n", __func__);
}

static int
arswitch_ifmedia_upd(struct ifnet *ifp)
{
	struct arswitch_softc *sc = ifp->if_softc;
	struct mii_data *mii = arswitch_miiforport(sc, ifp->if_dunit);

	if (mii == NULL)
		return (ENXIO);
	mii_mediachg(mii);
	return (0);
}

static void
arswitch_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct arswitch_softc *sc = ifp->if_softc;
	struct mii_data *mii = arswitch_miiforport(sc, ifp->if_dunit);

	DPRINTF(sc, ARSWITCH_DBG_POLL, "%s\n", __func__);

	if (mii == NULL)
		return;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
arswitch_getconf(device_t dev, etherswitch_conf_t *conf)
{
	struct arswitch_softc *sc;
	int ret;

	sc = device_get_softc(dev);

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = sc->vlan_mode;

	/* Return the switch ethernet address. */
	ret = sc->hal.arswitch_hw_get_switch_macaddr(sc,
	    &conf->switch_macaddr);
	if (ret == 0) {
		conf->cmd |= ETHERSWITCH_CONF_SWITCH_MACADDR;
	}

	return (0);
}

static int
arswitch_setconf(device_t dev, etherswitch_conf_t *conf)
{
	struct arswitch_softc *sc;
	int err;

	sc = device_get_softc(dev);

	/* Set the VLAN mode. */
	if (conf->cmd & ETHERSWITCH_CONF_VLAN_MODE) {
		err = arswitch_set_vlan_mode(sc, conf->vlan_mode);
		if (err != 0)
			return (err);
	}

	/* TODO: Set the switch ethernet address. */

	return (0);
}

static int
arswitch_atu_flush_all(device_t dev)
{
	struct arswitch_softc *sc;
	int err;

	sc = device_get_softc(dev);
	ARSWITCH_LOCK(sc);
	err = sc->hal.arswitch_atu_flush(sc);
	/* Invalidate cached ATU */
	sc->atu.count = 0;
	ARSWITCH_UNLOCK(sc);
	return (err);
}

static int
arswitch_atu_flush_port(device_t dev, int port)
{
	struct arswitch_softc *sc;
	int err;

	sc = device_get_softc(dev);
	ARSWITCH_LOCK(sc);
	err = sc->hal.arswitch_atu_flush_port(sc, port);
	/* Invalidate cached ATU */
	sc->atu.count = 0;
	ARSWITCH_UNLOCK(sc);
	return (err);
}

static int
arswitch_atu_fetch_table(device_t dev, etherswitch_atu_table_t *table)
{
	struct arswitch_softc *sc;
	int err, nitems;

	sc = device_get_softc(dev);

	ARSWITCH_LOCK(sc);
	/* Initial setup */
	nitems = 0;
	err = sc->hal.arswitch_atu_fetch_table(sc, NULL, 0);

	/* fetch - ideally yes we'd fetch into a separate table then switch */
	while (err == 0 && nitems < sc->atu.size) {
		err = sc->hal.arswitch_atu_fetch_table(sc,
		    &sc->atu.entries[nitems], 1);
		if (err == 0) {
			sc->atu.entries[nitems].id = nitems;
			nitems++;
		}
	}
	sc->atu.count = nitems;
	ARSWITCH_UNLOCK(sc);

	table->es_nitems = nitems;

	return (0);
}

static int
arswitch_atu_fetch_table_entry(device_t dev, etherswitch_atu_entry_t *e)
{
	struct arswitch_softc *sc;
	int id;

	sc = device_get_softc(dev);
	id = e->id;

	ARSWITCH_LOCK(sc);
	if (id > sc->atu.count) {
		ARSWITCH_UNLOCK(sc);
		return (ENOENT);
	}

	memcpy(e, &sc->atu.entries[id], sizeof(*e));
	ARSWITCH_UNLOCK(sc);
	return (0);
}

static int
arswitch_getvgroup(device_t dev, etherswitch_vlangroup_t *e)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.arswitch_vlan_getvgroup(sc, e));
}

static int
arswitch_setvgroup(device_t dev, etherswitch_vlangroup_t *e)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.arswitch_vlan_setvgroup(sc, e));
}

static int
arswitch_readphy(device_t dev, int phy, int reg)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.arswitch_phy_read(dev, phy, reg));
}

static int
arswitch_writephy(device_t dev, int phy, int reg, int val)
{
	struct arswitch_softc *sc = device_get_softc(dev);

	return (sc->hal.arswitch_phy_write(dev, phy, reg, val));
}

static device_method_t arswitch_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		arswitch_probe),
	DEVMETHOD(device_attach,	arswitch_attach),
	DEVMETHOD(device_detach,	arswitch_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,	arswitch_readphy),
	DEVMETHOD(miibus_writereg,	arswitch_writephy),
	DEVMETHOD(miibus_statchg,	arswitch_statchg),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		arswitch_readphy),
	DEVMETHOD(mdio_writereg,	arswitch_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_lock,	arswitch_lock),
	DEVMETHOD(etherswitch_unlock,	arswitch_unlock),
	DEVMETHOD(etherswitch_getinfo,	arswitch_getinfo),
	DEVMETHOD(etherswitch_readreg,	arswitch_readreg),
	DEVMETHOD(etherswitch_writereg,	arswitch_writereg),
	DEVMETHOD(etherswitch_readphyreg,	arswitch_readphy),
	DEVMETHOD(etherswitch_writephyreg,	arswitch_writephy),
	DEVMETHOD(etherswitch_getport,	arswitch_getport),
	DEVMETHOD(etherswitch_setport,	arswitch_setport),
	DEVMETHOD(etherswitch_getvgroup,	arswitch_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	arswitch_setvgroup),
	DEVMETHOD(etherswitch_getconf,	arswitch_getconf),
	DEVMETHOD(etherswitch_setconf,	arswitch_setconf),
	DEVMETHOD(etherswitch_flush_all, arswitch_atu_flush_all),
	DEVMETHOD(etherswitch_flush_port, arswitch_atu_flush_port),
	DEVMETHOD(etherswitch_fetch_table, arswitch_atu_fetch_table),
	DEVMETHOD(etherswitch_fetch_table_entry, arswitch_atu_fetch_table_entry),

	DEVMETHOD_END
};

DEFINE_CLASS_0(arswitch, arswitch_driver, arswitch_methods,
    sizeof(struct arswitch_softc));
static devclass_t arswitch_devclass;

DRIVER_MODULE(arswitch, mdio, arswitch_driver, arswitch_devclass, 0, 0);
DRIVER_MODULE(miibus, arswitch, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, arswitch, mdio_driver, mdio_devclass, 0, 0);
DRIVER_MODULE(etherswitch, arswitch, etherswitch_driver, etherswitch_devclass, 0, 0);
MODULE_VERSION(arswitch, 1);
MODULE_DEPEND(arswitch, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(arswitch, etherswitch, 1, 1, 1); /* XXX which versions? */
