/*	$OpenBSD: smc91cxxvar.h,v 1.10 2025/06/25 20:28:09 miod Exp $	*/
/*	$NetBSD: smc91cxxvar.h,v 1.4 1997/10/15 05:56:13 explorer Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

struct smc91cxx_softc {
	struct	device sc_dev;		/* generic device glue */
	struct	arpcom sc_arpcom;	/* ethernet common glue */

	struct mii_data sc_mii;		/* MII/media control */
	struct timeout sc_mii_timeout;	/* MII callout handle */

	bus_space_tag_t sc_bst;		/* bus space */
	bus_space_handle_t sc_bsh;

	/* Power management hooks and state. */
	int	(*sc_enable)(struct smc91cxx_softc *);
	void	(*sc_disable)(struct smc91cxx_softc *);
	int	sc_enabled;
	u_int32_t	sc_flags;	/* misc. flags*/
#define SMC_FLAGS_ENABLED	0x0001
#define SMC_FLAGS_ATTACHED	0x0002	/* attach was successful */
#define SMC_FLAGS_HAS_MII	0x0004	/* Has MII (FEAST) */
#define SMC_FLAGS_32BIT_READ	0x0008	/* reads are always 32-bits */

	u_int8_t	sc_chipid;
	u_int8_t	sc_internal_phy;	/* 91C111 only */
};

#define	SMC_SELECT_BANK(sc, x)						\
	bus_space_write_2((sc)->sc_bst, (sc)->sc_bsh,			\
	    BANK_SELECT_REG_W, (x))

void	smc91cxx_attach(struct smc91cxx_softc *, u_int8_t *);
int	smc91cxx_intr(void *);
void	smc91cxx_init(struct smc91cxx_softc *);
void	smc91cxx_stop(struct smc91cxx_softc *);
int	smc91cxx_enable(struct smc91cxx_softc *);
void	smc91cxx_disable(struct smc91cxx_softc *);
