/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Stefan Bethke.
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if.h>
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
#include <dev/etherswitch/arswitch/arswitch_phy.h>	/* XXX for probe */
#include <dev/etherswitch/arswitch/arswitch_9340.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

/*
 * AR9340 specific functions
 */
static int
ar9340_hw_setup(struct arswitch_softc *sc)
{

	return (0);
}

static int
ar9340_atu_learn_default(struct arswitch_softc *sc)
{

	/* Enable aging, MAC replacing */
	arswitch_writereg(sc->sc_dev, AR934X_REG_AT_CTRL,
	    0x2b /* 5 min age time */ |
	    AR934X_AT_CTRL_AGE_EN |
	    AR934X_AT_CTRL_LEARN_CHANGE);

	/* Enable ARP frame acknowledge */
	arswitch_modifyreg(sc->sc_dev, AR934X_REG_QM_CTRL,
	    AR934X_QM_CTRL_ARP_EN, AR934X_QM_CTRL_ARP_EN);

#if 0
	/* Copy frame to CPU port, not just redirect it */
	arswitch_modifyreg(sc->sc_dev, AR934X_REG_QM_CTRL,
	    AR934X_QM_CTRL_ARP_COPY_EN, AR934X_QM_CTRL_ARP_COPY_EN);
#endif

	return (0);
}

/*
 * Initialise other global values for the AR9340.
 */
static int
ar9340_hw_global_setup(struct arswitch_softc *sc)
{

	ARSWITCH_LOCK(sc);

	/* Enable CPU port; disable mirror port */
	arswitch_writereg(sc->sc_dev, AR8X16_REG_CPU_PORT,
	    AR8X16_CPU_PORT_EN | AR8X16_CPU_MIRROR_DIS);

	/* Setup TAG priority mapping */
	arswitch_writereg(sc->sc_dev, AR8X16_REG_TAG_PRIO, 0xfa50);

	/* Enable Broadcast frames transmitted to the CPU */
	arswitch_modifyreg(sc->sc_dev, AR934X_REG_FLOOD_MASK,
	    AR934X_FLOOD_MASK_BC_DP(0),
	    AR934X_FLOOD_MASK_BC_DP(0));
	arswitch_modifyreg(sc->sc_dev, AR934X_REG_FLOOD_MASK,
	    AR934X_FLOOD_MASK_MC_DP(0),
	    AR934X_FLOOD_MASK_MC_DP(0));
#if 0
	arswitch_modifyreg(sc->sc_dev, AR934X_REG_FLOOD_MASK,
	    AR934X_FLOOD_MASK_UC_DP(0),
	    AR934X_FLOOD_MASK_UC_DP(0));
#endif

	/* Enable MIB counters */
	arswitch_modifyreg(sc->sc_dev, AR8X16_REG_MIB_FUNC0,
	    AR934X_MIB_ENABLE, AR934X_MIB_ENABLE);

	/* Setup MTU */
	arswitch_modifyreg(sc->sc_dev, AR8X16_REG_GLOBAL_CTRL,
	    AR7240_GLOBAL_CTRL_MTU_MASK,
	    SM(1536, AR7240_GLOBAL_CTRL_MTU_MASK));

	/* Service Tag */
	arswitch_modifyreg(sc->sc_dev, AR8X16_REG_SERVICE_TAG,
	    AR8X16_SERVICE_TAG_MASK, 0);

	/* Settle time */
	DELAY(1000);

	/*
	 * Check PHY mode bits.
	 *
	 * This dictates whether the connected port is to be wired
	 * up via GMII or MII.  I'm not sure why - this is an internal
	 * wiring issue.
	 */
	if (sc->is_gmii) {
		device_printf(sc->sc_dev, "%s: GMII\n", __func__);
		arswitch_modifyreg(sc->sc_dev, AR934X_REG_OPER_MODE0,
		    AR934X_OPER_MODE0_MAC_GMII_EN,
		    AR934X_OPER_MODE0_MAC_GMII_EN);
	} else if (sc->is_mii) {
		device_printf(sc->sc_dev, "%s: MII\n", __func__);
		arswitch_modifyreg(sc->sc_dev, AR934X_REG_OPER_MODE0,
		    AR934X_OPER_MODE0_PHY_MII_EN,
		    AR934X_OPER_MODE0_PHY_MII_EN);
	} else {
		device_printf(sc->sc_dev, "%s: need is_gmii or is_mii set\n",
		    __func__);
		ARSWITCH_UNLOCK(sc);
		return (ENXIO);
	}

	/*
	 * Whether to connect PHY 4 via MII (ie a switch port) or
	 * treat it as a CPU port.
	 */
	if (sc->phy4cpu) {
		device_printf(sc->sc_dev, "%s: PHY4 - CPU\n", __func__);
		arswitch_modifyreg(sc->sc_dev, AR934X_REG_OPER_MODE1,
		    AR934X_REG_OPER_MODE1_PHY4_MII_EN,
		    AR934X_REG_OPER_MODE1_PHY4_MII_EN);
		sc->info.es_nports = 5;
	} else {
		device_printf(sc->sc_dev, "%s: PHY4 - Local\n", __func__);
		sc->info.es_nports = 6;
	}

	/* Settle time */
	DELAY(1000);

	ARSWITCH_UNLOCK(sc);
	return (0);
}

/*
 * The AR9340 switch probes (almost) the same as the AR7240 on-chip switch.
 *
 * However, the support is slightly different.
 *
 * So instead of checking the PHY revision or mask register contents,
 * we simply fall back to a hint check.
 */
int
ar9340_probe(device_t dev)
{
	int is_9340 = 0;

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "is_9340", &is_9340) != 0)
		return (ENXIO);

	if (is_9340 == 0)
		return (ENXIO);

	return (0);
}

void
ar9340_attach(struct arswitch_softc *sc)
{

	sc->hal.arswitch_hw_setup = ar9340_hw_setup;
	sc->hal.arswitch_hw_global_setup = ar9340_hw_global_setup;
	sc->hal.arswitch_atu_learn_default = ar9340_atu_learn_default;
	/*
	 * Note: the ar9340 table fetch code/registers matche
	 * the ar8216/ar8316 for now because we're not supporting
	 * static entry programming that includes any of the extra
	 * bits in the AR9340.
	 */

	/* Set the switch vlan capabilities. */
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_DOT1Q |
	    ETHERSWITCH_VLAN_PORT | ETHERSWITCH_VLAN_DOUBLE_TAG;
	sc->info.es_nvlangroups = AR8X16_MAX_VLANS;
}
