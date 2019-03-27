/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/machdep.h> /* For arm_set_delay */

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/freescale/imx/imx_ccmvar.h>
#include <arm/freescale/imx/imx_gptreg.h>

#define	WRITE4(_sc, _r, _v)						\
	    bus_space_write_4((_sc)->sc_iot, (_sc)->sc_ioh, (_r), (_v))
#define	READ4(_sc, _r)							\
	    bus_space_read_4((_sc)->sc_iot, (_sc)->sc_ioh, (_r))
#define	SET4(_sc, _r, _m)						\
	    WRITE4((_sc), (_r), READ4((_sc), (_r)) | (_m))
#define	CLEAR4(_sc, _r, _m)						\
	    WRITE4((_sc), (_r), READ4((_sc), (_r)) & ~(_m))

static u_int	imx_gpt_get_timecount(struct timecounter *);
static int	imx_gpt_timer_start(struct eventtimer *, sbintime_t,
    sbintime_t);
static int	imx_gpt_timer_stop(struct eventtimer *);

static void imx_gpt_do_delay(int, void *);

static int imx_gpt_intr(void *);
static int imx_gpt_probe(device_t);
static int imx_gpt_attach(device_t);

static struct timecounter imx_gpt_timecounter = {
	.tc_name           = "iMXGPT",
	.tc_get_timecount  = imx_gpt_get_timecount,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	.tc_quality        = 1000,
};

struct imx_gpt_softc {
	device_t 		sc_dev;
	struct resource *	res[2];
	bus_space_tag_t 	sc_iot;
	bus_space_handle_t	sc_ioh;
	void *			sc_ih;			/* interrupt handler */
	uint32_t 		sc_period;
	uint32_t 		sc_clksrc;
	uint32_t 		clkfreq;
	uint32_t		ir_reg;
	struct eventtimer 	et;
};

/* Try to divide down an available fast clock to this frequency. */
#define	TARGET_FREQUENCY	1000000000

static struct resource_spec imx_gpt_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6dl-gpt", 1},
	{"fsl,imx6q-gpt",  1},
	{"fsl,imx6ul-gpt", 1},
	{"fsl,imx53-gpt",  1},
	{"fsl,imx51-gpt",  1},
	{"fsl,imx31-gpt",  1},
	{"fsl,imx27-gpt",  1},
	{"fsl,imx25-gpt",  1},
	{NULL,             0}
};

static int
imx_gpt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	/*
	 *  We only support a single unit, because the only thing this driver
	 *  does with the complex timer hardware is supply the system
	 *  timecounter and eventtimer.  There is nothing useful we can do with
	 *  the additional device instances that exist in some chips.
	 */
	if (device_get_unit(dev) > 0)
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Freescale i.MX GPT timer");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
imx_gpt_attach(device_t dev)
{
	struct imx_gpt_softc *sc;
	int ctlreg, err;
	uint32_t basefreq, prescale, setup_ticks, t1, t2;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, imx_gpt_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->sc_dev = dev;
	sc->sc_iot = rman_get_bustag(sc->res[0]);
	sc->sc_ioh = rman_get_bushandle(sc->res[0]);

	/*
	 * For now, just automatically choose a good clock for the hardware
	 * we're running on.  Eventually we could allow selection from the fdt;
	 * the code in this driver will cope with any clock frequency.
	 */
	sc->sc_clksrc = GPT_CR_CLKSRC_IPG;

	ctlreg = 0;

	switch (sc->sc_clksrc) {
	case GPT_CR_CLKSRC_32K:
		basefreq = 32768;
		break;
	case GPT_CR_CLKSRC_IPG:
		basefreq = imx_ccm_ipg_hz();
		break;
	case GPT_CR_CLKSRC_IPG_HIGH:
		basefreq = imx_ccm_ipg_hz() * 2;
		break;
	case GPT_CR_CLKSRC_24M:
		ctlreg |= GPT_CR_24MEN;
		basefreq = 24000000;
		break;
	case GPT_CR_CLKSRC_NONE:/* Can't run without a clock. */
	case GPT_CR_CLKSRC_EXT:	/* No way to get the freq of an ext clock. */
	default:
		device_printf(dev, "Unsupported clock source '%d'\n", 
		    sc->sc_clksrc);
		return (EINVAL);
	}

	/*
	 * The following setup sequence is from the I.MX6 reference manual,
	 * "Selecting the clock source".  First, disable the clock and
	 * interrupts.  This also clears input and output mode bits and in
	 * general completes several of the early steps in the procedure.
	 */
	WRITE4(sc, IMX_GPT_CR, 0);
	WRITE4(sc, IMX_GPT_IR, 0);

	/* Choose the clock and the power-saving behaviors. */
	ctlreg |=
	    sc->sc_clksrc |	/* Use selected clock */
	    GPT_CR_FRR |	/* Just count (FreeRunner mode) */
	    GPT_CR_STOPEN |	/* Run in STOP mode */
	    GPT_CR_DOZEEN |	/* Run in DOZE mode */
	    GPT_CR_WAITEN |	/* Run in WAIT mode */
	    GPT_CR_DBGEN;	/* Run in DEBUG mode */
	WRITE4(sc, IMX_GPT_CR, ctlreg);

	/*
	 * The datasheet says to do the software reset after choosing the clock
	 * source.  It says nothing about needing to wait for the reset to
	 * complete, but the register description does document the fact that
	 * the reset isn't complete until the SWR bit reads 0, so let's be safe.
	 * The reset also clears all registers except for a few of the bits in
	 * CR, but we'll rewrite all the CR bits when we start the counter.
	 */
	WRITE4(sc, IMX_GPT_CR, ctlreg | GPT_CR_SWR);
	while (READ4(sc, IMX_GPT_CR) & GPT_CR_SWR)
		continue;

	/* Set a prescaler value that gets us near the target frequency. */
	if (basefreq < TARGET_FREQUENCY) {
		prescale = 0;
		sc->clkfreq = basefreq;
	} else {
		prescale = basefreq / TARGET_FREQUENCY;
		sc->clkfreq = basefreq / prescale;
		prescale -= 1; /* 1..n range is 0..n-1 in hardware. */
	}
	WRITE4(sc, IMX_GPT_PR, prescale);

	/* Clear the status register. */
	WRITE4(sc, IMX_GPT_SR, GPT_IR_ALL);

	/* Start the counter. */
	WRITE4(sc, IMX_GPT_CR, ctlreg | GPT_CR_EN);

	if (bootverbose)
		device_printf(dev, "Running on %dKHz clock, base freq %uHz CR=0x%08x, PR=0x%08x\n",
		    sc->clkfreq / 1000, basefreq, READ4(sc, IMX_GPT_CR), READ4(sc, IMX_GPT_PR));

	/* Setup the timer interrupt. */
	err = bus_setup_intr(dev, sc->res[1], INTR_TYPE_CLK, imx_gpt_intr,
	    NULL, sc, &sc->sc_ih);
	if (err != 0) {
		bus_release_resources(dev, imx_gpt_spec, sc->res);
		device_printf(dev, "Unable to setup the clock irq handler, "
		    "err = %d\n", err);
		return (ENXIO);
	}

	/*
	 * Measure how many clock ticks it takes to setup a one-shot event (it's
	 * longer than you might think, due to wait states in accessing gpt
	 * registers).  Scale up the result by a factor of 1.5 to be safe,
	 * and use that to set the minimum eventtimer period we can schedule. In
	 * the real world, the value works out to about 750ns on imx5 hardware.
	 */
	t1 = READ4(sc, IMX_GPT_CNT);
	WRITE4(sc, IMX_GPT_OCR3, 0);
	t2 = READ4(sc, IMX_GPT_CNT);
	setup_ticks = ((t2 - t1 + 1) * 3) / 2;

	/* Register as an eventtimer. */
	sc->et.et_name = "iMXGPT";
	sc->et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERIODIC;
	sc->et.et_quality = 800;
	sc->et.et_frequency = sc->clkfreq;
	sc->et.et_min_period = ((uint64_t)setup_ticks << 32) / sc->clkfreq;
	sc->et.et_max_period = ((uint64_t)0xfffffffe  << 32) / sc->clkfreq;
	sc->et.et_start = imx_gpt_timer_start;
	sc->et.et_stop = imx_gpt_timer_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);

	/* Register as a timecounter. */
	imx_gpt_timecounter.tc_frequency = sc->clkfreq;
	imx_gpt_timecounter.tc_priv = sc;
	tc_init(&imx_gpt_timecounter);

	/* If this is the first unit, store the softc for use in DELAY. */
	if (device_get_unit(dev) == 0) {
		arm_set_delay(imx_gpt_do_delay, sc);
	}

	return (0);
}

static int
imx_gpt_timer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct imx_gpt_softc *sc;
	uint32_t ticks;

	sc = (struct imx_gpt_softc *)et->et_priv;

	if (period != 0) {
		sc->sc_period = ((uint32_t)et->et_frequency * period) >> 32;
		/* Set expected value */
		WRITE4(sc, IMX_GPT_OCR2, READ4(sc, IMX_GPT_CNT) + sc->sc_period);
		/* Enable compare register 2 Interrupt */
		sc->ir_reg |= GPT_IR_OF2;
		WRITE4(sc, IMX_GPT_IR, sc->ir_reg);
		return (0);
	} else if (first != 0) {
		/* Enable compare register 3 interrupt if not already on. */
		if ((sc->ir_reg & GPT_IR_OF3) == 0) {
			sc->ir_reg |= GPT_IR_OF3;
			WRITE4(sc, IMX_GPT_IR, sc->ir_reg);
		}
		ticks = ((uint32_t)et->et_frequency * first) >> 32;
		/* Do not disturb, otherwise event will be lost */
		spinlock_enter();
		/* Set expected value */
		WRITE4(sc, IMX_GPT_OCR3, READ4(sc, IMX_GPT_CNT) + ticks);
		/* Now everybody can relax */
		spinlock_exit();
		return (0);
	}

	return (EINVAL);
}

static int
imx_gpt_timer_stop(struct eventtimer *et)
{
	struct imx_gpt_softc *sc;

	sc = (struct imx_gpt_softc *)et->et_priv;

	/* Disable interrupts and clear any pending status. */
	sc->ir_reg &= ~(GPT_IR_OF2 | GPT_IR_OF3);
	WRITE4(sc, IMX_GPT_IR, sc->ir_reg);
	WRITE4(sc, IMX_GPT_SR, GPT_IR_OF2 | GPT_IR_OF3);
	sc->sc_period = 0;

	return (0);
}

static int
imx_gpt_intr(void *arg)
{
	struct imx_gpt_softc *sc;
	uint32_t status;

	sc = (struct imx_gpt_softc *)arg;

	status = READ4(sc, IMX_GPT_SR);

	/*
	* Clear interrupt status before invoking event callbacks.  The callback
	* often sets up a new one-shot timer event and if the interval is short
	* enough it can fire before we get out of this function.  If we cleared
	* at the bottom we'd miss the interrupt and hang until the clock wraps.
	*/
	WRITE4(sc, IMX_GPT_SR, status);

	/* Handle one-shot timer events. */
	if (status & GPT_IR_OF3) {
		if (sc->et.et_active) {
			sc->et.et_event_cb(&sc->et, sc->et.et_arg);
		}
	}

	/* Handle periodic timer events. */
	if (status & GPT_IR_OF2) {
		if (sc->et.et_active)
			sc->et.et_event_cb(&sc->et, sc->et.et_arg);
		if (sc->sc_period != 0)
			WRITE4(sc, IMX_GPT_OCR2, READ4(sc, IMX_GPT_CNT) +
			    sc->sc_period);
	}

	return (FILTER_HANDLED);
}

static u_int
imx_gpt_get_timecount(struct timecounter *tc)
{
	struct imx_gpt_softc *sc;

	sc = tc->tc_priv;
	return (READ4(sc, IMX_GPT_CNT));
}

static device_method_t imx_gpt_methods[] = {
	DEVMETHOD(device_probe,		imx_gpt_probe),
	DEVMETHOD(device_attach,	imx_gpt_attach),

	DEVMETHOD_END
};

static driver_t imx_gpt_driver = {
	"imx_gpt",
	imx_gpt_methods,
	sizeof(struct imx_gpt_softc),
};

static devclass_t imx_gpt_devclass;

EARLY_DRIVER_MODULE(imx_gpt, simplebus, imx_gpt_driver, imx_gpt_devclass, 0,
    0, BUS_PASS_TIMER);

static void
imx_gpt_do_delay(int usec, void *arg)
{
	struct imx_gpt_softc *sc = arg;
	uint64_t curcnt, endcnt, startcnt, ticks;

	/*
	 * Calculate the tick count with 64-bit values so that it works for any
	 * clock frequency.  Loop until the hardware count reaches start+ticks.
	 * If the 32-bit hardware count rolls over while we're looping, just
	 * manually do a carry into the high bits after each read; don't worry
	 * that doing this on each loop iteration is inefficient -- we're trying
	 * to waste time here.
	 */
	ticks = 1 + ((uint64_t)usec * sc->clkfreq) / 1000000;
	curcnt = startcnt = READ4(sc, IMX_GPT_CNT);
	endcnt = startcnt + ticks;
	while (curcnt < endcnt) {
		curcnt = READ4(sc, IMX_GPT_CNT);
		if (curcnt < startcnt)
			curcnt += 1ULL << 32;
	}
}
