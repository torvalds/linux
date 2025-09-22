/*	$OpenBSD: sunms.c,v 1.3 2022/01/09 05:43:00 jsg Exp $	*/

/*
 * Copyright (c) 2002, 2009, Miodrag Vallat
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Common Sun mouse handling code.
 *
 * This code supports 3- and 5- byte Mouse Systems protocols, and speeds of
 * 1200, 4800 and 9600 bps.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/timeout.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/sun/sunmsvar.h>

void
sunms_attach(struct sunms_softc *sc, const struct wsmouse_accessops *ao)
{
	struct wsmousedev_attach_args a;

	printf("\n");

	/* Initialize state machine. */
	sc->sc_state = STATE_PROBING;
	sc->sc_bps = INIT_SPEED;
	timeout_set(&sc->sc_abort_tmo, sunms_abort_input, sc);

	/*
	 * Note that it doesn't matter if a long time elapses between this
	 * and the moment interrupts are enabled, as we either have the
	 * right speed, and will switch to decode state, or get a break
	 * or a framing error, causing an immediate speed change.
	 */
	getmicrotime(&sc->sc_lastbpschange);

	a.accessops = ao;
	a.accesscookie = sc;
	sc->sc_wsmousedev = config_found(&sc->sc_dev, &a, wsmousedevprint);
}

int
sunms_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
#if 0
	struct sunms_softc *sc = v;
#endif

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_SUN;
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * Reinitialize the line to a different speed.  Invoked at spltty().
 */
void
sunms_speed_change(struct sunms_softc *sc)
{
	uint bps;

	switch (sc->sc_bps) {
	default:
	case 9600:
		bps = 4800;
		break;
	case 4800:
		bps = 1200;
		break;
	case 1200:
		bps = 9600;
		break;
	}

#ifdef DEBUG
	printf("%s: %d bps\n", sc->sc_dev.dv_xname, bps);
#endif
	microtime(&sc->sc_lastbpschange);

	(*sc->sc_speed_change)(sc, bps);
	sc->sc_state = STATE_PROBING;
	sc->sc_bps = bps;
	sc->sc_brk = 0;
	timeout_del(&sc->sc_abort_tmo);
}

/*
 * Process actual mouse data.  Invoked at spltty().
 */
void
sunms_input(struct sunms_softc *sc, int c)
{
	struct timeval curtime;

	if (sc->sc_wsmousedev == NULL)
		return;	/* why bother */

	if (sc->sc_state == STATE_RATE_CHANGE)
		return;	/* not ready yet */

	/*
	 * If we have changed speed recently, ignore data for a few
	 * milliseconds to make sure that either we'll detect the speed
	 * is still not correct, or discard potential noise resulting
	 * from the speed change.
	 */
	if (sc->sc_state == STATE_PROBING) {
		microtime(&curtime);
		timersub(&curtime, &sc->sc_lastbpschange, &curtime);
		if (curtime.tv_sec != 0 ||
		    curtime.tv_usec >= 200 * 1000) {
			sc->sc_state = STATE_DECODING;
			sc->sc_byteno = -1;
		} else
			return;
	}

	/*
	 * The Sun mice use either 3 byte or 5 byte packets. The
	 * first byte of each packet has the topmost bit set;
	 * however motion parts of the packet may have the topmost
	 * bit set too; so we only check for a first byte pattern
	 * when we are not currently processing a packet.
	 */
	if (sc->sc_byteno < 0) {
		if (ISSET(c, 0x80) && !ISSET(c, 0x30))
			sc->sc_byteno = 0;
		else
			return;
	}

	switch (sc->sc_byteno) {
	case 0:
		/*
		 * First packet has bit 7 set; bits 0-2 are button states,
		 * and bit 3 is set if it is a short (3 byte) packet.
		 * On the Tadpole SPARCbook, mice connected to the external
		 * connector will also have bit 6 set to allow it to be
		 * differentiated from the onboard pointer.
		 */
		sc->sc_pktlen = ISSET(c, 0x08) ? 3 : 5;
		sc->sc_mb = 0;
		if (!ISSET(c, 1 << 2))	/* left button */
			sc->sc_mb |= 1 << 0;
		if (!ISSET(c, 1 << 1))	/* middle button */
			sc->sc_mb |= 1 << 1;
		if (!ISSET(c, 1 << 0))	/* right button */
			sc->sc_mb |= 1 << 2;
		sc->sc_byteno++;

		/*
		 * In case we do not receive the whole packet, we need
		 * to be able to reset sc_byteno.
		 *
		 * At 1200bps 8N2, a five byte packet will span 50 bits
		 * and thus will transmit in 1/24 second, or about 42ms.
		 *
		 * A reset timeout of 100ms will be more than enough.
		 */
		timeout_add_msec(&sc->sc_abort_tmo, 100);

		break;
	case 1:
	case 3:
		/*
		 * Following bytes contain signed 7 bit X, then Y deltas.
		 * Short packets only have one set of deltas (and are
		 * thus usually used on 4800 baud mice).
		 */
		sc->sc_dx += (int8_t)c;
		sc->sc_byteno++;
		break;
	case 2:
	case 4:
		sc->sc_dy += (int8_t)c;
		sc->sc_byteno++;
		break;
	}

	if (sc->sc_byteno == sc->sc_pktlen) {
		timeout_del(&sc->sc_abort_tmo);
		sc->sc_byteno = -1;
		WSMOUSE_INPUT(sc->sc_wsmousedev,
		    sc->sc_mb, sc->sc_dx, sc->sc_dy, 0, 0);
		sc->sc_dx = sc->sc_dy = 0;
	}
}

void
sunms_abort_input(void *v)
{
	struct sunms_softc *sc = v;
	int s;

#ifdef DEBUG
	printf("aborting incomplete packet\n");
#endif
	s = spltty();
	sc->sc_byteno = -1;
	splx(s);
}
