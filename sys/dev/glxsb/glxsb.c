/* $OpenBSD: glxsb.c,v 1.7 2007/02/12 14:31:45 tom Exp $ */

/*
 * Copyright (c) 2006 Tom Cosgrove <tom@openbsd.org>
 * Copyright (c) 2003, 2004 Theo de Raadt
 * Copyright (c) 2003 Jason Wright
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for the security block on the AMD Geode LX processors
 * http://www.amd.com/files/connectivitysolutions/geode/geode_lx/33234d_lx_ds.pdf
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/rman.h>
#include <sys/rwlock.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/cpufunc.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/cryptosoft.h>
#include <opencrypto/xform.h>

#include "cryptodev_if.h"
#include "glxsb.h"

#define PCI_VENDOR_AMD			0x1022	/* AMD */
#define PCI_PRODUCT_AMD_GEODE_LX_CRYPTO	0x2082	/* Geode LX Crypto */

#define SB_GLD_MSR_CAP		0x58002000	/* RO - Capabilities */
#define SB_GLD_MSR_CONFIG	0x58002001	/* RW - Master Config */
#define SB_GLD_MSR_SMI		0x58002002	/* RW - SMI */
#define SB_GLD_MSR_ERROR	0x58002003	/* RW - Error */
#define SB_GLD_MSR_PM		0x58002004	/* RW - Power Mgmt */
#define SB_GLD_MSR_DIAG		0x58002005	/* RW - Diagnostic */
#define SB_GLD_MSR_CTRL		0x58002006	/* RW - Security Block Cntrl */

						/* For GLD_MSR_CTRL: */
#define SB_GMC_DIV0		0x0000		/* AES update divisor values */
#define SB_GMC_DIV1		0x0001
#define SB_GMC_DIV2		0x0002
#define SB_GMC_DIV3		0x0003
#define SB_GMC_DIV_MASK		0x0003
#define SB_GMC_SBI		0x0004		/* AES swap bits */
#define SB_GMC_SBY		0x0008		/* AES swap bytes */
#define SB_GMC_TW		0x0010		/* Time write (EEPROM) */
#define SB_GMC_T_SEL0		0x0000		/* RNG post-proc: none */
#define SB_GMC_T_SEL1		0x0100		/* RNG post-proc: LFSR */
#define SB_GMC_T_SEL2		0x0200		/* RNG post-proc: whitener */
#define SB_GMC_T_SEL3		0x0300		/* RNG LFSR+whitener */
#define SB_GMC_T_SEL_MASK	0x0300
#define SB_GMC_T_NE		0x0400		/* Noise (generator) Enable */
#define SB_GMC_T_TM		0x0800		/* RNG test mode */
						/*     (deterministic) */

/* Security Block configuration/control registers (offsets from base) */
#define SB_CTL_A		0x0000		/* RW - SB Control A */
#define SB_CTL_B		0x0004		/* RW - SB Control B */
#define SB_AES_INT		0x0008		/* RW - SB AES Interrupt */
#define SB_SOURCE_A		0x0010		/* RW - Source A */
#define SB_DEST_A		0x0014		/* RW - Destination A */
#define SB_LENGTH_A		0x0018		/* RW - Length A */
#define SB_SOURCE_B		0x0020		/* RW - Source B */
#define SB_DEST_B		0x0024		/* RW - Destination B */
#define SB_LENGTH_B		0x0028		/* RW - Length B */
#define SB_WKEY			0x0030		/* WO - Writable Key 0-3 */
#define SB_WKEY_0		0x0030		/* WO - Writable Key 0 */
#define SB_WKEY_1		0x0034		/* WO - Writable Key 1 */
#define SB_WKEY_2		0x0038		/* WO - Writable Key 2 */
#define SB_WKEY_3		0x003C		/* WO - Writable Key 3 */
#define SB_CBC_IV		0x0040		/* RW - CBC IV 0-3 */
#define SB_CBC_IV_0		0x0040		/* RW - CBC IV 0 */
#define SB_CBC_IV_1		0x0044		/* RW - CBC IV 1 */
#define SB_CBC_IV_2		0x0048		/* RW - CBC IV 2 */
#define SB_CBC_IV_3		0x004C		/* RW - CBC IV 3 */
#define SB_RANDOM_NUM		0x0050		/* RW - Random Number */
#define SB_RANDOM_NUM_STATUS	0x0054		/* RW - Random Number Status */
#define SB_EEPROM_COMM		0x0800		/* RW - EEPROM Command */
#define SB_EEPROM_ADDR		0x0804		/* RW - EEPROM Address */
#define SB_EEPROM_DATA		0x0808		/* RW - EEPROM Data */
#define SB_EEPROM_SEC_STATE	0x080C		/* RW - EEPROM Security State */

						/* For SB_CTL_A and _B */
#define SB_CTL_ST		0x0001		/* Start operation (enc/dec) */
#define SB_CTL_ENC		0x0002		/* Encrypt (0 is decrypt) */
#define SB_CTL_DEC		0x0000		/* Decrypt */
#define SB_CTL_WK		0x0004		/* Use writable key (we set) */
#define SB_CTL_DC		0x0008		/* Destination coherent */
#define SB_CTL_SC		0x0010		/* Source coherent */
#define SB_CTL_CBC		0x0020		/* CBC (0 is ECB) */

						/* For SB_AES_INT */
#define SB_AI_DISABLE_AES_A	0x0001		/* Disable AES A compl int */
#define SB_AI_ENABLE_AES_A	0x0000		/* Enable AES A compl int */
#define SB_AI_DISABLE_AES_B	0x0002		/* Disable AES B compl int */
#define SB_AI_ENABLE_AES_B	0x0000		/* Enable AES B compl int */
#define SB_AI_DISABLE_EEPROM	0x0004		/* Disable EEPROM op comp int */
#define SB_AI_ENABLE_EEPROM	0x0000		/* Enable EEPROM op compl int */
#define SB_AI_AES_A_COMPLETE   0x10000		/* AES A operation complete */
#define SB_AI_AES_B_COMPLETE   0x20000		/* AES B operation complete */
#define SB_AI_EEPROM_COMPLETE  0x40000		/* EEPROM operation complete */

#define SB_AI_CLEAR_INTR \
	(SB_AI_DISABLE_AES_A | SB_AI_DISABLE_AES_B |\
	SB_AI_DISABLE_EEPROM | SB_AI_AES_A_COMPLETE |\
	SB_AI_AES_B_COMPLETE | SB_AI_EEPROM_COMPLETE)

#define SB_RNS_TRNG_VALID	0x0001		/* in SB_RANDOM_NUM_STATUS */

#define SB_MEM_SIZE		0x0810		/* Size of memory block */

#define SB_AES_ALIGN		0x0010		/* Source and dest buffers */
						/* must be 16-byte aligned */
#define SB_AES_BLOCK_SIZE	0x0010

/*
 * The Geode LX security block AES acceleration doesn't perform scatter-
 * gather: it just takes source and destination addresses.  Therefore the
 * plain- and ciphertexts need to be contiguous.  To this end, we allocate
 * a buffer for both, and accept the overhead of copying in and out.  If
 * the number of bytes in one operation is bigger than allowed for by the
 * buffer (buffer is twice the size of the max length, as it has both input
 * and output) then we have to perform multiple encryptions/decryptions.
 */

#define GLXSB_MAX_AES_LEN	16384

MALLOC_DEFINE(M_GLXSB, "glxsb_data", "Glxsb Data");

struct glxsb_dma_map {
	bus_dmamap_t		dma_map;	/* DMA map */
	bus_dma_segment_t	dma_seg;	/* segments */
	int			dma_nsegs;	/* #segments */
	int			dma_size;	/* size */
	caddr_t			dma_vaddr;	/* virtual address */
	bus_addr_t		dma_paddr;	/* physical address */
};

struct glxsb_taskop {
	struct glxsb_session	*to_ses;	/* crypto session */
	struct cryptop		*to_crp;	/* cryptop to perfom */
	struct cryptodesc	*to_enccrd;	/* enccrd to perform */
	struct cryptodesc	*to_maccrd;	/* maccrd to perform */
};

struct glxsb_softc {
	device_t		sc_dev;		/* device backpointer */
	struct resource		*sc_sr;		/* resource */
	int			sc_rid;		/* resource rid */
	struct callout		sc_rngco;	/* RNG callout */
	int			sc_rnghz;	/* RNG callout ticks */
	bus_dma_tag_t		sc_dmat;	/* DMA tag */
	struct glxsb_dma_map	sc_dma;		/* DMA map */
	int32_t			sc_cid;		/* crypto tag */
	struct mtx		sc_task_mtx;	/* task mutex */
	struct taskqueue	*sc_tq;		/* task queue */
	struct task		sc_cryptotask;	/* task */
	struct glxsb_taskop	sc_to;		/* task's crypto operation */
	int			sc_task_count;	/* tasks count */
};

static int glxsb_probe(device_t);
static int glxsb_attach(device_t);
static int glxsb_detach(device_t);

static void glxsb_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int  glxsb_dma_alloc(struct glxsb_softc *);
static void glxsb_dma_pre_op(struct glxsb_softc *, struct glxsb_dma_map *);
static void glxsb_dma_post_op(struct glxsb_softc *, struct glxsb_dma_map *);
static void glxsb_dma_free(struct glxsb_softc *, struct glxsb_dma_map *);

static void glxsb_rnd(void *);
static int  glxsb_crypto_setup(struct glxsb_softc *);
static int  glxsb_crypto_newsession(device_t, crypto_session_t, struct cryptoini *);
static void glxsb_crypto_freesession(device_t, crypto_session_t);
static int  glxsb_aes(struct glxsb_softc *, uint32_t, uint32_t,
	uint32_t, void *, int, void *);

static int  glxsb_crypto_encdec(struct cryptop *, struct cryptodesc *,
	struct glxsb_session *, struct glxsb_softc *);

static void glxsb_crypto_task(void *, int);
static int  glxsb_crypto_process(device_t, struct cryptop *, int);

static device_method_t glxsb_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		glxsb_probe),
	DEVMETHOD(device_attach,	glxsb_attach),
	DEVMETHOD(device_detach,	glxsb_detach),

	/* crypto device methods */
	DEVMETHOD(cryptodev_newsession,		glxsb_crypto_newsession),
	DEVMETHOD(cryptodev_freesession,	glxsb_crypto_freesession),
	DEVMETHOD(cryptodev_process,		glxsb_crypto_process),

	{0,0}
};

static driver_t glxsb_driver = {
	"glxsb",
	glxsb_methods,
	sizeof(struct glxsb_softc)
};

static devclass_t glxsb_devclass;

DRIVER_MODULE(glxsb, pci, glxsb_driver, glxsb_devclass, 0, 0);
MODULE_VERSION(glxsb, 1);
MODULE_DEPEND(glxsb, crypto, 1, 1, 1);

static int
glxsb_probe(device_t dev)
{

	if (pci_get_vendor(dev) == PCI_VENDOR_AMD &&
	    pci_get_device(dev) == PCI_PRODUCT_AMD_GEODE_LX_CRYPTO) {
		device_set_desc(dev,
		    "AMD Geode LX Security Block (AES-128-CBC, RNG)");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
glxsb_attach(device_t dev)
{
	struct glxsb_softc *sc = device_get_softc(dev);
	uint64_t msr;

	sc->sc_dev = dev;
	msr = rdmsr(SB_GLD_MSR_CAP);

	if ((msr & 0xFFFF00) != 0x130400) {
		device_printf(dev, "unknown ID 0x%x\n",
		    (int)((msr & 0xFFFF00) >> 16));
		return (ENXIO);
	}

	pci_enable_busmaster(dev);

	/* Map in the security block configuration/control registers */
	sc->sc_rid = PCIR_BAR(0);
	sc->sc_sr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rid,
	    RF_ACTIVE);
	if (sc->sc_sr == NULL) {
		device_printf(dev, "cannot map register space\n");
		return (ENXIO);
	}

	/*
	 * Configure the Security Block.
	 *
	 * We want to enable the noise generator (T_NE), and enable the
	 * linear feedback shift register and whitener post-processing
	 * (T_SEL = 3).  Also ensure that test mode (deterministic values)
	 * is disabled.
	 */
	msr = rdmsr(SB_GLD_MSR_CTRL);
	msr &= ~(SB_GMC_T_TM | SB_GMC_T_SEL_MASK);
	msr |= SB_GMC_T_NE | SB_GMC_T_SEL3;
#if 0
	msr |= SB_GMC_SBI | SB_GMC_SBY;		/* for AES, if necessary */
#endif
	wrmsr(SB_GLD_MSR_CTRL, msr);

	/* Disable interrupts */
	bus_write_4(sc->sc_sr, SB_AES_INT, SB_AI_CLEAR_INTR);

	/* Allocate a contiguous DMA-able buffer to work in */
	if (glxsb_dma_alloc(sc) != 0)
		goto fail0;

	/* Initialize our task queue */
	sc->sc_tq = taskqueue_create("glxsb_taskq", M_NOWAIT | M_ZERO,
	    taskqueue_thread_enqueue, &sc->sc_tq);
	if (sc->sc_tq == NULL) {
		device_printf(dev, "cannot create task queue\n");
		goto fail0;
	}
	if (taskqueue_start_threads(&sc->sc_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(dev)) != 0) {
		device_printf(dev, "cannot start task queue\n");
		goto fail1;
	}
	TASK_INIT(&sc->sc_cryptotask, 0, glxsb_crypto_task, sc);

	/* Initialize crypto */
	if (glxsb_crypto_setup(sc) != 0)
		goto fail1;

	/* Install a periodic collector for the "true" (AMD's word) RNG */
	if (hz > 100)
		sc->sc_rnghz = hz / 100;
	else
		sc->sc_rnghz = 1;
	callout_init(&sc->sc_rngco, 1);
	glxsb_rnd(sc);

	return (0);

fail1:
	taskqueue_free(sc->sc_tq);
fail0:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid, sc->sc_sr);
	return (ENXIO);
}

static int
glxsb_detach(device_t dev)
{
	struct glxsb_softc *sc = device_get_softc(dev);

	crypto_unregister_all(sc->sc_cid);

	callout_drain(&sc->sc_rngco);
	taskqueue_drain(sc->sc_tq, &sc->sc_cryptotask);
	bus_generic_detach(dev);
	glxsb_dma_free(sc, &sc->sc_dma);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rid, sc->sc_sr);
	taskqueue_free(sc->sc_tq);
	mtx_destroy(&sc->sc_task_mtx);
	return (0);
}

/*
 *	callback for bus_dmamap_load()
 */
static void
glxsb_dmamap_cb(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{

	bus_addr_t *paddr = (bus_addr_t*) arg;
	*paddr = seg[0].ds_addr;
}

static int
glxsb_dma_alloc(struct glxsb_softc *sc)
{
	struct glxsb_dma_map *dma = &sc->sc_dma;
	int rc;

	dma->dma_nsegs = 1;
	dma->dma_size = GLXSB_MAX_AES_LEN * 2;

	/* Setup DMA descriptor area */
	rc = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),	/* parent */
				SB_AES_ALIGN, 0,	/* alignments, bounds */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				dma->dma_size,		/* maxsize */
				dma->dma_nsegs,		/* nsegments */
				dma->dma_size,		/* maxsegsize */
				BUS_DMA_ALLOCNOW,	/* flags */
				NULL, NULL,		/* lockfunc, lockarg */
				&sc->sc_dmat);
	if (rc != 0) {
		device_printf(sc->sc_dev,
		    "cannot allocate DMA tag (%d)\n", rc);
		return (rc);
	}

	rc = bus_dmamem_alloc(sc->sc_dmat, (void **)&dma->dma_vaddr,
	    BUS_DMA_NOWAIT, &dma->dma_map);
	if (rc != 0) {
		device_printf(sc->sc_dev,
		    "cannot allocate DMA memory of %d bytes (%d)\n",
			dma->dma_size, rc);
		goto fail0;
	}

	rc = bus_dmamap_load(sc->sc_dmat, dma->dma_map, dma->dma_vaddr,
	    dma->dma_size, glxsb_dmamap_cb, &dma->dma_paddr, BUS_DMA_NOWAIT);
	if (rc != 0) {
		device_printf(sc->sc_dev,
		    "cannot load DMA memory for %d bytes (%d)\n",
		   dma->dma_size, rc);
		goto fail1;
	}

	return (0);

fail1:
	bus_dmamem_free(sc->sc_dmat, dma->dma_vaddr, dma->dma_map);
fail0:
	bus_dma_tag_destroy(sc->sc_dmat);
	return (rc);
}

static void
glxsb_dma_pre_op(struct glxsb_softc *sc, struct glxsb_dma_map *dma)
{

	bus_dmamap_sync(sc->sc_dmat, dma->dma_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
glxsb_dma_post_op(struct glxsb_softc *sc, struct glxsb_dma_map *dma)
{

	bus_dmamap_sync(sc->sc_dmat, dma->dma_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
}

static void
glxsb_dma_free(struct glxsb_softc *sc, struct glxsb_dma_map *dma)
{

	bus_dmamap_unload(sc->sc_dmat, dma->dma_map);
	bus_dmamem_free(sc->sc_dmat, dma->dma_vaddr, dma->dma_map);
	bus_dma_tag_destroy(sc->sc_dmat);
}

static void
glxsb_rnd(void *v)
{
	struct glxsb_softc *sc = v;
	uint32_t status, value;

	status = bus_read_4(sc->sc_sr, SB_RANDOM_NUM_STATUS);
	if (status & SB_RNS_TRNG_VALID) {
		value = bus_read_4(sc->sc_sr, SB_RANDOM_NUM);
		/* feed with one uint32 */
		/* MarkM: FIX!! Check that this does not swamp the harvester! */
		random_harvest_queue(&value, sizeof(value), RANDOM_PURE_GLXSB);
	}

	callout_reset(&sc->sc_rngco, sc->sc_rnghz, glxsb_rnd, sc);
}

static int
glxsb_crypto_setup(struct glxsb_softc *sc)
{

	sc->sc_cid = crypto_get_driverid(sc->sc_dev,
	    sizeof(struct glxsb_session), CRYPTOCAP_F_HARDWARE);

	if (sc->sc_cid < 0) {
		device_printf(sc->sc_dev, "cannot get crypto driver id\n");
		return (ENOMEM);
	}

	mtx_init(&sc->sc_task_mtx, "glxsb_crypto_mtx", NULL, MTX_DEF);

	if (crypto_register(sc->sc_cid, CRYPTO_AES_CBC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_NULL_HMAC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_RIPEMD160_HMAC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_SHA2_256_HMAC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_SHA2_384_HMAC, 0, 0) != 0)
		goto crypto_fail;
	if (crypto_register(sc->sc_cid, CRYPTO_SHA2_512_HMAC, 0, 0) != 0)
		goto crypto_fail;

	return (0);

crypto_fail:
	device_printf(sc->sc_dev, "cannot register crypto\n");
	crypto_unregister_all(sc->sc_cid);
	mtx_destroy(&sc->sc_task_mtx);
	return (ENOMEM);
}

static int
glxsb_crypto_newsession(device_t dev, crypto_session_t cses,
    struct cryptoini *cri)
{
	struct glxsb_softc *sc = device_get_softc(dev);
	struct glxsb_session *ses;
	struct cryptoini *encini, *macini;
	int error;

	if (sc == NULL || cri == NULL)
		return (EINVAL);

	encini = macini = NULL;
	for (; cri != NULL; cri = cri->cri_next) {
		switch(cri->cri_alg) {
		case CRYPTO_NULL_HMAC:
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if (macini != NULL)
				return (EINVAL);
			macini = cri;
			break;
		case CRYPTO_AES_CBC:
			if (encini != NULL)
				return (EINVAL);
			encini = cri;
			break;
		default:
			return (EINVAL);
		}
	}

	/*
	 * We only support HMAC algorithms to be able to work with
	 * ipsec(4), so if we are asked only for authentication without
	 * encryption, don't pretend we can accellerate it.
	 */
	if (encini == NULL)
		return (EINVAL);

	ses = crypto_get_driver_session(cses);
	if (encini->cri_alg == CRYPTO_AES_CBC) {
		if (encini->cri_klen != 128) {
			glxsb_crypto_freesession(sc->sc_dev, cses);
			return (EINVAL);
		}
		arc4rand(ses->ses_iv, sizeof(ses->ses_iv), 0);
		ses->ses_klen = encini->cri_klen;

		/* Copy the key (Geode LX wants the primary key only) */
		bcopy(encini->cri_key, ses->ses_key, sizeof(ses->ses_key));
	}

	if (macini != NULL) {
		error = glxsb_hash_setup(ses, macini);
		if (error != 0) {
			glxsb_crypto_freesession(sc->sc_dev, cses);
			return (error);
		}
	}

	return (0);
}

static void
glxsb_crypto_freesession(device_t dev, crypto_session_t cses)
{
	struct glxsb_softc *sc = device_get_softc(dev);
	struct glxsb_session *ses;

	if (sc == NULL)
		return;

	ses = crypto_get_driver_session(cses);
	glxsb_hash_free(ses);
}

static int
glxsb_aes(struct glxsb_softc *sc, uint32_t control, uint32_t psrc,
    uint32_t pdst, void *key, int len, void *iv)
{
	uint32_t status;
	int i;

	if (len & 0xF) {
		device_printf(sc->sc_dev,
		    "len must be a multiple of 16 (not %d)\n", len);
		return (EINVAL);
	}

	/* Set the source */
	bus_write_4(sc->sc_sr, SB_SOURCE_A, psrc);

	/* Set the destination address */
	bus_write_4(sc->sc_sr, SB_DEST_A, pdst);

	/* Set the data length */
	bus_write_4(sc->sc_sr, SB_LENGTH_A, len);

	/* Set the IV */
	if (iv != NULL) {
		bus_write_region_4(sc->sc_sr, SB_CBC_IV, iv, 4);
		control |= SB_CTL_CBC;
	}

	/* Set the key */
	bus_write_region_4(sc->sc_sr, SB_WKEY, key, 4);

	/* Ask the security block to do it */
	bus_write_4(sc->sc_sr, SB_CTL_A,
	    control | SB_CTL_WK | SB_CTL_DC | SB_CTL_SC | SB_CTL_ST);

	/*
	 * Now wait until it is done.
	 *
	 * We do a busy wait.  Obviously the number of iterations of
	 * the loop required to perform the AES operation depends upon
	 * the number of bytes to process.
	 *
	 * On a 500 MHz Geode LX we see
	 *
	 *	length (bytes)	typical max iterations
	 *	    16		   12
	 *	    64		   22
	 *	   256		   59
	 *	  1024		  212
	 *	  8192		1,537
	 *
	 * Since we have a maximum size of operation defined in
	 * GLXSB_MAX_AES_LEN, we use this constant to decide how long
	 * to wait.  Allow an order of magnitude longer than it should
	 * really take, just in case.
	 */

	for (i = 0; i < GLXSB_MAX_AES_LEN * 10; i++) {
		status = bus_read_4(sc->sc_sr, SB_CTL_A);
		if ((status & SB_CTL_ST) == 0)		/* Done */
			return (0);
	}

	device_printf(sc->sc_dev, "operation failed to complete\n");
	return (EIO);
}

static int
glxsb_crypto_encdec(struct cryptop *crp, struct cryptodesc *crd,
    struct glxsb_session *ses, struct glxsb_softc *sc)
{
	char *op_src, *op_dst;
	uint32_t op_psrc, op_pdst;
	uint8_t op_iv[SB_AES_BLOCK_SIZE], *piv;
	int error;
	int len, tlen, xlen;
	int offset;
	uint32_t control;

	if (crd == NULL || (crd->crd_len % SB_AES_BLOCK_SIZE) != 0)
		return (EINVAL);

	/* How much of our buffer will we need to use? */
	xlen = crd->crd_len > GLXSB_MAX_AES_LEN ?
	    GLXSB_MAX_AES_LEN : crd->crd_len;

	/*
	 * XXX Check if we can have input == output on Geode LX.
	 * XXX In the meantime, use two separate (adjacent) buffers.
	 */
	op_src = sc->sc_dma.dma_vaddr;
	op_dst = (char *)sc->sc_dma.dma_vaddr + xlen;

	op_psrc = sc->sc_dma.dma_paddr;
	op_pdst = sc->sc_dma.dma_paddr + xlen;

	if (crd->crd_flags & CRD_F_ENCRYPT) {
		control = SB_CTL_ENC;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, op_iv, sizeof(op_iv));
		else
			bcopy(ses->ses_iv, op_iv, sizeof(op_iv));

		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0) {
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, sizeof(op_iv), op_iv);
		}
	} else {
		control = SB_CTL_DEC;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, op_iv, sizeof(op_iv));
		else {
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, sizeof(op_iv), op_iv);
		}
	}

	offset = 0;
	tlen = crd->crd_len;
	piv = op_iv;

	/* Process the data in GLXSB_MAX_AES_LEN chunks */
	while (tlen > 0) {
		len = (tlen > GLXSB_MAX_AES_LEN) ? GLXSB_MAX_AES_LEN : tlen;
		crypto_copydata(crp->crp_flags, crp->crp_buf,
		    crd->crd_skip + offset, len, op_src);

		glxsb_dma_pre_op(sc, &sc->sc_dma);

		error = glxsb_aes(sc, control, op_psrc, op_pdst, ses->ses_key,
		    len, op_iv);

		glxsb_dma_post_op(sc, &sc->sc_dma);
		if (error != 0)
			return (error);

		crypto_copyback(crp->crp_flags, crp->crp_buf,
		    crd->crd_skip + offset, len, op_dst);

		offset += len;
		tlen -= len;

		if (tlen <= 0) {	/* Ideally, just == 0 */
			/* Finished - put the IV in session IV */
			piv = ses->ses_iv;
		}

		/*
		 * Copy out last block for use as next iteration/session IV.
		 *
		 * piv is set to op_iv[] before the loop starts, but is
		 * set to ses->ses_iv if we're going to exit the loop this
		 * time.
		 */
		if (crd->crd_flags & CRD_F_ENCRYPT)
			bcopy(op_dst + len - sizeof(op_iv), piv, sizeof(op_iv));
		else {
			/* Decryption, only need this if another iteration */
			if (tlen > 0) {
				bcopy(op_src + len - sizeof(op_iv), piv,
				    sizeof(op_iv));
			}
		}
	} /* while */

	/* All AES processing has now been done. */
	bzero(sc->sc_dma.dma_vaddr, xlen * 2);

	return (0);
}

static void
glxsb_crypto_task(void *arg, int pending)
{
	struct glxsb_softc *sc = arg;
	struct glxsb_session *ses;
	struct cryptop *crp;
	struct cryptodesc *enccrd, *maccrd;
	int error;

	maccrd = sc->sc_to.to_maccrd;
	enccrd = sc->sc_to.to_enccrd;
	crp = sc->sc_to.to_crp;
	ses = sc->sc_to.to_ses;

	/* Perform data authentication if requested before encryption */
	if (maccrd != NULL && maccrd->crd_next == enccrd) {
		error = glxsb_hash_process(ses, maccrd, crp);
		if (error != 0)
			goto out;
	}

	error = glxsb_crypto_encdec(crp, enccrd, ses, sc);
	if (error != 0)
		goto out;

	/* Perform data authentication if requested after encryption */
	if (maccrd != NULL && enccrd->crd_next == maccrd) {
		error = glxsb_hash_process(ses, maccrd, crp);
		if (error != 0)
			goto out;
	}
out:
	mtx_lock(&sc->sc_task_mtx);
	sc->sc_task_count--;
	mtx_unlock(&sc->sc_task_mtx);

	crp->crp_etype = error;
	crypto_unblock(sc->sc_cid, CRYPTO_SYMQ);
	crypto_done(crp);
}

static int
glxsb_crypto_process(device_t dev, struct cryptop *crp, int hint)
{
	struct glxsb_softc *sc = device_get_softc(dev);
	struct glxsb_session *ses;
	struct cryptodesc *crd, *enccrd, *maccrd;
	int error = 0;

	enccrd = maccrd = NULL;

	/* Sanity check. */
	if (crp == NULL)
		return (EINVAL);

	if (crp->crp_callback == NULL || crp->crp_desc == NULL) {
		error = EINVAL;
		goto fail;
	}

	for (crd = crp->crp_desc; crd != NULL; crd = crd->crd_next) {
		switch (crd->crd_alg) {
		case CRYPTO_NULL_HMAC:
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if (maccrd != NULL) {
				error = EINVAL;
				goto fail;
			}
			maccrd = crd;
			break;
		case CRYPTO_AES_CBC:
			if (enccrd != NULL) {
				error = EINVAL;
				goto fail;
			}
			enccrd = crd;
			break;
		default:
			error = EINVAL;
			goto fail;
		}
	}

	if (enccrd == NULL || enccrd->crd_len % AES_BLOCK_LEN != 0) {
		error = EINVAL;
		goto fail;
	}

	ses = crypto_get_driver_session(crp->crp_session);

	mtx_lock(&sc->sc_task_mtx);
	if (sc->sc_task_count != 0) {
		mtx_unlock(&sc->sc_task_mtx);
		return (ERESTART);
	}
	sc->sc_task_count++;

	sc->sc_to.to_maccrd = maccrd;
	sc->sc_to.to_enccrd = enccrd;
	sc->sc_to.to_crp = crp;
	sc->sc_to.to_ses = ses;
	mtx_unlock(&sc->sc_task_mtx);

	taskqueue_enqueue(sc->sc_tq, &sc->sc_cryptotask);
	return(0);

fail:
	crp->crp_etype = error;
	crypto_done(crp);
	return (error);
}
