/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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

#ifndef __OCTEON_GPIOVAR_H__
#define __OCTEON_GPIOVAR_H__

#define GPIO_LOCK(_sc)		mtx_lock(&(_sc)->gpio_mtx)
#define GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->gpio_mtx)
#define GPIO_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->gpio_mtx, MA_OWNED)

#define	OCTEON_GPIO_IRQ_LEVEL		0
#define	OCTEON_GPIO_IRQ_EDGE		1	

#define	OCTEON_GPIO_PINS	24
#define	OCTEON_GPIO_IRQS	16

struct octeon_gpio_softc {
	device_t		dev;
	device_t		busdev;
	struct mtx		gpio_mtx;
	struct resource		*gpio_irq_res[OCTEON_GPIO_IRQS];
	int			gpio_irq_rid[OCTEON_GPIO_IRQS];
	void			*gpio_ih[OCTEON_GPIO_IRQS];
	void			*gpio_intr_cookies[OCTEON_GPIO_IRQS];
	int			gpio_npins;
	struct gpio_pin		gpio_pins[OCTEON_GPIO_PINS];
};

#endif	/* __OCTEON_GPIOVAR_H__ */
