/*	$OpenBSD: qec.c,v 1.17 2022/10/16 01:22:40 jsg Exp $	*/
/*	$NetBSD: qec.c,v 1.12 2000/12/04 20:12:55 fvdl Exp $ */

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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>
#include <dev/sbus/qecreg.h>
#include <dev/sbus/qecvar.h>

int	qecprint(void *, const char *);
int	qecmatch(struct device *, void *, void *);
void	qecattach(struct device *, struct device *, void *);
void	qec_init(struct qec_softc *);

int	qec_bus_map(
		bus_space_tag_t,
		bus_space_tag_t,
		bus_addr_t,		/*offset*/
		bus_size_t,		/*size*/
		int,			/*flags*/
		bus_space_handle_t *);
void *	qec_intr_establish(
		bus_space_tag_t,
		bus_space_tag_t,
		int,			/*bus interrupt priority*/
		int,			/*`device class' interrupt level*/
		int,			/*flags*/
		int (*)(void *),	/*handler*/
		void *,			/*arg*/
		const char *);		/*what*/

const struct cfattach qec_ca = {
	sizeof(struct qec_softc), qecmatch, qecattach
};

struct cfdriver qec_cd = {
	NULL, "qec", DV_DULL
};

int
qecprint(void *aux, const char *busname)
{
	struct sbus_attach_args *sa = aux;
	bus_space_tag_t t = sa->sa_bustag;
	struct qec_softc *sc = t->cookie;

	sa->sa_bustag = sc->sc_bustag;	/* XXX */
	sbus_print(aux, busname);	/* XXX */
	sa->sa_bustag = t;		/* XXX */
	return (UNCONF);
}

int
qecmatch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}

/*
 * Attach all the sub-devices we can find
 */
void
qecattach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct qec_softc *sc = (void *)self;
	int node;
	int sbusburst;
	struct sparc_bus_space_tag *sbt;
	bus_space_handle_t bh;
	int error;

	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;
	node = sa->sa_node;

	if (sa->sa_nreg < 2) {
		printf("%s: only %d register sets\n",
			self->dv_xname, sa->sa_nreg);
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size,
	    0, 0, &sc->sc_regs) != 0) {
		printf("%s: attach: cannot map registers\n", self->dv_xname);
		return;
	}

	/*
	 * This device's "register space 1" is just a buffer where the
	 * Lance ring-buffers can be stored. Note the buffer's location
	 * and size, so the child driver can pick them up.
	 */
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
	    sa->sa_reg[1].sbr_offset, sa->sa_reg[1].sbr_size, 0, 0, &bh) != 0) {
		printf("%s: attach: cannot map registers\n", self->dv_xname);
		return;
	}
	sc->sc_buffer = (caddr_t)bus_space_vaddr(sc->sc_bustag, bh);
	sc->sc_bufsiz = (bus_size_t)sa->sa_reg[1].sbr_size;

	/* Get number of on-board channels */
	sc->sc_nchannels = getpropint(node, "#channels", -1);
	if (sc->sc_nchannels == -1) {
		printf(": no channels\n");
		return;
	}

	/*
	 * Get transfer burst size from PROM
	 */
	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

	sc->sc_burst = getpropint(node, "burst-sizes", -1);
	if (sc->sc_burst == -1)
		/* take SBus burst sizes */
		sc->sc_burst = sbusburst;

	/* Clamp at parent's burst sizes */
	sc->sc_burst &= sbusburst;

	/*
	 * Collect address translations from the OBP.
	 */
	error = getprop(node, "ranges", sizeof(struct sbus_range),
			 &sc->sc_nrange, (void **)&sc->sc_range);
	switch (error) {
	case 0:
		break;
	case ENOENT:
	default:
		panic("%s: error getting ranges property", self->dv_xname);
	}

	/* Allocate a bus tag */
	sbt = malloc(sizeof(*sbt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sbt == NULL) {
		printf("%s: attach: out of memory\n", self->dv_xname);
		return;
	}

	strlcpy(sbt->name, sc->sc_dev.dv_xname, sizeof(sbt->name));
	sbt->cookie = sc;
	sbt->parent = sc->sc_bustag;
	sbt->asi = sbt->parent->asi;
	sbt->sasi = sbt->parent->sasi;
	sbt->sparc_bus_map = qec_bus_map;
	sbt->sparc_intr_establish = qec_intr_establish;

	/*
	 * Save interrupt information for use in our qec_intr_establish()
	 * function below. Apparently, the intr level for the quad
	 * ethernet board (qe) is stored in the QEC node rather than
	 * separately in each of the QE nodes.
	 *
	 * XXX - qe.c should call bus_intr_establish() with `level = 0'..
	 * XXX - maybe we should have our own attach args for all that.
	 */
	sc->sc_intr = sa->sa_intr;

	printf(": %dK memory\n", sc->sc_bufsiz / 1024);

	qec_init(sc);

	/* search through children */
	for (node = firstchild(node); node; node = nextsibling(node)) {
		struct sbus_attach_args sa;

		sbus_setup_attach_args((struct sbus_softc *)parent,
				       sbt, sc->sc_dmatag, node, &sa);
		(void)config_found(&sc->sc_dev, (void *)&sa, qecprint);
		sbus_destroy_attach_args(&sa);
	}
}

int
qec_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t addr,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct qec_softc *sc = t->cookie;
	int slot = BUS_ADDR_IOSPACE(addr);
	bus_addr_t offset = BUS_ADDR_PADDR(addr);
	int i;

	for (t = t->parent; t; t = t->parent) {
		if (t->sparc_bus_map != NULL)
			break;
	}

        if (t == NULL) {
                printf("\nqec_bus_map: invalid parent");
                return (EINVAL);
        }

        if (flags & BUS_SPACE_MAP_PROMADDRESS) {
                return ((*t->sparc_bus_map)
                    (t, t0, offset, size, flags, hp));
        }

	for (i = 0; i < sc->sc_nrange; i++) {
		bus_addr_t paddr;
		int iospace;

		if (sc->sc_range[i].cspace != slot)
			continue;

		/* We've found the connection to the parent bus */
		paddr = sc->sc_range[i].poffset + offset;
		iospace = sc->sc_range[i].pspace;
                return ((*t->sparc_bus_map)
                    (t, t0, BUS_ADDR(iospace, paddr), size, flags, hp));
	}

	return (EINVAL);
}

void *
qec_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int pri, int level,
    int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct qec_softc *sc = t->cookie;

	if (pri == 0) {
		/*
		 * qe.c calls bus_intr_establish() with `pri == 0'
		 * XXX - see also comment in qec_attach().
		 */
		if (sc->sc_intr == NULL) {
			printf("%s: warning: no interrupts\n",
				sc->sc_dev.dv_xname);
			return (NULL);
		}
		pri = sc->sc_intr->sbi_pri;
	}

	for (t = t->parent; t; t = t->parent) {
		if (t->sparc_intr_establish != NULL)
			return ((*t->sparc_intr_establish)
			    (t, t0, pri, level, flags, handler, arg, what));
	}

	panic("qec_intr_establish): no handler found");

	return (NULL);
}

void
qec_init(struct qec_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t qr = sc->sc_regs;
	u_int32_t v, burst = 0, psize;
	int i;

	/* First, reset the controller */
	bus_space_write_4(t, qr, QEC_QRI_CTRL, QEC_CTRL_RESET);
	for (i = 0; i < 1000; i++) {
		DELAY(100);
		v = bus_space_read_4(t, qr, QEC_QRI_CTRL);
		if ((v & QEC_CTRL_RESET) == 0)
			break;
	}

	/*
	 * Cut available buffer size into receive and transmit buffers.
	 * XXX - should probably be done in be & qe driver...
	 */
	v = sc->sc_msize = sc->sc_bufsiz / sc->sc_nchannels;
	bus_space_write_4(t, qr, QEC_QRI_MSIZE, v);

	v = sc->sc_rsize = sc->sc_bufsiz / (sc->sc_nchannels * 2);
	bus_space_write_4(t, qr, QEC_QRI_RSIZE, v);
	bus_space_write_4(t, qr, QEC_QRI_TSIZE, v);

	psize = sc->sc_nchannels == 1 ? QEC_PSIZE_2048 : 0;
	bus_space_write_4(t, qr, QEC_QRI_PSIZE, psize);

	if (sc->sc_burst & SBUS_BURST_64)
		burst = QEC_CTRL_B64;
	else if (sc->sc_burst & SBUS_BURST_32)
		burst = QEC_CTRL_B32;
	else
		burst = QEC_CTRL_B16;

	v = bus_space_read_4(t, qr, QEC_QRI_CTRL);
	v = (v & QEC_CTRL_MODEMASK) | burst;
	bus_space_write_4(t, qr, QEC_QRI_CTRL, v);
}

/*
 * Common routine to initialize the QEC packet ring buffer.
 * Called from be & qe drivers.
 */
void
qec_meminit(struct qec_ring *qr, unsigned int pktbufsz)
{
	bus_addr_t txbufdma, rxbufdma;
	bus_addr_t dma;
	caddr_t p;
	unsigned int ntbuf, nrbuf, i;

	p = qr->rb_membase;
	dma = qr->rb_dmabase;

	ntbuf = qr->rb_ntbuf;
	nrbuf = qr->rb_nrbuf;

	/*
	 * Allocate transmit descriptors
	 */
	qr->rb_txd = (struct qec_xd *)p;
	qr->rb_txddma = dma;
	p += QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd);
	dma += QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd);

	/*
	 * Allocate receive descriptors
	 */
	qr->rb_rxd = (struct qec_xd *)p;
	qr->rb_rxddma = dma;
	p += QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd);
	dma += QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd);


	/*
	 * Allocate transmit buffers
	 */
	qr->rb_txbuf = p;
	txbufdma = dma;
	p += ntbuf * pktbufsz;
	dma += ntbuf * pktbufsz;

	/*
	 * Allocate receive buffers
	 */
	qr->rb_rxbuf = p;
	rxbufdma = dma;
	p += nrbuf * pktbufsz;
	dma += nrbuf * pktbufsz;

	/*
	 * Initialize transmit buffer descriptors
	 */
	for (i = 0; i < QEC_XD_RING_MAXSIZE; i++) {
		qr->rb_txd[i].xd_addr = (u_int32_t)
			(txbufdma + (i % ntbuf) * pktbufsz);
		qr->rb_txd[i].xd_flags = 0;
	}

	/*
	 * Initialize receive buffer descriptors
	 */
	for (i = 0; i < QEC_XD_RING_MAXSIZE; i++) {
		qr->rb_rxd[i].xd_addr = (u_int32_t)
			(rxbufdma + (i % nrbuf) * pktbufsz);
		qr->rb_rxd[i].xd_flags = (i < nrbuf)
			? QEC_XD_OWN | (pktbufsz & QEC_XD_LENGTH)
			: 0;
	}

	qr->rb_tdhead = qr->rb_tdtail = 0;
	qr->rb_td_nbusy = 0;
	qr->rb_rdtail = 0;
}
