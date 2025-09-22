/* $OpenBSD: acpitz.c,v 1.61 2025/02/05 11:03:36 kettenis Exp $ */
/*
 * Copyright (c) 2006 Can Erkin Acar <canacar@openbsd.org>
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
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
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/kthread.h>

#include <machine/bus.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <sys/sensors.h>

#define KTOC(k)			((k - 2732) / 10)
#define ACPITZ_MAX_AC		(10)
#define ACPITZ_TMP_RETRY	(3)
#define ACPITZ_UNKNOWN		(-1)

struct acpitz_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	int			sc_tmp;
	int			sc_crt;
	int			sc_hot;
	int			sc_ac[ACPITZ_MAX_AC];
	int			sc_ac_stat[ACPITZ_MAX_AC];
	int			sc_pse;
	int			sc_psv;
	int			sc_tc1;
	int			sc_tc2;
	int			sc_lasttmp;

	struct ksensor		sc_sens;
	struct ksensordev	sc_sensdev;

	struct acpi_devlist_head	sc_psl;
	struct acpi_devlist_head	sc_alx[ACPITZ_MAX_AC];
};

int	acpitz_match(struct device *, void *, void *);
void	acpitz_attach(struct device *, struct device *, void *);
int	acpitz_activate(struct device *, int);

const struct cfattach acpitz_ca = {
	sizeof(struct acpitz_softc), acpitz_match, acpitz_attach,
	NULL, acpitz_activate
};

struct cfdriver acpitz_cd = {
	NULL, "acpitz", DV_DULL
};

void	acpitz_init_perf(void *);
void	acpitz_setperf(int);
void	acpitz_refresh(void *);
int	acpitz_notify(struct aml_node *, int, void *);
int	acpitz_gettempreading(struct acpitz_softc *, char *);
int	acpitz_getreading(struct acpitz_softc *, char *);
int	acpitz_setfan(struct acpitz_softc *, int, char *);
void	acpitz_init(struct acpitz_softc *, int);

void		(*acpitz_cpu_setperf)(int);
int		acpitz_perflevel = -1;
extern void	(*cpu_setperf)(int);
extern int	perflevel;
#define PERFSTEP 10

#define ACPITZ_TRIPS	(1L << 0)
#define ACPITZ_DEVLIST	(1L << 1)
#define ACPITZ_INIT	(ACPITZ_TRIPS|ACPITZ_DEVLIST)

void
acpitz_init_perf(void *arg)
{
	if (acpitz_perflevel == -1)
		acpitz_perflevel = perflevel;

	if (cpu_setperf != acpitz_setperf) {
		acpitz_cpu_setperf = cpu_setperf;
		cpu_setperf = acpitz_setperf;
	}
}

void
acpitz_setperf(int level)
{
	extern struct acpi_softc *acpi_softc;

	if (level < 0 || level > 100)
		return;

	if (acpi_softc == NULL)
		return;
	if (acpi_softc->sc_pse && level > acpitz_perflevel)
		return;

	if (acpitz_cpu_setperf)
		acpitz_cpu_setperf(level);
}

void
acpitz_init(struct acpitz_softc *sc, int flag)
{
	int i;
	char name[5];
	struct aml_value res;

	/* Read trip points */
	if (flag & ACPITZ_TRIPS) {
		sc->sc_psv = acpitz_getreading(sc, "_PSV");
		for (i = 0; i < ACPITZ_MAX_AC; i++) {
			snprintf(name, sizeof(name), "_AC%d", i);
			sc->sc_ac[i] = acpitz_getreading(sc, name);
		}
	}

	/* Read device lists */
	if (flag & ACPITZ_DEVLIST) {
		if (!aml_evalname(sc->sc_acpi, sc->sc_devnode, "_PSL",
		     0, NULL, &res)) {
			acpi_freedevlist(&sc->sc_psl);
			acpi_getdevlist(&sc->sc_psl, sc->sc_devnode, &res, 0);
			aml_freevalue(&res);
		}
		for (i = 0; i < ACPITZ_MAX_AC; i++) {
			snprintf(name, sizeof(name), "_AL%d", i);
			if (!aml_evalname(sc->sc_acpi, sc->sc_devnode, name,
			    0, NULL, &res)) {
				acpi_freedevlist(&sc->sc_alx[i]);
				acpi_getdevlist(&sc->sc_alx[i],
				    sc->sc_devnode, &res, 0);
				aml_freevalue(&res);
			}
			/* initialize current state to unknown */
			sc->sc_ac_stat[i] = ACPITZ_UNKNOWN;
		}
	}
}

int
acpitz_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata		*cf = match;

	/* sanity */
	if (aa->aaa_name == NULL ||
	    strcmp(aa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aa->aaa_table != NULL)
		return (0);

	if (aa->aaa_node->value->type != AML_OBJTYPE_THERMZONE)
		return (0);

	return (1);
}

void
acpitz_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpitz_softc *sc = (struct acpitz_softc *)self;
	struct acpi_attach_args	*aa = aux;
	int i;
	char name[5];

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	TAILQ_INIT(&sc->sc_psl);
	for (i = 0; i < ACPITZ_MAX_AC; i++)
		TAILQ_INIT(&sc->sc_alx[i]);

	printf("\n");

	/*
	 * Preread the trip points (discard/ignore values read here as we will
	 * re-read them later)
	 */
	acpitz_gettempreading(sc, "_CRT");
	acpitz_gettempreading(sc, "_HOT");
	acpitz_gettempreading(sc, "_PSV");
	for (i = 0; i < ACPITZ_MAX_AC; i++) {
		snprintf(name, sizeof(name), "_AC%d", i);
		acpitz_getreading(sc, name);
	}

	sc->sc_lasttmp = -1;
	if ((sc->sc_tmp = acpitz_gettempreading(sc, "_TMP")) == -1) {
		dnprintf(10, "%s: failed to read _TMP\n", DEVNAME(sc));
		return;
	}

	if ((sc->sc_crt = acpitz_gettempreading(sc, "_CRT")) == -1)
		printf("%s: no critical temperature defined\n", DEVNAME(sc));
	else
		printf("%s: critical temperature is %d degC\n", DEVNAME(sc),
		    KTOC(sc->sc_crt));

	sc->sc_hot = acpitz_gettempreading(sc, "_HOT");
	sc->sc_tc1 = acpitz_getreading(sc, "_TC1");
	sc->sc_tc2 = acpitz_getreading(sc, "_TC2");

	/* get _PSL, _ALx */
	acpitz_init(sc, ACPITZ_INIT);

	dnprintf(10, "%s: _HOT: %d _TC1: %d _TC2: %d _PSV: %d _TMP: %d "
	    "_CRT: %d\n", DEVNAME(sc), sc->sc_hot, sc->sc_tc1, sc->sc_tc2,
	    sc->sc_psv, sc->sc_tmp, sc->sc_crt);

	strlcpy(sc->sc_sensdev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensdev.xname));
	strlcpy(sc->sc_sens.desc, "zone temperature",
	    sizeof(sc->sc_sens.desc));
	sc->sc_sens.type = SENSOR_TEMP;
	sensor_attach(&sc->sc_sensdev, &sc->sc_sens);
	sensordev_install(&sc->sc_sensdev);

	aml_register_notify(sc->sc_devnode, NULL,
	    acpitz_notify, sc, ACPIDEV_POLL);

	/*
	 * XXX use kthread_create_deferred to ensure we are the very last
	 * piece of code that touches this pointer after all CPUs have been
	 * fully attached
	 */
	kthread_create_deferred(acpitz_init_perf, sc);
}

int
acpitz_activate(struct device *self, int act)
{
	struct acpitz_softc	*sc = (struct acpitz_softc *)self;

	switch (act) {
	case DVACT_WAKEUP:
		acpitz_init(sc, ACPITZ_INIT);
		break;
	}
	return 0;
}

int
acpitz_setfan(struct acpitz_softc *sc, int i, char *method)
{
	struct aml_node		*node;
	struct aml_value	res1, *ref;
	char			name[8];
	int			rv = 1, x, y;
	int64_t			sta;
	struct acpi_devlist    *dl;

	dnprintf(20, "%s: acpitz_setfan(%d, %s)\n", DEVNAME(sc), i, method);

	x = 0;
	snprintf(name, sizeof(name), "_AL%d", i);
	TAILQ_FOREACH(dl, &sc->sc_alx[i], dev_link) {
		if (aml_evalname(sc->sc_acpi, dl->dev_node, "_PR0",0 , NULL,
		    &res1)) {
			printf("%s: %s[%d] _PR0 failed\n", DEVNAME(sc),
			    name, x);
			aml_freevalue(&res1);
			x++;

			/*
			 * This fan lacks the right method to operate:
			 * disabling active cooling trip points.
			 */
			sc->sc_ac[i] = -1;
			continue;
		}
		if (res1.type != AML_OBJTYPE_PACKAGE) {
			printf("%s: %s[%d] _PR0 not a package\n", DEVNAME(sc),
			    name, x);
			aml_freevalue(&res1);
			x++;
			continue;
		}
		for (y = 0; y < res1.length; y++) {
			ref = res1.v_package[y];
			if (ref->type == AML_OBJTYPE_NAMEREF) {
				node = aml_searchrel(sc->sc_devnode,
				    aml_getname(ref->v_nameref));
				if (node == NULL) {
					printf("%s: %s[%d.%d] _PR0"
					    " not a valid device\n",
					    DEVNAME(sc), name, x, y);
					continue;
				}
				ref = node->value;
			}
			if (ref->type == AML_OBJTYPE_OBJREF) {
				ref = ref->v_objref.ref;
			}
			if (ref->type != AML_OBJTYPE_DEVICE &&
			    ref->type != AML_OBJTYPE_POWERRSRC) {
				printf("%s: %s[%d.%d] _PR0 not a package\n",
				    DEVNAME(sc), name, x, y);
				continue;
			}
			if (aml_evalname(sc->sc_acpi, ref->node, method, 0,
			    NULL, NULL))
				printf("%s: %s[%d.%d] %s fails\n",
				    DEVNAME(sc), name, x, y, method);

			/* save off status of fan */
			if (aml_evalinteger(sc->sc_acpi, ref->node, "_STA", 0,
			    NULL, &sta))
				printf("%s: %s[%d.%d] _STA fails\n",
				    DEVNAME(sc), name, x, y);
			else {
				sc->sc_ac_stat[i] = sta;
			}
		}
		aml_freevalue(&res1);
		x++;
	}
	rv = 0;
	return (rv);
}

void
acpitz_refresh(void *arg)
{
	struct acpitz_softc	*sc = arg;
	int			i, trend, nperf;

	dnprintf(30, "%s: %s: refresh\n", DEVNAME(sc),
	    sc->sc_devnode->name);

	/* get _TMP and debounce the value */
	if ((sc->sc_tmp = acpitz_gettempreading(sc, "_TMP")) == -1) {
		printf("%s: %s: failed to read temp\n", DEVNAME(sc),
		    sc->sc_devnode->name);
		return;
	}
	/* critical trip points */
	if (sc->sc_crt != -1 && sc->sc_crt <= sc->sc_tmp) {
		/* do critical shutdown */
		printf("%s: critical temperature exceeded %dC, shutting "
		    "down\n", DEVNAME(sc), KTOC(sc->sc_tmp));
		prsignal(initprocess, SIGUSR2);
	}
	if (sc->sc_hot != -1 && sc->sc_hot <= sc->sc_tmp) {
		printf("%s: _HOT temperature\n", DEVNAME(sc));
		/* XXX go to S4, until then cool as hard as we can */
	}

	/* passive cooling */
	if (sc->sc_lasttmp != -1 && sc->sc_tc1 != -1 && sc->sc_tc2 != -1 &&
	    sc->sc_psv != -1) {
		dnprintf(30, "%s: passive cooling: lasttmp: %d tc1: %d "
		    "tc2: %d psv: %d\n", DEVNAME(sc), sc->sc_lasttmp,
		    sc->sc_tc1, sc->sc_tc2, sc->sc_psv);

		nperf = acpitz_perflevel;
		if (sc->sc_psv <= sc->sc_tmp) {
			/* Passive cooling enabled */
			dnprintf(1, "%s: enabling passive %d %d\n",
			    DEVNAME(sc), sc->sc_tmp, sc->sc_psv);
			if (!sc->sc_pse)
				sc->sc_acpi->sc_pse++;
			sc->sc_pse = 1;

			trend = sc->sc_tc1 * (sc->sc_tmp - sc->sc_lasttmp) +
			    sc->sc_tc2 * (sc->sc_tmp - sc->sc_psv);

			/* Depending on trend, slow down/speed up */
			if (trend > 0)
				nperf -= PERFSTEP;
			else
				nperf += PERFSTEP;
		}
		else {
			/* Passive cooling disabled, increase % */
			dnprintf(1, "%s: disabling passive %d %d\n",
			    DEVNAME(sc), sc->sc_tmp, sc->sc_psv);
			if (sc->sc_pse)
				sc->sc_acpi->sc_pse--;
			sc->sc_pse = 0;
			nperf += PERFSTEP;
		}
		if (nperf < 0)
			nperf = 0;
		else if (nperf > 100)
			nperf = 100;

		/* clamp passive cooling request */
		if (nperf > perflevel)
			nperf = perflevel;

		/* Perform CPU setperf */
		if (acpitz_cpu_setperf && nperf != acpitz_perflevel) {
			acpitz_perflevel = nperf;
			acpitz_cpu_setperf(nperf);
		}
	}
	sc->sc_lasttmp = sc->sc_tmp;

	/* active cooling */
	for (i = 0; i < ACPITZ_MAX_AC; i++) {
		if (sc->sc_ac[i] != -1 && sc->sc_ac[i] <= sc->sc_tmp) {
			/* turn on fan i */
			if (sc->sc_ac_stat[i] <= 0)
				acpitz_setfan(sc, i, "_ON_");
		} else if (sc->sc_ac[i] != -1) {
			/* turn off fan i */
			if ((sc->sc_ac_stat[i] == ACPITZ_UNKNOWN) ||
			    (sc->sc_ac_stat[i] > 0))
				acpitz_setfan(sc, i, "_OFF");
		}
	}
	sc->sc_sens.value = sc->sc_tmp * 100000 - 50000;
}

int
acpitz_getreading(struct acpitz_softc *sc, char *name)
{
	uint64_t		val;

	if (!aml_evalinteger(sc->sc_acpi, sc->sc_devnode, name, 0, NULL, &val))
		return (val);

	return (-1);
}

int
acpitz_gettempreading(struct acpitz_softc *sc, char *name)
{
	int			rv = -1, tmp = -1, i;

	for (i = 0; i < ACPITZ_TMP_RETRY; i++) {
		tmp = acpitz_getreading(sc, name);
		if (tmp == -1)
			goto out;
		if (KTOC(tmp) >= 0) {
			rv = tmp;
			break;
		} else {
			dnprintf(20, "%s: %d invalid reading on %s, "
			    "debouncing\n", DEVNAME(sc), tmp, name);
		}

		acpi_sleep(1000, "acpitz");	/* debounce: 1000 msec */
	}
	if (i >= ACPITZ_TMP_RETRY) {
		printf("%s: %s: failed to read %s\n", DEVNAME(sc),
		    sc->sc_devnode->name, name);
		goto out;
	}
 out:
	dnprintf(30, "%s: name: %s tmp: %d => %dC, rv: %d\n", DEVNAME(sc),
	    name, tmp, KTOC(tmp), rv);
	return (rv);
}

int
acpitz_notify(struct aml_node *node, int notify_type, void *arg)
{
	struct acpitz_softc	*sc = arg;

	dnprintf(10, "%s notify: %.2x %s\n", DEVNAME(sc), notify_type,
	    sc->sc_devnode->name);

	switch (notify_type) {
	case 0x80:	/* hardware notifications */
		break;
	case 0x81:	/* operating Points changed */
		acpitz_init(sc, ACPITZ_TRIPS);
		break;
	case 0x82:	/* re-evaluate thermal device list */
		acpitz_init(sc, ACPITZ_DEVLIST);
		break;
	default:
		break;
	}

	acpitz_refresh(sc);
	return (0);
}
