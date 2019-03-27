/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timetc.h>

#include <machine/bus.h>
#include <machine/bus_common.h>

#define	COUNTER_MASK	((1 << 29) - 1)
#define	COUNTER_FREQ	1000000
#define	COUNTER_QUALITY	100

/* Bits in the limit register. */
#define	CTLR_INTEN	(1U << 31)	/* Enable timer interrupts */
#define	CTLR_RELOAD	(1U << 30)	/* Zero counter on write to limit reg */
#define	CTLR_PERIODIC	(1U << 29)	/* Wrap to 0 if limit is reached */

/* Offsets of the registers for the two counters. */
#define	CTR_CT0		0x00
#define	CTR_CT1		0x10

/* Register offsets from the base address. */
#define	CTR_COUNT	0x00
#define	CTR_LIMIT	0x08


static timecounter_get_t counter_get_timecount;

struct ct_softc {
	bus_space_tag_t		sc_tag;
	bus_space_handle_t	sc_handle;
	bus_addr_t		sc_offset;
};


/*
 * This is called from the psycho and sbus drivers.  It does not directly
 * attach to the nexus because it shares register space with the bridge in
 * question.
 */
void
sparc64_counter_init(const char *name, bus_space_tag_t tag,
    bus_space_handle_t handle, bus_addr_t offset)
{
	struct timecounter *tc;
	struct ct_softc *sc;

	printf("initializing counter-timer\n");
	/*
	 * Turn off interrupts from both counters.  Set the limit to the
	 * maximum value (although that should not change anything with
	 * CTLR_INTEN and CTLR_PERIODIC off).
	 */
	bus_space_write_8(tag, handle, offset + CTR_CT0 + CTR_LIMIT,
	    COUNTER_MASK);
	bus_space_write_8(tag, handle, offset + CTR_CT1 + CTR_LIMIT,
	    COUNTER_MASK);
	/* Register as a time counter. */
	tc = malloc(sizeof(*tc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK);
	sc->sc_tag = tag;
	sc->sc_handle = handle;
	sc->sc_offset = offset + CTR_CT0;
	tc->tc_get_timecount = counter_get_timecount;
	tc->tc_counter_mask = COUNTER_MASK;
	tc->tc_frequency = COUNTER_FREQ;
	tc->tc_name = strdup(name, M_DEVBUF);
	tc->tc_priv = sc;
	tc->tc_quality = COUNTER_QUALITY;
	tc_init(tc);
}

static unsigned int
counter_get_timecount(struct timecounter *tc)
{
	struct ct_softc *sc;

	sc = tc->tc_priv;
	return (bus_space_read_8(sc->sc_tag, sc->sc_handle, sc->sc_offset));
}
