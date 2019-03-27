/*-
 * Copyright (C) 2018 Justin Hibbits
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "opal.h"

struct opal_sensor_softc {
	device_t	 sc_dev;
	struct mtx	 sc_mtx;
	uint32_t	 sc_handle;
	uint32_t	 sc_min_handle;
	uint32_t	 sc_max_handle;
	char		*sc_label;
	int		 sc_type;
};

#define	SENSOR_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	SENSOR_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	SENSOR_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), \
	    "opal-sensor", MTX_DEF)

/*
 * A bit confusing, maybe.  There are two types of nodes with compatible strings
 * of "ibm,opal-sensor".  One hangs off /ibm,opal/, named "sensors", the other
 * hangs off of this node.  For newbus attachments, we have one node (opalsens)
 * attach from opal0, and the others (opal_sensor) attach from opalsens.  These
 * are the real sensors.
 */
enum opal_sensor_type {
	OPAL_SENSOR_TEMP	= 0,	/* From OPAL: degC */
	OPAL_SENSOR_FAN		= 1,	/* From OPAL: RPM */
	OPAL_SENSOR_POWER	= 2,	/* From OPAL: W */
	OPAL_SENSOR_IN		= 3,	/* From OPAL: mV */
	OPAL_SENSOR_ENERGY	= 4,	/* From OPAL: uJ */
	OPAL_SENSOR_CURR	= 5,	/* From OPAL: mA */
	OPAL_SENSOR_MAX
};

/* This must be kept sorted with the enum above. */
const char *opal_sensor_types[] = {
	"temp",
	"fan",
	"power",
	"in",
	"energy",
	"curr"
};

/*
 * Retrieve the raw value from OPAL.  This will be cooked by the sysctl handler.
 */
static int
opal_sensor_get_val(uint32_t key, uint64_t *val)
{
	struct opal_msg msg;
	uint32_t val32;
	int rv, token;

	token = opal_alloc_async_token();
	rv = opal_call(OPAL_SENSOR_READ, key, token, vtophys(&val32));

	if (rv == OPAL_ASYNC_COMPLETION) {
		/* Sleep a little to let things settle. */
		DELAY(100);
		bzero(&msg, sizeof(msg));
		rv = opal_wait_completion(&msg, sizeof(msg), token);

		if (rv == OPAL_SUCCESS)
			val32 = msg.params[0];
	}

	if (rv == OPAL_SUCCESS)
		*val = val32;
	else
		rv = EIO;
	
	opal_free_async_token(token);
	return (rv);
}

static int
opal_sensor_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct opal_sensor_softc *sc;
	int error, result;
	uint32_t sensor;
	uint64_t sensval;

	sc = arg1;
	sensor = arg2;

	SENSOR_LOCK(sc);
	error = opal_sensor_get_val(sensor, &sensval);
	SENSOR_UNLOCK(sc);

	if (error)
		return (error);

	result = sensval;
	
	switch (sc->sc_type) {
	case OPAL_SENSOR_TEMP:
		result = result * 10 + 2731; /* Convert to K */
		break;
	case OPAL_SENSOR_POWER:
		result = result * 1000; /* Convert to mW */
		break;
	}

	error = sysctl_handle_int(oidp, &result, 0, req);

	return (error);
}

static int
opal_sensor_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "ibm,opal-sensor"))
		return (ENXIO);

	device_set_desc(dev, "OPAL sensor");
	return (BUS_PROBE_GENERIC);
}

static int
opal_sensor_attach(device_t dev)
{
	struct opal_sensor_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	char		type[8];
	phandle_t	node;
	cell_t		sensor_id;
	int		i;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	node = ofw_bus_get_node(dev);
	
	if (OF_getencprop(node, "sensor-data", &sensor_id, sizeof(sensor_id)) < 0) {
		device_printf(dev, "Missing sensor ID\n");
		return (ENXIO);
	}
	if (OF_getprop(node, "sensor-type", type, sizeof(type)) < 0) {
		device_printf(dev, "Missing sensor type\n");
		return (ENXIO);
	}
	
	sc->sc_type = -1;
	for (i = 0; i < OPAL_SENSOR_MAX; i++) {
		if (strcmp(type, opal_sensor_types[i]) == 0) {
			sc->sc_type = i;
			break;
		}
	}
	if (sc->sc_type == -1) {
		device_printf(dev, "Unknown sensor type '%s'\n", type);
		return (ENXIO);
	}

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	sc->sc_handle = sensor_id;
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "sensor", CTLTYPE_INT | CTLFLAG_RD, sc, sensor_id,
	    opal_sensor_sysctl, (sc->sc_type == OPAL_SENSOR_TEMP) ? "IK" : "I",
	    "current value");

	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "type",
	    CTLFLAG_RD, __DECONST(char *, opal_sensor_types[sc->sc_type]),
	    0, "");

	OF_getprop_alloc(node, "label", (void **)&sc->sc_label);
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "label",
	    CTLFLAG_RD, sc->sc_label, 0, "");

	if (OF_getprop(node, "sensor-data-min",
	    &sensor_id, sizeof(sensor_id)) > 0) {
		sc->sc_min_handle = sensor_id;
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "sensor_min", CTLTYPE_INT | CTLFLAG_RD, sc, sensor_id,
		    opal_sensor_sysctl,
		    (sc->sc_type == OPAL_SENSOR_TEMP) ? "IK" : "I",
		    "minimum value");
	}

	if (OF_getprop(node, "sensor-data-max",
	    &sensor_id, sizeof(sensor_id)) > 0) {
		sc->sc_max_handle = sensor_id;
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "sensor_max", CTLTYPE_INT | CTLFLAG_RD, sc, sensor_id,
		    opal_sensor_sysctl,
		    (sc->sc_type == OPAL_SENSOR_TEMP) ? "IK" : "I",
		    "maximum value");
	}

	SENSOR_LOCK_INIT(sc);

	return (0);
}

static device_method_t opal_sensor_methods[] = {
	DEVMETHOD(device_probe,		opal_sensor_probe),
	DEVMETHOD(device_attach,		opal_sensor_attach),
};

static driver_t opal_sensor_driver = {
        "opal_sensor",
        opal_sensor_methods,
        sizeof(struct opal_sensor_softc)
};

static devclass_t opal_sensor_devclass;

DRIVER_MODULE(opal_sensor, opalsens, opal_sensor_driver, opal_sensor_devclass,
    NULL, NULL);


static int
opalsens_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "ibm,opal-sensor"))
		return (ENXIO);

	device_set_desc(dev, "OPAL Sensors");
	return (BUS_PROBE_GENERIC);
}

static int 
opalsens_attach(device_t dev)
{
	phandle_t child;
	device_t cdev;
	struct ofw_bus_devinfo *dinfo;

	for (child = OF_child(ofw_bus_get_node(dev)); child != 0;
	    child = OF_peer(child)) {
		dinfo = malloc(sizeof(*dinfo), M_DEVBUF, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(dinfo, child) != 0) {
			free(dinfo, M_DEVBUF);
			continue;
		}
		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    dinfo->obd_name);
			ofw_bus_gen_destroy_devinfo(dinfo);
			free(dinfo, M_DEVBUF);
			continue;
		}
		device_set_ivars(cdev, dinfo);
	}

	return (bus_generic_attach(dev));
}

static const struct ofw_bus_devinfo *
opalsens_get_devinfo(device_t dev, device_t child)
{
        return (device_get_ivars(child));
}

static device_method_t opalsens_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		opalsens_probe),
	DEVMETHOD(device_attach,	opalsens_attach),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	opalsens_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t opalsens_driver = {
        "opalsens",
        opalsens_methods,
        0
};

static devclass_t opalsens_devclass;

DRIVER_MODULE(opalsens, opal, opalsens_driver, opalsens_devclass, NULL, NULL);
