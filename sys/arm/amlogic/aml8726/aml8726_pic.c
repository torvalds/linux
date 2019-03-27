/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726 PIC driver.
 *
 * The current implementation doesn't include support for FIQ.
 *
 * There is a set of four interrupt controllers per cpu located in adjacent
 * memory addresses (the set for cpu 1 starts right after the set for cpu 0)
 * ... this allows for interrupt handling to be spread across the cpus.
 *
 * The multicore chips also have a GIC ... typically they run SMP kernels
 * which include the GIC driver in which case this driver is simply used
 * to disable the PIC.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

struct aml8726_pic_softc {
	device_t		dev;
	struct resource *	res[1];
};

static struct resource_spec aml8726_pic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

/*
 * devclass_get_device / device_get_softc could be used
 * to dynamically locate this, however the pic is a
 * required device which can't be unloaded so there's
 * no need for the overhead.
 */
static struct aml8726_pic_softc *aml8726_pic_sc = NULL;

#define	AML_PIC_NCNTRLS		4
#define	AML_PIC_IRQS_PER_CNTRL	32

#define	AML_PIC_NIRQS		(AML_PIC_NCNTRLS * AML_PIC_IRQS_PER_CNTRL)

#define	AML_PIC_0_STAT_REG	0
#define	AML_PIC_0_STAT_CLR_REG	4
#define	AML_PIC_0_MASK_REG	8
#define	AML_PIC_0_FIRQ_SEL	12

#define	AML_PIC_1_STAT_REG	16
#define	AML_PIC_1_STAT_CLR_REG	20
#define	AML_PIC_1_MASK_REG	24
#define	AML_PIC_1_FIRQ_SEL	28

#define	AML_PIC_2_STAT_REG	32
#define	AML_PIC_2_STAT_CLR_REG	36
#define	AML_PIC_2_MASK_REG	40
#define	AML_PIC_2_FIRQ_SEL	44

#define	AML_PIC_3_STAT_REG	48
#define	AML_PIC_3_STAT_CLR_REG	52
#define	AML_PIC_3_MASK_REG	56
#define	AML_PIC_3_FIRQ_SEL	60

#define	AML_PIC_CTRL(x)		((x) >> 5)
#define	AML_PIC_BIT(x)		(1 << ((x) & 0x1f))

#define	AML_PIC_STAT_REG(x)	(AML_PIC_0_STAT_REG + AML_PIC_CTRL(x) * 16)
#define	AML_PIC_STAT_CLR_REG(x)	(AML_PIC_0_STAT_CLR_REG + AML_PIC_CTRL(x) * 16)
#define	AML_PIC_MASK_REG(x)	(AML_PIC_0_MASK_REG + AML_PIC_CTRL(x) * 16)
#define	AML_PIC_FIRQ_SEL(x)	(AML_PIC_0_FIRQ_REG + AML_PIC_CTRL(x) * 16)

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

static void
aml8726_pic_eoi(void *arg)
{
	uintptr_t nb = (uintptr_t) arg;

	if (nb >= AML_PIC_NIRQS)
		return;

	arm_irq_memory_barrier(nb);

	CSR_WRITE_4(aml8726_pic_sc, AML_PIC_STAT_CLR_REG(nb), AML_PIC_BIT(nb));

	CSR_BARRIER(aml8726_pic_sc, AML_PIC_STAT_CLR_REG(nb));
}

static int
aml8726_pic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-pic"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 PIC");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_pic_attach(device_t dev)
{
	struct aml8726_pic_softc *sc = device_get_softc(dev);
	int i;

	/* There should be exactly one instance. */
	if (aml8726_pic_sc != NULL)
		return (ENXIO);

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_pic_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	/*
	 * Disable, clear, and set the interrupts to normal mode.
	 */
	for (i = 0; i < AML_PIC_NCNTRLS; i++) {
		CSR_WRITE_4(sc, AML_PIC_0_MASK_REG + i * 16, 0);
		CSR_WRITE_4(sc, AML_PIC_0_STAT_CLR_REG + i * 16, ~0u);
		CSR_WRITE_4(sc, AML_PIC_0_FIRQ_SEL + i * 16, 0);
	}

#ifndef DEV_GIC
	arm_post_filter = aml8726_pic_eoi;
#else
	device_printf(dev, "disabled in favor of gic\n");
#endif

	aml8726_pic_sc = sc;

	return (0);
}

static int
aml8726_pic_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t aml8726_pic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_pic_probe),
	DEVMETHOD(device_attach,	aml8726_pic_attach),
	DEVMETHOD(device_detach,	aml8726_pic_detach),

	DEVMETHOD_END
};

static driver_t aml8726_pic_driver = {
	"pic",
	aml8726_pic_methods,
	sizeof(struct aml8726_pic_softc),
};

static devclass_t aml8726_pic_devclass;

EARLY_DRIVER_MODULE(pic, simplebus, aml8726_pic_driver, aml8726_pic_devclass,
    0, 0, BUS_PASS_INTERRUPT);

#ifndef DEV_GIC
int
arm_get_next_irq(int last)
{
	uint32_t value;
	int irq;
	int start;

	/*
	 * The extra complexity is simply so that all IRQs are checked
	 * round robin so a particularly busy interrupt can't prevent
	 * other interrupts from being serviced.
	 */

	start = (last + 1) % AML_PIC_NIRQS;
	irq = start;

	for ( ; ; ) {
		value = CSR_READ_4(aml8726_pic_sc, AML_PIC_STAT_REG(irq));

		for ( ; ; ) {
			if ((value & AML_PIC_BIT(irq)) != 0)
				return (irq);

			irq = (irq + 1) % AML_PIC_NIRQS;

			if (irq == start)
				return (-1);

			if ((irq % AML_PIC_IRQS_PER_CNTRL) == 0)
				break;
		}
	}
}

void
arm_mask_irq(uintptr_t nb)
{
	uint32_t mask;

	if (nb >= AML_PIC_NIRQS)
		return;

	mask = CSR_READ_4(aml8726_pic_sc, AML_PIC_MASK_REG(nb));
	mask &= ~AML_PIC_BIT(nb);
	CSR_WRITE_4(aml8726_pic_sc, AML_PIC_MASK_REG(nb), mask);

	CSR_BARRIER(aml8726_pic_sc, AML_PIC_MASK_REG(nb));

	aml8726_pic_eoi((void *)nb);
}

void
arm_unmask_irq(uintptr_t nb)
{
	uint32_t mask;

	if (nb >= AML_PIC_NIRQS)
		return;

	arm_irq_memory_barrier(nb);

	mask = CSR_READ_4(aml8726_pic_sc, AML_PIC_MASK_REG(nb));
	mask |= AML_PIC_BIT(nb);
	CSR_WRITE_4(aml8726_pic_sc, AML_PIC_MASK_REG(nb), mask);

	CSR_BARRIER(aml8726_pic_sc, AML_PIC_MASK_REG(nb));
}
#endif
