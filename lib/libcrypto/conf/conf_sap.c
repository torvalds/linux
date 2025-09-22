/* $OpenBSD: conf_sap.c,v 1.18 2024/10/18 11:12:10 tb Exp $ */
/* Written by Stephen Henson (steve@openssl.org) for the OpenSSL
 * project 2001.
 */
/* ====================================================================
 * Copyright (c) 2001 The OpenSSL Project.  All rights reserved.
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

#include <pthread.h>
#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include "conf_local.h"

/* This is the automatic configuration loader: it is called automatically by
 * OpenSSL when any of a number of standard initialisation functions are called,
 * unless this is overridden by calling OPENSSL_no_config()
 */

static pthread_once_t openssl_configured = PTHREAD_ONCE_INIT;

static const char *openssl_config_name;

void ASN1_add_oid_module(void);

static void
OPENSSL_config_internal(void)
{
	ASN1_add_oid_module();

	ERR_clear_error();
	if (CONF_modules_load_file(NULL, openssl_config_name,
	    CONF_MFLAGS_DEFAULT_SECTION|CONF_MFLAGS_IGNORE_MISSING_FILE) <= 0) {
		BIO *bio_err;
		ERR_load_crypto_strings();
		if ((bio_err = BIO_new_fp(stderr, BIO_NOCLOSE)) != NULL) {
			BIO_printf(bio_err, "Auto configuration failed\n");
			ERR_print_errors(bio_err);
			BIO_free(bio_err);
		}
		exit(1);
	}

	return;
}

int
OpenSSL_config(const char *config_name)
{
	/* Don't override if NULL */
	/*
	 * Note - multiple threads calling this with *different* config names
	 * is probably not advisable.  One thread will win, but you don't know
	 * if it will be the same thread as wins the pthread_once.
	 */
	if (config_name != NULL)
		openssl_config_name = config_name;

	if (OPENSSL_init_crypto(0, NULL) == 0)
		return 0;

	if (pthread_once(&openssl_configured, OPENSSL_config_internal) != 0)
		return 0;

	return 1;
}

void
OPENSSL_config(const char *config_name)
{
	(void) OpenSSL_config(config_name);
}
LCRYPTO_ALIAS(OPENSSL_config);

static void
OPENSSL_no_config_internal(void)
{
}

int
OpenSSL_no_config(void)
{
	if (pthread_once(&openssl_configured, OPENSSL_no_config_internal) != 0)
		return 0;

	return 1;
}

void
OPENSSL_no_config(void)
{
	(void) OpenSSL_no_config();
}
LCRYPTO_ALIAS(OPENSSL_no_config);
