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

#ifndef	_SHA1_IMPL_H
#define	_SHA1_IMPL_H


#ifdef __cplusplus
extern "C" {
#endif

#define	SHA1_HASH_SIZE		20	/* SHA_1 digest length in bytes */
#define	SHA1_DIGEST_LENGTH	20	/* SHA1 digest length in bytes */
#define	SHA1_HMAC_BLOCK_SIZE	64	/* SHA1-HMAC block size */
#define	SHA1_HMAC_MIN_KEY_LEN	1	/* SHA1-HMAC min key length in bytes */
#define	SHA1_HMAC_MAX_KEY_LEN	INT_MAX /* SHA1-HMAC max key length in bytes */
#define	SHA1_HMAC_INTS_PER_BLOCK	(SHA1_HMAC_BLOCK_SIZE/sizeof (uint32_t))

/*
 * CSPI information (entry points, provider info, etc.)
 */
typedef enum sha1_mech_type {
	SHA1_MECH_INFO_TYPE,		/* SUN_CKM_SHA1 */
	SHA1_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA1_HMAC */
	SHA1_HMAC_GEN_MECH_INFO_TYPE	/* SUN_CKM_SHA1_HMAC_GENERAL */
} sha1_mech_type_t;

/*
 * Context for SHA1 mechanism.
 */
typedef struct sha1_ctx {
	sha1_mech_type_t	sc_mech_type;	/* type of context */
	SHA1_CTX		sc_sha1_ctx;	/* SHA1 context */
} sha1_ctx_t;

/*
 * Context for SHA1-HMAC and SHA1-HMAC-GENERAL mechanisms.
 */
typedef struct sha1_hmac_ctx {
	sha1_mech_type_t	hc_mech_type;	/* type of context */
	uint32_t		hc_digest_len;	/* digest len in bytes */
	SHA1_CTX		hc_icontext;	/* inner SHA1 context */
	SHA1_CTX		hc_ocontext;	/* outer SHA1 context */
} sha1_hmac_ctx_t;


#ifdef	__cplusplus
}
#endif

#endif /* _SHA1_IMPL_H */
