/* $OpenBSD: smmu_acpi.c,v 1.10 2025/08/24 19:49:16 patrick Exp $ */
/*
 * Copyright (c) 2021 Patrick Wildt <patrick@blueri.se>
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/pool.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/dsdt.h>

#include <dev/pci/pcivar.h>
#include <arm64/dev/acpiiort.h>
#include <arm64/dev/smmuvar.h>
#include <arm64/dev/smmureg.h>

struct smmu_v2_acpi_softc {
	void			*sc_gih;
};

struct smmu_v3_acpi_softc {
	void			*sc_eih;
	void			*sc_gih;
	void			*sc_pih;
};

struct smmu_acpi_softc {
	struct smmu_softc	 sc_smmu;

	union {
		struct smmu_v2_acpi_softc v2;
		struct smmu_v3_acpi_softc v3;
	};
};

int smmu_acpi_match(struct device *, void *, void *);
void smmu_acpi_attach(struct device *, struct device *, void *);

int smmu_v2_acpi_attach(struct smmu_acpi_softc *, struct acpi_iort_node *);
int smmu_v3_acpi_attach(struct smmu_acpi_softc *, struct acpi_iort_node *);

int smmu_acpi_foundqcom(struct aml_node *, void *);

const struct cfattach smmu_acpi_ca = {
	sizeof(struct smmu_acpi_softc), smmu_acpi_match, smmu_acpi_attach
};

int
smmu_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpiiort_attach_args *aia = aux;
	struct acpi_iort_node *node = aia->aia_node;

	if (node->type != ACPI_IORT_SMMU)
		return 0;

	return 1;
}

void
smmu_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct smmu_acpi_softc *asc = (struct smmu_acpi_softc *)self;
	struct smmu_softc *sc = &asc->sc_smmu;
	struct acpiiort_attach_args *aia = aux;
	struct acpi_iort_node *node = aia->aia_node;
	struct acpiiort_smmu *as;
	int ret = ENXIO;

	sc->sc_dmat = aia->aia_dmat;
	sc->sc_iot = aia->aia_memt;

	if (node->type == ACPI_IORT_SMMU)
		ret = smmu_v2_acpi_attach(asc, node);
	if (node->type == ACPI_IORT_SMMU_V3)
		ret = smmu_v3_acpi_attach(asc, node);

	if (ret)
		return;

	as = malloc(sizeof(*as), M_DEVBUF, M_WAITOK | M_ZERO);
	as->as_node = node;
	as->as_cookie = sc;
	as->as_map = smmu_device_map;
	as->as_reserve = smmu_reserve_region;
	acpiiort_smmu_register(as);
}

int
smmu_v2_acpi_attach(struct smmu_acpi_softc *asc, struct acpi_iort_node *node)
{
	struct smmu_softc *sc = &asc->sc_smmu;
	struct acpi_iort_smmu_node *smmu;
	struct acpi_iort_smmu_global_interrupt *girq;
	struct acpi_iort_smmu_context_interrupt *cirq;
	int i;

	smmu = (struct acpi_iort_smmu_node *)&node[1];

	printf(" addr 0x%llx/0x%llx", smmu->base_address, smmu->span);

	if (bus_space_map(sc->sc_iot, smmu->base_address, smmu->span,
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return EIO;
	}

	switch (smmu->model) {
	case ACPI_IORT_SMMU_V1:
	case ACPI_IORT_SMMU_CORELINK_MMU400:
	case ACPI_IORT_SMMU_CORELINK_MMU401:
		printf(": SMMUv1 is unsupported\n");
		return ENXIO;
	case ACPI_IORT_SMMU_CORELINK_MMU500:
		sc->sc_is_mmu500 = 1;
	case ACPI_IORT_SMMU_V2:
	case ACPI_IORT_SMMU_CAVIUM_THUNDERX:
		break;
	default:
		printf(": unknown model %u\n", smmu->model);
		return ENXIO;
	}

	if (smmu->flags & ACPI_IORT_SMMU_COHERENT)
		sc->sc_coherent = 1;

	/* Check for QCOM devices to enable quirk. */
	aml_find_node(acpi_softc->sc_root, "_HID", smmu_acpi_foundqcom, sc);

	/* FIXME: Don't configure on QCOM until its runtime use is fixed. */
	if (sc->sc_is_qcom) {
		printf(": disabled\n");
		return ENXIO;
	}

	if (smmu_v2_attach(sc) != 0)
		return ENXIO;

	girq = (struct acpi_iort_smmu_global_interrupt *)
	    ((char *)node + smmu->global_interrupt_offset);
	asc->v2.sc_gih = acpi_intr_establish(girq->nsgirpt_gsiv,
	    girq->nsgirpt_flags & ACPI_IORT_SMMU_INTR_EDGE ?
	    LR_EXTIRQ_MODE : 0, IPL_TTY, smmu_v2_global_irq,
	    sc, sc->sc_dev.dv_xname);
	if (asc->v2.sc_gih == NULL)
		return ENXIO;

	cirq = (struct acpi_iort_smmu_context_interrupt *)
	    ((char *)node + smmu->context_interrupt_offset);
	for (i = 0; i < smmu->number_of_context_interrupts; i++) {
		struct smmu_cb_irq *cbi = malloc(sizeof(*cbi),
		    M_DEVBUF, M_WAITOK);
		cbi->cbi_sc = sc;
		cbi->cbi_idx = i;
		acpi_intr_establish(cirq[i].gsiv,
		    cirq[i].flags & ACPI_IORT_SMMU_INTR_EDGE ?
		    LR_EXTIRQ_MODE : 0, IPL_TTY, smmu_v2_context_irq,
		    cbi, sc->sc_dev.dv_xname);
	}

	return 0;
}

int
smmu_acpi_foundqcom(struct aml_node *node, void *arg)
{
	struct smmu_softc	*sc = (struct smmu_softc *)arg;
	char			 cdev[32], dev[32];

	if (acpi_parsehid(node, arg, cdev, dev, sizeof(dev)) != 0)
		return 0;

	if (strcmp(dev, "QCOM0409") == 0 || /* SC8180X/XP */
	    strcmp(dev, "QCOM0609") == 0 || /* SC8280XP */
	    strcmp(dev, "QCOM0809") == 0 || /* SC7180 */
	    strcmp(dev, "QCOM0C09") == 0) /* X1E80100 */
		sc->sc_is_qcom = 1;

	return 0;
}

int
smmu_v3_acpi_attach(struct smmu_acpi_softc *asc, struct acpi_iort_node *node)
{
	struct smmu_softc *sc = &asc->sc_smmu;
	struct acpi_iort_smmu_v3_node *smmu;
	uint64_t span = 0x20000;

	smmu = (struct acpi_iort_smmu_v3_node *)&node[1];

	printf(" addr 0x%llx/0x%llx", smmu->base_address, span);

	if (bus_space_map(sc->sc_iot, smmu->base_address, span,
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return EIO;
	}

	switch (smmu->model) {
	case ACPI_IORT_SMMU_V3_GENERIC:
		break;
	default:
		printf(": unknown model %u\n", smmu->model);
		return ENXIO;
	}

	if (ACPI_IORT_SMMU_V3_COHACC_OVERRIDE(smmu->flags))
		sc->sc_coherent = 1;

	if (smmu_v3_attach(sc) != 0)
		return ENXIO;

	if (smmu->event)
		asc->v3.sc_eih = acpi_intr_establish(smmu->event,
		    LR_EXTIRQ_MODE, IPL_TTY, smmu_v3_event_irq,
		    sc, sc->sc_dev.dv_xname);
	if (asc->v3.sc_eih == NULL)
		return ENXIO;

	if (smmu->gerr)
		asc->v3.sc_gih = acpi_intr_establish(smmu->gerr,
		    LR_EXTIRQ_MODE, IPL_TTY, smmu_v3_gerr_irq,
		    sc, sc->sc_dev.dv_xname);
	if (asc->v3.sc_gih == NULL)
		return ENXIO;

	if (sc->v3.sc_has_pri) {
		if (smmu->pri)
			asc->v3.sc_pih = acpi_intr_establish(smmu->pri,
			    LR_EXTIRQ_MODE, IPL_TTY, smmu_v3_priq_irq,
			    sc, sc->sc_dev.dv_xname);
		if (asc->v3.sc_pih == NULL)
			return ENXIO;
	}

	return 0;
}
