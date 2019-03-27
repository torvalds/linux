/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2010 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/platform.h>

#include "ps3-hvcall.h"
#include "pic_if.h"

static void	ps3pic_identify(driver_t *driver, device_t parent);
static int	ps3pic_probe(device_t);
static int	ps3pic_attach(device_t);

static void	ps3pic_dispatch(device_t, struct trapframe *);
static void	ps3pic_enable(device_t, u_int, u_int, void **);
static void	ps3pic_eoi(device_t, u_int, void *);
static void	ps3pic_ipi(device_t, u_int);
static void	ps3pic_mask(device_t, u_int, void *);
static void	ps3pic_unmask(device_t, u_int, void *);

struct ps3pic_softc {
	volatile uint64_t *bitmap_thread0;
	volatile uint64_t *mask_thread0;
	volatile uint64_t *bitmap_thread1;
	volatile uint64_t *mask_thread1;

	uint64_t	sc_ipi_outlet[2];
	uint64_t	sc_ipi_virq;
	int		sc_vector[64];
};

static device_method_t  ps3pic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	ps3pic_identify),
	DEVMETHOD(device_probe,		ps3pic_probe),
	DEVMETHOD(device_attach,	ps3pic_attach),

	/* PIC interface */
	DEVMETHOD(pic_dispatch,		ps3pic_dispatch),
	DEVMETHOD(pic_enable,		ps3pic_enable),
	DEVMETHOD(pic_eoi,		ps3pic_eoi),
	DEVMETHOD(pic_ipi,		ps3pic_ipi),
	DEVMETHOD(pic_mask,		ps3pic_mask),
	DEVMETHOD(pic_unmask,		ps3pic_unmask),

	{ 0, 0 },
};

static driver_t ps3pic_driver = {
	"ps3pic",
	ps3pic_methods,
	sizeof(struct ps3pic_softc)
};

static devclass_t ps3pic_devclass;

DRIVER_MODULE(ps3pic, nexus, ps3pic_driver, ps3pic_devclass, 0, 0);

static MALLOC_DEFINE(M_PS3PIC, "ps3pic", "PS3 PIC");

static void
ps3pic_identify(driver_t *driver, device_t parent)
{
	if (strcmp(installed_platform(), "ps3") != 0)
		return;

	if (device_find_child(parent, "ps3pic", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "ps3pic", 0);
}

static int
ps3pic_probe(device_t dev)
{
	device_set_desc(dev, "Playstation 3 interrupt controller");
	return (BUS_PROBE_NOWILDCARD);
}

static int
ps3pic_attach(device_t dev)
{
	struct ps3pic_softc *sc;
	uint64_t ppe;
	int thread;

	sc = device_get_softc(dev);

	sc->bitmap_thread0 = contigmalloc(128 /* 512 bits * 2 */, M_PS3PIC,
	    M_NOWAIT | M_ZERO, 0, BUS_SPACE_MAXADDR, 64 /* alignment */,
	    PAGE_SIZE /* boundary */);
	sc->mask_thread0 = sc->bitmap_thread0 + 4;
	sc->bitmap_thread1 = sc->bitmap_thread0 + 8;
	sc->mask_thread1 = sc->bitmap_thread0 + 12;

	lv1_get_logical_ppe_id(&ppe);
	thread = 32 - fls(mfctrl());
	lv1_configure_irq_state_bitmap(ppe, thread,
	    vtophys(sc->bitmap_thread0));

	sc->sc_ipi_virq = 63;

#ifdef SMP
	lv1_configure_irq_state_bitmap(ppe, !thread,
	    vtophys(sc->bitmap_thread1));

	/* Map both IPIs to the same VIRQ to avoid changes in intr_machdep */
	lv1_construct_event_receive_port(&sc->sc_ipi_outlet[0]);
	lv1_connect_irq_plug_ext(ppe, thread, sc->sc_ipi_virq,
	    sc->sc_ipi_outlet[0], 0);
	lv1_construct_event_receive_port(&sc->sc_ipi_outlet[1]);
	lv1_connect_irq_plug_ext(ppe, !thread, sc->sc_ipi_virq,
	    sc->sc_ipi_outlet[1], 0);
#endif

	powerpc_register_pic(dev, 0, sc->sc_ipi_virq, 1, FALSE);
	return (0);
}

/*
 * PIC I/F methods.
 */

static void
ps3pic_dispatch(device_t dev, struct trapframe *tf)
{
	uint64_t bitmap, mask;
	int irq;
	struct ps3pic_softc *sc;

	sc = device_get_softc(dev);

	if (PCPU_GET(cpuid) == 0) {
		bitmap = atomic_readandclear_64(&sc->bitmap_thread0[0]);
		mask = sc->mask_thread0[0];
	} else {
		bitmap = atomic_readandclear_64(&sc->bitmap_thread1[0]);
		mask = sc->mask_thread1[0];
	}
	powerpc_sync();

	while ((irq = ffsl(bitmap & mask) - 1) != -1) {
		bitmap &= ~(1UL << irq);
		powerpc_dispatch_intr(sc->sc_vector[63 - irq], tf);
	}
}

static void
ps3pic_enable(device_t dev, u_int irq, u_int vector, void **priv)
{
	struct ps3pic_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_vector[irq] = vector;

	ps3pic_unmask(dev, irq, priv);
}

static void
ps3pic_eoi(device_t dev, u_int irq, void *priv)
{
	uint64_t ppe;
	int thread;

	lv1_get_logical_ppe_id(&ppe);
	thread = 32 - fls(mfctrl());

	lv1_end_of_interrupt_ext(ppe, thread, irq);
}

static void
ps3pic_ipi(device_t dev, u_int cpu)
{
	struct ps3pic_softc *sc;
	sc = device_get_softc(dev);

	lv1_send_event_locally(sc->sc_ipi_outlet[cpu]);
}

static void
ps3pic_mask(device_t dev, u_int irq, void *priv)
{
	struct ps3pic_softc *sc;
	uint64_t ppe;

	sc = device_get_softc(dev);

	/* Do not mask IPIs! */
	if (irq == sc->sc_ipi_virq)
		return;

	atomic_clear_64(&sc->mask_thread0[0], 1UL << (63 - irq));
	atomic_clear_64(&sc->mask_thread1[0], 1UL << (63 - irq));

	lv1_get_logical_ppe_id(&ppe);
	lv1_did_update_interrupt_mask(ppe, 0);
	lv1_did_update_interrupt_mask(ppe, 1);
}

static void
ps3pic_unmask(device_t dev, u_int irq, void *priv)
{
	struct ps3pic_softc *sc;
	uint64_t ppe;

	sc = device_get_softc(dev);
	atomic_set_64(&sc->mask_thread0[0], 1UL << (63 - irq));
	atomic_set_64(&sc->mask_thread1[0], 1UL << (63 - irq));

	lv1_get_logical_ppe_id(&ppe);
	lv1_did_update_interrupt_mask(ppe, 0);
	lv1_did_update_interrupt_mask(ppe, 1);
}
