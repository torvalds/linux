/* $OpenBSD: ocsp.h,v 1.20 2022/07/12 14:42:49 kn Exp $ */
/* Written by Tom Titchener <Tom_Titchener@groove.net> for the OpenSSL
 * project. */

/* History:
   This file was transfered to Richard Levitte from CertCo by Kathy
   Weinhold in mid-spring 2000 to be included in OpenSSL or released
   as a patch kit. */

/* ====================================================================
 * Copyright (c) 1998-2000 The OpenSSL Project.  All rights reserved.
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

#ifndef HEADER_OCSP_H
#define HEADER_OCSP_H

#include <openssl/ossl_typ.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/safestack.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*
 *   CRLReason ::= ENUMERATED {
 *        unspecified             (0),
 *        keyCompromise           (1),
 *        cACompromise            (2),
 *        affiliationChanged      (3),
 *        superseded              (4),
 *        cessationOfOperation    (5),
 *        certificateHold         (6),
 *        removeFromCRL           (8) }
 */
#define OCSP_REVOKED_STATUS_NOSTATUS			-1
#define OCSP_REVOKED_STATUS_UNSPECIFIED			0
#define OCSP_REVOKED_STATUS_KEYCOMPROMISE		1
#define OCSP_REVOKED_STATUS_CACOMPROMISE		2
#define OCSP_REVOKED_STATUS_AFFILIATIONCHANGED		3
#define OCSP_REVOKED_STATUS_SUPERSEDED			4
#define OCSP_REVOKED_STATUS_CESSATIONOFOPERATION	5
#define OCSP_REVOKED_STATUS_CERTIFICATEHOLD		6
#define OCSP_REVOKED_STATUS_REMOVEFROMCRL		8


/* Various flags and values */

#define OCSP_DEFAULT_NONCE_LENGTH	16

#define OCSP_NOCERTS			0x1
#define OCSP_NOINTERN			0x2
#define OCSP_NOSIGS			0x4
#define OCSP_NOCHAIN			0x8
#define OCSP_NOVERIFY			0x10
#define OCSP_NOEXPLICIT			0x20
#define OCSP_NOCASIGN			0x40
#define OCSP_NODELEGATED		0x80
#define OCSP_NOCHECKS			0x100
#define OCSP_TRUSTOTHER			0x200
#define OCSP_RESPID_KEY			0x400
#define OCSP_NOTIME			0x800

typedef struct ocsp_cert_id_st OCSP_CERTID;

DECLARE_STACK_OF(OCSP_CERTID)

typedef struct ocsp_one_request_st OCSP_ONEREQ;

DECLARE_STACK_OF(OCSP_ONEREQ)

typedef struct ocsp_req_info_st OCSP_REQINFO;
typedef struct ocsp_signature_st OCSP_SIGNATURE;
typedef struct ocsp_request_st OCSP_REQUEST;

#define OCSP_RESPONSE_STATUS_SUCCESSFUL		0
#define OCSP_RESPONSE_STATUS_MALFORMEDREQUEST	1
#define OCSP_RESPONSE_STATUS_INTERNALERROR	2
#define OCSP_RESPONSE_STATUS_TRYLATER		3
#define OCSP_RESPONSE_STATUS_SIGREQUIRED	5
#define OCSP_RESPONSE_STATUS_UNAUTHORIZED	6

typedef struct ocsp_resp_bytes_st OCSP_RESPBYTES;

#define V_OCSP_RESPID_NAME 0
#define V_OCSP_RESPID_KEY  1

DECLARE_STACK_OF(OCSP_RESPID)

OCSP_RESPID *OCSP_RESPID_new(void);
void OCSP_RESPID_free(OCSP_RESPID *a);
OCSP_RESPID *d2i_OCSP_RESPID(OCSP_RESPID **a, const unsigned char **in, long len);
int i2d_OCSP_RESPID(OCSP_RESPID *a, unsigned char **out);
extern const ASN1_ITEM OCSP_RESPID_it;

typedef struct ocsp_revoked_info_st OCSP_REVOKEDINFO;

#define V_OCSP_CERTSTATUS_GOOD    0
#define V_OCSP_CERTSTATUS_REVOKED 1
#define V_OCSP_CERTSTATUS_UNKNOWN 2

typedef struct ocsp_cert_status_st OCSP_CERTSTATUS;
typedef struct ocsp_single_response_st OCSP_SINGLERESP;

DECLARE_STACK_OF(OCSP_SINGLERESP)

typedef struct ocsp_response_data_st OCSP_RESPDATA;

typedef struct ocsp_basic_response_st OCSP_BASICRESP;

typedef struct ocsp_crl_id_st OCSP_CRLID;
typedef struct ocsp_service_locator_st OCSP_SERVICELOC;

#define PEM_STRING_OCSP_REQUEST	"OCSP REQUEST"
#define PEM_STRING_OCSP_RESPONSE "OCSP RESPONSE"

#define	PEM_read_bio_OCSP_REQUEST(bp,x,cb) \
    (OCSP_REQUEST *)PEM_ASN1_read_bio((char *(*)())d2i_OCSP_REQUEST, \
	PEM_STRING_OCSP_REQUEST,bp,(char **)x,cb,NULL)

#define	PEM_read_bio_OCSP_RESPONSE(bp,x,cb) \
    (OCSP_RESPONSE *)PEM_ASN1_read_bio((char *(*)())d2i_OCSP_RESPONSE, \
	PEM_STRING_OCSP_RESPONSE,bp,(char **)x,cb,NULL)

#define PEM_write_bio_OCSP_REQUEST(bp,o) \
    PEM_ASN1_write_bio((int (*)())i2d_OCSP_REQUEST,PEM_STRING_OCSP_REQUEST,\
	bp,(char *)o, NULL,NULL,0,NULL,NULL)

#define PEM_write_bio_OCSP_RESPONSE(bp,o) \
    PEM_ASN1_write_bio((int (*)())i2d_OCSP_RESPONSE,PEM_STRING_OCSP_RESPONSE,\
	bp,(char *)o, NULL,NULL,0,NULL,NULL)

#define ASN1_BIT_STRING_digest(data,type,md,len) \
    ASN1_item_digest(&ASN1_BIT_STRING_it,type,data,md,len)

#define OCSP_CERTSTATUS_dup(cs) \
	ASN1_item_dup(&OCSP_CERTSTATUS_it, cs)

OCSP_CERTID *OCSP_CERTID_dup(OCSP_CERTID *id);

OCSP_RESPONSE *OCSP_sendreq_bio(BIO *b, const char *path, OCSP_REQUEST *req);
OCSP_REQ_CTX *OCSP_sendreq_new(BIO *io, const char *path, OCSP_REQUEST *req,
	    int maxline);
int	OCSP_sendreq_nbio(OCSP_RESPONSE **presp, OCSP_REQ_CTX *rctx);
void	OCSP_REQ_CTX_free(OCSP_REQ_CTX *rctx);
int	OCSP_REQ_CTX_set1_req(OCSP_REQ_CTX *rctx, OCSP_REQUEST *req);
int	OCSP_REQ_CTX_add1_header(OCSP_REQ_CTX *rctx, const char *name,
	    const char *value);

OCSP_CERTID *OCSP_cert_to_id(const EVP_MD *dgst, const X509 *subject,
	    const X509 *issuer);

OCSP_CERTID *OCSP_cert_id_new(const EVP_MD *dgst, const X509_NAME *issuerName,
	    const ASN1_BIT_STRING *issuerKey, const ASN1_INTEGER *serialNumber);

OCSP_ONEREQ *OCSP_request_add0_id(OCSP_REQUEST *req, OCSP_CERTID *cid);

int	OCSP_request_add1_nonce(OCSP_REQUEST *req, unsigned char *val, int len);
int	OCSP_basic_add1_nonce(OCSP_BASICRESP *resp, unsigned char *val, int len);
int	OCSP_check_nonce(OCSP_REQUEST *req, OCSP_BASICRESP *bs);
int	OCSP_copy_nonce(OCSP_BASICRESP *resp, OCSP_REQUEST *req);

int	OCSP_request_set1_name(OCSP_REQUEST *req, X509_NAME *nm);
int	OCSP_request_add1_cert(OCSP_REQUEST *req, X509 *cert);

int	OCSP_request_sign(OCSP_REQUEST *req, X509 *signer, EVP_PKEY *key,
	    const EVP_MD *dgst, STACK_OF(X509) *certs, unsigned long flags);

int	OCSP_response_status(OCSP_RESPONSE *resp);
OCSP_BASICRESP *OCSP_response_get1_basic(OCSP_RESPONSE *resp);

const ASN1_OCTET_STRING *OCSP_resp_get0_signature(const OCSP_BASICRESP *bs);
const X509_ALGOR *OCSP_resp_get0_tbs_sigalg(const OCSP_BASICRESP *bs);
const OCSP_RESPDATA *OCSP_resp_get0_respdata(const OCSP_BASICRESP *bs);
int	OCSP_resp_get0_signer(OCSP_BASICRESP *bs, X509 **signer,
	    STACK_OF(X509) *extra_certs);

int	OCSP_resp_count(OCSP_BASICRESP *bs);
OCSP_SINGLERESP *OCSP_resp_get0(OCSP_BASICRESP *bs, int idx);
const ASN1_GENERALIZEDTIME *OCSP_resp_get0_produced_at(const OCSP_BASICRESP *bs);
const STACK_OF(X509) *OCSP_resp_get0_certs(const OCSP_BASICRESP *bs);
int	OCSP_resp_get0_id(const OCSP_BASICRESP *bs,
	    const ASN1_OCTET_STRING **pid, const X509_NAME **pname);

int	OCSP_resp_find(OCSP_BASICRESP *bs, OCSP_CERTID *id, int last);
int	OCSP_single_get0_status(OCSP_SINGLERESP *single, int *reason,
	    ASN1_GENERALIZEDTIME **revtime, ASN1_GENERALIZEDTIME **thisupd,
	    ASN1_GENERALIZEDTIME **nextupd);
int	OCSP_resp_find_status(OCSP_BASICRESP *bs, OCSP_CERTID *id, int *status,
	    int *reason, ASN1_GENERALIZEDTIME **revtime,
	    ASN1_GENERALIZEDTIME **thisupd, ASN1_GENERALIZEDTIME **nextupd);
int	OCSP_check_validity(ASN1_GENERALIZEDTIME *thisupd,
	    ASN1_GENERALIZEDTIME *nextupd, long sec, long maxsec);

int	OCSP_request_verify(OCSP_REQUEST *req, STACK_OF(X509) *certs,
	    X509_STORE *store, unsigned long flags);

int	OCSP_parse_url(const char *url, char **phost, char **pport,
	    char **ppath, int *pssl);

int	OCSP_id_issuer_cmp(OCSP_CERTID *a, OCSP_CERTID *b);
int	OCSP_id_cmp(OCSP_CERTID *a, OCSP_CERTID *b);

int	OCSP_request_onereq_count(OCSP_REQUEST *req);
OCSP_ONEREQ *OCSP_request_onereq_get0(OCSP_REQUEST *req, int i);
OCSP_CERTID *OCSP_onereq_get0_id(OCSP_ONEREQ *one);
int	OCSP_id_get0_info(ASN1_OCTET_STRING **piNameHash, ASN1_OBJECT **pmd,
	    ASN1_OCTET_STRING **pikeyHash, ASN1_INTEGER **pserial,
	    OCSP_CERTID *cid);
int	OCSP_request_is_signed(OCSP_REQUEST *req);
OCSP_RESPONSE *OCSP_response_create(int status, OCSP_BASICRESP *bs);
OCSP_SINGLERESP *OCSP_basic_add1_status(OCSP_BASICRESP *rsp, OCSP_CERTID *cid,
	    int status, int reason, ASN1_TIME *revtime, ASN1_TIME *thisupd,
	    ASN1_TIME *nextupd);
int	OCSP_basic_add1_cert(OCSP_BASICRESP *resp, X509 *cert);
int	OCSP_basic_sign(OCSP_BASICRESP *brsp, X509 *signer, EVP_PKEY *key,
	    const EVP_MD *dgst, STACK_OF(X509) *certs, unsigned long flags);

X509_EXTENSION *OCSP_crlID_new(const char *url, long *n, char *tim);

X509_EXTENSION *OCSP_accept_responses_new(char **oids);

X509_EXTENSION *OCSP_archive_cutoff_new(char* tim);

X509_EXTENSION *OCSP_url_svcloc_new(X509_NAME* issuer, const char **urls);

int	OCSP_REQUEST_get_ext_count(OCSP_REQUEST *x);
int	OCSP_REQUEST_get_ext_by_NID(OCSP_REQUEST *x, int nid, int lastpos);
int	OCSP_REQUEST_get_ext_by_OBJ(OCSP_REQUEST *x, const ASN1_OBJECT *obj,
	    int lastpos);
int	OCSP_REQUEST_get_ext_by_critical(OCSP_REQUEST *x, int crit,
	    int lastpos);
X509_EXTENSION *OCSP_REQUEST_get_ext(OCSP_REQUEST *x, int loc);
X509_EXTENSION *OCSP_REQUEST_delete_ext(OCSP_REQUEST *x, int loc);
void *OCSP_REQUEST_get1_ext_d2i(OCSP_REQUEST *x, int nid, int *crit, int *idx);
int	OCSP_REQUEST_add1_ext_i2d(OCSP_REQUEST *x, int nid, void *value,
	    int crit, unsigned long flags);
int	OCSP_REQUEST_add_ext(OCSP_REQUEST *x, X509_EXTENSION *ex, int loc);

int	OCSP_ONEREQ_get_ext_count(OCSP_ONEREQ *x);
int	OCSP_ONEREQ_get_ext_by_NID(OCSP_ONEREQ *x, int nid, int lastpos);
int	OCSP_ONEREQ_get_ext_by_OBJ(OCSP_ONEREQ *x, const ASN1_OBJECT *obj,
	    int lastpos);
int	OCSP_ONEREQ_get_ext_by_critical(OCSP_ONEREQ *x, int crit, int lastpos);
X509_EXTENSION *OCSP_ONEREQ_get_ext(OCSP_ONEREQ *x, int loc);
X509_EXTENSION *OCSP_ONEREQ_delete_ext(OCSP_ONEREQ *x, int loc);
void *OCSP_ONEREQ_get1_ext_d2i(OCSP_ONEREQ *x, int nid, int *crit, int *idx);
int	OCSP_ONEREQ_add1_ext_i2d(OCSP_ONEREQ *x, int nid, void *value, int crit,
	    unsigned long flags);
int	OCSP_ONEREQ_add_ext(OCSP_ONEREQ *x, X509_EXTENSION *ex, int loc);

int	OCSP_BASICRESP_get_ext_count(OCSP_BASICRESP *x);
int	OCSP_BASICRESP_get_ext_by_NID(OCSP_BASICRESP *x, int nid, int lastpos);
int	OCSP_BASICRESP_get_ext_by_OBJ(OCSP_BASICRESP *x, const ASN1_OBJECT *obj,
	    int lastpos);
int	OCSP_BASICRESP_get_ext_by_critical(OCSP_BASICRESP *x, int crit,
	    int lastpos);
X509_EXTENSION *OCSP_BASICRESP_get_ext(OCSP_BASICRESP *x, int loc);
X509_EXTENSION *OCSP_BASICRESP_delete_ext(OCSP_BASICRESP *x, int loc);
void *OCSP_BASICRESP_get1_ext_d2i(OCSP_BASICRESP *x, int nid, int *crit,
	    int *idx);
int	OCSP_BASICRESP_add1_ext_i2d(OCSP_BASICRESP *x, int nid, void *value,
	    int crit, unsigned long flags);
int	OCSP_BASICRESP_add_ext(OCSP_BASICRESP *x, X509_EXTENSION *ex, int loc);

int	OCSP_SINGLERESP_get_ext_count(OCSP_SINGLERESP *x);
int	OCSP_SINGLERESP_get_ext_by_NID(OCSP_SINGLERESP *x, int nid,
	    int lastpos);
int	OCSP_SINGLERESP_get_ext_by_OBJ(OCSP_SINGLERESP *x,
	    const ASN1_OBJECT *obj, int lastpos);
int	OCSP_SINGLERESP_get_ext_by_critical(OCSP_SINGLERESP *x, int crit,
	    int lastpos);
X509_EXTENSION *OCSP_SINGLERESP_get_ext(OCSP_SINGLERESP *x, int loc);
X509_EXTENSION *OCSP_SINGLERESP_delete_ext(OCSP_SINGLERESP *x, int loc);
void *OCSP_SINGLERESP_get1_ext_d2i(OCSP_SINGLERESP *x, int nid, int *crit,
	    int *idx);
int	OCSP_SINGLERESP_add1_ext_i2d(OCSP_SINGLERESP *x, int nid, void *value,
	    int crit, unsigned long flags);
int	OCSP_SINGLERESP_add_ext(OCSP_SINGLERESP *x, X509_EXTENSION *ex,
	    int loc);
const OCSP_CERTID *OCSP_SINGLERESP_get0_id(const OCSP_SINGLERESP *x);

OCSP_SINGLERESP *OCSP_SINGLERESP_new(void);
void OCSP_SINGLERESP_free(OCSP_SINGLERESP *a);
OCSP_SINGLERESP *d2i_OCSP_SINGLERESP(OCSP_SINGLERESP **a, const unsigned char **in, long len);
int i2d_OCSP_SINGLERESP(OCSP_SINGLERESP *a, unsigned char **out);
extern const ASN1_ITEM OCSP_SINGLERESP_it;
OCSP_CERTSTATUS *OCSP_CERTSTATUS_new(void);
void OCSP_CERTSTATUS_free(OCSP_CERTSTATUS *a);
OCSP_CERTSTATUS *d2i_OCSP_CERTSTATUS(OCSP_CERTSTATUS **a, const unsigned char **in, long len);
int i2d_OCSP_CERTSTATUS(OCSP_CERTSTATUS *a, unsigned char **out);
extern const ASN1_ITEM OCSP_CERTSTATUS_it;
OCSP_REVOKEDINFO *OCSP_REVOKEDINFO_new(void);
void OCSP_REVOKEDINFO_free(OCSP_REVOKEDINFO *a);
OCSP_REVOKEDINFO *d2i_OCSP_REVOKEDINFO(OCSP_REVOKEDINFO **a, const unsigned char **in, long len);
int i2d_OCSP_REVOKEDINFO(OCSP_REVOKEDINFO *a, unsigned char **out);
extern const ASN1_ITEM OCSP_REVOKEDINFO_it;
OCSP_BASICRESP *OCSP_BASICRESP_new(void);
void OCSP_BASICRESP_free(OCSP_BASICRESP *a);
OCSP_BASICRESP *d2i_OCSP_BASICRESP(OCSP_BASICRESP **a, const unsigned char **in, long len);
int i2d_OCSP_BASICRESP(OCSP_BASICRESP *a, unsigned char **out);
extern const ASN1_ITEM OCSP_BASICRESP_it;
OCSP_RESPDATA *OCSP_RESPDATA_new(void);
void OCSP_RESPDATA_free(OCSP_RESPDATA *a);
OCSP_RESPDATA *d2i_OCSP_RESPDATA(OCSP_RESPDATA **a, const unsigned char **in, long len);
int i2d_OCSP_RESPDATA(OCSP_RESPDATA *a, unsigned char **out);
extern const ASN1_ITEM OCSP_RESPDATA_it;
OCSP_RESPID *OCSP_RESPID_new(void);
void OCSP_RESPID_free(OCSP_RESPID *a);
OCSP_RESPID *d2i_OCSP_RESPID(OCSP_RESPID **a, const unsigned char **in, long len);
int i2d_OCSP_RESPID(OCSP_RESPID *a, unsigned char **out);
extern const ASN1_ITEM OCSP_RESPID_it;
OCSP_RESPONSE *OCSP_RESPONSE_new(void);
void OCSP_RESPONSE_free(OCSP_RESPONSE *a);
OCSP_RESPONSE *d2i_OCSP_RESPONSE(OCSP_RESPONSE **a, const unsigned char **in, long len);
int i2d_OCSP_RESPONSE(OCSP_RESPONSE *a, unsigned char **out);
OCSP_RESPONSE *d2i_OCSP_RESPONSE_bio(BIO *bp, OCSP_RESPONSE **a);
int i2d_OCSP_RESPONSE_bio(BIO *bp, OCSP_RESPONSE *a);
extern const ASN1_ITEM OCSP_RESPONSE_it;
OCSP_RESPBYTES *OCSP_RESPBYTES_new(void);
void OCSP_RESPBYTES_free(OCSP_RESPBYTES *a);
OCSP_RESPBYTES *d2i_OCSP_RESPBYTES(OCSP_RESPBYTES **a, const unsigned char **in, long len);
int i2d_OCSP_RESPBYTES(OCSP_RESPBYTES *a, unsigned char **out);
extern const ASN1_ITEM OCSP_RESPBYTES_it;
OCSP_ONEREQ *OCSP_ONEREQ_new(void);
void OCSP_ONEREQ_free(OCSP_ONEREQ *a);
OCSP_ONEREQ *d2i_OCSP_ONEREQ(OCSP_ONEREQ **a, const unsigned char **in, long len);
int i2d_OCSP_ONEREQ(OCSP_ONEREQ *a, unsigned char **out);
extern const ASN1_ITEM OCSP_ONEREQ_it;
OCSP_CERTID *OCSP_CERTID_new(void);
void OCSP_CERTID_free(OCSP_CERTID *a);
OCSP_CERTID *d2i_OCSP_CERTID(OCSP_CERTID **a, const unsigned char **in, long len);
int i2d_OCSP_CERTID(OCSP_CERTID *a, unsigned char **out);
extern const ASN1_ITEM OCSP_CERTID_it;
OCSP_REQUEST *OCSP_REQUEST_new(void);
void OCSP_REQUEST_free(OCSP_REQUEST *a);
OCSP_REQUEST *d2i_OCSP_REQUEST(OCSP_REQUEST **a, const unsigned char **in, long len);
int i2d_OCSP_REQUEST(OCSP_REQUEST *a, unsigned char **out);
OCSP_REQUEST *d2i_OCSP_REQUEST_bio(BIO *bp, OCSP_REQUEST **a);
int i2d_OCSP_REQUEST_bio(BIO *bp, OCSP_REQUEST *a);
extern const ASN1_ITEM OCSP_REQUEST_it;
OCSP_SIGNATURE *OCSP_SIGNATURE_new(void);
void OCSP_SIGNATURE_free(OCSP_SIGNATURE *a);
OCSP_SIGNATURE *d2i_OCSP_SIGNATURE(OCSP_SIGNATURE **a, const unsigned char **in, long len);
int i2d_OCSP_SIGNATURE(OCSP_SIGNATURE *a, unsigned char **out);
extern const ASN1_ITEM OCSP_SIGNATURE_it;
OCSP_REQINFO *OCSP_REQINFO_new(void);
void OCSP_REQINFO_free(OCSP_REQINFO *a);
OCSP_REQINFO *d2i_OCSP_REQINFO(OCSP_REQINFO **a, const unsigned char **in, long len);
int i2d_OCSP_REQINFO(OCSP_REQINFO *a, unsigned char **out);
extern const ASN1_ITEM OCSP_REQINFO_it;
OCSP_CRLID *OCSP_CRLID_new(void);
void OCSP_CRLID_free(OCSP_CRLID *a);
OCSP_CRLID *d2i_OCSP_CRLID(OCSP_CRLID **a, const unsigned char **in, long len);
int i2d_OCSP_CRLID(OCSP_CRLID *a, unsigned char **out);
extern const ASN1_ITEM OCSP_CRLID_it;
OCSP_SERVICELOC *OCSP_SERVICELOC_new(void);
void OCSP_SERVICELOC_free(OCSP_SERVICELOC *a);
OCSP_SERVICELOC *d2i_OCSP_SERVICELOC(OCSP_SERVICELOC **a, const unsigned char **in, long len);
int i2d_OCSP_SERVICELOC(OCSP_SERVICELOC *a, unsigned char **out);
extern const ASN1_ITEM OCSP_SERVICELOC_it;

const char *OCSP_response_status_str(long s);
const char *OCSP_cert_status_str(long s);
const char *OCSP_crl_reason_str(long s);

int	OCSP_REQUEST_print(BIO *bp, OCSP_REQUEST* a, unsigned long flags);
int	OCSP_RESPONSE_print(BIO *bp, OCSP_RESPONSE* o, unsigned long flags);

int	OCSP_basic_verify(OCSP_BASICRESP *bs, STACK_OF(X509) *certs,
	    X509_STORE *st, unsigned long flags);

void ERR_load_OCSP_strings(void);

/* Error codes for the OCSP functions. */

/* Function codes. */
#define OCSP_F_ASN1_STRING_ENCODE			 100
#define OCSP_F_D2I_OCSP_NONCE				 102
#define OCSP_F_OCSP_BASIC_ADD1_STATUS			 103
#define OCSP_F_OCSP_BASIC_SIGN				 104
#define OCSP_F_OCSP_BASIC_VERIFY			 105
#define OCSP_F_OCSP_CERT_ID_NEW				 101
#define OCSP_F_OCSP_CHECK_DELEGATED			 106
#define OCSP_F_OCSP_CHECK_IDS				 107
#define OCSP_F_OCSP_CHECK_ISSUER			 108
#define OCSP_F_OCSP_CHECK_VALIDITY			 115
#define OCSP_F_OCSP_MATCH_ISSUERID			 109
#define OCSP_F_OCSP_PARSE_URL				 114
#define OCSP_F_OCSP_REQUEST_SIGN			 110
#define OCSP_F_OCSP_REQUEST_VERIFY			 116
#define OCSP_F_OCSP_RESPONSE_GET1_BASIC			 111
#define OCSP_F_OCSP_SENDREQ_BIO				 112
#define OCSP_F_OCSP_SENDREQ_NBIO			 117
#define OCSP_F_PARSE_HTTP_LINE1				 118
#define OCSP_F_REQUEST_VERIFY				 113

/* Reason codes. */
#define OCSP_R_BAD_DATA					 100
#define OCSP_R_CERTIFICATE_VERIFY_ERROR			 101
#define OCSP_R_DIGEST_ERR				 102
#define OCSP_R_ERROR_IN_NEXTUPDATE_FIELD		 122
#define OCSP_R_ERROR_IN_THISUPDATE_FIELD		 123
#define OCSP_R_ERROR_PARSING_URL			 121
#define OCSP_R_MISSING_OCSPSIGNING_USAGE		 103
#define OCSP_R_NEXTUPDATE_BEFORE_THISUPDATE		 124
#define OCSP_R_NOT_BASIC_RESPONSE			 104
#define OCSP_R_NO_CERTIFICATES_IN_CHAIN			 105
#define OCSP_R_NO_CONTENT				 106
#define OCSP_R_NO_PUBLIC_KEY				 107
#define OCSP_R_NO_RESPONSE_DATA				 108
#define OCSP_R_NO_REVOKED_TIME				 109
#define OCSP_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE	 110
#define OCSP_R_REQUEST_NOT_SIGNED			 128
#define OCSP_R_RESPONSE_CONTAINS_NO_REVOCATION_DATA	 111
#define OCSP_R_ROOT_CA_NOT_TRUSTED			 112
#define OCSP_R_SERVER_READ_ERROR			 113
#define OCSP_R_SERVER_RESPONSE_ERROR			 114
#define OCSP_R_SERVER_RESPONSE_PARSE_ERROR		 115
#define OCSP_R_SERVER_WRITE_ERROR			 116
#define OCSP_R_SIGNATURE_FAILURE			 117
#define OCSP_R_SIGNER_CERTIFICATE_NOT_FOUND		 118
#define OCSP_R_STATUS_EXPIRED				 125
#define OCSP_R_STATUS_NOT_YET_VALID			 126
#define OCSP_R_STATUS_TOO_OLD				 127
#define OCSP_R_UNKNOWN_MESSAGE_DIGEST			 119
#define OCSP_R_UNKNOWN_NID				 120
#define OCSP_R_UNSUPPORTED_REQUESTORNAME_TYPE		 129

#ifdef  __cplusplus
}
#endif
#endif
