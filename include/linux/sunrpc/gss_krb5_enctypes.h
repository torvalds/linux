/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Define the string that exports the set of kernel-supported
 * Kerberos enctypes. This list is sent via upcall to gssd, and
 * is also exposed via the nfsd /proc API. The consumers generally
 * treat this as an ordered list, where the first item in the list
 * is the most preferred.
 */

#ifndef _LINUX_SUNRPC_GSS_KRB5_ENCTYPES_H
#define _LINUX_SUNRPC_GSS_KRB5_ENCTYPES_H

#ifdef CONFIG_SUNRPC_DISABLE_INSECURE_ENCTYPES

/*
 * NB: This list includes encryption types that were deprecated
 * by RFC 8429 (DES3_CBC_SHA1 and ARCFOUR_HMAC).
 *
 * ENCTYPE_AES256_CTS_HMAC_SHA1_96
 * ENCTYPE_AES128_CTS_HMAC_SHA1_96
 * ENCTYPE_DES3_CBC_SHA1
 * ENCTYPE_ARCFOUR_HMAC
 */
#define KRB5_SUPPORTED_ENCTYPES "18,17,16,23"

#else	/* CONFIG_SUNRPC_DISABLE_INSECURE_ENCTYPES */

/*
 * NB: This list includes encryption types that were deprecated
 * by RFC 8429 and RFC 6649.
 *
 * ENCTYPE_AES256_CTS_HMAC_SHA1_96
 * ENCTYPE_AES128_CTS_HMAC_SHA1_96
 * ENCTYPE_DES3_CBC_SHA1
 * ENCTYPE_ARCFOUR_HMAC
 * ENCTYPE_DES_CBC_MD5
 * ENCTYPE_DES_CBC_CRC
 * ENCTYPE_DES_CBC_MD4
 */
#define KRB5_SUPPORTED_ENCTYPES "18,17,16,23,3,1,2"

#endif	/* CONFIG_SUNRPC_DISABLE_INSECURE_ENCTYPES */

#endif	/* _LINUX_SUNRPC_GSS_KRB5_ENCTYPES_H */
