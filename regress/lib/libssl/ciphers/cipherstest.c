/*
 * Copyright (c) 2015, 2020 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/ssl.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

int ssl3_num_ciphers(void);
const SSL_CIPHER *ssl3_get_cipher_by_index(int idx);

int ssl_parse_ciphersuites(STACK_OF(SSL_CIPHER) **out_ciphers, const char *str);

static inline int
ssl_aes_is_accelerated(void)
{
	return (OPENSSL_cpu_caps() & CRYPTO_CPU_CAPS_ACCELERATED_AES) != 0;
}

static int
check_cipher_order(void)
{
	unsigned long id, prev_id = 0;
	const SSL_CIPHER *cipher;
	int num_ciphers;
	int i;

	num_ciphers = ssl3_num_ciphers();

	for (i = 0; i < num_ciphers; i++) {
		if ((cipher = ssl3_get_cipher_by_index(i)) == NULL) {
			fprintf(stderr, "FAIL: ssl3_get_cipher(%d) returned "
			    "NULL\n", i);
			return 1;
		}
		if ((id = SSL_CIPHER_get_id(cipher)) <= prev_id) {
			fprintf(stderr, "FAIL: ssl3_ciphers is not sorted by "
			    "id - cipher %d (%lx) <= cipher %d (%lx)\n",
			    i, id, i - 1, prev_id);
			return 1;
		}
		prev_id = id;
	}

	return 0;
}

struct ssl_cipher_test {
	uint16_t value;
	int auth_nid;
	int cipher_nid;
	int digest_nid;
	int handshake_digest_nid;
	int kx_nid;
	int strength_bits;
	int symmetric_bits;
	int is_aead;
};

static const struct ssl_cipher_test ssl_cipher_tests[] = {
	{
		.value = 0x0004,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_rc4,
		.digest_nid = NID_md5,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0005,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_rc4,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x000a,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_des_ede3_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 112,
		.symmetric_bits = 168,
	},
	{
		.value = 0x0016,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_des_ede3_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 112,
		.symmetric_bits = 168,
	},
	{
		.value = 0x0018,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_rc4,
		.digest_nid = NID_md5,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x001b,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_des_ede3_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 112,
		.symmetric_bits = 168,
	},
	{
		.value = 0x002f,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0033,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0034,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0035,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x0039,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x003a,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x003c,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x003d,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x0041,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0045,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0046,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_camellia_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x0067,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x006b,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x006c,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x006d,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x0084,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x0088,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x0089,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_camellia_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x009c,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
		.is_aead = 1,
	},
	{
		.value = 0x009d,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0x009e,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
		.is_aead = 1,
	},
	{
		.value = 0x009f,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0x00a6,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_128_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
		.is_aead = 1,
	},
	{
		.value = 0x00a7,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_256_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0x00ba,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x00be,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x00bf,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_camellia_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0x00c0,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_256_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_rsa,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x00c4,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_camellia_256_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x00c5,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_camellia_256_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0x1301,
		.auth_nid = NID_undef,
		.cipher_nid = NID_aes_128_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_undef,
		.strength_bits = 128,
		.symmetric_bits = 128,
		.is_aead = 1,
	},
	{
		.value = 0x1302,
		.auth_nid = NID_undef,
		.cipher_nid = NID_aes_256_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_undef,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0x1303,
		.auth_nid = NID_undef,
		.cipher_nid = NID_chacha20_poly1305,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_undef,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0xc007,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_rc4,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc008,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_des_ede3_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 112,
		.symmetric_bits = 168,
	},
	{
		.value = 0xc009,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc00a,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0xc011,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_rc4,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc012,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_des_ede3_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 112,
		.symmetric_bits = 168,
	},
	{
		.value = 0xc013,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc014,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0xc016,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_rc4,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc017,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_des_ede3_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 112,
		.symmetric_bits = 168,
	},
	{
		.value = 0xc018,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc019,
		.auth_nid = NID_auth_null,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha1,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0xc023,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc024,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha384,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0xc027,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_cbc,
		.digest_nid = NID_sha256,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
	},
	{
		.value = 0xc028,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_cbc,
		.digest_nid = NID_sha384,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
	},
	{
		.value = 0xc02b,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_aes_128_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
		.is_aead = 1,
	},
	{
		.value = 0xc02c,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_aes_256_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0xc02f,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_128_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 128,
		.symmetric_bits = 128,
		.is_aead = 1,
	},
	{
		.value = 0xc030,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_aes_256_gcm,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha384,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0xcca8,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_chacha20_poly1305,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0xcca9,
		.auth_nid = NID_auth_ecdsa,
		.cipher_nid = NID_chacha20_poly1305,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_ecdhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
	{
		.value = 0xccaa,
		.auth_nid = NID_auth_rsa,
		.cipher_nid = NID_chacha20_poly1305,
		.digest_nid = NID_undef,
		.handshake_digest_nid = NID_sha256,
		.kx_nid = NID_kx_dhe,
		.strength_bits = 256,
		.symmetric_bits = 256,
		.is_aead = 1,
	},
};

#define N_SSL_CIPHER_TESTS (sizeof(ssl_cipher_tests) / sizeof(ssl_cipher_tests[0]))

static int
test_ssl_ciphers(void)
{
	int i, strength_bits, symmetric_bits;
	const struct ssl_cipher_test *sct;
	STACK_OF(SSL_CIPHER) *ciphers;
	const SSL_CIPHER *cipher;
	const EVP_MD *digest;
	unsigned char buf[2];
	const char *description;
	char desc_buf[256];
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	size_t j;
	int ret = 1;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL) {
		fprintf(stderr, "SSL_CTX_new() returned NULL\n");
		goto failure;
	}
	if ((ssl = SSL_new(ssl_ctx)) == NULL) {
		fprintf(stderr, "SSL_new() returned NULL\n");
		goto failure;
	}
	if (!SSL_set_cipher_list(ssl, "ALL")) {
		fprintf(stderr, "SSL_set_cipher_list failed\n");
		goto failure;
	}

	if ((ciphers = SSL_get_ciphers(ssl)) == NULL) {
		fprintf(stderr, "no ciphers\n");
		goto failure;
	}

	if (sk_SSL_CIPHER_num(ciphers) != N_SSL_CIPHER_TESTS) {
		fprintf(stderr, "number of ciphers mismatch (%d != %zu)\n",
		    sk_SSL_CIPHER_num(ciphers), N_SSL_CIPHER_TESTS);
		goto failure;
	}

	for (i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
		uint16_t cipher_value;

		cipher = sk_SSL_CIPHER_value(ciphers, i);
		cipher_value = SSL_CIPHER_get_value(cipher);

		buf[0] = cipher_value >> 8;
		buf[1] = cipher_value & 0xff;

		if ((cipher = SSL_CIPHER_find(ssl, buf)) == NULL) {
			fprintf(stderr, "SSL_CIPHER_find() returned NULL for %s\n",
			    SSL_CIPHER_get_name(cipher));
			goto failure;
		}
		if (SSL_CIPHER_get_value(cipher) != cipher_value) {
			fprintf(stderr, "got cipher with value 0x%04x, want 0x%04x\n",
			    SSL_CIPHER_get_value(cipher), cipher_value);
			goto failure;
		}
		if (SSL_CIPHER_get_id(cipher) != (0x03000000UL | cipher_value)) {
			fprintf(stderr, "got cipher id 0x%08lx, want 0x%08lx\n",
			    SSL_CIPHER_get_id(cipher), (0x03000000UL | cipher_value));
			goto failure;
		}

		sct = NULL;
		for (j = 0; j < N_SSL_CIPHER_TESTS; j++) {
			if (ssl_cipher_tests[j].value == cipher_value) {
				sct = &ssl_cipher_tests[j];
				break;
			}
		}
		if (sct == NULL) {
			fprintf(stderr, "cipher '%s' (0x%04x) not found in test "
			    "table\n", SSL_CIPHER_get_name(cipher), cipher_value);
			goto failure;
		}

		if (SSL_CIPHER_get_auth_nid(cipher) != sct->auth_nid) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got auth nid %d, "
			    "want %d\n", SSL_CIPHER_get_name(cipher), cipher_value,
			    SSL_CIPHER_get_auth_nid(cipher), sct->auth_nid);
			goto failure;
		}
		if (SSL_CIPHER_get_cipher_nid(cipher) != sct->cipher_nid) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got cipher nid %d, "
			    "want %d\n", SSL_CIPHER_get_name(cipher), cipher_value,
			    SSL_CIPHER_get_cipher_nid(cipher), sct->cipher_nid);
			goto failure;
		}
		if (SSL_CIPHER_get_digest_nid(cipher) != sct->digest_nid) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got digest nid %d, "
			    "want %d\n", SSL_CIPHER_get_name(cipher), cipher_value,
			    SSL_CIPHER_get_digest_nid(cipher), sct->digest_nid);
			goto failure;
		}
		if (SSL_CIPHER_get_kx_nid(cipher) != sct->kx_nid) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got kx nid %d, "
			    "want %d\n", SSL_CIPHER_get_name(cipher), cipher_value,
			    SSL_CIPHER_get_kx_nid(cipher), sct->kx_nid);
			goto failure;
		}

		/* Having API consistency is a wonderful thing... */
		digest = SSL_CIPHER_get_handshake_digest(cipher);
		if (EVP_MD_nid(digest) != sct->handshake_digest_nid) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got handshake "
			    "digest nid %d, want %d\n", SSL_CIPHER_get_name(cipher),
			    cipher_value, EVP_MD_nid(digest), sct->handshake_digest_nid);
			goto failure;
		}

		strength_bits = SSL_CIPHER_get_bits(cipher, &symmetric_bits);
		if (strength_bits != sct->strength_bits) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got strength bits "
			    "%d, want %d\n", SSL_CIPHER_get_name(cipher),
			    cipher_value, strength_bits, sct->strength_bits);
			goto failure;
		}
		if (symmetric_bits != sct->symmetric_bits) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got symmetric bits "
			    "%d, want %d\n", SSL_CIPHER_get_name(cipher),
			    cipher_value, symmetric_bits, sct->symmetric_bits);
			goto failure;
		}
		if (SSL_CIPHER_is_aead(cipher) != sct->is_aead) {
			fprintf(stderr, "cipher '%s' (0x%04x) - got is aead %d, "
			    "want %d\n", SSL_CIPHER_get_name(cipher), cipher_value,
			    SSL_CIPHER_is_aead(cipher), sct->is_aead);
			goto failure;
		}

		if ((description = SSL_CIPHER_description(cipher, desc_buf,
		    sizeof(desc_buf))) != desc_buf) {
			fprintf(stderr, "cipher '%s' (0x%04x) - failed to get "
			    "description\n", SSL_CIPHER_get_name(cipher), cipher_value);
			goto failure;
		}
	}

	ret = 0;

 failure:
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return (ret);
}

struct parse_ciphersuites_test {
	const char *str;
	const int want;
	const unsigned long cids[32];
};

struct parse_ciphersuites_test parse_ciphersuites_tests[] = {
	{
		/* LibreSSL names. */
		.str = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256:AEAD-AES128-GCM-SHA256",
		.want = 1,
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_3_CK_AES_128_GCM_SHA256,
		},
	},
	{
		/* OpenSSL names. */
		.str = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256",
		.want = 1,
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_3_CK_AES_128_GCM_SHA256,
		},
	},
	{
		/* Different priority order. */
		.str = "AEAD-AES128-GCM-SHA256:AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.want = 1,
		.cids = {
			TLS1_3_CK_AES_128_GCM_SHA256,
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
		},
	},
	{
		/* Known but unsupported names. */
		.str = "AEAD-AES256-GCM-SHA384:AEAD-AES128-CCM-SHA256:AEAD-AES128-CCM-8-SHA256",
		.want = 1,
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
		},
	},
	{
		/* Empty string means no TLSv1.3 ciphersuites. */
		.str = "",
		.want = 1,
		.cids = { 0 },
	},
	{
		.str = "TLS_CHACHA20_POLY1305_SHA256:TLS_NOT_A_CIPHERSUITE",
		.want = 0,
	},
	{
		.str = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256,TLS_AES_128_GCM_SHA256",
		.want = 0,
	},
};

#define N_PARSE_CIPHERSUITES_TESTS \
    (sizeof(parse_ciphersuites_tests) / sizeof(*parse_ciphersuites_tests))

static int
parse_ciphersuites_test(void)
{
	struct parse_ciphersuites_test *pct;
	STACK_OF(SSL_CIPHER) *ciphers = NULL;
	SSL_CIPHER *cipher;
	int failed = 1;
	int j, ret;
	size_t i;

	for (i = 0; i < N_PARSE_CIPHERSUITES_TESTS; i++) {
		pct = &parse_ciphersuites_tests[i];

		ret = ssl_parse_ciphersuites(&ciphers, pct->str);
		if (ret != pct->want) {
			fprintf(stderr, "FAIL: test %zu - "
			    "ssl_parse_ciphersuites returned %d, want %d\n",
			    i, ret, pct->want);
			goto failed;
		}
		if (ret == 0)
			continue;

		for (j = 0; j < sk_SSL_CIPHER_num(ciphers); j++) {
			cipher = sk_SSL_CIPHER_value(ciphers, j);
			if (SSL_CIPHER_get_id(cipher) == pct->cids[j])
				continue;
			fprintf(stderr, "FAIL: test %zu - got cipher %d with "
			    "id %lx, want %lx\n", i, j,
			    SSL_CIPHER_get_id(cipher), pct->cids[j]);
			goto failed;
		}
		if (pct->cids[j] != 0) {
			fprintf(stderr, "FAIL: test %zu - got %d ciphers, "
			    "expected more", i, sk_SSL_CIPHER_num(ciphers));
			goto failed;
		}
	}

	failed = 0;

 failed:
	sk_SSL_CIPHER_free(ciphers);

	return failed;
}

struct cipher_set_test {
	int ctx_ciphersuites_first;
	const char *ctx_ciphersuites;
	const char *ctx_rulestr;
	int ssl_ciphersuites_first;
	const char *ssl_ciphersuites;
	const char *ssl_rulestr;
	int cids_aes_accel_fixup;
	unsigned long cids[32];
};

struct cipher_set_test cipher_set_tests[] = {
	{
		.ctx_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids_aes_accel_fixup = 1,
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_3_CK_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ssl_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids_aes_accel_fixup = 1,
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_3_CK_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ctx_ciphersuites_first = 1,
		.ctx_ciphersuites = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.ctx_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ssl_ciphersuites_first = 1,
		.ssl_ciphersuites = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.ssl_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ctx_ciphersuites_first = 0,
		.ctx_ciphersuites = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.ctx_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ssl_ciphersuites_first = 0,
		.ssl_ciphersuites = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.ssl_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ssl_ciphersuites_first = 1,
		.ssl_ciphersuites = "",
		.ssl_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ssl_ciphersuites_first = 0,
		.ssl_ciphersuites = "",
		.ssl_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ctx_ciphersuites = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.ssl_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
	{
		.ctx_rulestr = "TLSv1.2+ECDHE+AEAD+AES",
		.ssl_ciphersuites = "AEAD-AES256-GCM-SHA384:AEAD-CHACHA20-POLY1305-SHA256",
		.cids = {
			TLS1_3_CK_AES_256_GCM_SHA384,
			TLS1_3_CK_CHACHA20_POLY1305_SHA256,
			TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
			TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		},
	},
};

#define N_CIPHER_SET_TESTS \
    (sizeof(cipher_set_tests) / sizeof(*cipher_set_tests))

static int
cipher_set_test(void)
{
	struct cipher_set_test *cst;
	STACK_OF(SSL_CIPHER) *ciphers = NULL;
	SSL_CIPHER *cipher;
	SSL_CTX *ctx = NULL;
	SSL *ssl = NULL;
	int failed = 0;
	size_t i;
	int j;

	for (i = 0; i < N_CIPHER_SET_TESTS; i++) {
		cst = &cipher_set_tests[i];

		if (!ssl_aes_is_accelerated() && cst->cids_aes_accel_fixup) {
			cst->cids[0] = TLS1_3_CK_CHACHA20_POLY1305_SHA256;
			cst->cids[1] = TLS1_3_CK_AES_256_GCM_SHA384;
		}

		if ((ctx = SSL_CTX_new(TLS_method())) == NULL)
			errx(1, "SSL_CTX_new");

		if (cst->ctx_ciphersuites_first && cst->ctx_ciphersuites != NULL) {
			if (!SSL_CTX_set_ciphersuites(ctx, cst->ctx_ciphersuites))
				errx(1, "SSL_CTX_set_ciphersuites");
		}
		if (cst->ctx_rulestr != NULL) {
			if (!SSL_CTX_set_cipher_list(ctx, cst->ctx_rulestr))
				errx(1, "SSL_CTX_set_cipher_list");
		}
		if (!cst->ctx_ciphersuites_first && cst->ctx_ciphersuites != NULL) {
			if (!SSL_CTX_set_ciphersuites(ctx, cst->ctx_ciphersuites))
				errx(1, "SSL_CTX_set_ciphersuites");
		}

		/* XXX - check SSL_CTX_get_ciphers(ctx) */

		if ((ssl = SSL_new(ctx)) == NULL)
			errx(1, "SSL_new");

		if (cst->ssl_ciphersuites_first && cst->ssl_ciphersuites != NULL) {
			if (!SSL_set_ciphersuites(ssl, cst->ssl_ciphersuites))
				errx(1, "SSL_set_ciphersuites");
		}
		if (cst->ssl_rulestr != NULL) {
			if (!SSL_set_cipher_list(ssl, cst->ssl_rulestr))
				errx(1, "SSL_set_cipher_list");
		}
		if (!cst->ssl_ciphersuites_first && cst->ssl_ciphersuites != NULL) {
			if (!SSL_set_ciphersuites(ssl, cst->ssl_ciphersuites))
				errx(1, "SSL_set_ciphersuites");
		}

		ciphers = SSL_get_ciphers(ssl);

		for (j = 0; j < sk_SSL_CIPHER_num(ciphers); j++) {
			cipher = sk_SSL_CIPHER_value(ciphers, j);
			if (SSL_CIPHER_get_id(cipher) == cst->cids[j])
				continue;
			fprintf(stderr, "FAIL: test %zu - got cipher %d with "
			    "id %lx, want %lx\n", i, j,
			    SSL_CIPHER_get_id(cipher), cst->cids[j]);
			failed |= 1;
		}
		if (cst->cids[j] != 0) {
			fprintf(stderr, "FAIL: test %zu - got %d ciphers, "
			    "expected more", i, sk_SSL_CIPHER_num(ciphers));
			failed |= 1;
		}

		SSL_CTX_free(ctx);
		SSL_free(ssl);
	}

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= check_cipher_order();

	failed |= test_ssl_ciphers();

	failed |= parse_ciphersuites_test();
	failed |= cipher_set_test();

	return (failed);
}
