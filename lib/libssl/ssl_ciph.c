/* $OpenBSD: ssl_ciph.c,v 1.151 2025/01/18 12:20:37 tb Exp $ */
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
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#include <stdio.h>

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/opensslconf.h>

#include "ssl_local.h"

#define CIPHER_ADD	1
#define CIPHER_KILL	2
#define CIPHER_DEL	3
#define CIPHER_ORD	4
#define CIPHER_SPECIAL	5

typedef struct cipher_order_st {
	const SSL_CIPHER *cipher;
	int active;
	int dead;
	struct cipher_order_st *next, *prev;
} CIPHER_ORDER;

static const SSL_CIPHER cipher_aliases[] = {

	/* "ALL" doesn't include eNULL (must be specifically enabled) */
	{
		.name = SSL_TXT_ALL,
		.algorithm_enc = ~SSL_eNULL,
	},

	/* "COMPLEMENTOFALL" */
	{
		.name = SSL_TXT_CMPALL,
		.algorithm_enc = SSL_eNULL,
	},

	/*
	 * "COMPLEMENTOFDEFAULT"
	 * (does *not* include ciphersuites not found in ALL!)
	 */
	{
		.name = SSL_TXT_CMPDEF,
		.algorithm_mkey = SSL_kDHE|SSL_kECDHE,
		.algorithm_auth = SSL_aNULL,
		.algorithm_enc = ~SSL_eNULL,
	},

	/*
	 * key exchange aliases
	 * (some of those using only a single bit here combine multiple key
	 * exchange algs according to the RFCs, e.g. kEDH combines DHE_DSS
	 * and DHE_RSA)
	 */
	{
		.name = SSL_TXT_kRSA,
		.algorithm_mkey = SSL_kRSA,
	},
	{
		.name = SSL_TXT_kEDH,
		.algorithm_mkey = SSL_kDHE,
	},
	{
		.name = SSL_TXT_DH,
		.algorithm_mkey = SSL_kDHE,
	},
	{
		.name = SSL_TXT_kEECDH,
		.algorithm_mkey = SSL_kECDHE,
	},
	{
		.name = SSL_TXT_ECDH,
		.algorithm_mkey = SSL_kECDHE,
	},

	/* server authentication aliases */
	{
		.name = SSL_TXT_aRSA,
		.algorithm_auth = SSL_aRSA,
	},
	{
		.name = SSL_TXT_aNULL,
		.algorithm_auth = SSL_aNULL,
	},
	{
		.name = SSL_TXT_aECDSA,
		.algorithm_auth = SSL_aECDSA,
	},
	{
		.name = SSL_TXT_ECDSA,
		.algorithm_auth = SSL_aECDSA,
	},

	/* aliases combining key exchange and server authentication */
	{
		.name = SSL_TXT_DHE,
		.algorithm_mkey = SSL_kDHE,
		.algorithm_auth = ~SSL_aNULL,
	},
	{
		.name = SSL_TXT_EDH,
		.algorithm_mkey = SSL_kDHE,
		.algorithm_auth = ~SSL_aNULL,
	},
	{
		.name = SSL_TXT_ECDHE,
		.algorithm_mkey = SSL_kECDHE,
		.algorithm_auth = ~SSL_aNULL,
	},
	{
		.name = SSL_TXT_EECDH,
		.algorithm_mkey = SSL_kECDHE,
		.algorithm_auth = ~SSL_aNULL,
	},
	{
		.name = SSL_TXT_NULL,
		.algorithm_enc = SSL_eNULL,
	},
	{
		.name = SSL_TXT_RSA,
		.algorithm_mkey = SSL_kRSA,
		.algorithm_auth = SSL_aRSA,
	},
	{
		.name = SSL_TXT_ADH,
		.algorithm_mkey = SSL_kDHE,
		.algorithm_auth = SSL_aNULL,
	},
	{
		.name = SSL_TXT_AECDH,
		.algorithm_mkey = SSL_kECDHE,
		.algorithm_auth = SSL_aNULL,
	},

	/* symmetric encryption aliases */
	{
		.name = SSL_TXT_3DES,
		.algorithm_enc = SSL_3DES,
	},
	{
		.name = SSL_TXT_RC4,
		.algorithm_enc = SSL_RC4,
	},
	{
		.name = SSL_TXT_eNULL,
		.algorithm_enc = SSL_eNULL,
	},
	{
		.name = SSL_TXT_AES128,
		.algorithm_enc = SSL_AES128|SSL_AES128GCM,
	},
	{
		.name = SSL_TXT_AES256,
		.algorithm_enc = SSL_AES256|SSL_AES256GCM,
	},
	{
		.name = SSL_TXT_AES,
		.algorithm_enc = SSL_AES,
	},
	{
		.name = SSL_TXT_AES_GCM,
		.algorithm_enc = SSL_AES128GCM|SSL_AES256GCM,
	},
	{
		.name = SSL_TXT_CAMELLIA128,
		.algorithm_enc = SSL_CAMELLIA128,
	},
	{
		.name = SSL_TXT_CAMELLIA256,
		.algorithm_enc = SSL_CAMELLIA256,
	},
	{
		.name = SSL_TXT_CAMELLIA,
		.algorithm_enc = SSL_CAMELLIA128|SSL_CAMELLIA256,
	},
	{
		.name = SSL_TXT_CHACHA20,
		.algorithm_enc = SSL_CHACHA20POLY1305,
	},

	/* MAC aliases */
	{
		.name = SSL_TXT_AEAD,
		.algorithm_mac = SSL_AEAD,
	},
	{
		.name = SSL_TXT_MD5,
		.algorithm_mac = SSL_MD5,
	},
	{
		.name = SSL_TXT_SHA1,
		.algorithm_mac = SSL_SHA1,
	},
	{
		.name = SSL_TXT_SHA,
		.algorithm_mac = SSL_SHA1,
	},
	{
		.name = SSL_TXT_SHA256,
		.algorithm_mac = SSL_SHA256,
	},
	{
		.name = SSL_TXT_SHA384,
		.algorithm_mac = SSL_SHA384,
	},

	/* protocol version aliases */
	{
		.name = SSL_TXT_SSLV3,
		.algorithm_ssl = SSL_SSLV3,
	},
	{
		.name = SSL_TXT_TLSV1,
		.algorithm_ssl = SSL_TLSV1,
	},
	{
		.name = SSL_TXT_TLSV1_2,
		.algorithm_ssl = SSL_TLSV1_2,
	},
	{
		.name = SSL_TXT_TLSV1_3,
		.algorithm_ssl = SSL_TLSV1_3,
	},

	/* cipher suite aliases */
#ifdef LIBRESSL_HAS_TLS1_3
	{
		.value = 0x1301,
		.name = "TLS_AES_128_GCM_SHA256",
		.algorithm_ssl = SSL_TLSV1_3,
	},
	{
		.value = 0x1302,
		.name = "TLS_AES_256_GCM_SHA384",
		.algorithm_ssl = SSL_TLSV1_3,
	},
	{
		.value = 0x1303,
		.name = "TLS_CHACHA20_POLY1305_SHA256",
		.algorithm_ssl = SSL_TLSV1_3,
	},
#endif

	/* strength classes */
	{
		.name = SSL_TXT_LOW,
		.algo_strength = SSL_LOW,
	},
	{
		.name = SSL_TXT_MEDIUM,
		.algo_strength = SSL_MEDIUM,
	},
	{
		.name = SSL_TXT_HIGH,
		.algo_strength = SSL_HIGH,
	},
};

int
ssl_cipher_get_evp(SSL *s, const EVP_CIPHER **enc, const EVP_MD **md,
    int *mac_pkey_type, int *mac_secret_size)
{
	const SSL_CIPHER *cipher;

	*enc = NULL;
	*md = NULL;
	*mac_pkey_type = NID_undef;
	*mac_secret_size = 0;

	if ((cipher = s->s3->hs.cipher) == NULL)
		return 0;

	/*
	 * This function does not handle EVP_AEAD.
	 * See ssl_cipher_get_evp_aead instead.
	 */
	if (cipher->algorithm_mac & SSL_AEAD)
		return 0;

	switch (cipher->algorithm_enc) {
	case SSL_3DES:
		*enc = EVP_des_ede3_cbc();
		break;
	case SSL_RC4:
		*enc = EVP_rc4();
		break;
	case SSL_eNULL:
		*enc = EVP_enc_null();
		break;
	case SSL_AES128:
		*enc = EVP_aes_128_cbc();
		break;
	case SSL_AES256:
		*enc = EVP_aes_256_cbc();
		break;
	case SSL_CAMELLIA128:
		*enc = EVP_camellia_128_cbc();
		break;
	case SSL_CAMELLIA256:
		*enc = EVP_camellia_256_cbc();
		break;
	}

	switch (cipher->algorithm_mac) {
	case SSL_MD5:
		*md = EVP_md5();
		break;
	case SSL_SHA1:
		*md = EVP_sha1();
		break;
	case SSL_SHA256:
		*md = EVP_sha256();
		break;
	case SSL_SHA384:
		*md = EVP_sha384();
		break;
	}
	if (*enc == NULL || *md == NULL)
		return 0;

	/* XXX remove these from ssl_cipher_get_evp? */
	/*
	 * EVP_CIPH_FLAG_AEAD_CIPHER and EVP_CIPH_GCM_MODE ciphers are not
	 * supported via EVP_CIPHER (they should be using EVP_AEAD instead).
	 */
	if (EVP_CIPHER_flags(*enc) & EVP_CIPH_FLAG_AEAD_CIPHER)
		return 0;
	if (EVP_CIPHER_mode(*enc) == EVP_CIPH_GCM_MODE)
		return 0;

	*mac_pkey_type = EVP_PKEY_HMAC;
	*mac_secret_size = EVP_MD_size(*md);
	return 1;
}

/*
 * ssl_cipher_get_evp_aead sets aead to point to the correct EVP_AEAD object
 * for s->cipher. It returns 1 on success and 0 on error.
 */
int
ssl_cipher_get_evp_aead(SSL *s, const EVP_AEAD **aead)
{
	const SSL_CIPHER *cipher;

	*aead = NULL;

	if ((cipher = s->s3->hs.cipher) == NULL)
		return 0;
	if ((cipher->algorithm_mac & SSL_AEAD) == 0)
		return 0;

	switch (cipher->algorithm_enc) {
	case SSL_AES128GCM:
		*aead = EVP_aead_aes_128_gcm();
		return 1;
	case SSL_AES256GCM:
		*aead = EVP_aead_aes_256_gcm();
		return 1;
	case SSL_CHACHA20POLY1305:
		*aead = EVP_aead_chacha20_poly1305();
		return 1;
	default:
		break;
	}
	return 0;
}

int
ssl_get_handshake_evp_md(SSL *s, const EVP_MD **md)
{
	const SSL_CIPHER *cipher;

	*md = NULL;

	if ((cipher = s->s3->hs.cipher) == NULL)
		return 0;

	switch (cipher->algorithm2 & SSL_HANDSHAKE_MAC_MASK) {
	case SSL_HANDSHAKE_MAC_SHA256:
		*md = EVP_sha256();
		return 1;
	case SSL_HANDSHAKE_MAC_SHA384:
		*md = EVP_sha384();
		return 1;
	default:
		break;
	}

	return 0;
}

#define ITEM_SEP(a) \
	(((a) == ':') || ((a) == ' ') || ((a) == ';') || ((a) == ','))

static void
ll_append_tail(CIPHER_ORDER **head, CIPHER_ORDER *curr,
    CIPHER_ORDER **tail)
{
	if (curr == *tail)
		return;
	if (curr == *head)
		*head = curr->next;
	if (curr->prev != NULL)
		curr->prev->next = curr->next;
	if (curr->next != NULL)
		curr->next->prev = curr->prev;
	(*tail)->next = curr;
	curr->prev= *tail;
	curr->next = NULL;
	*tail = curr;
}

static void
ll_append_head(CIPHER_ORDER **head, CIPHER_ORDER *curr,
    CIPHER_ORDER **tail)
{
	if (curr == *head)
		return;
	if (curr == *tail)
		*tail = curr->prev;
	if (curr->next != NULL)
		curr->next->prev = curr->prev;
	if (curr->prev != NULL)
		curr->prev->next = curr->next;
	(*head)->prev = curr;
	curr->next= *head;
	curr->prev = NULL;
	*head = curr;
}

static void
ssl_cipher_collect_ciphers(const SSL_METHOD *ssl_method, int num_of_ciphers,
    unsigned long disabled_mkey, unsigned long disabled_auth,
    unsigned long disabled_enc, unsigned long disabled_mac,
    unsigned long disabled_ssl, CIPHER_ORDER *co_list,
    CIPHER_ORDER **head_p, CIPHER_ORDER **tail_p)
{
	int i, co_list_num;
	const SSL_CIPHER *c;

	/*
	 * We have num_of_ciphers descriptions compiled in, depending on the
	 * method selected (SSLv3, TLSv1, etc). These will later be sorted in
	 * a linked list with at most num entries.
	 */

	/*
	 * Get the initial list of ciphers, iterating backwards over the
	 * cipher list - the list is ordered by cipher value and we currently
	 * hope that ciphers with higher cipher values are preferable...
	 */
	co_list_num = 0;	/* actual count of ciphers */
	for (i = num_of_ciphers - 1; i >= 0; i--) {
		c = ssl3_get_cipher_by_index(i);

		/*
		 * Drop any invalid ciphers and any which use unavailable
		 * algorithms.
		 */
		if ((c != NULL) &&
		    !(c->algorithm_mkey & disabled_mkey) &&
		    !(c->algorithm_auth & disabled_auth) &&
		    !(c->algorithm_enc & disabled_enc) &&
		    !(c->algorithm_mac & disabled_mac) &&
		    !(c->algorithm_ssl & disabled_ssl)) {
			co_list[co_list_num].cipher = c;
			co_list[co_list_num].next = NULL;
			co_list[co_list_num].prev = NULL;
			co_list[co_list_num].active = 0;
			co_list_num++;
		}
	}

	/*
	 * Prepare linked list from list entries
	 */
	if (co_list_num > 0) {
		co_list[0].prev = NULL;

		if (co_list_num > 1) {
			co_list[0].next = &co_list[1];

			for (i = 1; i < co_list_num - 1; i++) {
				co_list[i].prev = &co_list[i - 1];
				co_list[i].next = &co_list[i + 1];
			}

			co_list[co_list_num - 1].prev =
			    &co_list[co_list_num - 2];
		}

		co_list[co_list_num - 1].next = NULL;

		*head_p = &co_list[0];
		*tail_p = &co_list[co_list_num - 1];
	}
}

static void
ssl_cipher_collect_aliases(const SSL_CIPHER **ca_list, int num_of_group_aliases,
    unsigned long disabled_mkey, unsigned long disabled_auth,
    unsigned long disabled_enc, unsigned long disabled_mac,
    unsigned long disabled_ssl, CIPHER_ORDER *head)
{
	CIPHER_ORDER *ciph_curr;
	const SSL_CIPHER **ca_curr;
	int i;
	unsigned long mask_mkey = ~disabled_mkey;
	unsigned long mask_auth = ~disabled_auth;
	unsigned long mask_enc = ~disabled_enc;
	unsigned long mask_mac = ~disabled_mac;
	unsigned long mask_ssl = ~disabled_ssl;

	/*
	 * First, add the real ciphers as already collected
	 */
	ciph_curr = head;
	ca_curr = ca_list;
	while (ciph_curr != NULL) {
		*ca_curr = ciph_curr->cipher;
		ca_curr++;
		ciph_curr = ciph_curr->next;
	}

	/*
	 * Now we add the available ones from the cipher_aliases[] table.
	 * They represent either one or more algorithms, some of which
	 * in any affected category must be supported (set in enabled_mask),
	 * or represent a cipher strength value (will be added in any case because algorithms=0).
	 */
	for (i = 0; i < num_of_group_aliases; i++) {
		unsigned long algorithm_mkey = cipher_aliases[i].algorithm_mkey;
		unsigned long algorithm_auth = cipher_aliases[i].algorithm_auth;
		unsigned long algorithm_enc = cipher_aliases[i].algorithm_enc;
		unsigned long algorithm_mac = cipher_aliases[i].algorithm_mac;
		unsigned long algorithm_ssl = cipher_aliases[i].algorithm_ssl;

		if (algorithm_mkey)
			if ((algorithm_mkey & mask_mkey) == 0)
				continue;

		if (algorithm_auth)
			if ((algorithm_auth & mask_auth) == 0)
				continue;

		if (algorithm_enc)
			if ((algorithm_enc & mask_enc) == 0)
				continue;

		if (algorithm_mac)
			if ((algorithm_mac & mask_mac) == 0)
				continue;

		if (algorithm_ssl)
			if ((algorithm_ssl & mask_ssl) == 0)
				continue;

		*ca_curr = (SSL_CIPHER *)(cipher_aliases + i);
		ca_curr++;
	}

	*ca_curr = NULL;	/* end of list */
}

static void
ssl_cipher_apply_rule(uint16_t cipher_value, unsigned long alg_mkey,
    unsigned long alg_auth, unsigned long alg_enc, unsigned long alg_mac,
    unsigned long alg_ssl, unsigned long algo_strength, int rule,
    int strength_bits, CIPHER_ORDER **head_p, CIPHER_ORDER **tail_p)
{
	CIPHER_ORDER *head, *tail, *curr, *next, *last;
	const SSL_CIPHER *cp;
	int reverse = 0;

	if (rule == CIPHER_DEL)
		reverse = 1; /* needed to maintain sorting between currently deleted ciphers */

	head = *head_p;
	tail = *tail_p;

	if (reverse) {
		next = tail;
		last = head;
	} else {
		next = head;
		last = tail;
	}

	curr = NULL;
	for (;;) {
		if (curr == last)
			break;
		curr = next;
		next = reverse ? curr->prev : curr->next;

		cp = curr->cipher;

		if (cipher_value != 0 && cp->value != cipher_value)
			continue;

		/*
		 * Selection criteria is either the value of strength_bits
		 * or the algorithms used.
		 */
		if (strength_bits >= 0) {
			if (strength_bits != cp->strength_bits)
				continue;
		} else {
			if (alg_mkey && !(alg_mkey & cp->algorithm_mkey))
				continue;
			if (alg_auth && !(alg_auth & cp->algorithm_auth))
				continue;
			if (alg_enc && !(alg_enc & cp->algorithm_enc))
				continue;
			if (alg_mac && !(alg_mac & cp->algorithm_mac))
				continue;
			if (alg_ssl && !(alg_ssl & cp->algorithm_ssl))
				continue;
			if ((algo_strength & SSL_STRONG_MASK) && !(algo_strength & SSL_STRONG_MASK & cp->algo_strength))
				continue;
		}

		/* add the cipher if it has not been added yet. */
		if (rule == CIPHER_ADD) {
			/* reverse == 0 */
			if (!curr->active) {
				ll_append_tail(&head, curr, &tail);
				curr->active = 1;
			}
		}
		/* Move the added cipher to this location */
		else if (rule == CIPHER_ORD) {
			/* reverse == 0 */
			if (curr->active) {
				ll_append_tail(&head, curr, &tail);
			}
		} else if (rule == CIPHER_DEL) {
			/* reverse == 1 */
			if (curr->active) {
				/* most recently deleted ciphersuites get best positions
				 * for any future CIPHER_ADD (note that the CIPHER_DEL loop
				 * works in reverse to maintain the order) */
				ll_append_head(&head, curr, &tail);
				curr->active = 0;
			}
		} else if (rule == CIPHER_KILL) {
			/* reverse == 0 */
			if (head == curr)
				head = curr->next;
			else
				curr->prev->next = curr->next;
			if (tail == curr)
				tail = curr->prev;
			curr->active = 0;
			if (curr->next != NULL)
				curr->next->prev = curr->prev;
			if (curr->prev != NULL)
				curr->prev->next = curr->next;
			curr->next = NULL;
			curr->prev = NULL;
		}
	}

	*head_p = head;
	*tail_p = tail;
}

static int
ssl_cipher_strength_sort(CIPHER_ORDER **head_p, CIPHER_ORDER **tail_p)
{
	int max_strength_bits, i, *number_uses;
	CIPHER_ORDER *curr;

	/*
	 * This routine sorts the ciphers with descending strength. The sorting
	 * must keep the pre-sorted sequence, so we apply the normal sorting
	 * routine as '+' movement to the end of the list.
	 */
	max_strength_bits = 0;
	curr = *head_p;
	while (curr != NULL) {
		if (curr->active &&
		    (curr->cipher->strength_bits > max_strength_bits))
			max_strength_bits = curr->cipher->strength_bits;
		curr = curr->next;
	}

	number_uses = calloc((max_strength_bits + 1), sizeof(int));
	if (!number_uses) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		return (0);
	}

	/*
	 * Now find the strength_bits values actually used
	 */
	curr = *head_p;
	while (curr != NULL) {
		if (curr->active)
			number_uses[curr->cipher->strength_bits]++;
		curr = curr->next;
	}
	/*
	 * Go through the list of used strength_bits values in descending
	 * order.
	 */
	for (i = max_strength_bits; i >= 0; i--)
		if (number_uses[i] > 0)
			ssl_cipher_apply_rule(0, 0, 0, 0, 0, 0, 0, CIPHER_ORD, i, head_p, tail_p);

	free(number_uses);
	return (1);
}

static int
ssl_cipher_process_rulestr(const char *rule_str, CIPHER_ORDER **head_p,
    CIPHER_ORDER **tail_p, const SSL_CIPHER **ca_list, SSL_CERT *cert,
    int *tls13_seen)
{
	unsigned long alg_mkey, alg_auth, alg_enc, alg_mac, alg_ssl;
	unsigned long algo_strength;
	int j, multi, found, rule, retval, ok, buflen;
	uint16_t cipher_value = 0;
	const char *l, *buf;
	char ch;

	*tls13_seen = 0;

	retval = 1;
	l = rule_str;
	for (;;) {
		ch = *l;

		if (ch == '\0')
			break;

		if (ch == '-') {
			rule = CIPHER_DEL;
			l++;
		} else if (ch == '+') {
			rule = CIPHER_ORD;
			l++;
		} else if (ch == '!') {
			rule = CIPHER_KILL;
			l++;
		} else if (ch == '@') {
			rule = CIPHER_SPECIAL;
			l++;
		} else {
			rule = CIPHER_ADD;
		}

		if (ITEM_SEP(ch)) {
			l++;
			continue;
		}

		alg_mkey = 0;
		alg_auth = 0;
		alg_enc = 0;
		alg_mac = 0;
		alg_ssl = 0;
		algo_strength = 0;

		for (;;) {
			ch = *l;
			buf = l;
			buflen = 0;
			while (((ch >= 'A') && (ch <= 'Z')) ||
			    ((ch >= '0') && (ch <= '9')) ||
			    ((ch >= 'a') && (ch <= 'z')) ||
			    (ch == '-') || (ch == '.') ||
			    (ch == '_') || (ch == '=')) {
				ch = *(++l);
				buflen++;
			}

			if (buflen == 0) {
				/*
				 * We hit something we cannot deal with,
				 * it is no command or separator nor
				 * alphanumeric, so we call this an error.
				 */
				SSLerrorx(SSL_R_INVALID_COMMAND);
				return 0;
			}

			if (rule == CIPHER_SPECIAL) {
				 /* unused -- avoid compiler warning */
				found = 0;
				/* special treatment */
				break;
			}

			/* check for multi-part specification */
			if (ch == '+') {
				multi = 1;
				l++;
			} else
				multi = 0;

			/*
			 * Now search for the cipher alias in the ca_list.
			 * Be careful with the strncmp, because the "buflen"
			 * limitation will make the rule "ADH:SOME" and the
			 * cipher "ADH-MY-CIPHER" look like a match for
			 * buflen=3. So additionally check whether the cipher
			 * name found has the correct length. We can save a
			 * strlen() call: just checking for the '\0' at the
			 * right place is sufficient, we have to strncmp()
			 * anyway (we cannot use strcmp(), because buf is not
			 * '\0' terminated.)
			 */
			j = found = 0;
			cipher_value = 0;
			while (ca_list[j]) {
				if (!strncmp(buf, ca_list[j]->name, buflen) &&
				    (ca_list[j]->name[buflen] == '\0')) {
					found = 1;
					break;
				} else
					j++;
			}

			if (!found)
				break;	/* ignore this entry */

			if (ca_list[j]->algorithm_mkey) {
				if (alg_mkey) {
					alg_mkey &= ca_list[j]->algorithm_mkey;
					if (!alg_mkey) {
						found = 0;
						break;
					}
				} else
					alg_mkey = ca_list[j]->algorithm_mkey;
			}

			if (ca_list[j]->algorithm_auth) {
				if (alg_auth) {
					alg_auth &= ca_list[j]->algorithm_auth;
					if (!alg_auth) {
						found = 0;
						break;
					}
				} else
					alg_auth = ca_list[j]->algorithm_auth;
			}

			if (ca_list[j]->algorithm_enc) {
				if (alg_enc) {
					alg_enc &= ca_list[j]->algorithm_enc;
					if (!alg_enc) {
						found = 0;
						break;
					}
				} else
					alg_enc = ca_list[j]->algorithm_enc;
			}

			if (ca_list[j]->algorithm_mac) {
				if (alg_mac) {
					alg_mac &= ca_list[j]->algorithm_mac;
					if (!alg_mac) {
						found = 0;
						break;
					}
				} else
					alg_mac = ca_list[j]->algorithm_mac;
			}

			if (ca_list[j]->algo_strength & SSL_STRONG_MASK) {
				if (algo_strength & SSL_STRONG_MASK) {
					algo_strength &=
					    (ca_list[j]->algo_strength &
					    SSL_STRONG_MASK) | ~SSL_STRONG_MASK;
					if (!(algo_strength &
					    SSL_STRONG_MASK)) {
						found = 0;
						break;
					}
				} else
					algo_strength |=
					    ca_list[j]->algo_strength &
					    SSL_STRONG_MASK;
			}

			if (ca_list[j]->value != 0) {
				/*
				 * explicit ciphersuite found; its protocol
				 * version does not become part of the search
				 * pattern!
				 */
				cipher_value = ca_list[j]->value;
				if (ca_list[j]->algorithm_ssl == SSL_TLSV1_3)
					*tls13_seen = 1;
			} else {
				/*
				 * not an explicit ciphersuite; only in this
				 * case, the protocol version is considered
				 * part of the search pattern
				 */
				if (ca_list[j]->algorithm_ssl) {
					if (alg_ssl) {
						alg_ssl &=
						    ca_list[j]->algorithm_ssl;
						if (!alg_ssl) {
							found = 0;
							break;
						}
					} else
						alg_ssl =
						    ca_list[j]->algorithm_ssl;
				}
			}

			if (!multi)
				break;
		}

		/*
		 * Ok, we have the rule, now apply it
		 */
		if (rule == CIPHER_SPECIAL) {
			/* special command */
			ok = 0;
			if (buflen == 8 && strncmp(buf, "STRENGTH", 8) == 0) {
				ok = ssl_cipher_strength_sort(head_p, tail_p);
			} else if (buflen == 10 &&
			    strncmp(buf, "SECLEVEL=", 9) == 0) {
				int level = buf[9] - '0';

				if (level >= 0 && level <= 5) {
					cert->security_level = level;
					ok = 1;
				} else {
					SSLerrorx(SSL_R_INVALID_COMMAND);
				}
			} else {
				SSLerrorx(SSL_R_INVALID_COMMAND);
			}
			if (ok == 0)
				retval = 0;

			while ((*l != '\0') && !ITEM_SEP(*l))
				l++;
		} else if (found) {
			if (alg_ssl == SSL_TLSV1_3)
				*tls13_seen = 1;
			ssl_cipher_apply_rule(cipher_value, alg_mkey, alg_auth,
			    alg_enc, alg_mac, alg_ssl, algo_strength, rule,
			    -1, head_p, tail_p);
		} else {
			while ((*l != '\0') && !ITEM_SEP(*l))
				l++;
		}
		if (*l == '\0')
			break; /* done */
	}

	return (retval);
}

static inline int
ssl_aes_is_accelerated(void)
{
	return (OPENSSL_cpu_caps() & CRYPTO_CPU_CAPS_ACCELERATED_AES) != 0;
}

STACK_OF(SSL_CIPHER) *
ssl_create_cipher_list(const SSL_METHOD *ssl_method,
    STACK_OF(SSL_CIPHER) **cipher_list,
    STACK_OF(SSL_CIPHER) *cipher_list_tls13,
    const char *rule_str, SSL_CERT *cert)
{
	int ok, num_of_ciphers, num_of_alias_max, num_of_group_aliases;
	unsigned long disabled_mkey, disabled_auth, disabled_enc, disabled_mac, disabled_ssl;
	STACK_OF(SSL_CIPHER) *cipherstack = NULL, *ret = NULL;
	const char *rule_p;
	CIPHER_ORDER *co_list = NULL, *head = NULL, *tail = NULL, *curr;
	const SSL_CIPHER **ca_list = NULL;
	const SSL_CIPHER *cipher;
	int tls13_seen = 0;
	int any_active;
	int i;

	/*
	 * Return with error if nothing to do.
	 */
	if (rule_str == NULL || cipher_list == NULL)
		goto err;

	disabled_mkey = 0;
	disabled_auth = 0;
	disabled_enc = 0;
	disabled_mac = 0;
	disabled_ssl = 0;

#ifdef SSL_FORBID_ENULL
	disabled_enc |= SSL_eNULL;
#endif

	/* DTLS cannot be used with stream ciphers. */
	if (ssl_method->dtls)
		disabled_enc |= SSL_RC4;

	/*
	 * Now we have to collect the available ciphers from the compiled
	 * in ciphers. We cannot get more than the number compiled in, so
	 * it is used for allocation.
	 */
	num_of_ciphers = ssl3_num_ciphers();
	co_list = reallocarray(NULL, num_of_ciphers, sizeof(CIPHER_ORDER));
	if (co_list == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	ssl_cipher_collect_ciphers(ssl_method, num_of_ciphers,
	    disabled_mkey, disabled_auth, disabled_enc, disabled_mac, disabled_ssl,
	    co_list, &head, &tail);


	/* Now arrange all ciphers by preference: */

	/* Everything else being equal, prefer ephemeral ECDH over other key exchange mechanisms */
	ssl_cipher_apply_rule(0, SSL_kECDHE, 0, 0, 0, 0, 0, CIPHER_ADD, -1, &head, &tail);
	ssl_cipher_apply_rule(0, SSL_kECDHE, 0, 0, 0, 0, 0, CIPHER_DEL, -1, &head, &tail);

	if (ssl_aes_is_accelerated()) {
		/*
		 * We have hardware assisted AES - prefer AES as a symmetric
		 * cipher, with CHACHA20 second.
		 */
		ssl_cipher_apply_rule(0, 0, 0, SSL_AES, 0, 0, 0,
		    CIPHER_ADD, -1, &head, &tail);
		ssl_cipher_apply_rule(0, 0, 0, SSL_CHACHA20POLY1305,
		    0, 0, 0, CIPHER_ADD, -1, &head, &tail);
	} else {
		/*
		 * CHACHA20 is fast and safe on all hardware and is thus our
		 * preferred symmetric cipher, with AES second.
		 */
		ssl_cipher_apply_rule(0, 0, 0, SSL_CHACHA20POLY1305,
		    0, 0, 0, CIPHER_ADD, -1, &head, &tail);
		ssl_cipher_apply_rule(0, 0, 0, SSL_AES, 0, 0, 0,
		    CIPHER_ADD, -1, &head, &tail);
	}

	/* Temporarily enable everything else for sorting */
	ssl_cipher_apply_rule(0, 0, 0, 0, 0, 0, 0, CIPHER_ADD, -1, &head, &tail);

	/* Low priority for MD5 */
	ssl_cipher_apply_rule(0, 0, 0, 0, SSL_MD5, 0, 0, CIPHER_ORD, -1, &head, &tail);

	/* Move anonymous ciphers to the end.  Usually, these will remain disabled.
	 * (For applications that allow them, they aren't too bad, but we prefer
	 * authenticated ciphers.) */
	ssl_cipher_apply_rule(0, 0, SSL_aNULL, 0, 0, 0, 0, CIPHER_ORD, -1, &head, &tail);

	/* Move ciphers without forward secrecy to the end */
	ssl_cipher_apply_rule(0, SSL_kRSA, 0, 0, 0, 0, 0, CIPHER_ORD, -1, &head, &tail);

	/* RC4 is sort of broken - move it to the end */
	ssl_cipher_apply_rule(0, 0, 0, SSL_RC4, 0, 0, 0, CIPHER_ORD, -1, &head, &tail);

	/* Now sort by symmetric encryption strength.  The above ordering remains
	 * in force within each class */
	if (!ssl_cipher_strength_sort(&head, &tail))
		goto err;

	/* Now disable everything (maintaining the ordering!) */
	ssl_cipher_apply_rule(0, 0, 0, 0, 0, 0, 0, CIPHER_DEL, -1, &head, &tail);

	/* TLSv1.3 first. */
	ssl_cipher_apply_rule(0, 0, 0, 0, 0, SSL_TLSV1_3, 0, CIPHER_ADD, -1, &head, &tail);
	ssl_cipher_apply_rule(0, 0, 0, 0, 0, SSL_TLSV1_3, 0, CIPHER_DEL, -1, &head, &tail);

	/*
	 * We also need cipher aliases for selecting based on the rule_str.
	 * There might be two types of entries in the rule_str: 1) names
	 * of ciphers themselves 2) aliases for groups of ciphers.
	 * For 1) we need the available ciphers and for 2) the cipher
	 * groups of cipher_aliases added together in one list (otherwise
	 * we would be happy with just the cipher_aliases table).
	 */
	num_of_group_aliases = sizeof(cipher_aliases) / sizeof(SSL_CIPHER);
	num_of_alias_max = num_of_ciphers + num_of_group_aliases + 1;
	ca_list = reallocarray(NULL, num_of_alias_max, sizeof(SSL_CIPHER *));
	if (ca_list == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	ssl_cipher_collect_aliases(ca_list, num_of_group_aliases, disabled_mkey,
	    disabled_auth, disabled_enc, disabled_mac, disabled_ssl, head);

	/*
	 * If the rule_string begins with DEFAULT, apply the default rule
	 * before using the (possibly available) additional rules.
	 */
	ok = 1;
	rule_p = rule_str;
	if (strncmp(rule_str, "DEFAULT", 7) == 0) {
		ok = ssl_cipher_process_rulestr(SSL_DEFAULT_CIPHER_LIST,
		    &head, &tail, ca_list, cert, &tls13_seen);
		rule_p += 7;
		if (*rule_p == ':')
			rule_p++;
	}

	if (ok && (strlen(rule_p) > 0))
		ok = ssl_cipher_process_rulestr(rule_p, &head, &tail, ca_list,
		    cert, &tls13_seen);

	if (!ok) {
		/* Rule processing failure */
		goto err;
	}

	/*
	 * Allocate new "cipherstack" for the result, return with error
	 * if we cannot get one.
	 */
	if ((cipherstack = sk_SSL_CIPHER_new_null()) == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/* Prefer TLSv1.3 cipher suites. */
	if (cipher_list_tls13 != NULL) {
		for (i = 0; i < sk_SSL_CIPHER_num(cipher_list_tls13); i++) {
			cipher = sk_SSL_CIPHER_value(cipher_list_tls13, i);
			if (!sk_SSL_CIPHER_push(cipherstack, cipher)) {
				SSLerrorx(ERR_R_MALLOC_FAILURE);
				goto err;
			}
		}
		tls13_seen = 1;
	}

	/*
	 * The cipher selection for the list is done. The ciphers are added
	 * to the resulting precedence to the STACK_OF(SSL_CIPHER).
	 *
	 * If the rule string did not contain any references to TLSv1.3 and
	 * TLSv1.3 cipher suites have not been configured separately,
	 * include inactive TLSv1.3 cipher suites. This avoids attempts to
	 * use TLSv1.3 with an older rule string that does not include
	 * TLSv1.3 cipher suites. If the rule string resulted in no active
	 * cipher suites then we return an empty stack.
	 */
	any_active = 0;
	for (curr = head; curr != NULL; curr = curr->next) {
		if (curr->active ||
		    (!tls13_seen && curr->cipher->algorithm_ssl == SSL_TLSV1_3)) {
			if (!sk_SSL_CIPHER_push(cipherstack, curr->cipher)) {
				SSLerrorx(ERR_R_MALLOC_FAILURE);
				goto err;
			}
		}
		any_active |= curr->active;
	}
	if (!any_active)
		sk_SSL_CIPHER_zero(cipherstack);

	sk_SSL_CIPHER_free(*cipher_list);
	*cipher_list = cipherstack;
	cipherstack = NULL;

	ret = *cipher_list;

 err:
	sk_SSL_CIPHER_free(cipherstack);
	free((void *)ca_list);
	free(co_list);

	return ret;
}

char *
SSL_CIPHER_description(const SSL_CIPHER *cipher, char *buf, int len)
{
	unsigned long alg_mkey, alg_auth, alg_enc, alg_mac, alg_ssl;
	const char *ver, *kx, *au, *enc, *mac;
	char *ret;
	int l;

	alg_mkey = cipher->algorithm_mkey;
	alg_auth = cipher->algorithm_auth;
	alg_enc = cipher->algorithm_enc;
	alg_mac = cipher->algorithm_mac;
	alg_ssl = cipher->algorithm_ssl;

	if (alg_ssl & SSL_SSLV3)
		ver = "SSLv3";
	else if (alg_ssl & SSL_TLSV1_2)
		ver = "TLSv1.2";
	else if (alg_ssl & SSL_TLSV1_3)
		ver = "TLSv1.3";
	else
		ver = "unknown";

	switch (alg_mkey) {
	case SSL_kRSA:
		kx = "RSA";
		break;
	case SSL_kDHE:
		kx = "DH";
		break;
	case SSL_kECDHE:
		kx = "ECDH";
		break;
	case SSL_kTLS1_3:
		kx = "TLSv1.3";
		break;
	default:
		kx = "unknown";
	}

	switch (alg_auth) {
	case SSL_aRSA:
		au = "RSA";
		break;
	case SSL_aNULL:
		au = "None";
		break;
	case SSL_aECDSA:
		au = "ECDSA";
		break;
	case SSL_aTLS1_3:
		au = "TLSv1.3";
		break;
	default:
		au = "unknown";
		break;
	}

	switch (alg_enc) {
	case SSL_3DES:
		enc = "3DES(168)";
		break;
	case SSL_RC4:
		enc = "RC4(128)";
		break;
	case SSL_eNULL:
		enc = "None";
		break;
	case SSL_AES128:
		enc = "AES(128)";
		break;
	case SSL_AES256:
		enc = "AES(256)";
		break;
	case SSL_AES128GCM:
		enc = "AESGCM(128)";
		break;
	case SSL_AES256GCM:
		enc = "AESGCM(256)";
		break;
	case SSL_CAMELLIA128:
		enc = "Camellia(128)";
		break;
	case SSL_CAMELLIA256:
		enc = "Camellia(256)";
		break;
	case SSL_CHACHA20POLY1305:
		enc = "ChaCha20-Poly1305";
		break;
	default:
		enc = "unknown";
		break;
	}

	switch (alg_mac) {
	case SSL_MD5:
		mac = "MD5";
		break;
	case SSL_SHA1:
		mac = "SHA1";
		break;
	case SSL_SHA256:
		mac = "SHA256";
		break;
	case SSL_SHA384:
		mac = "SHA384";
		break;
	case SSL_AEAD:
		mac = "AEAD";
		break;
	default:
		mac = "unknown";
		break;
	}

	if (asprintf(&ret, "%-23s %s Kx=%-8s Au=%-4s Enc=%-9s Mac=%-4s\n",
	    cipher->name, ver, kx, au, enc, mac) == -1)
		return "OPENSSL_malloc Error";

	if (buf != NULL) {
		l = strlcpy(buf, ret, len);
		free(ret);
		ret = buf;
		if (l >= len)
			ret = "Buffer too small";
	}

	return (ret);
}
LSSL_ALIAS(SSL_CIPHER_description);

const char *
SSL_CIPHER_get_version(const SSL_CIPHER *cipher)
{
	if (cipher == NULL)
		return "(NONE)";

	return "TLSv1/SSLv3";
}
LSSL_ALIAS(SSL_CIPHER_get_version);

/* return the actual cipher being used */
const char *
SSL_CIPHER_get_name(const SSL_CIPHER *cipher)
{
	if (cipher == NULL)
		return "(NONE)";

	return cipher->name;
}
LSSL_ALIAS(SSL_CIPHER_get_name);

/* number of bits for symmetric cipher */
int
SSL_CIPHER_get_bits(const SSL_CIPHER *c, int *alg_bits)
{
	int ret = 0;

	if (c != NULL) {
		if (alg_bits != NULL)
			*alg_bits = c->alg_bits;
		ret = c->strength_bits;
	}
	return (ret);
}
LSSL_ALIAS(SSL_CIPHER_get_bits);

unsigned long
SSL_CIPHER_get_id(const SSL_CIPHER *cipher)
{
	return SSL3_CK_ID | cipher->value;
}
LSSL_ALIAS(SSL_CIPHER_get_id);

uint16_t
SSL_CIPHER_get_value(const SSL_CIPHER *cipher)
{
	return cipher->value;
}
LSSL_ALIAS(SSL_CIPHER_get_value);

const SSL_CIPHER *
SSL_CIPHER_find(SSL *ssl, const unsigned char *ptr)
{
	uint16_t cipher_value;
	CBS cbs;

	/* This API is documented with ptr being an array of length two. */
	CBS_init(&cbs, ptr, 2);
	if (!CBS_get_u16(&cbs, &cipher_value))
		return NULL;

	return ssl3_get_cipher_by_value(cipher_value);
}
LSSL_ALIAS(SSL_CIPHER_find);

int
SSL_CIPHER_get_cipher_nid(const SSL_CIPHER *c)
{
	switch (c->algorithm_enc) {
	case SSL_eNULL:
		return NID_undef;
	case SSL_3DES:
		return NID_des_ede3_cbc;
	case SSL_AES128:
		return NID_aes_128_cbc;
	case SSL_AES128GCM:
		return NID_aes_128_gcm;
	case SSL_AES256:
		return NID_aes_256_cbc;
	case SSL_AES256GCM:
		return NID_aes_256_gcm;
	case SSL_CAMELLIA128:
		return NID_camellia_128_cbc;
	case SSL_CAMELLIA256:
		return NID_camellia_256_cbc;
	case SSL_CHACHA20POLY1305:
		return NID_chacha20_poly1305;
	case SSL_RC4:
		return NID_rc4;
	default:
		return NID_undef;
	}
}
LSSL_ALIAS(SSL_CIPHER_get_cipher_nid);

int
SSL_CIPHER_get_digest_nid(const SSL_CIPHER *c)
{
	switch (c->algorithm_mac) {
	case SSL_AEAD:
		return NID_undef;
	case SSL_MD5:
		return NID_md5;
	case SSL_SHA1:
		return NID_sha1;
	case SSL_SHA256:
		return NID_sha256;
	case SSL_SHA384:
		return NID_sha384;
	default:
		return NID_undef;
	}
}
LSSL_ALIAS(SSL_CIPHER_get_digest_nid);

int
SSL_CIPHER_get_kx_nid(const SSL_CIPHER *c)
{
	switch (c->algorithm_mkey) {
	case SSL_kDHE:
		return NID_kx_dhe;
	case SSL_kECDHE:
		return NID_kx_ecdhe;
	case SSL_kRSA:
		return NID_kx_rsa;
	default:
		return NID_undef;
	}
}
LSSL_ALIAS(SSL_CIPHER_get_kx_nid);

int
SSL_CIPHER_get_auth_nid(const SSL_CIPHER *c)
{
	switch (c->algorithm_auth) {
	case SSL_aNULL:
		return NID_auth_null;
	case SSL_aECDSA:
		return NID_auth_ecdsa;
	case SSL_aRSA:
		return NID_auth_rsa;
	default:
		return NID_undef;
	}
}
LSSL_ALIAS(SSL_CIPHER_get_auth_nid);

const EVP_MD *
SSL_CIPHER_get_handshake_digest(const SSL_CIPHER *c)
{
	switch (c->algorithm2 & SSL_HANDSHAKE_MAC_MASK) {
	case SSL_HANDSHAKE_MAC_SHA256:
		return EVP_sha256();
	case SSL_HANDSHAKE_MAC_SHA384:
		return EVP_sha384();
	default:
		return NULL;
	}
}
LSSL_ALIAS(SSL_CIPHER_get_handshake_digest);

int
SSL_CIPHER_is_aead(const SSL_CIPHER *c)
{
	return (c->algorithm_mac & SSL_AEAD) == SSL_AEAD;
}
LSSL_ALIAS(SSL_CIPHER_is_aead);

void *
SSL_COMP_get_compression_methods(void)
{
	return NULL;
}
LSSL_ALIAS(SSL_COMP_get_compression_methods);

const char *
SSL_COMP_get_name(const void *comp)
{
	return NULL;
}
LSSL_ALIAS(SSL_COMP_get_name);
