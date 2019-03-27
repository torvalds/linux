/*-
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef TI_GPIO_H
#define	TI_GPIO_H

/* The maximum number of banks for any SoC */
#define	MAX_GPIO_BANKS			6

/*
 * Maximum GPIOS possible, max of *_MAX_GPIO_BANKS * *_INTR_PER_BANK.
 * These are defined in ti_gpio.c
 */
#define	MAX_GPIO_INTRS			8

struct ti_gpio_irqsrc {
	struct intr_irqsrc	tgi_isrc;
	u_int			tgi_irq;
	uint32_t		tgi_mask;
	uint32_t		tgi_mode;
};

/**
 *	Structure that stores the driver context.
 *
 *	This structure is allocated during driver attach.
 */
struct ti_gpio_softc {
	device_t		sc_dev;
	device_t		sc_busdev;
	int			sc_bank;
	int			sc_maxpin;
	struct mtx		sc_mtx;

	int			sc_mem_rid;
	struct resource		*sc_mem_res;
	int			sc_irq_rid;
	struct resource		*sc_irq_res;
	struct ti_gpio_irqsrc	*sc_isrcs;
	/* The handle for the register IRQ handlers. */
	void			*sc_irq_hdl;
};

#endif /* TI_GPIO_H */
