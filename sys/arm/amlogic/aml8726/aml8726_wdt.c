/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 *
 */

/*
 * Amlogic aml8726 watchdog driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <sys/watchdog.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>


struct aml8726_wdt_softc {
	device_t		dev;
	struct resource	*	res[2];
	struct mtx		mtx;
	void *			ih_cookie;
};

static struct resource_spec aml8726_wdt_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static struct {
	uint32_t ctrl_cpu_mask;
	uint32_t ctrl_en;
	uint32_t term_cnt_mask;
	uint32_t reset_cnt_mask;
} aml8726_wdt_soc_params;

/*
 * devclass_get_device / device_get_softc could be used
 * to dynamically locate this, however the wdt is a
 * required device which can't be unloaded so there's
 * no need for the overhead.
 */
static struct aml8726_wdt_softc *aml8726_wdt_sc = NULL;

#define	AML_WDT_LOCK(sc)		mtx_lock_spin(&(sc)->mtx)
#define	AML_WDT_UNLOCK(sc)		mtx_unlock_spin(&(sc)->mtx)
#define	AML_WDT_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev), \
    "wdt", MTX_SPIN)
#define	AML_WDT_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	AML_WDT_CTRL_REG		0
#define	AML_WDT_CTRL_CPU_WDRESET_MASK	aml8726_wdt_soc_params.ctrl_cpu_mask
#define	AML_WDT_CTRL_CPU_WDRESET_SHIFT	24
#define	AML_WDT_CTRL_IRQ_EN		(1 << 23)
#define	AML_WDT_CTRL_EN			aml8726_wdt_soc_params.ctrl_en
#define	AML_WDT_CTRL_TERMINAL_CNT_MASK	aml8726_wdt_soc_params.term_cnt_mask
#define	AML_WDT_CTRL_TERMINAL_CNT_SHIFT	0
#define	AML_WDT_RESET_REG		4
#define	AML_WDT_RESET_CNT_MASK		aml8726_wdt_soc_params.reset_cnt_mask
#define	AML_WDT_RESET_CNT_SHIFT		0

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

static void
aml8726_wdt_watchdog(void *private, u_int cmd, int *error)
{
	struct aml8726_wdt_softc *sc = (struct aml8726_wdt_softc *)private;
	uint32_t wcr;
	uint64_t tens_of_usec;

	AML_WDT_LOCK(sc);

	tens_of_usec = (((uint64_t)1 << (cmd & WD_INTERVAL)) + 9999) / 10000;

	if (cmd != 0 && tens_of_usec <= (AML_WDT_CTRL_TERMINAL_CNT_MASK >>
	    AML_WDT_CTRL_TERMINAL_CNT_SHIFT)) {

		wcr = AML_WDT_CTRL_CPU_WDRESET_MASK |
		    AML_WDT_CTRL_EN | ((uint32_t)tens_of_usec <<
		    AML_WDT_CTRL_TERMINAL_CNT_SHIFT);

		CSR_WRITE_4(sc, AML_WDT_RESET_REG, 0);
		CSR_WRITE_4(sc, AML_WDT_CTRL_REG, wcr);

		*error = 0;
	} else
		CSR_WRITE_4(sc, AML_WDT_CTRL_REG,
		    (CSR_READ_4(sc, AML_WDT_CTRL_REG) &
		    ~(AML_WDT_CTRL_IRQ_EN | AML_WDT_CTRL_EN)));

	AML_WDT_UNLOCK(sc);
}

static int
aml8726_wdt_intr(void *arg)
{
	struct aml8726_wdt_softc *sc = (struct aml8726_wdt_softc *)arg;

	/*
	 * Normally a timeout causes a hardware reset, however
	 * the watchdog timer can be configured to cause an
	 * interrupt instead by setting AML_WDT_CTRL_IRQ_EN
	 * and clearing AML_WDT_CTRL_CPU_WDRESET_MASK.
	 */

	AML_WDT_LOCK(sc);

	CSR_WRITE_4(sc, AML_WDT_CTRL_REG,
	    (CSR_READ_4(sc, AML_WDT_CTRL_REG) & ~(AML_WDT_CTRL_IRQ_EN |
	    AML_WDT_CTRL_EN)));

	CSR_BARRIER(sc, AML_WDT_CTRL_REG);

	AML_WDT_UNLOCK(sc);

	device_printf(sc->dev, "timeout expired\n");

	return (FILTER_HANDLED);
}

static int
aml8726_wdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,meson6-wdt"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 WDT");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_wdt_attach(device_t dev)
{
	struct aml8726_wdt_softc *sc = device_get_softc(dev);

	/* There should be exactly one instance. */
	if (aml8726_wdt_sc != NULL)
		return (ENXIO);

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_wdt_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	/*
	 * Certain bitfields are dependent on the hardware revision.
	 */
	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M8:
		aml8726_wdt_soc_params.ctrl_cpu_mask = 0xf <<
		    AML_WDT_CTRL_CPU_WDRESET_SHIFT;
		switch (aml8726_soc_metal_rev) {
		case AML_SOC_M8_METAL_REV_M2_A:
			aml8726_wdt_soc_params.ctrl_en = 1 << 19;
			aml8726_wdt_soc_params.term_cnt_mask = 0x07ffff <<
			    AML_WDT_CTRL_TERMINAL_CNT_SHIFT;
			aml8726_wdt_soc_params.reset_cnt_mask = 0x07ffff <<
			    AML_WDT_RESET_CNT_SHIFT;
			break;
		default:
			aml8726_wdt_soc_params.ctrl_en = 1 << 22;
			aml8726_wdt_soc_params.term_cnt_mask = 0x3fffff <<
			    AML_WDT_CTRL_TERMINAL_CNT_SHIFT;
			aml8726_wdt_soc_params.reset_cnt_mask = 0x3fffff <<
			    AML_WDT_RESET_CNT_SHIFT;
			break;
		}
		break;
	case AML_SOC_HW_REV_M8B:
		aml8726_wdt_soc_params.ctrl_cpu_mask = 0xf <<
		    AML_WDT_CTRL_CPU_WDRESET_SHIFT;
		aml8726_wdt_soc_params.ctrl_en = 1 << 19;
		aml8726_wdt_soc_params.term_cnt_mask = 0x07ffff <<
		    AML_WDT_CTRL_TERMINAL_CNT_SHIFT;
		aml8726_wdt_soc_params.reset_cnt_mask = 0x07ffff <<
		    AML_WDT_RESET_CNT_SHIFT;
		break;
	default:
		aml8726_wdt_soc_params.ctrl_cpu_mask = 3 <<
		    AML_WDT_CTRL_CPU_WDRESET_SHIFT;
		aml8726_wdt_soc_params.ctrl_en = 1 << 22;
		aml8726_wdt_soc_params.term_cnt_mask = 0x3fffff <<
		    AML_WDT_CTRL_TERMINAL_CNT_SHIFT;
		aml8726_wdt_soc_params.reset_cnt_mask = 0x3fffff <<
		    AML_WDT_RESET_CNT_SHIFT;
		break;
	}

	/*
	 * Disable the watchdog.
	 */
	CSR_WRITE_4(sc, AML_WDT_CTRL_REG,
	    (CSR_READ_4(sc, AML_WDT_CTRL_REG) & ~(AML_WDT_CTRL_IRQ_EN |
	    AML_WDT_CTRL_EN)));

	/*
	 * Initialize the mutex prior to installing the interrupt handler
	 * in case of a spurious interrupt.
	 */
	AML_WDT_LOCK_INIT(sc);

	if (bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC | INTR_MPSAFE,
	    aml8726_wdt_intr, NULL, sc, &sc->ih_cookie)) {
		device_printf(dev, "could not setup interrupt handler\n");
		bus_release_resources(dev, aml8726_wdt_spec, sc->res);
		AML_WDT_LOCK_DESTROY(sc);
		return (ENXIO);
	}

	aml8726_wdt_sc = sc;

	EVENTHANDLER_REGISTER(watchdog_list, aml8726_wdt_watchdog, sc, 0);

	return (0);
}

static int
aml8726_wdt_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t aml8726_wdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_wdt_probe),
	DEVMETHOD(device_attach,	aml8726_wdt_attach),
	DEVMETHOD(device_detach,	aml8726_wdt_detach),

	DEVMETHOD_END
};

static driver_t aml8726_wdt_driver = {
	"wdt",
	aml8726_wdt_methods,
	sizeof(struct aml8726_wdt_softc),
};

static devclass_t aml8726_wdt_devclass;

EARLY_DRIVER_MODULE(wdt, simplebus, aml8726_wdt_driver, aml8726_wdt_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);

void
cpu_reset(void)
{

	/* Watchdog has not yet been initialized */
	if (aml8726_wdt_sc == NULL)
		printf("Reset hardware has not yet been initialized.\n");
	else {
		CSR_WRITE_4(aml8726_wdt_sc, AML_WDT_RESET_REG, 0);
		CSR_WRITE_4(aml8726_wdt_sc, AML_WDT_CTRL_REG,
		    (AML_WDT_CTRL_CPU_WDRESET_MASK | AML_WDT_CTRL_EN |
		    (10 << AML_WDT_CTRL_TERMINAL_CNT_SHIFT)));
	}

	while (1);
}
