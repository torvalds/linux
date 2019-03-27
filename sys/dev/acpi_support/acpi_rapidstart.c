/*-
 * Copyright (c) 2013 Takanori Watanabe
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

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>

#include <contrib/dev/acpica/include/acpi.h>

#include "acpi_if.h"
#include <sys/module.h>
#include <dev/acpica/acpivar.h>
#include <sys/sysctl.h>
static int sysctl_acpi_rapidstart_gen_handler(SYSCTL_HANDLER_ARGS);


static struct acpi_rapidstart_name_list
{
	char *nodename;
	char *getmethod;
	char *setmethod;
	char *comment;
} acpi_rapidstart_oids[] ={
	{"ffs","GFFS","SFFS","Flash Fast Store Flag"},
	{"ftv","GFTV","SFTV","Time value"},
	{NULL, NULL, NULL, NULL}
};

struct acpi_rapidstart_softc {
	struct sysctl_ctx_list	*sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;

};
static char    *rapidstart_ids[] = {"INT3392", NULL};
static int
acpi_rapidstart_probe(device_t dev)
{
	int rv;

	if (acpi_disabled("rapidstart") ||
	    device_get_unit(dev) != 0)
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, rapidstart_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "Intel Rapid Start ACPI device");

	return (rv);
	
}

static int
acpi_rapidstart_attach(device_t dev)
{
	struct acpi_rapidstart_softc *sc;
	int i;

	sc = device_get_softc(dev);
	
	sc->sysctl_ctx = device_get_sysctl_ctx(dev);
	sc->sysctl_tree = device_get_sysctl_tree(dev);
	for (i = 0 ; acpi_rapidstart_oids[i].nodename != NULL; i++){
		if (acpi_rapidstart_oids[i].setmethod != NULL) {
			SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			    i, acpi_rapidstart_oids[i].nodename,
			    CTLTYPE_INT | CTLFLAG_RW,
			    dev, i, sysctl_acpi_rapidstart_gen_handler, "I",
			    acpi_rapidstart_oids[i].comment);
		} else {
			SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			    i, acpi_rapidstart_oids[i].nodename,
			    CTLTYPE_INT | CTLFLAG_RD,
			    dev, i, sysctl_acpi_rapidstart_gen_handler, "I",
			    acpi_rapidstart_oids[i].comment);
		}
	}
	return (0);
}

static int 
sysctl_acpi_rapidstart_gen_handler(SYSCTL_HANDLER_ARGS)
{
	device_t	dev = arg1;
	int 	function = oidp->oid_arg2;
	int		error = 0, val;

	acpi_GetInteger(acpi_get_handle(dev),
	    acpi_rapidstart_oids[function].getmethod, &val);
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr || !acpi_rapidstart_oids[function].setmethod)
		return (error);
	acpi_SetInteger(acpi_get_handle(dev),
	    acpi_rapidstart_oids[function].setmethod, val);
	return (0);
}

static device_method_t acpi_rapidstart_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, acpi_rapidstart_probe),
	DEVMETHOD(device_attach, acpi_rapidstart_attach),

	DEVMETHOD_END
};

static driver_t	acpi_rapidstart_driver = {
	"acpi_rapidstart",
	acpi_rapidstart_methods,
	sizeof(struct acpi_rapidstart_softc),
};

static devclass_t acpi_rapidstart_devclass;

DRIVER_MODULE(acpi_rapidstart, acpi, acpi_rapidstart_driver, acpi_rapidstart_devclass,
	      0, 0);
MODULE_DEPEND(acpi_rapidstart, acpi, 1, 1, 1);

