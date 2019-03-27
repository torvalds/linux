/*-
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2009, Luiz Otavio O Souza.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 */

#ifndef __AR5315_GPIOVAR_H__
#define __AR5315_GPIOVAR_H__

#include <sys/timepps.h>

#define GPIO_LOCK(_sc)		mtx_lock(&(_sc)->gpio_mtx)
#define GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->gpio_mtx)
#define GPIO_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->gpio_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define GPIO_WRITE(sc, reg, val)	do {	\
		bus_write_4(sc->gpio_mem_res, (reg), (val)); \
	} while (0)

#define GPIO_READ(sc, reg)	 bus_read_4(sc->gpio_mem_res, (reg))

#define GPIO_SET_BITS(sc, reg, bits)	\
	GPIO_WRITE(sc, reg, GPIO_READ(sc, (reg)) | (bits))

#define GPIO_CLEAR_BITS(sc, reg, bits)	\
	GPIO_WRITE(sc, reg, GPIO_READ(sc, (reg)) & ~(bits))

#define	AR5315_GPIO_PINS	23
#define	AR5312_GPIO_PINS	8

struct ar5315_gpio_softc {
	device_t		dev;
	device_t		busdev;
	struct mtx		gpio_mtx;
	struct resource		*gpio_mem_res;
	int			gpio_mem_rid;
	struct resource		*gpio_irq_res;
	int			gpio_irq_rid;
	void			*gpio_ih;
	int			gpio_npins;
	struct gpio_pin		*gpio_pins;
	int			gpio_ppspin;
	struct pps_state	gpio_pps;
	uint32_t		gpio_ppsenable;
};

#endif	/* __AR5315_GPIOVAR_H__ */
