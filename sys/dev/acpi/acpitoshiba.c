/* $OpenBSD: acpitoshiba.c,v 1.17 2024/04/13 23:44:11 jsg Exp $ */
/*-
 * Copyright (c) 2003 Hiroyuki Aizu <aizu@navi.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <machine/apmvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

/*
 * Toshiba HCI interface definitions
 *
 * HCI is Toshiba's "Hardware Control Interface" which is supposed to
 * be uniform across all their models.	Ideally we would just call
 * dedicated ACPI methods instead of using this primitive interface.
 * However, the ACPI methods seem to be incomplete in some areas (for
 * example they allow setting, but not reading, the LCD brightness
 * value), so this is still useful.
 */
#define	METHOD_HCI			"GHCI"
#define	METHOD_HCI_ENABLE		"ENAB"

/* Operations */
#define	HCI_SET				0xFF00
#define	HCI_GET				0xFE00

/* Functions */
#define	HCI_REG_SYSTEM_EVENT		0x0016
#define	HCI_REG_VIDEO_OUTPUT		0x001C
#define	HCI_REG_LCD_BRIGHTNESS		0x002A

/* Field definitions */
#define	HCI_LCD_BRIGHTNESS_BITS		3
#define	HCI_LCD_BRIGHTNESS_SHIFT	(16 - HCI_LCD_BRIGHTNESS_BITS)
#define	HCI_LCD_BRIGHTNESS_MAX		((1 << HCI_LCD_BRIGHTNESS_BITS) - 1)
#define	HCI_LCD_BRIGHTNESS_MIN		0
#define	HCI_VIDEO_OUTPUT_FLAG		0x0100
#define	HCI_VIDEO_OUTPUT_CYCLE_MIN	0
#define	HCI_VIDEO_OUTPUT_CYCLE_MAX	7

/* HCI register definitions */
#define	HCI_WORDS			6 /* Number of register */
#define	HCI_REG_AX			0 /* Operation, then return value */
#define	HCI_REG_BX			1 /* Function */
#define	HCI_REG_CX			2 /* Argument (in or out) */

/* Return codes */
#define	HCI_FAILURE			-1
#define	HCI_SUCCESS			0

/* Toshiba fn_keys events */
#define	FN_KEY_SUSPEND			0x01BD
#define	FN_KEY_HIBERNATE		0x01BE
#define	FN_KEY_VIDEO_OUTPUT		0x01BF
#define	FN_KEY_BRIGHTNESS_DOWN		0x01C0
#define	FN_KEY_BRIGHTNESS_UP		0x01C1

struct acpitoshiba_softc {
	struct device		 sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	uint32_t		 sc_brightness;
};

int	toshiba_enable_events(struct acpitoshiba_softc *);
int	toshiba_read_events(struct acpitoshiba_softc *);
int	toshiba_match(struct device *, void *, void *);
void	toshiba_attach(struct device *, struct device *, void *);
int	toshiba_hotkey(struct aml_node *, int, void *);
int	toshiba_get_brightness(struct acpitoshiba_softc *, uint32_t *);
int	toshiba_set_brightness(struct acpitoshiba_softc *, uint32_t *);
int	toshiba_get_video_output(struct acpitoshiba_softc *, uint32_t *);
int	toshiba_set_video_output(struct acpitoshiba_softc *, uint32_t *);
void	toshiba_update_brightness(void *, int);
int	toshiba_fn_key_brightness_up(struct acpitoshiba_softc *);
int	toshiba_fn_key_brightness_down(struct acpitoshiba_softc *);
int	toshiba_fn_key_video_output(struct acpitoshiba_softc *);

/* wconsole hook functions */
int	acpitoshiba_get_param(struct wsdisplay_param *);
int	acpitoshiba_set_param(struct wsdisplay_param *);
int	get_param_brightness(struct wsdisplay_param *);
int	set_param_brightness(struct wsdisplay_param *);

const struct cfattach acpitoshiba_ca = {
	sizeof(struct acpitoshiba_softc), toshiba_match, toshiba_attach
};

struct cfdriver acpitoshiba_cd = {
	NULL, "acpitoshiba", DV_DULL
};

const char *acpitoshiba_hids[] = {
	"TOS6200",	/* Libretto */
	"TOS6207",	/* Dynabook */
	"TOS6208",	/* SPA40 */
	NULL
};

int
get_param_brightness(struct wsdisplay_param *dp)
{
	struct acpitoshiba_softc *sc = acpitoshiba_cd.cd_devs[0];

	if (sc != NULL) {
		/* default settings */
		dp->min = HCI_LCD_BRIGHTNESS_MIN;
		dp->max = HCI_LCD_BRIGHTNESS_MAX;
		dp->curval = sc->sc_brightness;
		return (0);
	}

	return (1);
}

int
acpitoshiba_get_param(struct wsdisplay_param *dp)
{
	int ret;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		ret = get_param_brightness(dp);
		return (ret);
	default:
		return (1);
	}
}

int
set_param_brightness(struct wsdisplay_param *dp)
{
	struct acpitoshiba_softc *sc = acpitoshiba_cd.cd_devs[0];

	if (sc != NULL) {
		if (dp->curval < HCI_LCD_BRIGHTNESS_MIN)
			dp->curval = HCI_LCD_BRIGHTNESS_MIN;
		if (dp->curval > HCI_LCD_BRIGHTNESS_MAX)
			dp->curval = HCI_LCD_BRIGHTNESS_MAX;
		sc->sc_brightness = dp->curval;
		acpi_addtask(sc->sc_acpi, toshiba_update_brightness, sc, 0);
		acpi_wakeup(sc->sc_acpi);
		return (0);
	}

	return (1);
}

int
acpitoshiba_set_param(struct wsdisplay_param *dp)
{
	int ret;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		ret = set_param_brightness(dp);
		return (ret);
	default:
		return (1);
	}
}

void
toshiba_update_brightness(void *arg0, int arg1)
{
	struct acpitoshiba_softc *sc = arg0;

	toshiba_set_brightness(sc, &sc->sc_brightness);
}

int
toshiba_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata	      *cf = match;

	if (acpi_matchhids(aa, acpitoshiba_hids, cf->cf_driver->cd_name))
		return (1);

	if (aa->aaa_name == NULL ||
	    strcmp(aa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aa->aaa_table != NULL)
		return (0);

	return (1);
}

int
toshiba_enable_events(struct acpitoshiba_softc *sc)
{
	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, METHOD_HCI_ENABLE,
		    0, NULL, NULL)) {
		printf("%s: couldn't toggle METHOD_HCI_ENABLE\n", DEVNAME(sc));
		return (HCI_FAILURE);
	}

	return (HCI_SUCCESS);
}

int
toshiba_read_events(struct acpitoshiba_softc *sc)
{
	struct aml_value args[HCI_WORDS];
	struct aml_value res;
	int i, val;

	bzero(args, sizeof(args));
	bzero(&res, sizeof(res));

	for (i = 0; i < HCI_WORDS; ++i)
		args[i].type = AML_OBJTYPE_INTEGER;

	args[HCI_REG_AX].v_integer = HCI_GET;
	args[HCI_REG_BX].v_integer = HCI_REG_SYSTEM_EVENT;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, METHOD_HCI,
		    i, args, &res)) {
		printf("%s: couldn't toggle METHOD_HCI\n", DEVNAME(sc));
		return (HCI_FAILURE);
	}

	/*
	 * We receive a package type so we need to get the event
	 * value from the HCI_REG_CX.
	 */
	val = aml_val2int(res.v_package[HCI_REG_CX]);
	aml_freevalue(&res);

	return (val);
}

void
toshiba_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpitoshiba_softc *sc = (struct acpitoshiba_softc *)self;
	struct acpi_attach_args *aa = aux;
	int ret;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf("\n");

	/* enable events and hotkeys */
	ret = toshiba_enable_events(sc);
	if (ret != HCI_FAILURE) {
		/* Run toshiba_hotkey on button presses */
		aml_register_notify(sc->sc_devnode, aa->aaa_dev,
				toshiba_hotkey, sc, ACPIDEV_NOPOLL);
	}

	ret = toshiba_get_brightness(sc, &sc->sc_brightness);
	if (ret != HCI_FAILURE) {
		/* wsconsctl purpose */
		ws_get_param = acpitoshiba_get_param;
		ws_set_param = acpitoshiba_set_param;
	}
}

int
toshiba_fn_key_brightness_up(struct acpitoshiba_softc *sc)
{
	uint32_t brightness_level;
	int ret;

	ret = toshiba_get_brightness(sc, &brightness_level);
	if (ret != HCI_FAILURE) {

		if (brightness_level++ == HCI_LCD_BRIGHTNESS_MAX)
			brightness_level = HCI_LCD_BRIGHTNESS_MAX;
		else
			ret = toshiba_set_brightness(sc, &brightness_level);
	}

	return (ret);
}

int
toshiba_fn_key_brightness_down(struct acpitoshiba_softc *sc)
{
	uint32_t brightness_level;
	int ret;

	ret = toshiba_get_brightness(sc, &brightness_level);
	if (ret != HCI_FAILURE) {
		if (brightness_level-- == HCI_LCD_BRIGHTNESS_MIN)
			brightness_level = HCI_LCD_BRIGHTNESS_MIN;
		else
			ret = toshiba_set_brightness(sc, &brightness_level);
	}

	return (ret);
}

int
toshiba_fn_key_video_output(struct acpitoshiba_softc *sc)
{
	uint32_t video_output;
	int ret;

	ret = toshiba_get_video_output(sc, &video_output);
	if (ret != HCI_FAILURE) {
		video_output = (video_output + 1) % HCI_VIDEO_OUTPUT_CYCLE_MAX;

		ret = toshiba_set_video_output(sc, &video_output);
	}

	return (ret);
}

int
toshiba_hotkey(struct aml_node *node, int notify, void *arg)
{
	struct acpitoshiba_softc *sc = arg;
	int event, ret = HCI_FAILURE;

	event = toshiba_read_events(sc);
	if (!event)
		return (0);

	switch (event) {
	case FN_KEY_BRIGHTNESS_UP:
		/* Increase brightness */
		ret = toshiba_fn_key_brightness_up(sc);
		break;
	case FN_KEY_BRIGHTNESS_DOWN:
		/* Decrease brightness */
		ret = toshiba_fn_key_brightness_down(sc);
		break;
	case FN_KEY_SUSPEND:
#ifndef SMALL_KERNEL
		if (acpi_record_event(sc->sc_acpi, APM_USER_SUSPEND_REQ)) {
			acpi_addtask(sc->sc_acpi, acpi_sleep_task,
			    sc->sc_acpi, SLEEP_SUSPEND);
			ret = HCI_SUCCESS;
		}
#endif
		break;
	case FN_KEY_HIBERNATE:
#if defined(HIBERNATE) && !defined(SMALL_KERNEL)
		if (acpi_record_event(sc->sc_acpi, APM_USER_HIBERNATE_REQ)) {
			acpi_addtask(sc->sc_acpi, acpi_sleep_task,
			    sc->sc_acpi, SLEEP_HIBERNATE);
			ret = HCI_SUCCESS;
		}
#endif
		break;
	case FN_KEY_VIDEO_OUTPUT:
		/* Cycle through video outputs. */
		ret = toshiba_fn_key_video_output(sc);
		break;
	default:
		break;
	}

	if (ret != HCI_SUCCESS)
		return (1);

	return (0);
}

int
toshiba_set_brightness(struct acpitoshiba_softc *sc, uint32_t *brightness)
{
	struct aml_value args[HCI_WORDS];
	int i;

	bzero(args, sizeof(args));

	for (i = 0; i < HCI_WORDS; ++i)
		args[i].type = AML_OBJTYPE_INTEGER;

	if ((*brightness < HCI_LCD_BRIGHTNESS_MIN) ||
	    (*brightness > HCI_LCD_BRIGHTNESS_MAX))
		return (HCI_FAILURE);

	*brightness <<= HCI_LCD_BRIGHTNESS_SHIFT;

	args[HCI_REG_AX].v_integer = HCI_SET;
	args[HCI_REG_BX].v_integer = HCI_REG_LCD_BRIGHTNESS;
	args[HCI_REG_CX].v_integer = *brightness;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, METHOD_HCI,
	    i, args, NULL)) {
		printf("%s: set brightness failed\n", DEVNAME(sc));
		return (HCI_FAILURE);
	}

	sc->sc_brightness = *brightness;
	return (HCI_SUCCESS);
}

int
toshiba_get_brightness(struct acpitoshiba_softc *sc, uint32_t *brightness)
{
	struct aml_value args[HCI_WORDS];
	struct aml_value res;
	int i;

	bzero(args, sizeof(args));
	bzero(&res, sizeof(res));

	for (i = 0; i < HCI_WORDS; ++i)
		args[i].type = AML_OBJTYPE_INTEGER;

	args[HCI_REG_AX].v_integer = HCI_GET;
	args[HCI_REG_BX].v_integer = HCI_REG_LCD_BRIGHTNESS;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, METHOD_HCI,
	    i, args, &res)) {
		printf("%s: get brightness failed\n", DEVNAME(sc));
		return (HCI_FAILURE);
	}

	/*
	 * We receive a package type so we need to get the event
	 * value from the HCI_REG_CX.
	 */
	*brightness = aml_val2int(res.v_package[HCI_REG_CX]);

	*brightness >>= HCI_LCD_BRIGHTNESS_SHIFT;

	aml_freevalue(&res);

	return (HCI_SUCCESS);
}

int
toshiba_get_video_output(struct acpitoshiba_softc *sc, uint32_t *video_output)
{
	struct aml_value res, args[HCI_WORDS];
	int i;

	bzero(args, sizeof(args));
	bzero(&res, sizeof(res));

	for (i = 0; i < HCI_WORDS; ++i)
		args[i].type = AML_OBJTYPE_INTEGER;

	args[HCI_REG_AX].v_integer = HCI_GET;
	args[HCI_REG_BX].v_integer = HCI_REG_VIDEO_OUTPUT;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, METHOD_HCI,
	    i, args, &res)) {
		printf("%s: get video output failed\n", DEVNAME(sc));
		return (HCI_FAILURE);
	}

	/*
	 * We receive a package type so we need to get the event
	 * value from the HCI_REG_CX.
	 */
	*video_output = aml_val2int(res.v_package[HCI_REG_CX]);

	*video_output &= 0xff;

	aml_freevalue(&res);

	return (HCI_SUCCESS);
}

int
toshiba_set_video_output(struct acpitoshiba_softc *sc, uint32_t *video_output)
{
	struct aml_value args[HCI_WORDS];
	int i;

	bzero(args, sizeof(args));

	if ((*video_output < HCI_VIDEO_OUTPUT_CYCLE_MIN) ||
	    (*video_output > HCI_VIDEO_OUTPUT_CYCLE_MAX))
		return (HCI_FAILURE);

	*video_output |= HCI_VIDEO_OUTPUT_FLAG;

	for (i = 0; i < HCI_WORDS; ++i)
		args[i].type = AML_OBJTYPE_INTEGER;

	args[HCI_REG_AX].v_integer = HCI_SET;
	args[HCI_REG_BX].v_integer = HCI_REG_VIDEO_OUTPUT;
	args[HCI_REG_CX].v_integer = *video_output;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, METHOD_HCI,
	    i, args, NULL)) {
		printf("%s: set video output failed\n", DEVNAME(sc));
		return (HCI_FAILURE);
	}

	return (HCI_SUCCESS);
}
