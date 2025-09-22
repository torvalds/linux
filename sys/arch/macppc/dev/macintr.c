/*	$OpenBSD: macintr.c,v 1.57 2022/07/24 00:28:09 cheloha Exp $	*/

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

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>
#include <ddb/db_var.h>

#include <machine/atomic.h>
#include <machine/autoconf.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/pio.h>

#include <dev/ofw/openfirm.h>

#define ICU_LEN 64
#define LEGAL_IRQ(x) ((x >= 0) && (x < ICU_LEN))

int macintr_ienable_l[IPL_NUM], macintr_ienable_h[IPL_NUM];
int macintr_pri_share[IPL_NUM];

struct intrq macintr_handler[ICU_LEN];

void macintr_calc_mask(void);
void macintr_eoi(int irq);
int macintr_read_irq(void);

extern u_int32_t *heathrow_FCR;

#define INT_STATE_REG0  (interrupt_reg + 0x20)
#define INT_ENABLE_REG0 (interrupt_reg + 0x24)
#define INT_CLEAR_REG0  (interrupt_reg + 0x28)
#define INT_LEVEL_REG0  (interrupt_reg + 0x2c)
#define INT_STATE_REG1  (INT_STATE_REG0  - 0x10)
#define INT_ENABLE_REG1 (INT_ENABLE_REG0 - 0x10)
#define INT_CLEAR_REG1  (INT_CLEAR_REG0  - 0x10)
#define INT_LEVEL_REG1  (INT_LEVEL_REG0  - 0x10)

struct macintr_softc {
	struct device sc_dev;
};

int	macintr_match(struct device *parent, void *cf, void *aux);
void	macintr_attach(struct device *, struct device *, void *);
void	mac_ext_intr(void);
void	macintr_collect_preconf_intr(void);
void	macintr_setipl(int ipl);

const struct cfattach macintr_ca = {
	sizeof(struct macintr_softc),
	macintr_match,
	macintr_attach
};

struct cfdriver macintr_cd = {
	NULL, "macintr", DV_DULL
};

int
macintr_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;
	char type[40];

	/*
	 * Match entry according to "present" openfirmware entry.
	 */
	if (strcmp(ca->ca_name, "interrupt-controller") == 0 ) {
		OF_getprop(ca->ca_node, "device_type", type, sizeof(type));
		if (strcmp(type,  "interrupt-controller") == 0)
			return 1;
	}

	/*
	 * Check name for legacy interrupt controller, this is
	 * faked to allow old firmware which does not have an entry
	 * to attach to this device.
	 */
	if (strcmp(ca->ca_name, "legacy-interrupt-controller") == 0 )
		return 1;
	return 0;
}

u_int8_t *interrupt_reg;
typedef void  (void_f) (void);
int macintr_prog_button (void *arg);

intr_establish_t macintr_establish;
intr_disestablish_t macintr_disestablish;

ppc_splraise_t macintr_splraise;
ppc_spllower_t macintr_spllower;
ppc_splx_t macintr_splx;


int
macintr_splraise(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int ocpl = ci->ci_cpl;
	int s;

	newcpl = macintr_pri_share[newcpl];
	if (ocpl > newcpl)
		newcpl = ocpl;

	s = ppc_intr_disable();
	macintr_setipl(newcpl);
	ppc_intr_enable(s);

	return ocpl;
}

int
macintr_spllower(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int ocpl = ci->ci_cpl;

	macintr_splx(newcpl);

	return ocpl;
}

void
macintr_splx(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int intr, s;

	intr = ppc_intr_disable();
	macintr_setipl(newcpl);
	if (ci->ci_dec_deferred && newcpl < IPL_CLOCK) {
		ppc_mtdec(0);
		ppc_mtdec(UINT32_MAX);	/* raise DEC exception */
	}
	if ((newcpl < IPL_SOFTTTY && ci->ci_ipending & ppc_smask[newcpl])) {
		s = splsofttty();
		dosoftint(newcpl);
		macintr_setipl(s); /* no-overhead splx */
	}
	ppc_intr_enable(intr);
}

void
macintr_attach(struct device *parent, struct device *self, void *aux)
{
	struct cpu_info *ci = curcpu();
	struct confargs *ca = aux;
	extern intr_establish_t *intr_establish_func;
	extern intr_disestablish_t *intr_disestablish_func;
	struct intrq *iq;
	int i;

	interrupt_reg = (void *)mapiodev(ca->ca_baseaddr,0x100); /* XXX */

	for (i = 0; i < ICU_LEN; i++) {
		iq = &macintr_handler[i];
		TAILQ_INIT(&iq->iq_list);
	}
	ppc_smask_init();

	install_extint(mac_ext_intr);
	intr_establish_func  = macintr_establish;
	intr_disestablish_func  = macintr_disestablish;

	ppc_intr_func.raise = macintr_splraise;
	ppc_intr_func.lower = macintr_spllower;
	ppc_intr_func.x = macintr_splx;

	ci->ci_flags = 0;

	macintr_collect_preconf_intr();

	mac_intr_establish(parent, 0x14, IST_LEVEL, IPL_HIGH,
	    macintr_prog_button, (void *)0x14, "progbutton");

	ppc_intr_enable(1);
	printf("\n");
}

void
macintr_collect_preconf_intr(void)
{
	int i;
	for (i = 0; i < ppc_configed_intr_cnt; i++) {
#ifdef DEBUG
		printf("\n\t%s irq %d level %d fun %p arg %p",
			ppc_configed_intr[i].ih_what,
			ppc_configed_intr[i].ih_irq,
			ppc_configed_intr[i].ih_level,
			ppc_configed_intr[i].ih_fun,
			ppc_configed_intr[i].ih_arg
			);
#endif
		macintr_establish(NULL,
			ppc_configed_intr[i].ih_irq,
			IST_LEVEL,
			ppc_configed_intr[i].ih_level,
			ppc_configed_intr[i].ih_fun,
			ppc_configed_intr[i].ih_arg,
			ppc_configed_intr[i].ih_what);
	}
}


/*
 * programmer_button function to fix args to Debugger.
 * deal with any enables/disables, if necessary.
 */
int
macintr_prog_button (void *arg)
{
#ifdef DDB
	if (db_console)
		db_enter();
#else
	printf("programmer button pressed, debugger not available\n");
#endif
	return 1;
}

/* Must be called with interrupt disable. */
void
macintr_setipl(int ipl)
{
	struct cpu_info *ci = curcpu();

	ci->ci_cpl = ipl;
	if (heathrow_FCR)
		out32rb(INT_ENABLE_REG1,
		    macintr_ienable_h[macintr_pri_share[ipl]]);

	out32rb(INT_ENABLE_REG0, macintr_ienable_l[macintr_pri_share[ipl]]);
}

/*
 * Register an interrupt handler.
 */
void *
macintr_establish(void * lcv, int irq, int type, int level,
    int (*ih_fun)(void *), void *ih_arg, const char *name)
{
	struct cpu_info *ci = curcpu();
	struct intrq *iq;
	struct intrhand *ih;
	int s, flags;

	if (!LEGAL_IRQ(irq) || type == IST_NONE) {
		printf("%s: bogus irq %d or type %d", __func__, irq, type);
		return (NULL);
	}

	/* no point in sleeping unless someone can free memory. */
	ih = malloc(sizeof *ih, M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL)
		panic("intr_establish: can't malloc handler info");

	iq = &macintr_handler[irq];
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
	macintr_calc_mask();

	macintr_setipl(ci->ci_cpl);
	ppc_intr_enable(s);

	return (ih);
}

/*
 * Deregister an interrupt handler.
 */
void
macintr_disestablish(void *lcp, void *arg)
{
	struct cpu_info *ci = curcpu();
	struct intrhand *ih = arg;
	int irq = ih->ih_irq;
	int s;
	struct intrq *iq;

	if (!LEGAL_IRQ(irq)) {
		printf("%s: bogus irq %d", __func__, irq);
		return;
	}

	/*
	 * Remove the handler from the chain.
	 */

	iq = &macintr_handler[irq];
	s = ppc_intr_disable();

	TAILQ_REMOVE(&iq->iq_list, ih, ih_list);
	macintr_calc_mask();

	macintr_setipl(ci->ci_cpl);
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
macintr_calc_mask(void)
{
	int irq;
	struct intrhand *ih;
	int i;

	for (i = IPL_NONE; i < IPL_NUM; i++) {
		macintr_pri_share[i] = i;
	}

	for (irq = 0; irq < ICU_LEN; irq++) {
		int maxipl = IPL_NONE;
		int minipl = IPL_HIGH;
		struct intrq *iq = &macintr_handler[irq];

		TAILQ_FOREACH(ih, &iq->iq_list, ih_list) {
			if (ih->ih_level > maxipl)
				maxipl = ih->ih_level;
			if (ih->ih_level < minipl)
				minipl = ih->ih_level;
		}

		iq->iq_ipl = maxipl;

		if (maxipl == IPL_NONE) {
			minipl = IPL_NONE; /* Interrupt not enabled */
		} else {
			for (i = minipl; i < maxipl; i++)
				macintr_pri_share[i] =
				    macintr_pri_share[maxipl];
		}

		/* Enable interrupts at lower levels */

		if (irq < 32) {
			for (i = IPL_NONE; i < minipl; i++)
				macintr_ienable_l[i] |= (1 << irq);
			for (; i <= IPL_HIGH; i++)
				macintr_ienable_l[i] &= ~(1 << irq);
		} else {
			for (i = IPL_NONE; i < minipl; i++)
				macintr_ienable_h[i] |= (1 << (irq-32));
			for (; i <= IPL_HIGH; i++)
				macintr_ienable_h[i] &= ~(1 << (irq-32));
		}
	}

#if 0
	for (i = 0; i < IPL_NUM; i++)
		printf("imask[%d] %x %x\n", i, macintr_ienable_l[i],
		    macintr_ienable_h[i]);
#endif
}

/*
 * external interrupt handler
 */
void
mac_ext_intr(void)
{
	int irq = 0;
	int pcpl, ret;
	struct cpu_info *ci = curcpu();
	struct intrq *iq;
	struct intrhand *ih;

	pcpl = ci->ci_cpl;	/* Turn off all */

	irq = macintr_read_irq();
	while (irq != 255) {
		iq = &macintr_handler[irq];
		macintr_setipl(iq->iq_ipl);

		TAILQ_FOREACH(ih, &iq->iq_list, ih_list) {
			ppc_intr_enable(1);
			ret = ((*ih->ih_fun)(ih->ih_arg));
			if (ret) {
				ih->ih_count.ec_count++;
				if (intr_shared_edge == 0 && ret == 1)
					break;
			}
			(void)ppc_intr_disable();
		}
		macintr_eoi(irq);
		macintr_setipl(pcpl);

		uvmexp.intrs++;

		irq = macintr_read_irq();
	}

	macintr_splx(pcpl);	/* Process pendings. */
}

void
macintr_eoi(int irq)
{
	u_int32_t state0, state1;

	if (irq < 32) {
		state0 =  1 << irq;
		out32rb(INT_CLEAR_REG0, state0);
	} else {
		if (heathrow_FCR) {		/* has heathrow? */
			state1 = 1 << (irq - 32);
			out32rb(INT_CLEAR_REG1, state1);
		}
	}
}

int
macintr_read_irq(void)
{
	struct cpu_info *ci = curcpu();
	u_int32_t state0, state1, irq_mask;
	int ipl, irq;

	state0 = in32rb(INT_STATE_REG0);

	if (heathrow_FCR)			/* has heathrow? */
		state1 = in32rb(INT_STATE_REG1);
	else
		state1 = 0;

	for (ipl = IPL_HIGH; ipl >= ci->ci_cpl; ipl --) {
		irq_mask = state0 & macintr_ienable_l[ipl];
		if (irq_mask) {
			irq = ffs(irq_mask) - 1;
			return irq;
		}
		irq_mask = state1 & macintr_ienable_h[ipl];
		if (irq_mask) {
			irq = ffs(irq_mask) + 31;
			return irq;
		}
	}
	return 255;
}
