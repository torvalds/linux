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
 * Code paths:
 * BIO_READ:
 *	g_eli_start -> g_eli_crypto_read -> g_io_request -> g_eli_read_done -> g_eli_crypto_run -> g_eli_crypto_read_done -> g_io_deliver
 * BIO_WRITE:
 *	g_eli_start -> g_eli_crypto_run -> g_eli_crypto_write_done -> g_io_request -> g_eli_write_done -> g_io_deliver
 */

MALLOC_DECLARE(M_ELI);

/*
 * The function is called after we read and decrypt data.
 *
 * g_eli_start -> g_eli_crypto_read -> g_io_request -> g_eli_read_done -> g_eli_crypto_run -> G_ELI_CRYPTO_READ_DONE -> g_io_deliver
 */
static int
g_eli_crypto_read_done(struct cryptop *crp)
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
		G_ELI_DEBUG(3, "Crypto READ request done (%d/%d).",
		    bp->bio_inbed, bp->bio_children);
		bp->bio_completed += crp->crp_olen;
	} else {
		G_ELI_DEBUG(1, "Crypto READ request failed (%d/%d) error=%d.",
		    bp->bio_inbed, bp->bio_children, crp->crp_etype);
		if (bp->bio_error == 0)
			bp->bio_error = crp->crp_etype;
	}
	sc = bp->bio_to->geom->softc;
	if (sc != NULL)
		g_eli_key_drop(sc, crp->crp_desc->crd_key);
	/*
	 * Do we have all sectors already?
	 */
	if (bp->bio_inbed < bp->bio_children)
		return (0);
	free(bp->bio_driver2, M_ELI);
	bp->bio_driver2 = NULL;
	if (bp->bio_error != 0) {
		G_ELI_LOGREQ(0, bp, "Crypto READ request failed (error=%d).",
		    bp->bio_error);
		bp->bio_completed = 0;
	}
	/*
	 * Read is finished, send it up.
	 */
	g_io_deliver(bp, bp->bio_error);
	if (sc != NULL)
		atomic_subtract_int(&sc->sc_inflight, 1);
	return (0);
}

/*
 * The function is called after data encryption.
 *
 * g_eli_start -> g_eli_crypto_run -> G_ELI_CRYPTO_WRITE_DONE -> g_io_request -> g_eli_write_done -> g_io_deliver
 */
static int
g_eli_crypto_write_done(struct cryptop *crp)
{
	struct g_eli_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct bio *bp, *cbp;

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
	gp = bp->bio_to->geom;
	sc = gp->softc;
	g_eli_key_drop(sc, crp->crp_desc->crd_key);
	/*
	 * All sectors are already encrypted?
	 */
	if (bp->bio_inbed < bp->bio_children)
		return (0);
	bp->bio_inbed = 0;
	bp->bio_children = 1;
	cbp = bp->bio_driver1;
	bp->bio_driver1 = NULL;
	if (bp->bio_error != 0) {
		G_ELI_LOGREQ(0, bp, "Crypto WRITE request failed (error=%d).",
		    bp->bio_error);
		free(bp->bio_driver2, M_ELI);
		bp->bio_driver2 = NULL;
		g_destroy_bio(cbp);
		g_io_deliver(bp, bp->bio_error);
		atomic_subtract_int(&sc->sc_inflight, 1);
		return (0);
	}
	cbp->bio_data = bp->bio_driver2;
	cbp->bio_done = g_eli_write_done;
	cp = LIST_FIRST(&gp->consumer);
	cbp->bio_to = cp->provider;
	G_ELI_LOGREQ(2, cbp, "Sending request.");
	/*
	 * Send encrypted data to the provider.
	 */
	g_io_request(cbp, cp);
	return (0);
}

/*
 * The function is called to read encrypted data.
 *
 * g_eli_start -> G_ELI_CRYPTO_READ -> g_io_request -> g_eli_read_done -> g_eli_crypto_run -> g_eli_crypto_read_done -> g_io_deliver
 */
void
g_eli_crypto_read(struct g_eli_softc *sc, struct bio *bp, boolean_t fromworker)
{
	struct g_consumer *cp;
	struct bio *cbp;

	if (!fromworker) {
		/*
		 * We are not called from the worker thread, so check if
		 * device is suspended.
		 */
		mtx_lock(&sc->sc_queue_mtx);
		if (sc->sc_flags & G_ELI_FLAG_SUSPEND) {
			/*
			 * If device is suspended, we place the request onto
			 * the queue, so it can be handled after resume.
			 */
			G_ELI_DEBUG(0, "device suspended, move onto queue");
			bioq_insert_tail(&sc->sc_queue, bp);
			mtx_unlock(&sc->sc_queue_mtx);
			wakeup(sc);
			return;
		}
		atomic_add_int(&sc->sc_inflight, 1);
		mtx_unlock(&sc->sc_queue_mtx);
	}
	bp->bio_pflags = 0;
	bp->bio_driver2 = NULL;
	cbp = bp->bio_driver1;
	cbp->bio_done = g_eli_read_done;
	cp = LIST_FIRST(&sc->sc_geom->consumer);
	cbp->bio_to = cp->provider;
	G_ELI_LOGREQ(2, cbp, "Sending request.");
	/*
	 * Read encrypted data from provider.
	 */
	g_io_request(cbp, cp);
}

/*
 * This is the main function responsible for cryptography (ie. communication
 * with crypto(9) subsystem).
 *
 * BIO_READ:
 *	g_eli_start -> g_eli_crypto_read -> g_io_request -> g_eli_read_done -> G_ELI_CRYPTO_RUN -> g_eli_crypto_read_done -> g_io_deliver
 * BIO_WRITE:
 *	g_eli_start -> G_ELI_CRYPTO_RUN -> g_eli_crypto_write_done -> g_io_request -> g_eli_write_done -> g_io_deliver
 */
void
g_eli_crypto_run(struct g_eli_worker *wr, struct bio *bp)
{
	struct g_eli_softc *sc;
	struct cryptop *crp;
	struct cryptodesc *crd;
	u_int i, nsec, secsize;
	off_t dstoff;
	size_t size;
	u_char *p, *data;
	int error;

	G_ELI_LOGREQ(3, bp, "%s", __func__);

	bp->bio_pflags = wr->w_number;
	sc = wr->w_softc;
	secsize = LIST_FIRST(&sc->sc_geom->provider)->sectorsize;
	nsec = bp->bio_length / secsize;

	/*
	 * Calculate how much memory do we need.
	 * We need separate crypto operation for every single sector.
	 * It is much faster to calculate total amount of needed memory here and
	 * do the allocation once instead of allocating memory in pieces (many,
	 * many pieces).
	 */
	size = sizeof(*crp) * nsec;
	size += sizeof(*crd) * nsec;
	/*
	 * If we write the data we cannot destroy current bio_data content,
	 * so we need to allocate more memory for encrypted data.
	 */
	if (bp->bio_cmd == BIO_WRITE)
		size += bp->bio_length;
	p = malloc(size, M_ELI, M_WAITOK);

	bp->bio_inbed = 0;
	bp->bio_children = nsec;
	bp->bio_driver2 = p;

	if (bp->bio_cmd == BIO_READ)
		data = bp->bio_data;
	else {
		data = p;
		p += bp->bio_length;
		bcopy(bp->bio_data, data, bp->bio_length);
	}

	for (i = 0, dstoff = bp->bio_offset; i < nsec; i++, dstoff += secsize) {
		crp = (struct cryptop *)p;	p += sizeof(*crp);
		crd = (struct cryptodesc *)p;	p += sizeof(*crd);

		crp->crp_session = wr->w_sid;
		crp->crp_ilen = secsize;
		crp->crp_olen = secsize;
		crp->crp_opaque = (void *)bp;
		crp->crp_buf = (void *)data;
		data += secsize;
		if (bp->bio_cmd == BIO_WRITE)
			crp->crp_callback = g_eli_crypto_write_done;
		else /* if (bp->bio_cmd == BIO_READ) */
			crp->crp_callback = g_eli_crypto_read_done;
		crp->crp_flags = CRYPTO_F_CBIFSYNC;
		if (g_eli_batch)
			crp->crp_flags |= CRYPTO_F_BATCH;
		crp->crp_desc = crd;

		crd->crd_skip = 0;
		crd->crd_len = secsize;
		crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		if ((sc->sc_flags & G_ELI_FLAG_SINGLE_KEY) == 0)
			crd->crd_flags |= CRD_F_KEY_EXPLICIT;
		if (bp->bio_cmd == BIO_WRITE)
			crd->crd_flags |= CRD_F_ENCRYPT;
		crd->crd_alg = sc->sc_ealgo;
		crd->crd_key = g_eli_key_hold(sc, dstoff, secsize);
		crd->crd_klen = sc->sc_ekeylen;
		if (sc->sc_ealgo == CRYPTO_AES_XTS)
			crd->crd_klen <<= 1;
		g_eli_crypto_ivgen(sc, dstoff, crd->crd_iv,
		    sizeof(crd->crd_iv));
		crd->crd_next = NULL;

		crp->crp_etype = 0;
		error = crypto_dispatch(crp);
		KASSERT(error == 0, ("crypto_dispatch() failed (error=%d)",
		    error));
	}
}
