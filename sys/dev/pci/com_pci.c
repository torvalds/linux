/* $OpenBSD: com_pci.c,v 1.4 2024/05/24 06:02:53 jsg Exp $ */
/*
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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
#include <sys/tty.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#define com_usr 31	/* Synopsys DesignWare UART */

/* Intel Low Power Subsystem */
#define LPSS_CLK		0x200
#define  LPSS_CLK_GATE			(1 << 0)
#define  LPSS_CLK_MDIV_SHIFT		1
#define  LPSS_CLK_MDIV_MASK		0x3fff
#define  LPSS_CLK_NDIV_SHIFT		16
#define  LPSS_CLK_NDIV_MASK		0x3fff
#define  LPSS_CLK_UPDATE		(1U << 31)
#define LPSS_RESETS		0x204
#define  LPSS_RESETS_FUNC		(3 << 0)
#define  LPSS_RESETS_IDMA		(1 << 2)
#define LPSS_ACTIVELTR		0x210
#define LPSS_IDLELTR		0x214
#define  LPSS_LTR_VALUE_MASK		(0x3ff << 0)
#define  LPSS_LTR_SCALE_MASK		(0x3 << 10)
#define  LPSS_LTR_SCALE_1US		(2 << 10)
#define  LPSS_LTR_SCALE_32US		(3 << 10)
#define  LPSS_LTR_REQ			(1 << 15)
#define LPSS_SSP		0x220
#define  LPSS_SSP_DIS_DMA_FIN		(1 << 0)
#define LPSS_REMAP_ADDR		0x240
#define LPSS_CAPS		0x2fc
#define  LPSS_CAPS_TYPE_I2C		(0 << 4)
#define  LPSS_CAPS_TYPE_UART		(1 << 4)
#define  LPSS_CAPS_TYPE_SPI		(2 << 4)
#define  LPSS_CAPS_TYPE_MASK		(0xf << 4)
#define  LPSS_CAPS_NO_IDMA		(1 << 8)

#define LPSS_REG_OFF		0x200
#define LPSS_REG_SIZE		0x100
#define LPSS_REG_NUM		(LPSS_REG_SIZE / sizeof(uint32_t))

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc.sc_iot, (sc)->sc.sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc.sc_iot, (sc)->sc.sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

int	com_pci_match(struct device *, void *, void *);
void	com_pci_attach(struct device *, struct device *, void *);
int	com_pci_detach(struct device *, int);
int	com_pci_activate(struct device *, int);
int	com_pci_intr_designware(void *);

struct com_pci_softc {
	struct com_softc	 sc;
	pci_chipset_tag_t	 sc_pc;
	pcireg_t		 sc_id;

	bus_size_t		 sc_ios;
	void			*sc_ih;

	uint32_t		 sc_priv[LPSS_REG_NUM];
};

const struct cfattach com_pci_ca = {
	sizeof(struct com_pci_softc), com_pci_match,
	com_pci_attach, com_pci_detach, com_pci_activate,
};

const struct pci_matchid com_pci_ids[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_UART_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_UART_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_UART_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_UART_4 },
};

int
com_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, com_pci_ids, nitems(com_pci_ids)));
}

void
com_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct com_pci_softc *sc = (struct com_pci_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	uint64_t freq, m, n;
	uint32_t caps;

	sc->sc_pc = pa->pa_pc;
	sc->sc_id = pa->pa_id;
	sc->sc.sc_frequency = COM_FREQ;
	sc->sc.sc_uarttype = COM_UART_16550;
	sc->sc.sc_reg_width = 4;
	sc->sc.sc_reg_shift = 2;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_MEM_TYPE_64BIT, 0,
	    &sc->sc.sc_iot, &sc->sc.sc_ioh, &sc->sc.sc_iobase, &sc->sc_ios, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	/*
	 * Once we are adding non-Intel and non-LPSS device it will make
	 * sense to add a second table and use pci_matchbyid().
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL) {
		caps = HREAD4(sc, LPSS_CAPS);
		if ((caps & LPSS_CAPS_TYPE_MASK) != LPSS_CAPS_TYPE_UART) {
			bus_space_unmap(sc->sc.sc_iot, sc->sc.sc_ioh,
			    sc->sc_ios);
			printf(": not a UART\n");
			return;
		}

		HWRITE4(sc, LPSS_RESETS, 0);
		HWRITE4(sc, LPSS_RESETS, LPSS_RESETS_FUNC | LPSS_RESETS_IDMA);
		HWRITE4(sc, LPSS_REMAP_ADDR, sc->sc.sc_iobase);

		/* 100 MHz base clock */
		freq = 100 * 1000 * 1000;
		m = n = HREAD4(sc, LPSS_CLK);
		m = (m >> LPSS_CLK_MDIV_SHIFT) & LPSS_CLK_MDIV_MASK;
		n = (n >> LPSS_CLK_NDIV_SHIFT) & LPSS_CLK_NDIV_MASK;
		if (m && n) {
			freq *= m;
			freq /= n;
		}
		sc->sc.sc_frequency = freq;
	}

	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		bus_space_unmap(sc->sc.sc_iot, sc->sc.sc_ioh, sc->sc_ios);
		printf(": unable to map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_TTY,
	    com_pci_intr_designware, &sc->sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		bus_space_unmap(sc->sc.sc_iot, sc->sc.sc_ioh, sc->sc_ios);
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	com_attach_subr(&sc->sc);
}

int
com_pci_detach(struct device *self, int flags)
{
	struct com_pci_softc *sc = (struct com_pci_softc *)self;
	int rv;

	rv = com_detach(self, flags);
	if (rv != 0)
		return (rv);
	if (sc->sc_ih != NULL) {
		pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
		sc->sc_ih = NULL;
	}
	if (sc->sc_ios != 0) {
		bus_space_unmap(sc->sc.sc_iot, sc->sc.sc_ioh, sc->sc_ios);
		sc->sc_ios = 0;
	}

	return (rv);
}

int
com_pci_activate(struct device *self, int act)
{
	struct com_pci_softc *sc = (struct com_pci_softc *)self;
	int i, rv = 0;

	if (PCI_VENDOR(sc->sc_id) != PCI_VENDOR_INTEL)
		return com_activate(self, act);

	switch (act) {
	case DVACT_RESUME:
		for (i = 0; i < LPSS_REG_NUM; i++)
			HWRITE4(sc, i * sizeof(uint32_t), sc->sc_priv[i]);
		rv = com_activate(self, act);
		break;
	case DVACT_SUSPEND:
		rv = com_activate(self, act);
		for (i = 0; i < LPSS_REG_NUM; i++)
			sc->sc_priv[i] = HREAD4(sc, i * sizeof(uint32_t));
		break;
	default:
		rv = com_activate(self, act);
		break;
	}

	return (rv);
}

int
com_pci_intr_designware(void *cookie)
{
	struct com_softc *sc = cookie;

	com_read_reg(sc, com_usr);

	return comintr(sc);
}
