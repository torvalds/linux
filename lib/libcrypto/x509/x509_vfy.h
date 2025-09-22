/* $OpenBSD: x509_vfy.h,v 1.70 2025/03/09 15:20:20 tb Exp $ */
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

#ifndef HEADER_X509_H
#include <openssl/x509.h>
/* openssl/x509.h ends up #include-ing this file at about the only
 * appropriate moment. */
#endif

#ifndef HEADER_X509_VFY_H
#define HEADER_X509_VFY_H

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_LHASH
#include <openssl/lhash.h>
#endif
#include <openssl/bio.h>
#include <openssl/crypto.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * SSL_CTX -> X509_STORE
 *		-> X509_LOOKUP
 *			->X509_LOOKUP_METHOD
 *		-> X509_LOOKUP
 *			->X509_LOOKUP_METHOD
 *
 * SSL	-> X509_STORE_CTX
 *		->X509_STORE
 *
 * The X509_STORE holds the tables etc for verification stuff.
 * A X509_STORE_CTX is used while validating a single certificate.
 * The X509_STORE has X509_LOOKUPs for looking up certs.
 * The X509_STORE then calls a function to actually verify the
 * certificate chain.
 */

typedef enum {
	X509_LU_NONE,
	X509_LU_X509,
	X509_LU_CRL,
} X509_LOOKUP_TYPE;


DECLARE_STACK_OF(X509_LOOKUP)
DECLARE_STACK_OF(X509_OBJECT)
DECLARE_STACK_OF(X509_VERIFY_PARAM)

/* XXX - unused in OpenSSL. Can we remove this? */
typedef struct X509_VERIFY_PARAM_ID_st X509_VERIFY_PARAM_ID;


int X509_STORE_set_depth(X509_STORE *store, int depth);

void X509_STORE_CTX_set_depth(X509_STORE_CTX *ctx, int depth);

#define X509_STORE_CTX_set_app_data(ctx,data) \
	X509_STORE_CTX_set_ex_data(ctx,0,data)
#define X509_STORE_CTX_get_app_data(ctx) \
	X509_STORE_CTX_get_ex_data(ctx,0)

#define X509_L_FILE_LOAD	1
#define X509_L_ADD_DIR		2
#define X509_L_MEM		3

#define X509_LOOKUP_load_file(x,name,type) \
		X509_LOOKUP_ctrl((x),X509_L_FILE_LOAD,(name),(long)(type),NULL)

#define X509_LOOKUP_add_dir(x,name,type) \
		X509_LOOKUP_ctrl((x),X509_L_ADD_DIR,(name),(long)(type),NULL)

#define X509_LOOKUP_add_mem(x,iov,type) \
		X509_LOOKUP_ctrl((x),X509_L_MEM,(const char *)(iov),\
		(long)(type),NULL)

#define		X509_V_OK					0
#define		X509_V_ERR_UNSPECIFIED				1
#define		X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT		2
#define		X509_V_ERR_UNABLE_TO_GET_CRL			3
#define		X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE	4
#define		X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE	5
#define		X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY	6
#define		X509_V_ERR_CERT_SIGNATURE_FAILURE		7
#define		X509_V_ERR_CRL_SIGNATURE_FAILURE		8
#define		X509_V_ERR_CERT_NOT_YET_VALID			9
#define		X509_V_ERR_CERT_HAS_EXPIRED			10
#define		X509_V_ERR_CRL_NOT_YET_VALID			11
#define		X509_V_ERR_CRL_HAS_EXPIRED			12
#define		X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD	13
#define		X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD	14
#define		X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD	15
#define		X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD	16
#define		X509_V_ERR_OUT_OF_MEM				17
#define		X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT		18
#define		X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN		19
#define		X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY	20
#define		X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE	21
#define		X509_V_ERR_CERT_CHAIN_TOO_LONG			22
#define		X509_V_ERR_CERT_REVOKED				23
#define		X509_V_ERR_INVALID_CA				24
#define		X509_V_ERR_PATH_LENGTH_EXCEEDED			25
#define		X509_V_ERR_INVALID_PURPOSE			26
#define		X509_V_ERR_CERT_UNTRUSTED			27
#define		X509_V_ERR_CERT_REJECTED			28
/* These are 'informational' when looking for issuer cert */
#define		X509_V_ERR_SUBJECT_ISSUER_MISMATCH		29
#define		X509_V_ERR_AKID_SKID_MISMATCH			30
#define		X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH		31
#define		X509_V_ERR_KEYUSAGE_NO_CERTSIGN			32

#define		X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER		33
#define		X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION		34
#define		X509_V_ERR_KEYUSAGE_NO_CRL_SIGN			35
#define		X509_V_ERR_UNHANDLED_CRITICAL_CRL_EXTENSION	36
#define		X509_V_ERR_INVALID_NON_CA			37
#define		X509_V_ERR_PROXY_PATH_LENGTH_EXCEEDED		38
#define		X509_V_ERR_KEYUSAGE_NO_DIGITAL_SIGNATURE	39
#define		X509_V_ERR_PROXY_CERTIFICATES_NOT_ALLOWED	40

#define		X509_V_ERR_INVALID_EXTENSION			41
#define		X509_V_ERR_INVALID_POLICY_EXTENSION		42
#define		X509_V_ERR_NO_EXPLICIT_POLICY			43
#define		X509_V_ERR_DIFFERENT_CRL_SCOPE			44
#define		X509_V_ERR_UNSUPPORTED_EXTENSION_FEATURE	45

#define		X509_V_ERR_UNNESTED_RESOURCE			46

#define		X509_V_ERR_PERMITTED_VIOLATION			47
#define		X509_V_ERR_EXCLUDED_VIOLATION			48
#define		X509_V_ERR_SUBTREE_MINMAX			49
#define		X509_V_ERR_UNSUPPORTED_CONSTRAINT_TYPE		51
#define		X509_V_ERR_UNSUPPORTED_CONSTRAINT_SYNTAX	52
#define		X509_V_ERR_UNSUPPORTED_NAME_SYNTAX		53
#define		X509_V_ERR_CRL_PATH_VALIDATION_ERROR		54

/* The application is not happy */
#define		X509_V_ERR_APPLICATION_VERIFICATION		50

/* Host, email and IP check errors */
#define		X509_V_ERR_HOSTNAME_MISMATCH			62
#define		X509_V_ERR_EMAIL_MISMATCH			63
#define		X509_V_ERR_IP_ADDRESS_MISMATCH			64

/* Caller error */
#define		X509_V_ERR_INVALID_CALL				65
/* Issuer lookup error */
#define		X509_V_ERR_STORE_LOOKUP				66

/* Security level errors */
#define		X509_V_ERR_EE_KEY_TOO_SMALL                     67
#define		X509_V_ERR_CA_KEY_TOO_SMALL                     68
#define		X509_V_ERR_CA_MD_TOO_WEAK                       69

/* Certificate verify flags */

/* Deprecated in 1.1.0, has no effect. Various FFI bindings still expose it. */
#define	X509_V_FLAG_CB_ISSUER_CHECK		0x0
/* Use check time instead of current time */
#define	X509_V_FLAG_USE_CHECK_TIME		0x2
/* Lookup CRLs */
#define	X509_V_FLAG_CRL_CHECK			0x4
/* Lookup CRLs for whole chain */
#define	X509_V_FLAG_CRL_CHECK_ALL		0x8
/* Ignore unhandled critical extensions */
#define	X509_V_FLAG_IGNORE_CRITICAL		0x10
/* Disable workarounds for broken certificates */
#define	X509_V_FLAG_X509_STRICT			0x20
/* Enable proxy certificate validation */
#define	X509_V_FLAG_ALLOW_PROXY_CERTS		0x40
/* Does nothing as its functionality has been enabled by default */
#define X509_V_FLAG_POLICY_CHECK		0x80
/* Policy variable require-explicit-policy */
#define X509_V_FLAG_EXPLICIT_POLICY		0x100
/* Policy variable inhibit-any-policy */
#define	X509_V_FLAG_INHIBIT_ANY			0x200
/* Policy variable inhibit-policy-mapping */
#define X509_V_FLAG_INHIBIT_MAP			0x400
/* Notify callback that policy is OK */
#define X509_V_FLAG_NOTIFY_POLICY		0x800
/* Extended CRL features such as indirect CRLs, alternate CRL signing keys */
#define X509_V_FLAG_EXTENDED_CRL_SUPPORT	0x1000
/* Delta CRL support */
#define X509_V_FLAG_USE_DELTAS			0x2000
/* Check selfsigned CA signature */
#define X509_V_FLAG_CHECK_SS_SIGNATURE		0x4000
/* Use trusted store first */
#define X509_V_FLAG_TRUSTED_FIRST		0x8000
/* Allow partial chains if at least one certificate is in trusted store */
#define X509_V_FLAG_PARTIAL_CHAIN		0x80000

/* If the initial chain is not trusted, do not attempt to build an alternative
 * chain. Alternate chain checking was introduced in 1.0.2b. Setting this flag
 * will force the behaviour to match that of previous versions. */
#define X509_V_FLAG_NO_ALT_CHAINS		0x100000

/* Do not check certificate or CRL validity against current time. */
#define X509_V_FLAG_NO_CHECK_TIME		0x200000

/* Force the use of the legacy certificate verification */
#define X509_V_FLAG_LEGACY_VERIFY		0x400000

#define X509_VP_FLAG_DEFAULT			0x1
#define X509_VP_FLAG_OVERWRITE			0x2
#define X509_VP_FLAG_RESET_FLAGS		0x4
#define X509_VP_FLAG_LOCKED			0x8
#define X509_VP_FLAG_ONCE			0x10

/*
 * Obsolete internal use: mask of policy related options.
 * This should really go away.
 */
#define X509_V_FLAG_POLICY_MASK (X509_V_FLAG_POLICY_CHECK \
				| X509_V_FLAG_EXPLICIT_POLICY \
				| X509_V_FLAG_INHIBIT_ANY \
				| X509_V_FLAG_INHIBIT_MAP)

X509_OBJECT *X509_OBJECT_new(void);
void X509_OBJECT_free(X509_OBJECT *a);
int X509_OBJECT_idx_by_subject(STACK_OF(X509_OBJECT) *h, X509_LOOKUP_TYPE type,
	     X509_NAME *name);
X509_OBJECT *X509_OBJECT_retrieve_by_subject(STACK_OF(X509_OBJECT) *h,
    X509_LOOKUP_TYPE type, X509_NAME *name);
X509_OBJECT *X509_OBJECT_retrieve_match(STACK_OF(X509_OBJECT) *h, X509_OBJECT *x);
X509_LOOKUP_TYPE X509_OBJECT_get_type(const X509_OBJECT *a);
X509 *X509_OBJECT_get0_X509(const X509_OBJECT *xo);
X509_CRL *X509_OBJECT_get0_X509_CRL(X509_OBJECT *xo);

X509_STORE *X509_STORE_new(void);
void X509_STORE_free(X509_STORE *v);
int X509_STORE_up_ref(X509_STORE *x);
#define X509_STORE_get1_certs X509_STORE_CTX_get1_certs
#define X509_STORE_get1_crls X509_STORE_CTX_get1_crls
STACK_OF(X509) *X509_STORE_CTX_get1_certs(X509_STORE_CTX *st, X509_NAME *nm);
STACK_OF(X509_CRL) *X509_STORE_CTX_get1_crls(X509_STORE_CTX *st, X509_NAME *nm);
STACK_OF(X509_OBJECT) *X509_STORE_get0_objects(X509_STORE *xs);
STACK_OF(X509_OBJECT) *X509_STORE_get1_objects(X509_STORE *xs);
void *X509_STORE_get_ex_data(X509_STORE *xs, int idx);
int X509_STORE_set_ex_data(X509_STORE *xs, int idx, void *data);

#define X509_STORE_get_ex_new_index(l, p, newf, dupf, freef) \
	CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_X509_STORE, (l), (p), \
		(newf), (dupf), (freef))

int X509_STORE_set_flags(X509_STORE *ctx, unsigned long flags);
int X509_STORE_set_purpose(X509_STORE *ctx, int purpose);
int X509_STORE_set_trust(X509_STORE *ctx, int trust);
int X509_STORE_set1_param(X509_STORE *ctx, X509_VERIFY_PARAM *pm);
X509_VERIFY_PARAM *X509_STORE_get0_param(X509_STORE *ctx);

typedef int (*X509_STORE_CTX_verify_cb)(int, X509_STORE_CTX *);

X509_STORE_CTX_verify_cb X509_STORE_get_verify_cb(X509_STORE *);

void X509_STORE_set_verify_cb(X509_STORE *ctx,
    int (*verify_cb)(int, X509_STORE_CTX *));
#define X509_STORE_set_verify_cb_func(ctx, func) \
    X509_STORE_set_verify_cb((ctx), (func))

typedef int (*X509_STORE_CTX_check_issued_fn)(X509_STORE_CTX *ctx,
    X509 *subject, X509 *issuer);

X509_STORE_CTX_check_issued_fn X509_STORE_get_check_issued(X509_STORE *store);
void X509_STORE_set_check_issued(X509_STORE *store,
    X509_STORE_CTX_check_issued_fn check_issued);
X509_STORE_CTX_check_issued_fn
    X509_STORE_CTX_get_check_issued(X509_STORE_CTX *ctx);

X509_STORE_CTX *X509_STORE_CTX_new(void);

int X509_STORE_CTX_get1_issuer(X509 **issuer, X509_STORE_CTX *ctx, X509 *x);

void X509_STORE_CTX_free(X509_STORE_CTX *ctx);
int X509_STORE_CTX_init(X509_STORE_CTX *ctx, X509_STORE *store,
			 X509 *x509, STACK_OF(X509) *chain);
X509 *X509_STORE_CTX_get0_cert(X509_STORE_CTX *ctx);
STACK_OF(X509) *X509_STORE_CTX_get0_chain(X509_STORE_CTX *xs);
X509_STORE *X509_STORE_CTX_get0_store(X509_STORE_CTX *xs);
STACK_OF(X509) *X509_STORE_CTX_get0_untrusted(X509_STORE_CTX *ctx);
void X509_STORE_CTX_set0_untrusted(X509_STORE_CTX *ctx, STACK_OF(X509) *sk);
void X509_STORE_CTX_trusted_stack(X509_STORE_CTX *ctx, STACK_OF(X509) *sk);
void X509_STORE_CTX_set0_trusted_stack(X509_STORE_CTX *ctx, STACK_OF(X509) *sk);
void X509_STORE_CTX_cleanup(X509_STORE_CTX *ctx);

X509_LOOKUP *X509_STORE_add_lookup(X509_STORE *v, const X509_LOOKUP_METHOD *m);

const X509_LOOKUP_METHOD *X509_LOOKUP_hash_dir(void);
const X509_LOOKUP_METHOD *X509_LOOKUP_file(void);
const X509_LOOKUP_METHOD *X509_LOOKUP_mem(void);

int X509_STORE_add_cert(X509_STORE *ctx, X509 *x);
int X509_STORE_add_crl(X509_STORE *ctx, X509_CRL *x);

int X509_STORE_CTX_get_by_subject(X509_STORE_CTX *vs, X509_LOOKUP_TYPE type,
    X509_NAME *name, X509_OBJECT *ret);
#define X509_STORE_get_by_subject X509_STORE_CTX_get_by_subject
X509_OBJECT *X509_STORE_CTX_get_obj_by_subject(X509_STORE_CTX *vs,
    X509_LOOKUP_TYPE type, X509_NAME *name);

int X509_LOOKUP_ctrl(X509_LOOKUP *ctx, int cmd, const char *argc,
	long argl, char **ret);

int X509_load_cert_file(X509_LOOKUP *ctx, const char *file, int type);
int X509_load_crl_file(X509_LOOKUP *ctx, const char *file, int type);
int X509_load_cert_crl_file(X509_LOOKUP *ctx, const char *file, int type);

void X509_LOOKUP_free(X509_LOOKUP *ctx);

int	X509_STORE_load_locations(X509_STORE *ctx,
		const char *file, const char *dir);
int	X509_STORE_load_mem(X509_STORE *ctx, void *buf, int len);
int	X509_STORE_set_default_paths(X509_STORE *ctx);

int X509_STORE_CTX_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
	CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func);
int	X509_STORE_CTX_set_ex_data(X509_STORE_CTX *ctx,int idx,void *data);
void *	X509_STORE_CTX_get_ex_data(X509_STORE_CTX *ctx,int idx);
int	X509_STORE_CTX_get_error(X509_STORE_CTX *ctx);
void	X509_STORE_CTX_set_error(X509_STORE_CTX *ctx,int s);
int	X509_STORE_CTX_get_error_depth(X509_STORE_CTX *ctx);
void	X509_STORE_CTX_set_error_depth(X509_STORE_CTX *ctx, int depth);
X509 *	X509_STORE_CTX_get_current_cert(X509_STORE_CTX *ctx);
void	X509_STORE_CTX_set_current_cert(X509_STORE_CTX *ctx, X509 *x);
X509 *X509_STORE_CTX_get0_current_issuer(X509_STORE_CTX *ctx);
X509_CRL *X509_STORE_CTX_get0_current_crl(X509_STORE_CTX *ctx);
X509_STORE_CTX *X509_STORE_CTX_get0_parent_ctx(X509_STORE_CTX *ctx);
STACK_OF(X509) *X509_STORE_CTX_get_chain(X509_STORE_CTX *ctx);
STACK_OF(X509) *X509_STORE_CTX_get1_chain(X509_STORE_CTX *ctx);
void	X509_STORE_CTX_set_cert(X509_STORE_CTX *c,X509 *x);
void	X509_STORE_CTX_set_chain(X509_STORE_CTX *c,STACK_OF(X509) *sk);
void	X509_STORE_CTX_set0_crls(X509_STORE_CTX *c,STACK_OF(X509_CRL) *sk);
int X509_STORE_CTX_set_purpose(X509_STORE_CTX *ctx, int purpose);
int X509_STORE_CTX_set_trust(X509_STORE_CTX *ctx, int trust);
void X509_STORE_CTX_set_flags(X509_STORE_CTX *ctx, unsigned long flags);
void X509_STORE_CTX_set_time(X509_STORE_CTX *ctx, unsigned long flags,
								time_t t);
void X509_STORE_CTX_set0_verified_chain(X509_STORE_CTX *ctx, STACK_OF(X509) *sk);
int (*X509_STORE_CTX_get_verify(X509_STORE_CTX *ctx))(X509_STORE_CTX *);
void X509_STORE_CTX_set_verify(X509_STORE_CTX *ctx,
    int (*verify)(X509_STORE_CTX *));
int (*X509_STORE_CTX_get_verify_cb(X509_STORE_CTX *ctx))(int, X509_STORE_CTX *);
void X509_STORE_CTX_set_verify_cb(X509_STORE_CTX *ctx,
				  int (*verify_cb)(int, X509_STORE_CTX *));

typedef int (*X509_STORE_CTX_verify_fn)(X509_STORE_CTX *);

void X509_STORE_set_verify(X509_STORE *ctx, X509_STORE_CTX_verify_fn verify);
X509_STORE_CTX_verify_fn X509_STORE_get_verify(X509_STORE *ctx);
#define X509_STORE_set_verify_func(ctx, func) \
    X509_STORE_set_verify((ctx), (func))

int X509_STORE_CTX_get_num_untrusted(X509_STORE_CTX *ctx);

X509_VERIFY_PARAM *X509_STORE_CTX_get0_param(X509_STORE_CTX *ctx);
void X509_STORE_CTX_set0_param(X509_STORE_CTX *ctx, X509_VERIFY_PARAM *param);
int X509_STORE_CTX_set_default(X509_STORE_CTX *ctx, const char *name);

/* X509_VERIFY_PARAM functions */

X509_VERIFY_PARAM *X509_VERIFY_PARAM_new(void);
void X509_VERIFY_PARAM_free(X509_VERIFY_PARAM *param);
int X509_VERIFY_PARAM_inherit(X509_VERIFY_PARAM *to,
						const X509_VERIFY_PARAM *from);
int X509_VERIFY_PARAM_set1(X509_VERIFY_PARAM *to,
						const X509_VERIFY_PARAM *from);
int X509_VERIFY_PARAM_set1_name(X509_VERIFY_PARAM *param, const char *name);
int X509_VERIFY_PARAM_set_flags(X509_VERIFY_PARAM *param, unsigned long flags);
int X509_VERIFY_PARAM_clear_flags(X509_VERIFY_PARAM *param,
							unsigned long flags);
unsigned long X509_VERIFY_PARAM_get_flags(X509_VERIFY_PARAM *param);
int X509_VERIFY_PARAM_set_purpose(X509_VERIFY_PARAM *param, int purpose);
int X509_VERIFY_PARAM_set_trust(X509_VERIFY_PARAM *param, int trust);
void X509_VERIFY_PARAM_set_depth(X509_VERIFY_PARAM *param, int depth);
void X509_VERIFY_PARAM_set_auth_level(X509_VERIFY_PARAM *param, int auth_level);
time_t X509_VERIFY_PARAM_get_time(const X509_VERIFY_PARAM *param);
void X509_VERIFY_PARAM_set_time(X509_VERIFY_PARAM *param, time_t t);
int X509_VERIFY_PARAM_add0_policy(X509_VERIFY_PARAM *param,
						ASN1_OBJECT *policy);
int X509_VERIFY_PARAM_set1_policies(X509_VERIFY_PARAM *param,
					STACK_OF(ASN1_OBJECT) *policies);
int X509_VERIFY_PARAM_get_depth(const X509_VERIFY_PARAM *param);
int X509_VERIFY_PARAM_set1_host(X509_VERIFY_PARAM *param, const char *name,
    size_t namelen);
int X509_VERIFY_PARAM_add1_host(X509_VERIFY_PARAM *param, const char *name,
    size_t namelen);
void X509_VERIFY_PARAM_set_hostflags(X509_VERIFY_PARAM *param,
    unsigned int flags);
char *X509_VERIFY_PARAM_get0_peername(X509_VERIFY_PARAM *param);
int X509_VERIFY_PARAM_set1_email(X509_VERIFY_PARAM *param,  const char *email,
    size_t emaillen);
int X509_VERIFY_PARAM_set1_ip(X509_VERIFY_PARAM *param, const unsigned char *ip,
    size_t iplen);
int X509_VERIFY_PARAM_set1_ip_asc(X509_VERIFY_PARAM *param, const char *ipasc);
const char *X509_VERIFY_PARAM_get0_name(const X509_VERIFY_PARAM *param);
const X509_VERIFY_PARAM *X509_VERIFY_PARAM_get0(int id);
int X509_VERIFY_PARAM_get_count(void);

int X509_VERIFY_PARAM_add0_table(X509_VERIFY_PARAM *param);
const X509_VERIFY_PARAM *X509_VERIFY_PARAM_lookup(const char *name);
void X509_VERIFY_PARAM_table_cleanup(void);

#ifdef  __cplusplus
}
#endif
#endif
