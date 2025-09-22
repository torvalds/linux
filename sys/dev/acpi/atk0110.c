/*	$OpenBSD: atk0110.c,v 1.20 2024/09/04 07:54:52 mglocker Exp $	*/

/*
 * Copyright (c) 2009 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

/*
 * ASUSTeK AI Booster (ACPI ATK0110).
 *
 * The driver was inspired by Takanori Watanabe's acpi_aiboost driver.
 * http://cvsweb.freebsd.org/src/sys/dev/acpi_support/acpi_aiboost.c
 *
 * Special thanks goes to Sam Fourman Jr. for providing access to several
 * ASUS boxes where the driver could be tested.
 *
 *							-- cnst.su.
 */

#define ATK_ID_MUX_HWMON	0x00000006

#define ATK_CLASS(x)		(((x) >> 24) & 0xff)
#define ATK_CLASS_FREQ_CTL	3
#define ATK_CLASS_FAN_CTL	4
#define ATK_CLASS_HWMON		6
#define ATK_CLASS_MGMT		17

#define ATK_TYPE(x)		(((x) >> 16) & 0xff)
#define ATK_TYPE_VOLT		2
#define ATK_TYPE_TEMP		3
#define ATK_TYPE_FAN		4

#define AIBS_MORE_SENSORS
/* #define AIBS_VERBOSE */

struct aibs_sensor {
	struct ksensor	s;
	int64_t		i;
	int64_t		l;
	int64_t		h;
	SIMPLEQ_ENTRY(aibs_sensor)	entry;
};

struct aibs_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	struct aml_node		*sc_ggrpnode;
	struct aml_node		*sc_gitmnode;
	struct aml_node		*sc_sitmnode;
	struct aml_node		*sc_rtmpnode;
	struct aml_node		*sc_rvltnode;
	struct aml_node		*sc_rfannode;

	SIMPLEQ_HEAD(, aibs_sensor)	sc_sensorlist;
	struct ksensordev	sc_sensordev;

	int			sc_mode;	/* 1 = new, 0 = old */
};

/* Command buffer used for GITM and SITM methods */
struct aibs_cmd_buffer {
	uint32_t	id;
	uint32_t	param1;
	uint32_t	param2;
};

/* Return buffer used by the GITM and SITM methods */
struct aibs_ret_buffer {
	uint32_t	flags;
	uint32_t	value;
	/* there is more stuff that is unknown */
};

int	aibs_match(struct device *, void *, void *);
void	aibs_attach(struct device *, struct device *, void *);
int	aibs_notify(struct aml_node *, int, void *);
void	aibs_refresh(void *);

void	aibs_attach_sif(struct aibs_softc *, enum sensor_type);
void	aibs_attach_new(struct aibs_softc *);
void	aibs_add_sensor(struct aibs_softc *, const char *);
void	aibs_refresh_r(struct aibs_softc *, struct aibs_sensor *);
int	aibs_getvalue(struct aibs_softc *, int64_t, int64_t *);
int	aibs_getpack(struct aibs_softc *, struct aml_node *, int64_t,
	    struct aml_value *);
void	aibs_probe(struct aibs_softc *);
int	aibs_find_cb(struct aml_node *, void *);


const struct cfattach aibs_ca = {
	sizeof(struct aibs_softc), aibs_match, aibs_attach
};

struct cfdriver aibs_cd = {
	NULL, "aibs", DV_DULL
};

static const char* aibs_hids[] = {
	"ATK0110",
	NULL
};

int
aibs_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata		*cf = match;

	return acpi_matchhids(aa, aibs_hids, cf->cf_driver->cd_name);
}

void
aibs_attach(struct device *parent, struct device *self, void *aux)
{
	struct aibs_softc	*sc = (struct aibs_softc *)self;
	struct acpi_attach_args	*aa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	SIMPLEQ_INIT(&sc->sc_sensorlist);

	aibs_probe(sc);
	printf("\n");

	if (sc->sc_mode)
		aibs_attach_new(sc);
	else {
		aibs_attach_sif(sc, SENSOR_TEMP);
		aibs_attach_sif(sc, SENSOR_FANRPM);
		aibs_attach_sif(sc, SENSOR_VOLTS_DC);
	}

	if (sc->sc_sensordev.sensors_count == 0) {
		printf("%s: no sensors found\n", DEVNAME(sc));
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	aml_register_notify(sc->sc_devnode, aa->aaa_dev,
	    aibs_notify, sc, ACPIDEV_POLL);
}

void
aibs_attach_sif(struct aibs_softc *sc, enum sensor_type st)
{
	struct aml_value	res;
	struct aml_value	**v;
	int			i, n;
	char			name[] = "?SIF";

	switch (st) {
	case SENSOR_TEMP:
		name[0] = 'T';
		break;
	case SENSOR_FANRPM:
		name[0] = 'F';
		break;
	case SENSOR_VOLTS_DC:
		name[0] = 'V';
		break;
	default:
		return;
	}

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, name, 0, NULL, &res)) {
		printf("%s: %s not found\n", DEVNAME(sc), name);
		aml_freevalue(&res);
		return;
	}
	if (res.type != AML_OBJTYPE_PACKAGE) {
		printf("%s: %s: not a package\n", DEVNAME(sc), name);
		aml_freevalue(&res);
		return;
	}
	v = res.v_package;
	if (v[0]->type != AML_OBJTYPE_INTEGER) {
		printf("%s: %s[0]: invalid type\n", DEVNAME(sc), name);
		aml_freevalue(&res);
		return;
	}

	n = v[0]->v_integer;
	if (res.length - 1 < n) {
		printf("%s: %s: invalid package\n", DEVNAME(sc), name);
		aml_freevalue(&res);
		return;
	} else if (res.length - 1 > n) {
		printf("%s: %s: malformed package: %i/%i",
		    DEVNAME(sc), name, n, res.length - 1);
#ifdef AIBS_MORE_SENSORS
		n = res.length - 1;
#endif
		printf(", assume %i\n", n);
	}
	if (n < 1) {
		printf("%s: %s: no members in the package\n",
		    DEVNAME(sc), name);
		aml_freevalue(&res);
		return;
	}

	for (i = 0, v++; i < n; i++, v++) {
		if(v[0]->type != AML_OBJTYPE_NAMEREF) {
			printf("%s: %s: %i: not a nameref: %i type\n",
			    DEVNAME(sc), name, i, v[0]->type);
			continue;
		}
		aibs_add_sensor(sc, aml_getname(v[0]->v_nameref));
	}

	aml_freevalue(&res);
}

void
aibs_attach_new(struct aibs_softc *sc)
{
	struct aml_value	res;
	int			i;

	if (aibs_getpack(sc, sc->sc_ggrpnode, ATK_ID_MUX_HWMON, &res)) {
		printf("%s: GGRP: sensor enumeration failed\n", DEVNAME(sc));
		return;
	}

	for (i = 0; i < res.length; i++) {
		struct aml_value	*r;
		r = res.v_package[i];
		if (r->type != AML_OBJTYPE_STRING) {
			printf("%s: %s: %i: not a string (type %i)\n",
			    DEVNAME(sc), "GGRP", i, r->type);
			continue;
		}
		aibs_add_sensor(sc, r->v_string);
	}
	aml_freevalue(&res);
}

void
aibs_add_sensor(struct aibs_softc *sc, const char *name)
{
	struct aml_value	 ri;
	struct aibs_sensor	*as;
	int			 len, lim1, lim2, ena;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, name,
	    0, NULL, &ri)) {
		printf("%s: aibs_add_sensor: %s not found\n",
		    DEVNAME(sc), name);
		aml_freevalue(&ri);
		return;
	}
	if (ri.type != AML_OBJTYPE_PACKAGE) {
		printf("%s: aibs_add_sensor: %s: not a package\n",
		    DEVNAME(sc), name);
		aml_freevalue(&ri);
		return;
	}
	if (sc->sc_mode) {
		len = 7;
		lim1 = 4;
		lim2 = 5;
		ena = 6;
	} else {
		len = 5;
		lim1 = 2;
		lim2 = 3;
		ena = 4;
	}

	if (ri.length != len ||
	    ri.v_package[0]->type != AML_OBJTYPE_INTEGER ||
	    ri.v_package[1]->type != AML_OBJTYPE_STRING ||
	    ri.v_package[lim1]->type != AML_OBJTYPE_INTEGER ||
	    ri.v_package[lim2]->type != AML_OBJTYPE_INTEGER ||
	    ri.v_package[ena]->type != AML_OBJTYPE_INTEGER) {
		printf("%s: aibs_add_sensor: %s: invalid package\n",
		    DEVNAME(sc), name);
		aml_freevalue(&ri);
		return;
	}
	as = malloc(sizeof(*as), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!as) {
		printf("%s: aibs_add_sensor: %s: failed to allocate sensor\n",
		    DEVNAME(sc), name);
		aml_freevalue(&ri);
		return;
	}
	as->i = ri.v_package[0]->v_integer;
	switch (ATK_TYPE(as->i)) {
	case ATK_TYPE_VOLT:
		as->s.type = SENSOR_VOLTS_DC;
		break;
	case ATK_TYPE_TEMP:
		as->s.type = SENSOR_TEMP;
		break;
	case ATK_TYPE_FAN:
		as->s.type = SENSOR_FANRPM;
		break;
	default:
		printf("%s: aibs_add_sensor: %s: unknown sensor type %llx\n",
		    DEVNAME(sc), name, ri.v_package[0]->v_integer);
		aml_freevalue(&ri);
		free(as, M_DEVBUF, sizeof(*as));
		return;
	}
	strlcpy(as->s.desc, ri.v_package[1]->v_string,
	    sizeof(as->s.desc));
	as->l = ri.v_package[lim1]->v_integer;
	if (sc->sc_mode)
		/* the second limit is a actually a range */
		as->h = as->l + ri.v_package[lim2]->v_integer;
	else
		as->h = ri.v_package[lim2]->v_integer;
#ifdef AIBS_VERBOSE
	printf("%s: %4s: %s 0x%08llx %5lli / %5lli  0x%llx\n",
	    DEVNAME(sc), name, as->s.desc, as->i, as->l, as->h,
	    ri.v_package[ena]->v_integer);
#endif
	SIMPLEQ_INSERT_TAIL(&sc->sc_sensorlist, as, entry);
	sensor_attach(&sc->sc_sensordev, &as->s);
	aml_freevalue(&ri);
	return;
}

void
aibs_refresh(void *arg)
{
	struct aibs_softc	*sc = arg;
	struct aibs_sensor	*as;

	SIMPLEQ_FOREACH(as, &sc->sc_sensorlist, entry)
		aibs_refresh_r(sc, as);
}

void
aibs_refresh_r(struct aibs_softc *sc, struct aibs_sensor *as)
{
	struct ksensor		*s = &as->s;
	int64_t			v;
	const int64_t		l = as->l, h = as->h;

	if (aibs_getvalue(sc, as->i, &v)) {
		s->flags |= SENSOR_FINVALID;
		return;
	}
	switch (s->type) {
	case SENSOR_TEMP:
		s->value = v * 100 * 1000 + 273150000;
		if (v == 0) {
			s->status = SENSOR_S_UNKNOWN;
			s->flags |= SENSOR_FINVALID;
		} else {
			if (v > h)
				s->status = SENSOR_S_CRIT;
			else if (v > l)
				s->status = SENSOR_S_WARN;
			else
				s->status = SENSOR_S_OK;
			s->flags &= ~SENSOR_FINVALID;
		}
		break;
	case SENSOR_FANRPM:
		s->value = v;
		/* some boards have strange limits for fans */
		if ((l != 0 && l < v && v < h) ||
		    (l == 0 && v > h))
			s->status = SENSOR_S_OK;
		else
			s->status = SENSOR_S_WARN;
		s->flags &= ~SENSOR_FINVALID;
		break;
	case SENSOR_VOLTS_DC:
		s->value = v * 1000;
		if (l < v && v < h)
			s->status = SENSOR_S_OK;
		else
			s->status = SENSOR_S_WARN;
		s->flags &= ~SENSOR_FINVALID;
		break;
	default:
		/* NOTREACHED */
		break;
	}
}

int
aibs_getvalue(struct aibs_softc *sc, int64_t i, int64_t *v)
{
	struct aml_node		*n = sc->sc_gitmnode;
	struct aml_value	req, res;
	struct aibs_cmd_buffer	cmd;
	struct aibs_ret_buffer	ret;
	enum aml_objecttype	type;

	if (sc->sc_mode) {
		cmd.id = i;
		cmd.param1 = 0;
		cmd.param2 = 0;
		type = req.type = AML_OBJTYPE_BUFFER;
		req.v_buffer = (uint8_t *)&cmd;
		req.length = sizeof(cmd);
	} else {
		switch (ATK_TYPE(i)) {
		case ATK_TYPE_TEMP:
			n = sc->sc_rtmpnode;
			break;
		case ATK_TYPE_FAN:
			n = sc->sc_rfannode;
			break;
		case ATK_TYPE_VOLT:
			n = sc->sc_rvltnode;
			break;
		default:
			return (-1);
		}
		type = req.type = AML_OBJTYPE_INTEGER;
		req.v_integer = i;
	}

	if (aml_evalnode(sc->sc_acpi, n, 1, &req, &res)) {
		dprintf("%s: %s: %lld: evaluation failed\n",
		    DEVNAME(sc), n->name, i);
		aml_freevalue(&res);
		return (-1);
	}
	if (res.type != type) {
		dprintf("%s: %s: %lld: not an integer: type %i\n",
		    DEVNAME(sc), n->name, i, res.type);
		aml_freevalue(&res);
		return (-1);
	}

	if (sc->sc_mode) {
		if (res.length < sizeof(ret)) {
			dprintf("%s: %s: %lld: result buffer too small\n",
			    DEVNAME(sc), n->name, i);
			aml_freevalue(&res);
			return (-1);
		}
		memcpy(&ret, res.v_buffer, sizeof(ret));
		if (ret.flags == 0) {
			dprintf("%s: %s: %lld: bad flags in result\n",
			    DEVNAME(sc), n->name, i);
			aml_freevalue(&res);
			return (-1);
		}
		*v = ret.value;
	} else {
		*v = res.v_integer;
	}
	aml_freevalue(&res);

	return (0);
}

int
aibs_getpack(struct aibs_softc *sc, struct aml_node *n, int64_t i,
    struct aml_value *res)
{
	struct aml_value	req;

	req.type = AML_OBJTYPE_INTEGER;
	req.v_integer = i;

	if (aml_evalnode(sc->sc_acpi, n, 1, &req, res)) {
		dprintf("%s: %s: %lld: evaluation failed\n",
		    DEVNAME(sc), n->name, i);
		aml_freevalue(res);
		return (-1);
	}
	if (res->type != AML_OBJTYPE_PACKAGE) {
		dprintf("%s: %s: %lld: not a package: type %i\n",
		    DEVNAME(sc), n->name, i, res->type);
		aml_freevalue(res);
		return (-1);
	}

	return (0);
}

void
aibs_probe(struct aibs_softc *sc)
{
	/*
	 * Old mode uses TSIF, VSIF, and FSIF to enumerate sensors and
	 * RTMP, RVLT, and RFAN are used to get the values.
	 * New mode uses GGRP for enumeration and GITM and SITM as accessor.
	 * If the new methods are available use them else default to old mode.
	 */
	aml_find_node(sc->sc_devnode, "RTMP", aibs_find_cb, &sc->sc_rtmpnode);
	aml_find_node(sc->sc_devnode, "RVLT", aibs_find_cb, &sc->sc_rvltnode);
	aml_find_node(sc->sc_devnode, "RFAN", aibs_find_cb, &sc->sc_rfannode);

	aml_find_node(sc->sc_devnode, "GGRP", aibs_find_cb, &sc->sc_ggrpnode);
	aml_find_node(sc->sc_devnode, "GITM", aibs_find_cb, &sc->sc_gitmnode);
	aml_find_node(sc->sc_devnode, "SITM", aibs_find_cb, &sc->sc_sitmnode);

	if (sc->sc_ggrpnode && sc->sc_gitmnode && sc->sc_sitmnode &&
	    !sc->sc_rtmpnode && !sc->sc_rvltnode && !sc->sc_rfannode)
		sc->sc_mode = 1;
}

int
aibs_find_cb(struct aml_node *node, void *arg)
{
	struct aml_node	**np = arg;

	printf(" %s", node->name);
	*np = node;
	return (1);
}

int
aibs_notify(struct aml_node *node, int notify_type, void *arg)
{
	struct aibs_softc *sc = arg;

	if (notify_type == 0x00) {
		/* Poll sensors */
		aibs_refresh(sc);
	}
	return (0);
}
