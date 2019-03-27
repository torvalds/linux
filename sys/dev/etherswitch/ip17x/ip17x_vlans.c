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
#include <sys/errno.h>
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

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

/*
 * Reset vlans to default state.
 */
int
ip17x_reset_vlans(struct ip17x_softc *sc, uint32_t vlan_mode)
{
	struct ip17x_vlan *v;
	int i, j, phy;

	/* Do not add or strip vlan tags on any port. */
	sc->addtag = 0;
	sc->striptag = 0;

	/* Reset all vlan data. */
	memset(sc->vlan, 0, sizeof(sc->vlan));
	memset(sc->pvid, 0, sizeof(uint32_t) * sc->numports);

	if (vlan_mode == ETHERSWITCH_VLAN_PORT) {

		/* Initialize port based vlans. */
		for (i = 0, phy = 0; phy < MII_NPHY; phy++) {
			if (((1 << phy) & sc->phymask) == 0)
				continue;
			v = &sc->vlan[i];
			v->vlanid = i++ | ETHERSWITCH_VID_VALID;
			v->ports = (1 << sc->cpuport);
			for (j = 0; j < MII_NPHY; j++) {
				if (((1 << j) & sc->phymask) == 0)
					continue;
				v->ports |= (1 << j);
			}
		}

	} else if (vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {

		/*
		 * Setup vlan 1 as PVID for all switch ports.  Add all ports as
		 * members of vlan 1.
		 */
		v = &sc->vlan[0];
		v->vlanid = 1 | ETHERSWITCH_VID_VALID;
		/* Set PVID to 1 for everyone. */
		for (i = 0; i < sc->numports; i++)
			sc->pvid[i] = 1;
		for (i = 0; i < MII_NPHY; i++) {
			if ((sc->phymask & (1 << i)) == 0)
				continue;
			v->ports |= (1 << i);
		}
	}

	return (0);
}

int
ip17x_getvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct ip17x_softc *sc;
	uint32_t port;
	int i;

	sc = device_get_softc(dev);

	/* Vlan ID. */
	vg->es_vid = sc->vlan[vg->es_vlangroup].vlanid;

	/* Member Ports. */
	vg->es_member_ports = 0;
	for (i = 0; i < MII_NPHY; i++) {
		if ((sc->phymask & (1 << i)) == 0)
			continue;
		if ((sc->vlan[vg->es_vlangroup].ports & (1 << i)) == 0)
			continue;
		port = sc->phyport[i];
		vg->es_member_ports |= (1 << port);
	}

	/* Not supported. */
	vg->es_untagged_ports = vg->es_member_ports;
	vg->es_fid = 0;

	return (0);
}

int
ip17x_setvgroup(device_t dev, etherswitch_vlangroup_t *vg)
{
	struct ip17x_softc *sc;
	uint32_t phy;
	int i;

	sc = device_get_softc(dev);

	/* Check VLAN mode. */
	if (sc->vlan_mode == 0)
		return (EINVAL);

	/* IP175C don't support VLAN IDs > 15. */
	if (IP17X_IS_SWITCH(sc, IP175C) &&
	    (vg->es_vid & ETHERSWITCH_VID_MASK) > IP175C_LAST_VLAN)
		return (EINVAL);

	/* Vlan ID. */
	if (sc->vlan_mode == ETHERSWITCH_VLAN_DOT1Q) {
		for (i = 0; i < sc->info.es_nvlangroups; i++) {
			/* Is this Vlan ID already set in another vlangroup ? */
			if (i != vg->es_vlangroup &&
			    sc->vlan[i].vlanid & ETHERSWITCH_VID_VALID &&
			    (sc->vlan[i].vlanid & ETHERSWITCH_VID_MASK) ==
			    (vg->es_vid & ETHERSWITCH_VID_MASK))
				return (EINVAL);
		}
		sc->vlan[vg->es_vlangroup].vlanid = vg->es_vid &
		    ETHERSWITCH_VID_MASK;
		/* Setting the vlanid to zero disables the vlangroup. */
		if (sc->vlan[vg->es_vlangroup].vlanid == 0) {
			sc->vlan[vg->es_vlangroup].ports = 0;
			return (sc->hal.ip17x_hw_setup(sc));
		}
		sc->vlan[vg->es_vlangroup].vlanid |= ETHERSWITCH_VID_VALID;
	}

	/* Member Ports. */
	sc->vlan[vg->es_vlangroup].ports = 0;
	for (i = 0; i < sc->numports; i++) {
		if ((vg->es_member_ports & (1 << i)) == 0)
			continue;
		phy = sc->portphy[i];
		sc->vlan[vg->es_vlangroup].ports |= (1 << phy);
	}

	return (sc->hal.ip17x_hw_setup(sc));
}
