/*	$OpenBSD: necsb.c,v 1.6 2024/06/01 00:48:16 aoyama Exp $	*/
/*	$NecBSD: nec86_isa.c,v 1.9 1998/09/26 11:31:11 kmatsuda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1995, 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/board.h>		/* PC_BASE */
#include <machine/bus.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <luna88k/cbus/nec86reg.h>
#include <luna88k/cbus/nec86hwvar.h>
#include <luna88k/cbus/nec86var.h>

#include <luna88k/cbus/cbusvar.h>	/* cbus_isrlink() */

#define	NECSB_BASE	(PCEXIO_BASE + 0xa460)

int	necsb_match(struct device *, void *, void *);
void	necsb_attach(struct device *, struct device *, void *);

const struct cfattach necsb_ca = {
	sizeof(struct nec86_softc), necsb_match, necsb_attach
};

struct cfdriver necsb_cd = {
	NULL, "necsb", DV_DULL
};

/* bus space tag for necsb */
struct luna88k_bus_space_tag necsb_bst = {
	0,	/* when reading/writing 1 byte, no shift is needed. */
	0,
	0,
	0,
	0,	/* no offset */
};

int
necsb_match(struct device *parent, void *cf, void *aux)
{
	struct cbus_attach_args *caa = aux;

	if (strcmp(caa->ca_name, necsb_cd.cd_name))
		return 0;

	return 1;
}

void
necsb_attach(struct device *parent, struct device *self, void *aux)
{
	struct nec86_softc *nsc = (struct nec86_softc *)self;
	struct nec86hw_softc *ysc = &nsc->sc_nec86hw;
#if 0
	struct cbus_attach_args *caa = aux;
#endif

	bus_space_tag_t iot = &necsb_bst;

	nsc->sc_n86iot = iot;
	nsc->sc_n86ioh =
	    (bus_space_handle_t)(NECSB_BASE + NEC86_SOUND_ID);

	ysc->sc_iot = iot;
	ysc->sc_ioh =
	    (bus_space_handle_t)(NECSB_BASE + NEC86_COREOFFSET);
	ysc->sc_cfgflags = 0;	/* original 'PC-9801-86' */

	nsc->sc_ym_iot = iot;
	nsc->sc_ym_iobase = (bus_space_handle_t)PCEXIO_BASE;
	nsc->sc_ym_ioh = (bus_space_handle_t)0;	/* default */

	nec86_attachsubr(nsc);

	if (nsc->sc_intlevel == -1)
		return;

	if ((nsc->sc_intlevel < 0) || (nsc->sc_intlevel >= NCBUSISR))
		panic("necsb_attach: bad INT level");

	/* XXX: check return value ? */
	cbus_isrlink(nec86hw_intr, ysc, nsc->sc_intlevel, IPL_AUDIO,
	    self->dv_xname);
}
