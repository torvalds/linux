/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2019 Justin Hibbits
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#ifdef POWERNV
#include <powerpc/powernv/opal.h>
#endif

#include "pic_if.h"

#define XIVE_PRIORITY	7	/* Random non-zero number */
#define MAX_XIVE_IRQS	(1<<24)	/* 24-bit XIRR field */

/* Registers */
#define	XIVE_TM_QW1_OS		0x010	/* Guest OS registers */
#define	XIVE_TM_QW2_HV_POOL	0x020	/* Hypervisor pool registers */
#define	XIVE_TM_QW3_HV		0x030	/* Hypervisor registers */

#define	XIVE_TM_NSR	0x00
#define	XIVE_TM_CPPR	0x01
#define	XIVE_TM_IPB	0x02
#define	XIVE_TM_LSMFB	0x03
#define	XIVE_TM_ACK_CNT	0x04
#define	XIVE_TM_INC	0x05
#define	XIVE_TM_AGE	0x06
#define	XIVE_TM_PIPR	0x07

#define	TM_WORD0	0x0
#define	TM_WORD2	0x8
#define	  TM_QW2W2_VP	  0x80000000

#define	XIVE_TM_SPC_ACK			0x800
#define	  TM_QW3NSR_HE_SHIFT		  14
#define	  TM_QW3_NSR_HE_NONE		  0
#define	  TM_QW3_NSR_HE_POOL		  1
#define	  TM_QW3_NSR_HE_PHYS		  2
#define	  TM_QW3_NSR_HE_LSI		  3
#define	XIVE_TM_SPC_PULL_POOL_CTX	0x828

#define	XIVE_IRQ_LOAD_EOI	0x000
#define	XIVE_IRQ_STORE_EOI	0x400
#define	XIVE_IRQ_PQ_00		0xc00
#define	XIVE_IRQ_PQ_01		0xd00

#define	XIVE_IRQ_VAL_P		0x02
#define	XIVE_IRQ_VAL_Q		0x01

struct xive_softc;
struct xive_irq;

extern void (*powernv_smp_ap_extra_init)(void);

/* Private support */
static void	xive_setup_cpu(void);
static void	xive_smp_cpu_startup(void);
static void	xive_init_irq(struct xive_irq *irqd, u_int irq);
static struct xive_irq	*xive_configure_irq(u_int irq);
static int	xive_provision_page(struct xive_softc *sc);


/* Interfaces */
static int	xive_probe(device_t);
static int	xive_attach(device_t);
static int	xics_probe(device_t);
static int	xics_attach(device_t);

static void	xive_bind(device_t, u_int, cpuset_t, void **);
static void	xive_dispatch(device_t, struct trapframe *);
static void	xive_enable(device_t, u_int, u_int, void **);
static void	xive_eoi(device_t, u_int, void *);
static void	xive_ipi(device_t, u_int);
static void	xive_mask(device_t, u_int, void *);
static void	xive_unmask(device_t, u_int, void *);
static void	xive_translate_code(device_t dev, u_int irq, int code,
		    enum intr_trigger *trig, enum intr_polarity *pol);

static device_method_t  xive_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		xive_probe),
	DEVMETHOD(device_attach,	xive_attach),

	/* PIC interface */
	DEVMETHOD(pic_bind,		xive_bind),
	DEVMETHOD(pic_dispatch,		xive_dispatch),
	DEVMETHOD(pic_enable,		xive_enable),
	DEVMETHOD(pic_eoi,		xive_eoi),
	DEVMETHOD(pic_ipi,		xive_ipi),
	DEVMETHOD(pic_mask,		xive_mask),
	DEVMETHOD(pic_unmask,		xive_unmask),
	DEVMETHOD(pic_translate_code,	xive_translate_code),

	DEVMETHOD_END
};

static device_method_t  xics_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		xics_probe),
	DEVMETHOD(device_attach,	xics_attach),

	DEVMETHOD_END
};

struct xive_softc {
	struct mtx sc_mtx;
	struct resource *sc_mem;
	vm_size_t	sc_prov_page_size;
	uint32_t	sc_offset;
};

struct xive_queue {
	uint32_t	*q_page;
	uint32_t	*q_eoi_page;
	uint32_t	 q_toggle;
	uint32_t	 q_size;
	uint32_t	 q_index;
	uint32_t	 q_mask;
};

struct xive_irq {
	uint32_t	girq;
	uint32_t	lirq;
	uint64_t	vp;
	uint64_t	flags;
#define	OPAL_XIVE_IRQ_EOI_VIA_FW	0x00000020
#define	OPAL_XIVE_IRQ_MASK_VIA_FW	0x00000010
#define	OPAL_XIVE_IRQ_SHIFT_BUG		0x00000008
#define	OPAL_XIVE_IRQ_LSI		0x00000004
#define	OPAL_XIVE_IRQ_STORE_EOI		0x00000002
#define	OPAL_XIVE_IRQ_TRIGGER_PAGE	0x00000001
	uint8_t	prio;
	vm_offset_t	eoi_page;
	vm_offset_t	trig_page;
	vm_size_t	esb_size;
	int		chip;
};

struct xive_cpu {
	uint64_t	vp;
	uint64_t	flags;
	struct xive_irq	ipi_data;
	struct xive_queue	queue; /* We only use a single queue for now. */
	uint64_t	cam;
	uint32_t	chip;
};

static driver_t xive_driver = {
	"xive",
	xive_methods,
	sizeof(struct xive_softc)
};

static driver_t xics_driver = {
	"xivevc",
	xics_methods,
	0
};

static devclass_t xive_devclass;
static devclass_t xics_devclass;

EARLY_DRIVER_MODULE(xive, ofwbus, xive_driver, xive_devclass, 0, 0,
    BUS_PASS_INTERRUPT-1);
EARLY_DRIVER_MODULE(xivevc, ofwbus, xics_driver, xics_devclass, 0, 0,
    BUS_PASS_INTERRUPT);

MALLOC_DEFINE(M_XIVE, "xive", "XIVE Memory");

DPCPU_DEFINE_STATIC(struct xive_cpu, xive_cpu_data);

static int xive_ipi_vector = -1;

/*
 * XIVE Exploitation mode driver.
 *
 * The XIVE, present in the POWER9 CPU, can run in two modes: XICS emulation
 * mode, and "Exploitation mode".  XICS emulation mode is compatible with the
 * POWER8 and earlier XICS interrupt controller, using OPAL calls to emulate
 * hypervisor calls and memory accesses.  Exploitation mode gives us raw access
 * to the XIVE MMIO, improving performance significantly.
 *
 * The XIVE controller is a very bizarre interrupt controller.  It uses queues
 * in memory to pass interrupts around, and maps itself into 512GB of physical
 * device address space, giving each interrupt in the system one or more pages
 * of address space.  An IRQ is tied to a virtual processor, which could be a
 * physical CPU thread, or a guest CPU thread (LPAR running on a physical
 * thread).  Thus, the controller can route interrupts directly to guest OSes
 * bypassing processing by the hypervisor, thereby improving performance of the
 * guest OS.
 *
 * An IRQ, in addition to being tied to a virtual processor, has one or two
 * page mappings: an EOI page, and an optional trigger page.  The trigger page
 * could be the same as the EOI page.  Level-sensitive interrupts (LSIs) don't
 * have a trigger page, as they're external interrupts controlled by physical
 * lines.  MSIs and IPIs have trigger pages.  An IPI is really just another IRQ
 * in the XIVE, which is triggered by software.
 *
 * An interesting behavior of the XIVE controller is that oftentimes the
 * contents of an address location don't actually matter, but the direction of
 * the action is the signifier (read vs write), and the address is significant.
 * Hence, masking and unmasking an interrupt is done by reading different
 * addresses in the EOI page, and triggering an interrupt consists of writing to
 * the trigger page.
 *
 * Additionally, the MMIO region mapped is CPU-sensitive, just like the
 * per-processor register space (private access) in OpenPIC.  In order for a CPU
 * to receive interrupts it must itself configure its CPPR (Current Processor
 * Priority Register), it cannot be set by any other processor.  This
 * necessitates the xive_smp_cpu_startup() function.
 *
 * Queues are pages of memory, sized powers-of-two, that are shared with the
 * XIVE.  The XIVE writes into the queue with an alternating polarity bit, which
 * flips when the queue wraps.
 */

/*
 * Offset-based read/write interfaces.
 */
static uint16_t
xive_read_2(struct xive_softc *sc, bus_size_t offset)
{

	return (bus_read_2(sc->sc_mem, sc->sc_offset + offset));
}

static void
xive_write_1(struct xive_softc *sc, bus_size_t offset, uint8_t val)
{

	bus_write_1(sc->sc_mem, sc->sc_offset + offset, val);
}

/* EOI and Trigger page access interfaces. */
static uint64_t
xive_read_mmap8(vm_offset_t addr)
{
	return (*(volatile uint64_t *)addr);
}

static void
xive_write_mmap8(vm_offset_t addr, uint64_t val)
{
	*(uint64_t *)(addr) = val;
}


/* Device interfaces. */
static int
xive_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "ibm,opal-xive-pe"))
		return (ENXIO);

	device_set_desc(dev, "External Interrupt Virtualization Engine");

	/* Make sure we always win against the xicp driver. */
	return (BUS_PROBE_DEFAULT);
}

static int
xics_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "ibm,opal-xive-vc"))
		return (ENXIO);

	device_set_desc(dev, "External Interrupt Virtualization Engine Root");
	return (BUS_PROBE_DEFAULT);
}

static int
xive_attach(device_t dev)
{
	struct xive_softc *sc = device_get_softc(dev);
	struct xive_cpu *xive_cpud;
	phandle_t phandle = ofw_bus_get_node(dev);
	int64_t vp_block;
	int error;
	int rid;
	int i, order;
	uint64_t vp_id;
	int64_t ipi_irq;

	opal_call(OPAL_XIVE_RESET, OPAL_XIVE_XICS_MODE_EXP);

	error = OF_getencprop(phandle, "ibm,xive-provision-page-size",
	    (pcell_t *)&sc->sc_prov_page_size, sizeof(sc->sc_prov_page_size));

	rid = 1;	/* Get the Hypervisor-level register set. */
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	sc->sc_offset = XIVE_TM_QW3_HV;

	mtx_init(&sc->sc_mtx, "XIVE", NULL, MTX_DEF);

	order = fls(mp_maxid + (mp_maxid - 1)) - 1;

	do {
		vp_block = opal_call(OPAL_XIVE_ALLOCATE_VP_BLOCK, order);
		if (vp_block == OPAL_BUSY)
			DELAY(10);
		else if (vp_block == OPAL_XIVE_PROVISIONING)
			xive_provision_page(sc);
		else
			break;
	} while (1);

	if (vp_block < 0) {
		device_printf(dev,
		    "Unable to allocate VP block.  Opal error %d\n",
		    (int)vp_block);
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->sc_mem);
		return (ENXIO);
	}

	/*
	 * Set up the VPs.  Try to do as much as we can in attach, to lessen
	 * what's needed at AP spawn time.
	 */
	CPU_FOREACH(i) {
		vp_id = pcpu_find(i)->pc_hwref;

		xive_cpud = DPCPU_ID_PTR(i, xive_cpu_data);
		xive_cpud->vp = vp_id + vp_block;
		opal_call(OPAL_XIVE_GET_VP_INFO, xive_cpud->vp, NULL,
		    vtophys(&xive_cpud->cam), NULL, vtophys(&xive_cpud->chip));

		/* Allocate the queue page and populate the queue state data. */
		xive_cpud->queue.q_page = contigmalloc(PAGE_SIZE, M_XIVE,
		    M_ZERO | M_WAITOK, 0, BUS_SPACE_MAXADDR, PAGE_SIZE, 0);
		xive_cpud->queue.q_size = 1 << PAGE_SHIFT;
		xive_cpud->queue.q_mask =
		    ((xive_cpud->queue.q_size / sizeof(int)) - 1);
		xive_cpud->queue.q_toggle = 0;
		xive_cpud->queue.q_index = 0;
		do {
			error = opal_call(OPAL_XIVE_SET_VP_INFO, xive_cpud->vp,
			    OPAL_XIVE_VP_ENABLED, 0);
		} while (error == OPAL_BUSY);
		error = opal_call(OPAL_XIVE_SET_QUEUE_INFO, vp_id,
		    XIVE_PRIORITY, vtophys(xive_cpud->queue.q_page), PAGE_SHIFT,
		    OPAL_XIVE_EQ_ALWAYS_NOTIFY | OPAL_XIVE_EQ_ENABLED);

		do {
			ipi_irq = opal_call(OPAL_XIVE_ALLOCATE_IRQ,
			    xive_cpud->chip);
		} while (ipi_irq == OPAL_BUSY);

		if (ipi_irq < 0)
			device_printf(root_pic,
			    "Failed allocating IPI.  OPAL error %d\n",
			    (int)ipi_irq);
		else {
			xive_init_irq(&xive_cpud->ipi_data, ipi_irq);
			xive_cpud->ipi_data.vp = vp_id;
			xive_cpud->ipi_data.lirq = MAX_XIVE_IRQS;
			opal_call(OPAL_XIVE_SET_IRQ_CONFIG, ipi_irq,
			    xive_cpud->ipi_data.vp, XIVE_PRIORITY,
			    MAX_XIVE_IRQS);
		}
	}

	powerpc_register_pic(dev, OF_xref_from_node(phandle), MAX_XIVE_IRQS,
	    1 /* Number of IPIs */, FALSE);
	root_pic = dev;

	xive_setup_cpu();
	powernv_smp_ap_extra_init = xive_smp_cpu_startup;

	return (0);
}

static int
xics_attach(device_t dev)
{
	phandle_t phandle = ofw_bus_get_node(dev);

	/* The XIVE (root PIC) will handle all our interrupts */
	powerpc_register_pic(root_pic, OF_xref_from_node(phandle),
	    MAX_XIVE_IRQS, 1 /* Number of IPIs */, FALSE);

	return (0);
}

/*
 * PIC I/F methods.
 */

static void
xive_bind(device_t dev, u_int irq, cpuset_t cpumask, void **priv)
{
	struct xive_irq *irqd;
	int cpu;
	int ncpus, i, error;

	if (*priv == NULL)
		*priv = xive_configure_irq(irq);

	irqd = *priv;

	/*
	 * This doesn't appear to actually support affinity groups, so pick a
	 * random CPU.
	 */
	ncpus = 0;
	CPU_FOREACH(cpu)
		if (CPU_ISSET(cpu, &cpumask)) ncpus++;

	i = mftb() % ncpus;
	ncpus = 0;
	CPU_FOREACH(cpu) {
		if (!CPU_ISSET(cpu, &cpumask))
			continue;
		if (ncpus == i)
			break;
		ncpus++;
	}

	opal_call(OPAL_XIVE_SYNC);
	
	irqd->vp = pcpu_find(cpu)->pc_hwref;
	error = opal_call(OPAL_XIVE_SET_IRQ_CONFIG, irq, irqd->vp,
	    XIVE_PRIORITY, irqd->lirq);

	if (error < 0)
		panic("Cannot bind interrupt %d to CPU %d", irq, cpu);

	xive_eoi(dev, irq, irqd);
}

/* Read the next entry in the queue page and update the index. */
static int
xive_read_eq(struct xive_queue *q)
{
	uint32_t i = be32toh(q->q_page[q->q_index]);

	/* Check validity, using current queue polarity. */
	if ((i >> 31) == q->q_toggle)
		return (0);

	q->q_index = (q->q_index + 1) & q->q_mask;

	if (q->q_index == 0)
		q->q_toggle ^= 1;

	return (i & 0x7fffffff);
}

static void
xive_dispatch(device_t dev, struct trapframe *tf)
{
	struct xive_softc *sc;
	struct xive_cpu *xive_cpud;
	uint32_t vector;
	uint16_t ack;
	uint8_t cppr, he;

	sc = device_get_softc(dev);

	for (;;) {
		ack = xive_read_2(sc, XIVE_TM_SPC_ACK);
		cppr = (ack & 0xff);

		he = ack >> TM_QW3NSR_HE_SHIFT;

		if (he == TM_QW3_NSR_HE_NONE)
			break;
		switch (he) {
		case TM_QW3_NSR_HE_NONE:
			goto end;
		case TM_QW3_NSR_HE_POOL:
		case TM_QW3_NSR_HE_LSI:
			device_printf(dev,
			    "Unexpected interrupt he type: %d\n", he);
			goto end;
		case TM_QW3_NSR_HE_PHYS:
			break;
		}

		xive_cpud = DPCPU_PTR(xive_cpu_data);
		xive_write_1(sc, XIVE_TM_CPPR, cppr);

		for (;;) {
			vector = xive_read_eq(&xive_cpud->queue);

			if (vector == 0)
				break;

			if (vector == MAX_XIVE_IRQS)
				vector = xive_ipi_vector;

			powerpc_dispatch_intr(vector, tf);
		}
	}
end:
	xive_write_1(sc, XIVE_TM_CPPR, 0xff);
}

static void
xive_enable(device_t dev, u_int irq, u_int vector, void **priv)
{
	struct xive_irq *irqd;
	cell_t status, cpu;

	if (irq == MAX_XIVE_IRQS) {
		if (xive_ipi_vector == -1)
			xive_ipi_vector = vector;
		return;
	}
	if (*priv == NULL)
		*priv = xive_configure_irq(irq);

	irqd = *priv;

	/* Bind to this CPU to start */
	cpu = PCPU_GET(hwref);
	irqd->lirq = vector;

	for (;;) {
		status = opal_call(OPAL_XIVE_SET_IRQ_CONFIG, irq, cpu,
		    XIVE_PRIORITY, vector);
		if (status != OPAL_BUSY)
			break;
		DELAY(10);
	}

	if (status != 0)
		panic("OPAL_SET_XIVE IRQ %d -> cpu %d failed: %d", irq,
		    cpu, status);

	xive_unmask(dev, irq, *priv);
}

static void
xive_eoi(device_t dev, u_int irq, void *priv)
{
	struct xive_irq *rirq;
	struct xive_cpu *cpud;
	uint8_t eoi_val;

	if (irq == MAX_XIVE_IRQS) {
		cpud = DPCPU_PTR(xive_cpu_data);
		rirq = &cpud->ipi_data;
	} else
		rirq = priv;

	if (rirq->flags & OPAL_XIVE_IRQ_EOI_VIA_FW)
		opal_call(OPAL_INT_EOI, irq);
	else if (rirq->flags & OPAL_XIVE_IRQ_STORE_EOI)
		xive_write_mmap8(rirq->eoi_page + XIVE_IRQ_STORE_EOI, 0);
	else if (rirq->flags & OPAL_XIVE_IRQ_LSI)
		xive_read_mmap8(rirq->eoi_page + XIVE_IRQ_LOAD_EOI);
	else {
		eoi_val = xive_read_mmap8(rirq->eoi_page + XIVE_IRQ_PQ_00);
		if ((eoi_val & XIVE_IRQ_VAL_Q) && rirq->trig_page != 0)
			xive_write_mmap8(rirq->trig_page, 0);
	}
}

static void
xive_ipi(device_t dev, u_int cpu)
{
	struct xive_cpu *xive_cpud;

	xive_cpud = DPCPU_ID_PTR(cpu, xive_cpu_data);

	if (xive_cpud->ipi_data.trig_page == 0)
		return;
	xive_write_mmap8(xive_cpud->ipi_data.trig_page, 0);
}

static void
xive_mask(device_t dev, u_int irq, void *priv)
{
	struct xive_irq *rirq;

	/* Never mask IPIs */
	if (irq == MAX_XIVE_IRQS)
		return;

	rirq = priv;

	if (!(rirq->flags & OPAL_XIVE_IRQ_LSI))
		return;
	xive_read_mmap8(rirq->eoi_page + XIVE_IRQ_PQ_01);
}

static void
xive_unmask(device_t dev, u_int irq, void *priv)
{
	struct xive_irq *rirq;

	rirq = priv;

	xive_read_mmap8(rirq->eoi_page + XIVE_IRQ_PQ_00);
}

static void
xive_translate_code(device_t dev, u_int irq, int code,
    enum intr_trigger *trig, enum intr_polarity *pol)
{
	switch (code) {
	case 0:
		/* L to H edge */
		*trig = INTR_TRIGGER_EDGE;
		*pol = INTR_POLARITY_HIGH;
		break;
	case 1:
		/* Active L level */
		*trig = INTR_TRIGGER_LEVEL;
		*pol = INTR_POLARITY_LOW;
		break;
	default:
		*trig = INTR_TRIGGER_CONFORM;
		*pol = INTR_POLARITY_CONFORM;
	}
}

/* Private functions. */
/*
 * Setup the current CPU.  Called by the BSP at driver attachment, and by each
 * AP at wakeup (via xive_smp_cpu_startup()).
 */
static void
xive_setup_cpu(void)
{
	struct xive_softc *sc;
	struct xive_cpu *cpup;
	uint32_t val;

	cpup = DPCPU_PTR(xive_cpu_data);

	sc = device_get_softc(root_pic);

	val = bus_read_4(sc->sc_mem, XIVE_TM_QW2_HV_POOL + TM_WORD2);
	if (val & TM_QW2W2_VP)
		bus_read_8(sc->sc_mem, XIVE_TM_SPC_PULL_POOL_CTX);

	bus_write_4(sc->sc_mem, XIVE_TM_QW2_HV_POOL + TM_WORD0, 0xff);
	bus_write_4(sc->sc_mem, XIVE_TM_QW2_HV_POOL + TM_WORD2,
	    TM_QW2W2_VP | cpup->cam);

	xive_unmask(root_pic, cpup->ipi_data.girq, &cpup->ipi_data);
	xive_write_1(sc, XIVE_TM_CPPR, 0xff);
}

/* Populate an IRQ structure, mapping the EOI and trigger pages. */
static void
xive_init_irq(struct xive_irq *irqd, u_int irq)
{
	uint64_t eoi_phys, trig_phys;
	uint32_t esb_shift;

	opal_call(OPAL_XIVE_GET_IRQ_INFO, irq,
	    vtophys(&irqd->flags), vtophys(&eoi_phys),
	    vtophys(&trig_phys), vtophys(&esb_shift),
	    vtophys(&irqd->chip));

	irqd->girq = irq;
	irqd->esb_size = 1 << esb_shift;
	irqd->eoi_page = (vm_offset_t)pmap_mapdev(eoi_phys, irqd->esb_size);
	
	if (eoi_phys == trig_phys)
		irqd->trig_page = irqd->eoi_page;
	else if (trig_phys != 0)
		irqd->trig_page = (vm_offset_t)pmap_mapdev(trig_phys,
		    irqd->esb_size);
	else
		irqd->trig_page = 0;

	opal_call(OPAL_XIVE_GET_IRQ_CONFIG, irq, vtophys(&irqd->vp),
	    vtophys(&irqd->prio), vtophys(&irqd->lirq));
}

/* Allocate an IRQ struct before populating it. */
static struct xive_irq *
xive_configure_irq(u_int irq)
{
	struct xive_irq *irqd;

	irqd = malloc(sizeof(struct xive_irq), M_XIVE, M_WAITOK);

	xive_init_irq(irqd, irq);

	return (irqd);
}

/*
 * Part of the OPAL API.  OPAL_XIVE_ALLOCATE_VP_BLOCK might require more pages,
 * provisioned through this call.
 */
static int
xive_provision_page(struct xive_softc *sc)
{
	void *prov_page;
	int error;

	do {
		prov_page = contigmalloc(sc->sc_prov_page_size, M_XIVE, 0,
		    0, BUS_SPACE_MAXADDR,
		    sc->sc_prov_page_size, sc->sc_prov_page_size);

		error = opal_call(OPAL_XIVE_DONATE_PAGE, -1,
		    vtophys(prov_page));
	} while (error == OPAL_XIVE_PROVISIONING);

	return (0);
}

/* The XIVE_TM_CPPR register must be set by each thread */
static void
xive_smp_cpu_startup(void)
{

	xive_setup_cpu();
}
