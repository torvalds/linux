/* $OpenBSD: d1_srtp.c,v 1.33 2023/07/08 16:40:13 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/*
 * DTLS code by Eric Rescorla <ekr@rtfm.com>
 *
 * Copyright (C) 2006, Network Resonance, Inc.
 * Copyright (C) 2011, RTFM, Inc.
 */

#include <stdio.h>

#include <openssl/objects.h>
#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_SRTP

#include "bytestring.h"
#include "dtls_local.h"
#include "ssl_local.h"
#include "srtp.h"

static const SRTP_PROTECTION_PROFILE srtp_known_profiles[] = {
	{
		"SRTP_AES128_CM_SHA1_80",
		SRTP_AES128_CM_SHA1_80,
	},
	{
		"SRTP_AES128_CM_SHA1_32",
		SRTP_AES128_CM_SHA1_32,
	},
	{
		"SRTP_AEAD_AES_128_GCM",
		SRTP_AEAD_AES_128_GCM,
	},
	{
		"SRTP_AEAD_AES_256_GCM",
		SRTP_AEAD_AES_256_GCM,
	},
	{0}
};

int
srtp_find_profile_by_name(const char *profile_name,
    const SRTP_PROTECTION_PROFILE **pptr, unsigned int len)
{
	const SRTP_PROTECTION_PROFILE *p;

	p = srtp_known_profiles;
	while (p->name) {
		if ((len == strlen(p->name)) &&
		    !strncmp(p->name, profile_name, len)) {
			*pptr = p;
			return 0;
		}

		p++;
	}

	return 1;
}

int
srtp_find_profile_by_num(unsigned int profile_num,
    const SRTP_PROTECTION_PROFILE **pptr)
{
	const SRTP_PROTECTION_PROFILE *p;

	p = srtp_known_profiles;
	while (p->name) {
		if (p->id == profile_num) {
			*pptr = p;
			return 0;
		}
		p++;
	}

	return 1;
}

static int
ssl_ctx_make_profiles(const char *profiles_string,
    STACK_OF(SRTP_PROTECTION_PROFILE) **out)
{
	STACK_OF(SRTP_PROTECTION_PROFILE) *profiles;
	char *col;
	const char *ptr = profiles_string;
	const SRTP_PROTECTION_PROFILE *p;

	if (!(profiles = sk_SRTP_PROTECTION_PROFILE_new_null())) {
		SSLerrorx(SSL_R_SRTP_COULD_NOT_ALLOCATE_PROFILES);
		return 1;
	}

	do {
		col = strchr(ptr, ':');

		if (!srtp_find_profile_by_name(ptr, &p,
		    col ? col - ptr : (int)strlen(ptr))) {
			if (!sk_SRTP_PROTECTION_PROFILE_push(profiles, p)) {
				sk_SRTP_PROTECTION_PROFILE_free(profiles);
				return 1;
			}
		} else {
			SSLerrorx(SSL_R_SRTP_UNKNOWN_PROTECTION_PROFILE);
			sk_SRTP_PROTECTION_PROFILE_free(profiles);
			return 1;
		}

		if (col)
			ptr = col + 1;
	} while (col);

	sk_SRTP_PROTECTION_PROFILE_free(*out);
	*out = profiles;

	return 0;
}

int
SSL_CTX_set_tlsext_use_srtp(SSL_CTX *ctx, const char *profiles)
{
	return ssl_ctx_make_profiles(profiles, &ctx->srtp_profiles);
}
LSSL_ALIAS(SSL_CTX_set_tlsext_use_srtp);

int
SSL_set_tlsext_use_srtp(SSL *s, const char *profiles)
{
	return ssl_ctx_make_profiles(profiles, &s->srtp_profiles);
}
LSSL_ALIAS(SSL_set_tlsext_use_srtp);


STACK_OF(SRTP_PROTECTION_PROFILE) *
SSL_get_srtp_profiles(SSL *s)
{
	if (s != NULL) {
		if (s->srtp_profiles != NULL) {
			return s->srtp_profiles;
		} else if ((s->ctx != NULL) &&
		    (s->ctx->srtp_profiles != NULL)) {
			return s->ctx->srtp_profiles;
		}
	}

	return NULL;
}
LSSL_ALIAS(SSL_get_srtp_profiles);

SRTP_PROTECTION_PROFILE *
SSL_get_selected_srtp_profile(SSL *s)
{
	/* XXX cast away the const */
	return (SRTP_PROTECTION_PROFILE *)s->srtp_profile;
}
LSSL_ALIAS(SSL_get_selected_srtp_profile);

#endif
