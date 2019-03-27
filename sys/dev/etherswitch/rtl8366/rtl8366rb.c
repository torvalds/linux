/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Hiroki Mori.
 * Copyright (c) 2011-2012 Stefan Bethke.
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

#include "opt_etherswitch.h"

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
#include <dev/iicbus/iic.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mdio/mdio.h>

#include <dev/etherswitch/etherswitch.h>
#include <dev/etherswitch/rtl8366/rtl8366rbvar.h>

#include "mdio_if.h"
#include "iicbus_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"


struct rtl8366rb_softc {
	struct mtx	sc_mtx;		/* serialize access to softc */
	int		smi_acquired;	/* serialize access to SMI/I2C bus */
	struct mtx	callout_mtx;	/* serialize callout */
	device_t	dev;
	int		vid[RTL8366_NUM_VLANS];
	char		*ifname[RTL8366_NUM_PHYS];
	device_t	miibus[RTL8366_NUM_PHYS];
	struct ifnet	*ifp[RTL8366_NUM_PHYS];
	struct callout	callout_tick;
	etherswitch_info_t	info;
	int		chip_type;
	int		phy4cpu;
	int		numphys;
};

#define RTL_LOCK(_sc)	mtx_lock(&(_sc)->sc_mtx)
#define RTL_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)
#define RTL_LOCK_ASSERT(_sc, _what)	mtx_assert(&(_s)c->sc_mtx, (_what))
#define RTL_TRYLOCK(_sc)	mtx_trylock(&(_sc)->sc_mtx)

#define RTL_WAITOK	0
#define	RTL_NOWAIT	1

#define RTL_SMI_ACQUIRED	1
#define RTL_SMI_ACQUIRED_ASSERT(_sc) \
	KASSERT((_sc)->smi_acquired == RTL_SMI_ACQUIRED, ("smi must be acquired @%s", __FUNCTION__))

#if defined(DEBUG)
#define DPRINTF(dev, args...) device_printf(dev, args)
#define DEVERR(dev, err, fmt, args...) do { \
		if (err != 0) device_printf(dev, fmt, err, args); \
	} while (0)
#define DEBUG_INCRVAR(var)	do { \
		var++; \
	} while (0)

static int callout_blocked = 0;
static int iic_select_retries = 0;
static int phy_access_retries = 0;
static SYSCTL_NODE(_debug, OID_AUTO, rtl8366rb, CTLFLAG_RD, 0, "rtl8366rb");
SYSCTL_INT(_debug_rtl8366rb, OID_AUTO, callout_blocked, CTLFLAG_RW, &callout_blocked, 0,
	"number of times the callout couldn't acquire the bus");
SYSCTL_INT(_debug_rtl8366rb, OID_AUTO, iic_select_retries, CTLFLAG_RW, &iic_select_retries, 0,
	"number of times the I2C bus selection had to be retried");
SYSCTL_INT(_debug_rtl8366rb, OID_AUTO, phy_access_retries, CTLFLAG_RW, &phy_access_retries, 0,
	"number of times PHY register access had to be retried");
#else
#define DPRINTF(dev, args...)
#define DEVERR(dev, err, fmt, args...)
#define DEBUG_INCRVAR(var)
#endif

static int smi_probe(device_t dev);
static int smi_read(device_t dev, uint16_t addr, uint16_t *data, int sleep);
static int smi_write(device_t dev, uint16_t addr, uint16_t data, int sleep);
static int smi_rmw(device_t dev, uint16_t addr, uint16_t mask, uint16_t data, int sleep);
static void rtl8366rb_tick(void *arg);
static int rtl8366rb_ifmedia_upd(struct ifnet *);
static void rtl8366rb_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void
rtl8366rb_identify(driver_t *driver, device_t parent)
{
	device_t child;
	struct iicbus_ivar *devi;

	if (device_find_child(parent, "rtl8366rb", -1) == NULL) {
		child = BUS_ADD_CHILD(parent, 0, "rtl8366rb", -1);
		devi = IICBUS_IVAR(child);
		devi->addr = RTL8366_IIC_ADDR;
	}
}

static int
rtl8366rb_probe(device_t dev)
{
	struct rtl8366rb_softc *sc;

	sc = device_get_softc(dev);

	bzero(sc, sizeof(*sc));
	if (smi_probe(dev) != 0)
		return (ENXIO);
	if (sc->chip_type == RTL8366RB)
		device_set_desc(dev, "RTL8366RB Ethernet Switch Controller");
	else
		device_set_desc(dev, "RTL8366SR Ethernet Switch Controller");
	return (BUS_PROBE_DEFAULT);
}

static void
rtl8366rb_init(device_t dev)
{
	struct rtl8366rb_softc *sc;
	int i;

	sc = device_get_softc(dev);

	/* Initialisation for TL-WR1043ND */
#ifdef RTL8366_SOFT_RESET
	smi_rmw(dev, RTL8366_RCR,
		RTL8366_RCR_SOFT_RESET,
		RTL8366_RCR_SOFT_RESET, RTL_WAITOK);
#else
	smi_rmw(dev, RTL8366_RCR,
		RTL8366_RCR_HARD_RESET,
		RTL8366_RCR_HARD_RESET, RTL_WAITOK);
#endif
	/* hard reset not return ack */
	DELAY(100000);
	/* Enable 16 VLAN mode */
	smi_rmw(dev, RTL8366_SGCR,
		RTL8366_SGCR_EN_VLAN | RTL8366_SGCR_EN_VLAN_4KTB,
		RTL8366_SGCR_EN_VLAN, RTL_WAITOK);
	/* Initialize our vlan table. */
	for (i = 0; i <= 1; i++)
		sc->vid[i] = (i + 1) | ETHERSWITCH_VID_VALID;
	/* Remove port 0 from VLAN 1. */
	smi_rmw(dev, RTL8366_VMCR(RTL8366_VMCR_MU_REG, 0),
		(1 << 0), 0, RTL_WAITOK);
	/* Add port 0 untagged and port 5 tagged to VLAN 2. */
	smi_rmw(dev, RTL8366_VMCR(RTL8366_VMCR_MU_REG, 1),
		((1 << 5 | 1 << 0) << RTL8366_VMCR_MU_MEMBER_SHIFT)
			| ((1 << 5 | 1 << 0) << RTL8366_VMCR_MU_UNTAG_SHIFT),
		((1 << 5 | 1 << 0) << RTL8366_VMCR_MU_MEMBER_SHIFT
			| ((1 << 0) << RTL8366_VMCR_MU_UNTAG_SHIFT)),
		RTL_WAITOK);
	/* Set PVID 2 for port 0. */
	smi_rmw(dev, RTL8366_PVCR_REG(0),
		RTL8366_PVCR_VAL(0, RTL8366_PVCR_PORT_MASK),
		RTL8366_PVCR_VAL(0, 1), RTL_WAITOK);
}

static int
rtl8366rb_attach(device_t dev)
{
	struct rtl8366rb_softc *sc;
	uint16_t rev = 0;
	char name[IFNAMSIZ];
	int err = 0;
	int i;

	sc = device_get_softc(dev);

	sc->dev = dev;
	mtx_init(&sc->sc_mtx, "rtl8366rb", NULL, MTX_DEF);
	sc->smi_acquired = 0;
	mtx_init(&sc->callout_mtx, "rtl8366rbcallout", NULL, MTX_DEF);

	rtl8366rb_init(dev);
	smi_read(dev, RTL8366_CVCR, &rev, RTL_WAITOK);
	device_printf(dev, "rev. %d\n", rev & 0x000f);

	sc->phy4cpu = 0;
	(void) resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "phy4cpu", &sc->phy4cpu);

	sc->numphys = sc->phy4cpu ? RTL8366_NUM_PHYS - 1 : RTL8366_NUM_PHYS;

	sc->info.es_nports = sc->numphys + 1;
	sc->info.es_nvlangroups = RTL8366_NUM_VLANS;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_DOT1Q;
	if (sc->chip_type == RTL8366RB)
		sprintf(sc->info.es_name, "Realtek RTL8366RB");
	else
		sprintf(sc->info.es_name, "Realtek RTL8366SR");

	/* attach miibus and phys */
	/* PHYs need an interface, so we generate a dummy one */
	for (i = 0; i < sc->numphys; i++) {
		sc->ifp[i] = if_alloc(IFT_ETHER);
		if (sc->ifp[i] == NULL) {
			device_printf(dev, "couldn't allocate ifnet structure\n");
			err = ENOMEM;
			break;
		}

		sc->ifp[i]->if_softc = sc;
		sc->ifp[i]->if_flags |= IFF_UP | IFF_BROADCAST | IFF_DRV_RUNNING
			| IFF_SIMPLEX;
		snprintf(name, IFNAMSIZ, "%sport", device_get_nameunit(dev));
		sc->ifname[i] = malloc(strlen(name)+1, M_DEVBUF, M_WAITOK);
		bcopy(name, sc->ifname[i], strlen(name)+1);
		if_initname(sc->ifp[i], sc->ifname[i], i);
		err = mii_attach(dev, &sc->miibus[i], sc->ifp[i], rtl8366rb_ifmedia_upd, \
			rtl8366rb_ifmedia_sts, BMSR_DEFCAPMASK, \
			i, MII_OFFSET_ANY, 0);
		if (err != 0) {
			device_printf(dev, "attaching PHY %d failed\n", i);
			return (err);
		}
	}

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	err = bus_generic_attach(dev);
	if (err != 0)
		return (err);
	
	callout_init_mtx(&sc->callout_tick, &sc->callout_mtx, 0);
	rtl8366rb_tick(sc);
	
	return (err);
}

static int
rtl8366rb_detach(device_t dev)
{
	struct rtl8366rb_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i=0; i < sc->numphys; i++) {
		if (sc->miibus[i])
			device_delete_child(dev, sc->miibus[i]);
		if (sc->ifp[i] != NULL)
			if_free(sc->ifp[i]);
		free(sc->ifname[i], M_DEVBUF);
	}
	bus_generic_detach(dev);
	callout_drain(&sc->callout_tick);
	mtx_destroy(&sc->callout_mtx);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
rtl8366rb_update_ifmedia(int portstatus, u_int *media_status, u_int *media_active)
{
	*media_active = IFM_ETHER;
	*media_status = IFM_AVALID;
	if ((portstatus & RTL8366_PLSR_LINK) != 0)
		*media_status |= IFM_ACTIVE;
	else {
		*media_active |= IFM_NONE;
		return;
	}
	switch (portstatus & RTL8366_PLSR_SPEED_MASK) {
	case RTL8366_PLSR_SPEED_10:
		*media_active |= IFM_10_T;
		break;
	case RTL8366_PLSR_SPEED_100:
		*media_active |= IFM_100_TX;
		break;
	case RTL8366_PLSR_SPEED_1000:
		*media_active |= IFM_1000_T;
		break;
	}
	if ((portstatus & RTL8366_PLSR_FULLDUPLEX) != 0)
		*media_active |= IFM_FDX;
	else
		*media_active |= IFM_HDX;
	if ((portstatus & RTL8366_PLSR_TXPAUSE) != 0)
		*media_active |= IFM_ETH_TXPAUSE;
	if ((portstatus & RTL8366_PLSR_RXPAUSE) != 0)
		*media_active |= IFM_ETH_RXPAUSE;
}

static void
rtl833rb_miipollstat(struct rtl8366rb_softc *sc)
{
	int i;
	struct mii_data *mii;
	struct mii_softc *miisc;
	uint16_t value;
	int portstatus;

	for (i = 0; i < sc->numphys; i++) {
		mii = device_get_softc(sc->miibus[i]);
		if ((i % 2) == 0) {
			if (smi_read(sc->dev, RTL8366_PLSR_BASE + i/2, &value, RTL_NOWAIT) != 0) {
				DEBUG_INCRVAR(callout_blocked);
				return;
			}
			portstatus = value & 0xff;
		} else {
			portstatus = (value >> 8) & 0xff;
		}
		rtl8366rb_update_ifmedia(portstatus, &mii->mii_media_status, &mii->mii_media_active);
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
			if (IFM_INST(mii->mii_media.ifm_cur->ifm_media) != miisc->mii_inst)
				continue;
			mii_phy_update(miisc, MII_POLLSTAT);
		}
	}	
}

static void
rtl8366rb_tick(void *arg)
{
	struct rtl8366rb_softc *sc;

	sc = arg;

	rtl833rb_miipollstat(sc);
	callout_reset(&sc->callout_tick, hz, rtl8366rb_tick, sc);
}

static int
smi_probe(device_t dev)
{
	struct rtl8366rb_softc *sc;
	device_t iicbus, iicha;
	int err, i, j;
	uint16_t chipid;
	char bytes[2];
	int xferd;

	sc = device_get_softc(dev);

	iicbus = device_get_parent(dev);
	iicha = device_get_parent(iicbus);

	for (i = 0; i < 2; ++i) {
		iicbus_reset(iicbus, IIC_FASTEST, RTL8366_IIC_ADDR, NULL);
		for (j=3; j--; ) {
			IICBUS_STOP(iicha);
			/*
			 * we go directly to the host adapter because iicbus.c
			 * only issues a stop on a bus that was successfully started.
			 */
		}
		err = iicbus_request_bus(iicbus, dev, IIC_WAIT);
		if (err != 0)
			goto out;
		err = iicbus_start(iicbus, RTL8366_IIC_ADDR | RTL_IICBUS_READ, RTL_IICBUS_TIMEOUT);
		if (err != 0)
			goto out;
		if (i == 0) {
			bytes[0] = RTL8366RB_CIR & 0xff;
			bytes[1] = (RTL8366RB_CIR >> 8) & 0xff;
		} else {
			bytes[0] = RTL8366SR_CIR & 0xff;
			bytes[1] = (RTL8366SR_CIR >> 8) & 0xff;
		}
		err = iicbus_write(iicbus, bytes, 2, &xferd, RTL_IICBUS_TIMEOUT);
		if (err != 0)
			goto out;
		err = iicbus_read(iicbus, bytes, 2, &xferd, IIC_LAST_READ, 0);
		if (err != 0)
			goto out;
		chipid = ((bytes[1] & 0xff) << 8) | (bytes[0] & 0xff);
		if (i == 0 && chipid == RTL8366RB_CIR_ID8366RB) {
			DPRINTF(dev, "chip id 0x%04x\n", chipid);
			sc->chip_type = RTL8366RB;
			err = 0;
			break;
		}
		if (i == 1 && chipid == RTL8366SR_CIR_ID8366SR) {
			DPRINTF(dev, "chip id 0x%04x\n", chipid);
			sc->chip_type = RTL8366SR;
			err = 0;
			break;
		}
		if (i == 0) {
			iicbus_stop(iicbus);
			iicbus_release_bus(iicbus, dev);
		}
	}
	if (i == 2)
		err = ENXIO;
out:
	iicbus_stop(iicbus);
	iicbus_release_bus(iicbus, dev);
	return (err == 0 ? 0 : ENXIO);
}

static int
smi_acquire(struct rtl8366rb_softc *sc, int sleep)
{
	int r = 0;
	if (sleep == RTL_WAITOK)
		RTL_LOCK(sc);
	else
		if (RTL_TRYLOCK(sc) == 0)
			return (EWOULDBLOCK);
	if (sc->smi_acquired == RTL_SMI_ACQUIRED)
		r = EBUSY;
	else {
		r = iicbus_request_bus(device_get_parent(sc->dev), sc->dev, \
			sleep == RTL_WAITOK ? IIC_WAIT : IIC_DONTWAIT);
		if (r == 0)
			sc->smi_acquired = RTL_SMI_ACQUIRED;
	}
	RTL_UNLOCK(sc);
	return (r);
}

static int
smi_release(struct rtl8366rb_softc *sc, int sleep)
{
	if (sleep == RTL_WAITOK)
		RTL_LOCK(sc);
	else
		if (RTL_TRYLOCK(sc) == 0)
			return (EWOULDBLOCK);
	RTL_SMI_ACQUIRED_ASSERT(sc);
	iicbus_release_bus(device_get_parent(sc->dev), sc->dev);
	sc->smi_acquired = 0;
	RTL_UNLOCK(sc);
	return (0);
}

static int
smi_select(device_t dev, int op, int sleep)
{
	struct rtl8366rb_softc *sc;
	int err, i;
	device_t iicbus;
	struct iicbus_ivar *devi;
	int slave;

	sc = device_get_softc(dev);

	iicbus = device_get_parent(dev);
	devi = IICBUS_IVAR(dev);
	slave = devi->addr;

	RTL_SMI_ACQUIRED_ASSERT((struct rtl8366rb_softc *)device_get_softc(dev));

	if (sc->chip_type == RTL8366SR) {   // RTL8366SR work around
		// this is same work around at probe
		for (int i=3; i--; )
			IICBUS_STOP(device_get_parent(device_get_parent(dev)));
	}
	/*
	 * The chip does not use clock stretching when it is busy,
	 * instead ignoring the command. Retry a few times.
	 */
	for (i = RTL_IICBUS_RETRIES; i--; ) {
		err = iicbus_start(iicbus, slave | op, RTL_IICBUS_TIMEOUT);
		if (err != IIC_ENOACK)
			break;
		if (sleep == RTL_WAITOK) {
			DEBUG_INCRVAR(iic_select_retries);
			pause("smi_select", RTL_IICBUS_RETRY_SLEEP);
		} else
			break;
	}
	return (err);
}

static int
smi_read_locked(struct rtl8366rb_softc *sc, uint16_t addr, uint16_t *data, int sleep)
{
	int err;
	device_t iicbus;
	char bytes[2];
	int xferd;

	iicbus = device_get_parent(sc->dev);

	RTL_SMI_ACQUIRED_ASSERT(sc);
	bytes[0] = addr & 0xff;
	bytes[1] = (addr >> 8) & 0xff;
	err = smi_select(sc->dev, RTL_IICBUS_READ, sleep);
	if (err != 0)
		goto out;
	err = iicbus_write(iicbus, bytes, 2, &xferd, RTL_IICBUS_TIMEOUT);
	if (err != 0)
		goto out;
	err = iicbus_read(iicbus, bytes, 2, &xferd, IIC_LAST_READ, 0);
	if (err != 0)
		goto out;
	*data = ((bytes[1] & 0xff) << 8) | (bytes[0] & 0xff);

out:
	iicbus_stop(iicbus);
	return (err);
}

static int
smi_write_locked(struct rtl8366rb_softc *sc, uint16_t addr, uint16_t data, int sleep)
{
	int err;
	device_t iicbus;
	char bytes[4];
	int xferd;

	iicbus = device_get_parent(sc->dev);

	RTL_SMI_ACQUIRED_ASSERT(sc);
	bytes[0] = addr & 0xff;
	bytes[1] = (addr >> 8) & 0xff;
	bytes[2] = data & 0xff;
	bytes[3] = (data >> 8) & 0xff;

	err = smi_select(sc->dev, RTL_IICBUS_WRITE, sleep);
	if (err == 0)
		err = iicbus_write(iicbus, bytes, 4, &xferd, RTL_IICBUS_TIMEOUT);
	iicbus_stop(iicbus);

	return (err);
}

static int
smi_read(device_t dev, uint16_t addr, uint16_t *data, int sleep)
{
	struct rtl8366rb_softc *sc;
	int err;

	sc = device_get_softc(dev);

	err = smi_acquire(sc, sleep);
	if (err != 0)
		return (EBUSY);
	err = smi_read_locked(sc, addr, data, sleep);
	smi_release(sc, sleep);
	DEVERR(dev, err, "smi_read()=%d: addr=%04x\n", addr);
	return (err == 0 ? 0 : EIO);
}

static int
smi_write(device_t dev, uint16_t addr, uint16_t data, int sleep)
{
	struct rtl8366rb_softc *sc;
	int err;
	
	sc = device_get_softc(dev);

	err = smi_acquire(sc, sleep);
	if (err != 0)
		return (EBUSY);
	err = smi_write_locked(sc, addr, data, sleep);
	smi_release(sc, sleep);
	DEVERR(dev, err, "smi_write()=%d: addr=%04x\n", addr);
	return (err == 0 ? 0 : EIO);
}

static int
smi_rmw(device_t dev, uint16_t addr, uint16_t mask, uint16_t data, int sleep)
{
	struct rtl8366rb_softc *sc;
	int err;
	uint16_t oldv, newv;
	
	sc = device_get_softc(dev);

	err = smi_acquire(sc, sleep);
	if (err != 0)
		return (EBUSY);
	if (err == 0) {
		err = smi_read_locked(sc, addr, &oldv, sleep);
		if (err == 0) {
			newv = oldv & ~mask;
			newv |= data & mask;
			if (newv != oldv)
				err = smi_write_locked(sc, addr, newv, sleep);
		}
	}
	smi_release(sc, sleep);
	DEVERR(dev, err, "smi_rmw()=%d: addr=%04x\n", addr);
	return (err == 0 ? 0 : EIO);
}

static etherswitch_info_t *
rtl_getinfo(device_t dev)
{
	struct rtl8366rb_softc *sc;

	sc = device_get_softc(dev);

	return (&sc->info);
}

static int
rtl_readreg(device_t dev, int reg)
{
	uint16_t data;

	data = 0;

	smi_read(dev, reg, &data, RTL_WAITOK);
	return (data);
}

static int
rtl_writereg(device_t dev, int reg, int value)
{
	return (smi_write(dev, reg, value, RTL_WAITOK));
}

static int
rtl_getport(device_t dev, etherswitch_port_t *p)
{
	struct rtl8366rb_softc *sc;
	struct ifmedia *ifm;
	struct mii_data *mii;
	struct ifmediareq *ifmr;
	uint16_t v;
	int err, vlangroup;
	
	sc = device_get_softc(dev);

	ifmr = &p->es_ifmr;

	if (p->es_port < 0 || p->es_port >= (sc->numphys + 1))
		return (ENXIO);
	if (sc->phy4cpu && p->es_port == sc->numphys) {
		vlangroup = RTL8366_PVCR_GET(p->es_port + 1,
		    rtl_readreg(dev, RTL8366_PVCR_REG(p->es_port + 1)));
	} else {
		vlangroup = RTL8366_PVCR_GET(p->es_port,
		    rtl_readreg(dev, RTL8366_PVCR_REG(p->es_port)));
	}
	p->es_pvid = sc->vid[vlangroup] & ETHERSWITCH_VID_MASK;
	
	if (p->es_port < sc->numphys) {
		mii = device_get_softc(sc->miibus[p->es_port]);
		ifm = &mii->mii_media;
		err = ifmedia_ioctl(sc->ifp[p->es_port], &p->es_ifr, ifm, SIOCGIFMEDIA);
		if (err)
			return (err);
	} else {
		/* fill in fixed values for CPU port */
		p->es_flags |= ETHERSWITCH_PORT_CPU;
		smi_read(dev, RTL8366_PLSR_BASE + (RTL8366_NUM_PHYS)/2, &v, RTL_WAITOK);
		v = v >> (8 * ((RTL8366_NUM_PHYS) % 2));
		rtl8366rb_update_ifmedia(v, &ifmr->ifm_status, &ifmr->ifm_active);
		ifmr->ifm_current = ifmr->ifm_active;
		ifmr->ifm_mask = 0;
		ifmr->ifm_status = IFM_ACTIVE | IFM_AVALID;
		/* Return our static media list. */
		if (ifmr->ifm_count > 0) {
			ifmr->ifm_count = 1;
			ifmr->ifm_ulist[0] = IFM_MAKEWORD(IFM_ETHER, IFM_1000_T,
			    IFM_FDX, 0);
		} else
			ifmr->ifm_count = 0;
	}
	return (0);
}

static int
rtl_setport(device_t dev, etherswitch_port_t *p)
{
	struct rtl8366rb_softc *sc;
	int i, err, vlangroup;
	struct ifmedia *ifm;
	struct mii_data *mii;
	int port;

	sc = device_get_softc(dev);

	if (p->es_port < 0 || p->es_port >= (sc->numphys + 1))
		return (ENXIO);
	vlangroup = -1;
	for (i = 0; i < RTL8366_NUM_VLANS; i++) {
		if ((sc->vid[i] & ETHERSWITCH_VID_MASK) == p->es_pvid) {
			vlangroup = i;
			break;
		}
	}
	if (vlangroup == -1)
		return (ENXIO);
	if (sc->phy4cpu && p->es_port == sc->numphys) {
		port = p->es_port + 1;
	} else {
		port = p->es_port;
	}
	err = smi_rmw(dev, RTL8366_PVCR_REG(port),
	    RTL8366_PVCR_VAL(port, RTL8366_PVCR_PORT_MASK),
	    RTL8366_PVCR_VAL(port, vlangroup), RTL_WAITOK);
	if (err)
		return (err);
	/* CPU Port */
	if (p->es_port == sc->numphys)
		return (0);
	mii = device_get_softc(sc->miibus[p->es_port]);
	ifm = &mii->mii_media;
	err = ifmedia_ioctl(sc->ifp[p->es_port], &p->es_ifr, ifm, SIOCSIFMEDIA);
	return (err);
}

static int
rtl_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct rtl8366rb_softc *sc;
	uint16_t vmcr[3];
	int i;
	int member, untagged;
	
	sc = device_get_softc(dev);

	for (i=0; i<RTL8366_VMCR_MULT; i++)
		vmcr[i] = rtl_readreg(dev, RTL8366_VMCR(i, vg->es_vlangroup));
		
	vg->es_vid = sc->vid[vg->es_vlangroup];
	member = RTL8366_VMCR_MEMBER(vmcr);
	untagged = RTL8366_VMCR_UNTAG(vmcr);
	if (sc->phy4cpu) {
		vg->es_member_ports = ((member & 0x20) >> 1) | (member & 0x0f);
		vg->es_untagged_ports = ((untagged & 0x20) >> 1) | (untagged & 0x0f);
	} else {
		vg->es_member_ports = member;
		vg->es_untagged_ports = untagged;
	}
	vg->es_fid = RTL8366_VMCR_FID(vmcr);
	return (0);
}

static int
rtl_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct rtl8366rb_softc *sc;
	int g;
	int member, untagged;

	sc = device_get_softc(dev);

	g = vg->es_vlangroup;

	sc->vid[g] = vg->es_vid;
	/* VLAN group disabled ? */
	if (vg->es_member_ports == 0 && vg->es_untagged_ports == 0 && vg->es_vid == 0)
		return (0);
	sc->vid[g] |= ETHERSWITCH_VID_VALID;
	rtl_writereg(dev, RTL8366_VMCR(RTL8366_VMCR_DOT1Q_REG, g),
		(vg->es_vid << RTL8366_VMCR_DOT1Q_VID_SHIFT) & RTL8366_VMCR_DOT1Q_VID_MASK);
	if (sc->phy4cpu) {
		/* add space at phy4 */
		member = (vg->es_member_ports & 0x0f) |
		    ((vg->es_member_ports & 0x10) << 1);
		untagged = (vg->es_untagged_ports & 0x0f) |
		    ((vg->es_untagged_ports & 0x10) << 1);
	} else {
		member = vg->es_member_ports;
		untagged = vg->es_untagged_ports;
	}
	if (sc->chip_type == RTL8366RB) {
		rtl_writereg(dev, RTL8366_VMCR(RTL8366_VMCR_MU_REG, g),
	 	    ((member << RTL8366_VMCR_MU_MEMBER_SHIFT) & RTL8366_VMCR_MU_MEMBER_MASK) |
		    ((untagged << RTL8366_VMCR_MU_UNTAG_SHIFT) & RTL8366_VMCR_MU_UNTAG_MASK));
		rtl_writereg(dev, RTL8366_VMCR(RTL8366_VMCR_FID_REG, g),
		    vg->es_fid);
	} else {
		rtl_writereg(dev, RTL8366_VMCR(RTL8366_VMCR_MU_REG, g),
		    ((member << RTL8366_VMCR_MU_MEMBER_SHIFT) & RTL8366_VMCR_MU_MEMBER_MASK) |
		    ((untagged << RTL8366_VMCR_MU_UNTAG_SHIFT) & RTL8366_VMCR_MU_UNTAG_MASK) |
		    ((vg->es_fid << RTL8366_VMCR_FID_FID_SHIFT) & RTL8366_VMCR_FID_FID_MASK));
	}
	return (0);
}

static int
rtl_getconf(device_t dev, etherswitch_conf_t *conf)
{

	/* Return the VLAN mode. */
	conf->cmd = ETHERSWITCH_CONF_VLAN_MODE;
	conf->vlan_mode = ETHERSWITCH_VLAN_DOT1Q;

	return (0);
}

static int
rtl_readphy(device_t dev, int phy, int reg)
{
	struct rtl8366rb_softc *sc;
	uint16_t data;
	int err, i, sleep;

	sc = device_get_softc(dev);

	data = 0;

	if (phy < 0 || phy >= RTL8366_NUM_PHYS)
		return (ENXIO);
	if (reg < 0 || reg >= RTL8366_NUM_PHY_REG)
		return (ENXIO);
	sleep = RTL_WAITOK;
	err = smi_acquire(sc, sleep);
	if (err != 0)
		return (EBUSY);
	for (i = RTL_IICBUS_RETRIES; i--; ) {
		err = smi_write_locked(sc, RTL8366_PACR, RTL8366_PACR_READ, sleep);
		if (err == 0)
			err = smi_write_locked(sc, RTL8366_PHYREG(phy, 0, reg), 0, sleep);
		if (err == 0) {
			err = smi_read_locked(sc, RTL8366_PADR, &data, sleep);
			break;
		}
		DEBUG_INCRVAR(phy_access_retries);
		DPRINTF(dev, "rtl_readphy(): chip not responsive, retrying %d more times\n", i);
		pause("rtl_readphy", RTL_IICBUS_RETRY_SLEEP);
	}
	smi_release(sc, sleep);
	DEVERR(dev, err, "rtl_readphy()=%d: phy=%d.%02x\n", phy, reg);
	return (data);
}

static int
rtl_writephy(device_t dev, int phy, int reg, int data)
{
	struct rtl8366rb_softc *sc;
	int err, i, sleep;
	
	sc = device_get_softc(dev);

	if (phy < 0 || phy >= RTL8366_NUM_PHYS)
		return (ENXIO);
	if (reg < 0 || reg >= RTL8366_NUM_PHY_REG)
		return (ENXIO);
	sleep = RTL_WAITOK;
	err = smi_acquire(sc, sleep);
	if (err != 0)
		return (EBUSY);
	for (i = RTL_IICBUS_RETRIES; i--; ) {
		err = smi_write_locked(sc, RTL8366_PACR, RTL8366_PACR_WRITE, sleep);
		if (err == 0)
			err = smi_write_locked(sc, RTL8366_PHYREG(phy, 0, reg), data, sleep);
		if (err == 0) {
			break;
		}
		DEBUG_INCRVAR(phy_access_retries);
		DPRINTF(dev, "rtl_writephy(): chip not responsive, retrying %d more tiems\n", i);
		pause("rtl_writephy", RTL_IICBUS_RETRY_SLEEP);
	}
	smi_release(sc, sleep);
	DEVERR(dev, err, "rtl_writephy()=%d: phy=%d.%02x\n", phy, reg);
	return (err == 0 ? 0 : EIO);
}

static int
rtl8366rb_ifmedia_upd(struct ifnet *ifp)
{
	struct rtl8366rb_softc *sc;
	struct mii_data *mii;
	
	sc = ifp->if_softc;
	mii = device_get_softc(sc->miibus[ifp->if_dunit]);
	
	mii_mediachg(mii);
	return (0);
}

static void
rtl8366rb_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rtl8366rb_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->miibus[ifp->if_dunit]);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}


static device_method_t rtl8366rb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	rtl8366rb_identify),
	DEVMETHOD(device_probe,		rtl8366rb_probe),
	DEVMETHOD(device_attach,	rtl8366rb_attach),
	DEVMETHOD(device_detach,	rtl8366rb_detach),
	
	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MII interface */
	DEVMETHOD(miibus_readreg,	rtl_readphy),
	DEVMETHOD(miibus_writereg,	rtl_writephy),

	/* MDIO interface */
	DEVMETHOD(mdio_readreg,         rtl_readphy),
	DEVMETHOD(mdio_writereg,        rtl_writephy),

	/* etherswitch interface */
	DEVMETHOD(etherswitch_getconf,	rtl_getconf),
	DEVMETHOD(etherswitch_getinfo,	rtl_getinfo),
	DEVMETHOD(etherswitch_readreg,	rtl_readreg),
	DEVMETHOD(etherswitch_writereg,	rtl_writereg),
	DEVMETHOD(etherswitch_readphyreg,	rtl_readphy),
	DEVMETHOD(etherswitch_writephyreg,	rtl_writephy),
	DEVMETHOD(etherswitch_getport,	rtl_getport),
	DEVMETHOD(etherswitch_setport,	rtl_setport),
	DEVMETHOD(etherswitch_getvgroup,	rtl_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	rtl_setvgroup),

	DEVMETHOD_END
};

DEFINE_CLASS_0(rtl8366rb, rtl8366rb_driver, rtl8366rb_methods,
    sizeof(struct rtl8366rb_softc));
static devclass_t rtl8366rb_devclass;

DRIVER_MODULE(rtl8366rb, iicbus, rtl8366rb_driver, rtl8366rb_devclass, 0, 0);
DRIVER_MODULE(miibus, rtl8366rb, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, rtl8366rb, mdio_driver, mdio_devclass, 0, 0);
DRIVER_MODULE(etherswitch, rtl8366rb, etherswitch_driver, etherswitch_devclass, 0, 0);
MODULE_VERSION(rtl8366rb, 1);
MODULE_DEPEND(rtl8366rb, iicbus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(rtl8366rb, miibus, 1, 1, 1); /* XXX which versions? */
MODULE_DEPEND(rtl8366rb, etherswitch, 1, 1, 1); /* XXX which versions? */
