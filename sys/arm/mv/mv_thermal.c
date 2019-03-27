/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Rubicon Communications, LLC (Netgate)
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#define	CONTROL0		0x00
#define	 CONTROL0_TSEN_START	(1 << 0)
#define	 CONTROL0_TSEN_RESET	(1 << 1)
#define	 CONTROL0_TSEN_EN	(1 << 2)
#define	 CONTROL0_CHANNEL_SHIFT	13
#define	 CONTROL0_CHANNEL_MASK	0xF
#define	 CONTROL0_OSR_SHIFT	24
#define	 CONTROL0_OSR_MAX	3	/* OSR = 512 * 4uS = ~2mS */
#define	 CONTROL0_MODE_SHIFT	30
#define	 CONTROL0_MODE_EXTERNAL	0x2
#define	 CONTROL0_MODE_MASK	0x3

#define	CONTROL1	0x04
/* This doesn't seems to work */
#define	CONTROL1_TSEN_SENS_SHIFT	21
#define	CONTROL1_TSEN_SENS_MASK		0x7

#define	STATUS			0x00
#define	STATUS_TEMP_MASK	0x3FF

enum mv_thermal_type {
	MV_AP806 = 1,
	MV_CP110,
};

struct mv_thermal_config {
	enum mv_thermal_type	type;
	int			ncpus;
	int64_t			calib_mul;
	int64_t			calib_add;
	int64_t			calib_div;
	uint32_t		valid_mask;
	bool			signed_value;
};

struct mv_thermal_softc {
	device_t		dev;
	struct resource		*res[2];
	struct mtx		mtx;

	struct mv_thermal_config	*config;
	int				cur_sensor;
};

static struct mv_thermal_config mv_ap806_config = {
	.type = MV_AP806,
	.ncpus = 4,
	.calib_mul = 423,
	.calib_add = -150000,
	.calib_div = 100,
	.valid_mask = (1 << 16),
	.signed_value = true,
};

static struct mv_thermal_config mv_cp110_config = {
	.type = MV_CP110,
	.calib_mul = 2000096,
	.calib_add = 1172499100,
	.calib_div = 420100,
	.valid_mask = (1 << 10),
	.signed_value = false,
};

static struct resource_spec mv_thermal_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"marvell,armada-ap806-thermal", (uintptr_t) &mv_ap806_config},
	{"marvell,armada-cp110-thermal", (uintptr_t) &mv_cp110_config},
	{NULL,             0}
};

#define	RD_STA(sc, reg)		bus_read_4((sc)->res[0], (reg))
#define	WR_STA(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))
#define	RD_CON(sc, reg)		bus_read_4((sc)->res[1], (reg))
#define	WR_CON(sc, reg, val)	bus_write_4((sc)->res[1], (reg), (val))

static inline int32_t sign_extend(uint32_t value, int index)
{
	uint8_t shift;

	shift = 31 - index;
	return ((int32_t)(value << shift) >> shift);
}

static int
mv_thermal_wait_sensor(struct mv_thermal_softc *sc)
{
	uint32_t reg;
	uint32_t timeout;

	timeout = 100000;
	while (--timeout > 0) {
		reg = RD_STA(sc, STATUS);
		if ((reg & sc->config->valid_mask) == sc->config->valid_mask)
			break;
		DELAY(100);
	}
	if (timeout == 0) {
		return (ETIMEDOUT);
	}

	return (0);
}

static int
mv_thermal_select_sensor(struct mv_thermal_softc *sc, int sensor)
{
	uint32_t reg;

	if (sc->cur_sensor == sensor)
		return (0);

	/* Stop the current reading and reset the module */
	reg = RD_CON(sc, CONTROL0);
	reg &= ~(CONTROL0_TSEN_START | CONTROL0_TSEN_EN);
	WR_CON(sc, CONTROL0, reg);

	/* Switch to the selected sensor */
	/* 
	 * NOTE : Datasheet says to use CONTROL1 for selecting
	 * but when doing so the sensors >0 are never ready
	 * Do what Linux does using undocumented bits in CONTROL0
	 */
	/* This reset automatically to the sensor 0 */
	reg &= ~(CONTROL0_MODE_MASK << CONTROL0_MODE_SHIFT);
	if (sensor) {
		/* Select external sensor */
		reg |= CONTROL0_MODE_EXTERNAL << CONTROL0_MODE_SHIFT;
		reg &= ~(CONTROL0_CHANNEL_MASK << CONTROL0_CHANNEL_SHIFT);
		reg |= (sensor - 1) << CONTROL0_CHANNEL_SHIFT;
	}
	WR_CON(sc, CONTROL0, reg);
	sc->cur_sensor = sensor;

	/* Start the reading */
	reg = RD_CON(sc, CONTROL0);
	reg |= CONTROL0_TSEN_START | CONTROL0_TSEN_EN;
	WR_CON(sc, CONTROL0, reg);

	return (mv_thermal_wait_sensor(sc));
}

static int
mv_thermal_read_sensor(struct mv_thermal_softc *sc, int sensor, int *temp)
{
	uint32_t reg;
	int64_t sample, rv;

	rv = mv_thermal_select_sensor(sc, sensor);
	if (rv != 0)
		return (rv);

	reg = RD_STA(sc, STATUS) & STATUS_TEMP_MASK;

	if (sc->config->signed_value)
		sample = sign_extend(reg, fls(STATUS_TEMP_MASK) - 1);
	else
		sample = reg;

	*temp = ((sample * sc->config->calib_mul) - sc->config->calib_add) /
		sc->config->calib_div;

	return (0);
}

static int
ap806_init(struct mv_thermal_softc *sc)
{
	uint32_t reg;

	/* Start the temp capture/conversion */
	reg = RD_CON(sc, CONTROL0);
	reg &= ~CONTROL0_TSEN_RESET;
	reg |= CONTROL0_TSEN_START | CONTROL0_TSEN_EN;

	/* Sample every ~2ms */
	reg |= CONTROL0_OSR_MAX << CONTROL0_OSR_SHIFT;

	WR_CON(sc, CONTROL0, reg);

	/* Since we just started the module wait for the sensor to be ready */
	mv_thermal_wait_sensor(sc);

	return (0);
}

static int
cp110_init(struct mv_thermal_softc *sc)
{
	uint32_t reg;

	reg = RD_CON(sc, CONTROL1);
	reg &= (1 << 7);
	reg |= (1 << 8);
	WR_CON(sc, CONTROL1, reg);

	/* Sample every ~2ms */
	reg = RD_CON(sc, CONTROL0);
	reg |= CONTROL0_OSR_MAX << CONTROL0_OSR_SHIFT;
	WR_CON(sc, CONTROL0, reg);

	return (0);
}

static int
mv_thermal_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct mv_thermal_softc *sc;
	device_t dev = arg1;
	int sensor = arg2;
	int val = 0;

	sc = device_get_softc(dev);
	mtx_lock(&(sc)->mtx);

	if (mv_thermal_read_sensor(sc, sensor, &val) == 0) {
		/* Convert to Kelvin */
		val = val + 2732;
	} else {
		device_printf(dev, "Timeout waiting for sensor\n");
	}

	mtx_unlock(&(sc)->mtx);
	return sysctl_handle_opaque(oidp, &val, sizeof(val), req);
}

static int
mv_thermal_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Marvell Thermal Sensor Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_thermal_attach(device_t dev)
{
	struct mv_thermal_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *oid;
	char name[255];
	char desc[255];
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->config = (struct mv_thermal_config *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	if (bus_alloc_resources(dev, mv_thermal_res_spec, sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	sc->cur_sensor = -1;
	switch (sc->config->type) {
	case MV_AP806:
		ap806_init(sc);
		break;
	case MV_CP110:
		cp110_init(sc);
		break;
	}

	ctx = device_get_sysctl_ctx(dev);
	oid = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	/* There is always at least one sensor */
	SYSCTL_ADD_PROC(ctx, oid, OID_AUTO, "internal",
	    CTLTYPE_INT | CTLFLAG_RD,
	    dev, 0, mv_thermal_sysctl,
	    "IK",
	    "Internal Temperature");

	for (i = 0; i < sc->config->ncpus; i++) {
		snprintf(name, sizeof(name), "cpu%d", i);
		snprintf(desc, sizeof(desc), "CPU%d Temperature", i);
		SYSCTL_ADD_PROC(ctx, oid, OID_AUTO, name,
		    CTLTYPE_INT | CTLFLAG_RD,
		    dev, i + 1, mv_thermal_sysctl,
		    "IK",
		    desc);
	}

	return (0);
}

static int
mv_thermal_detach(device_t dev)
{
	struct mv_thermal_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resources(dev, mv_thermal_res_spec, sc->res);

	return (0);
}

static device_method_t mv_thermal_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mv_thermal_probe),
	DEVMETHOD(device_attach,	mv_thermal_attach),
	DEVMETHOD(device_detach,	mv_thermal_detach),

	DEVMETHOD_END
};

static devclass_t mv_thermal_devclass;

static driver_t mv_thermal_driver = {
	"mv_thermal",
	mv_thermal_methods,
	sizeof(struct mv_thermal_softc),
};

DRIVER_MODULE(mv_thermal, simplebus, mv_thermal_driver,
    mv_thermal_devclass, 0, 0);
