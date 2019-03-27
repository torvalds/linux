/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family System Reset Controller (SRC)
 * Chapter 18, Vybrid Reference Manual, Rev. 5, 07/2013
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
#include <sys/watchdog.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_src.h>
#include <arm/freescale/vybrid/vf_common.h>

#define	SRC_SCR		0x00	/* SRC Control Register */
#define	SRC_SBMR1	0x04	/* SRC Boot Mode Register 1 */
#define	SRC_SRSR	0x08	/* SRC Status Register */
#define	SRC_SECR	0x0C	/* SRC_SECR */
#define	SRC_SICR	0x14	/* SRC Reset Interrupt Configuration Register */
#define	SRC_SIMR	0x18	/* SRC Interrupt Masking Register */
#define	SRC_SBMR2	0x1C	/* SRC Boot Mode Register 2 */
#define	SRC_GPR0	0x20	/* General Purpose Register */
#define	SRC_GPR1	0x24	/* General Purpose Register */
#define	SRC_GPR2	0x28	/* General Purpose Register */
#define	SRC_GPR3	0x2C	/* General Purpose Register */
#define	SRC_GPR4	0x30	/* General Purpose Register */
#define	SRC_MISC0	0x4C	/* MISC0 */
#define	SRC_MISC1	0x50	/* MISC1 */
#define	SRC_MISC2	0x54	/* MISC2 */
#define	SRC_MISC3	0x58	/* MISC3 */

struct src_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

struct src_softc *src_sc;

static struct resource_spec src_spec[] = {
	{ SYS_RES_MEMORY,       0,      RF_ACTIVE },
	{ -1, 0 }
};

int
src_swreset(void)
{

	if (src_sc == NULL)
		return (1);

	WRITE4(src_sc, SRC_SCR, SW_RST);

	return (0);
}

static int
src_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-src"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family System Reset Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
src_attach(device_t dev)
{
	struct src_softc *sc;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, src_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	src_sc = sc;

	return (0);
}

static device_method_t src_methods[] = {
	DEVMETHOD(device_probe,		src_probe),
	DEVMETHOD(device_attach,	src_attach),
	{ 0, 0 }
};

static driver_t src_driver = {
	"src",
	src_methods,
	sizeof(struct src_softc),
};

static devclass_t src_devclass;

DRIVER_MODULE(src, simplebus, src_driver, src_devclass, 0, 0);
