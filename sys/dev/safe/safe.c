/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2003 Global Technology Associates, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SafeNet SafeXcel-1141 hardware crypto accelerator
 */
#include "opt_safe.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
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

#ifdef SAFE_RNDTEST
#include <dev/rndtest/rndtest.h>
#endif
#include <dev/safe/safereg.h>
#include <dev/safe/safevar.h>

#ifndef bswap32
#define	bswap32	NTOHL
#endif

/*
 * Prototypes and count for the pci_device structure
 */
static	int safe_probe(device_t);
static	int safe_attach(device_t);
static	int safe_detach(device_t);
static	int safe_suspend(device_t);
static	int safe_resume(device_t);
static	int safe_shutdown(device_t);

static	int safe_newsession(device_t, crypto_session_t, struct cryptoini *);
static	int safe_process(device_t, struct cryptop *, int);

static device_method_t safe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		safe_probe),
	DEVMETHOD(device_attach,	safe_attach),
	DEVMETHOD(device_detach,	safe_detach),
	DEVMETHOD(device_suspend,	safe_suspend),
	DEVMETHOD(device_resume,	safe_resume),
	DEVMETHOD(device_shutdown,	safe_shutdown),

	/* crypto device methods */
	DEVMETHOD(cryptodev_newsession,	safe_newsession),
	DEVMETHOD(cryptodev_process,	safe_process),

	DEVMETHOD_END
};
static driver_t safe_driver = {
	"safe",
	safe_methods,
	sizeof (struct safe_softc)
};
static devclass_t safe_devclass;

DRIVER_MODULE(safe, pci, safe_driver, safe_devclass, 0, 0);
MODULE_DEPEND(safe, crypto, 1, 1, 1);
#ifdef SAFE_RNDTEST
MODULE_DEPEND(safe, rndtest, 1, 1, 1);
#endif

static	void safe_intr(void *);
static	void safe_callback(struct safe_softc *, struct safe_ringentry *);
static	void safe_feed(struct safe_softc *, struct safe_ringentry *);
static	void safe_mcopy(struct mbuf *, struct mbuf *, u_int);
#ifndef SAFE_NO_RNG
static	void safe_rng_init(struct safe_softc *);
static	void safe_rng(void *);
#endif /* SAFE_NO_RNG */
static	int safe_dma_malloc(struct safe_softc *, bus_size_t,
	        struct safe_dma_alloc *, int);
#define	safe_dma_sync(_dma, _flags) \
	bus_dmamap_sync((_dma)->dma_tag, (_dma)->dma_map, (_flags))
static	void safe_dma_free(struct safe_softc *, struct safe_dma_alloc *);
static	int safe_dmamap_aligned(const struct safe_operand *);
static	int safe_dmamap_uniform(const struct safe_operand *);

static	void safe_reset_board(struct safe_softc *);
static	void safe_init_board(struct safe_softc *);
static	void safe_init_pciregs(device_t dev);
static	void safe_cleanchip(struct safe_softc *);
static	void safe_totalreset(struct safe_softc *);

static	int safe_free_entry(struct safe_softc *, struct safe_ringentry *);

static SYSCTL_NODE(_hw, OID_AUTO, safe, CTLFLAG_RD, 0,
    "SafeNet driver parameters");

#ifdef SAFE_DEBUG
static	void safe_dump_dmastatus(struct safe_softc *, const char *);
static	void safe_dump_ringstate(struct safe_softc *, const char *);
static	void safe_dump_intrstate(struct safe_softc *, const char *);
static	void safe_dump_request(struct safe_softc *, const char *,
		struct safe_ringentry *);

static	struct safe_softc *safec;		/* for use by hw.safe.dump */

static	int safe_debug = 0;
SYSCTL_INT(_hw_safe, OID_AUTO, debug, CTLFLAG_RW, &safe_debug,
	    0, "control debugging msgs");
#define	DPRINTF(_x)	if (safe_debug) printf _x
#else
#define	DPRINTF(_x)
#endif

#define	READ_REG(sc,r) \
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (r))

#define WRITE_REG(sc,reg,val) \
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, reg, val)

struct safe_stats safestats;
SYSCTL_STRUCT(_hw_safe, OID_AUTO, stats, CTLFLAG_RD, &safestats,
	    safe_stats, "driver statistics");
#ifndef SAFE_NO_RNG
static	int safe_rnginterval = 1;		/* poll once a second */
SYSCTL_INT(_hw_safe, OID_AUTO, rnginterval, CTLFLAG_RW, &safe_rnginterval,
	    0, "RNG polling interval (secs)");
static	int safe_rngbufsize = 16;		/* 64 bytes each poll  */
SYSCTL_INT(_hw_safe, OID_AUTO, rngbufsize, CTLFLAG_RW, &safe_rngbufsize,
	    0, "RNG polling buffer size (32-bit words)");
static	int safe_rngmaxalarm = 8;		/* max alarms before reset */
SYSCTL_INT(_hw_safe, OID_AUTO, rngmaxalarm, CTLFLAG_RW, &safe_rngmaxalarm,
	    0, "RNG max alarms before reset");
#endif /* SAFE_NO_RNG */

static int
safe_probe(device_t dev)
{
	if (pci_get_vendor(dev) == PCI_VENDOR_SAFENET &&
	    pci_get_device(dev) == PCI_PRODUCT_SAFEXCEL)
		return (BUS_PROBE_DEFAULT);
	return (ENXIO);
}

static const char*
safe_partname(struct safe_softc *sc)
{
	/* XXX sprintf numbers when not decoded */
	switch (pci_get_vendor(sc->sc_dev)) {
	case PCI_VENDOR_SAFENET:
		switch (pci_get_device(sc->sc_dev)) {
		case PCI_PRODUCT_SAFEXCEL: return "SafeNet SafeXcel-1141";
		}
		return "SafeNet unknown-part";
	}
	return "Unknown-vendor unknown-part";
}

#ifndef SAFE_NO_RNG
static void
default_harvest(struct rndtest_state *rsp, void *buf, u_int count)
{
	/* MarkM: FIX!! Check that this does not swamp the harvester! */
	random_harvest_queue(buf, count, RANDOM_PURE_SAFE);
}
#endif /* SAFE_NO_RNG */

static int
safe_attach(device_t dev)
{
	struct safe_softc *sc = device_get_softc(dev);
	u_int32_t raddr;
	u_int32_t i, devinfo;
	int rid;

	bzero(sc, sizeof (*sc));
	sc->sc_dev = dev;

	/* XXX handle power management */

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
			   NULL, safe_intr, sc, &sc->sc_ih)) {
		device_printf(dev, "could not establish interrupt\n");
		goto bad2;
	}

	sc->sc_cid = crypto_get_driverid(dev, sizeof(struct safe_session),
	    CRYPTOCAP_F_HARDWARE);
	if (sc->sc_cid < 0) {
		device_printf(dev, "could not get crypto driver id\n");
		goto bad3;
	}

	sc->sc_chiprev = READ_REG(sc, SAFE_DEVINFO) &
		(SAFE_DEVINFO_REV_MAJ | SAFE_DEVINFO_REV_MIN);

	/*
	 * Setup DMA descriptor area.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
			       1,			/* alignment */
			       SAFE_DMA_BOUNDARY,	/* boundary */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       SAFE_MAX_DMA,		/* maxsize */
			       SAFE_MAX_PART,		/* nsegments */
			       SAFE_MAX_SSIZE,		/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL, NULL,		/* locking */
			       &sc->sc_srcdmat)) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto bad4;
	}
	if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
			       1,			/* alignment */
			       SAFE_MAX_DSIZE,		/* boundary */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       SAFE_MAX_DMA,		/* maxsize */
			       SAFE_MAX_PART,		/* nsegments */
			       SAFE_MAX_DSIZE,		/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL, NULL,		/* locking */
			       &sc->sc_dstdmat)) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto bad4;
	}

	/*
	 * Allocate packet engine descriptors.
	 */
	if (safe_dma_malloc(sc,
	    SAFE_MAX_NQUEUE * sizeof (struct safe_ringentry),
	    &sc->sc_ringalloc, 0)) {
		device_printf(dev, "cannot allocate PE descriptor ring\n");
		bus_dma_tag_destroy(sc->sc_srcdmat);
		goto bad4;
	}
	/*
	 * Hookup the static portion of all our data structures.
	 */
	sc->sc_ring = (struct safe_ringentry *) sc->sc_ringalloc.dma_vaddr;
	sc->sc_ringtop = sc->sc_ring + SAFE_MAX_NQUEUE;
	sc->sc_front = sc->sc_ring;
	sc->sc_back = sc->sc_ring;
	raddr = sc->sc_ringalloc.dma_paddr;
	bzero(sc->sc_ring, SAFE_MAX_NQUEUE * sizeof(struct safe_ringentry));
	for (i = 0; i < SAFE_MAX_NQUEUE; i++) {
		struct safe_ringentry *re = &sc->sc_ring[i];

		re->re_desc.d_sa = raddr +
			offsetof(struct safe_ringentry, re_sa);
		re->re_sa.sa_staterec = raddr +
			offsetof(struct safe_ringentry, re_sastate);

		raddr += sizeof (struct safe_ringentry);
	}
	mtx_init(&sc->sc_ringmtx, device_get_nameunit(dev),
		"packet engine ring", MTX_DEF);

	/*
	 * Allocate scatter and gather particle descriptors.
	 */
	if (safe_dma_malloc(sc, SAFE_TOTAL_SPART * sizeof (struct safe_pdesc),
	    &sc->sc_spalloc, 0)) {
		device_printf(dev, "cannot allocate source particle "
			"descriptor ring\n");
		mtx_destroy(&sc->sc_ringmtx);
		safe_dma_free(sc, &sc->sc_ringalloc);
		bus_dma_tag_destroy(sc->sc_srcdmat);
		goto bad4;
	}
	sc->sc_spring = (struct safe_pdesc *) sc->sc_spalloc.dma_vaddr;
	sc->sc_springtop = sc->sc_spring + SAFE_TOTAL_SPART;
	sc->sc_spfree = sc->sc_spring;
	bzero(sc->sc_spring, SAFE_TOTAL_SPART * sizeof(struct safe_pdesc));

	if (safe_dma_malloc(sc, SAFE_TOTAL_DPART * sizeof (struct safe_pdesc),
	    &sc->sc_dpalloc, 0)) {
		device_printf(dev, "cannot allocate destination particle "
			"descriptor ring\n");
		mtx_destroy(&sc->sc_ringmtx);
		safe_dma_free(sc, &sc->sc_spalloc);
		safe_dma_free(sc, &sc->sc_ringalloc);
		bus_dma_tag_destroy(sc->sc_dstdmat);
		goto bad4;
	}
	sc->sc_dpring = (struct safe_pdesc *) sc->sc_dpalloc.dma_vaddr;
	sc->sc_dpringtop = sc->sc_dpring + SAFE_TOTAL_DPART;
	sc->sc_dpfree = sc->sc_dpring;
	bzero(sc->sc_dpring, SAFE_TOTAL_DPART * sizeof(struct safe_pdesc));

	device_printf(sc->sc_dev, "%s", safe_partname(sc));

	devinfo = READ_REG(sc, SAFE_DEVINFO);
	if (devinfo & SAFE_DEVINFO_RNG) {
		sc->sc_flags |= SAFE_FLAGS_RNG;
		printf(" rng");
	}
	if (devinfo & SAFE_DEVINFO_PKEY) {
#if 0
		printf(" key");
		sc->sc_flags |= SAFE_FLAGS_KEY;
		crypto_kregister(sc->sc_cid, CRK_MOD_EXP, 0);
		crypto_kregister(sc->sc_cid, CRK_MOD_EXP_CRT, 0);
#endif
	}
	if (devinfo & SAFE_DEVINFO_DES) {
		printf(" des/3des");
		crypto_register(sc->sc_cid, CRYPTO_3DES_CBC, 0, 0);
		crypto_register(sc->sc_cid, CRYPTO_DES_CBC, 0, 0);
	}
	if (devinfo & SAFE_DEVINFO_AES) {
		printf(" aes");
		crypto_register(sc->sc_cid, CRYPTO_AES_CBC, 0, 0);
	}
	if (devinfo & SAFE_DEVINFO_MD5) {
		printf(" md5");
		crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, 0, 0);
	}
	if (devinfo & SAFE_DEVINFO_SHA1) {
		printf(" sha1");
		crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, 0, 0);
	}
	printf(" null");
	crypto_register(sc->sc_cid, CRYPTO_NULL_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_NULL_HMAC, 0, 0);
	/* XXX other supported algorithms */
	printf("\n");

	safe_reset_board(sc);		/* reset h/w */
	safe_init_pciregs(dev);		/* init pci settings */
	safe_init_board(sc);		/* init h/w */

#ifndef SAFE_NO_RNG
	if (sc->sc_flags & SAFE_FLAGS_RNG) {
#ifdef SAFE_RNDTEST
		sc->sc_rndtest = rndtest_attach(dev);
		if (sc->sc_rndtest)
			sc->sc_harvest = rndtest_harvest;
		else
			sc->sc_harvest = default_harvest;
#else
		sc->sc_harvest = default_harvest;
#endif
		safe_rng_init(sc);

		callout_init(&sc->sc_rngto, 1);
		callout_reset(&sc->sc_rngto, hz*safe_rnginterval, safe_rng, sc);
	}
#endif /* SAFE_NO_RNG */
#ifdef SAFE_DEBUG
	safec = sc;			/* for use by hw.safe.dump */
#endif
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
safe_detach(device_t dev)
{
	struct safe_softc *sc = device_get_softc(dev);

	/* XXX wait/abort active ops */

	WRITE_REG(sc, SAFE_HI_MASK, 0);		/* disable interrupts */

	callout_stop(&sc->sc_rngto);

	crypto_unregister_all(sc->sc_cid);

#ifdef SAFE_RNDTEST
	if (sc->sc_rndtest)
		rndtest_detach(sc->sc_rndtest);
#endif

	safe_cleanchip(sc);
	safe_dma_free(sc, &sc->sc_dpalloc);
	safe_dma_free(sc, &sc->sc_spalloc);
	mtx_destroy(&sc->sc_ringmtx);
	safe_dma_free(sc, &sc->sc_ringalloc);

	bus_generic_detach(dev);
	bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq);

	bus_dma_tag_destroy(sc->sc_srcdmat);
	bus_dma_tag_destroy(sc->sc_dstdmat);
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, sc->sc_sr);

	return (0);
}

/*
 * Stop all chip i/o so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
safe_shutdown(device_t dev)
{
#ifdef notyet
	safe_stop(device_get_softc(dev));
#endif
	return (0);
}

/*
 * Device suspend routine.
 */
static int
safe_suspend(device_t dev)
{
	struct safe_softc *sc = device_get_softc(dev);

#ifdef notyet
	/* XXX stop the device and save PCI settings */
#endif
	sc->sc_suspended = 1;

	return (0);
}

static int
safe_resume(device_t dev)
{
	struct safe_softc *sc = device_get_softc(dev);

#ifdef notyet
	/* XXX retore PCI settings and start the device */
#endif
	sc->sc_suspended = 0;
	return (0);
}

/*
 * SafeXcel Interrupt routine
 */
static void
safe_intr(void *arg)
{
	struct safe_softc *sc = arg;
	volatile u_int32_t stat;

	stat = READ_REG(sc, SAFE_HM_STAT);
	if (stat == 0)			/* shared irq, not for us */
		return;

	WRITE_REG(sc, SAFE_HI_CLR, stat);	/* IACK */

	if ((stat & SAFE_INT_PE_DDONE)) {
		/*
		 * Descriptor(s) done; scan the ring and
		 * process completed operations.
		 */
		mtx_lock(&sc->sc_ringmtx);
		while (sc->sc_back != sc->sc_front) {
			struct safe_ringentry *re = sc->sc_back;
#ifdef SAFE_DEBUG
			if (safe_debug) {
				safe_dump_ringstate(sc, __func__);
				safe_dump_request(sc, __func__, re);
			}
#endif
			/*
			 * safe_process marks ring entries that were allocated
			 * but not used with a csr of zero.  This insures the
			 * ring front pointer never needs to be set backwards
			 * in the event that an entry is allocated but not used
			 * because of a setup error.
			 */
			if (re->re_desc.d_csr != 0) {
				if (!SAFE_PE_CSR_IS_DONE(re->re_desc.d_csr))
					break;
				if (!SAFE_PE_LEN_IS_DONE(re->re_desc.d_len))
					break;
				sc->sc_nqchip--;
				safe_callback(sc, re);
			}
			if (++(sc->sc_back) == sc->sc_ringtop)
				sc->sc_back = sc->sc_ring;
		}
		mtx_unlock(&sc->sc_ringmtx);
	}

	/*
	 * Check to see if we got any DMA Error
	 */
	if (stat & SAFE_INT_PE_ERROR) {
		DPRINTF(("dmaerr dmastat %08x\n",
			READ_REG(sc, SAFE_PE_DMASTAT)));
		safestats.st_dmaerr++;
		safe_totalreset(sc);
#if 0
		safe_feed(sc);
#endif
	}

	if (sc->sc_needwakeup) {		/* XXX check high watermark */
		int wakeup = sc->sc_needwakeup & (CRYPTO_SYMQ|CRYPTO_ASYMQ);
		DPRINTF(("%s: wakeup crypto %x\n", __func__,
			sc->sc_needwakeup));
		sc->sc_needwakeup &= ~wakeup;
		crypto_unblock(sc->sc_cid, wakeup);
	}
}

/*
 * safe_feed() - post a request to chip
 */
static void
safe_feed(struct safe_softc *sc, struct safe_ringentry *re)
{
	bus_dmamap_sync(sc->sc_srcdmat, re->re_src_map, BUS_DMASYNC_PREWRITE);
	if (re->re_dst_map != NULL)
		bus_dmamap_sync(sc->sc_dstdmat, re->re_dst_map,
			BUS_DMASYNC_PREREAD);
	/* XXX have no smaller granularity */
	safe_dma_sync(&sc->sc_ringalloc,
		BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	safe_dma_sync(&sc->sc_spalloc, BUS_DMASYNC_PREWRITE);
	safe_dma_sync(&sc->sc_dpalloc, BUS_DMASYNC_PREWRITE);

#ifdef SAFE_DEBUG
	if (safe_debug) {
		safe_dump_ringstate(sc, __func__);
		safe_dump_request(sc, __func__, re);
	}
#endif
	sc->sc_nqchip++;
	if (sc->sc_nqchip > safestats.st_maxqchip)
		safestats.st_maxqchip = sc->sc_nqchip;
	/* poke h/w to check descriptor ring, any value can be written */
	WRITE_REG(sc, SAFE_HI_RD_DESCR, 0);
}

#define	N(a)	(sizeof(a) / sizeof (a[0]))
static void
safe_setup_enckey(struct safe_session *ses, caddr_t key)
{
	int i;

	bcopy(key, ses->ses_key, ses->ses_klen / 8);

	/* PE is little-endian, insure proper byte order */
	for (i = 0; i < N(ses->ses_key); i++)
		ses->ses_key[i] = htole32(ses->ses_key[i]);
}

static void
safe_setup_mackey(struct safe_session *ses, int algo, caddr_t key, int klen)
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

	/* PE is little-endian, insure proper byte order */
	for (i = 0; i < N(ses->ses_hminner); i++) {
		ses->ses_hminner[i] = htole32(ses->ses_hminner[i]);
		ses->ses_hmouter[i] = htole32(ses->ses_hmouter[i]);
	}
}
#undef N

/*
 * Allocate a new 'session' and return an encoded session id.  'sidp'
 * contains our registration id, and should contain an encoded session
 * id on successful allocation.
 */
static int
safe_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct safe_softc *sc = device_get_softc(dev);
	struct cryptoini *c, *encini = NULL, *macini = NULL;
	struct safe_session *ses = NULL;

	if (cri == NULL || sc == NULL)
		return (EINVAL);

	for (c = cri; c != NULL; c = c->cri_next) {
		if (c->cri_alg == CRYPTO_MD5_HMAC ||
		    c->cri_alg == CRYPTO_SHA1_HMAC ||
		    c->cri_alg == CRYPTO_NULL_HMAC) {
			if (macini)
				return (EINVAL);
			macini = c;
		} else if (c->cri_alg == CRYPTO_DES_CBC ||
		    c->cri_alg == CRYPTO_3DES_CBC ||
		    c->cri_alg == CRYPTO_AES_CBC ||
		    c->cri_alg == CRYPTO_NULL_CBC) {
			if (encini)
				return (EINVAL);
			encini = c;
		} else
			return (EINVAL);
	}
	if (encini == NULL && macini == NULL)
		return (EINVAL);
	if (encini) {			/* validate key length */
		switch (encini->cri_alg) {
		case CRYPTO_DES_CBC:
			if (encini->cri_klen != 64)
				return (EINVAL);
			break;
		case CRYPTO_3DES_CBC:
			if (encini->cri_klen != 192)
				return (EINVAL);
			break;
		case CRYPTO_AES_CBC:
			if (encini->cri_klen != 128 &&
			    encini->cri_klen != 192 &&
			    encini->cri_klen != 256)
				return (EINVAL);
			break;
		}
	}

	ses = crypto_get_driver_session(cses);
	if (encini) {
		/* get an IV */
		/* XXX may read fewer than requested */
		read_random(ses->ses_iv, sizeof(ses->ses_iv));

		ses->ses_klen = encini->cri_klen;
		if (encini->cri_key != NULL)
			safe_setup_enckey(ses, encini->cri_key);
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
			safe_setup_mackey(ses, macini->cri_alg, macini->cri_key,
			    macini->cri_klen / 8);
		}
	}

	return (0);
}

static void
safe_op_cb(void *arg, bus_dma_segment_t *seg, int nsegs, bus_size_t mapsize, int error)
{
	struct safe_operand *op = arg;

	DPRINTF(("%s: mapsize %u nsegs %d error %d\n", __func__,
		(u_int) mapsize, nsegs, error));
	if (error != 0)
		return;
	op->mapsize = mapsize;
	op->nsegs = nsegs;
	bcopy(seg, op->segs, nsegs * sizeof (seg[0]));
}

static int
safe_process(device_t dev, struct cryptop *crp, int hint)
{
	struct safe_softc *sc = device_get_softc(dev);
	int err = 0, i, nicealign, uniform;
	struct cryptodesc *crd1, *crd2, *maccrd, *enccrd;
	int bypass, oplen, ivsize;
	caddr_t iv;
	int16_t coffset;
	struct safe_session *ses;
	struct safe_ringentry *re;
	struct safe_sarec *sa;
	struct safe_pdesc *pd;
	u_int32_t cmd0, cmd1, staterec;

	if (crp == NULL || crp->crp_callback == NULL || sc == NULL) {
		safestats.st_invalid++;
		return (EINVAL);
	}

	mtx_lock(&sc->sc_ringmtx);
	if (sc->sc_front == sc->sc_back && sc->sc_nqchip != 0) {
		safestats.st_ringfull++;
		sc->sc_needwakeup |= CRYPTO_SYMQ;
		mtx_unlock(&sc->sc_ringmtx);
		return (ERESTART);
	}
	re = sc->sc_front;

	staterec = re->re_sa.sa_staterec;	/* save */
	/* NB: zero everything but the PE descriptor */
	bzero(&re->re_sa, sizeof(struct safe_ringentry) - sizeof(re->re_desc));
	re->re_sa.sa_staterec = staterec;	/* restore */

	re->re_crp = crp;

	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		re->re_src_m = (struct mbuf *)crp->crp_buf;
		re->re_dst_m = (struct mbuf *)crp->crp_buf;
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		re->re_src_io = (struct uio *)crp->crp_buf;
		re->re_dst_io = (struct uio *)crp->crp_buf;
	} else {
		safestats.st_badflags++;
		err = EINVAL;
		goto errout;	/* XXX we don't handle contiguous blocks! */
	}

	sa = &re->re_sa;
	ses = crypto_get_driver_session(crp->crp_session);

	crd1 = crp->crp_desc;
	if (crd1 == NULL) {
		safestats.st_nodesc++;
		err = EINVAL;
		goto errout;
	}
	crd2 = crd1->crd_next;

	cmd0 = SAFE_SA_CMD0_BASIC;		/* basic group operation */
	cmd1 = 0;
	if (crd2 == NULL) {
		if (crd1->crd_alg == CRYPTO_MD5_HMAC ||
		    crd1->crd_alg == CRYPTO_SHA1_HMAC ||
		    crd1->crd_alg == CRYPTO_NULL_HMAC) {
			maccrd = crd1;
			enccrd = NULL;
			cmd0 |= SAFE_SA_CMD0_OP_HASH;
		} else if (crd1->crd_alg == CRYPTO_DES_CBC ||
		    crd1->crd_alg == CRYPTO_3DES_CBC ||
		    crd1->crd_alg == CRYPTO_AES_CBC ||
		    crd1->crd_alg == CRYPTO_NULL_CBC) {
			maccrd = NULL;
			enccrd = crd1;
			cmd0 |= SAFE_SA_CMD0_OP_CRYPT;
		} else {
			safestats.st_badalg++;
			err = EINVAL;
			goto errout;
		}
	} else {
		if ((crd1->crd_alg == CRYPTO_MD5_HMAC ||
		    crd1->crd_alg == CRYPTO_SHA1_HMAC ||
		    crd1->crd_alg == CRYPTO_NULL_HMAC) &&
		    (crd2->crd_alg == CRYPTO_DES_CBC ||
			crd2->crd_alg == CRYPTO_3DES_CBC ||
		        crd2->crd_alg == CRYPTO_AES_CBC ||
		        crd2->crd_alg == CRYPTO_NULL_CBC) &&
		    ((crd2->crd_flags & CRD_F_ENCRYPT) == 0)) {
			maccrd = crd1;
			enccrd = crd2;
		} else if ((crd1->crd_alg == CRYPTO_DES_CBC ||
		    crd1->crd_alg == CRYPTO_3DES_CBC ||
		    crd1->crd_alg == CRYPTO_AES_CBC ||
		    crd1->crd_alg == CRYPTO_NULL_CBC) &&
		    (crd2->crd_alg == CRYPTO_MD5_HMAC ||
			crd2->crd_alg == CRYPTO_SHA1_HMAC ||
			crd2->crd_alg == CRYPTO_NULL_HMAC) &&
		    (crd1->crd_flags & CRD_F_ENCRYPT)) {
			enccrd = crd1;
			maccrd = crd2;
		} else {
			safestats.st_badalg++;
			err = EINVAL;
			goto errout;
		}
		cmd0 |= SAFE_SA_CMD0_OP_BOTH;
	}

	if (enccrd) {
		if (enccrd->crd_flags & CRD_F_KEY_EXPLICIT)
			safe_setup_enckey(ses, enccrd->crd_key);

		if (enccrd->crd_alg == CRYPTO_DES_CBC) {
			cmd0 |= SAFE_SA_CMD0_DES;
			cmd1 |= SAFE_SA_CMD1_CBC;
			ivsize = 2*sizeof(u_int32_t);
		} else if (enccrd->crd_alg == CRYPTO_3DES_CBC) {
			cmd0 |= SAFE_SA_CMD0_3DES;
			cmd1 |= SAFE_SA_CMD1_CBC;
			ivsize = 2*sizeof(u_int32_t);
		} else if (enccrd->crd_alg == CRYPTO_AES_CBC) {
			cmd0 |= SAFE_SA_CMD0_AES;
			cmd1 |= SAFE_SA_CMD1_CBC;
			if (ses->ses_klen == 128)
			     cmd1 |=  SAFE_SA_CMD1_AES128;
			else if (ses->ses_klen == 192)
			     cmd1 |=  SAFE_SA_CMD1_AES192;
			else
			     cmd1 |=  SAFE_SA_CMD1_AES256;
			ivsize = 4*sizeof(u_int32_t);
		} else {
			cmd0 |= SAFE_SA_CMD0_CRYPT_NULL;
			ivsize = 0;
		}

		/*
		 * Setup encrypt/decrypt state.  When using basic ops
		 * we can't use an inline IV because hash/crypt offset
		 * must be from the end of the IV to the start of the
		 * crypt data and this leaves out the preceding header
		 * from the hash calculation.  Instead we place the IV
		 * in the state record and set the hash/crypt offset to
		 * copy both the header+IV.
		 */
		if (enccrd->crd_flags & CRD_F_ENCRYPT) {
			cmd0 |= SAFE_SA_CMD0_OUTBOUND;

			if (enccrd->crd_flags & CRD_F_IV_EXPLICIT)
				iv = enccrd->crd_iv;
			else
				iv = (caddr_t) ses->ses_iv;
			if ((enccrd->crd_flags & CRD_F_IV_PRESENT) == 0) {
				crypto_copyback(crp->crp_flags, crp->crp_buf,
				    enccrd->crd_inject, ivsize, iv);
			}
			bcopy(iv, re->re_sastate.sa_saved_iv, ivsize);
			cmd0 |= SAFE_SA_CMD0_IVLD_STATE | SAFE_SA_CMD0_SAVEIV;
			re->re_flags |= SAFE_QFLAGS_COPYOUTIV;
		} else {
			cmd0 |= SAFE_SA_CMD0_INBOUND;

			if (enccrd->crd_flags & CRD_F_IV_EXPLICIT) {
				bcopy(enccrd->crd_iv,
					re->re_sastate.sa_saved_iv, ivsize);
			} else {
				crypto_copydata(crp->crp_flags, crp->crp_buf,
				    enccrd->crd_inject, ivsize,
				    (caddr_t)re->re_sastate.sa_saved_iv);
			}
			cmd0 |= SAFE_SA_CMD0_IVLD_STATE;
		}
		/*
		 * For basic encryption use the zero pad algorithm.
		 * This pads results to an 8-byte boundary and
		 * suppresses padding verification for inbound (i.e.
		 * decrypt) operations.
		 *
		 * NB: Not sure if the 8-byte pad boundary is a problem.
		 */
		cmd0 |= SAFE_SA_CMD0_PAD_ZERO;

		/* XXX assert key bufs have the same size */
		bcopy(ses->ses_key, sa->sa_key, sizeof(sa->sa_key));
	}

	if (maccrd) {
		if (maccrd->crd_flags & CRD_F_KEY_EXPLICIT) {
			safe_setup_mackey(ses, maccrd->crd_alg,
			    maccrd->crd_key, maccrd->crd_klen / 8);
		}

		if (maccrd->crd_alg == CRYPTO_MD5_HMAC) {
			cmd0 |= SAFE_SA_CMD0_MD5;
			cmd1 |= SAFE_SA_CMD1_HMAC;	/* NB: enable HMAC */
		} else if (maccrd->crd_alg == CRYPTO_SHA1_HMAC) {
			cmd0 |= SAFE_SA_CMD0_SHA1;
			cmd1 |= SAFE_SA_CMD1_HMAC;	/* NB: enable HMAC */
		} else {
			cmd0 |= SAFE_SA_CMD0_HASH_NULL;
		}
		/*
		 * Digest data is loaded from the SA and the hash
		 * result is saved to the state block where we
		 * retrieve it for return to the caller.
		 */
		/* XXX assert digest bufs have the same size */
		bcopy(ses->ses_hminner, sa->sa_indigest,
			sizeof(sa->sa_indigest));
		bcopy(ses->ses_hmouter, sa->sa_outdigest,
			sizeof(sa->sa_outdigest));

		cmd0 |= SAFE_SA_CMD0_HSLD_SA | SAFE_SA_CMD0_SAVEHASH;
		re->re_flags |= SAFE_QFLAGS_COPYOUTICV;
	}

	if (enccrd && maccrd) {
		/*
		 * The offset from hash data to the start of
		 * crypt data is the difference in the skips.
		 */
		bypass = maccrd->crd_skip;
		coffset = enccrd->crd_skip - maccrd->crd_skip;
		if (coffset < 0) {
			DPRINTF(("%s: hash does not precede crypt; "
				"mac skip %u enc skip %u\n",
				__func__, maccrd->crd_skip, enccrd->crd_skip));
			safestats.st_skipmismatch++;
			err = EINVAL;
			goto errout;
		}
		oplen = enccrd->crd_skip + enccrd->crd_len;
		if (maccrd->crd_skip + maccrd->crd_len != oplen) {
			DPRINTF(("%s: hash amount %u != crypt amount %u\n",
				__func__, maccrd->crd_skip + maccrd->crd_len,
				oplen));
			safestats.st_lenmismatch++;
			err = EINVAL;
			goto errout;
		}
#ifdef SAFE_DEBUG
		if (safe_debug) {
			printf("mac: skip %d, len %d, inject %d\n",
			    maccrd->crd_skip, maccrd->crd_len,
			    maccrd->crd_inject);
			printf("enc: skip %d, len %d, inject %d\n",
			    enccrd->crd_skip, enccrd->crd_len,
			    enccrd->crd_inject);
			printf("bypass %d coffset %d oplen %d\n",
				bypass, coffset, oplen);
		}
#endif
		if (coffset & 3) {	/* offset must be 32-bit aligned */
			DPRINTF(("%s: coffset %u misaligned\n",
				__func__, coffset));
			safestats.st_coffmisaligned++;
			err = EINVAL;
			goto errout;
		}
		coffset >>= 2;
		if (coffset > 255) {	/* offset must be <256 dwords */
			DPRINTF(("%s: coffset %u too big\n",
				__func__, coffset));
			safestats.st_cofftoobig++;
			err = EINVAL;
			goto errout;
		}
		/*
		 * Tell the hardware to copy the header to the output.
		 * The header is defined as the data from the end of
		 * the bypass to the start of data to be encrypted. 
		 * Typically this is the inline IV.  Note that you need
		 * to do this even if src+dst are the same; it appears
		 * that w/o this bit the crypted data is written
		 * immediately after the bypass data.
		 */
		cmd1 |= SAFE_SA_CMD1_HDRCOPY;
		/*
		 * Disable IP header mutable bit handling.  This is
		 * needed to get correct HMAC calculations.
		 */
		cmd1 |= SAFE_SA_CMD1_MUTABLE;
	} else {
		if (enccrd) {
			bypass = enccrd->crd_skip;
			oplen = bypass + enccrd->crd_len;
		} else {
			bypass = maccrd->crd_skip;
			oplen = bypass + maccrd->crd_len;
		}
		coffset = 0;
	}
	/* XXX verify multiple of 4 when using s/g */
	if (bypass > 96) {		/* bypass offset must be <= 96 bytes */
		DPRINTF(("%s: bypass %u too big\n", __func__, bypass));
		safestats.st_bypasstoobig++;
		err = EINVAL;
		goto errout;
	}

	if (bus_dmamap_create(sc->sc_srcdmat, BUS_DMA_NOWAIT, &re->re_src_map)) {
		safestats.st_nomap++;
		err = ENOMEM;
		goto errout;
	}
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		if (bus_dmamap_load_mbuf(sc->sc_srcdmat, re->re_src_map,
		    re->re_src_m, safe_op_cb,
		    &re->re_src, BUS_DMA_NOWAIT) != 0) {
			bus_dmamap_destroy(sc->sc_srcdmat, re->re_src_map);
			re->re_src_map = NULL;
			safestats.st_noload++;
			err = ENOMEM;
			goto errout;
		}
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		if (bus_dmamap_load_uio(sc->sc_srcdmat, re->re_src_map,
		    re->re_src_io, safe_op_cb,
		    &re->re_src, BUS_DMA_NOWAIT) != 0) {
			bus_dmamap_destroy(sc->sc_srcdmat, re->re_src_map);
			re->re_src_map = NULL;
			safestats.st_noload++;
			err = ENOMEM;
			goto errout;
		}
	}
	nicealign = safe_dmamap_aligned(&re->re_src);
	uniform = safe_dmamap_uniform(&re->re_src);

	DPRINTF(("src nicealign %u uniform %u nsegs %u\n",
		nicealign, uniform, re->re_src.nsegs));
	if (re->re_src.nsegs > 1) {
		re->re_desc.d_src = sc->sc_spalloc.dma_paddr +
			((caddr_t) sc->sc_spfree - (caddr_t) sc->sc_spring);
		for (i = 0; i < re->re_src_nsegs; i++) {
			/* NB: no need to check if there's space */
			pd = sc->sc_spfree;
			if (++(sc->sc_spfree) == sc->sc_springtop)
				sc->sc_spfree = sc->sc_spring;

			KASSERT((pd->pd_flags&3) == 0 ||
				(pd->pd_flags&3) == SAFE_PD_DONE,
				("bogus source particle descriptor; flags %x",
				pd->pd_flags));
			pd->pd_addr = re->re_src_segs[i].ds_addr;
			pd->pd_size = re->re_src_segs[i].ds_len;
			pd->pd_flags = SAFE_PD_READY;
		}
		cmd0 |= SAFE_SA_CMD0_IGATHER;
	} else {
		/*
		 * No need for gather, reference the operand directly.
		 */
		re->re_desc.d_src = re->re_src_segs[0].ds_addr;
	}

	if (enccrd == NULL && maccrd != NULL) {
		/*
		 * Hash op; no destination needed.
		 */
	} else {
		if (crp->crp_flags & CRYPTO_F_IOV) {
			if (!nicealign) {
				safestats.st_iovmisaligned++;
				err = EINVAL;
				goto errout;
			}
			if (uniform != 1) {
				/*
				 * Source is not suitable for direct use as
				 * the destination.  Create a new scatter/gather
				 * list based on the destination requirements
				 * and check if that's ok.
				 */
				if (bus_dmamap_create(sc->sc_dstdmat,
				    BUS_DMA_NOWAIT, &re->re_dst_map)) {
					safestats.st_nomap++;
					err = ENOMEM;
					goto errout;
				}
				if (bus_dmamap_load_uio(sc->sc_dstdmat,
				    re->re_dst_map, re->re_dst_io,
				    safe_op_cb, &re->re_dst,
				    BUS_DMA_NOWAIT) != 0) {
					bus_dmamap_destroy(sc->sc_dstdmat,
						re->re_dst_map);
					re->re_dst_map = NULL;
					safestats.st_noload++;
					err = ENOMEM;
					goto errout;
				}
				uniform = safe_dmamap_uniform(&re->re_dst);
				if (!uniform) {
					/*
					 * There's no way to handle the DMA
					 * requirements with this uio.  We
					 * could create a separate DMA area for
					 * the result and then copy it back,
					 * but for now we just bail and return
					 * an error.  Note that uio requests
					 * > SAFE_MAX_DSIZE are handled because
					 * the DMA map and segment list for the
					 * destination wil result in a
					 * destination particle list that does
					 * the necessary scatter DMA.
					 */ 
					safestats.st_iovnotuniform++;
					err = EINVAL;
					goto errout;
				}
			} else
				re->re_dst = re->re_src;
		} else if (crp->crp_flags & CRYPTO_F_IMBUF) {
			if (nicealign && uniform == 1) {
				/*
				 * Source layout is suitable for direct
				 * sharing of the DMA map and segment list.
				 */
				re->re_dst = re->re_src;
			} else if (nicealign && uniform == 2) {
				/*
				 * The source is properly aligned but requires a
				 * different particle list to handle DMA of the
				 * result.  Create a new map and do the load to
				 * create the segment list.  The particle
				 * descriptor setup code below will handle the
				 * rest.
				 */
				if (bus_dmamap_create(sc->sc_dstdmat,
				    BUS_DMA_NOWAIT, &re->re_dst_map)) {
					safestats.st_nomap++;
					err = ENOMEM;
					goto errout;
				}
				if (bus_dmamap_load_mbuf(sc->sc_dstdmat,
				    re->re_dst_map, re->re_dst_m,
				    safe_op_cb, &re->re_dst,
				    BUS_DMA_NOWAIT) != 0) {
					bus_dmamap_destroy(sc->sc_dstdmat,
						re->re_dst_map);
					re->re_dst_map = NULL;
					safestats.st_noload++;
					err = ENOMEM;
					goto errout;
				}
			} else {		/* !(aligned and/or uniform) */
				int totlen, len;
				struct mbuf *m, *top, **mp;

				/*
				 * DMA constraints require that we allocate a
				 * new mbuf chain for the destination.  We
				 * allocate an entire new set of mbufs of
				 * optimal/required size and then tell the
				 * hardware to copy any bits that are not
				 * created as a byproduct of the operation.
				 */
				if (!nicealign)
					safestats.st_unaligned++;
				if (!uniform)
					safestats.st_notuniform++;
				totlen = re->re_src_mapsize;
				if (re->re_src_m->m_flags & M_PKTHDR) {
					len = MHLEN;
					MGETHDR(m, M_NOWAIT, MT_DATA);
					if (m && !m_dup_pkthdr(m, re->re_src_m,
					    M_NOWAIT)) {
						m_free(m);
						m = NULL;
					}
				} else {
					len = MLEN;
					MGET(m, M_NOWAIT, MT_DATA);
				}
				if (m == NULL) {
					safestats.st_nombuf++;
					err = sc->sc_nqchip ? ERESTART : ENOMEM;
					goto errout;
				}
				if (totlen >= MINCLSIZE) {
					if (!(MCLGET(m, M_NOWAIT))) {
						m_free(m);
						safestats.st_nomcl++;
						err = sc->sc_nqchip ?
							ERESTART : ENOMEM;
						goto errout;
					}
					len = MCLBYTES;
				}
				m->m_len = len;
				top = NULL;
				mp = &top;

				while (totlen > 0) {
					if (top) {
						MGET(m, M_NOWAIT, MT_DATA);
						if (m == NULL) {
							m_freem(top);
							safestats.st_nombuf++;
							err = sc->sc_nqchip ?
							    ERESTART : ENOMEM;
							goto errout;
						}
						len = MLEN;
					}
					if (top && totlen >= MINCLSIZE) {
						if (!(MCLGET(m, M_NOWAIT))) {
							*mp = m;
							m_freem(top);
							safestats.st_nomcl++;
							err = sc->sc_nqchip ?
							    ERESTART : ENOMEM;
							goto errout;
						}
						len = MCLBYTES;
					}
					m->m_len = len = min(totlen, len);
					totlen -= len;
					*mp = m;
					mp = &m->m_next;
				}
				re->re_dst_m = top;
				if (bus_dmamap_create(sc->sc_dstdmat, 
				    BUS_DMA_NOWAIT, &re->re_dst_map) != 0) {
					safestats.st_nomap++;
					err = ENOMEM;
					goto errout;
				}
				if (bus_dmamap_load_mbuf(sc->sc_dstdmat,
				    re->re_dst_map, re->re_dst_m,
				    safe_op_cb, &re->re_dst,
				    BUS_DMA_NOWAIT) != 0) {
					bus_dmamap_destroy(sc->sc_dstdmat,
					re->re_dst_map);
					re->re_dst_map = NULL;
					safestats.st_noload++;
					err = ENOMEM;
					goto errout;
				}
				if (re->re_src.mapsize > oplen) {
					/*
					 * There's data following what the
					 * hardware will copy for us.  If this
					 * isn't just the ICV (that's going to
					 * be written on completion), copy it
					 * to the new mbufs
					 */
					if (!(maccrd &&
					    (re->re_src.mapsize-oplen) == 12 &&
					    maccrd->crd_inject == oplen))
						safe_mcopy(re->re_src_m,
							   re->re_dst_m,
							   oplen);
					else
						safestats.st_noicvcopy++;
				}
			}
		} else {
			safestats.st_badflags++;
			err = EINVAL;
			goto errout;
		}

		if (re->re_dst.nsegs > 1) {
			re->re_desc.d_dst = sc->sc_dpalloc.dma_paddr +
			    ((caddr_t) sc->sc_dpfree - (caddr_t) sc->sc_dpring);
			for (i = 0; i < re->re_dst_nsegs; i++) {
				pd = sc->sc_dpfree;
				KASSERT((pd->pd_flags&3) == 0 ||
					(pd->pd_flags&3) == SAFE_PD_DONE,
					("bogus dest particle descriptor; flags %x",
						pd->pd_flags));
				if (++(sc->sc_dpfree) == sc->sc_dpringtop)
					sc->sc_dpfree = sc->sc_dpring;
				pd->pd_addr = re->re_dst_segs[i].ds_addr;
				pd->pd_flags = SAFE_PD_READY;
			}
			cmd0 |= SAFE_SA_CMD0_OSCATTER;
		} else {
			/*
			 * No need for scatter, reference the operand directly.
			 */
			re->re_desc.d_dst = re->re_dst_segs[0].ds_addr;
		}
	}

	/*
	 * All done with setup; fillin the SA command words
	 * and the packet engine descriptor.  The operation
	 * is now ready for submission to the hardware.
	 */
	sa->sa_cmd0 = cmd0 | SAFE_SA_CMD0_IPCI | SAFE_SA_CMD0_OPCI;
	sa->sa_cmd1 = cmd1
		    | (coffset << SAFE_SA_CMD1_OFFSET_S)
		    | SAFE_SA_CMD1_SAREV1	/* Rev 1 SA data structure */
		    | SAFE_SA_CMD1_SRPCI
		    ;
	/*
	 * NB: the order of writes is important here.  In case the
	 * chip is scanning the ring because of an outstanding request
	 * it might nab this one too.  In that case we need to make
	 * sure the setup is complete before we write the length
	 * field of the descriptor as it signals the descriptor is
	 * ready for processing.
	 */
	re->re_desc.d_csr = SAFE_PE_CSR_READY | SAFE_PE_CSR_SAPCI;
	if (maccrd)
		re->re_desc.d_csr |= SAFE_PE_CSR_LOADSA | SAFE_PE_CSR_HASHFINAL;
	re->re_desc.d_len = oplen
			  | SAFE_PE_LEN_READY
			  | (bypass << SAFE_PE_LEN_BYPASS_S)
			  ;

	safestats.st_ipackets++;
	safestats.st_ibytes += oplen;

	if (++(sc->sc_front) == sc->sc_ringtop)
		sc->sc_front = sc->sc_ring;

	/* XXX honor batching */
	safe_feed(sc, re);
	mtx_unlock(&sc->sc_ringmtx);
	return (0);

errout:
	if ((re->re_dst_m != NULL) && (re->re_src_m != re->re_dst_m))
		m_freem(re->re_dst_m);

	if (re->re_dst_map != NULL && re->re_dst_map != re->re_src_map) {
		bus_dmamap_unload(sc->sc_dstdmat, re->re_dst_map);
		bus_dmamap_destroy(sc->sc_dstdmat, re->re_dst_map);
	}
	if (re->re_src_map != NULL) {
		bus_dmamap_unload(sc->sc_srcdmat, re->re_src_map);
		bus_dmamap_destroy(sc->sc_srcdmat, re->re_src_map);
	}
	mtx_unlock(&sc->sc_ringmtx);
	if (err != ERESTART) {
		crp->crp_etype = err;
		crypto_done(crp);
	} else {
		sc->sc_needwakeup |= CRYPTO_SYMQ;
	}
	return (err);
}

static void
safe_callback(struct safe_softc *sc, struct safe_ringentry *re)
{
	struct cryptop *crp = (struct cryptop *)re->re_crp;
	struct safe_session *ses;
	struct cryptodesc *crd;

	ses = crypto_get_driver_session(crp->crp_session);

	safestats.st_opackets++;
	safestats.st_obytes += re->re_dst.mapsize;

	safe_dma_sync(&sc->sc_ringalloc,
		BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	if (re->re_desc.d_csr & SAFE_PE_CSR_STATUS) {
		device_printf(sc->sc_dev, "csr 0x%x cmd0 0x%x cmd1 0x%x\n",
			re->re_desc.d_csr,
			re->re_sa.sa_cmd0, re->re_sa.sa_cmd1);
		safestats.st_peoperr++;
		crp->crp_etype = EIO;		/* something more meaningful? */
	}
	if (re->re_dst_map != NULL && re->re_dst_map != re->re_src_map) {
		bus_dmamap_sync(sc->sc_dstdmat, re->re_dst_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dstdmat, re->re_dst_map);
		bus_dmamap_destroy(sc->sc_dstdmat, re->re_dst_map);
	}
	bus_dmamap_sync(sc->sc_srcdmat, re->re_src_map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_srcdmat, re->re_src_map);
	bus_dmamap_destroy(sc->sc_srcdmat, re->re_src_map);

	/* 
	 * If result was written to a differet mbuf chain, swap
	 * it in as the return value and reclaim the original.
	 */
	if ((crp->crp_flags & CRYPTO_F_IMBUF) && re->re_src_m != re->re_dst_m) {
		m_freem(re->re_src_m);
		crp->crp_buf = (caddr_t)re->re_dst_m;
	}

	if (re->re_flags & SAFE_QFLAGS_COPYOUTIV) {
		/* copy out IV for future use */
		for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
			int ivsize;

			if (crd->crd_alg == CRYPTO_DES_CBC ||
			    crd->crd_alg == CRYPTO_3DES_CBC) {
				ivsize = 2*sizeof(u_int32_t);
			} else if (crd->crd_alg == CRYPTO_AES_CBC) {
				ivsize = 4*sizeof(u_int32_t);
			} else
				continue;
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crd->crd_skip + crd->crd_len - ivsize, ivsize,
			    (caddr_t)ses->ses_iv);
			break;
		}
	}

	if (re->re_flags & SAFE_QFLAGS_COPYOUTICV) {
		/* copy out ICV result */
		for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
			if (!(crd->crd_alg == CRYPTO_MD5_HMAC ||
			    crd->crd_alg == CRYPTO_SHA1_HMAC ||
			    crd->crd_alg == CRYPTO_NULL_HMAC))
				continue;
			if (crd->crd_alg == CRYPTO_SHA1_HMAC) {
				/*
				 * SHA-1 ICV's are byte-swapped; fix 'em up
				 * before copy them to their destination.
				 */
				re->re_sastate.sa_saved_indigest[0] =
				    bswap32(re->re_sastate.sa_saved_indigest[0]);
				re->re_sastate.sa_saved_indigest[1] =
				    bswap32(re->re_sastate.sa_saved_indigest[1]);
				re->re_sastate.sa_saved_indigest[2] =
				    bswap32(re->re_sastate.sa_saved_indigest[2]);
			}
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, ses->ses_mlen,
			    (caddr_t)re->re_sastate.sa_saved_indigest);
			break;
		}
	}
	crypto_done(crp);
}

/*
 * Copy all data past offset from srcm to dstm.
 */
static void
safe_mcopy(struct mbuf *srcm, struct mbuf *dstm, u_int offset)
{
	u_int j, dlen, slen;
	caddr_t dptr, sptr;

	/*
	 * Advance src and dst to offset.
	 */
	j = offset;
	while (j >= srcm->m_len) {
		j -= srcm->m_len;
		srcm = srcm->m_next;
		if (srcm == NULL)
			return;
	}
	sptr = mtod(srcm, caddr_t) + j;
	slen = srcm->m_len - j;

	j = offset;
	while (j >= dstm->m_len) {
		j -= dstm->m_len;
		dstm = dstm->m_next;
		if (dstm == NULL)
			return;
	}
	dptr = mtod(dstm, caddr_t) + j;
	dlen = dstm->m_len - j;

	/*
	 * Copy everything that remains.
	 */
	for (;;) {
		j = min(slen, dlen);
		bcopy(sptr, dptr, j);
		if (slen == j) {
			srcm = srcm->m_next;
			if (srcm == NULL)
				return;
			sptr = srcm->m_data;
			slen = srcm->m_len;
		} else
			sptr += j, slen -= j;
		if (dlen == j) {
			dstm = dstm->m_next;
			if (dstm == NULL)
				return;
			dptr = dstm->m_data;
			dlen = dstm->m_len;
		} else
			dptr += j, dlen -= j;
	}
}

#ifndef SAFE_NO_RNG
#define	SAFE_RNG_MAXWAIT	1000

static void
safe_rng_init(struct safe_softc *sc)
{
	u_int32_t w, v;
	int i;

	WRITE_REG(sc, SAFE_RNG_CTRL, 0);
	/* use default value according to the manual */
	WRITE_REG(sc, SAFE_RNG_CNFG, 0x834);	/* magic from SafeNet */
	WRITE_REG(sc, SAFE_RNG_ALM_CNT, 0);

	/*
	 * There is a bug in rev 1.0 of the 1140 that when the RNG
	 * is brought out of reset the ready status flag does not
	 * work until the RNG has finished its internal initialization.
	 *
	 * So in order to determine the device is through its
	 * initialization we must read the data register, using the
	 * status reg in the read in case it is initialized.  Then read
	 * the data register until it changes from the first read.
	 * Once it changes read the data register until it changes
	 * again.  At this time the RNG is considered initialized. 
	 * This could take between 750ms - 1000ms in time.
	 */
	i = 0;
	w = READ_REG(sc, SAFE_RNG_OUT);
	do {
		v = READ_REG(sc, SAFE_RNG_OUT);
		if (v != w) {
			w = v;
			break;
		}
		DELAY(10);
	} while (++i < SAFE_RNG_MAXWAIT);

	/* Wait Until data changes again */
	i = 0;
	do {
		v = READ_REG(sc, SAFE_RNG_OUT);
		if (v != w)
			break;
		DELAY(10);
	} while (++i < SAFE_RNG_MAXWAIT);
}

static __inline void
safe_rng_disable_short_cycle(struct safe_softc *sc)
{
	WRITE_REG(sc, SAFE_RNG_CTRL,
		READ_REG(sc, SAFE_RNG_CTRL) &~ SAFE_RNG_CTRL_SHORTEN);
}

static __inline void
safe_rng_enable_short_cycle(struct safe_softc *sc)
{
	WRITE_REG(sc, SAFE_RNG_CTRL, 
		READ_REG(sc, SAFE_RNG_CTRL) | SAFE_RNG_CTRL_SHORTEN);
}

static __inline u_int32_t
safe_rng_read(struct safe_softc *sc)
{
	int i;

	i = 0;
	while (READ_REG(sc, SAFE_RNG_STAT) != 0 && ++i < SAFE_RNG_MAXWAIT)
		;
	return READ_REG(sc, SAFE_RNG_OUT);
}

static void
safe_rng(void *arg)
{
	struct safe_softc *sc = arg;
	u_int32_t buf[SAFE_RNG_MAXBUFSIZ];	/* NB: maybe move to softc */
	u_int maxwords;
	int i;

	safestats.st_rng++;
	/*
	 * Fetch the next block of data.
	 */
	maxwords = safe_rngbufsize;
	if (maxwords > SAFE_RNG_MAXBUFSIZ)
		maxwords = SAFE_RNG_MAXBUFSIZ;
retry:
	for (i = 0; i < maxwords; i++)
		buf[i] = safe_rng_read(sc);
	/*
	 * Check the comparator alarm count and reset the h/w if
	 * it exceeds our threshold.  This guards against the
	 * hardware oscillators resonating with external signals.
	 */
	if (READ_REG(sc, SAFE_RNG_ALM_CNT) > safe_rngmaxalarm) {
		u_int32_t freq_inc, w;

		DPRINTF(("%s: alarm count %u exceeds threshold %u\n", __func__,
			READ_REG(sc, SAFE_RNG_ALM_CNT), safe_rngmaxalarm));
		safestats.st_rngalarm++;
		safe_rng_enable_short_cycle(sc);
		freq_inc = 18;
		for (i = 0; i < 64; i++) {
			w = READ_REG(sc, SAFE_RNG_CNFG);
			freq_inc = ((w + freq_inc) & 0x3fL);
			w = ((w & ~0x3fL) | freq_inc);
			WRITE_REG(sc, SAFE_RNG_CNFG, w);

			WRITE_REG(sc, SAFE_RNG_ALM_CNT, 0);

			(void) safe_rng_read(sc);
			DELAY(25);

			if (READ_REG(sc, SAFE_RNG_ALM_CNT) == 0) {
				safe_rng_disable_short_cycle(sc);
				goto retry;
			}
			freq_inc = 1;
		}
		safe_rng_disable_short_cycle(sc);
	} else
		WRITE_REG(sc, SAFE_RNG_ALM_CNT, 0);

	(*sc->sc_harvest)(sc->sc_rndtest, buf, maxwords*sizeof (u_int32_t));
	callout_reset(&sc->sc_rngto,
		hz * (safe_rnginterval ? safe_rnginterval : 1), safe_rng, sc);
}
#endif /* SAFE_NO_RNG */

static void
safe_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;
	*paddr = segs->ds_addr;
}

static int
safe_dma_malloc(
	struct safe_softc *sc,
	bus_size_t size,
	struct safe_dma_alloc *dma,
	int mapflags
)
{
	int r;

	r = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),	/* parent */
			       sizeof(u_int32_t), 0,	/* alignment, bounds */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       size,			/* maxsize */
			       1,			/* nsegments */
			       size,			/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL, NULL,		/* locking */
			       &dma->dma_tag);
	if (r != 0) {
		device_printf(sc->sc_dev, "safe_dma_malloc: "
			"bus_dma_tag_create failed; error %u\n", r);
		goto fail_0;
	}

	r = bus_dmamem_alloc(dma->dma_tag, (void**) &dma->dma_vaddr,
			     BUS_DMA_NOWAIT, &dma->dma_map);
	if (r != 0) {
		device_printf(sc->sc_dev, "safe_dma_malloc: "
			"bus_dmammem_alloc failed; size %ju, error %u\n",
			(uintmax_t)size, r);
		goto fail_1;
	}

	r = bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr,
		            size,
			    safe_dmamap_cb,
			    &dma->dma_paddr,
			    mapflags | BUS_DMA_NOWAIT);
	if (r != 0) {
		device_printf(sc->sc_dev, "safe_dma_malloc: "
			"bus_dmamap_load failed; error %u\n", r);
		goto fail_2;
	}

	dma->dma_size = size;
	return (0);

	bus_dmamap_unload(dma->dma_tag, dma->dma_map);
fail_2:
	bus_dmamem_free(dma->dma_tag, dma->dma_vaddr, dma->dma_map);
fail_1:
	bus_dma_tag_destroy(dma->dma_tag);
fail_0:
	dma->dma_tag = NULL;
	return (r);
}

static void
safe_dma_free(struct safe_softc *sc, struct safe_dma_alloc *dma)
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
safe_reset_board(struct safe_softc *sc)
{
	u_int32_t v;
	/*
	 * Reset the device.  The manual says no delay
	 * is needed between marking and clearing reset.
	 */
	v = READ_REG(sc, SAFE_PE_DMACFG) &~
		(SAFE_PE_DMACFG_PERESET | SAFE_PE_DMACFG_PDRRESET |
		 SAFE_PE_DMACFG_SGRESET);
	WRITE_REG(sc, SAFE_PE_DMACFG, v
				    | SAFE_PE_DMACFG_PERESET
				    | SAFE_PE_DMACFG_PDRRESET
				    | SAFE_PE_DMACFG_SGRESET);
	WRITE_REG(sc, SAFE_PE_DMACFG, v);
}

/*
 * Initialize registers we need to touch only once.
 */
static void
safe_init_board(struct safe_softc *sc)
{
	u_int32_t v, dwords;

	v = READ_REG(sc, SAFE_PE_DMACFG);
	v &=~ SAFE_PE_DMACFG_PEMODE;
	v |= SAFE_PE_DMACFG_FSENA		/* failsafe enable */
	  |  SAFE_PE_DMACFG_GPRPCI		/* gather ring on PCI */
	  |  SAFE_PE_DMACFG_SPRPCI		/* scatter ring on PCI */
	  |  SAFE_PE_DMACFG_ESDESC		/* endian-swap descriptors */
	  |  SAFE_PE_DMACFG_ESSA		/* endian-swap SA's */
	  |  SAFE_PE_DMACFG_ESPDESC		/* endian-swap part. desc's */
	  ;
	WRITE_REG(sc, SAFE_PE_DMACFG, v);
#if 0
	/* XXX select byte swap based on host byte order */
	WRITE_REG(sc, SAFE_ENDIAN, 0x1b);
#endif
	if (sc->sc_chiprev == SAFE_REV(1,0)) {
		/*
		 * Avoid large PCI DMA transfers.  Rev 1.0 has a bug where
		 * "target mode transfers" done while the chip is DMA'ing
		 * >1020 bytes cause the hardware to lockup.  To avoid this
		 * we reduce the max PCI transfer size and use small source
		 * particle descriptors (<= 256 bytes).
		 */
		WRITE_REG(sc, SAFE_DMA_CFG, 256);
		device_printf(sc->sc_dev,
			"Reduce max DMA size to %u words for rev %u.%u WAR\n",
			(READ_REG(sc, SAFE_DMA_CFG)>>2) & 0xff,
			SAFE_REV_MAJ(sc->sc_chiprev),
			SAFE_REV_MIN(sc->sc_chiprev));
	}

	/* NB: operands+results are overlaid */
	WRITE_REG(sc, SAFE_PE_PDRBASE, sc->sc_ringalloc.dma_paddr);
	WRITE_REG(sc, SAFE_PE_RDRBASE, sc->sc_ringalloc.dma_paddr);
	/*
	 * Configure ring entry size and number of items in the ring.
	 */
	KASSERT((sizeof(struct safe_ringentry) % sizeof(u_int32_t)) == 0,
		("PE ring entry not 32-bit aligned!"));
	dwords = sizeof(struct safe_ringentry) / sizeof(u_int32_t);
	WRITE_REG(sc, SAFE_PE_RINGCFG,
		(dwords << SAFE_PE_RINGCFG_OFFSET_S) | SAFE_MAX_NQUEUE);
	WRITE_REG(sc, SAFE_PE_RINGPOLL, 0);	/* disable polling */

	WRITE_REG(sc, SAFE_PE_GRNGBASE, sc->sc_spalloc.dma_paddr);
	WRITE_REG(sc, SAFE_PE_SRNGBASE, sc->sc_dpalloc.dma_paddr);
	WRITE_REG(sc, SAFE_PE_PARTSIZE,
		(SAFE_TOTAL_DPART<<16) | SAFE_TOTAL_SPART);
	/*
	 * NB: destination particles are fixed size.  We use
	 *     an mbuf cluster and require all results go to
	 *     clusters or smaller.
	 */
	WRITE_REG(sc, SAFE_PE_PARTCFG, SAFE_MAX_DSIZE);

	/* it's now safe to enable PE mode, do it */
	WRITE_REG(sc, SAFE_PE_DMACFG, v | SAFE_PE_DMACFG_PEMODE);

	/*
	 * Configure hardware to use level-triggered interrupts and
	 * to interrupt after each descriptor is processed.
	 */
	WRITE_REG(sc, SAFE_HI_CFG, SAFE_HI_CFG_LEVEL);
	WRITE_REG(sc, SAFE_HI_DESC_CNT, 1);
	WRITE_REG(sc, SAFE_HI_MASK, SAFE_INT_PE_DDONE | SAFE_INT_PE_ERROR);
}

/*
 * Init PCI registers
 */
static void
safe_init_pciregs(device_t dev)
{
}

/*
 * Clean up after a chip crash.
 * It is assumed that the caller in splimp()
 */
static void
safe_cleanchip(struct safe_softc *sc)
{

	if (sc->sc_nqchip != 0) {
		struct safe_ringentry *re = sc->sc_back;

		while (re != sc->sc_front) {
			if (re->re_desc.d_csr != 0)
				safe_free_entry(sc, re);
			if (++re == sc->sc_ringtop)
				re = sc->sc_ring;
		}
		sc->sc_back = re;
		sc->sc_nqchip = 0;
	}
}

/*
 * free a safe_q
 * It is assumed that the caller is within splimp().
 */
static int
safe_free_entry(struct safe_softc *sc, struct safe_ringentry *re)
{
	struct cryptop *crp;

	/*
	 * Free header MCR
	 */
	if ((re->re_dst_m != NULL) && (re->re_src_m != re->re_dst_m))
		m_freem(re->re_dst_m);

	crp = (struct cryptop *)re->re_crp;
	
	re->re_desc.d_csr = 0;
	
	crp->crp_etype = EFAULT;
	crypto_done(crp);
	return(0);
}

/*
 * Routine to reset the chip and clean up.
 * It is assumed that the caller is in splimp()
 */
static void
safe_totalreset(struct safe_softc *sc)
{
	safe_reset_board(sc);
	safe_init_board(sc);
	safe_cleanchip(sc);
}

/*
 * Is the operand suitable aligned for direct DMA.  Each
 * segment must be aligned on a 32-bit boundary and all
 * but the last segment must be a multiple of 4 bytes.
 */
static int
safe_dmamap_aligned(const struct safe_operand *op)
{
	int i;

	for (i = 0; i < op->nsegs; i++) {
		if (op->segs[i].ds_addr & 3)
			return (0);
		if (i != (op->nsegs - 1) && (op->segs[i].ds_len & 3))
			return (0);
	}
	return (1);
}

/*
 * Is the operand suitable for direct DMA as the destination
 * of an operation.  The hardware requires that each ``particle''
 * but the last in an operation result have the same size.  We
 * fix that size at SAFE_MAX_DSIZE bytes.  This routine returns
 * 0 if some segment is not a multiple of of this size, 1 if all
 * segments are exactly this size, or 2 if segments are at worst
 * a multple of this size.
 */
static int
safe_dmamap_uniform(const struct safe_operand *op)
{
	int result = 1;

	if (op->nsegs > 0) {
		int i;

		for (i = 0; i < op->nsegs-1; i++) {
			if (op->segs[i].ds_len % SAFE_MAX_DSIZE)
				return (0);
			if (op->segs[i].ds_len != SAFE_MAX_DSIZE)
				result = 2;
		}
	}
	return (result);
}

#ifdef SAFE_DEBUG
static void
safe_dump_dmastatus(struct safe_softc *sc, const char *tag)
{
	printf("%s: ENDIAN 0x%x SRC 0x%x DST 0x%x STAT 0x%x\n"
		, tag
		, READ_REG(sc, SAFE_DMA_ENDIAN)
		, READ_REG(sc, SAFE_DMA_SRCADDR)
		, READ_REG(sc, SAFE_DMA_DSTADDR)
		, READ_REG(sc, SAFE_DMA_STAT)
	);
}

static void
safe_dump_intrstate(struct safe_softc *sc, const char *tag)
{
	printf("%s: HI_CFG 0x%x HI_MASK 0x%x HI_DESC_CNT 0x%x HU_STAT 0x%x HM_STAT 0x%x\n"
		, tag
		, READ_REG(sc, SAFE_HI_CFG)
		, READ_REG(sc, SAFE_HI_MASK)
		, READ_REG(sc, SAFE_HI_DESC_CNT)
		, READ_REG(sc, SAFE_HU_STAT)
		, READ_REG(sc, SAFE_HM_STAT)
	);
}

static void
safe_dump_ringstate(struct safe_softc *sc, const char *tag)
{
	u_int32_t estat = READ_REG(sc, SAFE_PE_ERNGSTAT);

	/* NB: assume caller has lock on ring */
	printf("%s: ERNGSTAT %x (next %u) back %lu front %lu\n",
		tag,
		estat, (estat >> SAFE_PE_ERNGSTAT_NEXT_S),
		(unsigned long)(sc->sc_back - sc->sc_ring),
		(unsigned long)(sc->sc_front - sc->sc_ring));
}

static void
safe_dump_request(struct safe_softc *sc, const char* tag, struct safe_ringentry *re)
{
	int ix, nsegs;

	ix = re - sc->sc_ring;
	printf("%s: %p (%u): csr %x src %x dst %x sa %x len %x\n"
		, tag
		, re, ix
		, re->re_desc.d_csr
		, re->re_desc.d_src
		, re->re_desc.d_dst
		, re->re_desc.d_sa
		, re->re_desc.d_len
	);
	if (re->re_src.nsegs > 1) {
		ix = (re->re_desc.d_src - sc->sc_spalloc.dma_paddr) /
			sizeof(struct safe_pdesc);
		for (nsegs = re->re_src.nsegs; nsegs; nsegs--) {
			printf(" spd[%u] %p: %p size %u flags %x"
				, ix, &sc->sc_spring[ix]
				, (caddr_t)(uintptr_t) sc->sc_spring[ix].pd_addr
				, sc->sc_spring[ix].pd_size
				, sc->sc_spring[ix].pd_flags
			);
			if (sc->sc_spring[ix].pd_size == 0)
				printf(" (zero!)");
			printf("\n");
			if (++ix == SAFE_TOTAL_SPART)
				ix = 0;
		}
	}
	if (re->re_dst.nsegs > 1) {
		ix = (re->re_desc.d_dst - sc->sc_dpalloc.dma_paddr) /
			sizeof(struct safe_pdesc);
		for (nsegs = re->re_dst.nsegs; nsegs; nsegs--) {
			printf(" dpd[%u] %p: %p flags %x\n"
				, ix, &sc->sc_dpring[ix]
				, (caddr_t)(uintptr_t) sc->sc_dpring[ix].pd_addr
				, sc->sc_dpring[ix].pd_flags
			);
			if (++ix == SAFE_TOTAL_DPART)
				ix = 0;
		}
	}
	printf("sa: cmd0 %08x cmd1 %08x staterec %x\n",
		re->re_sa.sa_cmd0, re->re_sa.sa_cmd1, re->re_sa.sa_staterec);
	printf("sa: key %x %x %x %x %x %x %x %x\n"
		, re->re_sa.sa_key[0]
		, re->re_sa.sa_key[1]
		, re->re_sa.sa_key[2]
		, re->re_sa.sa_key[3]
		, re->re_sa.sa_key[4]
		, re->re_sa.sa_key[5]
		, re->re_sa.sa_key[6]
		, re->re_sa.sa_key[7]
	);
	printf("sa: indigest %x %x %x %x %x\n"
		, re->re_sa.sa_indigest[0]
		, re->re_sa.sa_indigest[1]
		, re->re_sa.sa_indigest[2]
		, re->re_sa.sa_indigest[3]
		, re->re_sa.sa_indigest[4]
	);
	printf("sa: outdigest %x %x %x %x %x\n"
		, re->re_sa.sa_outdigest[0]
		, re->re_sa.sa_outdigest[1]
		, re->re_sa.sa_outdigest[2]
		, re->re_sa.sa_outdigest[3]
		, re->re_sa.sa_outdigest[4]
	);
	printf("sr: iv %x %x %x %x\n"
		, re->re_sastate.sa_saved_iv[0]
		, re->re_sastate.sa_saved_iv[1]
		, re->re_sastate.sa_saved_iv[2]
		, re->re_sastate.sa_saved_iv[3]
	);
	printf("sr: hashbc %u indigest %x %x %x %x %x\n"
		, re->re_sastate.sa_saved_hashbc
		, re->re_sastate.sa_saved_indigest[0]
		, re->re_sastate.sa_saved_indigest[1]
		, re->re_sastate.sa_saved_indigest[2]
		, re->re_sastate.sa_saved_indigest[3]
		, re->re_sastate.sa_saved_indigest[4]
	);
}

static void
safe_dump_ring(struct safe_softc *sc, const char *tag)
{
	mtx_lock(&sc->sc_ringmtx);
	printf("\nSafeNet Ring State:\n");
	safe_dump_intrstate(sc, tag);
	safe_dump_dmastatus(sc, tag);
	safe_dump_ringstate(sc, tag);
	if (sc->sc_nqchip) {
		struct safe_ringentry *re = sc->sc_back;
		do {
			safe_dump_request(sc, tag, re);
			if (++re == sc->sc_ringtop)
				re = sc->sc_ring;
		} while (re != sc->sc_front);
	}
	mtx_unlock(&sc->sc_ringmtx);
}

static int
sysctl_hw_safe_dump(SYSCTL_HANDLER_ARGS)
{
	char dmode[64];
	int error;

	strncpy(dmode, "", sizeof(dmode) - 1);
	dmode[sizeof(dmode) - 1] = '\0';
	error = sysctl_handle_string(oidp, &dmode[0], sizeof(dmode), req);

	if (error == 0 && req->newptr != NULL) {
		struct safe_softc *sc = safec;

		if (!sc)
			return EINVAL;
		if (strncmp(dmode, "dma", 3) == 0)
			safe_dump_dmastatus(sc, "safe0");
		else if (strncmp(dmode, "int", 3) == 0)
			safe_dump_intrstate(sc, "safe0");
		else if (strncmp(dmode, "ring", 4) == 0)
			safe_dump_ring(sc, "safe0");
		else
			return EINVAL;
	}
	return error;
}
SYSCTL_PROC(_hw_safe, OID_AUTO, dump, CTLTYPE_STRING | CTLFLAG_RW,
	0, 0, sysctl_hw_safe_dump, "A", "Dump driver state");
#endif /* SAFE_DEBUG */
