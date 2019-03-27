/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family Port control and interrupts (PORT)
 * Chapter 6, Vybrid Reference Manual, Rev. 5, 07/2013
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

#include <arm/freescale/vybrid/vf_port.h>
#include <arm/freescale/vybrid/vf_common.h>

/* Pin Control Register */
#define PORT_PCR(n)		(0x1000 * (n >> 5) + 0x4 * (n % 32))
#define	 PCR_IRQC_S	16
#define	 PCR_IRQC_M	0xF
#define	 PCR_DMA_RE	0x1
#define	 PCR_DMA_FE	0x2
#define	 PCR_DMA_EE	0x3
#define	 PCR_INT_LZ	0x8
#define	 PCR_INT_RE	0x9
#define	 PCR_INT_FE	0xA
#define	 PCR_INT_EE	0xB
#define	 PCR_INT_LO	0xC
#define	 PCR_ISF	(1 << 24)
#define	PORT0_ISFR	0xA0	/* Interrupt Status Flag Register */
#define	PORT0_DFER	0xC0	/* Digital Filter Enable Register */
#define	PORT0_DFCR	0xC4	/* Digital Filter Clock Register */
#define	PORT0_DFWR	0xC8	/* Digital Filter Width Register */

struct port_event {
	uint32_t	enabled;
	uint32_t	mux_num;
	uint32_t	mux_src;
	uint32_t	mux_chn;
	void		(*ih) (void *);
	void		*ih_user;
	enum ev_type	pevt;
};

static struct port_event event_map[NGPIO];

struct port_softc {
	struct resource		*res[6];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*gpio_ih[NGPIO];
};

struct port_softc *port_sc;

static struct resource_spec port_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE },
	{ -1, 0 }
};

static int
port_intr(void *arg)
{
	struct port_event *pev;
	struct port_softc *sc;
	int reg;
	int i;

	sc = arg;

	for (i = 0; i < NGPIO; i++) {
		reg = READ4(sc, PORT_PCR(i));
		if (reg & PCR_ISF) {

			/* Clear interrupt */
			WRITE4(sc, PORT_PCR(i), reg);

			/* Handle event */
			pev = &event_map[i];
			if (pev->enabled == 1) {
				if (pev->ih != NULL) {
					pev->ih(pev->ih_user);
				}
			}
		}
	}

	return (FILTER_HANDLED);
}

int
port_setup(int pnum, enum ev_type pevt, void (*ih)(void *), void *ih_user)
{
	struct port_event *pev;
	struct port_softc *sc;
	int reg;
	int val;

	sc = port_sc;

	switch (pevt) {
	case DMA_RISING_EDGE:
		val = PCR_DMA_RE;
		break;
	case DMA_FALLING_EDGE:
		val = PCR_DMA_FE;
		break;
	case DMA_EITHER_EDGE:
		val = PCR_DMA_EE;
		break;
	case INT_LOGIC_ZERO:
		val = PCR_INT_LZ;
		break;
	case INT_RISING_EDGE:
		val = PCR_INT_RE;
		break;
	case INT_FALLING_EDGE:
		val = PCR_INT_FE;
		break;
	case INT_EITHER_EDGE:
		val = PCR_INT_EE;
		break;
	case INT_LOGIC_ONE:
		val = PCR_INT_LO;
		break;
	default:
		return (-1);
	}

	reg = READ4(sc, PORT_PCR(pnum));
	reg &= ~(PCR_IRQC_M << PCR_IRQC_S);
	reg |= (val << PCR_IRQC_S);
	WRITE4(sc, PORT_PCR(pnum), reg);

	pev = &event_map[pnum];
	pev->ih = ih;
	pev->ih_user = ih_user;
	pev->pevt = pevt;
	pev->enabled = 1;

	return (0);
}

static int
port_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-port"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family Port control and interrupts");
	return (BUS_PROBE_DEFAULT);
}

static int
port_attach(device_t dev)
{
	struct port_softc *sc;
	int irq;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, port_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	port_sc = sc;

	for (irq = 0; irq < NPORTS; irq ++) {
		if ((bus_setup_intr(dev, sc->res[1 + irq], INTR_TYPE_MISC,
		    port_intr, NULL, sc, &sc->gpio_ih[irq]))) {
			device_printf(dev,
			    "ERROR: Unable to register interrupt handler\n");
			return (ENXIO);
		}
	}

	return (0);
}

static device_method_t port_methods[] = {
	DEVMETHOD(device_probe,		port_probe),
	DEVMETHOD(device_attach,	port_attach),
	{ 0, 0 }
};

static driver_t port_driver = {
	"port",
	port_methods,
	sizeof(struct port_softc),
};

static devclass_t port_devclass;

DRIVER_MODULE(port, simplebus, port_driver, port_devclass, 0, 0);
