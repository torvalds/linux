/*-
 * Copyright 2015 John Wehle <john@feith.com>
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

/*
 * Amlogic aml8726 clock control module driver.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>
#include <arm/amlogic/aml8726/aml8726_ccm.h>


struct aml8726_ccm_softc {
	device_t			dev;
	struct aml8726_ccm_function	*soc;
	struct resource			*res[1];
	struct mtx			mtx;
};

static struct resource_spec aml8726_ccm_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_CCM_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	AML_CCM_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_CCM_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "ccm", MTX_DEF)
#define	AML_CCM_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

static int
aml8726_ccm_configure_gates(struct aml8726_ccm_softc *sc)
{
	struct aml8726_ccm_function *f;
	struct aml8726_ccm_gate *g;
	char *function_name;
	char *functions;
	phandle_t node;
	ssize_t len;
	uint32_t value;

	node = ofw_bus_get_node(sc->dev);

	len = OF_getprop_alloc(node, "functions",
	    (void **)&functions);

	if (len < 0) {
		device_printf(sc->dev, "missing functions attribute in FDT\n");
		return (ENXIO);
	}

	function_name = functions;

	while (len) {
		for (f = sc->soc; f->name != NULL; f++)
			if (strncmp(f->name, function_name, len) == 0)
				break;

		if (f->name == NULL) {
			/* display message prior to queuing up next string */
			device_printf(sc->dev,
			    "unknown function attribute %.*s in FDT\n",
			    len, function_name);
		}

		/* queue up next string */
		while (*function_name && len) {
			function_name++;
			len--;
		}
		if (len) {
			function_name++;
			len--;
		}

		if (f->name == NULL)
			continue;

		AML_CCM_LOCK(sc);

		/*
		 * Enable the clock gates necessary for the function.
		 *
		 * In some cases a clock may be shared across functions
		 * (meaning don't disable a clock without ensuring that
		 * it's not required by someone else).
		 */
		for (g = f->gates; g->bits != 0x00000000; g++) {
			value = CSR_READ_4(sc, g->addr);
			value |= g->bits;
			CSR_WRITE_4(sc, g->addr, value);
		}

		AML_CCM_UNLOCK(sc);
	}

	OF_prop_free(functions);

	return (0);
}

static int
aml8726_ccm_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-ccm"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 ccm");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_ccm_attach(device_t dev)
{
	struct aml8726_ccm_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M3:
		sc->soc = aml8726_m3_ccm;
		break;
	case AML_SOC_HW_REV_M6:
		sc->soc = aml8726_m6_ccm;
		break;
	case AML_SOC_HW_REV_M8:
		sc->soc = aml8726_m8_ccm;
		break;
	case AML_SOC_HW_REV_M8B:
		sc->soc = aml8726_m8b_ccm;
		break;
	default:
		device_printf(dev, "unsupported SoC\n");
		return (ENXIO);
		/* NOTREACHED */
	}

	if (bus_alloc_resources(dev, aml8726_ccm_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	AML_CCM_LOCK_INIT(sc);

	return (aml8726_ccm_configure_gates(sc));
}

static int
aml8726_ccm_detach(device_t dev)
{
	struct aml8726_ccm_softc *sc = device_get_softc(dev);

	AML_CCM_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_ccm_spec, sc->res);

	return (0);
}

static device_method_t aml8726_ccm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_ccm_probe),
	DEVMETHOD(device_attach,	aml8726_ccm_attach),
	DEVMETHOD(device_detach,	aml8726_ccm_detach),

	DEVMETHOD_END
};

static driver_t aml8726_ccm_driver = {
	"ccm",
	aml8726_ccm_methods,
	sizeof(struct aml8726_ccm_softc),
};

static devclass_t aml8726_ccm_devclass;

EARLY_DRIVER_MODULE(ccm, simplebus, aml8726_ccm_driver,
    aml8726_ccm_devclass, 0, 0,  BUS_PASS_CPU + BUS_PASS_ORDER_LATE);
