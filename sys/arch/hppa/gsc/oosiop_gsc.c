/*	$OpenBSD: oosiop_gsc.c,v 1.5 2025/06/28 13:24:21 miod Exp $	*/
/*	$NetBSD: oosiop_gsc.c,v 1.2 2003/07/15 02:29:25 lukem Exp $	*/

/*
 * Copyright (c) 2001 Matt Fredette.  All rights reserved.
 * Copyright (c) 2001,2002 Izumi Tsutsui.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1998 Michael Shalayeff
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/malloc.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>
#include <machine/bus.h>

#include <dev/ic/oosiopreg.h>
#include <dev/ic/oosiopvar.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/gsc/gscbusvar.h>

#define	OOSIOP_GSC_RESET	0x0000
#define	OOSIOP_GSC_OFFSET	0x0100

int oosiop_gsc_match(struct device *, void *, void *);
void oosiop_gsc_attach(struct device *, struct device *, void *);
int oosiop_gsc_intr(void *);

const struct cfattach oosiop_gsc_ca = {
	sizeof(struct oosiop_softc), oosiop_gsc_match, oosiop_gsc_attach
};

int
oosiop_gsc_match(struct device *parent, void *match, void *aux)
{
	struct gsc_attach_args *ga = aux;

	if (ga->ga_type.iodc_type != HPPA_TYPE_FIO ||
	     ga->ga_type.iodc_sv_model != HPPA_FIO_SCSI)
		return 0;

	return 1;
}

void
oosiop_gsc_attach(struct device *parent, struct device *self, void *aux)
{
	struct oosiop_softc *sc = (void *)self;
	struct gsc_attach_args *ga = aux;
	bus_space_handle_t ioh;

	sc->sc_bst = ga->ga_iot;
	sc->sc_dmat = ga->ga_dmatag;
	if (bus_space_map(sc->sc_bst, ga->ga_hpa,
	    OOSIOP_GSC_OFFSET + OOSIOP_NREGS, 0, &ioh))
		panic("oosiop_gsc_attach: couldn't map I/O ports");
	if (bus_space_subregion(sc->sc_bst, ioh, 
	    OOSIOP_GSC_OFFSET, OOSIOP_NREGS, &sc->sc_bsh))
		panic("oosiop_gsc_attach: couldn't get chip ports");

	sc->sc_freq = ga->ga_ca.ca_pdc_iodc_read->filler2[14];
	if (!sc->sc_freq)
		sc->sc_freq = 50 * 1000000;

	sc->sc_chip = OOSIOP_700;
	sc->sc_id = 7;	/* XXX */

	/* default values */
	sc->sc_scntl0 = OOSIOP_SCNTL0_EPG;
	sc->sc_dmode = OOSIOP_DMODE_BL_8;
	sc->sc_dwt = 0xff;	/* Enable DMA timeout */
	sc->sc_ctest7 = 0;

	/*
	 * Reset the SCSI subsystem.
	 */
	bus_space_write_1(sc->sc_bst, ioh, OOSIOP_GSC_RESET, 0);
	DELAY(1000);

	/*
	 * Call common attachment
	 */
#ifdef OOSIOP_DEBUG
	{
		extern int oosiop_debug;
		oosiop_debug = -1;
	}
#endif /* OOSIOP_DEBUG */
	oosiop_attach(sc);

	(void)gsc_intr_establish((struct gsc_softc *)parent,
	    ga->ga_irq, IPL_BIO, oosiop_gsc_intr, sc, sc->sc_dev.dv_xname);
}

/*
 * interrupt handler
 */
int
oosiop_gsc_intr(void *arg)
{
	struct oosiop_softc *sc = arg;
	int rv;

	rv = oosiop_intr(sc);

#ifdef USELEDS
	ledctl(PALED_DISK, 0, 0);
#endif

	return (rv);
}
