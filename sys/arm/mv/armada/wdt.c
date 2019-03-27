/*-
 * Copyright (c) 2006 Benno Rice.
 * Copyright (C) 2007-2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Adapted to Marvell SoC by Semihalf.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: FreeBSD: //depot/projects/arm/src/sys/arm/xscale/pxa2x0/pxa2x0_timer.c, rev 1
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
#include <sys/kdb.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#define INITIAL_TIMECOUNTER	(0xffffffff)
#define MAX_WATCHDOG_TICKS	(0xffffffff)
#define WD_RST_OUT_EN           0x00000002

#define	MV_CLOCK_SRC_ARMV7	25000000	/* Timers' 25MHz mode */

struct mv_wdt_config {
	enum soc_family wdt_soc;
	uint32_t wdt_timer;
	void (*wdt_enable)(void);
	void (*wdt_disable)(void);
	unsigned int wdt_clock_src;
};

static void mv_wdt_enable_armv5(void);
static void mv_wdt_enable_armada_38x(void);
static void mv_wdt_enable_armada_xp(void);

static void mv_wdt_disable_armv5(void);
static void mv_wdt_disable_armada_38x(void);
static void mv_wdt_disable_armada_xp(void);

static struct mv_wdt_config mv_wdt_armada_38x_config = {
	.wdt_soc = MV_SOC_ARMADA_38X,
	.wdt_timer = 4,
	.wdt_enable = &mv_wdt_enable_armada_38x,
	.wdt_disable = &mv_wdt_disable_armada_38x,
	.wdt_clock_src = MV_CLOCK_SRC_ARMV7,
};

static struct mv_wdt_config mv_wdt_armada_xp_config = {
	.wdt_soc = MV_SOC_ARMADA_XP,
	.wdt_timer = 2,
	.wdt_enable = &mv_wdt_enable_armada_xp,
	.wdt_disable = &mv_wdt_disable_armada_xp,
	.wdt_clock_src = MV_CLOCK_SRC_ARMV7,
};

static struct mv_wdt_config mv_wdt_armv5_config = {
	.wdt_soc = MV_SOC_ARMV5,
	.wdt_timer = 2,
	.wdt_enable = &mv_wdt_enable_armv5,
	.wdt_disable = &mv_wdt_disable_armv5,
	.wdt_clock_src = 0,
};

struct mv_wdt_softc {
	struct resource	*	wdt_res;
	struct mtx		wdt_mtx;
	struct mv_wdt_config *	wdt_config;
};

static struct resource_spec mv_wdt_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static struct ofw_compat_data mv_wdt_compat[] = {
	{"marvell,armada-380-wdt",	(uintptr_t)&mv_wdt_armada_38x_config},
	{"marvell,armada-xp-wdt",	(uintptr_t)&mv_wdt_armada_xp_config},
	{"marvell,orion-wdt",		(uintptr_t)&mv_wdt_armv5_config},
	{NULL,				(uintptr_t)NULL}
};

static struct mv_wdt_softc *wdt_softc = NULL;
int timers_initialized = 0;

static int mv_wdt_probe(device_t);
static int mv_wdt_attach(device_t);

static uint32_t	mv_get_timer_control(void);
static void mv_set_timer_control(uint32_t);
static void mv_set_timer(uint32_t, uint32_t);

static void mv_watchdog_event(void *, unsigned int, int *);

static device_method_t mv_wdt_methods[] = {
	DEVMETHOD(device_probe, mv_wdt_probe),
	DEVMETHOD(device_attach, mv_wdt_attach),

	{ 0, 0 }
};

static driver_t mv_wdt_driver = {
	"wdt",
	mv_wdt_methods,
	sizeof(struct mv_wdt_softc),
};

static devclass_t mv_wdt_devclass;

DRIVER_MODULE(wdt, simplebus, mv_wdt_driver, mv_wdt_devclass, 0, 0);
static int
mv_wdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, mv_wdt_compat)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Marvell Watchdog Timer");
	return (0);
}

static int
mv_wdt_attach(device_t dev)
{
	struct mv_wdt_softc *sc;
	int error;

	if (wdt_softc != NULL)
		return (ENXIO);

	sc = device_get_softc(dev);
	wdt_softc = sc;

	error = bus_alloc_resources(dev, mv_wdt_spec, &sc->wdt_res);
	if (error) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	mtx_init(&sc->wdt_mtx, "watchdog", NULL, MTX_DEF);

	sc->wdt_config = (struct mv_wdt_config *)
	   ofw_bus_search_compatible(dev, mv_wdt_compat)->ocd_data;

	if (sc->wdt_config->wdt_clock_src == 0)
		sc->wdt_config->wdt_clock_src = get_tclk();

	if (wdt_softc->wdt_config->wdt_disable != NULL)
		wdt_softc->wdt_config->wdt_disable();
	EVENTHANDLER_REGISTER(watchdog_list, mv_watchdog_event, sc, 0);

	return (0);
}

static __inline uint32_t
mv_get_timer_control(void)
{

	return (bus_read_4(wdt_softc->wdt_res, CPU_TIMER_CONTROL));
}

static __inline void
mv_set_timer_control(uint32_t val)
{

	bus_write_4(wdt_softc->wdt_res, CPU_TIMER_CONTROL, val);
}

static __inline void
mv_set_timer(uint32_t timer, uint32_t val)
{

	bus_write_4(wdt_softc->wdt_res, CPU_TIMER0 + timer * 0x8, val);
}
static void
mv_wdt_enable_armv5(void)
{
	uint32_t val, irq_cause, irq_mask;

	irq_cause = read_cpu_ctrl(BRIDGE_IRQ_CAUSE);
	irq_cause &= IRQ_TIMER_WD_CLR;
	write_cpu_ctrl(BRIDGE_IRQ_CAUSE, irq_cause);

	irq_mask = read_cpu_ctrl(BRIDGE_IRQ_MASK);
	irq_mask |= IRQ_TIMER_WD_MASK;
	write_cpu_ctrl(BRIDGE_IRQ_MASK, irq_mask);

	val = read_cpu_ctrl(RSTOUTn_MASK);
	val |= WD_RST_OUT_EN;
	write_cpu_ctrl(RSTOUTn_MASK, val);

	val = mv_get_timer_control();
	val |= CPU_TIMER2_EN | CPU_TIMER2_AUTO;
	mv_set_timer_control(val);
}

static inline void
mv_wdt_enable_armada_38x_xp_helper()
{
	uint32_t val, irq_cause;

	irq_cause = read_cpu_ctrl(BRIDGE_IRQ_CAUSE);
	irq_cause &= IRQ_TIMER_WD_CLR;
	write_cpu_ctrl(BRIDGE_IRQ_CAUSE, irq_cause);

	val = read_cpu_mp_clocks(WD_RSTOUTn_MASK);
	val |= (WD_GLOBAL_MASK | WD_CPU0_MASK);
	write_cpu_mp_clocks(WD_RSTOUTn_MASK, val);

	val = read_cpu_misc(RSTOUTn_MASK_ARMV7);
	val &= ~RSTOUTn_MASK_WD;
	write_cpu_misc(RSTOUTn_MASK_ARMV7, val);
}

static void
mv_wdt_enable_armada_38x(void)
{
	uint32_t val, irq_cause;

	irq_cause = read_cpu_ctrl(BRIDGE_IRQ_CAUSE);
	irq_cause &= IRQ_TIMER_WD_CLR;
	write_cpu_ctrl(BRIDGE_IRQ_CAUSE, irq_cause);

	mv_wdt_enable_armada_38x_xp_helper();

	val = mv_get_timer_control();
	val |= CPU_TIMER_WD_EN | CPU_TIMER_WD_AUTO | CPU_TIMER_WD_25MHZ_EN;
	mv_set_timer_control(val);
}

static void
mv_wdt_enable_armada_xp(void)
{
	uint32_t val, irq_cause;
	irq_cause = read_cpu_ctrl(BRIDGE_IRQ_CAUSE_ARMADAXP);
	irq_cause &= IRQ_TIMER_WD_CLR_ARMADAXP;
	write_cpu_ctrl(BRIDGE_IRQ_CAUSE_ARMADAXP, irq_cause);

	mv_wdt_enable_armada_38x_xp_helper();

	val = mv_get_timer_control();
	val |= CPU_TIMER2_EN | CPU_TIMER2_AUTO | CPU_TIMER_WD_25MHZ_EN;
	mv_set_timer_control(val);
}

static void
mv_wdt_disable_armv5(void)
{
	uint32_t val, irq_cause, irq_mask;

	val = mv_get_timer_control();
	val &= ~(CPU_TIMER2_EN | CPU_TIMER2_AUTO);
	mv_set_timer_control(val);

	val = read_cpu_ctrl(RSTOUTn_MASK);
	val &= ~WD_RST_OUT_EN;
	write_cpu_ctrl(RSTOUTn_MASK, val);

	irq_mask = read_cpu_ctrl(BRIDGE_IRQ_MASK);
	irq_mask &= ~(IRQ_TIMER_WD_MASK);
	write_cpu_ctrl(BRIDGE_IRQ_MASK, irq_mask);

	irq_cause = read_cpu_ctrl(BRIDGE_IRQ_CAUSE);
	irq_cause &= IRQ_TIMER_WD_CLR;
	write_cpu_ctrl(BRIDGE_IRQ_CAUSE, irq_cause);
}

static __inline void
mv_wdt_disable_armada_38x_xp_helper(void)
{
	uint32_t val;

	val = read_cpu_mp_clocks(WD_RSTOUTn_MASK);
	val &= ~(WD_GLOBAL_MASK | WD_CPU0_MASK);
	write_cpu_mp_clocks(WD_RSTOUTn_MASK, val);

	val = read_cpu_misc(RSTOUTn_MASK_ARMV7);
	val |= RSTOUTn_MASK_WD;
	write_cpu_misc(RSTOUTn_MASK_ARMV7, RSTOUTn_MASK_WD);
}

static void
mv_wdt_disable_armada_38x(void)
{
	uint32_t val;

	val = mv_get_timer_control();
	val &= ~(CPU_TIMER_WD_EN | CPU_TIMER_WD_AUTO);
	mv_set_timer_control(val);

	mv_wdt_disable_armada_38x_xp_helper();
}

static void
mv_wdt_disable_armada_xp(void)
{
	uint32_t val;

	val = mv_get_timer_control();
	val &= ~(CPU_TIMER2_EN | CPU_TIMER2_AUTO);
	mv_set_timer_control(val);

	mv_wdt_disable_armada_38x_xp_helper();
}

/*
 * Watchdog event handler.
 */
static void
mv_watchdog_event(void *arg, unsigned int cmd, int *error)
{
	struct mv_wdt_softc *sc;
	uint64_t ns;
	uint64_t ticks;

	sc = arg;
	mtx_lock(&sc->wdt_mtx);
	if (cmd == 0) {
		if (wdt_softc->wdt_config->wdt_disable != NULL)
			wdt_softc->wdt_config->wdt_disable();
	} else {
		/*
		 * Watchdog timeout is in nanosecs, calculation according to
		 * watchdog(9)
		 */
		ns = (uint64_t)1 << (cmd & WD_INTERVAL);
		ticks = (uint64_t)(ns * sc->wdt_config->wdt_clock_src) / 1000000000;
		if (ticks > MAX_WATCHDOG_TICKS) {
			if (wdt_softc->wdt_config->wdt_disable != NULL)
				wdt_softc->wdt_config->wdt_disable();
		}
		else {
			mv_set_timer(wdt_softc->wdt_config->wdt_timer, ticks);
			if (wdt_softc->wdt_config->wdt_enable != NULL)
				wdt_softc->wdt_config->wdt_enable();
			*error = 0;
		}
	}
	mtx_unlock(&sc->wdt_mtx);
}
