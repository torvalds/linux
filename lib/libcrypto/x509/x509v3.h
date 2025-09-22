/* $OpenBSD: x509v3.h,v 1.40 2024/12/23 09:57:23 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999-2004 The OpenSSL Project.  All rights reserved.
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
#ifndef HEADER_X509V3_H
#define HEADER_X509V3_H

#include <openssl/opensslconf.h>

#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/conf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward reference */
struct v3_ext_method;
struct v3_ext_ctx;

/* Useful typedefs */

typedef void * (*X509V3_EXT_NEW)(void);
typedef void (*X509V3_EXT_FREE)(void *);
typedef void * (*X509V3_EXT_D2I)(void *, const unsigned char ** , long);
typedef int (*X509V3_EXT_I2D)(void *, unsigned char **);
typedef STACK_OF(CONF_VALUE) *
  (*X509V3_EXT_I2V)(const struct v3_ext_method *method, void *ext,
		    STACK_OF(CONF_VALUE) *extlist);
typedef void * (*X509V3_EXT_V2I)(const struct v3_ext_method *method,
				 struct v3_ext_ctx *ctx,
				 STACK_OF(CONF_VALUE) *values);
typedef char * (*X509V3_EXT_I2S)(const struct v3_ext_method *method, void *ext);
typedef void * (*X509V3_EXT_S2I)(const struct v3_ext_method *method,
				 struct v3_ext_ctx *ctx, const char *str);
typedef int (*X509V3_EXT_I2R)(const struct v3_ext_method *method, void *ext,
			      BIO *out, int indent);
typedef void * (*X509V3_EXT_R2I)(const struct v3_ext_method *method,
				 struct v3_ext_ctx *ctx, const char *str);

/* V3 extension structure */

struct v3_ext_method {
	int ext_nid;
	int ext_flags;
	/* If this is set the following four fields are ignored */
	ASN1_ITEM_EXP *it;
	/* Old style ASN1 calls */
	X509V3_EXT_NEW ext_new;
	X509V3_EXT_FREE ext_free;
	X509V3_EXT_D2I d2i;
	X509V3_EXT_I2D i2d;

	/* The following pair is used for string extensions */
	X509V3_EXT_I2S i2s;
	X509V3_EXT_S2I s2i;

	/* The following pair is used for multi-valued extensions */
	X509V3_EXT_I2V i2v;
	X509V3_EXT_V2I v2i;

	/* The following are used for raw extensions */
	X509V3_EXT_I2R i2r;
	X509V3_EXT_R2I r2i;

	const void *usr_data;	/* Any extension specific data */
};

struct v3_ext_ctx {
	#define CTX_TEST 0x1
	int flags;
	X509 *issuer_cert;
	X509 *subject_cert;
	X509_REQ *subject_req;
	X509_CRL *crl;
	void *db;
};

typedef struct v3_ext_method X509V3_EXT_METHOD;

DECLARE_STACK_OF(X509V3_EXT_METHOD)

/* XXX - can this be made internal? */
#define X509V3_EXT_MULTILINE	0x4

/* XXX - remove it anyway? */
/* Guess who uses this... Yes, of course, it's xca. */
typedef BIT_STRING_BITNAME ENUMERATED_NAMES;

typedef struct BASIC_CONSTRAINTS_st {
	int ca;
	ASN1_INTEGER *pathlen;
} BASIC_CONSTRAINTS;


typedef struct PKEY_USAGE_PERIOD_st {
	ASN1_GENERALIZEDTIME *notBefore;
	ASN1_GENERALIZEDTIME *notAfter;
} PKEY_USAGE_PERIOD;

typedef struct otherName_st {
	ASN1_OBJECT *type_id;
	ASN1_TYPE *value;
} OTHERNAME;

typedef struct EDIPartyName_st {
	ASN1_STRING *nameAssigner;
	ASN1_STRING *partyName;
} EDIPARTYNAME;

typedef struct GENERAL_NAME_st {

	#define GEN_OTHERNAME	0
	#define GEN_EMAIL	1
	#define GEN_DNS		2
	#define GEN_X400	3
	#define GEN_DIRNAME	4
	#define GEN_EDIPARTY	5
	#define GEN_URI		6
	#define GEN_IPADD	7
	#define GEN_RID		8

	int type;
	union {
		char *ptr;
		OTHERNAME *otherName; /* otherName */
		ASN1_IA5STRING *rfc822Name;
		ASN1_IA5STRING *dNSName;
		ASN1_STRING *x400Address;
		X509_NAME *directoryName;
		EDIPARTYNAME *ediPartyName;
		ASN1_IA5STRING *uniformResourceIdentifier;
		ASN1_OCTET_STRING *iPAddress;
		ASN1_OBJECT *registeredID;

		/* Old names */
		ASN1_OCTET_STRING *ip; /* iPAddress */
		X509_NAME *dirn;		/* dirn */
		ASN1_IA5STRING *ia5; /* rfc822Name, dNSName, uniformResourceIdentifier */
		ASN1_OBJECT *rid; /* registeredID */
	} d;
} GENERAL_NAME;

typedef struct ACCESS_DESCRIPTION_st {
	ASN1_OBJECT *method;
	GENERAL_NAME *location;
} ACCESS_DESCRIPTION;

typedef STACK_OF(ACCESS_DESCRIPTION) AUTHORITY_INFO_ACCESS;

typedef STACK_OF(ASN1_OBJECT) EXTENDED_KEY_USAGE;

DECLARE_STACK_OF(GENERAL_NAME)

typedef STACK_OF(GENERAL_NAME) GENERAL_NAMES;
DECLARE_STACK_OF(GENERAL_NAMES)

DECLARE_STACK_OF(ACCESS_DESCRIPTION)

typedef struct DIST_POINT_NAME_st {
	int type;
	union {
		GENERAL_NAMES *fullname;
		STACK_OF(X509_NAME_ENTRY) *relativename;
	} name;
	/* If relativename then this contains the full distribution point name */
	X509_NAME *dpname;
} DIST_POINT_NAME;
/* All existing reasons */
#define CRLDP_ALL_REASONS	0x807f

#define CRL_REASON_NONE				-1
#define CRL_REASON_UNSPECIFIED			0
#define CRL_REASON_KEY_COMPROMISE		1
#define CRL_REASON_CA_COMPROMISE		2
#define CRL_REASON_AFFILIATION_CHANGED		3
#define CRL_REASON_SUPERSEDED			4
#define CRL_REASON_CESSATION_OF_OPERATION	5
#define CRL_REASON_CERTIFICATE_HOLD		6
#define CRL_REASON_REMOVE_FROM_CRL		8
#define CRL_REASON_PRIVILEGE_WITHDRAWN		9
#define CRL_REASON_AA_COMPROMISE		10

struct DIST_POINT_st {
	DIST_POINT_NAME	*distpoint;
	ASN1_BIT_STRING *reasons;
	GENERAL_NAMES *CRLissuer;
	int dp_reasons;
};

typedef STACK_OF(DIST_POINT) CRL_DIST_POINTS;

DECLARE_STACK_OF(DIST_POINT)

struct AUTHORITY_KEYID_st {
	ASN1_OCTET_STRING *keyid;
	GENERAL_NAMES *issuer;
	ASN1_INTEGER *serial;
};

typedef struct NOTICEREF_st {
	ASN1_STRING *organization;
	STACK_OF(ASN1_INTEGER) *noticenos;
} NOTICEREF;

typedef struct USERNOTICE_st {
	NOTICEREF *noticeref;
	ASN1_STRING *exptext;
} USERNOTICE;

typedef struct POLICYQUALINFO_st {
	ASN1_OBJECT *pqualid;
	union {
		ASN1_IA5STRING *cpsuri;
		USERNOTICE *usernotice;
		ASN1_TYPE *other;
	} d;
} POLICYQUALINFO;

DECLARE_STACK_OF(POLICYQUALINFO)

typedef struct POLICYINFO_st {
	ASN1_OBJECT *policyid;
	STACK_OF(POLICYQUALINFO) *qualifiers;
} POLICYINFO;

typedef STACK_OF(POLICYINFO) CERTIFICATEPOLICIES;

DECLARE_STACK_OF(POLICYINFO)

typedef struct POLICY_MAPPING_st {
	ASN1_OBJECT *issuerDomainPolicy;
	ASN1_OBJECT *subjectDomainPolicy;
} POLICY_MAPPING;

DECLARE_STACK_OF(POLICY_MAPPING)

typedef STACK_OF(POLICY_MAPPING) POLICY_MAPPINGS;

typedef struct GENERAL_SUBTREE_st {
	GENERAL_NAME *base;
	ASN1_INTEGER *minimum;
	ASN1_INTEGER *maximum;
} GENERAL_SUBTREE;

DECLARE_STACK_OF(GENERAL_SUBTREE)

struct NAME_CONSTRAINTS_st {
	STACK_OF(GENERAL_SUBTREE) *permittedSubtrees;
	STACK_OF(GENERAL_SUBTREE) *excludedSubtrees;
};

typedef struct POLICY_CONSTRAINTS_st {
	ASN1_INTEGER *requireExplicitPolicy;
	ASN1_INTEGER *inhibitPolicyMapping;
} POLICY_CONSTRAINTS;

struct ISSUING_DIST_POINT_st {
	DIST_POINT_NAME *distpoint;
	int onlyuser;
	int onlyCA;
	ASN1_BIT_STRING *onlysomereasons;
	int indirectCRL;
	int onlyattr;
};

/* Values in idp_flags field */
/* IDP present */
#define	IDP_PRESENT	0x1
/* IDP values inconsistent */
#define IDP_INVALID	0x2
/* onlyuser true */
#define	IDP_ONLYUSER	0x4
/* onlyCA true */
#define	IDP_ONLYCA	0x8
/* onlyattr true */
#define IDP_ONLYATTR	0x10
/* indirectCRL true */
#define IDP_INDIRECT	0x20
/* onlysomereasons present */
#define IDP_REASONS	0x40

#define X509V3_conf_err(val) ERR_asprintf_error_data( \
			"section:%s,name:%s,value:%s", val->section, \
			val->name, val->value);

#define X509V3_set_ctx_test(ctx) \
			X509V3_set_ctx(ctx, NULL, NULL, NULL, NULL, CTX_TEST)
#define X509V3_set_ctx_nodb(ctx) (ctx)->db = NULL;

/* X509_PURPOSE stuff */

#define EXFLAG_BCONS		0x0001
#define EXFLAG_KUSAGE		0x0002
#define EXFLAG_XKUSAGE		0x0004
#define EXFLAG_NSCERT		0x0008

#define EXFLAG_CA		0x0010
#define EXFLAG_SI		0x0020  /* Self issued. */
#define EXFLAG_V1		0x0040
#define EXFLAG_INVALID		0x0080
#define EXFLAG_SET		0x0100
#define EXFLAG_CRITICAL		0x0200
#if !defined(LIBRESSL_INTERNAL)
#define EXFLAG_PROXY		0x0400
#endif
#define EXFLAG_INVALID_POLICY	0x0800
#define EXFLAG_FRESHEST		0x1000
#define EXFLAG_SS               0x2000	/* Self signed. */

#define KU_DIGITAL_SIGNATURE	0x0080
#define KU_NON_REPUDIATION	0x0040
#define KU_KEY_ENCIPHERMENT	0x0020
#define KU_DATA_ENCIPHERMENT	0x0010
#define KU_KEY_AGREEMENT	0x0008
#define KU_KEY_CERT_SIGN	0x0004
#define KU_CRL_SIGN		0x0002
#define KU_ENCIPHER_ONLY	0x0001
#define KU_DECIPHER_ONLY	0x8000

#define NS_SSL_CLIENT		0x80
#define NS_SSL_SERVER		0x40
#define NS_SMIME		0x20
#define NS_OBJSIGN		0x10
#define NS_SSL_CA		0x04
#define NS_SMIME_CA		0x02
#define NS_OBJSIGN_CA		0x01
#define NS_ANY_CA		(NS_SSL_CA|NS_SMIME_CA|NS_OBJSIGN_CA)

#define XKU_SSL_SERVER		0x1
#define XKU_SSL_CLIENT		0x2
#define XKU_SMIME		0x4
#define XKU_CODE_SIGN		0x8
#define XKU_SGC			0x10
#define XKU_OCSP_SIGN		0x20
#define XKU_TIMESTAMP		0x40
#define XKU_DVCS		0x80
#define XKU_ANYEKU		0x100

#define X509_PURPOSE_DYNAMIC	0x1
#define X509_PURPOSE_DYNAMIC_NAME	0x2

typedef struct x509_purpose_st X509_PURPOSE;

#define X509_PURPOSE_SSL_CLIENT		1
#define X509_PURPOSE_SSL_SERVER		2
#define X509_PURPOSE_NS_SSL_SERVER	3
#define X509_PURPOSE_SMIME_SIGN		4
#define X509_PURPOSE_SMIME_ENCRYPT	5
#define X509_PURPOSE_CRL_SIGN		6
#define X509_PURPOSE_ANY		7
#define X509_PURPOSE_OCSP_HELPER	8
#define X509_PURPOSE_TIMESTAMP_SIGN	9

#define X509_PURPOSE_MIN		1
#define X509_PURPOSE_MAX		9

/* Flags for X509V3_EXT_print() */

#define X509V3_EXT_UNKNOWN_MASK		(0xfL << 16)
/* Return error for unknown extensions */
#define X509V3_EXT_DEFAULT		0
/* Print error for unknown extensions */
#define X509V3_EXT_ERROR_UNKNOWN	(1L << 16)
/* ASN1 parse unknown extensions */
#define X509V3_EXT_PARSE_UNKNOWN	(2L << 16)
/* BIO_dump unknown extensions */
#define X509V3_EXT_DUMP_UNKNOWN		(3L << 16)

/* Flags for X509V3_add1_i2d */

#define X509V3_ADD_OP_MASK		0xfL
#define X509V3_ADD_DEFAULT		0L
#define X509V3_ADD_APPEND		1L
#define X509V3_ADD_REPLACE		2L
#define X509V3_ADD_REPLACE_EXISTING	3L
#define X509V3_ADD_KEEP_EXISTING	4L
#define X509V3_ADD_DELETE		5L
#define X509V3_ADD_SILENT		0x10

DECLARE_STACK_OF(X509_PURPOSE)

BASIC_CONSTRAINTS *BASIC_CONSTRAINTS_new(void);
void BASIC_CONSTRAINTS_free(BASIC_CONSTRAINTS *a);
BASIC_CONSTRAINTS *d2i_BASIC_CONSTRAINTS(BASIC_CONSTRAINTS **a, const unsigned char **in, long len);
int i2d_BASIC_CONSTRAINTS(BASIC_CONSTRAINTS *a, unsigned char **out);
extern const ASN1_ITEM BASIC_CONSTRAINTS_it;

AUTHORITY_KEYID *AUTHORITY_KEYID_new(void);
void AUTHORITY_KEYID_free(AUTHORITY_KEYID *a);
AUTHORITY_KEYID *d2i_AUTHORITY_KEYID(AUTHORITY_KEYID **a, const unsigned char **in, long len);
int i2d_AUTHORITY_KEYID(AUTHORITY_KEYID *a, unsigned char **out);
extern const ASN1_ITEM AUTHORITY_KEYID_it;

PKEY_USAGE_PERIOD *PKEY_USAGE_PERIOD_new(void);
void PKEY_USAGE_PERIOD_free(PKEY_USAGE_PERIOD *a);
PKEY_USAGE_PERIOD *d2i_PKEY_USAGE_PERIOD(PKEY_USAGE_PERIOD **a, const unsigned char **in, long len);
int i2d_PKEY_USAGE_PERIOD(PKEY_USAGE_PERIOD *a, unsigned char **out);
extern const ASN1_ITEM PKEY_USAGE_PERIOD_it;

GENERAL_NAME *GENERAL_NAME_new(void);
void GENERAL_NAME_free(GENERAL_NAME *a);
GENERAL_NAME *d2i_GENERAL_NAME(GENERAL_NAME **a, const unsigned char **in, long len);
int i2d_GENERAL_NAME(GENERAL_NAME *a, unsigned char **out);
extern const ASN1_ITEM GENERAL_NAME_it;
GENERAL_NAME *GENERAL_NAME_dup(GENERAL_NAME *a);
int GENERAL_NAME_cmp(GENERAL_NAME *a, GENERAL_NAME *b);



ASN1_BIT_STRING *v2i_ASN1_BIT_STRING(X509V3_EXT_METHOD *method,
				X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
STACK_OF(CONF_VALUE) *i2v_ASN1_BIT_STRING(X509V3_EXT_METHOD *method,
				ASN1_BIT_STRING *bits,
				STACK_OF(CONF_VALUE) *extlist);

STACK_OF(CONF_VALUE) *i2v_GENERAL_NAME(X509V3_EXT_METHOD *method, GENERAL_NAME *gen, STACK_OF(CONF_VALUE) *ret);
int GENERAL_NAME_print(BIO *out, GENERAL_NAME *gen);

GENERAL_NAMES *GENERAL_NAMES_new(void);
void GENERAL_NAMES_free(GENERAL_NAMES *a);
GENERAL_NAMES *d2i_GENERAL_NAMES(GENERAL_NAMES **a, const unsigned char **in, long len);
int i2d_GENERAL_NAMES(GENERAL_NAMES *a, unsigned char **out);
extern const ASN1_ITEM GENERAL_NAMES_it;

STACK_OF(CONF_VALUE) *i2v_GENERAL_NAMES(X509V3_EXT_METHOD *method,
		GENERAL_NAMES *gen, STACK_OF(CONF_VALUE) *extlist);
GENERAL_NAMES *v2i_GENERAL_NAMES(const X509V3_EXT_METHOD *method,
				 X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);

OTHERNAME *OTHERNAME_new(void);
void OTHERNAME_free(OTHERNAME *a);
OTHERNAME *d2i_OTHERNAME(OTHERNAME **a, const unsigned char **in, long len);
int i2d_OTHERNAME(OTHERNAME *a, unsigned char **out);
extern const ASN1_ITEM OTHERNAME_it;
EDIPARTYNAME *EDIPARTYNAME_new(void);
void EDIPARTYNAME_free(EDIPARTYNAME *a);
EDIPARTYNAME *d2i_EDIPARTYNAME(EDIPARTYNAME **a, const unsigned char **in, long len);
int i2d_EDIPARTYNAME(EDIPARTYNAME *a, unsigned char **out);
extern const ASN1_ITEM EDIPARTYNAME_it;
int OTHERNAME_cmp(OTHERNAME *a, OTHERNAME *b);
void GENERAL_NAME_set0_value(GENERAL_NAME *a, int type, void *value);
void *GENERAL_NAME_get0_value(GENERAL_NAME *a, int *ptype);
int GENERAL_NAME_set0_othername(GENERAL_NAME *gen,
				ASN1_OBJECT *oid, ASN1_TYPE *value);
int GENERAL_NAME_get0_otherName(GENERAL_NAME *gen,
				ASN1_OBJECT **poid, ASN1_TYPE **pvalue);

char *i2s_ASN1_OCTET_STRING(X509V3_EXT_METHOD *method,
    const ASN1_OCTET_STRING *ia5);
ASN1_OCTET_STRING *s2i_ASN1_OCTET_STRING(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, const char *str);

EXTENDED_KEY_USAGE *EXTENDED_KEY_USAGE_new(void);
void EXTENDED_KEY_USAGE_free(EXTENDED_KEY_USAGE *a);
EXTENDED_KEY_USAGE *d2i_EXTENDED_KEY_USAGE(EXTENDED_KEY_USAGE **a, const unsigned char **in, long len);
int i2d_EXTENDED_KEY_USAGE(EXTENDED_KEY_USAGE *a, unsigned char **out);
extern const ASN1_ITEM EXTENDED_KEY_USAGE_it;
int i2a_ACCESS_DESCRIPTION(BIO *bp, const ACCESS_DESCRIPTION* a);

CERTIFICATEPOLICIES *CERTIFICATEPOLICIES_new(void);
void CERTIFICATEPOLICIES_free(CERTIFICATEPOLICIES *a);
CERTIFICATEPOLICIES *d2i_CERTIFICATEPOLICIES(CERTIFICATEPOLICIES **a, const unsigned char **in, long len);
int i2d_CERTIFICATEPOLICIES(CERTIFICATEPOLICIES *a, unsigned char **out);
extern const ASN1_ITEM CERTIFICATEPOLICIES_it;
POLICYINFO *POLICYINFO_new(void);
void POLICYINFO_free(POLICYINFO *a);
POLICYINFO *d2i_POLICYINFO(POLICYINFO **a, const unsigned char **in, long len);
int i2d_POLICYINFO(POLICYINFO *a, unsigned char **out);
extern const ASN1_ITEM POLICYINFO_it;
POLICYQUALINFO *POLICYQUALINFO_new(void);
void POLICYQUALINFO_free(POLICYQUALINFO *a);
POLICYQUALINFO *d2i_POLICYQUALINFO(POLICYQUALINFO **a, const unsigned char **in, long len);
int i2d_POLICYQUALINFO(POLICYQUALINFO *a, unsigned char **out);
extern const ASN1_ITEM POLICYQUALINFO_it;
USERNOTICE *USERNOTICE_new(void);
void USERNOTICE_free(USERNOTICE *a);
USERNOTICE *d2i_USERNOTICE(USERNOTICE **a, const unsigned char **in, long len);
int i2d_USERNOTICE(USERNOTICE *a, unsigned char **out);
extern const ASN1_ITEM USERNOTICE_it;
NOTICEREF *NOTICEREF_new(void);
void NOTICEREF_free(NOTICEREF *a);
NOTICEREF *d2i_NOTICEREF(NOTICEREF **a, const unsigned char **in, long len);
int i2d_NOTICEREF(NOTICEREF *a, unsigned char **out);
extern const ASN1_ITEM NOTICEREF_it;

CRL_DIST_POINTS *CRL_DIST_POINTS_new(void);
void CRL_DIST_POINTS_free(CRL_DIST_POINTS *a);
CRL_DIST_POINTS *d2i_CRL_DIST_POINTS(CRL_DIST_POINTS **a, const unsigned char **in, long len);
int i2d_CRL_DIST_POINTS(CRL_DIST_POINTS *a, unsigned char **out);
extern const ASN1_ITEM CRL_DIST_POINTS_it;
DIST_POINT *DIST_POINT_new(void);
void DIST_POINT_free(DIST_POINT *a);
DIST_POINT *d2i_DIST_POINT(DIST_POINT **a, const unsigned char **in, long len);
int i2d_DIST_POINT(DIST_POINT *a, unsigned char **out);
extern const ASN1_ITEM DIST_POINT_it;
DIST_POINT_NAME *DIST_POINT_NAME_new(void);
void DIST_POINT_NAME_free(DIST_POINT_NAME *a);
DIST_POINT_NAME *d2i_DIST_POINT_NAME(DIST_POINT_NAME **a, const unsigned char **in, long len);
int i2d_DIST_POINT_NAME(DIST_POINT_NAME *a, unsigned char **out);
extern const ASN1_ITEM DIST_POINT_NAME_it;
ISSUING_DIST_POINT *ISSUING_DIST_POINT_new(void);
void ISSUING_DIST_POINT_free(ISSUING_DIST_POINT *a);
ISSUING_DIST_POINT *d2i_ISSUING_DIST_POINT(ISSUING_DIST_POINT **a, const unsigned char **in, long len);
int i2d_ISSUING_DIST_POINT(ISSUING_DIST_POINT *a, unsigned char **out);
extern const ASN1_ITEM ISSUING_DIST_POINT_it;

int DIST_POINT_set_dpname(DIST_POINT_NAME *dpn, X509_NAME *iname);

int NAME_CONSTRAINTS_check(X509 *x, NAME_CONSTRAINTS *nc);

ACCESS_DESCRIPTION *ACCESS_DESCRIPTION_new(void);
void ACCESS_DESCRIPTION_free(ACCESS_DESCRIPTION *a);
ACCESS_DESCRIPTION *d2i_ACCESS_DESCRIPTION(ACCESS_DESCRIPTION **a, const unsigned char **in, long len);
int i2d_ACCESS_DESCRIPTION(ACCESS_DESCRIPTION *a, unsigned char **out);
extern const ASN1_ITEM ACCESS_DESCRIPTION_it;
AUTHORITY_INFO_ACCESS *AUTHORITY_INFO_ACCESS_new(void);
void AUTHORITY_INFO_ACCESS_free(AUTHORITY_INFO_ACCESS *a);
AUTHORITY_INFO_ACCESS *d2i_AUTHORITY_INFO_ACCESS(AUTHORITY_INFO_ACCESS **a, const unsigned char **in, long len);
int i2d_AUTHORITY_INFO_ACCESS(AUTHORITY_INFO_ACCESS *a, unsigned char **out);
extern const ASN1_ITEM AUTHORITY_INFO_ACCESS_it;

extern const ASN1_ITEM POLICY_MAPPING_it;
POLICY_MAPPING *POLICY_MAPPING_new(void);
void POLICY_MAPPING_free(POLICY_MAPPING *a);
extern const ASN1_ITEM POLICY_MAPPINGS_it;

extern const ASN1_ITEM GENERAL_SUBTREE_it;
GENERAL_SUBTREE *GENERAL_SUBTREE_new(void);
void GENERAL_SUBTREE_free(GENERAL_SUBTREE *a);

extern const ASN1_ITEM NAME_CONSTRAINTS_it;
NAME_CONSTRAINTS *NAME_CONSTRAINTS_new(void);
void NAME_CONSTRAINTS_free(NAME_CONSTRAINTS *a);

POLICY_CONSTRAINTS *POLICY_CONSTRAINTS_new(void);
void POLICY_CONSTRAINTS_free(POLICY_CONSTRAINTS *a);
extern const ASN1_ITEM POLICY_CONSTRAINTS_it;

GENERAL_NAME *a2i_GENERAL_NAME(GENERAL_NAME *out,
			       const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
			       int gen_type, const char *value, int is_nc);

#ifdef HEADER_CONF_H
GENERAL_NAME *v2i_GENERAL_NAME(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
			       CONF_VALUE *cnf);
GENERAL_NAME *v2i_GENERAL_NAME_ex(GENERAL_NAME *out,
				  const X509V3_EXT_METHOD *method,
				  X509V3_CTX *ctx, CONF_VALUE *cnf, int is_nc);
void X509V3_conf_free(CONF_VALUE *val);

X509_EXTENSION *X509V3_EXT_nconf_nid(CONF *conf, X509V3_CTX *ctx, int ext_nid,
    const char *value);
X509_EXTENSION *X509V3_EXT_nconf(CONF *conf, X509V3_CTX *ctx, const char *name,
    const char *value);
int X509V3_EXT_add_nconf_sk(CONF *conf, X509V3_CTX *ctx, const char *section,
    STACK_OF(X509_EXTENSION) **sk);
int X509V3_EXT_add_nconf(CONF *conf, X509V3_CTX *ctx, const char *section,
    X509 *cert);
int X509V3_EXT_REQ_add_nconf(CONF *conf, X509V3_CTX *ctx, const char *section,
    X509_REQ *req);
int X509V3_EXT_CRL_add_nconf(CONF *conf, X509V3_CTX *ctx, const char *section,
    X509_CRL *crl);

X509_EXTENSION *X509V3_EXT_conf_nid(LHASH_OF(CONF_VALUE) *conf, X509V3_CTX *ctx,
    int ext_nid, const char *value);
X509_EXTENSION *X509V3_EXT_conf(LHASH_OF(CONF_VALUE) *conf, X509V3_CTX *ctx,
    const char *name, const char *value);

void X509V3_set_nconf(X509V3_CTX *ctx, CONF *conf);
#endif

void X509V3_set_ctx(X509V3_CTX *ctx, X509 *issuer, X509 *subject,
				 X509_REQ *req, X509_CRL *crl, int flags);

char *i2s_ASN1_INTEGER(X509V3_EXT_METHOD *meth, const ASN1_INTEGER *aint);
ASN1_INTEGER *s2i_ASN1_INTEGER(X509V3_EXT_METHOD *meth, const char *value);
char *i2s_ASN1_ENUMERATED(X509V3_EXT_METHOD *meth, const ASN1_ENUMERATED *aint);
char *i2s_ASN1_ENUMERATED_TABLE(X509V3_EXT_METHOD *meth,
    const ASN1_ENUMERATED *aint);

const X509V3_EXT_METHOD *X509V3_EXT_get(X509_EXTENSION *ext);
const X509V3_EXT_METHOD *X509V3_EXT_get_nid(int nid);
int X509V3_add_standard_extensions(void);
STACK_OF(CONF_VALUE) *X509V3_parse_list(const char *line);
void *X509V3_EXT_d2i(X509_EXTENSION *ext);
void *X509V3_get_d2i(const STACK_OF(X509_EXTENSION) *x, int nid, int *crit,
    int *idx);

X509_EXTENSION *X509V3_EXT_i2d(int ext_nid, int crit, void *ext_struc);
int X509V3_add1_i2d(STACK_OF(X509_EXTENSION) **x, int nid, void *value, int crit, unsigned long flags);

char *hex_to_string(const unsigned char *buffer, long len);
unsigned char *string_to_hex(const char *str, long *len);

void X509V3_EXT_val_prn(BIO *out, STACK_OF(CONF_VALUE) *val, int indent,
								 int ml);
int X509V3_EXT_print(BIO *out, X509_EXTENSION *ext, unsigned long flag, int indent);
int X509V3_EXT_print_fp(FILE *out, X509_EXTENSION *ext, int flag, int indent);

int X509V3_extensions_print(BIO *out, const char *title,
    const STACK_OF(X509_EXTENSION) *exts, unsigned long flag, int indent);

int X509_check_ca(X509 *x);
int X509_check_purpose(X509 *x, int id, int ca);
int X509_supported_extension(X509_EXTENSION *ex);
int X509_check_issued(X509 *issuer, X509 *subject);
int X509_check_akid(X509 *issuer, AUTHORITY_KEYID *akid);

int X509_PURPOSE_get_count(void);
const X509_PURPOSE *X509_PURPOSE_get0(int idx);
int X509_PURPOSE_get_by_sname(const char *sname);
const char *X509_PURPOSE_get0_name(const X509_PURPOSE *xp);
const char *X509_PURPOSE_get0_sname(const X509_PURPOSE *xp);
int X509_PURPOSE_get_id(const X509_PURPOSE *);
uint32_t X509_get_extension_flags(X509 *x);
uint32_t X509_get_key_usage(X509 *x);
uint32_t X509_get_extended_key_usage(X509 *x);

STACK_OF(OPENSSL_STRING) *X509_get1_email(X509 *x);
STACK_OF(OPENSSL_STRING) *X509_REQ_get1_email(X509_REQ *x);
void X509_email_free(STACK_OF(OPENSSL_STRING) *sk);
STACK_OF(OPENSSL_STRING) *X509_get1_ocsp(X509 *x);

/* Flags for X509_check_* functions */
/* Always check subject name for host match even if subject alt names present */
#define X509_CHECK_FLAG_ALWAYS_CHECK_SUBJECT	0x1
/* Disable wildcard matching for dnsName fields and common name. */
#define X509_CHECK_FLAG_NO_WILDCARDS	0x2
/* Wildcards must not match a partial label. */
#define X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS 0x4
/* Allow (non-partial) wildcards to match multiple labels. */
#define X509_CHECK_FLAG_MULTI_LABEL_WILDCARDS 0x8
/* Constraint verifier subdomain patterns to match a single labels. */
#define X509_CHECK_FLAG_SINGLE_LABEL_SUBDOMAINS 0x10
/* Disable checking the CN for a hostname, to support modern validation */
#define X509_CHECK_FLAG_NEVER_CHECK_SUBJECT 0x20

int X509_check_host(X509 *x, const char *chk, size_t chklen,
    unsigned int flags, char **peername);
int X509_check_email(X509 *x, const char *chk, size_t chklen,
    unsigned int flags);
int X509_check_ip(X509 *x, const unsigned char *chk, size_t chklen,
    unsigned int flags);
int X509_check_ip_asc(X509 *x, const char *ipasc, unsigned int flags);

ASN1_OCTET_STRING *a2i_IPADDRESS(const char *ipasc);
ASN1_OCTET_STRING *a2i_IPADDRESS_NC(const char *ipasc);
int a2i_ipadd(unsigned char *ipout, const char *ipasc);
int X509V3_NAME_from_section(X509_NAME *nm, STACK_OF(CONF_VALUE)*dn_sk,
						unsigned long chtype);

#ifndef OPENSSL_NO_RFC3779
typedef struct ASRange_st {
	ASN1_INTEGER *min;
	ASN1_INTEGER *max;
} ASRange;

#define ASIdOrRange_id		0
#define ASIdOrRange_range	1

typedef struct ASIdOrRange_st {
	int type;
	union {
		ASN1_INTEGER *id;
		ASRange *range;
	} u;
} ASIdOrRange;

typedef STACK_OF(ASIdOrRange) ASIdOrRanges;
DECLARE_STACK_OF(ASIdOrRange)

#define ASIdentifierChoice_inherit		0
#define ASIdentifierChoice_asIdsOrRanges	1

typedef struct ASIdentifierChoice_st {
	int type;
	union {
		ASN1_NULL *inherit;
		ASIdOrRanges *asIdsOrRanges;
	} u;
} ASIdentifierChoice;

typedef struct ASIdentifiers_st {
	ASIdentifierChoice *asnum;
	ASIdentifierChoice *rdi;
} ASIdentifiers;

ASRange *ASRange_new(void);
void ASRange_free(ASRange *a);
ASRange *d2i_ASRange(ASRange **a, const unsigned char **in, long len);
int i2d_ASRange(ASRange *a, unsigned char **out);
extern const ASN1_ITEM ASRange_it;

ASIdOrRange *ASIdOrRange_new(void);
void ASIdOrRange_free(ASIdOrRange *a);
ASIdOrRange *d2i_ASIdOrRange(ASIdOrRange **a, const unsigned char **in,
    long len);
int i2d_ASIdOrRange(ASIdOrRange *a, unsigned char **out);
extern const ASN1_ITEM ASIdOrRange_it;

ASIdentifierChoice *ASIdentifierChoice_new(void);
void ASIdentifierChoice_free(ASIdentifierChoice *a);
ASIdentifierChoice *d2i_ASIdentifierChoice(ASIdentifierChoice **a,
    const unsigned char **in, long len);
int i2d_ASIdentifierChoice(ASIdentifierChoice *a, unsigned char **out);
extern const ASN1_ITEM ASIdentifierChoice_it;

ASIdentifiers *ASIdentifiers_new(void);
void ASIdentifiers_free(ASIdentifiers *a);
ASIdentifiers *d2i_ASIdentifiers(ASIdentifiers **a, const unsigned char **in,
    long len);
int i2d_ASIdentifiers(ASIdentifiers *a, unsigned char **out);
extern const ASN1_ITEM ASIdentifiers_it;

typedef struct IPAddressRange_st {
	ASN1_BIT_STRING *min;
	ASN1_BIT_STRING *max;
} IPAddressRange;

#define IPAddressOrRange_addressPrefix	0
#define IPAddressOrRange_addressRange	1

typedef struct IPAddressOrRange_st {
	int type;
	union {
		ASN1_BIT_STRING *addressPrefix;
		IPAddressRange *addressRange;
	} u;
} IPAddressOrRange;

typedef STACK_OF(IPAddressOrRange) IPAddressOrRanges;
DECLARE_STACK_OF(IPAddressOrRange)

#define IPAddressChoice_inherit			0
#define IPAddressChoice_addressesOrRanges	1

typedef struct IPAddressChoice_st {
	int type;
	union {
		ASN1_NULL *inherit;
		IPAddressOrRanges *addressesOrRanges;
	} u;
} IPAddressChoice;

typedef struct IPAddressFamily_st {
	ASN1_OCTET_STRING *addressFamily;
	IPAddressChoice *ipAddressChoice;
} IPAddressFamily;

typedef STACK_OF(IPAddressFamily) IPAddrBlocks;
DECLARE_STACK_OF(IPAddressFamily)

IPAddressRange *IPAddressRange_new(void);
void IPAddressRange_free(IPAddressRange *a);
IPAddressRange *d2i_IPAddressRange(IPAddressRange **a,
    const unsigned char **in, long len);
int i2d_IPAddressRange(IPAddressRange *a, unsigned char **out);
extern const ASN1_ITEM IPAddressRange_it;

IPAddressOrRange *IPAddressOrRange_new(void);
void IPAddressOrRange_free(IPAddressOrRange *a);
IPAddressOrRange *d2i_IPAddressOrRange(IPAddressOrRange **a,
    const unsigned char **in, long len);
int i2d_IPAddressOrRange(IPAddressOrRange *a, unsigned char **out);
extern const ASN1_ITEM IPAddressOrRange_it;

IPAddressChoice *IPAddressChoice_new(void);
void IPAddressChoice_free(IPAddressChoice *a);
IPAddressChoice *d2i_IPAddressChoice(IPAddressChoice **a,
    const unsigned char **in, long len);
int i2d_IPAddressChoice(IPAddressChoice *a, unsigned char **out);
extern const ASN1_ITEM IPAddressChoice_it;

IPAddressFamily *IPAddressFamily_new(void);
void IPAddressFamily_free(IPAddressFamily *a);
IPAddressFamily *d2i_IPAddressFamily(IPAddressFamily **a,
    const unsigned char **in, long len);
int i2d_IPAddressFamily(IPAddressFamily *a, unsigned char **out);
extern const ASN1_ITEM IPAddressFamily_it;

/*
 * API tag for elements of the ASIdentifer SEQUENCE.
 */
#define V3_ASID_ASNUM	0
#define V3_ASID_RDI	1

/*
 * AFI values, assigned by IANA.  It'd be nice to make the AFI
 * handling code totally generic, but there are too many little things
 * that would need to be defined for other address families for it to
 * be worth the trouble.
 */
#define IANA_AFI_IPV4	1
#define IANA_AFI_IPV6	2

/*
 * Utilities to construct and extract values from RFC3779 extensions,
 * since some of the encodings (particularly for IP address prefixes
 * and ranges) are a bit tedious to work with directly.
 */
int X509v3_asid_add_inherit(ASIdentifiers *asid, int which);
int X509v3_asid_add_id_or_range(ASIdentifiers *asid, int which,
    ASN1_INTEGER *min, ASN1_INTEGER *max);
int X509v3_addr_add_inherit(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi);
int X509v3_addr_add_prefix(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi, unsigned char *a, const int prefixlen);
int X509v3_addr_add_range(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi, unsigned char *min, unsigned char *max);
unsigned X509v3_addr_get_afi(const IPAddressFamily *f);
int X509v3_addr_get_range(IPAddressOrRange *aor, const unsigned afi,
    unsigned char *min, unsigned char *max, const int length);

/*
 * Canonical forms.
 */
int X509v3_asid_is_canonical(ASIdentifiers *asid);
int X509v3_addr_is_canonical(IPAddrBlocks *addr);
int X509v3_asid_canonize(ASIdentifiers *asid);
int X509v3_addr_canonize(IPAddrBlocks *addr);

/*
 * Tests for inheritance and containment.
 */
int X509v3_asid_inherits(ASIdentifiers *asid);
int X509v3_addr_inherits(IPAddrBlocks *addr);
int X509v3_asid_subset(ASIdentifiers *a, ASIdentifiers *b);
int X509v3_addr_subset(IPAddrBlocks *a, IPAddrBlocks *b);

/*
 * Check whether RFC 3779 extensions nest properly in chains.
 */
int X509v3_asid_validate_path(X509_STORE_CTX *);
int X509v3_addr_validate_path(X509_STORE_CTX *);
int X509v3_asid_validate_resource_set(STACK_OF(X509) *chain, ASIdentifiers *ext,
    int allow_inheritance);
int X509v3_addr_validate_resource_set(STACK_OF(X509) *chain, IPAddrBlocks *ext,
    int allow_inheritance);

#endif /* !OPENSSL_NO_RFC3779 */

void ERR_load_X509V3_strings(void);

/* Error codes for the X509V3 functions. */

/* Function codes. */
#define X509V3_F_A2I_GENERAL_NAME			 164
#define X509V3_F_ASIDENTIFIERCHOICE_CANONIZE		 161
#define X509V3_F_ASIDENTIFIERCHOICE_IS_CANONICAL	 162
#define X509V3_F_COPY_EMAIL				 122
#define X509V3_F_COPY_ISSUER				 123
#define X509V3_F_DO_DIRNAME				 144
#define X509V3_F_DO_EXT_CONF				 124
#define X509V3_F_DO_EXT_I2D				 135
#define X509V3_F_DO_EXT_NCONF				 151
#define X509V3_F_DO_I2V_NAME_CONSTRAINTS		 148
#define X509V3_F_GNAMES_FROM_SECTNAME			 156
#define X509V3_F_HEX_TO_STRING				 111
#define X509V3_F_I2S_ASN1_ENUMERATED			 121
#define X509V3_F_I2S_ASN1_IA5STRING			 149
#define X509V3_F_I2S_ASN1_INTEGER			 120
#define X509V3_F_I2V_AUTHORITY_INFO_ACCESS		 138
#define X509V3_F_NOTICE_SECTION				 132
#define X509V3_F_NREF_NOS				 133
#define X509V3_F_POLICY_SECTION				 131
#define X509V3_F_PROCESS_PCI_VALUE			 150
#define X509V3_F_R2I_CERTPOL				 130
#define X509V3_F_R2I_PCI				 155
#define X509V3_F_S2I_ASN1_IA5STRING			 100
#define X509V3_F_S2I_ASN1_INTEGER			 108
#define X509V3_F_S2I_ASN1_OCTET_STRING			 112
#define X509V3_F_S2I_ASN1_SKEY_ID			 114
#define X509V3_F_S2I_SKEY_ID				 115
#define X509V3_F_SET_DIST_POINT_NAME			 158
#define X509V3_F_STRING_TO_HEX				 113
#define X509V3_F_SXNET_ADD_ID_ASC			 125
#define X509V3_F_SXNET_ADD_ID_INTEGER			 126
#define X509V3_F_SXNET_ADD_ID_ULONG			 127
#define X509V3_F_SXNET_GET_ID_ASC			 128
#define X509V3_F_SXNET_GET_ID_ULONG			 129
#define X509V3_F_V2I_ASIDENTIFIERS			 163
#define X509V3_F_V2I_ASN1_BIT_STRING			 101
#define X509V3_F_V2I_AUTHORITY_INFO_ACCESS		 139
#define X509V3_F_V2I_AUTHORITY_KEYID			 119
#define X509V3_F_V2I_BASIC_CONSTRAINTS			 102
#define X509V3_F_V2I_CRLD				 134
#define X509V3_F_V2I_EXTENDED_KEY_USAGE			 103
#define X509V3_F_V2I_GENERAL_NAMES			 118
#define X509V3_F_V2I_GENERAL_NAME_EX			 117
#define X509V3_F_V2I_IDP				 157
#define X509V3_F_V2I_IPADDRBLOCKS			 159
#define X509V3_F_V2I_ISSUER_ALT				 153
#define X509V3_F_V2I_NAME_CONSTRAINTS			 147
#define X509V3_F_V2I_POLICY_CONSTRAINTS			 146
#define X509V3_F_V2I_POLICY_MAPPINGS			 145
#define X509V3_F_V2I_SUBJECT_ALT			 154
#define X509V3_F_V3_ADDR_VALIDATE_PATH_INTERNAL		 160
#define X509V3_F_V3_GENERIC_EXTENSION			 116
#define X509V3_F_X509V3_ADD1_I2D			 140
#define X509V3_F_X509V3_ADD_VALUE			 105
#define X509V3_F_X509V3_EXT_ADD				 104
#define X509V3_F_X509V3_EXT_ADD_ALIAS			 106
#define X509V3_F_X509V3_EXT_CONF			 107
#define X509V3_F_X509V3_EXT_I2D				 136
#define X509V3_F_X509V3_EXT_NCONF			 152
#define X509V3_F_X509V3_GET_SECTION			 142
#define X509V3_F_X509V3_GET_STRING			 143
#define X509V3_F_X509V3_GET_VALUE_BOOL			 110
#define X509V3_F_X509V3_PARSE_LIST			 109
#define X509V3_F_X509_PURPOSE_ADD			 137
#define X509V3_F_X509_PURPOSE_SET			 141

/* Reason codes. */
#define X509V3_R_BAD_IP_ADDRESS				 118
#define X509V3_R_BAD_OBJECT				 119
#define X509V3_R_BN_DEC2BN_ERROR			 100
#define X509V3_R_BN_TO_ASN1_INTEGER_ERROR		 101
#define X509V3_R_DIRNAME_ERROR				 149
#define X509V3_R_DISTPOINT_ALREADY_SET			 160
#define X509V3_R_DUPLICATE_ZONE_ID			 133
#define X509V3_R_ERROR_CONVERTING_ZONE			 131
#define X509V3_R_ERROR_CREATING_EXTENSION		 144
#define X509V3_R_ERROR_IN_EXTENSION			 128
#define X509V3_R_EXPECTED_A_SECTION_NAME		 137
#define X509V3_R_EXTENSION_EXISTS			 145
#define X509V3_R_EXTENSION_NAME_ERROR			 115
#define X509V3_R_EXTENSION_NOT_FOUND			 102
#define X509V3_R_EXTENSION_SETTING_NOT_SUPPORTED	 103
#define X509V3_R_EXTENSION_VALUE_ERROR			 116
#define X509V3_R_ILLEGAL_EMPTY_EXTENSION		 151
#define X509V3_R_ILLEGAL_HEX_DIGIT			 113
#define X509V3_R_INCORRECT_POLICY_SYNTAX_TAG		 152
#define X509V3_R_INVALID_MULTIPLE_RDNS			 161
#define X509V3_R_INVALID_ASNUMBER			 162
#define X509V3_R_INVALID_ASRANGE			 163
#define X509V3_R_INVALID_BOOLEAN_STRING			 104
#define X509V3_R_INVALID_EXTENSION_STRING		 105
#define X509V3_R_INVALID_INHERITANCE			 165
#define X509V3_R_INVALID_IPADDRESS			 166
#define X509V3_R_INVALID_NAME				 106
#define X509V3_R_INVALID_NULL_ARGUMENT			 107
#define X509V3_R_INVALID_NULL_NAME			 108
#define X509V3_R_INVALID_NULL_VALUE			 109
#define X509V3_R_INVALID_NUMBER				 140
#define X509V3_R_INVALID_NUMBERS			 141
#define X509V3_R_INVALID_OBJECT_IDENTIFIER		 110
#define X509V3_R_INVALID_OPTION				 138
#define X509V3_R_INVALID_POLICY_IDENTIFIER		 134
#define X509V3_R_INVALID_PROXY_POLICY_SETTING		 153
#define X509V3_R_INVALID_PURPOSE			 146
#define X509V3_R_INVALID_SAFI				 164
#define X509V3_R_INVALID_SECTION			 135
#define X509V3_R_INVALID_SYNTAX				 143
#define X509V3_R_ISSUER_DECODE_ERROR			 126
#define X509V3_R_MISSING_VALUE				 124
#define X509V3_R_NEED_ORGANIZATION_AND_NUMBERS		 142
#define X509V3_R_NO_CONFIG_DATABASE			 136
#define X509V3_R_NO_ISSUER_CERTIFICATE			 121
#define X509V3_R_NO_ISSUER_DETAILS			 127
#define X509V3_R_NO_POLICY_IDENTIFIER			 139
#define X509V3_R_NO_PROXY_CERT_POLICY_LANGUAGE_DEFINED	 154
#define X509V3_R_NO_PUBLIC_KEY				 114
#define X509V3_R_NO_SUBJECT_DETAILS			 125
#define X509V3_R_ODD_NUMBER_OF_DIGITS			 112
#define X509V3_R_OPERATION_NOT_DEFINED			 148
#define X509V3_R_OTHERNAME_ERROR			 147
#define X509V3_R_POLICY_LANGUAGE_ALREADY_DEFINED	 155
#define X509V3_R_POLICY_PATH_LENGTH			 156
#define X509V3_R_POLICY_PATH_LENGTH_ALREADY_DEFINED	 157
#define X509V3_R_POLICY_SYNTAX_NOT_CURRENTLY_SUPPORTED	 158
#define X509V3_R_POLICY_WHEN_PROXY_LANGUAGE_REQUIRES_NO_POLICY 159
#define X509V3_R_SECTION_NOT_FOUND			 150
#define X509V3_R_UNABLE_TO_GET_ISSUER_DETAILS		 122
#define X509V3_R_UNABLE_TO_GET_ISSUER_KEYID		 123
#define X509V3_R_UNKNOWN_BIT_STRING_ARGUMENT		 111
#define X509V3_R_UNKNOWN_EXTENSION			 129
#define X509V3_R_UNKNOWN_EXTENSION_NAME			 130
#define X509V3_R_UNKNOWN_OPTION				 120
#define X509V3_R_UNSUPPORTED_OPTION			 117
#define X509V3_R_UNSUPPORTED_TYPE			 167
#define X509V3_R_USER_TOO_LONG				 132

#ifdef  __cplusplus
}
#endif
#endif
