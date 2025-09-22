/*	$OpenBSD: ali1543.c,v 1.6 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: ali1543.c,v 1.2 2001/09/13 14:00:52 tshiozak Exp $	*/

/*
 * Copyright (c) 2001
 *       HAYAKAWA Koichi.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999, by UCHIYAMA Yasushi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/* HAYAKAWA Koichi wrote ALi 1543 PCI ICU code basing on VIA82C586 driver */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <i386/pci/pcibiosvar.h>
#include <i386/pci/piixvar.h>


int ali1543_getclink (pciintr_icu_handle_t, int, int *);
int ali1543_get_intr (pciintr_icu_handle_t, int, int *);
int ali1543_set_intr (pciintr_icu_handle_t, int, int);


const struct pciintr_icu ali1543_icu = {
	ali1543_getclink,
	ali1543_get_intr,
	ali1543_set_intr,
	piix_get_trigger,
	piix_set_trigger,
};


/*
 * Linux source code (linux/arch/i386/kernel/pci-irq.c) says that the
 * irq order of ALi PCI ICU is shuffled.
 */
static const int ali1543_intr_shuffle_get[16] = {
	0, 9, 3, 10, 4, 5, 7, 6, 1, 11, 0, 12, 0, 14, 0, 15
};
static const int ali1543_intr_shuffle_set[16] = {
	0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15
};

#define ALI1543_IRQ_MASK		0xdefa

#define ALI1543_LEGAL_LINK(link)	(((link) >= 0) && ((link) <= 7))
#define ALI1543_LEGAL_IRQ(irq)		((1 << (irq)) & ALI1543_IRQ_MASK)

#define ALI1543_INTR_CFG_REG		0x48

#define ALI1543_INTR_PIRQA_SHIFT	0
#define ALI1543_INTR_PIRQA_MASK		0x0000000f
#define ALI1543_INTR_PIRQB_SHIFT	4
#define ALI1543_INTR_PIRQB_MASK		0x000000f0
#define ALI1543_INTR_PIRQC_SHIFT	8
#define ALI1543_INTR_PIRQC_MASK		0x00000f00
#define ALI1543_INTR_PIRQD_SHIFT	12
#define ALI1543_INTR_PIRQD_MASK		0x0000f000

#define ALI1543_INTR_PIRQ_SHIFT(clink)	((clink)*4)
#define ALI1543_INTR_PIRQ_IRQ(reg, clink)				\
	(((reg) >> ((clink)*4)) & 0x0f)
#define ALI1543_PIRQ(reg, clink)					\
	ali1543_intr_shuffle_get[ALI1543_INTR_PIRQ_IRQ((reg), (clink))]


int
ali1543_init(pci_chipset_tag_t pc, bus_space_tag_t iot, pcitag_t tag,
    pciintr_icu_tag_t *ptagp, pciintr_icu_handle_t *phandp)
{

	if (piix_init(pc, iot, tag, ptagp, phandp) == 0) {
		*ptagp = &ali1543_icu;

		return (0);
	}

	return (1);
}

int
ali1543_getclink(pciintr_icu_handle_t v, int link, int *clinkp)
{

	if (ALI1543_LEGAL_LINK(link - 1)) {
		*clinkp = link - 1;
		return (0);
	}

	return (1);
}

int
ali1543_get_intr(pciintr_icu_handle_t v, int clink, int *irqp)
{
	struct piix_handle *ph = v;
	pcireg_t reg;
	int val;

	if (ALI1543_LEGAL_LINK(clink) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, ALI1543_INTR_CFG_REG);
#ifdef DEBUG_1543
	printf("ali1543: PIRQ reg 0x%08x\n", reg); /* XXX debug */
#endif /* DEBUG_1543 */
	val = ALI1543_PIRQ(reg, clink);
	*irqp = (val == 0) ?
	    I386_PCI_INTERRUPT_LINE_NO_CONNECTION : val;

	return (0);
}

int
ali1543_set_intr(pciintr_icu_handle_t v, int clink, int irq)
{
	struct piix_handle *ph = v;
	int shift, val;
	pcireg_t reg;

	if (ALI1543_LEGAL_LINK(clink) == 0 || ALI1543_LEGAL_IRQ(irq) == 0)
		return (1);

	reg = pci_conf_read(ph->ph_pc, ph->ph_tag, ALI1543_INTR_CFG_REG);
	ali1543_get_intr(v, clink, &val);
	shift = ALI1543_INTR_PIRQ_SHIFT(clink);
	reg &= ~(0x0f << shift);
	reg |= (ali1543_intr_shuffle_set[irq] << shift);
	pci_conf_write(ph->ph_pc, ph->ph_tag, ALI1543_INTR_CFG_REG, reg);
	if (ali1543_get_intr(v, clink, &val) != 0 || val != irq)
		return (1);

	return (0);
}
