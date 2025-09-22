/*	$OpenBSD: sunmsvar.h,v 1.1 2009/05/20 18:22:33 miod Exp $	*/

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

struct sunms_softc {
	struct	device sc_dev;

	/*
	 * State of input translator
	 */
	uint	sc_state;		/* current FSM state */
#define	STATE_RATE_CHANGE	0 /* baud rate change pending */
#define	STATE_PROBING		1 /* checking packets after speed change */
#define	STATE_DECODING		2 /* normal operation */
	uint	sc_brk;			/* breaks in a row */

	int	sc_pktlen;		/* packet length */
	int	sc_byteno;		/* current packet position */
	int	sc_mb;			/* mouse button state */
	int	sc_dx;			/* delta-x */
	int	sc_dy;			/* delta-y */

	uint	sc_bps;			/* current link speed */
	struct timeval sc_lastbpschange;

	struct timeout sc_abort_tmo;

	struct device *sc_wsmousedev;

	void	(*sc_speed_change)(void *, uint);
};

#define	INIT_SPEED	9600

void	sunms_attach(struct sunms_softc *, const struct wsmouse_accessops *);
int	sunms_ioctl(void *, u_long, caddr_t, int, struct proc *);

void	sunms_abort_input(void *);
void	sunms_input(struct sunms_softc *, int c);
void	sunms_speed_change(struct sunms_softc *);
