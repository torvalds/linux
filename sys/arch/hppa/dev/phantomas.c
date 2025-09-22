/*	$OpenBSD: phantomas.c,v 1.6 2022/03/13 08:04:38 mpi Exp $	*/

/*
 * Copyright (c) 2002 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/pdc.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>

struct cfdriver phantomas_cd = {
	NULL, "phantomas", DV_DULL
};

struct phantomas_softc {
	struct device sc_dev;

};

int	phantomasmatch(struct device *, void *, void *);
void	phantomasattach(struct device *, struct device *, void *);

const struct cfattach phantomas_ca = {
	sizeof(struct phantomas_softc), phantomasmatch, phantomasattach
};

int
phantomasmatch(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;

	if (ca->ca_type.iodc_type != HPPA_TYPE_BCPORT ||
	    ca->ca_type.iodc_sv_model != HPPA_BCPORT_PHANTOM)
		return (0);

	return (1);
}

void
phantomasattach(struct device *parent, struct device *self, void *aux)
{
	struct confargs *ca = aux, nca;

	printf("\n");

	nca = *ca;
	nca.ca_hpamask = HPPA_IOBEGIN;
	pdc_scanbus(self, &nca, MAXMODBUS, 0, 0);
}
