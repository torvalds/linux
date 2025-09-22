/*	$OpenBSD: gus_isa.c,v 1.10 2024/05/28 09:27:08 jsg Exp $	*/
/*	$NetBSD: gus.c,v 1.51 1998/01/25 23:48:06 mycroft Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein and John Kohl.
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
 *
 * TODO:
 *	. figure out why mixer activity while sound is playing causes problems
 *	  (phantom interrupts?)
 *	. figure out a better deinterleave strategy that avoids sucking up
 *	  CPU, memory and cache bandwidth.  (Maybe a special encoding?
 *	  Maybe use the double-speed sampling/hardware deinterleave trick
 *	  from the GUS SDK?)  A 486/33 isn't quite fast enough to keep
 *	  up with 44.1kHz 16-bit stereo output without some drop-outs.
 *	. use CS4231 for 16-bit sampling, for a-law and mu-law playback.
 *	. actually test full-duplex sampling(recording) and playback.
 */

/*
 * Gravis UltraSound driver
 *
 * For more detailed information, see the GUS developers' kit
 * available on the net at:
 *
 * ftp://freedom.nmsu.edu/pub/ultrasound/gravis/util/
 *	gusdkXXX.zip (developers' kit--get rev 2.22 or later)
 *		See ultrawrd.doc inside--it's MS Word (ick), but it's the bible
 *
 */

/*
 * The GUS Max has a slightly strange set of connections between the CS4231
 * and the GF1 and the DMA interconnects.  It's set up so that the CS4231 can
 * be playing while the GF1 is loading patches from the system.
 *
 * Here's a recreation of the DMA interconnect diagram:
 *
 *       GF1
 *   +---------+				 digital
 *   |         |  record			 ASIC
 *   |         |--------------+
 *   |         |              |		       +--------+
 *   |         | play (dram)  |      +----+    |	|
 *   |         |--------------(------|-\  |    |   +-+  |
 *   +---------+              |      |  >-|----|---|C|--|------  dma chan 1
 *                            |  +---|-/  |    |   +-+	|
 *                            |  |   +----+    |    |   |
 *                            |	 |   +----+    |    |   |
 *   +---------+        +-+   +--(---|-\  |    |    |   |
 *   |         | play   |8|      |   |  >-|----|----+---|------  dma chan 2
 *   | ---C----|--------|/|------(---|-/  |    |        |
 *   |    ^    |record  |1|      |   +----+    |	|
 *   |    |    |   /----|6|------+	       +--------+
 *   | ---+----|--/     +-+
 *   +---------+
 *     CS4231	8-to-16 bit bus conversion, if needed
 *
 *
 * "C" is an optional combiner.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/cpufunc.h>
#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/ics2101reg.h>
#include <dev/ic/cs4231reg.h>
#include <dev/ic/ad1848reg.h>
#include <dev/isa/ics2101var.h>
#include <dev/isa/ad1848var.h>
#include "gusreg.h"
#include "gusvar.h"

/*
 * ISA bus driver routines
 */

int	gus_isa_match(struct device *, void *, void *);
void	gus_isa_attach(struct device *, struct device *, void *);

const struct cfattach gus_isa_ca = {
	sizeof(struct gus_softc), gus_isa_match, gus_isa_attach,
};

int
gus_isa_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	int iobase = ia->ia_iobase;
	int recdrq = ia->ia_drq2;

	/*
	 * Before we do anything else, make sure requested IRQ and DRQ are
	 * valid for this card.
	 */

	/* XXX range check before indexing!! */
	if (ia->ia_irq == IRQUNK || gus_irq_map[ia->ia_irq] == IRQUNK) {
		DPRINTF(("gus: invalid irq %d, card not probed\n", ia->ia_irq));
		return 0;
	}

	if (ia->ia_drq == DRQUNK || gus_drq_map[ia->ia_drq] == DRQUNK) {
		DPRINTF(("gus: invalid drq %d, card not probed\n", ia->ia_drq));
		return 0;
	}

	if (recdrq != DRQUNK) {
		if (recdrq > 7 || gus_drq_map[recdrq] == DRQUNK) {
		   DPRINTF(("gus: invalid second DMA channel (%d), card not probed\n", recdrq));
		   return 0;
	        }
	} else
		recdrq = ia->ia_drq;

	if (iobase == IOBASEUNK) {
		int i;
		for(i = 0; i < gus_addrs; i++)
			if (gus_test_iobase(ia->ia_iot, gus_base_addrs[i])) {
				iobase = gus_base_addrs[i];
				goto done;
			}
		return 0;
	} else if (!gus_test_iobase(ia->ia_iot, iobase))
			return 0;

done:
	if ((ia->ia_drq    != -1 && !isa_drq_isfree(parent, ia->ia_drq)) ||
	    (recdrq != -1 && !isa_drq_isfree(parent, recdrq)))
		return 0;

	ia->ia_iobase = iobase;
	ia->ia_iosize = GUS_NPORT1;
	return 1;
}


/*
 * Setup the GUS for use; called shortly after probe
 */

void
gus_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct gus_softc *sc = (void *) self;
	struct isa_attach_args *ia = aux;

	sc->sc_iot = ia->ia_iot;
	sc->sc_iobase = ia->ia_iobase;

	/* Map i/o space */
	if (bus_space_map(sc->sc_iot, sc->sc_iobase, GUS_NPORT1, 0, &sc->sc_ioh1))
		panic("%s: can't map io port range 1", self->dv_xname);
	if (bus_space_map(sc->sc_iot, sc->sc_iobase+GUS_IOH2_OFFSET, GUS_NPORT2, 0,
	    &sc->sc_ioh2))
		panic("%s: can't map io port range 2", self->dv_xname);

	/* XXX Maybe we shouldn't fail on mapping this, but just assume
	 * the card is of revision 0? */
	if (bus_space_map(sc->sc_iot, sc->sc_iobase+GUS_IOH3_OFFSET, GUS_NPORT3, 0,
	    &sc->sc_ioh3))
		panic("%s: can't map io port range 3", self->dv_xname);

	if (bus_space_map(sc->sc_iot, sc->sc_iobase+GUS_IOH4_OFFSET, GUS_NPORT4, 0,
	    &sc->sc_ioh4))
		panic("%s: can't map io port range 4", self->dv_xname);

	sc->sc_irq = ia->ia_irq;
	sc->sc_drq = ia->ia_drq;
	sc->sc_recdrq = ia->ia_drq2;
	sc->sc_isa = parent;

	gus_subattach(sc, aux);
}
