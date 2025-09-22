/* $OpenBSD: spc.c,v 1.13 2022/04/16 19:19:58 naddy Exp $ */
/* $NetBSD: spc.c,v 1.4 2003/07/05 19:00:17 tsutsui Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/autoconf.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

#include <luna88k/dev/mb89352reg.h>
#include <luna88k/dev/mb89352var.h>

#include <luna88k/luna88k/isr.h>

int  spc_mainbus_match(struct device *, void *, void *);
void spc_mainbus_attach(struct device *, struct device *, void *);

const struct cfattach spc_ca = {
	sizeof(struct spc_softc), spc_mainbus_match, spc_mainbus_attach
};

struct cfdriver spc_cd = {
	NULL, "spc", DV_DULL
};

const struct scsi_adapter spc_switch = {
	spc_scsi_cmd, NULL, NULL, NULL, NULL
};

/* bus space tag for spc */
struct luna88k_bus_space_tag spc_bst = {
	2,	/* when reading/writing 1 byte, the stride is 4. */
	0,	/* not used */
	0,	/* not used */
	0,	/* not used */
	0,	/* no offset */
};

int
spc_mainbus_match(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, spc_cd.cd_name))
		return 0;
#if 0
	if (badaddr((caddr_t)ma->ma_addr, 4)) 
		return 0;
	/* Experiments proved 2nd SPC address does NOT make a buserror. */
#endif
	return 1;
}

void
spc_mainbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct spc_softc *sc = (void *)self;
	struct mainbus_attach_args *ma = aux;

	printf ("\n");

	sc->sc_iot = &spc_bst;
	sc->sc_ioh = ma->ma_addr;
	sc->sc_initiator = 7;
	sc->sc_dma_start = NULL;
	sc->sc_dma_done = NULL;

	isrlink_autovec(spc_intr, (void *)sc, ma->ma_ilvl, ISRPRI_BIO,
	    self->dv_xname);

	spc_attach(sc, &spc_switch);
}
