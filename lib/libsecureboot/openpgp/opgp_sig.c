/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * RCSid:
 *	from: signer.c,v 1.10 2018/03/23 01:14:30 sjg
 *
 *	@(#) Copyright (c) 2012 Simon J. Gerraty
 *
 *	This file is provided in the hope that it will
 *	be of use.  There is absolutely NO WARRANTY.
 *	Permission to copy, redistribute or otherwise
 *	use this file is hereby granted provided that
 *	the above copyright notice and this notice are
 *	left intact.
 *
 *	Please send copies of changes and bug-fixes to:
 *	sjg@crufty.net
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "../libsecureboot-priv.h"
#ifdef _STANDALONE
#define warnx printf
#else

#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#endif

#include "decode.h"
#include "packet.h"

#ifdef USE_BEARSSL

#define get_error_string ve_error_get

void
initialize (void)
{
#ifdef _STANDALONE
    ve_trust_init();
#endif
}

#else

#include <openssl/err.h>

/**
 * @brief intialize OpenSSL
 */
void
initialize(void)
{
	static int once;

	if (once)
		return);
	once = 1;
	//CRYPTO_malloc_init();
	ERR_load_crypto_strings();
	OpenSSL_add_all_algorithms();
}

/**
 * @brief
 * last error from OpenSSL as a string
 */
char *
get_error_string(void)
{
	initialize();
	return (ERR_error_string(ERR_get_error(), NULL));
}
#endif

/**
 * @brief decode a signature packet
 *
 * We only support RSA
 *
 * @sa rfc4880:5.2
 */
ssize_t
decode_sig(int tag, unsigned char **pptr, size_t len, OpenPGP_sig *sig)
{
	unsigned char *ptr;
	unsigned char *pgpbytes;
	unsigned char *sp;
	int version;
	int hcount = 0;
	int ucount = 0;
	int stag = 0;
	int n;

	n = tag;			/* avoid unused */

	/*
	 * We need to keep a reference to the packet bytes
	 * as these form part of the signature data.
	 *
	 * @sa rfc4880:5.2.4
	 */
	pgpbytes = ptr = *pptr;
	version = *ptr++;
	if (version == 3) {
		ptr++;
		sig->pgpbytes = malloc(5);
		if (!sig->pgpbytes)
			return (-1);
		memcpy(sig->pgpbytes, ptr, 5);
		sig->pgpbytes_len = 5;
		sig->sig_type = *ptr++;
		ptr += 4;
		sig->key_id = octets2hex(ptr, 8);
		ptr += 8;
		sig->sig_alg = *ptr++;
		sig->hash_alg = *ptr++;
	} else if (version == 4) {
		sig->sig_type = *ptr++;
		sig->sig_alg = *ptr++;
		sig->hash_alg = *ptr++;
		hcount = octets2i(ptr, 2);
		ptr += 2;
		sig->pgpbytes_len = (size_t)hcount + 6;
		sig->pgpbytes = malloc(sig->pgpbytes_len + 6);
		if (!sig->pgpbytes)
			return (-1);
		memcpy(sig->pgpbytes, pgpbytes, sig->pgpbytes_len);
		sp = &sig->pgpbytes[sig->pgpbytes_len];
		*sp++ = 4;
		*sp++ = 255;
		memcpy(sp, i2octets(4, (int)sig->pgpbytes_len), 4);
		sig->pgpbytes_len += 6;

		while (hcount > 0) {
			sp = decode_subpacket(&ptr, &stag, &n);
			hcount -= n;
			/* can check stag to see if we care */
		}
		ucount = octets2i(ptr, 2);
		ptr += 2;
		while (ucount > 0) {
			sp = decode_subpacket(&ptr, &stag, &n);
			ucount -= n;
			/* can check stag to see if we care */
			if (stag == 16) {
				free(sig->key_id);
				sig->key_id = octets2hex(sp, 8);
			}
		}
	} else
		return (-1);
	ptr += 2;			/* skip hash16 */
	if (sig->sig_alg == 1) {	/* RSA */
		sig->sig = decode_mpi(&ptr, &sig->sig_len);
	}
	/* we are done */
	return ((ssize_t)len);
}

/**
 * @brief map OpenPGP hash algorithm id's to name
 *
 * @sa rfc4880:9.4
 */
static struct hash_alg_map {
	int halg;
	const char *hname;
} hash_algs[] = {
	{1, "md5"},
	{2, "sha1"},
	{8, "sha256"},
	{9, "sha384"},
	{10, "sha512"},
	{11, "sha224"},
	{0, NULL},
};

static const char *
get_hname(int hash_alg)
{
	struct hash_alg_map *hmp;

	for (hmp = hash_algs; hmp->halg > 0; hmp++) {
		if (hmp->halg == hash_alg)
			return (hmp->hname);
	}
	return (NULL);
}

/* lifted from signer.c */
/**
 * @brief verify a digest
 *
 * The public key, digest name, file and signature data.
 *
 * @return 1 on success 0 on failure, -1 on error
 */
#ifndef USE_BEARSSL
static int
verify_digest (EVP_PKEY *pkey,
    const char *digest,
    unsigned char *mdata, size_t mlen,
    unsigned char *sdata, size_t slen)
{
	EVP_MD_CTX ctx;
	const EVP_MD *md = NULL;
	EVP_PKEY_CTX *pctx = NULL;
	int rc = 0;
	int i = -1;

	initialize();
	md = EVP_get_digestbyname(digest);
	EVP_DigestInit(&ctx, md);

	pctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (!pctx)
		goto fail;
	if (EVP_PKEY_verify_init(pctx) <= 0)
		goto fail;
	if (EVP_PKEY_CTX_set_signature_md(pctx, ctx.digest) <= 0)
		goto fail;
	i = EVP_PKEY_verify(pctx, sdata, slen, mdata, mlen);
	if (i >= 0)
		rc = i;
fail:
	EVP_PKEY_CTX_free(pctx);
	return (rc);
}
#endif


/**
 * @brief verify OpenPGP signed file
 *
 *
 * @param[in] filename
 *	used to determine the signature name
 *
 * @param[in] fdata
 *	content of filename
 *
 * @param[in] fbytes
 *	of fdata
 *
 * @param[in] sdata
 *	content of signature
 *
 * @param[in] sbytes
 *	of sdata
 *
 * @param[in] flags
 *
 * @return 0 on success
 */
int
openpgp_verify(const char *filename,
    unsigned char *fdata, size_t fbytes,
    unsigned char *sdata, size_t sbytes,
    int flags)
{
	OpenPGP_key *key;
	OpenPGP_sig *sig;
#ifdef USE_BEARSSL
	const br_hash_class *md;
	br_hash_compat_context mctx;
	const unsigned char *hash_oid;
#else
	const EVP_MD *md = NULL;
	EVP_MD_CTX mctx;
#endif
	unsigned char mdata[64];
	unsigned char *ptr;
	unsigned char *ddata = NULL;
	const char *hname;
	size_t mlen;
	int rc = -1;

	initialize();

	sig = NEW(OpenPGP_sig);
	if (!sdata || !sig) {
		warnx("cannot verify %s", filename);
		goto oops;
	}
	if (!(sdata[0] & OPENPGP_TAG_ISTAG))
		sdata = ddata = dearmor((char *)sdata, sbytes, &sbytes);
	ptr = sdata;
	rc = decode_packet(2, &ptr, sbytes, (decoder_t)decode_sig, sig);
	if (rc == 0 && sig->key_id) {
		key = load_key_id(sig->key_id);
		if (!key) {
			warnx("cannot find key-id: %s", sig->key_id);
			rc = -1;
		} else if (!(hname = get_hname(sig->hash_alg))) {
			warnx("unsupported hash algorithm: %d", sig->hash_alg);
			rc = -1;
		} else {
			/*
			 * Hash fdata according to the OpenPGP recipe
			 *
			 * @sa rfc4880:5.2.4
			 */
#ifdef USE_BEARSSL
			switch (sig->hash_alg) { /* see hash_algs above */
			case 2:			 /* sha1 */
				md = &br_sha1_vtable;
				mlen = br_sha1_SIZE;
				hash_oid = BR_HASH_OID_SHA1;
				break;
			case 8:			/* sha256 */
				md = &br_sha256_vtable;
				mlen = br_sha256_SIZE;
				hash_oid = BR_HASH_OID_SHA256;
				break;
			default:
				warnx("unsupported hash algorithm: %s", hname);
				goto oops;
			}
			md->init(&mctx.vtable);
			md->update(&mctx.vtable, fdata, fbytes);
			md->update(&mctx.vtable, sig->pgpbytes,
			    sig->pgpbytes_len);
			md->out(&mctx.vtable, mdata);

			rc = verify_rsa_digest(key->key, hash_oid,
			    mdata, mlen, sig->sig, sig->sig_len);
#else
			md = EVP_get_digestbyname(hname);
			EVP_DigestInit(&mctx, md);
			EVP_DigestUpdate(&mctx, fdata, fbytes);
			EVP_DigestUpdate(&mctx, sig->pgpbytes,
			    sig->pgpbytes_len);
			mlen = sizeof(mdata);
			EVP_DigestFinal(&mctx,mdata,(unsigned int *)&mlen);

			rc = verify_digest(key->key, hname, mdata, mlen,
			    sig->sig, sig->sig_len);
#endif

			if (rc > 0) {
				if ((flags & 1))
					printf("Verified %s signed by %s\n",
					    filename,
					    key->user ? key->user->name : "someone");
				rc = 0;	/* success */
			} else if (rc == 0) {
				printf("Unverified %s: %s\n",
				    filename, get_error_string());
				rc = 1;
			} else {
				printf("Unverified %s\n", filename);
			}
		}
	} else {
		warnx("cannot decode signature for %s", filename);
		rc = -1;
	}
oops:
	free(ddata);
	free(sig);
	return (rc);
}

#ifndef _STANDALONE
/**
 * @brief list of extensions we handle
 *
 * ".asc" is preferred as it works seamlessly with openpgp
 */
static const char *sig_exts[] = {
	".asc",
	".pgp",
	".psig",
	NULL,
};

/**
 * @brief verify OpenPGP signed file
 *
 *
 * @param[in] filename
 *	used to determine the signature name
 *
 * @param[in] fdata
 *	content of filename
 *
 * @param[in] nbytes
 *	of fdata
 *
 * @return
 */

int
openpgp_verify_file(const char *filename, unsigned char *fdata, size_t nbytes)
{
	char pbuf[MAXPATHLEN];
	unsigned char *sdata;
	const char *sname = NULL;
	const char **ep;
	size_t sz;
	int n;

	for (ep = sig_exts; *ep; ep++) {
		n = snprintf(pbuf, sizeof(pbuf), "%s%s", filename, *ep);
		if (n >= (int)sizeof(pbuf)) {
			warnx("cannot form signature name for %s", filename);
			return (-1);
		}
		if (access(pbuf, R_OK) == 0) {
			sname = pbuf;
			break;
		}
	}
	if (!sname) {
		warnx("cannot find signature for %s", filename);
		return (-1);
	}
	sdata = read_file(sname, &sz);
	return (openpgp_verify(filename, fdata, nbytes, sdata, sz, 1));
}
#endif

/**
 * @brief verify OpenPGP signature
 *
 * @return content of signed file
 */
unsigned char *
verify_asc(const char *sigfile, int flags)
{
	char pbuf[MAXPATHLEN];
	char *cp;
	size_t n;
	unsigned char *fdata, *sdata;
	size_t fbytes, sbytes;
    
	if ((sdata = read_file(sigfile, &sbytes))) {
		n = strlcpy(pbuf, sigfile, sizeof(pbuf));
		if ((cp = strrchr(pbuf, '.')))
			*cp = '\0';
		if ((fdata = read_file(pbuf, &fbytes))) {
			if (openpgp_verify(pbuf, fdata, fbytes, sdata,
				sbytes, flags)) {
				free(fdata);
				fdata = NULL;
			}
		}
	} else
		fdata = NULL;
	free(sdata);
	return (fdata);
}
