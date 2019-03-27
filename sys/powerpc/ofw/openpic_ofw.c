/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2003 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/rman.h>

#include <machine/openpicreg.h>
#include <machine/openpicvar.h>

#include "pic_if.h"

/*
 * OFW interface
 */
static int	openpic_ofw_probe(device_t);
static int	openpic_ofw_attach(device_t);

static void	openpic_ofw_translate_code(device_t, u_int irq, int code,
		    enum intr_trigger *trig, enum intr_polarity *pol);

static device_method_t  openpic_ofw_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		openpic_ofw_probe),
	DEVMETHOD(device_attach,	openpic_ofw_attach),
	DEVMETHOD(device_suspend,	openpic_suspend),
	DEVMETHOD(device_resume,	openpic_resume),

	/* PIC interface */
	DEVMETHOD(pic_bind,		openpic_bind),
	DEVMETHOD(pic_config,		openpic_config),
	DEVMETHOD(pic_dispatch,		openpic_dispatch),
	DEVMETHOD(pic_enable,		openpic_enable),
	DEVMETHOD(pic_eoi,		openpic_eoi),
	DEVMETHOD(pic_ipi,		openpic_ipi),
	DEVMETHOD(pic_mask,		openpic_mask),
	DEVMETHOD(pic_unmask,		openpic_unmask),

	DEVMETHOD(pic_translate_code,	openpic_ofw_translate_code),

	DEVMETHOD_END
};

static driver_t openpic_ofw_driver = {
	"openpic",
	openpic_ofw_methods,
	sizeof(struct openpic_softc),
};

EARLY_DRIVER_MODULE(openpic, ofwbus, openpic_ofw_driver, openpic_devclass,
    0, 0, BUS_PASS_INTERRUPT);
EARLY_DRIVER_MODULE(openpic, simplebus, openpic_ofw_driver, openpic_devclass,
    0, 0, BUS_PASS_INTERRUPT);
EARLY_DRIVER_MODULE(openpic, macio, openpic_ofw_driver, openpic_devclass, 0, 0,
    BUS_PASS_INTERRUPT);

static int
openpic_ofw_probe(device_t dev)
{
	const char *type = ofw_bus_get_type(dev);

	if (type == NULL)
                return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "chrp,open-pic") &&
	    strcmp(type, "open-pic") != 0)
                return (ENXIO);

	/*
	 * On some U4 systems, there is a phantom MPIC in the mac-io cell.
	 * The uninorth driver will pick up the real PIC, so ignore it here.
	 */
	if (OF_finddevice("/u4") != (phandle_t)-1)
		return (ENXIO);

	device_set_desc(dev, OPENPIC_DEVSTR);
	return (0);
}

static int
openpic_ofw_attach(device_t dev)
{
	struct openpic_softc *sc;
	phandle_t xref, node;

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);

	if (OF_getencprop(node, "phandle", &xref, sizeof(xref)) == -1 &&
	    OF_getencprop(node, "ibm,phandle", &xref, sizeof(xref)) == -1 &&
	    OF_getencprop(node, "linux,phandle", &xref, sizeof(xref)) == -1)
		xref = node;
	
	if (ofw_bus_is_compatible(dev, "fsl,mpic"))
		sc->sc_quirks = OPENPIC_QUIRK_SINGLE_BIND;

	return (openpic_common_attach(dev, xref));
}

static void
openpic_ofw_translate_code(device_t dev, u_int irq, int code,
    enum intr_trigger *trig, enum intr_polarity *pol)
{
	switch (code) {
	case 0:
		/* L to H edge */
		*trig = INTR_TRIGGER_EDGE;
		*pol = INTR_POLARITY_HIGH;
		break;
	case 1:
		/* Active L level */
		*trig = INTR_TRIGGER_LEVEL;
		*pol = INTR_POLARITY_LOW;
		break;
	case 2:
		/* Active H level */
		*trig = INTR_TRIGGER_LEVEL;
		*pol = INTR_POLARITY_HIGH;
		break;
	case 3:
		/* H to L edge */
		*trig = INTR_TRIGGER_EDGE;
		*pol = INTR_POLARITY_LOW;
		break;
	default:
		*trig = INTR_TRIGGER_CONFORM;
		*pol = INTR_POLARITY_CONFORM;
	}
}

