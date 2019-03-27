/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <dev/etherswitch/ip17x/ip175c.h>

/*
 * IP175C specific functions.
 */

/*
 * Reset the switch.
 */
static int
ip175c_reset(struct ip17x_softc *sc)
{
	uint32_t data;

	/* Reset all the switch settings. */
	if (ip17x_writephy(sc->sc_dev, IP175C_RESET_PHY, IP175C_RESET_REG,
	    0x175c))
		return (-1);
	DELAY(2000);

	/* Force IP175C mode. */
	data = ip17x_readphy(sc->sc_dev, IP175C_MODE_PHY, IP175C_MODE_REG);
	if (data == 0x175a) {
		if (ip17x_writephy(sc->sc_dev, IP175C_MODE_PHY, IP175C_MODE_REG,
		    0x175c))
		return (-1);
	}

	return (0);
}

static int
ip175c_port_vlan_setup(struct ip17x_softc *sc)
{
	struct ip17x_vlan *v;
	uint32_t ports[IP175X_NUM_PORTS], reg[IP175X_NUM_PORTS/2];
	int i, err, phy;

	KASSERT(sc->cpuport == 5, ("cpuport != 5 not supported for IP175C"));
	KASSERT(sc->numports == 6, ("numports != 6 not supported for IP175C"));

	/* Build the port access masks. */
	memset(ports, 0, sizeof(ports));
	for (i = 0; i < sc->info.es_nports; i++) {
		phy = sc->portphy[i];
		v = &sc->vlan[i];
		ports[phy] = v->ports;
	}

	/* Move the cpuport bit to its correct place. */
	for (i = 0; i < sc->numports; i++) {
		if (ports[i] & (1 << sc->cpuport)) {
			ports[i] |= (1 << 7);
			ports[i] &= ~(1 << sc->cpuport);
		}
	}

	/* And now build the switch register data. */
	memset(reg, 0, sizeof(reg));
	for (i = 0; i < (sc->numports / 2); i++)
		reg[i] = ports[i * 2] << 8 | ports[i * 2 + 1];

	/* Update the switch resgisters. */
	err = ip17x_writephy(sc->sc_dev, 29, 19, reg[0]);
	if (err == 0)
		err = ip17x_writephy(sc->sc_dev, 29, 20, reg[1]);
	if (err == 0)
		err = ip17x_updatephy(sc->sc_dev, 29, 21, 0xff00, reg[2]);
	if (err == 0)
		err = ip17x_updatephy(sc->sc_dev, 30, 18, 0x00ff, reg[2]);
	return (err);
}

static int
ip175c_dot1q_vlan_setup(struct ip17x_softc *sc)
{
	struct ip17x_vlan *v;
	uint32_t data;
	uint32_t vlans[IP17X_MAX_VLANS];
	int i, j;

	KASSERT(sc->cpuport == 5, ("cpuport != 5 not supported for IP175C"));
	KASSERT(sc->numports == 6, ("numports != 6 not supported for IP175C"));

	/* Add and strip VLAN tags. */
	data = (sc->addtag & ~(1 << IP175X_CPU_PORT)) << 11;
	data |= (sc->striptag & ~(1 << IP175X_CPU_PORT)) << 6;
	if (sc->addtag & (1 << IP175X_CPU_PORT))
		data |= (1 << 1);
	if (sc->striptag & (1 << IP175X_CPU_PORT))
		data |= (1 << 0);
	if (ip17x_writephy(sc->sc_dev, 29, 23, data))
		return (-1);

	/* Set the VID_IDX_SEL to 0. */
	if (ip17x_updatephy(sc->sc_dev, 30, 9, 0x70, 0))
		return (-1);

	/* Calculate the port masks. */
	memset(vlans, 0, sizeof(vlans));
	for (i = 0; i < IP17X_MAX_VLANS; i++) {
		v = &sc->vlan[i];
		if ((v->vlanid & ETHERSWITCH_VID_VALID) == 0)
			continue;
		vlans[v->vlanid & ETHERSWITCH_VID_MASK] = v->ports;
	}

	for (j = 0, i = 1; i <= IP17X_MAX_VLANS / 2; i++) {
		data = vlans[j++] & 0x3f;
		data |= (vlans[j++] & 0x3f) << 8;
		if (ip17x_writephy(sc->sc_dev, 30, i, data))
			return (-1);
	}

	/* Port default VLAN ID. */
	for (i = 0; i < sc->numports; i++) {
		if (i == IP175X_CPU_PORT) {
			if (ip17x_writephy(sc->sc_dev, 29, 30, sc->pvid[i]))
				return (-1);
		} else {
			if (ip17x_writephy(sc->sc_dev, 29, 24 + i, sc->pvid[i]))
				return (-1);
		}
	}

	return (0);
}

/*
 * Set the Switch configuration.
 */
static int
ip175c_hw_setup(struct ip17x_softc *sc)
{

	switch (sc->vlan_mode) {
	case ETHERSWITCH_VLAN_PORT:
		return (ip175c_port_vlan_setup(sc));
		break;
	case ETHERSWITCH_VLAN_DOT1Q:
		return (ip175c_dot1q_vlan_setup(sc));
		break;
	}
	return (-1);
}

/*
 * Set the switch VLAN mode.
 */
static int
ip175c_set_vlan_mode(struct ip17x_softc *sc, uint32_t mode)
{

	switch (mode) {
	case ETHERSWITCH_VLAN_DOT1Q:
		/* Enable VLAN tag processing. */
		ip17x_updatephy(sc->sc_dev, 30, 9, 0x80, 0x80);
		sc->vlan_mode = mode;
		break;
	case ETHERSWITCH_VLAN_PORT:
	default:
		/* Disable VLAN tag processing. */
		ip17x_updatephy(sc->sc_dev, 30, 9, 0x80, 0);
		sc->vlan_mode = ETHERSWITCH_VLAN_PORT;
		break;
	}

	/* Reset vlans. */
	ip17x_reset_vlans(sc, sc->vlan_mode);

	/* Update switch configuration. */
	ip175c_hw_setup(sc);

	return (0);
}

/*
 * Get the switch VLAN mode.
 */
static int
ip175c_get_vlan_mode(struct ip17x_softc *sc)
{

	return (sc->vlan_mode);
}

void
ip175c_attach(struct ip17x_softc *sc)
{
	uint32_t data;

	data = ip17x_readphy(sc->sc_dev, IP175C_MII_PHY, IP175C_MII_CTL_REG);
	device_printf(sc->sc_dev, "MII: %x\n", data);
	/* check mii1 interface if disabled then phy4 and mac4 hold on switch */
	if((data & (1 << IP175C_MII_MII1_RMII_EN)) == 0)
		sc->phymask |= 0x10;

	sc->hal.ip17x_reset = ip175c_reset;
	sc->hal.ip17x_hw_setup = ip175c_hw_setup;
	sc->hal.ip17x_get_vlan_mode = ip175c_get_vlan_mode;
	sc->hal.ip17x_set_vlan_mode = ip175c_set_vlan_mode;

	/* Defaults for IP175C. */
	sc->cpuport = IP175X_CPU_PORT;
	sc->numports = IP175X_NUM_PORTS;
	sc->info.es_vlan_caps = ETHERSWITCH_VLAN_PORT | ETHERSWITCH_VLAN_DOT1Q;

	device_printf(sc->sc_dev, "type: IP175C\n");
}
