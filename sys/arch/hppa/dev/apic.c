/*	$OpenBSD: apic.c,v 1.19 2018/05/14 13:54:39 kettenis Exp $	*/

/*
 * Copyright (c) 2005 Michael Shalayeff
 * Copyright (c) 2007 Mark Kettenis
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/pdc.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <hppa/dev/elroyreg.h>
#include <hppa/dev/elroyvar.h>

#define APIC_INT_LINE_MASK	0x0000ff00
#define APIC_INT_LINE_SHIFT	8
#define APIC_INT_IRQ_MASK	0x0000001f

#define APIC_INT_LINE(x) (((x) & APIC_INT_LINE_MASK) >> APIC_INT_LINE_SHIFT)
#define APIC_INT_IRQ(x) ((x) & APIC_INT_IRQ_MASK)

/*
 * Interrupt types match the Intel MP Specification.
 */

#define MPS_INTPO_DEF		0
#define MPS_INTPO_ACTHI		1
#define MPS_INTPO_ACTLO		3
#define MPS_INTPO_SHIFT		0
#define MPS_INTPO_MASK		3

#define MPS_INTTR_DEF		0
#define MPS_INTTR_EDGE		1
#define MPS_INTTR_LEVEL		3
#define MPS_INTTR_SHIFT		2
#define MPS_INTTR_MASK		3

#define MPS_INT(p,t) \
    ((((p) & MPS_INTPO_MASK) << MPS_INTPO_SHIFT) | \
     (((t) & MPS_INTTR_MASK) << MPS_INTTR_SHIFT))

struct apic_iv {
	struct elroy_softc *sc;
	pci_intr_handle_t ih;
	int (*handler)(void *);
	void *arg;
	struct apic_iv *next;
	struct evcount *cnt;
};

struct apic_iv *apic_intr_list[CPU_NINTS];

void	apic_get_int_tbl(struct elroy_softc *);
u_int32_t apic_get_int_ent0(struct elroy_softc *, int);
#ifdef DEBUG
void	apic_dump(struct elroy_softc *);
#endif

void		apic_write(volatile struct elroy_regs *r, u_int32_t reg,
		    u_int32_t val);
u_int32_t	apic_read(volatile struct elroy_regs *r, u_int32_t reg);

void
apic_write(volatile struct elroy_regs *r, u_int32_t reg, u_int32_t val)
{
	elroy_write32(&r->apic_addr, htole32(reg));
	elroy_write32(&r->apic_data, htole32(val));
	elroy_read32(&r->apic_data);
}

u_int32_t
apic_read(volatile struct elroy_regs *r, u_int32_t reg)
{
	elroy_write32(&r->apic_addr, htole32(reg));
	return letoh32(elroy_read32(&r->apic_data));
}

void
apic_attach(struct elroy_softc *sc)
{
	volatile struct elroy_regs *r = sc->sc_regs;
	u_int32_t data;

	data = apic_read(r, APIC_VERSION);
	sc->sc_nints = (data & APIC_VERSION_NENT) >> APIC_VERSION_NENT_SHIFT;
	printf(" APIC ver %x, %d pins",
	    data & APIC_VERSION_MASK, sc->sc_nints);

	sc->sc_irq = mallocarray(sc->sc_nints, sizeof(int), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (sc->sc_irq == NULL)
		panic("apic_attach: cannot allocate irq table");

	apic_get_int_tbl(sc);

#ifdef DEBUG
	apic_dump(sc);
#endif
}

int
apic_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct elroy_softc *sc = pa->pa_pc->_cookie;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;
	int line;

	reg = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);
#ifdef DEBUG
	printf(" pin=%d line=%d ", PCI_INTERRUPT_PIN(reg),
	    PCI_INTERRUPT_LINE(reg));
#endif
	line = PCI_INTERRUPT_LINE(reg);
	if (sc->sc_irq[line] <= 0) {
		if ((sc->sc_irq[line] = cpu_intr_findirq()) == -1)
			return 1;
	}
	*ihp = (line << APIC_INT_LINE_SHIFT) | sc->sc_irq[line];
	return (APIC_INT_IRQ(*ihp) == 0);
}

const char *
apic_intr_string(void *v, pci_intr_handle_t ih)
{
	static char buf[32];

	snprintf(buf, 32, "line %ld irq %ld",
	    APIC_INT_LINE(ih), APIC_INT_IRQ(ih));

	return (buf);
}

void *
apic_intr_establish(void *v, pci_intr_handle_t ih,
    int pri, int (*handler)(void *), void *arg, const char *name)
{
	struct elroy_softc *sc = v;
	volatile struct elroy_regs *r = sc->sc_regs;
	hppa_hpa_t hpa = cpu_gethpa(0);
	struct evcount *cnt;
	struct apic_iv *aiv, *biv;
	void *iv;
	int irq = APIC_INT_IRQ(ih);
	int line = APIC_INT_LINE(ih);
	u_int32_t ent0;

	/* no mapping or bogus */
	if (irq <= 0 || irq > 31)
		return (NULL);

	aiv = malloc(sizeof(struct apic_iv), M_DEVBUF, M_NOWAIT);
	if (aiv == NULL)
		return (NULL);

	cnt = malloc(sizeof(struct evcount), M_DEVBUF, M_NOWAIT);
	if (!cnt) {
		free(aiv, M_DEVBUF, sizeof *aiv);
		return (NULL);
	}

	aiv->sc = sc;
	aiv->ih = ih;
	aiv->handler = handler;
	aiv->arg = arg;
	aiv->next = NULL;
	aiv->cnt = cnt;

	evcount_attach(cnt, name, NULL);

	if (apic_intr_list[irq]) {
		biv = apic_intr_list[irq];
		while (biv->next)
			biv = biv->next;
		biv->next = aiv;
		return (arg);
	}

	if ((iv = cpu_intr_establish(pri, irq, apic_intr, aiv, NULL))) {
		ent0 = (31 - irq) & APIC_ENT0_VEC;
		ent0 |= apic_get_int_ent0(sc, line);
#if 0
		if (cold) {
			sc->sc_imr |= (1 << irq);
			ent0 |= APIC_ENT0_MASK;
		}
#endif
		apic_write(sc->sc_regs, APIC_ENT0(line), APIC_ENT0_MASK);
		apic_write(sc->sc_regs, APIC_ENT1(line),
		    ((hpa & 0x0ff00000) >> 4) | ((hpa & 0x000ff000) << 12));
		apic_write(sc->sc_regs, APIC_ENT0(line), ent0);

		/* Signal EOI. */
		elroy_write32(&r->apic_eoi,
		    htole32((31 - irq) & APIC_ENT0_VEC));

		apic_intr_list[irq] = aiv;
	}

	return (arg);
}

void
apic_intr_disestablish(void *v, void *cookie)
{
}

int
apic_intr(void *v)
{
	struct apic_iv *iv = v;
	struct elroy_softc *sc = iv->sc;
	volatile struct elroy_regs *r = sc->sc_regs;
	pci_intr_handle_t ih = iv->ih;
	int claimed = 0;

	while (iv) {
		claimed = iv->handler(iv->arg);
		if (claimed != 0 && iv->cnt)
			iv->cnt->ec_count++;
		if (claimed == 1)
			break;
		iv = iv->next;
	}

	/* Signal EOI. */
	elroy_write32(&r->apic_eoi,
	    htole32((31 - APIC_INT_IRQ(ih)) & APIC_ENT0_VEC));

	return (claimed);
}

/* Maximum number of supported interrupt routing entries. */
#define MAX_INT_TBL_SZ	16

void
apic_get_int_tbl(struct elroy_softc *sc)
{
	struct pdc_pat_io_num int_tbl_sz PDC_ALIGNMENT;
	struct pdc_pat_pci_rt int_tbl[MAX_INT_TBL_SZ] PDC_ALIGNMENT;
	size_t size;

	/*
	 * XXX int_tbl should not be allocated on the stack, but we need a
	 * 1:1 mapping, and malloc doesn't provide that.
	 */

	if (pdc_call((iodcio_t)pdc, 0, PDC_PCI_INDEX, PDC_PCI_GET_INT_TBL_SZ,
	    &int_tbl_sz, 0, 0, 0, 0, 0))
		return;

	if (int_tbl_sz.num > MAX_INT_TBL_SZ)
		panic("interrupt routing table too big (%d entries)",
		    int_tbl_sz.num);

	size = int_tbl_sz.num * sizeof(struct pdc_pat_pci_rt);
	sc->sc_int_tbl_sz = int_tbl_sz.num;
	sc->sc_int_tbl = malloc(size, M_DEVBUF, M_NOWAIT);
	if (sc->sc_int_tbl == NULL)
		return;

	if (pdc_call((iodcio_t)pdc, 0, PDC_PCI_INDEX, PDC_PCI_GET_INT_TBL,
	    &int_tbl_sz, 0, &int_tbl, 0, 0, 0))
		return;

	memcpy(sc->sc_int_tbl, int_tbl, size);
}

u_int32_t
apic_get_int_ent0(struct elroy_softc *sc, int line)
{
	volatile struct elroy_regs *r = sc->sc_regs;
	int trigger = MPS_INT(MPS_INTPO_DEF, MPS_INTTR_DEF);
	u_int32_t ent0 = APIC_ENT0_LOW | APIC_ENT0_LEV;
	int bus, mpspo, mpstr;
	int i;

	bus = letoh32(elroy_read32(&r->busnum)) & 0xff;
	for (i = 0; i < sc->sc_int_tbl_sz; i++) {
		if (bus == sc->sc_int_tbl[i].bus &&
		    line == sc->sc_int_tbl[i].line)
			trigger = sc->sc_int_tbl[i].trigger;
	}

	mpspo = (trigger >> MPS_INTPO_SHIFT) & MPS_INTPO_MASK;
	mpstr = (trigger >> MPS_INTTR_SHIFT) & MPS_INTTR_MASK;

	switch (mpspo) {
	case MPS_INTPO_DEF:
		break;
	case MPS_INTPO_ACTHI:
		ent0 &= ~APIC_ENT0_LOW;
		break;
	case MPS_INTPO_ACTLO:
		ent0 |= APIC_ENT0_LOW;
		break;
	default:
		panic("unknown MPS interrupt polarity %d", mpspo);
	}

	switch(mpstr) {
	case MPS_INTTR_DEF:
		break;
	case MPS_INTTR_LEVEL:
		ent0 |= APIC_ENT0_LEV;
		break;
	case MPS_INTTR_EDGE:
		ent0 &= ~APIC_ENT0_LEV;
		break;
	default:
		panic("unknown MPS interrupt trigger %d", mpstr);
	}

	return ent0;
}

#ifdef DEBUG
void
apic_dump(struct elroy_softc *sc)
{
	int i;

	for (i = 0; i < sc->sc_nints; i++)
		printf("0x%04x 0x%04x\n", apic_read(sc->sc_regs, APIC_ENT0(i)),
		    apic_read(sc->sc_regs, APIC_ENT1(i)));

	for (i = 0; i < sc->sc_int_tbl_sz; i++) {
		printf("type=%x ", sc->sc_int_tbl[i].type);
		printf("len=%d ", sc->sc_int_tbl[i].len);
		printf("itype=%d ", sc->sc_int_tbl[i].itype);			
		printf("trigger=%x ", sc->sc_int_tbl[i].trigger);		
		printf("pin=%x ", sc->sc_int_tbl[i].pin);		
		printf("bus=%d ", sc->sc_int_tbl[i].bus);
		printf("line=%d ", sc->sc_int_tbl[i].line);
		printf("addr=%x\n", sc->sc_int_tbl[i].addr);
	}
}
#endif
