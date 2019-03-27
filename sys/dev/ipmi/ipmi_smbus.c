/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/selinfo.h>

#include <dev/smbus/smbconf.h>
#include <dev/smbus/smbus.h>
#include <dev/smbus/smb.h>

#include "smbus_if.h"

#ifdef LOCAL_MODULE
#include <ipmivars.h>
#else
#include <dev/ipmi/ipmivars.h>
#endif

static void ipmi_smbus_identify(driver_t *driver, device_t parent);
static int ipmi_smbus_probe(device_t dev);
static int ipmi_smbus_attach(device_t dev);

static void
ipmi_smbus_identify(driver_t *driver, device_t parent)
{
	struct ipmi_get_info info;

	if (ipmi_smbios_identify(&info) && info.iface_type == SSIF_MODE &&
	    device_find_child(parent, "ipmi", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "ipmi", -1);
}

static int
ipmi_smbus_probe(device_t dev)
{

	device_set_desc(dev, "IPMI System Interface");
	return (BUS_PROBE_DEFAULT);
}

static int
ipmi_smbus_attach(device_t dev)
{
	struct ipmi_softc *sc = device_get_softc(dev);
	struct ipmi_get_info info;
	int error;

	/* This should never fail. */
	if (!ipmi_smbios_identify(&info))
		return (ENXIO);

	if (info.iface_type != SSIF_MODE) {
		device_printf(dev, "No SSIF IPMI interface found\n");
		return (ENXIO);
	}

	sc->ipmi_dev = dev;

	if (info.irq != 0) {
		sc->ipmi_irq_rid = 0;
		sc->ipmi_irq_res = bus_alloc_resource(dev, SYS_RES_IRQ,
		    &sc->ipmi_irq_rid, info.irq, info.irq, 1,
		    RF_SHAREABLE | RF_ACTIVE);
	}

	device_printf(dev, "SSIF mode found at address 0x%llx on %s\n",
	    (long long)info.address, device_get_name(device_get_parent(dev)));
	error = ipmi_ssif_attach(sc, device_get_parent(dev), info.address);
	if (error)
		goto bad;

	error = ipmi_attach(dev);
	if (error)
		goto bad;

	return (0);
bad:
	ipmi_release_resources(dev);
	return (error);
}

static device_method_t ipmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	ipmi_smbus_identify),
	DEVMETHOD(device_probe,		ipmi_smbus_probe),
	DEVMETHOD(device_attach,	ipmi_smbus_attach),
	DEVMETHOD(device_detach,	ipmi_detach),
	{ 0, 0 }
};

static driver_t ipmi_smbus_driver = {
	"ipmi",
	ipmi_methods,
	sizeof(struct ipmi_softc)
};

DRIVER_MODULE(ipmi_smbus, smbus, ipmi_smbus_driver, ipmi_devclass, 0, 0);
MODULE_DEPEND(ipmi_smbus, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
