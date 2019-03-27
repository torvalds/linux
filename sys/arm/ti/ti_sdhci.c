/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
 * Copyright (c) 2011 Ben Gray <ben.r.gray@gmail.com>.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmcbrvar.h>

#include <dev/sdhci/sdhci.h>
#include <dev/sdhci/sdhci_fdt_gpio.h>
#include "sdhci_if.h"

#include <arm/ti/ti_cpuid.h>
#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>
#include "gpio_if.h"

#include "opt_mmccam.h"

struct ti_sdhci_softc {
	device_t		dev;
	struct sdhci_fdt_gpio * gpio;
	struct resource *	mem_res;
	struct resource *	irq_res;
	void *			intr_cookie;
	struct sdhci_slot	slot;
	clk_ident_t		mmchs_clk_id;
	uint32_t		mmchs_reg_off;
	uint32_t		sdhci_reg_off;
	uint32_t		baseclk_hz;
	uint32_t		cmd_and_mode;
	uint32_t		sdhci_clkdiv;
	boolean_t		disable_highspeed;
	boolean_t		force_card_present;
	boolean_t		disable_readonly;
};

/*
 * Table of supported FDT compat strings.
 *
 * Note that "ti,mmchs" is our own invention, and should be phased out in favor
 * of the documented names.
 *
 * Note that vendor Beaglebone dtsi files use "ti,omap3-hsmmc" for the am335x.
 */
static struct ofw_compat_data compat_data[] = {
	{"ti,omap3-hsmmc",	1},
	{"ti,omap4-hsmmc",	1},
	{"ti,mmchs",		1},
	{NULL,		 	0},
};

/*
 * The MMCHS hardware has a few control and status registers at the beginning of
 * the device's memory map, followed by the standard sdhci register block.
 * Different SoCs have the register blocks at different offsets from the
 * beginning of the device.  Define some constants to map out the registers we
 * access, and the various per-SoC offsets.  The SDHCI_REG_OFFSET is how far
 * beyond the MMCHS block the SDHCI block is found; it's the same on all SoCs.
 */
#define	OMAP3_MMCHS_REG_OFFSET		0x000
#define	OMAP4_MMCHS_REG_OFFSET		0x100
#define	AM335X_MMCHS_REG_OFFSET		0x100
#define	SDHCI_REG_OFFSET		0x100

#define	MMCHS_SYSCONFIG			0x010
#define	  MMCHS_SYSCONFIG_RESET		  (1 << 1)
#define	MMCHS_SYSSTATUS			0x014
#define	  MMCHS_SYSSTATUS_RESETDONE	  (1 << 0)
#define	MMCHS_CON			0x02C
#define	  MMCHS_CON_DW8			  (1 << 5)
#define	  MMCHS_CON_DVAL_8_4MS		  (3 << 9)
#define	  MMCHS_CON_OD			  (1 << 0)
#define MMCHS_SYSCTL			0x12C
#define   MMCHS_SYSCTL_CLKD_MASK	   0x3FF
#define   MMCHS_SYSCTL_CLKD_SHIFT	   6
#define	MMCHS_SD_CAPA			0x140
#define	  MMCHS_SD_CAPA_VS18		  (1 << 26)
#define	  MMCHS_SD_CAPA_VS30		  (1 << 25)
#define	  MMCHS_SD_CAPA_VS33		  (1 << 24)

/* Forward declarations, CAM-relataed */
// static void ti_sdhci_cam_poll(struct cam_sim *);
// static void ti_sdhci_cam_action(struct cam_sim *, union ccb *);
// static int ti_sdhci_cam_settran_settings(struct ti_sdhci_softc *sc, union ccb *);

static inline uint32_t
ti_mmchs_read_4(struct ti_sdhci_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off + sc->mmchs_reg_off));
}

static inline void
ti_mmchs_write_4(struct ti_sdhci_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off + sc->mmchs_reg_off, val);
}

static inline uint32_t
RD4(struct ti_sdhci_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off + sc->sdhci_reg_off));
}

static inline void
WR4(struct ti_sdhci_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off + sc->sdhci_reg_off, val);
}

static uint8_t
ti_sdhci_read_1(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);

	return ((RD4(sc, off & ~3) >> (off & 3) * 8) & 0xff);
}

static uint16_t
ti_sdhci_read_2(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);
	uint32_t clkdiv, val32;

	/*
	 * The MMCHS hardware has a non-standard interpretation of the sdclock
	 * divisor bits.  It uses the same bit positions as SDHCI 3.0 (15..6)
	 * but doesn't split them into low:high fields.  Instead they're a
	 * single number in the range 0..1023 and the number is exactly the
	 * clock divisor (with 0 and 1 both meaning divide by 1).  The SDHCI
	 * driver code expects a v2.0 or v3.0 divisor.  The shifting and masking
	 * here extracts the MMCHS representation from the hardware word, cleans
	 * those bits out, applies the 2N adjustment, and plugs the result into
	 * the bit positions for the 2.0 or 3.0 divisor in the returned register
	 * value. The ti_sdhci_write_2() routine performs the opposite
	 * transformation when the SDHCI driver writes to the register.
	 */
	if (off == SDHCI_CLOCK_CONTROL) {
		val32 = RD4(sc, SDHCI_CLOCK_CONTROL);
		clkdiv = ((val32 >> MMCHS_SYSCTL_CLKD_SHIFT) &
		    MMCHS_SYSCTL_CLKD_MASK) / 2;
		val32 &= ~(MMCHS_SYSCTL_CLKD_MASK << MMCHS_SYSCTL_CLKD_SHIFT);
		val32 |= (clkdiv & SDHCI_DIVIDER_MASK) << SDHCI_DIVIDER_SHIFT;
		if (slot->version >= SDHCI_SPEC_300)
			val32 |= ((clkdiv >> SDHCI_DIVIDER_MASK_LEN) &
			    SDHCI_DIVIDER_HI_MASK) << SDHCI_DIVIDER_HI_SHIFT;
		return (val32 & 0xffff);
	}

	/*
	 * Standard 32-bit handling of command and transfer mode.
	 */
	if (off == SDHCI_TRANSFER_MODE) {
		return (sc->cmd_and_mode >> 16);
	} else if (off == SDHCI_COMMAND_FLAGS) {
		return (sc->cmd_and_mode & 0x0000ffff);
	}

	return ((RD4(sc, off & ~3) >> (off & 3) * 8) & 0xffff);
}

static uint32_t
ti_sdhci_read_4(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32;

	val32 = RD4(sc, off);

	/*
	 * If we need to disallow highspeed mode due to the OMAP4 erratum, strip
	 * that flag from the returned capabilities.
	 */
	if (off == SDHCI_CAPABILITIES && sc->disable_highspeed)
		val32 &= ~SDHCI_CAN_DO_HISPD;

	/*
	 * Force the card-present state if necessary.
	 */
	if (off == SDHCI_PRESENT_STATE && sc->force_card_present)
		val32 |= SDHCI_CARD_PRESENT;

	return (val32);
}

static void
ti_sdhci_read_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);

	bus_read_multi_4(sc->mem_res, off + sc->sdhci_reg_off, data, count);
}

static void
ti_sdhci_write_1(device_t dev, struct sdhci_slot *slot, bus_size_t off, 
    uint8_t val)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);
	uint32_t val32;

#ifdef MMCCAM
	uint32_t newval32;
	if (off == SDHCI_HOST_CONTROL) {
		val32 = ti_mmchs_read_4(sc, MMCHS_CON);
		newval32  = val32;
		if (val & SDHCI_CTRL_8BITBUS) {
			device_printf(dev, "Custom-enabling 8-bit bus\n");
			newval32 |= MMCHS_CON_DW8;
		} else {
			device_printf(dev, "Custom-disabling 8-bit bus\n");
			newval32 &= ~MMCHS_CON_DW8;
		}
		if (newval32 != val32)
			ti_mmchs_write_4(sc, MMCHS_CON, newval32);
	}
#endif
	val32 = RD4(sc, off & ~3);
	val32 &= ~(0xff << (off & 3) * 8);
	val32 |= (val << (off & 3) * 8);

	WR4(sc, off & ~3, val32);
}

static void
ti_sdhci_write_2(device_t dev, struct sdhci_slot *slot, bus_size_t off, 
    uint16_t val)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);
	uint32_t clkdiv, val32;

	/*
	 * Translate between the hardware and SDHCI 2.0 or 3.0 representations
	 * of the clock divisor.  See the comments in ti_sdhci_read_2() for
	 * details.
	 */
	if (off == SDHCI_CLOCK_CONTROL) {
		clkdiv = (val >> SDHCI_DIVIDER_SHIFT) & SDHCI_DIVIDER_MASK;
		if (slot->version >= SDHCI_SPEC_300)
			clkdiv |= ((val >> SDHCI_DIVIDER_HI_SHIFT) &
			    SDHCI_DIVIDER_HI_MASK) << SDHCI_DIVIDER_MASK_LEN;
		clkdiv *= 2;
		if (clkdiv > MMCHS_SYSCTL_CLKD_MASK)
			clkdiv = MMCHS_SYSCTL_CLKD_MASK;
		val32 = RD4(sc, SDHCI_CLOCK_CONTROL);
		val32 &= 0xffff0000;
		val32 |= val & ~(MMCHS_SYSCTL_CLKD_MASK <<
		    MMCHS_SYSCTL_CLKD_SHIFT);
		val32 |= clkdiv << MMCHS_SYSCTL_CLKD_SHIFT;
		WR4(sc, SDHCI_CLOCK_CONTROL, val32);
		return;
	}

	/*
	 * Standard 32-bit handling of command and transfer mode.
	 */
	if (off == SDHCI_TRANSFER_MODE) {
		sc->cmd_and_mode = (sc->cmd_and_mode & 0xffff0000) |
		    ((uint32_t)val & 0x0000ffff);
		return;
	} else if (off == SDHCI_COMMAND_FLAGS) {
		sc->cmd_and_mode = (sc->cmd_and_mode & 0x0000ffff) |
		    ((uint32_t)val << 16);
		WR4(sc, SDHCI_TRANSFER_MODE, sc->cmd_and_mode);
		return;
	}

	val32 = RD4(sc, off & ~3);
	val32 &= ~(0xffff << (off & 3) * 8);
	val32 |= ((val & 0xffff) << (off & 3) * 8);
	WR4(sc, off & ~3, val32);	
}

static void
ti_sdhci_write_4(device_t dev, struct sdhci_slot *slot, bus_size_t off, 
    uint32_t val)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);

	WR4(sc, off, val);
}

static void
ti_sdhci_write_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);

	bus_write_multi_4(sc->mem_res, off + sc->sdhci_reg_off, data, count);
}

static void
ti_sdhci_intr(void *arg)
{
	struct ti_sdhci_softc *sc = arg;

	sdhci_generic_intr(&sc->slot);
}

static int
ti_sdhci_update_ios(device_t brdev, device_t reqdev)
{
	struct ti_sdhci_softc *sc = device_get_softc(brdev);
	struct sdhci_slot *slot;
	struct mmc_ios *ios;
	uint32_t val32, newval32;

	slot = device_get_ivars(reqdev);
	ios = &slot->host.ios;

	/*
	 * There is an 8-bit-bus bit in the MMCHS control register which, when
	 * set, overrides the 1 vs 4 bit setting in the standard SDHCI
	 * registers.  Set that bit first according to whether an 8-bit bus is
	 * requested, then let the standard driver handle everything else.
	 */
	val32 = ti_mmchs_read_4(sc, MMCHS_CON);
	newval32  = val32;

	if (ios->bus_width == bus_width_8)
		newval32 |= MMCHS_CON_DW8;
	else
		newval32 &= ~MMCHS_CON_DW8;

	if (ios->bus_mode == opendrain)
		newval32 |= MMCHS_CON_OD;
	else /* if (ios->bus_mode == pushpull) */
		newval32 &= ~MMCHS_CON_OD;

	if (newval32 != val32)
		ti_mmchs_write_4(sc, MMCHS_CON, newval32);

	return (sdhci_generic_update_ios(brdev, reqdev));
}

static int
ti_sdhci_get_ro(device_t brdev, device_t reqdev)
{
	struct ti_sdhci_softc *sc = device_get_softc(brdev);

	if (sc->disable_readonly)
		return (0);

	return (sdhci_fdt_gpio_get_readonly(sc->gpio));
}

static bool
ti_sdhci_get_card_present(device_t dev, struct sdhci_slot *slot)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);

	return (sdhci_fdt_gpio_get_present(sc->gpio));
}

static int
ti_sdhci_detach(device_t dev)
{

	/* sdhci_fdt_gpio_teardown(sc->gpio); */

	return (EBUSY);
}

static void
ti_sdhci_hw_init(device_t dev)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);
	uint32_t regval;
	unsigned long timeout;

	/* Enable the controller and interface/functional clocks */
	if (ti_prcm_clk_enable(sc->mmchs_clk_id) != 0) {
		device_printf(dev, "Error: failed to enable MMC clock\n");
		return;
	}

	/* Get the frequency of the source clock */
	if (ti_prcm_clk_get_source_freq(sc->mmchs_clk_id,
	    &sc->baseclk_hz) != 0) {
		device_printf(dev, "Error: failed to get source clock freq\n");
		return;
	}

	/* Issue a softreset to the controller */
	ti_mmchs_write_4(sc, MMCHS_SYSCONFIG, MMCHS_SYSCONFIG_RESET);
	timeout = 1000;
	while (!(ti_mmchs_read_4(sc, MMCHS_SYSSTATUS) &
	    MMCHS_SYSSTATUS_RESETDONE)) {
		if (--timeout == 0) {
			device_printf(dev,
			    "Error: Controller reset operation timed out\n");
			break;
		}
		DELAY(100);
	}

	/*
	 * Reset the command and data state machines and also other aspects of
	 * the controller such as bus clock and power.
	 *
	 * If we read the software reset register too fast after writing it we
	 * can get back a zero that means the reset hasn't started yet rather
	 * than that the reset is complete. Per TI recommendations, work around
	 * it by reading until we see the reset bit asserted, then read until
	 * it's clear. We also set the SDHCI_QUIRK_WAITFOR_RESET_ASSERTED quirk
	 * so that the main sdhci driver uses this same logic in its resets.
	 */
	ti_sdhci_write_1(dev, NULL, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
	timeout = 10000;
	while ((ti_sdhci_read_1(dev, NULL, SDHCI_SOFTWARE_RESET) &
	    SDHCI_RESET_ALL) != SDHCI_RESET_ALL) {
		if (--timeout == 0) {
			break;
		}
		DELAY(1);
	}
	timeout = 10000;
	while ((ti_sdhci_read_1(dev, NULL, SDHCI_SOFTWARE_RESET) &
	    SDHCI_RESET_ALL)) {
		if (--timeout == 0) {
			device_printf(dev,
			    "Error: Software reset operation timed out\n");
			break;
		}
		DELAY(100);
	}

	/*
	 * The attach() routine has examined fdt data and set flags in
	 * slot.host.caps to reflect what voltages we can handle.  Set those
	 * values in the CAPA register.  The manual says that these values can
	 * only be set once, "before initialization" whatever that means, and
	 * that they survive a reset.  So maybe doing this will be a no-op if
	 * u-boot has already initialized the hardware.
	 */
	regval = ti_mmchs_read_4(sc, MMCHS_SD_CAPA);
	if (sc->slot.host.caps & MMC_OCR_LOW_VOLTAGE)
		regval |= MMCHS_SD_CAPA_VS18;
	if (sc->slot.host.caps & (MMC_OCR_290_300 | MMC_OCR_300_310))
		regval |= MMCHS_SD_CAPA_VS30;
	ti_mmchs_write_4(sc, MMCHS_SD_CAPA, regval);

	/* Set initial host configuration (1-bit, std speed, pwr off). */
	ti_sdhci_write_1(dev, NULL, SDHCI_HOST_CONTROL, 0);
	ti_sdhci_write_1(dev, NULL, SDHCI_POWER_CONTROL, 0);

	/* Set the initial controller configuration. */
	ti_mmchs_write_4(sc, MMCHS_CON, MMCHS_CON_DVAL_8_4MS);
}

static int
ti_sdhci_attach(device_t dev)
{
	struct ti_sdhci_softc *sc = device_get_softc(dev);
	int rid, err;
	pcell_t prop;
	phandle_t node;

	sc->dev = dev;

	/*
	 * Get the MMCHS device id from FDT.  If it's not there use the newbus
	 * unit number (which will work as long as the devices are in order and
	 * none are skipped in the fdt).  Note that this is a property we made
	 * up and added in freebsd, it doesn't exist in the published bindings.
	 */
	node = ofw_bus_get_node(dev);
	sc->mmchs_clk_id = ti_hwmods_get_clock(dev);
	if (sc->mmchs_clk_id == INVALID_CLK_IDENT) {
		device_printf(dev, "failed to get clock based on hwmods property\n");
	}

	/*
	 * The hardware can inherently do dual-voltage (1p8v, 3p0v) on the first
	 * device, and only 1p8v on other devices unless an external transceiver
	 * is used.  The only way we could know about a transceiver is fdt data.
	 * Note that we have to do this before calling ti_sdhci_hw_init() so
	 * that it can set the right values in the CAPA register, which can only
	 * be done once and never reset.
	 */
	sc->slot.host.caps |= MMC_OCR_LOW_VOLTAGE;
	if (sc->mmchs_clk_id == MMC1_CLK || OF_hasprop(node, "ti,dual-volt")) {
		sc->slot.host.caps |= MMC_OCR_290_300 | MMC_OCR_300_310;
	}

	/*
	 * Set the offset from the device's memory start to the MMCHS registers.
	 * Also for OMAP4 disable high speed mode due to erratum ID i626.
	 */
	switch (ti_chip()) {
#ifdef SOC_OMAP4
	case CHIP_OMAP_4:
		sc->mmchs_reg_off = OMAP4_MMCHS_REG_OFFSET;
		sc->disable_highspeed = true;
		break;
#endif
#ifdef SOC_TI_AM335X
	case CHIP_AM335X:
		sc->mmchs_reg_off = AM335X_MMCHS_REG_OFFSET;
		break;
#endif
	default:
		panic("Unknown OMAP device\n");
	}

	/*
	 * The standard SDHCI registers are at a fixed offset (the same on all
	 * SoCs) beyond the MMCHS registers.
	 */
	sc->sdhci_reg_off = sc->mmchs_reg_off + SDHCI_REG_OFFSET;

	/* Resource setup. */
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
	    NULL, ti_sdhci_intr, sc, &sc->intr_cookie)) {
		device_printf(dev, "cannot setup interrupt handler\n");
		err = ENXIO;
		goto fail;
	}

	/*
	 * Set up handling of card-detect and write-protect gpio lines.
	 *
	 * If there is no write protect info in the fdt data, fall back to the
	 * historical practice of assuming that the card is writable.  This
	 * works around bad fdt data from the upstream source.  The alternative
	 * would be to trust the sdhci controller's PRESENT_STATE register WP
	 * bit, but it may say write protect is in effect when it's not if the
	 * pinmux setup doesn't route the WP signal into the sdchi block.
	 */
	sc->gpio = sdhci_fdt_gpio_setup(sc->dev, &sc->slot);

	if (!OF_hasprop(node, "wp-gpios") && !OF_hasprop(node, "wp-disable"))
		sc->disable_readonly = true;

	/* Initialise the MMCHS hardware. */
	ti_sdhci_hw_init(dev);

	/*
	 * The capabilities register can only express base clock frequencies in
	 * the range of 0-63MHz for a v2.0 controller.  Since our clock runs
	 * faster than that, the hardware sets the frequency to zero in the
	 * register.  When the register contains zero, the sdhci driver expects
	 * slot.max_clk to already have the right value in it.
	 */
	sc->slot.max_clk = sc->baseclk_hz;

	/*
	 * The MMCHS timeout counter is based on the output sdclock.  Tell the
	 * sdhci driver to recalculate the timeout clock whenever the output
	 * sdclock frequency changes.
	 */
	sc->slot.quirks |= SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK;

	/*
	 * The MMCHS hardware shifts the 136-bit response data (in violation of
	 * the spec), so tell the sdhci driver not to do the same in software.
	 */
	sc->slot.quirks |= SDHCI_QUIRK_DONT_SHIFT_RESPONSE;

	/*
	 * Reset bits are broken, have to wait to see the bits asserted
	 * before waiting to see them de-asserted.
	 */
	sc->slot.quirks |= SDHCI_QUIRK_WAITFOR_RESET_ASSERTED;
	
	/*
	 * The controller waits for busy responses.
	 */
	sc->slot.quirks |= SDHCI_QUIRK_WAIT_WHILE_BUSY;

	/*
	 * DMA is not really broken, I just haven't implemented it yet.
	 */
	sc->slot.quirks |= SDHCI_QUIRK_BROKEN_DMA;

	/*
	 *  Set up the hardware and go.  Note that this sets many of the
	 *  slot.host.* fields, so we have to do this before overriding any of
	 *  those values based on fdt data, below.
	 */
	sdhci_init_slot(dev, &sc->slot, 0);

	/*
	 * The SDHCI controller doesn't realize it, but we can support 8-bit
	 * even though we're not a v3.0 controller.  If there's an fdt bus-width
	 * property, honor it.
	 */
	if (OF_getencprop(node, "bus-width", &prop, sizeof(prop)) > 0) {
		sc->slot.host.caps &= ~(MMC_CAP_4_BIT_DATA | 
		    MMC_CAP_8_BIT_DATA);
		switch (prop) {
		case 8:
			sc->slot.host.caps |= MMC_CAP_8_BIT_DATA;
			/* FALLTHROUGH */
		case 4:
			sc->slot.host.caps |= MMC_CAP_4_BIT_DATA;
			break;
		case 1:
			break;
		default:
			device_printf(dev, "Bad bus-width value %u\n", prop);
			break;
		}
	}

	/*
	 * If the slot is flagged with the non-removable property, set our flag
	 * to always force the SDHCI_CARD_PRESENT bit on.
	 */
	node = ofw_bus_get_node(dev);
	if (OF_hasprop(node, "non-removable"))
		sc->force_card_present = true;

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	sdhci_start_slot(&sc->slot);
	return (0);

fail:
	if (sc->intr_cookie)
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	if (sc->irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (err);
}

static int
ti_sdhci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "TI MMCHS (SDHCI 2.0)");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static device_method_t ti_sdhci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ti_sdhci_probe),
	DEVMETHOD(device_attach,	ti_sdhci_attach),
	DEVMETHOD(device_detach,	ti_sdhci_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sdhci_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	sdhci_generic_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	ti_sdhci_update_ios),
	DEVMETHOD(mmcbr_request,	sdhci_generic_request),
	DEVMETHOD(mmcbr_get_ro,		ti_sdhci_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	sdhci_generic_acquire_host),
	DEVMETHOD(mmcbr_release_host,	sdhci_generic_release_host),

	/* SDHCI registers accessors */
	DEVMETHOD(sdhci_read_1,		ti_sdhci_read_1),
	DEVMETHOD(sdhci_read_2,		ti_sdhci_read_2),
	DEVMETHOD(sdhci_read_4,		ti_sdhci_read_4),
	DEVMETHOD(sdhci_read_multi_4,	ti_sdhci_read_multi_4),
	DEVMETHOD(sdhci_write_1,	ti_sdhci_write_1),
	DEVMETHOD(sdhci_write_2,	ti_sdhci_write_2),
	DEVMETHOD(sdhci_write_4,	ti_sdhci_write_4),
	DEVMETHOD(sdhci_write_multi_4,	ti_sdhci_write_multi_4),
	DEVMETHOD(sdhci_get_card_present, ti_sdhci_get_card_present),

	DEVMETHOD_END
};

static devclass_t ti_sdhci_devclass;

static driver_t ti_sdhci_driver = {
	"sdhci_ti",
	ti_sdhci_methods,
	sizeof(struct ti_sdhci_softc),
};

DRIVER_MODULE(sdhci_ti, simplebus, ti_sdhci_driver, ti_sdhci_devclass, NULL,
    NULL);
SDHCI_DEPEND(sdhci_ti);

#ifndef MMCCAM
MMC_DECLARE_BRIDGE(sdhci_ti);
#endif
