/* $OpenBSD: tls1.h,v 1.61 2025/04/18 07:34:01 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * ECC cipher suite support in OpenSSL originally written by
 * Vipul Gupta and Sumit Gupta of Sun Microsystems Laboratories.
 *
 */
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#ifndef HEADER_TLS1_H
#define HEADER_TLS1_H

#include <openssl/opensslconf.h>

#include <openssl/buffer.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define OPENSSL_TLS_SECURITY_LEVEL 1

#define TLS1_ALLOW_EXPERIMENTAL_CIPHERSUITES	0

#if defined(LIBRESSL_HAS_TLS1_3) || defined(LIBRESSL_INTERNAL)
#define TLS1_3_VERSION			0x0304
#endif

#define TLS1_2_VERSION			0x0303
#define TLS1_2_VERSION_MAJOR		0x03
#define TLS1_2_VERSION_MINOR		0x03

#define TLS1_1_VERSION			0x0302
#define TLS1_1_VERSION_MAJOR		0x03
#define TLS1_1_VERSION_MINOR		0x02

#define TLS1_VERSION			0x0301
#define TLS1_VERSION_MAJOR		0x03
#define TLS1_VERSION_MINOR		0x01

#ifndef LIBRESSL_INTERNAL
#define TLS1_AD_DECRYPTION_FAILED		21
#define TLS1_AD_RECORD_OVERFLOW			22
#define TLS1_AD_UNKNOWN_CA			48	/* fatal */
#define TLS1_AD_ACCESS_DENIED			49	/* fatal */
#define TLS1_AD_DECODE_ERROR			50	/* fatal */
#define TLS1_AD_DECRYPT_ERROR			51
#define TLS1_AD_EXPORT_RESTRICTION		60	/* fatal */
#define TLS1_AD_PROTOCOL_VERSION		70	/* fatal */
#define TLS1_AD_INSUFFICIENT_SECURITY		71	/* fatal */
#define TLS1_AD_INTERNAL_ERROR			80	/* fatal */
/* Code 86 from RFC 7507. */
#define TLS1_AD_INAPPROPRIATE_FALLBACK		86	/* fatal */
#define TLS1_AD_USER_CANCELLED			90
#define TLS1_AD_NO_RENEGOTIATION		100
/* Codes 110-114 from RFC 3546. */
#define TLS1_AD_UNSUPPORTED_EXTENSION		110
#define TLS1_AD_CERTIFICATE_UNOBTAINABLE	111
#define TLS1_AD_UNRECOGNIZED_NAME		112
#define TLS1_AD_BAD_CERTIFICATE_STATUS_RESPONSE	113
#define TLS1_AD_BAD_CERTIFICATE_HASH_VALUE	114
/* Code 115 from RFC 4279. */
#define TLS1_AD_UNKNOWN_PSK_IDENTITY		115	/* fatal */
#endif

/*
 * TLS ExtensionType values.
 *
 * https://www.iana.org/assignments/tls-extensiontype-values/
 */

/* ExtensionType values from RFC 3546, RFC 4366 and RFC 6066. */
#define TLSEXT_TYPE_server_name			0
#define TLSEXT_TYPE_max_fragment_length		1
#define TLSEXT_TYPE_client_certificate_url	2
#define TLSEXT_TYPE_trusted_ca_keys		3
#define TLSEXT_TYPE_truncated_hmac		4
#define TLSEXT_TYPE_status_request		5

/* ExtensionType values from RFC 4681. */
#define TLSEXT_TYPE_user_mapping		6

/* ExtensionType values from RFC 5878. */
#define TLSEXT_TYPE_client_authz		7
#define TLSEXT_TYPE_server_authz		8

/* ExtensionType values from RFC 6091. */
#define TLSEXT_TYPE_cert_type		9

/* ExtensionType values from RFC 7919. */
#define TLSEXT_TYPE_supported_groups		10

/* ExtensionType values from RFC 4492. */
#ifndef LIBRESSL_INTERNAL
#define TLSEXT_TYPE_elliptic_curves		TLSEXT_TYPE_supported_groups
#endif
#define TLSEXT_TYPE_ec_point_formats		11

/* ExtensionType value from RFC 5054. */
#define TLSEXT_TYPE_srp				12

/* ExtensionType value from RFC 5246/RFC 8446. */
#define TLSEXT_TYPE_signature_algorithms	13

/* ExtensionType value from RFC 5764. */
#define TLSEXT_TYPE_use_srtp	14

/* ExtensionType value from RFC 5620. */
#define TLSEXT_TYPE_heartbeat	15

/* ExtensionType value from RFC 7301. */
#define TLSEXT_TYPE_application_layer_protocol_negotiation 16

/* ExtensionType value from RFC 7685. */
#define TLSEXT_TYPE_padding	21

/* ExtensionType value from RFC 4507. */
#define TLSEXT_TYPE_session_ticket		35

/* ExtensionType values from RFC 8446 section 4.2 */
#if defined(LIBRESSL_HAS_TLS1_3) || defined(LIBRESSL_INTERNAL)
#define TLSEXT_TYPE_pre_shared_key		41
#define TLSEXT_TYPE_early_data			42
#define TLSEXT_TYPE_supported_versions		43
#define TLSEXT_TYPE_cookie			44
#define TLSEXT_TYPE_psk_key_exchange_modes	45
#define TLSEXT_TYPE_certificate_authorities	47
#define TLSEXT_TYPE_oid_filters			48
#define TLSEXT_TYPE_post_handshake_auth		49
#define TLSEXT_TYPE_signature_algorithms_cert	50
#define TLSEXT_TYPE_key_share			51
#endif

/* ExtensionType value from RFC 9001 section 8.2 */
#if defined(LIBRESSL_HAS_QUIC) || defined(LIBRESSL_INTERNAL)
#define TLSEXT_TYPE_quic_transport_parameters	57
#endif

/*
 * TLS 1.3 extension names from OpenSSL, where they decided to use a different
 * name from that given in RFC 8446.
 */
#if defined(LIBRESSL_HAS_TLS1_3)
#define TLSEXT_TYPE_psk				TLSEXT_TYPE_pre_shared_key
#define TLSEXT_TYPE_psk_kex_modes		TLSEXT_TYPE_psk_key_exchange_modes
#endif

/* Temporary extension type */
#define TLSEXT_TYPE_renegotiate                 0xff01

/* NameType value from RFC 3546. */
#define TLSEXT_NAMETYPE_host_name 0
/* status request value from RFC 3546 */
#define TLSEXT_STATUSTYPE_ocsp 1

/* ECPointFormat values from RFC 4492. */
#define TLSEXT_ECPOINTFORMAT_first			0
#define TLSEXT_ECPOINTFORMAT_uncompressed		0
#define TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime	1
#define TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2	2
#define TLSEXT_ECPOINTFORMAT_last			2

#define TLSEXT_MAXLEN_host_name 255

const char *SSL_get_servername(const SSL *s, const int type);
int SSL_get_servername_type(const SSL *s);
/* SSL_export_keying_material exports a value derived from the master secret,
 * as specified in RFC 5705. It writes |olen| bytes to |out| given a label and
 * optional context. (Since a zero length context is allowed, the |use_context|
 * flag controls whether a context is included.)
 *
 * It returns 1 on success and zero otherwise.
 */
int SSL_export_keying_material(SSL *s, unsigned char *out, size_t olen,
    const char *label, size_t llen, const unsigned char *p, size_t plen,
    int use_context);

int SSL_get_signature_type_nid(const SSL *ssl, int *nid);
int SSL_get_peer_signature_type_nid(const SSL *ssl, int *nid);

#define SSL_set_tlsext_host_name(s,name) \
SSL_ctrl(s,SSL_CTRL_SET_TLSEXT_HOSTNAME,TLSEXT_NAMETYPE_host_name,(char *)name)

#define SSL_set_tlsext_debug_callback(ssl, cb) \
SSL_callback_ctrl(ssl,SSL_CTRL_SET_TLSEXT_DEBUG_CB,(void (*)(void))cb)

#define SSL_set_tlsext_debug_arg(ssl, arg) \
SSL_ctrl(ssl,SSL_CTRL_SET_TLSEXT_DEBUG_ARG,0, (void *)arg)

#define SSL_get_tlsext_status_type(ssl) \
SSL_ctrl(ssl, SSL_CTRL_GET_TLSEXT_STATUS_REQ_TYPE, 0, NULL)

#define SSL_set_tlsext_status_type(ssl, type) \
SSL_ctrl(ssl,SSL_CTRL_SET_TLSEXT_STATUS_REQ_TYPE,type, NULL)

#define SSL_get_tlsext_status_exts(ssl, arg) \
SSL_ctrl(ssl,SSL_CTRL_GET_TLSEXT_STATUS_REQ_EXTS,0, (void *)arg)

#define SSL_set_tlsext_status_exts(ssl, arg) \
SSL_ctrl(ssl,SSL_CTRL_SET_TLSEXT_STATUS_REQ_EXTS,0, (void *)arg)

#define SSL_get_tlsext_status_ids(ssl, arg) \
SSL_ctrl(ssl,SSL_CTRL_GET_TLSEXT_STATUS_REQ_IDS,0, (void *)arg)

#define SSL_set_tlsext_status_ids(ssl, arg) \
SSL_ctrl(ssl,SSL_CTRL_SET_TLSEXT_STATUS_REQ_IDS,0, (void *)arg)

#define SSL_get_tlsext_status_ocsp_resp(ssl, arg) \
SSL_ctrl(ssl,SSL_CTRL_GET_TLSEXT_STATUS_REQ_OCSP_RESP,0, (void *)arg)

#define SSL_set_tlsext_status_ocsp_resp(ssl, arg, arglen) \
SSL_ctrl(ssl,SSL_CTRL_SET_TLSEXT_STATUS_REQ_OCSP_RESP,arglen, (void *)arg)

#define SSL_CTX_set_tlsext_servername_callback(ctx, cb) \
SSL_CTX_callback_ctrl(ctx,SSL_CTRL_SET_TLSEXT_SERVERNAME_CB,(void (*)(void))cb)

#define SSL_TLSEXT_ERR_OK 0
#define SSL_TLSEXT_ERR_ALERT_WARNING 1
#define SSL_TLSEXT_ERR_ALERT_FATAL 2
#define SSL_TLSEXT_ERR_NOACK 3

#define SSL_CTX_set_tlsext_servername_arg(ctx, arg) \
SSL_CTX_ctrl(ctx,SSL_CTRL_SET_TLSEXT_SERVERNAME_ARG,0, (void *)arg)

#define SSL_CTX_get_tlsext_ticket_keys(ctx, keys, keylen) \
	SSL_CTX_ctrl((ctx),SSL_CTRL_GET_TLSEXT_TICKET_KEYS,(keylen),(keys))
#define SSL_CTX_set_tlsext_ticket_keys(ctx, keys, keylen) \
	SSL_CTX_ctrl((ctx),SSL_CTRL_SET_TLSEXT_TICKET_KEYS,(keylen),(keys))

#define SSL_CTX_get_tlsext_status_cb(ssl, cb) \
SSL_CTX_callback_ctrl(ssl,SSL_CTRL_GET_TLSEXT_STATUS_REQ_CB,(void (*)(void))cb)
#define SSL_CTX_set_tlsext_status_cb(ssl, cb) \
SSL_CTX_callback_ctrl(ssl,SSL_CTRL_SET_TLSEXT_STATUS_REQ_CB,(void (*)(void))cb)

#define SSL_CTX_get_tlsext_status_arg(ssl, arg) \
SSL_CTX_ctrl(ssl,SSL_CTRL_GET_TLSEXT_STATUS_REQ_CB_ARG,0,(void *)arg)
#define SSL_CTX_set_tlsext_status_arg(ssl, arg) \
SSL_CTX_ctrl(ssl,SSL_CTRL_SET_TLSEXT_STATUS_REQ_CB_ARG,0,(void *)arg)

#define SSL_CTX_set_tlsext_ticket_key_cb(ssl, cb) \
SSL_CTX_callback_ctrl(ssl,SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB,(void (*)(void))cb)

/* PSK ciphersuites from RFC 4279. */
#define TLS1_CK_PSK_WITH_RC4_128_SHA                    0x0300008A
#define TLS1_CK_PSK_WITH_3DES_EDE_CBC_SHA               0x0300008B
#define TLS1_CK_PSK_WITH_AES_128_CBC_SHA                0x0300008C
#define TLS1_CK_PSK_WITH_AES_256_CBC_SHA                0x0300008D

/* Additional TLS ciphersuites from expired Internet Draft
 * draft-ietf-tls-56-bit-ciphersuites-01.txt
 * (available if TLS1_ALLOW_EXPERIMENTAL_CIPHERSUITES is defined, see
 * s3_lib.c).  We actually treat them like SSL 3.0 ciphers, which we probably
 * shouldn't.  Note that the first two are actually not in the IDs. */
#define TLS1_CK_RSA_EXPORT1024_WITH_RC4_56_MD5		0x03000060 /* not in ID */
#define TLS1_CK_RSA_EXPORT1024_WITH_RC2_CBC_56_MD5	0x03000061 /* not in ID */
#define TLS1_CK_RSA_EXPORT1024_WITH_DES_CBC_SHA		0x03000062
#define TLS1_CK_DHE_DSS_EXPORT1024_WITH_DES_CBC_SHA	0x03000063
#define TLS1_CK_RSA_EXPORT1024_WITH_RC4_56_SHA		0x03000064
#define TLS1_CK_DHE_DSS_EXPORT1024_WITH_RC4_56_SHA	0x03000065
#define TLS1_CK_DHE_DSS_WITH_RC4_128_SHA		0x03000066

/* AES ciphersuites from RFC 3268. */

#define TLS1_CK_RSA_WITH_AES_128_SHA			0x0300002F
#define TLS1_CK_DH_DSS_WITH_AES_128_SHA			0x03000030
#define TLS1_CK_DH_RSA_WITH_AES_128_SHA			0x03000031
#define TLS1_CK_DHE_DSS_WITH_AES_128_SHA		0x03000032
#define TLS1_CK_DHE_RSA_WITH_AES_128_SHA		0x03000033
#define TLS1_CK_ADH_WITH_AES_128_SHA			0x03000034

#define TLS1_CK_RSA_WITH_AES_256_SHA			0x03000035
#define TLS1_CK_DH_DSS_WITH_AES_256_SHA			0x03000036
#define TLS1_CK_DH_RSA_WITH_AES_256_SHA			0x03000037
#define TLS1_CK_DHE_DSS_WITH_AES_256_SHA		0x03000038
#define TLS1_CK_DHE_RSA_WITH_AES_256_SHA		0x03000039
#define TLS1_CK_ADH_WITH_AES_256_SHA			0x0300003A

/* TLS v1.2 ciphersuites */
#define TLS1_CK_RSA_WITH_NULL_SHA256			0x0300003B
#define TLS1_CK_RSA_WITH_AES_128_SHA256			0x0300003C
#define TLS1_CK_RSA_WITH_AES_256_SHA256			0x0300003D
#define TLS1_CK_DH_DSS_WITH_AES_128_SHA256		0x0300003E
#define TLS1_CK_DH_RSA_WITH_AES_128_SHA256		0x0300003F
#define TLS1_CK_DHE_DSS_WITH_AES_128_SHA256		0x03000040

/* Camellia ciphersuites from RFC 4132. */
#define TLS1_CK_RSA_WITH_CAMELLIA_128_CBC_SHA		0x03000041
#define TLS1_CK_DH_DSS_WITH_CAMELLIA_128_CBC_SHA	0x03000042
#define TLS1_CK_DH_RSA_WITH_CAMELLIA_128_CBC_SHA	0x03000043
#define TLS1_CK_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA	0x03000044
#define TLS1_CK_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA	0x03000045
#define TLS1_CK_ADH_WITH_CAMELLIA_128_CBC_SHA		0x03000046

/* TLS v1.2 ciphersuites */
#define TLS1_CK_DHE_RSA_WITH_AES_128_SHA256		0x03000067
#define TLS1_CK_DH_DSS_WITH_AES_256_SHA256		0x03000068
#define TLS1_CK_DH_RSA_WITH_AES_256_SHA256		0x03000069
#define TLS1_CK_DHE_DSS_WITH_AES_256_SHA256		0x0300006A
#define TLS1_CK_DHE_RSA_WITH_AES_256_SHA256		0x0300006B
#define TLS1_CK_ADH_WITH_AES_128_SHA256			0x0300006C
#define TLS1_CK_ADH_WITH_AES_256_SHA256			0x0300006D

/* Camellia ciphersuites from RFC 4132. */
#define TLS1_CK_RSA_WITH_CAMELLIA_256_CBC_SHA		0x03000084
#define TLS1_CK_DH_DSS_WITH_CAMELLIA_256_CBC_SHA	0x03000085
#define TLS1_CK_DH_RSA_WITH_CAMELLIA_256_CBC_SHA	0x03000086
#define TLS1_CK_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA	0x03000087
#define TLS1_CK_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA	0x03000088
#define TLS1_CK_ADH_WITH_CAMELLIA_256_CBC_SHA		0x03000089

/* SEED ciphersuites from RFC 4162. */
#define TLS1_CK_RSA_WITH_SEED_SHA                       0x03000096
#define TLS1_CK_DH_DSS_WITH_SEED_SHA                    0x03000097
#define TLS1_CK_DH_RSA_WITH_SEED_SHA                    0x03000098
#define TLS1_CK_DHE_DSS_WITH_SEED_SHA                   0x03000099
#define TLS1_CK_DHE_RSA_WITH_SEED_SHA                   0x0300009A
#define TLS1_CK_ADH_WITH_SEED_SHA			0x0300009B

/* TLS v1.2 GCM ciphersuites from RFC 5288. */
#define TLS1_CK_RSA_WITH_AES_128_GCM_SHA256		0x0300009C
#define TLS1_CK_RSA_WITH_AES_256_GCM_SHA384		0x0300009D
#define TLS1_CK_DHE_RSA_WITH_AES_128_GCM_SHA256		0x0300009E
#define TLS1_CK_DHE_RSA_WITH_AES_256_GCM_SHA384		0x0300009F
#define TLS1_CK_DH_RSA_WITH_AES_128_GCM_SHA256		0x030000A0
#define TLS1_CK_DH_RSA_WITH_AES_256_GCM_SHA384		0x030000A1
#define TLS1_CK_DHE_DSS_WITH_AES_128_GCM_SHA256		0x030000A2
#define TLS1_CK_DHE_DSS_WITH_AES_256_GCM_SHA384		0x030000A3
#define TLS1_CK_DH_DSS_WITH_AES_128_GCM_SHA256		0x030000A4
#define TLS1_CK_DH_DSS_WITH_AES_256_GCM_SHA384		0x030000A5
#define TLS1_CK_ADH_WITH_AES_128_GCM_SHA256		0x030000A6
#define TLS1_CK_ADH_WITH_AES_256_GCM_SHA384		0x030000A7

/* TLS 1.2 Camellia SHA-256 ciphersuites from RFC5932 */
#define TLS1_CK_RSA_WITH_CAMELLIA_128_CBC_SHA256	0x030000BA
#define TLS1_CK_DH_DSS_WITH_CAMELLIA_128_CBC_SHA256	0x030000BB
#define TLS1_CK_DH_RSA_WITH_CAMELLIA_128_CBC_SHA256	0x030000BC
#define TLS1_CK_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA256	0x030000BD
#define TLS1_CK_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA256	0x030000BE
#define TLS1_CK_ADH_WITH_CAMELLIA_128_CBC_SHA256	0x030000BF

#define TLS1_CK_RSA_WITH_CAMELLIA_256_CBC_SHA256	0x030000C0
#define TLS1_CK_DH_DSS_WITH_CAMELLIA_256_CBC_SHA256	0x030000C1
#define TLS1_CK_DH_RSA_WITH_CAMELLIA_256_CBC_SHA256	0x030000C2
#define TLS1_CK_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA256	0x030000C3
#define TLS1_CK_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA256	0x030000C4
#define TLS1_CK_ADH_WITH_CAMELLIA_256_CBC_SHA256	0x030000C5

/* TLS 1.3 cipher suites from RFC 8446 appendix B.4. */
#if defined(LIBRESSL_HAS_TLS1_3) || defined(LIBRESSL_INTERNAL)
#define TLS1_3_CK_AES_128_GCM_SHA256			0x03001301
#define TLS1_3_CK_AES_256_GCM_SHA384			0x03001302
#define TLS1_3_CK_CHACHA20_POLY1305_SHA256		0x03001303
#define TLS1_3_CK_AES_128_CCM_SHA256			0x03001304
#define TLS1_3_CK_AES_128_CCM_8_SHA256			0x03001305
#endif

/* ECC ciphersuites from RFC 4492. */
#define TLS1_CK_ECDH_ECDSA_WITH_NULL_SHA                0x0300C001
#define TLS1_CK_ECDH_ECDSA_WITH_RC4_128_SHA             0x0300C002
#define TLS1_CK_ECDH_ECDSA_WITH_DES_192_CBC3_SHA        0x0300C003
#define TLS1_CK_ECDH_ECDSA_WITH_AES_128_CBC_SHA         0x0300C004
#define TLS1_CK_ECDH_ECDSA_WITH_AES_256_CBC_SHA         0x0300C005

#define TLS1_CK_ECDHE_ECDSA_WITH_NULL_SHA               0x0300C006
#define TLS1_CK_ECDHE_ECDSA_WITH_RC4_128_SHA            0x0300C007
#define TLS1_CK_ECDHE_ECDSA_WITH_DES_192_CBC3_SHA       0x0300C008
#define TLS1_CK_ECDHE_ECDSA_WITH_AES_128_CBC_SHA        0x0300C009
#define TLS1_CK_ECDHE_ECDSA_WITH_AES_256_CBC_SHA        0x0300C00A

#define TLS1_CK_ECDH_RSA_WITH_NULL_SHA                  0x0300C00B
#define TLS1_CK_ECDH_RSA_WITH_RC4_128_SHA               0x0300C00C
#define TLS1_CK_ECDH_RSA_WITH_DES_192_CBC3_SHA          0x0300C00D
#define TLS1_CK_ECDH_RSA_WITH_AES_128_CBC_SHA           0x0300C00E
#define TLS1_CK_ECDH_RSA_WITH_AES_256_CBC_SHA           0x0300C00F

#define TLS1_CK_ECDHE_RSA_WITH_NULL_SHA                 0x0300C010
#define TLS1_CK_ECDHE_RSA_WITH_RC4_128_SHA              0x0300C011
#define TLS1_CK_ECDHE_RSA_WITH_DES_192_CBC3_SHA         0x0300C012
#define TLS1_CK_ECDHE_RSA_WITH_AES_128_CBC_SHA          0x0300C013
#define TLS1_CK_ECDHE_RSA_WITH_AES_256_CBC_SHA          0x0300C014

#define TLS1_CK_ECDH_anon_WITH_NULL_SHA                 0x0300C015
#define TLS1_CK_ECDH_anon_WITH_RC4_128_SHA              0x0300C016
#define TLS1_CK_ECDH_anon_WITH_DES_192_CBC3_SHA         0x0300C017
#define TLS1_CK_ECDH_anon_WITH_AES_128_CBC_SHA          0x0300C018
#define TLS1_CK_ECDH_anon_WITH_AES_256_CBC_SHA          0x0300C019

/* SRP ciphersuites from RFC 5054. */
#define TLS1_CK_SRP_SHA_WITH_3DES_EDE_CBC_SHA		0x0300C01A
#define TLS1_CK_SRP_SHA_RSA_WITH_3DES_EDE_CBC_SHA	0x0300C01B
#define TLS1_CK_SRP_SHA_DSS_WITH_3DES_EDE_CBC_SHA	0x0300C01C
#define TLS1_CK_SRP_SHA_WITH_AES_128_CBC_SHA		0x0300C01D
#define TLS1_CK_SRP_SHA_RSA_WITH_AES_128_CBC_SHA	0x0300C01E
#define TLS1_CK_SRP_SHA_DSS_WITH_AES_128_CBC_SHA	0x0300C01F
#define TLS1_CK_SRP_SHA_WITH_AES_256_CBC_SHA		0x0300C020
#define TLS1_CK_SRP_SHA_RSA_WITH_AES_256_CBC_SHA	0x0300C021
#define TLS1_CK_SRP_SHA_DSS_WITH_AES_256_CBC_SHA	0x0300C022

/* ECDH HMAC based ciphersuites from RFC 5289. */
#define TLS1_CK_ECDHE_ECDSA_WITH_AES_128_SHA256         0x0300C023
#define TLS1_CK_ECDHE_ECDSA_WITH_AES_256_SHA384         0x0300C024
#define TLS1_CK_ECDH_ECDSA_WITH_AES_128_SHA256          0x0300C025
#define TLS1_CK_ECDH_ECDSA_WITH_AES_256_SHA384          0x0300C026
#define TLS1_CK_ECDHE_RSA_WITH_AES_128_SHA256           0x0300C027
#define TLS1_CK_ECDHE_RSA_WITH_AES_256_SHA384           0x0300C028
#define TLS1_CK_ECDH_RSA_WITH_AES_128_SHA256            0x0300C029
#define TLS1_CK_ECDH_RSA_WITH_AES_256_SHA384            0x0300C02A

/* ECDH GCM based ciphersuites from RFC 5289. */
#define TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256	0x0300C02B
#define TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384	0x0300C02C
#define TLS1_CK_ECDH_ECDSA_WITH_AES_128_GCM_SHA256      0x0300C02D
#define TLS1_CK_ECDH_ECDSA_WITH_AES_256_GCM_SHA384      0x0300C02E
#define TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256       0x0300C02F
#define TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA384       0x0300C030
#define TLS1_CK_ECDH_RSA_WITH_AES_128_GCM_SHA256        0x0300C031
#define TLS1_CK_ECDH_RSA_WITH_AES_256_GCM_SHA384        0x0300C032

/* ChaCha20-Poly1305 based ciphersuites. */
#define TLS1_CK_ECDHE_RSA_CHACHA20_POLY1305		0x0300CCA8
#define TLS1_CK_ECDHE_ECDSA_CHACHA20_POLY1305		0x0300CCA9
#define TLS1_CK_DHE_RSA_CHACHA20_POLY1305		0x0300CCAA

#define TLS1_TXT_RSA_EXPORT1024_WITH_RC4_56_MD5		"EXP1024-RC4-MD5"
#define TLS1_TXT_RSA_EXPORT1024_WITH_RC2_CBC_56_MD5	"EXP1024-RC2-CBC-MD5"
#define TLS1_TXT_RSA_EXPORT1024_WITH_DES_CBC_SHA	"EXP1024-DES-CBC-SHA"
#define TLS1_TXT_DHE_DSS_EXPORT1024_WITH_DES_CBC_SHA	"EXP1024-DHE-DSS-DES-CBC-SHA"
#define TLS1_TXT_RSA_EXPORT1024_WITH_RC4_56_SHA		"EXP1024-RC4-SHA"
#define TLS1_TXT_DHE_DSS_EXPORT1024_WITH_RC4_56_SHA	"EXP1024-DHE-DSS-RC4-SHA"
#define TLS1_TXT_DHE_DSS_WITH_RC4_128_SHA		"DHE-DSS-RC4-SHA"

/* AES ciphersuites from RFC 3268. */
#define TLS1_TXT_RSA_WITH_AES_128_SHA			"AES128-SHA"
#define TLS1_TXT_DH_DSS_WITH_AES_128_SHA		"DH-DSS-AES128-SHA"
#define TLS1_TXT_DH_RSA_WITH_AES_128_SHA		"DH-RSA-AES128-SHA"
#define TLS1_TXT_DHE_DSS_WITH_AES_128_SHA		"DHE-DSS-AES128-SHA"
#define TLS1_TXT_DHE_RSA_WITH_AES_128_SHA		"DHE-RSA-AES128-SHA"
#define TLS1_TXT_ADH_WITH_AES_128_SHA			"ADH-AES128-SHA"

#define TLS1_TXT_RSA_WITH_AES_256_SHA			"AES256-SHA"
#define TLS1_TXT_DH_DSS_WITH_AES_256_SHA		"DH-DSS-AES256-SHA"
#define TLS1_TXT_DH_RSA_WITH_AES_256_SHA		"DH-RSA-AES256-SHA"
#define TLS1_TXT_DHE_DSS_WITH_AES_256_SHA		"DHE-DSS-AES256-SHA"
#define TLS1_TXT_DHE_RSA_WITH_AES_256_SHA		"DHE-RSA-AES256-SHA"
#define TLS1_TXT_ADH_WITH_AES_256_SHA			"ADH-AES256-SHA"

/* ECC ciphersuites from draft-ietf-tls-ecc-01.txt (Mar 15, 2001) */
#define TLS1_TXT_ECDH_ECDSA_WITH_NULL_SHA               "ECDH-ECDSA-NULL-SHA"
#define TLS1_TXT_ECDH_ECDSA_WITH_RC4_128_SHA            "ECDH-ECDSA-RC4-SHA"
#define TLS1_TXT_ECDH_ECDSA_WITH_DES_192_CBC3_SHA       "ECDH-ECDSA-DES-CBC3-SHA"
#define TLS1_TXT_ECDH_ECDSA_WITH_AES_128_CBC_SHA        "ECDH-ECDSA-AES128-SHA"
#define TLS1_TXT_ECDH_ECDSA_WITH_AES_256_CBC_SHA        "ECDH-ECDSA-AES256-SHA"

#define TLS1_TXT_ECDHE_ECDSA_WITH_NULL_SHA              "ECDHE-ECDSA-NULL-SHA"
#define TLS1_TXT_ECDHE_ECDSA_WITH_RC4_128_SHA           "ECDHE-ECDSA-RC4-SHA"
#define TLS1_TXT_ECDHE_ECDSA_WITH_DES_192_CBC3_SHA      "ECDHE-ECDSA-DES-CBC3-SHA"
#define TLS1_TXT_ECDHE_ECDSA_WITH_AES_128_CBC_SHA       "ECDHE-ECDSA-AES128-SHA"
#define TLS1_TXT_ECDHE_ECDSA_WITH_AES_256_CBC_SHA       "ECDHE-ECDSA-AES256-SHA"

#define TLS1_TXT_ECDH_RSA_WITH_NULL_SHA                 "ECDH-RSA-NULL-SHA"
#define TLS1_TXT_ECDH_RSA_WITH_RC4_128_SHA              "ECDH-RSA-RC4-SHA"
#define TLS1_TXT_ECDH_RSA_WITH_DES_192_CBC3_SHA         "ECDH-RSA-DES-CBC3-SHA"
#define TLS1_TXT_ECDH_RSA_WITH_AES_128_CBC_SHA          "ECDH-RSA-AES128-SHA"
#define TLS1_TXT_ECDH_RSA_WITH_AES_256_CBC_SHA          "ECDH-RSA-AES256-SHA"

#define TLS1_TXT_ECDHE_RSA_WITH_NULL_SHA                "ECDHE-RSA-NULL-SHA"
#define TLS1_TXT_ECDHE_RSA_WITH_RC4_128_SHA             "ECDHE-RSA-RC4-SHA"
#define TLS1_TXT_ECDHE_RSA_WITH_DES_192_CBC3_SHA        "ECDHE-RSA-DES-CBC3-SHA"
#define TLS1_TXT_ECDHE_RSA_WITH_AES_128_CBC_SHA         "ECDHE-RSA-AES128-SHA"
#define TLS1_TXT_ECDHE_RSA_WITH_AES_256_CBC_SHA         "ECDHE-RSA-AES256-SHA"

#define TLS1_TXT_ECDH_anon_WITH_NULL_SHA                "AECDH-NULL-SHA"
#define TLS1_TXT_ECDH_anon_WITH_RC4_128_SHA             "AECDH-RC4-SHA"
#define TLS1_TXT_ECDH_anon_WITH_DES_192_CBC3_SHA        "AECDH-DES-CBC3-SHA"
#define TLS1_TXT_ECDH_anon_WITH_AES_128_CBC_SHA         "AECDH-AES128-SHA"
#define TLS1_TXT_ECDH_anon_WITH_AES_256_CBC_SHA         "AECDH-AES256-SHA"

/* PSK ciphersuites from RFC 4279. */
#define TLS1_TXT_PSK_WITH_RC4_128_SHA			"PSK-RC4-SHA"
#define TLS1_TXT_PSK_WITH_3DES_EDE_CBC_SHA		"PSK-3DES-EDE-CBC-SHA"
#define TLS1_TXT_PSK_WITH_AES_128_CBC_SHA		"PSK-AES128-CBC-SHA"
#define TLS1_TXT_PSK_WITH_AES_256_CBC_SHA		"PSK-AES256-CBC-SHA"

/* SRP ciphersuite from RFC 5054. */
#define TLS1_TXT_SRP_SHA_WITH_3DES_EDE_CBC_SHA		"SRP-3DES-EDE-CBC-SHA"
#define TLS1_TXT_SRP_SHA_RSA_WITH_3DES_EDE_CBC_SHA	"SRP-RSA-3DES-EDE-CBC-SHA"
#define TLS1_TXT_SRP_SHA_DSS_WITH_3DES_EDE_CBC_SHA	"SRP-DSS-3DES-EDE-CBC-SHA"
#define TLS1_TXT_SRP_SHA_WITH_AES_128_CBC_SHA		"SRP-AES-128-CBC-SHA"
#define TLS1_TXT_SRP_SHA_RSA_WITH_AES_128_CBC_SHA	"SRP-RSA-AES-128-CBC-SHA"
#define TLS1_TXT_SRP_SHA_DSS_WITH_AES_128_CBC_SHA	"SRP-DSS-AES-128-CBC-SHA"
#define TLS1_TXT_SRP_SHA_WITH_AES_256_CBC_SHA		"SRP-AES-256-CBC-SHA"
#define TLS1_TXT_SRP_SHA_RSA_WITH_AES_256_CBC_SHA	"SRP-RSA-AES-256-CBC-SHA"
#define TLS1_TXT_SRP_SHA_DSS_WITH_AES_256_CBC_SHA	"SRP-DSS-AES-256-CBC-SHA"

/* Camellia ciphersuites from RFC 4132. */
#define TLS1_TXT_RSA_WITH_CAMELLIA_128_CBC_SHA		"CAMELLIA128-SHA"
#define TLS1_TXT_DH_DSS_WITH_CAMELLIA_128_CBC_SHA	"DH-DSS-CAMELLIA128-SHA"
#define TLS1_TXT_DH_RSA_WITH_CAMELLIA_128_CBC_SHA	"DH-RSA-CAMELLIA128-SHA"
#define TLS1_TXT_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA	"DHE-DSS-CAMELLIA128-SHA"
#define TLS1_TXT_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA	"DHE-RSA-CAMELLIA128-SHA"
#define TLS1_TXT_ADH_WITH_CAMELLIA_128_CBC_SHA		"ADH-CAMELLIA128-SHA"

#define TLS1_TXT_RSA_WITH_CAMELLIA_256_CBC_SHA		"CAMELLIA256-SHA"
#define TLS1_TXT_DH_DSS_WITH_CAMELLIA_256_CBC_SHA	"DH-DSS-CAMELLIA256-SHA"
#define TLS1_TXT_DH_RSA_WITH_CAMELLIA_256_CBC_SHA	"DH-RSA-CAMELLIA256-SHA"
#define TLS1_TXT_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA	"DHE-DSS-CAMELLIA256-SHA"
#define TLS1_TXT_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA	"DHE-RSA-CAMELLIA256-SHA"
#define TLS1_TXT_ADH_WITH_CAMELLIA_256_CBC_SHA		"ADH-CAMELLIA256-SHA"

/* TLS 1.2 Camellia SHA-256 ciphersuites from RFC5932 */
#define TLS1_TXT_RSA_WITH_CAMELLIA_128_CBC_SHA256	"CAMELLIA128-SHA256"
#define TLS1_TXT_DH_DSS_WITH_CAMELLIA_128_CBC_SHA256	"DH-DSS-CAMELLIA128-SHA256"
#define TLS1_TXT_DH_RSA_WITH_CAMELLIA_128_CBC_SHA256	"DH-RSA-CAMELLIA128-SHA256"
#define TLS1_TXT_DHE_DSS_WITH_CAMELLIA_128_CBC_SHA256	"DHE-DSS-CAMELLIA128-SHA256"
#define TLS1_TXT_DHE_RSA_WITH_CAMELLIA_128_CBC_SHA256	"DHE-RSA-CAMELLIA128-SHA256"
#define TLS1_TXT_ADH_WITH_CAMELLIA_128_CBC_SHA256	"ADH-CAMELLIA128-SHA256"

#define TLS1_TXT_RSA_WITH_CAMELLIA_256_CBC_SHA256	"CAMELLIA256-SHA256"
#define TLS1_TXT_DH_DSS_WITH_CAMELLIA_256_CBC_SHA256	"DH-DSS-CAMELLIA256-SHA256"
#define TLS1_TXT_DH_RSA_WITH_CAMELLIA_256_CBC_SHA256	"DH-RSA-CAMELLIA256-SHA256"
#define TLS1_TXT_DHE_DSS_WITH_CAMELLIA_256_CBC_SHA256	"DHE-DSS-CAMELLIA256-SHA256"
#define TLS1_TXT_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA256	"DHE-RSA-CAMELLIA256-SHA256"
#define TLS1_TXT_ADH_WITH_CAMELLIA_256_CBC_SHA256	"ADH-CAMELLIA256-SHA256"

/* SEED ciphersuites from RFC 4162. */
#define TLS1_TXT_RSA_WITH_SEED_SHA                      "SEED-SHA"
#define TLS1_TXT_DH_DSS_WITH_SEED_SHA                   "DH-DSS-SEED-SHA"
#define TLS1_TXT_DH_RSA_WITH_SEED_SHA                   "DH-RSA-SEED-SHA"
#define TLS1_TXT_DHE_DSS_WITH_SEED_SHA                  "DHE-DSS-SEED-SHA"
#define TLS1_TXT_DHE_RSA_WITH_SEED_SHA                  "DHE-RSA-SEED-SHA"
#define TLS1_TXT_ADH_WITH_SEED_SHA                      "ADH-SEED-SHA"

/* TLS v1.2 ciphersuites. */
#define TLS1_TXT_RSA_WITH_NULL_SHA256			"NULL-SHA256"
#define TLS1_TXT_RSA_WITH_AES_128_SHA256		"AES128-SHA256"
#define TLS1_TXT_RSA_WITH_AES_256_SHA256		"AES256-SHA256"
#define TLS1_TXT_DH_DSS_WITH_AES_128_SHA256		"DH-DSS-AES128-SHA256"
#define TLS1_TXT_DH_RSA_WITH_AES_128_SHA256		"DH-RSA-AES128-SHA256"
#define TLS1_TXT_DHE_DSS_WITH_AES_128_SHA256		"DHE-DSS-AES128-SHA256"
#define TLS1_TXT_DHE_RSA_WITH_AES_128_SHA256		"DHE-RSA-AES128-SHA256"
#define TLS1_TXT_DH_DSS_WITH_AES_256_SHA256		"DH-DSS-AES256-SHA256"
#define TLS1_TXT_DH_RSA_WITH_AES_256_SHA256		"DH-RSA-AES256-SHA256"
#define TLS1_TXT_DHE_DSS_WITH_AES_256_SHA256		"DHE-DSS-AES256-SHA256"
#define TLS1_TXT_DHE_RSA_WITH_AES_256_SHA256		"DHE-RSA-AES256-SHA256"
#define TLS1_TXT_ADH_WITH_AES_128_SHA256		"ADH-AES128-SHA256"
#define TLS1_TXT_ADH_WITH_AES_256_SHA256		"ADH-AES256-SHA256"

/* TLS v1.2 GCM ciphersuites from RFC 5288. */
#define TLS1_TXT_RSA_WITH_AES_128_GCM_SHA256		"AES128-GCM-SHA256"
#define TLS1_TXT_RSA_WITH_AES_256_GCM_SHA384		"AES256-GCM-SHA384"
#define TLS1_TXT_DHE_RSA_WITH_AES_128_GCM_SHA256	"DHE-RSA-AES128-GCM-SHA256"
#define TLS1_TXT_DHE_RSA_WITH_AES_256_GCM_SHA384	"DHE-RSA-AES256-GCM-SHA384"
#define TLS1_TXT_DH_RSA_WITH_AES_128_GCM_SHA256		"DH-RSA-AES128-GCM-SHA256"
#define TLS1_TXT_DH_RSA_WITH_AES_256_GCM_SHA384		"DH-RSA-AES256-GCM-SHA384"
#define TLS1_TXT_DHE_DSS_WITH_AES_128_GCM_SHA256	"DHE-DSS-AES128-GCM-SHA256"
#define TLS1_TXT_DHE_DSS_WITH_AES_256_GCM_SHA384	"DHE-DSS-AES256-GCM-SHA384"
#define TLS1_TXT_DH_DSS_WITH_AES_128_GCM_SHA256		"DH-DSS-AES128-GCM-SHA256"
#define TLS1_TXT_DH_DSS_WITH_AES_256_GCM_SHA384		"DH-DSS-AES256-GCM-SHA384"
#define TLS1_TXT_ADH_WITH_AES_128_GCM_SHA256		"ADH-AES128-GCM-SHA256"
#define TLS1_TXT_ADH_WITH_AES_256_GCM_SHA384		"ADH-AES256-GCM-SHA384"

/* ECDH HMAC based ciphersuites from RFC 5289. */
#define TLS1_TXT_ECDHE_ECDSA_WITH_AES_128_SHA256    "ECDHE-ECDSA-AES128-SHA256"
#define TLS1_TXT_ECDHE_ECDSA_WITH_AES_256_SHA384    "ECDHE-ECDSA-AES256-SHA384"
#define TLS1_TXT_ECDH_ECDSA_WITH_AES_128_SHA256     "ECDH-ECDSA-AES128-SHA256"
#define TLS1_TXT_ECDH_ECDSA_WITH_AES_256_SHA384     "ECDH-ECDSA-AES256-SHA384"
#define TLS1_TXT_ECDHE_RSA_WITH_AES_128_SHA256      "ECDHE-RSA-AES128-SHA256"
#define TLS1_TXT_ECDHE_RSA_WITH_AES_256_SHA384      "ECDHE-RSA-AES256-SHA384"
#define TLS1_TXT_ECDH_RSA_WITH_AES_128_SHA256       "ECDH-RSA-AES128-SHA256"
#define TLS1_TXT_ECDH_RSA_WITH_AES_256_SHA384       "ECDH-RSA-AES256-SHA384"

/* ECDH GCM based ciphersuites from RFC 5289. */
#define TLS1_TXT_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256    "ECDHE-ECDSA-AES128-GCM-SHA256"
#define TLS1_TXT_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384    "ECDHE-ECDSA-AES256-GCM-SHA384"
#define TLS1_TXT_ECDH_ECDSA_WITH_AES_128_GCM_SHA256     "ECDH-ECDSA-AES128-GCM-SHA256"
#define TLS1_TXT_ECDH_ECDSA_WITH_AES_256_GCM_SHA384     "ECDH-ECDSA-AES256-GCM-SHA384"
#define TLS1_TXT_ECDHE_RSA_WITH_AES_128_GCM_SHA256      "ECDHE-RSA-AES128-GCM-SHA256"
#define TLS1_TXT_ECDHE_RSA_WITH_AES_256_GCM_SHA384      "ECDHE-RSA-AES256-GCM-SHA384"
#define TLS1_TXT_ECDH_RSA_WITH_AES_128_GCM_SHA256       "ECDH-RSA-AES128-GCM-SHA256"
#define TLS1_TXT_ECDH_RSA_WITH_AES_256_GCM_SHA384       "ECDH-RSA-AES256-GCM-SHA384"

/* ChaCha20-Poly1305 based ciphersuites. */
#define TLS1_TXT_ECDHE_RSA_WITH_CHACHA20_POLY1305	"ECDHE-RSA-CHACHA20-POLY1305"
#define TLS1_TXT_ECDHE_ECDSA_WITH_CHACHA20_POLY1305	"ECDHE-ECDSA-CHACHA20-POLY1305"
#define TLS1_TXT_DHE_RSA_WITH_CHACHA20_POLY1305		"DHE-RSA-CHACHA20-POLY1305"

/* TLS 1.3 cipher suites from RFC 8446 appendix B.4. */
#if defined(LIBRESSL_HAS_TLS1_3) || defined(LIBRESSL_INTERNAL)
#define TLS1_3_TXT_AES_128_GCM_SHA256			"AEAD-AES128-GCM-SHA256"
#define TLS1_3_TXT_AES_256_GCM_SHA384			"AEAD-AES256-GCM-SHA384"
#define TLS1_3_TXT_CHACHA20_POLY1305_SHA256		"AEAD-CHACHA20-POLY1305-SHA256"
#define TLS1_3_TXT_AES_128_CCM_SHA256			"AEAD-AES128-CCM-SHA256"
#define TLS1_3_TXT_AES_128_CCM_8_SHA256			"AEAD-AES128-CCM-8-SHA256"

#define TLS1_3_RFC_AES_128_GCM_SHA256			"TLS_AES_128_GCM_SHA256"
#define TLS1_3_RFC_AES_256_GCM_SHA384			"TLS_AES_256_GCM_SHA384"
#define TLS1_3_RFC_CHACHA20_POLY1305_SHA256		"TLS_CHACHA20_POLY1305_SHA256"
#define TLS1_3_RFC_AES_128_CCM_SHA256			"TLS_AES_128_CCM_SHA256"
#define TLS1_3_RFC_AES_128_CCM_8_SHA256			"TLS_AES_128_CCM_8_SHA256"
#endif

#define TLS1_FINISH_MAC_LENGTH		12

#define TLS_MD_MAX_CONST_SIZE			20
#define TLS_MD_CLIENT_FINISH_CONST		"client finished"
#define TLS_MD_CLIENT_FINISH_CONST_SIZE		15
#define TLS_MD_SERVER_FINISH_CONST		"server finished"
#define TLS_MD_SERVER_FINISH_CONST_SIZE		15
#define TLS_MD_KEY_EXPANSION_CONST		"key expansion"
#define TLS_MD_KEY_EXPANSION_CONST_SIZE		13
#define TLS_MD_CLIENT_WRITE_KEY_CONST		"client write key"
#define TLS_MD_CLIENT_WRITE_KEY_CONST_SIZE	16
#define TLS_MD_SERVER_WRITE_KEY_CONST		"server write key"
#define TLS_MD_SERVER_WRITE_KEY_CONST_SIZE	16
#define TLS_MD_IV_BLOCK_CONST			"IV block"
#define TLS_MD_IV_BLOCK_CONST_SIZE		8
#define TLS_MD_MASTER_SECRET_CONST		"master secret"
#define TLS_MD_MASTER_SECRET_CONST_SIZE		13

#ifdef  __cplusplus
}
#endif
#endif
