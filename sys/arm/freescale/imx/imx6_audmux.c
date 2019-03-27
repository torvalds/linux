/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
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
 * i.MX6 Digital Audio Multiplexer (AUDMUX)
 * Chapter 16, i.MX 6Dual/6Quad Applications Processor Reference Manual,
 * Rev. 1, 04/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/endian.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#define	READ4(_sc, _reg)	\
	bus_space_read_4(_sc->bst, _sc->bsh, _reg)
#define	WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst, _sc->bsh, _reg, _val)

#define	AUDMUX_PTCR(n)	(0x8 * (n - 1))	/* Port Timing Control Register */
#define	 PTCR_TFS_DIR	(1 << 31)	/* Transmit Frame Sync Direction Control */
#define	 PTCR_TFSEL_S	27		/* Transmit Frame Sync Select */
#define	 PTCR_TFSEL_M	0xf
#define	 PTCR_TCLKDIR	(1 << 26)	/* Transmit Clock Direction Control */
#define	 PTCR_TCSEL_S	22		/* Transmit Clock Select. */
#define	 PTCR_TCSEL_M	0xf
#define	 PTCR_RFS_DIR	(1 << 21)	/* Receive Frame Sync Direction Control */
#define	 PTCR_SYN	(1 << 11)
#define	AUDMUX_PDCR(n)	(0x8 * (n - 1) + 0x4)	/* Port Data Control Reg */
#define	 PDCR_RXDSEL_S		13	/* Receive Data Select */
#define	 PDCR_RXDSEL_M		0x3
#define	 PDCR_RXDSEL_PORT(n)	(n - 1)

struct audmux_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*ih;
};

static struct resource_spec audmux_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
audmux_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,imx6q-audmux"))
		return (ENXIO);

	device_set_desc(dev, "i.MX6 Digital Audio Multiplexer");
	return (BUS_PROBE_DEFAULT);
}

static int
audmux_configure(struct audmux_softc *sc,
	int ssi_port, int audmux_port)
{
	uint32_t reg;

	/* Direction: output */
	reg = (PTCR_TFS_DIR | PTCR_TCLKDIR | PTCR_SYN);
	WRITE4(sc, AUDMUX_PTCR(audmux_port), reg);

	/* Select source */
	reg = (PDCR_RXDSEL_PORT(ssi_port) << PDCR_RXDSEL_S);
	WRITE4(sc, AUDMUX_PDCR(audmux_port), reg);

	return (0);
}

static int
audmux_attach(device_t dev)
{
	struct audmux_softc *sc;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, audmux_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/*
	 * Direct SSI1 output to AUDMUX5 pins.
	 * TODO: dehardcore this.
	 */
	audmux_configure(sc, 1, 5);

	return (0);
};

static device_method_t audmux_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		audmux_probe),
	DEVMETHOD(device_attach,	audmux_attach),
	{ 0, 0 }
};

static driver_t audmux_driver = {
	"audmux",
	audmux_methods,
	sizeof(struct audmux_softc),
};

static devclass_t audmux_devclass;

DRIVER_MODULE(audmux, simplebus, audmux_driver, audmux_devclass, 0, 0);
