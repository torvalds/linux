/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Poul-Henning Kamp.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pccard/pccardvar.h>

#include <dev/puc/puc_cfg.h>
#include <dev/puc/puc_bfe.h>

/* http://www.argosy.com.tw/product/sp320.htm */
const struct puc_cfg puc_pccard_rscom = {
	0, 0, 0, 0,
	"ARGOSY SP320 Dual port serial PCMCIA",
	DEFAULT_RCLK,
	PUC_PORT_2S, 0, 1, 0,
};

static int
puc_pccard_probe(device_t dev)
{
	const char *vendor, *product;
	int error;

	error = pccard_get_vendor_str(dev, &vendor);
	if (error)
		return(error);
	error = pccard_get_product_str(dev, &product);
	if (error)
		return(error);
	if (!strcmp(vendor, "PCMCIA") && !strcmp(product, "RS-COM 2P"))
		return (puc_bfe_probe(dev, &puc_pccard_rscom));

	return (ENXIO);
}

static device_method_t puc_pccard_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		puc_pccard_probe),
    DEVMETHOD(device_attach,		puc_bfe_attach),
    DEVMETHOD(device_detach,		puc_bfe_detach),

    DEVMETHOD(bus_alloc_resource,	puc_bus_alloc_resource),
    DEVMETHOD(bus_release_resource,	puc_bus_release_resource),
    DEVMETHOD(bus_get_resource,		puc_bus_get_resource),
    DEVMETHOD(bus_read_ivar,		puc_bus_read_ivar),
    DEVMETHOD(bus_setup_intr,		puc_bus_setup_intr),
    DEVMETHOD(bus_teardown_intr,	puc_bus_teardown_intr),
    DEVMETHOD(bus_print_child,		puc_bus_print_child),
    DEVMETHOD(bus_child_pnpinfo_str,	puc_bus_child_pnpinfo_str),
    DEVMETHOD(bus_child_location_str,	puc_bus_child_location_str),

    DEVMETHOD_END
};

static driver_t puc_pccard_driver = {
	puc_driver_name,
	puc_pccard_methods,
	sizeof(struct puc_softc),
};

DRIVER_MODULE(puc, pccard, puc_pccard_driver, puc_devclass, 0, 0);
