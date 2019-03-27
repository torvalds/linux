/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <dev/bhnd/bhnd_ids.h>
#include <dev/bhnd/bhndb/bhndbvar.h>
#include <dev/bhnd/bhndb/bhndb_hwdata.h>

#include "bcmavar.h"

#include "bcma_eromreg.h"
#include "bcma_eromvar.h"

/*
 * Supports attachment of bcma(4) bus devices via a bhndb bridge.
 */

static int
bcma_bhndb_probe(device_t dev)
{
	const struct bhnd_chipid	*cid;
	int				 error;

	/* Defer to default probe implementation */
	if ((error = bcma_probe(dev)) > 0)
		return (error);

	/* Check bus type */
	cid = BHNDB_GET_CHIPID(device_get_parent(dev), dev);
	if (cid->chip_type != BHND_CHIPTYPE_BCMA)
		return (ENXIO);

	/* Set device description */
	bhnd_set_default_bus_desc(dev, cid);

	return (error);
}

static int
bcma_bhndb_attach(device_t dev)
{
	int error;

	/* Perform initial attach and enumerate our children. */
	if ((error = bcma_attach(dev)))
		goto failed;

	/* Delegate remainder to standard bhnd method implementation */
	if ((error = bhnd_generic_attach(dev)))
		goto failed;

	return (0);

failed:
	device_delete_children(dev);
	return (error);
}

static int
bcma_bhndb_suspend_child(device_t dev, device_t child)
{
	struct bcma_devinfo	*dinfo;
	int			 error;

	if (device_get_parent(child) != dev)
		BUS_SUSPEND_CHILD(device_get_parent(dev), child);
	
	if (device_is_suspended(child))
		return (EBUSY);

	dinfo = device_get_ivars(child);

	/* Suspend the child */
	if ((error = bhnd_generic_br_suspend_child(dev, child)))
		return (error);

	/* Suspend child's agent resource  */
	if (dinfo->res_agent != NULL)
		BHNDB_SUSPEND_RESOURCE(device_get_parent(dev), dev,
		    SYS_RES_MEMORY, dinfo->res_agent->res);
	
	return (0);
}

static int
bcma_bhndb_resume_child(device_t dev, device_t child)
{
	struct bcma_devinfo	*dinfo;
	int			 error;

	if (device_get_parent(child) != dev)
		BUS_SUSPEND_CHILD(device_get_parent(dev), child);

	if (!device_is_suspended(child))
		return (EBUSY);

	dinfo = device_get_ivars(child);

	/* Resume child's agent resource  */
	if (dinfo->res_agent != NULL) {
		error = BHNDB_RESUME_RESOURCE(device_get_parent(dev), dev,
		    SYS_RES_MEMORY, dinfo->res_agent->res);
		if (error)
			return (error);
	}

	/* Resume the child */
	if ((error = bhnd_generic_br_resume_child(dev, child))) {
		/* On failure, re-suspend the agent resource */
		if (dinfo->res_agent != NULL) {
			BHNDB_SUSPEND_RESOURCE(device_get_parent(dev), dev,
			    SYS_RES_MEMORY, dinfo->res_agent->res);
		}

		return (error);
	}

	return (0);
}

static device_method_t bcma_bhndb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bcma_bhndb_probe),
	DEVMETHOD(device_attach,		bcma_bhndb_attach),

	/* Bus interface */
	DEVMETHOD(bus_suspend_child,		bcma_bhndb_suspend_child),
	DEVMETHOD(bus_resume_child,		bcma_bhndb_resume_child),

	DEVMETHOD_END
};

DEFINE_CLASS_2(bhnd, bcma_bhndb_driver, bcma_bhndb_methods,
    sizeof(struct bcma_softc), bhnd_bhndb_driver, bcma_driver);

DRIVER_MODULE(bcma_bhndb, bhndb, bcma_bhndb_driver, bhnd_devclass, NULL, NULL);
 
MODULE_VERSION(bcma_bhndb, 1);
MODULE_DEPEND(bcma_bhndb, bcma, 1, 1, 1);
MODULE_DEPEND(bcma_bhndb, bhnd, 1, 1, 1);
MODULE_DEPEND(bcma_bhndb, bhndb, 1, 1, 1);
