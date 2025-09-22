/* $OpenBSD: ics2101.c,v 1.10 2021/03/07 06:17:03 jsg Exp $ */
/* $NetBSD: ics2101.c,v 1.6 1997/10/09 07:57:23 jtc Exp $ */

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein and John Kohl.
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

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/ics2101reg.h>
#include <dev/isa/ics2101var.h>


#define ICS_VALUE	0x01
#define ICS_MUTE	0x02
#define ICS_MUTE_MUTED	0x04

/* convert from [AUDIO_MIN_GAIN,AUDIO_MAX_GAIN] (0,255) to
   [ICSMIX_MAX_ATTN,ICSMIX_MIN_ATTN] (0,127) */

#define cvt_value(val) ((val) >> 1)

static void ics2101_mix_doit(struct ics2101_softc *, u_int, u_int, u_int,
    u_int);
/*
 * Program one channel of the ICS mixer
 */


static void
ics2101_mix_doit(struct ics2101_softc *sc, u_int chan, u_int side, u_int value,
    u_int flags)
{
	bus_space_tag_t iot = sc->sc_iot;
	unsigned char flip_left[6] = {0x01, 0x01, 0x01, 0x02, 0x01, 0x02};
	unsigned char flip_right[6] = {0x02, 0x02, 0x02, 0x01, 0x02, 0x01};
	register unsigned char ctrl_addr;
	register unsigned char attn_addr;
	register unsigned char normal;

	if (chan < ICSMIX_CHAN_0 || chan > ICSMIX_CHAN_5)
		return;
	if (side != ICSMIX_LEFT && side != ICSMIX_RIGHT)
		return;

	if (flags & ICS_MUTE) {
		value = cvt_value(sc->sc_setting[chan][side]);
		sc->sc_mute[chan][side] = flags & ICS_MUTE_MUTED;
	} else if (flags & ICS_VALUE) {
		sc->sc_setting[chan][side] = value;
		value = cvt_value(value);
		if (value > ICSMIX_MIN_ATTN)
			value = ICSMIX_MIN_ATTN;
	} else
		return;

	ctrl_addr = chan << 3;
	attn_addr = chan << 3;

	if (side == ICSMIX_LEFT) {
		ctrl_addr |= ICSMIX_CTRL_LEFT;
		attn_addr |= ICSMIX_ATTN_LEFT;
		if (sc->sc_mute[chan][side])
			normal = 0x0;
		else if (sc->sc_flags & ICS_FLIP)
			normal = flip_left[chan];
		else
			normal = 0x01;
	} else {
		ctrl_addr |= ICSMIX_CTRL_RIGHT;
		attn_addr |= ICSMIX_ATTN_RIGHT;
		if (sc->sc_mute[chan][side])
			normal = 0x0;
		else if (sc->sc_flags & ICS_FLIP)
			normal = flip_right[chan];
		else
			normal = 0x02;
	}

	mtx_enter(&audio_lock);

	bus_space_write_1(iot, sc->sc_selio_ioh, sc->sc_selio, ctrl_addr);
	bus_space_write_1(iot, sc->sc_dataio_ioh, sc->sc_dataio, normal);

	bus_space_write_1(iot, sc->sc_selio_ioh, sc->sc_selio, attn_addr);
	bus_space_write_1(iot, sc->sc_dataio_ioh, sc->sc_dataio, (unsigned char) value);

	mtx_leave(&audio_lock);
}

void
ics2101_mix_mute(struct ics2101_softc *sc, unsigned int chan, unsigned int side,
    unsigned int domute)
{
    ics2101_mix_doit(sc, chan, side, 0,
		     domute ? ICS_MUTE|ICS_MUTE_MUTED : ICS_MUTE);
}

void
ics2101_mix_attenuate(struct ics2101_softc *sc, unsigned int chan,
    unsigned int side, unsigned int value)
{
    ics2101_mix_doit(sc, chan, side, value, ICS_VALUE);
}
