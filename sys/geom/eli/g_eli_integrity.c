/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/vnode.h>

#include <vm/uma.h>

#include <geom/geom.h>
#include <geom/eli/g_eli.h>
#include <geom/eli/pkcs5v2.h>

/*
 * The data layout description when integrity verification is configured.
 *
 * One of the most important assumption here is that authenticated data and its
 * HMAC has to be stored in the same place (namely in the same sector) to make
 * it work reliable.
 * The problem is that file systems work only with sectors that are multiple of
 * 512 bytes and a power of two number.
 * My idea to implement it is as follows.
 * Let's store HMAC in sector. This is a must. This leaves us 480 bytes for
 * data. We can't use that directly (ie. we can't create provider with 480 bytes
 * sector size). We need another sector from where we take only 32 bytes of data
 * and we store HMAC of this data as well. This takes two sectors from the
 * original provider at the input and leaves us one sector of authenticated data
 * at the output. Not very efficient, but you got the idea.
 * Now, let's assume, we want to create provider with 4096 bytes sector.
 * To output 4096 bytes of authenticated data we need 8x480 plus 1x256, so we
 * need nine 512-bytes sectors at the input to get one 4096-bytes sector at the
 * output. That's better. With 4096 bytes sector we can use 89% of size of the
 * original provider. I find it as an acceptable cost.
 * The reliability comes from the fact, that every HMAC stored inside the sector
 * is calculated only for the data in the same sector, so its impossible to
 * write new data and leave old HMAC or vice versa.
 *
 * And here is the picture:
 *
 * da0: +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+-----+
 *      |32b |480b| |32b |480b| |32b |480b| |32b |480b| |32b |480b| |32b |480b| |32b |480b| |32b |480b| |32b |256b |
 *      |HMAC|Data| |HMAC|Data| |HMAC|Data| |HMAC|Data| |HMAC|Data| |HMAC|Data| |HMAC|Data| |HMAC|Data| |HMAC|Data |
 *      +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+----+ +----+-----+
 *      |512 bytes| |512 bytes| |512 bytes| |512 bytes| |512 bytes| |512 bytes| |512 bytes| |512 bytes| |288 bytes |
 *      +---------+ +---------+ +---------+ +---------+ +---------+ +---------+ +---------+ +---------+ |224 unused|
 *                                                                                                      +----------+
 * da0.eli: +----+----+----+----+----+----+----+----+----+
 *          |480b|480b|480b|480b|480b|480b|480b|480b|256b|
 *          +----+----+----+----+----+----+----+----+----+
 *          |                 4096 bytes                 |
 *          +--------------------------------------------+
 *
 * PS. You can use any sector size with geli(8). My example is using 4kB,
 *     because it's most efficient. For 8kB sectors you need 2 extra sectors,
 *     so the cost is the same as for 4kB sectors.
 */

/*
 * Code paths:
 * BIO_READ:
 *	g_eli_start -> g_eli_auth_read -> g_io_request -> g_eli_read_done -> g_eli_auth_run -> g_eli_auth_read_done -> g_io_deliver
 * BIO_WRITE:
 *	g_eli_start -> g_eli_auth_run -> g_eli_auth_write_done -> g_io_request -> g_eli_write_done -> g_io_deliver
 */

MALLOC_DECLARE(M_ELI);

/*
 * Here we generate key for HMAC. Every sector has its own HMAC key, so it is
 * not possible to copy sectors.
 * We cannot depend on fact, that every sector has its own IV, because different
 * IV doesn't change HMAC, when we use encrypt-then-authenticate method.
 */
static void
g_eli_auth_keygen(struct g_eli_softc *sc, off_t offset, u_char *key)
{
	SHA256_CTX ctx;

	/* Copy precalculated SHA256 context. */
	bcopy(&sc->sc_akeyctx, &ctx, sizeof(ctx));
	SHA256_Update(&ctx, (uint8_t *)&offset, sizeof(offset));
	SHA256_Final(key, &ctx);
}

/*
 * The function is called after we read and decrypt data.
 *
 * g_eli_start -> g_eli_auth_read -> g_io_request -> g_eli_read_done -> g_eli_auth_run -> G_ELI_AUTH_READ_DONE -> g_io_deliver
 */
static int
g_eli_auth_read_done(struct cryptop *crp)
{
	struct g_eli_softc *sc;
	struct bio *bp;

	if (crp->crp_etype == EAGAIN) {
		if (g_eli_crypto_rerun(crp) == 0)
			return (0);
	}
	bp = (struct bio *)crp->crp_opaque;
	bp->bio_inbed++;
	if (crp->crp_etype == 0) {
		bp->bio_completed += crp->crp_olen;
		G_ELI_DEBUG(3, "Crypto READ request done (%d/%d) (add=%jd completed=%jd).",
		    bp->bio_inbed, bp->bio_children, (intmax_t)crp->crp_olen, (intmax_t)bp->bio_completed);
	} else {
		G_ELI_DEBUG(1, "Crypto READ request failed (%d/%d) error=%d.",
		    bp->bio_inbed, bp->bio_children, crp->crp_etype);
		if (bp->bio_error == 0)
			bp->bio_error = crp->crp_etype;
	}
	sc = bp->bio_to->geom->softc;
	g_eli_key_drop(sc, crp->crp_desc->crd_next->crd_key);
	/*
	 * Do we have all sectors already?
	 */
	if (bp->bio_inbed < bp->bio_children)
		return (0);
	if (bp->bio_error == 0) {
		u_int i, lsec, nsec, data_secsize, decr_secsize, encr_secsize;
		u_char *srcdata, *dstdata, *auth;
		off_t coroff, corsize;

		/*
		 * Verify data integrity based on calculated and read HMACs.
		 */
		/* Sectorsize of decrypted provider eg. 4096. */
		decr_secsize = bp->bio_to->sectorsize;
		/* The real sectorsize of encrypted provider, eg. 512. */
		encr_secsize = LIST_FIRST(&sc->sc_geom->consumer)->provider->sectorsize;
		/* Number of data bytes in one encrypted sector, eg. 480. */
		data_secsize = sc->sc_data_per_sector;
		/* Number of sectors from decrypted provider, eg. 2. */
		nsec = bp->bio_length / decr_secsize;
		/* Number of sectors from encrypted provider, eg. 18. */
		nsec = (nsec * sc->sc_bytes_per_sector) / encr_secsize;
		/* Last sector number in every big sector, eg. 9. */
		lsec = sc->sc_bytes_per_sector / encr_secsize;

		srcdata = bp->bio_driver2;
		dstdata = bp->bio_data;
		auth = srcdata + encr_secsize * nsec;
		coroff = -1;
		corsize = 0;

		for (i = 1; i <= nsec; i++) {
			data_secsize = sc->sc_data_per_sector;
			if ((i % lsec) == 0)
				data_secsize = decr_secsize % data_secsize;
			if (bcmp(srcdata, auth, sc->sc_alen) != 0) {
				/*
				 * Curruption detected, remember the offset if
				 * this is the first corrupted sector and
				 * increase size.
				 */
				if (bp->bio_error == 0)
					bp->bio_error = -1;
				if (coroff == -1) {
					coroff = bp->bio_offset +
					    (dstdata - (u_char *)bp->bio_data);
				}
				corsize += data_secsize;
			} else {
				/*
				 * No curruption, good.
				 * Report previous corruption if there was one.
				 */
				if (coroff != -1) {
					G_ELI_DEBUG(0, "%s: Failed to authenticate %jd "
					    "bytes of data at offset %jd.",
					    sc->sc_name, (intmax_t)corsize,
					    (intmax_t)coroff);
					coroff = -1;
					corsize = 0;
				}
				bcopy(srcdata + sc->sc_alen, dstdata,
				    data_secsize);
			}
			srcdata += encr_secsize;
			dstdata += data_secsize;
			auth += sc->sc_alen;
		}
		/* Report previous corruption if there was one. */
		if (coroff != -1) {
			G_ELI_DEBUG(0, "%s: Failed to authenticate %jd "
			    "bytes of data at offset %jd.",
			    sc->sc_name, (intmax_t)corsize, (intmax_t)coroff);
		}
	}
	free(bp->bio_driver2, M_ELI);
	bp->bio_driver2 = NULL;
	if (bp->bio_error != 0) {
		if (bp->bio_error == -1)
			bp->bio_error = EINVAL;
		else {
			G_ELI_LOGREQ(0, bp,
			    "Crypto READ request failed (error=%d).",
			    bp->bio_error);
		}
		bp->bio_completed = 0;
	}
	/*
	 * Read is finished, send it up.
	 */
	g_io_deliver(bp, bp->bio_error);
	atomic_subtract_int(&sc->sc_inflight, 1);
	return (0);
}

/*
 * The function is called after data encryption.
 *
 * g_eli_start -> g_eli_auth_run -> G_ELI_AUTH_WRITE_DONE -> g_io_request -> g_eli_write_done -> g_io_deliver
 */
static int
g_eli_auth_write_done(struct cryptop *crp)
{
	struct g_eli_softc *sc;
	struct g_consumer *cp;
	struct bio *bp, *cbp, *cbp2;
	u_int nsec;

	if (crp->crp_etype == EAGAIN) {
		if (g_eli_crypto_rerun(crp) == 0)
			return (0);
	}
	bp = (struct bio *)crp->crp_opaque;
	bp->bio_inbed++;
	if (crp->crp_etype == 0) {
		G_ELI_DEBUG(3, "Crypto WRITE request done (%d/%d).",
		    bp->bio_inbed, bp->bio_children);
	} else {
		G_ELI_DEBUG(1, "Crypto WRITE request failed (%d/%d) error=%d.",
		    bp->bio_inbed, bp->bio_children, crp->crp_etype);
		if (bp->bio_error == 0)
			bp->bio_error = crp->crp_etype;
	}
	sc = bp->bio_to->geom->softc;
	g_eli_key_drop(sc, crp->crp_desc->crd_key);
	/*
	 * All sectors are already encrypted?
	 */
	if (bp->bio_inbed < bp->bio_children)
		return (0);
	if (bp->bio_error != 0) {
		G_ELI_LOGREQ(0, bp, "Crypto WRITE request failed (error=%d).",
		    bp->bio_error);
		free(bp->bio_driver2, M_ELI);
		bp->bio_driver2 = NULL;
		cbp = bp->bio_driver1;
		bp->bio_driver1 = NULL;
		g_destroy_bio(cbp);
		g_io_deliver(bp, bp->bio_error);
		atomic_subtract_int(&sc->sc_inflight, 1);
		return (0);
	}
	cp = LIST_FIRST(&sc->sc_geom->consumer);
	cbp = bp->bio_driver1;
	bp->bio_driver1 = NULL;
	cbp->bio_to = cp->provider;
	cbp->bio_done = g_eli_write_done;

	/* Number of sectors from decrypted provider, eg. 1. */
	nsec = bp->bio_length / bp->bio_to->sectorsize;
	/* Number of sectors from encrypted provider, eg. 9. */
	nsec = (nsec * sc->sc_bytes_per_sector) / cp->provider->sectorsize;

	cbp->bio_length = cp->provider->sectorsize * nsec;
	cbp->bio_offset = (bp->bio_offset / bp->bio_to->sectorsize) * sc->sc_bytes_per_sector;
	cbp->bio_data = bp->bio_driver2;

	/*
	 * We write more than what is requested, so we have to be ready to write
	 * more than MAXPHYS.
	 */
	cbp2 = NULL;
	if (cbp->bio_length > MAXPHYS) {
		cbp2 = g_duplicate_bio(bp);
		cbp2->bio_length = cbp->bio_length - MAXPHYS;
		cbp2->bio_data = cbp->bio_data + MAXPHYS;
		cbp2->bio_offset = cbp->bio_offset + MAXPHYS;
		cbp2->bio_to = cp->provider;
		cbp2->bio_done = g_eli_write_done;
		cbp->bio_length = MAXPHYS;
	}
	/*
	 * Send encrypted data to the provider.
	 */
	G_ELI_LOGREQ(2, cbp, "Sending request.");
	bp->bio_inbed = 0;
	bp->bio_children = (cbp2 != NULL ? 2 : 1);
	g_io_request(cbp, cp);
	if (cbp2 != NULL) {
		G_ELI_LOGREQ(2, cbp2, "Sending request.");
		g_io_request(cbp2, cp);
	}
	return (0);
}

void
g_eli_auth_read(struct g_eli_softc *sc, struct bio *bp)
{
	struct g_consumer *cp;
	struct bio *cbp, *cbp2;
	size_t size;
	off_t nsec;

	bp->bio_pflags = 0;

	cp = LIST_FIRST(&sc->sc_geom->consumer);
	cbp = bp->bio_driver1;
	bp->bio_driver1 = NULL;
	cbp->bio_to = cp->provider;
	cbp->bio_done = g_eli_read_done;

	/* Number of sectors from decrypted provider, eg. 1. */
	nsec = bp->bio_length / bp->bio_to->sectorsize;
	/* Number of sectors from encrypted provider, eg. 9. */
	nsec = (nsec * sc->sc_bytes_per_sector) / cp->provider->sectorsize;

	cbp->bio_length = cp->provider->sectorsize * nsec;
	size = cbp->bio_length;
	size += sc->sc_alen * nsec;
	size += sizeof(struct cryptop) * nsec;
	size += sizeof(struct cryptodesc) * nsec * 2;
	size += G_ELI_AUTH_SECKEYLEN * nsec;
	cbp->bio_offset = (bp->bio_offset / bp->bio_to->sectorsize) * sc->sc_bytes_per_sector;
	bp->bio_driver2 = malloc(size, M_ELI, M_WAITOK);
	cbp->bio_data = bp->bio_driver2;

	/*
	 * We read more than what is requested, so we have to be ready to read
	 * more than MAXPHYS.
	 */
	cbp2 = NULL;
	if (cbp->bio_length > MAXPHYS) {
		cbp2 = g_duplicate_bio(bp);
		cbp2->bio_length = cbp->bio_length - MAXPHYS;
		cbp2->bio_data = cbp->bio_data + MAXPHYS;
		cbp2->bio_offset = cbp->bio_offset + MAXPHYS;
		cbp2->bio_to = cp->provider;
		cbp2->bio_done = g_eli_read_done;
		cbp->bio_length = MAXPHYS;
	}
	/*
	 * Read encrypted data from provider.
	 */
	G_ELI_LOGREQ(2, cbp, "Sending request.");
	g_io_request(cbp, cp);
	if (cbp2 != NULL) {
		G_ELI_LOGREQ(2, cbp2, "Sending request.");
		g_io_request(cbp2, cp);
	}
}

/*
 * This is the main function responsible for cryptography (ie. communication
 * with crypto(9) subsystem).
 *
 * BIO_READ:
 *	g_eli_start -> g_eli_auth_read -> g_io_request -> g_eli_read_done -> G_ELI_AUTH_RUN -> g_eli_auth_read_done -> g_io_deliver
 * BIO_WRITE:
 *	g_eli_start -> G_ELI_AUTH_RUN -> g_eli_auth_write_done -> g_io_request -> g_eli_write_done -> g_io_deliver
 */
void
g_eli_auth_run(struct g_eli_worker *wr, struct bio *bp)
{
	struct g_eli_softc *sc;
	struct cryptop *crp;
	struct cryptodesc *crde, *crda;
	u_int i, lsec, nsec, data_secsize, decr_secsize, encr_secsize;
	off_t dstoff;
	u_char *p, *data, *auth, *authkey, *plaindata;
	int error;

	G_ELI_LOGREQ(3, bp, "%s", __func__);

	bp->bio_pflags = wr->w_number;
	sc = wr->w_softc;
	/* Sectorsize of decrypted provider eg. 4096. */
	decr_secsize = bp->bio_to->sectorsize;
	/* The real sectorsize of encrypted provider, eg. 512. */
	encr_secsize = LIST_FIRST(&sc->sc_geom->consumer)->provider->sectorsize;
	/* Number of data bytes in one encrypted sector, eg. 480. */
	data_secsize = sc->sc_data_per_sector;
	/* Number of sectors from decrypted provider, eg. 2. */
	nsec = bp->bio_length / decr_secsize;
	/* Number of sectors from encrypted provider, eg. 18. */
	nsec = (nsec * sc->sc_bytes_per_sector) / encr_secsize;
	/* Last sector number in every big sector, eg. 9. */
	lsec = sc->sc_bytes_per_sector / encr_secsize;
	/* Destination offset, used for IV generation. */
	dstoff = (bp->bio_offset / bp->bio_to->sectorsize) * sc->sc_bytes_per_sector;

	auth = NULL;	/* Silence compiler warning. */
	plaindata = bp->bio_data;
	if (bp->bio_cmd == BIO_READ) {
		data = bp->bio_driver2;
		auth = data + encr_secsize * nsec;
		p = auth + sc->sc_alen * nsec;
	} else {
		size_t size;

		size = encr_secsize * nsec;
		size += sizeof(*crp) * nsec;
		size += sizeof(*crde) * nsec;
		size += sizeof(*crda) * nsec;
		size += G_ELI_AUTH_SECKEYLEN * nsec;
		size += sizeof(uintptr_t);	/* Space for alignment. */
		data = malloc(size, M_ELI, M_WAITOK);
		bp->bio_driver2 = data;
		p = data + encr_secsize * nsec;
	}
	bp->bio_inbed = 0;
	bp->bio_children = nsec;

#if defined(__mips_n64) || defined(__mips_o64)
	p = (char *)roundup((uintptr_t)p, sizeof(uintptr_t));
#endif

	for (i = 1; i <= nsec; i++, dstoff += encr_secsize) {
		crp = (struct cryptop *)p;	p += sizeof(*crp);
		crde = (struct cryptodesc *)p;	p += sizeof(*crde);
		crda = (struct cryptodesc *)p;	p += sizeof(*crda);
		authkey = (u_char *)p;		p += G_ELI_AUTH_SECKEYLEN;

		data_secsize = sc->sc_data_per_sector;
		if ((i % lsec) == 0) {
			data_secsize = decr_secsize % data_secsize;
			/*
			 * Last encrypted sector of each decrypted sector is
			 * only partially filled.
			 */
			if (bp->bio_cmd == BIO_WRITE)
				memset(data + sc->sc_alen + data_secsize, 0,
				    encr_secsize - sc->sc_alen - data_secsize);
		}

		if (bp->bio_cmd == BIO_READ) {
			/* Remember read HMAC. */
			bcopy(data, auth, sc->sc_alen);
			auth += sc->sc_alen;
			/* TODO: bzero(9) can be commented out later. */
			bzero(data, sc->sc_alen);
		} else {
			bcopy(plaindata, data + sc->sc_alen, data_secsize);
			plaindata += data_secsize;
		}

		crp->crp_session = wr->w_sid;
		crp->crp_ilen = sc->sc_alen + data_secsize;
		crp->crp_olen = data_secsize;
		crp->crp_opaque = (void *)bp;
		crp->crp_buf = (void *)data;
		data += encr_secsize;
		crp->crp_flags = CRYPTO_F_CBIFSYNC;
		if (g_eli_batch)
			crp->crp_flags |= CRYPTO_F_BATCH;
		if (bp->bio_cmd == BIO_WRITE) {
			crp->crp_callback = g_eli_auth_write_done;
			crp->crp_desc = crde;
			crde->crd_next = crda;
			crda->crd_next = NULL;
		} else {
			crp->crp_callback = g_eli_auth_read_done;
			crp->crp_desc = crda;
			crda->crd_next = crde;
			crde->crd_next = NULL;
		}

		crde->crd_skip = sc->sc_alen;
		crde->crd_len = data_secsize;
		crde->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		if ((sc->sc_flags & G_ELI_FLAG_FIRST_KEY) == 0)
			crde->crd_flags |= CRD_F_KEY_EXPLICIT;
		if (bp->bio_cmd == BIO_WRITE)
			crde->crd_flags |= CRD_F_ENCRYPT;
		crde->crd_alg = sc->sc_ealgo;
		crde->crd_key = g_eli_key_hold(sc, dstoff, encr_secsize);
		crde->crd_klen = sc->sc_ekeylen;
		if (sc->sc_ealgo == CRYPTO_AES_XTS)
			crde->crd_klen <<= 1;
		g_eli_crypto_ivgen(sc, dstoff, crde->crd_iv,
		    sizeof(crde->crd_iv));

		crda->crd_skip = sc->sc_alen;
		crda->crd_len = data_secsize;
		crda->crd_inject = 0;
		crda->crd_flags = CRD_F_KEY_EXPLICIT;
		crda->crd_alg = sc->sc_aalgo;
		g_eli_auth_keygen(sc, dstoff, authkey);
		crda->crd_key = authkey;
		crda->crd_klen = G_ELI_AUTH_SECKEYLEN * 8;

		crp->crp_etype = 0;
		error = crypto_dispatch(crp);
		KASSERT(error == 0, ("crypto_dispatch() failed (error=%d)",
		    error));
	}
}
