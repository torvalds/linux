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
#include <dev/etherswitch/arswitch/arswitch_8316.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

/*
 * AR8316 specific functions
 */
static int
ar8316_hw_setup(struct arswitch_softc *sc)
{

	/*
	 * Configure the switch mode based on whether:
	 *
	 * + The switch port is GMII/RGMII;
	 * + Port 4 is either connected to the CPU or to the internal switch.
	 */
	if (sc->is_rgmii && sc->phy4cpu) {
		arswitch_writereg(sc->sc_dev, AR8X16_REG_MODE,
		    AR8X16_MODE_RGMII_PORT4_ISO);
		device_printf(sc->sc_dev,
		    "%s: MAC port == RGMII, port 4 = dedicated PHY\n",
		    __func__);
	} else if (sc->is_rgmii) {
		arswitch_writereg(sc->sc_dev, AR8X16_REG_MODE,
		    AR8X16_MODE_RGMII_PORT4_SWITCH);
		device_printf(sc->sc_dev,
		    "%s: MAC port == RGMII, port 4 = switch port\n",
		    __func__);
	} else if (sc->is_gmii) {
		arswitch_writereg(sc->sc_dev, AR8X16_REG_MODE,
		    AR8X16_MODE_GMII);
		device_printf(sc->sc_dev, "%s: MAC port == GMII\n", __func__);
	} else {
		device_printf(sc->sc_dev, "%s: unknown switch PHY config\n",
		    __func__);
		return (ENXIO);
	}

	DELAY(1000);	/* 1ms wait for things to settle */

	/*
	 * If port 4 is RGMII, force workaround
	 */
	if (sc->is_rgmii && sc->phy4cpu) {
		device_printf(sc->sc_dev,
		    "%s: port 4 RGMII workaround\n",
		    __func__);

		/* work around for phy4 rgmii mode */
		arswitch_writedbg(sc->sc_dev, 4, 0x12, 0x480c);
		/* rx delay */
		arswitch_writedbg(sc->sc_dev, 4, 0x0, 0x824e);
		/* tx delay */
		arswitch_writedbg(sc->sc_dev, 4, 0x5, 0x3d47);
		DELAY(1000);	/* 1ms, again to let things settle */
	}

	return (0);
}

/*
 * Initialise other global values, for the AR8316.
 */
static int
ar8316_hw_global_setup(struct arswitch_softc *sc)
{

	ARSWITCH_LOCK(sc);

	arswitch_writereg(sc->sc_dev, 0x38, AR8X16_MAGIC);

	/* Enable CPU port and disable mirror port. */
	arswitch_writereg(sc->sc_dev, AR8X16_REG_CPU_PORT,
	    AR8X16_CPU_PORT_EN | AR8X16_CPU_MIRROR_DIS);

	/* Setup TAG priority mapping. */
	arswitch_writereg(sc->sc_dev, AR8X16_REG_TAG_PRIO, 0xfa50);

	/*
	 * Flood address table misses to all ports, and enable forwarding of
	 * broadcasts to the cpu port.
	 */
	arswitch_writereg(sc->sc_dev, AR8X16_REG_FLOOD_MASK,
	    AR8X16_FLOOD_MASK_BCAST_TO_CPU | 0x003f003f);

	/* Enable jumbo frames. */
	arswitch_modifyreg(sc->sc_dev, AR8X16_REG_GLOBAL_CTRL,
	    AR8316_GLOBAL_CTRL_MTU_MASK, 9018 + 8 + 2);

	/* Setup service TAG. */
	arswitch_modifyreg(sc->sc_dev, AR8X16_REG_SERVICE_TAG,
	    AR8X16_SERVICE_TAG_MASK, 0);

	ARSWITCH_UNLOCK(sc);
	return (0);
}

void
ar8316_attach(struct arswitch_softc *sc)
{

	sc->hal.arswitch_hw_setup = ar8316_hw_setup;
	sc->hal.arswitch_hw_global_setup = ar8316_hw_global_setup;

	/* Set the switch vlan capabilities. */
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_DOT1Q |
	    ETHERSWITCH_VLAN_PORT | ETHERSWITCH_VLAN_DOUBLE_TAG;
	sc->info.es_nvlangroups = AR8X16_MAX_VLANS;
}
