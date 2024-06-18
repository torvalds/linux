/*
 *  Adapted from MIT Kerberos 5-1.2.1 lib/include/krb5.h,
 *  lib/gssapi/krb5/gssapiP_krb5.h, and others
 *
 *  Copyright (c) 2000-2008 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *  Bruce Fields   <bfields@umich.edu>
 */

/*
 * Copyright 1995 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */

#ifndef _LINUX_SUNRPC_GSS_KRB5_H
#define _LINUX_SUNRPC_GSS_KRB5_H

#include <crypto/skcipher.h>
#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/sunrpc/gss_asn1.h>

/* Length of constant used in key derivation */
#define GSS_KRB5_K5CLENGTH (5)

/* Maximum key length (in bytes) for the supported crypto algorithms */
#define GSS_KRB5_MAX_KEYLEN (32)

/* Maximum checksum function output for the supported enctypes */
#define GSS_KRB5_MAX_CKSUM_LEN  (24)

/* Maximum blocksize for the supported crypto algorithms */
#define GSS_KRB5_MAX_BLOCKSIZE  (16)

/* The length of the Kerberos GSS token header */
#define GSS_KRB5_TOK_HDR_LEN	(16)

#define KG_TOK_MIC_MSG    0x0101
#define KG_TOK_WRAP_MSG   0x0201

#define KG2_TOK_INITIAL     0x0101
#define KG2_TOK_RESPONSE    0x0202
#define KG2_TOK_MIC         0x0404
#define KG2_TOK_WRAP        0x0504

#define KG2_TOKEN_FLAG_SENTBYACCEPTOR   0x01
#define KG2_TOKEN_FLAG_SEALED           0x02
#define KG2_TOKEN_FLAG_ACCEPTORSUBKEY   0x04

#define KG2_RESP_FLAG_ERROR             0x0001
#define KG2_RESP_FLAG_DELEG_OK          0x0002

enum sgn_alg {
	SGN_ALG_DES_MAC_MD5 = 0x0000,
	SGN_ALG_MD2_5 = 0x0001,
	SGN_ALG_DES_MAC = 0x0002,
	SGN_ALG_3 = 0x0003,		/* not published */
	SGN_ALG_HMAC_SHA1_DES3_KD = 0x0004
};
enum seal_alg {
	SEAL_ALG_NONE = 0xffff,
	SEAL_ALG_DES = 0x0000,
	SEAL_ALG_1 = 0x0001,		/* not published */
	SEAL_ALG_DES3KD = 0x0002
};

/*
 * These values are assigned by IANA and published via the
 * subregistry at the link below:
 *
 * https://www.iana.org/assignments/kerberos-parameters/kerberos-parameters.xhtml#kerberos-parameters-2
 */
#define CKSUMTYPE_CRC32			0x0001
#define CKSUMTYPE_RSA_MD4		0x0002
#define CKSUMTYPE_RSA_MD4_DES		0x0003
#define CKSUMTYPE_DESCBC		0x0004
#define CKSUMTYPE_RSA_MD5		0x0007
#define CKSUMTYPE_RSA_MD5_DES		0x0008
#define CKSUMTYPE_NIST_SHA		0x0009
#define CKSUMTYPE_HMAC_SHA1_DES3	0x000c
#define CKSUMTYPE_HMAC_SHA1_96_AES128   0x000f
#define CKSUMTYPE_HMAC_SHA1_96_AES256   0x0010
#define CKSUMTYPE_CMAC_CAMELLIA128	0x0011
#define CKSUMTYPE_CMAC_CAMELLIA256	0x0012
#define CKSUMTYPE_HMAC_SHA256_128_AES128	0x0013
#define CKSUMTYPE_HMAC_SHA384_192_AES256	0x0014
#define CKSUMTYPE_HMAC_MD5_ARCFOUR      -138 /* Microsoft md5 hmac cksumtype */

/* from gssapi_err_krb5.h */
#define KG_CCACHE_NOMATCH                        (39756032L)
#define KG_KEYTAB_NOMATCH                        (39756033L)
#define KG_TGT_MISSING                           (39756034L)
#define KG_NO_SUBKEY                             (39756035L)
#define KG_CONTEXT_ESTABLISHED                   (39756036L)
#define KG_BAD_SIGN_TYPE                         (39756037L)
#define KG_BAD_LENGTH                            (39756038L)
#define KG_CTX_INCOMPLETE                        (39756039L)
#define KG_CONTEXT                               (39756040L)
#define KG_CRED                                  (39756041L)
#define KG_ENC_DESC                              (39756042L)
#define KG_BAD_SEQ                               (39756043L)
#define KG_EMPTY_CCACHE                          (39756044L)
#define KG_NO_CTYPES                             (39756045L)

/* per Kerberos v5 protocol spec crypto types from the wire. 
 * these get mapped to linux kernel crypto routines.  
 *
 * These values are assigned by IANA and published via the
 * subregistry at the link below:
 *
 * https://www.iana.org/assignments/kerberos-parameters/kerberos-parameters.xhtml#kerberos-parameters-1
 */
#define ENCTYPE_NULL            0x0000
#define ENCTYPE_DES_CBC_CRC     0x0001	/* DES cbc mode with CRC-32 */
#define ENCTYPE_DES_CBC_MD4     0x0002	/* DES cbc mode with RSA-MD4 */
#define ENCTYPE_DES_CBC_MD5     0x0003	/* DES cbc mode with RSA-MD5 */
#define ENCTYPE_DES_CBC_RAW     0x0004	/* DES cbc mode raw */
/* XXX deprecated? */
#define ENCTYPE_DES3_CBC_SHA    0x0005	/* DES-3 cbc mode with NIST-SHA */
#define ENCTYPE_DES3_CBC_RAW    0x0006	/* DES-3 cbc mode raw */
#define ENCTYPE_DES_HMAC_SHA1   0x0008
#define ENCTYPE_DES3_CBC_SHA1   0x0010
#define ENCTYPE_AES128_CTS_HMAC_SHA1_96 0x0011
#define ENCTYPE_AES256_CTS_HMAC_SHA1_96 0x0012
#define ENCTYPE_AES128_CTS_HMAC_SHA256_128	0x0013
#define ENCTYPE_AES256_CTS_HMAC_SHA384_192	0x0014
#define ENCTYPE_ARCFOUR_HMAC            0x0017
#define ENCTYPE_ARCFOUR_HMAC_EXP        0x0018
#define ENCTYPE_CAMELLIA128_CTS_CMAC	0x0019
#define ENCTYPE_CAMELLIA256_CTS_CMAC	0x001A
#define ENCTYPE_UNKNOWN         0x01ff

/*
 * Constants used for key derivation
 */
/* for 3DES */
#define KG_USAGE_SEAL (22)
#define KG_USAGE_SIGN (23)
#define KG_USAGE_SEQ  (24)

/* from rfc3961 */
#define KEY_USAGE_SEED_CHECKSUM         (0x99)
#define KEY_USAGE_SEED_ENCRYPTION       (0xAA)
#define KEY_USAGE_SEED_INTEGRITY        (0x55)

/* from rfc4121 */
#define KG_USAGE_ACCEPTOR_SEAL  (22)
#define KG_USAGE_ACCEPTOR_SIGN  (23)
#define KG_USAGE_INITIATOR_SEAL (24)
#define KG_USAGE_INITIATOR_SIGN (25)

#endif /* _LINUX_SUNRPC_GSS_KRB5_H */
