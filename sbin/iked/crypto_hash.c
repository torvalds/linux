/* $OpenBSD: crypto_hash.c,v 1.1 2021/05/28 18:01:39 tobhe Exp $ */
/*
 * Public domain. Author: Christian Weisgerber <naddy@openbsd.org>
 * API compatible reimplementation of function from nacl
 */

#include "crypto_api.h"
#include <openssl/evp.h>

int
crypto_hash_sha512(unsigned char *out, const unsigned char *in,
    unsigned long long inlen)
{
	u_int mdlen;

	if (!EVP_Digest(in, inlen, out, &mdlen, EVP_sha512(), NULL))
		return -1;
	return 0;
}
