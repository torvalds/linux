/*-
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/proc.h>

#include <machine/bus.h>
#include <sys/rman.h>

#include <machine/cpufunc.h>
#include <machine/intr.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bcma/bcma_dmp.h>

#include "pic_if.h"

#include "bcm_machdep.h"

#include "bcm_mipsvar.h"
#include "bcm_mips74kreg.h"

/*
 * Broadcom MIPS74K Core
 *
 * These cores are only found on bcma(4) chipsets.
 */

struct bcm_mips74k_softc;

static int	bcm_mips74k_pic_intr(void *arg);
static void	bcm_mips74k_mask_irq(struct bcm_mips74k_softc *sc,
		    u_int mips_irq, u_int ivec);
static void	bcm_mips74k_unmask_irq(struct bcm_mips74k_softc *sc,
		    u_int mips_irq, u_int ivec);

static const struct bhnd_device bcm_mips74k_devs[] = {
	BHND_DEVICE(MIPS, MIPS74K, NULL, NULL, BHND_DF_SOC),
	BHND_DEVICE_END
};

struct bcm_mips74k_softc {
	struct bcm_mips_softc	 bcm_mips;	/**< parent softc */
	device_t		 dev;
	struct resource		*mem;		/**< cpu core registers */
	int			 mem_rid;
};

/* Early routing of the CPU timer interrupt is required */
static void
bcm_mips74k_timer_init(void *unused)
{
	struct bcm_platform	*bp;
	u_int			 irq;
	uint32_t		 mask;

	bp = bcm_get_platform();

	/* Must be a MIPS74K core attached to a BCMA interconnect */
	if (!bhnd_core_matches(&bp->cpu_id, &(struct bhnd_core_match) {
		BHND_MATCH_CORE(BHND_MFGID_MIPS, BHND_COREID_MIPS74K)
	})) {
		if (bootverbose) {
			BCM_ERR("not a MIPS74K core: %s %s\n",
			    bhnd_vendor_name(bp->cpu_id.vendor),
			    bhnd_core_name(&bp->cpu_id));
		}

		return;
	}

	if (!BHND_CHIPTYPE_IS_BCMA_COMPATIBLE(bp->cid.chip_type)) {
		if (bootverbose)
			BCM_ERR("not a BCMA device\n");
		return;
	}

	/* Route the timer bus ivec to the CPU's timer IRQ, and disable any
	 * other vectors assigned to the IRQ. */
	irq = BCM_MIPS74K_GET_TIMER_IRQ();
	mask = BCM_MIPS74K_INTR_SEL_FLAG(BCM_MIPS74K_TIMER_IVEC);

	BCM_CPU_WRITE_4(bp, BCM_MIPS74K_INTR_SEL(irq), mask);
}

static int
bcm_mips74k_probe(device_t dev)
{
	const struct bhnd_device	*id;
	const struct bhnd_chipid	*cid;

	id = bhnd_device_lookup(dev, bcm_mips74k_devs,
	    sizeof(bcm_mips74k_devs[0]));
	if (id == NULL)
		return (ENXIO);

	/* Check the chip type; the MIPS74K core should only be found
	 * on bcma(4) chipsets (and we rely on bcma OOB interrupt
	 * routing). */
	cid = bhnd_get_chipid(dev);
	if (!BHND_CHIPTYPE_IS_BCMA_COMPATIBLE(cid->chip_type))
		return (ENXIO);

	bhnd_set_default_core_desc(dev);
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_mips74k_attach(device_t dev)
{
	struct bcm_mips74k_softc	*sc;
	u_int				 timer_irq;
	int				 error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Allocate our core's register block */
	sc->mem_rid = 0;
	sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE);
	if (sc->mem == NULL) {
		device_printf(dev, "failed to allocate cpu register block\n");
		return (ENXIO);
	}

	/* Clear interrupt map */
	timer_irq = BCM_MIPS74K_GET_TIMER_IRQ();
	for (size_t i = 0; i < BCM_MIPS74K_NUM_INTR; i++) {
		/* We don't use the timer IRQ; leave it routed to the
		 * MIPS CPU */
		if (i == timer_irq)
			continue;

		bus_write_4(sc->mem, BCM_MIPS74K_INTR_SEL(i), 0);
	}

	/* Initialize the generic BHND MIPS driver state */
	error = bcm_mips_attach(dev, BCM_MIPS74K_NUM_INTR, timer_irq,
	    bcm_mips74k_pic_intr);
	if (error) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem);
		return (error);
	}

	return (0);
}

static int
bcm_mips74k_detach(device_t dev)
{
	struct bcm_mips74k_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);

	if ((error = bcm_mips_detach(dev)))
		return (error);

	bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem);

	return (0);
}


/* PIC_DISABLE_INTR() */
static void
bcm_mips74k_pic_disable_intr(device_t dev, struct intr_irqsrc *irqsrc)
{
	struct bcm_mips74k_softc	*sc;
	struct bcm_mips_irqsrc		*isrc;

	sc = device_get_softc(dev);
	isrc = (struct bcm_mips_irqsrc *)irqsrc;

	KASSERT(isrc->cpuirq != NULL, ("no assigned MIPS IRQ"));

	bcm_mips74k_mask_irq(sc, isrc->cpuirq->mips_irq, isrc->ivec);
}

/* PIC_ENABLE_INTR() */
static void
bcm_mips74k_pic_enable_intr(device_t dev, struct intr_irqsrc *irqsrc)
{
	struct bcm_mips74k_softc	*sc;
	struct bcm_mips_irqsrc		*isrc;

	sc = device_get_softc(dev);
	isrc = (struct bcm_mips_irqsrc *)irqsrc;

	KASSERT(isrc->cpuirq != NULL, ("no assigned MIPS IRQ"));

	bcm_mips74k_unmask_irq(sc, isrc->cpuirq->mips_irq, isrc->ivec);
}

/* PIC_PRE_ITHREAD() */
static void
bcm_mips74k_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	bcm_mips74k_pic_disable_intr(dev, isrc);
}

/* PIC_POST_ITHREAD() */
static void
bcm_mips74k_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	bcm_mips74k_pic_enable_intr(dev, isrc);
}

/* PIC_POST_FILTER() */
static void
bcm_mips74k_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

/**
 * Disable routing of backplane interrupt vector @p ivec to MIPS IRQ
 * @p mips_irq.
 */
static void
bcm_mips74k_mask_irq(struct bcm_mips74k_softc *sc, u_int mips_irq, u_int ivec)
{
	uint32_t oobsel;

	KASSERT(mips_irq < sc->bcm_mips.num_cpuirqs, ("invalid MIPS IRQ %u",
	    mips_irq));
	KASSERT(mips_irq < BCM_MIPS74K_NUM_INTR, ("unsupported MIPS IRQ %u",
	    mips_irq));
	KASSERT(ivec < BCMA_OOB_NUM_BUSLINES, ("invalid backplane ivec"));

	oobsel = bus_read_4(sc->mem, BCM_MIPS74K_INTR_SEL(mips_irq));
	oobsel &= ~(BCM_MIPS74K_INTR_SEL_FLAG(ivec));
	bus_write_4(sc->mem, BCM_MIPS74K_INTR_SEL(mips_irq), oobsel);
}

/**
 * Enable routing of an interrupt.
 */
static void
bcm_mips74k_unmask_irq(struct bcm_mips74k_softc *sc, u_int mips_irq, u_int ivec)
{
	uint32_t oobsel;

	KASSERT(mips_irq < sc->bcm_mips.num_cpuirqs, ("invalid MIPS IRQ %u",
	    mips_irq));
	KASSERT(mips_irq < BCM_MIPS74K_NUM_INTR, ("unsupported MIPS IRQ %u",
	    mips_irq));
	KASSERT(ivec < BCMA_OOB_NUM_BUSLINES, ("invalid backplane ivec"));

	oobsel = bus_read_4(sc->mem, BCM_MIPS74K_INTR_SEL(mips_irq));
	oobsel |= BCM_MIPS74K_INTR_SEL_FLAG(ivec);
	bus_write_4(sc->mem, BCM_MIPS74K_INTR_SEL(mips_irq), oobsel);
}

/* our MIPS CPU interrupt filter */
static int
bcm_mips74k_pic_intr(void *arg)
{
	struct bcm_mips74k_softc	*sc;
	struct bcm_mips_cpuirq		*cpuirq;
	struct bcm_mips_irqsrc		*isrc_solo;
	uint32_t			 oobsel, intr;
	u_int				 i;
	int				 error;

	cpuirq = arg;
	sc = (struct bcm_mips74k_softc*)cpuirq->sc;

	/* Fetch current interrupt state */
	intr = bus_read_4(sc->mem, BCM_MIPS74K_INTR_STATUS);

	/* Fetch mask of interrupt vectors routed to this MIPS IRQ */
	KASSERT(cpuirq->mips_irq < BCM_MIPS74K_NUM_INTR,
	    ("invalid irq %u", cpuirq->mips_irq));

	oobsel = bus_read_4(sc->mem, BCM_MIPS74K_INTR_SEL(cpuirq->mips_irq));

	/* Ignore interrupts not routed to this MIPS IRQ */
	intr &= oobsel;

	/* Handle isrc_solo direct dispatch path */
	isrc_solo = cpuirq->isrc_solo;
	if (isrc_solo != NULL) {
		if (intr & BCM_MIPS_IVEC_MASK(isrc_solo)) {
			error = intr_isrc_dispatch(&isrc_solo->isrc,
			    curthread->td_intr_frame);
			if (error) {
				device_printf(sc->dev, "Stray interrupt %u "
				    "detected\n", isrc_solo->ivec);
				bcm_mips74k_pic_disable_intr(sc->dev,
				    &isrc_solo->isrc);
			}
		}

		intr &= ~(BCM_MIPS_IVEC_MASK(isrc_solo));
		if (intr == 0)
			return (FILTER_HANDLED);

		/* Report and mask additional stray interrupts */
		while ((i = fls(intr)) != 0) {
			i--; /* Get a 0-offset interrupt. */
			intr &= ~(1 << i);

			device_printf(sc->dev, "Stray interrupt %u "
				"detected\n", i);
			bcm_mips74k_mask_irq(sc, cpuirq->mips_irq, i);
		}

		return (FILTER_HANDLED);
	}

	/* Standard dispatch path  */
	while ((i = fls(intr)) != 0) {
		i--; /* Get a 0-offset interrupt. */
		intr &= ~(1 << i);

		KASSERT(i < nitems(sc->bcm_mips.isrcs), ("invalid ivec %u", i));

		error = intr_isrc_dispatch(&sc->bcm_mips.isrcs[i].isrc,
		    curthread->td_intr_frame);
		if (error) {
			device_printf(sc->dev, "Stray interrupt %u detected\n",
			    i);
			bcm_mips74k_mask_irq(sc, cpuirq->mips_irq, i);
			continue;
		}
	}

	return (FILTER_HANDLED);
}

static device_method_t bcm_mips74k_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm_mips74k_probe),
	DEVMETHOD(device_attach,	bcm_mips74k_attach),
	DEVMETHOD(device_detach,	bcm_mips74k_detach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	bcm_mips74k_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	bcm_mips74k_pic_enable_intr),
	DEVMETHOD(pic_pre_ithread,	bcm_mips74k_pic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	bcm_mips74k_pic_post_ithread),
	DEVMETHOD(pic_post_filter,	bcm_mips74k_pic_post_filter),

	DEVMETHOD_END
};

static devclass_t bcm_mips_devclass;

DEFINE_CLASS_1(bcm_mips, bcm_mips74k_driver, bcm_mips74k_methods, sizeof(struct bcm_mips_softc), bcm_mips_driver);
EARLY_DRIVER_MODULE(bcm_mips74k, bhnd, bcm_mips74k_driver, bcm_mips_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
SYSINIT(cpu_init, SI_SUB_CPU, SI_ORDER_FIRST, bcm_mips74k_timer_init, NULL);
MODULE_VERSION(bcm_mips74k, 1);
MODULE_DEPEND(bcm_mips74k, bhnd, 1, 1, 1);
