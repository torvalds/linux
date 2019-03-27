/*-
 * Copyright (c) 2017 Semihalf.
 * Copyright (c) 2017 Stormshield.
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
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/fdt.h>

#include <dev/ofw/ofw_bus_subr.h>

#define	READOUT_TO_C(temp)	((temp) / 1000)

#define	STAT_RID		0
#define	CTRL_RID		1

#define	TSEN_STAT_READOUT_VALID	0x1

#define	A380_TSEN_CTRL_RESET	(1 << 8)

struct armada_thermal_softc;

typedef struct armada_thermal_data {
	/* Initialize the sensor */
	void (*tsen_init)(struct armada_thermal_softc *);

	/* Test for a valid sensor value */
	boolean_t (*is_valid)(struct armada_thermal_softc *);

	/* Formula coefficients: temp = (b + m * reg) / div */
	u_long coef_b;
	u_long coef_m;
	u_long coef_div;

	boolean_t inverted;

	/* Shift and mask to access the sensor temperature */
	u_int temp_shift;
	u_int temp_mask;
	u_int is_valid_shift;
} armada_tdata_t;

static boolean_t armada_tsen_readout_valid(struct armada_thermal_softc *);
static int armada_tsen_get_temp(struct armada_thermal_softc *, u_long *);
static void armada380_tsen_init(struct armada_thermal_softc *);
static void armada_temp_update(void *);

static const armada_tdata_t armada380_tdata = {
	.tsen_init = armada380_tsen_init,
	.is_valid = armada_tsen_readout_valid,
	.is_valid_shift = 10,
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = 1172499100UL,
	.coef_m = 2000096UL,
	.coef_div = 4201,
	.inverted = TRUE,
};

static int armada_thermal_probe(device_t);
static int armada_thermal_attach(device_t);
static int armada_thermal_detach(device_t);

static device_method_t armada_thermal_methods[] = {
	DEVMETHOD(device_probe,		armada_thermal_probe),
	DEVMETHOD(device_attach,	armada_thermal_attach),
	DEVMETHOD(device_detach,	armada_thermal_detach),

	DEVMETHOD_END
};

struct armada_thermal_softc {
	device_t		dev;

	struct resource		*stat_res;
	struct resource		*ctrl_res;

	struct callout		temp_upd;
	struct mtx		temp_upd_mtx;

	const armada_tdata_t	*tdata;

	u_long			chip_temperature;
};

static driver_t	armada_thermal_driver = {
	"armada_thermal",
	armada_thermal_methods,
	sizeof(struct armada_thermal_softc)
};

static devclass_t armada_thermal_devclass;

DRIVER_MODULE(armada_thermal, simplebus, armada_thermal_driver,
        armada_thermal_devclass, 0, 0);
DRIVER_MODULE(armada_thermal, ofwbus, armada_thermal_driver,
        armada_thermal_devclass, 0, 0);

static int
armada_thermal_probe(device_t dev)
{
	struct armada_thermal_softc *sc;

	sc = device_get_softc(dev);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "marvell,armada380-thermal")) {
		device_set_desc(dev, "Armada380 Thermal Control");
		sc->tdata = &armada380_tdata;

		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
armada_thermal_attach(device_t dev)
{
	struct armada_thermal_softc *sc;
	const armada_tdata_t *tdata;
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid_list *schildren;
	int timeout;
	int rid;

	sc = device_get_softc(dev);

	/* Allocate CTRL and STAT register spaces */
	rid = STAT_RID;
	sc->stat_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->stat_res == NULL) {
		device_printf(dev,
		    "Could not allocate memory for the status register\n");
		return (ENXIO);
	}

	rid = CTRL_RID;
	sc->ctrl_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->ctrl_res == NULL) {
		device_printf(dev,
		    "Could not allocate memory for the control register\n");
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->stat_res), sc->stat_res);
		sc->stat_res = NULL;
		return (ENXIO);
	}

	/* Now initialize the sensor */
	tdata = sc->tdata;
	tdata->tsen_init(sc);
	/* Set initial temperature value */
	for (timeout = 1000; timeout > 0; timeout--) {
		if (armada_tsen_get_temp(sc, &sc->chip_temperature) == 0)
			break;
		DELAY(10);
	}
	if (timeout <= 0) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->stat_res), sc->stat_res);
		sc->stat_res = NULL;
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->ctrl_res), sc->ctrl_res);
		sc->ctrl_res = NULL;
		return (ENXIO);
	}
	/* Initialize mutex */
	mtx_init(&sc->temp_upd_mtx, "Armada Thermal", NULL, MTX_DEF);
	/* Set up the temperature update callout */
	callout_init_mtx(&sc->temp_upd, &sc->temp_upd_mtx, 0);
	/* Schedule callout */
	callout_reset(&sc->temp_upd, hz, armada_temp_update, sc);

	sctx = device_get_sysctl_ctx(dev);
	schildren = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_LONG(sctx, schildren, OID_AUTO, "temperature",
	    CTLFLAG_RD, &sc->chip_temperature, "SoC temperature");

	return (0);
}

static int
armada_thermal_detach(device_t dev)
{
	struct armada_thermal_softc *sc;

	sc = device_get_softc(dev);

	if (!device_is_attached(dev))
		return (0);

	callout_drain(&sc->temp_upd);

	sc->chip_temperature = 0;

	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(sc->stat_res), sc->stat_res);
	sc->stat_res = NULL;

	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(sc->ctrl_res), sc->ctrl_res);
	sc->ctrl_res = NULL;

	return (0);
}

static boolean_t
armada_tsen_readout_valid(struct armada_thermal_softc *sc)
{
	const armada_tdata_t *tdata;
	uint32_t tsen_stat;
	boolean_t is_valid;

	tdata = sc->tdata;
	tsen_stat = bus_read_4(sc->stat_res, 0);

	tsen_stat >>= tdata->is_valid_shift;
	is_valid = ((tsen_stat & TSEN_STAT_READOUT_VALID) != 0);

	return (is_valid);
}

static int
armada_tsen_get_temp(struct armada_thermal_softc *sc, u_long *temp)
{
	const armada_tdata_t *tdata;
	uint32_t reg;
	u_long tmp;
	u_long m, b, div;

	tdata = sc->tdata;
	/* Check if the readout is valid */
	if ((tdata->is_valid != NULL) && !tdata->is_valid(sc))
		return (EIO);

	reg = bus_read_4(sc->stat_res, 0);
	reg = (reg >> tdata->temp_shift) & tdata->temp_mask;

	/* Get formula coefficients */
	b = tdata->coef_b;
	m = tdata->coef_m;
	div = tdata->coef_div;

	if (tdata->inverted)
		tmp = ((m * reg) - b) / div;
	else
		tmp = (b - (m * reg)) / div;

	*temp = READOUT_TO_C(tmp);

	return (0);
}

static void
armada380_tsen_init(struct armada_thermal_softc *sc)
{
	uint32_t tsen_ctrl;

	tsen_ctrl = bus_read_4(sc->ctrl_res, 0);
	if ((tsen_ctrl & A380_TSEN_CTRL_RESET) == 0) {
		tsen_ctrl |= A380_TSEN_CTRL_RESET;
		bus_write_4(sc->ctrl_res, 0, tsen_ctrl);
		DELAY(10000);
	}
}

static void
armada_temp_update(void *arg)
{
	struct armada_thermal_softc *sc;

	sc = arg;
	/* Update temperature value, keel old if the readout is not valid */
	(void)armada_tsen_get_temp(sc, &sc->chip_temperature);

	callout_reset(&sc->temp_upd, hz, armada_temp_update, sc);
}
