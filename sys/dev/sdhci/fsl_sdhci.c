/*-
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SDHCI driver glue for Freescale i.MX SoC and QorIQ families.
 *
 * This supports both eSDHC (earlier SoCs) and uSDHC (more recent SoCs).
 */

#include "opt_mmccam.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/time.h>

#include <machine/bus.h>
#include <machine/resource.h>
#ifdef __arm__
#include <machine/intr.h>

#include <arm/freescale/imx/imx_ccmvar.h>
#endif

#ifdef __powerpc__
#include <powerpc/mpc85xx/mpc85xx.h>
#endif

#include <dev/gpio/gpiobusvar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/bridge.h>

#include <dev/sdhci/sdhci.h>
#include <dev/sdhci/sdhci_fdt_gpio.h>

#include "mmcbr_if.h"
#include "sdhci_if.h"

struct fsl_sdhci_softc {
	device_t		dev;
	struct resource *	mem_res;
	struct resource *	irq_res;
	void *			intr_cookie;
	struct sdhci_slot	slot;
	struct callout		r1bfix_callout;
	sbintime_t		r1bfix_timeout_at;
	struct sdhci_fdt_gpio * gpio;
	uint32_t		baseclk_hz;
	uint32_t		cmd_and_mode;
	uint32_t		r1bfix_intmask;
	uint16_t		sdclockreg_freq_bits;
	uint8_t			r1bfix_type;
	uint8_t			hwtype;
	bool			slot_init_done;
};

#define	R1BFIX_NONE	0	/* No fix needed at next interrupt. */
#define	R1BFIX_NODATA	1	/* Synthesize DATA_END for R1B w/o data. */
#define	R1BFIX_AC12	2	/* Wait for busy after auto command 12. */

#define	HWTYPE_NONE	0	/* Hardware not recognized/supported. */
#define	HWTYPE_ESDHC	1	/* fsl5x and earlier. */
#define	HWTYPE_USDHC	2	/* fsl6. */

/*
 * Freescale-specific registers, or in some cases the layout of bits within the
 * sdhci-defined register is different on Freescale.  These names all begin with
 * SDHC_ (not SDHCI_).
 */

#define	SDHC_WTMK_LVL		0x44	/* Watermark Level register. */
#define	USDHC_MIX_CONTROL	0x48	/* Mix(ed) Control register. */
#define	SDHC_VEND_SPEC		0xC0	/* Vendor-specific register. */
#define	 SDHC_VEND_FRC_SDCLK_ON	(1 <<  8)
#define	 SDHC_VEND_IPGEN	(1 << 11)
#define	 SDHC_VEND_HCKEN	(1 << 12)
#define	 SDHC_VEND_PEREN	(1 << 13)

#define	SDHC_PRES_STATE		0x24
#define	  SDHC_PRES_CIHB	  (1 <<  0)
#define	  SDHC_PRES_CDIHB	  (1 <<  1)
#define	  SDHC_PRES_DLA		  (1 <<  2)
#define	  SDHC_PRES_SDSTB	  (1 <<  3)
#define	  SDHC_PRES_IPGOFF	  (1 <<  4)
#define	  SDHC_PRES_HCKOFF	  (1 <<  5)
#define	  SDHC_PRES_PEROFF	  (1 <<  6)
#define	  SDHC_PRES_SDOFF	  (1 <<  7)
#define	  SDHC_PRES_WTA		  (1 <<  8)
#define	  SDHC_PRES_RTA		  (1 <<  9)
#define	  SDHC_PRES_BWEN	  (1 << 10)
#define	  SDHC_PRES_BREN	  (1 << 11)
#define	  SDHC_PRES_RTR		  (1 << 12)
#define	  SDHC_PRES_CINST	  (1 << 16)
#define	  SDHC_PRES_CDPL	  (1 << 18)
#define	  SDHC_PRES_WPSPL	  (1 << 19)
#define	  SDHC_PRES_CLSL	  (1 << 23)
#define	  SDHC_PRES_DLSL_SHIFT	  24
#define	  SDHC_PRES_DLSL_MASK	  (0xffU << SDHC_PRES_DLSL_SHIFT)

#define	SDHC_PROT_CTRL		0x28
#define	 SDHC_PROT_LED		(1 << 0)
#define	 SDHC_PROT_WIDTH_1BIT	(0 << 1)
#define	 SDHC_PROT_WIDTH_4BIT	(1 << 1)
#define	 SDHC_PROT_WIDTH_8BIT	(2 << 1)
#define	 SDHC_PROT_WIDTH_MASK	(3 << 1)
#define	 SDHC_PROT_D3CD		(1 << 3)
#define	 SDHC_PROT_EMODE_BIG	(0 << 4)
#define	 SDHC_PROT_EMODE_HALF	(1 << 4)
#define	 SDHC_PROT_EMODE_LITTLE	(2 << 4)
#define	 SDHC_PROT_EMODE_MASK	(3 << 4)
#define	 SDHC_PROT_SDMA		(0 << 8)
#define	 SDHC_PROT_ADMA1	(1 << 8)
#define	 SDHC_PROT_ADMA2	(2 << 8)
#define	 SDHC_PROT_ADMA264	(3 << 8)
#define	 SDHC_PROT_DMA_MASK	(3 << 8)
#define	 SDHC_PROT_CDTL		(1 << 6)
#define	 SDHC_PROT_CDSS		(1 << 7)

#define	SDHC_SYS_CTRL		0x2c

/*
 * The clock enable bits exist in different registers for ESDHC vs USDHC, but
 * they are the same bits in both cases.  The divisor values go into the
 * standard sdhci clock register, but in different bit positions and meanings
   than the sdhci spec values.
 */
#define	SDHC_CLK_IPGEN		(1 << 0)
#define	SDHC_CLK_HCKEN		(1 << 1)
#define	SDHC_CLK_PEREN		(1 << 2)
#define	SDHC_CLK_SDCLKEN	(1 << 3)
#define	SDHC_CLK_ENABLE_MASK	0x0000000f
#define	SDHC_CLK_DIVISOR_MASK	0x000000f0
#define	SDHC_CLK_DIVISOR_SHIFT	4
#define	SDHC_CLK_PRESCALE_MASK	0x0000ff00
#define	SDHC_CLK_PRESCALE_SHIFT	8

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6q-usdhc",	HWTYPE_USDHC},
	{"fsl,imx6sl-usdhc",	HWTYPE_USDHC},
	{"fsl,imx53-esdhc",	HWTYPE_ESDHC},
	{"fsl,imx51-esdhc",	HWTYPE_ESDHC},
	{"fsl,esdhc",		HWTYPE_ESDHC},
	{NULL,			HWTYPE_NONE},
};

static uint16_t fsl_sdhc_get_clock(struct fsl_sdhci_softc *sc);
static void fsl_sdhc_set_clock(struct fsl_sdhci_softc *sc, uint16_t val);
static void fsl_sdhci_r1bfix_func(void *arg);

static inline uint32_t
RD4(struct fsl_sdhci_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static inline void
WR4(struct fsl_sdhci_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}

static uint8_t
fsl_sdhci_read_1(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32, wrk32;

	/*
	 * Most of the things in the standard host control register are in the
	 * hardware's wider protocol control register, but some of the bits are
	 * moved around.
	 */
	if (off == SDHCI_HOST_CONTROL) {
		wrk32 = RD4(sc, SDHC_PROT_CTRL);
		val32 = wrk32 & (SDHCI_CTRL_LED | SDHCI_CTRL_CARD_DET |
		    SDHCI_CTRL_FORCE_CARD);
		switch (wrk32 & SDHC_PROT_WIDTH_MASK) {
		case SDHC_PROT_WIDTH_1BIT:
			/* Value is already 0. */
			break;
		case SDHC_PROT_WIDTH_4BIT:
			val32 |= SDHCI_CTRL_4BITBUS;
			break;
		case SDHC_PROT_WIDTH_8BIT:
			val32 |= SDHCI_CTRL_8BITBUS;
			break;
		}
		switch (wrk32 & SDHC_PROT_DMA_MASK) {
		case SDHC_PROT_SDMA:
			/* Value is already 0. */
			break;
		case SDHC_PROT_ADMA1:
			/* This value is deprecated, should never appear. */
			break;
		case SDHC_PROT_ADMA2:
			val32 |= SDHCI_CTRL_ADMA2;
			break;
		case SDHC_PROT_ADMA264:
			val32 |= SDHCI_CTRL_ADMA264;
			break;
		}
		return val32;
	}

	/*
	 * XXX can't find the bus power on/off knob.  For now we have to say the
	 * power is always on and always set to the same voltage.
	 */
	if (off == SDHCI_POWER_CONTROL) {
		return (SDHCI_POWER_ON | SDHCI_POWER_300);
	}


	return ((RD4(sc, off & ~3) >> (off & 3) * 8) & 0xff);
}

static uint16_t
fsl_sdhci_read_2(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32;

	if (sc->hwtype == HWTYPE_USDHC) {
		/*
		 * The USDHC hardware has nothing in the version register, but
		 * it's v3 compatible with all our translation code.
		 */
		if (off == SDHCI_HOST_VERSION) {
			return (SDHCI_SPEC_300 << SDHCI_SPEC_VER_SHIFT);
		}
		/*
		 * The USDHC hardware moved the transfer mode bits to the mixed
		 * control register, fetch them from there.
		 */
		if (off == SDHCI_TRANSFER_MODE)
			return (RD4(sc, USDHC_MIX_CONTROL) & 0x37);

	} else if (sc->hwtype == HWTYPE_ESDHC) {

		/*
		 * The ESDHC hardware has the typical 32-bit combined "command
		 * and mode" register that we have to cache so that command
		 * isn't written until after mode.  On a read, just retrieve the
		 * cached values last written.
		 */
		if (off == SDHCI_TRANSFER_MODE) {
			return (sc->cmd_and_mode & 0x0000ffff);
		} else if (off == SDHCI_COMMAND_FLAGS) {
			return (sc->cmd_and_mode >> 16);
		}
	}

	/*
	 * This hardware only manages one slot.  Synthesize a slot interrupt
	 * status register... if there are any enabled interrupts active they
	 * must be coming from our one and only slot.
	 */
	if (off == SDHCI_SLOT_INT_STATUS) {
		val32  = RD4(sc, SDHCI_INT_STATUS);
		val32 &= RD4(sc, SDHCI_SIGNAL_ENABLE);
		return (val32 ? 1 : 0);
	}

	/*
	 * Clock bits are scattered into various registers which differ by
	 * hardware type, complex enough to have their own function.
	 */
	if (off == SDHCI_CLOCK_CONTROL) {
		return (fsl_sdhc_get_clock(sc));
	}

	return ((RD4(sc, off & ~3) >> (off & 3) * 8) & 0xffff);
}

static uint32_t
fsl_sdhci_read_4(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32, wrk32;

	val32 = RD4(sc, off);

	/*
	 * The hardware leaves the base clock frequency out of the capabilities
	 * register, but we filled it in by setting slot->max_clk at attach time
	 * rather than here, because we can't represent frequencies above 63MHz
	 * in an sdhci 2.0 capabliities register.  The timeout clock is the same
	 * as the active output sdclock; we indicate that with a quirk setting
	 * so don't populate the timeout frequency bits.
	 *
	 * XXX Turn off (for now) features the hardware can do but this driver
	 * doesn't yet handle (1.8v, suspend/resume, etc).
	 */
	if (off == SDHCI_CAPABILITIES) {
		val32 &= ~SDHCI_CAN_VDD_180;
		val32 &= ~SDHCI_CAN_DO_SUSPEND;
		val32 |= SDHCI_CAN_DO_8BITBUS;
		return (val32);
	}
	
	/*
	 * The hardware moves bits around in the present state register to make
	 * room for all 8 data line state bits.  To translate, mask out all the
	 * bits which are not in the same position in both registers (this also
	 * masks out some Freescale-specific bits in locations defined as
	 * reserved by sdhci), then shift the data line and retune request bits
	 * down to their standard locations.
	 */
	if (off == SDHCI_PRESENT_STATE) {
		wrk32 = val32;
		val32 &= 0x000F0F07;
		val32 |= (wrk32 >> 4) & SDHCI_STATE_DAT_MASK;
		val32 |= (wrk32 >> 9) & SDHCI_RETUNE_REQUEST;
		return (val32);
	}

	/*
	 * fsl_sdhci_intr() can synthesize a DATA_END interrupt following a
	 * command with an R1B response, mix it into the hardware status.
	 */
	if (off == SDHCI_INT_STATUS) {
		return (val32 | sc->r1bfix_intmask);
	}

	return val32;
}

static void
fsl_sdhci_read_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);

	bus_read_multi_4(sc->mem_res, off, data, count);
}

static void
fsl_sdhci_write_1(device_t dev, struct sdhci_slot *slot, bus_size_t off, uint8_t val)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32;

	/*
	 * Most of the things in the standard host control register are in the
	 * hardware's wider protocol control register, but some of the bits are
	 * moved around.
	 */
	if (off == SDHCI_HOST_CONTROL) {
		val32 = RD4(sc, SDHC_PROT_CTRL);
		val32 &= ~(SDHC_PROT_LED | SDHC_PROT_DMA_MASK |
		    SDHC_PROT_WIDTH_MASK | SDHC_PROT_CDTL | SDHC_PROT_CDSS);
		val32 |= (val & SDHCI_CTRL_LED);
		if (val & SDHCI_CTRL_8BITBUS)
			val32 |= SDHC_PROT_WIDTH_8BIT;
		else
			val32 |= (val & SDHCI_CTRL_4BITBUS);
		val32 |= (val & (SDHCI_CTRL_SDMA | SDHCI_CTRL_ADMA2)) << 4;
		val32 |= (val & (SDHCI_CTRL_CARD_DET | SDHCI_CTRL_FORCE_CARD));
		WR4(sc, SDHC_PROT_CTRL, val32);
		return;
	}

	/* XXX I can't find the bus power on/off knob; do nothing. */
	if (off == SDHCI_POWER_CONTROL) {
		return;
	}
#ifdef __powerpc__
	/* XXX Reset doesn't seem to work as expected.  Do nothing for now. */
	if (off == SDHCI_SOFTWARE_RESET)
		return;
#endif

	val32 = RD4(sc, off & ~3);
	val32 &= ~(0xff << (off & 3) * 8);
	val32 |= (val << (off & 3) * 8);

	WR4(sc, off & ~3, val32);
}

static void
fsl_sdhci_write_2(device_t dev, struct sdhci_slot *slot, bus_size_t off, uint16_t val)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32;

	/*
	 * The clock control stuff is complex enough to have its own function
	 * that can handle the ESDHC versus USDHC differences.
	 */
	if (off == SDHCI_CLOCK_CONTROL) {
		fsl_sdhc_set_clock(sc, val);
		return;
	}

	/*
	 * Figure out whether we need to check the DAT0 line for busy status at
	 * interrupt time.  The controller should be doing this, but for some
	 * reason it doesn't.  There are two cases:
	 *  - R1B response with no data transfer should generate a DATA_END (aka
	 *    TRANSFER_COMPLETE) interrupt after waiting for busy, but if
	 *    there's no data transfer there's no DATA_END interrupt.  This is
	 *    documented; they seem to think it's a feature.
	 *  - R1B response after Auto-CMD12 appears to not work, even though
	 *    there's a control bit for it (bit 3) in the vendor register.
	 * When we're starting a command that needs a manual DAT0 line check at
	 * interrupt time, we leave ourselves a note in r1bfix_type so that we
	 * can do the extra work in fsl_sdhci_intr().
	 */
	if (off == SDHCI_COMMAND_FLAGS) {
		if (val & SDHCI_CMD_DATA) {
			const uint32_t MBAUTOCMD = SDHCI_TRNS_ACMD12 | SDHCI_TRNS_MULTI;
			val32 = RD4(sc, USDHC_MIX_CONTROL);
			if ((val32 & MBAUTOCMD) == MBAUTOCMD)
				sc->r1bfix_type = R1BFIX_AC12;
		} else {
			if ((val & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_SHORT_BUSY) {
				WR4(sc, SDHCI_INT_ENABLE, slot->intmask | SDHCI_INT_RESPONSE);
				WR4(sc, SDHCI_SIGNAL_ENABLE, slot->intmask | SDHCI_INT_RESPONSE);
				sc->r1bfix_type = R1BFIX_NODATA;
			}
		}
	}

	/*
	 * The USDHC hardware moved the transfer mode bits to mixed control; we
	 * just write them there and we're done.  The ESDHC hardware has the
	 * typical combined cmd-and-mode register that allows only 32-bit
	 * access, so when writing the mode bits just save them, then later when
	 * writing the command bits, add in the saved mode bits.
	 */
	if (sc->hwtype == HWTYPE_USDHC) {
		if (off == SDHCI_TRANSFER_MODE) {
			val32 = RD4(sc, USDHC_MIX_CONTROL);
			val32 &= ~0x3f;
			val32 |= val & 0x37;
			// XXX acmd23 not supported here (or by sdhci driver)
			WR4(sc, USDHC_MIX_CONTROL, val32);
			return;
		}
	} else if (sc->hwtype == HWTYPE_ESDHC) {
		if (off == SDHCI_TRANSFER_MODE) {
			sc->cmd_and_mode =
			    (sc->cmd_and_mode & 0xffff0000) | val;
			return;
		} else if (off == SDHCI_COMMAND_FLAGS) {
			sc->cmd_and_mode =
			    (sc->cmd_and_mode & 0xffff) | (val << 16);
			WR4(sc, SDHCI_TRANSFER_MODE, sc->cmd_and_mode);
			return;
		}
	}

	val32 = RD4(sc, off & ~3);
	val32 &= ~(0xffff << (off & 3) * 8);
	val32 |= ((val & 0xffff) << (off & 3) * 8);
	WR4(sc, off & ~3, val32);	
}

static void
fsl_sdhci_write_4(device_t dev, struct sdhci_slot *slot, bus_size_t off, uint32_t val)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);

	/* Clear synthesized interrupts, then pass the value to the hardware. */
	if (off == SDHCI_INT_STATUS) {
		sc->r1bfix_intmask &= ~val;
	}

	WR4(sc, off, val);
}

static void
fsl_sdhci_write_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);

	bus_write_multi_4(sc->mem_res, off, data, count);
}

static uint16_t
fsl_sdhc_get_clock(struct fsl_sdhci_softc *sc)
{
	uint16_t val;

	/*
	 * Whenever the sdhci driver writes the clock register we save a
	 * snapshot of just the frequency bits, so that we can play them back
	 * here on a register read without recalculating the frequency from the
	 * prescalar and divisor bits in the real register.  We'll start with
	 * those bits, and mix in the clock status and enable bits that come
	 * from different places depending on which hardware we've got.
	 */
	val = sc->sdclockreg_freq_bits;

	/*
	 * The internal clock is always enabled (actually, the hardware manages
	 * it).  Whether the internal clock is stable yet after a frequency
	 * change comes from the present-state register on both hardware types.
	 */
	val |= SDHCI_CLOCK_INT_EN;
	if (RD4(sc, SDHC_PRES_STATE) & SDHC_PRES_SDSTB)
	    val |= SDHCI_CLOCK_INT_STABLE;

	/*
	 * On i.MX ESDHC hardware the card bus clock enable is in the usual
	 * sdhci register but it's a different bit, so transcribe it (note the
	 * difference between standard SDHCI_ and Freescale SDHC_ prefixes
	 * here). On USDHC and QorIQ ESDHC hardware there is a force-on bit, but
	 * no force-off for the card bus clock (the hardware runs the clock when
	 * transfers are active no matter what), so we always say the clock is
	 * on.
	 * XXX Maybe we should say it's in whatever state the sdhci driver last
	 * set it to.
	 */
	if (sc->hwtype == HWTYPE_ESDHC) {
#ifdef __arm__
		if (RD4(sc, SDHC_SYS_CTRL) & SDHC_CLK_SDCLKEN)
#endif
			val |= SDHCI_CLOCK_CARD_EN;
	} else {
		val |= SDHCI_CLOCK_CARD_EN;
	}

	return (val);
}

static void 
fsl_sdhc_set_clock(struct fsl_sdhci_softc *sc, uint16_t val)
{
	uint32_t divisor, freq, prescale, val32;

	val32 = RD4(sc, SDHCI_CLOCK_CONTROL);

	/*
	 * Save the frequency-setting bits in SDHCI format so that we can play
	 * them back in get_clock without complex decoding of hardware regs,
	 * then deal with the freqency part of the value based on hardware type.
	 */
	sc->sdclockreg_freq_bits = val & SDHCI_DIVIDERS_MASK;
	if (sc->hwtype == HWTYPE_ESDHC) {
		/*
		 * The i.MX5 ESDHC hardware requires the driver to manually
		 * start and stop the sd bus clock.  If the enable bit is not
		 * set, turn off the clock in hardware and we're done, otherwise
		 * decode the requested frequency.  ESDHC hardware is sdhci 2.0;
		 * the sdhci driver will use the original 8-bit divisor field
		 * and the "base / 2^N" divisor scheme.
		 */
		if ((val & SDHCI_CLOCK_CARD_EN) == 0) {
#ifdef __arm__
			/* On QorIQ, this is a reserved bit. */
			WR4(sc, SDHCI_CLOCK_CONTROL, val32 & ~SDHC_CLK_SDCLKEN);
#endif
			return;

		}
		divisor = (val >> SDHCI_DIVIDER_SHIFT) & SDHCI_DIVIDER_MASK;
		freq = sc->baseclk_hz >> ffs(divisor);
	} else {
		/*
		 * The USDHC hardware provides only "force always on" control
		 * over the sd bus clock, but no way to turn it off.  (If a cmd
		 * or data transfer is in progress the clock is on, otherwise it
		 * is off.)  If the clock is being disabled, we can just return
		 * now, otherwise we decode the requested frequency.  USDHC
		 * hardware is sdhci 3.0; the sdhci driver will use a 10-bit
		 * divisor using the "base / 2*N" divisor scheme.
		 */
		if ((val & SDHCI_CLOCK_CARD_EN) == 0)
			return;
		divisor = ((val >> SDHCI_DIVIDER_SHIFT) & SDHCI_DIVIDER_MASK) |
		    ((val >> SDHCI_DIVIDER_HI_SHIFT) & SDHCI_DIVIDER_HI_MASK) <<
		    SDHCI_DIVIDER_MASK_LEN;
		if (divisor == 0)
			freq = sc->baseclk_hz;
		else
			freq = sc->baseclk_hz / (2 * divisor);
	}

	/*
	 * Get a prescaler and final divisor to achieve the desired frequency.
	 */
	for (prescale = 2; freq < sc->baseclk_hz / (prescale * 16);)
		prescale <<= 1;

	for (divisor = 1; freq < sc->baseclk_hz / (prescale * divisor);)
		++divisor;

#ifdef DEBUG	
	device_printf(sc->dev,
	    "desired SD freq: %d, actual: %d; base %d prescale %d divisor %d\n",
	    freq, sc->baseclk_hz / (prescale * divisor), sc->baseclk_hz, 
	    prescale, divisor);
#endif	

	/*
	 * Adjust to zero-based values, and store them to the hardware.
	 */
	prescale >>= 1;
	divisor -= 1;

	val32 &= ~(SDHC_CLK_DIVISOR_MASK | SDHC_CLK_PRESCALE_MASK);
	val32 |= divisor << SDHC_CLK_DIVISOR_SHIFT;
	val32 |= prescale << SDHC_CLK_PRESCALE_SHIFT;
	val32 |= SDHC_CLK_IPGEN;
	WR4(sc, SDHCI_CLOCK_CONTROL, val32);
}

static boolean_t
fsl_sdhci_r1bfix_is_wait_done(struct fsl_sdhci_softc *sc)
{
	uint32_t inhibit;

	mtx_assert(&sc->slot.mtx, MA_OWNED);

	/*
	 * Check the DAT0 line status using both the DLA (data line active) and
	 * CDIHB (data inhibit) bits in the present state register.  In theory
	 * just DLA should do the trick,  but in practice it takes both.  If the
	 * DAT0 line is still being held and we're not yet beyond the timeout
	 * point, just schedule another callout to check again later.
	 */
	inhibit = RD4(sc, SDHC_PRES_STATE) & (SDHC_PRES_DLA | SDHC_PRES_CDIHB);

	if (inhibit && getsbinuptime() < sc->r1bfix_timeout_at) {
		callout_reset_sbt(&sc->r1bfix_callout, SBT_1MS, 0, 
		    fsl_sdhci_r1bfix_func, sc, 0);
		return (false);
	}

	/*
	 * If we reach this point with the inhibit bits still set, we've got a
	 * timeout, synthesize a DATA_TIMEOUT interrupt.  Otherwise the DAT0
	 * line has been released, and we synthesize a DATA_END, and if the type
	 * of fix needed was on a command-without-data we also now add in the
	 * original INT_RESPONSE that we suppressed earlier.
	 */
	if (inhibit)
		sc->r1bfix_intmask |= SDHCI_INT_DATA_TIMEOUT;
	else {
		sc->r1bfix_intmask |= SDHCI_INT_DATA_END;
		if (sc->r1bfix_type == R1BFIX_NODATA)
			sc->r1bfix_intmask |= SDHCI_INT_RESPONSE;
	}

	sc->r1bfix_type = R1BFIX_NONE;
	return (true);
}

static void
fsl_sdhci_r1bfix_func(void * arg)
{
	struct fsl_sdhci_softc *sc = arg;
	boolean_t r1bwait_done;

	mtx_lock(&sc->slot.mtx);
	r1bwait_done = fsl_sdhci_r1bfix_is_wait_done(sc);
	mtx_unlock(&sc->slot.mtx);
	if (r1bwait_done)
		sdhci_generic_intr(&sc->slot);
}

static void
fsl_sdhci_intr(void *arg)
{
	struct fsl_sdhci_softc *sc = arg;
	uint32_t intmask;

	mtx_lock(&sc->slot.mtx);

	/*
	 * Manually check the DAT0 line for R1B response types that the
	 * controller fails to handle properly.  The controller asserts the done
	 * interrupt while the card is still asserting busy with the DAT0 line.
	 *
	 * We check DAT0 immediately because most of the time, especially on a
	 * read, the card will actually be done by time we get here.  If it's
	 * not, then the wait_done routine will schedule a callout to re-check
	 * periodically until it is done.  In that case we clear the interrupt
	 * out of the hardware now so that we can present it later when the DAT0
	 * line is released.
	 *
	 * If we need to wait for the DAT0 line to be released, we set up a
	 * timeout point 250ms in the future.  This number comes from the SD
	 * spec, which allows a command to take that long.  In the real world,
	 * cards tend to take 10-20ms for a long-running command such as a write
	 * or erase that spans two pages.
	 */
	switch (sc->r1bfix_type) {
	case R1BFIX_NODATA:
		intmask = RD4(sc, SDHCI_INT_STATUS) & SDHCI_INT_RESPONSE;
		break;
	case R1BFIX_AC12:
		intmask = RD4(sc, SDHCI_INT_STATUS) & SDHCI_INT_DATA_END;
		break;
	default:
		intmask = 0;
		break;
	}
	if (intmask) {
		sc->r1bfix_timeout_at = getsbinuptime() + 250 * SBT_1MS;
		if (!fsl_sdhci_r1bfix_is_wait_done(sc)) {
			WR4(sc, SDHCI_INT_STATUS, intmask);
			bus_barrier(sc->mem_res, SDHCI_INT_STATUS, 4, 
			    BUS_SPACE_BARRIER_WRITE);
		}
	}

	mtx_unlock(&sc->slot.mtx);
	sdhci_generic_intr(&sc->slot);
}

static int
fsl_sdhci_get_ro(device_t bus, device_t child)
{
	struct fsl_sdhci_softc *sc = device_get_softc(bus);

	return (sdhci_fdt_gpio_get_readonly(sc->gpio));
}

static bool
fsl_sdhci_get_card_present(device_t dev, struct sdhci_slot *slot)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);

	return (sdhci_fdt_gpio_get_present(sc->gpio));
}

#ifdef __powerpc__
static uint32_t
fsl_sdhci_get_platform_clock(device_t dev)
{
	phandle_t node;
	uint32_t clock;

	node = ofw_bus_get_node(dev);

	/* Get sdhci node properties */
	if((OF_getprop(node, "clock-frequency", (void *)&clock,
	    sizeof(clock)) <= 0) || (clock == 0)) {

		clock = mpc85xx_get_system_clock();

		if (clock == 0) {
			device_printf(dev,"Cannot acquire correct sdhci "
			    "frequency from DTS.\n");

			return (0);
		}
	}

	if (bootverbose)
		device_printf(dev, "Acquired clock: %d from DTS\n", clock);

	return (clock);
}
#endif


static int
fsl_sdhci_detach(device_t dev)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);

	if (sc->gpio != NULL)
		sdhci_fdt_gpio_teardown(sc->gpio);

	callout_drain(&sc->r1bfix_callout);

	if (sc->slot_init_done)
		sdhci_cleanup_slot(&sc->slot);

	if (sc->intr_cookie != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);

	if (sc->mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
	}

	return (0);
}

static int
fsl_sdhci_attach(device_t dev)
{
	struct fsl_sdhci_softc *sc = device_get_softc(dev);
	int rid, err;
#ifdef __powerpc__
	phandle_t node;
	uint32_t protctl;
#endif

	sc->dev = dev;

	callout_init(&sc->r1bfix_callout, 1);

	sc->hwtype = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (sc->hwtype == HWTYPE_NONE)
		panic("Impossible: not compatible in fsl_sdhci_attach()");

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		err = ENXIO;
		goto fail;
	}

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->irq_res) {
		device_printf(dev, "cannot allocate interrupt\n");
		err = ENXIO;
		goto fail;
	}

	if (bus_setup_intr(dev, sc->irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, fsl_sdhci_intr, sc, &sc->intr_cookie)) {
		device_printf(dev, "cannot setup interrupt handler\n");
		err = ENXIO;
		goto fail;
	}

	sc->slot.quirks |= SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK;

	/*
	 * DMA is not really broken, I just haven't implemented it yet.
	 */
	sc->slot.quirks |= SDHCI_QUIRK_BROKEN_DMA;

	/*
	 * Set the buffer watermark level to 128 words (512 bytes) for both read
	 * and write.  The hardware has a restriction that when the read or
	 * write ready status is asserted, that means you can read exactly the
	 * number of words set in the watermark register before you have to
	 * re-check the status and potentially wait for more data.  The main
	 * sdhci driver provides no hook for doing status checking on less than
	 * a full block boundary, so we set the watermark level to be a full
	 * block.  Reads and writes where the block size is less than the
	 * watermark size will work correctly too, no need to change the
	 * watermark for different size blocks.  However, 128 is the maximum
	 * allowed for the watermark, so PIO is limitted to 512 byte blocks
	 * (which works fine for SD cards, may be a problem for SDIO some day).
	 *
	 * XXX need named constants for this stuff.
	 */
	/* P1022 has the '*_BRST_LEN' fields as reserved, always reading 0x10 */
	if (ofw_bus_is_compatible(dev, "fsl,p1022-esdhc"))
		WR4(sc, SDHC_WTMK_LVL, 0x10801080);
	else
		WR4(sc, SDHC_WTMK_LVL, 0x08800880);

	/*
	 * We read in native byte order in the main driver, but the register
	 * defaults to little endian.
	 */
#ifdef __powerpc__
	sc->baseclk_hz = fsl_sdhci_get_platform_clock(dev);
#else
	sc->baseclk_hz = imx_ccm_sdhci_hz();
#endif
	sc->slot.max_clk = sc->baseclk_hz;

	/*
	 * Set up any gpio pin handling described in the FDT data. This cannot
	 * fail; see comments in sdhci_fdt_gpio.h for details.
	 */
	sc->gpio = sdhci_fdt_gpio_setup(dev, &sc->slot);

#ifdef __powerpc__
	node = ofw_bus_get_node(dev);
	/* Default to big-endian on powerpc */
	protctl = RD4(sc, SDHC_PROT_CTRL);
	protctl &= ~SDHC_PROT_EMODE_MASK;
	if (OF_hasprop(node, "little-endian"))
		protctl |= SDHC_PROT_EMODE_LITTLE;
	else
		protctl |= SDHC_PROT_EMODE_BIG;
	WR4(sc, SDHC_PROT_CTRL, protctl);
#endif

	sdhci_init_slot(dev, &sc->slot, 0);
	sc->slot_init_done = true;

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	sdhci_start_slot(&sc->slot);

	return (0);

fail:
	fsl_sdhci_detach(dev);
	return (err);
}

static int
fsl_sdhci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
	case HWTYPE_ESDHC:
		device_set_desc(dev, "Freescale eSDHC controller");
		return (BUS_PROBE_DEFAULT);
	case HWTYPE_USDHC:
		device_set_desc(dev, "Freescale uSDHC controller");
		return (BUS_PROBE_DEFAULT);
	default:
		break;
	}
	return (ENXIO);
}

static device_method_t fsl_sdhci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fsl_sdhci_probe),
	DEVMETHOD(device_attach,	fsl_sdhci_attach),
	DEVMETHOD(device_detach,	fsl_sdhci_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sdhci_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	sdhci_generic_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	sdhci_generic_update_ios),
	DEVMETHOD(mmcbr_request,	sdhci_generic_request),
	DEVMETHOD(mmcbr_get_ro,		fsl_sdhci_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	sdhci_generic_acquire_host),
	DEVMETHOD(mmcbr_release_host,	sdhci_generic_release_host),

	/* SDHCI accessors */
	DEVMETHOD(sdhci_read_1,		fsl_sdhci_read_1),
	DEVMETHOD(sdhci_read_2,		fsl_sdhci_read_2),
	DEVMETHOD(sdhci_read_4,		fsl_sdhci_read_4),
	DEVMETHOD(sdhci_read_multi_4,	fsl_sdhci_read_multi_4),
	DEVMETHOD(sdhci_write_1,	fsl_sdhci_write_1),
	DEVMETHOD(sdhci_write_2,	fsl_sdhci_write_2),
	DEVMETHOD(sdhci_write_4,	fsl_sdhci_write_4),
	DEVMETHOD(sdhci_write_multi_4,	fsl_sdhci_write_multi_4),
	DEVMETHOD(sdhci_get_card_present,fsl_sdhci_get_card_present),

	DEVMETHOD_END
};

static devclass_t fsl_sdhci_devclass;

static driver_t fsl_sdhci_driver = {
	"sdhci_fsl",
	fsl_sdhci_methods,
	sizeof(struct fsl_sdhci_softc),
};

DRIVER_MODULE(sdhci_fsl, simplebus, fsl_sdhci_driver, fsl_sdhci_devclass,
    NULL, NULL);
SDHCI_DEPEND(sdhci_fsl);

#ifndef MMCCAM
MMC_DECLARE_BRIDGE(sdhci_fsl);
#endif
