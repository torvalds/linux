/* $OpenBSD: acpihid.c,v 1.4 2022/05/29 22:03:44 jca Exp $ */
/*
 * ACPI HID event and 5-button array driver
 *
 * Copyright (c) 2018, 2020 joshua stein <jcs@jcs.org>
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
#include <machine/apmvar.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include "audio.h"
#include "wskbd.h"

/* #define ACPIHID_DEBUG */

#ifdef ACPIHID_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct acpihid_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
	int			sc_5_button;

	/*
	 * HEBC v1
	 * 0 - Rotation Lock, Num Lock, Home, End, Page Up, Page Down
	 * 1 - Wireless Radio Control
	 * 2 - System Power Down
	 * 3 - System Hibernate
	 * 4 - System Sleep/ System Wake
	 * 5 - Scan Next Track
	 * 6 - Scan Previous Track
	 * 7 - Stop
	 * 8 - Play/Pause
	 * 9 - Mute
	 * 10 - Volume Increment
	 * 11 - Volume Decrement
	 * 12 - Display Brightness Increment
	 * 13 - Display Brightness Decrement
	 * 14 - Lock Tablet
	 * 15 - Release Tablet
	 * 16 - Toggle Bezel
	 * 17 - 5 button array
	 * 18-31 - reserved
	 *
	 * HEBC v2
	 * 0-17 - Same as v1 version
	 * 18 – Power Button
	 * 19 - W Home Button
	 * 20 - Volume Up Button
	 * 21 - Volume Down Button
	 * 22 – Rotation Lock Button
	 * 23-31 – reserved
	 */
	uint32_t		sc_dsm_fn_mask;
};

enum {
	ACPIHID_FUNC_INVALID,
	ACPIHID_FUNC_BTNL,
	ACPIHID_FUNC_HDMM,
	ACPIHID_FUNC_HDSM,
	ACPIHID_FUNC_HDEM,
	ACPIHID_FUNC_BTNS,
	ACPIHID_FUNC_BTNE,
	ACPIHID_FUNC_HEBC_V1,
	ACPIHID_FUNC_VGBS,
	ACPIHID_FUNC_HEBC_V2,
	ACPIHID_FUNC_MAX,
};

static const char *acpihid_dsm_funcs[] = {
	NULL,
	"BTNL",
	"HDMM",
	"HDSM",
	"HDEM",
	"BTNS",
	"BTNE",
	"HEBC",
	"VGBS",
	"HEBC",
};

int	acpihid_match(struct device *, void *, void *);
void	acpihid_attach(struct device *, struct device *, void *);
void	acpihid_init_dsm(struct acpihid_softc *);
int	acpihid_button_array_enable(struct acpihid_softc *, int);
int	acpihid_eval(struct acpihid_softc *, int, int64_t, int64_t *);
int	acpihid_notify(struct aml_node *, int, void *);

#if NAUDIO > 0 && NWSKBD > 0
extern int wskbd_set_mixervolume(long, long);
#endif

const struct cfattach acpihid_ca = {
	sizeof(struct acpihid_softc),
	acpihid_match,
	acpihid_attach,
	NULL,
	NULL,
};

struct cfdriver acpihid_cd = {
	NULL, "acpihid", DV_DULL
};

const char *acpihid_hids[] = {
	"INT33D5",
	NULL
};

/* eeec56b3-4442-408f-a792-4edd4d758054 */
static uint8_t acpihid_guid[] = {
	0xB3, 0x56, 0xEC, 0xEE, 0x42, 0x44, 0x8F, 0x40,
	0xA7, 0x92, 0x4E, 0xDD, 0x4D, 0x75, 0x80, 0x54,
};

int
acpihid_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata		*cf = match;

	return (acpi_matchhids(aa, acpihid_hids, cf->cf_driver->cd_name));
}

void
acpihid_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpihid_softc	*sc = (struct acpihid_softc *)self;
	struct acpi_attach_args *aa = aux;
	uint64_t		 val;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf(": %s", sc->sc_devnode->name);

	acpihid_init_dsm(sc);

	if (acpihid_eval(sc, ACPIHID_FUNC_HDMM, 0, &val) != 0) {
		printf(", failed reading mode\n");
		return;
	} else if (val != 0) {
		printf(", unknown mode %lld\n", val);
		return;
	}

	if ((acpihid_eval(sc, ACPIHID_FUNC_HEBC_V2, 0, &val) == 0 &&
	    (val & 0x60000)) ||
	    (acpihid_eval(sc, ACPIHID_FUNC_HEBC_V1, 0, &val) == 0 &&
	    (val & 0x20000)))
		sc->sc_5_button = 1;

	aml_register_notify(sc->sc_devnode, aa->aaa_dev, acpihid_notify,
	    sc, ACPIDEV_NOPOLL);

	/* enable hid set */
	acpihid_eval(sc, ACPIHID_FUNC_HDSM, 1, NULL);

	if (sc->sc_5_button) {
		acpihid_button_array_enable(sc, 1);

		if (acpihid_eval(sc, ACPIHID_FUNC_BTNL, 0, NULL) == 0)
			printf(", 5 button array");
		else
			printf(", failed enabling HID power button");
	}

	printf("\n");
}

void
acpihid_init_dsm(struct acpihid_softc *sc)
{
	struct aml_value cmd[4], res;

	sc->sc_dsm_fn_mask = 0;

	if (!aml_searchname(sc->sc_devnode, "_DSM")) {
		DPRINTF(("%s: no _DSM support\n", sc->sc_dev.dv_xname));
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd[0].type = AML_OBJTYPE_BUFFER;
	cmd[0].v_buffer = (uint8_t *)&acpihid_guid;
	cmd[0].length = sizeof(acpihid_guid);
	/* rev */
	cmd[1].type = AML_OBJTYPE_INTEGER;
	cmd[1].v_integer = 1;
	cmd[1].length = 1;
	/* func */
	cmd[2].type = AML_OBJTYPE_INTEGER;
	cmd[2].v_integer = 0;
	cmd[2].length = 1;
	/* not used */
	cmd[3].type = AML_OBJTYPE_BUFFER;
	cmd[3].length = 0;

	if (aml_evalname(acpi_softc, sc->sc_devnode, "_DSM", 4, cmd,
	    &res)) {
		printf("%s: eval of _DSM at %s failed\n",
		    sc->sc_dev.dv_xname, aml_nodename(sc->sc_devnode));
		return;
	}

	if (res.type != AML_OBJTYPE_BUFFER) {
		printf("%s: bad _DSM result at %s: %d\n", sc->sc_dev.dv_xname,
		    aml_nodename(sc->sc_devnode), res.type);
		aml_freevalue(&res);
		return;
	}

	sc->sc_dsm_fn_mask = *res.v_buffer;
	DPRINTF(("%s: _DSM function mask 0x%x\n", sc->sc_dev.dv_xname,
	    sc->sc_dsm_fn_mask));

	aml_freevalue(&res);
}

int
acpihid_eval(struct acpihid_softc *sc, int idx, int64_t arg, int64_t *ret)
{
	struct aml_value cmd[4], pkg, *ppkg;
	int64_t tret;
	const char *dsm_func;

	if (idx <= ACPIHID_FUNC_INVALID || idx >= ACPIHID_FUNC_MAX) {
		printf("%s: _DSM func index %d out of bounds\n",
		    sc->sc_dev.dv_xname, idx);
		return 1;
	}

	dsm_func = acpihid_dsm_funcs[idx];

	DPRINTF(("%s: executing _DSM %s\n", sc->sc_dev.dv_xname, dsm_func));

	if (!(sc->sc_dsm_fn_mask & idx)) {
		DPRINTF(("%s: _DSM mask does not support %s (%d), executing "
		    "directly\n", sc->sc_dev.dv_xname, dsm_func, idx));
		goto eval_direct;
	}

	bzero(&pkg, sizeof(pkg));
	pkg.type = AML_OBJTYPE_INTEGER;
	pkg.v_integer = arg;
	pkg.length = 1;
	ppkg = &pkg;

	bzero(&cmd, sizeof(cmd));
	cmd[0].type = AML_OBJTYPE_BUFFER;
	cmd[0].v_buffer = (uint8_t *)&acpihid_guid;
	cmd[0].length = sizeof(acpihid_guid);
	/* rev */
	cmd[1].type = AML_OBJTYPE_INTEGER;
	cmd[1].v_integer = 1;
	cmd[1].length = 1;
	/* func */
	cmd[2].type = AML_OBJTYPE_INTEGER;
	cmd[2].v_integer = idx;
	cmd[2].length = 1;
	/* arg */
	cmd[3].type = AML_OBJTYPE_PACKAGE;
	cmd[3].length = 1;
	cmd[3].v_package = &ppkg;

	if (aml_evalinteger(acpi_softc, sc->sc_devnode, "_DSM", 4, cmd,
	    &tret)) {
		DPRINTF(("%s: _DSM %s failed\n", sc->sc_dev.dv_xname,
		    dsm_func));
		return 1;
	}

	DPRINTF(("%s: _DSM eval of %s succeeded\n", sc->sc_dev.dv_xname,
	    dsm_func));

	if (ret != NULL)
		*ret = tret;

	return 0;

eval_direct:
	cmd[0].type = AML_OBJTYPE_INTEGER;
	cmd[0].v_integer = arg;
	cmd[0].length = 1;

	if (aml_evalinteger(acpi_softc, sc->sc_devnode, dsm_func, 1, cmd,
	    &tret) != 0) {
		printf("%s: exec of %s failed\n", sc->sc_dev.dv_xname,
		    dsm_func);
		return 1;
	}

	if (ret != NULL)
		*ret = tret;

	return 0;
}

int
acpihid_button_array_enable(struct acpihid_softc *sc, int enable)
{
	int64_t cap;

	if (aml_evalinteger(acpi_softc, sc->sc_devnode, "BTNC", 0, NULL,
	    &cap) != 0) {
		printf("%s: failed getting button array capability\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	if (acpihid_eval(sc, ACPIHID_FUNC_BTNE, enable ? cap : 1, NULL) != 0) {
		printf("%s: failed enabling button array\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	return 0;
}

int
acpihid_notify(struct aml_node *node, int notify_type, void *arg)
{
#ifdef ACPIHID_DEBUG
	struct acpihid_softc *sc = arg;

	DPRINTF(("%s: %s: %.2x\n", sc->sc_dev.dv_xname, __func__,
	    notify_type));
#endif

	switch (notify_type) {
	case 0xc2: /* left meta press */
		break;
	case 0xc3: /* left meta release */
		break;
	case 0xc4: /* volume up press */
#if NAUDIO > 0 && NWSKBD > 0
		wskbd_set_mixervolume(1, 1);
#endif
		break;
	case 0xc5: /* volume up release */
		break;
	case 0xc6: /* volume down press */
#if NAUDIO > 0 && NWSKBD > 0
		wskbd_set_mixervolume(-1, 1);
#endif
		break;
	case 0xc7: /* volume down release */
		break;
	case 0xc8: /* rotate lock toggle press */
		break;
	case 0xc9: /* rotate lock toggle release */
		break;
	case 0xce: /* power button press */
		break;
	case 0xcf: /* power button release */
		break;
	default:
		DPRINTF(("%s: unhandled button 0x%x\n", sc->sc_dev.dv_xname,
		    notify_type));
	}

	return 0;
}
