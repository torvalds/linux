/*	$OpenBSD: sm2.h,v 1.4 2025/01/25 17:59:44 tb Exp $ */
/*
 * Copyright (c) 2017, 2019 Ribose Inc
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#ifndef HEADER_SM2_H
#define HEADER_SM2_H

#include <openssl/opensslconf.h>

#include <openssl/ec.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * SM2 signature generation.
 */
int SM2_sign(const unsigned char *dgst, int dgstlen, unsigned char *sig,
    unsigned int *siglen, EC_KEY *eckey);

/*
 * SM2 signature verification. Assumes input is an SM3 digest
 */
int SM2_verify(const unsigned char *dgst, int dgstlen, const unsigned char *sig,
    int siglen, EC_KEY *eckey);

/*
 * SM2 encryption
 */
int SM2_ciphertext_size(const EC_KEY *key, const EVP_MD *digest, size_t msg_len,
    size_t *c_size);

int SM2_plaintext_size(const EC_KEY *key, const EVP_MD *digest, size_t msg_len,
    size_t *pl_size);

int SM2_encrypt(const EC_KEY *key, const EVP_MD *digest, const uint8_t *msg,
    size_t msg_len, uint8_t *ciphertext_buf, size_t *ciphertext_len);

int SM2_decrypt(const EC_KEY *key, const EVP_MD *digest,
    const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *ptext_buf,
    size_t *ptext_len);

void ERR_load_SM2_strings(void);

/* Error codes for the SM2 functions. */

/* Function codes. */
# define SM2_F_PKEY_SM2_CTRL                              274
# define SM2_F_PKEY_SM2_CTRL_STR                          275
# define SM2_F_PKEY_SM2_KEYGEN                            276
# define SM2_F_PKEY_SM2_PARAMGEN                          277
# define SM2_F_PKEY_SM2_SIGN                              278
# define SM2_F_PKEY_SM2_VERIFY                            279
# define SM2_F_PKEY_SM2_ENCRYPT                           280
# define SM2_F_PKEY_SM2_DECRYPT                           281

/* Reason codes. */
# define SM2_R_ASN1_ERROR                                 115
# define SM2_R_ASN5_ERROR                                 1150
# define SM2_R_BAD_SIGNATURE                              156
# define SM2_R_BIGNUM_OUT_OF_RANGE                        144
# define SM2_R_BUFFER_TOO_SMALL                           100
# define SM2_R_COORDINATES_OUT_OF_RANGE                   146
# define SM2_R_CURVE_DOES_NOT_SUPPORT_ECDH                160
# define SM2_R_CURVE_DOES_NOT_SUPPORT_SIGNING             159
# define SM2_R_D2I_ECPKPARAMETERS_FAILURE                 117
# define SM2_R_DECODE_ERROR                               142
# define SM2_R_DIGEST_FAILURE                             163
# define SM2_R_DISCRIMINANT_IS_ZERO                       118
# define SM2_R_EC_GROUP_NEW_BY_NAME_FAILURE               119
# define SM2_R_FIELD_TOO_LARGE                            143
# define SM2_R_GF2M_NOT_SUPPORTED                         147
# define SM2_R_GROUP2PKPARAMETERS_FAILURE                 120
# define SM2_R_I2D_ECPKPARAMETERS_FAILURE                 121
# define SM2_R_INCOMPATIBLE_OBJECTS                       101
# define SM2_R_INVALID_ARGUMENT                           112
# define SM2_R_INVALID_COMPRESSED_POINT                   110
# define SM2_R_INVALID_COMPRESSION_BIT                    109
# define SM2_R_INVALID_CURVE                              141
# define SM2_R_INVALID_DIGEST                             151
# define SM2_R_INVALID_DIGEST_TYPE                        138
# define SM2_R_INVALID_ENCODING                           102
# define SM2_R_INVALID_FIELD                              103
# define SM2_R_INVALID_FORM                               104
# define SM2_R_INVALID_GROUP_ORDER                        122
# define SM2_R_INVALID_KEY                                116
# define SM2_R_INVALID_OUTPUT_LENGTH                      161
# define SM2_R_INVALID_PEER_KEY                           133
# define SM2_R_INVALID_PENTANOMIAL_BASIS                  132
# define SM2_R_INVALID_PRIVATE_KEY                        123
# define SM2_R_INVALID_TRINOMIAL_BASIS                    137
# define SM2_R_KDF_FAILURE                                162
# define SM2_R_KDF_PARAMETER_ERROR                        148
# define SM2_R_KEYS_NOT_SET                               140
# define SM2_R_MISSING_PARAMETERS                         124
# define SM2_R_MISSING_PRIVATE_KEY                        125
# define SM2_R_NEED_NEW_SETUP_VALUES                      157
# define SM2_R_NOT_A_NIST_PRIME                           135
# define SM2_R_NOT_IMPLEMENTED                            126
# define SM2_R_NOT_INITIALIZED                            111
# define SM2_R_NO_PARAMETERS_SET                          139
# define SM2_R_NO_PRIVATE_VALUE                           154
# define SM2_R_OPERATION_NOT_SUPPORTED                    152
# define SM2_R_PASSED_NULL_PARAMETER                      134
# define SM2_R_PEER_KEY_ERROR                             149
# define SM2_R_PKPARAMETERS2GROUP_FAILURE                 127
# define SM2_R_POINT_ARITHMETIC_FAILURE                   155
# define SM2_R_POINT_AT_INFINITY                          106
# define SM2_R_POINT_IS_NOT_ON_CURVE                      107
# define SM2_R_RANDOM_NUMBER_GENERATION_FAILED            158
# define SM2_R_SHARED_INFO_ERROR                          150
# define SM2_R_SLOT_FULL                                  108
# define SM2_R_UNDEFINED_GENERATOR                        113
# define SM2_R_UNDEFINED_ORDER                            128
# define SM2_R_UNKNOWN_GROUP                              129
# define SM2_R_UNKNOWN_ORDER                              114
# define SM2_R_UNSUPPORTED_FIELD                          131
# define SM2_R_WRONG_CURVE_PARAMETERS                     145
# define SM2_R_WRONG_ORDER                                130

#ifdef  __cplusplus
}
#endif
#endif
