/*	$OpenBSD: stp4020var.h,v 1.5 2009/04/10 20:54:59 miod Exp $	*/
/*	$NetBSD: stp4020.c,v 1.23 2002/06/01 23:51:03 lukem Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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

/*
 * STP4020: SBus/PCMCIA bridge supporting one Type-3 PCMCIA card, or up to
 * two Type-1 and Type-2 PCMCIA cards..
 */

/*
 * Per socket data.
 */
struct stp4020_socket {
	struct stp4020_softc	*sc;	/* Back link */
	int		flags;
#define STP4020_SOCKET_BUSY	0x0001
#define STP4020_SOCKET_SHUTDOWN	0x0002
#define	STP4020_SOCKET_ENABLING	0x0004
	int		sense;
	int		sock;		/* Socket number (0 or 1) */
	int		int_enable;	/* ICR0 value for interrupt enabled */
	int		int_disable;	/* ICR0 value for interrupt disabled */
	bus_space_tag_t	tag;		/* socket control space */
	bus_space_handle_t	regs;	/* 			*/
	struct device	*pcmcia;	/* Associated PCMCIA device */
	int		(*intrhandler)	/* Card driver interrupt handler */
			    (void *);
	void		*intrarg;	/* Card interrupt handler argument */
	void		*softint;	/* softintr cookie */
	bus_space_tag_t	wintag;		/* windows access tag */
	struct {
		bus_space_handle_t	winaddr;/* this window's address */
	} windows[STP4020_NWIN];

};

struct stp4020_softc {
	struct device	sc_dev;		/* Base device */
	bus_space_tag_t	sc_bustag;
	pcmcia_chipset_tag_t	sc_pct;	/* Chipset methods */

	struct proc	*event_thread;	/* event handling thread */
	unsigned int	events;		/* sockets with pending events */

	struct stp4020_socket sc_socks[STP4020_NSOCK];
};

void	stpattach_common(struct stp4020_softc *, int);
int	stp4020_iointr(void *);
int	stp4020_statintr(void *);
