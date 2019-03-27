/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#include <sys/rman.h>
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
#include <dev/etherswitch/mtkswitch/mtkswitch_rt3050.h>

static int
mtkswitch_reg_read(device_t dev, int reg)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	uint32_t val;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	val = MTKSWITCH_READ(sc, MTKSWITCH_REG32(reg));
	if (MTKSWITCH_IS_HI16(reg))
		return (MTKSWITCH_HI16(val));
	return (MTKSWITCH_LO16(val));
}

static int
mtkswitch_reg_write(device_t dev, int reg, int val)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	uint32_t tmp;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	tmp = MTKSWITCH_READ(sc, MTKSWITCH_REG32(reg));
	if (MTKSWITCH_IS_HI16(reg)) {
		tmp &= MTKSWITCH_LO16_MSK;
		tmp |= MTKSWITCH_TO_HI16(val);
	} else {
		tmp &= MTKSWITCH_HI16_MSK;
		tmp |= MTKSWITCH_TO_LO16(val);
	}
	MTKSWITCH_WRITE(sc, MTKSWITCH_REG32(reg), tmp);

	return (0);
}

static int
mtkswitch_phy_read(device_t dev, int phy, int reg)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	int val;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	while (MTKSWITCH_READ(sc, MTKSWITCH_PCR0) & PCR0_ACTIVE);
	MTKSWITCH_WRITE(sc, MTKSWITCH_PCR0, PCR0_READ | PCR0_REG(reg) |
	    PCR0_PHY(phy));
	while (MTKSWITCH_READ(sc, MTKSWITCH_PCR0) & PCR0_ACTIVE);
	val = (MTKSWITCH_READ(sc, MTKSWITCH_PCR1) >> PCR1_DATA_OFF) &
	    PCR1_DATA_MASK;
	MTKSWITCH_UNLOCK(sc);
	return (val);
}

static int
mtkswitch_phy_write(device_t dev, int phy, int reg, int val)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	while (MTKSWITCH_READ(sc, MTKSWITCH_PCR0) & PCR0_ACTIVE);
	MTKSWITCH_WRITE(sc, MTKSWITCH_PCR0, PCR0_WRITE | PCR0_REG(reg) |
	    PCR0_PHY(phy) | PCR0_DATA(val));
	while (MTKSWITCH_READ(sc, MTKSWITCH_PCR0) & PCR0_ACTIVE);
	MTKSWITCH_UNLOCK(sc);
	return (0);
}

static int
mtkswitch_reset(struct mtkswitch_softc *sc)
{

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	MTKSWITCH_WRITE(sc, MTKSWITCH_STRT, STRT_RESET);
	while (MTKSWITCH_READ(sc, MTKSWITCH_STRT) != 0);
	MTKSWITCH_UNLOCK(sc);

	return (0);
}

static int
mtkswitch_hw_setup(struct mtkswitch_softc *sc)
{

	/*
	 * TODO: parse the device tree and see if we need to configure
	 *       ports, etc. differently. For now we fallback to defaults.
	 */

	/* Called early and hence unlocked */
	/* Set ports 0-4 to auto negotiation */
	MTKSWITCH_WRITE(sc, MTKSWITCH_FPA, FPA_ALL_AUTO);

	return (0);
}

static int
mtkswitch_hw_global_setup(struct mtkswitch_softc *sc)
{

	/* Called early and hence unlocked */
	return (0);
}

static void
mtkswitch_port_init(struct mtkswitch_softc *sc, int port)
{
	/* Called early and hence unlocked */
	/* Do nothing - ports are set to auto negotiation in hw_setup */
}

static uint32_t
mtkswitch_get_port_status(struct mtkswitch_softc *sc, int port)
{
	uint32_t val, res;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	res = 0;
	val = MTKSWITCH_READ(sc, MTKSWITCH_POA);

	if (val & POA_PRT_LINK(port))
		res |= MTKSWITCH_LINK_UP;
	if (val & POA_PRT_DPX(port))
		res |= MTKSWITCH_DUPLEX;

	if (MTKSWITCH_PORT_IS_100M(port)) {
		if (val & POA_FE_SPEED(port))
			res |= MTKSWITCH_SPEED_100;
		if (val & POA_FE_XFC(port))
			res |= (MTKSWITCH_TXFLOW | MTKSWITCH_RXFLOW);
	} else {
		switch (POA_GE_SPEED(val, port)) {
		case POA_GE_SPEED_10:
			res |= MTKSWITCH_SPEED_10;
			break;
		case POA_GE_SPEED_100:
			res |= MTKSWITCH_SPEED_100;
			break;
		case POA_GE_SPEED_1000:
			res |= MTKSWITCH_SPEED_1000;
			break;
		}

		val = POA_GE_XFC(val, port);
		if (val & POA_GE_XFC_TX_MSK)
			res |= MTKSWITCH_TXFLOW;
		if (val & POA_GE_XFC_RX_MSK)
			res |= MTKSWITCH_RXFLOW;
	}

	return (res);
}

static int
mtkswitch_atu_flush(struct mtkswitch_softc *sc)
{
	return (0);
}

static int
mtkswitch_port_vlan_setup(struct mtkswitch_softc *sc, etherswitch_port_t *p)
{
	uint32_t val;
	int err, invert = 0;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	/* Set the PVID. */
	if (p->es_pvid != 0) {
		err = sc->hal.mtkswitch_vlan_set_pvid(sc, p->es_port,
		    p->es_pvid);
		if (err != 0) {
			MTKSWITCH_UNLOCK(sc);
			return (err);
		}
	}

	/* Mutually exclusive */
	if (p->es_flags & ETHERSWITCH_PORT_ADDTAG &&
	    p->es_flags & ETHERSWITCH_PORT_STRIPTAG) {
		invert = 1;
	}

	val = MTKSWITCH_READ(sc, MTKSWITCH_SGC2);
	if (p->es_flags & ETHERSWITCH_PORT_DOUBLE_TAG)
		val |= SGC2_DOUBLE_TAG_PORT(p->es_port);
	else
		val &= ~SGC2_DOUBLE_TAG_PORT(p->es_port);
	MTKSWITCH_WRITE(sc, MTKSWITCH_SGC2, val);

	val = MTKSWITCH_READ(sc, MTKSWITCH_POC2);
	if (invert) {
		if (val & POC2_UNTAG_PORT(p->es_port))
			val &= ~POC2_UNTAG_PORT(p->es_port);
		else
			val |= POC2_UNTAG_PORT(p->es_port);
	} else if (p->es_flags & ETHERSWITCH_PORT_STRIPTAG)
		val |= POC2_UNTAG_PORT(p->es_port);
	else
		val &= ~POC2_UNTAG_PORT(p->es_port);
	MTKSWITCH_WRITE(sc, MTKSWITCH_POC2, val);
	MTKSWITCH_UNLOCK(sc);

	return (0);
}

static int
mtkswitch_port_vlan_get(struct mtkswitch_softc *sc, etherswitch_port_t *p)
{
	uint32_t val;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);

	/* Retrieve the PVID */
	sc->hal.mtkswitch_vlan_get_pvid(sc, p->es_port, &p->es_pvid);

	/* Port flags */
	p->es_flags = 0;
	val = MTKSWITCH_READ(sc, MTKSWITCH_SGC2);
	if (val & SGC2_DOUBLE_TAG_PORT(p->es_port))
		p->es_flags |= ETHERSWITCH_PORT_DOUBLE_TAG;

	val = MTKSWITCH_READ(sc, MTKSWITCH_POC2);
	if (val & POC2_UNTAG_PORT(p->es_port))
		p->es_flags |= ETHERSWITCH_PORT_STRIPTAG;
	else
		p->es_flags |= ETHERSWITCH_PORT_ADDTAG;

	MTKSWITCH_UNLOCK(sc);

	return (0);
}

static void
mtkswitch_vlan_init_hw(struct mtkswitch_softc *sc)
{
	uint32_t val, vid;
	int i;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);

	/* Reset everything to defaults first */
	for (i = 0; i < sc->info.es_nvlangroups; i++) {
		/* Remove all VLAN members and untag info, if any */
		if (i % 4 == 0) {
			MTKSWITCH_WRITE(sc, MTKSWITCH_VMSC(i), 0);
			if (sc->sc_switchtype != MTK_SWITCH_RT3050)
				MTKSWITCH_WRITE(sc, MTKSWITCH_VUB(i), 0);
		}
		/* Reset to default VIDs */
		val = MTKSWITCH_READ(sc, MTKSWITCH_VLANI(i));
		val &= ~(VLANI_MASK << VLANI_OFF(i));
		val |= ((i + 1) << VLANI_OFF(i));
		MTKSWITCH_WRITE(sc, MTKSWITCH_VLANI(i), val);
	}

	/* Now, add all ports as untagged members to VLAN1 */
	vid = 0;
	val = MTKSWITCH_READ(sc, MTKSWITCH_VMSC(vid));
	val &= ~(VMSC_MASK << VMSC_OFF(vid));
	val |= (((1<<sc->numports)-1) << VMSC_OFF(vid));
	MTKSWITCH_WRITE(sc, MTKSWITCH_VMSC(vid), val);
	if (sc->sc_switchtype != MTK_SWITCH_RT3050) {
		val = MTKSWITCH_READ(sc, MTKSWITCH_VUB(vid));
		val &= ~(VUB_MASK << VUB_OFF(vid));
		val |= (((1<<sc->numports)-1) << VUB_OFF(vid));
		MTKSWITCH_WRITE(sc, MTKSWITCH_VUB(vid), val);
	}
	val = MTKSWITCH_READ(sc, MTKSWITCH_POC2);
	if (sc->sc_switchtype != MTK_SWITCH_RT3050)
		val |= POC2_UNTAG_VLAN;
	val |= ((1<<sc->numports)-1);
	MTKSWITCH_WRITE(sc, MTKSWITCH_POC2, val);

	/* only the first vlangroup is valid */
	sc->valid_vlans = (1<<0);

	/* Set all port PVIDs to 1 */
	vid = 1;
	for (i = 0; i < sc->info.es_nports; i++) {
		val = MTKSWITCH_READ(sc, MTKSWITCH_PVID(i));
		val &= ~(PVID_MASK << PVID_OFF(i));
		val |= (vid << PVID_OFF(i));
		MTKSWITCH_WRITE(sc, MTKSWITCH_PVID(i), val);
	}

	MTKSWITCH_UNLOCK(sc);
}

static int
mtkswitch_vlan_getvgroup(struct mtkswitch_softc *sc, etherswitch_vlangroup_t *v)
{
	uint32_t val;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);

	if ((sc->vlan_mode != ETHERSWITCH_VLAN_DOT1Q) ||
	    (v->es_vlangroup > sc->info.es_nvlangroups))
		return (EINVAL);

	/* Reset the member ports. */
	v->es_untagged_ports = 0;
	v->es_member_ports = 0;

	/* Not supported */
	v->es_fid = 0;

	/* Vlan ID */
	v->es_vid = 0;
	if ((sc->valid_vlans & (1<<v->es_vlangroup)) == 0)
		return (0);

	MTKSWITCH_LOCK(sc);
	v->es_vid = (MTKSWITCH_READ(sc, MTKSWITCH_VLANI(v->es_vlangroup)) >>
	    VLANI_OFF(v->es_vlangroup)) & VLANI_MASK;
	v->es_vid |= ETHERSWITCH_VID_VALID;

	/* Member ports */
	v->es_member_ports = v->es_untagged_ports =
	    (MTKSWITCH_READ(sc, MTKSWITCH_VMSC(v->es_vlangroup)) >>
	    VMSC_OFF(v->es_vlangroup)) & VMSC_MASK;

	val = MTKSWITCH_READ(sc, MTKSWITCH_POC2);

	if ((val & POC2_UNTAG_VLAN) && sc->sc_switchtype != MTK_SWITCH_RT3050) {
		val = (MTKSWITCH_READ(sc, MTKSWITCH_VUB(v->es_vlangroup)) >>
		    VUB_OFF(v->es_vlangroup)) & VUB_MASK;
	} else {
		val &= VUB_MASK;
	}
	v->es_untagged_ports &= val;

	MTKSWITCH_UNLOCK(sc);
	return (0);
}

static int
mtkswitch_vlan_setvgroup(struct mtkswitch_softc *sc, etherswitch_vlangroup_t *v)
{
	uint32_t val, tmp;

	if ((sc->vlan_mode != ETHERSWITCH_VLAN_DOT1Q) ||
	    (v->es_vlangroup > sc->info.es_nvlangroups))
		return (EINVAL);

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	/* First, see if we can accomodate the request at all */
	val = MTKSWITCH_READ(sc, MTKSWITCH_POC2);
	if (sc->sc_switchtype == MTK_SWITCH_RT3050 ||
	    (val & POC2_UNTAG_VLAN) == 0) {
		/*
		 * There are 2 things we can't support in per-port untagging
		 * mode:
		 * 1. Adding a port as an untagged member if the port is not
		 *    set up to do untagging.
		 * 2. Adding a port as a tagged member if the port is set up
		 *    to do untagging.
		 */
		val &= VUB_MASK;

		/* get all untagged members from the member list */
		tmp = v->es_untagged_ports & v->es_member_ports;
		/* fail if untagged members are not a subset of all members */
		if (tmp != v->es_untagged_ports) {
			/* Cannot accomodate request */
			MTKSWITCH_UNLOCK(sc);
			return (ENOTSUP);
		}

		/* fail if any untagged member is set up to do tagging */
		if ((tmp & val) != tmp) {
			/* Cannot accomodate request */
			MTKSWITCH_UNLOCK(sc);
			return (ENOTSUP);
		}

		/* now, get the list of all tagged members */
		tmp = v->es_member_ports & ~tmp;
		/* fail if any tagged member is set up to do untagging */
		if ((tmp & val) != 0) {
			/* Cannot accomodate request */
			MTKSWITCH_UNLOCK(sc);
			return (ENOTSUP);
		}
	} else {
		/* Prefer per-Vlan untag and set its members */
		val = MTKSWITCH_READ(sc, MTKSWITCH_VUB(v->es_vlangroup));
		val &= ~(VUB_MASK << VUB_OFF(v->es_vlangroup));
		val |= (((v->es_untagged_ports) & VUB_MASK) <<
		    VUB_OFF(v->es_vlangroup));
		MTKSWITCH_WRITE(sc, MTKSWITCH_VUB(v->es_vlangroup), val);
	}

	/* Set VID */
	val = MTKSWITCH_READ(sc, MTKSWITCH_VLANI(v->es_vlangroup));
	val &= ~(VLANI_MASK << VLANI_OFF(v->es_vlangroup));
	val |= (v->es_vid & VLANI_MASK) << VLANI_OFF(v->es_vlangroup);
	MTKSWITCH_WRITE(sc, MTKSWITCH_VLANI(v->es_vlangroup), val);

	/* Set members */
	val = MTKSWITCH_READ(sc, MTKSWITCH_VMSC(v->es_vlangroup));
	val &= ~(VMSC_MASK << VMSC_OFF(v->es_vlangroup));
	val |= (v->es_member_ports << VMSC_OFF(v->es_vlangroup));
	MTKSWITCH_WRITE(sc, MTKSWITCH_VMSC(v->es_vlangroup), val);

	sc->valid_vlans |= (1<<v->es_vlangroup);

	MTKSWITCH_UNLOCK(sc);
	return (0);
}

static int
mtkswitch_vlan_get_pvid(struct mtkswitch_softc *sc, int port, int *pvid)
{

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	*pvid = (MTKSWITCH_READ(sc, MTKSWITCH_PVID(port)) >> PVID_OFF(port)) &
	    PVID_MASK;

	return (0); 
}

static int
mtkswitch_vlan_set_pvid(struct mtkswitch_softc *sc, int port, int pvid)
{
	uint32_t val;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	val = MTKSWITCH_READ(sc, MTKSWITCH_PVID(port));
	val &= ~(PVID_MASK << PVID_OFF(port));
	val |= (pvid & PVID_MASK) << PVID_OFF(port);
	MTKSWITCH_WRITE(sc, MTKSWITCH_PVID(port), val);
	
	return (0);
}

extern void
mtk_attach_switch_rt3050(struct mtkswitch_softc *sc)
{

	sc->portmap = 0x7f;
	sc->phymap = 0x1f;

	sc->info.es_nports = 7;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_DOT1Q;
	sc->info.es_nvlangroups = 16;
	sprintf(sc->info.es_name, "Ralink ESW");

	sc->hal.mtkswitch_reset = mtkswitch_reset;
	sc->hal.mtkswitch_hw_setup = mtkswitch_hw_setup;
	sc->hal.mtkswitch_hw_global_setup = mtkswitch_hw_global_setup;
	sc->hal.mtkswitch_port_init = mtkswitch_port_init;
	sc->hal.mtkswitch_get_port_status = mtkswitch_get_port_status;
	sc->hal.mtkswitch_atu_flush = mtkswitch_atu_flush;
	sc->hal.mtkswitch_port_vlan_setup = mtkswitch_port_vlan_setup;
	sc->hal.mtkswitch_port_vlan_get = mtkswitch_port_vlan_get;
	sc->hal.mtkswitch_vlan_init_hw = mtkswitch_vlan_init_hw;
	sc->hal.mtkswitch_vlan_getvgroup = mtkswitch_vlan_getvgroup;
	sc->hal.mtkswitch_vlan_setvgroup = mtkswitch_vlan_setvgroup;
	sc->hal.mtkswitch_vlan_get_pvid = mtkswitch_vlan_get_pvid;
	sc->hal.mtkswitch_vlan_set_pvid = mtkswitch_vlan_set_pvid;
	sc->hal.mtkswitch_phy_read = mtkswitch_phy_read;
	sc->hal.mtkswitch_phy_write = mtkswitch_phy_write;
	sc->hal.mtkswitch_reg_read = mtkswitch_reg_read;
	sc->hal.mtkswitch_reg_write = mtkswitch_reg_write;
}
