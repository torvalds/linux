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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2013 Saso Kiselkov.  All rights reserved.
 */

#ifndef _SYS_CRYPTO_COMMON_H
#define	_SYS_CRYPTO_COMMON_H

/*
 * Header file for the common data structures of the cryptographic framework
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>

/* Cryptographic Mechanisms */

#define	CRYPTO_MAX_MECH_NAME 32
typedef char crypto_mech_name_t[CRYPTO_MAX_MECH_NAME];

typedef uint64_t crypto_mech_type_t;

typedef struct crypto_mechanism {
	crypto_mech_type_t	cm_type;	/* mechanism type */
	caddr_t			cm_param;	/* mech. parameter */
	size_t			cm_param_len;	/* mech. parameter len */
} crypto_mechanism_t;

#ifdef  _SYSCALL32

typedef struct crypto_mechanism32 {
	crypto_mech_type_t	cm_type;	/* mechanism type */
	caddr32_t		cm_param;	/* mech. parameter */
	size32_t		cm_param_len;   /* mech. parameter len */
} crypto_mechanism32_t;

#endif  /* _SYSCALL32 */

/* CK_AES_CTR_PARAMS provides parameters to the CKM_AES_CTR mechanism */
typedef struct CK_AES_CTR_PARAMS {
	ulong_t	ulCounterBits;
	uint8_t cb[16];
} CK_AES_CTR_PARAMS;

/* CK_AES_CCM_PARAMS provides parameters to the CKM_AES_CCM mechanism */
typedef struct CK_AES_CCM_PARAMS {
	ulong_t ulMACSize;
	ulong_t ulNonceSize;
	ulong_t ulAuthDataSize;
	ulong_t ulDataSize; /* used for plaintext or ciphertext */
	uchar_t *nonce;
	uchar_t *authData;
} CK_AES_CCM_PARAMS;

/* CK_AES_GCM_PARAMS provides parameters to the CKM_AES_GCM mechanism */
typedef struct CK_AES_GCM_PARAMS {
	uchar_t *pIv;
	ulong_t ulIvLen;
	ulong_t ulIvBits;
	uchar_t *pAAD;
	ulong_t ulAADLen;
	ulong_t ulTagBits;
} CK_AES_GCM_PARAMS;

/* CK_AES_GMAC_PARAMS provides parameters to the CKM_AES_GMAC mechanism */
typedef struct CK_AES_GMAC_PARAMS {
	uchar_t *pIv;
	uchar_t *pAAD;
	ulong_t ulAADLen;
} CK_AES_GMAC_PARAMS;

/*
 * CK_ECDH1_DERIVE_PARAMS provides the parameters to the
 * CKM_ECDH1_KEY_DERIVE mechanism
 */
typedef struct CK_ECDH1_DERIVE_PARAMS {
	ulong_t		kdf;
	ulong_t		ulSharedDataLen;
	uchar_t		*pSharedData;
	ulong_t		ulPublicDataLen;
	uchar_t		*pPublicData;
} CK_ECDH1_DERIVE_PARAMS;

#ifdef  _SYSCALL32

/* needed for 32-bit applications running on 64-bit kernels */
typedef struct CK_AES_CTR_PARAMS32 {
	uint32_t ulCounterBits;
	uint8_t cb[16];
} CK_AES_CTR_PARAMS32;

/* needed for 32-bit applications running on 64-bit kernels */
typedef struct CK_AES_CCM_PARAMS32 {
	uint32_t ulMACSize;
	uint32_t ulNonceSize;
	uint32_t ulAuthDataSize;
	uint32_t ulDataSize;
	caddr32_t nonce;
	caddr32_t authData;
} CK_AES_CCM_PARAMS32;

/* needed for 32-bit applications running on 64-bit kernels */
typedef struct CK_AES_GCM_PARAMS32 {
	caddr32_t pIv;
	uint32_t ulIvLen;
	uint32_t ulIvBits;
	caddr32_t pAAD;
	uint32_t ulAADLen;
	uint32_t ulTagBits;
} CK_AES_GCM_PARAMS32;

/* needed for 32-bit applications running on 64-bit kernels */
typedef struct CK_AES_GMAC_PARAMS32 {
	caddr32_t pIv;
	caddr32_t pAAD;
	uint32_t ulAADLen;
} CK_AES_GMAC_PARAMS32;

typedef struct CK_ECDH1_DERIVE_PARAMS32 {
	uint32_t	kdf;
	uint32_t	ulSharedDataLen;
	caddr32_t	pSharedData;
	uint32_t	ulPublicDataLen;
	caddr32_t	pPublicData;
} CK_ECDH1_DERIVE_PARAMS32;

#endif  /* _SYSCALL32 */

/*
 * The measurement unit bit flag for a mechanism's minimum or maximum key size.
 * The unit are mechanism dependent.  It can be in bits or in bytes.
 */
typedef uint32_t crypto_keysize_unit_t;

/*
 * The following bit flags are valid in cm_mech_flags field in
 * the crypto_mech_info_t structure of the SPI.
 *
 * Only the first two bit flags are valid in mi_keysize_unit
 * field in the crypto_mechanism_info_t structure of the API.
 */
#define	CRYPTO_KEYSIZE_UNIT_IN_BITS	0x00000001
#define	CRYPTO_KEYSIZE_UNIT_IN_BYTES	0x00000002
#define	CRYPTO_CAN_SHARE_OPSTATE	0x00000004 /* supports sharing */


/* Mechanisms supported out-of-the-box */
#define	SUN_CKM_MD4			"CKM_MD4"
#define	SUN_CKM_MD5			"CKM_MD5"
#define	SUN_CKM_MD5_HMAC		"CKM_MD5_HMAC"
#define	SUN_CKM_MD5_HMAC_GENERAL	"CKM_MD5_HMAC_GENERAL"
#define	SUN_CKM_SHA1			"CKM_SHA_1"
#define	SUN_CKM_SHA1_HMAC		"CKM_SHA_1_HMAC"
#define	SUN_CKM_SHA1_HMAC_GENERAL	"CKM_SHA_1_HMAC_GENERAL"
#define	SUN_CKM_SHA256			"CKM_SHA256"
#define	SUN_CKM_SHA256_HMAC		"CKM_SHA256_HMAC"
#define	SUN_CKM_SHA256_HMAC_GENERAL	"CKM_SHA256_HMAC_GENERAL"
#define	SUN_CKM_SHA384			"CKM_SHA384"
#define	SUN_CKM_SHA384_HMAC		"CKM_SHA384_HMAC"
#define	SUN_CKM_SHA384_HMAC_GENERAL	"CKM_SHA384_HMAC_GENERAL"
#define	SUN_CKM_SHA512			"CKM_SHA512"
#define	SUN_CKM_SHA512_HMAC		"CKM_SHA512_HMAC"
#define	SUN_CKM_SHA512_HMAC_GENERAL	"CKM_SHA512_HMAC_GENERAL"
#define	SUN_CKM_SHA512_224		"CKM_SHA512_224"
#define	SUN_CKM_SHA512_256		"CKM_SHA512_256"
#define	SUN_CKM_DES_CBC			"CKM_DES_CBC"
#define	SUN_CKM_DES3_CBC		"CKM_DES3_CBC"
#define	SUN_CKM_DES_ECB			"CKM_DES_ECB"
#define	SUN_CKM_DES3_ECB		"CKM_DES3_ECB"
#define	SUN_CKM_BLOWFISH_CBC		"CKM_BLOWFISH_CBC"
#define	SUN_CKM_BLOWFISH_ECB		"CKM_BLOWFISH_ECB"
#define	SUN_CKM_AES_CBC			"CKM_AES_CBC"
#define	SUN_CKM_AES_ECB			"CKM_AES_ECB"
#define	SUN_CKM_AES_CTR			"CKM_AES_CTR"
#define	SUN_CKM_AES_CCM			"CKM_AES_CCM"
#define	SUN_CKM_AES_GCM			"CKM_AES_GCM"
#define	SUN_CKM_AES_GMAC		"CKM_AES_GMAC"
#define	SUN_CKM_AES_CFB128		"CKM_AES_CFB128"
#define	SUN_CKM_RC4			"CKM_RC4"
#define	SUN_CKM_RSA_PKCS		"CKM_RSA_PKCS"
#define	SUN_CKM_RSA_X_509		"CKM_RSA_X_509"
#define	SUN_CKM_MD5_RSA_PKCS		"CKM_MD5_RSA_PKCS"
#define	SUN_CKM_SHA1_RSA_PKCS		"CKM_SHA1_RSA_PKCS"
#define	SUN_CKM_SHA256_RSA_PKCS		"CKM_SHA256_RSA_PKCS"
#define	SUN_CKM_SHA384_RSA_PKCS		"CKM_SHA384_RSA_PKCS"
#define	SUN_CKM_SHA512_RSA_PKCS		"CKM_SHA512_RSA_PKCS"
#define	SUN_CKM_EC_KEY_PAIR_GEN		"CKM_EC_KEY_PAIR_GEN"
#define	SUN_CKM_ECDH1_DERIVE		"CKM_ECDH1_DERIVE"
#define	SUN_CKM_ECDSA_SHA1		"CKM_ECDSA_SHA1"
#define	SUN_CKM_ECDSA			"CKM_ECDSA"

/* Shared operation context format for CKM_RC4 */
typedef struct {
#if defined(__amd64)
	uint32_t	i, j;
	uint32_t	arr[256];
	uint32_t	flag;
#else
	uchar_t		arr[256];
	uchar_t		i, j;
#endif /* __amd64 */
	uint64_t	pad;		/* For 64-bit alignment */
} arcfour_state_t;

/* Data arguments of cryptographic operations */

typedef enum crypto_data_format {
	CRYPTO_DATA_RAW = 1,
	CRYPTO_DATA_UIO,
} crypto_data_format_t;

typedef struct crypto_data {
	crypto_data_format_t	cd_format;	/* Format identifier	*/
	off_t			cd_offset;	/* Offset from the beginning */
	size_t			cd_length;	/* # of bytes in use */
	caddr_t			cd_miscdata;	/* ancillary data */
	union {
		/* Raw format */
		iovec_t cdu_raw;		/* Pointer and length	    */

		/* uio scatter-gather format */
		uio_t	*cdu_uio;

	} cdu;	/* Crypto Data Union */
} crypto_data_t;

#define	cd_raw		cdu.cdu_raw
#define	cd_uio		cdu.cdu_uio
#define	cd_mp		cdu.cdu_mp

typedef struct crypto_dual_data {
	crypto_data_t		dd_data;	/* The data */
	off_t			dd_offset2;	/* Used by dual operation */
	size_t			dd_len2;	/* # of bytes to take	*/
} crypto_dual_data_t;

#define	dd_format	dd_data.cd_format
#define	dd_offset1	dd_data.cd_offset
#define	dd_len1		dd_data.cd_length
#define	dd_miscdata	dd_data.cd_miscdata
#define	dd_raw		dd_data.cd_raw
#define	dd_uio		dd_data.cd_uio
#define	dd_mp		dd_data.cd_mp

/* The keys, and their contents */

typedef enum {
	CRYPTO_KEY_RAW = 1,	/* ck_data is a cleartext key */
	CRYPTO_KEY_REFERENCE,	/* ck_obj_id is an opaque reference */
	CRYPTO_KEY_ATTR_LIST	/* ck_attrs is a list of object attributes */
} crypto_key_format_t;

typedef uint64_t crypto_attr_type_t;

/* Attribute types to use for passing a RSA public key or a private key. */
#define	SUN_CKA_MODULUS			0x00000120
#define	SUN_CKA_MODULUS_BITS		0x00000121
#define	SUN_CKA_PUBLIC_EXPONENT		0x00000122
#define	SUN_CKA_PRIVATE_EXPONENT	0x00000123
#define	SUN_CKA_PRIME_1			0x00000124
#define	SUN_CKA_PRIME_2			0x00000125
#define	SUN_CKA_EXPONENT_1		0x00000126
#define	SUN_CKA_EXPONENT_2		0x00000127
#define	SUN_CKA_COEFFICIENT		0x00000128
#define	SUN_CKA_PRIME			0x00000130
#define	SUN_CKA_SUBPRIME		0x00000131
#define	SUN_CKA_BASE			0x00000132

#define	CKK_EC			0x00000003
#define	CKK_GENERIC_SECRET	0x00000010
#define	CKK_RC4			0x00000012
#define	CKK_AES			0x0000001F
#define	CKK_DES			0x00000013
#define	CKK_DES2		0x00000014
#define	CKK_DES3		0x00000015

#define	CKO_PUBLIC_KEY		0x00000002
#define	CKO_PRIVATE_KEY		0x00000003
#define	CKA_CLASS		0x00000000
#define	CKA_VALUE		0x00000011
#define	CKA_KEY_TYPE		0x00000100
#define	CKA_VALUE_LEN		0x00000161
#define	CKA_EC_PARAMS		0x00000180
#define	CKA_EC_POINT		0x00000181

typedef uint32_t	crypto_object_id_t;

typedef struct crypto_object_attribute {
	crypto_attr_type_t	oa_type;	/* attribute type */
	caddr_t			oa_value;	/* attribute value */
	ssize_t			oa_value_len;	/* length of attribute value */
} crypto_object_attribute_t;

typedef struct crypto_key {
	crypto_key_format_t	ck_format;	/* format identifier */
	union {
		/* for CRYPTO_KEY_RAW ck_format */
		struct {
			uint_t	cku_v_length;	/* # of bits in ck_data   */
			void	*cku_v_data;	/* ptr to key value */
		} cku_key_value;

		/* for CRYPTO_KEY_REFERENCE ck_format */
		crypto_object_id_t cku_key_id;	/* reference to object key */

		/* for CRYPTO_KEY_ATTR_LIST ck_format */
		struct {
			uint_t cku_a_count;	/* number of attributes */
			crypto_object_attribute_t *cku_a_oattr;
		} cku_key_attrs;
	} cku_data;				/* Crypto Key union */
} crypto_key_t;

#ifdef  _SYSCALL32

typedef struct crypto_object_attribute32 {
	uint64_t	oa_type;	/* attribute type */
	caddr32_t	oa_value;	/* attribute value */
	ssize32_t	oa_value_len;	/* length of attribute value */
} crypto_object_attribute32_t;

typedef struct crypto_key32 {
	crypto_key_format_t	ck_format;	/* format identifier */
	union {
		/* for CRYPTO_KEY_RAW ck_format */
		struct {
			uint32_t cku_v_length;	/* # of bytes in ck_data */
			caddr32_t cku_v_data;	/* ptr to key value */
		} cku_key_value;

		/* for CRYPTO_KEY_REFERENCE ck_format */
		crypto_object_id_t cku_key_id; /* reference to object key */

		/* for CRYPTO_KEY_ATTR_LIST ck_format */
		struct {
			uint32_t cku_a_count;	/* number of attributes */
			caddr32_t cku_a_oattr;
		} cku_key_attrs;
	} cku_data;				/* Crypto Key union */
} crypto_key32_t;

#endif  /* _SYSCALL32 */

#define	ck_data		cku_data.cku_key_value.cku_v_data
#define	ck_length	cku_data.cku_key_value.cku_v_length
#define	ck_obj_id	cku_data.cku_key_id
#define	ck_count	cku_data.cku_key_attrs.cku_a_count
#define	ck_attrs	cku_data.cku_key_attrs.cku_a_oattr

/*
 * Raw key lengths are expressed in number of bits.
 * The following macro returns the minimum number of
 * bytes that can contain the specified number of bits.
 * Round up without overflowing the integer type.
 */
#define	CRYPTO_BITS2BYTES(n) ((n) == 0 ? 0 : (((n) - 1) >> 3) + 1)
#define	CRYPTO_BYTES2BITS(n) ((n) << 3)

/* Providers */

typedef enum {
	CRYPTO_HW_PROVIDER = 0,
	CRYPTO_SW_PROVIDER,
	CRYPTO_LOGICAL_PROVIDER
} crypto_provider_type_t;

typedef uint32_t 	crypto_provider_id_t;
#define	KCF_PROVID_INVALID	((uint32_t)-1)

typedef struct crypto_provider_entry {
	crypto_provider_id_t	pe_provider_id;
	uint_t			pe_mechanism_count;
} crypto_provider_entry_t;

typedef struct crypto_dev_list_entry {
	char			le_dev_name[MAXNAMELEN];
	uint_t			le_dev_instance;
	uint_t			le_mechanism_count;
} crypto_dev_list_entry_t;

/* User type for authentication ioctls and SPI entry points */

typedef enum crypto_user_type {
	CRYPTO_SO = 0,
	CRYPTO_USER
} crypto_user_type_t;

/* Version for provider management ioctls and SPI entry points */

typedef struct crypto_version {
	uchar_t	cv_major;
	uchar_t	cv_minor;
} crypto_version_t;

/* session data structure opaque to the consumer */
typedef void *crypto_session_t;

/* provider data structure opaque to the consumer */
typedef void *crypto_provider_t;

/* Limits used by both consumers and providers */
#define	CRYPTO_EXT_SIZE_LABEL		32
#define	CRYPTO_EXT_SIZE_MANUF		32
#define	CRYPTO_EXT_SIZE_MODEL		16
#define	CRYPTO_EXT_SIZE_SERIAL		16
#define	CRYPTO_EXT_SIZE_TIME		16

typedef struct crypto_provider_ext_info {
	uchar_t			ei_label[CRYPTO_EXT_SIZE_LABEL];
	uchar_t			ei_manufacturerID[CRYPTO_EXT_SIZE_MANUF];
	uchar_t			ei_model[CRYPTO_EXT_SIZE_MODEL];
	uchar_t			ei_serial_number[CRYPTO_EXT_SIZE_SERIAL];
	ulong_t			ei_flags;
	ulong_t			ei_max_session_count;
	ulong_t			ei_max_pin_len;
	ulong_t			ei_min_pin_len;
	ulong_t			ei_total_public_memory;
	ulong_t			ei_free_public_memory;
	ulong_t			ei_total_private_memory;
	ulong_t			ei_free_private_memory;
	crypto_version_t	ei_hardware_version;
	crypto_version_t	ei_firmware_version;
	uchar_t			ei_time[CRYPTO_EXT_SIZE_TIME];
	int			ei_hash_max_input_len;
	int			ei_hmac_max_input_len;
} crypto_provider_ext_info_t;

typedef uint_t		crypto_session_id_t;

typedef enum cmd_type {
	COPY_FROM_DATA,
	COPY_TO_DATA,
	COMPARE_TO_DATA,
	MD5_DIGEST_DATA,
	SHA1_DIGEST_DATA,
	SHA2_DIGEST_DATA,
	GHASH_DATA
} cmd_type_t;

#define	CRYPTO_DO_UPDATE	0x01
#define	CRYPTO_DO_FINAL		0x02
#define	CRYPTO_DO_MD5		0x04
#define	CRYPTO_DO_SHA1		0x08
#define	CRYPTO_DO_SIGN		0x10
#define	CRYPTO_DO_VERIFY	0x20
#define	CRYPTO_DO_SHA2		0x40

#define	PROVIDER_OWNS_KEY_SCHEDULE	0x00000001

/*
 * Common cryptographic status and error codes.
 */
#define	CRYPTO_SUCCESS				0x00000000
#define	CRYPTO_CANCEL				0x00000001
#define	CRYPTO_HOST_MEMORY			0x00000002
#define	CRYPTO_GENERAL_ERROR			0x00000003
#define	CRYPTO_FAILED				0x00000004
#define	CRYPTO_ARGUMENTS_BAD			0x00000005
#define	CRYPTO_ATTRIBUTE_READ_ONLY		0x00000006
#define	CRYPTO_ATTRIBUTE_SENSITIVE		0x00000007
#define	CRYPTO_ATTRIBUTE_TYPE_INVALID		0x00000008
#define	CRYPTO_ATTRIBUTE_VALUE_INVALID		0x00000009
#define	CRYPTO_CANCELED				0x0000000A
#define	CRYPTO_DATA_INVALID			0x0000000B
#define	CRYPTO_DATA_LEN_RANGE			0x0000000C
#define	CRYPTO_DEVICE_ERROR			0x0000000D
#define	CRYPTO_DEVICE_MEMORY			0x0000000E
#define	CRYPTO_DEVICE_REMOVED			0x0000000F
#define	CRYPTO_ENCRYPTED_DATA_INVALID		0x00000010
#define	CRYPTO_ENCRYPTED_DATA_LEN_RANGE		0x00000011
#define	CRYPTO_KEY_HANDLE_INVALID		0x00000012
#define	CRYPTO_KEY_SIZE_RANGE			0x00000013
#define	CRYPTO_KEY_TYPE_INCONSISTENT		0x00000014
#define	CRYPTO_KEY_NOT_NEEDED			0x00000015
#define	CRYPTO_KEY_CHANGED			0x00000016
#define	CRYPTO_KEY_NEEDED			0x00000017
#define	CRYPTO_KEY_INDIGESTIBLE			0x00000018
#define	CRYPTO_KEY_FUNCTION_NOT_PERMITTED	0x00000019
#define	CRYPTO_KEY_NOT_WRAPPABLE		0x0000001A
#define	CRYPTO_KEY_UNEXTRACTABLE		0x0000001B
#define	CRYPTO_MECHANISM_INVALID		0x0000001C
#define	CRYPTO_MECHANISM_PARAM_INVALID		0x0000001D
#define	CRYPTO_OBJECT_HANDLE_INVALID		0x0000001E
#define	CRYPTO_OPERATION_IS_ACTIVE		0x0000001F
#define	CRYPTO_OPERATION_NOT_INITIALIZED	0x00000020
#define	CRYPTO_PIN_INCORRECT			0x00000021
#define	CRYPTO_PIN_INVALID			0x00000022
#define	CRYPTO_PIN_LEN_RANGE			0x00000023
#define	CRYPTO_PIN_EXPIRED			0x00000024
#define	CRYPTO_PIN_LOCKED			0x00000025
#define	CRYPTO_SESSION_CLOSED			0x00000026
#define	CRYPTO_SESSION_COUNT			0x00000027
#define	CRYPTO_SESSION_HANDLE_INVALID		0x00000028
#define	CRYPTO_SESSION_READ_ONLY		0x00000029
#define	CRYPTO_SESSION_EXISTS			0x0000002A
#define	CRYPTO_SESSION_READ_ONLY_EXISTS		0x0000002B
#define	CRYPTO_SESSION_READ_WRITE_SO_EXISTS	0x0000002C
#define	CRYPTO_SIGNATURE_INVALID		0x0000002D
#define	CRYPTO_SIGNATURE_LEN_RANGE		0x0000002E
#define	CRYPTO_TEMPLATE_INCOMPLETE		0x0000002F
#define	CRYPTO_TEMPLATE_INCONSISTENT		0x00000030
#define	CRYPTO_UNWRAPPING_KEY_HANDLE_INVALID	0x00000031
#define	CRYPTO_UNWRAPPING_KEY_SIZE_RANGE	0x00000032
#define	CRYPTO_UNWRAPPING_KEY_TYPE_INCONSISTENT	0x00000033
#define	CRYPTO_USER_ALREADY_LOGGED_IN		0x00000034
#define	CRYPTO_USER_NOT_LOGGED_IN		0x00000035
#define	CRYPTO_USER_PIN_NOT_INITIALIZED		0x00000036
#define	CRYPTO_USER_TYPE_INVALID		0x00000037
#define	CRYPTO_USER_ANOTHER_ALREADY_LOGGED_IN	0x00000038
#define	CRYPTO_USER_TOO_MANY_TYPES		0x00000039
#define	CRYPTO_WRAPPED_KEY_INVALID		0x0000003A
#define	CRYPTO_WRAPPED_KEY_LEN_RANGE		0x0000003B
#define	CRYPTO_WRAPPING_KEY_HANDLE_INVALID	0x0000003C
#define	CRYPTO_WRAPPING_KEY_SIZE_RANGE		0x0000003D
#define	CRYPTO_WRAPPING_KEY_TYPE_INCONSISTENT	0x0000003E
#define	CRYPTO_RANDOM_SEED_NOT_SUPPORTED	0x0000003F
#define	CRYPTO_RANDOM_NO_RNG			0x00000040
#define	CRYPTO_DOMAIN_PARAMS_INVALID		0x00000041
#define	CRYPTO_BUFFER_TOO_SMALL			0x00000042
#define	CRYPTO_INFORMATION_SENSITIVE		0x00000043
#define	CRYPTO_NOT_SUPPORTED			0x00000044

#define	CRYPTO_QUEUED				0x00000045
#define	CRYPTO_BUFFER_TOO_BIG			0x00000046
#define	CRYPTO_INVALID_CONTEXT			0x00000047
#define	CRYPTO_INVALID_MAC			0x00000048
#define	CRYPTO_MECH_NOT_SUPPORTED		0x00000049
#define	CRYPTO_INCONSISTENT_ATTRIBUTE		0x0000004A
#define	CRYPTO_NO_PERMISSION			0x0000004B
#define	CRYPTO_INVALID_PROVIDER_ID		0x0000004C
#define	CRYPTO_VERSION_MISMATCH			0x0000004D
#define	CRYPTO_BUSY				0x0000004E
#define	CRYPTO_UNKNOWN_PROVIDER			0x0000004F
#define	CRYPTO_MODVERIFICATION_FAILED		0x00000050
#define	CRYPTO_OLD_CTX_TEMPLATE			0x00000051
#define	CRYPTO_WEAK_KEY				0x00000052
#define	CRYPTO_FIPS140_ERROR			0x00000053
/*
 * Don't forget to update CRYPTO_LAST_ERROR and the error_number_table[]
 * in kernelUtil.c when new error code is added.
 */
#define	CRYPTO_LAST_ERROR			0x00000053

/*
 * Special values that can be used to indicate that information is unavailable
 * or that there is not practical limit. These values can be used
 * by fields of the SPI crypto_provider_ext_info(9S) structure.
 * The value of CRYPTO_UNAVAILABLE_INFO should be the same as
 * CK_UNAVAILABLE_INFO in the PKCS#11 spec.
 */
#define	CRYPTO_UNAVAILABLE_INFO		((ulong_t)(-1))
#define	CRYPTO_EFFECTIVELY_INFINITE	0x0

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_COMMON_H */
