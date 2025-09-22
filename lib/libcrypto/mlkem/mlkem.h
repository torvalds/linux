/*	$OpenBSD: mlkem.h,v 1.8 2025/08/19 21:37:08 tb Exp $ */
/*
 * Copyright (c) 2025 Bob Beck <beck@obtuse.com>
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

#ifndef OPENSSL_HEADER_MLKEM_H
#define OPENSSL_HEADER_MLKEM_H

#include <sys/types.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * ML-KEM constants
 */

#define RANK768 3
#define RANK1024 4

/*
 * ML-KEM keys
 */

typedef struct MLKEM_private_key_st MLKEM_private_key;
typedef struct MLKEM_public_key_st MLKEM_public_key;

/*
 * MLKEM_private_key_new allocates a new uninitialized ML-KEM private key for
 * |rank|, which must be RANK768 or RANK1024. It returns a pointer to an
 * allocated structure suitable for holding a generated private key of the
 * corresponding rank on success, NULL is returned on failure. The caller is
 * responsible for deallocating the resulting key with |MLKEM_private_key_free|.
 */
MLKEM_private_key *MLKEM_private_key_new(int rank);

/*
 * MLKEM_private_key_free zeroes and frees all memory for |key| if |key| is
 * non NULL. If |key| is NULL it does nothing and returns.
 */
void MLKEM_private_key_free(MLKEM_private_key *key);

/*
 * MLKEM_private_key_encoded_length the number of bytes used by the encoded form
 * of |key|. Thie corresponds to the length of the buffer allocated for the
 * encoded_public_key from |MLKEM_marshal_private_key|. Zero is returned if
 * |key| is NULL or has an invalid rank.
 */
size_t MLKEM_private_key_encoded_length(const MLKEM_private_key *key);

/*
 * MLKEM_private_key_ciphertext_length returns the number of bytes of ciphertext
 * required to decrypt a shared secret with |key| using |MLKEM_decap|. Zero is
 * returned if |key| is NULL or has an invalid rank.
 */
size_t MLKEM_private_key_ciphertext_length(const MLKEM_private_key *key);

/*
 * MLKEM_public_key_new allocates a new uninitialized ML-KEM public key for
 * |rank|, which must be RANK768 or RANK1024. It returns a pointer to an
 * allocated structure suitable for holding a generated public key of the
 * corresponding rank on success, NULL is returned on failure. The caller is
 * responsible for deallocating the resulting key with |MLKEM_public_key_free|.
 */
MLKEM_public_key *MLKEM_public_key_new(int rank);

/*
 * MLKEM_public_key_free zeros and deallocates all memory for |key| if |key| is
 * non NULL. If |key| is NULL it does nothing and returns.
 */
void MLKEM_public_key_free(MLKEM_public_key *key);

/*
 * MLKEM_public_key_encoded_length the number of bytes used by the encoded form
 * of |key|. Thie corresponds to the length of the buffer allocated for the
 * encoded_public_key from |MLKEM_generate_key| or |MLKEM_marshal_public_key|.
 * Zero is returned if |key| is NULL or has an invalid rank.
 */
size_t MLKEM_public_key_encoded_length(const MLKEM_public_key *key);

/*
 * MLKEM_public_key_cipertext_length returns the number of bytes produced as the
 * ciphertext when encrypting a shared secret with |key| using |MLKEM_encap|. Zero
 * is returned if |key| is NULL or has an invalid rank.
 */
size_t MLKEM_public_key_ciphertext_length(const MLKEM_public_key *key);

/*
 * ML-KEM operations
 */

/*
 * MLKEM_generate_key generates a random private/public key pair, initializing
 * |private_key|. It returns one on success, and zero on failure or error.
 * |private_key| must be a new uninitialized key. |*out_encoded_public_key| and
 * |*out_optional_seed|, if provided, must have the value of NULL. On success, a
 * pointer to the encoded public key of the correct size for |key| is returned
 * in |out_encoded_public_key|, and the length in bytes of
 * |*out_encoded_public_key| is returned in |out_encoded_public_key_len|. If
 * |out_optional_seed| is not NULL, a pointer to the seed used to generate the
 * private key is returned in |*out_optional_seed| and the length in bytes of
 * the seed is returned in |*out_optional_seed_len|. The caller is responsible
 * for freeing the values returned in |out_encoded_public_key|, and
 * |out_optional_seed|.
 *
 * In the event a private key needs to be saved, The normal best practice is to
 * save |out_optional_seed| as the private key, along with the ML-KEM rank value.
 * An MLKEM_private_key of the correct rank can then be constructed using
 * |MLKEM_private_key_from_seed|.
 */
int MLKEM_generate_key(MLKEM_private_key *private_key,
    uint8_t **out_encoded_public_key, size_t *out_encoded_public_key_len,
    uint8_t **out_optional_seed, size_t *out_optional_seed_len);

/*
 * MLKEM_private_key_from_seed derives a private key from a seed that was
 * generated by |MLKEM_generate_key| initializing |private_key|. It returns one
 * on success, and zero on failure or error. |private_key| must be a new
 * uninitialized key. |seed_len| must be MLKEM_SEED_LENGTH.
 *
 * For |private_key| to match the key generated by |MLKEM_generate_key|,
 * |private_key| must have been created with the same rank as used when generating
 * the key.
 */
int MLKEM_private_key_from_seed(MLKEM_private_key *private_key,
    const uint8_t *seed, size_t seed_len);

/*
 * MLKEM_public_from_private initializes |public_key| with the public key that
 * corresponds to |private_key|. It returns one on success and zero on
 * error. This is faster than parsing the output of |MLKEM_generate_key| if, for
 * some reason, you need to encapsulate to a key that was just
 * generated. |private key| must be a new uninitialized key, of the same rank as
 * |public_key|.
 */
int MLKEM_public_from_private(const MLKEM_private_key *private_key,
    MLKEM_public_key *public_key);

/*
 * MLKEM_encap encrypts a random shared secret for an initialized
 * |public_key|. It returns one on success, and zero on failure or error. |*out
 * ciphertext| and |*out_shared_secret| must have the value NULL. On success, a
 * pointer to the ciphertext of the correct size for |key| is returned in
 * |out_ciphertext|, the length in bytes of |*out_ciphertext| is returned in
 * |*out_ciphertext_len|, a pointer to the random shared secret is returned in
 * |out_shared_secret|, and the length in bytes of |*out_shared_secret| is
 * returned in |*out_ciphtertext_len|. The caller is responsible for zeroing and
 * freeing the values returned in |out_ciphertext| and |out_shared_secret|
 */
int MLKEM_encap(const MLKEM_public_key *public_key,
    uint8_t **out_ciphertext, size_t *out_ciphertext_len,
    uint8_t **out_shared_secret, size_t *out_shared_secret_len);

/*
 * MLKEM_decap decrypts a shared secret from |ciphertext| using an initialized
 * |private_key|. It returns a pointer to the shared secret|out_shared_secret|
 * and the length in bytes of |*out_shared_secret| in |*out_shared_secret_len|.
 *
 * If |ciphertext_len| is incorrect for |private_key|, |*out_shared_secret| is
 * not NULL, or memory can not be allocated, it returns zero, otherwise it
 * returns one. If |ciphertext| is invalid, a pointer is returned in
 * |out_shared_secret| pointing to a key that will always be the same for the
 * same |ciphertext| and |private_key|, but which appears to be random unless
 * one has access to |private_key|. These alternatives occur in constant time.
 * Any subsequent symmetric encryption using |out_shared_secret| must use an
 * authenticated encryption scheme in order to discover the decapsulation
 * failure. The caller is responsible for zeroing and freeing the value returned
 * in |out_shared_secret|.
 */
int MLKEM_decap(const MLKEM_private_key *private_key,
    const uint8_t *ciphertext, size_t ciphertext_len,
    uint8_t **out_shared_secret, size_t *out_shared_secret_len);

/* Serialization of ML-KEM keys. */

/*
 * MLKEM_marshal_public_key serializes an initialized |public_key| in the
 * standard format for ML-KEM public keys. It returns one on success or zero on
 * allocation error or failure. |*out| must have the value NULL. On success a
 * pointer is returned in |out| to the encoded public key matching |public_key|,
 * and a pointer to the length in bytes of the encoded public key is stored in
 * |out_len|. The caller is responsible for freeing the values returned in
 * |out|.
 */
int MLKEM_marshal_public_key(const MLKEM_public_key *public_key, uint8_t **out,
    size_t *out_len);

/*
 * MLKEM_parse_public_key parses a public key, in the format generated by
 * |MLKEM_marshal_public_key|, from |in|. It returns one on success or zero on
 * error or failure. |public_key| must be a new uninitialized key. |in_len| must
 * be the correct length for the encoded format of |public_key. On success
 * |public_key| is initialized to the value parsed from |in|.
 */
int MLKEM_parse_public_key(MLKEM_public_key *public_key, const uint8_t *in,
    size_t in_len);

/*
 * Marshals a private key to encoded format, used for NIST tests.
 */
int MLKEM_marshal_private_key(const MLKEM_private_key *private_key,
    uint8_t **out, size_t *out_len);

/*
 * MLKEM_parse_private_key parses a private key, in the format generated by
 * |MLKEM_marshal_private_key|, from |in|. It returns one on success or zero on
 * error or failure. |private_key| must be a new uninitialized key. |in_len|
 * must be the correct length for the encoded format of |private_key. On success
 * |private_key| is initialized to the value parsed from |in|.
 *
 * This format is wastefully verbose and should be avoided. Private keys should
 * be stored as seeds from |MLKEM_generate_key|, and then parsed using
 * |MLKEM_private_key_from_seed|.
 */
int MLKEM_parse_private_key(MLKEM_private_key *private_key, const uint8_t *in,
    size_t in_len);

#if defined(__cplusplus)
}
#endif

#endif  /* OPENSSL_HEADER_MLKEM_H */
