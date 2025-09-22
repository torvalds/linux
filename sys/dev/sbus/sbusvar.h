/*	$OpenBSD: sbusvar.h,v 1.14 2016/09/04 18:20:34 tedu Exp $	*/
/*	$NetBSD: sbusvar.h,v 1.11 2000/11/01 06:18:45 eeh Exp $ */

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

#ifndef _SBUS_VAR_H
#define _SBUS_VAR_H

struct sbus_softc;

/*
 * S-bus variables.
 */

/* Device register space description */
struct sbus_reg {
	u_int32_t	sbr_slot;
	u_int32_t	sbr_offset;
	u_int32_t	sbr_size;
};

/* Interrupt information */
struct sbus_intr {
	u_int32_t	sbi_pri;	/* priority (IPL) */
	u_int32_t	sbi_vec;	/* vector (always 0?) */
};

/* Address translation across busses */
struct sbus_range {
	u_int32_t	cspace;		/* Client space */
	u_int32_t	coffset;	/* Client offset */
	u_int32_t	pspace;		/* Parent space */
	u_int32_t	poffset;	/* Parent offset */
	u_int32_t	size;		/* Size in bytes of this range */
};

/*
 * SBus driver attach arguments.
 */
struct sbus_attach_args {
	int		sa_placeholder;	/* for obio attach args sharing */
	bus_space_tag_t	sa_bustag;
	bus_dma_tag_t	sa_dmatag;
	char		*sa_name;	/* PROM node name */
	int		sa_node;	/* PROM handle */
	struct sbus_reg	*sa_reg;	/* SBus register space for device */
	int		sa_nreg;	/* Number of SBus register spaces */
#define sa_slot		sa_reg[0].sbr_slot
#define sa_offset	sa_reg[0].sbr_offset
#define sa_size		sa_reg[0].sbr_size

	struct sbus_intr *sa_intr;	/* SBus interrupts for device */
	int		sa_nintr;	/* Number of interrupts */
#define sa_pri		sa_intr[0].sbi_pri

	u_int32_t	*sa_promvaddrs;/* PROM-supplied virtual addresses -- 32-bit */
	int		sa_npromvaddrs;	/* Number of PROM VAs */
#define sa_promvaddr	sa_promvaddrs[0]
	int		sa_frequency;	/* SBus clockrate */
};

int	sbus_print(void *, const char *);

int	sbus_setup_attach_args(
		struct sbus_softc *,
		bus_space_tag_t,
		bus_dma_tag_t,
		int,			/*node*/
		struct sbus_attach_args *);

void	sbus_destroy_attach_args(struct sbus_attach_args *);

#define sbus_bus_map(t, slot, offset, sz, flags, unused, hp) \
	bus_space_map(t, BUS_ADDR(slot, offset), sz, flags, hp)

#if notyet
/* variables per SBus */
struct sbus_softc {
	struct	device sc_dev;		/* base device */
	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;
	int	sc_clockfreq;		/* clock frequency (in Hz) */
	struct	sbus_range *sc_range;
	int	sc_nrange;
	int	sc_burst;		/* burst transfer sizes supported */
	/* machdep stuff follows here */
	int	*sc_intr2ipl;		/* Interrupt level translation */
	int	*sc_intr_compat;	/* `intr' property to sbus compat */
};
#endif


/*
 * PROM-reported DMA burst sizes for the SBus
 */
#define SBUS_BURST_1	0x1
#define SBUS_BURST_2	0x2
#define SBUS_BURST_4	0x4
#define SBUS_BURST_8	0x8
#define SBUS_BURST_16	0x10
#define SBUS_BURST_32	0x20
#define SBUS_BURST_64	0x40

#include <sparc64/dev/sbusvar.h>
#include <sparc64/dev/iommureg.h>

#endif /* _SBUS_VAR_H */
