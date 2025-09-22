/*	$OpenBSD: sunkbd.c,v 1.28 2020/04/06 19:03:09 cheloha Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#ifdef WSDISPLAY_COMPAT_RAWKBD
#include <dev/wscons/wskbdraw.h>
#endif

#include <dev/sun/sunkbdreg.h>
#include <dev/sun/sunkbdvar.h>

#ifdef __sparc64__
#define	NTCTRL 0
#else
#include "tctrl.h"
#endif

#if NTCTRL > 0
#include <sparc/dev/tctrlvar.h>		/* XXX for tadpole_bell() */
#endif

void	sunkbd_bell(struct sunkbd_softc *, u_int, u_int, u_int);
void	sunkbd_decode5(u_int8_t, u_int *, int *);
int	sunkbd_enable(void *, int);
int	sunkbd_getleds(struct sunkbd_softc *);
int	sunkbd_ioctl(void *, u_long, caddr_t, int, struct proc *);
void	sunkbd_setleds(void *, int);

struct wskbd_accessops sunkbd_accessops = {
	sunkbd_enable,
	sunkbd_setleds,
	sunkbd_ioctl
};

void
sunkbd_attach(struct sunkbd_softc *sc, struct wskbddev_attach_args *waa)
{
	if (ISTYPE5(sc->sc_layout))
		sc->sc_decode = sunkbd_decode5;
	else
		sc->sc_decode = sunkbd_decode;

	sc->sc_wskbddev = config_found((struct device *)sc, waa,
	    wskbddevprint);
}

void
sunkbd_bell(struct sunkbd_softc *sc, u_int period, u_int pitch, u_int volume)
{
	int s;
	u_int8_t c = SKBD_CMD_BELLON;

#if NTCTRL > 0
	if (tadpole_bell(period / 10, pitch, volume) != 0)
		return;
#endif

	s = spltty();
	if (sc->sc_bellactive) {
		if (sc->sc_belltimeout == 0)
			timeout_del(&sc->sc_bellto);
	}
	if (pitch == 0 || period == 0) {
		sunkbd_bellstop(sc);
		splx(s);
		return;
	}
	if (sc->sc_bellactive == 0) {
		sc->sc_bellactive = 1;
		sc->sc_belltimeout = 1;
		(*sc->sc_sendcmd)(sc, &c, 1);
		timeout_add_msec(&sc->sc_bellto, period);
	}
	splx(s);
}

void
sunkbd_bellstop(void *v)
{
	struct sunkbd_softc *sc = v;
	int s;
	u_int8_t c;

	s = spltty();
	sc->sc_belltimeout = 0;
	c = SKBD_CMD_BELLOFF;
	(*sc->sc_sendcmd)(v, &c, 1);
	sc->sc_bellactive = 0;
	splx(s);
}

void
sunkbd_decode(u_int8_t c, u_int *type, int *value)
{
	switch (c) {
	case SKBD_RSP_IDLE:
		*type = WSCONS_EVENT_ALL_KEYS_UP;
		*value = 0;
		break;
	default:
		*type = (c & 0x80) ?
		    WSCONS_EVENT_KEY_UP : WSCONS_EVENT_KEY_DOWN;
		*value = c & 0x7f;
		break;
	}
}

void
sunkbd_decode5(u_int8_t c, u_int *type, int *value)
{
	sunkbd_decode(c, type, value);
	/*
	 * Scancode 0x2d is KS_KP_Equal on type 4, and KS_AudioMute on
	 * type 5. Rather than provide two distinct maps, we remap the
	 * scancode here.
	 */
	if (*value == 0x2d)
		*value = 0x7f;
}

int
sunkbd_enable(void *v, int on)
{
	return (0);
}

int
sunkbd_getleds(struct sunkbd_softc *sc)
{
	return (sc->sc_leds);
}

void
sunkbd_input(struct sunkbd_softc *sc, u_int8_t *buf, u_int buflen)
{
	u_int type;
	int value;
	int s;

	if (sc->sc_wskbddev == NULL)
		return;	/* why bother */

#ifdef WSDISPLAY_COMPAT_RAWKBD
	if (sc->sc_rawkbd) {
		u_char rbuf[SUNKBD_MAX_INPUT_SIZE * 2];
		int c, rlen = 0;

		while (buflen-- != 0) {
			(*sc->sc_decode)(*buf++, &type, &value);
			c = sunkbd_rawmap[value];
			if (c == RAWKEY_Null)
				continue;
			/* fake extended scancode if necessary */
			if (c & 0x80)
				rbuf[rlen++] = 0xe0;
			rbuf[rlen] = c & 0x7f;
			if (type == WSCONS_EVENT_KEY_UP)
				rbuf[rlen] |= 0x80;
			rlen++;
		}

		s = spltty();
		wskbd_rawinput(sc->sc_wskbddev, rbuf, rlen);
		splx(s);
	} else
#endif
	{
		s = spltty();
		while (buflen-- != 0) {
			(*sc->sc_decode)(*buf++, &type, &value);
			wskbd_input(sc->sc_wskbddev, type, value);
		}
		splx(s);
	}
}

int
sunkbd_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct sunkbd_softc *sc = v;
	int *d_int = (int *)data;
	struct wskbd_bell_data *d_bell = (struct wskbd_bell_data *)data;

	switch (cmd) {
	case WSKBDIO_GTYPE:
		if (ISTYPE5(sc->sc_layout)) {
			*d_int = WSKBD_TYPE_SUN5;
		} else {
			*d_int = WSKBD_TYPE_SUN;
		}
		return (0);
	case WSKBDIO_SETLEDS:
		sunkbd_setleds(sc, *d_int);
		return (0);
	case WSKBDIO_GETLEDS:
		*d_int = sunkbd_getleds(sc);
		return (0);
	case WSKBDIO_COMPLEXBELL:
		sunkbd_bell(sc, d_bell->period, d_bell->pitch, d_bell->volume);
		return (0);
#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE:
		sc->sc_rawkbd = *(int *)data == WSKBD_RAW;
		return (0);
#endif
	}

	return (-1);
}

void
sunkbd_raw(struct sunkbd_softc *sc, u_int8_t c)
{
	int claimed = 0;

	if (sc->sc_kbdstate == SKBD_STATE_LAYOUT) {
		sc->sc_kbdstate = SKBD_STATE_GETKEY;
		sc->sc_layout = c;
		return;
	}

	switch (c) {
	case SKBD_RSP_RESET:
		sc->sc_kbdstate = SKBD_STATE_RESET;
		claimed = 1;
		break;
	case SKBD_RSP_LAYOUT:
		sc->sc_kbdstate = SKBD_STATE_LAYOUT;
		claimed = 1;
		break;
	case SKBD_RSP_IDLE:
		sc->sc_kbdstate = SKBD_STATE_GETKEY;
		claimed = 1;
	}

	if (claimed)
		return;

	switch (sc->sc_kbdstate) {
	case SKBD_STATE_RESET:
		sc->sc_kbdstate = SKBD_STATE_GETKEY;
		if (c < KB_SUN2 || c > KB_SUN4)
			printf("%s: reset: invalid keyboard type 0x%02x\n",
			    sc->sc_dev.dv_xname, c);
		else
			sc->sc_id = c;
		break;
	case SKBD_STATE_GETKEY:
		break;
	}
}

int
sunkbd_setclick(struct sunkbd_softc *sc, int click)
{
	u_int8_t c;

	/* Type 2 keyboards do not support keyclick */
	if (sc->sc_id == KB_SUN2)
		return (ENXIO);

	c = click ? SKBD_CMD_CLICKON : SKBD_CMD_CLICKOFF;
	(*sc->sc_sendcmd)(sc, &c, 1);
	return (0);
}

void
sunkbd_setleds(void *v, int wled)
{
	struct sunkbd_softc *sc = v;
	u_int8_t sled = 0;
	u_int8_t cmd[2];

	sc->sc_leds = wled;

	if (wled & WSKBD_LED_CAPS)
		sled |= SKBD_LED_CAPSLOCK;
	if (wled & WSKBD_LED_NUM)
		sled |= SKBD_LED_NUMLOCK;
	if (wled & WSKBD_LED_SCROLL)
		sled |= SKBD_LED_SCROLLLOCK;
	if (wled & WSKBD_LED_COMPOSE)
		sled |= SKBD_LED_COMPOSE;

	cmd[0] = SKBD_CMD_SETLED;
	cmd[1] = sled;
	(*sc->sc_sendcmd)(sc, cmd, sizeof(cmd));
}
