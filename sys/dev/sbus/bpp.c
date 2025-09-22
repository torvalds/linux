/*	$OpenBSD: bpp.c,v 1.7 2024/04/14 03:23:13 jsg Exp $	*/
/*	$NetBSD: bpp.c,v 1.25 2005/12/11 12:23:44 christos Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
#include <sys/ioctl.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/conf.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/conf.h>
#include <machine/intr.h>

#include <dev/ic/lsi64854reg.h>
#include <dev/ic/lsi64854var.h>

#include <dev/sbus/sbusvar.h>
#include <dev/sbus/bppreg.h>

#define splbpp()	spltty()	/* XXX */

#ifdef DEBUG
#define DPRINTF(x) do { if (bppdebug) printf x ; } while (0)
int bppdebug = 1;
#else
#define DPRINTF(x)
#endif

#if 0
struct bpp_param {
	int	bpp_dss;		/* data setup to strobe */
	int	bpp_dsw;		/* data strobe width */
	int	bpp_outputpins;		/* Select/Autofeed/Init pins */
	int	bpp_inputpins;		/* Error/Select/Paperout pins */
};
#endif

struct hwstate {
	u_int16_t	hw_hcr;		/* Hardware config register */
	u_int16_t	hw_ocr;		/* Operation config register */
	u_int8_t	hw_tcr;		/* Transfer Control register */
	u_int8_t	hw_or;		/* Output register */
	u_int16_t	hw_irq;		/* IRQ; polarity bits only */
};

struct bpp_softc {
	struct lsi64854_softc	sc_lsi64854;	/* base device */

	size_t		sc_bufsz;		/* temp buffer */
	caddr_t		sc_buf;

	int		sc_error;		/* bottom-half error */
	int		sc_flags;
#define BPP_LOCKED	0x01		/* DMA in progress */
#define BPP_WANT	0x02		/* Waiting for DMA */

	/* Hardware state */
	struct hwstate		sc_hwstate;
};

int	bppmatch(struct device *, void *, void *);
void	bppattach(struct device *, struct device *, void *);
int	bppintr		(void *);
void	bpp_setparams(struct bpp_softc *, struct hwstate *);

const struct cfattach bpp_ca = {
	sizeof(struct bpp_softc), bppmatch, bppattach
};

struct cfdriver bpp_cd = {
	NULL, "bpp", DV_DULL
};

#define BPPUNIT(dev)	(minor(dev))

int
bppmatch(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp("SUNW,bpp", sa->sa_name) == 0);
}

void
bppattach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct bpp_softc *dsc = (void *)self;
	struct lsi64854_softc *sc = &dsc->sc_lsi64854;
	int burst, sbusburst;
	int node;

	node = sa->sa_node;

	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	/* Map device registers */
	if (sa->sa_npromvaddrs != 0) {
		if (sbus_bus_map(sa->sa_bustag, 0, sa->sa_promvaddrs[0],
		    sa->sa_size,		/* ???? */
		    BUS_SPACE_MAP_PROMADDRESS, 0, &sc->sc_regs) != 0) {
			printf(": cannot map registers\n");
			return;
		}
	} else if (sbus_bus_map(sa->sa_bustag, sa->sa_slot, sa->sa_offset,
	    sa->sa_size, 0, 0, &sc->sc_regs) != 0) {
		printf(": cannot map registers\n");
		return;
	}

	/* Check for the interrupt property */
	if (sa->sa_nintr == 0) {
		printf(": no interrupt property\n");
		return;
	}

	/*
	 * Get transfer burst size from PROM and plug it into the
	 * controller registers. This is needed on the Sun4m; do
	 * others need it too?
	 */
	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

	burst = getpropint(node, "burst-sizes", -1);
	if (burst == -1)
		/* take SBus burst sizes */
		burst = sbusburst;

	/* Clamp at parent's burst sizes */
	burst &= sbusburst;
	sc->sc_burst = (burst & SBUS_BURST_32) ? 32 :
		       (burst & SBUS_BURST_16) ? 16 : 0;

	/* Initialize the DMA channel */
	sc->sc_channel = L64854_CHANNEL_PP;
	if (lsi64854_attach(sc) != 0)
		return;

	/* Establish interrupt handler */
	sc->sc_intrchain = bppintr;
	sc->sc_intrchainarg = dsc;
	(void)bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_TTY, 0,
	    bppintr, sc, self->dv_xname);

	/* Allocate buffer XXX - should actually use dmamap_uio() */
	dsc->sc_bufsz = 1024;
	dsc->sc_buf = malloc(dsc->sc_bufsz, M_DEVBUF, M_NOWAIT);

	/* XXX read default state */
    {
	bus_space_handle_t h = sc->sc_regs;
	struct hwstate *hw = &dsc->sc_hwstate;
	int ack_rate = sa->sa_frequency/1000000;

	hw->hw_hcr = bus_space_read_2(sc->sc_bustag, h, L64854_REG_HCR);
	hw->hw_ocr = bus_space_read_2(sc->sc_bustag, h, L64854_REG_OCR);
	hw->hw_tcr = bus_space_read_1(sc->sc_bustag, h, L64854_REG_TCR);
	hw->hw_or = bus_space_read_1(sc->sc_bustag, h, L64854_REG_OR);

	DPRINTF(("bpp: hcr %x ocr %x tcr %x or %x\n",
		 hw->hw_hcr, hw->hw_ocr, hw->hw_tcr, hw->hw_or));
	/* Set these to sane values */
	hw->hw_hcr = ((ack_rate<<BPP_HCR_DSS_SHFT)&BPP_HCR_DSS_MASK)
		| ((ack_rate<<BPP_HCR_DSW_SHFT)&BPP_HCR_DSW_MASK);
	hw->hw_ocr |= BPP_OCR_ACK_OP;
    }
}

void
bpp_setparams(struct bpp_softc *sc, struct hwstate *hw)
{
	u_int16_t irq;
	bus_space_tag_t t = sc->sc_lsi64854.sc_bustag;
	bus_space_handle_t h = sc->sc_lsi64854.sc_regs;

	bus_space_write_2(t, h, L64854_REG_HCR, hw->hw_hcr);
	bus_space_write_2(t, h, L64854_REG_OCR, hw->hw_ocr);
	bus_space_write_1(t, h, L64854_REG_TCR, hw->hw_tcr);
	bus_space_write_1(t, h, L64854_REG_OR, hw->hw_or);

	/* Only change IRP settings in interrupt status register */
	irq = bus_space_read_2(t, h, L64854_REG_ICR);
	irq &= ~BPP_ALLIRP;
	irq |= (hw->hw_irq & BPP_ALLIRP);
	bus_space_write_2(t, h, L64854_REG_ICR, irq);
	DPRINTF(("bpp_setparams: hcr %x ocr %x tcr %x or %x, irq %x\n",
		 hw->hw_hcr, hw->hw_ocr, hw->hw_tcr, hw->hw_or, irq));
}

int
bppopen(dev_t dev, int flags, int mode, struct proc *p)
{
	int unit = BPPUNIT(dev);
	struct bpp_softc *sc;
	struct lsi64854_softc *lsi;
	u_int16_t irq;
	int s;

	if (unit >= bpp_cd.cd_ndevs)
		return (ENXIO);
	if ((sc = bpp_cd.cd_devs[unit]) == NULL)
		return (ENXIO);

	lsi = &sc->sc_lsi64854;

	/* Set default parameters */
	s = splbpp();
	bpp_setparams(sc, &sc->sc_hwstate);
	splx(s);

	/* Enable interrupts */
	irq = BPP_ERR_IRQ_EN;
	irq |= sc->sc_hwstate.hw_irq;
	bus_space_write_2(lsi->sc_bustag, lsi->sc_regs, L64854_REG_ICR, irq);
	return (0);
}

int
bppclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct bpp_softc *sc = bpp_cd.cd_devs[BPPUNIT(dev)];
	struct lsi64854_softc *lsi = &sc->sc_lsi64854;
	u_int16_t irq;

	/* Turn off all interrupt enables */
	irq = sc->sc_hwstate.hw_irq | BPP_ALLIRQ;
	irq &= ~BPP_ALLEN;
	bus_space_write_2(lsi->sc_bustag, lsi->sc_regs, L64854_REG_ICR, irq);

	sc->sc_flags = 0;
	return (0);
}

int
bppwrite(dev_t dev, struct uio *uio, int flags)
{
	struct bpp_softc *sc = bpp_cd.cd_devs[BPPUNIT(dev)];
	struct lsi64854_softc *lsi = &sc->sc_lsi64854;
	int error = 0;
	int s;

	/*
	 * Wait until the DMA engine is free.
	 */
	s = splbpp();
	while ((sc->sc_flags & BPP_LOCKED) != 0) {
		if ((flags & IO_NDELAY) != 0) {
			splx(s);
			return (EWOULDBLOCK);
		}

		sc->sc_flags |= BPP_WANT;
		error = tsleep_nsec(sc->sc_buf, PZERO | PCATCH, "bppwrite",
		    INFSLP);
		if (error != 0) {
			splx(s);
			return (error);
		}
	}
	sc->sc_flags |= BPP_LOCKED;
	splx(s);

	/*
	 * Move data from user space into our private buffer
	 * and start DMA.
	 */
	while (uio->uio_resid > 0) {
		caddr_t bp = sc->sc_buf;
		size_t len = ulmin(sc->sc_bufsz, uio->uio_resid);

		if ((error = uiomove(bp, len, uio)) != 0)
			break;

		while (len > 0) {
			u_int8_t tcr;
			size_t size = len;
			DMA_SETUP(lsi, &bp, &len, 0, &size);

#ifdef DEBUG
			if (bppdebug) {
				size_t i;
				printf("bpp: writing %ld : ", len);
				for (i=0; i<len; i++)
					printf("%c(0x%x)", bp[i], bp[i]);
				printf("\n");
			}
#endif

			/* Clear direction control bit */
			tcr = bus_space_read_1(lsi->sc_bustag, lsi->sc_regs,
						L64854_REG_TCR);
			tcr &= ~BPP_TCR_DIR;
			bus_space_write_1(lsi->sc_bustag, lsi->sc_regs,
					  L64854_REG_TCR, tcr);

			/* Enable DMA */
			s = splbpp();
			DMA_GO(lsi);
			error = tsleep_nsec(sc, PZERO | PCATCH, "bppdma",
			    INFSLP);
			splx(s);
			if (error != 0)
				goto out;

			/* Bail out if bottom half reported an error */
			if ((error = sc->sc_error) != 0)
				goto out;

			/*
			 * DMA_INTR() does this part.
			 *
			 * len -= size;
			 */
		}
	}

out:
	DPRINTF(("bpp done %x\n", error));
	s = splbpp();
	sc->sc_flags &= ~BPP_LOCKED;
	if ((sc->sc_flags & BPP_WANT) != 0) {
		sc->sc_flags &= ~BPP_WANT;
		wakeup(sc->sc_buf);
	}
	splx(s);
	return (error);
}

int
bppioctl(dev_t dev, u_long cmd, caddr_t	data, int flag, struct proc *p)
{
	int error = 0;

	switch(cmd) {
	default:
		error = ENODEV;
		break;
	}

	return (error);
}

int
bppintr(void *arg)
{
	struct bpp_softc *sc = arg;
	struct lsi64854_softc *lsi = &sc->sc_lsi64854;
	u_int16_t irq;

	/* First handle any possible DMA interrupts */
	if (DMA_INTR(lsi) == -1)
		sc->sc_error = 1;

	irq = bus_space_read_2(lsi->sc_bustag, lsi->sc_regs, L64854_REG_ICR);
	/* Ack all interrupts */
	bus_space_write_2(lsi->sc_bustag, lsi->sc_regs, L64854_REG_ICR,
			  irq | BPP_ALLIRQ);

	DPRINTF(("bpp_intr: %x\n", irq));
	/* Did our device interrupt? */
	if ((irq & BPP_ALLIRQ) == 0)
		return (0);

	if ((sc->sc_flags & BPP_LOCKED) != 0)
		wakeup(sc);
	else if ((sc->sc_flags & BPP_WANT) != 0) {
		sc->sc_flags &= ~BPP_WANT;
		wakeup(sc->sc_buf);
	}
	return (1);
}
