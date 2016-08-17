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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/* Copyright 2013 Saso Kiselkov.  All rights reserved. */

#ifndef _SYS_SHA2_H
#define	_SYS_SHA2_H

#ifdef  _KERNEL
#include <sys/types.h>		/* for uint_* */
#else
#include <stdint.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define	SHA2_HMAC_MIN_KEY_LEN	1	/* SHA2-HMAC min key length in bytes */
#define	SHA2_HMAC_MAX_KEY_LEN	INT_MAX	/* SHA2-HMAC max key length in bytes */

#define	SHA256_DIGEST_LENGTH	32	/* SHA256 digest length in bytes */
#define	SHA384_DIGEST_LENGTH	48	/* SHA384 digest length in bytes */
#define	SHA512_DIGEST_LENGTH	64	/* SHA512 digest length in bytes */

/* Truncated versions of SHA-512 according to FIPS-180-4, section 5.3.6 */
#define	SHA512_224_DIGEST_LENGTH	28	/* SHA512/224 digest length */
#define	SHA512_256_DIGEST_LENGTH	32	/* SHA512/256 digest length */

#define	SHA256_HMAC_BLOCK_SIZE	64	/* SHA256-HMAC block size */
#define	SHA512_HMAC_BLOCK_SIZE	128	/* SHA512-HMAC block size */

#define	SHA256			0
#define	SHA256_HMAC		1
#define	SHA256_HMAC_GEN		2
#define	SHA384			3
#define	SHA384_HMAC		4
#define	SHA384_HMAC_GEN		5
#define	SHA512			6
#define	SHA512_HMAC		7
#define	SHA512_HMAC_GEN		8
#define	SHA512_224		9
#define	SHA512_256		10

/*
 * SHA2 context.
 * The contents of this structure are a private interface between the
 * Init/Update/Final calls of the functions defined below.
 * Callers must never attempt to read or write any of the fields
 * in this structure directly.
 */
typedef struct 	{
	uint32_t algotype;		/* Algorithm Type */

	/* state (ABCDEFGH) */
	union {
		uint32_t s32[8];	/* for SHA256 */
		uint64_t s64[8];	/* for SHA384/512 */
	} state;
	/* number of bits */
	union {
		uint32_t c32[2];	/* for SHA256 , modulo 2^64 */
		uint64_t c64[2];	/* for SHA384/512, modulo 2^128 */
	} count;
	union {
		uint8_t		buf8[128];	/* undigested input */
		uint32_t	buf32[32];	/* realigned input */
		uint64_t	buf64[16];	/* realigned input */
	} buf_un;
} SHA2_CTX;

typedef SHA2_CTX SHA256_CTX;
typedef SHA2_CTX SHA384_CTX;
typedef SHA2_CTX SHA512_CTX;

extern void SHA2Init(uint64_t mech, SHA2_CTX *);

extern void SHA2Update(SHA2_CTX *, const void *, size_t);

extern void SHA2Final(void *, SHA2_CTX *);

extern void SHA256Init(SHA256_CTX *);

extern void SHA256Update(SHA256_CTX *, const void *, size_t);

extern void SHA256Final(void *, SHA256_CTX *);

extern void SHA384Init(SHA384_CTX *);

extern void SHA384Update(SHA384_CTX *, const void *, size_t);

extern void SHA384Final(void *, SHA384_CTX *);

extern void SHA512Init(SHA512_CTX *);

extern void SHA512Update(SHA512_CTX *, const void *, size_t);

extern void SHA512Final(void *, SHA512_CTX *);

#ifdef _SHA2_IMPL
/*
 * The following types/functions are all private to the implementation
 * of the SHA2 functions and must not be used by consumers of the interface
 */

/*
 * List of support mechanisms in this module.
 *
 * It is important to note that in the module, division or modulus calculations
 * are used on the enumerated type to determine which mechanism is being used;
 * therefore, changing the order or additional mechanisms should be done
 * carefully
 */
typedef enum sha2_mech_type {
	SHA256_MECH_INFO_TYPE,		/* SUN_CKM_SHA256 */
	SHA256_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA256_HMAC */
	SHA256_HMAC_GEN_MECH_INFO_TYPE,	/* SUN_CKM_SHA256_HMAC_GENERAL */
	SHA384_MECH_INFO_TYPE,		/* SUN_CKM_SHA384 */
	SHA384_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA384_HMAC */
	SHA384_HMAC_GEN_MECH_INFO_TYPE,	/* SUN_CKM_SHA384_HMAC_GENERAL */
	SHA512_MECH_INFO_TYPE,		/* SUN_CKM_SHA512 */
	SHA512_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA512_HMAC */
	SHA512_HMAC_GEN_MECH_INFO_TYPE,	/* SUN_CKM_SHA512_HMAC_GENERAL */
	SHA512_224_MECH_INFO_TYPE,	/* SUN_CKM_SHA512_224 */
	SHA512_256_MECH_INFO_TYPE	/* SUN_CKM_SHA512_256 */
} sha2_mech_type_t;

#endif /* _SHA2_IMPL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SHA2_H */
