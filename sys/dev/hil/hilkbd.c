/*	$OpenBSD: hilkbd.c,v 1.18 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/hil/hilreg.h>
#include <dev/hil/hilvar.h>
#include <dev/hil/hildevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>
#ifdef WSDISPLAY_COMPAT_RAWKBD
#include <dev/wscons/wskbdraw.h>
#endif

#include <dev/hil/hilkbdmap.h>

struct hilkbd_softc {
	struct hildev_softc sc_hildev;

	int		sc_numleds;
	int		sc_ledstate;
	int		sc_enabled;
	int		sc_console;
	int		sc_lastarrow;

	struct device	*sc_wskbddev;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	int		sc_rawkbd;
#endif
};

int	hilkbdprobe(struct device *, void *, void *);
void	hilkbdattach(struct device *, struct device *, void *);
int	hilkbddetach(struct device *, int);

struct cfdriver hilkbd_cd = {
	NULL, "hilkbd", DV_DULL
};

const struct cfattach hilkbd_ca = {
	sizeof(struct hilkbd_softc), hilkbdprobe, hilkbdattach, hilkbddetach,
};

int	hilkbd_enable(void *, int);
void	hilkbd_set_leds(void *, int);
int	hilkbd_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wskbd_accessops hilkbd_accessops = {
	hilkbd_enable,
	hilkbd_set_leds,
	hilkbd_ioctl,
};

void	hilkbd_cngetc(void *, u_int *, int *);
void	hilkbd_cnpollc(void *, int);
void	hilkbd_cnbell(void *, u_int, u_int, u_int);

const struct wskbd_consops hilkbd_consops = {
	hilkbd_cngetc,
	hilkbd_cnpollc,
	hilkbd_cnbell,
};

struct wskbd_mapdata hilkbd_keymapdata = {
	hilkbd_keydesctab,
#ifdef HILKBD_LAYOUT
	HILKBD_LAYOUT,
#else
	KB_US | KB_DEFAULT,
#endif
};

struct wskbd_mapdata hilkbd_keymapdata_ps2 = {
	hilkbd_keydesctab_ps2,
#ifdef HILKBD_LAYOUT
	HILKBD_LAYOUT,
#else
	KB_US | KB_DEFAULT,
#endif
};

void	hilkbd_bell(struct hil_softc *, u_int, u_int, u_int);
void	hilkbd_callback(struct hildev_softc *, u_int, u_int8_t *);
void	hilkbd_decode(struct hilkbd_softc *, u_int8_t, u_int *, int *, int);
int	hilkbd_is_console(int);

int	seen_hilkbd_console;

int
hilkbdprobe(struct device *parent, void *match, void *aux)
{
	struct hil_attach_args *ha = aux;

	if (ha->ha_type != HIL_DEVICE_KEYBOARD &&
	    ha->ha_type != HIL_DEVICE_BUTTONBOX)
		return (0);

	return (1);
}

void
hilkbdattach(struct device *parent, struct device *self, void *aux)
{
	struct hilkbd_softc *sc = (void *)self;
	struct hil_attach_args *ha = aux;
	struct wskbddev_attach_args a;
	u_int8_t layoutcode;
	int ps2;

	sc->hd_code = ha->ha_code;
	sc->hd_type = ha->ha_type;
	sc->hd_infolen = ha->ha_infolen;
	bcopy(ha->ha_info, sc->hd_info, ha->ha_infolen);
	sc->hd_fn = hilkbd_callback;

	if (ha->ha_type == HIL_DEVICE_KEYBOARD) {
		/*
		 * Determine the keyboard language configuration, but don't
		 * override a user-specified setting.
		 */
		layoutcode = ha->ha_id & (MAXHILKBDLAYOUT - 1);
#ifndef HILKBD_LAYOUT
		if (layoutcode < MAXHILKBDLAYOUT &&
		    hilkbd_layouts[layoutcode] != -1)
			hilkbd_keymapdata.layout =
			hilkbd_keymapdata_ps2.layout =
			    hilkbd_layouts[layoutcode];
#endif

		printf(", layout %x", layoutcode);
	}

	/*
	 * Interpret the identification bytes, if any
	 */
	if (ha->ha_infolen > 2 && (ha->ha_info[1] & HIL_IOB) != 0) {
		/* HILIOB_PROMPT is not always reported... */
		sc->sc_numleds = (ha->ha_info[2] & HILIOB_PMASK) >> 4;
		if (sc->sc_numleds != 0)
			printf(", %d leds", sc->sc_numleds);
	}

	printf("\n");

	/*
	 * Red lettered keyboards come in two flavours, the old one
	 * with only one control key, to the left of the escape key,
	 * and the modern one which has a PS/2 like layout, and leds.
	 *
	 * Unfortunately for us, they use the same device ID range.
	 * We'll differentiate them by looking at the leds property.
	 */
	ps2 = (sc->sc_numleds != 0);

	/* Do not consider button boxes as console devices. */
	if (ha->ha_type == HIL_DEVICE_BUTTONBOX)
		a.console = 0;
	else
		a.console = hilkbd_is_console(ha->ha_console);
	a.keymap = ps2 ? &hilkbd_keymapdata_ps2 : &hilkbd_keymapdata;
	a.accessops = &hilkbd_accessops;
	a.accesscookie = sc;

	if (a.console) {
		sc->sc_console = sc->sc_enabled = 1;
		wskbd_cnattach(&hilkbd_consops, sc, a.keymap);
	} else {
		sc->sc_console = sc->sc_enabled = 0;
	}

	sc->sc_wskbddev = config_found(self, &a, wskbddevprint);

	/*
	 * If this is an old keyboard with a numeric pad but no ``num lock''
	 * key, simulate it being pressed so that the keyboard runs in
	 * numeric mode.
	 */
	if (!ps2 && sc->sc_wskbddev != NULL) {
		wskbd_input(sc->sc_wskbddev, WSCONS_EVENT_KEY_DOWN, 80);
		wskbd_input(sc->sc_wskbddev, WSCONS_EVENT_KEY_UP, 80);
	}
}

int
hilkbddetach(struct device *self, int flags)
{
	struct hilkbd_softc *sc = (void *)self;

	/*
	 * Handle console keyboard for the best. It should have been set
	 * as the first device in the loop anyways.
	 */
	if (sc->sc_console) {
		seen_hilkbd_console = 0;
	}

	if (sc->sc_wskbddev != NULL)
		return config_detach(sc->sc_wskbddev, flags);

	return (0);
}

int
hilkbd_enable(void *v, int on)
{
	struct hilkbd_softc *sc = v;

	if (on) {
		if (sc->sc_enabled)
			return (EBUSY);
	} else {
		if (sc->sc_console)
			return (EBUSY);
	}

	sc->sc_enabled = on;

	return (0);
}

void
hilkbd_set_leds(void *v, int leds)
{
	struct hilkbd_softc *sc = v;
	int changemask;

	if (sc->sc_numleds == 0)
		return;

	changemask = leds ^ sc->sc_ledstate;
	if (changemask == 0)
		return;

	/* We do not handle more than 3 leds here */
	if (changemask & WSKBD_LED_SCROLL)
		send_hildev_cmd((struct hildev_softc *)sc,
		    (leds & WSKBD_LED_SCROLL) ? HIL_PROMPT1 : HIL_ACK1,
		    NULL, NULL);
	if (changemask & WSKBD_LED_NUM)
		send_hildev_cmd((struct hildev_softc *)sc,
		    (leds & WSKBD_LED_NUM) ? HIL_PROMPT2 : HIL_ACK2,
		    NULL, NULL);
	if (changemask & WSKBD_LED_CAPS)
		send_hildev_cmd((struct hildev_softc *)sc,
		    (leds & WSKBD_LED_CAPS) ? HIL_PROMPT3 : HIL_ACK3,
		    NULL, NULL);

	sc->sc_ledstate = leds;
}

int
hilkbd_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct hilkbd_softc *sc = v;

	switch (cmd) {
	case WSKBDIO_GTYPE:
		*(int *)data = WSKBD_TYPE_HIL;
		return 0;
	case WSKBDIO_SETLEDS:
		hilkbd_set_leds(v, *(int *)data);
		return 0;
	case WSKBDIO_GETLEDS:
		*(int *)data = sc->sc_ledstate;
		return 0;
#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE:
		sc->sc_rawkbd = *(int *)data == WSKBD_RAW;
		return 0;
#endif
	case WSKBDIO_COMPLEXBELL:
#define	d ((struct wskbd_bell_data *)data)
		hilkbd_bell((struct hil_softc *)sc->hd_parent,
		    d->pitch, d->period, d->volume);
#undef d
		return 0;
	}

	return -1;
}

void
hilkbd_cngetc(void *v, u_int *type, int *data)
{
	struct hilkbd_softc *sc = v;
	u_int8_t c, stat;

	for (;;) {
		while (hil_poll_data((struct hildev_softc *)sc, &stat, &c) != 0)
			;

		/*
		 * Disregard keyboard data packet header.
		 * Note that no key generates it, so we're safe.
		 */
		if (c != HIL_KBDBUTTON)
			break;
	}

	hilkbd_decode(sc, c, type, data, HIL_KBDBUTTON);
}

void
hilkbd_cnpollc(void *v, int on)
{
	struct hilkbd_softc *sc = v;

	hil_set_poll((struct hil_softc *)sc->hd_parent, on);
}

void
hilkbd_cnbell(void *v, u_int pitch, u_int period, u_int volume)
{
	struct hilkbd_softc *sc = v;

	hilkbd_bell((struct hil_softc *)sc->hd_parent,
	    pitch, period, volume);
}

void
hilkbd_bell(struct hil_softc *sc, u_int pitch, u_int period, u_int volume)
{
	u_int8_t buf[2];

	/* XXX there could be at least a pitch -> HIL pitch conversion here */
#define	BELLDUR		80	/* tone duration in msec (10-2560) */
#define	BELLFREQ	8	/* tone frequency (0-63) */
	buf[0] = ar_format(period - 10);
	buf[1] = BELLFREQ;
	send_hil_cmd(sc, HIL_SETTONE, buf, 2, NULL);
}

void
hilkbd_callback(struct hildev_softc *dev, u_int buflen, u_int8_t *buf)
{
	struct hilkbd_softc *sc = (struct hilkbd_softc *)dev;
	u_int type;
	int kbdtype, key;
	int i, s;

	/*
	 * Ignore packet if we don't need it
	 */
	if (sc->sc_enabled == 0)
		return;

	if (buflen == 0)
		return;
	switch ((kbdtype = *buf & HIL_KBDDATA)) {
	case HIL_BUTTONBOX:
	case HIL_KBDBUTTON:
		break;
	default:
		return;
	}

#ifdef WSDISPLAY_COMPAT_RAWKBD
	if (sc->sc_rawkbd) {
		u_char cbuf[HILBUFSIZE * 2];
		int c, j = 0;

		for (i = 1, buf++; i < buflen; i++) {
			hilkbd_decode(sc, *buf++, &type, &key, kbdtype);
			c = hilkbd_raw[key];
			if (c == RAWKEY_Null)
				continue;
			/* fake extended scancode if necessary */
			if (c & 0x80)
				cbuf[j++] = 0xe0;
			cbuf[j] = c & 0x7f;
			if (type == WSCONS_EVENT_KEY_UP)
				cbuf[j] |= 0x80;
			j++;
		}

		s = spltty();
		wskbd_rawinput(sc->sc_wskbddev, cbuf, j);
		splx(s);
	} else
#endif
	{
		s = spltty();
		for (i = 1, buf++; i < buflen; i++) {
			hilkbd_decode(sc, *buf++, &type, &key, kbdtype);
			if (sc->sc_wskbddev != NULL)
				wskbd_input(sc->sc_wskbddev, type, key);
		}
		splx(s);
	}
}

void
hilkbd_decode(struct hilkbd_softc *sc, u_int8_t data, u_int *type, int *key,
    int kbdtype)
{
	if (kbdtype == HIL_BUTTONBOX) {
		if (data == 0x02)	/* repeat arrow */
			data = sc->sc_lastarrow;
		else if (data >= 0xf8)
			sc->sc_lastarrow = data;
	}

	*type = (data & 1) ? WSCONS_EVENT_KEY_UP : WSCONS_EVENT_KEY_DOWN;
	*key = data >> 1;
}

int
hilkbd_is_console(int hil_is_console)
{
	/* if not first hil keyboard, then not the console */
	if (seen_hilkbd_console)
		return (0);

	/* if PDC console does not match hil bus path, then not the console */
	if (hil_is_console == 0)
		return (0);

	seen_hilkbd_console = 1;
	return (1);
}
