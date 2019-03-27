/*	$FreeBSD$	*/
/*	$OpenBSD: xform.h,v 1.8 2001/08/28 12:20:43 ben Exp $	*/

/*-
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 * Copyright (c) 2000 Angelos D. Keromytis
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by John-Mark Gurney
 * under sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
 *
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all source code copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifndef _CRYPTO_XFORM_AUTH_H_
#define _CRYPTO_XFORM_AUTH_H_

#include <sys/malloc.h>
#include <sys/errno.h>

#include <sys/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha224.h>
#include <crypto/sha2/sha256.h>
#include <crypto/sha2/sha384.h>
#include <crypto/sha2/sha512.h>
#include <opencrypto/rmd160.h>
#include <opencrypto/gmac.h>
#include <opencrypto/cbc_mac.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform_userland.h>

/* XXX use a define common with other hash stuff ! */
#define	AH_ALEN_MAX	64	/* max authenticator hash length */

/* Declarations */
struct auth_hash {
	int type;
	char *name;
	u_int16_t keysize;
	u_int16_t hashsize; 
	u_int16_t ctxsize;
	u_int16_t blocksize;
	void (*Init) (void *);
	void (*Setkey) (void *, const u_int8_t *, u_int16_t);
	void (*Reinit) (void *, const u_int8_t *, u_int16_t);
	int  (*Update) (void *, const u_int8_t *, u_int16_t);
	void (*Final) (u_int8_t *, void *);
};

extern struct auth_hash auth_hash_null;
extern struct auth_hash auth_hash_key_md5;
extern struct auth_hash auth_hash_key_sha1;
extern struct auth_hash auth_hash_hmac_md5;
extern struct auth_hash auth_hash_hmac_sha1;
extern struct auth_hash auth_hash_hmac_ripemd_160;
extern struct auth_hash auth_hash_hmac_sha2_224;
extern struct auth_hash auth_hash_hmac_sha2_256;
extern struct auth_hash auth_hash_hmac_sha2_384;
extern struct auth_hash auth_hash_hmac_sha2_512;
extern struct auth_hash auth_hash_sha1;
extern struct auth_hash auth_hash_sha2_224;
extern struct auth_hash auth_hash_sha2_256;
extern struct auth_hash auth_hash_sha2_384;
extern struct auth_hash auth_hash_sha2_512;
extern struct auth_hash auth_hash_nist_gmac_aes_128;
extern struct auth_hash auth_hash_nist_gmac_aes_192;
extern struct auth_hash auth_hash_nist_gmac_aes_256;
extern struct auth_hash auth_hash_blake2b;
extern struct auth_hash auth_hash_blake2s;
extern struct auth_hash auth_hash_poly1305;
extern struct auth_hash auth_hash_ccm_cbc_mac_128;
extern struct auth_hash auth_hash_ccm_cbc_mac_192;
extern struct auth_hash auth_hash_ccm_cbc_mac_256;

union authctx {
	MD5_CTX md5ctx;
	SHA1_CTX sha1ctx;
	RMD160_CTX rmd160ctx;
	SHA224_CTX sha224ctx;
	SHA256_CTX sha256ctx;
	SHA384_CTX sha384ctx;
	SHA512_CTX sha512ctx;
	struct aes_gmac_ctx aes_gmac_ctx;
	struct aes_cbc_mac_ctx aes_cbc_mac_ctx;
};

#endif /* _CRYPTO_XFORM_AUTH_H_ */
