/*	$NetBSD: atk0110.c,v 1.4 2010/02/11 06:54:57 cnst Exp $	*/
/*	$OpenBSD: atk0110.c,v 1.1 2009/07/23 01:38:16 cnst Exp $	*/

/*
 * Copyright (c) 2009, 2010 Constantine A. Murenin <cnst++@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <machine/_inttypes.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/stdint.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

/*
 * ASUSTeK AI Booster (ACPI ASOC ATK0110).
 *
 * This code was originally written for OpenBSD after the techniques
 * described in the Linux's asus_atk0110.c and FreeBSD's Takanori Watanabe's
 * acpi_aiboost.c were verified to be accurate on the actual hardware kindly
 * provided by Sam Fourman Jr.  It was subsequently ported from OpenBSD to
 * DragonFly BSD, to NetBSD's sysmon_envsys(9) and to FreeBSD's sysctl(9).
 *
 *				  -- Constantine A. Murenin <http://cnst.su/>
 */

#define _COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("aibs");
ACPI_SERIAL_DECL(aibs, "aibs");

#define AIBS_MORE_SENSORS
#define AIBS_VERBOSE

#define	AIBS_GROUP_SENSORS	0x06

#define AIBS_SENS_TYPE(x)	(((x) >> 16) & 0xff)
#define AIBS_SENS_TYPE_VOLT	2
#define AIBS_SENS_TYPE_TEMP	3
#define AIBS_SENS_TYPE_FAN	4

#define	AIBS_SENS_TYPE_VOLT_NAME		"volt"
#define	AIBS_SENS_TYPE_VOLT_TEMP		"temp"
#define	AIBS_SENS_TYPE_VOLT_FAN		"fan"

struct aibs_sensor {
	ACPI_INTEGER	v;
	ACPI_INTEGER	i;
	ACPI_INTEGER	l;
	ACPI_INTEGER	h;
	int		t;
};

struct aibs_softc {
	device_t		sc_dev;
	ACPI_HANDLE		sc_ah;

	struct aibs_sensor	*sc_asens_volt;
	struct aibs_sensor	*sc_asens_temp;
	struct aibs_sensor	*sc_asens_fan;
	struct aibs_sensor	*sc_asens_all;

	struct sysctl_oid	*sc_volt_sysctl;
	struct sysctl_oid	*sc_temp_sysctl;
	struct sysctl_oid	*sc_fan_sysctl;

	bool			sc_ggrp_method;
};

static int aibs_probe(device_t);
static int aibs_attach(device_t);
static int aibs_detach(device_t);
static int aibs_sysctl(SYSCTL_HANDLER_ARGS);
static int aibs_sysctl_ggrp(SYSCTL_HANDLER_ARGS);

static int aibs_attach_ggrp(struct aibs_softc *);
static int aibs_attach_sif(struct aibs_softc *, int);

static device_method_t aibs_methods[] = {
	DEVMETHOD(device_probe,		aibs_probe),
	DEVMETHOD(device_attach,	aibs_attach),
	DEVMETHOD(device_detach,	aibs_detach),
	{ NULL, NULL }
};

static driver_t aibs_driver = {
	"aibs",
	aibs_methods,
	sizeof(struct aibs_softc)
};

static devclass_t aibs_devclass;

DRIVER_MODULE(aibs, acpi, aibs_driver, aibs_devclass, NULL, NULL);
MODULE_DEPEND(aibs, acpi, 1, 1, 1);

static char* aibs_hids[] = {
	"ATK0110",
	NULL
};

static int
aibs_probe(device_t dev)
{
	int rv;

	if (acpi_disabled("aibs"))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, aibs_hids, NULL);
	if (rv <= 0 )
		device_set_desc(dev, "ASUSTeK AI Booster (ACPI ASOC ATK0110)");
	return (rv);
}

static int
aibs_attach(device_t dev)
{
	struct aibs_softc *sc = device_get_softc(dev);
	int err;

	sc->sc_dev = dev;
	sc->sc_ah = acpi_get_handle(dev);

	sc->sc_ggrp_method = false;
	err = aibs_attach_sif(sc, AIBS_SENS_TYPE_VOLT);
	if (err == 0)
		err = aibs_attach_sif(sc, AIBS_SENS_TYPE_TEMP);
	if (err == 0)
		err = aibs_attach_sif(sc, AIBS_SENS_TYPE_FAN);

	if (err == 0)
		return (0);

	/* Clean up whatever was allocated earlier. */
	if (sc->sc_volt_sysctl != NULL)
		sysctl_remove_oid(sc->sc_volt_sysctl, true, true);
	if (sc->sc_temp_sysctl != NULL)
		sysctl_remove_oid(sc->sc_temp_sysctl, true, true);
	if (sc->sc_fan_sysctl != NULL)
		sysctl_remove_oid(sc->sc_fan_sysctl, true, true);
	aibs_detach(dev);

	sc->sc_ggrp_method = true;
	err = aibs_attach_ggrp(sc);
	return (err);
}

static int
aibs_add_sensor(struct aibs_softc *sc, ACPI_OBJECT *o,
    struct aibs_sensor* sensor, const char ** descr)
{
	int		off;

	/*
	 * Packages for the old and new methods are quite
	 * similar except that the new package has two
	 * new (unknown / unused) fields after the name field.
	 */
	if (sc->sc_ggrp_method)
		off = 4;
	else
		off = 2;

	if (o->Type != ACPI_TYPE_PACKAGE) {
		device_printf(sc->sc_dev,
		    "sensor object is not a package: %i type\n",
		     o->Type);
		return (ENXIO);
	}
	if (o[0].Package.Count != (off + 3) ||
	    o->Package.Elements[0].Type != ACPI_TYPE_INTEGER ||
	    o->Package.Elements[1].Type != ACPI_TYPE_STRING ||
	    o->Package.Elements[off].Type != ACPI_TYPE_INTEGER ||
	    o->Package.Elements[off + 1].Type != ACPI_TYPE_INTEGER ||
	    o->Package.Elements[off + 2].Type != ACPI_TYPE_INTEGER) {
		device_printf(sc->sc_dev, "unexpected package content\n");
		return (ENXIO);
	}

	sensor->i = o->Package.Elements[0].Integer.Value;
	*descr = o->Package.Elements[1].String.Pointer;
	sensor->l = o->Package.Elements[off].Integer.Value;
	sensor->h = o->Package.Elements[off + 1].Integer.Value;
	/* For the new method the second value is a range size. */
	if (sc->sc_ggrp_method)
		sensor->h += sensor->l;
	sensor->t = AIBS_SENS_TYPE(sensor->i);

	switch (sensor->t) {
	case AIBS_SENS_TYPE_VOLT:
	case AIBS_SENS_TYPE_TEMP:
	case AIBS_SENS_TYPE_FAN:
		return (0);
	default:
		device_printf(sc->sc_dev, "unknown sensor type 0x%x",
		    sensor->t);
		return (ENXIO);
	}
}

static void
aibs_sensor_added(struct aibs_softc *sc, struct sysctl_oid *so,
    const char *type_name, int idx, struct aibs_sensor *sensor,
    const char *descr)
{
	char	sysctl_name[8];

	snprintf(sysctl_name, sizeof(sysctl_name), "%i", idx);
#ifdef AIBS_VERBOSE
	device_printf(sc->sc_dev, "%c%i: 0x%08jx %20s %5jd / %5jd\n",
	    type_name[0], idx,
	    (uintmax_t)sensor->i, descr, (intmax_t)sensor->l,
	    (intmax_t)sensor->h);
#endif
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->sc_dev),
	    SYSCTL_CHILDREN(so), idx, sysctl_name,
	    CTLTYPE_INT | CTLFLAG_RD, sc, (uintptr_t)sensor,
	    sc->sc_ggrp_method ? aibs_sysctl_ggrp : aibs_sysctl,
	    sensor->t == AIBS_SENS_TYPE_TEMP ? "IK" : "I", descr);
}

static int
aibs_attach_ggrp(struct aibs_softc *sc)
{
	ACPI_STATUS		s;
	ACPI_BUFFER		buf;
	ACPI_HANDLE		h;
	ACPI_OBJECT		id;
	ACPI_OBJECT		*bp;
	ACPI_OBJECT_LIST	arg;
	int			i;
	int			t, v, f;
	int			err;
	int			*s_idx;
	const char		*name;
	const char		*descr;
	struct aibs_sensor	*sensor;
	struct sysctl_oid	**so;

	/* First see if GITM is available. */
	s = AcpiGetHandle(sc->sc_ah, "GITM", &h);
	if (ACPI_FAILURE(s)) {
		if (bootverbose)
			device_printf(sc->sc_dev, "GITM not found\n");
		return (ENXIO);
	}

	/*
	 * Now call GGRP with the appropriate argument to list sensors.
	 * The method lists different groups of entities depending on
	 * the argument.
	 */
	id.Integer.Value = AIBS_GROUP_SENSORS;
	id.Type = ACPI_TYPE_INTEGER;
	arg.Count = 1;
	arg.Pointer = &id;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	buf.Pointer = NULL;
	s = AcpiEvaluateObjectTyped(sc->sc_ah, "GGRP", &arg, &buf,
	    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->sc_dev, "GGRP not found\n");
		return (ENXIO);
	}

	bp = buf.Pointer;
	sc->sc_asens_all = malloc(sizeof(*sc->sc_asens_all) * bp->Package.Count,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	v = t = f = 0;
	for (i = 0; i < bp->Package.Count; i++) {
		sensor = &sc->sc_asens_all[i];
		err = aibs_add_sensor(sc, &bp->Package.Elements[i], sensor,
		    &descr);
		if (err != 0)
			continue;

		switch (sensor->t) {
		case AIBS_SENS_TYPE_VOLT:
			name = "volt";
			so = &sc->sc_volt_sysctl;
			s_idx = &v;
			break;
		case AIBS_SENS_TYPE_TEMP:
			name = "temp";
			so = &sc->sc_temp_sysctl;
			s_idx = &t;
			break;
		case AIBS_SENS_TYPE_FAN:
			name = "fan";
			so = &sc->sc_fan_sysctl;
			s_idx = &f;
			break;
		default:
			panic("add_sensor succeeded for unknown sensor type %d",
			    sensor->t);
		}

		if (*so == NULL) {
			/* sysctl subtree for sensors of this type */
			*so = SYSCTL_ADD_NODE(device_get_sysctl_ctx(sc->sc_dev),
			    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
			    sensor->t, name, CTLFLAG_RD, NULL, NULL);
		}
		aibs_sensor_added(sc, *so, name, *s_idx, sensor, descr);
		*s_idx += 1;
	}

	AcpiOsFree(buf.Pointer);
	return (0);
}

static int
aibs_attach_sif(struct aibs_softc *sc, int st)
{
	char			name[] = "?SIF";
	ACPI_STATUS		s;
	ACPI_BUFFER		b;
	ACPI_OBJECT		*bp, *o;
	const char		*node;
	struct aibs_sensor	*as;
	struct sysctl_oid	**so;
	int			i, n;
	int err;

	switch (st) {
	case AIBS_SENS_TYPE_VOLT:
		node = "volt";
		name[0] = 'V';
		so = &sc->sc_volt_sysctl;
		break;
	case AIBS_SENS_TYPE_TEMP:
		node = "temp";
		name[0] = 'T';
		so = &sc->sc_temp_sysctl;
		break;
	case AIBS_SENS_TYPE_FAN:
		node = "fan";
		name[0] = 'F';
		so = &sc->sc_fan_sysctl;
		break;
	default:
		panic("Unsupported sensor type %d", st);
	}

	b.Length = ACPI_ALLOCATE_BUFFER;
	s = AcpiEvaluateObjectTyped(sc->sc_ah, name, NULL, &b,
	    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(s)) {
		device_printf(sc->sc_dev, "%s not found\n", name);
		return (ENXIO);
	}

	bp = b.Pointer;
	o = bp->Package.Elements;
	if (o[0].Type != ACPI_TYPE_INTEGER) {
		device_printf(sc->sc_dev, "%s[0]: invalid type\n", name);
		AcpiOsFree(b.Pointer);
		return (ENXIO);
	}

	n = o[0].Integer.Value;
	if (bp->Package.Count - 1 < n) {
		device_printf(sc->sc_dev, "%s: invalid package\n", name);
		AcpiOsFree(b.Pointer);
		return (ENXIO);
	} else if (bp->Package.Count - 1 > n) {
		int on = n;

#ifdef AIBS_MORE_SENSORS
		n = bp->Package.Count - 1;
#endif
		device_printf(sc->sc_dev, "%s: malformed package: %i/%i"
		    ", assume %i\n", name, on, bp->Package.Count - 1, n);
	}
	if (n < 1) {
		device_printf(sc->sc_dev, "%s: no members in the package\n",
		    name);
		AcpiOsFree(b.Pointer);
		return (ENXIO);
	}

	as = malloc(sizeof(*as) * n, M_DEVBUF, M_WAITOK | M_ZERO);
	switch (st) {
	case AIBS_SENS_TYPE_VOLT:
		sc->sc_asens_volt = as;
		break;
	case AIBS_SENS_TYPE_TEMP:
		sc->sc_asens_temp = as;
		break;
	case AIBS_SENS_TYPE_FAN:
		sc->sc_asens_fan = as;
		break;
	}

	/* sysctl subtree for sensors of this type */
	*so = SYSCTL_ADD_NODE(device_get_sysctl_ctx(sc->sc_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)), st,
	    node, CTLFLAG_RD, NULL, NULL);

	for (i = 0, o++; i < n; i++, o++) {
		const char	*descr;

		err = aibs_add_sensor(sc, o, &as[i], &descr);
		if (err == 0)
			aibs_sensor_added(sc, *so, node, i, &as[i], descr);
	}

	AcpiOsFree(b.Pointer);
	return (0);
}

static int
aibs_detach(device_t dev)
{
	struct aibs_softc	*sc = device_get_softc(dev);

	if (sc->sc_asens_volt != NULL)
		free(sc->sc_asens_volt, M_DEVBUF);
	if (sc->sc_asens_temp != NULL)
		free(sc->sc_asens_temp, M_DEVBUF);
	if (sc->sc_asens_fan != NULL)
		free(sc->sc_asens_fan, M_DEVBUF);
	if (sc->sc_asens_all != NULL)
		free(sc->sc_asens_all, M_DEVBUF);
	return (0);
}

#ifdef AIBS_VERBOSE
#define ddevice_printf(x...) device_printf(x)
#else
#define ddevice_printf(x...)
#endif

static int
aibs_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct aibs_softc	*sc = arg1;
	struct aibs_sensor	*sensor = (void *)(intptr_t)arg2;
	int			i = oidp->oid_number;
	ACPI_STATUS		rs;
	ACPI_OBJECT		p, *bp;
	ACPI_OBJECT_LIST	mp;
	ACPI_BUFFER		b;
	char			*name;
	ACPI_INTEGER		v, l, h;
	int			so[3];

	switch (sensor->t) {
	case AIBS_SENS_TYPE_VOLT:
		name = "RVLT";
		break;
	case AIBS_SENS_TYPE_TEMP:
		name = "RTMP";
		break;
	case AIBS_SENS_TYPE_FAN:
		name = "RFAN";
		break;
	default:
		return (ENOENT);
	}
	l = sensor->l;
	h = sensor->h;
	p.Type = ACPI_TYPE_INTEGER;
	p.Integer.Value = sensor->i;
	mp.Count = 1;
	mp.Pointer = &p;
	b.Length = ACPI_ALLOCATE_BUFFER;
	ACPI_SERIAL_BEGIN(aibs);
	rs = AcpiEvaluateObjectTyped(sc->sc_ah, name, &mp, &b,
	    ACPI_TYPE_INTEGER);
	if (ACPI_FAILURE(rs)) {
		ddevice_printf(sc->sc_dev,
		    "%s: %i: evaluation failed\n",
		    name, i);
		ACPI_SERIAL_END(aibs);
		return (EIO);
	}
	bp = b.Pointer;
	v = bp->Integer.Value;
	AcpiOsFree(b.Pointer);
	ACPI_SERIAL_END(aibs);

	switch (sensor->t) {
	case AIBS_SENS_TYPE_VOLT:
		break;
	case AIBS_SENS_TYPE_TEMP:
		v += 2731;
		l += 2731;
		h += 2731;
		break;
	case AIBS_SENS_TYPE_FAN:
		break;
	}
	so[0] = v;
	so[1] = l;
	so[2] = h;
	return (sysctl_handle_opaque(oidp, &so, sizeof(so), req));
}

static int
aibs_sysctl_ggrp(SYSCTL_HANDLER_ARGS)
{
	struct aibs_softc	*sc = arg1;
	struct aibs_sensor	*sensor = (void *)(intptr_t)arg2;
	ACPI_STATUS		rs;
	ACPI_OBJECT		p, *bp;
	ACPI_OBJECT_LIST	arg;
	ACPI_BUFFER		buf;
	ACPI_INTEGER		v, l, h;
	int			so[3];
	uint32_t		*ret;
	uint32_t		cmd[3];

	cmd[0] = sensor->i;
	cmd[1] = 0;
	cmd[2] = 0;
	p.Type = ACPI_TYPE_BUFFER;
	p.Buffer.Pointer = (void *)cmd;
	p.Buffer.Length = sizeof(cmd);
	arg.Count = 1;
	arg.Pointer = &p;
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	ACPI_SERIAL_BEGIN(aibs);
	rs = AcpiEvaluateObjectTyped(sc->sc_ah, "GITM", &arg, &buf,
	    ACPI_TYPE_BUFFER);
	ACPI_SERIAL_END(aibs);
	if (ACPI_FAILURE(rs)) {
		device_printf(sc->sc_dev, "GITM evaluation failed\n");
		return (EIO);
	}
	bp = buf.Pointer;
	if (bp->Buffer.Length < 8) {
		device_printf(sc->sc_dev, "GITM returned short buffer\n");
		return (EIO);
	}
	ret = (uint32_t *)bp->Buffer.Pointer;
	if (ret[0] == 0) {
		device_printf(sc->sc_dev, "GITM returned error status\n");
		return (EINVAL);
	}
	v = ret[1];
	AcpiOsFree(buf.Pointer);

	l = sensor->l;
	h = sensor->h;

	switch (sensor->t) {
	case AIBS_SENS_TYPE_VOLT:
		break;
	case AIBS_SENS_TYPE_TEMP:
		v += 2731;
		l += 2731;
		h += 2731;
		break;
	case AIBS_SENS_TYPE_FAN:
		break;
	}
	so[0] = v;
	so[1] = l;
	so[2] = h;
	return (sysctl_handle_opaque(oidp, &so, sizeof(so), req));
}
