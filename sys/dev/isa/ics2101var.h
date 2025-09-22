/* $OpenBSD: ics2101var.h,v 1.5 2008/06/26 05:42:16 ray Exp $ */
/* $NetBSD: ics2101var.h,v 1.5 1997/10/09 07:57:24 jtc Exp $ */

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

#define ICSMIX_LEFT		0		/* Value for left channel */
#define ICSMIX_RIGHT		1		/* Value for right channel */

struct ics2101_softc {
	bus_space_tag_t sc_iot;		/* tag */
	bus_space_handle_t sc_selio_ioh;
	bus_space_handle_t sc_dataio_ioh;
	u_short sc_selio;		/* select I/O address */
	u_short sc_dataio;		/* data I/O address */
	int sc_flags;			/* Various flags */
#define ICS_FLIP	0x01		/* flip channels */
	u_char sc_setting[ICSMIX_CHAN_5+1][2]; /* current settings */
	u_char sc_mute[ICSMIX_CHAN_5+1][2];/* muted? */
};

void ics2101_mix_attenuate(struct ics2101_softc *, u_int, u_int, u_int);
void ics2101_mix_mute(struct ics2101_softc *, u_int, u_int, u_int);
