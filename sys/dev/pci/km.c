/*	$OpenBSD: km.c,v 1.14 2022/03/11 18:00:50 mpi Exp $	*/

/*
 * Copyright (c) 2008 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>


/*
 * AMD Family > 10h Processors, Function 3 -- Miscellaneous Control
 */

/* Function 3 Registers */
#define KM_REP_TEMP_CONTR_R	0xa4
#define KM_THERMTRIP_STAT_R	0xe4
#define KM_NORTHBRIDGE_CAP_R	0xe8
#define KM_CPUID_FAMILY_MODEL_R	0xfc

/* Operations on Reported Temperature Control Register */
#define KM_GET_CURTMP(r)	(((r) >> 21) & 0x7ff)

/* Operations on Thermtrip Status Register */
#define KM_GET_DIODEOFFSET(r)	(((r) >> 8) & 0x7f)


struct km_softc {
	struct device		sc_dev;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_pcitag;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
};

int	km_match(struct device *, void *, void *);
void	km_attach(struct device *, struct device *, void *);
void	km_refresh(void *);

const struct cfattach km_ca = {
	sizeof(struct km_softc), km_match, km_attach
};

struct cfdriver km_cd = {
	NULL, "km", DV_DULL
};

static const struct pci_matchid km_devices[] = {
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_10_MISC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_11_MISC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_14_MISC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_15_0X_MISC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_15_1X_MISC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_16_MISC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_16_3X_MISC }
};


int
km_match(struct device *parent, void *match, void *aux)
{
	/* successful match supersedes pchb(4) */
	return pci_matchbyid((struct pci_attach_args *)aux, km_devices,
	    sizeof(km_devices) / sizeof(km_devices[0])) * 2;
}

void
km_attach(struct device *parent, struct device *self, void *aux)
{
	struct km_softc		*sc = (struct km_softc *)self;
	struct pci_attach_args	*pa = aux;

	sc->sc_pc = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor.type = SENSOR_TEMP;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

	if (sensor_task_register(sc, km_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
km_refresh(void *arg)
{
	struct km_softc	*sc = arg;
	struct ksensor	*s = &sc->sc_sensor;
	pcireg_t	r;
	int		c;

	r = pci_conf_read(sc->sc_pc, sc->sc_pcitag, KM_REP_TEMP_CONTR_R);
	c = KM_GET_CURTMP(r);
	s->value = c * 125000 + 273150000;
}
