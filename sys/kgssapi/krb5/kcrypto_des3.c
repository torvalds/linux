/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/kobj.h>
#include <sys/mbuf.h>
#include <crypto/des/des.h>
#include <opencrypto/cryptodev.h>

#include <kgssapi/gssapi.h>
#include <kgssapi/gssapi_impl.h>

#include "kcrypto.h"

#define DES3_FLAGS	(CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE)

struct des3_state {
	struct mtx	ds_lock;
	crypto_session_t ds_session;
};

static void
des3_init(struct krb5_key_state *ks)
{
	struct des3_state *ds;

	ds = malloc(sizeof(struct des3_state), M_GSSAPI, M_WAITOK|M_ZERO);
	mtx_init(&ds->ds_lock, "gss des3 lock", NULL, MTX_DEF);
	ks->ks_priv = ds;
}

static void
des3_destroy(struct krb5_key_state *ks)
{
	struct des3_state *ds = ks->ks_priv;

	if (ds->ds_session)
		crypto_freesession(ds->ds_session);
	mtx_destroy(&ds->ds_lock);
	free(ks->ks_priv, M_GSSAPI);
}

static void
des3_set_key(struct krb5_key_state *ks, const void *in)
{
	void *kp = ks->ks_key;
	struct des3_state *ds = ks->ks_priv;
	struct cryptoini cri[2];

	if (kp != in)
		bcopy(in, kp, ks->ks_class->ec_keylen);

	if (ds->ds_session)
		crypto_freesession(ds->ds_session);

	bzero(cri, sizeof(cri));

	cri[0].cri_alg = CRYPTO_SHA1_HMAC;
	cri[0].cri_klen = 192;
	cri[0].cri_mlen = 0;
	cri[0].cri_key = ks->ks_key;
	cri[0].cri_next = &cri[1];

	cri[1].cri_alg = CRYPTO_3DES_CBC;
	cri[1].cri_klen = 192;
	cri[1].cri_mlen = 0;
	cri[1].cri_key = ks->ks_key;
	cri[1].cri_next = NULL;

	crypto_newsession(&ds->ds_session, cri,
	    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE);
}

static void
des3_random_to_key(struct krb5_key_state *ks, const void *in)
{
	uint8_t *outkey;
	const uint8_t *inkey;
	int subkey;

	for (subkey = 0, outkey = ks->ks_key, inkey = in; subkey < 3;
	     subkey++, outkey += 8, inkey += 7) {
		/*
		 * Expand 56 bits of random data to 64 bits as follows
		 * (in the example, bit number 1 is the MSB of the 56
		 * bits of random data):
		 *
		 * expanded = 
		 *	 1  2  3  4  5  6  7  p
		 *	 9 10 11 12 13 14 15  p
		 *	17 18 19 20 21 22 23  p
		 *	25 26 27 28 29 30 31  p
		 *	33 34 35 36 37 38 39  p
		 *	41 42 43 44 45 46 47  p
		 *	49 50 51 52 53 54 55  p
		 *	56 48 40 32 24 16  8  p
		 */
		outkey[0] = inkey[0];
		outkey[1] = inkey[1];
		outkey[2] = inkey[2];
		outkey[3] = inkey[3];
		outkey[4] = inkey[4];
		outkey[5] = inkey[5];
		outkey[6] = inkey[6];
		outkey[7] = (((inkey[0] & 1) << 1)
		    | ((inkey[1] & 1) << 2)
		    | ((inkey[2] & 1) << 3)
		    | ((inkey[3] & 1) << 4)
		    | ((inkey[4] & 1) << 5)
		    | ((inkey[5] & 1) << 6)
		    | ((inkey[6] & 1) << 7));
		des_set_odd_parity((des_cblock *) outkey);
		if (des_is_weak_key((des_cblock *) outkey))
			outkey[7] ^= 0xf0;
	}

	des3_set_key(ks, ks->ks_key);
}

static int
des3_crypto_cb(struct cryptop *crp)
{
	int error;
	struct des3_state *ds = (struct des3_state *) crp->crp_opaque;
	
	if (crypto_ses2caps(ds->ds_session) & CRYPTOCAP_F_SYNC)
		return (0);

	error = crp->crp_etype;
	if (error == EAGAIN)
		error = crypto_dispatch(crp);
	mtx_lock(&ds->ds_lock);
	if (error || (crp->crp_flags & CRYPTO_F_DONE))
		wakeup(crp);
	mtx_unlock(&ds->ds_lock);

	return (0);
}

static void
des3_encrypt_1(const struct krb5_key_state *ks, struct mbuf *inout,
    size_t skip, size_t len, void *ivec, int encdec)
{
	struct des3_state *ds = ks->ks_priv;
	struct cryptop *crp;
	struct cryptodesc *crd;
	int error;

	crp = crypto_getreq(1);
	crd = crp->crp_desc;

	crd->crd_skip = skip;
	crd->crd_len = len;
	crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT | encdec;
	if (ivec) {
		bcopy(ivec, crd->crd_iv, 8);
	} else {
		bzero(crd->crd_iv, 8);
	}
	crd->crd_next = NULL;
	crd->crd_alg = CRYPTO_3DES_CBC;

	crp->crp_session = ds->ds_session;
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	crp->crp_buf = (void *) inout;
	crp->crp_opaque = (void *) ds;
	crp->crp_callback = des3_crypto_cb;

	error = crypto_dispatch(crp);

	if ((crypto_ses2caps(ds->ds_session) & CRYPTOCAP_F_SYNC) == 0) {
		mtx_lock(&ds->ds_lock);
		if (!error && !(crp->crp_flags & CRYPTO_F_DONE))
			error = msleep(crp, &ds->ds_lock, 0, "gssdes3", 0);
		mtx_unlock(&ds->ds_lock);
	}

	crypto_freereq(crp);
}

static void
des3_encrypt(const struct krb5_key_state *ks, struct mbuf *inout,
    size_t skip, size_t len, void *ivec, size_t ivlen)
{

	des3_encrypt_1(ks, inout, skip, len, ivec, CRD_F_ENCRYPT);
}

static void
des3_decrypt(const struct krb5_key_state *ks, struct mbuf *inout,
    size_t skip, size_t len, void *ivec, size_t ivlen)
{

	des3_encrypt_1(ks, inout, skip, len, ivec, 0);
}

static void
des3_checksum(const struct krb5_key_state *ks, int usage,
    struct mbuf *inout, size_t skip, size_t inlen, size_t outlen)
{
	struct des3_state *ds = ks->ks_priv;
	struct cryptop *crp;
	struct cryptodesc *crd;
	int error;

	crp = crypto_getreq(1);
	crd = crp->crp_desc;

	crd->crd_skip = skip;
	crd->crd_len = inlen;
	crd->crd_inject = skip + inlen;
	crd->crd_flags = 0;
	crd->crd_next = NULL;
	crd->crd_alg = CRYPTO_SHA1_HMAC;

	crp->crp_session = ds->ds_session;
	crp->crp_ilen = inlen;
	crp->crp_olen = 20;
	crp->crp_etype = 0;
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	crp->crp_buf = (void *) inout;
	crp->crp_opaque = (void *) ds;
	crp->crp_callback = des3_crypto_cb;

	error = crypto_dispatch(crp);

	if ((crypto_ses2caps(ds->ds_session) & CRYPTOCAP_F_SYNC) == 0) {
		mtx_lock(&ds->ds_lock);
		if (!error && !(crp->crp_flags & CRYPTO_F_DONE))
			error = msleep(crp, &ds->ds_lock, 0, "gssdes3", 0);
		mtx_unlock(&ds->ds_lock);
	}

	crypto_freereq(crp);
}

struct krb5_encryption_class krb5_des3_encryption_class = {
	"des3-cbc-sha1",	/* name */
	ETYPE_DES3_CBC_SHA1,	/* etype */
	EC_DERIVED_KEYS,	/* flags */
	8,			/* blocklen */
	8,			/* msgblocklen */
	20,			/* checksumlen */
	168,			/* keybits */
	24,			/* keylen */
	des3_init,
	des3_destroy,
	des3_set_key,
	des3_random_to_key,
	des3_encrypt,
	des3_decrypt,
	des3_checksum
};

#if 0
struct des3_dk_test {
	uint8_t key[24];
	uint8_t usage[8];
	size_t usagelen;
	uint8_t dk[24];
};
struct des3_dk_test tests[] = {
	{{0xdc, 0xe0, 0x6b, 0x1f, 0x64, 0xc8, 0x57, 0xa1, 0x1c, 0x3d, 0xb5,
	  0x7c, 0x51, 0x89, 0x9b, 0x2c, 0xc1, 0x79, 0x10, 0x08, 0xce, 0x97,
	  0x3b, 0x92},
	 {0x00, 0x00, 0x00, 0x01, 0x55}, 5,
	 {0x92, 0x51, 0x79, 0xd0, 0x45, 0x91, 0xa7, 0x9b, 0x5d, 0x31, 0x92,
	  0xc4, 0xa7, 0xe9, 0xc2, 0x89, 0xb0, 0x49, 0xc7, 0x1f, 0x6e, 0xe6,
	  0x04, 0xcd}},

	{{0x5e, 0x13, 0xd3, 0x1c, 0x70, 0xef, 0x76, 0x57, 0x46, 0x57, 0x85,
	  0x31, 0xcb, 0x51, 0xc1, 0x5b, 0xf1, 0x1c, 0xa8, 0x2c, 0x97, 0xce,
	  0xe9, 0xf2},
	 {0x00, 0x00, 0x00, 0x01, 0xaa}, 5,
	 {0x9e, 0x58, 0xe5, 0xa1, 0x46, 0xd9, 0x94, 0x2a, 0x10, 0x1c, 0x46,
	  0x98, 0x45, 0xd6, 0x7a, 0x20, 0xe3, 0xc4, 0x25, 0x9e, 0xd9, 0x13,
	  0xf2, 0x07}},

	{{0x98, 0xe6, 0xfd, 0x8a, 0x04, 0xa4, 0xb6, 0x85, 0x9b, 0x75, 0xa1,
	  0x76, 0x54, 0x0b, 0x97, 0x52, 0xba, 0xd3, 0xec, 0xd6, 0x10, 0xa2,
	  0x52, 0xbc},
	 {0x00, 0x00, 0x00, 0x01, 0x55}, 5,
	 {0x13, 0xfe, 0xf8, 0x0d, 0x76, 0x3e, 0x94, 0xec, 0x6d, 0x13, 0xfd,
	  0x2c, 0xa1, 0xd0, 0x85, 0x07, 0x02, 0x49, 0xda, 0xd3, 0x98, 0x08,
	  0xea, 0xbf}},

	{{0x62, 0x2a, 0xec, 0x25, 0xa2, 0xfe, 0x2c, 0xad, 0x70, 0x94, 0x68,
	  0x0b, 0x7c, 0x64, 0x94, 0x02, 0x80, 0x08, 0x4c, 0x1a, 0x7c, 0xec,
	  0x92, 0xb5},
	 {0x00, 0x00, 0x00, 0x01, 0xaa}, 5,
	 {0xf8, 0xdf, 0xbf, 0x04, 0xb0, 0x97, 0xe6, 0xd9, 0xdc, 0x07, 0x02,
	  0x68, 0x6b, 0xcb, 0x34, 0x89, 0xd9, 0x1f, 0xd9, 0xa4, 0x51, 0x6b,
	  0x70, 0x3e}},

	{{0xd3, 0xf8, 0x29, 0x8c, 0xcb, 0x16, 0x64, 0x38, 0xdc, 0xb9, 0xb9,
	  0x3e, 0xe5, 0xa7, 0x62, 0x92, 0x86, 0xa4, 0x91, 0xf8, 0x38, 0xf8,
	  0x02, 0xfb},
	 {0x6b, 0x65, 0x72, 0x62, 0x65, 0x72, 0x6f, 0x73}, 8,
	 {0x23, 0x70, 0xda, 0x57, 0x5d, 0x2a, 0x3d, 0xa8, 0x64, 0xce, 0xbf,
	  0xdc, 0x52, 0x04, 0xd5, 0x6d, 0xf7, 0x79, 0xa7, 0xdf, 0x43, 0xd9,
	  0xda, 0x43}},

	{{0xc1, 0x08, 0x16, 0x49, 0xad, 0xa7, 0x43, 0x62, 0xe6, 0xa1, 0x45,
	  0x9d, 0x01, 0xdf, 0xd3, 0x0d, 0x67, 0xc2, 0x23, 0x4c, 0x94, 0x07,
	  0x04, 0xda},
	 {0x00, 0x00, 0x00, 0x01, 0x55}, 5,
	 {0x34, 0x80, 0x57, 0xec, 0x98, 0xfd, 0xc4, 0x80, 0x16, 0x16, 0x1c,
	  0x2a, 0x4c, 0x7a, 0x94, 0x3e, 0x92, 0xae, 0x49, 0x2c, 0x98, 0x91,
	  0x75, 0xf7}},

	{{0x5d, 0x15, 0x4a, 0xf2, 0x38, 0xf4, 0x67, 0x13, 0x15, 0x57, 0x19,
	  0xd5, 0x5e, 0x2f, 0x1f, 0x79, 0x0d, 0xd6, 0x61, 0xf2, 0x79, 0xa7,
	  0x91, 0x7c},
	 {0x00, 0x00, 0x00, 0x01, 0xaa}, 5,
	 {0xa8, 0x80, 0x8a, 0xc2, 0x67, 0xda, 0xda, 0x3d, 0xcb, 0xe9, 0xa7,
	  0xc8, 0x46, 0x26, 0xfb, 0xc7, 0x61, 0xc2, 0x94, 0xb0, 0x13, 0x15,
	  0xe5, 0xc1}},

	{{0x79, 0x85, 0x62, 0xe0, 0x49, 0x85, 0x2f, 0x57, 0xdc, 0x8c, 0x34,
	  0x3b, 0xa1, 0x7f, 0x2c, 0xa1, 0xd9, 0x73, 0x94, 0xef, 0xc8, 0xad,
	  0xc4, 0x43},
	 {0x00, 0x00, 0x00, 0x01, 0x55}, 5,
	 {0xc8, 0x13, 0xf8, 0x8a, 0x3b, 0xe3, 0xb3, 0x34, 0xf7, 0x54, 0x25,
	  0xce, 0x91, 0x75, 0xfb, 0xe3, 0xc8, 0x49, 0x3b, 0x89, 0xc8, 0x70,
	  0x3b, 0x49}},

	{{0x26, 0xdc, 0xe3, 0x34, 0xb5, 0x45, 0x29, 0x2f, 0x2f, 0xea, 0xb9,
	  0xa8, 0x70, 0x1a, 0x89, 0xa4, 0xb9, 0x9e, 0xb9, 0x94, 0x2c, 0xec,
	  0xd0, 0x16},
	 {0x00, 0x00, 0x00, 0x01, 0xaa}, 5,
	 {0xf4, 0x8f, 0xfd, 0x6e, 0x83, 0xf8, 0x3e, 0x73, 0x54, 0xe6, 0x94,
	  0xfd, 0x25, 0x2c, 0xf8, 0x3b, 0xfe, 0x58, 0xf7, 0xd5, 0xba, 0x37,
	  0xec, 0x5d}},
};
#define N_TESTS		(sizeof(tests) / sizeof(tests[0]))

int
main(int argc, char **argv)
{
	struct krb5_key_state *key, *dk;
	uint8_t *dkp;
	int j, i;

	for (j = 0; j < N_TESTS; j++) {
		struct des3_dk_test *t = &tests[j];
		key = krb5_create_key(&des3_encryption_class);
		krb5_set_key(key, t->key);
		dk = krb5_derive_key(key, t->usage, t->usagelen);
		krb5_free_key(key);
		if (memcmp(dk->ks_key, t->dk, 24)) {
			printf("DES3 dk(");
			for (i = 0; i < 24; i++)
				printf("%02x", t->key[i]);
			printf(", ");
			for (i = 0; i < t->usagelen; i++)
				printf("%02x", t->usage[i]);
			printf(") failed\n");
			printf("should be: ");
			for (i = 0; i < 24; i++)
				printf("%02x", t->dk[i]);
			printf("\n result was: ");
			dkp = dk->ks_key;
			for (i = 0; i < 24; i++)
				printf("%02x", dkp[i]);
			printf("\n");
		}
		krb5_free_key(dk);
	}

	return (0);
}
#endif
