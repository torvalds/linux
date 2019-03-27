/*-
 * Copyright (c) 2016 Emmanuel Vadot <manu@freebsd.org>
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
 * Allwinner Touch Sreen driver
 * Touch screen part is not done, only the thermal sensor part is.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#define	READ(_sc, _r) bus_read_4((_sc)->res[0], (_r))
#define	WRITE(_sc, _r, _v) bus_write_4((_sc)->res[0], (_r), (_v))

/* Control register 0 */
#define	TP_CTRL0	0x00
#define	 TP_CTRL0_TACQ(x)	((x & 0xFF) << 0)
#define	 TP_CTRL0_FS_DIV(x)	((x & 0xF) << 16)
#define	 TP_CTRL0_CLK_DIV(x)	((x & 0x3) << 20)
#define	 TP_CTRL0_CLK_SELECT(x)	((x & 0x1) << 22)

/* Control register 1 */
#define	TP_CTRL1	0x04
#define	 TP_CTRL1_MODE_EN	(1 << 4)

/* Control register 2 */
#define	TP_CTRL2	0x08

/* Control register 3 */
#define	TP_CTRL3	0x0C

/* Int/FIFO control register */
#define	TP_FIFOC	0x10
#define	 TP_FIFOC_TEMP_IRQ_ENABLE	(1 << 18)

/* Int/FIFO status register */
#define	TP_FIFOS	0x14
#define	 TP_FIFOS_TEMP_IRQ_PENDING	(1 << 18)

/* Temperature Period Register */
#define	TP_TPR		0x18
#define	 TP_TPR_TEMP_EN		(1 << 16)
#define	 TP_TPR_TEMP_PERIOD(x)	(x << 0)

/* Common data register */
#define	TP_CDAT		0x1C

/* Temperature data register */
#define	TEMP_DATA	0x20

/* TP Data register*/
#define	TP_DATA		0x24

/* TP IO config register */
#define	TP_IO_CONFIG	0x28

/* TP IO port data register */
#define	TP_IO_DATA	0x2C

struct aw_ts_softc {
	device_t		dev;
	struct resource *	res[2];
	void *			intrhand;
	int			temp_data;
	int			temp_offset;
	int			temp_step;
};

static struct resource_spec aw_ts_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

#define	A10_TS	1
#define	A13_TS	2

#define	AW_TS_TEMP_SYSCTL	1

static struct ofw_compat_data compat_data[] = {
	{"allwinner,sun4i-a10-ts", A10_TS},
	{"allwinner,sun5i-a13-ts", A13_TS},
	{NULL,             0}
};

static void
aw_ts_intr(void *arg)
{
	struct aw_ts_softc *sc;
	int val;

	sc= (struct aw_ts_softc *)arg;

	val = READ(sc, TP_FIFOS);
	if (val & TP_FIFOS_TEMP_IRQ_PENDING) {
		/* Convert the value to millicelsius then millikelvin */
		sc->temp_data = (READ(sc, TEMP_DATA) * sc->temp_step - sc->temp_offset)
			+ 273150;
	}

	WRITE(sc, TP_FIFOS, val);
}

static int
aw_ts_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner Touch Screen controller");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_ts_attach(device_t dev)
{
	struct aw_ts_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, aw_ts_spec, sc->res) != 0) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->res[1],
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, aw_ts_intr, sc,
	    &sc->intrhand)) {
		bus_release_resources(dev, aw_ts_spec, sc->res);
		device_printf(dev, "cannot setup interrupt handler\n");
		return (ENXIO);
	}

	/*
	 * Thoses magic values were taken from linux which take them from
	 * the allwinner SDK or found them by deduction
	 */
	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
	case A10_TS:
		sc->temp_offset = 257000;
		sc->temp_step = 133;
		break;
	case A13_TS:
		sc->temp_offset = 144700;
		sc->temp_step = 100;
		break;
	}

	/* Enable clock and set divisers */
	WRITE(sc, TP_CTRL0, TP_CTRL0_CLK_SELECT(0) |
	  TP_CTRL0_CLK_DIV(2) |
	  TP_CTRL0_FS_DIV(7) |
	  TP_CTRL0_TACQ(63));

	/* Enable TS module */
	WRITE(sc, TP_CTRL1, TP_CTRL1_MODE_EN);

	/* Enable Temperature, period is ~2s */
	WRITE(sc, TP_TPR, TP_TPR_TEMP_EN | TP_TPR_TEMP_PERIOD(1953));

	/* Enable temp irq */
	WRITE(sc, TP_FIFOC, TP_FIFOC_TEMP_IRQ_ENABLE);

	/* Add sysctl */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD,
	    &sc->temp_data, 0, sysctl_handle_int,
	    "IK3", "CPU Temperature");

	return (0);
}

static device_method_t aw_ts_methods[] = {
	DEVMETHOD(device_probe, aw_ts_probe),
	DEVMETHOD(device_attach, aw_ts_attach),

	DEVMETHOD_END
};

static driver_t aw_ts_driver = {
	"aw_ts",
	aw_ts_methods,
	sizeof(struct aw_ts_softc),
};
static devclass_t aw_ts_devclass;

DRIVER_MODULE(aw_ts, simplebus, aw_ts_driver, aw_ts_devclass, 0, 0);
