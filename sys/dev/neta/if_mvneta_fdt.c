/*
 * Copyright (c) 2017 Stormshield.
 * Copyright (c) 2017 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_platform.h"
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp_lro.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "if_mvnetareg.h"
#include "if_mvnetavar.h"

#define	PHY_MODE_MAXLEN	10
#define	INBAND_STATUS_MAXLEN 16

static int mvneta_fdt_probe(device_t);
static int mvneta_fdt_attach(device_t);

static device_method_t mvneta_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mvneta_fdt_probe),
	DEVMETHOD(device_attach,	mvneta_fdt_attach),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(mvneta, mvneta_fdt_driver, mvneta_fdt_methods,
    sizeof(struct mvneta_softc), mvneta_driver);

static devclass_t mvneta_fdt_devclass;

DRIVER_MODULE(mvneta, ofwbus, mvneta_fdt_driver, mvneta_fdt_devclass, 0, 0);
DRIVER_MODULE(mvneta, simplebus, mvneta_fdt_driver, mvneta_fdt_devclass, 0, 0);

static int mvneta_fdt_phy_acquire(device_t);

static struct ofw_compat_data compat_data[] = {
	{"marvell,armada-370-neta",	true},
	{"marvell,armada-3700-neta",	true},
	{NULL,				false}
};

static int
mvneta_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "NETA controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mvneta_fdt_attach(device_t dev)
{
	int err;

	/* Try to fetch PHY information from FDT */
	err = mvneta_fdt_phy_acquire(dev);
	if (err != 0)
		return (err);

	return (mvneta_attach(dev));
}

static int
mvneta_fdt_phy_acquire(device_t dev)
{
	struct mvneta_softc *sc;
	phandle_t node, child, phy_handle;
	char phymode[PHY_MODE_MAXLEN];
	char managed[INBAND_STATUS_MAXLEN];
	char *name;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	/* PHY mode is crucial */
	if (OF_getprop(node, "phy-mode", phymode, sizeof(phymode)) <= 0) {
		device_printf(dev, "Failed to acquire PHY mode from FDT.\n");
		return (ENXIO);
	}

	if (strncmp(phymode, "rgmii-id", 8) == 0)
		sc->phy_mode = MVNETA_PHY_RGMII_ID;
	else if (strncmp(phymode, "rgmii", 5) == 0)
		sc->phy_mode = MVNETA_PHY_RGMII;
	else if (strncmp(phymode, "sgmii", 5) == 0)
		sc->phy_mode = MVNETA_PHY_SGMII;
	else if (strncmp(phymode, "qsgmii", 6) == 0)
		sc->phy_mode = MVNETA_PHY_QSGMII;
	else
		sc->phy_mode = MVNETA_PHY_SGMII;

	/* Check if in-band link status will be used */
	if (OF_getprop(node, "managed", managed, sizeof(managed)) > 0) {
		if (strncmp(managed, "in-band-status", 14) == 0) {
			sc->use_inband_status = TRUE;
			device_printf(dev, "Use in-band link status.\n");
			return (0);
		}
	}

	if (OF_getencprop(node, "phy", (void *)&phy_handle,
	    sizeof(phy_handle)) <= 0) {
		/* Test for fixed-link (present i.e. in 388-gp) */
		for (child = OF_child(node); child != 0; child = OF_peer(child)) {
			if (OF_getprop_alloc(child,
			    "name", (void **)&name) <= 0) {
				continue;
			}
			if (strncmp(name, "fixed-link", 10) == 0) {
				free(name, M_OFWPROP);
				if (OF_getencprop(child, "speed",
				    &sc->phy_speed, sizeof(sc->phy_speed)) <= 0) {
					if (bootverbose) {
						device_printf(dev,
						    "No PHY information.\n");
					}
					return (ENXIO);
				}
				if (OF_hasprop(child, "full-duplex"))
					sc->phy_fdx = TRUE;
				else
					sc->phy_fdx = FALSE;

				/* Keep this flag just for the record */
				sc->phy_addr = MII_PHY_ANY;

				return (0);
			}
			free(name, M_OFWPROP);
		}
		if (bootverbose) {
			device_printf(dev,
			    "Could not find PHY information in FDT.\n");
		}
		return (ENXIO);
	} else {
		phy_handle = OF_instance_to_package(phy_handle);
		if (OF_getencprop(phy_handle, "reg", &sc->phy_addr,
		    sizeof(sc->phy_addr)) <= 0) {
			device_printf(dev,
			    "Could not find PHY address in FDT.\n");
			return (ENXIO);
		}
	}

	return (0);
}

int
mvneta_fdt_mac_address(struct mvneta_softc *sc, uint8_t *addr)
{
	phandle_t node;
	uint8_t lmac[ETHER_ADDR_LEN];
	uint8_t zeromac[] = {[0 ... (ETHER_ADDR_LEN - 1)] = 0};
	int len;

	/*
	 * Retrieve hw address from the device tree.
	 */
	node = ofw_bus_get_node(sc->dev);
	if (node == 0)
		return (ENXIO);

	len = OF_getprop(node, "local-mac-address", (void *)lmac, sizeof(lmac));
	if (len != ETHER_ADDR_LEN)
		return (ENOENT);

	if (memcmp(lmac, zeromac, ETHER_ADDR_LEN) == 0) {
		/* Invalid MAC address (all zeros) */
		return (EINVAL);
	}
	memcpy(addr, lmac, ETHER_ADDR_LEN);

	return (0);
}
