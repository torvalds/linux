/*	$OpenBSD: wdc_isapnp.c,v 1.13 2022/04/06 18:59:29 naddy Exp $	*/
/*	$NetBSD: wdc_isapnp.c,v 1.13 1999/03/22 10:00:12 mycroft Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Onno van der Linden.
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
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ata/atavar.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>

struct wdc_isapnp_softc {
	struct	wdc_softc sc_wdcdev;
	struct	channel_softc *wdc_chanptr;
	struct	channel_softc wdc_channel;
	isa_chipset_tag_t sc_ic;
	void	*sc_ih;
	int	sc_drq;
};

int	wdc_isapnp_match(struct device *, void *, void *);
void	wdc_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach wdc_isapnp_ca = {
	sizeof(struct wdc_isapnp_softc), wdc_isapnp_match, wdc_isapnp_attach
};

int
wdc_isapnp_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ipa = aux;

	if (ipa->ipa_nio != 2 ||
	    ipa->ipa_nmem != 0 ||
	    ipa->ipa_nmem32 != 0 ||
	    ipa->ipa_nirq != 1 ||
	    ipa->ipa_ndrq > 1)
		return 0;

	return (1);
}

void
wdc_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct wdc_isapnp_softc *sc = (void *)self;
	struct isa_attach_args *ipa = aux;


	sc->wdc_channel.cmd_iot = ipa->ia_iot;
	sc->wdc_channel.ctl_iot = ipa->ia_iot;

	/*
	 * An IDE controller can feed us the regions in any order. Pass
	 * them along with the 8-byte region in sc_ad.ioh, and the other
	 * (2 byte) region in auxioh.
	 */
	if (ipa->ipa_io[0].length == 8) {
		sc->wdc_channel.cmd_ioh = ipa->ipa_io[0].h;
		sc->wdc_channel.ctl_ioh = ipa->ipa_io[1].h;
	} else {
		sc->wdc_channel.cmd_ioh = ipa->ipa_io[1].h;
		sc->wdc_channel.ctl_ioh = ipa->ipa_io[0].h;
	}
	sc->wdc_channel.data32iot = sc->wdc_channel.cmd_iot;
	sc->wdc_channel.data32ioh = sc->wdc_channel.cmd_ioh;

	sc->sc_ic = ipa->ia_ic;
	sc->sc_ih = isa_intr_establish(ipa->ia_ic, ipa->ipa_irq[0].num,
	    ipa->ipa_irq[0].type, IPL_BIO, wdcintr, &sc->wdc_channel,
	    sc->sc_wdcdev.sc_dev.dv_xname);

	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32;
	sc->sc_wdcdev.PIO_cap = 0;
	sc->wdc_chanptr = &sc->wdc_channel;
	sc->sc_wdcdev.channels = &sc->wdc_chanptr;
	sc->sc_wdcdev.nchannels = 1;
	sc->wdc_channel.channel = 0;
	sc->wdc_channel.wdc = &sc->sc_wdcdev;
	sc->wdc_channel.ch_queue = wdc_alloc_queue();
	if (sc->wdc_channel.ch_queue == NULL) {
		printf(": cannot allocate channel queue\n");
		return;
	}

	printf("\n");
	wdcattach(&sc->wdc_channel);
	wdc_print_current_modes(&sc->wdc_channel);
}
