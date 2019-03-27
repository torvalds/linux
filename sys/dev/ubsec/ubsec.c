/*	$OpenBSD: ubsec.c,v 1.115 2002/09/24 18:33:26 jason Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2000 Theo de Raadt (deraadt@openbsd.org)
 * Copyright (c) 2001 Patrik Lindergren (patrik@ipunplugged.com)
 *
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
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * uBsec 5[56]01, 58xx hardware crypto accelerator
 */

#include "opt_ubsec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/endian.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <crypto/sha1.h>
#include <opencrypto/cryptodev.h>
#include <opencrypto/cryptosoft.h>
#include <sys/md5.h>
#include <sys/random.h>
#include <sys/kobj.h>

#include "cryptodev_if.h"

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/* grr, #defines for gratuitous incompatibility in queue.h */
#define	SIMPLEQ_HEAD		STAILQ_HEAD
#define	SIMPLEQ_ENTRY		STAILQ_ENTRY
#define	SIMPLEQ_INIT		STAILQ_INIT
#define	SIMPLEQ_INSERT_TAIL	STAILQ_INSERT_TAIL
#define	SIMPLEQ_EMPTY		STAILQ_EMPTY
#define	SIMPLEQ_FIRST		STAILQ_FIRST
#define	SIMPLEQ_REMOVE_HEAD	STAILQ_REMOVE_HEAD
#define	SIMPLEQ_FOREACH		STAILQ_FOREACH
/* ditto for endian.h */
#define	letoh16(x)		le16toh(x)
#define	letoh32(x)		le32toh(x)

#ifdef UBSEC_RNDTEST
#include <dev/rndtest/rndtest.h>
#endif
#include <dev/ubsec/ubsecreg.h>
#include <dev/ubsec/ubsecvar.h>

/*
 * Prototypes and count for the pci_device structure
 */
static	int ubsec_probe(device_t);
static	int ubsec_attach(device_t);
static	int ubsec_detach(device_t);
static	int ubsec_suspend(device_t);
static	int ubsec_resume(device_t);
static	int ubsec_shutdown(device_t);

static	int ubsec_newsession(device_t, crypto_session_t, struct cryptoini *);
static	int ubsec_process(device_t, struct cryptop *, int);
static	int ubsec_kprocess(device_t, struct cryptkop *, int);

static device_method_t ubsec_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ubsec_probe),
	DEVMETHOD(device_attach,	ubsec_attach),
	DEVMETHOD(device_detach,	ubsec_detach),
	DEVMETHOD(device_suspend,	ubsec_suspend),
	DEVMETHOD(device_resume,	ubsec_resume),
	DEVMETHOD(device_shutdown,	ubsec_shutdown),

	/* crypto device methods */
	DEVMETHOD(cryptodev_newsession,	ubsec_newsession),
	DEVMETHOD(cryptodev_process,	ubsec_process),
	DEVMETHOD(cryptodev_kprocess,	ubsec_kprocess),

	DEVMETHOD_END
};
static driver_t ubsec_driver = {
	"ubsec",
	ubsec_methods,
	sizeof (struct ubsec_softc)
};
static devclass_t ubsec_devclass;

DRIVER_MODULE(ubsec, pci, ubsec_driver, ubsec_devclass, 0, 0);
MODULE_DEPEND(ubsec, crypto, 1, 1, 1);
#ifdef UBSEC_RNDTEST
MODULE_DEPEND(ubsec, rndtest, 1, 1, 1);
#endif

static	void ubsec_intr(void *);
static	void ubsec_callback(struct ubsec_softc *, struct ubsec_q *);
static	void ubsec_feed(struct ubsec_softc *);
static	void ubsec_mcopy(struct mbuf *, struct mbuf *, int, int);
static	void ubsec_callback2(struct ubsec_softc *, struct ubsec_q2 *);
static	int ubsec_feed2(struct ubsec_softc *);
static	void ubsec_rng(void *);
static	int ubsec_dma_malloc(struct ubsec_softc *, bus_size_t,
			     struct ubsec_dma_alloc *, int);
#define	ubsec_dma_sync(_dma, _flags) \
	bus_dmamap_sync((_dma)->dma_tag, (_dma)->dma_map, (_flags))
static	void ubsec_dma_free(struct ubsec_softc *, struct ubsec_dma_alloc *);
static	int ubsec_dmamap_aligned(struct ubsec_operand *op);

static	void ubsec_reset_board(struct ubsec_softc *sc);
static	void ubsec_init_board(struct ubsec_softc *sc);
static	void ubsec_init_pciregs(device_t dev);
static	void ubsec_totalreset(struct ubsec_softc *sc);

static	int ubsec_free_q(struct ubsec_softc *sc, struct ubsec_q *q);

static	int ubsec_kprocess_modexp_hw(struct ubsec_softc *, struct cryptkop *, int);
static	int ubsec_kprocess_modexp_sw(struct ubsec_softc *, struct cryptkop *, int);
static	int ubsec_kprocess_rsapriv(struct ubsec_softc *, struct cryptkop *, int);
static	void ubsec_kfree(struct ubsec_softc *, struct ubsec_q2 *);
static	int ubsec_ksigbits(struct crparam *);
static	void ubsec_kshift_r(u_int, u_int8_t *, u_int, u_int8_t *, u_int);
static	void ubsec_kshift_l(u_int, u_int8_t *, u_int, u_int8_t *, u_int);

static SYSCTL_NODE(_hw, OID_AUTO, ubsec, CTLFLAG_RD, 0,
    "Broadcom driver parameters");

#ifdef UBSEC_DEBUG
static	void ubsec_dump_pb(volatile struct ubsec_pktbuf *);
static	void ubsec_dump_mcr(struct ubsec_mcr *);
static	void ubsec_dump_ctx2(struct ubsec_ctx_keyop *);

static	int ubsec_debug = 0;
SYSCTL_INT(_hw_ubsec, OID_AUTO, debug, CTLFLAG_RW, &ubsec_debug,
	    0, "control debugging msgs");
#endif

#define	READ_REG(sc,r) \
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (r))

#define WRITE_REG(sc,reg,val) \
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, reg, val)

#define	SWAP32(x) (x) = htole32(ntohl((x)))
#define	HTOLE32(x) (x) = htole32(x)

struct ubsec_stats ubsecstats;
SYSCTL_STRUCT(_hw_ubsec, OID_AUTO, stats, CTLFLAG_RD, &ubsecstats,
	    ubsec_stats, "driver statistics");

static int
ubsec_probe(device_t dev)
{
	if (pci_get_vendor(dev) == PCI_VENDOR_SUN &&
	    (pci_get_device(dev) == PCI_PRODUCT_SUN_5821 ||
	     pci_get_device(dev) == PCI_PRODUCT_SUN_SCA1K))
		return (BUS_PROBE_DEFAULT);
	if (pci_get_vendor(dev) == PCI_VENDOR_BLUESTEEL &&
	    (pci_get_device(dev) == PCI_PRODUCT_BLUESTEEL_5501 ||
	     pci_get_device(dev) == PCI_PRODUCT_BLUESTEEL_5601))
		return (BUS_PROBE_DEFAULT);
	if (pci_get_vendor(dev) == PCI_VENDOR_BROADCOM &&
	    (pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5801 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5802 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5805 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5820 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5821 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5822 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5823 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5825
	     ))
		return (BUS_PROBE_DEFAULT);
	return (ENXIO);
}

static const char*
ubsec_partname(struct ubsec_softc *sc)
{
	/* XXX sprintf numbers when not decoded */
	switch (pci_get_vendor(sc->sc_dev)) {
	case PCI_VENDOR_BROADCOM:
		switch (pci_get_device(sc->sc_dev)) {
		case PCI_PRODUCT_BROADCOM_5801:	return "Broadcom 5801";
		case PCI_PRODUCT_BROADCOM_5802:	return "Broadcom 5802";
		case PCI_PRODUCT_BROADCOM_5805:	return "Broadcom 5805";
		case PCI_PRODUCT_BROADCOM_5820:	return "Broadcom 5820";
		case PCI_PRODUCT_BROADCOM_5821:	return "Broadcom 5821";
		case PCI_PRODUCT_BROADCOM_5822:	return "Broadcom 5822";
		case PCI_PRODUCT_BROADCOM_5823:	return "Broadcom 5823";
		case PCI_PRODUCT_BROADCOM_5825:	return "Broadcom 5825";
		}
		return "Broadcom unknown-part";
	case PCI_VENDOR_BLUESTEEL:
		switch (pci_get_device(sc->sc_dev)) {
		case PCI_PRODUCT_BLUESTEEL_5601: return "Bluesteel 5601";
		}
		return "Bluesteel unknown-part";
	case PCI_VENDOR_SUN:
		switch (pci_get_device(sc->sc_dev)) {
		case PCI_PRODUCT_SUN_5821: return "Sun Crypto 5821";
		case PCI_PRODUCT_SUN_SCA1K: return "Sun Crypto 1K";
		}
		return "Sun unknown-part";
	}
	return "Unknown-vendor unknown-part";
}

static void
default_harvest(struct rndtest_state *rsp, void *buf, u_int count)
{
	/* MarkM: FIX!! Check that this does not swamp the harvester! */
	random_harvest_queue(buf, count, RANDOM_PURE_UBSEC);
}

static int
ubsec_attach(device_t dev)
{
	struct ubsec_softc *sc = device_get_softc(dev);
	struct ubsec_dma *dmap;
	u_int32_t i;
	int rid;

	bzero(sc, sizeof (*sc));
	sc->sc_dev = dev;

	SIMPLEQ_INIT(&sc->sc_queue);
	SIMPLEQ_INIT(&sc->sc_qchip);
	SIMPLEQ_INIT(&sc->sc_queue2);
	SIMPLEQ_INIT(&sc->sc_qchip2);
	SIMPLEQ_INIT(&sc->sc_q2free);

	/* XXX handle power management */

	sc->sc_statmask = BS_STAT_MCR1_DONE | BS_STAT_DMAERR;

	if (pci_get_vendor(dev) == PCI_VENDOR_BLUESTEEL &&
	    pci_get_device(dev) == PCI_PRODUCT_BLUESTEEL_5601)
		sc->sc_flags |= UBS_FLAGS_KEY | UBS_FLAGS_RNG;

	if (pci_get_vendor(dev) == PCI_VENDOR_BROADCOM &&
	    (pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5802 ||
	     pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5805))
		sc->sc_flags |= UBS_FLAGS_KEY | UBS_FLAGS_RNG;

	if (pci_get_vendor(dev) == PCI_VENDOR_BROADCOM &&
	    pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5820)
		sc->sc_flags |= UBS_FLAGS_KEY | UBS_FLAGS_RNG |
		    UBS_FLAGS_LONGCTX | UBS_FLAGS_HWNORM | UBS_FLAGS_BIGKEY;

	if ((pci_get_vendor(dev) == PCI_VENDOR_BROADCOM &&
	     (pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5821 ||
	      pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5822 ||
	      pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5823 ||
	      pci_get_device(dev) == PCI_PRODUCT_BROADCOM_5825)) ||
	    (pci_get_vendor(dev) == PCI_VENDOR_SUN &&
	     (pci_get_device(dev) == PCI_PRODUCT_SUN_SCA1K ||
	      pci_get_device(dev) == PCI_PRODUCT_SUN_5821))) {
		/* NB: the 5821/5822 defines some additional status bits */
		sc->sc_statmask |= BS_STAT_MCR1_ALLEMPTY |
		    BS_STAT_MCR2_ALLEMPTY;
		sc->sc_flags |= UBS_FLAGS_KEY | UBS_FLAGS_RNG |
		    UBS_FLAGS_LONGCTX | UBS_FLAGS_HWNORM | UBS_FLAGS_BIGKEY;
	}

	pci_enable_busmaster(dev);

	/*
	 * Setup memory-mapping of PCI registers.
	 */
	rid = BS_BAR;
	sc->sc_sr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					   RF_ACTIVE);
	if (sc->sc_sr == NULL) {
		device_printf(dev, "cannot map register space\n");
		goto bad;
	}
	sc->sc_st = rman_get_bustag(sc->sc_sr);
	sc->sc_sh = rman_get_bushandle(sc->sc_sr);

	/*
	 * Arrange interrupt line.
	 */
	rid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					    RF_SHAREABLE|RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "could not map interrupt\n");
		goto bad1;
	}
	/*
	 * NB: Network code assumes we are blocked with splimp()
	 *     so make sure the IRQ is mapped appropriately.
	 */
	if (bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_NET | INTR_MPSAFE,
			   NULL, ubsec_intr, sc, &sc->sc_ih)) {
		device_printf(dev, "could not establish interrupt\n");
		goto bad2;
	}

	sc->sc_cid = crypto_get_driverid(dev, sizeof(struct ubsec_session),
	    CRYPTOCAP_F_HARDWARE);
	if (sc->sc_cid < 0) {
		device_printf(dev, "could not get crypto driver id\n");
		goto bad3;
	}

	/*
	 * Setup DMA descriptor area.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       0x3ffff,			/* maxsize */
			       UBS_MAX_SCATTER,		/* nsegments */
			       0xffff,			/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL, NULL,		/* lockfunc, lockarg */
			       &sc->sc_dmat)) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto bad4;
	}
	SIMPLEQ_INIT(&sc->sc_freequeue);
	dmap = sc->sc_dmaa;
	for (i = 0; i < UBS_MAX_NQUEUE; i++, dmap++) {
		struct ubsec_q *q;

		q = (struct ubsec_q *)malloc(sizeof(struct ubsec_q),
		    M_DEVBUF, M_NOWAIT);
		if (q == NULL) {
			device_printf(dev, "cannot allocate queue buffers\n");
			break;
		}

		if (ubsec_dma_malloc(sc, sizeof(struct ubsec_dmachunk),
		    &dmap->d_alloc, 0)) {
			device_printf(dev, "cannot allocate dma buffers\n");
			free(q, M_DEVBUF);
			break;
		}
		dmap->d_dma = (struct ubsec_dmachunk *)dmap->d_alloc.dma_vaddr;

		q->q_dma = dmap;
		sc->sc_queuea[i] = q;

		SIMPLEQ_INSERT_TAIL(&sc->sc_freequeue, q, q_next);
	}
	mtx_init(&sc->sc_mcr1lock, device_get_nameunit(dev),
		"mcr1 operations", MTX_DEF);
	mtx_init(&sc->sc_freeqlock, device_get_nameunit(dev),
		"mcr1 free q", MTX_DEF);

	device_printf(sc->sc_dev, "%s\n", ubsec_partname(sc));

	crypto_register(sc->sc_cid, CRYPTO_3DES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_DES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, 0, 0);

	/*
	 * Reset Broadcom chip
	 */
	ubsec_reset_board(sc);

	/*
	 * Init Broadcom specific PCI settings
	 */
	ubsec_init_pciregs(dev);

	/*
	 * Init Broadcom chip
	 */
	ubsec_init_board(sc);

#ifndef UBSEC_NO_RNG
	if (sc->sc_flags & UBS_FLAGS_RNG) {
		sc->sc_statmask |= BS_STAT_MCR2_DONE;
#ifdef UBSEC_RNDTEST
		sc->sc_rndtest = rndtest_attach(dev);
		if (sc->sc_rndtest)
			sc->sc_harvest = rndtest_harvest;
		else
			sc->sc_harvest = default_harvest;
#else
		sc->sc_harvest = default_harvest;
#endif

		if (ubsec_dma_malloc(sc, sizeof(struct ubsec_mcr),
		    &sc->sc_rng.rng_q.q_mcr, 0))
			goto skip_rng;

		if (ubsec_dma_malloc(sc, sizeof(struct ubsec_ctx_rngbypass),
		    &sc->sc_rng.rng_q.q_ctx, 0)) {
			ubsec_dma_free(sc, &sc->sc_rng.rng_q.q_mcr);
			goto skip_rng;
		}

		if (ubsec_dma_malloc(sc, sizeof(u_int32_t) *
		    UBSEC_RNG_BUFSIZ, &sc->sc_rng.rng_buf, 0)) {
			ubsec_dma_free(sc, &sc->sc_rng.rng_q.q_ctx);
			ubsec_dma_free(sc, &sc->sc_rng.rng_q.q_mcr);
			goto skip_rng;
		}

		if (hz >= 100)
			sc->sc_rnghz = hz / 100;
		else
			sc->sc_rnghz = 1;
		callout_init(&sc->sc_rngto, 1);
		callout_reset(&sc->sc_rngto, sc->sc_rnghz, ubsec_rng, sc);
skip_rng:
	;
	}
#endif /* UBSEC_NO_RNG */
	mtx_init(&sc->sc_mcr2lock, device_get_nameunit(dev),
		"mcr2 operations", MTX_DEF);

	if (sc->sc_flags & UBS_FLAGS_KEY) {
		sc->sc_statmask |= BS_STAT_MCR2_DONE;

		crypto_kregister(sc->sc_cid, CRK_MOD_EXP, 0);
#if 0
		crypto_kregister(sc->sc_cid, CRK_MOD_EXP_CRT, 0);
#endif
	}
	return (0);
bad4:
	crypto_unregister_all(sc->sc_cid);
bad3:
	bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
bad2:
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq);
bad1:
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, sc->sc_sr);
bad:
	return (ENXIO);
}

/*
 * Detach a device that successfully probed.
 */
static int
ubsec_detach(device_t dev)
{
	struct ubsec_softc *sc = device_get_softc(dev);

	/* XXX wait/abort active ops */

	/* disable interrupts */
	WRITE_REG(sc, BS_CTRL, READ_REG(sc, BS_CTRL) &~
		(BS_CTRL_MCR2INT | BS_CTRL_MCR1INT | BS_CTRL_DMAERR));

	callout_stop(&sc->sc_rngto);

	crypto_unregister_all(sc->sc_cid);

#ifdef UBSEC_RNDTEST
	if (sc->sc_rndtest)
		rndtest_detach(sc->sc_rndtest);
#endif

	while (!SIMPLEQ_EMPTY(&sc->sc_freequeue)) {
		struct ubsec_q *q;

		q = SIMPLEQ_FIRST(&sc->sc_freequeue);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_freequeue, q_next);
		ubsec_dma_free(sc, &q->q_dma->d_alloc);
		free(q, M_DEVBUF);
	}
	mtx_destroy(&sc->sc_mcr1lock);
	mtx_destroy(&sc->sc_freeqlock);
#ifndef UBSEC_NO_RNG
	if (sc->sc_flags & UBS_FLAGS_RNG) {
		ubsec_dma_free(sc, &sc->sc_rng.rng_q.q_mcr);
		ubsec_dma_free(sc, &sc->sc_rng.rng_q.q_ctx);
		ubsec_dma_free(sc, &sc->sc_rng.rng_buf);
	}
#endif /* UBSEC_NO_RNG */
	mtx_destroy(&sc->sc_mcr2lock);

	bus_generic_detach(dev);
	bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq);

	bus_dma_tag_destroy(sc->sc_dmat);
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, sc->sc_sr);

	return (0);
}

/*
 * Stop all chip i/o so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
ubsec_shutdown(device_t dev)
{
#ifdef notyet
	ubsec_stop(device_get_softc(dev));
#endif
	return (0);
}

/*
 * Device suspend routine.
 */
static int
ubsec_suspend(device_t dev)
{
	struct ubsec_softc *sc = device_get_softc(dev);

#ifdef notyet
	/* XXX stop the device and save PCI settings */
#endif
	sc->sc_suspended = 1;

	return (0);
}

static int
ubsec_resume(device_t dev)
{
	struct ubsec_softc *sc = device_get_softc(dev);

#ifdef notyet
	/* XXX retore PCI settings and start the device */
#endif
	sc->sc_suspended = 0;
	return (0);
}

/*
 * UBSEC Interrupt routine
 */
static void
ubsec_intr(void *arg)
{
	struct ubsec_softc *sc = arg;
	volatile u_int32_t stat;
	struct ubsec_q *q;
	struct ubsec_dma *dmap;
	int npkts = 0, i;

	stat = READ_REG(sc, BS_STAT);
	stat &= sc->sc_statmask;
	if (stat == 0)
		return;

	WRITE_REG(sc, BS_STAT, stat);		/* IACK */

	/*
	 * Check to see if we have any packets waiting for us
	 */
	if ((stat & BS_STAT_MCR1_DONE)) {
		mtx_lock(&sc->sc_mcr1lock);
		while (!SIMPLEQ_EMPTY(&sc->sc_qchip)) {
			q = SIMPLEQ_FIRST(&sc->sc_qchip);
			dmap = q->q_dma;

			if ((dmap->d_dma->d_mcr.mcr_flags & htole16(UBS_MCR_DONE)) == 0)
				break;

			SIMPLEQ_REMOVE_HEAD(&sc->sc_qchip, q_next);

			npkts = q->q_nstacked_mcrs;
			sc->sc_nqchip -= 1+npkts;
			/*
			 * search for further sc_qchip ubsec_q's that share
			 * the same MCR, and complete them too, they must be
			 * at the top.
			 */
			for (i = 0; i < npkts; i++) {
				if(q->q_stacked_mcr[i]) {
					ubsec_callback(sc, q->q_stacked_mcr[i]);
				} else {
					break;
				}
			}
			ubsec_callback(sc, q);
		}
		/*
		 * Don't send any more packet to chip if there has been
		 * a DMAERR.
		 */
		if (!(stat & BS_STAT_DMAERR))
			ubsec_feed(sc);
		mtx_unlock(&sc->sc_mcr1lock);
	}

	/*
	 * Check to see if we have any key setups/rng's waiting for us
	 */
	if ((sc->sc_flags & (UBS_FLAGS_KEY|UBS_FLAGS_RNG)) &&
	    (stat & BS_STAT_MCR2_DONE)) {
		struct ubsec_q2 *q2;
		struct ubsec_mcr *mcr;

		mtx_lock(&sc->sc_mcr2lock);
		while (!SIMPLEQ_EMPTY(&sc->sc_qchip2)) {
			q2 = SIMPLEQ_FIRST(&sc->sc_qchip2);

			ubsec_dma_sync(&q2->q_mcr,
			    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

			mcr = (struct ubsec_mcr *)q2->q_mcr.dma_vaddr;
			if ((mcr->mcr_flags & htole16(UBS_MCR_DONE)) == 0) {
				ubsec_dma_sync(&q2->q_mcr,
				    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
				break;
			}
			SIMPLEQ_REMOVE_HEAD(&sc->sc_qchip2, q_next);
			ubsec_callback2(sc, q2);
			/*
			 * Don't send any more packet to chip if there has been
			 * a DMAERR.
			 */
			if (!(stat & BS_STAT_DMAERR))
				ubsec_feed2(sc);
		}
		mtx_unlock(&sc->sc_mcr2lock);
	}

	/*
	 * Check to see if we got any DMA Error
	 */
	if (stat & BS_STAT_DMAERR) {
#ifdef UBSEC_DEBUG
		if (ubsec_debug) {
			volatile u_int32_t a = READ_REG(sc, BS_ERR);

			printf("dmaerr %s@%08x\n",
			    (a & BS_ERR_READ) ? "read" : "write",
			    a & BS_ERR_ADDR);
		}
#endif /* UBSEC_DEBUG */
		ubsecstats.hst_dmaerr++;
		mtx_lock(&sc->sc_mcr1lock);
		ubsec_totalreset(sc);
		ubsec_feed(sc);
		mtx_unlock(&sc->sc_mcr1lock);
	}

	if (sc->sc_needwakeup) {		/* XXX check high watermark */
		int wakeup;

		mtx_lock(&sc->sc_freeqlock);
		wakeup = sc->sc_needwakeup & (CRYPTO_SYMQ|CRYPTO_ASYMQ);
#ifdef UBSEC_DEBUG
		if (ubsec_debug)
			device_printf(sc->sc_dev, "wakeup crypto (%x)\n",
				sc->sc_needwakeup);
#endif /* UBSEC_DEBUG */
		sc->sc_needwakeup &= ~wakeup;
		mtx_unlock(&sc->sc_freeqlock);
		crypto_unblock(sc->sc_cid, wakeup);
	}
}

/*
 * ubsec_feed() - aggregate and post requests to chip
 */
static void
ubsec_feed(struct ubsec_softc *sc)
{
	struct ubsec_q *q, *q2;
	int npkts, i;
	void *v;
	u_int32_t stat;

	/*
	 * Decide how many ops to combine in a single MCR.  We cannot
	 * aggregate more than UBS_MAX_AGGR because this is the number
	 * of slots defined in the data structure.  Note that
	 * aggregation only happens if ops are marked batch'able.
	 * Aggregating ops reduces the number of interrupts to the host
	 * but also (potentially) increases the latency for processing
	 * completed ops as we only get an interrupt when all aggregated
	 * ops have completed.
	 */
	if (sc->sc_nqueue == 0)
		return;
	if (sc->sc_nqueue > 1) {
		npkts = 0;
		SIMPLEQ_FOREACH(q, &sc->sc_queue, q_next) {
			npkts++;
			if ((q->q_crp->crp_flags & CRYPTO_F_BATCH) == 0)
				break;
		}
	} else
		npkts = 1;
	/*
	 * Check device status before going any further.
	 */
	if ((stat = READ_REG(sc, BS_STAT)) & (BS_STAT_MCR1_FULL | BS_STAT_DMAERR)) {
		if (stat & BS_STAT_DMAERR) {
			ubsec_totalreset(sc);
			ubsecstats.hst_dmaerr++;
		} else
			ubsecstats.hst_mcr1full++;
		return;
	}
	if (sc->sc_nqueue > ubsecstats.hst_maxqueue)
		ubsecstats.hst_maxqueue = sc->sc_nqueue;
	if (npkts > UBS_MAX_AGGR)
		npkts = UBS_MAX_AGGR;
	if (npkts < 2)				/* special case 1 op */
		goto feed1;

	ubsecstats.hst_totbatch += npkts-1;
#ifdef UBSEC_DEBUG
	if (ubsec_debug)
		printf("merging %d records\n", npkts);
#endif /* UBSEC_DEBUG */

	q = SIMPLEQ_FIRST(&sc->sc_queue);
	SIMPLEQ_REMOVE_HEAD(&sc->sc_queue, q_next);
	--sc->sc_nqueue;

	bus_dmamap_sync(sc->sc_dmat, q->q_src_map, BUS_DMASYNC_PREWRITE);
	if (q->q_dst_map != NULL)
		bus_dmamap_sync(sc->sc_dmat, q->q_dst_map, BUS_DMASYNC_PREREAD);

	q->q_nstacked_mcrs = npkts - 1;		/* Number of packets stacked */

	for (i = 0; i < q->q_nstacked_mcrs; i++) {
		q2 = SIMPLEQ_FIRST(&sc->sc_queue);
		bus_dmamap_sync(sc->sc_dmat, q2->q_src_map,
		    BUS_DMASYNC_PREWRITE);
		if (q2->q_dst_map != NULL)
			bus_dmamap_sync(sc->sc_dmat, q2->q_dst_map,
			    BUS_DMASYNC_PREREAD);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_queue, q_next);
		--sc->sc_nqueue;

		v = (void*)(((char *)&q2->q_dma->d_dma->d_mcr) + sizeof(struct ubsec_mcr) -
		    sizeof(struct ubsec_mcr_add));
		bcopy(v, &q->q_dma->d_dma->d_mcradd[i], sizeof(struct ubsec_mcr_add));
		q->q_stacked_mcr[i] = q2;
	}
	q->q_dma->d_dma->d_mcr.mcr_pkts = htole16(npkts);
	SIMPLEQ_INSERT_TAIL(&sc->sc_qchip, q, q_next);
	sc->sc_nqchip += npkts;
	if (sc->sc_nqchip > ubsecstats.hst_maxqchip)
		ubsecstats.hst_maxqchip = sc->sc_nqchip;
	ubsec_dma_sync(&q->q_dma->d_alloc,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	WRITE_REG(sc, BS_MCR1, q->q_dma->d_alloc.dma_paddr +
	    offsetof(struct ubsec_dmachunk, d_mcr));
	return;
feed1:
	q = SIMPLEQ_FIRST(&sc->sc_queue);

	bus_dmamap_sync(sc->sc_dmat, q->q_src_map, BUS_DMASYNC_PREWRITE);
	if (q->q_dst_map != NULL)
		bus_dmamap_sync(sc->sc_dmat, q->q_dst_map, BUS_DMASYNC_PREREAD);
	ubsec_dma_sync(&q->q_dma->d_alloc,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	WRITE_REG(sc, BS_MCR1, q->q_dma->d_alloc.dma_paddr +
	    offsetof(struct ubsec_dmachunk, d_mcr));
#ifdef UBSEC_DEBUG
	if (ubsec_debug)
		printf("feed1: q->chip %p %08x stat %08x\n",
		      q, (u_int32_t)vtophys(&q->q_dma->d_dma->d_mcr),
		      stat);
#endif /* UBSEC_DEBUG */
	SIMPLEQ_REMOVE_HEAD(&sc->sc_queue, q_next);
	--sc->sc_nqueue;
	SIMPLEQ_INSERT_TAIL(&sc->sc_qchip, q, q_next);
	sc->sc_nqchip++;
	if (sc->sc_nqchip > ubsecstats.hst_maxqchip)
		ubsecstats.hst_maxqchip = sc->sc_nqchip;
	return;
}

static void
ubsec_setup_enckey(struct ubsec_session *ses, int algo, caddr_t key)
{

	/* Go ahead and compute key in ubsec's byte order */
	if (algo == CRYPTO_DES_CBC) {
		bcopy(key, &ses->ses_deskey[0], 8);
		bcopy(key, &ses->ses_deskey[2], 8);
		bcopy(key, &ses->ses_deskey[4], 8);
	} else
		bcopy(key, ses->ses_deskey, 24);

	SWAP32(ses->ses_deskey[0]);
	SWAP32(ses->ses_deskey[1]);
	SWAP32(ses->ses_deskey[2]);
	SWAP32(ses->ses_deskey[3]);
	SWAP32(ses->ses_deskey[4]);
	SWAP32(ses->ses_deskey[5]);
}

static void
ubsec_setup_mackey(struct ubsec_session *ses, int algo, caddr_t key, int klen)
{
	MD5_CTX md5ctx;
	SHA1_CTX sha1ctx;
	int i;

	for (i = 0; i < klen; i++)
		key[i] ^= HMAC_IPAD_VAL;

	if (algo == CRYPTO_MD5_HMAC) {
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, key, klen);
		MD5Update(&md5ctx, hmac_ipad_buffer, MD5_BLOCK_LEN - klen);
		bcopy(md5ctx.state, ses->ses_hminner, sizeof(md5ctx.state));
	} else {
		SHA1Init(&sha1ctx);
		SHA1Update(&sha1ctx, key, klen);
		SHA1Update(&sha1ctx, hmac_ipad_buffer,
		    SHA1_BLOCK_LEN - klen);
		bcopy(sha1ctx.h.b32, ses->ses_hminner, sizeof(sha1ctx.h.b32));
	}

	for (i = 0; i < klen; i++)
		key[i] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

	if (algo == CRYPTO_MD5_HMAC) {
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, key, klen);
		MD5Update(&md5ctx, hmac_opad_buffer, MD5_BLOCK_LEN - klen);
		bcopy(md5ctx.state, ses->ses_hmouter, sizeof(md5ctx.state));
	} else {
		SHA1Init(&sha1ctx);
		SHA1Update(&sha1ctx, key, klen);
		SHA1Update(&sha1ctx, hmac_opad_buffer,
		    SHA1_BLOCK_LEN - klen);
		bcopy(sha1ctx.h.b32, ses->ses_hmouter, sizeof(sha1ctx.h.b32));
	}

	for (i = 0; i < klen; i++)
		key[i] ^= HMAC_OPAD_VAL;
}

/*
 * Allocate a new 'session' and return an encoded session id.  'sidp'
 * contains our registration id, and should contain an encoded session
 * id on successful allocation.
 */
static int
ubsec_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct ubsec_softc *sc = device_get_softc(dev);
	struct cryptoini *c, *encini = NULL, *macini = NULL;
	struct ubsec_session *ses = NULL;

	if (cri == NULL || sc == NULL)
		return (EINVAL);

	for (c = cri; c != NULL; c = c->cri_next) {
		if (c->cri_alg == CRYPTO_MD5_HMAC ||
		    c->cri_alg == CRYPTO_SHA1_HMAC) {
			if (macini)
				return (EINVAL);
			macini = c;
		} else if (c->cri_alg == CRYPTO_DES_CBC ||
		    c->cri_alg == CRYPTO_3DES_CBC) {
			if (encini)
				return (EINVAL);
			encini = c;
		} else
			return (EINVAL);
	}
	if (encini == NULL && macini == NULL)
		return (EINVAL);

	ses = crypto_get_driver_session(cses);
	if (encini) {
		/* get an IV, network byte order */
		/* XXX may read fewer than requested */
		read_random(ses->ses_iv, sizeof(ses->ses_iv));

		if (encini->cri_key != NULL) {
			ubsec_setup_enckey(ses, encini->cri_alg,
			    encini->cri_key);
		}
	}

	if (macini) {
		ses->ses_mlen = macini->cri_mlen;
		if (ses->ses_mlen == 0) {
			if (macini->cri_alg == CRYPTO_MD5_HMAC)
				ses->ses_mlen = MD5_HASH_LEN;
			else
				ses->ses_mlen = SHA1_HASH_LEN;
		}

		if (macini->cri_key != NULL) {
			ubsec_setup_mackey(ses, macini->cri_alg,
			    macini->cri_key, macini->cri_klen / 8);
		}
	}

	return (0);
}

static void
ubsec_op_cb(void *arg, bus_dma_segment_t *seg, int nsegs, bus_size_t mapsize, int error)
{
	struct ubsec_operand *op = arg;

	KASSERT(nsegs <= UBS_MAX_SCATTER,
		("Too many DMA segments returned when mapping operand"));
#ifdef UBSEC_DEBUG
	if (ubsec_debug)
		printf("ubsec_op_cb: mapsize %u nsegs %d error %d\n",
			(u_int) mapsize, nsegs, error);
#endif
	if (error != 0)
		return;
	op->mapsize = mapsize;
	op->nsegs = nsegs;
	bcopy(seg, op->segs, nsegs * sizeof (seg[0]));
}

static int
ubsec_process(device_t dev, struct cryptop *crp, int hint)
{
	struct ubsec_softc *sc = device_get_softc(dev);
	struct ubsec_q *q = NULL;
	int err = 0, i, j, nicealign;
	struct cryptodesc *crd1, *crd2, *maccrd, *enccrd;
	int encoffset = 0, macoffset = 0, cpskip, cpoffset;
	int sskip, dskip, stheend, dtheend;
	int16_t coffset;
	struct ubsec_session *ses;
	struct ubsec_pktctx ctx;
	struct ubsec_dma *dmap = NULL;

	if (crp == NULL || crp->crp_callback == NULL || sc == NULL) {
		ubsecstats.hst_invalid++;
		return (EINVAL);
	}

	mtx_lock(&sc->sc_freeqlock);
	if (SIMPLEQ_EMPTY(&sc->sc_freequeue)) {
		ubsecstats.hst_queuefull++;
		sc->sc_needwakeup |= CRYPTO_SYMQ;
		mtx_unlock(&sc->sc_freeqlock);
		return (ERESTART);
	}
	q = SIMPLEQ_FIRST(&sc->sc_freequeue);
	SIMPLEQ_REMOVE_HEAD(&sc->sc_freequeue, q_next);
	mtx_unlock(&sc->sc_freeqlock);

	dmap = q->q_dma; /* Save dma pointer */
	bzero(q, sizeof(struct ubsec_q));
	bzero(&ctx, sizeof(ctx));

	q->q_dma = dmap;
	ses = crypto_get_driver_session(crp->crp_session);

	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		q->q_src_m = (struct mbuf *)crp->crp_buf;
		q->q_dst_m = (struct mbuf *)crp->crp_buf;
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		q->q_src_io = (struct uio *)crp->crp_buf;
		q->q_dst_io = (struct uio *)crp->crp_buf;
	} else {
		ubsecstats.hst_badflags++;
		err = EINVAL;
		goto errout;	/* XXX we don't handle contiguous blocks! */
	}

	bzero(&dmap->d_dma->d_mcr, sizeof(struct ubsec_mcr));

	dmap->d_dma->d_mcr.mcr_pkts = htole16(1);
	dmap->d_dma->d_mcr.mcr_flags = 0;
	q->q_crp = crp;

	crd1 = crp->crp_desc;
	if (crd1 == NULL) {
		ubsecstats.hst_nodesc++;
		err = EINVAL;
		goto errout;
	}
	crd2 = crd1->crd_next;

	if (crd2 == NULL) {
		if (crd1->crd_alg == CRYPTO_MD5_HMAC ||
		    crd1->crd_alg == CRYPTO_SHA1_HMAC) {
			maccrd = crd1;
			enccrd = NULL;
		} else if (crd1->crd_alg == CRYPTO_DES_CBC ||
		    crd1->crd_alg == CRYPTO_3DES_CBC) {
			maccrd = NULL;
			enccrd = crd1;
		} else {
			ubsecstats.hst_badalg++;
			err = EINVAL;
			goto errout;
		}
	} else {
		if ((crd1->crd_alg == CRYPTO_MD5_HMAC ||
		    crd1->crd_alg == CRYPTO_SHA1_HMAC) &&
		    (crd2->crd_alg == CRYPTO_DES_CBC ||
			crd2->crd_alg == CRYPTO_3DES_CBC) &&
		    ((crd2->crd_flags & CRD_F_ENCRYPT) == 0)) {
			maccrd = crd1;
			enccrd = crd2;
		} else if ((crd1->crd_alg == CRYPTO_DES_CBC ||
		    crd1->crd_alg == CRYPTO_3DES_CBC) &&
		    (crd2->crd_alg == CRYPTO_MD5_HMAC ||
			crd2->crd_alg == CRYPTO_SHA1_HMAC) &&
		    (crd1->crd_flags & CRD_F_ENCRYPT)) {
			enccrd = crd1;
			maccrd = crd2;
		} else {
			/*
			 * We cannot order the ubsec as requested
			 */
			ubsecstats.hst_badalg++;
			err = EINVAL;
			goto errout;
		}
	}

	if (enccrd) {
		if (enccrd->crd_flags & CRD_F_KEY_EXPLICIT) {
			ubsec_setup_enckey(ses, enccrd->crd_alg,
			    enccrd->crd_key);
		}

		encoffset = enccrd->crd_skip;
		ctx.pc_flags |= htole16(UBS_PKTCTX_ENC_3DES);

		if (enccrd->crd_flags & CRD_F_ENCRYPT) {
			q->q_flags |= UBSEC_QFLAGS_COPYOUTIV;

			if (enccrd->crd_flags & CRD_F_IV_EXPLICIT)
				bcopy(enccrd->crd_iv, ctx.pc_iv, 8);
			else {
				ctx.pc_iv[0] = ses->ses_iv[0];
				ctx.pc_iv[1] = ses->ses_iv[1];
			}

			if ((enccrd->crd_flags & CRD_F_IV_PRESENT) == 0) {
				crypto_copyback(crp->crp_flags, crp->crp_buf,
				    enccrd->crd_inject, 8, (caddr_t)ctx.pc_iv);
			}
		} else {
			ctx.pc_flags |= htole16(UBS_PKTCTX_INBOUND);

			if (enccrd->crd_flags & CRD_F_IV_EXPLICIT)
				bcopy(enccrd->crd_iv, ctx.pc_iv, 8);
			else {
				crypto_copydata(crp->crp_flags, crp->crp_buf,
				    enccrd->crd_inject, 8, (caddr_t)ctx.pc_iv);
			}
		}

		ctx.pc_deskey[0] = ses->ses_deskey[0];
		ctx.pc_deskey[1] = ses->ses_deskey[1];
		ctx.pc_deskey[2] = ses->ses_deskey[2];
		ctx.pc_deskey[3] = ses->ses_deskey[3];
		ctx.pc_deskey[4] = ses->ses_deskey[4];
		ctx.pc_deskey[5] = ses->ses_deskey[5];
		SWAP32(ctx.pc_iv[0]);
		SWAP32(ctx.pc_iv[1]);
	}

	if (maccrd) {
		if (maccrd->crd_flags & CRD_F_KEY_EXPLICIT) {
			ubsec_setup_mackey(ses, maccrd->crd_alg,
			    maccrd->crd_key, maccrd->crd_klen / 8);
		}

		macoffset = maccrd->crd_skip;

		if (maccrd->crd_alg == CRYPTO_MD5_HMAC)
			ctx.pc_flags |= htole16(UBS_PKTCTX_AUTH_MD5);
		else
			ctx.pc_flags |= htole16(UBS_PKTCTX_AUTH_SHA1);

		for (i = 0; i < 5; i++) {
			ctx.pc_hminner[i] = ses->ses_hminner[i];
			ctx.pc_hmouter[i] = ses->ses_hmouter[i];

			HTOLE32(ctx.pc_hminner[i]);
			HTOLE32(ctx.pc_hmouter[i]);
		}
	}

	if (enccrd && maccrd) {
		/*
		 * ubsec cannot handle packets where the end of encryption
		 * and authentication are not the same, or where the
		 * encrypted part begins before the authenticated part.
		 */
		if ((encoffset + enccrd->crd_len) !=
		    (macoffset + maccrd->crd_len)) {
			ubsecstats.hst_lenmismatch++;
			err = EINVAL;
			goto errout;
		}
		if (enccrd->crd_skip < maccrd->crd_skip) {
			ubsecstats.hst_skipmismatch++;
			err = EINVAL;
			goto errout;
		}
		sskip = maccrd->crd_skip;
		cpskip = dskip = enccrd->crd_skip;
		stheend = maccrd->crd_len;
		dtheend = enccrd->crd_len;
		coffset = enccrd->crd_skip - maccrd->crd_skip;
		cpoffset = cpskip + dtheend;
#ifdef UBSEC_DEBUG
		if (ubsec_debug) {
			printf("mac: skip %d, len %d, inject %d\n",
			    maccrd->crd_skip, maccrd->crd_len, maccrd->crd_inject);
			printf("enc: skip %d, len %d, inject %d\n",
			    enccrd->crd_skip, enccrd->crd_len, enccrd->crd_inject);
			printf("src: skip %d, len %d\n", sskip, stheend);
			printf("dst: skip %d, len %d\n", dskip, dtheend);
			printf("ubs: coffset %d, pktlen %d, cpskip %d, cpoffset %d\n",
			    coffset, stheend, cpskip, cpoffset);
		}
#endif
	} else {
		cpskip = dskip = sskip = macoffset + encoffset;
		dtheend = stheend = (enccrd)?enccrd->crd_len:maccrd->crd_len;
		cpoffset = cpskip + dtheend;
		coffset = 0;
	}
	ctx.pc_offset = htole16(coffset >> 2);

	if (bus_dmamap_create(sc->sc_dmat, BUS_DMA_NOWAIT, &q->q_src_map)) {
		ubsecstats.hst_nomap++;
		err = ENOMEM;
		goto errout;
	}
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		if (bus_dmamap_load_mbuf(sc->sc_dmat, q->q_src_map,
		    q->q_src_m, ubsec_op_cb, &q->q_src, BUS_DMA_NOWAIT) != 0) {
			bus_dmamap_destroy(sc->sc_dmat, q->q_src_map);
			q->q_src_map = NULL;
			ubsecstats.hst_noload++;
			err = ENOMEM;
			goto errout;
		}
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		if (bus_dmamap_load_uio(sc->sc_dmat, q->q_src_map,
		    q->q_src_io, ubsec_op_cb, &q->q_src, BUS_DMA_NOWAIT) != 0) {
			bus_dmamap_destroy(sc->sc_dmat, q->q_src_map);
			q->q_src_map = NULL;
			ubsecstats.hst_noload++;
			err = ENOMEM;
			goto errout;
		}
	}
	nicealign = ubsec_dmamap_aligned(&q->q_src);

	dmap->d_dma->d_mcr.mcr_pktlen = htole16(stheend);

#ifdef UBSEC_DEBUG
	if (ubsec_debug)
		printf("src skip: %d nicealign: %u\n", sskip, nicealign);
#endif
	for (i = j = 0; i < q->q_src_nsegs; i++) {
		struct ubsec_pktbuf *pb;
		bus_size_t packl = q->q_src_segs[i].ds_len;
		bus_addr_t packp = q->q_src_segs[i].ds_addr;

		if (sskip >= packl) {
			sskip -= packl;
			continue;
		}

		packl -= sskip;
		packp += sskip;
		sskip = 0;

		if (packl > 0xfffc) {
			err = EIO;
			goto errout;
		}

		if (j == 0)
			pb = &dmap->d_dma->d_mcr.mcr_ipktbuf;
		else
			pb = &dmap->d_dma->d_sbuf[j - 1];

		pb->pb_addr = htole32(packp);

		if (stheend) {
			if (packl > stheend) {
				pb->pb_len = htole32(stheend);
				stheend = 0;
			} else {
				pb->pb_len = htole32(packl);
				stheend -= packl;
			}
		} else
			pb->pb_len = htole32(packl);

		if ((i + 1) == q->q_src_nsegs)
			pb->pb_next = 0;
		else
			pb->pb_next = htole32(dmap->d_alloc.dma_paddr +
			    offsetof(struct ubsec_dmachunk, d_sbuf[j]));
		j++;
	}

	if (enccrd == NULL && maccrd != NULL) {
		dmap->d_dma->d_mcr.mcr_opktbuf.pb_addr = 0;
		dmap->d_dma->d_mcr.mcr_opktbuf.pb_len = 0;
		dmap->d_dma->d_mcr.mcr_opktbuf.pb_next = htole32(dmap->d_alloc.dma_paddr +
		    offsetof(struct ubsec_dmachunk, d_macbuf[0]));
#ifdef UBSEC_DEBUG
		if (ubsec_debug)
			printf("opkt: %x %x %x\n",
			    dmap->d_dma->d_mcr.mcr_opktbuf.pb_addr,
			    dmap->d_dma->d_mcr.mcr_opktbuf.pb_len,
			    dmap->d_dma->d_mcr.mcr_opktbuf.pb_next);
#endif
	} else {
		if (crp->crp_flags & CRYPTO_F_IOV) {
			if (!nicealign) {
				ubsecstats.hst_iovmisaligned++;
				err = EINVAL;
				goto errout;
			}
			if (bus_dmamap_create(sc->sc_dmat, BUS_DMA_NOWAIT,
			     &q->q_dst_map)) {
				ubsecstats.hst_nomap++;
				err = ENOMEM;
				goto errout;
			}
			if (bus_dmamap_load_uio(sc->sc_dmat, q->q_dst_map,
			    q->q_dst_io, ubsec_op_cb, &q->q_dst, BUS_DMA_NOWAIT) != 0) {
				bus_dmamap_destroy(sc->sc_dmat, q->q_dst_map);
				q->q_dst_map = NULL;
				ubsecstats.hst_noload++;
				err = ENOMEM;
				goto errout;
			}
		} else if (crp->crp_flags & CRYPTO_F_IMBUF) {
			if (nicealign) {
				q->q_dst = q->q_src;
			} else {
				int totlen, len;
				struct mbuf *m, *top, **mp;

				ubsecstats.hst_unaligned++;
				totlen = q->q_src_mapsize;
				if (totlen >= MINCLSIZE) {
					m = m_getcl(M_NOWAIT, MT_DATA,
					    q->q_src_m->m_flags & M_PKTHDR);
					len = MCLBYTES;
				} else if (q->q_src_m->m_flags & M_PKTHDR) {
					m = m_gethdr(M_NOWAIT, MT_DATA);
					len = MHLEN;
				} else {
					m = m_get(M_NOWAIT, MT_DATA);
					len = MLEN;
				}
				if (m && q->q_src_m->m_flags & M_PKTHDR &&
				    !m_dup_pkthdr(m, q->q_src_m, M_NOWAIT)) {
					m_free(m);
					m = NULL;
				}
				if (m == NULL) {
					ubsecstats.hst_nombuf++;
					err = sc->sc_nqueue ? ERESTART : ENOMEM;
					goto errout;
				}
				m->m_len = len = min(totlen, len);
				totlen -= len;
				top = m;
				mp = &top;

				while (totlen > 0) {
					if (totlen >= MINCLSIZE) {
						m = m_getcl(M_NOWAIT,
						    MT_DATA, 0);
						len = MCLBYTES;
					} else {
						m = m_get(M_NOWAIT, MT_DATA);
						len = MLEN;
					}
					if (m == NULL) {
						m_freem(top);
						ubsecstats.hst_nombuf++;
						err = sc->sc_nqueue ? ERESTART : ENOMEM;
						goto errout;
					}
					m->m_len = len = min(totlen, len);
					totlen -= len;
					*mp = m;
					mp = &m->m_next;
				}
				q->q_dst_m = top;
				ubsec_mcopy(q->q_src_m, q->q_dst_m,
				    cpskip, cpoffset);
				if (bus_dmamap_create(sc->sc_dmat,
				    BUS_DMA_NOWAIT, &q->q_dst_map) != 0) {
					ubsecstats.hst_nomap++;
					err = ENOMEM;
					goto errout;
				}
				if (bus_dmamap_load_mbuf(sc->sc_dmat,
				    q->q_dst_map, q->q_dst_m,
				    ubsec_op_cb, &q->q_dst,
				    BUS_DMA_NOWAIT) != 0) {
					bus_dmamap_destroy(sc->sc_dmat,
					q->q_dst_map);
					q->q_dst_map = NULL;
					ubsecstats.hst_noload++;
					err = ENOMEM;
					goto errout;
				}
			}
		} else {
			ubsecstats.hst_badflags++;
			err = EINVAL;
			goto errout;
		}

#ifdef UBSEC_DEBUG
		if (ubsec_debug)
			printf("dst skip: %d\n", dskip);
#endif
		for (i = j = 0; i < q->q_dst_nsegs; i++) {
			struct ubsec_pktbuf *pb;
			bus_size_t packl = q->q_dst_segs[i].ds_len;
			bus_addr_t packp = q->q_dst_segs[i].ds_addr;

			if (dskip >= packl) {
				dskip -= packl;
				continue;
			}

			packl -= dskip;
			packp += dskip;
			dskip = 0;

			if (packl > 0xfffc) {
				err = EIO;
				goto errout;
			}

			if (j == 0)
				pb = &dmap->d_dma->d_mcr.mcr_opktbuf;
			else
				pb = &dmap->d_dma->d_dbuf[j - 1];

			pb->pb_addr = htole32(packp);

			if (dtheend) {
				if (packl > dtheend) {
					pb->pb_len = htole32(dtheend);
					dtheend = 0;
				} else {
					pb->pb_len = htole32(packl);
					dtheend -= packl;
				}
			} else
				pb->pb_len = htole32(packl);

			if ((i + 1) == q->q_dst_nsegs) {
				if (maccrd)
					pb->pb_next = htole32(dmap->d_alloc.dma_paddr +
					    offsetof(struct ubsec_dmachunk, d_macbuf[0]));
				else
					pb->pb_next = 0;
			} else
				pb->pb_next = htole32(dmap->d_alloc.dma_paddr +
				    offsetof(struct ubsec_dmachunk, d_dbuf[j]));
			j++;
		}
	}

	dmap->d_dma->d_mcr.mcr_cmdctxp = htole32(dmap->d_alloc.dma_paddr +
	    offsetof(struct ubsec_dmachunk, d_ctx));

	if (sc->sc_flags & UBS_FLAGS_LONGCTX) {
		struct ubsec_pktctx_long *ctxl;

		ctxl = (struct ubsec_pktctx_long *)(dmap->d_alloc.dma_vaddr +
		    offsetof(struct ubsec_dmachunk, d_ctx));

		/* transform small context into long context */
		ctxl->pc_len = htole16(sizeof(struct ubsec_pktctx_long));
		ctxl->pc_type = htole16(UBS_PKTCTX_TYPE_IPSEC);
		ctxl->pc_flags = ctx.pc_flags;
		ctxl->pc_offset = ctx.pc_offset;
		for (i = 0; i < 6; i++)
			ctxl->pc_deskey[i] = ctx.pc_deskey[i];
		for (i = 0; i < 5; i++)
			ctxl->pc_hminner[i] = ctx.pc_hminner[i];
		for (i = 0; i < 5; i++)
			ctxl->pc_hmouter[i] = ctx.pc_hmouter[i];
		ctxl->pc_iv[0] = ctx.pc_iv[0];
		ctxl->pc_iv[1] = ctx.pc_iv[1];
	} else
		bcopy(&ctx, dmap->d_alloc.dma_vaddr +
		    offsetof(struct ubsec_dmachunk, d_ctx),
		    sizeof(struct ubsec_pktctx));

	mtx_lock(&sc->sc_mcr1lock);
	SIMPLEQ_INSERT_TAIL(&sc->sc_queue, q, q_next);
	sc->sc_nqueue++;
	ubsecstats.hst_ipackets++;
	ubsecstats.hst_ibytes += dmap->d_alloc.dma_size;
	if ((hint & CRYPTO_HINT_MORE) == 0 || sc->sc_nqueue >= UBS_MAX_AGGR)
		ubsec_feed(sc);
	mtx_unlock(&sc->sc_mcr1lock);
	return (0);

errout:
	if (q != NULL) {
		if ((q->q_dst_m != NULL) && (q->q_src_m != q->q_dst_m))
			m_freem(q->q_dst_m);

		if (q->q_dst_map != NULL && q->q_dst_map != q->q_src_map) {
			bus_dmamap_unload(sc->sc_dmat, q->q_dst_map);
			bus_dmamap_destroy(sc->sc_dmat, q->q_dst_map);
		}
		if (q->q_src_map != NULL) {
			bus_dmamap_unload(sc->sc_dmat, q->q_src_map);
			bus_dmamap_destroy(sc->sc_dmat, q->q_src_map);
		}
	}
	if (q != NULL || err == ERESTART) {
		mtx_lock(&sc->sc_freeqlock);
		if (q != NULL)
			SIMPLEQ_INSERT_TAIL(&sc->sc_freequeue, q, q_next);
		if (err == ERESTART)
			sc->sc_needwakeup |= CRYPTO_SYMQ;
		mtx_unlock(&sc->sc_freeqlock);
	}
	if (err != ERESTART) {
		crp->crp_etype = err;
		crypto_done(crp);
	}
	return (err);
}

static void
ubsec_callback(struct ubsec_softc *sc, struct ubsec_q *q)
{
	struct cryptop *crp = (struct cryptop *)q->q_crp;
	struct ubsec_session *ses;
	struct cryptodesc *crd;
	struct ubsec_dma *dmap = q->q_dma;

	ses = crypto_get_driver_session(crp->crp_session);

	ubsecstats.hst_opackets++;
	ubsecstats.hst_obytes += dmap->d_alloc.dma_size;

	ubsec_dma_sync(&dmap->d_alloc,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	if (q->q_dst_map != NULL && q->q_dst_map != q->q_src_map) {
		bus_dmamap_sync(sc->sc_dmat, q->q_dst_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, q->q_dst_map);
		bus_dmamap_destroy(sc->sc_dmat, q->q_dst_map);
	}
	bus_dmamap_sync(sc->sc_dmat, q->q_src_map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, q->q_src_map);
	bus_dmamap_destroy(sc->sc_dmat, q->q_src_map);

	if ((crp->crp_flags & CRYPTO_F_IMBUF) && (q->q_src_m != q->q_dst_m)) {
		m_freem(q->q_src_m);
		crp->crp_buf = (caddr_t)q->q_dst_m;
	}

	/* copy out IV for future use */
	if (q->q_flags & UBSEC_QFLAGS_COPYOUTIV) {
		for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
			if (crd->crd_alg != CRYPTO_DES_CBC &&
			    crd->crd_alg != CRYPTO_3DES_CBC)
				continue;
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crd->crd_skip + crd->crd_len - 8, 8,
			    (caddr_t)ses->ses_iv);
			break;
		}
	}

	for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
		if (crd->crd_alg != CRYPTO_MD5_HMAC &&
		    crd->crd_alg != CRYPTO_SHA1_HMAC)
			continue;
		crypto_copyback(crp->crp_flags, crp->crp_buf, crd->crd_inject,
		    ses->ses_mlen, (caddr_t)dmap->d_dma->d_macbuf);
		break;
	}
	mtx_lock(&sc->sc_freeqlock);
	SIMPLEQ_INSERT_TAIL(&sc->sc_freequeue, q, q_next);
	mtx_unlock(&sc->sc_freeqlock);
	crypto_done(crp);
}

static void
ubsec_mcopy(struct mbuf *srcm, struct mbuf *dstm, int hoffset, int toffset)
{
	int i, j, dlen, slen;
	caddr_t dptr, sptr;

	j = 0;
	sptr = srcm->m_data;
	slen = srcm->m_len;
	dptr = dstm->m_data;
	dlen = dstm->m_len;

	while (1) {
		for (i = 0; i < min(slen, dlen); i++) {
			if (j < hoffset || j >= toffset)
				*dptr++ = *sptr++;
			slen--;
			dlen--;
			j++;
		}
		if (slen == 0) {
			srcm = srcm->m_next;
			if (srcm == NULL)
				return;
			sptr = srcm->m_data;
			slen = srcm->m_len;
		}
		if (dlen == 0) {
			dstm = dstm->m_next;
			if (dstm == NULL)
				return;
			dptr = dstm->m_data;
			dlen = dstm->m_len;
		}
	}
}

/*
 * feed the key generator, must be called at splimp() or higher.
 */
static int
ubsec_feed2(struct ubsec_softc *sc)
{
	struct ubsec_q2 *q;

	while (!SIMPLEQ_EMPTY(&sc->sc_queue2)) {
		if (READ_REG(sc, BS_STAT) & BS_STAT_MCR2_FULL)
			break;
		q = SIMPLEQ_FIRST(&sc->sc_queue2);

		ubsec_dma_sync(&q->q_mcr,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		ubsec_dma_sync(&q->q_ctx, BUS_DMASYNC_PREWRITE);

		WRITE_REG(sc, BS_MCR2, q->q_mcr.dma_paddr);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_queue2, q_next);
		--sc->sc_nqueue2;
		SIMPLEQ_INSERT_TAIL(&sc->sc_qchip2, q, q_next);
	}
	return (0);
}

/*
 * Callback for handling random numbers
 */
static void
ubsec_callback2(struct ubsec_softc *sc, struct ubsec_q2 *q)
{
	struct cryptkop *krp;
	struct ubsec_ctx_keyop *ctx;

	ctx = (struct ubsec_ctx_keyop *)q->q_ctx.dma_vaddr;
	ubsec_dma_sync(&q->q_ctx, BUS_DMASYNC_POSTWRITE);

	switch (q->q_type) {
#ifndef UBSEC_NO_RNG
	case UBS_CTXOP_RNGBYPASS: {
		struct ubsec_q2_rng *rng = (struct ubsec_q2_rng *)q;

		ubsec_dma_sync(&rng->rng_buf, BUS_DMASYNC_POSTREAD);
		(*sc->sc_harvest)(sc->sc_rndtest,
			rng->rng_buf.dma_vaddr,
			UBSEC_RNG_BUFSIZ*sizeof (u_int32_t));
		rng->rng_used = 0;
		callout_reset(&sc->sc_rngto, sc->sc_rnghz, ubsec_rng, sc);
		break;
	}
#endif
	case UBS_CTXOP_MODEXP: {
		struct ubsec_q2_modexp *me = (struct ubsec_q2_modexp *)q;
		u_int rlen, clen;

		krp = me->me_krp;
		rlen = (me->me_modbits + 7) / 8;
		clen = (krp->krp_param[krp->krp_iparams].crp_nbits + 7) / 8;

		ubsec_dma_sync(&me->me_M, BUS_DMASYNC_POSTWRITE);
		ubsec_dma_sync(&me->me_E, BUS_DMASYNC_POSTWRITE);
		ubsec_dma_sync(&me->me_C, BUS_DMASYNC_POSTREAD);
		ubsec_dma_sync(&me->me_epb, BUS_DMASYNC_POSTWRITE);

		if (clen < rlen)
			krp->krp_status = E2BIG;
		else {
			if (sc->sc_flags & UBS_FLAGS_HWNORM) {
				bzero(krp->krp_param[krp->krp_iparams].crp_p,
				    (krp->krp_param[krp->krp_iparams].crp_nbits
					+ 7) / 8);
				bcopy(me->me_C.dma_vaddr,
				    krp->krp_param[krp->krp_iparams].crp_p,
				    (me->me_modbits + 7) / 8);
			} else
				ubsec_kshift_l(me->me_shiftbits,
				    me->me_C.dma_vaddr, me->me_normbits,
				    krp->krp_param[krp->krp_iparams].crp_p,
				    krp->krp_param[krp->krp_iparams].crp_nbits);
		}

		crypto_kdone(krp);

		/* bzero all potentially sensitive data */
		bzero(me->me_E.dma_vaddr, me->me_E.dma_size);
		bzero(me->me_M.dma_vaddr, me->me_M.dma_size);
		bzero(me->me_C.dma_vaddr, me->me_C.dma_size);
		bzero(me->me_q.q_ctx.dma_vaddr, me->me_q.q_ctx.dma_size);

		/* Can't free here, so put us on the free list. */
		SIMPLEQ_INSERT_TAIL(&sc->sc_q2free, &me->me_q, q_next);
		break;
	}
	case UBS_CTXOP_RSAPRIV: {
		struct ubsec_q2_rsapriv *rp = (struct ubsec_q2_rsapriv *)q;
		u_int len;

		krp = rp->rpr_krp;
		ubsec_dma_sync(&rp->rpr_msgin, BUS_DMASYNC_POSTWRITE);
		ubsec_dma_sync(&rp->rpr_msgout, BUS_DMASYNC_POSTREAD);

		len = (krp->krp_param[UBS_RSAPRIV_PAR_MSGOUT].crp_nbits + 7) / 8;
		bcopy(rp->rpr_msgout.dma_vaddr,
		    krp->krp_param[UBS_RSAPRIV_PAR_MSGOUT].crp_p, len);

		crypto_kdone(krp);

		bzero(rp->rpr_msgin.dma_vaddr, rp->rpr_msgin.dma_size);
		bzero(rp->rpr_msgout.dma_vaddr, rp->rpr_msgout.dma_size);
		bzero(rp->rpr_q.q_ctx.dma_vaddr, rp->rpr_q.q_ctx.dma_size);

		/* Can't free here, so put us on the free list. */
		SIMPLEQ_INSERT_TAIL(&sc->sc_q2free, &rp->rpr_q, q_next);
		break;
	}
	default:
		device_printf(sc->sc_dev, "unknown ctx op: %x\n",
		    letoh16(ctx->ctx_op));
		break;
	}
}

#ifndef UBSEC_NO_RNG
static void
ubsec_rng(void *vsc)
{
	struct ubsec_softc *sc = vsc;
	struct ubsec_q2_rng *rng = &sc->sc_rng;
	struct ubsec_mcr *mcr;
	struct ubsec_ctx_rngbypass *ctx;

	mtx_lock(&sc->sc_mcr2lock);
	if (rng->rng_used) {
		mtx_unlock(&sc->sc_mcr2lock);
		return;
	}
	sc->sc_nqueue2++;
	if (sc->sc_nqueue2 >= UBS_MAX_NQUEUE)
		goto out;

	mcr = (struct ubsec_mcr *)rng->rng_q.q_mcr.dma_vaddr;
	ctx = (struct ubsec_ctx_rngbypass *)rng->rng_q.q_ctx.dma_vaddr;

	mcr->mcr_pkts = htole16(1);
	mcr->mcr_flags = 0;
	mcr->mcr_cmdctxp = htole32(rng->rng_q.q_ctx.dma_paddr);
	mcr->mcr_ipktbuf.pb_addr = mcr->mcr_ipktbuf.pb_next = 0;
	mcr->mcr_ipktbuf.pb_len = 0;
	mcr->mcr_reserved = mcr->mcr_pktlen = 0;
	mcr->mcr_opktbuf.pb_addr = htole32(rng->rng_buf.dma_paddr);
	mcr->mcr_opktbuf.pb_len = htole32(((sizeof(u_int32_t) * UBSEC_RNG_BUFSIZ)) &
	    UBS_PKTBUF_LEN);
	mcr->mcr_opktbuf.pb_next = 0;

	ctx->rbp_len = htole16(sizeof(struct ubsec_ctx_rngbypass));
	ctx->rbp_op = htole16(UBS_CTXOP_RNGBYPASS);
	rng->rng_q.q_type = UBS_CTXOP_RNGBYPASS;

	ubsec_dma_sync(&rng->rng_buf, BUS_DMASYNC_PREREAD);

	SIMPLEQ_INSERT_TAIL(&sc->sc_queue2, &rng->rng_q, q_next);
	rng->rng_used = 1;
	ubsec_feed2(sc);
	ubsecstats.hst_rng++;
	mtx_unlock(&sc->sc_mcr2lock);

	return;

out:
	/*
	 * Something weird happened, generate our own call back.
	 */
	sc->sc_nqueue2--;
	mtx_unlock(&sc->sc_mcr2lock);
	callout_reset(&sc->sc_rngto, sc->sc_rnghz, ubsec_rng, sc);
}
#endif /* UBSEC_NO_RNG */

static void
ubsec_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;
	*paddr = segs->ds_addr;
}

static int
ubsec_dma_malloc(
	struct ubsec_softc *sc,
	bus_size_t size,
	struct ubsec_dma_alloc *dma,
	int mapflags
)
{
	int r;

	/* XXX could specify sc_dmat as parent but that just adds overhead */
	r = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),	/* parent */
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       size,			/* maxsize */
			       1,			/* nsegments */
			       size,			/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL, NULL,		/* lockfunc, lockarg */
			       &dma->dma_tag);
	if (r != 0) {
		device_printf(sc->sc_dev, "ubsec_dma_malloc: "
			"bus_dma_tag_create failed; error %u\n", r);
		goto fail_1;
	}

	r = bus_dmamem_alloc(dma->dma_tag, (void**) &dma->dma_vaddr,
			     BUS_DMA_NOWAIT, &dma->dma_map);
	if (r != 0) {
		device_printf(sc->sc_dev, "ubsec_dma_malloc: "
			"bus_dmammem_alloc failed; size %ju, error %u\n",
			(intmax_t)size, r);
		goto fail_2;
	}

	r = bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr,
		            size,
			    ubsec_dmamap_cb,
			    &dma->dma_paddr,
			    mapflags | BUS_DMA_NOWAIT);
	if (r != 0) {
		device_printf(sc->sc_dev, "ubsec_dma_malloc: "
			"bus_dmamap_load failed; error %u\n", r);
		goto fail_3;
	}

	dma->dma_size = size;
	return (0);

fail_3:
	bus_dmamap_unload(dma->dma_tag, dma->dma_map);
fail_2:
	bus_dmamem_free(dma->dma_tag, dma->dma_vaddr, dma->dma_map);
fail_1:
	bus_dma_tag_destroy(dma->dma_tag);
	dma->dma_tag = NULL;
	return (r);
}

static void
ubsec_dma_free(struct ubsec_softc *sc, struct ubsec_dma_alloc *dma)
{
	bus_dmamap_unload(dma->dma_tag, dma->dma_map);
	bus_dmamem_free(dma->dma_tag, dma->dma_vaddr, dma->dma_map);
	bus_dma_tag_destroy(dma->dma_tag);
}

/*
 * Resets the board.  Values in the regesters are left as is
 * from the reset (i.e. initial values are assigned elsewhere).
 */
static void
ubsec_reset_board(struct ubsec_softc *sc)
{
    volatile u_int32_t ctrl;

    ctrl = READ_REG(sc, BS_CTRL);
    ctrl |= BS_CTRL_RESET;
    WRITE_REG(sc, BS_CTRL, ctrl);

    /*
     * Wait aprox. 30 PCI clocks = 900 ns = 0.9 us
     */
    DELAY(10);
}

/*
 * Init Broadcom registers
 */
static void
ubsec_init_board(struct ubsec_softc *sc)
{
	u_int32_t ctrl;

	ctrl = READ_REG(sc, BS_CTRL);
	ctrl &= ~(BS_CTRL_BE32 | BS_CTRL_BE64);
	ctrl |= BS_CTRL_LITTLE_ENDIAN | BS_CTRL_MCR1INT;

	if (sc->sc_flags & (UBS_FLAGS_KEY|UBS_FLAGS_RNG))
		ctrl |= BS_CTRL_MCR2INT;
	else
		ctrl &= ~BS_CTRL_MCR2INT;

	if (sc->sc_flags & UBS_FLAGS_HWNORM)
		ctrl &= ~BS_CTRL_SWNORM;

	WRITE_REG(sc, BS_CTRL, ctrl);
}

/*
 * Init Broadcom PCI registers
 */
static void
ubsec_init_pciregs(device_t dev)
{
#if 0
	u_int32_t misc;

	misc = pci_conf_read(pc, pa->pa_tag, BS_RTY_TOUT);
	misc = (misc & ~(UBS_PCI_RTY_MASK << UBS_PCI_RTY_SHIFT))
	    | ((UBS_DEF_RTY & 0xff) << UBS_PCI_RTY_SHIFT);
	misc = (misc & ~(UBS_PCI_TOUT_MASK << UBS_PCI_TOUT_SHIFT))
	    | ((UBS_DEF_TOUT & 0xff) << UBS_PCI_TOUT_SHIFT);
	pci_conf_write(pc, pa->pa_tag, BS_RTY_TOUT, misc);
#endif

	/*
	 * This will set the cache line size to 1, this will
	 * force the BCM58xx chip just to do burst read/writes.
	 * Cache line read/writes are to slow
	 */
	pci_write_config(dev, PCIR_CACHELNSZ, UBS_DEF_CACHELINE, 1);
}

/*
 * Clean up after a chip crash.
 * It is assumed that the caller in splimp()
 */
static void
ubsec_cleanchip(struct ubsec_softc *sc)
{
	struct ubsec_q *q;

	while (!SIMPLEQ_EMPTY(&sc->sc_qchip)) {
		q = SIMPLEQ_FIRST(&sc->sc_qchip);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_qchip, q_next);
		ubsec_free_q(sc, q);
	}
	sc->sc_nqchip = 0;
}

/*
 * free a ubsec_q
 * It is assumed that the caller is within splimp().
 */
static int
ubsec_free_q(struct ubsec_softc *sc, struct ubsec_q *q)
{
	struct ubsec_q *q2;
	struct cryptop *crp;
	int npkts;
	int i;

	npkts = q->q_nstacked_mcrs;

	for (i = 0; i < npkts; i++) {
		if(q->q_stacked_mcr[i]) {
			q2 = q->q_stacked_mcr[i];

			if ((q2->q_dst_m != NULL) && (q2->q_src_m != q2->q_dst_m))
				m_freem(q2->q_dst_m);

			crp = (struct cryptop *)q2->q_crp;

			SIMPLEQ_INSERT_TAIL(&sc->sc_freequeue, q2, q_next);

			crp->crp_etype = EFAULT;
			crypto_done(crp);
		} else {
			break;
		}
	}

	/*
	 * Free header MCR
	 */
	if ((q->q_dst_m != NULL) && (q->q_src_m != q->q_dst_m))
		m_freem(q->q_dst_m);

	crp = (struct cryptop *)q->q_crp;

	SIMPLEQ_INSERT_TAIL(&sc->sc_freequeue, q, q_next);

	crp->crp_etype = EFAULT;
	crypto_done(crp);
	return(0);
}

/*
 * Routine to reset the chip and clean up.
 * It is assumed that the caller is in splimp()
 */
static void
ubsec_totalreset(struct ubsec_softc *sc)
{
	ubsec_reset_board(sc);
	ubsec_init_board(sc);
	ubsec_cleanchip(sc);
}

static int
ubsec_dmamap_aligned(struct ubsec_operand *op)
{
	int i;

	for (i = 0; i < op->nsegs; i++) {
		if (op->segs[i].ds_addr & 3)
			return (0);
		if ((i != (op->nsegs - 1)) &&
		    (op->segs[i].ds_len & 3))
			return (0);
	}
	return (1);
}

static void
ubsec_kfree(struct ubsec_softc *sc, struct ubsec_q2 *q)
{
	switch (q->q_type) {
	case UBS_CTXOP_MODEXP: {
		struct ubsec_q2_modexp *me = (struct ubsec_q2_modexp *)q;

		ubsec_dma_free(sc, &me->me_q.q_mcr);
		ubsec_dma_free(sc, &me->me_q.q_ctx);
		ubsec_dma_free(sc, &me->me_M);
		ubsec_dma_free(sc, &me->me_E);
		ubsec_dma_free(sc, &me->me_C);
		ubsec_dma_free(sc, &me->me_epb);
		free(me, M_DEVBUF);
		break;
	}
	case UBS_CTXOP_RSAPRIV: {
		struct ubsec_q2_rsapriv *rp = (struct ubsec_q2_rsapriv *)q;

		ubsec_dma_free(sc, &rp->rpr_q.q_mcr);
		ubsec_dma_free(sc, &rp->rpr_q.q_ctx);
		ubsec_dma_free(sc, &rp->rpr_msgin);
		ubsec_dma_free(sc, &rp->rpr_msgout);
		free(rp, M_DEVBUF);
		break;
	}
	default:
		device_printf(sc->sc_dev, "invalid kfree 0x%x\n", q->q_type);
		break;
	}
}

static int
ubsec_kprocess(device_t dev, struct cryptkop *krp, int hint)
{
	struct ubsec_softc *sc = device_get_softc(dev);
	int r;

	if (krp == NULL || krp->krp_callback == NULL)
		return (EINVAL);

	while (!SIMPLEQ_EMPTY(&sc->sc_q2free)) {
		struct ubsec_q2 *q;

		q = SIMPLEQ_FIRST(&sc->sc_q2free);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_q2free, q_next);
		ubsec_kfree(sc, q);
	}

	switch (krp->krp_op) {
	case CRK_MOD_EXP:
		if (sc->sc_flags & UBS_FLAGS_HWNORM)
			r = ubsec_kprocess_modexp_hw(sc, krp, hint);
		else
			r = ubsec_kprocess_modexp_sw(sc, krp, hint);
		break;
	case CRK_MOD_EXP_CRT:
		return (ubsec_kprocess_rsapriv(sc, krp, hint));
	default:
		device_printf(sc->sc_dev, "kprocess: invalid op 0x%x\n",
		    krp->krp_op);
		krp->krp_status = EOPNOTSUPP;
		crypto_kdone(krp);
		return (0);
	}
	return (0);			/* silence compiler */
}

/*
 * Start computation of cr[C] = (cr[M] ^ cr[E]) mod cr[N] (sw normalization)
 */
static int
ubsec_kprocess_modexp_sw(struct ubsec_softc *sc, struct cryptkop *krp, int hint)
{
	struct ubsec_q2_modexp *me;
	struct ubsec_mcr *mcr;
	struct ubsec_ctx_modexp *ctx;
	struct ubsec_pktbuf *epb;
	int err = 0;
	u_int nbits, normbits, mbits, shiftbits, ebits;

	me = (struct ubsec_q2_modexp *)malloc(sizeof *me, M_DEVBUF, M_NOWAIT);
	if (me == NULL) {
		err = ENOMEM;
		goto errout;
	}
	bzero(me, sizeof *me);
	me->me_krp = krp;
	me->me_q.q_type = UBS_CTXOP_MODEXP;

	nbits = ubsec_ksigbits(&krp->krp_param[UBS_MODEXP_PAR_N]);
	if (nbits <= 512)
		normbits = 512;
	else if (nbits <= 768)
		normbits = 768;
	else if (nbits <= 1024)
		normbits = 1024;
	else if (sc->sc_flags & UBS_FLAGS_BIGKEY && nbits <= 1536)
		normbits = 1536;
	else if (sc->sc_flags & UBS_FLAGS_BIGKEY && nbits <= 2048)
		normbits = 2048;
	else {
		err = E2BIG;
		goto errout;
	}

	shiftbits = normbits - nbits;

	me->me_modbits = nbits;
	me->me_shiftbits = shiftbits;
	me->me_normbits = normbits;

	/* Sanity check: result bits must be >= true modulus bits. */
	if (krp->krp_param[krp->krp_iparams].crp_nbits < nbits) {
		err = ERANGE;
		goto errout;
	}

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_mcr),
	    &me->me_q.q_mcr, 0)) {
		err = ENOMEM;
		goto errout;
	}
	mcr = (struct ubsec_mcr *)me->me_q.q_mcr.dma_vaddr;

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_ctx_modexp),
	    &me->me_q.q_ctx, 0)) {
		err = ENOMEM;
		goto errout;
	}

	mbits = ubsec_ksigbits(&krp->krp_param[UBS_MODEXP_PAR_M]);
	if (mbits > nbits) {
		err = E2BIG;
		goto errout;
	}
	if (ubsec_dma_malloc(sc, normbits / 8, &me->me_M, 0)) {
		err = ENOMEM;
		goto errout;
	}
	ubsec_kshift_r(shiftbits,
	    krp->krp_param[UBS_MODEXP_PAR_M].crp_p, mbits,
	    me->me_M.dma_vaddr, normbits);

	if (ubsec_dma_malloc(sc, normbits / 8, &me->me_C, 0)) {
		err = ENOMEM;
		goto errout;
	}
	bzero(me->me_C.dma_vaddr, me->me_C.dma_size);

	ebits = ubsec_ksigbits(&krp->krp_param[UBS_MODEXP_PAR_E]);
	if (ebits > nbits) {
		err = E2BIG;
		goto errout;
	}
	if (ubsec_dma_malloc(sc, normbits / 8, &me->me_E, 0)) {
		err = ENOMEM;
		goto errout;
	}
	ubsec_kshift_r(shiftbits,
	    krp->krp_param[UBS_MODEXP_PAR_E].crp_p, ebits,
	    me->me_E.dma_vaddr, normbits);

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_pktbuf),
	    &me->me_epb, 0)) {
		err = ENOMEM;
		goto errout;
	}
	epb = (struct ubsec_pktbuf *)me->me_epb.dma_vaddr;
	epb->pb_addr = htole32(me->me_E.dma_paddr);
	epb->pb_next = 0;
	epb->pb_len = htole32(normbits / 8);

#ifdef UBSEC_DEBUG
	if (ubsec_debug) {
		printf("Epb ");
		ubsec_dump_pb(epb);
	}
#endif

	mcr->mcr_pkts = htole16(1);
	mcr->mcr_flags = 0;
	mcr->mcr_cmdctxp = htole32(me->me_q.q_ctx.dma_paddr);
	mcr->mcr_reserved = 0;
	mcr->mcr_pktlen = 0;

	mcr->mcr_ipktbuf.pb_addr = htole32(me->me_M.dma_paddr);
	mcr->mcr_ipktbuf.pb_len = htole32(normbits / 8);
	mcr->mcr_ipktbuf.pb_next = htole32(me->me_epb.dma_paddr);

	mcr->mcr_opktbuf.pb_addr = htole32(me->me_C.dma_paddr);
	mcr->mcr_opktbuf.pb_next = 0;
	mcr->mcr_opktbuf.pb_len = htole32(normbits / 8);

#ifdef DIAGNOSTIC
	/* Misaligned output buffer will hang the chip. */
	if ((letoh32(mcr->mcr_opktbuf.pb_addr) & 3) != 0)
		panic("%s: modexp invalid addr 0x%x\n",
		    device_get_nameunit(sc->sc_dev),
		    letoh32(mcr->mcr_opktbuf.pb_addr));
	if ((letoh32(mcr->mcr_opktbuf.pb_len) & 3) != 0)
		panic("%s: modexp invalid len 0x%x\n",
		    device_get_nameunit(sc->sc_dev),
		    letoh32(mcr->mcr_opktbuf.pb_len));
#endif

	ctx = (struct ubsec_ctx_modexp *)me->me_q.q_ctx.dma_vaddr;
	bzero(ctx, sizeof(*ctx));
	ubsec_kshift_r(shiftbits,
	    krp->krp_param[UBS_MODEXP_PAR_N].crp_p, nbits,
	    ctx->me_N, normbits);
	ctx->me_len = htole16((normbits / 8) + (4 * sizeof(u_int16_t)));
	ctx->me_op = htole16(UBS_CTXOP_MODEXP);
	ctx->me_E_len = htole16(nbits);
	ctx->me_N_len = htole16(nbits);

#ifdef UBSEC_DEBUG
	if (ubsec_debug) {
		ubsec_dump_mcr(mcr);
		ubsec_dump_ctx2((struct ubsec_ctx_keyop *)ctx);
	}
#endif

	/*
	 * ubsec_feed2 will sync mcr and ctx, we just need to sync
	 * everything else.
	 */
	ubsec_dma_sync(&me->me_M, BUS_DMASYNC_PREWRITE);
	ubsec_dma_sync(&me->me_E, BUS_DMASYNC_PREWRITE);
	ubsec_dma_sync(&me->me_C, BUS_DMASYNC_PREREAD);
	ubsec_dma_sync(&me->me_epb, BUS_DMASYNC_PREWRITE);

	/* Enqueue and we're done... */
	mtx_lock(&sc->sc_mcr2lock);
	SIMPLEQ_INSERT_TAIL(&sc->sc_queue2, &me->me_q, q_next);
	ubsec_feed2(sc);
	ubsecstats.hst_modexp++;
	mtx_unlock(&sc->sc_mcr2lock);

	return (0);

errout:
	if (me != NULL) {
		if (me->me_q.q_mcr.dma_tag != NULL)
			ubsec_dma_free(sc, &me->me_q.q_mcr);
		if (me->me_q.q_ctx.dma_tag != NULL) {
			bzero(me->me_q.q_ctx.dma_vaddr, me->me_q.q_ctx.dma_size);
			ubsec_dma_free(sc, &me->me_q.q_ctx);
		}
		if (me->me_M.dma_tag != NULL) {
			bzero(me->me_M.dma_vaddr, me->me_M.dma_size);
			ubsec_dma_free(sc, &me->me_M);
		}
		if (me->me_E.dma_tag != NULL) {
			bzero(me->me_E.dma_vaddr, me->me_E.dma_size);
			ubsec_dma_free(sc, &me->me_E);
		}
		if (me->me_C.dma_tag != NULL) {
			bzero(me->me_C.dma_vaddr, me->me_C.dma_size);
			ubsec_dma_free(sc, &me->me_C);
		}
		if (me->me_epb.dma_tag != NULL)
			ubsec_dma_free(sc, &me->me_epb);
		free(me, M_DEVBUF);
	}
	krp->krp_status = err;
	crypto_kdone(krp);
	return (0);
}

/*
 * Start computation of cr[C] = (cr[M] ^ cr[E]) mod cr[N] (hw normalization)
 */
static int
ubsec_kprocess_modexp_hw(struct ubsec_softc *sc, struct cryptkop *krp, int hint)
{
	struct ubsec_q2_modexp *me;
	struct ubsec_mcr *mcr;
	struct ubsec_ctx_modexp *ctx;
	struct ubsec_pktbuf *epb;
	int err = 0;
	u_int nbits, normbits, mbits, shiftbits, ebits;

	me = (struct ubsec_q2_modexp *)malloc(sizeof *me, M_DEVBUF, M_NOWAIT);
	if (me == NULL) {
		err = ENOMEM;
		goto errout;
	}
	bzero(me, sizeof *me);
	me->me_krp = krp;
	me->me_q.q_type = UBS_CTXOP_MODEXP;

	nbits = ubsec_ksigbits(&krp->krp_param[UBS_MODEXP_PAR_N]);
	if (nbits <= 512)
		normbits = 512;
	else if (nbits <= 768)
		normbits = 768;
	else if (nbits <= 1024)
		normbits = 1024;
	else if (sc->sc_flags & UBS_FLAGS_BIGKEY && nbits <= 1536)
		normbits = 1536;
	else if (sc->sc_flags & UBS_FLAGS_BIGKEY && nbits <= 2048)
		normbits = 2048;
	else {
		err = E2BIG;
		goto errout;
	}

	shiftbits = normbits - nbits;

	/* XXX ??? */
	me->me_modbits = nbits;
	me->me_shiftbits = shiftbits;
	me->me_normbits = normbits;

	/* Sanity check: result bits must be >= true modulus bits. */
	if (krp->krp_param[krp->krp_iparams].crp_nbits < nbits) {
		err = ERANGE;
		goto errout;
	}

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_mcr),
	    &me->me_q.q_mcr, 0)) {
		err = ENOMEM;
		goto errout;
	}
	mcr = (struct ubsec_mcr *)me->me_q.q_mcr.dma_vaddr;

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_ctx_modexp),
	    &me->me_q.q_ctx, 0)) {
		err = ENOMEM;
		goto errout;
	}

	mbits = ubsec_ksigbits(&krp->krp_param[UBS_MODEXP_PAR_M]);
	if (mbits > nbits) {
		err = E2BIG;
		goto errout;
	}
	if (ubsec_dma_malloc(sc, normbits / 8, &me->me_M, 0)) {
		err = ENOMEM;
		goto errout;
	}
	bzero(me->me_M.dma_vaddr, normbits / 8);
	bcopy(krp->krp_param[UBS_MODEXP_PAR_M].crp_p,
	    me->me_M.dma_vaddr, (mbits + 7) / 8);

	if (ubsec_dma_malloc(sc, normbits / 8, &me->me_C, 0)) {
		err = ENOMEM;
		goto errout;
	}
	bzero(me->me_C.dma_vaddr, me->me_C.dma_size);

	ebits = ubsec_ksigbits(&krp->krp_param[UBS_MODEXP_PAR_E]);
	if (ebits > nbits) {
		err = E2BIG;
		goto errout;
	}
	if (ubsec_dma_malloc(sc, normbits / 8, &me->me_E, 0)) {
		err = ENOMEM;
		goto errout;
	}
	bzero(me->me_E.dma_vaddr, normbits / 8);
	bcopy(krp->krp_param[UBS_MODEXP_PAR_E].crp_p,
	    me->me_E.dma_vaddr, (ebits + 7) / 8);

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_pktbuf),
	    &me->me_epb, 0)) {
		err = ENOMEM;
		goto errout;
	}
	epb = (struct ubsec_pktbuf *)me->me_epb.dma_vaddr;
	epb->pb_addr = htole32(me->me_E.dma_paddr);
	epb->pb_next = 0;
	epb->pb_len = htole32((ebits + 7) / 8);

#ifdef UBSEC_DEBUG
	if (ubsec_debug) {
		printf("Epb ");
		ubsec_dump_pb(epb);
	}
#endif

	mcr->mcr_pkts = htole16(1);
	mcr->mcr_flags = 0;
	mcr->mcr_cmdctxp = htole32(me->me_q.q_ctx.dma_paddr);
	mcr->mcr_reserved = 0;
	mcr->mcr_pktlen = 0;

	mcr->mcr_ipktbuf.pb_addr = htole32(me->me_M.dma_paddr);
	mcr->mcr_ipktbuf.pb_len = htole32(normbits / 8);
	mcr->mcr_ipktbuf.pb_next = htole32(me->me_epb.dma_paddr);

	mcr->mcr_opktbuf.pb_addr = htole32(me->me_C.dma_paddr);
	mcr->mcr_opktbuf.pb_next = 0;
	mcr->mcr_opktbuf.pb_len = htole32(normbits / 8);

#ifdef DIAGNOSTIC
	/* Misaligned output buffer will hang the chip. */
	if ((letoh32(mcr->mcr_opktbuf.pb_addr) & 3) != 0)
		panic("%s: modexp invalid addr 0x%x\n",
		    device_get_nameunit(sc->sc_dev),
		    letoh32(mcr->mcr_opktbuf.pb_addr));
	if ((letoh32(mcr->mcr_opktbuf.pb_len) & 3) != 0)
		panic("%s: modexp invalid len 0x%x\n",
		    device_get_nameunit(sc->sc_dev),
		    letoh32(mcr->mcr_opktbuf.pb_len));
#endif

	ctx = (struct ubsec_ctx_modexp *)me->me_q.q_ctx.dma_vaddr;
	bzero(ctx, sizeof(*ctx));
	bcopy(krp->krp_param[UBS_MODEXP_PAR_N].crp_p, ctx->me_N,
	    (nbits + 7) / 8);
	ctx->me_len = htole16((normbits / 8) + (4 * sizeof(u_int16_t)));
	ctx->me_op = htole16(UBS_CTXOP_MODEXP);
	ctx->me_E_len = htole16(ebits);
	ctx->me_N_len = htole16(nbits);

#ifdef UBSEC_DEBUG
	if (ubsec_debug) {
		ubsec_dump_mcr(mcr);
		ubsec_dump_ctx2((struct ubsec_ctx_keyop *)ctx);
	}
#endif

	/*
	 * ubsec_feed2 will sync mcr and ctx, we just need to sync
	 * everything else.
	 */
	ubsec_dma_sync(&me->me_M, BUS_DMASYNC_PREWRITE);
	ubsec_dma_sync(&me->me_E, BUS_DMASYNC_PREWRITE);
	ubsec_dma_sync(&me->me_C, BUS_DMASYNC_PREREAD);
	ubsec_dma_sync(&me->me_epb, BUS_DMASYNC_PREWRITE);

	/* Enqueue and we're done... */
	mtx_lock(&sc->sc_mcr2lock);
	SIMPLEQ_INSERT_TAIL(&sc->sc_queue2, &me->me_q, q_next);
	ubsec_feed2(sc);
	mtx_unlock(&sc->sc_mcr2lock);

	return (0);

errout:
	if (me != NULL) {
		if (me->me_q.q_mcr.dma_tag != NULL)
			ubsec_dma_free(sc, &me->me_q.q_mcr);
		if (me->me_q.q_ctx.dma_tag != NULL) {
			bzero(me->me_q.q_ctx.dma_vaddr, me->me_q.q_ctx.dma_size);
			ubsec_dma_free(sc, &me->me_q.q_ctx);
		}
		if (me->me_M.dma_tag != NULL) {
			bzero(me->me_M.dma_vaddr, me->me_M.dma_size);
			ubsec_dma_free(sc, &me->me_M);
		}
		if (me->me_E.dma_tag != NULL) {
			bzero(me->me_E.dma_vaddr, me->me_E.dma_size);
			ubsec_dma_free(sc, &me->me_E);
		}
		if (me->me_C.dma_tag != NULL) {
			bzero(me->me_C.dma_vaddr, me->me_C.dma_size);
			ubsec_dma_free(sc, &me->me_C);
		}
		if (me->me_epb.dma_tag != NULL)
			ubsec_dma_free(sc, &me->me_epb);
		free(me, M_DEVBUF);
	}
	krp->krp_status = err;
	crypto_kdone(krp);
	return (0);
}

static int
ubsec_kprocess_rsapriv(struct ubsec_softc *sc, struct cryptkop *krp, int hint)
{
	struct ubsec_q2_rsapriv *rp = NULL;
	struct ubsec_mcr *mcr;
	struct ubsec_ctx_rsapriv *ctx;
	int err = 0;
	u_int padlen, msglen;

	msglen = ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_P]);
	padlen = ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_Q]);
	if (msglen > padlen)
		padlen = msglen;

	if (padlen <= 256)
		padlen = 256;
	else if (padlen <= 384)
		padlen = 384;
	else if (padlen <= 512)
		padlen = 512;
	else if (sc->sc_flags & UBS_FLAGS_BIGKEY && padlen <= 768)
		padlen = 768;
	else if (sc->sc_flags & UBS_FLAGS_BIGKEY && padlen <= 1024)
		padlen = 1024;
	else {
		err = E2BIG;
		goto errout;
	}

	if (ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_DP]) > padlen) {
		err = E2BIG;
		goto errout;
	}

	if (ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_DQ]) > padlen) {
		err = E2BIG;
		goto errout;
	}

	if (ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_PINV]) > padlen) {
		err = E2BIG;
		goto errout;
	}

	rp = (struct ubsec_q2_rsapriv *)malloc(sizeof *rp, M_DEVBUF, M_NOWAIT);
	if (rp == NULL)
		return (ENOMEM);
	bzero(rp, sizeof *rp);
	rp->rpr_krp = krp;
	rp->rpr_q.q_type = UBS_CTXOP_RSAPRIV;

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_mcr),
	    &rp->rpr_q.q_mcr, 0)) {
		err = ENOMEM;
		goto errout;
	}
	mcr = (struct ubsec_mcr *)rp->rpr_q.q_mcr.dma_vaddr;

	if (ubsec_dma_malloc(sc, sizeof(struct ubsec_ctx_rsapriv),
	    &rp->rpr_q.q_ctx, 0)) {
		err = ENOMEM;
		goto errout;
	}
	ctx = (struct ubsec_ctx_rsapriv *)rp->rpr_q.q_ctx.dma_vaddr;
	bzero(ctx, sizeof *ctx);

	/* Copy in p */
	bcopy(krp->krp_param[UBS_RSAPRIV_PAR_P].crp_p,
	    &ctx->rpr_buf[0 * (padlen / 8)],
	    (krp->krp_param[UBS_RSAPRIV_PAR_P].crp_nbits + 7) / 8);

	/* Copy in q */
	bcopy(krp->krp_param[UBS_RSAPRIV_PAR_Q].crp_p,
	    &ctx->rpr_buf[1 * (padlen / 8)],
	    (krp->krp_param[UBS_RSAPRIV_PAR_Q].crp_nbits + 7) / 8);

	/* Copy in dp */
	bcopy(krp->krp_param[UBS_RSAPRIV_PAR_DP].crp_p,
	    &ctx->rpr_buf[2 * (padlen / 8)],
	    (krp->krp_param[UBS_RSAPRIV_PAR_DP].crp_nbits + 7) / 8);

	/* Copy in dq */
	bcopy(krp->krp_param[UBS_RSAPRIV_PAR_DQ].crp_p,
	    &ctx->rpr_buf[3 * (padlen / 8)],
	    (krp->krp_param[UBS_RSAPRIV_PAR_DQ].crp_nbits + 7) / 8);

	/* Copy in pinv */
	bcopy(krp->krp_param[UBS_RSAPRIV_PAR_PINV].crp_p,
	    &ctx->rpr_buf[4 * (padlen / 8)],
	    (krp->krp_param[UBS_RSAPRIV_PAR_PINV].crp_nbits + 7) / 8);

	msglen = padlen * 2;

	/* Copy in input message (aligned buffer/length). */
	if (ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_MSGIN]) > msglen) {
		/* Is this likely? */
		err = E2BIG;
		goto errout;
	}
	if (ubsec_dma_malloc(sc, (msglen + 7) / 8, &rp->rpr_msgin, 0)) {
		err = ENOMEM;
		goto errout;
	}
	bzero(rp->rpr_msgin.dma_vaddr, (msglen + 7) / 8);
	bcopy(krp->krp_param[UBS_RSAPRIV_PAR_MSGIN].crp_p,
	    rp->rpr_msgin.dma_vaddr,
	    (krp->krp_param[UBS_RSAPRIV_PAR_MSGIN].crp_nbits + 7) / 8);

	/* Prepare space for output message (aligned buffer/length). */
	if (ubsec_ksigbits(&krp->krp_param[UBS_RSAPRIV_PAR_MSGOUT]) < msglen) {
		/* Is this likely? */
		err = E2BIG;
		goto errout;
	}
	if (ubsec_dma_malloc(sc, (msglen + 7) / 8, &rp->rpr_msgout, 0)) {
		err = ENOMEM;
		goto errout;
	}
	bzero(rp->rpr_msgout.dma_vaddr, (msglen + 7) / 8);

	mcr->mcr_pkts = htole16(1);
	mcr->mcr_flags = 0;
	mcr->mcr_cmdctxp = htole32(rp->rpr_q.q_ctx.dma_paddr);
	mcr->mcr_ipktbuf.pb_addr = htole32(rp->rpr_msgin.dma_paddr);
	mcr->mcr_ipktbuf.pb_next = 0;
	mcr->mcr_ipktbuf.pb_len = htole32(rp->rpr_msgin.dma_size);
	mcr->mcr_reserved = 0;
	mcr->mcr_pktlen = htole16(msglen);
	mcr->mcr_opktbuf.pb_addr = htole32(rp->rpr_msgout.dma_paddr);
	mcr->mcr_opktbuf.pb_next = 0;
	mcr->mcr_opktbuf.pb_len = htole32(rp->rpr_msgout.dma_size);

#ifdef DIAGNOSTIC
	if (rp->rpr_msgin.dma_paddr & 3 || rp->rpr_msgin.dma_size & 3) {
		panic("%s: rsapriv: invalid msgin %x(0x%jx)",
		    device_get_nameunit(sc->sc_dev),
		    rp->rpr_msgin.dma_paddr, (uintmax_t)rp->rpr_msgin.dma_size);
	}
	if (rp->rpr_msgout.dma_paddr & 3 || rp->rpr_msgout.dma_size & 3) {
		panic("%s: rsapriv: invalid msgout %x(0x%jx)",
		    device_get_nameunit(sc->sc_dev),
		    rp->rpr_msgout.dma_paddr, (uintmax_t)rp->rpr_msgout.dma_size);
	}
#endif

	ctx->rpr_len = (sizeof(u_int16_t) * 4) + (5 * (padlen / 8));
	ctx->rpr_op = htole16(UBS_CTXOP_RSAPRIV);
	ctx->rpr_q_len = htole16(padlen);
	ctx->rpr_p_len = htole16(padlen);

	/*
	 * ubsec_feed2 will sync mcr and ctx, we just need to sync
	 * everything else.
	 */
	ubsec_dma_sync(&rp->rpr_msgin, BUS_DMASYNC_PREWRITE);
	ubsec_dma_sync(&rp->rpr_msgout, BUS_DMASYNC_PREREAD);

	/* Enqueue and we're done... */
	mtx_lock(&sc->sc_mcr2lock);
	SIMPLEQ_INSERT_TAIL(&sc->sc_queue2, &rp->rpr_q, q_next);
	ubsec_feed2(sc);
	ubsecstats.hst_modexpcrt++;
	mtx_unlock(&sc->sc_mcr2lock);
	return (0);

errout:
	if (rp != NULL) {
		if (rp->rpr_q.q_mcr.dma_tag != NULL)
			ubsec_dma_free(sc, &rp->rpr_q.q_mcr);
		if (rp->rpr_msgin.dma_tag != NULL) {
			bzero(rp->rpr_msgin.dma_vaddr, rp->rpr_msgin.dma_size);
			ubsec_dma_free(sc, &rp->rpr_msgin);
		}
		if (rp->rpr_msgout.dma_tag != NULL) {
			bzero(rp->rpr_msgout.dma_vaddr, rp->rpr_msgout.dma_size);
			ubsec_dma_free(sc, &rp->rpr_msgout);
		}
		free(rp, M_DEVBUF);
	}
	krp->krp_status = err;
	crypto_kdone(krp);
	return (0);
}

#ifdef UBSEC_DEBUG
static void
ubsec_dump_pb(volatile struct ubsec_pktbuf *pb)
{
	printf("addr 0x%x (0x%x) next 0x%x\n",
	    pb->pb_addr, pb->pb_len, pb->pb_next);
}

static void
ubsec_dump_ctx2(struct ubsec_ctx_keyop *c)
{
	printf("CTX (0x%x):\n", c->ctx_len);
	switch (letoh16(c->ctx_op)) {
	case UBS_CTXOP_RNGBYPASS:
	case UBS_CTXOP_RNGSHA1:
		break;
	case UBS_CTXOP_MODEXP:
	{
		struct ubsec_ctx_modexp *cx = (void *)c;
		int i, len;

		printf(" Elen %u, Nlen %u\n",
		    letoh16(cx->me_E_len), letoh16(cx->me_N_len));
		len = (cx->me_N_len + 7)/8;
		for (i = 0; i < len; i++)
			printf("%s%02x", (i == 0) ? " N: " : ":", cx->me_N[i]);
		printf("\n");
		break;
	}
	default:
		printf("unknown context: %x\n", c->ctx_op);
	}
	printf("END CTX\n");
}

static void
ubsec_dump_mcr(struct ubsec_mcr *mcr)
{
	volatile struct ubsec_mcr_add *ma;
	int i;

	printf("MCR:\n");
	printf(" pkts: %u, flags 0x%x\n",
	    letoh16(mcr->mcr_pkts), letoh16(mcr->mcr_flags));
	ma = (volatile struct ubsec_mcr_add *)&mcr->mcr_cmdctxp;
	for (i = 0; i < letoh16(mcr->mcr_pkts); i++) {
		printf(" %d: ctx 0x%x len 0x%x rsvd 0x%x\n", i,
		    letoh32(ma->mcr_cmdctxp), letoh16(ma->mcr_pktlen),
		    letoh16(ma->mcr_reserved));
		printf(" %d: ipkt ", i);
		ubsec_dump_pb(&ma->mcr_ipktbuf);
		printf(" %d: opkt ", i);
		ubsec_dump_pb(&ma->mcr_opktbuf);
		ma++;
	}
	printf("END MCR\n");
}
#endif /* UBSEC_DEBUG */

/*
 * Return the number of significant bits of a big number.
 */
static int
ubsec_ksigbits(struct crparam *cr)
{
	u_int plen = (cr->crp_nbits + 7) / 8;
	int i, sig = plen * 8;
	u_int8_t c, *p = cr->crp_p;

	for (i = plen - 1; i >= 0; i--) {
		c = p[i];
		if (c != 0) {
			while ((c & 0x80) == 0) {
				sig--;
				c <<= 1;
			}
			break;
		}
		sig -= 8;
	}
	return (sig);
}

static void
ubsec_kshift_r(
	u_int shiftbits,
	u_int8_t *src, u_int srcbits,
	u_int8_t *dst, u_int dstbits)
{
	u_int slen, dlen;
	int i, si, di, n;

	slen = (srcbits + 7) / 8;
	dlen = (dstbits + 7) / 8;

	for (i = 0; i < slen; i++)
		dst[i] = src[i];
	for (i = 0; i < dlen - slen; i++)
		dst[slen + i] = 0;

	n = shiftbits / 8;
	if (n != 0) {
		si = dlen - n - 1;
		di = dlen - 1;
		while (si >= 0)
			dst[di--] = dst[si--];
		while (di >= 0)
			dst[di--] = 0;
	}

	n = shiftbits % 8;
	if (n != 0) {
		for (i = dlen - 1; i > 0; i--)
			dst[i] = (dst[i] << n) |
			    (dst[i - 1] >> (8 - n));
		dst[0] = dst[0] << n;
	}
}

static void
ubsec_kshift_l(
	u_int shiftbits,
	u_int8_t *src, u_int srcbits,
	u_int8_t *dst, u_int dstbits)
{
	int slen, dlen, i, n;

	slen = (srcbits + 7) / 8;
	dlen = (dstbits + 7) / 8;

	n = shiftbits / 8;
	for (i = 0; i < slen; i++)
		dst[i] = src[i + n];
	for (i = 0; i < dlen - slen; i++)
		dst[slen + i] = 0;

	n = shiftbits % 8;
	if (n != 0) {
		for (i = 0; i < (dlen - 1); i++)
			dst[i] = (dst[i] >> n) | (dst[i + 1] << (8 - n));
		dst[dlen - 1] = dst[dlen - 1] >> n;
	}
}
