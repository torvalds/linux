/*	$OpenBSD: sb_isapnp.c,v 1.17 2022/04/06 18:59:29 naddy Exp $	*/
/*	$NetBSD: sb_isa.c,v 1.3 1997/03/20 11:03:11 mycroft Exp $	*/

/*
 * Copyright (c) 1991-1993 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/isa/sbreg.h>
#include <dev/isa/sbvar.h>

#include <dev/isa/sbdspvar.h>

int	sb_isapnp_match(struct device *, void *, void *);
void	sb_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach sb_isapnp_ca = {
	sizeof(struct sbdsp_softc), sb_isapnp_match, sb_isapnp_attach
};

/*
 * Probe / attach routines.
 */

/*
 * Probe for the soundblaster hardware.
 */
int
sb_isapnp_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;

	if (ia->ipa_ndrq < 1)
		return 0;
	return 1;
}

/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver.
 */
void
sb_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbdsp_softc *sc = (struct sbdsp_softc *) self;
	struct isa_attach_args *ia = aux;

	sc->sc_iot = ia->ia_iot;
	sc->sc_ioh = ia->ipa_io[0].h;
	sc->sc_irq = ia->ipa_irq[0].num;
	sc->sc_iobase = ia->ipa_io[0].base;
	sc->sc_ic = ia->ia_ic;
	sc->sc_drq8 = ia->ipa_drq[0].num;
	
	if (ia->ipa_ndrq > 1 && ia->ipa_drq[0].num != ia->ipa_drq[1].num) {
		/* Some cards have the 16 bit drq first */
		if (sc->sc_drq8 >= 4) {
			sc->sc_drq16 = sc->sc_drq8;
			sc->sc_drq8 = ia->ipa_drq[1].num;
		} else
			sc->sc_drq16 = ia->ipa_drq[1].num;
	} else
		sc->sc_drq16 = DRQUNK;

#if NMIDI > 0
	if (ia->ipa_nio > 1) {
		sc->sc_mpu_sc.iobase = ia->ipa_io[1].base;
		sc->sc_mpu_sc.ioh = ia->ipa_io[1].h;
	} else
		sc->sc_mpu_sc.iobase = 0;
#endif

	if (!sbmatch(sc)) {
		printf(": sbmatch failed\n");
		return;
	}

	sc->sc_isa = parent->dv_parent;
	sbattach(sc);
}
