/*	$OpenBSD: isapnp_machdep.c,v 1.5 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: isapnp_machdep.c,v 1.5 1997/10/04 17:32:30 thorpej Exp $	*/

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
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

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Machine-dependent portions of ISA PnP bus autoconfiguration.
 *
 * N.B. This file exists mostly to get around some lameness surrounding
 * the PnP spec.  ISA PnP registers live where some `normal' ISA
 * devices do, but are e.g. write-only registers where the normal
 * device has a read-only register.  This breaks in the presence of
 * i/o port accounting.  This file takes care of mapping ISA PnP
 * registers without actually allocating them in extent maps.
 *
 * Since this is a machine-dependent file, we make all sorts of
 * assumptions about bus.h's guts.  Beware!
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/isa/isapnpreg.h>

#include <dev/isa/isavar.h>

/* isapnp_map():
 *	Map I/O regions used by PnP
 */
int
isapnp_map(struct isapnp_softc *sc)
{

#ifdef DIAGNOSTIC
	if (sc->sc_iot != I386_BUS_SPACE_IO)
		panic("isapnp_map: bogus bus space tag");
#endif

	sc->sc_addr_ioh = ISAPNP_ADDR;
	sc->sc_wrdata_ioh = ISAPNP_WRDATA;
	return (0);
}

/* isapnp_unmap():
 *	Unmap I/O regions used by PnP
 */
void
isapnp_unmap(struct isapnp_softc *sc)
{

	/* Do nothing. */
}

/* isapnp_map_readport():
 *	Called to map the PnP `read port', which is mapped independently
 *	of the `write' and `addr' ports.
 *
 *	NOTE: assumes the caller has filled in sc->sc_read_port!
 */
int
isapnp_map_readport(struct isapnp_softc *sc)
{
#ifdef _KERNEL
	int error;
#endif

#ifdef DIAGNOSTIC
	if (sc->sc_iot != I386_BUS_SPACE_IO)
		panic("isapnp_map_readport: bogus bus space tag");
#endif

#ifdef _KERNEL
	/* Check if some other device has already claimed this port. */
	if ((error = bus_space_map(sc->sc_iot, sc->sc_read_port, 1, 0,
	    &sc->sc_read_ioh)) != 0)
		return error;

	/*
	 * XXX: We unmap the port because it can and will be used by other
	 *	devices such as a joystick. We need a better port accounting
	 *	scheme with read and write ports.
	 */
	bus_space_unmap(sc->sc_iot, sc->sc_read_ioh, 1);
#endif
	return 0;
}

/* isapnp_unmap_readport():
 *	Pretend to unmap a previously mapped `read port'.
 */
void
isapnp_unmap_readport(struct isapnp_softc *sc)
{

	/* Do nothing */
}
