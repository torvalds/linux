/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_SPI_H
#define	_SYS_CRYPTO_SPI_H

/*
 * CSPI: Cryptographic Service Provider Interface.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef CONSTIFY_PLUGIN
#define	__no_const __attribute__((no_const))
#else
#define	__no_const
#endif /* CONSTIFY_PLUGIN */

#define	CRYPTO_SPI_VERSION_1	1
#define	CRYPTO_SPI_VERSION_2	2
#define	CRYPTO_SPI_VERSION_3	3

/*
 * Provider-private handle. This handle is specified by a provider
 * when it registers by means of the pi_provider_handle field of
 * the crypto_provider_info structure, and passed to the provider
 * when its entry points are invoked.
 */
typedef void *crypto_provider_handle_t;

/*
 * Context templates can be used to by software providers to pre-process
 * keying material, such as key schedules. They are allocated by
 * a software provider create_ctx_template(9E) entry point, and passed
 * as argument to initialization and atomic provider entry points.
 */
typedef void *crypto_spi_ctx_template_t;

/*
 * Request handles are used by the kernel to identify an asynchronous
 * request being processed by a provider. It is passed by the kernel
 * to a hardware provider when submitting a request, and must be
 * specified by a provider when calling crypto_op_notification(9F)
 */
typedef void *crypto_req_handle_t;

/* Values for cc_flags field */
#define	CRYPTO_INIT_OPSTATE	0x00000001 /* allocate and init cc_opstate */
#define	CRYPTO_USE_OPSTATE	0x00000002 /* .. start using it as context */

/*
 * The context structure is passed from the kernel to a provider.
 * It contains the information needed to process a multi-part or
 * single part operation. The context structure is not used
 * by atomic operations.
 *
 * Parameters needed to perform a cryptographic operation, such
 * as keys, mechanisms, input and output buffers, are passed
 * as separate arguments to Provider routines.
 */
typedef struct crypto_ctx {
	crypto_provider_handle_t cc_provider;
	crypto_session_id_t	cc_session;
	void			*cc_provider_private;	/* owned by provider */
	void			*cc_framework_private;	/* owned by framework */
	uint32_t		cc_flags;		/* flags */
	void			*cc_opstate;		/* state */
} crypto_ctx_t;

/*
 * Extended provider information.
 */

/*
 * valid values for ei_flags field of extended info structure
 * They match the RSA Security, Inc PKCS#11 tokenInfo flags.
 */
#define	CRYPTO_EXTF_RNG					0x00000001
#define	CRYPTO_EXTF_WRITE_PROTECTED			0x00000002
#define	CRYPTO_EXTF_LOGIN_REQUIRED			0x00000004
#define	CRYPTO_EXTF_USER_PIN_INITIALIZED		0x00000008
#define	CRYPTO_EXTF_CLOCK_ON_TOKEN			0x00000040
#define	CRYPTO_EXTF_PROTECTED_AUTHENTICATION_PATH	0x00000100
#define	CRYPTO_EXTF_DUAL_CRYPTO_OPERATIONS		0x00000200
#define	CRYPTO_EXTF_TOKEN_INITIALIZED			0x00000400
#define	CRYPTO_EXTF_USER_PIN_COUNT_LOW			0x00010000
#define	CRYPTO_EXTF_USER_PIN_FINAL_TRY			0x00020000
#define	CRYPTO_EXTF_USER_PIN_LOCKED			0x00040000
#define	CRYPTO_EXTF_USER_PIN_TO_BE_CHANGED		0x00080000
#define	CRYPTO_EXTF_SO_PIN_COUNT_LOW			0x00100000
#define	CRYPTO_EXTF_SO_PIN_FINAL_TRY			0x00200000
#define	CRYPTO_EXTF_SO_PIN_LOCKED			0x00400000
#define	CRYPTO_EXTF_SO_PIN_TO_BE_CHANGED		0x00800000

/*
 * The crypto_control_ops structure contains pointers to control
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_control_ops {
	void (*provider_status)(crypto_provider_handle_t, uint_t *);
} __no_const crypto_control_ops_t;

/*
 * The crypto_ctx_ops structure contains points to context and context
 * templates management operations for cryptographic providers. It is
 * passed through the crypto_ops(9S) structure when providers register
 * with the kernel using crypto_register_provider(9F).
 */
typedef struct crypto_ctx_ops {
	int (*create_ctx_template)(crypto_provider_handle_t,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t *, size_t *, crypto_req_handle_t);
	int (*free_context)(crypto_ctx_t *);
} __no_const crypto_ctx_ops_t;

/*
 * The crypto_digest_ops structure contains pointers to digest
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_digest_ops {
	int (*digest_init)(crypto_ctx_t *, crypto_mechanism_t *,
	    crypto_req_handle_t);
	int (*digest)(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
	    crypto_req_handle_t);
	int (*digest_update)(crypto_ctx_t *, crypto_data_t *,
	    crypto_req_handle_t);
	int (*digest_key)(crypto_ctx_t *, crypto_key_t *, crypto_req_handle_t);
	int (*digest_final)(crypto_ctx_t *, crypto_data_t *,
	    crypto_req_handle_t);
	int (*digest_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_data_t *,
	    crypto_data_t *, crypto_req_handle_t);
} __no_const crypto_digest_ops_t;

/*
 * The crypto_cipher_ops structure contains pointers to encryption
 * and decryption operations for cryptographic providers.  It is
 * passed through the crypto_ops(9S) structure when providers register
 * with the kernel using crypto_register_provider(9F).
 */
typedef struct crypto_cipher_ops {
	int (*encrypt_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
	int (*encrypt)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*encrypt_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*encrypt_final)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*encrypt_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

	int (*decrypt_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
	int (*decrypt)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*decrypt_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*decrypt_final)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*decrypt_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
} __no_const crypto_cipher_ops_t;

/*
 * The crypto_mac_ops structure contains pointers to MAC
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_mac_ops {
	int (*mac_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
	int (*mac)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*mac_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*mac_final)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*mac_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*mac_verify_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
} __no_const crypto_mac_ops_t;

/*
 * The crypto_sign_ops structure contains pointers to signing
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_sign_ops {
	int (*sign_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*sign)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*sign_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*sign_final)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*sign_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*sign_recover_init)(crypto_ctx_t *, crypto_mechanism_t *,
	    crypto_key_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*sign_recover)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*sign_recover_atomic)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
} __no_const crypto_sign_ops_t;

/*
 * The crypto_verify_ops structure contains pointers to verify
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_verify_ops {
	int (*verify_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*do_verify)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*verify_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*verify_final)(crypto_ctx_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*verify_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*verify_recover_init)(crypto_ctx_t *, crypto_mechanism_t *,
	    crypto_key_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
	int (*verify_recover)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*verify_recover_atomic)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_req_handle_t);
} __no_const crypto_verify_ops_t;

/*
 * The crypto_dual_ops structure contains pointers to dual
 * cipher and sign/verify operations for cryptographic providers.
 * It is passed through the crypto_ops(9S) structure when
 * providers register with the kernel using
 * crypto_register_provider(9F).
 */
typedef struct crypto_dual_ops {
	int (*digest_encrypt_update)(
	    crypto_ctx_t *, crypto_ctx_t *, crypto_data_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*decrypt_digest_update)(
	    crypto_ctx_t *, crypto_ctx_t *, crypto_data_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*sign_encrypt_update)(
	    crypto_ctx_t *, crypto_ctx_t *, crypto_data_t *,
	    crypto_data_t *, crypto_req_handle_t);
	int (*decrypt_verify_update)(
	    crypto_ctx_t *, crypto_ctx_t *, crypto_data_t *,
	    crypto_data_t *, crypto_req_handle_t);
} __no_const crypto_dual_ops_t;

/*
 * The crypto_dual_cipher_mac_ops structure contains pointers to dual
 * cipher and MAC operations for cryptographic providers.
 * It is passed through the crypto_ops(9S) structure when
 * providers register with the kernel using
 * crypto_register_provider(9F).
 */
typedef struct crypto_dual_cipher_mac_ops {
	int (*encrypt_mac_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *, crypto_mechanism_t *,
	    crypto_key_t *, crypto_spi_ctx_template_t,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
	int (*encrypt_mac)(crypto_ctx_t *,
	    crypto_data_t *, crypto_dual_data_t *, crypto_data_t *,
	    crypto_req_handle_t);
	int (*encrypt_mac_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_dual_data_t *, crypto_req_handle_t);
	int (*encrypt_mac_final)(crypto_ctx_t *,
	    crypto_dual_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*encrypt_mac_atomic)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_mechanism_t *,
	    crypto_key_t *, crypto_data_t *, crypto_dual_data_t *,
	    crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);

	int (*mac_decrypt_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *, crypto_mechanism_t *,
	    crypto_key_t *, crypto_spi_ctx_template_t,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
	int (*mac_decrypt)(crypto_ctx_t *,
	    crypto_dual_data_t *, crypto_data_t *, crypto_data_t *,
	    crypto_req_handle_t);
	int (*mac_decrypt_update)(crypto_ctx_t *,
	    crypto_dual_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*mac_decrypt_final)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *, crypto_req_handle_t);
	int (*mac_decrypt_atomic)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
	    crypto_mechanism_t *, crypto_key_t *, crypto_dual_data_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
	int (*mac_verify_decrypt_atomic)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
	    crypto_mechanism_t *, crypto_key_t *, crypto_dual_data_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t,
	    crypto_spi_ctx_template_t, crypto_req_handle_t);
} __no_const crypto_dual_cipher_mac_ops_t;

/*
 * The crypto_random_number_ops structure contains pointers to random
 * number operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_random_number_ops {
	int (*seed_random)(crypto_provider_handle_t, crypto_session_id_t,
	    uchar_t *, size_t, uint_t, uint32_t, crypto_req_handle_t);
	int (*generate_random)(crypto_provider_handle_t, crypto_session_id_t,
	    uchar_t *, size_t, crypto_req_handle_t);
} __no_const crypto_random_number_ops_t;

/*
 * Flag values for seed_random.
 */
#define	CRYPTO_SEED_NOW		0x00000001

/*
 * The crypto_session_ops structure contains pointers to session
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_session_ops {
	int (*session_open)(crypto_provider_handle_t, crypto_session_id_t *,
	    crypto_req_handle_t);
	int (*session_close)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_req_handle_t);
	int (*session_login)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_user_type_t, char *, size_t, crypto_req_handle_t);
	int (*session_logout)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_req_handle_t);
} __no_const crypto_session_ops_t;

/*
 * The crypto_object_ops structure contains pointers to object
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_object_ops {
	int (*object_create)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_object_attribute_t *, uint_t, crypto_object_id_t *,
	    crypto_req_handle_t);
	int (*object_copy)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_object_id_t, crypto_object_attribute_t *, uint_t,
	    crypto_object_id_t *, crypto_req_handle_t);
	int (*object_destroy)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_object_id_t, crypto_req_handle_t);
	int (*object_get_size)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_object_id_t, size_t *, crypto_req_handle_t);
	int (*object_get_attribute_value)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_object_id_t,
	    crypto_object_attribute_t *, uint_t, crypto_req_handle_t);
	int (*object_set_attribute_value)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_object_id_t,
	    crypto_object_attribute_t *,  uint_t, crypto_req_handle_t);
	int (*object_find_init)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_object_attribute_t *, uint_t, void **,
	    crypto_req_handle_t);
	int (*object_find)(crypto_provider_handle_t, void *,
	    crypto_object_id_t *, uint_t, uint_t *, crypto_req_handle_t);
	int (*object_find_final)(crypto_provider_handle_t, void *,
	    crypto_req_handle_t);
} __no_const crypto_object_ops_t;

/*
 * The crypto_key_ops structure contains pointers to key
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_key_ops {
	int (*key_generate)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_object_attribute_t *, uint_t,
	    crypto_object_id_t *, crypto_req_handle_t);
	int (*key_generate_pair)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_object_attribute_t *, uint_t,
	    crypto_object_attribute_t *, uint_t, crypto_object_id_t *,
	    crypto_object_id_t *, crypto_req_handle_t);
	int (*key_wrap)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_object_id_t *,
	    uchar_t *, size_t *, crypto_req_handle_t);
	int (*key_unwrap)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, uchar_t *, size_t *,
	    crypto_object_attribute_t *, uint_t,
	    crypto_object_id_t *, crypto_req_handle_t);
	int (*key_derive)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_object_attribute_t *,
	    uint_t, crypto_object_id_t *, crypto_req_handle_t);
	int (*key_check)(crypto_provider_handle_t, crypto_mechanism_t *,
	    crypto_key_t *);
} __no_const crypto_key_ops_t;

/*
 * The crypto_provider_management_ops structure contains pointers
 * to management operations for cryptographic providers.  It is passed
 * through the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_provider_management_ops {
	int (*ext_info)(crypto_provider_handle_t,
	    crypto_provider_ext_info_t *, crypto_req_handle_t);
	int (*init_token)(crypto_provider_handle_t, char *, size_t,
	    char *, crypto_req_handle_t);
	int (*init_pin)(crypto_provider_handle_t, crypto_session_id_t,
	    char *, size_t, crypto_req_handle_t);
	int (*set_pin)(crypto_provider_handle_t, crypto_session_id_t,
	    char *, size_t, char *, size_t, crypto_req_handle_t);
} __no_const crypto_provider_management_ops_t;

typedef struct crypto_mech_ops {
	int (*copyin_mechanism)(crypto_provider_handle_t,
	    crypto_mechanism_t *, crypto_mechanism_t *, int *, int);
	int (*copyout_mechanism)(crypto_provider_handle_t,
	    crypto_mechanism_t *, crypto_mechanism_t *, int *, int);
	int (*free_mechanism)(crypto_provider_handle_t, crypto_mechanism_t *);
} __no_const crypto_mech_ops_t;

typedef struct crypto_nostore_key_ops {
	int (*nostore_key_generate)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_mechanism_t *,
	    crypto_object_attribute_t *, uint_t, crypto_object_attribute_t *,
	    uint_t, crypto_req_handle_t);
	int (*nostore_key_generate_pair)(crypto_provider_handle_t,
	    crypto_session_id_t, crypto_mechanism_t *,
	    crypto_object_attribute_t *, uint_t, crypto_object_attribute_t *,
	    uint_t, crypto_object_attribute_t *, uint_t,
	    crypto_object_attribute_t *, uint_t, crypto_req_handle_t);
	int (*nostore_key_derive)(crypto_provider_handle_t, crypto_session_id_t,
	    crypto_mechanism_t *, crypto_key_t *, crypto_object_attribute_t *,
	    uint_t, crypto_object_attribute_t *, uint_t, crypto_req_handle_t);
} __no_const crypto_nostore_key_ops_t;

/*
 * The crypto_ops(9S) structure contains the structures containing
 * the pointers to functions implemented by cryptographic providers.
 * It is specified as part of the crypto_provider_info(9S)
 * supplied by a provider when it registers with the kernel
 * by calling crypto_register_provider(9F).
 */
typedef struct crypto_ops_v1 {
	crypto_control_ops_t			*co_control_ops;
	crypto_digest_ops_t			*co_digest_ops;
	crypto_cipher_ops_t			*co_cipher_ops;
	crypto_mac_ops_t			*co_mac_ops;
	crypto_sign_ops_t			*co_sign_ops;
	crypto_verify_ops_t			*co_verify_ops;
	crypto_dual_ops_t			*co_dual_ops;
	crypto_dual_cipher_mac_ops_t		*co_dual_cipher_mac_ops;
	crypto_random_number_ops_t		*co_random_ops;
	crypto_session_ops_t			*co_session_ops;
	crypto_object_ops_t			*co_object_ops;
	crypto_key_ops_t			*co_key_ops;
	crypto_provider_management_ops_t	*co_provider_ops;
	crypto_ctx_ops_t			*co_ctx_ops;
} crypto_ops_v1_t;

typedef struct crypto_ops_v2 {
	crypto_ops_v1_t				v1_ops;
	crypto_mech_ops_t			*co_mech_ops;
} crypto_ops_v2_t;

typedef struct crypto_ops_v3 {
	crypto_ops_v2_t				v2_ops;
	crypto_nostore_key_ops_t		*co_nostore_key_ops;
} crypto_ops_v3_t;

typedef struct crypto_ops {
	union {
		crypto_ops_v3_t	cou_v3;
		crypto_ops_v2_t	cou_v2;
		crypto_ops_v1_t	cou_v1;
	} cou;
} crypto_ops_t;

#define	co_control_ops			cou.cou_v1.co_control_ops
#define	co_digest_ops			cou.cou_v1.co_digest_ops
#define	co_cipher_ops			cou.cou_v1.co_cipher_ops
#define	co_mac_ops			cou.cou_v1.co_mac_ops
#define	co_sign_ops			cou.cou_v1.co_sign_ops
#define	co_verify_ops			cou.cou_v1.co_verify_ops
#define	co_dual_ops			cou.cou_v1.co_dual_ops
#define	co_dual_cipher_mac_ops		cou.cou_v1.co_dual_cipher_mac_ops
#define	co_random_ops			cou.cou_v1.co_random_ops
#define	co_session_ops			cou.cou_v1.co_session_ops
#define	co_object_ops			cou.cou_v1.co_object_ops
#define	co_key_ops			cou.cou_v1.co_key_ops
#define	co_provider_ops			cou.cou_v1.co_provider_ops
#define	co_ctx_ops			cou.cou_v1.co_ctx_ops
#define	co_mech_ops			cou.cou_v2.co_mech_ops
#define	co_nostore_key_ops		cou.cou_v3.co_nostore_key_ops

/*
 * The mechanism info structure crypto_mech_info_t contains a function group
 * bit mask cm_func_group_mask. This field, of type crypto_func_group_t,
 * specifies the provider entry point that can be used a particular
 * mechanism. The function group mask is a combination of the following values.
 */

typedef uint32_t crypto_func_group_t;


#define	CRYPTO_FG_ENCRYPT		0x00000001 /* encrypt_init() */
#define	CRYPTO_FG_DECRYPT		0x00000002 /* decrypt_init() */
#define	CRYPTO_FG_DIGEST		0x00000004 /* digest_init() */
#define	CRYPTO_FG_SIGN			0x00000008 /* sign_init() */
#define	CRYPTO_FG_SIGN_RECOVER		0x00000010 /* sign_recover_init() */
#define	CRYPTO_FG_VERIFY		0x00000020 /* verify_init() */
#define	CRYPTO_FG_VERIFY_RECOVER	0x00000040 /* verify_recover_init() */
#define	CRYPTO_FG_GENERATE		0x00000080 /* key_generate() */
#define	CRYPTO_FG_GENERATE_KEY_PAIR	0x00000100 /* key_generate_pair() */
#define	CRYPTO_FG_WRAP			0x00000200 /* key_wrap() */
#define	CRYPTO_FG_UNWRAP		0x00000400 /* key_unwrap() */
#define	CRYPTO_FG_DERIVE		0x00000800 /* key_derive() */
#define	CRYPTO_FG_MAC			0x00001000 /* mac_init() */
#define	CRYPTO_FG_ENCRYPT_MAC		0x00002000 /* encrypt_mac_init() */
#define	CRYPTO_FG_MAC_DECRYPT		0x00004000 /* decrypt_mac_init() */
#define	CRYPTO_FG_ENCRYPT_ATOMIC	0x00008000 /* encrypt_atomic() */
#define	CRYPTO_FG_DECRYPT_ATOMIC	0x00010000 /* decrypt_atomic() */
#define	CRYPTO_FG_MAC_ATOMIC		0x00020000 /* mac_atomic() */
#define	CRYPTO_FG_DIGEST_ATOMIC		0x00040000 /* digest_atomic() */
#define	CRYPTO_FG_SIGN_ATOMIC		0x00080000 /* sign_atomic() */
#define	CRYPTO_FG_SIGN_RECOVER_ATOMIC   0x00100000 /* sign_recover_atomic() */
#define	CRYPTO_FG_VERIFY_ATOMIC		0x00200000 /* verify_atomic() */
#define	CRYPTO_FG_VERIFY_RECOVER_ATOMIC	0x00400000 /* verify_recover_atomic() */
#define	CRYPTO_FG_ENCRYPT_MAC_ATOMIC	0x00800000 /* encrypt_mac_atomic() */
#define	CRYPTO_FG_MAC_DECRYPT_ATOMIC	0x01000000 /* mac_decrypt_atomic() */
#define	CRYPTO_FG_RESERVED		0x80000000

/*
 * Maximum length of the pi_provider_description field of the
 * crypto_provider_info structure.
 */
#define	CRYPTO_PROVIDER_DESCR_MAX_LEN	64


/* Bit mask for all the simple operations */
#define	CRYPTO_FG_SIMPLEOP_MASK	(CRYPTO_FG_ENCRYPT | CRYPTO_FG_DECRYPT | \
    CRYPTO_FG_DIGEST | CRYPTO_FG_SIGN | CRYPTO_FG_VERIFY | CRYPTO_FG_MAC | \
    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC |		\
    CRYPTO_FG_MAC_ATOMIC | CRYPTO_FG_DIGEST_ATOMIC | CRYPTO_FG_SIGN_ATOMIC | \
    CRYPTO_FG_VERIFY_ATOMIC)

/* Bit mask for all the dual operations */
#define	CRYPTO_FG_MAC_CIPHER_MASK	(CRYPTO_FG_ENCRYPT_MAC |	\
    CRYPTO_FG_MAC_DECRYPT | CRYPTO_FG_ENCRYPT_MAC_ATOMIC | 		\
    CRYPTO_FG_MAC_DECRYPT_ATOMIC)

/* Add other combos to CRYPTO_FG_DUAL_MASK */
#define	CRYPTO_FG_DUAL_MASK	CRYPTO_FG_MAC_CIPHER_MASK

/*
 * The crypto_mech_info structure specifies one of the mechanisms
 * supported by a cryptographic provider. The pi_mechanisms field of
 * the crypto_provider_info structure contains a pointer to an array
 * of crypto_mech_info's.
 */
typedef struct crypto_mech_info {
	crypto_mech_name_t	cm_mech_name;
	crypto_mech_type_t	cm_mech_number;
	crypto_func_group_t	cm_func_group_mask;
	ssize_t			cm_min_key_length;
	ssize_t			cm_max_key_length;
	uint32_t		cm_mech_flags;
} crypto_mech_info_t;

/* Alias the old name to the new name for compatibility. */
#define	cm_keysize_unit	cm_mech_flags

/*
 * The following is used by a provider that sets
 * CRYPTO_HASH_NO_UPDATE. It needs to specify the maximum
 * input data size it can digest in this field.
 */
#define	cm_max_input_length	cm_max_key_length

/*
 * crypto_kcf_provider_handle_t is a handle allocated by the kernel.
 * It is returned after the provider registers with
 * crypto_register_provider(), and must be specified by the provider
 * when calling crypto_unregister_provider(), and
 * crypto_provider_notification().
 */
typedef uint_t crypto_kcf_provider_handle_t;

/*
 * Provider information. Passed as argument to crypto_register_provider(9F).
 * Describes the provider and its capabilities. Multiple providers can
 * register for the same device instance. In this case, the same
 * pi_provider_dev must be specified with a different pi_provider_handle.
 */
typedef struct crypto_provider_info_v1 {
	uint_t				pi_interface_version;
	char				*pi_provider_description;
	crypto_provider_type_t		pi_provider_type;
	crypto_provider_handle_t	pi_provider_handle;
	crypto_ops_t			*pi_ops_vector;
	uint_t				pi_mech_list_count;
	crypto_mech_info_t		*pi_mechanisms;
	uint_t				pi_logical_provider_count;
	crypto_kcf_provider_handle_t	*pi_logical_providers;
} crypto_provider_info_v1_t;

typedef struct crypto_provider_info_v2 {
	crypto_provider_info_v1_t	v1_info;
	uint_t				pi_flags;
} crypto_provider_info_v2_t;

typedef struct crypto_provider_info {
	union {
		crypto_provider_info_v2_t piu_v2;
		crypto_provider_info_v1_t piu_v1;
	} piu;
} crypto_provider_info_t;

#define	pi_interface_version		piu.piu_v1.pi_interface_version
#define	pi_provider_description		piu.piu_v1.pi_provider_description
#define	pi_provider_type		piu.piu_v1.pi_provider_type
#define	pi_provider_handle		piu.piu_v1.pi_provider_handle
#define	pi_ops_vector			piu.piu_v1.pi_ops_vector
#define	pi_mech_list_count		piu.piu_v1.pi_mech_list_count
#define	pi_mechanisms			piu.piu_v1.pi_mechanisms
#define	pi_logical_provider_count	piu.piu_v1.pi_logical_provider_count
#define	pi_logical_providers		piu.piu_v1.pi_logical_providers
#define	pi_flags			piu.piu_v2.pi_flags

/* hidden providers can only be accessed via a logical provider */
#define	CRYPTO_HIDE_PROVIDER		0x00000001
/*
 * provider can not do multi-part digest (updates) and has a limit
 * on maximum input data that it can digest.
 */
#define	CRYPTO_HASH_NO_UPDATE		0x00000002

/* provider can handle the request without returning a CRYPTO_QUEUED */
#define	CRYPTO_SYNCHRONOUS		0x00000004

#define	CRYPTO_PIFLAGS_RESERVED2	0x40000000
#define	CRYPTO_PIFLAGS_RESERVED1	0x80000000

/*
 * Provider status passed by a provider to crypto_provider_notification(9F)
 * and returned by the provider_stauts(9E) entry point.
 */
#define	CRYPTO_PROVIDER_READY		0
#define	CRYPTO_PROVIDER_BUSY		1
#define	CRYPTO_PROVIDER_FAILED		2

/*
 * Functions exported by Solaris to cryptographic providers. Providers
 * call these functions to register and unregister, notify the kernel
 * of state changes, and notify the kernel when a asynchronous request
 * completed.
 */
extern int crypto_register_provider(crypto_provider_info_t *,
		crypto_kcf_provider_handle_t *);
extern int crypto_unregister_provider(crypto_kcf_provider_handle_t);
extern void crypto_provider_notification(crypto_kcf_provider_handle_t, uint_t);
extern void crypto_op_notification(crypto_req_handle_t, int);
extern int crypto_kmflag(crypto_req_handle_t);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_SPI_H */
