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
#include <dev/etherswitch/mtkswitch/mtkswitch_mt7620.h>

static int
mtkswitch_phy_read_locked(struct mtkswitch_softc *sc, int phy, int reg)
{
	uint32_t data;
        
	MTKSWITCH_WRITE(sc, MTKSWITCH_PIAC, PIAC_PHY_ACS_ST | PIAC_MDIO_ST |
	    (reg << PIAC_MDIO_REG_ADDR_OFF) | (phy << PIAC_MDIO_PHY_ADDR_OFF) |
	    PIAC_MDIO_CMD_READ);
	while ((data = MTKSWITCH_READ(sc, MTKSWITCH_PIAC)) & PIAC_PHY_ACS_ST);
        
	return ((int)(data & PIAC_MDIO_RW_DATA_MASK));
}

static int
mtkswitch_phy_read(device_t dev, int phy, int reg)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	int data;

	if ((phy < 0 || phy >= 32) || (reg < 0 || reg >= 32))
		return (ENXIO);

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	data = mtkswitch_phy_read_locked(sc, phy, reg);
	MTKSWITCH_UNLOCK(sc);

	return (data);
}

static int
mtkswitch_phy_write_locked(struct mtkswitch_softc *sc, int phy, int reg,
    int val)
{

	MTKSWITCH_WRITE(sc, MTKSWITCH_PIAC, PIAC_PHY_ACS_ST | PIAC_MDIO_ST |
	    (reg << PIAC_MDIO_REG_ADDR_OFF) | (phy << PIAC_MDIO_PHY_ADDR_OFF) |
	    (val & PIAC_MDIO_RW_DATA_MASK) | PIAC_MDIO_CMD_WRITE);
	while (MTKSWITCH_READ(sc, MTKSWITCH_PIAC) & PIAC_PHY_ACS_ST);

	return (0);
}

static int
mtkswitch_phy_write(device_t dev, int phy, int reg, int val)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	int res;

	if ((phy < 0 || phy >= 32) || (reg < 0 || reg >= 32))
		return (ENXIO);

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	res = mtkswitch_phy_write_locked(sc, phy, reg, val);
	MTKSWITCH_UNLOCK(sc);

	return (res);
}

static uint32_t
mtkswitch_reg_read32(struct mtkswitch_softc *sc, int reg)
{

	return (MTKSWITCH_READ(sc, reg));
}

static uint32_t
mtkswitch_reg_write32(struct mtkswitch_softc *sc, int reg, uint32_t val)
{

	MTKSWITCH_WRITE(sc, reg, val);
	return (0);
}

static uint32_t
mtkswitch_reg_read32_mt7621(struct mtkswitch_softc *sc, int reg)
{
	uint32_t low, hi;

	mtkswitch_phy_write_locked(sc, MTKSWITCH_GLOBAL_PHY,
	    MTKSWITCH_GLOBAL_REG, MTKSWITCH_REG_ADDR(reg));
	low = mtkswitch_phy_read_locked(sc, MTKSWITCH_GLOBAL_PHY,
	    MTKSWITCH_REG_LO(reg));
	hi = mtkswitch_phy_read_locked(sc, MTKSWITCH_GLOBAL_PHY,
	    MTKSWITCH_REG_HI(reg));;
	return (low | (hi << 16));
}

static uint32_t
mtkswitch_reg_write32_mt7621(struct mtkswitch_softc *sc, int reg, uint32_t val)
{

	mtkswitch_phy_write_locked(sc, MTKSWITCH_GLOBAL_PHY,
	    MTKSWITCH_GLOBAL_REG, MTKSWITCH_REG_ADDR(reg));
	mtkswitch_phy_write_locked(sc, MTKSWITCH_GLOBAL_PHY,
	    MTKSWITCH_REG_LO(reg), MTKSWITCH_VAL_LO(val));
	mtkswitch_phy_write_locked(sc, MTKSWITCH_GLOBAL_PHY,
	    MTKSWITCH_REG_HI(reg), MTKSWITCH_VAL_HI(val));
	return (0);
}

static int
mtkswitch_reg_read(device_t dev, int reg)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	uint32_t val;

	val = sc->hal.mtkswitch_read(sc, MTKSWITCH_REG32(reg));
	if (MTKSWITCH_IS_HI16(reg))
		return (MTKSWITCH_HI16(val));
	return (MTKSWITCH_LO16(val));
}

static int
mtkswitch_reg_write(device_t dev, int reg, int val)
{
	struct mtkswitch_softc *sc = device_get_softc(dev);
	uint32_t tmp;

	tmp = sc->hal.mtkswitch_read(sc, MTKSWITCH_REG32(reg));
	if (MTKSWITCH_IS_HI16(reg)) {
		tmp &= MTKSWITCH_LO16_MSK;
		tmp |= MTKSWITCH_TO_HI16(val);
	} else {
		tmp &= MTKSWITCH_HI16_MSK;
		tmp |= MTKSWITCH_TO_LO16(val);
	}
	sc->hal.mtkswitch_write(sc, MTKSWITCH_REG32(reg), tmp);

	return (0);
}

static int
mtkswitch_reset(struct mtkswitch_softc *sc)
{

	/* We don't reset the switch for now */
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
	return (0);
}

static int
mtkswitch_hw_global_setup(struct mtkswitch_softc *sc)
{
	/* Currently does nothing */

	/* Called early and hence unlocked */
	return (0);
}

static void
mtkswitch_port_init(struct mtkswitch_softc *sc, int port)
{
	uint32_t val;

	/* Called early and hence unlocked */

	/* Set the port to secure mode */
	val = sc->hal.mtkswitch_read(sc, MTKSWITCH_PCR(port));
	val |= PCR_PORT_VLAN_SECURE;
	sc->hal.mtkswitch_write(sc, MTKSWITCH_PCR(port), val);

	/* Set port's vlan_attr to user port */
	val = sc->hal.mtkswitch_read(sc, MTKSWITCH_PVC(port));
	val &= ~PVC_VLAN_ATTR_MASK;
	sc->hal.mtkswitch_write(sc, MTKSWITCH_PVC(port), val);

	val = PMCR_CFG_DEFAULT;
	if (port == sc->cpuport)
		val |= PMCR_FORCE_LINK | PMCR_FORCE_DPX | PMCR_FORCE_SPD_1000 |
		    PMCR_FORCE_MODE;
	/* Set port's MAC to default settings */
	sc->hal.mtkswitch_write(sc, MTKSWITCH_PMCR(port), val);
}

static uint32_t
mtkswitch_get_port_status(struct mtkswitch_softc *sc, int port)
{
	uint32_t val, res, tmp;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	res = 0;
	val = sc->hal.mtkswitch_read(sc, MTKSWITCH_PMSR(port));

	if (val & PMSR_MAC_LINK_STS)
		res |= MTKSWITCH_LINK_UP;
	if (val & PMSR_MAC_DPX_STS)
		res |= MTKSWITCH_DUPLEX;
	tmp = PMSR_MAC_SPD(val);
	if (tmp == 0)
		res |= MTKSWITCH_SPEED_10;
	else if (tmp == 1)
		res |= MTKSWITCH_SPEED_100;
	else if (tmp == 2)
		res |= MTKSWITCH_SPEED_1000;
	if (val & PMSR_TX_FC_STS)
		res |= MTKSWITCH_TXFLOW;
	if (val & PMSR_RX_FC_STS)
		res |= MTKSWITCH_RXFLOW;

	return (res);
}

static int
mtkswitch_atu_flush(struct mtkswitch_softc *sc)
{

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	/* Flush all non-static MAC addresses */
	while (sc->hal.mtkswitch_read(sc, MTKSWITCH_ATC) & ATC_BUSY);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_ATC, ATC_BUSY |
	    ATC_AC_MAT_NON_STATIC_MACS | ATC_AC_CMD_CLEAN);
	while (sc->hal.mtkswitch_read(sc, MTKSWITCH_ATC) & ATC_BUSY);

	return (0);
}

static int
mtkswitch_port_vlan_setup(struct mtkswitch_softc *sc, etherswitch_port_t *p)
{
	int err;

	/*
	 * Port behaviour wrt tag/untag/stack is currently defined per-VLAN.
	 * So we say we don't support it here.
	 */
	if ((p->es_flags & (ETHERSWITCH_PORT_DOUBLE_TAG |
	    ETHERSWITCH_PORT_ADDTAG | ETHERSWITCH_PORT_STRIPTAG)) != 0)
		return (ENOTSUP);

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);

	/* Set the PVID */
	if (p->es_pvid != 0) {
		err = sc->hal.mtkswitch_vlan_set_pvid(sc, p->es_port,
		    p->es_pvid);
		if (err != 0) {
			MTKSWITCH_UNLOCK(sc);
			return (err);
		}
	}

	MTKSWITCH_UNLOCK(sc);

	return (0);
}

static int
mtkswitch_port_vlan_get(struct mtkswitch_softc *sc, etherswitch_port_t *p)
{

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);

	/* Retrieve the PVID */
	sc->hal.mtkswitch_vlan_get_pvid(sc, p->es_port, &p->es_pvid);

	/*
	 * Port flags are not supported at the moment.
	 * Port's tag/untag/stack behaviour is defined per-VLAN.
	 */
	p->es_flags = 0;

	MTKSWITCH_UNLOCK(sc);

	return (0);
}

static void
mtkswitch_invalidate_vlan(struct mtkswitch_softc *sc, uint32_t vid)
{

	while (sc->hal.mtkswitch_read(sc, MTKSWITCH_VTCR) & VTCR_BUSY);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VTCR, VTCR_BUSY |
	    VTCR_FUNC_VID_INVALID | (vid & VTCR_VID_MASK));
	while (sc->hal.mtkswitch_read(sc, MTKSWITCH_VTCR) & VTCR_BUSY);
}

static void
mtkswitch_vlan_init_hw(struct mtkswitch_softc *sc)
{
	uint32_t val, vid, i;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);
	MTKSWITCH_LOCK(sc);
	/* Reset all VLANs to defaults first */
	for (i = 0; i < sc->info.es_nvlangroups; i++) {
		mtkswitch_invalidate_vlan(sc, i);
		if (sc->sc_switchtype == MTK_SWITCH_MT7620) {
			val = sc->hal.mtkswitch_read(sc, MTKSWITCH_VTIM(i));
			val &= ~(VTIM_MASK << VTIM_OFF(i));
			val |= ((i + 1) << VTIM_OFF(i));
			sc->hal.mtkswitch_write(sc, MTKSWITCH_VTIM(i), val);
		}
	}

	/* Now, add all ports as untagged members of VLAN 1 */
	if (sc->sc_switchtype == MTK_SWITCH_MT7620) {
		/* MT7620 uses vid index instead of actual vid */
		vid = 0;
	} else {
		/* MT7621 uses the vid itself */
		vid = 1;
	}
	val = VAWD1_IVL_MAC | VAWD1_VTAG_EN | VAWD1_VALID;
	for (i = 0; i < sc->info.es_nports; i++)
		val |= VAWD1_PORT_MEMBER(i);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VAWD1, val);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VAWD2, 0);
	val = VTCR_BUSY | VTCR_FUNC_VID_WRITE | vid;
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VTCR, val);

	/* Set all port PVIDs to 1 */
	for (i = 0; i < sc->info.es_nports; i++) {
		sc->hal.mtkswitch_vlan_set_pvid(sc, i, 1);
	}

	MTKSWITCH_UNLOCK(sc);
}

static int
mtkswitch_vlan_getvgroup(struct mtkswitch_softc *sc, etherswitch_vlangroup_t *v)
{
	uint32_t val, i;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);

	if ((sc->vlan_mode != ETHERSWITCH_VLAN_DOT1Q) ||
	    (v->es_vlangroup > sc->info.es_nvlangroups))
		return (EINVAL);

	/* Reset the member ports. */
	v->es_untagged_ports = 0;
	v->es_member_ports = 0;

	/* Not supported for now */
	v->es_fid = 0;

	MTKSWITCH_LOCK(sc);
	if (sc->sc_switchtype == MTK_SWITCH_MT7620) {
		v->es_vid = (sc->hal.mtkswitch_read(sc,
		    MTKSWITCH_VTIM(v->es_vlangroup)) >>
		    VTIM_OFF(v->es_vlangroup)) & VTIM_MASK;
	} else {
		v->es_vid = v->es_vlangroup;
	}

	while (sc->hal.mtkswitch_read(sc, MTKSWITCH_VTCR) & VTCR_BUSY);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VTCR, VTCR_BUSY |
	    VTCR_FUNC_VID_READ | (v->es_vlangroup & VTCR_VID_MASK));
	while ((val = sc->hal.mtkswitch_read(sc, MTKSWITCH_VTCR)) & VTCR_BUSY);
	if (val & VTCR_IDX_INVALID) {
		MTKSWITCH_UNLOCK(sc);
		return (0);
	}

	val = sc->hal.mtkswitch_read(sc, MTKSWITCH_VAWD1);
	if (val & VAWD1_VALID)
		v->es_vid |= ETHERSWITCH_VID_VALID;
	else {
		MTKSWITCH_UNLOCK(sc);
		return (0);
	}
	v->es_member_ports = (val >> VAWD1_MEMBER_OFF) & VAWD1_MEMBER_MASK;

	val = sc->hal.mtkswitch_read(sc, MTKSWITCH_VAWD2);
	for (i = 0; i < sc->info.es_nports; i++) {
		if ((val & VAWD2_PORT_MASK(i)) == VAWD2_PORT_UNTAGGED(i))
			v->es_untagged_ports |= (1<<i);
	}

	MTKSWITCH_UNLOCK(sc);
	return (0);
}

static int
mtkswitch_vlan_setvgroup(struct mtkswitch_softc *sc, etherswitch_vlangroup_t *v)
{
	uint32_t val, i, vid;

	MTKSWITCH_LOCK_ASSERT(sc, MA_NOTOWNED);

	if ((sc->vlan_mode != ETHERSWITCH_VLAN_DOT1Q) ||
	    (v->es_vlangroup > sc->info.es_nvlangroups))
		return (EINVAL);

	/* We currently don't support FID */
	if (v->es_fid != 0)
		return (EINVAL);

	MTKSWITCH_LOCK(sc);
	while (sc->hal.mtkswitch_read(sc, MTKSWITCH_VTCR) & VTCR_BUSY);
	if (sc->sc_switchtype == MTK_SWITCH_MT7620) {
		val = sc->hal.mtkswitch_read(sc,
		    MTKSWITCH_VTIM(v->es_vlangroup));
		val &= ~(VTIM_MASK << VTIM_OFF(v->es_vlangroup));
		val |= ((v->es_vid & VTIM_MASK) << VTIM_OFF(v->es_vlangroup));
		sc->hal.mtkswitch_write(sc, MTKSWITCH_VTIM(v->es_vlangroup),
		    val);
		vid = v->es_vlangroup;
	} else
		vid = v->es_vid;

	/* We use FID 0 */
	val = VAWD1_IVL_MAC | VAWD1_VTAG_EN | VAWD1_VALID;
	val |= ((v->es_member_ports & VAWD1_MEMBER_MASK) << VAWD1_MEMBER_OFF);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VAWD1, val);

	/* Set tagged ports */
	val = 0;
	for (i = 0; i < sc->info.es_nports; i++)
		if (((1<<i) & v->es_untagged_ports) == 0)
			val |= VAWD2_PORT_TAGGED(i);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VAWD2, val);

	/* Write the VLAN entry */
	sc->hal.mtkswitch_write(sc, MTKSWITCH_VTCR, VTCR_BUSY |
	    VTCR_FUNC_VID_WRITE | (vid & VTCR_VID_MASK));
	while ((val = sc->hal.mtkswitch_read(sc, MTKSWITCH_VTCR)) & VTCR_BUSY);

	MTKSWITCH_UNLOCK(sc);

	if (val & VTCR_IDX_INVALID)
		return (EINVAL);

	return (0);
}

static int
mtkswitch_vlan_get_pvid(struct mtkswitch_softc *sc, int port, int *pvid)
{

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	*pvid = sc->hal.mtkswitch_read(sc, MTKSWITCH_PPBV1(port));
	*pvid = PPBV_VID_FROM_REG(*pvid);

	return (0); 
}

static int
mtkswitch_vlan_set_pvid(struct mtkswitch_softc *sc, int port, int pvid)
{
	uint32_t val;

	MTKSWITCH_LOCK_ASSERT(sc, MA_OWNED);
	val = PPBV_VID(pvid & PPBV_VID_MASK);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_PPBV1(port), val);
	sc->hal.mtkswitch_write(sc, MTKSWITCH_PPBV2(port), val);

	return (0);
}

extern void
mtk_attach_switch_mt7620(struct mtkswitch_softc *sc)
{

	sc->portmap = 0x7f;
	sc->phymap = 0x1f;

	sc->info.es_nports = 7;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_DOT1Q;
	sc->info.es_nvlangroups = 16;
	sprintf(sc->info.es_name, "Mediatek GSW");

	if (sc->sc_switchtype == MTK_SWITCH_MT7621) {
		sc->hal.mtkswitch_read = mtkswitch_reg_read32_mt7621;
		sc->hal.mtkswitch_write = mtkswitch_reg_write32_mt7621;
		sc->info.es_nvlangroups = 4096;
	} else {
		sc->hal.mtkswitch_read = mtkswitch_reg_read32;
		sc->hal.mtkswitch_write = mtkswitch_reg_write32;
	}

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
