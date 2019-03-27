/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_UART_DEV_NS8250_H_
#define _DEV_UART_DEV_NS8250_H_

/*
 * High-level UART interface.
 */
struct ns8250_softc {
	struct uart_softc base;
	uint8_t		fcr;
	uint8_t		ier;
	uint8_t		mcr;
	
	uint8_t		ier_mask;
	uint8_t		ier_rxbits;
	uint8_t		busy_detect;
};

extern struct uart_ops uart_ns8250_ops;

int ns8250_bus_attach(struct uart_softc *);
int ns8250_bus_detach(struct uart_softc *);
int ns8250_bus_flush(struct uart_softc *, int);
int ns8250_bus_getsig(struct uart_softc *);
int ns8250_bus_ioctl(struct uart_softc *, int, intptr_t);
int ns8250_bus_ipend(struct uart_softc *);
int ns8250_bus_param(struct uart_softc *, int, int, int, int);
int ns8250_bus_probe(struct uart_softc *);
int ns8250_bus_receive(struct uart_softc *);
int ns8250_bus_setsig(struct uart_softc *, int);
int ns8250_bus_transmit(struct uart_softc *);
void ns8250_bus_grab(struct uart_softc *);
void ns8250_bus_ungrab(struct uart_softc *);

#endif /* _DEV_UART_DEV_NS8250_H_ */
