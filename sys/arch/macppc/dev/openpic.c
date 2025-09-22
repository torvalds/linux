/*	$OpenBSD: openpic.c,v 1.90 2022/07/24 00:28:09 cheloha Exp $	*/

/*-
 * Copyright (c) 2008 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 1995 Per Fogelstrom
 * Copyright (c) 1993, 1994 Charles M. Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)isa.c	7.2 (Berkeley) 5/12/91
 */

#include "hpb.h"

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/atomic.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/pio.h>
#include <dev/ofw/openfirm.h>

#include <macppc/dev/openpicreg.h>

#ifdef OPENPIC_DEBUG
#define DPRINTF(x...)	do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif

#define ICU_LEN 128
int openpic_numirq = ICU_LEN;
#define LEGAL_IRQ(x) ((x >= 0) && (x < ICU_LEN))

int openpic_pri_share[IPL_NUM];

struct intrq openpic_handler[ICU_LEN];

struct openpic_softc {
	struct device sc_dev;
};

vaddr_t openpic_base;
int	openpic_big_endian;
struct	evcount openpic_spurious;
int	openpic_spurious_irq = 255;

int	openpic_match(struct device *parent, void *cf, void *aux);
void	openpic_attach(struct device *, struct device *, void *);

int	openpic_splraise(int);
int	openpic_spllower(int);
void	openpic_splx(int);

u_int	openpic_read(int reg);
void	openpic_write(int reg, u_int val);

void	openpic_acknowledge_irq(int, int);
void	openpic_enable_irq(int, int, int);
void	openpic_disable_irq(int, int);

void	openpic_calc_mask(void);
void	openpic_set_priority(int, int);
void	*openpic_intr_establish(void *, int, int, int, int (*)(void *), void *,
	    const char *);
void	openpic_intr_disestablish(void *, void *);
void	openpic_collect_preconf_intr(void);
void	openpic_ext_intr(void);
int	openpic_ext_intr_handler(struct intrhand *, int *);

/* Generic IRQ management routines. */
void	openpic_gen_acknowledge_irq(int, int);
void	openpic_gen_enable_irq(int, int, int);
void	openpic_gen_disable_irq(int, int);

#if NHPB > 0
/* CPC945 IRQ management routines. */
void	openpic_cpc945_acknowledge_irq(int, int);
void	openpic_cpc945_enable_irq(int, int, int);
void	openpic_cpc945_disable_irq(int, int);
#endif /* NHPB */

struct openpic_ops {
	void	(*acknowledge_irq)(int, int);
	void	(*enable_irq)(int, int, int);
	void	(*disable_irq)(int, int);
} openpic_ops = {
	openpic_gen_acknowledge_irq,
	openpic_gen_enable_irq,
	openpic_gen_disable_irq
};

#ifdef MULTIPROCESSOR
void	openpic_ipi_ddb(void);

/* IRQ vector used for inter-processor interrupts. */
#define IPI_VECTOR_NOP	64
#define IPI_VECTOR_DDB	65

static struct evcount ipi_count;

static int ipi_irq = IPI_VECTOR_NOP;

intr_send_ipi_t openpic_send_ipi;
#endif /* MULTIPROCESSOR */

const struct cfattach openpic_ca = {
	sizeof(struct openpic_softc), openpic_match, openpic_attach
};

struct cfdriver openpic_cd = {
	NULL, "openpic", DV_DULL
};

u_int
openpic_read(int reg)
{
	char *addr = (void *)(openpic_base + reg);

	membar_sync();
	if (openpic_big_endian)
		return in32(addr);
	else
		return in32rb(addr);
}

void
openpic_write(int reg, u_int val)
{
	char *addr = (void *)(openpic_base + reg);

	if (openpic_big_endian)
		out32(addr, val);
	else
		out32rb(addr, val);
	membar_sync();
}

static inline int
openpic_read_irq(int cpu)
{
	return openpic_read(OPENPIC_IACK(cpu)) & OPENPIC_VECTOR_MASK;
}

static inline void
openpic_eoi(int cpu)
{
	openpic_write(OPENPIC_EOI(cpu), 0);
}

int
openpic_match(struct device *parent, void *cf, void *aux)
{
	char type[40];
	int pirq;
	struct confargs *ca = aux;

	bzero (type, sizeof(type));

	if (OF_getprop(ca->ca_node, "interrupt-parent", &pirq, sizeof(pirq))
	    == sizeof(pirq))
		return 0; /* XXX */

	if (strcmp(ca->ca_name, "interrupt-controller") != 0 &&
	    strcmp(ca->ca_name, "mpic") != 0)
		return 0;

	OF_getprop(ca->ca_node, "device_type", type, sizeof(type));
	if (strcmp(type, "open-pic") != 0)
		return 0;

	if (ca->ca_nreg < 8)
		return 0;

	return 1;
}

void
openpic_attach(struct device *parent, struct device *self, void *aux)
{
	struct cpu_info *ci = curcpu();
	struct confargs *ca = aux;
	struct intrq *iq;
	uint32_t reg = 0;
	int i, irq;
	u_int x;

	if (OF_getprop(ca->ca_node, "big-endian", &reg, sizeof reg) == 0)
		openpic_big_endian = 1;

	openpic_base = (vaddr_t) mapiodev (ca->ca_baseaddr +
			ca->ca_reg[0], 0x40000);

	/* Reset the PIC */
	x = openpic_read(OPENPIC_CONFIG) | OPENPIC_CONFIG_RESET;
	openpic_write(OPENPIC_CONFIG, x);

	while (openpic_read(OPENPIC_CONFIG) & OPENPIC_CONFIG_RESET)
		delay(100);

	/* openpic may support more than 128 interrupts but driver doesn't */
	openpic_numirq = ((openpic_read(OPENPIC_FEATURE) >> 16) & 0x7f)+1;

	printf(": version 0x%x feature %x %s",
	    openpic_read(OPENPIC_VENDOR_ID),
	    openpic_read(OPENPIC_FEATURE),
		openpic_big_endian ? "BE" : "LE" );

	openpic_set_priority(ci->ci_cpuid, 15);

	/* disable all interrupts */
	for (irq = 0; irq < openpic_numirq; irq++)
		openpic_write(OPENPIC_SRC_VECTOR(irq), OPENPIC_IMASK);

	for (i = 0; i < openpic_numirq; i++) {
		iq = &openpic_handler[i];
		TAILQ_INIT(&iq->iq_list);
	}

	/* we don't need 8259 pass through mode */
	x = openpic_read(OPENPIC_CONFIG);
	x |= OPENPIC_CONFIG_8259_PASSTHRU_DISABLE;
	openpic_write(OPENPIC_CONFIG, x);

	/* initialize all vectors to something sane */
	for (irq = 0; irq < ICU_LEN; irq++) {
		x = irq;
		x |= OPENPIC_IMASK;
		x |= OPENPIC_POLARITY_NEGATIVE;
		x |= OPENPIC_SENSE_LEVEL;
		x |= 8 << OPENPIC_PRIORITY_SHIFT;
		openpic_write(OPENPIC_SRC_VECTOR(irq), x);
	}

	/* send all interrupts to cpu 0 */
	for (irq = 0; irq < openpic_numirq; irq++)
		openpic_write(OPENPIC_IDEST(irq), 1 << 0);

	/* clear all pending interrupts */
	for (irq = 0; irq < ICU_LEN; irq++) {
		openpic_read_irq(ci->ci_cpuid);
		openpic_eoi(ci->ci_cpuid);
	}

#ifdef MULTIPROCESSOR
	/* Set up inter-processor interrupts. */
	/* IPI0 - NOP */
	x = IPI_VECTOR_NOP;
	x |= 15 << OPENPIC_PRIORITY_SHIFT;
	openpic_write(OPENPIC_IPI_VECTOR(0), x);
	/* IPI1 - DDB */
	x = IPI_VECTOR_DDB;
	x |= 15 << OPENPIC_PRIORITY_SHIFT;
	openpic_write(OPENPIC_IPI_VECTOR(1), x);

	evcount_attach(&ipi_count, "ipi", &ipi_irq);
#endif

	/* clear all pending interrupts */
	for (irq = 0; irq < ICU_LEN; irq++) {
		openpic_read_irq(0);
		openpic_eoi(0);
	}

#if 0
	openpic_write(OPENPIC_SPURIOUS_VECTOR, 255);
#endif

#if NHPB > 0
	/* Only U4 systems have a big-endian MPIC. */
	if (openpic_big_endian) {
		openpic_ops.acknowledge_irq = openpic_cpc945_acknowledge_irq;
		openpic_ops.enable_irq = openpic_cpc945_enable_irq;
		openpic_ops.disable_irq = openpic_cpc945_disable_irq;
	}
#endif

	install_extint(openpic_ext_intr);

	openpic_set_priority(ci->ci_cpuid, 0);

	intr_establish_func  = openpic_intr_establish;
	intr_disestablish_func = openpic_intr_disestablish;
#ifdef MULTIPROCESSOR
	intr_send_ipi_func = openpic_send_ipi;
#endif

	ppc_smask_init();

	openpic_collect_preconf_intr();

	evcount_attach(&openpic_spurious, "spurious", &openpic_spurious_irq);

	ppc_intr_func.raise = openpic_splraise;
	ppc_intr_func.lower = openpic_spllower;
	ppc_intr_func.x = openpic_splx;

	openpic_set_priority(0, ci->ci_cpl);

	ppc_intr_enable(1);

	printf("\n");
}

/* Must be called with interrupt disable. */
static inline void
openpic_setipl(int newcpl)
{
	struct cpu_info *ci = curcpu();

	ci->ci_cpl = newcpl;
	openpic_set_priority(ci->ci_cpuid, newcpl);
}

int
openpic_splraise(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int ocpl = ci->ci_cpl;
	int s;

	newcpl = openpic_pri_share[newcpl];
	if (ocpl > newcpl)
		newcpl = ocpl;

	s = ppc_intr_disable();
	openpic_setipl(newcpl);
	ppc_intr_enable(s);

	return ocpl;
}

int
openpic_spllower(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int ocpl = ci->ci_cpl;

	openpic_splx(newcpl);

	return ocpl;
}

void
openpic_splx(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int intr, s;

	intr = ppc_intr_disable();
	openpic_setipl(newcpl);
	if (ci->ci_dec_deferred && newcpl < IPL_CLOCK) {
		ppc_mtdec(0);
		ppc_mtdec(UINT32_MAX);	/* raise DEC exception */
	}
	if (newcpl < IPL_SOFTTTY && (ci->ci_ipending & ppc_smask[newcpl])) {
		s = splsofttty();
		dosoftint(newcpl);
		openpic_setipl(s); /* no-overhead splx */
	}
	ppc_intr_enable(intr);
}

void
openpic_collect_preconf_intr(void)
{
	int i;
	for (i = 0; i < ppc_configed_intr_cnt; i++) {
		DPRINTF("\n\t%s irq %d level %d fun %p arg %p",
		    ppc_configed_intr[i].ih_what, ppc_configed_intr[i].ih_irq,
		    ppc_configed_intr[i].ih_level, ppc_configed_intr[i].ih_fun,
		    ppc_configed_intr[i].ih_arg);
		openpic_intr_establish(NULL, ppc_configed_intr[i].ih_irq,
		    IST_LEVEL, ppc_configed_intr[i].ih_level,
		    ppc_configed_intr[i].ih_fun, ppc_configed_intr[i].ih_arg,
		    ppc_configed_intr[i].ih_what);
	}
}

/*
 * Register an interrupt handler.
 */
void *
openpic_intr_establish(void *lcv, int irq, int type, int level,
    int (*ih_fun)(void *), void *ih_arg, const char *name)
{
	struct intrhand *ih;
	struct intrq *iq;
	int s, flags;

	if (!LEGAL_IRQ(irq) || type == IST_NONE) {
		printf("%s: bogus irq %d or type %d", __func__, irq, type);
		return (NULL);
	}

	/* no point in sleeping unless someone can free memory. */
	ih = malloc(sizeof *ih, M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL)
		panic("%s: can't malloc handler info", __func__);

	iq = &openpic_handler[irq];
	switch (iq->iq_ist) {
	case IST_NONE:
		iq->iq_ist = type;
		break;
	case IST_EDGE:
		intr_shared_edge = 1;
		/* FALLTHROUGH */
	case IST_LEVEL:
		if (type == iq->iq_ist)
			break;
	case IST_PULSE:
		if (type != IST_NONE)
			panic("intr_establish: can't share %s with %s",
			    ppc_intr_typename(iq->iq_ist),
			    ppc_intr_typename(type));
		break;
	}

	flags = level & IPL_MPSAFE;
	level &= ~IPL_MPSAFE;

	KASSERT(level <= IPL_TTY || level >= IPL_CLOCK || flags & IPL_MPSAFE);

	ih->ih_fun = ih_fun;
	ih->ih_arg = ih_arg;
	ih->ih_level = level;
	ih->ih_flags = flags;
	ih->ih_irq = irq;

	evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	/*
	 * Append handler to end of list
	 */
	s = ppc_intr_disable();

	TAILQ_INSERT_TAIL(&iq->iq_list, ih, ih_list);
	openpic_calc_mask();

	ppc_intr_enable(s);

	return (ih);
}

/*
 * Deregister an interrupt handler.
 */
void
openpic_intr_disestablish(void *lcp, void *arg)
{
	struct intrhand *ih = arg;
	int irq = ih->ih_irq;
	struct intrq *iq;
	int s;

	if (!LEGAL_IRQ(irq)) {
		printf("%s: bogus irq %d", __func__, irq);
		return;
	}
	iq = &openpic_handler[irq];

	/*
	 * Remove the handler from the chain.
	 */
	s = ppc_intr_disable();

	TAILQ_REMOVE(&iq->iq_list, ih, ih_list);
	openpic_calc_mask();

	ppc_intr_enable(s);

	evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof *ih);

	if (TAILQ_EMPTY(&iq->iq_list))
		iq->iq_ist = IST_NONE;
}

/*
 * Recalculate the interrupt masks from scratch.
 * We could code special registry and deregistry versions of this function that
 * would be faster, but the code would be nastier, and we don't expect this to
 * happen very much anyway.
 */

void
openpic_calc_mask(void)
{
	struct cpu_info *ci = curcpu();
	int irq;
	struct intrhand *ih;
	int i;

	/* disable all openpic interrupts */
	openpic_set_priority(ci->ci_cpuid, 15);

	for (i = IPL_NONE; i < IPL_NUM; i++) {
		openpic_pri_share[i] = i;
	}

	for (irq = 0; irq < openpic_numirq; irq++) {
		int maxipl = IPL_NONE;
		int minipl = IPL_HIGH;
		struct intrq *iq = &openpic_handler[irq];

		TAILQ_FOREACH(ih, &iq->iq_list, ih_list) {
			if (ih->ih_level > maxipl)
				maxipl = ih->ih_level;
			if (ih->ih_level < minipl)
				minipl = ih->ih_level;
		}

		if (maxipl == IPL_NONE) {
			minipl = IPL_NONE; /* Interrupt not enabled */

			openpic_disable_irq(irq, iq->iq_ist);
		} else {
			for (i = minipl; i <= maxipl; i++) {
				openpic_pri_share[i] = maxipl;
			}
			openpic_enable_irq(irq, iq->iq_ist, maxipl);
		}

		iq->iq_ipl = maxipl;
	}

	/* restore interrupts */
	openpic_set_priority(ci->ci_cpuid, ci->ci_cpl);
}

void
openpic_gen_acknowledge_irq(int irq, int cpuid)
{
	openpic_eoi(cpuid);
}

void
openpic_gen_enable_irq(int irq, int ist, int pri)
{
	u_int x;

	x = irq;

	if (ist == IST_LEVEL)
		x |= OPENPIC_SENSE_LEVEL;
	else
		x |= OPENPIC_SENSE_EDGE;
	x |= OPENPIC_POLARITY_NEGATIVE;
	x |= pri << OPENPIC_PRIORITY_SHIFT;
	openpic_write(OPENPIC_SRC_VECTOR(irq), x);
}

void
openpic_gen_disable_irq(int irq, int ist)
{
	u_int x;

	x = openpic_read(OPENPIC_SRC_VECTOR(irq));
	x |= OPENPIC_IMASK;
	openpic_write(OPENPIC_SRC_VECTOR(irq), x);
}

void
openpic_set_priority(int cpu, int pri)
{
	openpic_write(OPENPIC_CPU_PRIORITY(cpu), pri);
}

int openpic_irqnest[PPC_MAXPROCS];
int openpic_irqloop[PPC_MAXPROCS];

void
openpic_ext_intr(void)
{
	struct cpu_info *ci = curcpu();
	int irq, pcpl, ret;
	int maxipl = IPL_NONE;
	struct intrhand *ih;
	struct intrq *iq;
	int spurious;

	pcpl = ci->ci_cpl;

	openpic_irqloop[ci->ci_cpuid] = 0;
	irq = openpic_read_irq(ci->ci_cpuid);
	openpic_irqnest[ci->ci_cpuid]++;

	while (irq != 255) {
		openpic_irqloop[ci->ci_cpuid]++;
#ifdef OPENPIC_DEBUG
		if (openpic_irqloop[ci->ci_cpuid] > 20 ||
		    openpic_irqnest[ci->ci_cpuid] > 3) {
			printf("irqloop %d irqnest %d\n",
			    openpic_irqloop[ci->ci_cpuid],
			    openpic_irqnest[ci->ci_cpuid]);
		}
#endif
		if (openpic_irqloop[ci->ci_cpuid] > 20) {
			DPRINTF("irqloop %d irqnest %d: returning\n",
			    openpic_irqloop[ci->ci_cpuid],
			    openpic_irqnest[ci->ci_cpuid]);
			openpic_irqnest[ci->ci_cpuid]--;
			return;
		}
#ifdef MULTIPROCESSOR
		if (irq == IPI_VECTOR_NOP || irq == IPI_VECTOR_DDB) {
			ipi_count.ec_count++;
			openpic_eoi(ci->ci_cpuid);
			if (irq == IPI_VECTOR_DDB)
				openpic_ipi_ddb();
			irq = openpic_read_irq(ci->ci_cpuid);
			continue;
		}
#endif
		iq = &openpic_handler[irq];

#ifdef OPENPIC_DEBUG
		if (iq->iq_ipl <= pcpl)
			printf("invalid interrupt %d lvl %d at %d hw %d\n",
			    irq, iq->iq_ipl, pcpl,
			    openpic_read(OPENPIC_CPU_PRIORITY(ci->ci_cpuid)));
#endif

		if (iq->iq_ipl > maxipl)
			maxipl = iq->iq_ipl;
		openpic_splraise(iq->iq_ipl);
		openpic_acknowledge_irq(irq, ci->ci_cpuid);

		spurious = 1;
		TAILQ_FOREACH(ih, &iq->iq_list, ih_list) {
			ppc_intr_enable(1);
			ret = openpic_ext_intr_handler(ih, &spurious);
			(void)ppc_intr_disable();
			if (intr_shared_edge == 00 && ret == 1)
				break;
 		}
		if (spurious) {
			openpic_spurious.ec_count++;
			DPRINTF("spurious intr %d\n", irq);
		}

		uvmexp.intrs++;
		openpic_setipl(pcpl);

		irq = openpic_read_irq(ci->ci_cpuid);
	}

	openpic_splx(pcpl);	/* Process pendings. */
	openpic_irqnest[ci->ci_cpuid]--;
}

int
openpic_ext_intr_handler(struct intrhand *ih, int *spurious)
{
	int ret;
#ifdef MULTIPROCESSOR
	int need_lock;

	if (ih->ih_flags & IPL_MPSAFE)
		need_lock = 0;
	else
		need_lock = 1;

	if (need_lock)
		KERNEL_LOCK();
#endif
	ret = (*ih->ih_fun)(ih->ih_arg);
	if (ret) {
		ih->ih_count.ec_count++;
		*spurious = 0;
	}

#ifdef MULTIPROCESSOR
	if (need_lock)
		KERNEL_UNLOCK();
#endif

	return (ret);
}

void
openpic_acknowledge_irq(int irq, int cpuid)
{
	(openpic_ops.acknowledge_irq)(irq, cpuid);
}

void
openpic_enable_irq(int irq, int ist, int pri)
{
	(openpic_ops.enable_irq)(irq, ist, pri);
}

void
openpic_disable_irq(int irq, int ist)
{
	(openpic_ops.disable_irq)(irq, ist);
}

#ifdef MULTIPROCESSOR
void
openpic_send_ipi(struct cpu_info *ci, int id)
{
	switch (id) {
	case PPC_IPI_NOP:
		id = 0;
		break;
	case PPC_IPI_DDB:
		id = 1;
		break;
	default:
		panic("invalid ipi send to cpu %d %d", ci->ci_cpuid, id);
	}

	openpic_write(OPENPIC_IPI(curcpu()->ci_cpuid, id), 1 << ci->ci_cpuid);
}

void
openpic_ipi_ddb(void)
{
#ifdef DDB
	db_enter();
#endif
}
#endif /* MULTIPROCESSOR */

#if NHPB > 0
extern int	hpb_enable_irq(int, int);
extern int	hpb_disable_irq(int, int);
extern void	hpb_eoi(int);

void
openpic_cpc945_acknowledge_irq(int irq, int cpuid)
{
	hpb_eoi(irq);
	openpic_gen_acknowledge_irq(irq, cpuid);
}

void
openpic_cpc945_enable_irq(int irq, int ist, int pri)
{
	if (hpb_enable_irq(irq, ist)) {
		u_int x = irq;

		x |= OPENPIC_SENSE_EDGE;
		x |= OPENPIC_POLARITY_POSITIVE;
		x |= pri << OPENPIC_PRIORITY_SHIFT;
		openpic_write(OPENPIC_SRC_VECTOR(irq), x);

		hpb_eoi(irq);
	} else
		openpic_gen_enable_irq(irq, ist, pri);
}

void
openpic_cpc945_disable_irq(int irq, int ist)
{
	hpb_disable_irq(irq, ist);
	openpic_gen_disable_irq(irq, ist);
}
#endif /* NHPB */

