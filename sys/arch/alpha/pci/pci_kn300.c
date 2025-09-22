/* $OpenBSD: pci_kn300.c,v 1.10 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pci_kn300.c,v 1.28 2005/12/11 12:16:17 christos Exp $ */

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/syslog.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <alpha/mcbus/mcbusvar.h>
#include <alpha/mcbus/mcbusreg.h>
#include <alpha/pci/mcpciareg.h>
#include <alpha/pci/mcpciavar.h>
#include <alpha/pci/pci_kn300.h>

#include "sio.h"
#if NSIO > 0 || NPCEB > 0
#include <alpha/pci/siovar.h>
#endif

int	dec_kn300_intr_map(struct pci_attach_args *, pci_intr_handle_t *);

const char *dec_kn300_intr_string(void *, pci_intr_handle_t);
void	*dec_kn300_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void	dec_kn300_intr_disestablish(void *, void *);

#define	KN300_PCEB_IRQ	16
#define	NPIN		4

#define	NIRQ	(MAX_MC_BUS * MCPCIA_PER_MCBUS * MCPCIA_MAXSLOT * NPIN)
static int savirqs[NIRQ];

static struct alpha_shared_intr *kn300_pci_intr;

static struct mcpcia_config *mcpcia_eisaccp = NULL;

void	kn300_iointr (void *, unsigned long);
void	kn300_enable_intr (struct mcpcia_config *, int);
void	kn300_disable_intr (struct mcpcia_config *, int);

void
pci_kn300_pickintr(struct mcpcia_config *ccp, int first)
{
	pci_chipset_tag_t pc = &ccp->cc_pc;

	if (first) {
		int g;

		kn300_pci_intr = alpha_shared_intr_alloc(NIRQ);
		for (g = 0; g < NIRQ; g++) {
			alpha_shared_intr_set_maxstrays(kn300_pci_intr, g, 25);
			savirqs[g] = (char) -1;
		}
	}

	pc->pc_intr_v = ccp;
	pc->pc_intr_map = dec_kn300_intr_map;
	pc->pc_intr_string = dec_kn300_intr_string;
	pc->pc_intr_establish = dec_kn300_intr_establish;
	pc->pc_intr_disestablish = dec_kn300_intr_disestablish;

	/* Not supported on KN300. */
	pc->pc_pciide_compat_intr_establish = NULL;

	if (EISA_PRESENT(REGVAL(MCPCIA_PCI_REV(ccp)))) {
		mcpcia_eisaccp = ccp;
#if NSIO > 0 || NPCEB > 0
		sio_intr_setup(pc, &ccp->cc_iot);
		kn300_enable_intr(ccp, KN300_PCEB_IRQ);
#endif
	}
}

int
dec_kn300_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pcitag_t bustag = pa->pa_intrtag;
	int buspin = pa->pa_intrpin;
	pci_chipset_tag_t pc = pa->pa_pc;
	struct mcpcia_config *ccp = (struct mcpcia_config *)pc->pc_intr_v;
	int device;
	int mcpcia_irq;

	if (pa->pa_bridgetag) {
		buspin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin,
		    pa->pa_device);
		if (pa->pa_bridgeih[buspin - 1] != 0) {
			*ihp = pa->pa_bridgeih[buspin - 1];
			return 0;
		}

		return 1;
	}

	pci_decompose_tag(pc, bustag, NULL, &device, NULL);

	/*
	 * On MID 5 device 1 is the internal NCR 53c810.
	 */
	if (ccp->cc_mid == 5 && device == 1) {
		mcpcia_irq = 16;
	} else if (device >= 2 && device <= 5) {
		mcpcia_irq = (device - 2) * 4 + buspin - 1;
	} else {
		printf("dec_kn300_intr_map: don't know how to setup %d/%d/%d\n",
		    pa->pa_bus, pa->pa_device, pa->pa_function);
		return 1;
	}

	/*
	 * handle layout:
	 *
	 *	Determine kn300 IRQ (encoded in SCB vector):
	 *	bits 0..1	buspin-1
	 *	bits 2..4	PCI Slot (0..7- yes, some don't exist)
	 *	bits 5..7	MID-4
	 *	bits 8..10	7-GID
	 *
	 *	Software only:
	 *	bits 11-15	MCPCIA IRQ
	 */
	*ihp = (pci_intr_handle_t)
		(buspin - 1			    )	|
		((device & 0x7)			<< 2)	|
		((ccp->cc_mid - 4)		<< 5)	|
		((7 - ccp->cc_gid)		<< 8)	|
		(mcpcia_irq			<< 11);

	return (0);
}

const char *
dec_kn300_intr_string(void *ccv, pci_intr_handle_t ih)
{
	static char irqstr[64];
	int irq;

	irq = ih & 0x3ff;
	if (irq >= NIRQ)
		panic("dec_kn300_intr_string: bogus kn300 IRQ 0x%x", irq);

	snprintf(irqstr, sizeof irqstr, "kn300 irq %d", irq);

	return (irqstr);
}

void *
dec_kn300_intr_establish(void *ccv, pci_intr_handle_t ih, int level,
    int (*func) (void *), void *arg, const char *name)
{
	struct mcpcia_config *ccp = ccv;
	void *cookie;
	int irq;

	irq = ih & 0x3ff;
	if (irq >= NIRQ)
		panic("dec_kn300_intr_establish: bogus kn300 IRQ 0x%x", irq);

	cookie = alpha_shared_intr_establish(kn300_pci_intr, irq, IST_LEVEL,
	    level, func, arg, "kn300 irq");

	if (cookie != NULL &&
	    alpha_shared_intr_firstactive(kn300_pci_intr, irq)) {
		scb_set(MCPCIA_VEC_PCI + SCB_IDXTOVEC(irq),
		    kn300_iointr, NULL);
		alpha_shared_intr_set_private(kn300_pci_intr, irq, ccp);
		savirqs[irq] = (ih >> 11) & 0x1f;
		kn300_enable_intr(ccp, savirqs[irq]);
		alpha_mb();
	}
	return (cookie);
}

void
dec_kn300_intr_disestablish(void *ccv, void *cookie)
{
	panic("dec_kn300_intr_disestablish not implemented");
}

void
kn300_iointr(void *arg, unsigned long vec)
{
	struct mcpcia_softc *mcp;
	u_long irq;

	irq = SCB_VECTOIDX(vec - MCPCIA_VEC_PCI);

	if (alpha_shared_intr_dispatch(kn300_pci_intr, irq)) {
		/*
		 * Any claim of an interrupt at this level is a hint to
		 * reset the stray interrupt count- elsewise a slow leak
		 * over time will cause this level to be shutdown.
		 */
		alpha_shared_intr_reset_strays(kn300_pci_intr, irq);
		return;
	}

	/*
	 * If we haven't finished configuring yet, or there is no mcp
	 * registered for this level yet, just return.
	 */
	mcp = alpha_shared_intr_get_private(kn300_pci_intr, irq);
	if (mcp == NULL || mcp->mcpcia_cc == NULL)
		return;

	/*
	 * We're getting an interrupt for a device we haven't enabled.
	 * We had better not try and use -1 to find the right bit to disable.
	 */
	if (savirqs[irq] == -1) {
		printf("kn300_iointr: stray interrupt vector 0x%lx\n", vec);
		return;
	}

	/*
	 * Stray interrupt; disable the IRQ on the appropriate MCPCIA
	 * if we've reached the limit.
	 */
	alpha_shared_intr_stray(kn300_pci_intr, irq, "kn300 irq");
	if (ALPHA_SHARED_INTR_DISABLE(kn300_pci_intr, irq) == 0)
		return;
	kn300_disable_intr(mcp->mcpcia_cc, savirqs[irq]);
}

void
kn300_enable_intr(struct mcpcia_config *ccp, int irq)
{
	alpha_mb();
	REGVAL(MCPCIA_INT_MASK0(ccp)) |= (1 << irq);
	alpha_mb();
}

void
kn300_disable_intr(struct mcpcia_config *ccp, int irq)
{
	alpha_mb();
	REGVAL(MCPCIA_INT_MASK0(ccp)) &= ~(1 << irq);
	alpha_mb();
}
