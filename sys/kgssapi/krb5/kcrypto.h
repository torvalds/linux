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
 *
 * $FreeBSD$
 */

#include <sys/_iovec.h>

#define ETYPE_NULL		0
#define ETYPE_DES_CBC_CRC	1
#define ETYPE_DES_CBC_MD4	2
#define ETYPE_DES_CBC_MD5	3
#define ETYPE_DES3_CBC_MD5	5
#define ETYPE_OLD_DES3_CBC_SHA1	7
#define ETYPE_DES3_CBC_SHA1	16
#define ETYPE_AES128_CTS_HMAC_SHA1_96 17
#define ETYPE_AES256_CTS_HMAC_SHA1_96 18
#define ETYPE_ARCFOUR_HMAC_MD5	23
#define ETYPE_ARCFOUR_HMAC_MD5_56 24

/*
 * Key usages for des3-cbc-sha1 tokens
 */
#define KG_USAGE_SEAL		22
#define KG_USAGE_SIGN		23
#define KG_USAGE_SEQ		24

/*
 * Key usages for RFC4121 tokens
 */
#define KG_USAGE_ACCEPTOR_SEAL	22
#define KG_USAGE_ACCEPTOR_SIGN	23
#define KG_USAGE_INITIATOR_SEAL	24
#define KG_USAGE_INITIATOR_SIGN	25

struct krb5_key_state;

typedef void init_func(struct krb5_key_state *ks);
typedef void destroy_func(struct krb5_key_state *ks);
typedef void set_key_func(struct krb5_key_state *ks, const void *in);
typedef void random_to_key_func(struct krb5_key_state *ks, const void *in);
typedef void encrypt_func(const struct krb5_key_state *ks,
    struct mbuf *inout, size_t skip, size_t len, void *ivec, size_t ivlen);
typedef void checksum_func(const struct krb5_key_state *ks, int usage,
    struct mbuf *inout, size_t skip, size_t inlen, size_t outlen);

struct krb5_encryption_class {
	const char		*ec_name;
	int			ec_type;
	int			ec_flags;
#define EC_DERIVED_KEYS		1
	size_t			ec_blocklen;
	size_t			ec_msgblocklen;
	size_t			ec_checksumlen;
	size_t			ec_keybits;	/* key length in bits */
	size_t			ec_keylen;	/* size of key in memory */
	init_func		*ec_init;
	destroy_func		*ec_destroy;
	set_key_func		*ec_set_key;
	random_to_key_func	*ec_random_to_key;
	encrypt_func		*ec_encrypt;
	encrypt_func		*ec_decrypt;
	checksum_func		*ec_checksum;
};

struct krb5_key_state {
	const struct krb5_encryption_class *ks_class;
	volatile u_int		ks_refs;
	void			*ks_key;
	void			*ks_priv;
};

extern struct krb5_encryption_class krb5_des_encryption_class;
extern struct krb5_encryption_class krb5_des3_encryption_class;
extern struct krb5_encryption_class krb5_aes128_encryption_class;
extern struct krb5_encryption_class krb5_aes256_encryption_class;
extern struct krb5_encryption_class krb5_arcfour_encryption_class;
extern struct krb5_encryption_class krb5_arcfour_56_encryption_class;

static __inline void
krb5_set_key(struct krb5_key_state *ks, const void *keydata)
{

	ks->ks_class->ec_set_key(ks, keydata);
}

static __inline void
krb5_random_to_key(struct krb5_key_state *ks, const void *keydata)
{

	ks->ks_class->ec_random_to_key(ks, keydata);
}

static __inline void
krb5_encrypt(const struct krb5_key_state *ks, struct mbuf *inout,
    size_t skip, size_t len, void *ivec, size_t ivlen)
{

	ks->ks_class->ec_encrypt(ks, inout, skip, len, ivec, ivlen);
}

static __inline void
krb5_decrypt(const struct krb5_key_state *ks, struct mbuf *inout,
    size_t skip, size_t len, void *ivec, size_t ivlen)
{

	ks->ks_class->ec_decrypt(ks, inout, skip, len, ivec, ivlen);
}

static __inline void
krb5_checksum(const struct krb5_key_state *ks, int usage,
    struct mbuf *inout, size_t skip, size_t inlen, size_t outlen)
{

	ks->ks_class->ec_checksum(ks, usage, inout, skip, inlen, outlen);
}

extern struct krb5_encryption_class *
	krb5_find_encryption_class(int etype);
extern struct krb5_key_state *
	krb5_create_key(const struct krb5_encryption_class *ec);
extern void krb5_free_key(struct krb5_key_state *ks);
extern struct krb5_key_state *
	krb5_derive_key(struct krb5_key_state *inkey,
	    void *constant, size_t constantlen);
extern struct krb5_key_state *
	krb5_get_encryption_key(struct krb5_key_state *basekey, int usage);
extern struct krb5_key_state *
	krb5_get_integrity_key(struct krb5_key_state *basekey, int usage);
extern struct krb5_key_state *
	krb5_get_checksum_key(struct krb5_key_state *basekey, int usage);
