/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/malloc.h>

#include <opencrypto/cryptosoft.h> /* for hmac_ipad_buffer and hmac_opad_buffer */
#include <opencrypto/xform.h>

#include "glxsb.h"

/*
 * Implementation notes.
 *
 * The Geode LX Security Block provides AES-128-CBC acceleration.
 * We implement all HMAC algorithms provided by crypto(9) framework so glxsb can work
 * with ipsec(4)
 *
 * This code was stolen from crypto/via/padlock_hash.c
 */

MALLOC_DECLARE(M_GLXSB);

static void
glxsb_hash_key_setup(struct glxsb_session *ses, caddr_t key, int klen)
{
	struct auth_hash *axf;
	int i;

	klen /= 8;
	axf = ses->ses_axf;

	for (i = 0; i < klen; i++)
		key[i] ^= HMAC_IPAD_VAL;

	axf->Init(ses->ses_ictx);
	axf->Update(ses->ses_ictx, key, klen);
	axf->Update(ses->ses_ictx, hmac_ipad_buffer, axf->blocksize - klen);

	for (i = 0; i < klen; i++)
		key[i] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

	axf->Init(ses->ses_octx);
	axf->Update(ses->ses_octx, key, klen);
	axf->Update(ses->ses_octx, hmac_opad_buffer, axf->blocksize - klen);

	for (i = 0; i < klen; i++)
		key[i] ^= HMAC_OPAD_VAL;
}

/*
 * Compute keyed-hash authenticator.
 */
static int
glxsb_authcompute(struct glxsb_session *ses, struct cryptodesc *crd,
    caddr_t buf, int flags)
{
	u_char hash[HASH_MAX_LEN];
	struct auth_hash *axf;
	union authctx ctx;
	int error;

	axf = ses->ses_axf;
	bcopy(ses->ses_ictx, &ctx, axf->ctxsize);
	error = crypto_apply(flags, buf, crd->crd_skip, crd->crd_len,
	    (int (*)(void *, void *, unsigned int))axf->Update, (caddr_t)&ctx);
	if (error != 0)
		return (error);
	axf->Final(hash, &ctx);

	bcopy(ses->ses_octx, &ctx, axf->ctxsize);
	axf->Update(&ctx, hash, axf->hashsize);
	axf->Final(hash, &ctx);

	/* Inject the authentication data */
	crypto_copyback(flags, buf, crd->crd_inject,
	ses->ses_mlen == 0 ? axf->hashsize : ses->ses_mlen, hash);
	return (0);
}

int
glxsb_hash_setup(struct glxsb_session *ses, struct cryptoini *macini)
{

	ses->ses_mlen = macini->cri_mlen;

	/* Find software structure which describes HMAC algorithm. */
	switch (macini->cri_alg) {
	case CRYPTO_NULL_HMAC:
		ses->ses_axf = &auth_hash_null;
		break;
	case CRYPTO_MD5_HMAC:
		ses->ses_axf = &auth_hash_hmac_md5;
		break;
	case CRYPTO_SHA1_HMAC:
		ses->ses_axf = &auth_hash_hmac_sha1;
		break;
	case CRYPTO_RIPEMD160_HMAC:
		ses->ses_axf = &auth_hash_hmac_ripemd_160;
		break;
	case CRYPTO_SHA2_256_HMAC:
		ses->ses_axf = &auth_hash_hmac_sha2_256;
		break;
	case CRYPTO_SHA2_384_HMAC:
		ses->ses_axf = &auth_hash_hmac_sha2_384;
		break;
	case CRYPTO_SHA2_512_HMAC:
		ses->ses_axf = &auth_hash_hmac_sha2_512;
		break;
	}

	/* Allocate memory for HMAC inner and outer contexts. */
	ses->ses_ictx = malloc(ses->ses_axf->ctxsize, M_GLXSB,
	    M_ZERO | M_NOWAIT);
	ses->ses_octx = malloc(ses->ses_axf->ctxsize, M_GLXSB,
	    M_ZERO | M_NOWAIT);
	if (ses->ses_ictx == NULL || ses->ses_octx == NULL)
		return (ENOMEM);

	/* Setup key if given. */
	if (macini->cri_key != NULL) {
		glxsb_hash_key_setup(ses, macini->cri_key,
		    macini->cri_klen);
	}
	return (0);
}

int
glxsb_hash_process(struct glxsb_session *ses, struct cryptodesc *maccrd,
    struct cryptop *crp)
{
	int error;

	if ((maccrd->crd_flags & CRD_F_KEY_EXPLICIT) != 0)
		glxsb_hash_key_setup(ses, maccrd->crd_key, maccrd->crd_klen);

	error = glxsb_authcompute(ses, maccrd, crp->crp_buf, crp->crp_flags);
	return (error);
}

void
glxsb_hash_free(struct glxsb_session *ses)
{

	if (ses->ses_ictx != NULL) {
		bzero(ses->ses_ictx, ses->ses_axf->ctxsize);
		free(ses->ses_ictx, M_GLXSB);
		ses->ses_ictx = NULL;
	}
	if (ses->ses_octx != NULL) {
		bzero(ses->ses_octx, ses->ses_axf->ctxsize);
		free(ses->ses_octx, M_GLXSB);
		ses->ses_octx = NULL;
	}
}
