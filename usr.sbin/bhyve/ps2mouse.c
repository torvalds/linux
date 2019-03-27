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

/* mouse device commands */
#define	PS2MC_RESET_DEV		0xff
#define	PS2MC_SET_DEFAULTS	0xf6
#define	PS2MC_DISABLE		0xf5
#define	PS2MC_ENABLE		0xf4
#define	PS2MC_SET_SAMPLING_RATE	0xf3
#define	PS2MC_SEND_DEV_ID	0xf2
#define	PS2MC_SET_REMOTE_MODE	0xf0
#define	PS2MC_SEND_DEV_DATA	0xeb
#define	PS2MC_SET_STREAM_MODE	0xea
#define	PS2MC_SEND_DEV_STATUS	0xe9
#define	PS2MC_SET_RESOLUTION	0xe8
#define	PS2MC_SET_SCALING1	0xe7
#define	PS2MC_SET_SCALING2	0xe6

#define	PS2MC_BAT_SUCCESS	0xaa
#define	PS2MC_ACK		0xfa

/* mouse device id */
#define	PS2MOUSE_DEV_ID		0x0

/* mouse data bits */
#define	PS2M_DATA_Y_OFLOW	0x80
#define	PS2M_DATA_X_OFLOW	0x40
#define	PS2M_DATA_Y_SIGN	0x20
#define	PS2M_DATA_X_SIGN	0x10
#define	PS2M_DATA_AONE		0x08
#define	PS2M_DATA_MID_BUTTON	0x04
#define	PS2M_DATA_RIGHT_BUTTON	0x02
#define	PS2M_DATA_LEFT_BUTTON	0x01

/* mouse status bits */
#define	PS2M_STS_REMOTE_MODE	0x40
#define	PS2M_STS_ENABLE_DEV	0x20
#define	PS2M_STS_SCALING_21	0x10
#define	PS2M_STS_MID_BUTTON	0x04
#define	PS2M_STS_RIGHT_BUTTON	0x02
#define	PS2M_STS_LEFT_BUTTON	0x01

#define	PS2MOUSE_FIFOSZ		16

struct fifo {
	uint8_t	buf[PS2MOUSE_FIFOSZ];
	int	rindex;		/* index to read from */
	int	windex;		/* index to write to */
	int	num;		/* number of bytes in the fifo */
	int	size;		/* size of the fifo */
};

struct ps2mouse_softc {
	struct atkbdc_softc	*atkbdc_sc;
	pthread_mutex_t		mtx;

	uint8_t		status;
	uint8_t		resolution;
	uint8_t		sampling_rate;
	int		ctrlenable;
	struct fifo	fifo;

	uint8_t		curcmd;	/* current command for next byte */

	int		cur_x, cur_y;
	int		delta_x, delta_y;
};

static void
fifo_init(struct ps2mouse_softc *sc)
{
	struct fifo *fifo;

	fifo = &sc->fifo;
	fifo->size = sizeof(((struct fifo *)0)->buf);
}

static void
fifo_reset(struct ps2mouse_softc *sc)
{
	struct fifo *fifo;

	fifo = &sc->fifo;
	bzero(fifo, sizeof(struct fifo));
	fifo->size = sizeof(((struct fifo *)0)->buf);
}

static void
fifo_put(struct ps2mouse_softc *sc, uint8_t val)
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
fifo_get(struct ps2mouse_softc *sc, uint8_t *val)
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

static void
movement_reset(struct ps2mouse_softc *sc)
{
	assert(pthread_mutex_isowned_np(&sc->mtx));

	sc->delta_x = 0;
	sc->delta_y = 0;
}

static void
movement_update(struct ps2mouse_softc *sc, int x, int y)
{
	sc->delta_x += x - sc->cur_x;
	sc->delta_y += sc->cur_y - y;
	sc->cur_x = x;
	sc->cur_y = y;
}

static void
movement_get(struct ps2mouse_softc *sc)
{
	uint8_t val0, val1, val2;

	assert(pthread_mutex_isowned_np(&sc->mtx));

	val0 = PS2M_DATA_AONE;
	val0 |= sc->status & (PS2M_DATA_LEFT_BUTTON |
	    PS2M_DATA_RIGHT_BUTTON | PS2M_DATA_MID_BUTTON);

	if (sc->delta_x >= 0) {
		if (sc->delta_x > 255) {
			val0 |= PS2M_DATA_X_OFLOW;
			val1 = 255;
		} else
			val1 = sc->delta_x;
	} else {
		val0 |= PS2M_DATA_X_SIGN;
		if (sc->delta_x < -255) {
			val0 |= PS2M_DATA_X_OFLOW;
			val1 = 255;
		} else
			val1 = sc->delta_x;
	}
	sc->delta_x = 0;

	if (sc->delta_y >= 0) {
		if (sc->delta_y > 255) {
			val0 |= PS2M_DATA_Y_OFLOW;
			val2 = 255;
		} else
			val2 = sc->delta_y;
	} else {
		val0 |= PS2M_DATA_Y_SIGN;
		if (sc->delta_y < -255) {
			val0 |= PS2M_DATA_Y_OFLOW;
			val2 = 255;
		} else
			val2 = sc->delta_y;
	}
	sc->delta_y = 0;

	if (sc->fifo.num < (sc->fifo.size - 3)) {
		fifo_put(sc, val0);
		fifo_put(sc, val1);
		fifo_put(sc, val2);
	}
}

static void
ps2mouse_reset(struct ps2mouse_softc *sc)
{
	assert(pthread_mutex_isowned_np(&sc->mtx));
	fifo_reset(sc);
	movement_reset(sc);
	sc->status = PS2M_STS_ENABLE_DEV;
	sc->resolution = 4;
	sc->sampling_rate = 100;

	sc->cur_x = 0;
	sc->cur_y = 0;
	sc->delta_x = 0;
	sc->delta_y = 0;
}

int
ps2mouse_read(struct ps2mouse_softc *sc, uint8_t *val)
{
	int retval;

	pthread_mutex_lock(&sc->mtx);
	retval = fifo_get(sc, val);
	pthread_mutex_unlock(&sc->mtx);

	return (retval);
}

int
ps2mouse_fifocnt(struct ps2mouse_softc *sc)
{
	return (sc->fifo.num);
}

void
ps2mouse_toggle(struct ps2mouse_softc *sc, int enable)
{
	pthread_mutex_lock(&sc->mtx);
	if (enable)
		sc->ctrlenable = 1;
	else {
		sc->ctrlenable = 0;
		sc->fifo.rindex = 0;
		sc->fifo.windex = 0;
		sc->fifo.num = 0;
	}
	pthread_mutex_unlock(&sc->mtx);
}

void
ps2mouse_write(struct ps2mouse_softc *sc, uint8_t val, int insert)
{
	pthread_mutex_lock(&sc->mtx);
	fifo_reset(sc);
	if (sc->curcmd) {
		switch (sc->curcmd) {
		case PS2MC_SET_SAMPLING_RATE:
			sc->sampling_rate = val;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_SET_RESOLUTION:
			sc->resolution = val;
			fifo_put(sc, PS2MC_ACK);
			break;
		default:
			fprintf(stderr, "Unhandled ps2 mouse current "
			    "command byte 0x%02x\n", val);
			break;
		}
		sc->curcmd = 0;

	} else if (insert) {
		fifo_put(sc, val);
	} else {
		switch (val) {
		case 0x00:
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_RESET_DEV:
			ps2mouse_reset(sc);
			fifo_put(sc, PS2MC_ACK);
			fifo_put(sc, PS2MC_BAT_SUCCESS);
			fifo_put(sc, PS2MOUSE_DEV_ID);
			break;
		case PS2MC_SET_DEFAULTS:
			ps2mouse_reset(sc);
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_DISABLE:
			fifo_reset(sc);
			sc->status &= ~PS2M_STS_ENABLE_DEV;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_ENABLE:
			fifo_reset(sc);
			sc->status |= PS2M_STS_ENABLE_DEV;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_SET_SAMPLING_RATE:
			sc->curcmd = val;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_SEND_DEV_ID:
			fifo_put(sc, PS2MC_ACK);
			fifo_put(sc, PS2MOUSE_DEV_ID);
			break;
		case PS2MC_SET_REMOTE_MODE:
			sc->status |= PS2M_STS_REMOTE_MODE;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_SEND_DEV_DATA:
			fifo_put(sc, PS2MC_ACK);
			movement_get(sc);
			break;
		case PS2MC_SET_STREAM_MODE:
			sc->status &= ~PS2M_STS_REMOTE_MODE;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_SEND_DEV_STATUS:
			fifo_put(sc, PS2MC_ACK);
			fifo_put(sc, sc->status);
			fifo_put(sc, sc->resolution);
			fifo_put(sc, sc->sampling_rate);
			break;
		case PS2MC_SET_RESOLUTION:
			sc->curcmd = val;
			fifo_put(sc, PS2MC_ACK);
			break;
		case PS2MC_SET_SCALING1:
		case PS2MC_SET_SCALING2:
			fifo_put(sc, PS2MC_ACK);
			break;
		default:
			fifo_put(sc, PS2MC_ACK);
			fprintf(stderr, "Unhandled ps2 mouse command "
			    "0x%02x\n", val);
			break;
		}
	}
	pthread_mutex_unlock(&sc->mtx);
}

static void
ps2mouse_event(uint8_t button, int x, int y, void *arg)
{
	struct ps2mouse_softc *sc = arg;

	pthread_mutex_lock(&sc->mtx);
	movement_update(sc, x, y);

	sc->status &= ~(PS2M_STS_LEFT_BUTTON |
	    PS2M_STS_RIGHT_BUTTON | PS2M_STS_MID_BUTTON);
	if (button & (1 << 0))
		sc->status |= PS2M_STS_LEFT_BUTTON;
	if (button & (1 << 1))
		sc->status |= PS2M_STS_MID_BUTTON;
	if (button & (1 << 2))
		sc->status |= PS2M_STS_RIGHT_BUTTON;

	if ((sc->status & PS2M_STS_ENABLE_DEV) == 0 || !sc->ctrlenable) {
		/* no data reporting */
		pthread_mutex_unlock(&sc->mtx);
		return;
	}

	movement_get(sc);
	pthread_mutex_unlock(&sc->mtx);

	if (sc->fifo.num > 0)
		atkbdc_event(sc->atkbdc_sc, 0);
}

struct ps2mouse_softc *
ps2mouse_init(struct atkbdc_softc *atkbdc_sc)
{
	struct ps2mouse_softc *sc;

	sc = calloc(1, sizeof (struct ps2mouse_softc));
	pthread_mutex_init(&sc->mtx, NULL);
	fifo_init(sc);
	sc->atkbdc_sc = atkbdc_sc;

	pthread_mutex_lock(&sc->mtx);
	ps2mouse_reset(sc);
	pthread_mutex_unlock(&sc->mtx);

	console_ptr_register(ps2mouse_event, sc, 1);

	return (sc);
}


