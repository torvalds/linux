/*-
 * Copyright (c) 2014-2015 M. Warner Losh <imp@freebsd.org>
 * Copyright (c) 2015 Luiz Otavio O Souza <loos@freebsd.org>
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
 * The magic-bit-bang sequence used in this code may be based on a linux
 * platform driver in the Allwinner SDK from Allwinner Technology Co., Ltd.
 * www.allwinnertech.com, by Daniel Wang <danielwang@allwinnertech.com>
 * though none of the original code was copied.
 */

#include "opt_bus.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>
#include <dev/ofw/ofw_bus.h> 
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/ahci/ahci.h>
#include <dev/extres/clk/clk.h>

/*
 * Allwinner a1x/a2x/a8x SATA attachment.  This is just the AHCI register
 * set with a few extra implementation-specific registers that need to
 * be accounted for.  There's only one PHY in the system, and it needs
 * to be trained to bring the link up.  In addition, there's some DMA
 * specific things that need to be done as well.  These things are also
 * just about completely undocumented, except in ugly code in the Linux
 * SDK Allwinner releases.
 */

/* BITx -- Unknown bit that needs to be set/cleared at position x */
/* UFx -- Uknown multi-bit field frobbed during init */
#define	AHCI_BISTAFR	0x00A0
#define	AHCI_BISTCR	0x00A4
#define	AHCI_BISTFCTR	0x00A8
#define	AHCI_BISTSR	0x00AC
#define	AHCI_BISTDECR	0x00B0
#define	AHCI_DIAGNR	0x00B4
#define	AHCI_DIAGNR1	0x00B8
#define	AHCI_OOBR	0x00BC
#define	AHCI_PHYCS0R	0x00C0
/* Bits 0..17 are a mystery */
#define	 PHYCS0R_BIT18			(1 << 18)
#define	 PHYCS0R_POWER_ENABLE		(1 << 19)
#define	 PHYCS0R_UF1_MASK		(7 << 20)	/* Unknown Field 1 */
#define	  PHYCS0R_UF1_INIT		(3 << 20)
#define	 PHYCS0R_BIT23			(1 << 23)
#define	 PHYCS0R_UF2_MASK		(7 << 24)	/* Uknown Field 2 */
#define	  PHYCS0R_UF2_INIT		(5 << 24)
/* Bit 27 mystery */
#define	 PHYCS0R_POWER_STATUS_MASK	(7 << 28)
#define	  PHYCS0R_PS_GOOD		(2 << 28)
/* Bit 31 mystery */
#define	AHCI_PHYCS1R	0x00C4
/* Bits 0..5 are a mystery */
#define	 PHYCS1R_UF1_MASK		(3 << 6)
#define	  PHYCS1R_UF1_INIT		(2 << 6)
#define	 PHYCS1R_UF2_MASK		(0x1f << 8)
#define	  PHYCS1R_UF2_INIT		(6 << 8)
/* Bits 13..14 are a mystery */
#define	 PHYCS1R_BIT15			(1 << 15)
#define	 PHYCS1R_UF3_MASK		(3 << 16)
#define	  PHYCS1R_UF3_INIT		(2 << 16)
/* Bit 18 mystery */
#define	 PHYCS1R_HIGHZ			(1 << 19)
/* Bits 20..27 mystery */
#define	 PHYCS1R_BIT28			(1 << 28)
/* Bits 29..31 mystery */
#define	AHCI_PHYCS2R	0x00C8
/* bits 0..4 mystery */
#define	 PHYCS2R_UF1_MASK		(0x1f << 5)
#define	  PHYCS2R_UF1_INIT		(0x19 << 5)
/* Bits 10..23 mystery */
#define	 PHYCS2R_CALIBRATE		(1 << 24)
/* Bits 25..31 mystery */
#define	AHCI_TIMER1MS	0x00E0
#define	AHCI_GPARAM1R	0x00E8
#define	AHCI_GPARAM2R	0x00EC
#define	AHCI_PPARAMR	0x00F0
#define	AHCI_TESTR	0x00F4
#define	AHCI_VERSIONR	0x00F8
#define	AHCI_IDR	0x00FC
#define	AHCI_RWCR	0x00FC

#define	AHCI_P0DMACR	0x0070
#define	AHCI_P0PHYCR	0x0078
#define	AHCI_P0PHYSR	0x007C

#define	PLL_FREQ	100000000

static void inline
ahci_set(struct resource *m, bus_size_t off, uint32_t set)
{
	uint32_t val = ATA_INL(m, off);

	val |= set;
	ATA_OUTL(m, off, val);
}

static void inline
ahci_clr(struct resource *m, bus_size_t off, uint32_t clr)
{
	uint32_t val = ATA_INL(m, off);

	val &= ~clr;
	ATA_OUTL(m, off, val);
}

static void inline
ahci_mask_set(struct resource *m, bus_size_t off, uint32_t mask, uint32_t set)
{
	uint32_t val = ATA_INL(m, off);

	val &= mask;
	val |= set;
	ATA_OUTL(m, off, val);
}

/*
 * Should this be phy_reset or phy_init
 */
#define	PHY_RESET_TIMEOUT	1000
static void
ahci_a10_phy_reset(device_t dev)
{
	uint32_t to, val;
	struct ahci_controller *ctlr = device_get_softc(dev);

	/*
	 * Here starts the magic -- most of the comments are based
	 * on guesswork, names of routines and printf error
	 * messages.  The code works, but it will do that even if the
	 * comments are 100% BS.
	 */

	/*
	 * Lock out other access while we initialize.  Or at least that
	 * seems to be the case based on Linux SDK #defines.  Maybe this
	 * put things into reset?
	 */
	ATA_OUTL(ctlr->r_mem, AHCI_RWCR, 0);
	DELAY(100);

	/*
	 * Set bit 19 in PHYCS1R.  Guessing this disables driving the PHY
	 * port for a bit while we reset things.
	 */
	ahci_set(ctlr->r_mem, AHCI_PHYCS1R, PHYCS1R_HIGHZ);

	/*
	 * Frob PHYCS0R...
	 */
	ahci_mask_set(ctlr->r_mem, AHCI_PHYCS0R,
	    ~PHYCS0R_UF2_MASK,
	    PHYCS0R_UF2_INIT | PHYCS0R_BIT23 | PHYCS0R_BIT18);

	/*
	 * Set three fields in PHYCS1R
	 */
	ahci_mask_set(ctlr->r_mem, AHCI_PHYCS1R,
	    ~(PHYCS1R_UF1_MASK | PHYCS1R_UF2_MASK | PHYCS1R_UF3_MASK),
	    PHYCS1R_UF1_INIT | PHYCS1R_UF2_INIT | PHYCS1R_UF3_INIT);

	/*
	 * Two more mystery bits in PHYCS1R. -- can these be combined above?
	 */
	ahci_set(ctlr->r_mem, AHCI_PHYCS1R, PHYCS1R_BIT15 | PHYCS1R_BIT28);

	/*
	 * Now clear that first mysery bit.  Perhaps this starts
	 * driving the PHY again so we can power it up and start
	 * talking to the SATA drive, if any below.
	 */
	ahci_clr(ctlr->r_mem, AHCI_PHYCS1R, PHYCS1R_HIGHZ);

	/*
	 * Frob PHYCS0R again...
	 */
	ahci_mask_set(ctlr->r_mem, AHCI_PHYCS0R,
	    ~PHYCS0R_UF1_MASK, PHYCS0R_UF1_INIT);

	/*
	 * Frob PHYCS2R, because 25 means something?
	 */
	ahci_mask_set(ctlr->r_mem, AHCI_PHYCS2R, ~PHYCS2R_UF1_MASK,
	    PHYCS2R_UF1_INIT);

	DELAY(100);		/* WAG */

	/*
	 * Turn on the power to the PHY and wait for it to report back
	 * good?
	 */
	ahci_set(ctlr->r_mem, AHCI_PHYCS0R, PHYCS0R_POWER_ENABLE);
	for (to = PHY_RESET_TIMEOUT; to > 0; to--) {
		val = ATA_INL(ctlr->r_mem, AHCI_PHYCS0R);
		if ((val & PHYCS0R_POWER_STATUS_MASK) == PHYCS0R_PS_GOOD)
			break;
		DELAY(10);
	}
	if (to == 0 && bootverbose)
		device_printf(dev, "PHY Power Failed PHYCS0R = %#x\n", val);

	/*
	 * Calibrate the clocks between the device and the host.  This appears
	 * to be an automated process that clears the bit when it is done.
	 */
	ahci_set(ctlr->r_mem, AHCI_PHYCS2R, PHYCS2R_CALIBRATE);
	for (to = PHY_RESET_TIMEOUT; to > 0; to--) {
		val = ATA_INL(ctlr->r_mem, AHCI_PHYCS2R);
		if ((val & PHYCS2R_CALIBRATE) == 0)
			break;
		DELAY(10);
	}
	if (to == 0 && bootverbose)
		device_printf(dev, "PHY Cal Failed PHYCS2R %#x\n", val);

	/*
	 * OK, let things settle down a bit.
	 */
	DELAY(1000);

	/*
	 * Go back into normal mode now that we've calibrated the PHY.
	 */
	ATA_OUTL(ctlr->r_mem, AHCI_RWCR, 7);
}

static void
ahci_a10_ch_start(struct ahci_channel *ch)
{
	uint32_t reg;

	/*
	 * Magical values from Allwinner SDK, setup the DMA before start
	 * operations on this channel.
	 */
	reg = ATA_INL(ch->r_mem, AHCI_P0DMACR);
	reg &= ~0xff00;
	reg |= 0x4400;
	ATA_OUTL(ch->r_mem, AHCI_P0DMACR, reg);
}

static int
ahci_a10_ctlr_reset(device_t dev)
{

	ahci_a10_phy_reset(dev);

	return (ahci_ctlr_reset(dev));
}

static int
ahci_a10_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "allwinner,sun4i-a10-ahci"))
		return (ENXIO);
	device_set_desc(dev, "Allwinner Integrated AHCI controller");

	return (BUS_PROBE_DEFAULT);
}

static int
ahci_a10_attach(device_t dev)
{
	int error;
	struct ahci_controller *ctlr;
	clk_t clk_pll, clk_gate;

	ctlr = device_get_softc(dev);
	clk_pll = clk_gate = NULL;

	ctlr->quirks = AHCI_Q_NOPMP;
	ctlr->vendorid = 0;
	ctlr->deviceid = 0;
	ctlr->subvendorid = 0;
	ctlr->subdeviceid = 0;
	ctlr->r_rid = 0;
	if (!(ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_rid, RF_ACTIVE)))
		return (ENXIO);

	/* Enable clocks */
	error = clk_get_by_ofw_index(dev, 0, 0, &clk_gate);
	if (error != 0) {
		device_printf(dev, "Cannot get gate clock\n");
		goto fail;
	}
	error = clk_get_by_ofw_index(dev, 0, 1, &clk_pll);
	if (error != 0) {
		device_printf(dev, "Cannot get PLL clock\n");
		goto fail;
	}
	error = clk_set_freq(clk_pll, PLL_FREQ, CLK_SET_ROUND_DOWN);
	if (error != 0) {
		device_printf(dev, "Cannot set PLL frequency\n");
		goto fail;
	}
	error = clk_enable(clk_pll);
	if (error != 0) {
		device_printf(dev, "Cannot enable PLL\n");
		goto fail;
	}
	error = clk_enable(clk_gate);
	if (error != 0) {
		device_printf(dev, "Cannot enable clk gate\n");
		goto fail;
	}
	
	/* Reset controller */
	if ((error = ahci_a10_ctlr_reset(dev)) != 0)
		goto fail;

	/*
	 * No MSI registers on this platform.
	 */
	ctlr->msi = 0;
	ctlr->numirqs = 1;

	/* Channel start callback(). */
	ctlr->ch_start = ahci_a10_ch_start;

	/*
	 * Note: ahci_attach will release ctlr->r_mem on errors automatically
	 */
	return (ahci_attach(dev));

fail:
	if (clk_gate != NULL)
		clk_release(clk_gate);
	if (clk_pll != NULL)
		clk_release(clk_pll);
	bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
	return (error);
}

static int
ahci_a10_detach(device_t dev)
{

	return (ahci_detach(dev));
}

static device_method_t ahci_ata_methods[] = {
	DEVMETHOD(device_probe,     ahci_a10_probe),
	DEVMETHOD(device_attach,    ahci_a10_attach),
	DEVMETHOD(device_detach,    ahci_a10_detach),
	DEVMETHOD(bus_print_child,  ahci_print_child),
	DEVMETHOD(bus_alloc_resource,       ahci_alloc_resource),
	DEVMETHOD(bus_release_resource,     ahci_release_resource),
	DEVMETHOD(bus_setup_intr,   ahci_setup_intr),
	DEVMETHOD(bus_teardown_intr,ahci_teardown_intr),
	DEVMETHOD(bus_child_location_str, ahci_child_location_str),
	DEVMETHOD_END
};

static driver_t ahci_ata_driver = {
        "ahci",
        ahci_ata_methods,
        sizeof(struct ahci_controller)
};

DRIVER_MODULE(a10_ahci, simplebus, ahci_ata_driver, ahci_devclass, 0, 0);
