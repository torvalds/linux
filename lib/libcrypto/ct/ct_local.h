/*	$OpenBSD: ct_local.h,v 1.8 2021/12/20 17:19:19 jsing Exp $ */
/*
 * Written by Rob Percival (robpercival@google.com) for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 2016 The OpenSSL Project.  All rights reserved.
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
 */

#include <stddef.h>

#include <openssl/ct.h>
#include <openssl/evp.h>
#include <openssl/safestack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "bytestring.h"

/* Number of bytes in an SCT v1 LogID - see RFC 6962 section 3.2. */
#define CT_V1_LOG_ID_LEN	32

/* Maximum size of an SCT - see RFC 6962 section 3.3. */
#define MAX_SCT_SIZE            65535
#define MAX_SCT_LIST_SIZE       MAX_SCT_SIZE

/*
 * Macros to write integers in network-byte order.
 */

#define s2n(s,c)        ((c[0]=(unsigned char)(((s)>> 8)&0xff), \
                          c[1]=(unsigned char)(((s)    )&0xff)),c+=2)

#define l2n3(l,c)       ((c[0]=(unsigned char)(((l)>>16)&0xff), \
                          c[1]=(unsigned char)(((l)>> 8)&0xff), \
                          c[2]=(unsigned char)(((l)    )&0xff)),c+=3)

#define l2n8(l,c)       (*((c)++)=(unsigned char)(((l)>>56)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>48)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>40)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>32)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>24)&0xff), \
                         *((c)++)=(unsigned char)(((l)>>16)&0xff), \
                         *((c)++)=(unsigned char)(((l)>> 8)&0xff), \
                         *((c)++)=(unsigned char)(((l)    )&0xff))

/* Signed Certificate Timestamp */
struct sct_st {
	sct_version_t version;
	/* If version is not SCT_VERSION_V1, this contains the encoded SCT */
	unsigned char *sct;
	size_t sct_len;
	/*
	 * If version is SCT_VERSION_V1, fields below contain components of
	 * the SCT
	 */
	unsigned char *log_id;
	size_t log_id_len;
	/*
	 * Note, we cannot distinguish between an unset timestamp, and one
	 * that is set to 0.  However since CT didn't exist in 1970, no real
	 * SCT should ever be set as such.
	 */
	uint64_t timestamp;
	unsigned char *ext;
	size_t ext_len;
	unsigned char hash_alg;
	unsigned char sig_alg;
	unsigned char *sig;
	size_t sig_len;
	/* Log entry type */
	ct_log_entry_type_t entry_type;
	/* Where this SCT was found, e.g. certificate, OCSP response, etc. */
	sct_source_t source;
	/* The result of the last attempt to validate this SCT. */
	sct_validation_status_t validation_status;
};

/* Miscellaneous data that is useful when verifying an SCT  */
struct sct_ctx_st {
	/* Public key */
	EVP_PKEY *pkey;
	/* Hash of public key */
	unsigned char *pkeyhash;
	size_t pkeyhashlen;
	/* For pre-certificate: issuer public key hash */
	unsigned char *ihash;
	size_t ihashlen;
	/* certificate encoding */
	unsigned char *certder;
	size_t certderlen;
	/* pre-certificate encoding */
	unsigned char *preder;
	size_t prederlen;
	/*
	 * milliseconds since epoch (to check that the SCT isn't from the
	 * future)
	 */
	uint64_t epoch_time_in_ms;
};

/* Context when evaluating whether a Certificate Transparency policy is met */
struct ct_policy_eval_ctx_st {
	X509 *cert;
	X509 *issuer;
	CTLOG_STORE *log_store;
	/*
	 * milliseconds since epoch (to check that the SCT isn't from the
	 * future)
	 */
	uint64_t epoch_time_in_ms;
};

/*
 * Creates a new context for verifying an SCT.
 */
SCT_CTX *SCT_CTX_new(void);
/*
 * Deletes an SCT verification context.
 */
void SCT_CTX_free(SCT_CTX *sctx);

/*
 * Sets the certificate that the SCT was created for.
 * If *cert does not have a poison extension, presigner must be NULL.
 * If *cert does not have a poison extension, it may have a single SCT
 * (NID_ct_precert_scts) extension.
 * If either *cert or *presigner have an AKID (NID_authority_key_identifier)
 * extension, both must have one.
 * Returns 1 on success, 0 on failure.
 */
int SCT_CTX_set1_cert(SCT_CTX *sctx, X509 *cert, X509 *presigner);

/*
 * Sets the issuer of the certificate that the SCT was created for.
 * This is just a convenience method to save extracting the public key and
 * calling SCT_CTX_set1_issuer_pubkey().
 * Issuer must not be NULL.
 * Returns 1 on success, 0 on failure.
 */
int SCT_CTX_set1_issuer(SCT_CTX *sctx, const X509 *issuer);

/*
 * Sets the public key of the issuer of the certificate that the SCT was created
 * for.
 * The public key must not be NULL.
 * Returns 1 on success, 0 on failure.
 */
int SCT_CTX_set1_issuer_pubkey(SCT_CTX *sctx, X509_PUBKEY *pubkey);

/*
 * Sets the public key of the CT log that the SCT is from.
 * Returns 1 on success, 0 on failure.
 */
int SCT_CTX_set1_pubkey(SCT_CTX *sctx, X509_PUBKEY *pubkey);

/*
 * Sets the time to evaluate the SCT against, in milliseconds since the Unix
 * epoch. If the SCT's timestamp is after this time, it will be interpreted as
 * having been issued in the future. RFC6962 states that "TLS clients MUST
 * reject SCTs whose timestamp is in the future", so an SCT will not validate
 * in this case.
 */
void SCT_CTX_set_time(SCT_CTX *sctx, uint64_t time_in_ms);

/*
 * Verifies an SCT with the given context.
 * Returns 1 if the SCT verifies successfully; any other value indicates
 * failure. See EVP_DigestVerifyFinal() for the meaning of those values.
 */
int SCT_CTX_verify(const SCT_CTX *sctx, const SCT *sct);

/*
 * Does this SCT have the minimum fields populated to be usable?
 * Returns 1 if so, 0 otherwise.
 */
int SCT_is_complete(const SCT *sct);

/*
 * Does this SCT have the signature-related fields populated?
 * Returns 1 if so, 0 otherwise.
 * This checks that the signature and hash algorithms are set to supported
 * values and that the signature field is set.
 */
int SCT_signature_is_complete(const SCT *sct);

/*
 * TODO(RJPercival): Create an SCT_signature struct and make i2o_SCT_signature
 * and o2i_SCT_signature conform to the i2d/d2i conventions.
 */

/*
 * Serialize (to TLS format) an |sct| signature and write it to |out|.
 * If |out| is null, no signature will be output but the length will be returned.
 * If |out| points to a null pointer, a string will be allocated to hold the
 * TLS-format signature. It is the responsibility of the caller to free it.
 * If |out| points to an allocated string, the signature will be written to it.
 * The length of the signature in TLS format will be returned.
 */
int i2o_SCT_signature(const SCT *sct, unsigned char **out);

/*
 * Parses an SCT signature in TLS format and populates the |sct| with it.
 * |in| should be a pointer to a string containing the TLS-format signature.
 * |in| will be advanced to the end of the signature if parsing succeeds.
 * |len| should be the length of the signature in |in|.
 * Returns the number of bytes parsed, or a negative integer if an error occurs.
 * If an error occurs, the SCT's signature NID may be updated whilst the
 * signature field itself remains unset.
 */
int o2i_SCT_signature(SCT *sct, CBS *cbs);

/*
 * Handlers for Certificate Transparency X509v3/OCSP extensions
 */
extern const X509V3_EXT_METHOD v3_ct_scts[3];
