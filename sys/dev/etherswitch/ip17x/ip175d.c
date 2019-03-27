/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Luiz Otavio O Souza.
 * Copyright (c) 2011-2012 Stefan Bethke.
 * Copyright (c) 2012 Adrian Chadd.
 * Copyright (C) 2008 Patrick Horn.
 * Copyright (C) 2008, 2010 Martin Mares.
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/if.h>

#include <dev/mii/mii.h>

#include <dev/etherswitch/etherswitch.h>
#include <dev/etherswitch/ip17x/ip17x_phy.h>
#include <dev/etherswitch/ip17x/ip17x_reg.h>
#include <dev/etherswitch/ip17x/ip17x_var.h>
#include <dev/etherswitch/ip17x/ip17x_vlans.h>
#include <dev/etherswitch/ip17x/ip175d.h>

/*
 * IP175D specific functions.
 */

/*
 * Reset the switch to default state.
 */
static int
ip175d_reset(struct ip17x_softc *sc)
{

	/* Reset all the switch settings. */
	ip17x_writephy(sc->sc_dev, IP175D_RESET_PHY, IP175D_RESET_REG, 0x175d);
	DELAY(2000);

	/* Disable the special tagging mode. */
	ip17x_updatephy(sc->sc_dev, 21, 22, 0x3, 0x0);

	/* Set 802.1q protocol type. */
	ip17x_writephy(sc->sc_dev, 22, 3, 0x8100);

	return (0);
}

/*
 * Set the Switch configuration.
 */
static int
ip175d_hw_setup(struct ip17x_softc *sc)
{
	struct ip17x_vlan *v;
	uint32_t ports[IP17X_MAX_VLANS];
	uint32_t addtag[IP17X_MAX_VLANS];
	uint32_t striptag[IP17X_MAX_VLANS];
	uint32_t vlan_mask;
	int i, j;

	vlan_mask = 0;
	for (i = 0; i < IP17X_MAX_VLANS; i++) {

		ports[i] = 0;
		addtag[i] = 0;
		striptag[i] = 0;

		v = &sc->vlan[i];
		if ((v->vlanid & ETHERSWITCH_VID_VALID) == 0 ||
		    sc->vlan_mode == 0) {
			/* Vlangroup disabled.  Reset the filter. */
			ip17x_writephy(sc->sc_dev, 22, 14 + i, i + 1);
			ports[i] = 0x3f;
			continue;
		}

		vlan_mask |= (1 << i);
		ports[i] = v->ports;

		/* Setup the filter, write the VLAN id. */
		ip17x_writephy(sc->sc_dev, 22, 14 + i,
		    v->vlanid & ETHERSWITCH_VID_MASK);

		for (j = 0; j < MII_NPHY; j++) {
			if ((ports[i] & (1 << j)) == 0)
				continue;
			if (sc->addtag & (1 << j))
				addtag[i] |= (1 << j);
			if (sc->striptag & (1 << j))
				striptag[i] |= (1 << j);
		}
	}

	/* Write the port masks, tag adds and removals. */
	for (i = 0; i < IP17X_MAX_VLANS / 2; i++) {
		ip17x_writephy(sc->sc_dev, 23, i,
		    ports[2 * i] | (ports[2 * i + 1] << 8));
		ip17x_writephy(sc->sc_dev, 23, i + 8,
		    addtag[2 * i] | (addtag[2 * i + 1] << 8));
		ip17x_writephy(sc->sc_dev, 23, i + 16,
		    striptag[2 * i] | (striptag[2 * i + 1] << 8));
	}

	/* Write the in use vlan mask. */
	ip17x_writephy(sc->sc_dev, 22, 10, vlan_mask);

	/* Write the PVID of each port. */
	for (i = 0; i < sc->numports; i++)
		ip17x_writephy(sc->sc_dev, 22, 4 + i, sc->pvid[i]);

	return (0);
}

/*
 * Set the switch VLAN mode.
 */
static int
ip175d_set_vlan_mode(struct ip17x_softc *sc, uint32_t mode)
{

	switch (mode) {
	case ETHERSWITCH_VLAN_DOT1Q:
		/*
		 * VLAN classification rules: tag-based VLANs,
		 * use VID to classify, drop packets that cannot
		 * be classified.
		 */
		ip17x_updatephy(sc->sc_dev, 22, 0, 0x3fff, 0x003f);
		sc->vlan_mode = mode;
		break;
	case ETHERSWITCH_VLAN_PORT:
		sc->vlan_mode = mode;
		/* fallthrough */
	default:
		/*
		 * VLAN classification rules: everything off &
		 * clear table.
		 */
		ip17x_updatephy(sc->sc_dev, 22, 0, 0xbfff, 0x8000);
		sc->vlan_mode = 0;
		break;
	}

	if (sc->vlan_mode != 0) {
		/*
		 * Ingress rules: CFI=1 dropped, null VID is untagged, VID=1 passed,
		 * VID=0xfff discarded, admin both tagged and untagged, ingress
		 * filters enabled.
		 */
		ip17x_updatephy(sc->sc_dev, 22, 1, 0x0fff, 0x0c3f);

		/* Egress rules: IGMP processing off, keep VLAN header off. */
		ip17x_updatephy(sc->sc_dev, 22, 2, 0x0fff, 0x0000);
	} else {
		ip17x_updatephy(sc->sc_dev, 22, 1, 0x0fff, 0x043f);
		ip17x_updatephy(sc->sc_dev, 22, 2, 0x0fff, 0x0020);
	}

	/* Reset vlans. */
	ip17x_reset_vlans(sc, sc->vlan_mode);

	/* Update switch configuration. */
	ip175d_hw_setup(sc);

	return (0);
}

/*
 * Get the switch VLAN mode.
 */
static int
ip175d_get_vlan_mode(struct ip17x_softc *sc)
{

	return (sc->vlan_mode);
}

void
ip175d_attach(struct ip17x_softc *sc)
{

	sc->hal.ip17x_reset = ip175d_reset;
	sc->hal.ip17x_hw_setup = ip175d_hw_setup;
	sc->hal.ip17x_get_vlan_mode = ip175d_get_vlan_mode;
	sc->hal.ip17x_set_vlan_mode = ip175d_set_vlan_mode;

	/* Defaults for IP175C. */
	sc->cpuport = IP175X_CPU_PORT;
	sc->numports = IP175X_NUM_PORTS;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_DOT1Q;

	device_printf(sc->sc_dev, "type: IP175D\n");
}
