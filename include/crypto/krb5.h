/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Kerberos 5 crypto
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _CRYPTO_KRB5_H
#define _CRYPTO_KRB5_H

/*
 * Per Kerberos v5 protocol spec crypto types from the wire.  These get mapped
 * to linux kernel crypto routines.
 */
#define KRB5_ENCTYPE_NULL			0x0000
#define KRB5_ENCTYPE_DES_CBC_CRC		0x0001	/* DES cbc mode with CRC-32 */
#define KRB5_ENCTYPE_DES_CBC_MD4		0x0002	/* DES cbc mode with RSA-MD4 */
#define KRB5_ENCTYPE_DES_CBC_MD5		0x0003	/* DES cbc mode with RSA-MD5 */
#define KRB5_ENCTYPE_DES_CBC_RAW		0x0004	/* DES cbc mode raw */
/* XXX deprecated? */
#define KRB5_ENCTYPE_DES3_CBC_SHA		0x0005	/* DES-3 cbc mode with NIST-SHA */
#define KRB5_ENCTYPE_DES3_CBC_RAW		0x0006	/* DES-3 cbc mode raw */
#define KRB5_ENCTYPE_DES_HMAC_SHA1		0x0008
#define KRB5_ENCTYPE_DES3_CBC_SHA1		0x0010
#define KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96	0x0011
#define KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96	0x0012
#define KRB5_ENCTYPE_ARCFOUR_HMAC		0x0017
#define KRB5_ENCTYPE_ARCFOUR_HMAC_EXP		0x0018
#define KRB5_ENCTYPE_UNKNOWN			0x01ff

#define KRB5_CKSUMTYPE_CRC32			0x0001
#define KRB5_CKSUMTYPE_RSA_MD4			0x0002
#define KRB5_CKSUMTYPE_RSA_MD4_DES		0x0003
#define KRB5_CKSUMTYPE_DESCBC			0x0004
#define KRB5_CKSUMTYPE_RSA_MD5			0x0007
#define KRB5_CKSUMTYPE_RSA_MD5_DES		0x0008
#define KRB5_CKSUMTYPE_NIST_SHA			0x0009
#define KRB5_CKSUMTYPE_HMAC_SHA1_DES3		0x000c
#define KRB5_CKSUMTYPE_HMAC_SHA1_96_AES128	0x000f
#define KRB5_CKSUMTYPE_HMAC_SHA1_96_AES256	0x0010
#define KRB5_CKSUMTYPE_HMAC_MD5_ARCFOUR		-138 /* Microsoft md5 hmac cksumtype */

/*
 * Constants used for key derivation
 */
/* from rfc3961 */
#define KEY_USAGE_SEED_CHECKSUM         (0x99)
#define KEY_USAGE_SEED_ENCRYPTION       (0xAA)
#define KEY_USAGE_SEED_INTEGRITY        (0x55)

#endif /* _CRYPTO_KRB5_H */
