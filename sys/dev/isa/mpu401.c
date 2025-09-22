/*	$OpenBSD: mpu401.c,v 1.17 2022/03/21 19:22:40 miod Exp $	*/
/*	$NetBSD: mpu401.c,v 1.3 1998/11/25 22:17:06 augustss Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/buf.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/audio_if.h>
#include <dev/midi_if.h>

#include <dev/isa/isavar.h>

#include <dev/ic/mpuvar.h>

#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (mpu401debug) printf x
#define DPRINTFN(n,x)	if (mpu401debug >= (n)) printf x
int	mpu401debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define MPU_GETSTATUS(iot, ioh) (bus_space_read_1(iot, ioh, MPU_STATUS))

int	mpu_reset(struct mpu_softc *);
static	__inline int mpu_waitready(struct mpu_softc *);
void	mpu_readinput(struct mpu_softc *);

struct cfdriver mpu_cd = {
	NULL, "mpu", DV_DULL
};

const struct midi_hw_if mpu_midi_hw_if = {
	mpu_open,
	mpu_close,
	mpu_output,
	0,			/* flush */
	mpu_getinfo,
	0,                      /* ioctl */
};

int
mpu_find(void *v)
{
	struct mpu_softc *sc = v;

	if (MPU_GETSTATUS(sc->iot, sc->ioh) == 0xff) {
		DPRINTF(("mpu_find: No status\n"));
		goto bad;
	}
	sc->open = 0;
	sc->intr = 0;
	if (mpu_reset(sc) == 0)
		return 1;
bad:
	return 0;
}

static __inline int
mpu_waitready(struct mpu_softc *sc)
{
	int i;

	for(i = 0; i < MPU_MAXWAIT; i++) {
		if (!(MPU_GETSTATUS(sc->iot, sc->ioh) & MPU_OUTPUT_BUSY))
			return 0;
		delay(10);
	}
	return 1;
}

int
mpu_reset(struct mpu_softc *sc)
{
	bus_space_tag_t iot = sc->iot;
	bus_space_handle_t ioh = sc->ioh;
	int i;

	if (mpu_waitready(sc)) {
		DPRINTF(("mpu_reset: not ready\n"));
		return EIO;
	}
	mtx_enter(&audio_lock);	/* Don't let the interrupt get our ACK. */
	bus_space_write_1(iot, ioh, MPU_COMMAND, MPU_RESET);
	for(i = 0; i < 2*MPU_MAXWAIT; i++) {
		if (!(MPU_GETSTATUS(iot, ioh) & MPU_INPUT_EMPTY) &&
		    bus_space_read_1(iot, ioh, MPU_DATA) == MPU_ACK) {
			mtx_leave(&audio_lock);
			return 0;
		}
	}
	mtx_leave(&audio_lock);
	DPRINTF(("mpu_reset: No ACK\n"));
	return EIO;
}

int
mpu_open(void *v, int flags, void (*iintr)(void *, int), void (*ointr)(void *),
    void *arg)
{
	struct mpu_softc *sc = v;

        DPRINTF(("mpu_open: sc=%p\n", sc));

	if (sc->open)
		return EBUSY;
	if (mpu_reset(sc) != 0)
		return EIO;

	bus_space_write_1(sc->iot, sc->ioh, MPU_COMMAND, MPU_UART_MODE);
	sc->open = 1;
	sc->intr = iintr;
	sc->arg = arg;
	return 0;
}

void
mpu_close(void *v)
{
	struct mpu_softc *sc = v;

        DPRINTF(("mpu_close: sc=%p\n", sc));

	sc->open = 0;
	sc->intr = 0;
	mpu_reset(sc); /* exit UART mode */
}

void
mpu_readinput(struct mpu_softc *sc)
{
	bus_space_tag_t iot = sc->iot;
	bus_space_handle_t ioh = sc->ioh;
	int data;

	while(!(MPU_GETSTATUS(iot, ioh) & MPU_INPUT_EMPTY)) {
		data = bus_space_read_1(iot, ioh, MPU_DATA);
		DPRINTFN(3, ("mpu_rea: sc=%p 0x%02x\n", sc, data));
		if (sc->intr)
			sc->intr(sc->arg, data);
	}
}

/*
 * called with audio_lock
 */
int
mpu_output(void *v, int d)
{
	struct mpu_softc *sc = v;

	DPRINTFN(3, ("mpu_output: sc=%p 0x%02x\n", sc, d));
	if (!(MPU_GETSTATUS(sc->iot, sc->ioh) & MPU_INPUT_EMPTY)) {
		mpu_readinput(sc);
	}
	if (MPU_GETSTATUS(sc->iot, sc->ioh) & MPU_OUTPUT_BUSY)
		delay(10);
	if (MPU_GETSTATUS(sc->iot, sc->ioh) & MPU_OUTPUT_BUSY)
		return 0;
	bus_space_write_1(sc->iot, sc->ioh, MPU_DATA, d);
	return 1;
}

void
mpu_getinfo(void *addr, struct midi_info *mi)
{
	mi->name = "MPU-401 MIDI UART";
	mi->props = 0;
}

int
mpu_intr(void *v)
{
	struct mpu_softc *sc = v;

	mtx_enter(&audio_lock);
	if (MPU_GETSTATUS(sc->iot, sc->ioh) & MPU_INPUT_EMPTY) {
		mtx_leave(&audio_lock);
		DPRINTF(("mpu_intr: no data\n"));
		return 0;
	}
	mpu_readinput(sc);
	mtx_leave(&audio_lock);
	return 1;
}
