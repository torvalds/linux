/*	$OpenBSD: aoa.c,v 1.16 2022/10/26 20:19:07 kn Exp $	*/

/*-
 * Copyright (c) 2005 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * WORK-IN-PROGRESS AOAKeylargo audio driver.
 */

#include <sys/param.h>
#include <sys/audioio.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/audio_if.h>
#include <dev/ofw/openfirm.h>
#include <macppc/dev/dbdma.h>

#include <machine/autoconf.h>

#include <macppc/dev/i2svar.h>

#ifdef AOA_DEBUG
# define DPRINTF printf
#else
# define DPRINTF while (0) printf
#endif

/* XXX */
#define aoa_softc i2s_softc

int aoa_match(struct device *, void *, void *);
void aoa_attach(struct device *, struct device *, void *);
void aoa_defer(struct device *);
void aoa_set_volume(struct aoa_softc *, int, int);

const struct cfattach aoa_ca = {
	sizeof(struct aoa_softc), aoa_match, aoa_attach
};

struct cfdriver aoa_cd = {
	NULL, "aoa", DV_DULL
};

const struct audio_hw_if aoa_hw_if = {
	.open = i2s_open,
	.close = i2s_close,
	.set_params = i2s_set_params,
	.round_blocksize = i2s_round_blocksize,
	.halt_output = i2s_halt_output,
	.halt_input = i2s_halt_input,
	.set_port = i2s_set_port,
	.get_port = i2s_get_port,
	.query_devinfo = i2s_query_devinfo,
	.allocm = i2s_allocm,
	.round_buffersize = i2s_round_buffersize,
	.trigger_output = i2s_trigger_output,
	.trigger_input = i2s_trigger_input,
};

int
aoa_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	int soundbus, soundchip;
	char compat[32];

	if (strcmp(ca->ca_name, "i2s") != 0)
		return (0);

	if ((soundbus = OF_child(ca->ca_node)) == 0 ||
	    (soundchip = OF_child(soundbus)) == 0)
		return (0);

	bzero(compat, sizeof compat);
	OF_getprop(soundchip, "compatible", compat, sizeof compat);

	if (strcmp(compat, "AOAKeylargo") == 0 &&
	    strcmp(hw_prod, "PowerBook5,4") != 0)
		return (1);
	if (strcmp(compat, "AOAK2") == 0)
		return (1);
	if (strcmp(compat, "AOAShasta") == 0)
		return (1);

	return (0);
}

void
aoa_attach(struct device *parent, struct device *self, void *aux)
{
	struct aoa_softc *sc = (struct aoa_softc *)self;

	sc->sc_setvolume = aoa_set_volume;

	i2s_attach(parent, sc, aux);
	config_defer(self, aoa_defer);
}

void
aoa_defer(struct device *dev)
{
	struct aoa_softc *sc = (struct aoa_softc *)dev;

	audio_attach_mi(&aoa_hw_if, sc, NULL, &sc->sc_dev);
	deq_reset(sc);
}

void
aoa_set_volume(struct aoa_softc *sc, int left, int right)
{
	/* This device doesn't provide volume control. */
}
