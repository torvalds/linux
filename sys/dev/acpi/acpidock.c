/* $OpenBSD: acpidock.c,v 1.46 2022/08/10 16:58:16 patrick Exp $ */
/*
 * Copyright (c) 2006,2007 Michael Knudsen <mk@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

struct aml_nodelist {
	struct aml_node *node;
	TAILQ_ENTRY(aml_nodelist) entries;
};

int	acpidock_match(struct device *, void *, void *);
void	acpidock_attach(struct device *, struct device *, void *);

const struct cfattach acpidock_ca = {
	sizeof(struct acpidock_softc), acpidock_match, acpidock_attach
};

struct cfdriver acpidock_cd = {
	NULL, "acpidock", DV_DULL
};

int	acpidock_docklock(struct acpidock_softc *, int);
int	acpidock_dockctl(struct acpidock_softc *, int);
int	acpidock_eject(struct acpidock_softc *, struct aml_node *);
int	acpidock_notify(struct aml_node *, int, void *);
int	acpidock_status(struct acpidock_softc *);
int	acpidock_walkchildren(struct aml_node *, void *);

int	acpidock_foundejd(struct aml_node *, void *);

int
acpidock_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	 *aaa = aux;
	struct cfdata		 *cf = match;

	/* sanity */
	if (aaa->aaa_name == NULL ||
	    strcmp(aaa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aaa->aaa_table != NULL)
		return (0);

	return (1);
}

void
acpidock_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpidock_softc	*sc = (struct acpidock_softc *)self;
	struct acpi_attach_args *aa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf(": %s", sc->sc_devnode->name);

	acpidock_status(sc);
	if (sc->sc_docked == ACPIDOCK_STATUS_DOCKED) {
		acpidock_docklock(sc, 1);
		acpidock_dockctl(sc, 1);
	}

	acpidock_status(sc);
	printf("%s docked (%d)\n",
	    sc->sc_docked == ACPIDOCK_STATUS_DOCKED ? "" : " not",
	    sc->sc_sta);

	strlcpy(sc->sc_sensdev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensdev.xname));
	if (sc->sc_docked)
		strlcpy(sc->sc_sens.desc, "docked",
		    sizeof(sc->sc_sens.desc));
	else
		strlcpy(sc->sc_sens.desc, "not docked",
		    sizeof(sc->sc_sens.desc));

	sc->sc_sens.type = SENSOR_INDICATOR;
	sc->sc_sens.value = sc->sc_docked == ACPIDOCK_STATUS_DOCKED;
	sc->sc_sens.status = sc->sc_docked ? SENSOR_S_OK : SENSOR_S_UNKNOWN;
	sensor_attach(&sc->sc_sensdev, &sc->sc_sens);
	sensordev_install(&sc->sc_sensdev);

	TAILQ_INIT(&sc->sc_deps_h);
	aml_find_node(sc->sc_acpi->sc_root, "_EJD", acpidock_foundejd, sc);

	aml_register_notify(sc->sc_devnode, aa->aaa_dev,
	    acpidock_notify, sc, ACPIDEV_NOPOLL);
}

int
acpidock_status(struct acpidock_softc *sc)
{
	int64_t			sta;
	int			rv;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_STA", 0, NULL,
	    &sta) != 0) {
		sta = 0;
		rv = 0;
	} else
		rv = 1;

	sc->sc_sta = sta;
	sc->sc_docked = sc->sc_sta & STA_PRESENT;

	return (rv);
}

int
acpidock_docklock(struct acpidock_softc *sc, int lock)
{
	struct aml_value	cmd;
	struct aml_value	res;
	int			rv;

	memset(&cmd, 0, sizeof cmd);
	cmd.v_integer = lock;
	cmd.type = AML_OBJTYPE_INTEGER;
	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_LCK", 1, &cmd,
	    &res) != 0) {
		dnprintf(20, "%s: _LCK %d failed\n", DEVNAME(sc), lock);
		rv = 0;
	} else {
		dnprintf(20, "%s: _LCK %d successful\n", DEVNAME(sc), lock);
		rv = 1;
	}

	aml_freevalue(&res);

	return (rv);
}

int
acpidock_dockctl(struct acpidock_softc *sc, int dock)
{
	struct aml_value	cmd;
	struct aml_value	res;
	int			rv;

	memset(&cmd, 0, sizeof cmd);
	cmd.v_integer = dock;
	cmd.type = AML_OBJTYPE_INTEGER;
	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_DCK", 1, &cmd,
	    &res) != 0) {
		dnprintf(15, "%s: _DCK %d failed\n", DEVNAME(sc), dock);
		rv = 0;
	} else {
		dnprintf(15, "%s: _DCK %d successful\n", DEVNAME(sc), dock);
		rv = 1;
	}

	aml_freevalue(&res);

	return (rv);
}

int
acpidock_eject(struct acpidock_softc *sc, struct aml_node *node)
{
	struct aml_value	cmd;
	struct aml_value	res;
	int			rv;

	if (node != sc->sc_devnode)
		aml_notify(node, 3);

	memset(&cmd, 0, sizeof cmd);
	cmd.v_integer = 1;
	cmd.type = AML_OBJTYPE_INTEGER;
	if (aml_evalname(sc->sc_acpi, node, "_EJ0", 1, &cmd,
	    &res) != 0) {
		dnprintf(15, "%s: _EJ0 failed\n", DEVNAME(sc));
		rv = 0;
	} else {
		dnprintf(15, "%s: _EJ0 successful\n", DEVNAME(sc));
		rv = 1;
	}

	aml_freevalue(&res);

	return (rv);
}

int
acpidock_notify(struct aml_node *node, int notify_type, void *arg)
{
	struct acpidock_softc	*sc = arg;
	struct aml_nodelist	*n;

	dnprintf(5, "%s: acpidock_notify: notify %d\n", DEVNAME(sc),
	    notify_type);

	switch (notify_type) {
	case ACPIDOCK_EVENT_INSERT:
		acpidock_docklock(sc, 1);
		acpidock_dockctl(sc, 1);

		TAILQ_FOREACH_REVERSE(n, &sc->sc_deps_h, aml_nodelisth, entries)
			aml_notify(n->node, 0x00);
		break;

	case ACPIDOCK_EVENT_EJECT:
	case ACPIDOCK_EVENT_DEVCHECK:
		/* ACPI Spec says eject button press generates
		 * a Notify(Device, 1); */
		TAILQ_FOREACH(n, &sc->sc_deps_h, entries)
			acpidock_eject(sc, n->node);
		acpidock_dockctl(sc, 0);
		acpidock_docklock(sc, 0);

		/* now actually undock */
		acpidock_eject(sc, sc->sc_devnode);
		break;
	}

	acpidock_status(sc);
	sc->sc_sens.value = sc->sc_docked == ACPIDOCK_STATUS_DOCKED;
	sc->sc_sens.status = sc->sc_docked ? SENSOR_S_OK : SENSOR_S_UNKNOWN;
	if (sc->sc_docked)
		strlcpy(sc->sc_sens.desc, "docked",
		    sizeof(sc->sc_sens.desc));
	else
		strlcpy(sc->sc_sens.desc, "not docked",
		    sizeof(sc->sc_sens.desc));

	printf("%s: %s\n",
	    DEVNAME(sc), sc->sc_docked == ACPIDOCK_STATUS_DOCKED ?
	    "docked" : "undocked");

	return (0);
}

int
acpidock_walkchildren(struct aml_node *node, void *arg)
{
	struct acpidock_softc	*sc = arg;
	struct aml_nodelist	*n;

	if (node && node->value && node->value->type == AML_OBJTYPE_DEVICE) {
		n = malloc(sizeof *n, M_DEVBUF, M_WAITOK | M_ZERO);
		n->node = node;
		dnprintf(10,"%s depends on", aml_nodename(node));
		dnprintf(10,"%s\n", aml_nodename(sc->sc_devnode));
		TAILQ_INSERT_TAIL(&sc->sc_deps_h, n, entries);
	}

	return (0);
}

int
acpidock_foundejd(struct aml_node *node, void *arg)
{
	struct acpidock_softc	*sc = (struct acpidock_softc *)arg;
	struct aml_value	res;
	struct aml_node		*dock;

	dnprintf(15, "%s: %s", DEVNAME(sc), node->name);

	if (aml_evalnode(sc->sc_acpi, node, 0, NULL, &res) == -1)
		printf(": error\n");
	else {
		dock = aml_searchname(sc->sc_acpi->sc_root, res.v_string);

		if (dock == sc->sc_devnode)
			/* Add all children devices of Device containing _EJD */
			aml_walknodes(node->parent, AML_WALK_POST,
			    acpidock_walkchildren, sc);
		aml_freevalue(&res);
	}

	return (0);
}
