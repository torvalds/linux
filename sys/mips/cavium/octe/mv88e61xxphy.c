/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Juli Mallett <jmallett@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Marvell 88E61xx family of switch PHYs
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include "miibus_if.h"

#include "mv88e61xxphyreg.h"

struct mv88e61xxphy_softc;

struct mv88e61xxphy_port_softc {
	struct mv88e61xxphy_softc *sc_switch;
	unsigned sc_port;
	unsigned sc_domain;
	unsigned sc_vlan;
	unsigned sc_priority;
	unsigned sc_flags;
};

#define	MV88E61XXPHY_PORT_FLAG_VTU_UPDATE	(0x0001)

struct mv88e61xxphy_softc {
	device_t sc_dev;
	struct mv88e61xxphy_port_softc sc_ports[MV88E61XX_PORTS];
};

enum mv88e61xxphy_vtu_membership_type {
	MV88E61XXPHY_VTU_UNMODIFIED,
	MV88E61XXPHY_VTU_UNTAGGED,
	MV88E61XXPHY_VTU_TAGGED,
	MV88E61XXPHY_VTU_DISCARDED,
};

enum mv88e61xxphy_sysctl_link_type {
	MV88E61XXPHY_LINK_SYSCTL_DUPLEX,
	MV88E61XXPHY_LINK_SYSCTL_LINK,
	MV88E61XXPHY_LINK_SYSCTL_MEDIA,
};

enum mv88e61xxphy_sysctl_port_type {
	MV88E61XXPHY_PORT_SYSCTL_DOMAIN,
	MV88E61XXPHY_PORT_SYSCTL_VLAN,
	MV88E61XXPHY_PORT_SYSCTL_PRIORITY,
};

/*
 * Register access macros.
 */
#define	MV88E61XX_READ(sc, phy, reg)					\
	MIIBUS_READREG(device_get_parent((sc)->sc_dev), (phy), (reg))

#define	MV88E61XX_WRITE(sc, phy, reg, val)				\
	MIIBUS_WRITEREG(device_get_parent((sc)->sc_dev), (phy), (reg), (val))

#define	MV88E61XX_READ_PORT(psc, reg)					\
	MV88E61XX_READ((psc)->sc_switch, MV88E61XX_PORT((psc)->sc_port), (reg))

#define	MV88E61XX_WRITE_PORT(psc, reg, val)				\
	MV88E61XX_WRITE((psc)->sc_switch, MV88E61XX_PORT((psc)->sc_port), (reg), (val))

static int mv88e61xxphy_probe(device_t);
static int mv88e61xxphy_attach(device_t);

static void mv88e61xxphy_init(struct mv88e61xxphy_softc *);
static void mv88e61xxphy_init_port(struct mv88e61xxphy_port_softc *);
static void mv88e61xxphy_init_vtu(struct mv88e61xxphy_softc *);
static int mv88e61xxphy_sysctl_link_proc(SYSCTL_HANDLER_ARGS);
static int mv88e61xxphy_sysctl_port_proc(SYSCTL_HANDLER_ARGS);
static void mv88e61xxphy_vtu_load(struct mv88e61xxphy_softc *, uint16_t);
static void mv88e61xxphy_vtu_set_membership(struct mv88e61xxphy_softc *, unsigned, enum mv88e61xxphy_vtu_membership_type);
static void mv88e61xxphy_vtu_wait(struct mv88e61xxphy_softc *);

static int
mv88e61xxphy_probe(device_t dev)
{
	uint16_t val;

	val = MIIBUS_READREG(device_get_parent(dev), MV88E61XX_PORT(0),
	    MV88E61XX_PORT_REVISION);
	switch (val >> 4) {
	case 0x121:
		device_set_desc(dev, "Marvell Link Street 88E6123 3-Port Gigabit Switch");
		return (0);
	case 0x161:
		device_set_desc(dev, "Marvell Link Street 88E6161 6-Port Gigabit Switch");
		return (0);
	case 0x165:
		device_set_desc(dev, "Marvell Link Street 88E6161 6-Port Advanced Gigabit Switch");
		return (0);
	default:
		return (ENXIO);
	}
}

static int
mv88e61xxphy_attach(device_t dev)
{
	char portbuf[] = "N";
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	struct sysctl_oid *port_node, *portN_node;
	struct sysctl_oid_list *port_tree, *portN_tree;
	struct mv88e61xxphy_softc *sc;
	unsigned port;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	/*
	 * Initialize port softcs.
	 */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];
		psc->sc_switch = sc;
		psc->sc_port = port;
		psc->sc_domain = 0; /* One broadcast domain by default.  */
		psc->sc_vlan = port + 1; /* Tag VLANs by default.  */
		psc->sc_priority = 0; /* No default special priority.  */
		psc->sc_flags = 0;
	}

	/*
	 * Add per-port sysctl tree/handlers.
	 */
	port_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "port",
	    CTLFLAG_RD, NULL, "Switch Ports");
	port_tree = SYSCTL_CHILDREN(port_node);
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];

		portbuf[0] = '0' + port;
		portN_node = SYSCTL_ADD_NODE(ctx, port_tree, OID_AUTO, portbuf,
		    CTLFLAG_RD, NULL, "Switch Port");
		portN_tree = SYSCTL_CHILDREN(portN_node);

		SYSCTL_ADD_PROC(ctx, portN_tree, OID_AUTO, "duplex",
		    CTLFLAG_RD | CTLTYPE_INT, psc,
		    MV88E61XXPHY_LINK_SYSCTL_DUPLEX,
		    mv88e61xxphy_sysctl_link_proc, "IU",
		    "Media duplex status (0 = half duplex; 1 = full duplex)");

		SYSCTL_ADD_PROC(ctx, portN_tree, OID_AUTO, "link",
		    CTLFLAG_RD | CTLTYPE_INT, psc,
		    MV88E61XXPHY_LINK_SYSCTL_LINK,
		    mv88e61xxphy_sysctl_link_proc, "IU",
		    "Link status (0 = down; 1 = up)");

		SYSCTL_ADD_PROC(ctx, portN_tree, OID_AUTO, "media",
		    CTLFLAG_RD | CTLTYPE_INT, psc,
		    MV88E61XXPHY_LINK_SYSCTL_MEDIA,
		    mv88e61xxphy_sysctl_link_proc, "IU",
		    "Media speed (0 = unknown; 10 = 10Mbps; 100 = 100Mbps; 1000 = 1Gbps)");

		SYSCTL_ADD_PROC(ctx, portN_tree, OID_AUTO, "domain",
		    CTLFLAG_RW | CTLTYPE_INT, psc,
		    MV88E61XXPHY_PORT_SYSCTL_DOMAIN,
		    mv88e61xxphy_sysctl_port_proc, "IU",
		    "Broadcast domain (ports can only talk to other ports in the same domain)");

		SYSCTL_ADD_PROC(ctx, portN_tree, OID_AUTO, "vlan",
		    CTLFLAG_RW | CTLTYPE_INT, psc,
		    MV88E61XXPHY_PORT_SYSCTL_VLAN,
		    mv88e61xxphy_sysctl_port_proc, "IU",
		    "Tag packets from/for this port with a given VLAN.");

		SYSCTL_ADD_PROC(ctx, portN_tree, OID_AUTO, "priority",
		    CTLFLAG_RW | CTLTYPE_INT, psc,
		    MV88E61XXPHY_PORT_SYSCTL_PRIORITY,
		    mv88e61xxphy_sysctl_port_proc, "IU",
		    "Default packet priority for this port.");
	}

	mv88e61xxphy_init(sc);

	return (0);
}

static void
mv88e61xxphy_init(struct mv88e61xxphy_softc *sc)
{
	unsigned port;
	uint16_t val;
	unsigned i;

	/* Disable all ports.  */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];

		val = MV88E61XX_READ_PORT(psc, MV88E61XX_PORT_CONTROL);
		val &= ~0x3;
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_CONTROL, val);
	}

	DELAY(2000);

	/* Reset the switch.  */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_CONTROL, 0xc400);
	for (i = 0; i < 100; i++) {
		val = MV88E61XX_READ(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_STATUS);
		if ((val & 0xc800) == 0xc800)
			break;
		DELAY(10);
	}
	if (i == 100) {
		device_printf(sc->sc_dev, "%s: switch reset timed out.\n", __func__);
		return;
	}

	/* Disable PPU.  */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_CONTROL, 0x0000);

	/* Configure host port and send monitor frames to it.  */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_MONITOR,
	    (MV88E61XX_HOST_PORT << 12) | (MV88E61XX_HOST_PORT << 8) |
	    (MV88E61XX_HOST_PORT << 4));

	/* Disable remote management.  */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_REMOTE_MGMT, 0x0000);

	/* Send all specifically-addressed frames to the host port.  */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL2, MV88E61XX_GLOBAL2_MANAGE_2X, 0xffff);
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL2, MV88E61XX_GLOBAL2_MANAGE_0X, 0xffff);

	/* Remove provider-supplied tag and use it for switching.  */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL2, MV88E61XX_GLOBAL2_CONTROL2,
	    MV88E61XX_GLOBAL2_CONTROL2_REMOVE_PTAG);

	/* Configure all ports.  */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];
		mv88e61xxphy_init_port(psc);
	}

	/* Reprogram VLAN table (VTU.)  */
	mv88e61xxphy_init_vtu(sc);

	/* Enable all ports.  */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];

		val = MV88E61XX_READ_PORT(psc, MV88E61XX_PORT_CONTROL);
		val |= 0x3;
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_CONTROL, val);
	}
}

static void
mv88e61xxphy_init_port(struct mv88e61xxphy_port_softc *psc)
{
	struct mv88e61xxphy_softc *sc;
	unsigned allow_mask;

	sc = psc->sc_switch;

	/* Set media type and flow control.  */
	if (psc->sc_port != MV88E61XX_HOST_PORT) {
		/* Don't force any media type or flow control.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_FORCE_MAC, 0x0003);
	} else {
		/* Make CPU port 1G FDX.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_FORCE_MAC, 0x003e);
	}

	/* Don't limit flow control pauses.  */
	MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_PAUSE_CONTROL, 0x0000);

	/* Set various port functions per Linux.  */
	if (psc->sc_port != MV88E61XX_HOST_PORT) {
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_CONTROL, 0x04bc);
	} else {
		/*
		 * Send frames for unknown unicast and multicast groups to
		 * host, too.
		 */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_CONTROL, 0x063f);
	}

	if (psc->sc_port != MV88E61XX_HOST_PORT) {
		/* Disable trunking.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_CONTROL2, 0x0000);
	} else {
		/* Disable trunking and send learn messages to host.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_CONTROL2, 0x8000);
	}

	/*
	 * Port-based VLAN map; isolates MAC tables and forces ports to talk
	 * only to the host.
	 *
	 * Always allow the host to send to all ports and allow all ports to
	 * send to the host.
	 */
	if (psc->sc_port != MV88E61XX_HOST_PORT) {
		allow_mask = 1 << MV88E61XX_HOST_PORT;
	} else {
		allow_mask = (1 << MV88E61XX_PORTS) - 1;
		allow_mask &= ~(1 << MV88E61XX_HOST_PORT);
	}
	MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_VLAN_MAP,
	    (psc->sc_domain << 12) | allow_mask);

	/* VLAN tagging.  Set default priority and VLAN tag (or none.)  */
	MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_VLAN,
	    (psc->sc_priority << 14) | psc->sc_vlan);

	if (psc->sc_port == MV88E61XX_HOST_PORT) {
		/* Set provider ingress tag.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_PROVIDER_PROTO,
		    ETHERTYPE_VLAN);

		/* Set provider egress tag.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_ETHER_PROTO,
		    ETHERTYPE_VLAN);

		/* Use secure 802.1q mode and accept only tagged frames.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_FILTER,
		    MV88E61XX_PORT_FILTER_MAP_DEST |
		    MV88E61XX_PORT_FILTER_8021Q_SECURE |
		    MV88E61XX_PORT_FILTER_DISCARD_UNTAGGED);
	} else {
		/* Don't allow tagged frames.  */
		MV88E61XX_WRITE_PORT(psc, MV88E61XX_PORT_FILTER,
		    MV88E61XX_PORT_FILTER_MAP_DEST |
		    MV88E61XX_PORT_FILTER_DISCARD_TAGGED);
	}
}

static void
mv88e61xxphy_init_vtu(struct mv88e61xxphy_softc *sc)
{
	unsigned port;

	/*
	 * Start flush of the VTU.
	 */
	mv88e61xxphy_vtu_wait(sc);
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_VTU_OP,
	    MV88E61XX_GLOBAL_VTU_OP_BUSY | MV88E61XX_GLOBAL_VTU_OP_OP_FLUSH);

	/*
	 * Queue each port's VLAN to be programmed.
	 */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];
		psc->sc_flags &= ~MV88E61XXPHY_PORT_FLAG_VTU_UPDATE;
		if (psc->sc_vlan == 0)
			continue;
		psc->sc_flags |= MV88E61XXPHY_PORT_FLAG_VTU_UPDATE;
	}

	/*
	 * Program each VLAN that is in use.
	 */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];
		if ((psc->sc_flags & MV88E61XXPHY_PORT_FLAG_VTU_UPDATE) == 0)
			continue;
		mv88e61xxphy_vtu_load(sc, psc->sc_vlan);
	}

	/*
	 * Wait for last pending VTU operation to complete.
	 */
	mv88e61xxphy_vtu_wait(sc);
}

static int
mv88e61xxphy_sysctl_link_proc(SYSCTL_HANDLER_ARGS)
{
	struct mv88e61xxphy_port_softc *psc = arg1;
	enum mv88e61xxphy_sysctl_link_type type = arg2;
	uint16_t val;
	unsigned out;

	val = MV88E61XX_READ_PORT(psc, MV88E61XX_PORT_STATUS);
	switch (type) {
	case MV88E61XXPHY_LINK_SYSCTL_DUPLEX:
		if ((val & MV88E61XX_PORT_STATUS_DUPLEX) != 0)
			out = 1;
		else
			out = 0;
		break;
	case MV88E61XXPHY_LINK_SYSCTL_LINK:
		if ((val & MV88E61XX_PORT_STATUS_LINK) != 0)
			out = 1;
		else
			out = 0;
		break;
	case MV88E61XXPHY_LINK_SYSCTL_MEDIA:
		switch (val & MV88E61XX_PORT_STATUS_MEDIA) {
		case MV88E61XX_PORT_STATUS_MEDIA_10M:
			out = 10;
			break;
		case MV88E61XX_PORT_STATUS_MEDIA_100M:
			out = 100;
			break;
		case MV88E61XX_PORT_STATUS_MEDIA_1G:
			out = 1000;
			break;
		default:
			out = 0;
			break;
		}
		break;
	default:
		return (EINVAL);
	}
	return (sysctl_handle_int(oidp, NULL, out, req));
}

static int
mv88e61xxphy_sysctl_port_proc(SYSCTL_HANDLER_ARGS)
{
	struct mv88e61xxphy_port_softc *psc = arg1;
	enum mv88e61xxphy_sysctl_port_type type = arg2;
	struct mv88e61xxphy_softc *sc = psc->sc_switch;
	unsigned max, val, *valp;
	int error;

	switch (type) {
	case MV88E61XXPHY_PORT_SYSCTL_DOMAIN:
		valp = &psc->sc_domain;
		max = 0xf;
		break;
	case MV88E61XXPHY_PORT_SYSCTL_VLAN:
		valp = &psc->sc_vlan;
		max = 0x1000;
		break;
	case MV88E61XXPHY_PORT_SYSCTL_PRIORITY:
		valp = &psc->sc_priority;
		max = 3;
		break;
	default:
		return (EINVAL);
	}

	val = *valp;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/* Bounds check value.  */
	if (val >= max)
		return (EINVAL);

	/* Reinitialize switch with new value.  */
	*valp = val;
	mv88e61xxphy_init(sc);

	return (0);
}

static void
mv88e61xxphy_vtu_load(struct mv88e61xxphy_softc *sc, uint16_t vid)
{
	unsigned port;

	/*
	 * Wait for previous operation to complete.
	 */
	mv88e61xxphy_vtu_wait(sc);

	/*
	 * Set VID.
	 */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_VTU_VID,
	    MV88E61XX_GLOBAL_VTU_VID_VALID | vid);

	/*
	 * Add ports to this VTU.
	 */
	for (port = 0; port < MV88E61XX_PORTS; port++) {
		struct mv88e61xxphy_port_softc *psc;

		psc = &sc->sc_ports[port];
		if (psc->sc_vlan == vid) {
			/*
			 * Send this port its VLAN traffic untagged.
			 */
			psc->sc_flags &= ~MV88E61XXPHY_PORT_FLAG_VTU_UPDATE;
			mv88e61xxphy_vtu_set_membership(sc, port, MV88E61XXPHY_VTU_UNTAGGED);
		} else if (psc->sc_port == MV88E61XX_HOST_PORT) {
			/*
			 * The host sees all VLANs tagged.
			 */
			mv88e61xxphy_vtu_set_membership(sc, port, MV88E61XXPHY_VTU_TAGGED);
		} else {
			/*
			 * This port isn't on this VLAN.
			 */
			mv88e61xxphy_vtu_set_membership(sc, port, MV88E61XXPHY_VTU_DISCARDED);
		}
	}

	/*
	 * Start adding this entry.
	 */
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_VTU_OP,
	    MV88E61XX_GLOBAL_VTU_OP_BUSY |
	    MV88E61XX_GLOBAL_VTU_OP_OP_VTU_LOAD);
}

static void
mv88e61xxphy_vtu_set_membership(struct mv88e61xxphy_softc *sc, unsigned port,
    enum mv88e61xxphy_vtu_membership_type type)
{
	unsigned shift, reg;
	uint16_t bits;
	uint16_t val;

	switch (type) {
	case MV88E61XXPHY_VTU_UNMODIFIED:
		bits = 0;
		break;
	case MV88E61XXPHY_VTU_UNTAGGED:
		bits = 1;
		break;
	case MV88E61XXPHY_VTU_TAGGED:
		bits = 2;
		break;
	case MV88E61XXPHY_VTU_DISCARDED:
		bits = 3;
		break;
	default:
		return;
	}

	if (port < 4) {
		reg = MV88E61XX_GLOBAL_VTU_DATA_P0P3;
		shift = port * 4;
	} else {
		reg = MV88E61XX_GLOBAL_VTU_DATA_P4P5;
		shift = (port - 4) * 4;
	}

	val = MV88E61XX_READ(sc, MV88E61XX_GLOBAL, reg);
	val |= bits << shift;
	MV88E61XX_WRITE(sc, MV88E61XX_GLOBAL, reg, val);
}

static void
mv88e61xxphy_vtu_wait(struct mv88e61xxphy_softc *sc)
{
	uint16_t val;

	for (;;) {
		val = MV88E61XX_READ(sc, MV88E61XX_GLOBAL, MV88E61XX_GLOBAL_VTU_OP);
		if ((val & MV88E61XX_GLOBAL_VTU_OP_BUSY) == 0)
			return;
	}
}

static device_method_t mv88e61xxphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		mv88e61xxphy_probe),
	DEVMETHOD(device_attach,	mv88e61xxphy_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	{ 0, 0 }
};

static devclass_t mv88e61xxphy_devclass;

static driver_t mv88e61xxphy_driver = {
	"mv88e61xxphy",
	mv88e61xxphy_methods,
	sizeof(struct mv88e61xxphy_softc)
};

DRIVER_MODULE(mv88e61xxphy, octe, mv88e61xxphy_driver, mv88e61xxphy_devclass, 0, 0);
