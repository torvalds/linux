/* $OpenBSD: ts_conf.c,v 1.16 2025/05/10 05:54:39 tb Exp $ */
/* Written by Zoltan Glozik (zglozik@stones.com) for the OpenSSL
 * project 2002.
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/ts.h>

/* Macro definitions for the configuration file. */

#define	BASE_SECTION			"tsa"
#define	ENV_DEFAULT_TSA			"default_tsa"
#define	ENV_SERIAL			"serial"
#define ENV_CRYPTO_DEVICE		"crypto_device"
#define	ENV_SIGNER_CERT			"signer_cert"
#define	ENV_CERTS			"certs"
#define	ENV_SIGNER_KEY			"signer_key"
#define	ENV_DEFAULT_POLICY		"default_policy"
#define	ENV_OTHER_POLICIES		"other_policies"
#define	ENV_DIGESTS			"digests"
#define	ENV_ACCURACY			"accuracy"
#define	ENV_ORDERING			"ordering"
#define	ENV_TSA_NAME			"tsa_name"
#define	ENV_ESS_CERT_ID_CHAIN		"ess_cert_id_chain"
#define	ENV_VALUE_SECS			"secs"
#define	ENV_VALUE_MILLISECS		"millisecs"
#define	ENV_VALUE_MICROSECS		"microsecs"
#define	ENV_CLOCK_PRECISION_DIGITS	"clock_precision_digits"
#define	ENV_VALUE_YES			"yes"
#define	ENV_VALUE_NO			"no"

/* Function definitions for certificate and key loading. */

X509 *
TS_CONF_load_cert(const char *file)
{
	BIO *cert = NULL;
	X509 *x = NULL;

	if ((cert = BIO_new_file(file, "r")) == NULL)
		goto end;
	x = PEM_read_bio_X509_AUX(cert, NULL, NULL, NULL);

end:
	if (x == NULL)
		fprintf(stderr, "unable to load certificate: %s\n", file);
	BIO_free(cert);
	return x;
}
LCRYPTO_ALIAS(TS_CONF_load_cert);

STACK_OF(X509) *
TS_CONF_load_certs(const char *file)
{
	BIO *certs = NULL;
	STACK_OF(X509) *othercerts = NULL;
	STACK_OF(X509_INFO) *allcerts = NULL;
	int i;

	if (!(certs = BIO_new_file(file, "r")))
		goto end;

	if (!(othercerts = sk_X509_new_null()))
		goto end;
	allcerts = PEM_X509_INFO_read_bio(certs, NULL, NULL, NULL);
	for (i = 0; i < sk_X509_INFO_num(allcerts); i++) {
		X509_INFO *xi = sk_X509_INFO_value(allcerts, i);
		if (xi->x509) {
			if (sk_X509_push(othercerts, xi->x509) == 0) {
				sk_X509_pop_free(othercerts, X509_free);
				othercerts = NULL;
				goto end;
			}
			xi->x509 = NULL;
		}
	}

end:
	if (othercerts == NULL)
		fprintf(stderr, "unable to load certificates: %s\n", file);
	sk_X509_INFO_pop_free(allcerts, X509_INFO_free);
	BIO_free(certs);
	return othercerts;
}
LCRYPTO_ALIAS(TS_CONF_load_certs);

EVP_PKEY *
TS_CONF_load_key(const char *file, const char *pass)
{
	BIO *key = NULL;
	EVP_PKEY *pkey = NULL;

	if (!(key = BIO_new_file(file, "r")))
		goto end;
	pkey = PEM_read_bio_PrivateKey(key, NULL, NULL, (char *) pass);

end:
	if (pkey == NULL)
		fprintf(stderr, "unable to load private key: %s\n", file);
	BIO_free(key);
	return pkey;
}
LCRYPTO_ALIAS(TS_CONF_load_key);

/* Function definitions for handling configuration options. */

static void
TS_CONF_lookup_fail(const char *name, const char *tag)
{
	fprintf(stderr, "variable lookup failed for %s::%s\n", name, tag);
}

static void
TS_CONF_invalid(const char *name, const char *tag)
{
	fprintf(stderr, "invalid variable value for %s::%s\n", name, tag);
}

const char *
TS_CONF_get_tsa_section(CONF *conf, const char *section)
{
	if (!section) {
		section = NCONF_get_string(conf, BASE_SECTION, ENV_DEFAULT_TSA);
		if (!section)
			TS_CONF_lookup_fail(BASE_SECTION, ENV_DEFAULT_TSA);
	}
	return section;
}
LCRYPTO_ALIAS(TS_CONF_get_tsa_section);

int
TS_CONF_set_serial(CONF *conf, const char *section, TS_serial_cb cb,
    TS_RESP_CTX *ctx)
{
	int ret = 0;
	char *serial = NCONF_get_string(conf, section, ENV_SERIAL);

	if (!serial) {
		TS_CONF_lookup_fail(section, ENV_SERIAL);
		goto err;
	}
	TS_RESP_CTX_set_serial_cb(ctx, cb, serial);

	ret = 1;

err:
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_serial);

int
TS_CONF_set_signer_cert(CONF *conf, const char *section, const char *cert,
    TS_RESP_CTX *ctx)
{
	int ret = 0;
	X509 *cert_obj = NULL;

	if (!cert)
		cert = NCONF_get_string(conf, section, ENV_SIGNER_CERT);
	if (!cert) {
		TS_CONF_lookup_fail(section, ENV_SIGNER_CERT);
		goto err;
	}
	if (!(cert_obj = TS_CONF_load_cert(cert)))
		goto err;
	if (!TS_RESP_CTX_set_signer_cert(ctx, cert_obj))
		goto err;

	ret = 1;

err:
	X509_free(cert_obj);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_signer_cert);

int
TS_CONF_set_certs(CONF *conf, const char *section, const char *certs,
    TS_RESP_CTX *ctx)
{
	int ret = 0;
	STACK_OF(X509) *certs_obj = NULL;

	if (!certs)
		certs = NCONF_get_string(conf, section, ENV_CERTS);
	/* Certificate chain is optional. */
	if (!certs)
		goto end;
	if (!(certs_obj = TS_CONF_load_certs(certs)))
		goto err;
	if (!TS_RESP_CTX_set_certs(ctx, certs_obj))
		goto err;

end:
	ret = 1;
err:
	sk_X509_pop_free(certs_obj, X509_free);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_certs);

int
TS_CONF_set_signer_key(CONF *conf, const char *section, const char *key,
    const char *pass, TS_RESP_CTX *ctx)
{
	int ret = 0;
	EVP_PKEY *key_obj = NULL;

	if (!key)
		key = NCONF_get_string(conf, section, ENV_SIGNER_KEY);
	if (!key) {
		TS_CONF_lookup_fail(section, ENV_SIGNER_KEY);
		goto err;
	}
	if (!(key_obj = TS_CONF_load_key(key, pass)))
		goto err;
	if (!TS_RESP_CTX_set_signer_key(ctx, key_obj))
		goto err;

	ret = 1;

err:
	EVP_PKEY_free(key_obj);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_signer_key);

int
TS_CONF_set_def_policy(CONF *conf, const char *section, const char *policy,
    TS_RESP_CTX *ctx)
{
	int ret = 0;
	ASN1_OBJECT *policy_obj = NULL;

    	if (!policy)
		policy = NCONF_get_string(conf, section, ENV_DEFAULT_POLICY);
	if (!policy) {
		TS_CONF_lookup_fail(section, ENV_DEFAULT_POLICY);
		goto err;
	}
	if (!(policy_obj = OBJ_txt2obj(policy, 0))) {
		TS_CONF_invalid(section, ENV_DEFAULT_POLICY);
		goto err;
	}
	if (!TS_RESP_CTX_set_def_policy(ctx, policy_obj))
		goto err;

	ret = 1;

err:
	ASN1_OBJECT_free(policy_obj);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_def_policy);

int
TS_CONF_set_policies(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
	int ret = 0;
	int i;
	STACK_OF(CONF_VALUE) *list = NULL;
	char *policies = NCONF_get_string(conf, section, ENV_OTHER_POLICIES);

	/* If no other policy is specified, that's fine. */
	if (policies && !(list = X509V3_parse_list(policies))) {
		TS_CONF_invalid(section, ENV_OTHER_POLICIES);
		goto err;
	}
	for (i = 0; i < sk_CONF_VALUE_num(list); ++i) {
		CONF_VALUE *val = sk_CONF_VALUE_value(list, i);
		const char *extval = val->value ? val->value : val->name;
		ASN1_OBJECT *objtmp;
		if (!(objtmp = OBJ_txt2obj(extval, 0))) {
			TS_CONF_invalid(section, ENV_OTHER_POLICIES);
			goto err;
		}
		if (!TS_RESP_CTX_add_policy(ctx, objtmp))
			goto err;
		ASN1_OBJECT_free(objtmp);
	}

	ret = 1;

err:
	sk_CONF_VALUE_pop_free(list, X509V3_conf_free);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_policies);

int
TS_CONF_set_digests(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
	int ret = 0;
	int i;
	STACK_OF(CONF_VALUE) *list = NULL;
	char *digests = NCONF_get_string(conf, section, ENV_DIGESTS);

	if (!digests) {
		TS_CONF_lookup_fail(section, ENV_DIGESTS);
		goto err;
	}
	if (!(list = X509V3_parse_list(digests))) {
		TS_CONF_invalid(section, ENV_DIGESTS);
		goto err;
	}
	if (sk_CONF_VALUE_num(list) == 0) {
		TS_CONF_invalid(section, ENV_DIGESTS);
		goto err;
	}
	for (i = 0; i < sk_CONF_VALUE_num(list); ++i) {
		CONF_VALUE *val = sk_CONF_VALUE_value(list, i);
		const char *extval = val->value ? val->value : val->name;
		const EVP_MD *md;
		if (!(md = EVP_get_digestbyname(extval))) {
			TS_CONF_invalid(section, ENV_DIGESTS);
			goto err;
		}
		if (!TS_RESP_CTX_add_md(ctx, md))
			goto err;
	}

	ret = 1;

err:
	sk_CONF_VALUE_pop_free(list, X509V3_conf_free);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_digests);

int
TS_CONF_set_accuracy(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
	int ret = 0;
	int i;
	int secs = 0, millis = 0, micros = 0;
	STACK_OF(CONF_VALUE) *list = NULL;
	char *accuracy = NCONF_get_string(conf, section, ENV_ACCURACY);
	const char *errstr;

	if (accuracy && !(list = X509V3_parse_list(accuracy))) {
		TS_CONF_invalid(section, ENV_ACCURACY);
		goto err;
	}
	for (i = 0; i < sk_CONF_VALUE_num(list); ++i) {
		CONF_VALUE *val = sk_CONF_VALUE_value(list, i);
		if (strcmp(val->name, ENV_VALUE_SECS) == 0) {
			if (val->value) {
				secs = strtonum(val->value, 0, INT_MAX,
				    &errstr);
				if (errstr != NULL) {
					TS_CONF_invalid(section,
					    ENV_VALUE_SECS);
					goto err;
				}
			}
		} else if (strcmp(val->name, ENV_VALUE_MILLISECS) == 0) {
			if (val->value) {
				millis = strtonum(val->value, 1, 999, &errstr);
				if (errstr != NULL) {
					TS_CONF_invalid(section,
					    ENV_VALUE_MILLISECS);
					goto err;
				}
			}
		} else if (strcmp(val->name, ENV_VALUE_MICROSECS) == 0) {
			if (val->value) {
				micros = strtonum(val->value, 1, 999, &errstr);
				if (errstr != NULL) {
					TS_CONF_invalid(section,
					    ENV_VALUE_MICROSECS);
					goto err;
				}
			}
		} else {
			TS_CONF_invalid(section, ENV_ACCURACY);
			goto err;
		}
	}
	if (!TS_RESP_CTX_set_accuracy(ctx, secs, millis, micros))
		goto err;

	ret = 1;

err:
	sk_CONF_VALUE_pop_free(list, X509V3_conf_free);
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_accuracy);

int
TS_CONF_set_clock_precision_digits(CONF *conf, const char *section,
    TS_RESP_CTX *ctx)
{
	int ret = 0;
	long digits = 0;

	/* If not specified, set the default value to 0, i.e. sec  precision */
	if (!NCONF_get_number_e(conf, section, ENV_CLOCK_PRECISION_DIGITS,
	    &digits))
		digits = 0;
	/* We only support second precision, so reject everything else */
	if (digits != 0) {
		TS_CONF_invalid(section, ENV_CLOCK_PRECISION_DIGITS);
		goto err;
	}

	if (!TS_RESP_CTX_set_clock_precision_digits(ctx, digits))
		goto err;

	return 1;

err:
	return ret;
}
LCRYPTO_ALIAS(TS_CONF_set_clock_precision_digits);

static int
TS_CONF_add_flag(CONF *conf, const char *section, const char *field, int flag,
    TS_RESP_CTX *ctx)
{
	/* Default is false. */
	const char *value = NCONF_get_string(conf, section, field);

	if (value) {
		if (strcmp(value, ENV_VALUE_YES) == 0)
			TS_RESP_CTX_add_flags(ctx, flag);
		else if (strcmp(value, ENV_VALUE_NO) != 0) {
			TS_CONF_invalid(section, field);
			return 0;
		}
	}

	return 1;
}

int
TS_CONF_set_ordering(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
	return TS_CONF_add_flag(conf, section, ENV_ORDERING, TS_ORDERING, ctx);
}
LCRYPTO_ALIAS(TS_CONF_set_ordering);

int
TS_CONF_set_tsa_name(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
	return TS_CONF_add_flag(conf, section, ENV_TSA_NAME, TS_TSA_NAME, ctx);
}
LCRYPTO_ALIAS(TS_CONF_set_tsa_name);

int
TS_CONF_set_ess_cert_id_chain(CONF *conf, const char *section, TS_RESP_CTX *ctx)
{
	return TS_CONF_add_flag(conf, section, ENV_ESS_CERT_ID_CHAIN,
	    TS_ESS_CERT_ID_CHAIN, ctx);
}
LCRYPTO_ALIAS(TS_CONF_set_ess_cert_id_chain);
