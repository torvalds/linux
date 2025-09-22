/*	$OpenBSD: ct.h,v 1.8 2024/08/08 23:50:29 tb Exp $ */
/*
 * Public API for Certificate Transparency (CT).
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

#ifndef HEADER_CT_H
#define HEADER_CT_H

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_CT
#include <openssl/ossl_typ.h>
#include <openssl/safestack.h>
#include <openssl/x509.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Minimum RSA key size, from RFC6962 */
#define SCT_MIN_RSA_BITS 2048

/* All hashes are SHA256 in v1 of Certificate Transparency */
#define CT_V1_HASHLEN SHA256_DIGEST_LENGTH

typedef enum {
	CT_LOG_ENTRY_TYPE_NOT_SET = -1,
	CT_LOG_ENTRY_TYPE_X509 = 0,
	CT_LOG_ENTRY_TYPE_PRECERT = 1
} ct_log_entry_type_t;

typedef enum {
	SCT_VERSION_NOT_SET = -1,
	SCT_VERSION_V1 = 0
} sct_version_t;

typedef enum {
	SCT_SOURCE_UNKNOWN,
	SCT_SOURCE_TLS_EXTENSION,
	SCT_SOURCE_X509V3_EXTENSION,
	SCT_SOURCE_OCSP_STAPLED_RESPONSE
} sct_source_t;

typedef enum {
	SCT_VALIDATION_STATUS_NOT_SET,
	SCT_VALIDATION_STATUS_UNKNOWN_LOG,
	SCT_VALIDATION_STATUS_VALID,
	SCT_VALIDATION_STATUS_INVALID,
	SCT_VALIDATION_STATUS_UNVERIFIED,
	SCT_VALIDATION_STATUS_UNKNOWN_VERSION
} sct_validation_status_t;

DECLARE_STACK_OF(SCT)
DECLARE_STACK_OF(CTLOG)

/******************************************
 * CT policy evaluation context functions *
 ******************************************/

/*
 * Creates a new, empty policy evaluation context.
 * The caller is responsible for calling CT_POLICY_EVAL_CTX_free when finished
 * with the CT_POLICY_EVAL_CTX.
 */
CT_POLICY_EVAL_CTX *CT_POLICY_EVAL_CTX_new(void);

/* Deletes a policy evaluation context and anything it owns. */
void CT_POLICY_EVAL_CTX_free(CT_POLICY_EVAL_CTX *ctx);

/* Gets the peer certificate that the SCTs are for */
X509 *CT_POLICY_EVAL_CTX_get0_cert(const CT_POLICY_EVAL_CTX *ctx);

/*
 * Sets the certificate associated with the received SCTs.
 * Increments the reference count of cert.
 * Returns 1 on success, 0 otherwise.
 */
int CT_POLICY_EVAL_CTX_set1_cert(CT_POLICY_EVAL_CTX *ctx, X509 *cert);

/* Gets the issuer of the aforementioned certificate */
X509 *CT_POLICY_EVAL_CTX_get0_issuer(const CT_POLICY_EVAL_CTX *ctx);

/*
 * Sets the issuer of the certificate associated with the received SCTs.
 * Increments the reference count of issuer.
 * Returns 1 on success, 0 otherwise.
 */
int CT_POLICY_EVAL_CTX_set1_issuer(CT_POLICY_EVAL_CTX *ctx, X509 *issuer);

/* Gets the CT logs that are trusted sources of SCTs */
const CTLOG_STORE *CT_POLICY_EVAL_CTX_get0_log_store(const CT_POLICY_EVAL_CTX *ctx);

/* Sets the log store that is in use. It must outlive the CT_POLICY_EVAL_CTX. */
void CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE(CT_POLICY_EVAL_CTX *ctx,
                                               CTLOG_STORE *log_store);

/*
 * Gets the time, in milliseconds since the Unix epoch, that will be used as the
 * current time when checking whether an SCT was issued in the future.
 * Such SCTs will fail validation, as required by RFC6962.
 */
uint64_t CT_POLICY_EVAL_CTX_get_time(const CT_POLICY_EVAL_CTX *ctx);

/*
 * Sets the time to evaluate SCTs against, in milliseconds since the Unix epoch.
 * If an SCT's timestamp is after this time, it will be interpreted as having
 * been issued in the future. RFC6962 states that "TLS clients MUST reject SCTs
 * whose timestamp is in the future", so an SCT will not validate in this case.
 */
void CT_POLICY_EVAL_CTX_set_time(CT_POLICY_EVAL_CTX *ctx, uint64_t time_in_ms);

/*****************
 * SCT functions *
 *****************/

/*
 * Creates a new, blank SCT.
 * The caller is responsible for calling SCT_free when finished with the SCT.
 */
SCT *SCT_new(void);

/*
 * Creates a new SCT from some base64-encoded strings.
 * The caller is responsible for calling SCT_free when finished with the SCT.
 */
SCT *SCT_new_from_base64(unsigned char version, const char *logid_base64,
    ct_log_entry_type_t entry_type, uint64_t timestamp,
    const char *extensions_base64, const char *signature_base64);

/*
 * Frees the SCT and the underlying data structures.
 */
void SCT_free(SCT *sct);

/*
 * Free a stack of SCTs, and the underlying SCTs themselves.
 * Intended to be compatible with X509V3_EXT_FREE.
 */
void SCT_LIST_free(STACK_OF(SCT) *a);

/*
 * Returns the version of the SCT.
 */
sct_version_t SCT_get_version(const SCT *sct);

/*
 * Set the version of an SCT.
 * Returns 1 on success, 0 if the version is unrecognized.
 */
int SCT_set_version(SCT *sct, sct_version_t version);

/*
 * Returns the log entry type of the SCT.
 */
ct_log_entry_type_t SCT_get_log_entry_type(const SCT *sct);

/*
 * Set the log entry type of an SCT.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set_log_entry_type(SCT *sct, ct_log_entry_type_t entry_type);

/*
 * Gets the ID of the log that an SCT came from.
 * Ownership of the log ID remains with the SCT.
 * Returns the length of the log ID.
 */
size_t SCT_get0_log_id(const SCT *sct, unsigned char **log_id);

/*
 * Set the log ID of an SCT to point directly to the *log_id specified.
 * The SCT takes ownership of the specified pointer.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set0_log_id(SCT *sct, unsigned char *log_id, size_t log_id_len);

/*
 * Set the log ID of an SCT.
 * This makes a copy of the log_id.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set1_log_id(SCT *sct, const unsigned char *log_id,
                           size_t log_id_len);

/*
 * Returns the timestamp for the SCT (epoch time in milliseconds).
 */
uint64_t SCT_get_timestamp(const SCT *sct);

/*
 * Set the timestamp of an SCT (epoch time in milliseconds).
 */
void SCT_set_timestamp(SCT *sct, uint64_t timestamp);

/*
 * Return the NID for the signature used by the SCT.
 * For CT v1, this will be either NID_sha256WithRSAEncryption or
 * NID_ecdsa_with_SHA256 (or NID_undef if incorrect/unset).
 */
int SCT_get_signature_nid(const SCT *sct);

/*
 * Set the signature type of an SCT
 * For CT v1, this should be either NID_sha256WithRSAEncryption or
 * NID_ecdsa_with_SHA256.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set_signature_nid(SCT *sct, int nid);

/*
 * Set *ext to point to the extension data for the SCT. ext must not be NULL.
 * The SCT retains ownership of this pointer.
 * Returns length of the data pointed to.
 */
size_t SCT_get0_extensions(const SCT *sct, unsigned char **ext);

/*
 * Set the extensions of an SCT to point directly to the *ext specified.
 * The SCT takes ownership of the specified pointer.
 */
void SCT_set0_extensions(SCT *sct, unsigned char *ext, size_t ext_len);

/*
 * Set the extensions of an SCT.
 * This takes a copy of the ext.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set1_extensions(SCT *sct, const unsigned char *ext,
                               size_t ext_len);

/*
 * Set *sig to point to the signature for the SCT. sig must not be NULL.
 * The SCT retains ownership of this pointer.
 * Returns length of the data pointed to.
 */
size_t SCT_get0_signature(const SCT *sct, unsigned char **sig);

/*
 * Set the signature of an SCT to point directly to the *sig specified.
 * The SCT takes ownership of the specified pointer.
 */
void SCT_set0_signature(SCT *sct, unsigned char *sig, size_t sig_len);

/*
 * Set the signature of an SCT to be a copy of the *sig specified.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set1_signature(SCT *sct, const unsigned char *sig,
    size_t sig_len);

/*
 * The origin of this SCT, e.g. TLS extension, OCSP response, etc.
 */
sct_source_t SCT_get_source(const SCT *sct);

/*
 * Set the origin of this SCT, e.g. TLS extension, OCSP response, etc.
 * Returns 1 on success, 0 otherwise.
 */
int SCT_set_source(SCT *sct, sct_source_t source);

/*
 * Returns a text string describing the validation status of |sct|.
 */
const char *SCT_validation_status_string(const SCT *sct);

/*
 * Pretty-prints an |sct| to |out|.
 * It will be indented by the number of spaces specified by |indent|.
 * If |logs| is not NULL, it will be used to lookup the CT log that the SCT came
 * from, so that the log name can be printed.
 */
void SCT_print(const SCT *sct, BIO *out, int indent, const CTLOG_STORE *logs);

/*
 * Pretty-prints an |sct_list| to |out|.
 * It will be indented by the number of spaces specified by |indent|.
 * SCTs will be delimited by |separator|.
 * If |logs| is not NULL, it will be used to lookup the CT log that each SCT
 * came from, so that the log names can be printed.
 */
void SCT_LIST_print(const STACK_OF(SCT) *sct_list, BIO *out, int indent,
    const char *separator, const CTLOG_STORE *logs);

/*
 * Gets the last result of validating this SCT.
 * If it has not been validated yet, returns SCT_VALIDATION_STATUS_NOT_SET.
 */
sct_validation_status_t SCT_get_validation_status(const SCT *sct);

/*
 * Validates the given SCT with the provided context.
 * Sets the "validation_status" field of the SCT.
 * Returns 1 if the SCT is valid and the signature verifies.
 * Returns 0 if the SCT is invalid or could not be verified.
 * Returns -1 if an error occurs.
 */
int SCT_validate(SCT *sct, const CT_POLICY_EVAL_CTX *ctx);

/*
 * Validates the given list of SCTs with the provided context.
 * Sets the "validation_status" field of each SCT.
 * Returns 1 if there are no invalid SCTs and all signatures verify.
 * Returns 0 if at least one SCT is invalid or could not be verified.
 * Returns a negative integer if an error occurs.
 */
int SCT_LIST_validate(const STACK_OF(SCT) *scts,
    CT_POLICY_EVAL_CTX *ctx);


/*********************************
 * SCT parsing and serialisation *
 *********************************/

/*
 * Serialize (to TLS format) a stack of SCTs and return the length.
 * "a" must not be NULL.
 * If "pp" is NULL, just return the length of what would have been serialized.
 * If "pp" is not NULL and "*pp" is null, function will allocate a new pointer
 * for data that caller is responsible for freeing (only if function returns
 * successfully).
 * If "pp" is NULL and "*pp" is not NULL, caller is responsible for ensuring
 * that "*pp" is large enough to accept all of the serialized data.
 * Returns < 0 on error, >= 0 indicating bytes written (or would have been)
 * on success.
 */
int i2o_SCT_LIST(const STACK_OF(SCT) *a, unsigned char **pp);

/*
 * Convert TLS format SCT list to a stack of SCTs.
 * If "a" or "*a" is NULL, a new stack will be created that the caller is
 * responsible for freeing (by calling SCT_LIST_free).
 * "**pp" and "*pp" must not be NULL.
 * Upon success, "*pp" will point to after the last bytes read, and a stack
 * will be returned.
 * Upon failure, a NULL pointer will be returned, and the position of "*pp" is
 * not defined.
 */
STACK_OF(SCT) *o2i_SCT_LIST(STACK_OF(SCT) **a, const unsigned char **pp,
    size_t len);

/*
 * Serialize (to DER format) a stack of SCTs and return the length.
 * "a" must not be NULL.
 * If "pp" is NULL, just returns the length of what would have been serialized.
 * If "pp" is not NULL and "*pp" is null, function will allocate a new pointer
 * for data that caller is responsible for freeing (only if function returns
 * successfully).
 * If "pp" is NULL and "*pp" is not NULL, caller is responsible for ensuring
 * that "*pp" is large enough to accept all of the serialized data.
 * Returns < 0 on error, >= 0 indicating bytes written (or would have been)
 * on success.
 */
int i2d_SCT_LIST(const STACK_OF(SCT) *a, unsigned char **pp);

/*
 * Parses an SCT list in DER format and returns it.
 * If "a" or "*a" is NULL, a new stack will be created that the caller is
 * responsible for freeing (by calling SCT_LIST_free).
 * "**pp" and "*pp" must not be NULL.
 * Upon success, "*pp" will point to after the last bytes read, and a stack
 * will be returned.
 * Upon failure, a NULL pointer will be returned, and the position of "*pp" is
 * not defined.
 */
STACK_OF(SCT) *d2i_SCT_LIST(STACK_OF(SCT) **a, const unsigned char **pp,
    long len);

/*
 * Serialize (to TLS format) an |sct| and write it to |out|.
 * If |out| is null, no SCT will be output but the length will still be returned.
 * If |out| points to a null pointer, a string will be allocated to hold the
 * TLS-format SCT. It is the responsibility of the caller to free it.
 * If |out| points to an allocated string, the TLS-format SCT will be written
 * to it.
 * The length of the SCT in TLS format will be returned.
 */
int i2o_SCT(const SCT *sct, unsigned char **out);

/*
 * Parses an SCT in TLS format and returns it.
 * If |psct| is not null, it will end up pointing to the parsed SCT. If it
 * already points to a non-null pointer, the pointer will be free'd.
 * |in| should be a pointer to a string containing the TLS-format SCT.
 * |in| will be advanced to the end of the SCT if parsing succeeds.
 * |len| should be the length of the SCT in |in|.
 * Returns NULL if an error occurs.
 * If the SCT is an unsupported version, only the SCT's 'sct' and 'sct_len'
 * fields will be populated (with |in| and |len| respectively).
 */
SCT *o2i_SCT(SCT **psct, const unsigned char **in, size_t len);

/********************
 * CT log functions *
 ********************/

/*
 * Creates a new CT log instance with the given |public_key| and |name|.
 * Takes ownership of |public_key| but copies |name|.
 * Returns NULL if malloc fails or if |public_key| cannot be converted to DER.
 * Should be deleted by the caller using CTLOG_free when no longer needed.
 */
CTLOG *CTLOG_new(EVP_PKEY *public_key, const char *name);

/*
 * Creates a new CTLOG instance with the base64-encoded SubjectPublicKeyInfo DER
 * in |pkey_base64|. The |name| is a string to help users identify this log.
 * Returns 1 on success, 0 on failure.
 * Should be deleted by the caller using CTLOG_free when no longer needed.
 */
int CTLOG_new_from_base64(CTLOG **ct_log, const char *pkey_base64,
    const char *name);

/*
 * Deletes a CT log instance and its fields.
 */
void CTLOG_free(CTLOG *log);

/* Gets the name of the CT log */
const char *CTLOG_get0_name(const CTLOG *log);
/* Gets the ID of the CT log */
void CTLOG_get0_log_id(const CTLOG *log, const uint8_t **log_id,
                       size_t *log_id_len);
/* Gets the public key of the CT log */
EVP_PKEY *CTLOG_get0_public_key(const CTLOG *log);

/**************************
 * CT log store functions *
 **************************/

/*
 * Creates a new CT log store.
 * Should be deleted by the caller using CTLOG_STORE_free when no longer needed.
 */
CTLOG_STORE *CTLOG_STORE_new(void);

/*
 * Deletes a CT log store and all of the CT log instances held within.
 */
void CTLOG_STORE_free(CTLOG_STORE *store);

/*
 * Finds a CT log in the store based on its log ID.
 * Returns the CT log, or NULL if no match is found.
 */
const CTLOG *CTLOG_STORE_get0_log_by_id(const CTLOG_STORE *store,
    const uint8_t *log_id, size_t log_id_len);

/*
 * Loads a CT log list into a |store| from a |file|.
 * Returns 1 if loading is successful, or 0 otherwise.
 */
int CTLOG_STORE_load_file(CTLOG_STORE *store, const char *file);

/*
 * Loads the default CT log list into a |store|.
 * Returns 1 if loading is successful, or 0 otherwise.
 */
int CTLOG_STORE_load_default_file(CTLOG_STORE *store);

int ERR_load_CT_strings(void);

/*
 * CT function codes.
 */
#  define CT_F_CTLOG_NEW                                   117
#  define CT_F_CTLOG_NEW_FROM_BASE64                       118
#  define CT_F_CTLOG_NEW_FROM_CONF                         119
#  define CT_F_CTLOG_STORE_LOAD_CTX_NEW                    122
#  define CT_F_CTLOG_STORE_LOAD_FILE                       123
#  define CT_F_CTLOG_STORE_LOAD_LOG                        130
#  define CT_F_CTLOG_STORE_NEW                             131
#  define CT_F_CT_BASE64_DECODE                            124
#  define CT_F_CT_POLICY_EVAL_CTX_NEW                      133
#  define CT_F_CT_V1_LOG_ID_FROM_PKEY                      125
#  define CT_F_I2O_SCT                                     107
#  define CT_F_I2O_SCT_LIST                                108
#  define CT_F_I2O_SCT_SIGNATURE                           109
#  define CT_F_O2I_SCT                                     110
#  define CT_F_O2I_SCT_LIST                                111
#  define CT_F_O2I_SCT_SIGNATURE                           112
#  define CT_F_SCT_CTX_NEW                                 126
#  define CT_F_SCT_CTX_VERIFY                              128
#  define CT_F_SCT_NEW                                     100
#  define CT_F_SCT_NEW_FROM_BASE64                         127
#  define CT_F_SCT_SET0_LOG_ID                             101
#  define CT_F_SCT_SET1_EXTENSIONS                         114
#  define CT_F_SCT_SET1_LOG_ID                             115
#  define CT_F_SCT_SET1_SIGNATURE                          116
#  define CT_F_SCT_SET_LOG_ENTRY_TYPE                      102
#  define CT_F_SCT_SET_SIGNATURE_NID                       103
#  define CT_F_SCT_SET_VERSION                             104

/*
 * CT reason codes.
 */
#  define CT_R_BASE64_DECODE_ERROR                         108
#  define CT_R_INVALID_LOG_ID_LENGTH                       100
#  define CT_R_LOG_CONF_INVALID                            109
#  define CT_R_LOG_CONF_INVALID_KEY                        110
#  define CT_R_LOG_CONF_MISSING_DESCRIPTION                111
#  define CT_R_LOG_CONF_MISSING_KEY                        112
#  define CT_R_LOG_KEY_INVALID                             113
#  define CT_R_SCT_FUTURE_TIMESTAMP                        116
#  define CT_R_SCT_INVALID                                 104
#  define CT_R_SCT_INVALID_SIGNATURE                       107
#  define CT_R_SCT_LIST_INVALID                            105
#  define CT_R_SCT_LOG_ID_MISMATCH                         114
#  define CT_R_SCT_NOT_SET                                 106
#  define CT_R_SCT_UNSUPPORTED_VERSION                     115
#  define CT_R_UNRECOGNIZED_SIGNATURE_NID                  101
#  define CT_R_UNSUPPORTED_ENTRY_TYPE                      102
#  define CT_R_UNSUPPORTED_VERSION                         103

#ifdef __cplusplus
}
#endif
#endif
#endif
