/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2015 Nahanni Systems Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <pthread.h>
#include <pthread_np.h>

#include "atkbdc.h"
#include "console.h"

/* keyboard device commands */
#define	PS2KC_RESET_DEV		0xff
#define	PS2KC_DISABLE		0xf5
#define	PS2KC_ENABLE		0xf4
#define	PS2KC_SET_TYPEMATIC	0xf3
#define	PS2KC_SEND_DEV_ID	0xf2
#define	PS2KC_SET_SCANCODE_SET	0xf0
#define	PS2KC_ECHO		0xee
#define	PS2KC_SET_LEDS		0xed

#define	PS2KC_BAT_SUCCESS	0xaa
#define	PS2KC_ACK		0xfa

#define	PS2KBD_FIFOSZ		16

struct fifo {
	uint8_t	buf[PS2KBD_FIFOSZ];
	int	rindex;		/* index to read from */
	int	windex;		/* index to write to */
	int	num;		/* number of bytes in the fifo */
	int	size;		/* size of the fifo */
};

struct ps2kbd_softc {
	struct atkbdc_softc	*atkbdc_sc;
	pthread_mutex_t		mtx;

	bool			enabled;
	struct fifo		fifo;

	uint8_t			curcmd;	/* current command for next byte */
};

#define SCANCODE_E0_PREFIX 1
struct extended_translation {
	uint32_t keysym;
	uint8_t scancode;
	int flags;
};

/*
 * FIXME: Pause/break and Print Screen/SysRq require special handling.
 */
static const struct extended_translation extended_translations[] = {
		{0xff08, 0x66},		/* Back space */
		{0xff09, 0x0d},		/* Tab */
		{0xff0d, 0x5a},		/* Return */
		{0xff1b, 0x76},		/* Escape */
		{0xff50, 0x6c, SCANCODE_E0_PREFIX}, 	/* Home */
		{0xff51, 0x6b, SCANCODE_E0_PREFIX}, 	/* Left arrow */
		{0xff52, 0x75, SCANCODE_E0_PREFIX}, 	/* Up arrow */
		{0xff53, 0x74, SCANCODE_E0_PREFIX}, 	/* Right arrow */
		{0xff54, 0x72, SCANCODE_E0_PREFIX}, 	/* Down arrow */
		{0xff55, 0x7d, SCANCODE_E0_PREFIX}, 	/* PgUp */
		{0xff56, 0x7a, SCANCODE_E0_PREFIX}, 	/* PgDown */
		{0xff57, 0x69, SCANCODE_E0_PREFIX}, 	/* End */
		{0xff63, 0x70, SCANCODE_E0_PREFIX}, 	/* Ins */
		{0xff8d, 0x5a, SCANCODE_E0_PREFIX}, 	/* Keypad Enter */
		{0xffe1, 0x12},		/* Left shift */
		{0xffe2, 0x59},		/* Right shift */
		{0xffe3, 0x14},		/* Left control */
		{0xffe4, 0x14, SCANCODE_E0_PREFIX}, 	/* Right control */
		/* {0xffe7, XXX}, Left meta */
		/* {0xffe8, XXX}, Right meta */
		{0xffe9, 0x11},		/* Left alt */
		{0xfe03, 0x11, SCANCODE_E0_PREFIX}, 	/* AltGr */
		{0xffea, 0x11, SCANCODE_E0_PREFIX}, 	/* Right alt */
		{0xffeb, 0x1f, SCANCODE_E0_PREFIX}, 	/* Left Windows */
		{0xffec, 0x27, SCANCODE_E0_PREFIX}, 	/* Right Windows */
		{0xffbe, 0x05},		/* F1 */
		{0xffbf, 0x06},		/* F2 */
		{0xffc0, 0x04},		/* F3 */
		{0xffc1, 0x0c},		/* F4 */
		{0xffc2, 0x03},		/* F5 */
		{0xffc3, 0x0b},		/* F6 */
		{0xffc4, 0x83},		/* F7 */
		{0xffc5, 0x0a},		/* F8 */
		{0xffc6, 0x01},		/* F9 */
		{0xffc7, 0x09},		/* F10 */
		{0xffc8, 0x78},		/* F11 */
		{0xffc9, 0x07},		/* F12 */
		{0xffff, 0x71, SCANCODE_E0_PREFIX},	/* Del */
		{0xff14, 0x7e},		/* ScrollLock */
		/* NumLock and Keypads*/
		{0xff7f, 0x77}, 	/* NumLock */
		{0xffaf, 0x4a, SCANCODE_E0_PREFIX}, 	/* Keypad slash */
		{0xffaa, 0x7c}, 	/* Keypad asterisk */
		{0xffad, 0x7b}, 	/* Keypad minus */
		{0xffab, 0x79}, 	/* Keypad plus */
		{0xffb7, 0x6c}, 	/* Keypad 7 */
		{0xff95, 0x6c}, 	/* Keypad home */
		{0xffb8, 0x75}, 	/* Keypad 8 */
		{0xff97, 0x75}, 	/* Keypad up arrow */
		{0xffb9, 0x7d}, 	/* Keypad 9 */
		{0xff9a, 0x7d}, 	/* Keypad PgUp */
		{0xffb4, 0x6b}, 	/* Keypad 4 */
		{0xff96, 0x6b}, 	/* Keypad left arrow */
		{0xffb5, 0x73}, 	/* Keypad 5 */
		{0xff9d, 0x73}, 	/* Keypad empty */
		{0xffb6, 0x74}, 	/* Keypad 6 */
		{0xff98, 0x74}, 	/* Keypad right arrow */
		{0xffb1, 0x69}, 	/* Keypad 1 */
		{0xff9c, 0x69}, 	/* Keypad end */
		{0xffb2, 0x72}, 	/* Keypad 2 */
		{0xff99, 0x72}, 	/* Keypad down arrow */
		{0xffb3, 0x7a}, 	/* Keypad 3 */
		{0xff9b, 0x7a}, 	/* Keypad PgDown */
		{0xffb0, 0x70}, 	/* Keypad 0 */
		{0xff9e, 0x70}, 	/* Keypad ins */
		{0xffae, 0x71}, 	/* Keypad . */
		{0xff9f, 0x71}, 	/* Keypad del */
		{0, 0, 0} 		/* Terminator */
};

/* ASCII to type 2 scancode lookup table */
static const uint8_t ascii_translations[128] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x29, 0x16, 0x52, 0x26, 0x25, 0x2e, 0x3d, 0x52,
		0x46, 0x45, 0x3e, 0x55, 0x41, 0x4e, 0x49, 0x4a,
		0x45, 0x16, 0x1e, 0x26, 0x25, 0x2e, 0x36, 0x3d,
		0x3e, 0x46, 0x4c, 0x4c, 0x41, 0x55, 0x49, 0x4a,
		0x1e, 0x1c, 0x32, 0x21, 0x23, 0x24, 0x2b, 0x34,
		0x33, 0x43, 0x3b, 0x42, 0x4b, 0x3a, 0x31, 0x44,
		0x4d, 0x15, 0x2d, 0x1b, 0x2c, 0x3c, 0x2a, 0x1d,
		0x22, 0x35, 0x1a, 0x54, 0x5d, 0x5b, 0x36, 0x4e,
		0x0e, 0x1c, 0x32, 0x21, 0x23, 0x24, 0x2b, 0x34,
		0x33, 0x43, 0x3b, 0x42, 0x4b, 0x3a, 0x31, 0x44,
		0x4d, 0x15, 0x2d, 0x1b, 0x2c, 0x3c, 0x2a, 0x1d,
		0x22, 0x35, 0x1a, 0x54, 0x5d, 0x5b, 0x0e, 0x00,
};

static void
fifo_init(struct ps2kbd_softc *sc)
{
	struct fifo *fifo;

	fifo = &sc->fifo;
	fifo->size = sizeof(((struct fifo *)0)->buf);
}

static void
fifo_reset(struct ps2kbd_softc *sc)
{
	struct fifo *fifo;

	fifo = &sc->fifo;
	bzero(fifo, sizeof(struct fifo));
	fifo->size = sizeof(((struct fifo *)0)->buf);
}

static void
fifo_put(struct ps2kbd_softc *sc, uint8_t val)
{
	struct fifo *fifo;

	fifo = &sc->fifo;
	if (fifo->num < fifo->size) {
		fifo->buf[fifo->windex] = val;
		fifo->windex = (fifo->windex + 1) % fifo->size;
		fifo->num++;
	}
}

static int
fifo_get(struct ps2kbd_softc *sc, uint8_t *val)
{
	struct fifo *fifo;

	fifo = &sc->fifo;
	if (fifo->num > 0) {
		*val = fifo->buf[fifo->rindex];
		fifo->rindex = (fifo->rindex + 1) % fifo->size;
		fifo->num--;
		return (0);
	}

	return (-1);
}

int
ps2kbd_read(struct ps2kbd_softc *sc, uint8_t *val)
{
	int retval;

	pthread_mutex_lock(&sc->mtx);
	retval = fifo_get(sc, val);
	pthread_mutex_unlock(&sc->mtx);

	return (retval);
}

void
ps2kbd_write(struct ps2kbd_softc *sc, uint8_t val)
{
	pthread_mutex_lock(&sc->mtx);
	if (sc->curcmd) {
		switch (sc->curcmd) {
		case PS2KC_SET_TYPEMATIC:
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_SET_SCANCODE_SET:
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_SET_LEDS:
			fifo_put(sc, PS2KC_ACK);
			break;
		default:
			fprintf(stderr, "Unhandled ps2 keyboard current "
			    "command byte 0x%02x\n", val);
			break;
		}
		sc->curcmd = 0;
	} else {
		switch (val) {
		case 0x00:
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_RESET_DEV:
			fifo_reset(sc);
			fifo_put(sc, PS2KC_ACK);
			fifo_put(sc, PS2KC_BAT_SUCCESS);
			break;
		case PS2KC_DISABLE:
			sc->enabled = false;
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_ENABLE:
			sc->enabled = true;
			fifo_reset(sc);
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_SET_TYPEMATIC:
			sc->curcmd = val;
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_SEND_DEV_ID:
			fifo_put(sc, PS2KC_ACK);
			fifo_put(sc, 0xab);
			fifo_put(sc, 0x83);
			break;
		case PS2KC_SET_SCANCODE_SET:
			sc->curcmd = val;
			fifo_put(sc, PS2KC_ACK);
			break;
		case PS2KC_ECHO:
			fifo_put(sc, PS2KC_ECHO);
			break;
		case PS2KC_SET_LEDS:
			sc->curcmd = val;
			fifo_put(sc, PS2KC_ACK);
			break;
		default:
			fprintf(stderr, "Unhandled ps2 keyboard command "
			    "0x%02x\n", val);
			break;
		}
	}
	pthread_mutex_unlock(&sc->mtx);
}

/*
 * Translate keysym to type 2 scancode and insert into keyboard buffer.
 */
static void
ps2kbd_keysym_queue(struct ps2kbd_softc *sc,
    int down, uint32_t keysym)
{
	assert(pthread_mutex_isowned_np(&sc->mtx));
	int e0_prefix, found;
	uint8_t code;
	const struct extended_translation *trans;

	found = 0;
	if (keysym < 0x80) {
		code = ascii_translations[keysym];
		e0_prefix = 0;
		found = 1;
	} else {
		for (trans = &(extended_translations[0]); trans->keysym != 0;
		    trans++) {
			if (keysym == trans->keysym) {
				code = trans->scancode;
				e0_prefix = trans->flags & SCANCODE_E0_PREFIX;
				found = 1;
				break;
			}
		}
	}

	if (!found) {
		fprintf(stderr, "Unhandled ps2 keyboard keysym 0x%x\n", keysym);
		return;
	}

	if (e0_prefix)
		fifo_put(sc, 0xe0);
	if (!down)
		fifo_put(sc, 0xf0);
	fifo_put(sc, code);
}

static void
ps2kbd_event(int down, uint32_t keysym, void *arg)
{
	struct ps2kbd_softc *sc = arg;
	int fifo_full;

	pthread_mutex_lock(&sc->mtx);
	if (!sc->enabled) {
		pthread_mutex_unlock(&sc->mtx);
		return;
	}
	fifo_full = sc->fifo.num == PS2KBD_FIFOSZ;
	ps2kbd_keysym_queue(sc, down, keysym);
	pthread_mutex_unlock(&sc->mtx);

	if (!fifo_full)
		atkbdc_event(sc->atkbdc_sc, 1);
}

struct ps2kbd_softc *
ps2kbd_init(struct atkbdc_softc *atkbdc_sc)
{
	struct ps2kbd_softc *sc;

	sc = calloc(1, sizeof (struct ps2kbd_softc));
	pthread_mutex_init(&sc->mtx, NULL);
	fifo_init(sc);
	sc->atkbdc_sc = atkbdc_sc;

	console_kbd_register(ps2kbd_event, sc, 1);

	return (sc);
}

