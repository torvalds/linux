/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * Altera FPGA Manager.
 * Chapter 4, Cyclone V Device Handbook (CV-5V2 2014.07.22)
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
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/altera/socfpga/socfpga_common.h>

/* FPGA Manager Module Registers */
#define	FPGAMGR_STAT		0x0	/* Status Register */
#define	 STAT_MSEL_MASK		0x1f
#define	 STAT_MSEL_SHIFT	3
#define	 STAT_MODE_SHIFT	0
#define	 STAT_MODE_MASK		0x7
#define	FPGAMGR_CTRL		0x4	/* Control Register */
#define	 CTRL_AXICFGEN		(1 << 8)
#define	 CTRL_CDRATIO_MASK	0x3
#define	 CTRL_CDRATIO_SHIFT	6
#define	 CTRL_CFGWDTH_MASK	1
#define	 CTRL_CFGWDTH_SHIFT	9
#define	 CTRL_NCONFIGPULL	(1 << 2)
#define	 CTRL_NCE		(1 << 1)
#define	 CTRL_EN		(1 << 0)
#define	FPGAMGR_DCLKCNT		0x8	/* DCLK Count Register */
#define	FPGAMGR_DCLKSTAT	0xC	/* DCLK Status Register */
#define	FPGAMGR_GPO		0x10	/* General-Purpose Output Register */
#define	FPGAMGR_GPI		0x14	/* General-Purpose Input Register */
#define	FPGAMGR_MISCI		0x18	/* Miscellaneous Input Register */

/* Configuration Monitor (MON) Registers */
#define	GPIO_INTEN		0x830	/* Interrupt Enable Register */
#define	GPIO_INTMASK		0x834	/* Interrupt Mask Register */
#define	GPIO_INTTYPE_LEVEL	0x838	/* Interrupt Level Register */
#define	GPIO_INT_POLARITY	0x83C	/* Interrupt Polarity Register */
#define	GPIO_INTSTATUS		0x840	/* Interrupt Status Register */
#define	GPIO_RAW_INTSTATUS	0x844	/* Raw Interrupt Status Register */
#define	GPIO_PORTA_EOI		0x84C	/* Clear Interrupt Register */
#define	 PORTA_EOI_NS		(1 << 0)
#define	GPIO_EXT_PORTA		0x850	/* External Port A Register */
#define	 EXT_PORTA_CDP		(1 << 10) /* Configuration done */
#define	GPIO_LS_SYNC		0x860	/* Synchronization Level Register */
#define	GPIO_VER_ID_CODE	0x86C	/* GPIO Version Register */
#define	GPIO_CONFIG_REG2	0x870	/* Configuration Register 2 */
#define	GPIO_CONFIG_REG1	0x874	/* Configuration Register 1 */

#define	MSEL_PP16_FAST_NOAES_NODC	0x0
#define	MSEL_PP16_FAST_AES_NODC		0x1
#define	MSEL_PP16_FAST_AESOPT_DC	0x2
#define	MSEL_PP16_SLOW_NOAES_NODC	0x4
#define	MSEL_PP16_SLOW_AES_NODC		0x5
#define	MSEL_PP16_SLOW_AESOPT_DC	0x6
#define	MSEL_PP32_FAST_NOAES_NODC	0x8
#define	MSEL_PP32_FAST_AES_NODC		0x9
#define	MSEL_PP32_FAST_AESOPT_DC	0xa
#define	MSEL_PP32_SLOW_NOAES_NODC	0xc
#define	MSEL_PP32_SLOW_AES_NODC		0xd
#define	MSEL_PP32_SLOW_AESOPT_DC	0xe

#define	CFGWDTH_16	0
#define	CFGWDTH_32	1

#define	CDRATIO_1	0
#define	CDRATIO_2	1
#define	CDRATIO_4	2
#define	CDRATIO_8	3

#define	FPGAMGR_MODE_POWEROFF	0x0
#define	FPGAMGR_MODE_RESET	0x1
#define	FPGAMGR_MODE_CONFIG	0x2
#define	FPGAMGR_MODE_INIT	0x3
#define	FPGAMGR_MODE_USER	0x4

struct cfgmgr_mode {
	int msel;
	int cfgwdth;
	int cdratio;
};

static struct cfgmgr_mode cfgmgr_modes[] = {
	{ MSEL_PP16_FAST_NOAES_NODC, CFGWDTH_16, CDRATIO_1 },
	{ MSEL_PP16_FAST_AES_NODC,   CFGWDTH_16, CDRATIO_2 },
	{ MSEL_PP16_FAST_AESOPT_DC,  CFGWDTH_16, CDRATIO_4 },
	{ MSEL_PP16_SLOW_NOAES_NODC, CFGWDTH_16, CDRATIO_1 },
	{ MSEL_PP16_SLOW_AES_NODC,   CFGWDTH_16, CDRATIO_2 },
	{ MSEL_PP16_SLOW_AESOPT_DC,  CFGWDTH_16, CDRATIO_4 },
	{ MSEL_PP32_FAST_NOAES_NODC, CFGWDTH_32, CDRATIO_1 },
	{ MSEL_PP32_FAST_AES_NODC,   CFGWDTH_32, CDRATIO_4 },
	{ MSEL_PP32_FAST_AESOPT_DC,  CFGWDTH_32, CDRATIO_8 },
	{ MSEL_PP32_SLOW_NOAES_NODC, CFGWDTH_32, CDRATIO_1 },
	{ MSEL_PP32_SLOW_AES_NODC,   CFGWDTH_32, CDRATIO_4 },
	{ MSEL_PP32_SLOW_AESOPT_DC,  CFGWDTH_32, CDRATIO_8 },
	{ -1, -1, -1 },
};

struct fpgamgr_softc {
	struct resource		*res[3];
	bus_space_tag_t		bst_data;
	bus_space_handle_t	bsh_data;
	struct cdev		*mgr_cdev;
	device_t		dev;
};

static struct resource_spec fpgamgr_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
fpgamgr_state_get(struct fpgamgr_softc *sc)
{
	int reg;

	reg = READ4(sc, FPGAMGR_STAT);
	reg >>= STAT_MODE_SHIFT;
	reg &= STAT_MODE_MASK;

	return reg;
}

static int
fpgamgr_state_wait(struct fpgamgr_softc *sc, int state)
{
	int tout;

	tout = 1000;
	while (tout > 0) {
		if (fpgamgr_state_get(sc) == state)
			break;
		tout--;
		DELAY(10);
	}
	if (tout == 0) {
		return (1);
	}

	return (0);
}

static int
fpga_open(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct fpgamgr_softc *sc;
	struct cfgmgr_mode *mode;
	int msel;
	int reg;
	int i;

	sc = dev->si_drv1;

	msel = READ4(sc, FPGAMGR_STAT);
	msel >>= STAT_MSEL_SHIFT;
	msel &= STAT_MSEL_MASK;

	mode = NULL;
	for (i = 0; cfgmgr_modes[i].msel != -1; i++) {
		if (msel == cfgmgr_modes[i].msel) {
			mode = &cfgmgr_modes[i];
			break;
		}
	}
	if (mode == NULL) {
		device_printf(sc->dev, "Can't configure: unknown mode\n");
		return (ENXIO);
	}

	reg = READ4(sc, FPGAMGR_CTRL);
	reg &= ~(CTRL_CDRATIO_MASK << CTRL_CDRATIO_SHIFT);
	reg |= (mode->cdratio << CTRL_CDRATIO_SHIFT);
	reg &= ~(CTRL_CFGWDTH_MASK << CTRL_CFGWDTH_SHIFT);
	reg |= (mode->cfgwdth << CTRL_CFGWDTH_SHIFT);
	reg &= ~(CTRL_NCE);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	/* Enable configuration */
	reg = READ4(sc, FPGAMGR_CTRL);
	reg |= (CTRL_EN);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	/* Reset FPGA */
	reg = READ4(sc, FPGAMGR_CTRL);
	reg |= (CTRL_NCONFIGPULL);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	/* Wait reset state */
	if (fpgamgr_state_wait(sc, FPGAMGR_MODE_RESET)) {
		device_printf(sc->dev, "Can't get RESET state\n");
		return (ENXIO);
	}

	/* Release from reset */
	reg = READ4(sc, FPGAMGR_CTRL);
	reg &= ~(CTRL_NCONFIGPULL);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	if (fpgamgr_state_wait(sc, FPGAMGR_MODE_CONFIG)) {
		device_printf(sc->dev, "Can't get CONFIG state\n");
		return (ENXIO);
	}

	/* Clear nSTATUS edge interrupt */
	WRITE4(sc, GPIO_PORTA_EOI, PORTA_EOI_NS);

	/* Enter configuration state */
	reg = READ4(sc, FPGAMGR_CTRL);
	reg |= (CTRL_AXICFGEN);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	return (0);
}

static int
fpga_wait_dclk_pulses(struct fpgamgr_softc *sc, int npulses)
{
	int tout;

	/* Clear done bit, if any */
	if (READ4(sc, FPGAMGR_DCLKSTAT) != 0)
		WRITE4(sc, FPGAMGR_DCLKSTAT, 0x1);

	/* Request DCLK pulses */
	WRITE4(sc, FPGAMGR_DCLKCNT, npulses);

	/* Wait finish */
	tout = 1000;
	while (tout > 0) {
		if (READ4(sc, FPGAMGR_DCLKSTAT) == 1) {
			WRITE4(sc, FPGAMGR_DCLKSTAT, 0x1);
			break;
		}
		tout--;
		DELAY(10);
	}
	if (tout == 0) {
		return (1);
	}

	return (0);
}

static int
fpga_close(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct fpgamgr_softc *sc;
	int reg;

	sc = dev->si_drv1;

	reg = READ4(sc, GPIO_EXT_PORTA);
	if ((reg & EXT_PORTA_CDP) == 0) {
		device_printf(sc->dev, "Err: configuration failed\n");
		return (ENXIO);
	}

	/* Exit configuration state */
	reg = READ4(sc, FPGAMGR_CTRL);
	reg &= ~(CTRL_AXICFGEN);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	/* Wait dclk pulses */
	if (fpga_wait_dclk_pulses(sc, 4)) {
		device_printf(sc->dev, "Can't proceed 4 dclk pulses\n");
		return (ENXIO);
	}

	if (fpgamgr_state_wait(sc, FPGAMGR_MODE_USER)) {
		device_printf(sc->dev, "Can't get USER mode\n");
		return (ENXIO);
	}

	/* Disable configuration */
	reg = READ4(sc, FPGAMGR_CTRL);
	reg &= ~(CTRL_EN);
	WRITE4(sc, FPGAMGR_CTRL, reg);

	return (0);
}

static int
fpga_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct fpgamgr_softc *sc;
	int buffer;

	sc = dev->si_drv1;

	/*
	 * Device supports 4-byte copy only.
	 * TODO: add padding for <4 bytes.
	 */

	while (uio->uio_resid > 0) {
		uiomove(&buffer, 4, uio);
		bus_space_write_4(sc->bst_data, sc->bsh_data,
		    0x0, buffer);
	}

	return (0);
}

static int
fpga_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{

	return (0);
}

static struct cdevsw fpga_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	fpga_open,
	.d_close =	fpga_close,
	.d_write =	fpga_write,
	.d_ioctl =	fpga_ioctl,
	.d_name =	"FPGA Manager",
};

static int
fpgamgr_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "altr,socfpga-fpga-mgr"))
		return (ENXIO);

	device_set_desc(dev, "FPGA Manager");
	return (BUS_PROBE_DEFAULT);
}

static int
fpgamgr_attach(device_t dev)
{
	struct fpgamgr_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, fpgamgr_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst_data = rman_get_bustag(sc->res[1]);
	sc->bsh_data = rman_get_bushandle(sc->res[1]);

	sc->mgr_cdev = make_dev(&fpga_cdevsw, 0, UID_ROOT, GID_WHEEL,
	    0600, "fpga%d", device_get_unit(sc->dev));

	if (sc->mgr_cdev == NULL) {
		device_printf(dev, "Failed to create character device.\n");
		return (ENXIO);
	}

	sc->mgr_cdev->si_drv1 = sc;

	return (0);
}

static device_method_t fpgamgr_methods[] = {
	DEVMETHOD(device_probe,		fpgamgr_probe),
	DEVMETHOD(device_attach,	fpgamgr_attach),
	{ 0, 0 }
};

static driver_t fpgamgr_driver = {
	"fpgamgr",
	fpgamgr_methods,
	sizeof(struct fpgamgr_softc),
};

static devclass_t fpgamgr_devclass;

DRIVER_MODULE(fpgamgr, simplebus, fpgamgr_driver, fpgamgr_devclass, 0, 0);
