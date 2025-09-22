/*	$OpenBSD: x509_local.h,v 1.38 2025/03/06 07:20:01 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2013.
 */
/* ====================================================================
 * Copyright (c) 2013 The OpenSSL Project.  All rights reserved.
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

#ifndef HEADER_X509_LOCAL_H
#define HEADER_X509_LOCAL_H

#include <openssl/x509v3.h>

#include "bytestring.h"

__BEGIN_HIDDEN_DECLS

#define TS_HASH_EVP		EVP_sha1()
#define TS_HASH_LEN		SHA_DIGEST_LENGTH

#define X509_CERT_HASH_EVP	EVP_sha512()
#define X509_CERT_HASH_LEN	SHA512_DIGEST_LENGTH
#define X509_CRL_HASH_EVP	EVP_sha512()
#define X509_CRL_HASH_LEN	SHA512_DIGEST_LENGTH

#define X509_TRUST_ACCEPT_ALL	-1

/* check_trust return codes */
#define X509_TRUST_TRUSTED	1
#define X509_TRUST_REJECTED	2
#define X509_TRUST_UNTRUSTED	3

int X509_check_trust(X509 *x, int id, int flags);

struct X509_val_st {
	ASN1_TIME *notBefore;
	ASN1_TIME *notAfter;
} /* X509_VAL */;

struct X509_pubkey_st {
	X509_ALGOR *algor;
	ASN1_BIT_STRING *public_key;
	EVP_PKEY *pkey;
};

struct X509_sig_st {
	X509_ALGOR *algor;
	ASN1_OCTET_STRING *digest;
} /* X509_SIG */;

struct X509_name_entry_st {
	ASN1_OBJECT *object;
	ASN1_STRING *value;
	int set;
	int size;	/* temp variable */
} /* X509_NAME_ENTRY */;

/* we always keep X509_NAMEs in 2 forms. */
struct X509_name_st {
	STACK_OF(X509_NAME_ENTRY) *entries;
	int modified;	/* true if 'bytes' needs to be built */
#ifndef OPENSSL_NO_BUFFER
	BUF_MEM *bytes;
#else
	char *bytes;
#endif
/*	unsigned long hash; Keep the hash around for lookups */
	unsigned char *canon_enc;
	int canon_enclen;
} /* X509_NAME */;

struct X509_extension_st {
	ASN1_OBJECT *object;
	ASN1_BOOLEAN critical;
	ASN1_OCTET_STRING *value;
} /* X509_EXTENSION */;

struct x509_attributes_st {
	ASN1_OBJECT *object;
	STACK_OF(ASN1_TYPE) *set;
} /* X509_ATTRIBUTE */;

struct X509_req_info_st {
	ASN1_ENCODING enc;
	ASN1_INTEGER *version;
	X509_NAME *subject;
	X509_PUBKEY *pubkey;
	/*  d=2 hl=2 l=  0 cons: cont: 00 */
	STACK_OF(X509_ATTRIBUTE) *attributes; /* [ 0 ] */
} /* X509_REQ_INFO */;

struct X509_req_st {
	X509_REQ_INFO *req_info;
	X509_ALGOR *sig_alg;
	ASN1_BIT_STRING *signature;
	int references;
} /* X509_REQ */;

/*
 * This stuff is certificate "auxiliary info" it contains details which are
 * useful in certificate stores and databases. When used this is tagged onto
 * the end of the certificate itself.
 */
typedef struct x509_cert_aux_st {
	STACK_OF(ASN1_OBJECT) *trust;		/* trusted uses */
	STACK_OF(ASN1_OBJECT) *reject;		/* rejected uses */
	ASN1_UTF8STRING *alias;			/* "friendly name" */
	ASN1_OCTET_STRING *keyid;		/* key id of private key */
	STACK_OF(X509_ALGOR) *other;		/* other unspecified info */
} X509_CERT_AUX;

X509_CERT_AUX *X509_CERT_AUX_new(void);
void X509_CERT_AUX_free(X509_CERT_AUX *a);
X509_CERT_AUX *d2i_X509_CERT_AUX(X509_CERT_AUX **a, const unsigned char **in, long len);
int i2d_X509_CERT_AUX(X509_CERT_AUX *a, unsigned char **out);
extern const ASN1_ITEM X509_CERT_AUX_it;
int X509_CERT_AUX_print(BIO *bp,X509_CERT_AUX *x, int indent);

struct x509_cinf_st {
	ASN1_INTEGER *version;		/* [ 0 ] default of v1 */
	ASN1_INTEGER *serialNumber;
	X509_ALGOR *signature;
	X509_NAME *issuer;
	X509_VAL *validity;
	X509_NAME *subject;
	X509_PUBKEY *key;
	ASN1_BIT_STRING *issuerUID;		/* [ 1 ] optional in v2 */
	ASN1_BIT_STRING *subjectUID;		/* [ 2 ] optional in v2 */
	STACK_OF(X509_EXTENSION) *extensions;	/* [ 3 ] optional in v3 */
	ASN1_ENCODING enc;
} /* X509_CINF */;

struct x509_st {
	X509_CINF *cert_info;
	X509_ALGOR *sig_alg;
	ASN1_BIT_STRING *signature;
	int references;
	CRYPTO_EX_DATA ex_data;
	/* These contain copies of various extension values */
	long ex_pathlen;
	unsigned long ex_flags;
	unsigned long ex_kusage;
	unsigned long ex_xkusage;
	unsigned long ex_nscert;
	ASN1_OCTET_STRING *skid;
	AUTHORITY_KEYID *akid;
	STACK_OF(DIST_POINT) *crldp;
	STACK_OF(GENERAL_NAME) *altname;
	NAME_CONSTRAINTS *nc;
#ifndef OPENSSL_NO_RFC3779
	STACK_OF(IPAddressFamily) *rfc3779_addr;
	ASIdentifiers *rfc3779_asid;
#endif
	unsigned char hash[X509_CERT_HASH_LEN];
	X509_CERT_AUX *aux;
} /* X509 */;

struct x509_revoked_st {
	ASN1_INTEGER *serialNumber;
	ASN1_TIME *revocationDate;
	STACK_OF(X509_EXTENSION) /* optional */ *extensions;
	/* Set up if indirect CRL */
	STACK_OF(GENERAL_NAME) *issuer;
	/* Revocation reason */
	int reason;
	int sequence; /* load sequence */
};

struct X509_crl_info_st {
	ASN1_INTEGER *version;
	X509_ALGOR *sig_alg;
	X509_NAME *issuer;
	ASN1_TIME *lastUpdate;
	ASN1_TIME *nextUpdate;
	STACK_OF(X509_REVOKED) *revoked;
	STACK_OF(X509_EXTENSION) /* [0] */ *extensions;
	ASN1_ENCODING enc;
} /* X509_CRL_INFO */;

struct X509_crl_st {
	/* actual signature */
	X509_CRL_INFO *crl;
	X509_ALGOR *sig_alg;
	ASN1_BIT_STRING *signature;
	int references;
	int flags;
	/* Copies of various extensions */
	AUTHORITY_KEYID *akid;
	ISSUING_DIST_POINT *idp;
	/* Convenient breakdown of IDP */
	int idp_flags;
	int idp_reasons;
	/* CRL and base CRL numbers for delta processing */
	ASN1_INTEGER *crl_number;
	ASN1_INTEGER *base_crl_number;
	unsigned char hash[X509_CRL_HASH_LEN];
	STACK_OF(GENERAL_NAMES) *issuers;
} /* X509_CRL */;

struct pkcs8_priv_key_info_st {
        ASN1_INTEGER *version;
        X509_ALGOR *pkeyalg;
        ASN1_OCTET_STRING *pkey;
        STACK_OF(X509_ATTRIBUTE) *attributes;
};

struct x509_object_st {
	/* one of the above types */
	int type;
	union {
		X509 *x509;
		X509_CRL *crl;
	} data;
} /* X509_OBJECT */;

struct x509_lookup_method_st {
	const char *name;
	int (*new_item)(X509_LOOKUP *ctx);
	void (*free)(X509_LOOKUP *ctx);
	int (*ctrl)(X509_LOOKUP *ctx, int cmd, const char *argc, long argl,
	    char **ret);
	int (*get_by_subject)(X509_LOOKUP *ctx, int type, X509_NAME *name,
	    X509_OBJECT *ret);
} /* X509_LOOKUP_METHOD */;

struct X509_VERIFY_PARAM_st {
	char *name;
	time_t check_time;	/* Time to use */
	unsigned long inh_flags; /* Inheritance flags */
	unsigned long flags;	/* Various verify flags */
	int purpose;		/* purpose to check untrusted certificates */
	int trust;		/* trust setting to check */
	int depth;		/* Verify depth */
	int security_level;	/* 'Security level', see SP800-57. */
	STACK_OF(ASN1_OBJECT) *policies;	/* Permissible policies */
	STACK_OF(OPENSSL_STRING) *hosts; /* Set of acceptable names */
	unsigned int hostflags;     /* Flags to control matching features */
	char *peername;             /* Matching hostname in peer certificate */
	char *email;                /* If not NULL email address to match */
	size_t emaillen;
	unsigned char *ip;          /* If not NULL IP address to match */
	size_t iplen;               /* Length of IP address */
	int poisoned;
} /* X509_VERIFY_PARAM */;

/*
 * This is used to hold everything.  It is used for all certificate
 * validation.  Once we have a certificate chain, the 'verify'
 * function is then called to actually check the cert chain.
 */
struct x509_store_st {
	/* The following is a cache of trusted certs */
	STACK_OF(X509_OBJECT) *objs;	/* Cache of all objects */

	/* These are external lookup methods */
	STACK_OF(X509_LOOKUP) *get_cert_methods;

	X509_VERIFY_PARAM *param;

	/* Callbacks for various operations */
	int (*verify)(X509_STORE_CTX *ctx);	/* called to verify a certificate */
	int (*verify_cb)(int ok,X509_STORE_CTX *ctx);	/* error callback */
	int (*check_issued)(X509_STORE_CTX *ctx, X509 *x, X509 *issuer); /* check issued */

	CRYPTO_EX_DATA ex_data;
	int references;
} /* X509_STORE */;

/* This is the functions plus an instance of the local variables. */
struct x509_lookup_st {
	const X509_LOOKUP_METHOD *method;	/* the functions */
	void *method_data;		/* method data */

	X509_STORE *store_ctx;	/* who owns us */
} /* X509_LOOKUP */;

/*
 * This is used when verifying cert chains.  Since the gathering of the cert
 * chain can take some time (and has to be 'retried'), this needs to be kept
 * and passed around.
 */
struct x509_store_ctx_st {
	X509_STORE *store;
	int current_method;	/* used when looking up certs */

	/* The following are set by the caller */
	X509 *cert;		/* The cert to check */
	STACK_OF(X509) *untrusted;	/* chain of X509s - untrusted - passed in */
	STACK_OF(X509) *trusted;	/* trusted stack for use with get_issuer() */
	STACK_OF(X509_CRL) *crls;	/* set of CRLs passed in */

	X509_VERIFY_PARAM *param;

	/* Callbacks for various operations */
	int (*verify)(X509_STORE_CTX *ctx);	/* called to verify a certificate */
	int (*verify_cb)(int ok,X509_STORE_CTX *ctx);		/* error callback */
	int (*get_issuer)(X509 **issuer, X509_STORE_CTX *ctx, X509 *x);	/* get issuers cert from ctx */
	int (*check_issued)(X509_STORE_CTX *ctx, X509 *x, X509 *issuer); /* check issued */

	/* The following is built up */
	int valid;		/* if 0, rebuild chain */
	int num_untrusted;	/* number of untrusted certs in chain */
	STACK_OF(X509) *chain;		/* chain of X509s - built up and trusted */

	int explicit_policy;	/* Require explicit policy value */

	/* When something goes wrong, this is why */
	int error_depth;
	int error;
	X509 *current_cert;
	X509 *current_issuer;	/* cert currently being tested as valid issuer */
	X509_CRL *current_crl;	/* current CRL */

	int current_crl_score;  /* score of current CRL */
	unsigned int current_reasons;  /* Reason mask */

	X509_STORE_CTX *parent; /* For CRL path validation: parent context */

	CRYPTO_EX_DATA ex_data;
} /* X509_STORE_CTX */;

int x509_check_cert_time(X509_STORE_CTX *ctx, X509 *x, int quiet);

int name_cmp(const char *name, const char *cmp);

int X509_ALGOR_set_evp_md(X509_ALGOR *alg, const EVP_MD *md);
int X509_ALGOR_set0_by_nid(X509_ALGOR *alg, int nid, int parameter_type,
    void *parameter_value);

int X509_policy_check(const STACK_OF(X509) *certs,
    const STACK_OF(ASN1_OBJECT) *user_policies, unsigned long flags,
    X509 **out_current_cert);

PBEPARAM *PBEPARAM_new(void);
void PBEPARAM_free(PBEPARAM *a);
PBEPARAM *d2i_PBEPARAM(PBEPARAM **a, const unsigned char **in, long len);
int i2d_PBEPARAM(PBEPARAM *a, unsigned char **out);

/* Password based encryption V2 structures */
typedef struct PBE2PARAM_st {
	X509_ALGOR *keyfunc;
	X509_ALGOR *encryption;
} PBE2PARAM;

PBE2PARAM *PBE2PARAM_new(void);
void PBE2PARAM_free(PBE2PARAM *a);
PBE2PARAM *d2i_PBE2PARAM(PBE2PARAM **a, const unsigned char **in, long len);
int i2d_PBE2PARAM(PBE2PARAM *a, unsigned char **out);
extern const ASN1_ITEM PBE2PARAM_it;

typedef struct PBKDF2PARAM_st {
	/* Usually OCTET STRING but could be anything */
	ASN1_TYPE *salt;
	ASN1_INTEGER *iter;
	ASN1_INTEGER *keylength;
	X509_ALGOR *prf;
} PBKDF2PARAM;

PBKDF2PARAM *PBKDF2PARAM_new(void);
void PBKDF2PARAM_free(PBKDF2PARAM *a);
PBKDF2PARAM *d2i_PBKDF2PARAM(PBKDF2PARAM **a, const unsigned char **in, long len);
int i2d_PBKDF2PARAM(PBKDF2PARAM *a, unsigned char **out);
extern const ASN1_ITEM PBKDF2PARAM_it;

int PKCS5_pbe_set0_algor(X509_ALGOR *algor, int alg, int iter,
    const unsigned char *salt, int saltlen);
X509_ALGOR *PKCS5_pbe2_set(const EVP_CIPHER *cipher, int iter,
    unsigned char *salt, int saltlen);
X509_ALGOR *PKCS5_pbe_set(int alg, int iter, const unsigned char *salt,
    int saltlen);
X509_ALGOR *PKCS5_pbkdf2_set(int iter, unsigned char *salt, int saltlen,
    int prf_nid, int keylen);

int X509_PURPOSE_get_by_id(int id);
int X509_PURPOSE_get_trust(const X509_PURPOSE *xp);

int X509at_get_attr_by_NID(const STACK_OF(X509_ATTRIBUTE) *x, int nid,
    int lastpos);
int X509at_get_attr_by_OBJ(const STACK_OF(X509_ATTRIBUTE) *sk,
    const ASN1_OBJECT *obj, int lastpos);
STACK_OF(X509_ATTRIBUTE) *X509at_add1_attr(STACK_OF(X509_ATTRIBUTE) **x,
    X509_ATTRIBUTE *attr);
STACK_OF(X509_ATTRIBUTE) *X509at_add1_attr_by_OBJ(STACK_OF(X509_ATTRIBUTE) **x,
    const ASN1_OBJECT *obj, int type, const unsigned char *bytes, int len);
STACK_OF(X509_ATTRIBUTE) *X509at_add1_attr_by_NID(STACK_OF(X509_ATTRIBUTE) **x,
    int nid, int type, const unsigned char *bytes, int len);
STACK_OF(X509_ATTRIBUTE) *X509at_add1_attr_by_txt(STACK_OF(X509_ATTRIBUTE) **x,
    const char *attrname, int type, const unsigned char *bytes, int len);
void *X509at_get0_data_by_OBJ(STACK_OF(X509_ATTRIBUTE) *x,
    const ASN1_OBJECT *obj, int lastpos, int type);

int X509_NAME_ENTRY_add_cbb(CBB *cbb, const X509_NAME_ENTRY *ne);

int X509V3_add_value(const char *name, const char *value,
    STACK_OF(CONF_VALUE) **extlist);
int X509V3_add_value_uchar(const char *name, const unsigned char *value,
    STACK_OF(CONF_VALUE) **extlist);
int X509V3_add_value_bool(const char *name, int asn1_bool,
    STACK_OF(CONF_VALUE) **extlist);
int X509V3_add_value_int(const char *name, const ASN1_INTEGER *aint,
    STACK_OF(CONF_VALUE) **extlist);

int X509V3_get_value_bool(const CONF_VALUE *value, int *asn1_bool);
int X509V3_get_value_int(const CONF_VALUE *value, ASN1_INTEGER **aint);

STACK_OF(CONF_VALUE) *X509V3_get0_section(X509V3_CTX *ctx, const char *section);

const X509V3_EXT_METHOD *x509v3_ext_method_authority_key_identifier(void);
const X509V3_EXT_METHOD *x509v3_ext_method_basic_constraints(void);
const X509V3_EXT_METHOD *x509v3_ext_method_certificate_issuer(void);
const X509V3_EXT_METHOD *x509v3_ext_method_certificate_policies(void);
const X509V3_EXT_METHOD *x509v3_ext_method_crl_distribution_points(void);
const X509V3_EXT_METHOD *x509v3_ext_method_crl_number(void);
const X509V3_EXT_METHOD *x509v3_ext_method_crl_reason(void);
const X509V3_EXT_METHOD *x509v3_ext_method_ct_cert_scts(void);
const X509V3_EXT_METHOD *x509v3_ext_method_ct_precert_poison(void);
const X509V3_EXT_METHOD *x509v3_ext_method_ct_precert_scts(void);
const X509V3_EXT_METHOD *x509v3_ext_method_delta_crl(void);
const X509V3_EXT_METHOD *x509v3_ext_method_ext_key_usage(void);
const X509V3_EXT_METHOD *x509v3_ext_method_freshest_crl(void);
const X509V3_EXT_METHOD *x509v3_ext_method_hold_instruction_code(void);
const X509V3_EXT_METHOD *x509v3_ext_method_id_pkix_OCSP_CrlID(void);
const X509V3_EXT_METHOD *x509v3_ext_method_id_pkix_OCSP_Nonce(void);
const X509V3_EXT_METHOD *x509v3_ext_method_id_pkix_OCSP_acceptableResponses(void);
const X509V3_EXT_METHOD *x509v3_ext_method_id_pkix_OCSP_archiveCutoff(void);
const X509V3_EXT_METHOD *x509v3_ext_method_id_pkix_OCSP_serviceLocator(void);
const X509V3_EXT_METHOD *x509v3_ext_method_info_access(void);
const X509V3_EXT_METHOD *x509v3_ext_method_inhibit_any_policy(void);
const X509V3_EXT_METHOD *x509v3_ext_method_invalidity_date(void);
const X509V3_EXT_METHOD *x509v3_ext_method_issuer_alt_name(void);
const X509V3_EXT_METHOD *x509v3_ext_method_issuing_distribution_point(void);
const X509V3_EXT_METHOD *x509v3_ext_method_key_usage(void);
const X509V3_EXT_METHOD *x509v3_ext_method_name_constraints(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_base_url(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_ca_policy_url(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_ca_revocation_url(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_cert_type(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_comment(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_renewal_url(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_revocation_url(void);
const X509V3_EXT_METHOD *x509v3_ext_method_netscape_ssl_server_name(void);
const X509V3_EXT_METHOD *x509v3_ext_method_policy_constraints(void);
const X509V3_EXT_METHOD *x509v3_ext_method_policy_mappings(void);
const X509V3_EXT_METHOD *x509v3_ext_method_private_key_usage_period(void);
const X509V3_EXT_METHOD *x509v3_ext_method_sbgp_ipAddrBlock(void);
const X509V3_EXT_METHOD *x509v3_ext_method_sbgp_autonomousSysNum(void);
const X509V3_EXT_METHOD *x509v3_ext_method_sinfo_access(void);
const X509V3_EXT_METHOD *x509v3_ext_method_subject_alt_name(void);
const X509V3_EXT_METHOD *x509v3_ext_method_subject_key_identifier(void);

__END_HIDDEN_DECLS

#endif /* !HEADER_X509_LOCAL_H */
