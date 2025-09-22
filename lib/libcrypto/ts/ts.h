/* $OpenBSD: ts.h,v 1.24 2024/03/26 00:39:22 beck Exp $ */
/* Written by Zoltan Glozik (zglozik@opentsa.org) for the OpenSSL
 * project 2002, 2003, 2004.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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

#ifndef HEADER_TS_H
#define HEADER_TS_H

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_BUFFER
#include <openssl/buffer.h>
#endif
#ifndef OPENSSL_NO_EVP
#include <openssl/evp.h>
#endif
#ifndef OPENSSL_NO_BIO
#include <openssl/bio.h>
#endif
#include <openssl/stack.h>
#include <openssl/asn1.h>
#include <openssl/safestack.h>

#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif

#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif

#ifdef  __cplusplus
extern "C" {
#endif

#include <openssl/x509.h>
#include <openssl/x509v3.h>

typedef struct TS_msg_imprint_st TS_MSG_IMPRINT;
typedef struct TS_req_st TS_REQ;
typedef struct TS_accuracy_st TS_ACCURACY;
typedef struct TS_tst_info_st TS_TST_INFO;

/* Possible values for status. */
#define	TS_STATUS_GRANTED			0
#define	TS_STATUS_GRANTED_WITH_MODS		1
#define	TS_STATUS_REJECTION			2
#define	TS_STATUS_WAITING			3
#define	TS_STATUS_REVOCATION_WARNING		4
#define	TS_STATUS_REVOCATION_NOTIFICATION	5

/* Possible values for failure_info. */
#define	TS_INFO_BAD_ALG			0
#define	TS_INFO_BAD_REQUEST		2
#define	TS_INFO_BAD_DATA_FORMAT		5
#define	TS_INFO_TIME_NOT_AVAILABLE	14
#define	TS_INFO_UNACCEPTED_POLICY	15
#define	TS_INFO_UNACCEPTED_EXTENSION	16
#define	TS_INFO_ADD_INFO_NOT_AVAILABLE	17
#define	TS_INFO_SYSTEM_FAILURE		25

typedef struct TS_status_info_st TS_STATUS_INFO;

DECLARE_STACK_OF(ASN1_UTF8STRING)

typedef struct ESS_issuer_serial ESS_ISSUER_SERIAL;
typedef struct ESS_cert_id ESS_CERT_ID;
DECLARE_STACK_OF(ESS_CERT_ID)
typedef struct ESS_signing_cert ESS_SIGNING_CERT;

typedef struct ESS_cert_id_v2 ESS_CERT_ID_V2;
DECLARE_STACK_OF(ESS_CERT_ID_V2)

typedef struct ESS_signing_cert_v2 ESS_SIGNING_CERT_V2;

typedef struct TS_resp_st TS_RESP;

TS_REQ	*TS_REQ_new(void);
void	TS_REQ_free(TS_REQ *a);
int	i2d_TS_REQ(const TS_REQ *a, unsigned char **pp);
TS_REQ	*d2i_TS_REQ(TS_REQ **a, const unsigned char **pp, long length);

TS_REQ	*TS_REQ_dup(TS_REQ *a);

TS_REQ	*d2i_TS_REQ_fp(FILE *fp, TS_REQ **a);
int	i2d_TS_REQ_fp(FILE *fp, TS_REQ *a);
TS_REQ	*d2i_TS_REQ_bio(BIO *fp, TS_REQ **a);
int	i2d_TS_REQ_bio(BIO *fp, TS_REQ *a);

TS_MSG_IMPRINT	*TS_MSG_IMPRINT_new(void);
void		TS_MSG_IMPRINT_free(TS_MSG_IMPRINT *a);
int		i2d_TS_MSG_IMPRINT(const TS_MSG_IMPRINT *a, unsigned char **pp);
TS_MSG_IMPRINT	*d2i_TS_MSG_IMPRINT(TS_MSG_IMPRINT **a,
		    const unsigned char **pp, long length);

TS_MSG_IMPRINT	*TS_MSG_IMPRINT_dup(TS_MSG_IMPRINT *a);

TS_MSG_IMPRINT	*d2i_TS_MSG_IMPRINT_fp(FILE *fp, TS_MSG_IMPRINT **a);
int		i2d_TS_MSG_IMPRINT_fp(FILE *fp, TS_MSG_IMPRINT *a);
TS_MSG_IMPRINT	*d2i_TS_MSG_IMPRINT_bio(BIO *fp, TS_MSG_IMPRINT **a);
int		i2d_TS_MSG_IMPRINT_bio(BIO *fp, TS_MSG_IMPRINT *a);

TS_RESP	*TS_RESP_new(void);
void	TS_RESP_free(TS_RESP *a);
int	i2d_TS_RESP(const TS_RESP *a, unsigned char **pp);
TS_RESP	*d2i_TS_RESP(TS_RESP **a, const unsigned char **pp, long length);
TS_TST_INFO *PKCS7_to_TS_TST_INFO(PKCS7 *token);
TS_RESP	*TS_RESP_dup(TS_RESP *a);

TS_RESP	*d2i_TS_RESP_fp(FILE *fp, TS_RESP **a);
int	i2d_TS_RESP_fp(FILE *fp, TS_RESP *a);
TS_RESP	*d2i_TS_RESP_bio(BIO *fp, TS_RESP **a);
int	i2d_TS_RESP_bio(BIO *fp, TS_RESP *a);

TS_STATUS_INFO	*TS_STATUS_INFO_new(void);
void		TS_STATUS_INFO_free(TS_STATUS_INFO *a);
int		i2d_TS_STATUS_INFO(const TS_STATUS_INFO *a, unsigned char **pp);
TS_STATUS_INFO	*d2i_TS_STATUS_INFO(TS_STATUS_INFO **a,
		    const unsigned char **pp, long length);
TS_STATUS_INFO	*TS_STATUS_INFO_dup(TS_STATUS_INFO *a);

TS_TST_INFO	*TS_TST_INFO_new(void);
void		TS_TST_INFO_free(TS_TST_INFO *a);
int		i2d_TS_TST_INFO(const TS_TST_INFO *a, unsigned char **pp);
TS_TST_INFO	*d2i_TS_TST_INFO(TS_TST_INFO **a, const unsigned char **pp,
		    long length);
TS_TST_INFO	*TS_TST_INFO_dup(TS_TST_INFO *a);

TS_TST_INFO	*d2i_TS_TST_INFO_fp(FILE *fp, TS_TST_INFO **a);
int		i2d_TS_TST_INFO_fp(FILE *fp, TS_TST_INFO *a);
TS_TST_INFO	*d2i_TS_TST_INFO_bio(BIO *fp, TS_TST_INFO **a);
int		i2d_TS_TST_INFO_bio(BIO *fp, TS_TST_INFO *a);

TS_ACCURACY	*TS_ACCURACY_new(void);
void		TS_ACCURACY_free(TS_ACCURACY *a);
int		i2d_TS_ACCURACY(const TS_ACCURACY *a, unsigned char **pp);
TS_ACCURACY	*d2i_TS_ACCURACY(TS_ACCURACY **a, const unsigned char **pp,
		    long length);
TS_ACCURACY	*TS_ACCURACY_dup(TS_ACCURACY *a);

ESS_ISSUER_SERIAL *ESS_ISSUER_SERIAL_new(void);
void		  ESS_ISSUER_SERIAL_free(ESS_ISSUER_SERIAL *a);
int		  i2d_ESS_ISSUER_SERIAL(const ESS_ISSUER_SERIAL *a,
		    unsigned char **pp);
ESS_ISSUER_SERIAL *d2i_ESS_ISSUER_SERIAL(ESS_ISSUER_SERIAL **a,
		    const unsigned char **pp, long length);
ESS_ISSUER_SERIAL *ESS_ISSUER_SERIAL_dup(ESS_ISSUER_SERIAL *a);

ESS_CERT_ID	*ESS_CERT_ID_new(void);
void		ESS_CERT_ID_free(ESS_CERT_ID *a);
int		i2d_ESS_CERT_ID(const ESS_CERT_ID *a, unsigned char **pp);
ESS_CERT_ID	*d2i_ESS_CERT_ID(ESS_CERT_ID **a, const unsigned char **pp,
		    long length);
ESS_CERT_ID	*ESS_CERT_ID_dup(ESS_CERT_ID *a);

ESS_SIGNING_CERT *ESS_SIGNING_CERT_new(void);
void		 ESS_SIGNING_CERT_free(ESS_SIGNING_CERT *a);
int		 i2d_ESS_SIGNING_CERT(const ESS_SIGNING_CERT *a,
		    unsigned char **pp);
ESS_SIGNING_CERT *d2i_ESS_SIGNING_CERT(ESS_SIGNING_CERT **a,
		    const unsigned char **pp, long length);
ESS_SIGNING_CERT *ESS_SIGNING_CERT_dup(ESS_SIGNING_CERT *a);

int TS_REQ_set_version(TS_REQ *a, long version);
long TS_REQ_get_version(const TS_REQ *a);

int TS_REQ_set_msg_imprint(TS_REQ *a, TS_MSG_IMPRINT *msg_imprint);
TS_MSG_IMPRINT *TS_REQ_get_msg_imprint(TS_REQ *a);

int TS_MSG_IMPRINT_set_algo(TS_MSG_IMPRINT *a, X509_ALGOR *alg);
X509_ALGOR *TS_MSG_IMPRINT_get_algo(TS_MSG_IMPRINT *a);

int TS_MSG_IMPRINT_set_msg(TS_MSG_IMPRINT *a, unsigned char *d, int len);
ASN1_OCTET_STRING *TS_MSG_IMPRINT_get_msg(TS_MSG_IMPRINT *a);

int TS_REQ_set_policy_id(TS_REQ *a, const ASN1_OBJECT *policy);
ASN1_OBJECT *TS_REQ_get_policy_id(TS_REQ *a);

int TS_REQ_set_nonce(TS_REQ *a, const ASN1_INTEGER *nonce);
const ASN1_INTEGER *TS_REQ_get_nonce(const TS_REQ *a);

int TS_REQ_set_cert_req(TS_REQ *a, int cert_req);
int TS_REQ_get_cert_req(const TS_REQ *a);

STACK_OF(X509_EXTENSION) *TS_REQ_get_exts(TS_REQ *a);
void TS_REQ_ext_free(TS_REQ *a);
int TS_REQ_get_ext_count(TS_REQ *a);
int TS_REQ_get_ext_by_NID(TS_REQ *a, int nid, int lastpos);
int TS_REQ_get_ext_by_OBJ(TS_REQ *a, const ASN1_OBJECT *obj, int lastpos);
int TS_REQ_get_ext_by_critical(TS_REQ *a, int crit, int lastpos);
X509_EXTENSION *TS_REQ_get_ext(TS_REQ *a, int loc);
X509_EXTENSION *TS_REQ_delete_ext(TS_REQ *a, int loc);
int TS_REQ_add_ext(TS_REQ *a, X509_EXTENSION *ex, int loc);
void *TS_REQ_get_ext_d2i(TS_REQ *a, int nid, int *crit, int *idx);

/* Function declarations for TS_REQ defined in ts/ts_req_print.c */

int TS_REQ_print_bio(BIO *bio, TS_REQ *a);

/* Function declarations for TS_RESP defined in ts/ts_rsp_utils.c */

int TS_RESP_set_status_info(TS_RESP *a, TS_STATUS_INFO *info);
TS_STATUS_INFO *TS_RESP_get_status_info(TS_RESP *a);

const ASN1_UTF8STRING *TS_STATUS_INFO_get0_failure_info(const TS_STATUS_INFO *si);
const STACK_OF(ASN1_UTF8STRING) *
    TS_STATUS_INFO_get0_text(const TS_STATUS_INFO *si);
const ASN1_INTEGER *TS_STATUS_INFO_get0_status(const TS_STATUS_INFO *si);
int TS_STATUS_INFO_set_status(TS_STATUS_INFO *si, int i);

/* Caller loses ownership of PKCS7 and TS_TST_INFO objects. */
void TS_RESP_set_tst_info(TS_RESP *a, PKCS7 *p7, TS_TST_INFO *tst_info);
PKCS7 *TS_RESP_get_token(TS_RESP *a);
TS_TST_INFO *TS_RESP_get_tst_info(TS_RESP *a);

int TS_TST_INFO_set_version(TS_TST_INFO *a, long version);
long TS_TST_INFO_get_version(const TS_TST_INFO *a);

int TS_TST_INFO_set_policy_id(TS_TST_INFO *a, ASN1_OBJECT *policy_id);
ASN1_OBJECT *TS_TST_INFO_get_policy_id(TS_TST_INFO *a);

int TS_TST_INFO_set_msg_imprint(TS_TST_INFO *a, TS_MSG_IMPRINT *msg_imprint);
TS_MSG_IMPRINT *TS_TST_INFO_get_msg_imprint(TS_TST_INFO *a);

int TS_TST_INFO_set_serial(TS_TST_INFO *a, const ASN1_INTEGER *serial);
const ASN1_INTEGER *TS_TST_INFO_get_serial(const TS_TST_INFO *a);

int TS_TST_INFO_set_time(TS_TST_INFO *a, const ASN1_GENERALIZEDTIME *gtime);
const ASN1_GENERALIZEDTIME *TS_TST_INFO_get_time(const TS_TST_INFO *a);

int TS_TST_INFO_set_accuracy(TS_TST_INFO *a, TS_ACCURACY *accuracy);
TS_ACCURACY *TS_TST_INFO_get_accuracy(TS_TST_INFO *a);

int TS_ACCURACY_set_seconds(TS_ACCURACY *a, const ASN1_INTEGER *seconds);
const ASN1_INTEGER *TS_ACCURACY_get_seconds(const TS_ACCURACY *a);

int TS_ACCURACY_set_millis(TS_ACCURACY *a, const ASN1_INTEGER *millis);
const ASN1_INTEGER *TS_ACCURACY_get_millis(const TS_ACCURACY *a);

int TS_ACCURACY_set_micros(TS_ACCURACY *a, const ASN1_INTEGER *micros);
const ASN1_INTEGER *TS_ACCURACY_get_micros(const TS_ACCURACY *a);

int TS_TST_INFO_set_ordering(TS_TST_INFO *a, int ordering);
int TS_TST_INFO_get_ordering(const TS_TST_INFO *a);

int TS_TST_INFO_set_nonce(TS_TST_INFO *a, const ASN1_INTEGER *nonce);
const ASN1_INTEGER *TS_TST_INFO_get_nonce(const TS_TST_INFO *a);

int TS_TST_INFO_set_tsa(TS_TST_INFO *a, GENERAL_NAME *tsa);
GENERAL_NAME *TS_TST_INFO_get_tsa(TS_TST_INFO *a);

STACK_OF(X509_EXTENSION) *TS_TST_INFO_get_exts(TS_TST_INFO *a);
void TS_TST_INFO_ext_free(TS_TST_INFO *a);
int TS_TST_INFO_get_ext_count(TS_TST_INFO *a);
int TS_TST_INFO_get_ext_by_NID(TS_TST_INFO *a, int nid, int lastpos);
int TS_TST_INFO_get_ext_by_OBJ(TS_TST_INFO *a, const ASN1_OBJECT *obj,
    int lastpos);
int TS_TST_INFO_get_ext_by_critical(TS_TST_INFO *a, int crit, int lastpos);
X509_EXTENSION *TS_TST_INFO_get_ext(TS_TST_INFO *a, int loc);
X509_EXTENSION *TS_TST_INFO_delete_ext(TS_TST_INFO *a, int loc);
int TS_TST_INFO_add_ext(TS_TST_INFO *a, X509_EXTENSION *ex, int loc);
void *TS_TST_INFO_get_ext_d2i(TS_TST_INFO *a, int nid, int *crit, int *idx);

/* Declarations related to response generation, defined in ts/ts_rsp_sign.c. */

/* Optional flags for response generation. */

/* Don't include the TSA name in response. */
#define	TS_TSA_NAME		0x01

/* Set ordering to true in response. */
#define	TS_ORDERING		0x02

/*
 * Include the signer certificate and the other specified certificates in
 * the ESS signing certificate attribute beside the PKCS7 signed data.
 * Only the signer certificates is included by default.
 */
#define	TS_ESS_CERT_ID_CHAIN	0x04

/* Forward declaration. */
struct TS_resp_ctx;

/* This must return a unique number less than 160 bits long. */
typedef ASN1_INTEGER *(*TS_serial_cb)(struct TS_resp_ctx *, void *);

/* This must return the seconds and microseconds since Jan 1, 1970 in
   the sec and usec variables allocated by the caller.
   Return non-zero for success and zero for failure. */
typedef	int (*TS_time_cb)(struct TS_resp_ctx *, void *, time_t *sec, long *usec);

/* This must process the given extension.
 * It can modify the TS_TST_INFO object of the context.
 * Return values: !0 (processed), 0 (error, it must set the
 * status info/failure info of the response).
 */
typedef	int (*TS_extension_cb)(struct TS_resp_ctx *, X509_EXTENSION *, void *);

typedef struct TS_resp_ctx TS_RESP_CTX;

DECLARE_STACK_OF(EVP_MD)

/* Creates a response context that can be used for generating responses. */
TS_RESP_CTX *TS_RESP_CTX_new(void);
void TS_RESP_CTX_free(TS_RESP_CTX *ctx);

/* This parameter must be set. */
int TS_RESP_CTX_set_signer_cert(TS_RESP_CTX *ctx, X509 *signer);

/* This parameter must be set. */
int TS_RESP_CTX_set_signer_key(TS_RESP_CTX *ctx, EVP_PKEY *key);

/* This parameter must be set. */
int TS_RESP_CTX_set_def_policy(TS_RESP_CTX *ctx, const ASN1_OBJECT *def_policy);

/* No additional certs are included in the response by default. */
int TS_RESP_CTX_set_certs(TS_RESP_CTX *ctx, STACK_OF(X509) *certs);

/* Adds a new acceptable policy, only the default policy
   is accepted by default. */
int TS_RESP_CTX_add_policy(TS_RESP_CTX *ctx, const ASN1_OBJECT *policy);

/* Adds a new acceptable message digest. Note that no message digests
   are accepted by default. The md argument is shared with the caller. */
int TS_RESP_CTX_add_md(TS_RESP_CTX *ctx, const EVP_MD *md);

/* Accuracy is not included by default. */
int TS_RESP_CTX_set_accuracy(TS_RESP_CTX *ctx,
    int secs, int millis, int micros);

/* Clock precision digits, i.e. the number of decimal digits:
   '0' means sec, '3' msec, '6' usec, and so on. Default is 0. */
int TS_RESP_CTX_set_clock_precision_digits(TS_RESP_CTX *ctx,
    unsigned clock_precision_digits);
/* At most we accept sec precision. */
#define TS_MAX_CLOCK_PRECISION_DIGITS 0

/* No flags are set by default. */
void TS_RESP_CTX_add_flags(TS_RESP_CTX *ctx, int flags);

/* Default callback always returns a constant. */
void TS_RESP_CTX_set_serial_cb(TS_RESP_CTX *ctx, TS_serial_cb cb, void *data);

/* Default callback uses gettimeofday() and gmtime(). */
void TS_RESP_CTX_set_time_cb(TS_RESP_CTX *ctx, TS_time_cb cb, void *data);

/* Default callback rejects all extensions. The extension callback is called
 * when the TS_TST_INFO object is already set up and not signed yet. */
/* FIXME: extension handling is not tested yet. */
void TS_RESP_CTX_set_extension_cb(TS_RESP_CTX *ctx,
    TS_extension_cb cb, void *data);

/* The following methods can be used in the callbacks. */
int TS_RESP_CTX_set_status_info(TS_RESP_CTX *ctx,
    int status, const char *text);

/* Sets the status info only if it is still TS_STATUS_GRANTED. */
int TS_RESP_CTX_set_status_info_cond(TS_RESP_CTX *ctx,
    int status, const char *text);

int TS_RESP_CTX_add_failure_info(TS_RESP_CTX *ctx, int failure);

/* The get methods below can be used in the extension callback. */
TS_REQ *TS_RESP_CTX_get_request(TS_RESP_CTX *ctx);

TS_TST_INFO *TS_RESP_CTX_get_tst_info(TS_RESP_CTX *ctx);

/*
 * Creates the signed TS_TST_INFO and puts it in TS_RESP.
 * In case of errors it sets the status info properly.
 * Returns NULL only in case of memory allocation/fatal error.
 */
TS_RESP *TS_RESP_create_response(TS_RESP_CTX *ctx, BIO *req_bio);

/*
 * Declarations related to response verification,
 * they are defined in ts/ts_rsp_verify.c.
 */

int TS_RESP_verify_signature(PKCS7 *token, STACK_OF(X509) *certs,
    X509_STORE *store, X509 **signer_out);

/* Context structure for the generic verify method. */

/* Verify the signer's certificate and the signature of the response. */
#define	TS_VFY_SIGNATURE	(1u << 0)
/* Verify the version number of the response. */
#define	TS_VFY_VERSION		(1u << 1)
/* Verify if the policy supplied by the user matches the policy of the TSA. */
#define	TS_VFY_POLICY		(1u << 2)
/* Verify the message imprint provided by the user. This flag should not be
   specified with TS_VFY_DATA. */
#define	TS_VFY_IMPRINT		(1u << 3)
/* Verify the message imprint computed by the verify method from the user
   provided data and the MD algorithm of the response. This flag should not be
   specified with TS_VFY_IMPRINT. */
#define	TS_VFY_DATA		(1u << 4)
/* Verify the nonce value. */
#define	TS_VFY_NONCE		(1u << 5)
/* Verify if the TSA name field matches the signer certificate. */
#define	TS_VFY_SIGNER		(1u << 6)
/* Verify if the TSA name field equals to the user provided name. */
#define	TS_VFY_TSA_NAME		(1u << 7)

/* You can use the following convenience constants. */
#define	TS_VFY_ALL_IMPRINT	(TS_VFY_SIGNATURE	\
				 | TS_VFY_VERSION	\
				 | TS_VFY_POLICY	\
				 | TS_VFY_IMPRINT	\
				 | TS_VFY_NONCE		\
				 | TS_VFY_SIGNER	\
				 | TS_VFY_TSA_NAME)
#define	TS_VFY_ALL_DATA		(TS_VFY_SIGNATURE	\
				 | TS_VFY_VERSION	\
				 | TS_VFY_POLICY	\
				 | TS_VFY_DATA		\
				 | TS_VFY_NONCE		\
				 | TS_VFY_SIGNER	\
				 | TS_VFY_TSA_NAME)

typedef struct TS_verify_ctx TS_VERIFY_CTX;

int TS_RESP_verify_response(TS_VERIFY_CTX *ctx, TS_RESP *response);
int TS_RESP_verify_token(TS_VERIFY_CTX *ctx, PKCS7 *token);

/*
 * Declarations related to response verification context,
 * they are defined in ts/ts_verify_ctx.c.
 */

/* Set all fields to zero. */
TS_VERIFY_CTX *TS_VERIFY_CTX_new(void);
void TS_VERIFY_CTX_free(TS_VERIFY_CTX *ctx);
void TS_VERIFY_CTX_cleanup(TS_VERIFY_CTX *ctx);

int TS_VERIFY_CTX_add_flags(TS_VERIFY_CTX *ctx, int flags);
int TS_VERIFY_CTX_set_flags(TS_VERIFY_CTX *ctx, int flags);
BIO *TS_VERIFY_CTX_set_data(TS_VERIFY_CTX *ctx, BIO *bio);
X509_STORE *TS_VERIFY_CTX_set_store(TS_VERIFY_CTX *ctx, X509_STORE *store);
/* R$ special */
#define TS_VERIFY_CTS_set_certs TS_VERIFY_CTX_set_certs
STACK_OF(X509) *TS_VERIFY_CTX_set_certs(TS_VERIFY_CTX *ctx,
    STACK_OF(X509) *certs);
unsigned char *TS_VERIFY_CTX_set_imprint(TS_VERIFY_CTX *ctx,
    unsigned char *imprint, long imprint_len);

/*
 * If ctx is NULL, it allocates and returns a new object, otherwise
 * it returns ctx. It initialises all the members as follows:
 * flags = TS_VFY_ALL_IMPRINT & ~(TS_VFY_TSA_NAME | TS_VFY_SIGNATURE)
 * certs = NULL
 * store = NULL
 * policy = policy from the request or NULL if absent (in this case
 *	TS_VFY_POLICY is cleared from flags as well)
 * md_alg = MD algorithm from request
 * imprint, imprint_len = imprint from request
 * data = NULL
 * nonce, nonce_len = nonce from the request or NULL if absent (in this case
 *	TS_VFY_NONCE is cleared from flags as well)
 * tsa_name = NULL
 * Important: after calling this method TS_VFY_SIGNATURE should be added!
 */
TS_VERIFY_CTX *TS_REQ_to_TS_VERIFY_CTX(TS_REQ *req, TS_VERIFY_CTX *ctx);

/* Function declarations for TS_RESP defined in ts/ts_rsp_print.c */

int TS_RESP_print_bio(BIO *bio, TS_RESP *a);
int TS_STATUS_INFO_print_bio(BIO *bio, TS_STATUS_INFO *a);
int TS_TST_INFO_print_bio(BIO *bio, TS_TST_INFO *a);

/* Common utility functions defined in ts/ts_lib.c */

int TS_ASN1_INTEGER_print_bio(BIO *bio, const ASN1_INTEGER *num);
int TS_OBJ_print_bio(BIO *bio, const ASN1_OBJECT *obj);
int TS_ext_print_bio(BIO *bio, const STACK_OF(X509_EXTENSION) *extensions);
int TS_X509_ALGOR_print_bio(BIO *bio, const X509_ALGOR *alg);
int TS_MSG_IMPRINT_print_bio(BIO *bio, TS_MSG_IMPRINT *msg);

/* Function declarations for handling configuration options,
   defined in ts/ts_conf.c */

X509 *TS_CONF_load_cert(const char *file);
STACK_OF(X509) *TS_CONF_load_certs(const char *file);
EVP_PKEY *TS_CONF_load_key(const char *file, const char *pass);
const char *TS_CONF_get_tsa_section(CONF *conf, const char *section);
int TS_CONF_set_serial(CONF *conf, const char *section, TS_serial_cb cb,
    TS_RESP_CTX *ctx);
int TS_CONF_set_signer_cert(CONF *conf, const char *section,
    const char *cert, TS_RESP_CTX *ctx);
int TS_CONF_set_certs(CONF *conf, const char *section, const char *certs,
    TS_RESP_CTX *ctx);
int TS_CONF_set_signer_key(CONF *conf, const char *section,
    const char *key, const char *pass, TS_RESP_CTX *ctx);
int TS_CONF_set_def_policy(CONF *conf, const char *section,
    const char *policy, TS_RESP_CTX *ctx);
int TS_CONF_set_policies(CONF *conf, const char *section, TS_RESP_CTX *ctx);
int TS_CONF_set_digests(CONF *conf, const char *section, TS_RESP_CTX *ctx);
int TS_CONF_set_accuracy(CONF *conf, const char *section, TS_RESP_CTX *ctx);
int TS_CONF_set_clock_precision_digits(CONF *conf, const char *section,
    TS_RESP_CTX *ctx);
int TS_CONF_set_ordering(CONF *conf, const char *section, TS_RESP_CTX *ctx);
int TS_CONF_set_tsa_name(CONF *conf, const char *section, TS_RESP_CTX *ctx);
int TS_CONF_set_ess_cert_id_chain(CONF *conf, const char *section,
    TS_RESP_CTX *ctx);

void ERR_load_TS_strings(void);

/* Error codes for the TS functions. */

/* Function codes. */
#define TS_F_D2I_TS_RESP				 147
#define TS_F_DEF_SERIAL_CB				 110
#define TS_F_DEF_TIME_CB				 111
#define TS_F_ESS_ADD_SIGNING_CERT			 112
#define TS_F_ESS_CERT_ID_NEW_INIT			 113
#define TS_F_ESS_SIGNING_CERT_NEW_INIT			 114
#define TS_F_INT_TS_RESP_VERIFY_TOKEN			 149
#define TS_F_PKCS7_TO_TS_TST_INFO			 148
#define TS_F_TS_ACCURACY_SET_MICROS			 115
#define TS_F_TS_ACCURACY_SET_MILLIS			 116
#define TS_F_TS_ACCURACY_SET_SECONDS			 117
#define TS_F_TS_CHECK_IMPRINTS				 100
#define TS_F_TS_CHECK_NONCES				 101
#define TS_F_TS_CHECK_POLICY				 102
#define TS_F_TS_CHECK_SIGNING_CERTS			 103
#define TS_F_TS_CHECK_STATUS_INFO			 104
#define TS_F_TS_COMPUTE_IMPRINT				 145
#define TS_F_TS_CONF_SET_DEFAULT_ENGINE			 146
#define TS_F_TS_GET_STATUS_TEXT				 105
#define TS_F_TS_MSG_IMPRINT_SET_ALGO			 118
#define TS_F_TS_REQ_SET_MSG_IMPRINT			 119
#define TS_F_TS_REQ_SET_NONCE				 120
#define TS_F_TS_REQ_SET_POLICY_ID			 121
#define TS_F_TS_RESP_CREATE_RESPONSE			 122
#define TS_F_TS_RESP_CREATE_TST_INFO			 123
#define TS_F_TS_RESP_CTX_ADD_FAILURE_INFO		 124
#define TS_F_TS_RESP_CTX_ADD_MD				 125
#define TS_F_TS_RESP_CTX_ADD_POLICY			 126
#define TS_F_TS_RESP_CTX_NEW				 127
#define TS_F_TS_RESP_CTX_SET_ACCURACY			 128
#define TS_F_TS_RESP_CTX_SET_CERTS			 129
#define TS_F_TS_RESP_CTX_SET_DEF_POLICY			 130
#define TS_F_TS_RESP_CTX_SET_SIGNER_CERT		 131
#define TS_F_TS_RESP_CTX_SET_STATUS_INFO		 132
#define TS_F_TS_RESP_GET_POLICY				 133
#define TS_F_TS_RESP_SET_GENTIME_WITH_PRECISION		 134
#define TS_F_TS_RESP_SET_STATUS_INFO			 135
#define TS_F_TS_RESP_SET_TST_INFO			 150
#define TS_F_TS_RESP_SIGN				 136
#define TS_F_TS_RESP_VERIFY_SIGNATURE			 106
#define TS_F_TS_RESP_VERIFY_TOKEN			 107
#define TS_F_TS_TST_INFO_SET_ACCURACY			 137
#define TS_F_TS_TST_INFO_SET_MSG_IMPRINT		 138
#define TS_F_TS_TST_INFO_SET_NONCE			 139
#define TS_F_TS_TST_INFO_SET_POLICY_ID			 140
#define TS_F_TS_TST_INFO_SET_SERIAL			 141
#define TS_F_TS_TST_INFO_SET_TIME			 142
#define TS_F_TS_TST_INFO_SET_TSA			 143
#define TS_F_TS_VERIFY					 108
#define TS_F_TS_VERIFY_CERT				 109
#define TS_F_TS_VERIFY_CTX_NEW				 144

/* Reason codes. */
#define TS_R_BAD_PKCS7_TYPE				 132
#define TS_R_BAD_TYPE					 133
#define TS_R_CERTIFICATE_VERIFY_ERROR			 100
#define TS_R_COULD_NOT_SET_ENGINE			 127
#define TS_R_COULD_NOT_SET_TIME				 115
#define TS_R_D2I_TS_RESP_INT_FAILED			 128
#define TS_R_DETACHED_CONTENT				 134
#define TS_R_ESS_ADD_SIGNING_CERT_ERROR			 116
#define TS_R_ESS_SIGNING_CERTIFICATE_ERROR		 101
#define TS_R_INVALID_NULL_POINTER			 102
#define TS_R_INVALID_SIGNER_CERTIFICATE_PURPOSE		 117
#define TS_R_MESSAGE_IMPRINT_MISMATCH			 103
#define TS_R_NONCE_MISMATCH				 104
#define TS_R_NONCE_NOT_RETURNED				 105
#define TS_R_NO_CONTENT					 106
#define TS_R_NO_TIME_STAMP_TOKEN			 107
#define TS_R_PKCS7_ADD_SIGNATURE_ERROR			 118
#define TS_R_PKCS7_ADD_SIGNED_ATTR_ERROR		 119
#define TS_R_PKCS7_TO_TS_TST_INFO_FAILED		 129
#define TS_R_POLICY_MISMATCH				 108
#define TS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE	 120
#define TS_R_RESPONSE_SETUP_ERROR			 121
#define TS_R_SIGNATURE_FAILURE				 109
#define TS_R_THERE_MUST_BE_ONE_SIGNER			 110
#define TS_R_TIME_SYSCALL_ERROR				 122
#define TS_R_TOKEN_NOT_PRESENT				 130
#define TS_R_TOKEN_PRESENT				 131
#define TS_R_TSA_NAME_MISMATCH				 111
#define TS_R_TSA_UNTRUSTED				 112
#define TS_R_TST_INFO_SETUP_ERROR			 123
#define TS_R_TS_DATASIGN				 124
#define TS_R_UNACCEPTABLE_POLICY			 125
#define TS_R_UNSUPPORTED_MD_ALGORITHM			 126
#define TS_R_UNSUPPORTED_VERSION			 113
#define TS_R_WRONG_CONTENT_TYPE				 114

#ifdef  __cplusplus
}
#endif
#endif
