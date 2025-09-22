/*	$OpenBSD: ssltest.c,v 1.45 2024/03/01 03:45:16 tb Exp $ */
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
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

/* XXX - USE_BIOPAIR code needs updating for BIO_n{read,write}{,0} removal. */
/* #define USE_BIOPAIR */

#define _BSD_SOURCE 1		/* Or gethostname won't be declared properly
				   on Linux and GNU platforms. */
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/opensslconf.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/dsa.h>
#include <openssl/dh.h>
#include <openssl/bn.h>

#include "ssl_local.h"

#define TEST_SERVER_CERT "../apps/server.pem"
#define TEST_CLIENT_CERT "../apps/client.pem"

static int verify_callback(int ok, X509_STORE_CTX *ctx);
static int app_verify_callback(X509_STORE_CTX *ctx, void *arg);

static DH *get_dh1024(void);
static DH *get_dh1024dsa(void);

static BIO *bio_err = NULL;
static BIO *bio_stdout = NULL;

static const char *alpn_client;
static const char *alpn_server;
static const char *alpn_expected;
static unsigned char *alpn_selected;

/*
 * next_protos_parse parses a comma separated list of strings into a string
 * in a format suitable for passing to SSL_CTX_set_next_protos_advertised.
 *   outlen: (output) set to the length of the resulting buffer on success.
 *   err: (maybe NULL) on failure, an error message line is written to this BIO.
 *   in: a NUL terminated string like "abc,def,ghi"
 *
 *   returns: a malloced buffer or NULL on failure.
 */
static unsigned char *
next_protos_parse(unsigned short *outlen, const char *in)
{
	size_t i, len, start = 0;
	unsigned char *out;

	len = strlen(in);
	if (len >= 65535)
		return (NULL);

	if ((out = malloc(strlen(in) + 1)) == NULL)
		return (NULL);

	for (i = 0; i <= len; ++i) {
		if (i == len || in[i] == ',') {
			if (i - start > 255) {
				free(out);
				return (NULL);
			}
			out[start] = i - start;
			start = i + 1;
		} else
			out[i+1] = in[i];
	}
	*outlen = len + 1;
	return (out);
}

static int
cb_server_alpn(SSL *s, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg)
{
	unsigned char *protos;
	unsigned short protos_len;

	if ((protos = next_protos_parse(&protos_len, alpn_server)) == NULL) {
		fprintf(stderr,
		    "failed to parser ALPN server protocol string: %s\n",
		    alpn_server);
		abort();
	}

	if (SSL_select_next_proto((unsigned char **)out, outlen, protos,
	    protos_len, in, inlen) != OPENSSL_NPN_NEGOTIATED) {
		free(protos);
		return (SSL_TLSEXT_ERR_NOACK);
	}

	/*
	 * Make a copy of the selected protocol which will be freed in
	 * verify_alpn.
	 */
	free(alpn_selected);
	if ((alpn_selected = malloc(*outlen)) == NULL) {
		fprintf(stderr, "malloc failed\n");
		abort();
	}
	memcpy(alpn_selected, *out, *outlen);
	*out = alpn_selected;
	free(protos);

	return (SSL_TLSEXT_ERR_OK);
}

static int
verify_alpn(SSL *client, SSL *server)
{
	const unsigned char *client_proto, *server_proto;
	unsigned int client_proto_len = 0, server_proto_len = 0;

	SSL_get0_alpn_selected(client, &client_proto, &client_proto_len);
	SSL_get0_alpn_selected(server, &server_proto, &server_proto_len);

	free(alpn_selected);
	alpn_selected = NULL;

	if (client_proto_len != server_proto_len || (client_proto_len > 0 &&
	    memcmp(client_proto, server_proto, client_proto_len) != 0)) {
		BIO_printf(bio_stdout, "ALPN selected protocols differ!\n");
		goto err;
	}

	if (client_proto_len > 0 && alpn_expected == NULL) {
		BIO_printf(bio_stdout, "ALPN unexpectedly negotiated\n");
		goto err;
	}

	if (alpn_expected != NULL &&
	    (client_proto_len != strlen(alpn_expected) ||
	     memcmp(client_proto, alpn_expected, client_proto_len) != 0)) {
		BIO_printf(bio_stdout, "ALPN selected protocols not equal to "
		    "expected protocol: %s\n", alpn_expected);
		goto err;
	}

	return (0);

err:
	BIO_printf(bio_stdout, "ALPN results: client: '");
	BIO_write(bio_stdout, client_proto, client_proto_len);
	BIO_printf(bio_stdout, "', server: '");
	BIO_write(bio_stdout, server_proto, server_proto_len);
	BIO_printf(bio_stdout, "'\n");
	BIO_printf(bio_stdout, "ALPN configured: client: '%s', server: '%s'\n",
	    alpn_client, alpn_server);

	return (-1);
}

static char *cipher = NULL;
static int verbose = 0;
static int debug = 0;

int doit_biopair(SSL *s_ssl, SSL *c_ssl, long bytes, clock_t *s_time,
    clock_t *c_time);
int doit(SSL *s_ssl, SSL *c_ssl, long bytes);

static void
sv_usage(void)
{
	fprintf(stderr, "usage: ssltest [args ...]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, " -server_auth  - check server certificate\n");
	fprintf(stderr, " -client_auth  - do client authentication\n");
	fprintf(stderr, " -proxy        - allow proxy certificates\n");
	fprintf(stderr, " -proxy_auth <val> - set proxy policy rights\n");
	fprintf(stderr, " -proxy_cond <val> - experssion to test proxy policy rights\n");
	fprintf(stderr, " -v            - more output\n");
	fprintf(stderr, " -d            - debug output\n");
	fprintf(stderr, " -reuse        - use session-id reuse\n");
	fprintf(stderr, " -num <val>    - number of connections to perform\n");
	fprintf(stderr, " -bytes <val>  - number of bytes to swap between client/server\n");
	fprintf(stderr, " -dhe1024dsa   - use 1024 bit key (with 160-bit subprime) for DHE\n");
	fprintf(stderr, " -no_dhe       - disable DHE\n");
	fprintf(stderr, " -no_ecdhe     - disable ECDHE\n");
	fprintf(stderr, " -dtls1_2      - use DTLSv1.2\n");
	fprintf(stderr, " -tls1         - use TLSv1\n");
	fprintf(stderr, " -tls1_2       - use TLSv1.2\n");
	fprintf(stderr, " -CApath arg   - PEM format directory of CA's\n");
	fprintf(stderr, " -CAfile arg   - PEM format file of CA's\n");
	fprintf(stderr, " -cert arg     - Server certificate file\n");
	fprintf(stderr, " -key arg      - Server key file (default: same as -cert)\n");
	fprintf(stderr, " -c_cert arg   - Client certificate file\n");
	fprintf(stderr, " -c_key arg    - Client key file (default: same as -c_cert)\n");
	fprintf(stderr, " -cipher arg   - The cipher list\n");
	fprintf(stderr, " -bio_pair     - Use BIO pairs\n");
	fprintf(stderr, " -f            - Test even cases that can't work\n");
	fprintf(stderr, " -time         - measure processor time used by client and server\n");
	fprintf(stderr, " -named_curve arg  - Elliptic curve name to use for ephemeral ECDH keys.\n" \
	               "                 Use \"openssl ecparam -list_curves\" for all names\n"  \
	               "                 (default is sect163r2).\n");
	fprintf(stderr, " -alpn_client <string> - have client side offer ALPN\n");
	fprintf(stderr, " -alpn_server <string> - have server side offer ALPN\n");
	fprintf(stderr, " -alpn_expected <string> - the ALPN protocol that should be negotiated\n");
}

static void
print_details(SSL *c_ssl, const char *prefix)
{
	const SSL_CIPHER *ciph;
	X509 *cert = NULL;
	EVP_PKEY *pkey;

	ciph = SSL_get_current_cipher(c_ssl);
	BIO_printf(bio_stdout, "%s%s, cipher %s %s",
	    prefix, SSL_get_version(c_ssl), SSL_CIPHER_get_version(ciph),
	    SSL_CIPHER_get_name(ciph));

	if ((cert = SSL_get_peer_certificate(c_ssl)) == NULL)
		goto out;
	if ((pkey = X509_get0_pubkey(cert)) == NULL)
		goto out;
	if (EVP_PKEY_id(pkey) == EVP_PKEY_RSA) {
		RSA *rsa;

		if ((rsa = EVP_PKEY_get0_RSA(pkey)) == NULL)
			goto out;

		BIO_printf(bio_stdout, ", %d bit RSA", RSA_bits(rsa));
	} else if (EVP_PKEY_id(pkey) == EVP_PKEY_DSA) {
		DSA *dsa;
		const BIGNUM *p;

		if ((dsa = EVP_PKEY_get0_DSA(pkey)) == NULL)
			goto out;

		DSA_get0_pqg(dsa, &p, NULL, NULL);

		BIO_printf(bio_stdout, ", %d bit DSA", BN_num_bits(p));
	}

 out:
	/*
	 * The SSL API does not allow us to look at temporary RSA/DH keys,
	 * otherwise we should print their lengths too
	 */
	BIO_printf(bio_stdout, "\n");

	X509_free(cert);
}

int
main(int argc, char *argv[])
{
	char *CApath = NULL, *CAfile = NULL;
	int badop = 0;
	int bio_pair = 0;
	int force = 0;
	int tls1 = 0, tls1_2 = 0, dtls1_2 = 0, ret = 1;
	int client_auth = 0;
	int server_auth = 0, i;
	char *app_verify_arg = "Test Callback Argument";
	char *server_cert = TEST_SERVER_CERT;
	char *server_key = NULL;
	char *client_cert = TEST_CLIENT_CERT;
	char *client_key = NULL;
	char *named_curve = NULL;
	SSL_CTX *s_ctx = NULL;
	SSL_CTX *c_ctx = NULL;
	const SSL_METHOD *meth = NULL;
	SSL *c_ssl, *s_ssl;
	int number = 1, reuse = 0;
	int seclevel = 0;
	long bytes = 256L;
	DH *dh;
	int dhe1024dsa = 0;
	EC_KEY *ecdh = NULL;
	int no_dhe = 0;
	int no_ecdhe = 0;
	int print_time = 0;
	clock_t s_time = 0, c_time = 0;

	verbose = 0;
	debug = 0;
	cipher = 0;

	bio_err = BIO_new_fp(stderr, BIO_NOCLOSE|BIO_FP_TEXT);

	bio_stdout = BIO_new_fp(stdout, BIO_NOCLOSE|BIO_FP_TEXT);

	argc--;
	argv++;

	while (argc >= 1) {
		if (!strcmp(*argv, "-F")) {
			fprintf(stderr, "not compiled with FIPS support, so exiting without running.\n");
			exit(0);
		} else if (strcmp(*argv, "-server_auth") == 0)
			server_auth = 1;
		else if (strcmp(*argv, "-client_auth") == 0)
			client_auth = 1;
		else if (strcmp(*argv, "-v") == 0)
			verbose = 1;
		else if (strcmp(*argv, "-d") == 0)
			debug = 1;
		else if (strcmp(*argv, "-reuse") == 0)
			reuse = 1;
		else if (strcmp(*argv, "-dhe1024dsa") == 0) {
			dhe1024dsa = 1;
		} else if (strcmp(*argv, "-no_dhe") == 0)
			no_dhe = 1;
		else if (strcmp(*argv, "-no_ecdhe") == 0)
			no_ecdhe = 1;
		else if (strcmp(*argv, "-dtls1_2") == 0)
			dtls1_2 = 1;
		else if (strcmp(*argv, "-tls1") == 0)
			tls1 = 1;
		else if (strcmp(*argv, "-tls1_2") == 0)
			tls1_2 = 1;
		else if (strncmp(*argv, "-num", 4) == 0) {
			if (--argc < 1)
				goto bad;
			number = atoi(*(++argv));
			if (number == 0)
				number = 1;
		} else if (strncmp(*argv, "-seclevel", 9) == 0) {
			if (--argc < 1)
				goto bad;
			seclevel = atoi(*(++argv));
		} else if (strcmp(*argv, "-bytes") == 0) {
			if (--argc < 1)
				goto bad;
			bytes = atol(*(++argv));
			if (bytes == 0L)
				bytes = 1L;
			i = strlen(argv[0]);
			if (argv[0][i - 1] == 'k')
				bytes*=1024L;
			if (argv[0][i - 1] == 'm')
				bytes*=1024L*1024L;
		} else if (strcmp(*argv, "-cert") == 0) {
			if (--argc < 1)
				goto bad;
			server_cert= *(++argv);
		} else if (strcmp(*argv, "-s_cert") == 0) {
			if (--argc < 1)
				goto bad;
			server_cert= *(++argv);
		} else if (strcmp(*argv, "-key") == 0) {
			if (--argc < 1)
				goto bad;
			server_key= *(++argv);
		} else if (strcmp(*argv, "-s_key") == 0) {
			if (--argc < 1)
				goto bad;
			server_key= *(++argv);
		} else if (strcmp(*argv, "-c_cert") == 0) {
			if (--argc < 1)
				goto bad;
			client_cert= *(++argv);
		} else if (strcmp(*argv, "-c_key") == 0) {
			if (--argc < 1)
				goto bad;
			client_key= *(++argv);
		} else if (strcmp(*argv, "-cipher") == 0) {
			if (--argc < 1)
				goto bad;
			cipher= *(++argv);
		} else if (strcmp(*argv, "-CApath") == 0) {
			if (--argc < 1)
				goto bad;
			CApath= *(++argv);
		} else if (strcmp(*argv, "-CAfile") == 0) {
			if (--argc < 1)
				goto bad;
			CAfile= *(++argv);
		} else if (strcmp(*argv, "-bio_pair") == 0) {
			bio_pair = 1;
		} else if (strcmp(*argv, "-f") == 0) {
			force = 1;
		} else if (strcmp(*argv, "-time") == 0) {
			print_time = 1;
		} else if (strcmp(*argv, "-named_curve") == 0) {
			if (--argc < 1)
				goto bad;
			named_curve = *(++argv);
		} else if (strcmp(*argv, "-app_verify") == 0) {
			;
		} else if (strcmp(*argv, "-alpn_client") == 0) {
			if (--argc < 1)
				goto bad;
			alpn_client = *(++argv);
		} else if (strcmp(*argv, "-alpn_server") == 0) {
			if (--argc < 1)
				goto bad;
			alpn_server = *(++argv);
		} else if (strcmp(*argv, "-alpn_expected") == 0) {
			if (--argc < 1)
				goto bad;
			alpn_expected = *(++argv);
		} else {
			fprintf(stderr, "unknown option %s\n", *argv);
			badop = 1;
			break;
		}
		argc--;
		argv++;
	}
	if (badop) {
bad:
		sv_usage();
		goto end;
	}

	if (!dtls1_2 && !tls1 && !tls1_2 && number > 1 && !reuse && !force) {
		fprintf(stderr,
		    "This case cannot work.  Use -f to perform "
		    "the test anyway (and\n-d to see what happens), "
		    "or add one of -dtls1, -tls1, -tls1_2, -reuse\n"
		    "to avoid protocol mismatch.\n");
		exit(1);
	}

	if (print_time) {
		if (!bio_pair) {
			fprintf(stderr, "Using BIO pair (-bio_pair)\n");
			bio_pair = 1;
		}
		if (number < 50 && !force)
			fprintf(stderr, "Warning: For accurate timings, use more connections (e.g. -num 1000)\n");
	}

/*	if (cipher == NULL) cipher=getenv("SSL_CIPHER"); */

	SSL_library_init();
	SSL_load_error_strings();

	if (dtls1_2)
		meth = DTLSv1_2_method();
	else if (tls1)
		meth = TLSv1_method();
	else if (tls1_2)
		meth = TLSv1_2_method();
	else
		meth = TLS_method();

	c_ctx = SSL_CTX_new(meth);
	s_ctx = SSL_CTX_new(meth);
	if ((c_ctx == NULL) || (s_ctx == NULL)) {
		ERR_print_errors(bio_err);
		goto end;
	}

	SSL_CTX_set_security_level(c_ctx, seclevel);
	SSL_CTX_set_security_level(s_ctx, seclevel);

	if (cipher != NULL) {
		SSL_CTX_set_cipher_list(c_ctx, cipher);
		SSL_CTX_set_cipher_list(s_ctx, cipher);
	}

	if (!no_dhe) {
		if (dhe1024dsa) {
			/* use SSL_OP_SINGLE_DH_USE to avoid small subgroup attacks */
			SSL_CTX_set_options(s_ctx, SSL_OP_SINGLE_DH_USE);
			dh = get_dh1024dsa();
		} else
			dh = get_dh1024();
		SSL_CTX_set_tmp_dh(s_ctx, dh);
		DH_free(dh);
	}

	if (!no_ecdhe) {
		int nid;

		if (named_curve != NULL) {
			nid = OBJ_sn2nid(named_curve);
			if (nid == 0) {
				BIO_printf(bio_err, "unknown curve name (%s)\n", named_curve);
				goto end;
			}
		} else
			nid = NID_X9_62_prime256v1;

		ecdh = EC_KEY_new_by_curve_name(nid);
		if (ecdh == NULL) {
			BIO_printf(bio_err, "unable to create curve\n");
			goto end;
		}

		SSL_CTX_set_tmp_ecdh(s_ctx, ecdh);
		SSL_CTX_set_options(s_ctx, SSL_OP_SINGLE_ECDH_USE);
		EC_KEY_free(ecdh);
	}

	if (!SSL_CTX_use_certificate_chain_file(s_ctx, server_cert)) {
		ERR_print_errors(bio_err);
	} else if (!SSL_CTX_use_PrivateKey_file(s_ctx,
	    (server_key ? server_key : server_cert), SSL_FILETYPE_PEM)) {
		ERR_print_errors(bio_err);
		goto end;
	}

	if (client_auth) {
		SSL_CTX_use_certificate_chain_file(c_ctx, client_cert);
		SSL_CTX_use_PrivateKey_file(c_ctx,
		    (client_key ? client_key : client_cert),
		    SSL_FILETYPE_PEM);
	}

	if ((!SSL_CTX_load_verify_locations(s_ctx, CAfile, CApath)) ||
	    (!SSL_CTX_set_default_verify_paths(s_ctx)) ||
	    (!SSL_CTX_load_verify_locations(c_ctx, CAfile, CApath)) ||
	    (!SSL_CTX_set_default_verify_paths(c_ctx))) {
		/* fprintf(stderr,"SSL_load_verify_locations\n"); */
		ERR_print_errors(bio_err);
		/* goto end; */
	}

	if (client_auth) {
		BIO_printf(bio_err, "client authentication\n");
		SSL_CTX_set_verify(s_ctx,
		    SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
		    verify_callback);
		SSL_CTX_set_cert_verify_callback(s_ctx, app_verify_callback,
		    app_verify_arg);
	}
	if (server_auth) {
		BIO_printf(bio_err, "server authentication\n");
		SSL_CTX_set_verify(c_ctx, SSL_VERIFY_PEER,
		    verify_callback);
		SSL_CTX_set_cert_verify_callback(c_ctx, app_verify_callback,
		    app_verify_arg);
	}

	{
		int session_id_context = 0;
		SSL_CTX_set_session_id_context(s_ctx,
		    (void *)&session_id_context, sizeof(session_id_context));
	}

	if (alpn_server != NULL)
		SSL_CTX_set_alpn_select_cb(s_ctx, cb_server_alpn, NULL);

	if (alpn_client != NULL) {
		unsigned short alpn_len;
		unsigned char *alpn = next_protos_parse(&alpn_len, alpn_client);

		if (alpn == NULL) {
			BIO_printf(bio_err, "Error parsing -alpn_client argument\n");
			goto end;
		}
		SSL_CTX_set_alpn_protos(c_ctx, alpn, alpn_len);
		free(alpn);
	}

	c_ssl = SSL_new(c_ctx);
	s_ssl = SSL_new(s_ctx);

	for (i = 0; i < number; i++) {
		if (!reuse)
			SSL_set_session(c_ssl, NULL);
#ifdef USE_BIOPAIR
		if (bio_pair)
			ret = doit_biopair(s_ssl, c_ssl, bytes, &s_time,
			    &c_time);
		else
#endif
			ret = doit(s_ssl, c_ssl, bytes);
	}

	if (!verbose) {
		print_details(c_ssl, "");
	}
	if ((number > 1) || (bytes > 1L))
		BIO_printf(bio_stdout, "%d handshakes of %ld bytes done\n",
		    number, bytes);
	if (print_time) {
#ifdef CLOCKS_PER_SEC
		/* "To determine the time in seconds, the value returned
		 * by the clock function should be divided by the value
		 * of the macro CLOCKS_PER_SEC."
		 *                                       -- ISO/IEC 9899 */
		BIO_printf(bio_stdout,
		    "Approximate total server time: %6.2f s\n"
		    "Approximate total client time: %6.2f s\n",
		    (double)s_time/CLOCKS_PER_SEC,
		    (double)c_time/CLOCKS_PER_SEC);
#else
		/* "`CLOCKS_PER_SEC' undeclared (first use this function)"
		 *                            -- cc on NeXTstep/OpenStep */
		BIO_printf(bio_stdout,
		    "Approximate total server time: %6.2f units\n"
		    "Approximate total client time: %6.2f units\n",
		    (double)s_time,
		    (double)c_time);
#endif
	}

	SSL_free(s_ssl);
	SSL_free(c_ssl);

end:
	SSL_CTX_free(s_ctx);
	SSL_CTX_free(c_ctx);
	BIO_free(bio_stdout);

	CRYPTO_cleanup_all_ex_data();
	ERR_free_strings();
	ERR_remove_thread_state(NULL);
	EVP_cleanup();
	BIO_free(bio_err);

	exit(ret);
	return ret;
}

#if USE_BIOPAIR
int
doit_biopair(SSL *s_ssl, SSL *c_ssl, long count, clock_t *s_time,
    clock_t *c_time)
{
	long cw_num = count, cr_num = count, sw_num = count, sr_num = count;
	BIO *s_ssl_bio = NULL, *c_ssl_bio = NULL;
	BIO *server = NULL, *server_io = NULL;
	BIO *client = NULL, *client_io = NULL;
	int ret = 1;

	size_t bufsiz = 256; /* small buffer for testing */

	if (!BIO_new_bio_pair(&server, bufsiz, &server_io, bufsiz))
		goto err;
	if (!BIO_new_bio_pair(&client, bufsiz, &client_io, bufsiz))
		goto err;

	s_ssl_bio = BIO_new(BIO_f_ssl());
	if (!s_ssl_bio)
		goto err;

	c_ssl_bio = BIO_new(BIO_f_ssl());
	if (!c_ssl_bio)
		goto err;

	SSL_set_connect_state(c_ssl);
	SSL_set_bio(c_ssl, client, client);
	(void)BIO_set_ssl(c_ssl_bio, c_ssl, BIO_NOCLOSE);

	SSL_set_accept_state(s_ssl);
	SSL_set_bio(s_ssl, server, server);
	(void)BIO_set_ssl(s_ssl_bio, s_ssl, BIO_NOCLOSE);

	do {
		/* c_ssl_bio:          SSL filter BIO
		 *
		 * client:             pseudo-I/O for SSL library
		 *
		 * client_io:          client's SSL communication; usually to be
		 *                     relayed over some I/O facility, but in this
		 *                     test program, we're the server, too:
		 *
		 * server_io:          server's SSL communication
		 *
		 * server:             pseudo-I/O for SSL library
		 *
		 * s_ssl_bio:          SSL filter BIO
		 *
		 * The client and the server each employ a "BIO pair":
		 * client + client_io, server + server_io.
		 * BIO pairs are symmetric.  A BIO pair behaves similar
		 * to a non-blocking socketpair (but both endpoints must
		 * be handled by the same thread).
		 * [Here we could connect client and server to the ends
		 * of a single BIO pair, but then this code would be less
		 * suitable as an example for BIO pairs in general.]
		 *
		 * Useful functions for querying the state of BIO pair endpoints:
		 *
		 * BIO_ctrl_pending(bio)              number of bytes we can read now
		 * BIO_ctrl_get_read_request(bio)     number of bytes needed to fulfil
		 *                                      other side's read attempt
		 * BIO_ctrl_get_write_guarantee(bio)   number of bytes we can write now
		 *
		 * ..._read_request is never more than ..._write_guarantee;
		 * it depends on the application which one you should use.
		 */

		/* We have non-blocking behaviour throughout this test program, but
		 * can be sure that there is *some* progress in each iteration; so
		 * we don't have to worry about ..._SHOULD_READ or ..._SHOULD_WRITE
		 * -- we just try everything in each iteration
		 */

		{
			/* CLIENT */

			char cbuf[1024*8];
			int i, r;
			clock_t c_clock = clock();

			memset(cbuf, 0, sizeof(cbuf));

			if (debug)
				if (SSL_in_init(c_ssl))
					printf("client waiting in SSL_connect - %s\n",
					    SSL_state_string_long(c_ssl));

			if (cw_num > 0) {
				/* Write to server. */

				if (cw_num > (long)sizeof cbuf)
					i = sizeof cbuf;
				else
					i = (int)cw_num;
				r = BIO_write(c_ssl_bio, cbuf, i);
				if (r < 0) {
					if (!BIO_should_retry(c_ssl_bio)) {
						fprintf(stderr, "ERROR in CLIENT\n");
						goto err;
					}
					/* BIO_should_retry(...) can just be ignored here.
					 * The library expects us to call BIO_write with
					 * the same arguments again, and that's what we will
					 * do in the next iteration. */
				} else if (r == 0) {
					fprintf(stderr, "SSL CLIENT STARTUP FAILED\n");
					goto err;
				} else {
					if (debug)
						printf("client wrote %d\n", r);
					cw_num -= r;

				}
			}

			if (cr_num > 0) {
				/* Read from server. */

				r = BIO_read(c_ssl_bio, cbuf, sizeof(cbuf));
				if (r < 0) {
					if (!BIO_should_retry(c_ssl_bio)) {
						fprintf(stderr, "ERROR in CLIENT\n");
						goto err;
					}
					/* Again, "BIO_should_retry" can be ignored. */
				} else if (r == 0) {
					fprintf(stderr, "SSL CLIENT STARTUP FAILED\n");
					goto err;
				} else {
					if (debug)
						printf("client read %d\n", r);
					cr_num -= r;
				}
			}

			/* c_time and s_time increments will typically be very small
			 * (depending on machine speed and clock tick intervals),
			 * but sampling over a large number of connections should
			 * result in fairly accurate figures.  We cannot guarantee
			 * a lot, however -- if each connection lasts for exactly
			 * one clock tick, it will be counted only for the client
			 * or only for the server or even not at all.
			 */
			*c_time += (clock() - c_clock);
		}

		{
			/* SERVER */

			char sbuf[1024*8];
			int i, r;
			clock_t s_clock = clock();

			memset(sbuf, 0, sizeof(sbuf));

			if (debug)
				if (SSL_in_init(s_ssl))
					printf("server waiting in SSL_accept - %s\n",
					    SSL_state_string_long(s_ssl));

			if (sw_num > 0) {
				/* Write to client. */

				if (sw_num > (long)sizeof sbuf)
					i = sizeof sbuf;
				else
					i = (int)sw_num;
				r = BIO_write(s_ssl_bio, sbuf, i);
				if (r < 0) {
					if (!BIO_should_retry(s_ssl_bio)) {
						fprintf(stderr, "ERROR in SERVER\n");
						goto err;
					}
					/* Ignore "BIO_should_retry". */
				} else if (r == 0) {
					fprintf(stderr, "SSL SERVER STARTUP FAILED\n");
					goto err;
				} else {
					if (debug)
						printf("server wrote %d\n", r);
					sw_num -= r;

				}
			}

			if (sr_num > 0) {
				/* Read from client. */

				r = BIO_read(s_ssl_bio, sbuf, sizeof(sbuf));
				if (r < 0) {
					if (!BIO_should_retry(s_ssl_bio)) {
						fprintf(stderr, "ERROR in SERVER\n");
						goto err;
					}
					/* blah, blah */
				} else if (r == 0) {
					fprintf(stderr, "SSL SERVER STARTUP FAILED\n");
					goto err;
				} else {
					if (debug)
						printf("server read %d\n", r);
					sr_num -= r;
				}
			}

			*s_time += (clock() - s_clock);
		}

		{
			/* "I/O" BETWEEN CLIENT AND SERVER. */

			size_t r1, r2;
			BIO *io1 = server_io, *io2 = client_io;
			/* we use the non-copying interface for io1
			 * and the standard BIO_write/BIO_read interface for io2
			 */

			static int prev_progress = 1;
			int progress = 0;

			/* io1 to io2 */
			do {
				size_t num;
				int r;

				r1 = BIO_ctrl_pending(io1);
				r2 = BIO_ctrl_get_write_guarantee(io2);

				num = r1;
				if (r2 < num)
					num = r2;
				if (num) {
					char *dataptr;

					if (INT_MAX < num) /* yeah, right */
						num = INT_MAX;

					r = BIO_nread(io1, &dataptr, (int)num);
					assert(r > 0);
					assert(r <= (int)num);
					/* possibly r < num (non-contiguous data) */
					num = r;
					r = BIO_write(io2, dataptr, (int)num);
					if (r != (int)num) /* can't happen */
					{
						fprintf(stderr, "ERROR: BIO_write could not write "
						    "BIO_ctrl_get_write_guarantee() bytes");
						goto err;
					}
					progress = 1;

					if (debug)
						printf((io1 == client_io) ?
						    "C->S relaying: %d bytes\n" :
						    "S->C relaying: %d bytes\n",
						    (int)num);
				}
			} while (r1 && r2);

			/* io2 to io1 */
			{
				size_t num;
				int r;

				r1 = BIO_ctrl_pending(io2);
				r2 = BIO_ctrl_get_read_request(io1);
				/* here we could use ..._get_write_guarantee instead of
				 * ..._get_read_request, but by using the latter
				 * we test restartability of the SSL implementation
				 * more thoroughly */
				num = r1;
				if (r2 < num)
					num = r2;
				if (num) {
					char *dataptr;

					if (INT_MAX < num)
						num = INT_MAX;

					if (num > 1)
						--num; /* test restartability even more thoroughly */

					r = BIO_nwrite0(io1, &dataptr);
					assert(r > 0);
					if (r < (int)num)
						num = r;
					r = BIO_read(io2, dataptr, (int)num);
					if (r != (int)num) /* can't happen */
					{
						fprintf(stderr, "ERROR: BIO_read could not read "
						    "BIO_ctrl_pending() bytes");
						goto err;
					}
					progress = 1;
					r = BIO_nwrite(io1, &dataptr, (int)num);
					if (r != (int)num) /* can't happen */
					{
						fprintf(stderr, "ERROR: BIO_nwrite() did not accept "
						    "BIO_nwrite0() bytes");
						goto err;
					}

					if (debug)
						printf((io2 == client_io) ?
						    "C->S relaying: %d bytes\n" :
						    "S->C relaying: %d bytes\n",
						    (int)num);
				}
			} /* no loop, BIO_ctrl_get_read_request now returns 0 anyway */

			if (!progress && !prev_progress) {
				if (cw_num > 0 || cr_num > 0 || sw_num > 0 || sr_num > 0) {
					fprintf(stderr, "ERROR: got stuck\n");
					goto err;
				}
			}
			prev_progress = progress;
		}
	} while (cw_num > 0 || cr_num > 0 || sw_num > 0 || sr_num > 0);

	if (verbose)
		print_details(c_ssl, "DONE via BIO pair: ");

	if (verify_alpn(c_ssl, s_ssl) < 0) {
		ret = 1;
		goto err;
	}

	ret = 0;

err:
	ERR_print_errors(bio_err);

	BIO_free(server);
	BIO_free(server_io);
	BIO_free(client);
	BIO_free(client_io);
	BIO_free(s_ssl_bio);
	BIO_free(c_ssl_bio);

	return ret;
}
#endif


#define W_READ	1
#define W_WRITE	2
#define C_DONE	1
#define S_DONE	2

int
doit(SSL *s_ssl, SSL *c_ssl, long count)
{
	char cbuf[1024*8], sbuf[1024*8];
	long cw_num = count, cr_num = count;
	long sw_num = count, sr_num = count;
	int ret = 1;
	BIO *c_to_s = NULL;
	BIO *s_to_c = NULL;
	BIO *c_bio = NULL;
	BIO *s_bio = NULL;
	int c_r, c_w, s_r, s_w;
	int i, j;
	int done = 0;
	int c_write, s_write;
	int do_server = 0, do_client = 0;

	memset(cbuf, 0, sizeof(cbuf));
	memset(sbuf, 0, sizeof(sbuf));

	c_to_s = BIO_new(BIO_s_mem());
	s_to_c = BIO_new(BIO_s_mem());
	if ((s_to_c == NULL) || (c_to_s == NULL)) {
		ERR_print_errors(bio_err);
		goto err;
	}

	c_bio = BIO_new(BIO_f_ssl());
	s_bio = BIO_new(BIO_f_ssl());
	if ((c_bio == NULL) || (s_bio == NULL)) {
		ERR_print_errors(bio_err);
		goto err;
	}

	SSL_set_connect_state(c_ssl);
	SSL_set_bio(c_ssl, s_to_c, c_to_s);
	BIO_set_ssl(c_bio, c_ssl, BIO_NOCLOSE);

	SSL_set_accept_state(s_ssl);
	SSL_set_bio(s_ssl, c_to_s, s_to_c);
	BIO_set_ssl(s_bio, s_ssl, BIO_NOCLOSE);

	c_r = 0;
	s_r = 1;
	c_w = 1;
	s_w = 0;
	c_write = 1, s_write = 0;

	/* We can always do writes */
	for (;;) {
		do_server = 0;
		do_client = 0;

		i = (int)BIO_pending(s_bio);
		if ((i && s_r) || s_w)
			do_server = 1;

		i = (int)BIO_pending(c_bio);
		if ((i && c_r) || c_w)
			do_client = 1;

		if (do_server && debug) {
			if (SSL_in_init(s_ssl))
				printf("server waiting in SSL_accept - %s\n",
				    SSL_state_string_long(s_ssl));
		}

		if (do_client && debug) {
			if (SSL_in_init(c_ssl))
				printf("client waiting in SSL_connect - %s\n",
				    SSL_state_string_long(c_ssl));
		}

		if (!do_client && !do_server) {
			fprintf(stdout, "ERROR in STARTUP\n");
			ERR_print_errors(bio_err);
			goto err;
		}

		if (do_client && !(done & C_DONE)) {
			if (c_write) {
				j = (cw_num > (long)sizeof(cbuf)) ?
				    (int)sizeof(cbuf) : (int)cw_num;
				i = BIO_write(c_bio, cbuf, j);
				if (i < 0) {
					c_r = 0;
					c_w = 0;
					if (BIO_should_retry(c_bio)) {
						if (BIO_should_read(c_bio))
							c_r = 1;
						if (BIO_should_write(c_bio))
							c_w = 1;
					} else {
						fprintf(stderr, "ERROR in CLIENT\n");
						ERR_print_errors(bio_err);
						goto err;
					}
				} else if (i == 0) {
					fprintf(stderr, "SSL CLIENT STARTUP FAILED\n");
					goto err;
				} else {
					if (debug)
						printf("client wrote %d\n", i);
					/* ok */
					s_r = 1;
					c_write = 0;
					cw_num -= i;
				}
			} else {
				i = BIO_read(c_bio, cbuf, sizeof(cbuf));
				if (i < 0) {
					c_r = 0;
					c_w = 0;
					if (BIO_should_retry(c_bio)) {
						if (BIO_should_read(c_bio))
							c_r = 1;
						if (BIO_should_write(c_bio))
							c_w = 1;
					} else {
						fprintf(stderr, "ERROR in CLIENT\n");
						ERR_print_errors(bio_err);
						goto err;
					}
				} else if (i == 0) {
					fprintf(stderr, "SSL CLIENT STARTUP FAILED\n");
					goto err;
				} else {
					if (debug)
						printf("client read %d\n", i);
					cr_num -= i;
					if (sw_num > 0) {
						s_write = 1;
						s_w = 1;
					}
					if (cr_num <= 0) {
						s_write = 1;
						s_w = 1;
						done = S_DONE|C_DONE;
					}
				}
			}
		}

		if (do_server && !(done & S_DONE)) {
			if (!s_write) {
				i = BIO_read(s_bio, sbuf, sizeof(cbuf));
				if (i < 0) {
					s_r = 0;
					s_w = 0;
					if (BIO_should_retry(s_bio)) {
						if (BIO_should_read(s_bio))
							s_r = 1;
						if (BIO_should_write(s_bio))
							s_w = 1;
					} else {
						fprintf(stderr, "ERROR in SERVER\n");
						ERR_print_errors(bio_err);
						goto err;
					}
				} else if (i == 0) {
					ERR_print_errors(bio_err);
					fprintf(stderr, "SSL SERVER STARTUP FAILED in SSL_read\n");
					goto err;
				} else {
					if (debug)
						printf("server read %d\n", i);
					sr_num -= i;
					if (cw_num > 0) {
						c_write = 1;
						c_w = 1;
					}
					if (sr_num <= 0) {
						s_write = 1;
						s_w = 1;
						c_write = 0;
					}
				}
			} else {
				j = (sw_num > (long)sizeof(sbuf)) ?
				    (int)sizeof(sbuf) : (int)sw_num;
				i = BIO_write(s_bio, sbuf, j);
				if (i < 0) {
					s_r = 0;
					s_w = 0;
					if (BIO_should_retry(s_bio)) {
						if (BIO_should_read(s_bio))
							s_r = 1;
						if (BIO_should_write(s_bio))
							s_w = 1;
					} else {
						fprintf(stderr, "ERROR in SERVER\n");
						ERR_print_errors(bio_err);
						goto err;
					}
				} else if (i == 0) {
					ERR_print_errors(bio_err);
					fprintf(stderr, "SSL SERVER STARTUP FAILED in SSL_write\n");
					goto err;
				} else {
					if (debug)
						printf("server wrote %d\n", i);
					sw_num -= i;
					s_write = 0;
					c_r = 1;
					if (sw_num <= 0)
						done |= S_DONE;
				}
			}
		}

		if ((done & S_DONE) && (done & C_DONE))
			break;
	}

	if (verbose)
		print_details(c_ssl, "DONE: ");

	if (verify_alpn(c_ssl, s_ssl) < 0) {
		ret = 1;
		goto err;
	}

	ret = 0;
err:
	/* We have to set the BIO's to NULL otherwise they will be
	 * free()ed twice.  Once when th s_ssl is SSL_free()ed and
	 * again when c_ssl is SSL_free()ed.
	 * This is a hack required because s_ssl and c_ssl are sharing the same
	 * BIO structure and SSL_set_bio() and SSL_free() automatically
	 * BIO_free non NULL entries.
	 * You should not normally do this or be required to do this */
	if (s_ssl != NULL) {
		s_ssl->rbio = NULL;
		s_ssl->wbio = NULL;
	}
	if (c_ssl != NULL) {
		c_ssl->rbio = NULL;
		c_ssl->wbio = NULL;
	}

	BIO_free(c_to_s);
	BIO_free(s_to_c);
	BIO_free_all(c_bio);
	BIO_free_all(s_bio);

	return (ret);
}

static int
verify_callback(int ok, X509_STORE_CTX *ctx)
{
	X509 *xs;
	char *s, buf[256];
	int error, error_depth;

	xs = X509_STORE_CTX_get_current_cert(ctx);
	s = X509_NAME_oneline(X509_get_subject_name(xs), buf, sizeof buf);
	error = X509_STORE_CTX_get_error(ctx);
	error_depth = X509_STORE_CTX_get_error_depth(ctx);
	if (s != NULL) {
		if (ok)
			fprintf(stderr, "depth=%d %s\n", error_depth, buf);
		else {
			fprintf(stderr, "depth=%d error=%d %s\n", error_depth,
			    error, buf);
		}
	}

	if (ok == 0) {
		fprintf(stderr, "Error string: %s\n",
		    X509_verify_cert_error_string(error));
		switch (error) {
		case X509_V_ERR_CERT_NOT_YET_VALID:
		case X509_V_ERR_CERT_HAS_EXPIRED:
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
			fprintf(stderr, "  ... ignored.\n");
			ok = 1;
		}
	}

	return (ok);
}

static int
app_verify_callback(X509_STORE_CTX *ctx, void *arg)
{
	X509 *xs;
	char *s = NULL, buf[256];
	const char *cb_arg = arg;

	xs = X509_STORE_CTX_get0_cert(ctx);
	fprintf(stderr, "In app_verify_callback, allowing cert. ");
	fprintf(stderr, "Arg is: %s\n", cb_arg);
	fprintf(stderr, "Finished printing do we have a context? 0x%p a cert? 0x%p\n",
	    (void *)ctx, (void *)xs);
	if (xs)
		s = X509_NAME_oneline(X509_get_subject_name(xs), buf, 256);
	if (s != NULL) {
		fprintf(stderr, "cert depth=%d %s\n",
		    X509_STORE_CTX_get_error_depth(ctx), buf);
	}

	return 1;
}

/* These DH parameters have been generated as follows:
 *    $ openssl dhparam -C -noout 1024
 *    $ openssl dhparam -C -noout -dsaparam 1024
 * (The second function has been renamed to avoid name conflicts.)
 */
static DH *
get_dh1024(void)
{
	static unsigned char dh1024_p[] = {
		0xF8, 0x81, 0x89, 0x7D, 0x14, 0x24, 0xC5, 0xD1, 0xE6, 0xF7, 0xBF, 0x3A,
		0xE4, 0x90, 0xF4, 0xFC, 0x73, 0xFB, 0x34, 0xB5, 0xFA, 0x4C, 0x56, 0xA2,
		0xEA, 0xA7, 0xE9, 0xC0, 0xC0, 0xCE, 0x89, 0xE1, 0xFA, 0x63, 0x3F, 0xB0,
		0x6B, 0x32, 0x66, 0xF1, 0xD1, 0x7B, 0xB0, 0x00, 0x8F, 0xCA, 0x87, 0xC2,
		0xAE, 0x98, 0x89, 0x26, 0x17, 0xC2, 0x05, 0xD2, 0xEC, 0x08, 0xD0, 0x8C,
		0xFF, 0x17, 0x52, 0x8C, 0xC5, 0x07, 0x93, 0x03, 0xB1, 0xF6, 0x2F, 0xB8,
		0x1C, 0x52, 0x47, 0x27, 0x1B, 0xDB, 0xD1, 0x8D, 0x9D, 0x69, 0x1D, 0x52,
		0x4B, 0x32, 0x81, 0xAA, 0x7F, 0x00, 0xC8, 0xDC, 0xE6, 0xD9, 0xCC, 0xC1,
		0x11, 0x2D, 0x37, 0x34, 0x6C, 0xEA, 0x02, 0x97, 0x4B, 0x0E, 0xBB, 0xB1,
		0x71, 0x33, 0x09, 0x15, 0xFD, 0xDD, 0x23, 0x87, 0x07, 0x5E, 0x89, 0xAB,
		0x6B, 0x7C, 0x5F, 0xEC, 0xA6, 0x24, 0xDC, 0x53,
	};
	static unsigned char dh1024_g[] = {
		0x02,
	};
	DH *dh;
	BIGNUM *dh_p = NULL, *dh_g = NULL;

	if ((dh = DH_new()) == NULL)
		return NULL;

	dh_p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), NULL);
	dh_g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), NULL);
	if (dh_p == NULL || dh_g == NULL)
		goto err;

	if (!DH_set0_pqg(dh, dh_p, NULL, dh_g))
		goto err;

	return dh;

 err:
	BN_free(dh_p);
	BN_free(dh_g);
	DH_free(dh);
	return NULL;
}

static DH *
get_dh1024dsa(void)
{
	static unsigned char dh1024_p[] = {
		0xC8, 0x00, 0xF7, 0x08, 0x07, 0x89, 0x4D, 0x90, 0x53, 0xF3, 0xD5, 0x00,
		0x21, 0x1B, 0xF7, 0x31, 0xA6, 0xA2, 0xDA, 0x23, 0x9A, 0xC7, 0x87, 0x19,
		0x3B, 0x47, 0xB6, 0x8C, 0x04, 0x6F, 0xFF, 0xC6, 0x9B, 0xB8, 0x65, 0xD2,
		0xC2, 0x5F, 0x31, 0x83, 0x4A, 0xA7, 0x5F, 0x2F, 0x88, 0x38, 0xB6, 0x55,
		0xCF, 0xD9, 0x87, 0x6D, 0x6F, 0x9F, 0xDA, 0xAC, 0xA6, 0x48, 0xAF, 0xFC,
		0x33, 0x84, 0x37, 0x5B, 0x82, 0x4A, 0x31, 0x5D, 0xE7, 0xBD, 0x52, 0x97,
		0xA1, 0x77, 0xBF, 0x10, 0x9E, 0x37, 0xEA, 0x64, 0xFA, 0xCA, 0x28, 0x8D,
		0x9D, 0x3B, 0xD2, 0x6E, 0x09, 0x5C, 0x68, 0xC7, 0x45, 0x90, 0xFD, 0xBB,
		0x70, 0xC9, 0x3A, 0xBB, 0xDF, 0xD4, 0x21, 0x0F, 0xC4, 0x6A, 0x3C, 0xF6,
		0x61, 0xCF, 0x3F, 0xD6, 0x13, 0xF1, 0x5F, 0xBC, 0xCF, 0xBC, 0x26, 0x9E,
		0xBC, 0x0B, 0xBD, 0xAB, 0x5D, 0xC9, 0x54, 0x39,
	};
	static unsigned char dh1024_g[] = {
		0x3B, 0x40, 0x86, 0xE7, 0xF3, 0x6C, 0xDE, 0x67, 0x1C, 0xCC, 0x80, 0x05,
		0x5A, 0xDF, 0xFE, 0xBD, 0x20, 0x27, 0x74, 0x6C, 0x24, 0xC9, 0x03, 0xF3,
		0xE1, 0x8D, 0xC3, 0x7D, 0x98, 0x27, 0x40, 0x08, 0xB8, 0x8C, 0x6A, 0xE9,
		0xBB, 0x1A, 0x3A, 0xD6, 0x86, 0x83, 0x5E, 0x72, 0x41, 0xCE, 0x85, 0x3C,
		0xD2, 0xB3, 0xFC, 0x13, 0xCE, 0x37, 0x81, 0x9E, 0x4C, 0x1C, 0x7B, 0x65,
		0xD3, 0xE6, 0xA6, 0x00, 0xF5, 0x5A, 0x95, 0x43, 0x5E, 0x81, 0xCF, 0x60,
		0xA2, 0x23, 0xFC, 0x36, 0xA7, 0x5D, 0x7A, 0x4C, 0x06, 0x91, 0x6E, 0xF6,
		0x57, 0xEE, 0x36, 0xCB, 0x06, 0xEA, 0xF5, 0x3D, 0x95, 0x49, 0xCB, 0xA7,
		0xDD, 0x81, 0xDF, 0x80, 0x09, 0x4A, 0x97, 0x4D, 0xA8, 0x22, 0x72, 0xA1,
		0x7F, 0xC4, 0x70, 0x56, 0x70, 0xE8, 0x20, 0x10, 0x18, 0x8F, 0x2E, 0x60,
		0x07, 0xE7, 0x68, 0x1A, 0x82, 0x5D, 0x32, 0xA2,
	};
	DH *dh;
	BIGNUM *dh_p = NULL, *dh_g = NULL;

	if ((dh = DH_new()) == NULL)
		return NULL;

	dh_p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), NULL);
	dh_g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), NULL);
	if (dh_p == NULL || dh_g == NULL)
		goto err;

	if (!DH_set0_pqg(dh, dh_p, NULL, dh_g))
		goto err;

	DH_set_length(dh, 160);

	return dh;

 err:
	BN_free(dh_p);
	BN_free(dh_g);
	DH_free(dh);
	return NULL;
}
