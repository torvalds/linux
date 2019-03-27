/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_POWERPC_OPENPICVAR_H_
#define	_POWERPC_OPENPICVAR_H_

#define OPENPIC_DEVSTR	"OpenPIC Interrupt Controller"

#define OPENPIC_IRQMAX	256	/* h/w allows more */

#define	OPENPIC_QUIRK_SINGLE_BIND	1	/* Bind interrupts to only 1 CPU */

/* Names match the macros in openpicreg.h. */
struct openpic_timer {
    	uint32_t	tcnt;
    	uint32_t	tbase;
    	uint32_t	tvec;
    	uint32_t	tdst;
};

struct openpic_softc {
	device_t	sc_dev;
	struct resource	*sc_memr;
	struct resource	*sc_intr;
	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_bh;
	char		*sc_version;
	int		sc_rid;
	int		sc_irq;
	void		*sc_icookie;
	u_int		sc_ncpu;
	u_int		sc_nirq;
	int		sc_psim;
	u_int		sc_quirks;

	/* Saved states. */
	uint32_t		sc_saved_config;
	uint32_t		sc_saved_ipis[4];
	uint32_t		sc_saved_prios[4];
	struct openpic_timer	sc_saved_timers[OPENPIC_TIMERS];
	uint32_t		sc_saved_vectors[OPENPIC_SRC_VECTOR_COUNT];
	
};

extern devclass_t openpic_devclass;

/*
 * Bus-independent attach i/f
 */
int	openpic_common_attach(device_t, uint32_t);

/*
 * PIC interface.
 */
void	openpic_bind(device_t dev, u_int irq, cpuset_t cpumask, void **);
void	openpic_config(device_t, u_int, enum intr_trigger, enum intr_polarity);
void	openpic_dispatch(device_t, struct trapframe *);
void	openpic_enable(device_t, u_int, u_int, void **);
void	openpic_eoi(device_t, u_int, void *);
void	openpic_ipi(device_t, u_int);
void	openpic_mask(device_t, u_int, void *);
void	openpic_unmask(device_t, u_int, void *);

int	openpic_suspend(device_t dev);
int	openpic_resume(device_t dev);

#endif /* _POWERPC_OPENPICVAR_H_ */
