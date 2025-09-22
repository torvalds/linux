/*
 * iterated_hash.c -- nsec3 hash calculation.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 * With thanks to Ben Laurie.
 */
#include "config.h"
#ifdef NSEC3
#if defined(HAVE_SHA1_INIT) && !defined(DEPRECATED_SHA1_INIT)
#include <openssl/sha.h>
#else
#include <openssl/evp.h>
#endif
#include <stdio.h>
#include <assert.h>

#include "iterated_hash.h"
#include "util.h"

int
iterated_hash(unsigned char out[SHA_DIGEST_LENGTH],
	const unsigned char *salt, int saltlength,
	const unsigned char *in, int inlength, int iterations)
{
#if defined(NSEC3) && defined(HAVE_SSL)
#if defined(HAVE_SHA1_INIT) && !defined(DEPRECATED_SHA1_INIT)
	SHA_CTX ctx;
#else
	EVP_MD_CTX* ctx;
#endif
	int n;
#if defined(HAVE_SHA1_INIT) && !defined(DEPRECATED_SHA1_INIT)
#else
	ctx = EVP_MD_CTX_create();
	if(!ctx) {
		log_msg(LOG_ERR, "out of memory in iterated_hash");
		return 0;
	}
#endif
	assert(in && inlength > 0 && iterations >= 0);
	for(n=0 ; n <= iterations ; ++n)
	{
#if defined(HAVE_SHA1_INIT) && !defined(DEPRECATED_SHA1_INIT)
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, in, inlength);
		if(saltlength > 0)
			SHA1_Update(&ctx, salt, saltlength);
		SHA1_Final(out, &ctx);
#else
		if(!EVP_DigestInit(ctx, EVP_sha1()))
			log_msg(LOG_ERR, "iterated_hash could not EVP_DigestInit");

		if(!EVP_DigestUpdate(ctx, in, inlength))
			log_msg(LOG_ERR, "iterated_hash could not EVP_DigestUpdate");
		if(saltlength > 0) {
			if(!EVP_DigestUpdate(ctx, salt, saltlength))
				log_msg(LOG_ERR, "iterated_hash could not EVP_DigestUpdate salt");
		}
		if(!EVP_DigestFinal_ex(ctx, out, NULL))
			log_msg(LOG_ERR, "iterated_hash could not EVP_DigestFinal_ex");
#endif
		in=out;
		inlength=SHA_DIGEST_LENGTH;
	}
#if defined(HAVE_SHA1_INIT) && !defined(DEPRECATED_SHA1_INIT)
#else
	EVP_MD_CTX_destroy(ctx);
#endif
	return SHA_DIGEST_LENGTH;
#else
	(void)out; (void)salt; (void)saltlength;
	(void)in; (void)inlength; (void)iterations;
	return 0;
#endif
}

#endif /* NSEC3 */
