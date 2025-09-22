/*	$OpenBSD: elroyvar.h,v 1.4 2009/08/22 02:54:50 mk Exp $	*/

/*
 * Copyright (c) 2005 Michael Shalayeff
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
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/pdc.h>

struct elroy_softc {
	struct  device sc_dv;

	int sc_ver;
	hppa_hpa_t sc_hpa;
	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_bh;
	bus_dma_tag_t sc_dmat;
	volatile struct elroy_regs *sc_regs;
	bus_addr_t sc_iobase;

	u_int32_t sc_imr;
	int sc_nints;
	int *sc_irq;

	struct pdc_pat_pci_rt *sc_int_tbl;
	int sc_int_tbl_sz;

	struct hppa_pci_chipset_tag sc_pc;
	struct hppa_bus_space_tag sc_iot;
	struct hppa_bus_space_tag sc_memt;
	char sc_memexname[20];
	struct extent *sc_memex;
	struct hppa_bus_dma_tag sc_dmatag;
};

void apic_attach(struct elroy_softc *sc);
int apic_intr(void *v);
int apic_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp);
const char *apic_intr_string(void *v, pci_intr_handle_t ih);
void *apic_intr_establish(void *v, pci_intr_handle_t ih, int pri,
    int (*handler)(void *), void *arg, const char *name);
void apic_intr_disestablish(void *v, void *cookie);

void elroy_write32(volatile u_int32_t *p, u_int32_t v);
u_int32_t elroy_read32(volatile u_int32_t *p);
