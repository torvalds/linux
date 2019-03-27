/*-
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
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
 * Intel Arria 10 FPGA Manager.
 * Chapter 4, Arria 10 Hard Processor System Technical Reference Manual.
 * Chapter A, FPGA Reconfiguration.
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

#define	FPGAMGR_DCLKCNT			0x8	/* DCLK Count Register */
#define	FPGAMGR_DCLKSTAT		0xC	/* DCLK Status Register */
#define	FPGAMGR_GPO			0x10	/* General-Purpose Output Register */
#define	FPGAMGR_GPI			0x14	/* General-Purpose Input Register */
#define	FPGAMGR_MISCI			0x18	/* Miscellaneous Input Register */
#define	IMGCFG_CTRL_00			0x70
#define	 S2F_CONDONE_OE			(1 << 24)
#define	 S2F_NSTATUS_OE			(1 << 16)
#define	 CTRL_00_NCONFIG		(1 << 8)
#define	 CTRL_00_NENABLE_CONDONE	(1 << 2)
#define	 CTRL_00_NENABLE_NSTATUS	(1 << 1)
#define	 CTRL_00_NENABLE_NCONFIG	(1 << 0)
#define	IMGCFG_CTRL_01			0x74
#define	 CTRL_01_S2F_NCE		(1 << 24)
#define	 CTRL_01_S2F_PR_REQUEST		(1 << 16)
#define	 CTRL_01_S2F_NENABLE_CONFIG	(1 << 0)
#define	IMGCFG_CTRL_02			0x78
#define	 CTRL_02_CDRATIO_S		16
#define	 CTRL_02_CDRATIO_M		(0x3 << CTRL_02_CDRATIO_S)
#define	 CTRL_02_CFGWIDTH_16		(0 << 24)
#define	 CTRL_02_CFGWIDTH_32		(1 << 24)
#define	 CTRL_02_EN_CFG_DATA		(1 << 8)
#define	 CTRL_02_EN_CFG_CTRL		(1 << 0)
#define	IMGCFG_STAT			0x80
#define	 F2S_PR_ERROR			(1 << 11)
#define	 F2S_PR_DONE			(1 << 10)
#define	 F2S_PR_READY			(1 << 9)
#define	 F2S_MSEL_S			16
#define	 F2S_MSEL_M			(0x7 << F2S_MSEL_S)
#define	 MSEL_PASSIVE_FAST		0
#define	 MSEL_PASSIVE_SLOW		1
#define	 F2S_NCONFIG_PIN		(1 << 12)
#define	 F2S_CONDONE_OE			(1 << 7)
#define	 F2S_NSTATUS_PIN		(1 << 4)
#define	 F2S_CONDONE_PIN		(1 << 6)
#define	 F2S_USERMODE			(1 << 2)

struct fpgamgr_a10_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst_data;
	bus_space_handle_t	bsh_data;
	struct cdev		*mgr_cdev;
	device_t		dev;
};

static struct resource_spec fpgamgr_a10_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ -1, 0 }
};

static int
fpga_wait_dclk_pulses(struct fpgamgr_a10_softc *sc, int npulses)
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
		device_printf(sc->dev,
		    "Error: dclkpulses wait timeout\n");
		return (1);
	}

	return (0);
}


static int
fpga_open(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct fpgamgr_a10_softc *sc;
	int tout;
	int msel;
	int reg;

	sc = dev->si_drv1;

	/* Step 1 */
	reg = READ4(sc, IMGCFG_STAT);
	if ((reg & F2S_USERMODE) == 0) {
		device_printf(sc->dev, "Error: invalid mode\n");
		return (ENXIO);
	};

	/* Step 2 */
	reg = READ4(sc, IMGCFG_STAT);
	msel = (reg & F2S_MSEL_M) >> F2S_MSEL_S;
	if ((msel != MSEL_PASSIVE_FAST) && \
	    (msel != MSEL_PASSIVE_SLOW)) {
		device_printf(sc->dev,
		    "Error: invalid msel %d\n", msel);
		return (ENXIO);
	};

	/*
	 * Step 3.
	 * TODO: add support for compressed, encrypted images.
	 */
	reg = READ4(sc, IMGCFG_CTRL_02);
	reg &= ~(CTRL_02_CDRATIO_M);
	WRITE4(sc, IMGCFG_CTRL_02, reg);

	reg = READ4(sc, IMGCFG_CTRL_02);
	reg &= ~CTRL_02_CFGWIDTH_32;
	WRITE4(sc, IMGCFG_CTRL_02, reg);

	/* Step 4. a */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg &= ~CTRL_01_S2F_PR_REQUEST;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	reg = READ4(sc, IMGCFG_CTRL_00);
	reg |= CTRL_00_NCONFIG;
	WRITE4(sc, IMGCFG_CTRL_00, reg);

	/* b */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg &= ~CTRL_01_S2F_NCE;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	/* c */
	reg = READ4(sc, IMGCFG_CTRL_02);
	reg |= CTRL_02_EN_CFG_CTRL;
	WRITE4(sc, IMGCFG_CTRL_02, reg);

	/* d */
	reg = READ4(sc, IMGCFG_CTRL_00);
	reg &= ~S2F_CONDONE_OE;
	reg &= ~S2F_NSTATUS_OE;
	reg |= CTRL_00_NCONFIG;
	reg |= CTRL_00_NENABLE_NSTATUS;
	reg |= CTRL_00_NENABLE_CONDONE;
	reg &= ~CTRL_00_NENABLE_NCONFIG;
	WRITE4(sc, IMGCFG_CTRL_00, reg);

	/* Step 5 */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg &= ~CTRL_01_S2F_NENABLE_CONFIG;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	/* Step 6 */
	fpga_wait_dclk_pulses(sc, 0x100);

	/* Step 7. a */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg |= CTRL_01_S2F_PR_REQUEST;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	/* b, c */
	fpga_wait_dclk_pulses(sc, 0x7ff);

	/* Step 8 */
	tout = 10;
	while (tout--) {
		reg = READ4(sc, IMGCFG_STAT);
		if (reg & F2S_PR_ERROR) {
			device_printf(sc->dev,
			    "Error: PR failed on open.\n");
			return (ENXIO);
		}
		if (reg & F2S_PR_READY) {
			break;
		}
	}
	if (tout == 0) {
		device_printf(sc->dev,
		    "Error: Timeout waiting PR ready bit.\n");
		return (ENXIO);
	}

	return (0);
}

static int
fpga_close(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct fpgamgr_a10_softc *sc;
	int tout;
	int reg;

	sc = dev->si_drv1;

	/* Step 10 */
	tout = 10;
	while (tout--) {
		reg = READ4(sc, IMGCFG_STAT);
		if (reg & F2S_PR_ERROR) {
			device_printf(sc->dev,
			    "Error: PR failed.\n");
			return (ENXIO);
		}
		if (reg & F2S_PR_DONE) {
			break;
		}
	}

	/* Step 11 */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg &= ~CTRL_01_S2F_PR_REQUEST;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	/* Step 12, 13 */
	fpga_wait_dclk_pulses(sc, 0x100);

	/* Step 14 */
	reg = READ4(sc, IMGCFG_CTRL_02);
	reg &= ~CTRL_02_EN_CFG_CTRL;
	WRITE4(sc, IMGCFG_CTRL_02, reg);

	/* Step 15 */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg |= CTRL_01_S2F_NCE;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	/* Step 16 */
	reg = READ4(sc, IMGCFG_CTRL_01);
	reg |= CTRL_01_S2F_NENABLE_CONFIG;
	WRITE4(sc, IMGCFG_CTRL_01, reg);

	/* Step 17 */
	reg = READ4(sc, IMGCFG_STAT);
	if ((reg & F2S_USERMODE) == 0) {
		device_printf(sc->dev,
		    "Error: invalid mode\n");
		return (ENXIO);
	};

	if ((reg & F2S_CONDONE_PIN) == 0) {
		device_printf(sc->dev,
		    "Error: configuration not done\n");
		return (ENXIO);
	};

	if ((reg & F2S_NSTATUS_PIN) == 0) {
		device_printf(sc->dev,
		    "Error: nstatus pin\n");
		return (ENXIO);
	};

	return (0);
}

static int
fpga_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct fpgamgr_a10_softc *sc;
	uint32_t buffer;

	sc = dev->si_drv1;

	/*
	 * Step 9.
	 * Device supports 4-byte writes only.
	 */

	while (uio->uio_resid >= 4) {
		uiomove(&buffer, 4, uio);
		bus_space_write_4(sc->bst_data, sc->bsh_data,
		    0x0, buffer);
	}

	switch (uio->uio_resid) {
	case 3:
		uiomove(&buffer, 3, uio);
		buffer &= 0xffffff;
		bus_space_write_4(sc->bst_data, sc->bsh_data,
		    0x0, buffer);
		break;
	case 2:
		uiomove(&buffer, 2, uio);
		buffer &= 0xffff;
		bus_space_write_4(sc->bst_data, sc->bsh_data,
		    0x0, buffer);
		break;
	case 1:
		uiomove(&buffer, 1, uio);
		buffer &= 0xff;
		bus_space_write_4(sc->bst_data, sc->bsh_data,
		    0x0, buffer);
		break;
	default:
		break;
	};

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
fpgamgr_a10_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "altr,socfpga-a10-fpga-mgr"))
		return (ENXIO);

	device_set_desc(dev, "Arria 10 FPGA Manager");

	return (BUS_PROBE_DEFAULT);
}

static int
fpgamgr_a10_attach(device_t dev)
{
	struct fpgamgr_a10_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, fpgamgr_a10_spec, sc->res)) {
		device_printf(dev, "Could not allocate resources.\n");
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

static device_method_t fpgamgr_a10_methods[] = {
	DEVMETHOD(device_probe,		fpgamgr_a10_probe),
	DEVMETHOD(device_attach,	fpgamgr_a10_attach),
	{ 0, 0 }
};

static driver_t fpgamgr_a10_driver = {
	"fpgamgr_a10",
	fpgamgr_a10_methods,
	sizeof(struct fpgamgr_a10_softc),
};

static devclass_t fpgamgr_a10_devclass;

DRIVER_MODULE(fpgamgr_a10, simplebus, fpgamgr_a10_driver,
    fpgamgr_a10_devclass, 0, 0);
