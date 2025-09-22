/*	$OpenBSD: sb_isa.c,v 1.12 2022/04/06 18:59:28 naddy Exp $	*/
/*	$NetBSD: sb_isa.c,v 1.15 1997/11/30 15:32:25 drochner Exp $	*/

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

#include <machine/bus.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/isa/sbreg.h>
#include <dev/isa/sbvar.h>

#include <dev/isa/sbdspvar.h>

static	int sbfind(struct device *, struct sbdsp_softc *, struct isa_attach_args *);

int	sb_isa_match(struct device *, void *, void *);
void	sb_isa_attach(struct device *, struct device *, void *);

const struct cfattach sb_isa_ca = {
	sizeof(struct sbdsp_softc), sb_isa_match, sb_isa_attach
};

/*
 * Probe / attach routines.
 */

/*
 * Probe for the soundblaster hardware.
 */
int
sb_isa_match(struct device *parent, void *match, void *aux)
{
	struct sbdsp_softc probesc, *sc = &probesc;

	bzero(sc, sizeof *sc);
	sc->sc_dev.dv_cfdata = ((struct device *)match)->dv_cfdata;
	strlcpy(sc->sc_dev.dv_xname, "sb", sizeof sc->sc_dev.dv_xname);
	return sbfind(parent, sc, aux);
}

static int
sbfind(struct device *parent, struct sbdsp_softc *sc,
    struct isa_attach_args *ia)
{
	int rc = 0;

	if (!SB_BASE_VALID(ia->ia_iobase))
		return 0;

	sc->sc_iot = ia->ia_iot;

	/* Map i/o space [we map 24 ports which is the max of the sb and pro] */
	if (bus_space_map(sc->sc_iot, ia->ia_iobase, SBP_NPORT, 0,
	    &sc->sc_ioh))
		return 0;

	sc->sc_iobase = ia->ia_iobase;
	sc->sc_irq = ia->ia_irq;
	sc->sc_drq8 = ia->ia_drq;
	sc->sc_drq16 = ia->ia_drq2;
	sc->sc_ic = ia->ia_ic;

	if (!sbmatch(sc))
		goto bad;

	if ((sc->sc_drq8 != -1 && !isa_drq_isfree(parent, sc->sc_drq8)) ||
	    (sc->sc_drq16 != -1 && !isa_drq_isfree(parent, sc->sc_drq16)))
		goto bad;

	if (ISSBPROCLASS(sc))
		ia->ia_iosize = SBP_NPORT;
	else
		ia->ia_iosize = SB_NPORT;

	if (!ISSB16CLASS(sc) && sc->sc_model != SB_JAZZ)
		ia->ia_drq2 = -1;

	ia->ia_irq = sc->sc_irq;

	rc = 1;

bad:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, SBP_NPORT);
	return rc;
}


/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver .
 */
void
sb_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbdsp_softc *sc = (struct sbdsp_softc *)self;
	struct isa_attach_args *ia = aux;

	if (!sbfind(parent, sc, ia) || 
	    bus_space_map(sc->sc_iot, ia->ia_iobase, ia->ia_iosize, 0,
	    &sc->sc_ioh)) {
		printf("%s: sbfind failed\n", sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_ic = ia->ia_ic;
	sc->sc_isa = parent;
	sbattach(sc);
}
