/*	$OpenBSD: acpi_machdep.c,v 1.22 2024/05/22 05:51:49 jsg Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/apmvar.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/dsdt.h>

#include <arm64/dev/acpiiort.h>

#include "apm.h"

int	pwr_action = 1;

int	acpi_fdt_match(struct device *, void *, void *);
void	acpi_fdt_attach(struct device *, struct device *, void *);

const struct cfattach acpi_fdt_ca = {
	sizeof(struct acpi_softc), acpi_fdt_match, acpi_fdt_attach
};

int
acpi_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "openbsd,acpi-5.0");
}

void
acpi_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_softc *sc = (struct acpi_softc *)self;
	struct fdt_attach_args *faa = aux;
	bus_dma_tag_t dmat;

	sc->sc_memt = faa->fa_iot;
	sc->sc_ci_dmat = faa->fa_dmat;

	/* Create coherent DMA tag. */
	dmat = malloc(sizeof(*sc->sc_cc_dmat), M_DEVBUF, M_WAITOK | M_ZERO);
	memcpy(dmat, faa->fa_dmat, sizeof(*dmat));
	dmat->_flags |= BUS_DMA_COHERENT;
	sc->sc_cc_dmat = dmat;

	acpi_attach_common(sc, faa->fa_reg[0].addr);
}

int
acpi_map(paddr_t pa, size_t len, struct acpi_mem_map *handle)
{
	paddr_t pgpa = trunc_page(pa);
	paddr_t endpa = round_page(pa + len);
	vaddr_t va = (vaddr_t)km_alloc(endpa - pgpa, &kv_any, &kp_none,
	    &kd_nowait);

	if (va == 0)
		return (ENOMEM);

	handle->baseva = va;
	handle->va = (u_int8_t *)(va + (pa & PGOFSET));
	handle->vsize = endpa - pgpa;
	handle->pa = pa;

	do {
		pmap_kenter_pa(va, pgpa, PROT_READ | PROT_WRITE);
		va += NBPG;
		pgpa += NBPG;
	} while (pgpa < endpa);

	return 0;
}

void
acpi_unmap(struct acpi_mem_map *handle)
{
	pmap_kremove(handle->baseva, handle->vsize);
	km_free((void *)handle->baseva, handle->vsize, &kv_any, &kp_none);
}

int
acpi_bus_space_map(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	return bus_space_map(t, addr, size, flags, bshp);
}

void
acpi_bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t size)
{
	bus_space_unmap(t, bsh, size);
}

int
acpi_acquire_glk(uint32_t *lock)
{
	/* No global lock. */
	return 1;
}

int
acpi_release_glk(uint32_t *lock)
{
	/* No global lock. */
	return 0;
}

void
acpi_attach_machdep(struct acpi_softc *sc)
{
#if NAPM > 0
	apm_setinfohook(acpi_apminfo);
#endif
}

void *
acpi_intr_establish(int irq, int flags, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct interrupt_controller *ic;
	struct machine_intr_handle *aih;
	uint32_t interrupt[3];
	void *cookie;

	extern LIST_HEAD(, interrupt_controller) interrupt_controllers;
	LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
		if (ic->ic_phandle == 1)
			break;
	}
	if (ic == NULL)
		return NULL;

	interrupt[0] = 0;
	interrupt[1] = irq - 32;
	if (flags & LR_EXTIRQ_MODE) { /* edge */
		if (flags & LR_EXTIRQ_POLARITY)
			interrupt[2] = 0x2; /* falling */
		else
			interrupt[2] = 0x1; /* rising */
	} else { /* level */
		if (flags & LR_EXTIRQ_POLARITY)
			interrupt[2] = 0x8; /* low */
		else
			interrupt[2] = 0x4; /* high */
	}

	cookie = ic->ic_establish(ic->ic_cookie, interrupt, level, NULL,
	    func, arg, (char *)name);
	if (cookie == NULL)
		return NULL;

	aih = malloc(sizeof(*aih), M_DEVBUF, M_WAITOK);
	aih->ih_ic = ic;
	aih->ih_ih = cookie;

	return aih;
}

void
acpi_intr_disestablish(void *cookie)
{
	struct machine_intr_handle *aih = cookie;
	struct interrupt_controller *ic = aih->ih_ic;

	ic->ic_disestablish(aih->ih_ih);
	free(aih, M_DEVBUF, sizeof(*aih));
}

bus_dma_tag_t
acpi_iommu_device_map(struct aml_node *node, bus_dma_tag_t dmat)
{
	return acpiiort_device_map(node, dmat);
}
