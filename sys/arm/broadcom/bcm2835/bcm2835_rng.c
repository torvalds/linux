/*
 * Copyright (c) 2015, 2016, Stephen J. Kiernan
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/random.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/selinfo.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/random/randomdev.h>
#include <dev/random/random_harvestq.h>

static device_attach_t bcm2835_rng_attach;
static device_detach_t bcm2835_rng_detach;
static device_probe_t bcm2835_rng_probe;

#define	RNG_CTRL		0x00		/* RNG Control Register */
#define	RNG_COMBLK1_OSC		0x003f0000	/*  Combiner Blk 1 Oscillator */
#define	RNG_COMBLK1_OSC_SHIFT	16
#define	RNG_COMBLK2_OSC		0x0fc00000	/*  Combiner Blk 2 Oscillator */
#define	RNG_COMBLK2_OSC_SHIFT	22
#define	RNG_JCLK_BYP_DIV_CNT	0x0000ff00	/*  Jitter clk bypass divider
						    count */
#define	RNG_JCLK_BYP_DIV_CNT_SHIFT 8
#define	RNG_JCLK_BYP_SRC	0x00000020	/*  Jitter clk bypass source */
#define	RNG_JCLK_BYP_SEL	0x00000010	/*  Jitter clk bypass select */
#define	RNG_RBG2X		0x00000002	/*  RBG 2X SPEED */
#define	RNG_RBGEN_BIT		0x00000001	/*  Enable RNG bit */

#define	RNG_STATUS		0x04		/* RNG status register */
#define	RND_VAL_SHIFT		24		/*  Shift for valid words */
#define	RND_VAL_MASK		0x000000ff	/*  Number valid words mask */
#define	RND_VAL_WARM_CNT	0x40000		/*  RNG Warm Up count */
#define	RND_WARM_CNT		0xfffff		/*  RNG Warm Up Count mask */

#define	RNG_DATA		0x08		/* RNG Data Register */
#define	RNG_FF_THRES		0x0c
#define	RNG_FF_THRES_MASK	0x0000001f

#define	RNG_INT_MASK		0x10
#define	RNG_INT_OFF_BIT		0x00000001

#define	RNG_FF_DEFAULT		0x10		/* FIFO threshold default */

#define	RNG_FIFO_WORDS		(RNG_FF_DEFAULT / sizeof(uint32_t))

#define	RNG_NUM_OSCILLATORS	6
#define	RNG_STALL_COUNT_DEFAULT	10

#define	RNG_CALLOUT_TICKS	(hz * 4)

struct bcm2835_rng_softc {
	device_t		sc_dev;
	struct resource *	sc_mem_res;
	struct resource *	sc_irq_res;
	void *			sc_intr_hdl;
	uint32_t		sc_buf[RNG_FIFO_WORDS];
	struct callout		sc_rngto;
	int			sc_stall_count;
	int			sc_rbg2x;
	long			sc_underrun;
};

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-rng",	1},
	{"brcm,bcm2835-rng",		1},
	{NULL,				0}
};

static __inline void
bcm2835_rng_stat_inc_underrun(struct bcm2835_rng_softc *sc)
{

	atomic_add_long(&sc->sc_underrun, 1);
}

static __inline uint32_t
bcm2835_rng_read4(struct bcm2835_rng_softc *sc, bus_size_t off)
{

	return bus_read_4(sc->sc_mem_res, off);
}

static __inline void
bcm2835_rng_read_multi4(struct bcm2835_rng_softc *sc, bus_size_t off,
    uint32_t *datap, bus_size_t count)
{

	bus_read_multi_4(sc->sc_mem_res, off, datap, count);
}

static __inline void
bcm2835_rng_write4(struct bcm2835_rng_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->sc_mem_res, off, val);
}

static void
bcm2835_rng_dump_registers(struct bcm2835_rng_softc *sc, struct sbuf *sbp)
{
	uint32_t comblk2_osc, comblk1_osc, jclk_byp_div, val;
	int i;

	/* Display RNG control register contents */
	val = bcm2835_rng_read4(sc, RNG_CTRL);
	sbuf_printf(sbp, "RNG_CTRL (%08x)\n", val);

	comblk2_osc = (val & RNG_COMBLK2_OSC) >> RNG_COMBLK2_OSC_SHIFT;
	sbuf_printf(sbp, "  RNG_COMBLK2_OSC (%02x)\n", comblk2_osc);
	for (i = 0; i < RNG_NUM_OSCILLATORS; i++)
		if ((comblk2_osc & (1 << i)) == 0)
			sbuf_printf(sbp, "    Oscillator %d enabled\n", i + 1);

	comblk1_osc = (val & RNG_COMBLK1_OSC) >> RNG_COMBLK1_OSC_SHIFT;
	sbuf_printf(sbp, "  RNG_COMBLK1_OSC (%02x)\n", comblk1_osc);
	for (i = 0; i < RNG_NUM_OSCILLATORS; i++)
		if ((comblk1_osc & (1 << i)) == 0)
			sbuf_printf(sbp, "    Oscillator %d enabled\n", i + 1);

	jclk_byp_div = (val & RNG_JCLK_BYP_DIV_CNT) >>
	    RNG_JCLK_BYP_DIV_CNT_SHIFT;
	sbuf_printf(sbp,
	    "  RNG_JCLK_BYP_DIV_CNT (%02x)\n    APB clock frequency / %d\n",
	    jclk_byp_div, 2 * (jclk_byp_div + 1));

	sbuf_printf(sbp, "  RNG_JCLK_BYP_SRC:\n    %s\n",
	    (val & RNG_JCLK_BYP_SRC) ? "Use divided down APB clock" :
	    "Use RNG clock (APB clock)");

	sbuf_printf(sbp, "  RNG_JCLK_BYP_SEL:\n    %s\n",
	    (val & RNG_JCLK_BYP_SEL) ? "Bypass internal jitter clock" :
	    "Use internal jitter clock");

	if ((val & RNG_RBG2X) != 0)
		sbuf_cat(sbp, "  RNG_RBG2X: RNG 2X SPEED enabled\n");

	if ((val & RNG_RBGEN_BIT) != 0)
		sbuf_cat(sbp, "  RNG_RBGEN_BIT: RBG enabled\n");

	/* Display RNG status register contents */
	val = bcm2835_rng_read4(sc, RNG_STATUS);
	sbuf_printf(sbp, "RNG_CTRL (%08x)\n", val);
	sbuf_printf(sbp, "  RND_VAL: %02x\n",
	    (val >> RND_VAL_SHIFT) & RND_VAL_MASK);
	sbuf_printf(sbp, "  RND_WARM_CNT: %05x\n", val & RND_WARM_CNT);

	/* Display FIFO threshold register contents */
	val = bcm2835_rng_read4(sc, RNG_FF_THRES);
	sbuf_printf(sbp, "RNG_FF_THRES: %05x\n", val & RNG_FF_THRES_MASK);

	/* Display interrupt mask register contents */
	val = bcm2835_rng_read4(sc, RNG_INT_MASK);
	sbuf_printf(sbp, "RNG_INT_MASK: interrupt %s\n",
	     ((val & RNG_INT_OFF_BIT) != 0) ? "disabled" : "enabled");
}

static void
bcm2835_rng_disable_intr(struct bcm2835_rng_softc *sc)
{
	uint32_t mask;

	/* Set the interrupt off bit in the interrupt mask register */
	mask = bcm2835_rng_read4(sc, RNG_INT_MASK);
	mask |= RNG_INT_OFF_BIT;
        bcm2835_rng_write4(sc, RNG_INT_MASK, mask);
}

static void
bcm2835_rng_start(struct bcm2835_rng_softc *sc)
{
	uint32_t ctrl;

	/* Disable the interrupt */
	bcm2835_rng_disable_intr(sc);

	/* Set the warmup count */
	bcm2835_rng_write4(sc, RNG_STATUS, RND_VAL_WARM_CNT);

	/* Enable the RNG */
	ctrl = bcm2835_rng_read4(sc, RNG_CTRL);
	ctrl |= RNG_RBGEN_BIT;
	if (sc->sc_rbg2x)
		ctrl |= RNG_RBG2X;
	bcm2835_rng_write4(sc, RNG_CTRL, ctrl);
}

static void
bcm2835_rng_stop(struct bcm2835_rng_softc *sc)
{
	uint32_t ctrl;

	/* Disable the RNG */
	ctrl = bcm2835_rng_read4(sc, RNG_CTRL);
	ctrl &= ~RNG_RBGEN_BIT;
	bcm2835_rng_write4(sc, RNG_CTRL, ctrl);
}

static void
bcm2835_rng_harvest(void *arg)
{
	uint32_t *dest;
	uint32_t status;
	u_int cnt, nread, num_avail, num_words;
	int seen_underrun, num_stalls;
	struct bcm2835_rng_softc *sc = arg;

	dest = sc->sc_buf;
	nread = num_words = 0;
	seen_underrun = num_stalls = 0;
	for (cnt = sizeof(sc->sc_buf) / sizeof(uint32_t); cnt > 0;
	    cnt -= num_words) {
		/* Read status register to find out how many words available */
		status = bcm2835_rng_read4(sc, RNG_STATUS);
		num_avail = (status >> RND_VAL_SHIFT) & RND_VAL_MASK;

		/* If we have none... */
		if (num_avail == 0) {
			bcm2835_rng_stat_inc_underrun(sc);
			if (++seen_underrun >= sc->sc_stall_count) {
				if (num_stalls++ > 0) {
					device_printf(sc->sc_dev,
					    "RNG stalled, disabling device\n");
					bcm2835_rng_stop(sc);
					break;
				} else {
					device_printf(sc->sc_dev,
					    "Too many underruns, resetting\n");
					bcm2835_rng_stop(sc);
					bcm2835_rng_start(sc);
					seen_underrun = 0;
				}
			}
			/* Try again */
			continue;
		}

		CTR2(KTR_DEV, "%s: %d words available in RNG FIFO",
		    device_get_nameunit(sc->sc_dev), num_avail);

		/* Pull MIN(num_avail, cnt) words from the FIFO */
		num_words = (num_avail > cnt) ? cnt : num_avail;
		bcm2835_rng_read_multi4(sc, RNG_DATA, dest,
		    num_words);
		dest += num_words;
		nread += num_words;
	}

	cnt = nread * sizeof(uint32_t);
	if (cnt > 0)
		random_harvest_queue(sc->sc_buf, cnt, RANDOM_PURE_BROADCOM);

	callout_reset(&sc->sc_rngto, RNG_CALLOUT_TICKS, bcm2835_rng_harvest, sc);
}

static int
sysctl_bcm2835_rng_2xspeed(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_rng_softc *sc = arg1;
	int error, rbg2x;

	rbg2x = sc->sc_rbg2x;
	error = sysctl_handle_int(oidp, &rbg2x, 0, req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (error);
	if (rbg2x == sc->sc_rbg2x)
		return (0);

	/* Reset the RNG */
	bcm2835_rng_stop(sc);
	sc->sc_rbg2x = rbg2x;
	bcm2835_rng_start(sc);

	return (0);
}

#ifdef BCM2835_RNG_DEBUG_REGISTERS
static int
sysctl_bcm2835_rng_dump(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	struct bcm2835_rng_softc *sc = arg1;
	int error;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sb, NULL, 128, req);
	bcm2835_rng_dump_registers(sc, &sb);
        error = sbuf_finish(&sb);
        sbuf_delete(&sb);
        return (error);
}
#endif

static int
bcm2835_rng_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Broadcom BCM2835 RNG");

	return (BUS_PROBE_DEFAULT);
}

static int
bcm2835_rng_attach(device_t dev)
{
	struct bcm2835_rng_softc *sc;
	struct sysctl_ctx_list *sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	int error, rid;

	error = 0;
	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_stall_count = RNG_STALL_COUNT_DEFAULT;

	/* Initialize callout */
	callout_init(&sc->sc_rngto, CALLOUT_MPSAFE);

	TUNABLE_INT_FETCH("bcmrng.2xspeed", &sc->sc_rbg2x);
	TUNABLE_INT_FETCH("bcmrng.stall_count", &sc->sc_stall_count);

	/* Allocate memory resources */
	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		bcm2835_rng_detach(dev);
		return (ENXIO);
	}

	/* Start the RNG */
	bcm2835_rng_start(sc);

	/* Dump the registers if booting verbose */
	if (bootverbose) {
		struct sbuf sb;

		(void) sbuf_new(&sb, NULL, 256,
		    SBUF_AUTOEXTEND | SBUF_INCLUDENUL);
		bcm2835_rng_dump_registers(sc, &sb);
		sbuf_trim(&sb);
		error = sbuf_finish(&sb);
		if (error == 0)
			device_printf(dev, "%s", sbuf_data(&sb));
		sbuf_delete(&sb);
	}

	sysctl_ctx = device_get_sysctl_ctx(dev);
	sysctl_tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_LONG(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "underrun", CTLFLAG_RD, &sc->sc_underrun,
	    "Number of FIFO underruns");
	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "2xspeed", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    sysctl_bcm2835_rng_2xspeed, "I", "Enable RBG 2X SPEED");
	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "stall_count", CTLFLAG_RW, &sc->sc_stall_count,
	    RNG_STALL_COUNT_DEFAULT, "Number of underruns to assume RNG stall");
#ifdef BCM2835_RNG_DEBUG_REGISTERS
	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "dumpregs", CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    sysctl_bcm2835_rng_dump, "S", "Dump RNG registers");
#endif

	/* 
	 * Schedule the initial harvesting one second from now, which should give the
	 * hardware RNG plenty of time to generate the first random bytes.
	 */
	callout_reset(&sc->sc_rngto, hz, bcm2835_rng_harvest, sc);

	return (0);
}

static int
bcm2835_rng_detach(device_t dev)
{
	struct bcm2835_rng_softc *sc;

	sc = device_get_softc(dev);

	/* Stop the RNG */
	bcm2835_rng_stop(sc);

	/* Drain the callout it */
	callout_drain(&sc->sc_rngto);

	/* Release memory resource */
	if (sc->sc_mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (0);
}

static device_method_t bcm2835_rng_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm2835_rng_probe),
	DEVMETHOD(device_attach,	bcm2835_rng_attach),
	DEVMETHOD(device_detach,	bcm2835_rng_detach),

	DEVMETHOD_END
};

static driver_t bcm2835_rng_driver = {
	"bcmrng",
	bcm2835_rng_methods,
	sizeof(struct bcm2835_rng_softc)
};
static devclass_t bcm2835_rng_devclass;

DRIVER_MODULE(bcm2835_rng, simplebus, bcm2835_rng_driver,
    bcm2835_rng_devclass, 0, 0);
DRIVER_MODULE(bcm2835_rng, ofwbus, bcm2835_rng_driver, bcm2835_rng_devclass, 0,
    0);
MODULE_VERSION(bcm2835_rng, 1);
MODULE_DEPEND(bcm2835_rng, randomdev, 1, 1, 1);
