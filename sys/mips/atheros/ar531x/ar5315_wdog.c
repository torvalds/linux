/*-
 * Copyright (c) 2016, Hiroki Mori
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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
 * Watchdog driver for AR5315
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

#include <mips/atheros/ar531x/ar5315reg.h>
#include <mips/atheros/ar531x/ar5315_cpudef.h>

struct ar5315_wdog_softc {
	device_t dev;
	int armed;
	int reboot_from_watchdog;
	int debug;
};

static void
ar5315_wdog_watchdog_fn(void *private, u_int cmd, int *error)
{
	struct ar5315_wdog_softc *sc = private;
	uint64_t timer_val;

	cmd &= WD_INTERVAL;
	if (sc->debug)
		device_printf(sc->dev, "ar5315_wdog_watchdog_fn: cmd: %x\n", cmd);
	if (cmd > 0) {
		timer_val = (uint64_t)(1ULL << cmd) * ar531x_ahb_freq() /
		    1000000000;
		if (sc->debug)
			device_printf(sc->dev, "ar5315_wdog_watchdog_fn: programming timer: %jx\n", (uintmax_t) timer_val);
		/*
		 * Load timer with large enough value to prevent spurious
		 * reset
		 */
		ATH_WRITE_REG(ar531x_wdog_timer(), 
		    ar531x_ahb_freq() * 10);
		ATH_WRITE_REG(ar531x_wdog_ctl(), 
		    AR5315_WDOG_CTL_RESET);
		ATH_WRITE_REG(ar531x_wdog_timer(), 
		    (timer_val & 0xffffffff));
		sc->armed = 1;
		*error = 0;
	} else {
		if (sc->debug)
			device_printf(sc->dev, "ar5315_wdog_watchdog_fn: disarming\n");
		if (sc->armed) {
			ATH_WRITE_REG(ar531x_wdog_ctl(),
			    AR5315_WDOG_CTL_IGNORE);
			sc->armed = 0;
		}
	}
}

static int
ar5315_wdog_probe(device_t dev)
{

	device_set_desc(dev, "Atheros AR531x watchdog timer");
	return (0);
}

static void
ar5315_wdog_sysctl(device_t dev)
{
	struct ar5315_wdog_softc *sc = device_get_softc(dev);

        struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
        struct sysctl_oid *tree = device_get_sysctl_tree(sc->dev);

        SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
                "debug", CTLFLAG_RW, &sc->debug, 0,
                "enable watchdog debugging");
        SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
                "armed", CTLFLAG_RD, &sc->armed, 0,
                "whether the watchdog is armed");
        SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
                "reboot_from_watchdog", CTLFLAG_RD, &sc->reboot_from_watchdog, 0,
                "whether the system rebooted from the watchdog");
}


static int
ar5315_wdog_attach(device_t dev)
{
	struct ar5315_wdog_softc *sc = device_get_softc(dev);
	
	/* Initialise */
	sc->reboot_from_watchdog = 0;
	sc->armed = 0;
	sc->debug = 0;
	ATH_WRITE_REG(ar531x_wdog_ctl(), AR5315_WDOG_CTL_IGNORE);

	sc->dev = dev;
	EVENTHANDLER_REGISTER(watchdog_list, ar5315_wdog_watchdog_fn, sc, 0);
	ar5315_wdog_sysctl(dev);

	return (0);
}

static device_method_t ar5315_wdog_methods[] = {
	DEVMETHOD(device_probe, ar5315_wdog_probe),
	DEVMETHOD(device_attach, ar5315_wdog_attach),
	DEVMETHOD_END
};

static driver_t ar5315_wdog_driver = {
	"ar5315_wdog",
	ar5315_wdog_methods,
	sizeof(struct ar5315_wdog_softc),
};
static devclass_t ar5315_wdog_devclass;

DRIVER_MODULE(ar5315_wdog, apb, ar5315_wdog_driver, ar5315_wdog_devclass, 0, 0);
