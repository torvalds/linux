/* $OpenBSD: acpipwrres.c,v 1.16 2024/08/12 17:24:58 kettenis Exp $ */

/*
 * Copyright (c) 2013 Martin Pieuchot <mpi@openbsd.org>
 * Copyright (c) 2009 Paul Irofti <paul@irofti.net>
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
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

int	acpipwrres_match(struct device *, void *, void *);
void	acpipwrres_attach(struct device *, struct device *, void *);

#ifdef ACPIPWRRES_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

struct acpipwrres_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	SIMPLEQ_HEAD(, acpipwrres_consumer)	sc_cons;
	int					sc_cons_ref;

	int	sc_level;
	int	sc_order;
	int	sc_state;
#define ACPIPWRRES_OFF		0
#define ACPIPWRRES_ON		1
#define ACPIPWRRES_UNK		-1
};

struct acpipwrres_consumer {
	struct aml_node				*cs_node;
	SIMPLEQ_ENTRY(acpipwrres_consumer)	cs_next;
};

const struct cfattach acpipwrres_ca = {
	sizeof(struct acpipwrres_softc), acpipwrres_match, acpipwrres_attach
};

struct cfdriver acpipwrres_cd = {
	NULL, "acpipwrres", DV_DULL
};

int	acpipwrres_hascons(struct acpipwrres_softc *, struct aml_node *);
int	acpipwrres_addcons(struct acpipwrres_softc *, struct aml_node *);
int	acpipwrres_foundcons(struct aml_node *, void *);

int
acpipwrres_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata		*cf = match;

	if (aaa->aaa_name == NULL || strcmp(aaa->aaa_name,
	    cf->cf_driver->cd_name) != 0 || aaa->aaa_table != NULL)
		return (0);

	return (1);
}

void
acpipwrres_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpipwrres_softc		*sc = (struct acpipwrres_softc *)self;
	struct acpi_attach_args		*aaa = aux;
	struct aml_value		res;
	struct acpipwrres_consumer	*cons;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;
	memset(&res, 0, sizeof res);

	printf(": %s", sc->sc_devnode->name);

	if (!aml_evalname(sc->sc_acpi, sc->sc_devnode, "_STA", 0, NULL, &res)) {
		sc->sc_state = (int)aml_val2int(&res);
		if (sc->sc_state != ACPIPWRRES_ON &&
		    sc->sc_state != ACPIPWRRES_OFF)
			sc->sc_state = ACPIPWRRES_UNK;
	} else
		sc->sc_state = ACPIPWRRES_UNK;
	DPRINTF(("\n%s: state = %d\n", DEVNAME(sc), sc->sc_state));
	if (aml_evalnode(sc->sc_acpi, aaa->aaa_node, 0, NULL, &res) == 0) {
		sc->sc_level = res.v_powerrsrc.pwr_level;
		sc->sc_order = res.v_powerrsrc.pwr_order;
		DPRINTF(("%s: level = %d, order %d\n", DEVNAME(sc),
		    sc->sc_level, sc->sc_order));
		aml_freevalue(&res);
	}

	/* Get the list of consumers */
	SIMPLEQ_INIT(&sc->sc_cons);
#if notyet
	aml_find_node(sc->sc_acpi->sc_root, "_PRW", acpipwrres_foundcons, sc);
#endif
	aml_find_node(sc->sc_acpi->sc_root, "_PR0", acpipwrres_foundcons, sc);
	aml_find_node(sc->sc_acpi->sc_root, "_PR1", acpipwrres_foundcons, sc);
	aml_find_node(sc->sc_acpi->sc_root, "_PR2", acpipwrres_foundcons, sc);
	aml_find_node(sc->sc_acpi->sc_root, "_PR3", acpipwrres_foundcons, sc);

	DPRINTF(("%s", DEVNAME(sc)));
	if (!SIMPLEQ_EMPTY(&sc->sc_cons)) {
		printf(", resource for");
		SIMPLEQ_FOREACH(cons, &sc->sc_cons, cs_next)
			printf(" %s%s", cons->cs_node->name,
			   (SIMPLEQ_NEXT(cons, cs_next) == NULL) ? "" : ",");
	}
	printf("\n");
}

int
acpipwrres_ref_incr(struct acpipwrres_softc *sc, struct aml_node *node)
{
	if (!acpipwrres_hascons(sc, node))
		return (1);

	DPRINTF(("%s: dev %s ON %d\n", DEVNAME(sc), node->name,
	    sc->sc_cons_ref));

	if (sc->sc_cons_ref++ == 0) {
		aml_evalname(sc->sc_acpi, sc->sc_devnode, "_ON", 0,
		    NULL, NULL);
		sc->sc_state = ACPIPWRRES_ON;
	}

	return (0);
}

int
acpipwrres_ref_decr(struct acpipwrres_softc *sc, struct aml_node *node)
{
	if (!acpipwrres_hascons(sc, node))
		return (1);

	DPRINTF(("%s: dev %s OFF %d\n", DEVNAME(sc), node->name,
	    sc->sc_cons_ref));

	if (--sc->sc_cons_ref == 0) {
		aml_evalname(sc->sc_acpi, sc->sc_devnode, "_OFF", 0,
		    NULL, NULL);
		sc->sc_state = ACPIPWRRES_OFF;
	}

	return (0);
}

int
acpipwrres_hascons(struct acpipwrres_softc *sc, struct aml_node *node)
{
	struct acpipwrres_consumer	*cons;

	SIMPLEQ_FOREACH(cons, &sc->sc_cons, cs_next) {
		if (cons->cs_node == node)
			return (1);
	}

	return (0);
}

int
acpipwrres_addcons(struct acpipwrres_softc *sc, struct aml_node *node)
{
	struct acpipwrres_consumer	*cons;
	struct acpi_pwrres		*pr;
	int				state;

	/*
	 * Add handlers to put the device into Dx states.
	 *
	 * XXX What about PRW?
	 */
	if (strcmp(node->name, "_PR0") == 0) {
		state = ACPI_STATE_D0;
	} else if (strcmp(node->name, "_PR1") == 0) {
		state = ACPI_STATE_D1;
	} else if (strcmp(node->name, "_PR2") == 0) {
		state = ACPI_STATE_D2;
	} else if (strcmp(node->name, "_PR3") == 0) {
		state = ACPI_STATE_D3;
	} else {
		return (0);
	}

	if (!acpipwrres_hascons(sc, node->parent)) {
		cons = malloc(sizeof(*cons), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (cons == NULL)
			return (ENOMEM);

		cons->cs_node = node->parent;
		SIMPLEQ_INSERT_TAIL(&sc->sc_cons, cons, cs_next);
	}

	DPRINTF(("%s: resource for %s (D%d) \n", DEVNAME(sc),
	    node->parent->name, state));

	/*
	 * Make sure we attach only once the same Power Resource for a
	 * given state.
	 */
	SIMPLEQ_FOREACH(pr, &sc->sc_acpi->sc_pwrresdevs, p_next) {
		if (pr->p_node == node->parent &&
		    pr->p_res_state == state &&
		    pr->p_res_sc == sc) {
			DPRINTF(("error: pr for %s already set\n",
			   aml_nodename(pr->p_node)));
			return (EINVAL);
		}
	}

	pr = malloc(sizeof(struct acpi_pwrres), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pr == NULL)
		return (ENOMEM);

	pr->p_node = node->parent;
	pr->p_state = -1;
	pr->p_res_state = state;
	pr->p_res_sc = sc;

	SIMPLEQ_INSERT_TAIL(&sc->sc_acpi->sc_pwrresdevs, pr, p_next);

	return (0);
}

int
acpipwrres_foundcons(struct aml_node *node, void *arg)
{
	struct acpipwrres_softc		*sc = (struct acpipwrres_softc *)arg;
	struct aml_value		res, *ref;
	int				i = 0;

	memset(&res, 0, sizeof(res));

	if (aml_evalnode(sc->sc_acpi, node, 0, NULL, &res) == -1) {
		DPRINTF(("pwr: consumer not found\n"));
		return (1);
	}

	if (res.type != AML_OBJTYPE_PACKAGE) {
		DPRINTF(("%s: %s is not a package\n", DEVNAME(sc),
		    aml_nodename(node)));
		aml_freevalue(&res);
		return (1);
	}

	DPRINTF(("%s: node name %s\n", DEVNAME(sc), aml_nodename(node)));
	if (!strcmp(node->name, "_PRW"))
		i = 2;          /* _PRW first two values are ints */

	for (; i < res.length; i++) {
		ref = res.v_package[i];

		if (ref->type == AML_OBJTYPE_NAMEREF) {
			struct aml_node	*pnode;

			pnode = aml_searchrel(sc->sc_acpi->sc_root,
			    aml_getname(ref->v_nameref));
			if (pnode == NULL) {
				DPRINTF(("%s: device %s not found\n",
				    DEVNAME(sc), ref->v_string));
				continue;
			}
			ref = pnode->value;
		}

		if (ref->type == AML_OBJTYPE_OBJREF)
			ref = ref->v_objref.ref;

		if (ref->type != AML_OBJTYPE_POWERRSRC) {
			DPRINTF(("%s: object reference has a wrong type (%d)\n",
			    DEVNAME(sc), ref->type));
			continue;
		}

		if (ref->node == sc->sc_devnode)
			(void)acpipwrres_addcons(sc, node);
	}
	aml_freevalue(&res);

	return (0);
}
