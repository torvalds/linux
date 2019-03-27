/*-
 * Copyright (c) 2015 Ian lepore <ian@freebsd.org>
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
 * AM335x PPS driver using DMTimer capture.
 *
 * Note that this PPS driver does not use an interrupt.  Instead it uses the
 * hardware's ability to latch the timer's count register in response to a
 * signal on an IO pin.  Each of timers 4-7 have an associated pin, and this
 * code allows any one of those to be used.
 *
 * The timecounter routines in kern_tc.c call the pps poll routine periodically
 * to see if a new counter value has been latched.  When a new value has been
 * latched, the only processing done in the poll routine is to capture the
 * current set of timecounter timehands (done with pps_capture()) and the
 * latched value from the timer.  The remaining work (done by pps_event() while
 * holding a mutex) is scheduled to be done later in a non-interrupt context.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/taskqueue.h>
#include <sys/timepps.h>
#include <sys/timetc.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>
#include <arm/ti/ti_pinmux.h>
#include <arm/ti/am335x/am335x_scm_padconf.h>

#include "am335x_dmtreg.h"

#define	PPS_CDEV_NAME	"dmtpps"

struct dmtpps_softc {
	device_t		dev;
	int			mem_rid;
	struct resource *	mem_res;
	int			tmr_num;	/* N from hwmod str "timerN" */
	char			tmr_name[12];	/* "DMTimerN" */
	uint32_t		tclr;		/* Cached TCLR register. */
	struct timecounter	tc;
	int			pps_curmode;	/* Edge mode now set in hw. */
	struct task 		pps_task;	/* For pps_event handling. */
	struct cdev *		pps_cdev;
	struct pps_state	pps_state;
	struct mtx		pps_mtx;
};

static int dmtpps_tmr_num;	/* Set by probe() */

/* List of compatible strings for FDT tree */
static struct ofw_compat_data compat_data[] = {
	{"ti,am335x-timer",     1},
	{"ti,am335x-timer-1ms", 1},
	{NULL,                  0},
};

/*
 * A table relating pad names to the hardware timer number they can be mux'd to.
 */
struct padinfo {
	char *	ballname;
	int	tmr_num;
};
static struct padinfo dmtpps_padinfo[] = {
	{"GPMC_ADVn_ALE",    4},
	{"I2C0_SDA",         4},
	{"MII1_TX_EN",       4},
	{"XDMA_EVENT_INTR0", 4},
	{"GPMC_BEn0_CLE",    5},
	{"MDC",              5},
	{"MMC0_DAT3",        5},
	{"UART1_RTSn",       5},
	{"GPMC_WEn",         6},
	{"MDIO",             6},
	{"MMC0_DAT2",        6},
	{"UART1_CTSn",       6},
	{"GPMC_OEn_REn",     7},
	{"I2C0_SCL",         7},
	{"UART0_CTSn",       7},
	{"XDMA_EVENT_INTR1", 7},
	{NULL, 0}
};

/*
 * This is either brilliantly user-friendly, or utterly lame...
 *
 * The am335x chip is used on the popular Beaglebone boards.  Those boards have
 * pins for all four capture-capable timers available on the P8 header. Allow
 * users to configure the input pin by giving the name of the header pin.
 */
struct nicknames {
	const char * nick;
	const char * name;
};
static struct nicknames dmtpps_pin_nicks[] = {
	{"P8-7",  "GPMC_ADVn_ALE"},
	{"P8-9",  "GPMC_BEn0_CLE"},
	{"P8-10", "GPMC_WEn"},
	{"P8-8",  "GPMC_OEn_REn",},
	{NULL, NULL}
};

#define	DMTIMER_READ4(sc, reg)		bus_read_4((sc)->mem_res, (reg))
#define	DMTIMER_WRITE4(sc, reg, val)	bus_write_4((sc)->mem_res, (reg), (val))

/*
 * Translate a short friendly case-insensitive name to its canonical name.
 */
static const char *
dmtpps_translate_nickname(const char *nick)
{
	struct nicknames *nn;

	for (nn = dmtpps_pin_nicks; nn->nick != NULL; nn++)
		if (strcasecmp(nick, nn->nick) == 0)
			return nn->name;
	return (nick);
}

/*
 * See if our tunable is set to the name of the input pin.  If not, that's NOT
 * an error, return 0.  If so, try to configure that pin as a timer capture
 * input pin, and if that works, then we have our timer unit number and if it
 * fails that IS an error, return -1.
 */
static int
dmtpps_find_tmr_num_by_tunable(void)
{
	struct padinfo *pi;
	char iname[20];
	char muxmode[12];
	const char * ballname;
	int err;

	if (!TUNABLE_STR_FETCH("hw.am335x_dmtpps.input", iname, sizeof(iname)))
		return (0);
	ballname = dmtpps_translate_nickname(iname);
	for (pi = dmtpps_padinfo; pi->ballname != NULL; pi++) {
		if (strcmp(ballname, pi->ballname) != 0)
			continue;
		snprintf(muxmode, sizeof(muxmode), "timer%d", pi->tmr_num);
		err = ti_pinmux_padconf_set(pi->ballname, muxmode,
		    PADCONF_INPUT);
		if (err != 0) {
			printf("am335x_dmtpps: unable to configure capture pin "
			    "for %s to input mode\n", muxmode);
			return (-1);
		} else if (bootverbose) {
			printf("am335x_dmtpps: configured pin %s as input "
			    "for %s\n", iname, muxmode);
		}
		return (pi->tmr_num);
	}

	/* Invalid name in the tunable, that's an error. */
	printf("am335x_dmtpps: unknown pin name '%s'\n", iname);
	return (-1);
}

/*
 * Ask the pinmux driver whether any pin has been configured as a TIMER4..TIMER7
 * input pin.  If so, return the timer number, if not return 0.
 */
static int
dmtpps_find_tmr_num_by_padconf(void)
{
	int err;
	unsigned int padstate;
	const char * padmux;
	struct padinfo *pi;
	char muxmode[12];

	for (pi = dmtpps_padinfo; pi->ballname != NULL; pi++) {
		err = ti_pinmux_padconf_get(pi->ballname, &padmux, &padstate);
		snprintf(muxmode, sizeof(muxmode), "timer%d", pi->tmr_num);
		if (err == 0 && (padstate & RXACTIVE) != 0 &&
		    strcmp(muxmode, padmux) == 0)
			return (pi->tmr_num);
	}
	/* Nothing found, not an error. */
	return (0);
}

/*
 * Figure out which hardware timer number to use based on input pin
 * configuration.  This is done just once, the first time probe() runs.
 */
static int
dmtpps_find_tmr_num(void)
{
	int tmr_num;

	if ((tmr_num = dmtpps_find_tmr_num_by_tunable()) == 0)
		tmr_num = dmtpps_find_tmr_num_by_padconf();

	if (tmr_num <= 0) {
		printf("am335x_dmtpps: PPS driver not enabled: unable to find "
		    "or configure a capture input pin\n");
		tmr_num = -1; /* Must return non-zero to prevent re-probing. */
	}
	return (tmr_num);
}

static void
dmtpps_set_hw_capture(struct dmtpps_softc *sc, bool force_off)
{
	int newmode;

	if (force_off)
		newmode = 0;
	else
		newmode = sc->pps_state.ppsparam.mode & PPS_CAPTUREASSERT;

	if (newmode == sc->pps_curmode)
		return;
	sc->pps_curmode = newmode;

	if (newmode == PPS_CAPTUREASSERT)
		sc->tclr |= DMT_TCLR_CAPTRAN_LOHI;
	else
		sc->tclr &= ~DMT_TCLR_CAPTRAN_MASK;
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);
}

static unsigned
dmtpps_get_timecount(struct timecounter *tc)
{
	struct dmtpps_softc *sc;

	sc = tc->tc_priv;

	return (DMTIMER_READ4(sc, DMT_TCRR));
}

static void
dmtpps_poll(struct timecounter *tc)
{
	struct dmtpps_softc *sc;

	sc = tc->tc_priv;

	/*
	 * If a new value has been latched we've got a PPS event.  Capture the
	 * timecounter data, then override the capcount field (pps_capture()
	 * populates it from the current DMT_TCRR register) with the latched
	 * value from the TCAR1 register.
	 *
	 * There is no locking here, by design.  pps_capture() writes into an
	 * area of struct pps_state which is read only by pps_event().  The
	 * synchronization of access to that area is temporal rather than
	 * interlock based... we write in this routine and trigger the task that
	 * will read the data, so no simultaneous access can occur.
	 *
	 * Note that we don't have the TCAR interrupt enabled, but the hardware
	 * still provides the status bits in the "RAW" status register even when
	 * they're masked from generating an irq.  However, when clearing the
	 * TCAR status to re-arm the capture for the next second, we have to
	 * write to the IRQ status register, not the RAW register.  Quirky.
	 */
	if (DMTIMER_READ4(sc, DMT_IRQSTATUS_RAW) & DMT_IRQ_TCAR) {
		pps_capture(&sc->pps_state);
		sc->pps_state.capcount = DMTIMER_READ4(sc, DMT_TCAR1);
		DMTIMER_WRITE4(sc, DMT_IRQSTATUS, DMT_IRQ_TCAR);
		taskqueue_enqueue(taskqueue_fast, &sc->pps_task);
	}
}

static void
dmtpps_event(void *arg, int pending)
{
	struct dmtpps_softc *sc;

	sc = arg;

	/* This is the task function that gets enqueued by poll_pps.  Once the
	 * time has been captured by the timecounter polling code which runs in
	 * primary interrupt context, the remaining (more expensive) work to
	 * process the event is done later in a threaded context.
	 *
	 * Here there is an interlock that protects the event data in struct
	 * pps_state.  That data can be accessed at any time from userland via
	 * ioctl() calls so we must ensure that there is no read access to
	 * partially updated data while pps_event() does its work.
	 */
	mtx_lock(&sc->pps_mtx);
	pps_event(&sc->pps_state, PPS_CAPTUREASSERT);
	mtx_unlock(&sc->pps_mtx);
}

static int
dmtpps_open(struct cdev *dev, int flags, int fmt, 
    struct thread *td)
{
	struct dmtpps_softc *sc;

	sc = dev->si_drv1;

	/*
	 * Begin polling for pps and enable capture in the hardware whenever the
	 * device is open.  Doing this stuff again is harmless if this isn't the
	 * first open.
	 */
	sc->tc.tc_poll_pps = dmtpps_poll;
	dmtpps_set_hw_capture(sc, false);

	return 0;
}

static	int
dmtpps_close(struct cdev *dev, int flags, int fmt, 
    struct thread *td)
{
	struct dmtpps_softc *sc;

	sc = dev->si_drv1;

	/*
	 * Stop polling and disable capture on last close.  Use the force-off
	 * flag to override the configured mode and turn off the hardware.
	 */
	sc->tc.tc_poll_pps = NULL;
	dmtpps_set_hw_capture(sc, true);

	return 0;
}

static int
dmtpps_ioctl(struct cdev *dev, u_long cmd, caddr_t data, 
    int flags, struct thread *td)
{
	struct dmtpps_softc *sc;
	int err;

	sc = dev->si_drv1;

	/* Let the kernel do the heavy lifting for ioctl. */
	mtx_lock(&sc->pps_mtx);
	err = pps_ioctl(cmd, data, &sc->pps_state);
	mtx_unlock(&sc->pps_mtx);
	if (err != 0)
		return (err);

	/*
	 * The capture mode could have changed, set the hardware to whatever
	 * mode is now current.  Effectively a no-op if nothing changed.
	 */
	dmtpps_set_hw_capture(sc, false);

	return (err);
}

static struct cdevsw dmtpps_cdevsw = {
	.d_version =    D_VERSION,
	.d_open =       dmtpps_open,
	.d_close =      dmtpps_close,
	.d_ioctl =      dmtpps_ioctl,
	.d_name =       PPS_CDEV_NAME,
};

static int
dmtpps_probe(device_t dev)
{
	char strbuf[64];
	int tmr_num;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	/*
	 * If we haven't chosen which hardware timer to use yet, go do that now.
	 * We need to know that to decide whether to return success for this
	 * hardware timer instance or not.
	 */
	if (dmtpps_tmr_num == 0)
		dmtpps_tmr_num = dmtpps_find_tmr_num();

	/*
	 * Figure out which hardware timer is being probed and see if it matches
	 * the configured timer number determined earlier.
	 */
	tmr_num = ti_hwmods_get_unit(dev, "timer");
	if (dmtpps_tmr_num != tmr_num)
		return (ENXIO);

	snprintf(strbuf, sizeof(strbuf), "AM335x PPS-Capture DMTimer%d",
	    tmr_num);
	device_set_desc_copy(dev, strbuf);

	return(BUS_PROBE_DEFAULT);
}

static int
dmtpps_attach(device_t dev)
{
	struct dmtpps_softc *sc;
	clk_ident_t timer_id;
	int err, sysclk_freq;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Get the base clock frequency. */
	err = ti_prcm_clk_get_source_freq(SYS_CLK, &sysclk_freq);

	/* Enable clocks and power on the device. */
	if ((timer_id = ti_hwmods_get_clock(dev)) == INVALID_CLK_IDENT)
		return (ENXIO);
	if ((err = ti_prcm_clk_set_source(timer_id, SYSCLK_CLK)) != 0)
		return (err);
	if ((err = ti_prcm_clk_enable(timer_id)) != 0)
		return (err);

	/* Request the memory resources. */
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mem_rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		return (ENXIO);
	}

	/* Figure out which hardware timer this is and set the name string. */
	sc->tmr_num = ti_hwmods_get_unit(dev, "timer");
	snprintf(sc->tmr_name, sizeof(sc->tmr_name), "DMTimer%d", sc->tmr_num);

	/*
	 * Configure the timer pulse/capture pin to input/capture mode.  This is
	 * required in addition to configuring the pin as input with the pinmux
	 * controller (which was done via fdt data or tunable at probe time).
	 */
	sc->tclr = DMT_TCLR_GPO_CFG;
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);

	/* Set up timecounter hardware, start it. */
	DMTIMER_WRITE4(sc, DMT_TSICR, DMT_TSICR_RESET);
	while (DMTIMER_READ4(sc, DMT_TIOCP_CFG) & DMT_TIOCP_RESET)
		continue;

	sc->tclr |= DMT_TCLR_START | DMT_TCLR_AUTOLOAD;
	DMTIMER_WRITE4(sc, DMT_TLDR, 0);
	DMTIMER_WRITE4(sc, DMT_TCRR, 0);
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);

	/* Register the timecounter. */
	sc->tc.tc_name           = sc->tmr_name;
	sc->tc.tc_get_timecount  = dmtpps_get_timecount;
	sc->tc.tc_counter_mask   = ~0u;
	sc->tc.tc_frequency      = sysclk_freq;
	sc->tc.tc_quality        = 1000;
	sc->tc.tc_priv           = sc;

	tc_init(&sc->tc);

	/*
	 * Indicate our PPS capabilities.  Have the kernel init its part of the
	 * pps_state struct and add its capabilities.
	 *
	 * While the hardware has a mode to capture each edge, it's not clear we
	 * can use it that way, because there's only a single interrupt/status
	 * bit to say something was captured, but not which edge it was.  For
	 * now, just say we can only capture assert events (the positive-going
	 * edge of the pulse).
	 */
	mtx_init(&sc->pps_mtx, "dmtpps", NULL, MTX_DEF);
	sc->pps_state.ppscap = PPS_CAPTUREASSERT;
	sc->pps_state.driver_abi = PPS_ABI_VERSION;
	sc->pps_state.driver_mtx = &sc->pps_mtx;
	pps_init_abi(&sc->pps_state);

	/*
	 * Init the task that does deferred pps_event() processing after
	 * the polling routine has captured a pps pulse time.
	 */
	TASK_INIT(&sc->pps_task, 0, dmtpps_event, sc);

	/* Create the PPS cdev. */
	sc->pps_cdev = make_dev(&dmtpps_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    PPS_CDEV_NAME);
	sc->pps_cdev->si_drv1 = sc;

	if (bootverbose)
		device_printf(sc->dev, "Using %s for PPS device /dev/%s\n",
		    sc->tmr_name, PPS_CDEV_NAME);

	return (0);
}

static int
dmtpps_detach(device_t dev)
{

	/*
	 * There is no way to remove a timecounter once it has been registered,
	 * even if it's not in use, so we can never detach.  If we were
	 * dynamically loaded as a module this will prevent unloading.
	 */
	return (EBUSY);
}

static device_method_t dmtpps_methods[] = {
	DEVMETHOD(device_probe,		dmtpps_probe),
	DEVMETHOD(device_attach,	dmtpps_attach),
	DEVMETHOD(device_detach,	dmtpps_detach),
	{ 0, 0 }
};

static driver_t dmtpps_driver = {
	"am335x_dmtpps",
	dmtpps_methods,
	sizeof(struct dmtpps_softc),
};

static devclass_t dmtpps_devclass;

DRIVER_MODULE(am335x_dmtpps, simplebus, dmtpps_driver, dmtpps_devclass, 0, 0);
MODULE_DEPEND(am335x_dmtpps, am335x_prcm, 1, 1, 1);

