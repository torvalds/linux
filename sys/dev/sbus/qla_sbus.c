/*	$OpenBSD: qla_sbus.c,v 1.3 2022/03/13 13:34:54 mpi Exp $	*/
/*
 * Copyright (c) 2014 Mark Kettenis
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

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/qlareg.h>
#include <dev/ic/qlavar.h>

#define QLA_SBUS_REG_OFFSET	0x100
#define QLA_SBUS_REG_SIZE	0x300

int	qla_sbus_match(struct device *, void *, void *);
void	qla_sbus_attach(struct device *, struct device *, void *);

const struct cfattach qla_sbus_ca = {
	sizeof(struct qla_softc),
	qla_sbus_match,
	qla_sbus_attach
};

int
qla_sbus_match(struct device *parent, void *cf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp("SUNW,qlc", sa->sa_name) == 0 ||
	    strcmp("QLGC,qla", sa->sa_name) == 0)
		return 2;

	return 0;
}

void
qla_sbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct qla_softc *sc = (void *)self;
	struct sbus_attach_args *sa = aux;
	struct sparc_bus_space_tag *iot;
	bus_space_handle_t ioh;
	pcireg_t id, class;
	char devinfo[256];

	if (sa->sa_nintr < 1) {
		printf(": no interrupt\n");
		return;
	}

	if (sa->sa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	/*
	 * These cards have a standard PCI chips that sit behind an
	 * FPGA with some bridging logic.  Since the PCI bus is
	 * little-endian, we need a little-endian bus tag.  So we
	 * build one here.
	 */
	iot = malloc(sizeof(*iot), M_DEVBUF, M_NOWAIT);
	if (iot == NULL) {
		printf(": can't allocate bus tag\n");
		return;
	}
	*iot = *sa->sa_bustag;
	iot->asi = ASI_PRIMARY_LITTLE;

	if (sbus_bus_map(iot, sa->sa_slot, sa->sa_offset,
	    sa->sa_size, 0, 0, &ioh) != 0) {
		printf(": can't map registers\n");
		goto free;
	}

	/*
	 * PCI config space is mapped at the start of the SBus
	 * register space.  We use it to identify the ISP chip.
	 */
	id = bus_space_read_4(iot, ioh, PCI_ID_REG);
	class = bus_space_read_4(iot, ioh, PCI_CLASS_REG);

	pci_devinfo(id, class, 0, devinfo, sizeof(devinfo));
	printf(": %s\n", devinfo);

	switch (PCI_PRODUCT(id)) {
	case PCI_PRODUCT_QLOGIC_ISP2200:
		sc->sc_isp_gen = QLA_GEN_ISP2200;
		sc->sc_isp_type = QLA_ISP2200;
		break;

	case PCI_PRODUCT_QLOGIC_ISP2300:
		sc->sc_isp_gen = QLA_GEN_ISP23XX;
		sc->sc_isp_type = QLA_ISP2300;
		break;

	case PCI_PRODUCT_QLOGIC_ISP2312:
		sc->sc_isp_gen = QLA_GEN_ISP23XX;
		sc->sc_isp_type = QLA_ISP2312;
		break;

	default:
		printf("%s: unsupported ISP chip\n", DEVNAME(sc));
		goto unmap;
	}

	if (bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_BIO, 0,
	    qla_intr, sc, sc->sc_dev.dv_xname) == NULL) {
		printf("%s: can't establish interrupt\n", DEVNAME(sc));
		goto unmap;
	}

	if (bus_space_subregion(iot, ioh, QLA_SBUS_REG_OFFSET,
	    QLA_SBUS_REG_SIZE, &sc->sc_ioh) != 0) {
		printf("%s: can't submap registers\n", DEVNAME(sc));
		goto unmap;
	}

	sc->sc_iot = iot;
	sc->sc_ios = QLA_SBUS_REG_SIZE;
	sc->sc_dmat = sa->sa_dmatag;

	qla_attach(sc);
	return;

unmap:
	bus_space_unmap(iot, ioh, sa->sa_size);
free:
	free(iot, M_DEVBUF, 0);
}
