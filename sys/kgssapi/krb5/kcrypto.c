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
#include <sys/malloc.h>
#include <sys/kobj.h>
#include <sys/mbuf.h>

#include <kgssapi/gssapi.h>
#include <kgssapi/gssapi_impl.h>

#include "kcrypto.h"

static struct krb5_encryption_class *krb5_encryption_classes[] = {
	&krb5_des_encryption_class,
	&krb5_des3_encryption_class,
	&krb5_aes128_encryption_class,
	&krb5_aes256_encryption_class,
	&krb5_arcfour_encryption_class,
	&krb5_arcfour_56_encryption_class,
	NULL
};

struct krb5_encryption_class *
krb5_find_encryption_class(int etype)
{
	int i;

	for (i = 0; krb5_encryption_classes[i]; i++) {
		if (krb5_encryption_classes[i]->ec_type == etype)
			return (krb5_encryption_classes[i]);
	}
	return (NULL);
}

struct krb5_key_state *
krb5_create_key(const struct krb5_encryption_class *ec)
{
	struct krb5_key_state *ks;

	ks = malloc(sizeof(struct krb5_key_state), M_GSSAPI, M_WAITOK);
	ks->ks_class = ec;
	refcount_init(&ks->ks_refs, 1);
	ks->ks_key = malloc(ec->ec_keylen, M_GSSAPI, M_WAITOK);
	ec->ec_init(ks);

	return (ks);
}

void
krb5_free_key(struct krb5_key_state *ks)
{

	if (refcount_release(&ks->ks_refs)) {
		ks->ks_class->ec_destroy(ks);
		bzero(ks->ks_key, ks->ks_class->ec_keylen);
		free(ks->ks_key, M_GSSAPI);
		free(ks, M_GSSAPI);
	}
}

static size_t
gcd(size_t a, size_t b)
{

	if (b == 0)
		return (a);
	return gcd(b, a % b);
}

static size_t
lcm(size_t a, size_t b)
{
	return ((a * b) / gcd(a, b));
}

/*
 * Rotate right 13 of a variable precision number in 'in', storing the
 * result in 'out'. The number is assumed to be big-endian in memory
 * representation.
 */
static void
krb5_rotate_right_13(uint8_t *out, uint8_t *in, size_t numlen)
{
	uint32_t carry;
	size_t i;

	/*
	 * Special case when numlen == 1. A rotate right 13 of a
	 * single byte number changes to a rotate right 5.
	 */
	if (numlen == 1) {
		carry = in[0] >> 5;
		out[0] = (in[0] << 3) | carry;
		return;
	}

	carry = ((in[numlen - 2] & 31) << 8) | in[numlen - 1];
	for (i = 2; i < numlen; i++) {
		out[i] = ((in[i - 2] & 31) << 3) | (in[i - 1] >> 5);
	}
	out[1] = ((carry & 31) << 3) | (in[0] >> 5);
	out[0] = carry >> 5;
}

/*
 * Add two variable precision numbers in big-endian representation
 * using ones-complement arithmetic.
 */
static void
krb5_ones_complement_add(uint8_t *out, const uint8_t *in, size_t len)
{
	int n, i;

	/*
	 * First calculate the 2s complement sum, remembering the
	 * carry.
	 */
	n = 0;
	for (i = len - 1; i >= 0; i--) {
		n = out[i] + in[i] + n;
		out[i] = n;
		n >>= 8;
	}
	/*
	 * Then add back the carry.
	 */
	for (i = len - 1; n && i >= 0; i--) {
		n = out[i] + n;
		out[i] = n;
		n >>= 8;
	}
}

static void
krb5_n_fold(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
	size_t tmplen;
	uint8_t *tmp;
	size_t i;
	uint8_t *p;

	tmplen = lcm(inlen, outlen);
	tmp = malloc(tmplen, M_GSSAPI, M_WAITOK);

	bcopy(in, tmp, inlen);
	for (i = inlen, p = tmp; i < tmplen; i += inlen, p += inlen) {
		krb5_rotate_right_13(p + inlen, p, inlen);
	}
	bzero(out, outlen);
	for (i = 0, p = tmp; i < tmplen; i += outlen, p += outlen) {
		krb5_ones_complement_add(out, p, outlen);
	}
	free(tmp, M_GSSAPI);
}

struct krb5_key_state *
krb5_derive_key(struct krb5_key_state *inkey,
    void *constant, size_t constantlen)
{
	struct krb5_key_state *dk;
	const struct krb5_encryption_class *ec = inkey->ks_class;
	uint8_t *folded;
	uint8_t *bytes, *p, *q;
	struct mbuf *m;
	int randomlen, i;

	/*
	 * Expand the constant to blocklen bytes.
	 */
	folded = malloc(ec->ec_blocklen, M_GSSAPI, M_WAITOK);
	krb5_n_fold(folded, ec->ec_blocklen, constant, constantlen);

	/*
	 * Generate enough bytes for keybits rounded up to a multiple
	 * of blocklen.
	 */
	randomlen = roundup(ec->ec_keybits / 8, ec->ec_blocklen);
	bytes = malloc(randomlen, M_GSSAPI, M_WAITOK);
	MGET(m, M_WAITOK, MT_DATA);
	m->m_len = ec->ec_blocklen;
	for (i = 0, p = bytes, q = folded; i < randomlen;
	     q = p, i += ec->ec_blocklen, p += ec->ec_blocklen) {
		bcopy(q, m->m_data, ec->ec_blocklen);
		krb5_encrypt(inkey, m, 0, ec->ec_blocklen, NULL, 0);
		bcopy(m->m_data, p, ec->ec_blocklen);
	}
	m_free(m);

	dk = krb5_create_key(ec);
	krb5_random_to_key(dk, bytes);

	free(folded, M_GSSAPI);
	free(bytes, M_GSSAPI);

	return (dk);
}

static struct krb5_key_state *
krb5_get_usage_key(struct krb5_key_state *basekey, int usage, int which)
{
	const struct krb5_encryption_class *ec = basekey->ks_class;

	if (ec->ec_flags & EC_DERIVED_KEYS) {
		uint8_t constant[5];

		constant[0] = usage >> 24;
		constant[1] = usage >> 16;
		constant[2] = usage >> 8;
		constant[3] = usage;
		constant[4] = which;
		return (krb5_derive_key(basekey, constant, 5));
	} else {
		refcount_acquire(&basekey->ks_refs);
		return (basekey);
	}
}

struct krb5_key_state *
krb5_get_encryption_key(struct krb5_key_state *basekey, int usage)
{

	return (krb5_get_usage_key(basekey, usage, 0xaa));
}

struct krb5_key_state *
krb5_get_integrity_key(struct krb5_key_state *basekey, int usage)
{

	return (krb5_get_usage_key(basekey, usage, 0x55));
}

struct krb5_key_state *
krb5_get_checksum_key(struct krb5_key_state *basekey, int usage)
{

	return (krb5_get_usage_key(basekey, usage, 0x99));
}
