/*	$OpenBSD: wdc_obio.c,v 1.5 2022/04/06 18:59:26 naddy Exp $	*/
/*	$NetBSD: wdc_obio.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 1998, 2003 The NetBSD Foundation, Inc.
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

#include <dev/ata/atavar.h>
#include <dev/ic/wdcvar.h>

#include <landisk/dev/obiovar.h>

struct wdc_obio_softc {
	struct	wdc_softc sc_wdcdev;
	struct	channel_softc *sc_chanptr;
	struct	channel_softc sc_channel;

	void	*sc_ih;
};

int	wdc_obio_match(struct device *, void *, void *);
void	wdc_obio_attach(struct device *, struct device *, void *);

const struct cfattach wdc_obio_ca = {
	sizeof(struct wdc_obio_softc), wdc_obio_match, wdc_obio_attach
};

#define	WDC_OBIO_REG_NPORTS	WDC_NREG
#define	WDC_OBIO_REG_SIZE	(WDC_OBIO_REG_NPORTS * 2)
#define	WDC_OBIO_AUXREG_NPORTS	1
#define	WDC_OBIO_AUXREG_SIZE	(WDC_OBIO_AUXREG_NPORTS * 2)
#define	WDC_OBIO_AUXREG_OFFSET	0x2c

u_int8_t wdc_obio_read_reg(struct channel_softc *chp,  enum wdc_regs reg);
void wdc_obio_write_reg(struct channel_softc *chp,  enum wdc_regs reg,
    u_int8_t val);

struct channel_softc_vtbl wdc_obio_vtbl = {
	wdc_obio_read_reg,
	wdc_obio_write_reg,
	wdc_default_lba48_write_reg,
	wdc_default_read_raw_multi_2,
	wdc_default_write_raw_multi_2,
	wdc_default_read_raw_multi_4,
	wdc_default_write_raw_multi_4
};

int
wdc_obio_match(struct device *parent, void *vcf, void *aux)
{
	struct obio_attach_args *oa = aux;

	if (oa->oa_nio != 1)
		return (0);
	if (oa->oa_nirq != 1)
		return (0);
	if (oa->oa_niomem != 0)
		return (0);

	if (oa->oa_io[0].or_addr == IOBASEUNK)
		return (0);
	if (oa->oa_irq[0].or_irq == IRQUNK)
		return (0);

	/* XXX should probe for hardware */

	oa->oa_io[0].or_size = WDC_OBIO_REG_SIZE;

	return (1);
}

void
wdc_obio_attach(struct device *parent, struct device *self, void *aux)
{
	struct wdc_obio_softc *sc = (void *)self;
	struct obio_attach_args *oa = aux;
	struct channel_softc *chp = &sc->sc_channel;

	printf("\n");

	chp->cmd_iot = chp->ctl_iot = oa->oa_iot;
	chp->_vtbl = &wdc_obio_vtbl;

	if (bus_space_map(chp->cmd_iot, oa->oa_io[0].or_addr,
	    WDC_OBIO_REG_SIZE, 0, &chp->cmd_ioh)
	 || bus_space_map(chp->ctl_iot,
	    oa->oa_io[0].or_addr + WDC_OBIO_AUXREG_OFFSET,
	    WDC_OBIO_AUXREG_SIZE, 0, &chp->ctl_ioh)) {
		printf(": couldn't map registers\n");
		return;
	}

	sc->sc_ih = obio_intr_establish(oa->oa_irq[0].or_irq, IPL_BIO, wdcintr,
	    chp, self->dv_xname);

	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_PREATA;
	sc->sc_wdcdev.PIO_cap = 0;
	sc->sc_chanptr = chp;
	sc->sc_wdcdev.channels = &sc->sc_chanptr;
	sc->sc_wdcdev.nchannels = 1;
	chp->channel = 0;
	chp->wdc = &sc->sc_wdcdev;

	chp->ch_queue = wdc_alloc_queue();
	if (chp->ch_queue == NULL) {
		printf("%s: cannot allocate channel queue\n",
		    self->dv_xname);
		obio_intr_disestablish(sc->sc_ih);
		return;
	}

	wdcattach(chp);
	wdc_print_current_modes(chp);
}

u_int8_t
wdc_obio_read_reg(struct channel_softc *chp,  enum wdc_regs reg)
{
	if (reg & _WDC_AUX) 
		return (bus_space_read_1(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 1));
	else
		return (bus_space_read_1(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 1));
}

void
wdc_obio_write_reg(struct channel_softc *chp,  enum wdc_regs reg, u_int8_t val)
{
	if (reg & _WDC_AUX) 
		bus_space_write_1(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 1, val);
	else
		bus_space_write_1(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 1, val);
}
