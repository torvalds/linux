/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2011 Semihalf.
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

/*
 * CESA SRAM Memory Map:
 *
 * +------------------------+ <= sc->sc_sram_base_va + CESA_SRAM_SIZE
 * |                        |
 * |          DATA          |
 * |                        |
 * +------------------------+ <= sc->sc_sram_base_va + CESA_DATA(0)
 * |  struct cesa_sa_data   |
 * +------------------------+
 * |  struct cesa_sa_hdesc  |
 * +------------------------+ <= sc->sc_sram_base_va
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/resource.h>
#include <machine/fdt.h>

#include <dev/fdt/simplebus.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <sys/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha256.h>
#include <crypto/rijndael/rijndael.h>
#include <opencrypto/cryptodev.h>
#include "cryptodev_if.h"

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include "cesa.h"

static int	cesa_probe(device_t);
static int	cesa_attach(device_t);
static int	cesa_attach_late(device_t);
static int	cesa_detach(device_t);
static void	cesa_intr(void *);
static int	cesa_newsession(device_t, crypto_session_t, struct cryptoini *);
static int	cesa_process(device_t, struct cryptop *, int);

static struct resource_spec cesa_res_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_MEMORY, 1, RF_ACTIVE },
	{ SYS_RES_IRQ, 0, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static device_method_t cesa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cesa_probe),
	DEVMETHOD(device_attach,	cesa_attach),
	DEVMETHOD(device_detach,	cesa_detach),

	/* Crypto device methods */
	DEVMETHOD(cryptodev_newsession,	cesa_newsession),
	DEVMETHOD(cryptodev_process,	cesa_process),

	DEVMETHOD_END
};

static driver_t cesa_driver = {
	"cesa",
	cesa_methods,
	sizeof (struct cesa_softc)
};
static devclass_t cesa_devclass;

DRIVER_MODULE(cesa, simplebus, cesa_driver, cesa_devclass, 0, 0);
MODULE_DEPEND(cesa, crypto, 1, 1, 1);

static void
cesa_dump_cshd(struct cesa_softc *sc, struct cesa_sa_hdesc *cshd)
{
#ifdef DEBUG
	device_t dev;

	dev = sc->sc_dev;
	device_printf(dev, "CESA SA Hardware Descriptor:\n");
	device_printf(dev, "\t\tconfig: 0x%08X\n", cshd->cshd_config);
	device_printf(dev, "\t\te_src:  0x%08X\n", cshd->cshd_enc_src);
	device_printf(dev, "\t\te_dst:  0x%08X\n", cshd->cshd_enc_dst);
	device_printf(dev, "\t\te_dlen: 0x%08X\n", cshd->cshd_enc_dlen);
	device_printf(dev, "\t\te_key:  0x%08X\n", cshd->cshd_enc_key);
	device_printf(dev, "\t\te_iv_1: 0x%08X\n", cshd->cshd_enc_iv);
	device_printf(dev, "\t\te_iv_2: 0x%08X\n", cshd->cshd_enc_iv_buf);
	device_printf(dev, "\t\tm_src:  0x%08X\n", cshd->cshd_mac_src);
	device_printf(dev, "\t\tm_dst:  0x%08X\n", cshd->cshd_mac_dst);
	device_printf(dev, "\t\tm_dlen: 0x%08X\n", cshd->cshd_mac_dlen);
	device_printf(dev, "\t\tm_tlen: 0x%08X\n", cshd->cshd_mac_total_dlen);
	device_printf(dev, "\t\tm_iv_i: 0x%08X\n", cshd->cshd_mac_iv_in);
	device_printf(dev, "\t\tm_iv_o: 0x%08X\n", cshd->cshd_mac_iv_out);
#endif
}

static void
cesa_alloc_dma_mem_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct cesa_dma_mem *cdm;

	if (error)
		return;

	KASSERT(nseg == 1, ("Got wrong number of DMA segments, should be 1."));
	cdm = arg;
	cdm->cdm_paddr = segs->ds_addr;
}

static int
cesa_alloc_dma_mem(struct cesa_softc *sc, struct cesa_dma_mem *cdm,
    bus_size_t size)
{
	int error;

	KASSERT(cdm->cdm_vaddr == NULL,
	    ("%s(): DMA memory descriptor in use.", __func__));

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),	/* parent */
	    PAGE_SIZE, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    size, 1,				/* maxsize, nsegments */
	    size, 0,				/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &cdm->cdm_tag);			/* dmat */
	if (error) {
		device_printf(sc->sc_dev, "failed to allocate busdma tag, error"
		    " %i!\n", error);

		goto err1;
	}

	error = bus_dmamem_alloc(cdm->cdm_tag, &cdm->cdm_vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &cdm->cdm_map);
	if (error) {
		device_printf(sc->sc_dev, "failed to allocate DMA safe"
		    " memory, error %i!\n", error);

		goto err2;
	}

	error = bus_dmamap_load(cdm->cdm_tag, cdm->cdm_map, cdm->cdm_vaddr,
	    size, cesa_alloc_dma_mem_cb, cdm, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(sc->sc_dev, "cannot get address of the DMA"
		    " memory, error %i\n", error);

		goto err3;
	}

	return (0);
err3:
	bus_dmamem_free(cdm->cdm_tag, cdm->cdm_vaddr, cdm->cdm_map);
err2:
	bus_dma_tag_destroy(cdm->cdm_tag);
err1:
	cdm->cdm_vaddr = NULL;
	return (error);
}

static void
cesa_free_dma_mem(struct cesa_dma_mem *cdm)
{

	bus_dmamap_unload(cdm->cdm_tag, cdm->cdm_map);
	bus_dmamem_free(cdm->cdm_tag, cdm->cdm_vaddr, cdm->cdm_map);
	bus_dma_tag_destroy(cdm->cdm_tag);
	cdm->cdm_vaddr = NULL;
}

static void
cesa_sync_dma_mem(struct cesa_dma_mem *cdm, bus_dmasync_op_t op)
{

	/* Sync only if dma memory is valid */
        if (cdm->cdm_vaddr != NULL)
		bus_dmamap_sync(cdm->cdm_tag, cdm->cdm_map, op);
}

static void
cesa_sync_desc(struct cesa_softc *sc, bus_dmasync_op_t op)
{

	cesa_sync_dma_mem(&sc->sc_tdesc_cdm, op);
	cesa_sync_dma_mem(&sc->sc_sdesc_cdm, op);
	cesa_sync_dma_mem(&sc->sc_requests_cdm, op);
}

static struct cesa_request *
cesa_alloc_request(struct cesa_softc *sc)
{
	struct cesa_request *cr;

	CESA_GENERIC_ALLOC_LOCKED(sc, cr, requests);
	if (!cr)
		return (NULL);

	STAILQ_INIT(&cr->cr_tdesc);
	STAILQ_INIT(&cr->cr_sdesc);

	return (cr);
}

static void
cesa_free_request(struct cesa_softc *sc, struct cesa_request *cr)
{

	/* Free TDMA descriptors assigned to this request */
	CESA_LOCK(sc, tdesc);
	STAILQ_CONCAT(&sc->sc_free_tdesc, &cr->cr_tdesc);
	CESA_UNLOCK(sc, tdesc);

	/* Free SA descriptors assigned to this request */
	CESA_LOCK(sc, sdesc);
	STAILQ_CONCAT(&sc->sc_free_sdesc, &cr->cr_sdesc);
	CESA_UNLOCK(sc, sdesc);

	/* Unload DMA memory associated with request */
	if (cr->cr_dmap_loaded) {
		bus_dmamap_unload(sc->sc_data_dtag, cr->cr_dmap);
		cr->cr_dmap_loaded = 0;
	}

	CESA_GENERIC_FREE_LOCKED(sc, cr, requests);
}

static void
cesa_enqueue_request(struct cesa_softc *sc, struct cesa_request *cr)
{

	CESA_LOCK(sc, requests);
	STAILQ_INSERT_TAIL(&sc->sc_ready_requests, cr, cr_stq);
	CESA_UNLOCK(sc, requests);
}

static struct cesa_tdma_desc *
cesa_alloc_tdesc(struct cesa_softc *sc)
{
	struct cesa_tdma_desc *ctd;

	CESA_GENERIC_ALLOC_LOCKED(sc, ctd, tdesc);

	if (!ctd)
		device_printf(sc->sc_dev, "TDMA descriptors pool exhaused. "
		    "Consider increasing CESA_TDMA_DESCRIPTORS.\n");

	return (ctd);
}

static struct cesa_sa_desc *
cesa_alloc_sdesc(struct cesa_softc *sc, struct cesa_request *cr)
{
	struct cesa_sa_desc *csd;

	CESA_GENERIC_ALLOC_LOCKED(sc, csd, sdesc);
	if (!csd) {
		device_printf(sc->sc_dev, "SA descriptors pool exhaused. "
		    "Consider increasing CESA_SA_DESCRIPTORS.\n");
		return (NULL);
	}

	STAILQ_INSERT_TAIL(&cr->cr_sdesc, csd, csd_stq);

	/* Fill-in SA descriptor with default values */
	csd->csd_cshd->cshd_enc_key = CESA_SA_DATA(csd_key);
	csd->csd_cshd->cshd_enc_iv = CESA_SA_DATA(csd_iv);
	csd->csd_cshd->cshd_enc_iv_buf = CESA_SA_DATA(csd_iv);
	csd->csd_cshd->cshd_enc_src = 0;
	csd->csd_cshd->cshd_enc_dst = 0;
	csd->csd_cshd->cshd_enc_dlen = 0;
	csd->csd_cshd->cshd_mac_dst = CESA_SA_DATA(csd_hash);
	csd->csd_cshd->cshd_mac_iv_in = CESA_SA_DATA(csd_hiv_in);
	csd->csd_cshd->cshd_mac_iv_out = CESA_SA_DATA(csd_hiv_out);
	csd->csd_cshd->cshd_mac_src = 0;
	csd->csd_cshd->cshd_mac_dlen = 0;

	return (csd);
}

static struct cesa_tdma_desc *
cesa_tdma_copy(struct cesa_softc *sc, bus_addr_t dst, bus_addr_t src,
    bus_size_t size)
{
	struct cesa_tdma_desc *ctd;

	ctd = cesa_alloc_tdesc(sc);
	if (!ctd)
		return (NULL);

	ctd->ctd_cthd->cthd_dst = dst;
	ctd->ctd_cthd->cthd_src = src;
	ctd->ctd_cthd->cthd_byte_count = size;

	/* Handle special control packet */
	if (size != 0)
		ctd->ctd_cthd->cthd_flags = CESA_CTHD_OWNED;
	else
		ctd->ctd_cthd->cthd_flags = 0;

	return (ctd);
}

static struct cesa_tdma_desc *
cesa_tdma_copyin_sa_data(struct cesa_softc *sc, struct cesa_request *cr)
{

	return (cesa_tdma_copy(sc, sc->sc_sram_base_pa +
	    sizeof(struct cesa_sa_hdesc), cr->cr_csd_paddr,
	    sizeof(struct cesa_sa_data)));
}

static struct cesa_tdma_desc *
cesa_tdma_copyout_sa_data(struct cesa_softc *sc, struct cesa_request *cr)
{

	return (cesa_tdma_copy(sc, cr->cr_csd_paddr, sc->sc_sram_base_pa +
	    sizeof(struct cesa_sa_hdesc), sizeof(struct cesa_sa_data)));
}

static struct cesa_tdma_desc *
cesa_tdma_copy_sdesc(struct cesa_softc *sc, struct cesa_sa_desc *csd)
{

	return (cesa_tdma_copy(sc, sc->sc_sram_base_pa, csd->csd_cshd_paddr,
	    sizeof(struct cesa_sa_hdesc)));
}

static void
cesa_append_tdesc(struct cesa_request *cr, struct cesa_tdma_desc *ctd)
{
	struct cesa_tdma_desc *ctd_prev;

	if (!STAILQ_EMPTY(&cr->cr_tdesc)) {
		ctd_prev = STAILQ_LAST(&cr->cr_tdesc, cesa_tdma_desc, ctd_stq);
		ctd_prev->ctd_cthd->cthd_next = ctd->ctd_cthd_paddr;
	}

	ctd->ctd_cthd->cthd_next = 0;
	STAILQ_INSERT_TAIL(&cr->cr_tdesc, ctd, ctd_stq);
}

static int
cesa_append_packet(struct cesa_softc *sc, struct cesa_request *cr,
    struct cesa_packet *cp, struct cesa_sa_desc *csd)
{
	struct cesa_tdma_desc *ctd, *tmp;

	/* Copy SA descriptor for this packet */
	ctd = cesa_tdma_copy_sdesc(sc, csd);
	if (!ctd)
		return (ENOMEM);

	cesa_append_tdesc(cr, ctd);

	/* Copy data to be processed */
	STAILQ_FOREACH_SAFE(ctd, &cp->cp_copyin, ctd_stq, tmp)
		cesa_append_tdesc(cr, ctd);
	STAILQ_INIT(&cp->cp_copyin);

	/* Insert control descriptor */
	ctd = cesa_tdma_copy(sc, 0, 0, 0);
	if (!ctd)
		return (ENOMEM);

	cesa_append_tdesc(cr, ctd);

	/* Copy back results */
	STAILQ_FOREACH_SAFE(ctd, &cp->cp_copyout, ctd_stq, tmp)
		cesa_append_tdesc(cr, ctd);
	STAILQ_INIT(&cp->cp_copyout);

	return (0);
}

static int
cesa_set_mkey(struct cesa_session *cs, int alg, const uint8_t *mkey, int mklen)
{
	uint8_t ipad[CESA_MAX_HMAC_BLOCK_LEN];
	uint8_t opad[CESA_MAX_HMAC_BLOCK_LEN];
	SHA1_CTX sha1ctx;
	SHA256_CTX sha256ctx;
	MD5_CTX md5ctx;
	uint32_t *hout;
	uint32_t *hin;
	int i;

	memset(ipad, HMAC_IPAD_VAL, CESA_MAX_HMAC_BLOCK_LEN);
	memset(opad, HMAC_OPAD_VAL, CESA_MAX_HMAC_BLOCK_LEN);
	for (i = 0; i < mklen; i++) {
		ipad[i] ^= mkey[i];
		opad[i] ^= mkey[i];
	}

	hin = (uint32_t *)cs->cs_hiv_in;
	hout = (uint32_t *)cs->cs_hiv_out;

	switch (alg) {
	case CRYPTO_MD5_HMAC:
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, ipad, MD5_BLOCK_LEN);
		memcpy(hin, md5ctx.state, sizeof(md5ctx.state));
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, opad, MD5_BLOCK_LEN);
		memcpy(hout, md5ctx.state, sizeof(md5ctx.state));
		break;
	case CRYPTO_SHA1_HMAC:
		SHA1Init(&sha1ctx);
		SHA1Update(&sha1ctx, ipad, SHA1_BLOCK_LEN);
		memcpy(hin, sha1ctx.h.b32, sizeof(sha1ctx.h.b32));
		SHA1Init(&sha1ctx);
		SHA1Update(&sha1ctx, opad, SHA1_BLOCK_LEN);
		memcpy(hout, sha1ctx.h.b32, sizeof(sha1ctx.h.b32));
		break;
	case CRYPTO_SHA2_256_HMAC:
		SHA256_Init(&sha256ctx);
		SHA256_Update(&sha256ctx, ipad, SHA2_256_BLOCK_LEN);
		memcpy(hin, sha256ctx.state, sizeof(sha256ctx.state));
		SHA256_Init(&sha256ctx);
		SHA256_Update(&sha256ctx, opad, SHA2_256_BLOCK_LEN);
		memcpy(hout, sha256ctx.state, sizeof(sha256ctx.state));
		break;
	default:
		return (EINVAL);
	}

	for (i = 0; i < CESA_MAX_HASH_LEN / sizeof(uint32_t); i++) {
		hin[i] = htobe32(hin[i]);
		hout[i] = htobe32(hout[i]);
	}

	return (0);
}

static int
cesa_prep_aes_key(struct cesa_session *cs)
{
	uint32_t ek[4 * (RIJNDAEL_MAXNR + 1)];
	uint32_t *dkey;
	int i;

	rijndaelKeySetupEnc(ek, cs->cs_key, cs->cs_klen * 8);

	cs->cs_config &= ~CESA_CSH_AES_KLEN_MASK;
	dkey = (uint32_t *)cs->cs_aes_dkey;

	switch (cs->cs_klen) {
	case 16:
		cs->cs_config |= CESA_CSH_AES_KLEN_128;
		for (i = 0; i < 4; i++)
			*dkey++ = htobe32(ek[4 * 10 + i]);
		break;
	case 24:
		cs->cs_config |= CESA_CSH_AES_KLEN_192;
		for (i = 0; i < 4; i++)
			*dkey++ = htobe32(ek[4 * 12 + i]);
		for (i = 0; i < 2; i++)
			*dkey++ = htobe32(ek[4 * 11 + 2 + i]);
		break;
	case 32:
		cs->cs_config |= CESA_CSH_AES_KLEN_256;
		for (i = 0; i < 4; i++)
			*dkey++ = htobe32(ek[4 * 14 + i]);
		for (i = 0; i < 4; i++)
			*dkey++ = htobe32(ek[4 * 13 + i]);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
cesa_is_hash(int alg)
{

	switch (alg) {
	case CRYPTO_MD5:
	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_256_HMAC:
		return (1);
	default:
		return (0);
	}
}

static void
cesa_start_packet(struct cesa_packet *cp, unsigned int size)
{

	cp->cp_size = size;
	cp->cp_offset = 0;
	STAILQ_INIT(&cp->cp_copyin);
	STAILQ_INIT(&cp->cp_copyout);
}

static int
cesa_fill_packet(struct cesa_softc *sc, struct cesa_packet *cp,
    bus_dma_segment_t *seg)
{
	struct cesa_tdma_desc *ctd;
	unsigned int bsize;

	/* Calculate size of block copy */
	bsize = MIN(seg->ds_len, cp->cp_size - cp->cp_offset);

	if (bsize > 0) {
		ctd = cesa_tdma_copy(sc, sc->sc_sram_base_pa +
		    CESA_DATA(cp->cp_offset), seg->ds_addr, bsize);
		if (!ctd)
			return (-ENOMEM);

		STAILQ_INSERT_TAIL(&cp->cp_copyin, ctd, ctd_stq);

		ctd = cesa_tdma_copy(sc, seg->ds_addr, sc->sc_sram_base_pa +
		    CESA_DATA(cp->cp_offset), bsize);
		if (!ctd)
			return (-ENOMEM);

		STAILQ_INSERT_TAIL(&cp->cp_copyout, ctd, ctd_stq);

		seg->ds_len -= bsize;
		seg->ds_addr += bsize;
		cp->cp_offset += bsize;
	}

	return (bsize);
}

static void
cesa_create_chain_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	unsigned int mpsize, fragmented;
	unsigned int mlen, mskip, tmlen;
	struct cesa_chain_info *cci;
	unsigned int elen, eskip;
	unsigned int skip, len;
	struct cesa_sa_desc *csd;
	struct cesa_request *cr;
	struct cesa_softc *sc;
	struct cesa_packet cp;
	bus_dma_segment_t seg;
	uint32_t config;
	int size;

	cci = arg;
	sc = cci->cci_sc;
	cr = cci->cci_cr;

	if (error) {
		cci->cci_error = error;
		return;
	}

	elen = cci->cci_enc ? cci->cci_enc->crd_len : 0;
	eskip = cci->cci_enc ? cci->cci_enc->crd_skip : 0;
	mlen = cci->cci_mac ? cci->cci_mac->crd_len : 0;
	mskip = cci->cci_mac ? cci->cci_mac->crd_skip : 0;

	if (elen && mlen &&
	    ((eskip > mskip && ((eskip - mskip) & (cr->cr_cs->cs_ivlen - 1))) ||
	    (mskip > eskip && ((mskip - eskip) & (cr->cr_cs->cs_mblen - 1))) ||
	    (eskip > (mskip + mlen)) || (mskip > (eskip + elen)))) {
		/*
		 * Data alignment in the request does not meet CESA requiremnts
		 * for combined encryption/decryption and hashing. We have to
		 * split the request to separate operations and process them
		 * one by one.
		 */
		config = cci->cci_config;
		if ((config & CESA_CSHD_OP_MASK) == CESA_CSHD_MAC_AND_ENC) {
			config &= ~CESA_CSHD_OP_MASK;

			cci->cci_config = config | CESA_CSHD_MAC;
			cci->cci_enc = NULL;
			cci->cci_mac = cr->cr_mac;
			cesa_create_chain_cb(cci, segs, nseg, cci->cci_error);

			cci->cci_config = config | CESA_CSHD_ENC;
			cci->cci_enc = cr->cr_enc;
			cci->cci_mac = NULL;
			cesa_create_chain_cb(cci, segs, nseg, cci->cci_error);
		} else {
			config &= ~CESA_CSHD_OP_MASK;

			cci->cci_config = config | CESA_CSHD_ENC;
			cci->cci_enc = cr->cr_enc;
			cci->cci_mac = NULL;
			cesa_create_chain_cb(cci, segs, nseg, cci->cci_error);

			cci->cci_config = config | CESA_CSHD_MAC;
			cci->cci_enc = NULL;
			cci->cci_mac = cr->cr_mac;
			cesa_create_chain_cb(cci, segs, nseg, cci->cci_error);
		}

		return;
	}

	tmlen = mlen;
	fragmented = 0;
	mpsize = CESA_MAX_PACKET_SIZE;
	mpsize &= ~((cr->cr_cs->cs_ivlen - 1) | (cr->cr_cs->cs_mblen - 1));

	if (elen && mlen) {
		skip = MIN(eskip, mskip);
		len = MAX(elen + eskip, mlen + mskip) - skip;
	} else if (elen) {
		skip = eskip;
		len = elen;
	} else {
		skip = mskip;
		len = mlen;
	}

	/* Start first packet in chain */
	cesa_start_packet(&cp, MIN(mpsize, len));

	while (nseg-- && len > 0) {
		seg = *(segs++);

		/*
		 * Skip data in buffer on which neither ENC nor MAC operation
		 * is requested.
		 */
		if (skip > 0) {
			size = MIN(skip, seg.ds_len);
			skip -= size;

			seg.ds_addr += size;
			seg.ds_len -= size;

			if (eskip > 0)
				eskip -= size;

			if (mskip > 0)
				mskip -= size;

			if (seg.ds_len == 0)
				continue;
		}

		while (1) {
			/*
			 * Fill in current packet with data. Break if there is
			 * no more data in current DMA segment or an error
			 * occurred.
			 */
			size = cesa_fill_packet(sc, &cp, &seg);
			if (size <= 0) {
				error = -size;
				break;
			}

			len -= size;

			/* If packet is full, append it to the chain */
			if (cp.cp_size == cp.cp_offset) {
				csd = cesa_alloc_sdesc(sc, cr);
				if (!csd) {
					error = ENOMEM;
					break;
				}

				/* Create SA descriptor for this packet */
				csd->csd_cshd->cshd_config = cci->cci_config;
				csd->csd_cshd->cshd_mac_total_dlen = tmlen;

				/*
				 * Enable fragmentation if request will not fit
				 * into one packet.
				 */
				if (len > 0) {
					if (!fragmented) {
						fragmented = 1;
						csd->csd_cshd->cshd_config |=
						    CESA_CSHD_FRAG_FIRST;
					} else
						csd->csd_cshd->cshd_config |=
						    CESA_CSHD_FRAG_MIDDLE;
				} else if (fragmented)
					csd->csd_cshd->cshd_config |=
					    CESA_CSHD_FRAG_LAST;

				if (eskip < cp.cp_size && elen > 0) {
					csd->csd_cshd->cshd_enc_src =
					    CESA_DATA(eskip);
					csd->csd_cshd->cshd_enc_dst =
					    CESA_DATA(eskip);
					csd->csd_cshd->cshd_enc_dlen =
					    MIN(elen, cp.cp_size - eskip);
				}

				if (mskip < cp.cp_size && mlen > 0) {
					csd->csd_cshd->cshd_mac_src =
					    CESA_DATA(mskip);
					csd->csd_cshd->cshd_mac_dlen =
					    MIN(mlen, cp.cp_size - mskip);
				}

				elen -= csd->csd_cshd->cshd_enc_dlen;
				eskip -= MIN(eskip, cp.cp_size);
				mlen -= csd->csd_cshd->cshd_mac_dlen;
				mskip -= MIN(mskip, cp.cp_size);

				cesa_dump_cshd(sc, csd->csd_cshd);

				/* Append packet to the request */
				error = cesa_append_packet(sc, cr, &cp, csd);
				if (error)
					break;

				/* Start a new packet, as current is full */
				cesa_start_packet(&cp, MIN(mpsize, len));
			}
		}

		if (error)
			break;
	}

	if (error) {
		/*
		 * Move all allocated resources to the request. They will be
		 * freed later.
		 */
		STAILQ_CONCAT(&cr->cr_tdesc, &cp.cp_copyin);
		STAILQ_CONCAT(&cr->cr_tdesc, &cp.cp_copyout);
		cci->cci_error = error;
	}
}

static void
cesa_create_chain_cb2(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t size, int error)
{

	cesa_create_chain_cb(arg, segs, nseg, error);
}

static int
cesa_create_chain(struct cesa_softc *sc, struct cesa_request *cr)
{
	struct cesa_chain_info cci;
	struct cesa_tdma_desc *ctd;
	uint32_t config;
	int error;

	error = 0;
	CESA_LOCK_ASSERT(sc, sessions);

	/* Create request metadata */
	if (cr->cr_enc) {
		if (cr->cr_enc->crd_alg == CRYPTO_AES_CBC &&
		    (cr->cr_enc->crd_flags & CRD_F_ENCRYPT) == 0)
			memcpy(cr->cr_csd->csd_key, cr->cr_cs->cs_aes_dkey,
			    cr->cr_cs->cs_klen);
		else
			memcpy(cr->cr_csd->csd_key, cr->cr_cs->cs_key,
			    cr->cr_cs->cs_klen);
	}

	if (cr->cr_mac) {
		memcpy(cr->cr_csd->csd_hiv_in, cr->cr_cs->cs_hiv_in,
		    CESA_MAX_HASH_LEN);
		memcpy(cr->cr_csd->csd_hiv_out, cr->cr_cs->cs_hiv_out,
		    CESA_MAX_HASH_LEN);
	}

	ctd = cesa_tdma_copyin_sa_data(sc, cr);
	if (!ctd)
		return (ENOMEM);

	cesa_append_tdesc(cr, ctd);

	/* Prepare SA configuration */
	config = cr->cr_cs->cs_config;

	if (cr->cr_enc && (cr->cr_enc->crd_flags & CRD_F_ENCRYPT) == 0)
		config |= CESA_CSHD_DECRYPT;
	if (cr->cr_enc && !cr->cr_mac)
		config |= CESA_CSHD_ENC;
	if (!cr->cr_enc && cr->cr_mac)
		config |= CESA_CSHD_MAC;
	if (cr->cr_enc && cr->cr_mac)
		config |= (config & CESA_CSHD_DECRYPT) ? CESA_CSHD_MAC_AND_ENC :
		    CESA_CSHD_ENC_AND_MAC;

	/* Create data packets */
	cci.cci_sc = sc;
	cci.cci_cr = cr;
	cci.cci_enc = cr->cr_enc;
	cci.cci_mac = cr->cr_mac;
	cci.cci_config = config;
	cci.cci_error = 0;

	if (cr->cr_crp->crp_flags & CRYPTO_F_IOV)
		error = bus_dmamap_load_uio(sc->sc_data_dtag,
		    cr->cr_dmap, (struct uio *)cr->cr_crp->crp_buf,
		    cesa_create_chain_cb2, &cci, BUS_DMA_NOWAIT);
	else if (cr->cr_crp->crp_flags & CRYPTO_F_IMBUF)
		error = bus_dmamap_load_mbuf(sc->sc_data_dtag,
		    cr->cr_dmap, (struct mbuf *)cr->cr_crp->crp_buf,
		    cesa_create_chain_cb2, &cci, BUS_DMA_NOWAIT);
	else
		error = bus_dmamap_load(sc->sc_data_dtag,
		    cr->cr_dmap, cr->cr_crp->crp_buf,
		    cr->cr_crp->crp_ilen, cesa_create_chain_cb, &cci,
		    BUS_DMA_NOWAIT);

	if (!error)
		cr->cr_dmap_loaded = 1;

	if (cci.cci_error)
		error = cci.cci_error;

	if (error)
		return (error);

	/* Read back request metadata */
	ctd = cesa_tdma_copyout_sa_data(sc, cr);
	if (!ctd)
		return (ENOMEM);

	cesa_append_tdesc(cr, ctd);

	return (0);
}

static void
cesa_execute(struct cesa_softc *sc)
{
	struct cesa_tdma_desc *prev_ctd, *ctd;
	struct cesa_request *prev_cr, *cr;

	CESA_LOCK(sc, requests);

	/*
	 * If ready list is empty, there is nothing to execute. If queued list
	 * is not empty, the hardware is busy and we cannot start another
	 * execution.
	 */
	if (STAILQ_EMPTY(&sc->sc_ready_requests) ||
	    !STAILQ_EMPTY(&sc->sc_queued_requests)) {
		CESA_UNLOCK(sc, requests);
		return;
	}

	/* Move all ready requests to queued list */
	STAILQ_CONCAT(&sc->sc_queued_requests, &sc->sc_ready_requests);
	STAILQ_INIT(&sc->sc_ready_requests);

	/* Create one execution chain from all requests on the list */
	if (STAILQ_FIRST(&sc->sc_queued_requests) !=
	    STAILQ_LAST(&sc->sc_queued_requests, cesa_request, cr_stq)) {
		prev_cr = NULL;
		cesa_sync_dma_mem(&sc->sc_tdesc_cdm, BUS_DMASYNC_POSTREAD |
		    BUS_DMASYNC_POSTWRITE);

		STAILQ_FOREACH(cr, &sc->sc_queued_requests, cr_stq) {
			if (prev_cr) {
				ctd = STAILQ_FIRST(&cr->cr_tdesc);
				prev_ctd = STAILQ_LAST(&prev_cr->cr_tdesc,
				    cesa_tdma_desc, ctd_stq);

				prev_ctd->ctd_cthd->cthd_next =
				    ctd->ctd_cthd_paddr;
			}

			prev_cr = cr;
		}

		cesa_sync_dma_mem(&sc->sc_tdesc_cdm, BUS_DMASYNC_PREREAD |
		    BUS_DMASYNC_PREWRITE);
	}

	/* Start chain execution in hardware */
	cr = STAILQ_FIRST(&sc->sc_queued_requests);
	ctd = STAILQ_FIRST(&cr->cr_tdesc);

	CESA_TDMA_WRITE(sc, CESA_TDMA_ND, ctd->ctd_cthd_paddr);

	if (sc->sc_soc_id == MV_DEV_88F6828 ||
	    sc->sc_soc_id == MV_DEV_88F6820 ||
	    sc->sc_soc_id == MV_DEV_88F6810)
		CESA_REG_WRITE(sc, CESA_SA_CMD, CESA_SA_CMD_ACTVATE | CESA_SA_CMD_SHA2);
	else
		CESA_REG_WRITE(sc, CESA_SA_CMD, CESA_SA_CMD_ACTVATE);

	CESA_UNLOCK(sc, requests);
}

static int
cesa_setup_sram(struct cesa_softc *sc)
{
	phandle_t sram_node;
	ihandle_t sram_ihandle;
	pcell_t sram_handle, sram_reg[2];
	void *sram_va;
	int rv;

	rv = OF_getencprop(ofw_bus_get_node(sc->sc_dev), "sram-handle",
	    (void *)&sram_handle, sizeof(sram_handle));
	if (rv <= 0)
		return (rv);

	sram_ihandle = (ihandle_t)sram_handle;
	sram_node = OF_instance_to_package(sram_ihandle);

	rv = OF_getencprop(sram_node, "reg", (void *)sram_reg, sizeof(sram_reg));
	if (rv <= 0)
		return (rv);

	sc->sc_sram_base_pa = sram_reg[0];
	/* Store SRAM size to be able to unmap in detach() */
	sc->sc_sram_size = sram_reg[1];

	if (sc->sc_soc_id != MV_DEV_88F6828 &&
	    sc->sc_soc_id != MV_DEV_88F6820 &&
	    sc->sc_soc_id != MV_DEV_88F6810)
		return (0);

	/* SRAM memory was not mapped in platform_sram_devmap(), map it now */
	sram_va = pmap_mapdev(sc->sc_sram_base_pa, sc->sc_sram_size);
	if (sram_va == NULL)
		return (ENOMEM);
	sc->sc_sram_base_va = (vm_offset_t)sram_va;

	return (0);
}

/*
 * Function: device_from_node
 * This function returns appropriate device_t to phandle_t
 * Parameters:
 * root - device where you want to start search
 *     if you provide NULL here, function will take
 *     "root0" device as root.
 * node - we are checking every device_t to be
 *     appropriate with this.
 */
static device_t
device_from_node(device_t root, phandle_t node)
{
	device_t *children, retval;
	int nkid, i;

	/* Nothing matches no node */
	if (node == -1)
		return (NULL);

	if (root == NULL)
		/* Get root of device tree */
		if ((root = device_lookup_by_name("root0")) == NULL)
			return (NULL);

	if (device_get_children(root, &children, &nkid) != 0)
		return (NULL);

	retval = NULL;
	for (i = 0; i < nkid; i++) {
		/* Check if device and node matches */
		if (OFW_BUS_GET_NODE(root, children[i]) == node) {
			retval = children[i];
			break;
		}
		/* or go deeper */
		if ((retval = device_from_node(children[i], node)) != NULL)
			break;
	}
	free(children, M_TEMP);

	return (retval);
}

static int
cesa_setup_sram_armada(struct cesa_softc *sc)
{
	phandle_t sram_node;
	ihandle_t sram_ihandle;
	pcell_t sram_handle[2];
	void *sram_va;
	int rv, j;
	struct resource_list rl;
	struct resource_list_entry *rle;
	struct simplebus_softc *ssc;
	device_t sdev;

	/* Get refs to SRAMS from CESA node */
	rv = OF_getencprop(ofw_bus_get_node(sc->sc_dev), "marvell,crypto-srams",
	    (void *)sram_handle, sizeof(sram_handle));
	if (rv <= 0)
		return (rv);

	if (sc->sc_cesa_engine_id >= 2)
		return (ENXIO);

	/* Get SRAM node on the basis of sc_cesa_engine_id */
	sram_ihandle = (ihandle_t)sram_handle[sc->sc_cesa_engine_id];
	sram_node = OF_instance_to_package(sram_ihandle);

	/* Get device_t of simplebus (sram_node parent) */
	sdev = device_from_node(NULL, OF_parent(sram_node));
	if (!sdev)
		return (ENXIO);

	ssc = device_get_softc(sdev);

	resource_list_init(&rl);
	/* Parse reg property to resource list */
	ofw_bus_reg_to_rl(sdev, sram_node, ssc->acells,
	    ssc->scells, &rl);

	/* We expect only one resource */
	rle = resource_list_find(&rl, SYS_RES_MEMORY, 0);
	if (rle == NULL)
		return (ENXIO);

	/* Remap through ranges property */
	for (j = 0; j < ssc->nranges; j++) {
		if (rle->start >= ssc->ranges[j].bus &&
		    rle->end < ssc->ranges[j].bus + ssc->ranges[j].size) {
			rle->start -= ssc->ranges[j].bus;
			rle->start += ssc->ranges[j].host;
			rle->end -= ssc->ranges[j].bus;
			rle->end += ssc->ranges[j].host;
		}
	}

	sc->sc_sram_base_pa = rle->start;
	sc->sc_sram_size = rle->count;

	/* SRAM memory was not mapped in platform_sram_devmap(), map it now */
	sram_va = pmap_mapdev(sc->sc_sram_base_pa, sc->sc_sram_size);
	if (sram_va == NULL)
		return (ENOMEM);
	sc->sc_sram_base_va = (vm_offset_t)sram_va;

	return (0);
}

struct ofw_compat_data cesa_devices[] = {
	{ "mrvl,cesa", (uintptr_t)true },
	{ "marvell,armada-38x-crypto", (uintptr_t)true },
	{ NULL, 0 }
};

static int
cesa_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, cesa_devices)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Marvell Cryptographic Engine and Security "
	    "Accelerator");

	return (BUS_PROBE_DEFAULT);
}

static int
cesa_attach(device_t dev)
{
	static int engine_idx = 0;
	struct simplebus_devinfo *ndi;
	struct resource_list *rl;
	struct cesa_softc *sc;

	if (!ofw_bus_is_compatible(dev, "marvell,armada-38x-crypto"))
		return (cesa_attach_late(dev));

	/*
	 * Get simplebus_devinfo which contains
	 * resource list filled with adresses and
	 * interrupts read form FDT.
	 * Let's correct it by splitting resources
	 * for each engine.
	 */
	if ((ndi = device_get_ivars(dev)) == NULL)
		return (ENXIO);

	rl = &ndi->rl;

	switch (engine_idx) {
		case 0:
			/* Update regs values */
			resource_list_add(rl, SYS_RES_MEMORY, 0, CESA0_TDMA_ADDR,
			    CESA0_TDMA_ADDR + CESA_TDMA_SIZE - 1, CESA_TDMA_SIZE);
			resource_list_add(rl, SYS_RES_MEMORY, 1, CESA0_CESA_ADDR,
			    CESA0_CESA_ADDR + CESA_CESA_SIZE - 1, CESA_CESA_SIZE);

			/* Remove unused interrupt */
			resource_list_delete(rl, SYS_RES_IRQ, 1);
			break;

		case 1:
			/* Update regs values */
			resource_list_add(rl, SYS_RES_MEMORY, 0, CESA1_TDMA_ADDR,
			    CESA1_TDMA_ADDR + CESA_TDMA_SIZE - 1, CESA_TDMA_SIZE);
			resource_list_add(rl, SYS_RES_MEMORY, 1, CESA1_CESA_ADDR,
			    CESA1_CESA_ADDR + CESA_CESA_SIZE - 1, CESA_CESA_SIZE);

			/* Remove unused interrupt */
			resource_list_delete(rl, SYS_RES_IRQ, 0);
			resource_list_find(rl, SYS_RES_IRQ, 1)->rid = 0;
			break;

		default:
			device_printf(dev, "Bad cesa engine_idx\n");
			return (ENXIO);
	}

	sc = device_get_softc(dev);
	sc->sc_cesa_engine_id = engine_idx;

	/*
	 * Call simplebus_add_device only once.
	 * It will create second cesa driver instance
	 * with the same FDT node as first instance.
	 * When second driver reach this function,
	 * it will be configured to use second cesa engine
	 */
	if (engine_idx == 0)
		simplebus_add_device(device_get_parent(dev), ofw_bus_get_node(dev),
		    0, "cesa", 1, NULL);

	engine_idx++;

	return (cesa_attach_late(dev));
}

static int
cesa_attach_late(device_t dev)
{
	struct cesa_softc *sc;
	uint32_t d, r, val;
	int error;
	int i;

	sc = device_get_softc(dev);
	sc->sc_blocked = 0;
	sc->sc_error = 0;
	sc->sc_dev = dev;

	soc_id(&d, &r);

	switch (d) {
	case MV_DEV_88F6281:
	case MV_DEV_88F6282:
		/* Check if CESA peripheral device has power turned on */
		if (soc_power_ctrl_get(CPU_PM_CTRL_CRYPTO) ==
		    CPU_PM_CTRL_CRYPTO) {
			device_printf(dev, "not powered on\n");
			return (ENXIO);
		}
		sc->sc_tperr = 0;
		break;
	case MV_DEV_88F6828:
	case MV_DEV_88F6820:
	case MV_DEV_88F6810:
		sc->sc_tperr = 0;
		break;
	case MV_DEV_MV78100:
	case MV_DEV_MV78100_Z0:
		/* Check if CESA peripheral device has power turned on */
		if (soc_power_ctrl_get(CPU_PM_CTRL_CRYPTO) !=
		    CPU_PM_CTRL_CRYPTO) {
			device_printf(dev, "not powered on\n");
			return (ENXIO);
		}
		sc->sc_tperr = CESA_ICR_TPERR;
		break;
	default:
		return (ENXIO);
	}

	sc->sc_soc_id = d;

	/* Initialize mutexes */
	mtx_init(&sc->sc_sc_lock, device_get_nameunit(dev),
	    "CESA Shared Data", MTX_DEF);
	mtx_init(&sc->sc_tdesc_lock, device_get_nameunit(dev),
	    "CESA TDMA Descriptors Pool", MTX_DEF);
	mtx_init(&sc->sc_sdesc_lock, device_get_nameunit(dev),
	    "CESA SA Descriptors Pool", MTX_DEF);
	mtx_init(&sc->sc_requests_lock, device_get_nameunit(dev),
	    "CESA Requests Pool", MTX_DEF);
	mtx_init(&sc->sc_sessions_lock, device_get_nameunit(dev),
	    "CESA Sessions Pool", MTX_DEF);

	/* Allocate I/O and IRQ resources */
	error = bus_alloc_resources(dev, cesa_res_spec, sc->sc_res);
	if (error) {
		device_printf(dev, "could not allocate resources\n");
		goto err0;
	}

	/* Acquire SRAM base address */
	if (!ofw_bus_is_compatible(dev, "marvell,armada-38x-crypto"))
		error = cesa_setup_sram(sc);
	else
		error = cesa_setup_sram_armada(sc);

	if (error) {
		device_printf(dev, "could not setup SRAM\n");
		goto err1;
	}

	/* Setup interrupt handler */
	error = bus_setup_intr(dev, sc->sc_res[RES_CESA_IRQ], INTR_TYPE_NET |
	    INTR_MPSAFE, NULL, cesa_intr, sc, &(sc->sc_icookie));
	if (error) {
		device_printf(dev, "could not setup engine completion irq\n");
		goto err2;
	}

	/* Create DMA tag for processed data */
	error = bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
	    1, 0,				/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    CESA_MAX_REQUEST_SIZE,		/* maxsize */
	    CESA_MAX_FRAGMENTS,			/* nsegments */
	    CESA_MAX_REQUEST_SIZE, 0,		/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->sc_data_dtag);			/* dmat */
	if (error)
		goto err3;

	/* Initialize data structures: TDMA Descriptors Pool */
	error = cesa_alloc_dma_mem(sc, &sc->sc_tdesc_cdm,
	    CESA_TDMA_DESCRIPTORS * sizeof(struct cesa_tdma_hdesc));
	if (error)
		goto err4;

	STAILQ_INIT(&sc->sc_free_tdesc);
	for (i = 0; i < CESA_TDMA_DESCRIPTORS; i++) {
		sc->sc_tdesc[i].ctd_cthd =
		    (struct cesa_tdma_hdesc *)(sc->sc_tdesc_cdm.cdm_vaddr) + i;
		sc->sc_tdesc[i].ctd_cthd_paddr = sc->sc_tdesc_cdm.cdm_paddr +
		    (i * sizeof(struct cesa_tdma_hdesc));
		STAILQ_INSERT_TAIL(&sc->sc_free_tdesc, &sc->sc_tdesc[i],
		    ctd_stq);
	}

	/* Initialize data structures: SA Descriptors Pool */
	error = cesa_alloc_dma_mem(sc, &sc->sc_sdesc_cdm,
	    CESA_SA_DESCRIPTORS * sizeof(struct cesa_sa_hdesc));
	if (error)
		goto err5;

	STAILQ_INIT(&sc->sc_free_sdesc);
	for (i = 0; i < CESA_SA_DESCRIPTORS; i++) {
		sc->sc_sdesc[i].csd_cshd =
		    (struct cesa_sa_hdesc *)(sc->sc_sdesc_cdm.cdm_vaddr) + i;
		sc->sc_sdesc[i].csd_cshd_paddr = sc->sc_sdesc_cdm.cdm_paddr +
		    (i * sizeof(struct cesa_sa_hdesc));
		STAILQ_INSERT_TAIL(&sc->sc_free_sdesc, &sc->sc_sdesc[i],
		    csd_stq);
	}

	/* Initialize data structures: Requests Pool */
	error = cesa_alloc_dma_mem(sc, &sc->sc_requests_cdm,
	    CESA_REQUESTS * sizeof(struct cesa_sa_data));
	if (error)
		goto err6;

	STAILQ_INIT(&sc->sc_free_requests);
	STAILQ_INIT(&sc->sc_ready_requests);
	STAILQ_INIT(&sc->sc_queued_requests);
	for (i = 0; i < CESA_REQUESTS; i++) {
		sc->sc_requests[i].cr_csd =
		    (struct cesa_sa_data *)(sc->sc_requests_cdm.cdm_vaddr) + i;
		sc->sc_requests[i].cr_csd_paddr =
		    sc->sc_requests_cdm.cdm_paddr +
		    (i * sizeof(struct cesa_sa_data));

		/* Preallocate DMA maps */
		error = bus_dmamap_create(sc->sc_data_dtag, 0,
		    &sc->sc_requests[i].cr_dmap);
		if (error && i > 0) {
			i--;
			do {
				bus_dmamap_destroy(sc->sc_data_dtag,
				    sc->sc_requests[i].cr_dmap);
			} while (i--);

			goto err7;
		}

		STAILQ_INSERT_TAIL(&sc->sc_free_requests, &sc->sc_requests[i],
		    cr_stq);
	}

	/*
	 * Initialize TDMA:
	 * - Burst limit: 128 bytes,
	 * - Outstanding reads enabled,
	 * - No byte-swap.
	 */
	val = CESA_TDMA_CR_DBL128 | CESA_TDMA_CR_SBL128 |
	    CESA_TDMA_CR_ORDEN | CESA_TDMA_CR_NBS | CESA_TDMA_CR_ENABLE;

	if (sc->sc_soc_id == MV_DEV_88F6828 ||
	    sc->sc_soc_id == MV_DEV_88F6820 ||
	    sc->sc_soc_id == MV_DEV_88F6810)
		val |= CESA_TDMA_NUM_OUTSTAND;

	CESA_TDMA_WRITE(sc, CESA_TDMA_CR, val);

	/*
	 * Initialize SA:
	 * - SA descriptor is present at beginning of CESA SRAM,
	 * - Multi-packet chain mode,
	 * - Cooperation with TDMA enabled.
	 */
	CESA_REG_WRITE(sc, CESA_SA_DPR, 0);
	CESA_REG_WRITE(sc, CESA_SA_CR, CESA_SA_CR_ACTIVATE_TDMA |
	    CESA_SA_CR_WAIT_FOR_TDMA | CESA_SA_CR_MULTI_MODE);

	/* Unmask interrupts */
	CESA_REG_WRITE(sc, CESA_ICR, 0);
	CESA_REG_WRITE(sc, CESA_ICM, CESA_ICM_ACCTDMA | sc->sc_tperr);
	CESA_TDMA_WRITE(sc, CESA_TDMA_ECR, 0);
	CESA_TDMA_WRITE(sc, CESA_TDMA_EMR, CESA_TDMA_EMR_MISS |
	    CESA_TDMA_EMR_DOUBLE_HIT | CESA_TDMA_EMR_BOTH_HIT |
	    CESA_TDMA_EMR_DATA_ERROR);

	/* Register in OCF */
	sc->sc_cid = crypto_get_driverid(dev, sizeof(struct cesa_session),
	    CRYPTOCAP_F_HARDWARE);
	if (sc->sc_cid < 0) {
		device_printf(dev, "could not get crypto driver id\n");
		goto err8;
	}

	crypto_register(sc->sc_cid, CRYPTO_AES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_DES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_3DES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_MD5, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA1, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, 0, 0);
	if (sc->sc_soc_id == MV_DEV_88F6828 ||
	    sc->sc_soc_id == MV_DEV_88F6820 ||
	    sc->sc_soc_id == MV_DEV_88F6810)
		crypto_register(sc->sc_cid, CRYPTO_SHA2_256_HMAC, 0, 0);

	return (0);
err8:
	for (i = 0; i < CESA_REQUESTS; i++)
		bus_dmamap_destroy(sc->sc_data_dtag,
		    sc->sc_requests[i].cr_dmap);
err7:
	cesa_free_dma_mem(&sc->sc_requests_cdm);
err6:
	cesa_free_dma_mem(&sc->sc_sdesc_cdm);
err5:
	cesa_free_dma_mem(&sc->sc_tdesc_cdm);
err4:
	bus_dma_tag_destroy(sc->sc_data_dtag);
err3:
	bus_teardown_intr(dev, sc->sc_res[RES_CESA_IRQ], sc->sc_icookie);
err2:
	if (sc->sc_soc_id == MV_DEV_88F6828 ||
	    sc->sc_soc_id == MV_DEV_88F6820 ||
	    sc->sc_soc_id == MV_DEV_88F6810)
		pmap_unmapdev(sc->sc_sram_base_va, sc->sc_sram_size);
err1:
	bus_release_resources(dev, cesa_res_spec, sc->sc_res);
err0:
	mtx_destroy(&sc->sc_sessions_lock);
	mtx_destroy(&sc->sc_requests_lock);
	mtx_destroy(&sc->sc_sdesc_lock);
	mtx_destroy(&sc->sc_tdesc_lock);
	mtx_destroy(&sc->sc_sc_lock);
	return (ENXIO);
}

static int
cesa_detach(device_t dev)
{
	struct cesa_softc *sc;
	int i;
 
	sc = device_get_softc(dev);

	/* TODO: Wait for queued requests completion before shutdown. */

	/* Mask interrupts */
	CESA_REG_WRITE(sc, CESA_ICM, 0);
	CESA_TDMA_WRITE(sc, CESA_TDMA_EMR, 0);

	/* Unregister from OCF */
	crypto_unregister_all(sc->sc_cid);

	/* Free DMA Maps */
	for (i = 0; i < CESA_REQUESTS; i++)
		bus_dmamap_destroy(sc->sc_data_dtag,
		    sc->sc_requests[i].cr_dmap);

	/* Free DMA Memory */
	cesa_free_dma_mem(&sc->sc_requests_cdm);
	cesa_free_dma_mem(&sc->sc_sdesc_cdm);
	cesa_free_dma_mem(&sc->sc_tdesc_cdm);

	/* Free DMA Tag */
	bus_dma_tag_destroy(sc->sc_data_dtag);

	/* Stop interrupt */
	bus_teardown_intr(dev, sc->sc_res[RES_CESA_IRQ], sc->sc_icookie);

	/* Relase I/O and IRQ resources */
	bus_release_resources(dev, cesa_res_spec, sc->sc_res);

	/* Unmap SRAM memory */
	if (sc->sc_soc_id == MV_DEV_88F6828 ||
	    sc->sc_soc_id == MV_DEV_88F6820 ||
	    sc->sc_soc_id == MV_DEV_88F6810)
		pmap_unmapdev(sc->sc_sram_base_va, sc->sc_sram_size);

	/* Destroy mutexes */
	mtx_destroy(&sc->sc_sessions_lock);
	mtx_destroy(&sc->sc_requests_lock);
	mtx_destroy(&sc->sc_sdesc_lock);
	mtx_destroy(&sc->sc_tdesc_lock);
	mtx_destroy(&sc->sc_sc_lock);

	return (0);
}

static void
cesa_intr(void *arg)
{
	STAILQ_HEAD(, cesa_request) requests;
	struct cesa_request *cr, *tmp;
	struct cesa_softc *sc;
	uint32_t ecr, icr;
	int blocked;

	sc = arg;

	/* Ack interrupt */
	ecr = CESA_TDMA_READ(sc, CESA_TDMA_ECR);
	CESA_TDMA_WRITE(sc, CESA_TDMA_ECR, 0);
	icr = CESA_REG_READ(sc, CESA_ICR);
	CESA_REG_WRITE(sc, CESA_ICR, 0);

	/* Check for TDMA errors */
	if (ecr & CESA_TDMA_ECR_MISS) {
		device_printf(sc->sc_dev, "TDMA Miss error detected!\n");
		sc->sc_error = EIO;
	}

	if (ecr & CESA_TDMA_ECR_DOUBLE_HIT) {
		device_printf(sc->sc_dev, "TDMA Double Hit error detected!\n");
		sc->sc_error = EIO;
	}

	if (ecr & CESA_TDMA_ECR_BOTH_HIT) {
		device_printf(sc->sc_dev, "TDMA Both Hit error detected!\n");
		sc->sc_error = EIO;
	}

	if (ecr & CESA_TDMA_ECR_DATA_ERROR) {
		device_printf(sc->sc_dev, "TDMA Data error detected!\n");
		sc->sc_error = EIO;
	}

	/* Check for CESA errors */
	if (icr & sc->sc_tperr) {
		device_printf(sc->sc_dev, "CESA SRAM Parity error detected!\n");
		sc->sc_error = EIO;
	}

	/* If there is nothing more to do, return */
	if ((icr & CESA_ICR_ACCTDMA) == 0)
		return;

	/* Get all finished requests */
	CESA_LOCK(sc, requests);
	STAILQ_INIT(&requests);
	STAILQ_CONCAT(&requests, &sc->sc_queued_requests);
	STAILQ_INIT(&sc->sc_queued_requests);
	CESA_UNLOCK(sc, requests);

	/* Execute all ready requests */
	cesa_execute(sc);

	/* Process completed requests */
	cesa_sync_dma_mem(&sc->sc_requests_cdm, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	STAILQ_FOREACH_SAFE(cr, &requests, cr_stq, tmp) {
		bus_dmamap_sync(sc->sc_data_dtag, cr->cr_dmap,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		cr->cr_crp->crp_etype = sc->sc_error;
		if (cr->cr_mac)
			crypto_copyback(cr->cr_crp->crp_flags,
			    cr->cr_crp->crp_buf, cr->cr_mac->crd_inject,
			    cr->cr_cs->cs_hlen, cr->cr_csd->csd_hash);

		crypto_done(cr->cr_crp);
		cesa_free_request(sc, cr);
	}

	cesa_sync_dma_mem(&sc->sc_requests_cdm, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	sc->sc_error = 0;

	/* Unblock driver if it ran out of resources */
	CESA_LOCK(sc, sc);
	blocked = sc->sc_blocked;
	sc->sc_blocked = 0;
	CESA_UNLOCK(sc, sc);

	if (blocked)
		crypto_unblock(sc->sc_cid, blocked);
}

static int
cesa_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct cesa_session *cs;
	struct cesa_softc *sc;
	struct cryptoini *enc;
	struct cryptoini *mac;
	int error;
 
	sc = device_get_softc(dev);
	enc = NULL;
	mac = NULL;
	error = 0;

	/* Check and parse input */
	if (cesa_is_hash(cri->cri_alg))
		mac = cri;
	else
		enc = cri;

	cri = cri->cri_next;

	if (cri) {
		if (!enc && !cesa_is_hash(cri->cri_alg))
			enc = cri;

		if (!mac && cesa_is_hash(cri->cri_alg))
			mac = cri;

		if (cri->cri_next || !(enc && mac))
			return (EINVAL);
	}

	if ((enc && (enc->cri_klen / 8) > CESA_MAX_KEY_LEN) ||
	    (mac && (mac->cri_klen / 8) > CESA_MAX_MKEY_LEN))
		return (E2BIG);

	/* Allocate session */
	cs = crypto_get_driver_session(cses);

	/* Prepare CESA configuration */
	cs->cs_config = 0;
	cs->cs_ivlen = 1;
	cs->cs_mblen = 1;

	if (enc) {
		switch (enc->cri_alg) {
		case CRYPTO_AES_CBC:
			cs->cs_config |= CESA_CSHD_AES | CESA_CSHD_CBC;
			cs->cs_ivlen = AES_BLOCK_LEN;
			break;
		case CRYPTO_DES_CBC:
			cs->cs_config |= CESA_CSHD_DES | CESA_CSHD_CBC;
			cs->cs_ivlen = DES_BLOCK_LEN;
			break;
		case CRYPTO_3DES_CBC:
			cs->cs_config |= CESA_CSHD_3DES | CESA_CSHD_3DES_EDE |
			    CESA_CSHD_CBC;
			cs->cs_ivlen = DES3_BLOCK_LEN;
			break;
		default:
			error = EINVAL;
			break;
		}
	}

	if (!error && mac) {
		switch (mac->cri_alg) {
		case CRYPTO_MD5:
			cs->cs_mblen = 1;
			cs->cs_hlen = (mac->cri_mlen == 0) ? MD5_HASH_LEN :
			    mac->cri_mlen;
			cs->cs_config |= CESA_CSHD_MD5;
			break;
		case CRYPTO_MD5_HMAC:
			cs->cs_mblen = MD5_BLOCK_LEN;
			cs->cs_hlen = (mac->cri_mlen == 0) ? MD5_HASH_LEN :
			    mac->cri_mlen;
			cs->cs_config |= CESA_CSHD_MD5_HMAC;
			if (cs->cs_hlen == CESA_HMAC_TRUNC_LEN)
				cs->cs_config |= CESA_CSHD_96_BIT_HMAC;
			break;
		case CRYPTO_SHA1:
			cs->cs_mblen = 1;
			cs->cs_hlen = (mac->cri_mlen == 0) ? SHA1_HASH_LEN :
			    mac->cri_mlen;
			cs->cs_config |= CESA_CSHD_SHA1;
			break;
		case CRYPTO_SHA1_HMAC:
			cs->cs_mblen = SHA1_BLOCK_LEN;
			cs->cs_hlen = (mac->cri_mlen == 0) ? SHA1_HASH_LEN :
			    mac->cri_mlen;
			cs->cs_config |= CESA_CSHD_SHA1_HMAC;
			if (cs->cs_hlen == CESA_HMAC_TRUNC_LEN)
				cs->cs_config |= CESA_CSHD_96_BIT_HMAC;
			break;
		case CRYPTO_SHA2_256_HMAC:
			cs->cs_mblen = SHA2_256_BLOCK_LEN;
			cs->cs_hlen = (mac->cri_mlen == 0) ? SHA2_256_HASH_LEN :
			    mac->cri_mlen;
			cs->cs_config |= CESA_CSHD_SHA2_256_HMAC;
			break;
		default:
			error = EINVAL;
			break;
		}
	}

	/* Save cipher key */
	if (!error && enc && enc->cri_key) {
		cs->cs_klen = enc->cri_klen / 8;
		memcpy(cs->cs_key, enc->cri_key, cs->cs_klen);
		if (enc->cri_alg == CRYPTO_AES_CBC)
			error = cesa_prep_aes_key(cs);
	}

	/* Save digest key */
	if (!error && mac && mac->cri_key)
		error = cesa_set_mkey(cs, mac->cri_alg, mac->cri_key,
		    mac->cri_klen / 8);

	if (error)
		return (error);

	return (0);
}

static int
cesa_process(device_t dev, struct cryptop *crp, int hint)
{
	struct cesa_request *cr;
	struct cesa_session *cs;
	struct cryptodesc *crd;
	struct cryptodesc *enc;
	struct cryptodesc *mac;
	struct cesa_softc *sc;
	int error;

	sc = device_get_softc(dev);
	crd = crp->crp_desc;
	enc = NULL;
	mac = NULL;
	error = 0;

	cs = crypto_get_driver_session(crp->crp_session);

	/* Check and parse input */
	if (crp->crp_ilen > CESA_MAX_REQUEST_SIZE) {
		crp->crp_etype = E2BIG;
		crypto_done(crp);
		return (0);
	}

	if (cesa_is_hash(crd->crd_alg))
		mac = crd;
	else
		enc = crd;

	crd = crd->crd_next;

	if (crd) {
		if (!enc && !cesa_is_hash(crd->crd_alg))
			enc = crd;

		if (!mac && cesa_is_hash(crd->crd_alg))
			mac = crd;

		if (crd->crd_next || !(enc && mac)) {
			crp->crp_etype = EINVAL;
			crypto_done(crp);
			return (0);
		}
	}

	/*
	 * Get request descriptor. Block driver if there is no free
	 * descriptors in pool.
	 */
	cr = cesa_alloc_request(sc);
	if (!cr) {
		CESA_LOCK(sc, sc);
		sc->sc_blocked = CRYPTO_SYMQ;
		CESA_UNLOCK(sc, sc);
		return (ERESTART);
	}

	/* Prepare request */
	cr->cr_crp = crp;
	cr->cr_enc = enc;
	cr->cr_mac = mac;
	cr->cr_cs = cs;

	CESA_LOCK(sc, sessions);
	cesa_sync_desc(sc, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	if (enc && enc->crd_flags & CRD_F_ENCRYPT) {
		if (enc->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(cr->cr_csd->csd_iv, enc->crd_iv, cs->cs_ivlen);
		else
			arc4rand(cr->cr_csd->csd_iv, cs->cs_ivlen, 0);

		if ((enc->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    enc->crd_inject, cs->cs_ivlen, cr->cr_csd->csd_iv);
	} else if (enc) {
		if (enc->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(cr->cr_csd->csd_iv, enc->crd_iv, cs->cs_ivlen);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    enc->crd_inject, cs->cs_ivlen, cr->cr_csd->csd_iv);
	}

	if (enc && enc->crd_flags & CRD_F_KEY_EXPLICIT) {
		if ((enc->crd_klen / 8) <= CESA_MAX_KEY_LEN) {
			cs->cs_klen = enc->crd_klen / 8;
			memcpy(cs->cs_key, enc->crd_key, cs->cs_klen);
			if (enc->crd_alg == CRYPTO_AES_CBC)
				error = cesa_prep_aes_key(cs);
		} else
			error = E2BIG;
	}

	if (!error && mac && mac->crd_flags & CRD_F_KEY_EXPLICIT) {
		if ((mac->crd_klen / 8) <= CESA_MAX_MKEY_LEN)
			error = cesa_set_mkey(cs, mac->crd_alg, mac->crd_key,
			    mac->crd_klen / 8);
		else
			error = E2BIG;
	}

	/* Convert request to chain of TDMA and SA descriptors */
	if (!error)
		error = cesa_create_chain(sc, cr);

	cesa_sync_desc(sc, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CESA_UNLOCK(sc, sessions);

	if (error) {
		cesa_free_request(sc, cr);
		crp->crp_etype = error;
		crypto_done(crp);
		return (0);
	}

	bus_dmamap_sync(sc->sc_data_dtag, cr->cr_dmap, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	/* Enqueue request to execution */
	cesa_enqueue_request(sc, cr);

	/* Start execution, if we have no more requests in queue */
	if ((hint & CRYPTO_HINT_MORE) == 0)
		cesa_execute(sc);

	return (0);
}
