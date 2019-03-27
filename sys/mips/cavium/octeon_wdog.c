/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2010-2011, Juli Mallett <jmallett@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * Watchdog driver for Cavium Octeon
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/watchdog.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/rman.h>
#include <sys/smp.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>

#define	DEFAULT_TIMER_VAL	65535

struct octeon_wdog_softc {
	device_t sc_dev;
	struct octeon_wdog_core_softc {
		int csc_core;
		struct resource *csc_intr;
		void *csc_intr_cookie;
	} sc_cores[MAXCPU];
	int sc_armed;
	int sc_debug;
};

extern void octeon_wdog_nmi_handler(void);
void octeon_wdog_nmi(void);

static void octeon_watchdog_arm_core(int);
static void octeon_watchdog_disarm_core(int);
static int octeon_wdog_attach(device_t);
static void octeon_wdog_identify(driver_t *, device_t);
static int octeon_wdog_intr(void *);
static int octeon_wdog_probe(device_t);
static void octeon_wdog_setup(struct octeon_wdog_softc *, int);
static void octeon_wdog_sysctl(device_t);
static void octeon_wdog_watchdog_fn(void *, u_int, int *);

void
octeon_wdog_nmi(void)
{
	int core;

	core = cvmx_get_core_num();

	printf("cpu%u: NMI detected\n", core);
	printf("cpu%u: Exception PC: %p\n", core, (void *)mips_rd_excpc());
	printf("cpu%u: status %#x cause %#x\n", core, mips_rd_status(), mips_rd_cause());

	/*
	 * This is the end
	 * Beautiful friend
	 *
	 * Just wait for Soft Reset to come and take us
	 */
	for (;;)
		continue;
}

static void
octeon_watchdog_arm_core(int core)
{
	cvmx_ciu_wdogx_t ciu_wdog;

	/* Poke it! */
	cvmx_write_csr(CVMX_CIU_PP_POKEX(core), 1);

	/*
	 * XXX
	 * Perhaps if KDB is enabled, we should use mode=2 and drop into the
	 * debugger on NMI?
	 *
	 * XXX
	 * Timer should be calculated based on CPU frquency
	 */
	ciu_wdog.u64 = 0;
	ciu_wdog.s.len = DEFAULT_TIMER_VAL;
	ciu_wdog.s.mode = 3;
	cvmx_write_csr(CVMX_CIU_WDOGX(core), ciu_wdog.u64);
}

static void
octeon_watchdog_disarm_core(int core)
{

	cvmx_write_csr(CVMX_CIU_WDOGX(core), 0);
}

static void
octeon_wdog_watchdog_fn(void *private, u_int cmd, int *error)
{
	struct octeon_wdog_softc *sc = private;
	int core;

	cmd &= WD_INTERVAL;
	if (sc->sc_debug)
		device_printf(sc->sc_dev, "%s: cmd: %x\n", __func__, cmd);
	if (cmd > 0) {
		CPU_FOREACH(core)
			octeon_watchdog_arm_core(core);
		sc->sc_armed = 1;
		*error = 0;
	} else {
		if (sc->sc_armed) {
			CPU_FOREACH(core)
				octeon_watchdog_disarm_core(core);
			sc->sc_armed = 0;
		}
	}
}

static void
octeon_wdog_sysctl(device_t dev)
{
	struct octeon_wdog_softc *sc = device_get_softc(dev);

        struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
        struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

        SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
                "debug", CTLFLAG_RW, &sc->sc_debug, 0,
                "enable watchdog debugging");
        SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
                "armed", CTLFLAG_RD, &sc->sc_armed, 0,
                "whether the watchdog is armed");
}

static void
octeon_wdog_setup(struct octeon_wdog_softc *sc, int core)
{
	struct octeon_wdog_core_softc *csc;
	int rid, error;

	csc = &sc->sc_cores[core];

	csc->csc_core = core;

	/* Interrupt part */
	rid = 0;
	csc->csc_intr = bus_alloc_resource(sc->sc_dev, SYS_RES_IRQ, &rid,
	    OCTEON_IRQ_WDOG0 + core, OCTEON_IRQ_WDOG0 + core, 1, RF_ACTIVE);
	if (csc->csc_intr == NULL)
		panic("%s: bus_alloc_resource for core %u failed",
		    __func__, core);

	error = bus_setup_intr(sc->sc_dev, csc->csc_intr, INTR_TYPE_MISC,
	    octeon_wdog_intr, NULL, csc, &csc->csc_intr_cookie);
	if (error != 0)
		panic("%s: bus_setup_intr for core %u: %d", __func__, core,
		    error);

	bus_bind_intr(sc->sc_dev, csc->csc_intr, core);
	bus_describe_intr(sc->sc_dev, csc->csc_intr, csc->csc_intr_cookie,
	    "cpu%u", core);

	if (sc->sc_armed) {
		/* Armed by default.  */
		octeon_watchdog_arm_core(core);
	} else {
		/* Disarmed by default.  */
		octeon_watchdog_disarm_core(core);
	}
}

static int
octeon_wdog_intr(void *arg)
{
	struct octeon_wdog_core_softc *csc = arg;

	KASSERT(csc->csc_core == cvmx_get_core_num(),
	    ("got watchdog interrupt for core %u on core %u.",
	     csc->csc_core, cvmx_get_core_num()));

	(void)csc;

	/* Poke it! */
	cvmx_write_csr(CVMX_CIU_PP_POKEX(cvmx_get_core_num()), 1);

	return (FILTER_HANDLED);
}

static int
octeon_wdog_probe(device_t dev)
{

	device_set_desc(dev, "Cavium Octeon watchdog timer");
	return (0);
}

static int
octeon_wdog_attach(device_t dev)
{
	struct octeon_wdog_softc *sc = device_get_softc(dev);
	uint64_t *nmi_handler = (uint64_t*)octeon_wdog_nmi_handler;
	int core, i;

	/* Initialise */
	sc->sc_armed = 0; /* XXX Ought to be a tunable / config option.  */
	sc->sc_debug = 0;

	sc->sc_dev = dev;
	EVENTHANDLER_REGISTER(watchdog_list, octeon_wdog_watchdog_fn, sc, 0);
	octeon_wdog_sysctl(dev);

	for (i = 0; i < 16; i++) {
		cvmx_write_csr(CVMX_MIO_BOOT_LOC_ADR, i * 8);
		cvmx_write_csr(CVMX_MIO_BOOT_LOC_DAT, nmi_handler[i]);
        }

	cvmx_write_csr(CVMX_MIO_BOOT_LOC_CFGX(0), 0x81fc0000);

	CPU_FOREACH(core)
		octeon_wdog_setup(sc, core);
	return (0);
}

static void
octeon_wdog_identify(driver_t *drv, device_t parent)
{

	BUS_ADD_CHILD(parent, 0, "owdog", 0);
}

static device_method_t octeon_wdog_methods[] = {
	DEVMETHOD(device_identify, octeon_wdog_identify),

	DEVMETHOD(device_probe, octeon_wdog_probe),
	DEVMETHOD(device_attach, octeon_wdog_attach),
	{0, 0},
};

static driver_t octeon_wdog_driver = {
	"owdog",
	octeon_wdog_methods,
	sizeof(struct octeon_wdog_softc),
};
static devclass_t octeon_wdog_devclass;

DRIVER_MODULE(owdog, ciu, octeon_wdog_driver, octeon_wdog_devclass, 0, 0);
