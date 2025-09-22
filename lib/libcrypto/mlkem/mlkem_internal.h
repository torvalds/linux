/*	$OpenBSD: mlkem_internal.h,v 1.10 2025/09/05 23:30:12 beck Exp $ */
/*
 * Copyright (c) 2023, Google Inc.
 * Copyright (c) 2025, Bob Beck <beck@obtuse.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef OPENSSL_HEADER_CRYPTO_MLKEM_INTERNAL_H
#define OPENSSL_HEADER_CRYPTO_MLKEM_INTERNAL_H

#include "bytestring.h"
#include "mlkem.h"

#if defined(__cplusplus)
extern "C" {
#endif

__BEGIN_HIDDEN_DECLS

/* Public opaque ML-KEM key structures. */

#define MLKEM_PUBLIC_KEY_UNINITIALIZED 1
#define MLKEM_PUBLIC_KEY_INITIALIZED 2
#define MLKEM_PRIVATE_KEY_UNINITIALIZED 3
#define MLKEM_PRIVATE_KEY_INITIALIZED 4

struct MLKEM_public_key_st {
	uint16_t rank;
	int state;
	struct MLKEM768_public_key *key_768;
	struct MLKEM1024_public_key *key_1024;
};

struct MLKEM_private_key_st {
	uint16_t rank;
	int state;
	struct MLKEM768_private_key *key_768;
	struct MLKEM1024_private_key *key_1024;
};

/*
 * ML-KEM-768 and ML-KEM-1024
 *
 * This implements the Module-Lattice-Based Key-Encapsulation Mechanism from
 * https://csrc.nist.gov/pubs/fips/204/final
 *
 * You should prefer ML-KEM-768 where possible. ML-KEM-1024 is larger and exists
 * for people who are obsessed with more 'bits of crypto', and who are also
 * lacking the knowledge to realize that anything that can count to 256 bits
 * must likely use an equivalent amount of energy to that of an entire star to
 * do so.
 *
 * ML-KEM-768 is adequate to protect against a future cryptographically relevant
 * quantum computer, VIC 20, abacus, or carefully calibrated reference dog. I
 * for one plan on welcoming our new Kardashev-II civilization overlords with
 * open arms. In the meantime will not waste bytes on the wire by to adding
 * the fear of the possible future existence of a cryptographically relevant
 * Dyson sphere to the aforementioned list of fear-inducing future
 * cryptographically relevant hypotheticals.
 *
 * If your carefully calibrated reference dog notices the sun starting to dim,
 * you might need ML-KEM-1024, but you probably have bigger concerns than
 * the decryption of your stored past TLS sessions at that point.
 */

/*
 * MLKEM1024_public_key contains an ML-KEM-1024 public key. The contents of this
 * object should never leave the address space since the format is unstable.
 */
struct MLKEM1024_public_key {
	uint8_t bytes[512 * (4 + 16) + 32 + 32];
	uint16_t alignment;
};

/*
 * MLKEM1024_private_key contains a ML-KEM-1024 private key. The contents of
 * this object should never leave the address space since the format is
 * unstable.
 */
struct MLKEM1024_private_key {
	uint8_t bytes[512 * (4 + 4 + 16) + 32 + 32 + 32];
	uint16_t alignment;
};

/*
 * MLKEM768_public_key contains a ML-KEM-768 public key. The contents of this
 * object should never leave the address space since the format is unstable.
 */
struct MLKEM768_public_key {
	uint8_t bytes[512 * (3 + 9) + 32 + 32];
	uint16_t alignment;
};

/*
 * MLKEM768_private_key contains a ML-KEM-768 private key. The contents of this
 * object should never leave the address space since the format is unstable.
 */
struct MLKEM768_private_key {
	uint8_t bytes[512 * (3 + 3 + 9) + 32 + 32 + 32];
	uint16_t alignment;
};

/*
 * MLKEM_SEED_LENGTH is the number of bytes in an ML-KEM seed. An ML-KEM
 * seed is normally used to represent a private key.
 */
#define MLKEM_SEED_LENGTH 64

/*
 * MLKEM_SHARED_SECRET_LENGTH is the number of bytes in an ML-KEM shared
 * secret.
 */
#define MLKEM_SHARED_SECRET_LENGTH 32

/*
 * MLKEM_ENCAP_ENTROPY is the number of bytes of uniformly random entropy
 * necessary to encapsulate a secret. The entropy will be leaked to the
 * decapsulating party.
 */
#define MLKEM_ENCAP_ENTROPY 32

/* MLKEM1024_CIPHERTEXT_BYTES is number of bytes in the ML-KEM-1024 ciphertext. */
#define MLKEM1024_CIPHERTEXT_BYTES 1568

/* MLKEM768_CIPHERTEXT_BYTES is number of bytes in the ML-KEM768 ciphertext. */
#define MLKEM768_CIPHERTEXT_BYTES 1088

/*
 * MLKEM768_PUBLIC_KEY_BYTES is the number of bytes in an encoded ML-KEM768 public
 * key.
 */
#define MLKEM768_PUBLIC_KEY_BYTES 1184

/*
 * MLKEM1024_PUBLIC_KEY_BYTES is the number of bytes in an encoded ML-KEM-1024
 * public key.
 */
#define MLKEM1024_PUBLIC_KEY_BYTES 1568

/*
 * MLKEM768_PRIVATE_KEY_BYTES is the length of the data produced by
 * |marshal_private_key| for a RANK768 MLKEM_private_key.
 */
#define MLKEM768_PRIVATE_KEY_BYTES 2400

/*
 * MLKEM1024_PRIVATE_KEY_BYTES is the length of the data produced by
 * |marshal_private_key| for a RANK1024 MLKEM_private_key.
 */
#define MLKEM1024_PRIVATE_KEY_BYTES 3168

/*
 * Internal MLKEM 768 and MLKEM 1024 functions come largely from BoringSSL, but
 * converted to C from templated C++. Due to this history, most internal
 * functions do not allocate, and are expected to be handed memory allocated by
 * the caller. The caller is generally expected to know what sizes to allocate
 * based upon the rank of the key (either public or private) that they are
 * starting with. This avoids the need to handle memory allocation failures
 * (which boring in C++ just crashes by choice) deep in the implementation, as
 * what is needed is allocated up front in the public facing functions, and
 * failure is handled there.
 */

/* Key generation. */

/*
 * mlkem_generate_key generates a random public/private key pair, writes the
 * encoded public key to |out_encoded_public_key| and sets |out_private_key| to
 * the private key. If |optional_out_seed| is not NULL then the seed used to
 * generate the private key is written to it. The caller is responsible for
 * ensuring that |out_encoded_public_key| and |out_optonal_seed| point to
 * enough memory to contain a key and seed for the rank of |out_private_key|.
 */
int mlkem_generate_key(uint8_t *out_encoded_public_key,
    uint8_t *optional_out_seed, MLKEM_private_key *out_private_key);

/*
 * mlkem_private_key_from_seed modifies |out_private_key| to generate a key of
 * the rank of |*out_private_key| from a seed that was generated by
 * |mlkem_generate_key|. It fails and returns 0 if |seed_len| is incorrect, or
 * if |*out_private_key| has not been initialized. otherwise it writes to
 * |*out_private_key| and returns 1.
 */
int mlkem_private_key_from_seed(const uint8_t *seed, size_t seed_len,
    MLKEM_private_key *out_private_key);

/*
 * mlkem_public_from_private sets |*out_public_key| to the public key that
 * corresponds to |*private_key|. (This is faster than parsing the output of
 * |MLKEM_generate_key| if, for some reason, you need to encapsulate to a key
 * that was just generated.)
 */
void mlkem_public_from_private(const MLKEM_private_key *private_key,
    MLKEM_public_key *out_public_key);


/* Encapsulation and decapsulation of secrets. */

/*
 * mlkem_encap encrypts a random shared secret for |public_key|, writes the
 * ciphertext to |out_ciphertext|, and writes the random shared secret to
 * |out_shared_secret|.
 */
void mlkem_encap(const MLKEM_public_key *public_key,
    uint8_t out_ciphertext[MLKEM768_CIPHERTEXT_BYTES],
    uint8_t out_shared_secret[MLKEM_SHARED_SECRET_LENGTH]);

/*
 * mlkem_decap decrypts a shared secret from |ciphertext| using |private_key|
 * and writes it to |out_shared_secret|. If |ciphertext_len| is incorrect it
 * returns 0, otherwise it returns 1. If |ciphertext| is invalid,
 * |out_shared_secret| is filled with a key that will always be the same for the
 * same |ciphertext| and |private_key|, but which appears to be random unless
 * one has access to |private_key|. These alternatives occur in constant time.
 * Any subsequent symmetric encryption using |out_shared_secret| must use an
 * authenticated encryption scheme in order to discover the decapsulation
 * failure.
 */
int mlkem_decap(const MLKEM_private_key *private_key,
    const uint8_t *ciphertext, size_t ciphertext_len,
    uint8_t out_shared_secret[MLKEM_SHARED_SECRET_LENGTH]);


/* Serialisation of keys. */

/*
 * mlkem_marshal_public_key serializes |public_key| to |output| in the standard
 * format for ML-KEM public keys. It returns one on success or zero on allocation
 * error.
 */
int mlkem_marshal_public_key(const MLKEM_public_key *public_key,
    uint8_t **output, size_t *output_len);

/*
 * mlkem_parse_public_key parses a public key, in the format generated by
 * |MLKEM_marshal_public_key|, from |input| and writes the result to
 * |out_public_key|. It returns one on success or zero on parse error or if
 * there are trailing bytes in |input|.
 */
int mlkem_parse_public_key(const uint8_t *input, size_t input_len,
    MLKEM_public_key *out_public_key);

/*
 * mlkem_parse_private_key parses a private key, in the format generated by
 * |MLKEM_marshal_private_key|, from |input| and writes the result to
 * |out_private_key|. It returns one on success or zero on parse error or if
 * there are trailing bytes in |input|. This formate is verbose and should be avoided.
 * Private keys should be stored as seeds and parsed using |mlkem_private_key_from_seed|.
 */
int mlkem_parse_private_key(const uint8_t *input, size_t input_len,
    MLKEM_private_key *out_private_key);


/* Functions that are only used for test purposes. */

/*
 * mlkem_generate_key_external_entropy is a deterministic function to create a
 * pair of ML-KEM 768 keys, using the supplied entropy. The entropy needs to be
 * uniformly random generated. This function is should only be used for tests,
 * regular callers should use the non-deterministic |MLKEM_generate_key|
 * directly.
 */
int mlkem_generate_key_external_entropy(
    uint8_t out_encoded_public_key[MLKEM768_PUBLIC_KEY_BYTES],
    MLKEM_private_key *out_private_key,
    const uint8_t entropy[MLKEM_SEED_LENGTH]);

/*
 * mlkem_marshal_private_key serializes |private_key| to |out_private_key| in the standard
 * format for ML-KEM private keys. It returns one on success or zero on
 * allocation error.
 */
int mlkem_marshal_private_key(const MLKEM_private_key *private_key,
    uint8_t **out_private_key, size_t *out_private_key_len);

/*
 * mlkem_encap_external_entropy behaves like |mlkem_encap|, but uses
 * |MLKEM_ENCAP_ENTROPY| bytes of |entropy| for randomization. The decapsulating
 * side will be able to recover |entropy| in full. This function should only be
 * used for tests, regular callers should use the non-deterministic
 * |MLKEM_encap| directly.
 */
void mlkem_encap_external_entropy(
    uint8_t out_ciphertext[MLKEM768_CIPHERTEXT_BYTES],
    uint8_t out_shared_secret[MLKEM_SHARED_SECRET_LENGTH],
    const MLKEM_public_key *public_key,
    const uint8_t entropy[MLKEM_ENCAP_ENTROPY]);

/*
 * |MLKEM_encap_external_entropy| behaves exactly like the public |MLKEM_encap|
 * with the entropy provided by the caller. It is directly called internally
 * and by tests.
 */
int MLKEM_encap_external_entropy(const MLKEM_public_key *public_key,
    const uint8_t *entropy, uint8_t **out_ciphertext,
    size_t *out_ciphertext_len, uint8_t **out_shared_secret,
    size_t *out_shared_secret_len);

/*
 * |MLKEM_generate_key_external_entropy| behaves exactly like the public
 * |MLKEM_generate_key| with the entropy provided by the caller.
 * It is directly called internally and by tests.
 */
int MLKEM_generate_key_external_entropy(MLKEM_private_key *private_key,
    uint8_t **out_encoded_public_key, size_t *out_encoded_public_key_len,
    const uint8_t *entropy);


__END_HIDDEN_DECLS

#if defined(__cplusplus)
}
#endif

#endif  /* OPENSSL_HEADER_CRYPTO_MLKEM_INTERNAL_H */
