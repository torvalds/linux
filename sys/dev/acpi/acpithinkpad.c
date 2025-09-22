/*	$OpenBSD: acpithinkpad.c,v 1.74 2023/07/07 07:37:59 claudio Exp $	*/
/*
 * Copyright (c) 2008 joshua stein <jcs@openbsd.org>
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
#include <sys/sensors.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <machine/apmvar.h>

#include "audio.h"
#include "wskbd.h"

/* #define ACPITHINKPAD_DEBUG */

#ifdef ACPITHINKPAD_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define	THINKPAD_HKEY_VERSION1		0x0100
#define	THINKPAD_HKEY_VERSION2		0x0200

#define	THINKPAD_CMOS_VOLUME_DOWN	0x00
#define	THINKPAD_CMOS_VOLUME_UP		0x01
#define	THINKPAD_CMOS_VOLUME_MUTE	0x02
#define	THINKPAD_CMOS_BRIGHTNESS_UP	0x04
#define	THINKPAD_CMOS_BRIGHTNESS_DOWN	0x05

#define	THINKPAD_BLUETOOTH_PRESENT	0x01
#define	THINKPAD_BLUETOOTH_ENABLED	0x02

/* wan (not wifi) card */
#define	THINKPAD_WAN_PRESENT		0x01
#define	THINKPAD_WAN_ENABLED		0x02

#define	THINKPAD_BUTTON_FN_F1		0x1001
#define	THINKPAD_BUTTON_LOCK_SCREEN	0x1002
#define	THINKPAD_BUTTON_BATTERY_INFO	0x1003
#define	THINKPAD_BUTTON_SUSPEND		0x1004
#define	THINKPAD_BUTTON_WIRELESS	0x1005
#define	THINKPAD_BUTTON_FN_F6		0x1006
#define	THINKPAD_BUTTON_EXTERNAL_SCREEN	0x1007
#define	THINKPAD_BUTTON_POINTER_SWITCH	0x1008
#define	THINKPAD_BUTTON_EJECT		0x1009
#define	THINKPAD_BUTTON_FN_F11		0x100b
#define	THINKPAD_BUTTON_HIBERNATE	0x100c
#define	THINKPAD_BUTTON_BRIGHTNESS_UP	0x1010
#define	THINKPAD_BUTTON_BRIGHTNESS_DOWN	0x1011
#define	THINKPAD_BUTTON_THINKLIGHT	0x1012
#define	THINKPAD_BUTTON_FN_SPACE	0x1014
#define	THINKPAD_BUTTON_VOLUME_UP	0x1015
#define	THINKPAD_BUTTON_VOLUME_DOWN	0x1016
#define	THINKPAD_BUTTON_VOLUME_MUTE	0x1017
#define	THINKPAD_BUTTON_THINKVANTAGE	0x1018
#define	THINKPAD_BUTTON_BLACK		0x101a
#define	THINKPAD_BUTTON_MICROPHONE_MUTE	0x101b
#define	THINKPAD_KEYLIGHT_CHANGED	0x101c
#define	THINKPAD_BUTTON_CONFIG		0x101d
#define	THINKPAD_BUTTON_FIND		0x101e
#define	THINKPAD_BUTTON_ALL_ACTIVEPROGS	0x101f
#define	THINKPAD_BUTTON_ALL_PROGS	0x1020

#define	THINKPAD_ADAPTIVE_NEXT		0x1101
#define	THINKPAD_ADAPTIVE_QUICK		0x1102
#define	THINKPAD_ADAPTIVE_SNIP		0x1105
#define	THINKPAD_ADAPTIVE_VOICE		0x1108
#define	THINKPAD_ADAPTIVE_GESTURES	0x110a
#define	THINKPAD_ADAPTIVE_SETTINGS	0x110e
#define	THINKPAD_ADAPTIVE_TAB		0x110f
#define	THINKPAD_ADAPTIVE_REFRESH	0x1110
#define	THINKPAD_ADAPTIVE_BACK		0x1111
#define THINKPAD_PORT_REPL_DOCKED	0x4010
#define THINKPAD_PORT_REPL_UNDOCKED	0x4011
#define	THINKPAD_TABLET_DOCKED		0x4012
#define	THINKPAD_TABLET_UNDOCKED	0x4013
#define	THINKPAD_LID_OPEN		0x5001
#define	THINKPAD_LID_CLOSED		0x5002
#define	THINKPAD_TABLET_SCREEN_NORMAL	0x500a
#define	THINKPAD_TABLET_SCREEN_ROTATED	0x5009
#define	THINKPAD_BRIGHTNESS_CHANGED	0x5010
#define	THINKPAD_TABLET_PEN_INSERTED	0x500b
#define	THINKPAD_TABLET_PEN_REMOVED	0x500c
#define	THINKPAD_SWITCH_NUMLOCK		0x6000
#define	THINKPAD_BUTTON_ROTATION_LOCK	0x6020
#define	THINKPAD_THERMAL_TABLE_CHANGED	0x6030
#define	THINKPAD_POWER_CHANGED		0x6040
#define	THINKPAD_BACKLIGHT_CHANGED	0x6050
#define	THINKPAD_BUTTON_FN_TOGGLE       0x6060
#define	THINKPAD_TABLET_SCREEN_CHANGED	0x60c0
#define	THINKPAD_SWITCH_WIRELESS	0x7000

#define THINKPAD_SENSOR_FANRPM		0
#define THINKPAD_SENSOR_PORTREPL	1
#define THINKPAD_SENSOR_TMP0		2
#define THINKPAD_NSENSORS		10

#define THINKPAD_ECOFFSET_VOLUME	0x30
#define THINKPAD_ECOFFSET_VOLUME_MUTE_MASK 0x40
#define THINKPAD_ECOFFSET_FANLO		0x84
#define THINKPAD_ECOFFSET_FANHI		0x85

#define	THINKPAD_ADAPTIVE_MODE_HOME	1
#define	THINKPAD_ADAPTIVE_MODE_FUNCTION	3

#define THINKPAD_MASK_MIC_MUTE		(1 << 14)
#define THINKPAD_MASK_BRIGHTNESS_UP	(1 << 15)
#define THINKPAD_MASK_BRIGHTNESS_DOWN	(1 << 16)
#define THINKPAD_MASK_KBD_BACKLIGHT	(1 << 17)

#define THINKPAD_BATTERY_ERROR		0x80000000
#define THINKPAD_BATTERY_SUPPORT	0x00000100
#define THINKPAD_BATTERY_SUPPORT_BICG	0x00000020
#define THINKPAD_BATTERY_SHIFT		8

struct acpithinkpad_softc {
	struct device		 sc_dev;

	struct acpiec_softc     *sc_ec;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	struct ksensor		 sc_sens[THINKPAD_NSENSORS];
	struct ksensordev	 sc_sensdev;
	int			 sc_ntempsens;

	uint64_t		 sc_hkey_version;

	uint64_t		 sc_thinklight;
	const char		*sc_thinklight_get;
	const char		*sc_thinklight_set;

	uint64_t		 sc_brightness;
};

extern void acpiec_read(struct acpiec_softc *, uint8_t, int, uint8_t *);

int	thinkpad_match(struct device *, void *, void *);
void	thinkpad_attach(struct device *, struct device *, void *);
int	thinkpad_hotkey(struct aml_node *, int, void *);
int	thinkpad_enable_events(struct acpithinkpad_softc *);
int	thinkpad_toggle_bluetooth(struct acpithinkpad_softc *);
int	thinkpad_toggle_wan(struct acpithinkpad_softc *);
int	thinkpad_cmos(struct acpithinkpad_softc *sc, uint8_t);
int	thinkpad_volume_down(struct acpithinkpad_softc *);
int	thinkpad_volume_up(struct acpithinkpad_softc *);
int	thinkpad_volume_mute(struct acpithinkpad_softc *);
int	thinkpad_brightness_up(struct acpithinkpad_softc *);
int	thinkpad_brightness_down(struct acpithinkpad_softc *);
int	thinkpad_adaptive_change(struct acpithinkpad_softc *);
int	thinkpad_activate(struct device *, int);

/* wscons hook functions */
void	thinkpad_get_thinklight(struct acpithinkpad_softc *);
void	thinkpad_set_thinklight(void *, int);
int	thinkpad_get_kbd_backlight(struct wskbd_backlight *);
int	thinkpad_set_kbd_backlight(struct wskbd_backlight *);
extern int (*wskbd_get_backlight)(struct wskbd_backlight *);
extern int (*wskbd_set_backlight)(struct wskbd_backlight *);
int	thinkpad_get_brightness(struct acpithinkpad_softc *);
int	thinkpad_set_brightness(void *, int);
int	thinkpad_get_param(struct wsdisplay_param *);
int	thinkpad_set_param(struct wsdisplay_param *);
int	thinkpad_get_temp(struct acpithinkpad_softc *, int, int64_t *);

void    thinkpad_sensor_attach(struct acpithinkpad_softc *sc);
void    thinkpad_sensor_refresh(void *);

#if NAUDIO > 0 && NWSKBD > 0
void thinkpad_attach_deferred(void *);
int thinkpad_get_volume_mute(struct acpithinkpad_softc *);
extern int wskbd_set_mixermute(long, long);
extern int wskbd_set_mixervolume(long, long);
#endif

int	thinkpad_battery_setchargemode(int);
int	thinkpad_battery_setchargestart(int);
int	thinkpad_battery_setchargestop(int);

extern int (*hw_battery_setchargemode)(int);
extern int (*hw_battery_setchargestart)(int);
extern int (*hw_battery_setchargestop)(int);
extern int hw_battery_chargemode;
extern int hw_battery_chargestart;
extern int hw_battery_chargestop;

const struct cfattach acpithinkpad_ca = {
	sizeof(struct acpithinkpad_softc), thinkpad_match, thinkpad_attach,
	NULL, thinkpad_activate
};

struct cfdriver acpithinkpad_cd = {
	NULL, "acpithinkpad", DV_DULL
};

const char *acpithinkpad_hids[] = {
	"IBM0068",
	"LEN0068",
	"LEN0268",
	NULL
};

int
thinkpad_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata *cf = match;
	int64_t	res;

	if (!acpi_matchhids(aa, acpithinkpad_hids, cf->cf_driver->cd_name))
		return (0);

	if (aml_evalinteger((struct acpi_softc *)parent, aa->aaa_node,
	    "MHKV", 0, NULL, &res))
		return (0);

	if (!(res == THINKPAD_HKEY_VERSION1 || res == THINKPAD_HKEY_VERSION2))
		return (0);

	return (1);
}

void
thinkpad_sensor_attach(struct acpithinkpad_softc *sc)
{
	int64_t tmp;
	int i;

	if (sc->sc_acpi->sc_ec == NULL)
		return;
	sc->sc_ec = sc->sc_acpi->sc_ec;

	/* Add temperature probes */
	strlcpy(sc->sc_sensdev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensdev.xname));
	sc->sc_ntempsens = 0;
	for (i = 0; i < THINKPAD_NSENSORS - THINKPAD_SENSOR_TMP0; i++) {
		if (thinkpad_get_temp(sc, i, &tmp) != 0)
			break;

		sc->sc_sens[THINKPAD_SENSOR_TMP0 + i].type = SENSOR_TEMP;
		sensor_attach(&sc->sc_sensdev,
		    &sc->sc_sens[THINKPAD_SENSOR_TMP0 + i]);
		sc->sc_ntempsens++;
 	}

	/* Add fan probe */
	sc->sc_sens[THINKPAD_SENSOR_FANRPM].type = SENSOR_FANRPM;
	sensor_attach(&sc->sc_sensdev, &sc->sc_sens[THINKPAD_SENSOR_FANRPM]);

	/* Add port replicator indicator */
	sc->sc_sens[THINKPAD_SENSOR_PORTREPL].type = SENSOR_INDICATOR;
	sc->sc_sens[THINKPAD_SENSOR_PORTREPL].status = SENSOR_S_UNKNOWN;
	strlcpy(sc->sc_sens[THINKPAD_SENSOR_PORTREPL].desc, "port replicator",
	        sizeof(sc->sc_sens[THINKPAD_SENSOR_PORTREPL].desc));
	sensor_attach(&sc->sc_sensdev, &sc->sc_sens[THINKPAD_SENSOR_PORTREPL]);

	sensordev_install(&sc->sc_sensdev);
}

void
thinkpad_sensor_refresh(void *arg)
{
	struct acpithinkpad_softc *sc = arg;
	uint8_t lo, hi, i;
	int64_t tmp;

	/* Refresh sensor readings */
	for (i = 0; i < sc->sc_ntempsens; i++) {
		if (thinkpad_get_temp(sc, i, &tmp) != 0) {
 			sc->sc_sens[i].flags = SENSOR_FINVALID;
			continue;
		}

		sc->sc_sens[THINKPAD_SENSOR_TMP0 + i].value =
		    (tmp * 1000000) + 273150000;
		sc->sc_sens[THINKPAD_SENSOR_TMP0 + i].flags =
		    (tmp > 127 || tmp < -127) ? SENSOR_FINVALID : 0;
 	}

	/* Read fan RPM */
	acpiec_read(sc->sc_ec, THINKPAD_ECOFFSET_FANLO, 1, &lo);
	acpiec_read(sc->sc_ec, THINKPAD_ECOFFSET_FANHI, 1, &hi);
	if (hi == 0xff && lo == 0xff) {
 		sc->sc_sens[THINKPAD_SENSOR_FANRPM].flags = SENSOR_FINVALID;
	} else {
		sc->sc_sens[THINKPAD_SENSOR_FANRPM].value = ((hi << 8L) + lo);
 		sc->sc_sens[THINKPAD_SENSOR_FANRPM].flags = 0;
	}
}

void
thinkpad_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpithinkpad_softc *sc = (struct acpithinkpad_softc *)self;
	struct acpi_attach_args	*aa = aux;
	struct aml_value arg;
	uint64_t ret;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "MHKV", 0, NULL,
	    &sc->sc_hkey_version))
		sc->sc_hkey_version = THINKPAD_HKEY_VERSION1;

	printf(": version %lld.%lld\n", sc->sc_hkey_version >> 8,
	    sc->sc_hkey_version & 0xff);

#if NAUDIO > 0 && NWSKBD > 0
	/* Defer speaker mute */
	if (thinkpad_get_volume_mute(sc) == 1)
		startuphook_establish(thinkpad_attach_deferred, sc);
#endif

	/* Set event mask to receive everything */
	thinkpad_enable_events(sc);
	thinkpad_sensor_attach(sc);

	/* Check for ThinkLight or keyboard backlight */
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "KLCG",
	    0, NULL, &sc->sc_thinklight) == 0) {
		sc->sc_thinklight_get = "KLCG";
		sc->sc_thinklight_set = "KLCS";
		wskbd_get_backlight = thinkpad_get_kbd_backlight;
		wskbd_set_backlight = thinkpad_set_kbd_backlight;
	} else if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "MLCG",
	    0, NULL, &sc->sc_thinklight) == 0) {
		sc->sc_thinklight_get = "MLCG";
		sc->sc_thinklight_set = "MLCS";
		wskbd_get_backlight = thinkpad_get_kbd_backlight;
		wskbd_set_backlight = thinkpad_set_kbd_backlight;
	}

	/* On version 2 and newer, let *drm or acpivout control brightness */
	if (sc->sc_hkey_version == THINKPAD_HKEY_VERSION1 &&
	    (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "PBLG",
	    0, NULL, &sc->sc_brightness) == 0)) {
		ws_get_param = thinkpad_get_param;
		ws_set_param = thinkpad_set_param;
	}

	memset(&arg, 0, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = 1;

	hw_battery_chargemode = 1;
	hw_battery_chargestart = 0;
	hw_battery_chargestop = 100;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BCTG",
	    1, &arg, &ret) == 0 && (ret & THINKPAD_BATTERY_ERROR) == 0) {
		if (ret & THINKPAD_BATTERY_SUPPORT) {
			hw_battery_chargestart = ret & 0xff;
			hw_battery_setchargestart =
				thinkpad_battery_setchargestart;
		}
	}
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BCSG",
	    1, &arg, &ret) == 0 && (ret & THINKPAD_BATTERY_ERROR) == 0) {
		if (ret & THINKPAD_BATTERY_SUPPORT) {
			if ((ret & 0xff) == 0)
				hw_battery_chargestop = 100;
			else
				hw_battery_chargestop = ret & 0xff;
			hw_battery_setchargestop =
				thinkpad_battery_setchargestop;
		}
	}
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BDSG",
	    1, &arg, &ret) == 0 && (ret & THINKPAD_BATTERY_ERROR) == 0) {
		if (ret & THINKPAD_BATTERY_SUPPORT) {
			if (ret & 0x1)
				hw_battery_chargemode = -1;
			hw_battery_setchargemode =
				thinkpad_battery_setchargemode;
		}
	}
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BICG",
	    1, &arg, &ret) == 0 && (ret & THINKPAD_BATTERY_ERROR) == 0) {
		if (ret & THINKPAD_BATTERY_SUPPORT_BICG) {
			if (ret & 0x1)
				hw_battery_chargemode = 0;
			hw_battery_setchargemode =
				thinkpad_battery_setchargemode;
		}
	}

	/* Run thinkpad_hotkey on button presses */
	aml_register_notify(sc->sc_devnode, aa->aaa_dev,
	    thinkpad_hotkey, sc, ACPIDEV_POLL);
}

int
thinkpad_enable_events(struct acpithinkpad_softc *sc)
{
	struct aml_value arg, args[2];
	int64_t	mask;
	int i;

	/* Get the default event mask */
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "MHKA",
	    0, NULL, &mask)) {
		printf("%s: no MHKA\n", DEVNAME(sc));
		return (1);
	}

	/* Enable events we need to know about */
	mask |= (THINKPAD_MASK_MIC_MUTE	|
	    THINKPAD_MASK_BRIGHTNESS_UP |
	    THINKPAD_MASK_BRIGHTNESS_DOWN |
	    THINKPAD_MASK_KBD_BACKLIGHT);

	DPRINTF(("%s: setting event mask to 0x%llx\n", DEVNAME(sc), mask));

	/* Update hotkey mask */
	bzero(args, sizeof(args));
	args[0].type = args[1].type = AML_OBJTYPE_INTEGER;
	for (i = 0; i < 32; i++) {
		args[0].v_integer = i + 1;
		args[1].v_integer = (((1 << i) & mask) != 0);

		if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "MHKM",
		    2, args, NULL)) {
			printf("%s: couldn't toggle MHKM\n", DEVNAME(sc));
			return (1);
		}
	}

	/* Enable hotkeys */
	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = 1;
	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "MHKC",
	    1, &arg, NULL)) {
		printf("%s: couldn't enable hotkeys\n", DEVNAME(sc));
		return (1);
	}

	return (0);
}

int
thinkpad_hotkey(struct aml_node *node, int notify_type, void *arg)
{
	struct acpithinkpad_softc *sc = arg;
	int64_t	event;

	if (notify_type == 0x00) {
		/* Poll sensors */
		thinkpad_sensor_refresh(sc);
		return (0);
	}

	if (notify_type != 0x80)
		return (1);

	for (;;) {
		if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "MHKP",
		    0, NULL, &event))
			break;

		DPRINTF(("%s: event 0x%03llx\n", DEVNAME(sc), event));
		if (event == 0)
			break;

		switch (event) {
		case THINKPAD_BUTTON_BRIGHTNESS_UP:
			thinkpad_brightness_up(sc);
			break;
		case THINKPAD_BUTTON_BRIGHTNESS_DOWN:
			thinkpad_brightness_down(sc);
			break;
		case THINKPAD_BUTTON_WIRELESS:
			thinkpad_toggle_bluetooth(sc);
			break;
		case THINKPAD_BUTTON_SUSPEND:
#ifndef SMALL_KERNEL
			if (acpi_record_event(sc->sc_acpi, APM_USER_SUSPEND_REQ))
				acpi_addtask(sc->sc_acpi, acpi_sleep_task, 
				    sc->sc_acpi, SLEEP_SUSPEND);
#endif
			break;
		case THINKPAD_BUTTON_VOLUME_MUTE:
			thinkpad_volume_mute(sc);
			break;
		case THINKPAD_BUTTON_VOLUME_DOWN:
			thinkpad_volume_down(sc);
			break;
		case THINKPAD_BUTTON_VOLUME_UP:
			thinkpad_volume_up(sc);
			break;
		case THINKPAD_BUTTON_MICROPHONE_MUTE:
#if NAUDIO > 0 && NWSKBD > 0
			wskbd_set_mixervolume(0, 0);
#endif
			break;
		case THINKPAD_BUTTON_HIBERNATE:
#if defined(HIBERNATE) && !defined(SMALL_KERNEL)
			if (acpi_record_event(sc->sc_acpi, APM_USER_HIBERNATE_REQ))
				acpi_addtask(sc->sc_acpi, acpi_sleep_task, 
				    sc->sc_acpi, SLEEP_HIBERNATE);
#endif
			break;
		case THINKPAD_BUTTON_THINKLIGHT:
			thinkpad_get_thinklight(sc);
			break;
		case THINKPAD_ADAPTIVE_NEXT:
		case THINKPAD_ADAPTIVE_QUICK:
			thinkpad_adaptive_change(sc);
			break;
		case THINKPAD_BACKLIGHT_CHANGED:
			thinkpad_get_brightness(sc);
			break;
		case THINKPAD_PORT_REPL_DOCKED:
			sc->sc_sens[THINKPAD_SENSOR_PORTREPL].value = 1;
			sc->sc_sens[THINKPAD_SENSOR_PORTREPL].status = 
			    SENSOR_S_OK;
			break;
		case THINKPAD_PORT_REPL_UNDOCKED:
			sc->sc_sens[THINKPAD_SENSOR_PORTREPL].value = 0;
			sc->sc_sens[THINKPAD_SENSOR_PORTREPL].status = 
			    SENSOR_S_OK;
			break;
		default:
			/* unknown or boring event */
			DPRINTF(("%s: unhandled event 0x%03llx\n", DEVNAME(sc),
			    event));
			break;
		}
	}

	return (0);
}

int
thinkpad_toggle_bluetooth(struct acpithinkpad_softc *sc)
{
	struct aml_value arg;
	int64_t	bluetooth;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "GBDC",
	    0, NULL, &bluetooth))
		return (1);

	if (!(bluetooth & THINKPAD_BLUETOOTH_PRESENT))
		return (1);

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = bluetooth ^ THINKPAD_BLUETOOTH_ENABLED;
	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "SBDC",
	    1, &arg, NULL)) {
		printf("%s: couldn't toggle bluetooth\n", DEVNAME(sc));
		return (1);
	}

	return (0);
}

int
thinkpad_toggle_wan(struct acpithinkpad_softc *sc)
{
	struct aml_value arg;
	int64_t wan;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "GWAN",
	    0, NULL, &wan))
		return (1);

	if (!(wan & THINKPAD_WAN_PRESENT))
		return (1);

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = wan ^ THINKPAD_WAN_ENABLED;
	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "SWAN",
	    1, &arg, NULL)) {
		printf("%s: couldn't toggle wan\n", DEVNAME(sc));
		return (1);
	}

	return (0);
}

int
thinkpad_cmos(struct acpithinkpad_softc *sc, uint8_t cmd)
{
	struct aml_value arg;

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = cmd;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "\\UCMS", 1, &arg, NULL);
	return (0);
}

int
thinkpad_volume_down(struct acpithinkpad_softc *sc)
{
	return (thinkpad_cmos(sc, THINKPAD_CMOS_VOLUME_DOWN));
}

int
thinkpad_volume_up(struct acpithinkpad_softc *sc)
{
	return (thinkpad_cmos(sc, THINKPAD_CMOS_VOLUME_UP));
}

int
thinkpad_volume_mute(struct acpithinkpad_softc *sc)
{
	return (thinkpad_cmos(sc, THINKPAD_CMOS_VOLUME_MUTE));
}

int
thinkpad_brightness_up(struct acpithinkpad_softc *sc)
{
	int b;

	if (thinkpad_get_brightness(sc) == 0) {
		b = sc->sc_brightness & 0xff;
		if (b < ((sc->sc_brightness >> 8) & 0xff)) {
			sc->sc_brightness = b + 1;
			thinkpad_set_brightness(sc, 0);
		}

		return (0);
	} else
		return (thinkpad_cmos(sc, THINKPAD_CMOS_BRIGHTNESS_UP));
}

int
thinkpad_brightness_down(struct acpithinkpad_softc *sc)
{
	int b;

	if (thinkpad_get_brightness(sc) == 0) {
		b = sc->sc_brightness & 0xff;
		if (b > 0) {
			sc->sc_brightness = b - 1;
			thinkpad_set_brightness(sc, 0);
		}

		return (0);
	} else
		return (thinkpad_cmos(sc, THINKPAD_CMOS_BRIGHTNESS_DOWN));
}

int
thinkpad_adaptive_change(struct acpithinkpad_softc *sc)
{
	struct aml_value arg;
	int64_t	mode;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "GTRW",
	    0, NULL, &mode)) {
		printf("%s: couldn't get adaptive keyboard mode\n", DEVNAME(sc));
		return (1);
	}

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;

	if (mode == THINKPAD_ADAPTIVE_MODE_FUNCTION)
		arg.v_integer = THINKPAD_ADAPTIVE_MODE_HOME;
	else
		arg.v_integer = THINKPAD_ADAPTIVE_MODE_FUNCTION;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "STRW",
	    1, &arg, NULL)) {
		printf("%s: couldn't set adaptive keyboard mode\n", DEVNAME(sc));
		return (1);
	}

	return (0);
}

int
thinkpad_activate(struct device *self, int act)
{

	struct acpithinkpad_softc *sc = (struct acpithinkpad_softc *)self;
	int64_t res;
#if NAUDIO > 0 && NWSKBD > 0
	int mute;
#endif

	switch (act) {
	case DVACT_WAKEUP:
		if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "GTRW",
		    0, NULL, &res) == 0)
			thinkpad_adaptive_change(sc);
#if NAUDIO > 0 && NWSKBD > 0
		mute = thinkpad_get_volume_mute(sc);
		if (mute != -1)
			wskbd_set_mixermute(mute, 1);
#endif
		break;
	}
	return (0);
}

void
thinkpad_get_thinklight(struct acpithinkpad_softc *sc)
{
	if (sc->sc_thinklight_get)
		aml_evalinteger(sc->sc_acpi, sc->sc_devnode,
		    sc->sc_thinklight_get, 0, NULL, &sc->sc_thinklight);
}

void
thinkpad_set_thinklight(void *arg0, int arg1)
{
	struct acpithinkpad_softc *sc = arg0;
	struct aml_value arg;

	memset(&arg, 0, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = sc->sc_thinklight & 0x0f;
	aml_evalname(sc->sc_acpi, sc->sc_devnode,
	    sc->sc_thinklight_set, 1, &arg, NULL);
}

int
thinkpad_get_kbd_backlight(struct wskbd_backlight *kbl)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];

	KASSERT(sc != NULL);

	kbl->min = 0;
	kbl->max = (sc->sc_thinklight >> 8) & 0x0f;
	kbl->curval = sc->sc_thinklight & 0x0f;

	if (kbl->max == 0)
		return (ENOTTY);

	return 0;
}

int
thinkpad_set_kbd_backlight(struct wskbd_backlight *kbl)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];
	int maxval;

	KASSERT(sc != NULL);

	maxval = (sc->sc_thinklight >> 8) & 0x0f;

	if (maxval == 0)
		return (ENOTTY);

	if (kbl->curval > maxval)
		return EINVAL;

	sc->sc_thinklight &= ~0xff;
	sc->sc_thinklight |= kbl->curval;
	acpi_addtask(sc->sc_acpi, thinkpad_set_thinklight, sc, 0);
	acpi_wakeup(sc->sc_acpi);
	return 0;
}

int
thinkpad_get_brightness(struct acpithinkpad_softc *sc)
{
	int ret;

	ret = aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "PBLG", 0, NULL,
	    &sc->sc_brightness);

	DPRINTF(("%s: %s: 0x%llx\n", DEVNAME(sc), __func__, sc->sc_brightness));

	return ret;
}

int
thinkpad_set_brightness(void *arg0, int arg1)
{
	struct acpithinkpad_softc *sc = arg0;
	struct aml_value arg;
	int ret;

	DPRINTF(("%s: %s: 0x%llx\n", DEVNAME(sc), __func__, sc->sc_brightness));

	memset(&arg, 0, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = sc->sc_brightness & 0xff;
	ret = aml_evalname(sc->sc_acpi, sc->sc_devnode, "PBLS", 1, &arg, NULL);

	if (ret)
		return ret;

	thinkpad_get_brightness(sc);

	return 0;
}

int
thinkpad_get_param(struct wsdisplay_param *dp)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];

	if (sc == NULL)
		return -1;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		dp->min = 0;
		dp->max = (sc->sc_brightness >> 8) & 0xff;
		dp->curval = sc->sc_brightness & 0xff;
		return 0;
	default:
		return -1;
	}
}

int
thinkpad_set_param(struct wsdisplay_param *dp)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];
	int maxval;

	if (sc == NULL)
		return -1;

	maxval = (sc->sc_brightness >> 8) & 0xff;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		if (dp->curval < 0)
			dp->curval = 0;
		if (dp->curval > maxval)
			dp->curval = maxval;
		sc->sc_brightness &= ~0xff;
		sc->sc_brightness |= dp->curval;
		acpi_addtask(sc->sc_acpi, (void *)thinkpad_set_brightness, sc,
		    0);
		acpi_wakeup(sc->sc_acpi);
		return 0;
	default:
		return -1;
	}
}

int
thinkpad_get_temp(struct acpithinkpad_softc *sc, int idx, int64_t *temp)
{
	char sname[5];

	snprintf(sname, sizeof(sname), "TMP%d", idx);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_ec->sc_devnode, sname, 0, 0,
	    temp) != 0)
		return (1);

	return (0);
}

#if NAUDIO > 0 && NWSKBD > 0
void
thinkpad_attach_deferred(void *v __unused)
{
	wskbd_set_mixermute(1, 1);
}

int
thinkpad_get_volume_mute(struct acpithinkpad_softc *sc)
{
	uint8_t vol = 0;

	if (sc->sc_acpi->sc_ec == NULL)
		return (-1);

	acpiec_read(sc->sc_acpi->sc_ec, THINKPAD_ECOFFSET_VOLUME, 1, &vol);
	return ((vol & THINKPAD_ECOFFSET_VOLUME_MUTE_MASK) ==
	    THINKPAD_ECOFFSET_VOLUME_MUTE_MASK);
}
#endif

int
thinkpad_battery_inhibit_charge(int state)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];
	struct aml_value arg;
	int battery, count;
	uint64_t ret;

	count = acpi_batcount(sc->sc_acpi);
	for (battery = 1; battery <= count; battery++) {
		memset(&arg, 0, sizeof(arg));
		arg.type = AML_OBJTYPE_INTEGER;
		arg.v_integer = (0xffff << 8) | (battery << 4) | state;
		if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BICS",
		    1, &arg, &ret) || (ret & THINKPAD_BATTERY_ERROR))
			return EIO;
	}
	return 0;
}

int
thinkpad_battery_force_discharge(int state)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];
	struct aml_value arg;
	int battery, count;
	uint64_t ret;

	count = acpi_batcount(sc->sc_acpi);
	for (battery = 1; battery <= count; battery++) {
		memset(&arg, 0, sizeof(arg));
		arg.type = AML_OBJTYPE_INTEGER;
		arg.v_integer = (battery << THINKPAD_BATTERY_SHIFT) | state;
		if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BDSS",
		    1, &arg, &ret) || (ret & THINKPAD_BATTERY_ERROR))
			return EIO;
	}
	return 0;
}

int
thinkpad_battery_setchargemode(int mode)
{
	int error;

	switch (mode) {
	case -1:
		error = thinkpad_battery_inhibit_charge(1);
		if (error)
			return error;
		error = thinkpad_battery_force_discharge(1);
		if (error)
			return error;
		break;
	case 0:
		error = thinkpad_battery_force_discharge(0);
		if (error)
			return error;
		error = thinkpad_battery_inhibit_charge(1);
		if (error)
			return error;
		break;
	case 1:
		error = thinkpad_battery_force_discharge(0);
		if (error)
			return error;
		error = thinkpad_battery_inhibit_charge(0);
		if (error)
			return error;
		break;
	default:
		return EOPNOTSUPP;
	}

	hw_battery_chargemode = mode;
	return 0;
}

int
thinkpad_battery_setchargestart(int start)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];
	struct aml_value arg;
	int battery, count;
	uint64_t ret;

	if (start >= hw_battery_chargestop)
		return EINVAL;

	count = acpi_batcount(sc->sc_acpi);
	for (battery = 1; battery <= count; battery++) {
		memset(&arg, 0, sizeof(arg));
		arg.type = AML_OBJTYPE_INTEGER;
		arg.v_integer = (battery << THINKPAD_BATTERY_SHIFT) | start;
		if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BCCS",
		    1, &arg, &ret) || (ret & THINKPAD_BATTERY_ERROR))
			return EIO;
	}
	hw_battery_chargestart = start;
	return 0;
}

int
thinkpad_battery_setchargestop(int stop)
{
	struct acpithinkpad_softc *sc = acpithinkpad_cd.cd_devs[0];
	struct aml_value arg;
	int battery, count;
	uint64_t ret;

	if (stop <= hw_battery_chargestart)
		return EINVAL;

	if (stop == 100)
		stop = 0;

	count = acpi_batcount(sc->sc_acpi);
	for (battery = 1; battery <= count; battery++) {
		memset(&arg, 0, sizeof(arg));
		arg.type = AML_OBJTYPE_INTEGER;
		arg.v_integer = (battery << THINKPAD_BATTERY_SHIFT) | stop;
		if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "BCSS",
		    1, &arg, &ret) || (ret & THINKPAD_BATTERY_ERROR))
			return EIO;
	}
	hw_battery_chargestop = (stop == 0) ? 100 : stop;
	return 0;
}
