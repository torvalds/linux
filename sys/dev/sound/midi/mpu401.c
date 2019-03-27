/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mathew Kanner
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/bus.h>			/* to get driver_intr_t */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/midi/mpu401.h>
#include <dev/sound/midi/midi.h>

#include "mpu_if.h"
#include "mpufoi_if.h"

#ifndef KOBJMETHOD_END
#define KOBJMETHOD_END	{ NULL, NULL }
#endif

#define MPU_DATAPORT   0
#define MPU_CMDPORT    1
#define MPU_STATPORT   1
#define MPU_RESET      0xff
#define MPU_UART       0x3f
#define MPU_ACK        0xfe
#define MPU_STATMASK   0xc0
#define MPU_OUTPUTBUSY 0x40
#define MPU_INPUTBUSY  0x80
#define MPU_TRYDATA 50
#define MPU_DELAY   2500

#define CMD(m,d)	MPUFOI_WRITE(m, m->cookie, MPU_CMDPORT,d)
#define STATUS(m)	MPUFOI_READ(m, m->cookie, MPU_STATPORT)
#define READ(m)		MPUFOI_READ(m, m->cookie, MPU_DATAPORT)
#define WRITE(m,d)	MPUFOI_WRITE(m, m->cookie, MPU_DATAPORT,d)

struct mpu401 {
	KOBJ_FIELDS;
	struct snd_midi *mid;
	int	flags;
	driver_intr_t *si;
	void   *cookie;
	struct callout timer;
};

static void mpu401_timeout(void *m);
static mpu401_intr_t mpu401_intr;

static int mpu401_minit(struct snd_midi *, void *);
static int mpu401_muninit(struct snd_midi *, void *);
static int mpu401_minqsize(struct snd_midi *, void *);
static int mpu401_moutqsize(struct snd_midi *, void *);
static void mpu401_mcallback(struct snd_midi *, void *, int);
static void mpu401_mcallbackp(struct snd_midi *, void *, int);
static const char *mpu401_mdescr(struct snd_midi *, void *, int);
static const char *mpu401_mprovider(struct snd_midi *, void *);

static kobj_method_t mpu401_methods[] = {
	KOBJMETHOD(mpu_init, mpu401_minit),
	KOBJMETHOD(mpu_uninit, mpu401_muninit),
	KOBJMETHOD(mpu_inqsize, mpu401_minqsize),
	KOBJMETHOD(mpu_outqsize, mpu401_moutqsize),
	KOBJMETHOD(mpu_callback, mpu401_mcallback),
	KOBJMETHOD(mpu_callbackp, mpu401_mcallbackp),
	KOBJMETHOD(mpu_descr, mpu401_mdescr),
	KOBJMETHOD(mpu_provider, mpu401_mprovider),
	KOBJMETHOD_END
};

DEFINE_CLASS(mpu401, mpu401_methods, 0);

void
mpu401_timeout(void *a)
{
	struct mpu401 *m = (struct mpu401 *)a;

	if (m->si)
		(m->si)(m->cookie);

}
static int
mpu401_intr(struct mpu401 *m)
{
#define MPU_INTR_BUF	16
	MIDI_TYPE b[MPU_INTR_BUF];
	int i;
	int s;

/*
	printf("mpu401_intr\n");
*/
#define RXRDY(m) ( (STATUS(m) & MPU_INPUTBUSY) == 0)
#define TXRDY(m) ( (STATUS(m) & MPU_OUTPUTBUSY) == 0)
#if 0
#define D(x,l) printf("mpu401_intr %d %x %s %s\n",l, x, x&MPU_INPUTBUSY?"RX":"", x&MPU_OUTPUTBUSY?"TX":"")
#else
#define D(x,l)
#endif
	i = 0;
	s = STATUS(m);
	D(s, 1);
	while ((s & MPU_INPUTBUSY) == 0 && i < MPU_INTR_BUF) {
		b[i] = READ(m);
/*
		printf("mpu401_intr in i %d d %d\n", i, b[i]);
*/
		i++;
		s = STATUS(m);
	}
	if (i)
		midi_in(m->mid, b, i);
	i = 0;
	while (!(s & MPU_OUTPUTBUSY) && i < MPU_INTR_BUF) {
		if (midi_out(m->mid, b, 1)) {
/*
			printf("mpu401_intr out i %d d %d\n", i, b[0]);
*/

			WRITE(m, *b);
		} else {
/*
			printf("mpu401_intr write: no output\n");
*/
			return 0;
		}
		i++;
		/* DELAY(100); */
		s = STATUS(m);
	}

	if ((m->flags & M_TXEN) && (m->si)) {
		callout_reset(&m->timer, 1, mpu401_timeout, m);
	}
	return (m->flags & M_TXEN) == M_TXEN;
}

struct mpu401 *
mpu401_init(kobj_class_t cls, void *cookie, driver_intr_t softintr,
    mpu401_intr_t ** cb)
{
	struct mpu401 *m;

	*cb = NULL;
	m = malloc(sizeof(*m), M_MIDI, M_NOWAIT | M_ZERO);

	if (!m)
		return NULL;

	kobj_init((kobj_t)m, cls);

	callout_init(&m->timer, 1);

	m->si = softintr;
	m->cookie = cookie;
	m->flags = 0;

	m->mid = midi_init(&mpu401_class, 0, 0, m);
	if (!m->mid)
		goto err;
	*cb = mpu401_intr;
	return m;
err:
	printf("mpu401_init error\n");
	free(m, M_MIDI);
	return NULL;
}

int
mpu401_uninit(struct mpu401 *m)
{
	int retval;

	CMD(m, MPU_RESET);
	retval = midi_uninit(m->mid);
	if (retval)
		return retval;
	free(m, M_MIDI);
	return 0;
}

static int
mpu401_minit(struct snd_midi *sm, void *arg)
{
	struct mpu401 *m = arg;
	int i;

	CMD(m, MPU_RESET);
	CMD(m, MPU_UART);
	return 0;
	i = 0;
	while (++i < 2000) {
		if (RXRDY(m))
			if (READ(m) == MPU_ACK)
				break;
	}

	if (i < 2000) {
		CMD(m, MPU_UART);
		return 0;
	}
	printf("mpu401_minit failed active sensing\n");
	return 1;
}


int
mpu401_muninit(struct snd_midi *sm, void *arg)
{
	struct mpu401 *m = arg;

	return MPUFOI_UNINIT(m, m->cookie);
}

int
mpu401_minqsize(struct snd_midi *sm, void *arg)
{
	return 128;
}

int
mpu401_moutqsize(struct snd_midi *sm, void *arg)
{
	return 128;
}

static void
mpu401_mcallback(struct snd_midi *sm, void *arg, int flags)
{
	struct mpu401 *m = arg;
#if 0
	printf("mpu401_callback %s %s %s %s\n",
	    flags & M_RX ? "M_RX" : "",
	    flags & M_TX ? "M_TX" : "",
	    flags & M_RXEN ? "M_RXEN" : "",
	    flags & M_TXEN ? "M_TXEN" : "");
#endif
	if (flags & M_TXEN && m->si) {
		callout_reset(&m->timer, 1, mpu401_timeout, m);
	}
	m->flags = flags;
}

static void
mpu401_mcallbackp(struct snd_midi *sm, void *arg, int flags)
{
/*	printf("mpu401_callbackp\n"); */
	mpu401_mcallback(sm, arg, flags);
}

static const char *
mpu401_mdescr(struct snd_midi *sm, void *arg, int verbosity)
{

	return "descr mpu401";
}

static const char *
mpu401_mprovider(struct snd_midi *m, void *arg)
{
	return "provider mpu401";
}
