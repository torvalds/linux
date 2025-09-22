/*	$OpenBSD: pchtemp.c,v 1.7 2022/03/11 18:00:51 mpi Exp $	*/
/*
 * Copyright (c) 2015 Mark Kettenis
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

/*
 * Intel X99, C610, 9 Series and 100 Series PCH thermal sensor
 * controller driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define PCHTEMP_PCI_TBAR	0x10

#define PCHTEMP_TEMP		0x00
#define PCHTEMP_TSEL		0x08
#define  PCHTEMP_TSEL_ETS	0x01

struct pchtemp_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
	bus_size_t		sc_mems;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
};

int	pchtemp_match(struct device *, void *, void *);
void	pchtemp_attach(struct device *, struct device *, void *);
void	pchtemp_refresh(void *);

struct cfdriver pchtemp_cd = {
	NULL, "pchtemp", DV_DULL
};

const struct cfattach pchtemp_ca = {
	sizeof(struct pchtemp_softc), pchtemp_match, pchtemp_attach
};

const struct pci_matchid pchtemp_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C610_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_8SERIES_LP_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_9SERIES_LP_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_THERM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_THERM },
};

int
pchtemp_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, pchtemp_devices, nitems(pchtemp_devices)));
}

void
pchtemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct pchtemp_softc *sc = (struct pchtemp_softc *)self;
	struct pci_attach_args *pa = aux;
	pcireg_t memtype;
	uint8_t tsel;

	memtype = PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT;
	if (pci_mapreg_map(pa, PCHTEMP_PCI_TBAR, memtype, 0,
	    &sc->sc_memt, &sc->sc_memh, NULL, &sc->sc_mems, 0)) {
		printf(": can't map registers\n");
		return;
	}

	tsel = bus_space_read_1(sc->sc_memt, sc->sc_memh, PCHTEMP_TSEL);
	if ((tsel & PCHTEMP_TSEL_ETS) == 0) {
		printf(": disabled\n");
		goto unmap;
	}

	pchtemp_refresh(sc);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor.type = SENSOR_TEMP;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

	if (sensor_task_register(sc, pchtemp_refresh, 5) == NULL) {
		printf(": can't register update task\n");
		goto unmap;
	}

	sensordev_install(&sc->sc_sensordev);

	printf("\n");
	return;

 unmap:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
}

void
pchtemp_refresh(void *arg)
{
	struct pchtemp_softc *sc = arg;
	uint16_t temp;

	temp = bus_space_read_2(sc->sc_memt, sc->sc_memh, PCHTEMP_TEMP);
	sc->sc_sensor.value = (temp * 500000 - 50000000) + 273150000;
}
