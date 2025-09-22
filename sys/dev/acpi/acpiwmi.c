/*	$OpenBSD: acpiwmi.c,v 1.6 2025/07/14 23:49:08 jsg Exp $ */
/*
 * Copyright (c) 2025 Ted Unangst <tedu@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

/* #define ACPIWMI_DEBUG */

#ifdef ACPIWMI_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

struct acpiwmi_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;

	SLIST_HEAD(, wmihandler) sc_handlers;
};

struct wmihandler {
	SLIST_ENTRY(wmihandler) w_next;
	struct acpiwmi_softc *w_sc;
	int (*w_event)(struct wmihandler *, int);
	char w_method[5];
};

int  acpiwmi_match(struct device *, void *, void *);
void acpiwmi_attach(struct device *, struct device *, void *);
int  acpiwmi_notify(struct aml_node *, int, void *);

const struct cfattach acpiwmi_ca = {
	sizeof (struct acpiwmi_softc), acpiwmi_match, acpiwmi_attach,
	NULL, NULL
};

struct cfdriver acpiwmi_cd = {
	NULL, "acpiwmi", DV_DULL
};

const char *acpiwmi_hids[] = {
	"PNP0C14",
	NULL
};

int
acpiwmi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, acpiwmi_hids, cf->cf_driver->cd_name);
}

#define GUID_SIZE	16

struct guidinfo {
	uint8_t guid[GUID_SIZE];
	uint8_t oid[2];
	uint8_t maxinstance;
	uint8_t flags;
};

#define WMI_EVENT_MASK	0xffff

#define WMI_EXPENSIVE	1
#define WMI_METHOD	2
#define WMI_STRING	4
#define WMI_EVENT	8

#ifdef ACPIWMI_DEBUG
static char *
guid_string(const uint8_t *guid, char *buf)
{
	size_t space = 64; /* xxx */
	char *p = buf;
	int i = 0;

	/* 3210-54-76-89-012345 */
	for (i = 0; i < 4; i++) { // 3210
		snprintf(p, space, "%02X", guid[0 + 3 - i]);
		p += 2;
	}
	*p++ = '-';
	for (i = 0; i < 2; i++) { // 54
		snprintf(p, space, "%02X", guid[4 + 1 - i]);
		p += 2;
	}
	*p++ = '-';
	for (i = 0; i < 2; i++) { // 76
		snprintf(p, space, "%02X", guid[6 + 1 - i]);
		p += 2;
	}
	*p++ = '-';
	for (i = 0; i < 2; i++) { // 89
		snprintf(p, space, "%02X", guid[8 + i]);
		p += 2;
	}
	*p++ = '-';
	for (i = 0; i < 6; i++) { // 012345
		snprintf(p, space, "%02X", guid[10 + i]);
		p += 2;
	}
	return buf;
}
#endif

static int wmi_asus_init(struct acpiwmi_softc *, struct guidinfo *);
static int wmi_asus_event(struct wmihandler *, int);

struct wmi_target {
	uint8_t guid[GUID_SIZE];
	int (*init)(struct acpiwmi_softc *, struct guidinfo *);
};

static struct wmi_target targets[] = {
	{
		/* "97845ED0-4E6D-11DE-8A39-0800200C9A66" */
		{ 0xd0, 0x5e, 0x84, 0x97, 0x6d, 0x4e, 0xde, 0x11,
		  0x8a, 0x39, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 },
		wmi_asus_init,
	},
};

void
acpiwmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpiwmi_softc *sc = (struct acpiwmi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct aml_value res;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	SLIST_INIT(&sc->sc_handlers);

	printf(": %s", aaa->aaa_node->name);

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_WDG", 0, NULL, &res) == -1) {
        printf(": error\n");
		return;
	}
	printf("\n");

	int num = res.length / sizeof(struct guidinfo);
	struct guidinfo *guids = (struct guidinfo *)res.v_buffer;
	for (int i = 0; i < num; i++) {
#ifdef ACPIWMI_DEBUG
		char buf[64];
		DPRINTF(("method: %s\n", guid_string(guids[i].guid, buf)));
#endif
		for (int j = 0; j < 1; j++) {
			if (memcmp(guids[i].guid, targets[j].guid, GUID_SIZE) == 0)
				targets[j].init(sc, &guids[i]);
		}
	}
	aml_freevalue(&res);

	if (!SLIST_EMPTY(&sc->sc_handlers)) {
		aml_register_notify(sc->sc_node, aaa->aaa_dev,
			acpiwmi_notify, sc, ACPIDEV_NOPOLL);
	}
}

int
acpiwmi_notify(struct aml_node *node, int note, void *arg)
{
	struct aml_value input, res;
	struct acpiwmi_softc *sc = arg;
	struct wmihandler *wh;
	int code;

	memset(&input, 0, sizeof(input));
	input.type = AML_OBJTYPE_INTEGER;
	input.v_integer = note; /* ??? */
	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_WED", 1, &input, &res) == -1) {
		printf("%s: failed to evaluate _WED\n", DEVNAME(sc));
		return 0;
	}

	if (res.type != AML_OBJTYPE_INTEGER) {
		aml_freevalue(&res);
		return 0;
	}

	code = res.v_integer & WMI_EVENT_MASK;
	aml_freevalue(&res);
	SLIST_FOREACH(wh, &sc->sc_handlers, w_next)
		wh->w_event(wh, code);

	return 0;
}

static int
wmi_eval_method(struct wmihandler *wh, int32_t instance, uint32_t methodid,
	int arg0, int arg1)
{
	int args[6] = { arg0, arg1, 0 };
	struct aml_value params[3], res;
	struct acpiwmi_softc *sc = wh->w_sc;
	int rv;

	memset(&params, 0, sizeof(params));
	params[0].type = AML_OBJTYPE_INTEGER;
	params[0].v_integer = instance;
	params[1].type = AML_OBJTYPE_INTEGER;
	params[1].v_integer = methodid;
	params[2].type = AML_OBJTYPE_BUFFER;
	params[2].length = sizeof(args);
	params[2].v_buffer = (void *)args;
	if (aml_evalname(sc->sc_acpi, sc->sc_node, wh->w_method, 3, params, &res) == -1)
		return -1;
	rv = res.v_integer;
	aml_freevalue(&res);
	return rv;
}

/* ASUS related WMI code */
struct wmiasus {
	struct wmihandler	w_wmi;
	int					w_kbdlight;
	int					w_fnlock;
	int					w_perf;
	int					w_perfid;
};

#define ASUS_DSTS_PRESENCE	0x00010000

#define ASUS_METHOD_INIT	0x54494E49
#define ASUS_METHOD_DGET	0x53545344
#define ASUS_METHOD_DSET	0x53564544

#define ASUS_DEV_BATTERY	0x00120057
#define ASUS_DEV_ALS		0x00050001
#define ASUS_DEV_PERF		0x00120075
#define ASUS_DEV_PERF_2		0x00110019
#define ASUS_DEV_KBDLIGHT	0x00050021
#define ASUS_DEV_SCRLIGHT	0x00050011
#define ASUS_DEV_FNLOCK		0x00100023
#define ASUS_DEV_CAMLED		0x00060079
#define ASUS_DEV_MICLED		0x00040017

#define ASUS_FNLOCK_BIOS_DISABLED	0x1

#define ASUS_EVENT_AC_OFF		0x57
#define ASUS_EVENT_AC_ON		0x58
#define ASUS_EVENT_KBDLIGHT_UP		0xc4
#define ASUS_EVENT_KBDLIGHT_DOWN	0xc5
#define ASUS_EVENT_KBDLIGHT_TOGGLE	0xc7
#define ASUS_EVENT_FNLOCK		0x4e
#define ASUS_EVENT_PERF			0x9d
#define ASUS_EVENT_MIC			124
#define ASUS_EVENT_CAM			133

static int
asus_dev_get(struct wmiasus *wh, uint32_t devid)
{
	return wmi_eval_method(&wh->w_wmi, 0, ASUS_METHOD_DGET, devid, 0);
}
static int
asus_dev_set(struct wmiasus *wh, uint32_t devid, uint32_t val)
{
	return wmi_eval_method(&wh->w_wmi, 0, ASUS_METHOD_DSET, devid, val);
}
static void
asus_toggle(struct wmiasus *wh, int devid, int *val)
{
	int maxval = 1, mask = 0;

	switch (devid) {
	case ASUS_DEV_KBDLIGHT:
		maxval = 3;
		mask = 0x80;
		break;
	case ASUS_DEV_PERF:
	case ASUS_DEV_PERF_2:
		maxval = 2;
		break;
	}
	*val += 1;
	if (*val > maxval)
		*val = 0;
	asus_dev_set(wh, devid, *val | mask);
}

static int
wmi_asus_init(struct acpiwmi_softc *sc, struct guidinfo *ginfo)
{
	struct wmiasus *wh;
	struct aml_value on;
	char method[5];
	int res;

	wh = malloc(sizeof(*wh), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!wh)
		return -1;
	wh->w_wmi.w_sc = sc;
	snprintf(wh->w_wmi.w_method, sizeof(wh->w_wmi.w_method), "WM%c%c",
		ginfo->oid[0], ginfo->oid[1]);

	if (wmi_eval_method(&wh->w_wmi, 0, ASUS_METHOD_INIT, 0, 0) != 1)
		return -1;
	wh->w_wmi.w_event = wmi_asus_event;
	SLIST_INSERT_HEAD(&sc->sc_handlers, &wh->w_wmi, w_next);

	memset(&on, 0, sizeof(on));
	on.type = AML_OBJTYPE_INTEGER;
	on.v_integer = 1;
	snprintf(method, sizeof(method), "WC%c%c", ginfo->oid[0], ginfo->oid[1]);
	aml_evalname(sc->sc_acpi, sc->sc_node, method, 1, &on, NULL);
	res = asus_dev_get(wh, ASUS_DEV_PERF);
	if (res >= 0)
		wh->w_perfid = ASUS_DEV_PERF;
	res = asus_dev_get(wh, ASUS_DEV_PERF_2);
	if (res >= 0)
		wh->w_perfid = ASUS_DEV_PERF_2;
	// turn on by default
	asus_toggle(wh, ASUS_DEV_KBDLIGHT, &wh->w_kbdlight);

	/* turn on FnLock by default if available */
	res = asus_dev_get(wh, ASUS_DEV_FNLOCK);
	if ((res & ASUS_DSTS_PRESENCE) &&
	    !(res & ASUS_FNLOCK_BIOS_DISABLED)) {
		asus_toggle(wh, ASUS_DEV_FNLOCK, &wh->w_fnlock);
	}

	return 0;
}

int
wmi_asus_event(struct wmihandler *wmi, int code)
{
	struct wmiasus *wh = (struct wmiasus *)wmi;

	switch (code) {
	case ASUS_EVENT_AC_OFF:
	case ASUS_EVENT_AC_ON:
		break;
	case ASUS_EVENT_KBDLIGHT_UP:
	case ASUS_EVENT_KBDLIGHT_DOWN:
	case ASUS_EVENT_KBDLIGHT_TOGGLE:
		asus_toggle(wh, ASUS_DEV_KBDLIGHT, &wh->w_kbdlight);
		break;
	case ASUS_EVENT_FNLOCK:
		asus_toggle(wh, ASUS_DEV_FNLOCK, &wh->w_fnlock);
		break;
	case ASUS_EVENT_PERF:
		asus_toggle(wh, wh->w_perfid, &wh->w_perf);
		DPRINTF(("toggle perf %d\n", wh->w_perf));
		break;
	default:
		DPRINTF(("asus button %d\n", code));
		break;
	}
	return 0;
}
