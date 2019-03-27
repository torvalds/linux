/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_TERASIC_DE4LED_H_
#define	_DEV_TERASIC_DE4LED_H_

#define	TERASIC_DE4LED_NUMLEDS	8
struct terasic_de4led_softc {
	device_t	 tdl_dev;
	int		 tdl_unit;
	struct resource	*tdl_res;
	int		 tdl_rid;
	struct mtx	 tdl_lock;
	uint8_t		 tdl_bits;
	struct cdev	*tdl_leds[TERASIC_DE4LED_NUMLEDS];
};

#define	TERASIC_DE4LED_LOCK(sc)		mtx_lock(&(sc)->tdl_lock)
#define	TERASIC_DE4LED_LOCK_ASSERT(sc)	mtx_assert(&(sc)->tdl_lock, MA_OWNED)
#define	TERASIC_DE4LED_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->tdl_lock)
#define	TERASIC_DE4LED_LOCK_INIT(sc)	mtx_init(&(sc)->tdl_lock,	\
					    "terasic_de4led", NULL, MTX_DEF)
#define	TERASIC_DE4LED_UNLOCK(sc)	mtx_unlock(&(sc)->tdl_lock)

/*
 * Setting and clearing LEDs.  tdl_bits is in the bit order preferred for I/O.
 * The LED elements are labelled 1..8 on the DE-4, so bit 0 is LED 1, and so
 * on.
 */
#define	TERASIC_DE4LED_CLEARBAR(sc) do {				\
	(sc)->tdl_bits = 0;						\
} while (0)
#define	TERASIC_DE4LED_SETLED(sc, led, onoff) do {			\
	(sc)->tdl_bits &= ~(1 << (led));				\
	(sc)->tdl_bits |= ((onoff != 0) ? 1 : 0) << (led);		\
} while (0)

/*
 * Only one offset matters for this device -- 0.
 */
#define	TERASIC_DE4LED_OFF_LED		0

void	terasic_de4led_attach(struct terasic_de4led_softc *sc);
void	terasic_de4led_detach(struct terasic_de4led_softc *sc);

extern devclass_t	terasic_de4led_devclass;

#endif /* _DEV_TERASIC_DE4LED_H_ */
