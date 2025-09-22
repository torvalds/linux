/*	$OpenBSD: kate.c,v 1.8 2022/03/11 18:00:50 mpi Exp $	*/

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

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>


/*
 * AMD NPT Family 0Fh Processors, Function 3 -- Miscellaneous Control
 */

/* Function 3 Registers */
#define K_THERMTRIP_STAT_R	0xe4
#define K_NORTHBRIDGE_CAP_R	0xe8
#define K_CPUID_FAMILY_MODEL_R	0xfc

/* Bits within Thermtrip Status Register */
#define K_THERM_SENSE_SEL	(1 << 6)
#define K_THERM_SENSE_CORE_SEL	(1 << 2)

/* Flip core and sensor selection bits */
#define K_T_SEL_C0(v)		(v |= K_THERM_SENSE_CORE_SEL)
#define K_T_SEL_C1(v)		(v &= ~(K_THERM_SENSE_CORE_SEL))
#define K_T_SEL_S0(v)		(v &= ~(K_THERM_SENSE_SEL))
#define K_T_SEL_S1(v)		(v |= K_THERM_SENSE_SEL)


/*
 * Revision Guide for AMD NPT Family 0Fh Processors, 
 * Publication # 33610, Revision 3.30, February 2008
 */
static const struct {
	const char	rev[5];
	const pcireg_t	cpuid[5];
} kate_proc[] = {
	{ "BH-F", { 0x00040FB0, 0x00040F80, 0, 0, 0 } },	/* F2 */
	{ "DH-F", { 0x00040FF0, 0x00050FF0, 0x00040FC0, 0, 0 } }, /* F2, F3 */
	{ "JH-F", { 0x00040F10, 0x00040F30, 0x000C0F10, 0, 0 } }, /* F2, F3 */
	{ "BH-G", { 0x00060FB0, 0x00060F80, 0, 0, 0 } },	/* G1, G2 */
	{ "DH-G", { 0x00070FF0, 0x00060FF0,
	    0x00060FC0, 0x00070FC0, 0 } }	/* G1, G2 */
};


struct kate_softc {
	struct device		sc_dev;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_pcitag;

	struct ksensor		sc_sensors[4];
	struct ksensordev	sc_sensordev;

	char			sc_rev;
	int8_t			sc_numsensors;
};

int	kate_match(struct device *, void *, void *);
void	kate_attach(struct device *, struct device *, void *);
void	kate_refresh(void *);

const struct cfattach kate_ca = {
	sizeof(struct kate_softc), kate_match, kate_attach
};

struct cfdriver kate_cd = {
	NULL, "kate", DV_DULL
};


int
kate_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args	*pa = aux;
#ifndef KATE_STRICT
	struct kate_softc	ks;
	struct kate_softc	*sc = &ks;
#endif /* !KATE_STRICT */
	pcireg_t		c;
	int			i, j;

	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_AMD ||
	    PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_AMD_0F_MISC)
		return 0;

	/*
	 * First, let's probe for chips at or after Revision F, which is 
	 * when the temperature readings were officially introduced.
	 */
	c = pci_conf_read(pa->pa_pc, pa->pa_tag, K_CPUID_FAMILY_MODEL_R);
	for (i = 0; i < sizeof(kate_proc) / sizeof(kate_proc[0]); i++)
		for (j = 0; kate_proc[i].cpuid[j] != 0; j++)
			if ((c & ~0xf) == kate_proc[i].cpuid[j])
				return 2;	/* supersede pchb(4) */

#ifndef KATE_STRICT
	/*
	 * If the probe above was not successful, let's try to actually
	 * read the sensors from the chip, and see if they make any sense.
	 */
	sc->sc_numsensors = 4;
	sc->sc_pc = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;
	kate_refresh(sc);
	for (i = 0; i < sc->sc_numsensors; i++)
		if (!(sc->sc_sensors[i].flags & SENSOR_FINVALID))
			return 2;	/* supersede pchb(4) */
#endif /* !KATE_STRICT */

	return 0;
}

void
kate_attach(struct device *parent, struct device *self, void *aux)
{
	struct kate_softc	*sc = (struct kate_softc *)self;
	struct pci_attach_args	*pa = aux;
	pcireg_t		c, d;
	int			i, j, cmpcap;

	c = pci_conf_read(pa->pa_pc, pa->pa_tag, K_CPUID_FAMILY_MODEL_R);
	for (i = 0; i < sizeof(kate_proc) / sizeof(kate_proc[0]) &&
	    sc->sc_rev == '\0'; i++)
		for (j = 0; kate_proc[i].cpuid[j] != 0; j++)
			if ((c & ~0xf) == kate_proc[i].cpuid[j]) {
				sc->sc_rev = kate_proc[i].rev[3];
				printf(": core rev %.4s%.1x",
				    kate_proc[i].rev, c & 0xf);
			}

	if (c != 0x0 && sc->sc_rev == '\0') {
		/* CPUID Family Model Register was introduced in Revision F */
		sc->sc_rev = 'G';	/* newer than E, assume G */
		printf(": cpuid 0x%x", c);
	}

	d = pci_conf_read(pa->pa_pc, pa->pa_tag, K_NORTHBRIDGE_CAP_R);
	cmpcap = (d >> 12) & 0x3;

	sc->sc_pc = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

#ifndef KATE_STRICT
	sc->sc_numsensors = 4;
	kate_refresh(sc);
	if (cmpcap == 0 &&
	    (sc->sc_sensors[2].flags & SENSOR_FINVALID) &&
	    (sc->sc_sensors[3].flags & SENSOR_FINVALID))
		sc->sc_numsensors = 2;
#else
	sc->sc_numsensors = cmpcap ? 4 : 2;
#endif /* !KATE_STRICT */

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < sc->sc_numsensors; i++) {
		sc->sc_sensors[i].type = SENSOR_TEMP;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	if (sensor_task_register(sc, kate_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
kate_refresh(void *arg)
{
	struct kate_softc	*sc = arg;
	struct ksensor		*s = sc->sc_sensors;
	int8_t			n = sc->sc_numsensors;
	pcireg_t		t, m;
	int			i, v;

	t = pci_conf_read(sc->sc_pc, sc->sc_pcitag, K_THERMTRIP_STAT_R);

	for (i = 0; i < n; i++) {
		switch(i) {
		case 0:
			K_T_SEL_C0(t);
			K_T_SEL_S0(t);
			break;
		case 1:
			K_T_SEL_C0(t);
			K_T_SEL_S1(t);
			break;
		case 2:
			K_T_SEL_C1(t);
			K_T_SEL_S0(t);
			break;
		case 3:
			K_T_SEL_C1(t);
			K_T_SEL_S1(t);
			break;
		}
		m = t & (K_THERM_SENSE_CORE_SEL | K_THERM_SENSE_SEL);
		pci_conf_write(sc->sc_pc, sc->sc_pcitag, K_THERMTRIP_STAT_R, t);
		t = pci_conf_read(sc->sc_pc, sc->sc_pcitag, K_THERMTRIP_STAT_R);
		v = 0x3ff & (t >> 14);
#ifdef KATE_STRICT
		if (sc->sc_rev != 'G')
			v &= ~0x3;
#endif /* KATE_STRICT */
		if ((t & (K_THERM_SENSE_CORE_SEL | K_THERM_SENSE_SEL)) == m &&
		    (v & ~0x3) != 0)
			s[i].flags &= ~SENSOR_FINVALID;
		else
			s[i].flags |= SENSOR_FINVALID;
		s[i].value = (v * 250000 - 49000000) + 273150000;
	}
}
