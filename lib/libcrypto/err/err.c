/* $OpenBSD: err.c,v 1.78 2025/06/10 08:53:37 tb Exp $ */
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

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/lhash.h>

DECLARE_LHASH_OF(ERR_STRING_DATA);
DECLARE_LHASH_OF(ERR_STATE);

typedef struct err_state_st {
	pthread_t tid;
	int err_flags[ERR_NUM_ERRORS];
	unsigned long err_buffer[ERR_NUM_ERRORS];
	char *err_data[ERR_NUM_ERRORS];
	int err_data_flags[ERR_NUM_ERRORS];
	const char *err_file[ERR_NUM_ERRORS];
	int err_line[ERR_NUM_ERRORS];
	int top, bottom;
} ERR_STATE;

#ifndef OPENSSL_NO_ERR
static const ERR_STRING_DATA ERR_str_libraries[] = {
	{ERR_PACK(ERR_LIB_NONE, 0, 0),		"unknown library"},
	{ERR_PACK(ERR_LIB_SYS, 0, 0),		"system library"},
	{ERR_PACK(ERR_LIB_BN, 0, 0),		"bignum routines"},
	{ERR_PACK(ERR_LIB_RSA, 0, 0),		"rsa routines"},
	{ERR_PACK(ERR_LIB_DH, 0, 0),		"Diffie-Hellman routines"},
	{ERR_PACK(ERR_LIB_EVP, 0, 0),		"digital envelope routines"},
	{ERR_PACK(ERR_LIB_BUF, 0, 0),		"memory buffer routines"},
	{ERR_PACK(ERR_LIB_OBJ, 0, 0),		"object identifier routines"},
	{ERR_PACK(ERR_LIB_PEM, 0, 0),		"PEM routines"},
	{ERR_PACK(ERR_LIB_DSA, 0, 0),		"dsa routines"},
	{ERR_PACK(ERR_LIB_X509, 0, 0),		"x509 certificate routines"},
	{ERR_PACK(ERR_LIB_ASN1, 0, 0),		"asn1 encoding routines"},
	{ERR_PACK(ERR_LIB_CONF, 0, 0),		"configuration file routines"},
	{ERR_PACK(ERR_LIB_CRYPTO, 0, 0),	"common libcrypto routines"},
	{ERR_PACK(ERR_LIB_EC, 0, 0),		"elliptic curve routines"},
	{ERR_PACK(ERR_LIB_SSL, 0, 0),		"SSL routines"},
	{ERR_PACK(ERR_LIB_BIO, 0, 0),		"BIO routines"},
	{ERR_PACK(ERR_LIB_PKCS7, 0, 0),		"PKCS7 routines"},
	{ERR_PACK(ERR_LIB_X509V3, 0, 0),	"X509 V3 routines"},
	{ERR_PACK(ERR_LIB_PKCS12, 0, 0),	"PKCS12 routines"},
	{ERR_PACK(ERR_LIB_RAND, 0, 0),		"random number generator"},
	{ERR_PACK(ERR_LIB_DSO, 0, 0),		"DSO support routines"},
	{ERR_PACK(ERR_LIB_TS, 0, 0),		"time stamp routines"},
	{ERR_PACK(ERR_LIB_ENGINE, 0, 0),	"engine routines"},
	{ERR_PACK(ERR_LIB_OCSP, 0, 0),		"OCSP routines"},
	{ERR_PACK(ERR_LIB_FIPS, 0, 0),		"FIPS routines"},
	{ERR_PACK(ERR_LIB_CMS, 0, 0),		"CMS routines"},
	{ERR_PACK(ERR_LIB_HMAC, 0, 0),		"HMAC routines"},
	{ERR_PACK(ERR_LIB_GOST, 0, 0),		"GOST routines"},
	{0, NULL},
};

static const ERR_STRING_DATA ERR_str_functs[] = {
	{ERR_PACK(ERR_LIB_SYS, SYS_F_FOPEN, 0),		"fopen"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_CONNECT, 0),	"connect"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_GETSERVBYNAME, 0),	"getservbyname"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_SOCKET, 0),	"socket"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_IOCTLSOCKET, 0),	"ioctl"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_BIND, 0),		"bind"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_LISTEN, 0),	"listen"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_ACCEPT, 0),	"accept"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_OPENDIR, 0),	"opendir"},
	{ERR_PACK(ERR_LIB_SYS, SYS_F_FREAD, 0),		"fread"},
	{0, NULL},
};

static const ERR_STRING_DATA ERR_str_reasons[] = {
	{ERR_R_SYS_LIB,				"system lib"},
	{ERR_R_BN_LIB,				"BN lib"},
	{ERR_R_RSA_LIB,				"RSA lib"},
	{ERR_R_DH_LIB,				"DH lib"},
	{ERR_R_EVP_LIB,				"EVP lib"},
	{ERR_R_BUF_LIB,				"BUF lib"},
	{ERR_R_OBJ_LIB,				"OBJ lib"},
	{ERR_R_PEM_LIB,				"PEM lib"},
	{ERR_R_DSA_LIB,				"DSA lib"},
	{ERR_R_X509_LIB,			"X509 lib"},
	{ERR_R_ASN1_LIB,			"ASN1 lib"},
	{ERR_R_CONF_LIB,			"CONF lib"},
	{ERR_R_CRYPTO_LIB,			"CRYPTO lib"},
	{ERR_R_EC_LIB,				"EC lib"},
	{ERR_R_SSL_LIB,				"SSL lib"},
	{ERR_R_BIO_LIB,				"BIO lib"},
	{ERR_R_PKCS7_LIB,			"PKCS7 lib"},
	{ERR_R_X509V3_LIB,			"X509V3 lib"},
	{ERR_R_PKCS12_LIB,			"PKCS12 lib"},
	{ERR_R_RAND_LIB,			"RAND lib"},
	{ERR_R_DSO_LIB,				"DSO lib"},
	{ERR_R_ENGINE_LIB,			"ENGINE lib"},
	{ERR_R_OCSP_LIB,			"OCSP lib"},
	{ERR_R_TS_LIB,				"TS lib"},

	{ERR_R_NESTED_ASN1_ERROR,		"nested asn1 error"},
	{ERR_R_BAD_ASN1_OBJECT_HEADER,		"bad asn1 object header"},
	{ERR_R_BAD_GET_ASN1_OBJECT_CALL,	"bad get asn1 object call"},
	{ERR_R_EXPECTING_AN_ASN1_SEQUENCE,	"expecting an asn1 sequence"},
	{ERR_R_ASN1_LENGTH_MISMATCH,		"asn1 length mismatch"},
	{ERR_R_MISSING_ASN1_EOS,		"missing asn1 eos"},

	{ERR_R_FATAL,				"fatal"},
	{ERR_R_MALLOC_FAILURE,			"malloc failure"},
	{ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED,	"called a function you should not call"},
	{ERR_R_PASSED_NULL_PARAMETER,		"passed a null parameter"},
	{ERR_R_INTERNAL_ERROR,			"internal error"},
	{ERR_R_DISABLED,			"called a function that was disabled at compile-time"},
	{ERR_R_INIT_FAIL,			"initialization failure"},

	{0, NULL},
};
#endif

static void ERR_STATE_free(ERR_STATE *s);

/*
 * The internal state used by "err_defaults" - as such, the setting, reading,
 * creating, and deleting of this data should only be permitted via the
 * "err_defaults" functions. This way, a linked module can completely defer all
 * ERR state operation (together with requisite locking) to the implementations
 * and state in the loading application.
 */
static LHASH_OF(ERR_STRING_DATA) *err_error_hash = NULL;
static LHASH_OF(ERR_STATE) *err_thread_hash = NULL;
static int err_thread_hash_references = 0;
static int err_library_number = ERR_LIB_USER;

static pthread_t err_init_thread;

/*
 * These are the callbacks provided to "lh_new()" when creating the LHASH tables
 * internal to the "err_defaults" implementation.
 */

static unsigned long
err_string_data_hash(const ERR_STRING_DATA *a)
{
	unsigned long ret, l;

	l = a->error;
	ret = l^ERR_GET_LIB(l)^ERR_GET_FUNC(l);
	return (ret^ret % 19*13);
}
static IMPLEMENT_LHASH_HASH_FN(err_string_data, ERR_STRING_DATA)

static int
err_string_data_cmp(const ERR_STRING_DATA *a, const ERR_STRING_DATA *b)
{
	return (int)(a->error - b->error);
}
static IMPLEMENT_LHASH_COMP_FN(err_string_data, ERR_STRING_DATA)

static LHASH_OF(ERR_STRING_DATA) *
err_get(int create)
{
	LHASH_OF(ERR_STRING_DATA) *ret = NULL;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	if (!err_error_hash && create)
		err_error_hash = lh_ERR_STRING_DATA_new();
	if (err_error_hash)
		ret = err_error_hash;
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);

	return ret;
}

static void
err_del(void)
{
	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	if (err_error_hash) {
		lh_ERR_STRING_DATA_free(err_error_hash);
		err_error_hash = NULL;
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);
}

static const ERR_STRING_DATA *
err_get_item(const ERR_STRING_DATA *d)
{
	ERR_STRING_DATA *p;
	LHASH_OF(ERR_STRING_DATA) *hash;

	hash = err_get(0);
	if (!hash)
		return NULL;

	CRYPTO_r_lock(CRYPTO_LOCK_ERR);
	p = lh_ERR_STRING_DATA_retrieve(hash, d);
	CRYPTO_r_unlock(CRYPTO_LOCK_ERR);

	return p;
}

static const ERR_STRING_DATA *
err_set_item(const ERR_STRING_DATA *d)
{
	const ERR_STRING_DATA *p;
	LHASH_OF(ERR_STRING_DATA) *hash;

	hash = err_get(1);
	if (!hash)
		return NULL;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	p = lh_ERR_STRING_DATA_insert(hash, (void *)d);
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);

	return p;
}

static const ERR_STRING_DATA *
err_del_item(const ERR_STRING_DATA *d)
{
	ERR_STRING_DATA *p;
	LHASH_OF(ERR_STRING_DATA) *hash;

	hash = err_get(0);
	if (!hash)
		return NULL;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	p = lh_ERR_STRING_DATA_delete(hash, d);
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);

	return p;
}

static unsigned long
err_state_hash(const ERR_STATE *a)
{
	return 13 * (unsigned long)a->tid;
}
static IMPLEMENT_LHASH_HASH_FN(err_state, ERR_STATE)

static int
err_state_cmp(const ERR_STATE *a, const ERR_STATE *b)
{
	return pthread_equal(a->tid, b->tid) == 0;
}
static IMPLEMENT_LHASH_COMP_FN(err_state, ERR_STATE)

static LHASH_OF(ERR_STATE) *
err_thread_get(int create)
{
	LHASH_OF(ERR_STATE) *ret = NULL;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	if (!err_thread_hash && create)
		err_thread_hash = lh_ERR_STATE_new();
	if (err_thread_hash) {
		err_thread_hash_references++;
		ret = err_thread_hash;
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);
	return ret;
}

static void
err_thread_release(LHASH_OF(ERR_STATE) **hash)
{
	int i;

	if (hash == NULL || *hash == NULL)
		return;

	i = CRYPTO_add(&err_thread_hash_references, -1, CRYPTO_LOCK_ERR);
	if (i > 0)
		return;

	*hash = NULL;
}

static ERR_STATE *
err_thread_get_item(const ERR_STATE *d)
{
	ERR_STATE *p;
	LHASH_OF(ERR_STATE) *hash;

	hash = err_thread_get(0);
	if (!hash)
		return NULL;

	CRYPTO_r_lock(CRYPTO_LOCK_ERR);
	p = lh_ERR_STATE_retrieve(hash, d);
	CRYPTO_r_unlock(CRYPTO_LOCK_ERR);

	err_thread_release(&hash);
	return p;
}

static ERR_STATE *
err_thread_set_item(ERR_STATE *d)
{
	ERR_STATE *p;
	LHASH_OF(ERR_STATE) *hash;

	hash = err_thread_get(1);
	if (!hash)
		return NULL;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	p = lh_ERR_STATE_insert(hash, d);
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);

	err_thread_release(&hash);
	return p;
}

static void
err_thread_del_item(const ERR_STATE *d)
{
	ERR_STATE *p;
	LHASH_OF(ERR_STATE) *hash;

	hash = err_thread_get(0);
	if (!hash)
		return;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	p = lh_ERR_STATE_delete(hash, d);
	/* make sure we don't leak memory */
	if (err_thread_hash_references == 1 &&
	    err_thread_hash && lh_ERR_STATE_num_items(err_thread_hash) == 0) {
		lh_ERR_STATE_free(err_thread_hash);
		err_thread_hash = NULL;
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);

	err_thread_release(&hash);
	if (p)
		ERR_STATE_free(p);
}

static int
err_get_next_lib(void)
{
	int ret;

	CRYPTO_w_lock(CRYPTO_LOCK_ERR);
	ret = err_library_number++;
	CRYPTO_w_unlock(CRYPTO_LOCK_ERR);

	return ret;
}


#ifndef OPENSSL_NO_ERR
#define NUM_SYS_STR_REASONS 127
#define LEN_SYS_STR_REASON 32

static ERR_STRING_DATA SYS_str_reasons[NUM_SYS_STR_REASONS + 1];

/*
 * SYS_str_reasons is filled with copies of strerror() results at
 * initialization. 'errno' values up to 127 should cover all usual errors,
 * others will be displayed numerically by ERR_error_string. It is crucial that
 * we have something for each reason code that occurs in ERR_str_reasons, or
 * bogus reason strings will be returned for SYSerror(), which always gets an
 * errno value and never one of those 'standard' reason codes.
 */

static void
err_build_SYS_str_reasons(void)
{
	/* malloc cannot be used here, use static storage instead */
	static char strerror_tab[NUM_SYS_STR_REASONS][LEN_SYS_STR_REASON];
	const char *errstr;
	int save_errno;
	int i;

	/* strerror(3) will set errno to EINVAL when i is an unknown errno. */
	save_errno = errno;
	for (i = 0; i < NUM_SYS_STR_REASONS; i++) {
		ERR_STRING_DATA *str = &SYS_str_reasons[i];

		str->error = i + 1;
		str->string = "unknown";

		if ((errstr = strerror((int)str->error)) != NULL) {
			strlcpy(strerror_tab[i], errstr, sizeof(strerror_tab[i]));
			str->string = strerror_tab[i];
		}
	}
	errno = save_errno;

	SYS_str_reasons[NUM_SYS_STR_REASONS].error = 0;
	SYS_str_reasons[NUM_SYS_STR_REASONS].string = NULL;
}
#endif

static void
err_clear_data(ERR_STATE *s, int i)
{
	if ((s->err_data_flags[i] & ERR_TXT_MALLOCED) != 0)
		free(s->err_data[i]);

	s->err_data[i] = NULL;
	s->err_data_flags[i] = 0;
}

static void
err_clear(ERR_STATE *s, int i)
{
	s->err_flags[i] = 0;
	s->err_buffer[i] = 0;
	s->err_file[i] = NULL;
	s->err_line[i] = -1;

	err_clear_data(s, i);
}

static void
ERR_STATE_free(ERR_STATE *s)
{
	int i;

	if (s == NULL)
		return;

	for (i = 0; i < ERR_NUM_ERRORS; i++)
		err_clear_data(s, i);

	free(s);
}

static ERR_STATE *
ERR_get_state(void)
{
	static ERR_STATE fallback;
	ERR_STATE *ret, tmp, *tmpp = NULL;
	int i;

	tmp.tid = pthread_self();
	ret = err_thread_get_item(&tmp);

	/* ret == the error state, if NULL, make a new one */
	if (ret == NULL) {
		ret = malloc(sizeof(ERR_STATE));
		if (ret == NULL)
			return (&fallback);
		ret->tid = pthread_self();
		ret->top = 0;
		ret->bottom = 0;
		for (i = 0; i < ERR_NUM_ERRORS; i++) {
			ret->err_data[i] = NULL;
			ret->err_data_flags[i] = 0;
		}
		tmpp = err_thread_set_item(ret);
		/* To check if insertion failed, do a get. */
		if (err_thread_get_item(ret) != ret) {
			ERR_STATE_free(ret); /* could not insert it */
			return (&fallback);
		}
		/*
		 * If a race occurred in this function and we came second,
		 * tmpp is the first one that we just replaced.
		 */
		if (tmpp)
			ERR_STATE_free(tmpp);
	}
	return ret;
}

static void
err_load_strings(int lib, ERR_STRING_DATA *str)
{
	while (str->error != 0) {
		if (lib)
			str->error |= ERR_PACK(lib, 0, 0);
		err_set_item(str);
		str++;
	}
}

static void
err_load_const_strings(const ERR_STRING_DATA *str)
{
	while (str->error != 0) {
		err_set_item(str);
		str++;
	}
}

static unsigned long
get_error_values(int inc, int top, const char **file, int *line,
    const char **data, int *flags)
{
	int i = 0;
	ERR_STATE *es;
	unsigned long ret;

	es = ERR_get_state();

	if (inc && top) {
		if (file)
			*file = "";
		if (line)
			*line = 0;
		if (data)
			*data = "";
		if (flags)
			*flags = 0;

		return ERR_R_INTERNAL_ERROR;
	}

	if (es->bottom == es->top)
		return 0;
	if (top)
		i = es->top;			 /* last error */
	else
		i = (es->bottom + 1) % ERR_NUM_ERRORS; /* first error */

	ret = es->err_buffer[i];
	if (inc) {
		es->bottom = i;
		es->err_buffer[i] = 0;
	}

	if ((file != NULL) && (line != NULL)) {
		if (es->err_file[i] == NULL) {
			*file = "NA";
			if (line != NULL)
				*line = 0;
		} else {
			*file = es->err_file[i];
			if (line != NULL)
				*line = es->err_line[i];
		}
	}

	if (data == NULL) {
		if (inc) {
			err_clear_data(es, i);
		}
	} else {
		if (es->err_data[i] == NULL) {
			*data = "";
			if (flags != NULL)
				*flags = 0;
		} else {
			*data = es->err_data[i];
			if (flags != NULL)
				*flags = es->err_data_flags[i];
		}
	}
	return ret;
}

void
ERR_load_ERR_strings_internal(void)
{
	err_init_thread = pthread_self();
#ifndef OPENSSL_NO_ERR
	err_load_const_strings(ERR_str_libraries);
	err_load_const_strings(ERR_str_reasons);
	err_load_const_strings(ERR_str_functs);
	err_build_SYS_str_reasons();
	err_load_strings(ERR_LIB_SYS, SYS_str_reasons);
#endif
}

void
ERR_load_ERR_strings(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	if (pthread_equal(pthread_self(), err_init_thread))
		return; /* don't recurse */

	/* Prayer and clean living lets you ignore errors, OpenSSL style */
	(void) OPENSSL_init_crypto(0, NULL);

	(void) pthread_once(&once, ERR_load_ERR_strings_internal);
}
LCRYPTO_ALIAS(ERR_load_ERR_strings);

void
ERR_load_strings(int lib, ERR_STRING_DATA *str)
{
	ERR_load_ERR_strings();
	err_load_strings(lib, str);
}
LCRYPTO_ALIAS(ERR_load_strings);

void
ERR_load_const_strings(const ERR_STRING_DATA *str)
{
	ERR_load_ERR_strings();
	err_load_const_strings(str);
}

void
ERR_unload_strings(int lib, ERR_STRING_DATA *str)
{
	/* Prayer and clean living lets you ignore errors, OpenSSL style */
	(void) OPENSSL_init_crypto(0, NULL);

	while (str->error) {
		if (lib)
			str->error |= ERR_PACK(lib, 0, 0);
		err_del_item(str);
		str++;
	}
}
LCRYPTO_ALIAS(ERR_unload_strings);

void
ERR_free_strings(void)
{
	/* Prayer and clean living lets you ignore errors, OpenSSL style */
	(void) OPENSSL_init_crypto(0, NULL);

	err_del();
}
LCRYPTO_ALIAS(ERR_free_strings);

int
ERR_get_next_error_library(void)
{
	return err_get_next_lib();
}
LCRYPTO_ALIAS(ERR_get_next_error_library);

void
ERR_remove_thread_state(const CRYPTO_THREADID *id)
{
	ERR_STATE tmp;

	OPENSSL_assert(id == NULL);
	tmp.tid = pthread_self();

	/*
	 * err_thread_del_item automatically destroys the LHASH if the number of
	 * items reaches zero.
	 */
	err_thread_del_item(&tmp);
}
LCRYPTO_ALIAS(ERR_remove_thread_state);

void
ERR_remove_state(unsigned long pid)
{
	ERR_remove_thread_state(NULL);
}
LCRYPTO_ALIAS(ERR_remove_state);

int
ERR_set_mark(void)
{
	ERR_STATE *es;

	es = ERR_get_state();

	if (es->bottom == es->top)
		return 0;
	es->err_flags[es->top] |= ERR_FLAG_MARK;
	return 1;
}
LCRYPTO_ALIAS(ERR_set_mark);

int
ERR_pop_to_mark(void)
{
	ERR_STATE *es;

	es = ERR_get_state();

	while (es->bottom != es->top &&
	    (es->err_flags[es->top] & ERR_FLAG_MARK) == 0) {
		err_clear(es, es->top);
		es->top -= 1;
		if (es->top == -1)
			es->top = ERR_NUM_ERRORS - 1;
	}

	if (es->bottom == es->top)
		return 0;
	es->err_flags[es->top]&=~ERR_FLAG_MARK;
	return 1;
}
LCRYPTO_ALIAS(ERR_pop_to_mark);

void
ERR_clear_error(void)
{
	int i;
	ERR_STATE *es;

	es = ERR_get_state();

	for (i = 0; i < ERR_NUM_ERRORS; i++)
		err_clear(es, i);

	es->top = es->bottom = 0;
}
LCRYPTO_ALIAS(ERR_clear_error);

void
err_clear_last_constant_time(int clear)
{
	ERR_STATE *es;
	int top;

	es = ERR_get_state();
	if (es == NULL)
		return;

	top = es->top;

	es->err_flags[top] &= ~(0 - clear);
	es->err_buffer[top] &= ~(0UL - clear);
	es->err_file[top] = (const char *)((uintptr_t)es->err_file[top] &
	    ~((uintptr_t)0 - clear));
	es->err_line[top] |= 0 - clear;

	es->top = (top + ERR_NUM_ERRORS - clear) % ERR_NUM_ERRORS;
}

void
ERR_put_error(int lib, int func, int reason, const char *file, int line)
{
	ERR_STATE *es;
	int save_errno = errno;

	es = ERR_get_state();

	es->top = (es->top + 1) % ERR_NUM_ERRORS;
	if (es->top == es->bottom)
		es->bottom = (es->bottom + 1) % ERR_NUM_ERRORS;
	es->err_flags[es->top] = 0;
	es->err_buffer[es->top] = ERR_PACK(lib, func, reason);
	es->err_file[es->top] = file;
	es->err_line[es->top] = line;
	err_clear_data(es, es->top);
	errno = save_errno;
}
LCRYPTO_ALIAS(ERR_put_error);

void
ERR_asprintf_error_data(char * format, ...)
{
	char *errbuf = NULL;
	va_list ap;
	int r;

	va_start(ap, format);
	r = vasprintf(&errbuf, format, ap);
	va_end(ap);
	if (r == -1)
		ERR_set_error_data("malloc failed", ERR_TXT_STRING);
	else
		ERR_set_error_data(errbuf, ERR_TXT_MALLOCED|ERR_TXT_STRING);
}
LCRYPTO_ALIAS(ERR_asprintf_error_data);

void
ERR_set_error_data(char *data, int flags)
{
	ERR_STATE *es;
	int i;

	es = ERR_get_state();

	i = es->top;
	if (i == 0)
		i = ERR_NUM_ERRORS - 1;

	err_clear_data(es, i);
	es->err_data[i] = data;
	es->err_data_flags[i] = flags;
}
LCRYPTO_ALIAS(ERR_set_error_data);

unsigned long
ERR_get_error(void)
{
	return (get_error_values(1, 0, NULL, NULL, NULL, NULL));
}
LCRYPTO_ALIAS(ERR_get_error);

unsigned long
ERR_get_error_line(const char **file, int *line)
{
	return (get_error_values(1, 0, file, line, NULL, NULL));
}
LCRYPTO_ALIAS(ERR_get_error_line);

unsigned long
ERR_get_error_line_data(const char **file, int *line,
    const char **data, int *flags)
{
	return (get_error_values(1, 0, file, line, data, flags));
}
LCRYPTO_ALIAS(ERR_get_error_line_data);

unsigned long
ERR_peek_error(void)
{
	return (get_error_values(0, 0, NULL, NULL, NULL, NULL));
}
LCRYPTO_ALIAS(ERR_peek_error);

unsigned long
ERR_peek_error_line(const char **file, int *line)
{
	return (get_error_values(0, 0, file, line, NULL, NULL));
}
LCRYPTO_ALIAS(ERR_peek_error_line);

unsigned long
ERR_peek_error_line_data(const char **file, int *line,
    const char **data, int *flags)
{
	return (get_error_values(0, 0, file, line, data, flags));
}
LCRYPTO_ALIAS(ERR_peek_error_line_data);

unsigned long
ERR_peek_last_error(void)
{
	return (get_error_values(0, 1, NULL, NULL, NULL, NULL));
}
LCRYPTO_ALIAS(ERR_peek_last_error);

unsigned long
ERR_peek_last_error_line(const char **file, int *line)
{
	return (get_error_values(0, 1, file, line, NULL, NULL));
}
LCRYPTO_ALIAS(ERR_peek_last_error_line);

unsigned long
ERR_peek_last_error_line_data(const char **file, int *line,
    const char **data, int *flags)
{
	return (get_error_values(0, 1, file, line, data, flags));
}
LCRYPTO_ALIAS(ERR_peek_last_error_line_data);

const char *
ERR_lib_error_string(unsigned long e)
{
	const ERR_STRING_DATA *p;
	ERR_STRING_DATA d;
	unsigned long l;

	if (!OPENSSL_init_crypto(0, NULL))
		return NULL;

	l = ERR_GET_LIB(e);
	d.error = ERR_PACK(l, 0, 0);
	p = err_get_item(&d);
	return ((p == NULL) ? NULL : p->string);
}
LCRYPTO_ALIAS(ERR_lib_error_string);

const char *
ERR_func_error_string(unsigned long e)
{
	const ERR_STRING_DATA *p;
	ERR_STRING_DATA d;
	unsigned long l, f;

	l = ERR_GET_LIB(e);
	f = ERR_GET_FUNC(e);
	d.error = ERR_PACK(l, f, 0);
	p = err_get_item(&d);
	return ((p == NULL) ? NULL : p->string);
}
LCRYPTO_ALIAS(ERR_func_error_string);

const char *
ERR_reason_error_string(unsigned long e)
{
	const ERR_STRING_DATA *p = NULL;
	ERR_STRING_DATA d;
	unsigned long l, r;

	l = ERR_GET_LIB(e);
	r = ERR_GET_REASON(e);
	d.error = ERR_PACK(l, 0, r);
	p = err_get_item(&d);
	if (!p) {
		d.error = ERR_PACK(0, 0, r);
		p = err_get_item(&d);
	}
	return ((p == NULL) ? NULL : p->string);
}
LCRYPTO_ALIAS(ERR_reason_error_string);

void
ERR_error_string_n(unsigned long e, char *buf, size_t len)
{
	char lsbuf[30], fsbuf[30], rsbuf[30];
	const char *ls, *fs, *rs;
	int l, f, r, ret;

	l = ERR_GET_LIB(e);
	f = ERR_GET_FUNC(e);
	r = ERR_GET_REASON(e);

	ls = ERR_lib_error_string(e);
	fs = ERR_func_error_string(e);
	rs = ERR_reason_error_string(e);

	if (ls == NULL) {
		(void) snprintf(lsbuf, sizeof(lsbuf), "lib(%d)", l);
		ls = lsbuf;
	}
	if (fs == NULL) {
		(void) snprintf(fsbuf, sizeof(fsbuf), "func(%d)", f);
		fs = fsbuf;
	}
	if (rs == NULL) {
		(void) snprintf(rsbuf, sizeof(rsbuf), "reason(%d)", r);
		rs = rsbuf;
	}

	ret = snprintf(buf, len, "error:%08lX:%s:%s:%s", e, ls, fs, rs);
	if (ret == -1)
		return;	/* can't happen, and can't do better if it does */
	if (ret >= len) {
		/*
		 * output may be truncated; make sure we always have 5
		 * colon-separated fields, i.e. 4 colons ...
		 */
#define NUM_COLONS 4
		if (len > NUM_COLONS) /* ... if possible */
		{
			int i;
			char *s = buf;

			for (i = 0; i < NUM_COLONS; i++) {
				char *colon = strchr(s, ':');
				if (colon == NULL ||
				    colon > &buf[len - 1] - NUM_COLONS + i) {
					/* set colon no. i at last possible position
					 * (buf[len-1] is the terminating 0)*/
					colon = &buf[len - 1] - NUM_COLONS + i;
					*colon = ':';
				}
				s = colon + 1;
			}
		}
	}
}
LCRYPTO_ALIAS(ERR_error_string_n);

/*
 * ERR_error_string_n should be used instead for ret != NULL
 * as ERR_error_string cannot know how large the buffer is.
 *
 * BAD for multi-threading: uses a local buffer if ret == NULL.
 */
char *
ERR_error_string(unsigned long e, char *ret)
{
	static char buf[256];

	if (ret == NULL)
		ret = buf;
	ERR_error_string_n(e, ret, 256);

	return ret;
}
LCRYPTO_ALIAS(ERR_error_string);
