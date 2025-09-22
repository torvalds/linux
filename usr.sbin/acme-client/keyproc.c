/*	$Id: keyproc.c,v 1.18 2022/08/28 18:30:29 tb Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "extern.h"
#include "key.h"

/*
 * This was lifted more or less directly from demos/x509/mkreq.c of the
 * OpenSSL source code.
 */
static int
add_ext(STACK_OF(X509_EXTENSION) *sk, int nid, const char *value)
{
	X509_EXTENSION	*ex;
	char		*cp;

	/*
	 * XXX: I don't like this at all.
	 * There's no documentation for X509V3_EXT_conf_nid, so I'm not
	 * sure if the "value" parameter is ever written to, touched,
	 * etc.
	 * The 'official' examples suggest not (they use a string
	 * literal as the input), but to be safe, I'm doing an
	 * allocation here and just letting it go.
	 * This leaks memory, but bounded to the number of SANs.
	 */

	if ((cp = strdup(value)) == NULL) {
		warn("strdup");
		return (0);
	}
	ex = X509V3_EXT_conf_nid(NULL, NULL, nid, cp);
	if (ex == NULL) {
		warnx("X509V3_EXT_conf_nid");
		free(cp);
		return (0);
	}
	sk_X509_EXTENSION_push(sk, ex);
	return (1);
}

/*
 * Create an X509 certificate from the private key we have on file.
 * To do this, we first open the key file, then jail ourselves.
 * We then use the crypto library to create the certificate within the
 * jail and, on success, ship it to "netsock" as an X509 request.
 */
int
keyproc(int netsock, const char *keyfile, const char **alts, size_t altsz,
    enum keytype keytype)
{
	char		*der64 = NULL, *der = NULL, *dercp;
	char		*sans = NULL, *san = NULL;
	FILE		*f;
	size_t		 i, sansz;
	void		*pp;
	EVP_PKEY	*pkey = NULL;
	X509_REQ	*x = NULL;
	X509_NAME	*name = NULL;
	int		 len, rc = 0, cc, nid, newkey = 0;
	mode_t		 prev;
	STACK_OF(X509_EXTENSION) *exts = NULL;

	/*
	 * First, open our private key file read-only or write-only if
	 * we're creating from scratch.
	 * Set our umask to be maximally restrictive.
	 */

	prev = umask((S_IWUSR | S_IXUSR) | S_IRWXG | S_IRWXO);
	if ((f = fopen(keyfile, "r")) == NULL && errno == ENOENT) {
		f = fopen(keyfile, "wx");
		newkey = 1;
	}
	umask(prev);

	if (f == NULL) {
		warn("%s", keyfile);
		goto out;
	}

	/* File-system, user, and sandbox jail. */

	ERR_load_crypto_strings();

	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	if (newkey) {
		switch (keytype) {
		case KT_ECDSA:
			if ((pkey = ec_key_create(f, keyfile)) == NULL)
				goto out;
			dodbg("%s: generated ECDSA domain key", keyfile);
			break;
		case KT_RSA:
			if ((pkey = rsa_key_create(f, keyfile)) == NULL)
				goto out;
			dodbg("%s: generated RSA domain key", keyfile);
			break;
		}
	} else {
		if ((pkey = key_load(f, keyfile)) == NULL)
			goto out;
		/* XXX check if domain key type equals configured key type */
		doddbg("%s: loaded domain key", keyfile);
	}

	fclose(f);
	f = NULL;

	/*
	 * Generate our certificate from the EVP public key.
	 * Then set it as the X509 requester's key.
	 */

	if ((x = X509_REQ_new()) == NULL) {
		warnx("X509_REQ_new");
		goto out;
	} else if (!X509_REQ_set_version(x, 0)) {
		warnx("X509_REQ_set_version");
		goto out;
	} else if (!X509_REQ_set_pubkey(x, pkey)) {
		warnx("X509_REQ_set_pubkey");
		goto out;
	}

	/* Now specify the common name that we'll request. */

	if ((name = X509_NAME_new()) == NULL) {
		warnx("X509_NAME_new");
		goto out;
	} else if (!X509_NAME_add_entry_by_txt(name, "CN",
		MBSTRING_ASC, (u_char *)alts[0], -1, -1, 0)) {
		warnx("X509_NAME_add_entry_by_txt: CN=%s", alts[0]);
		goto out;
	} else if (!X509_REQ_set_subject_name(x, name)) {
		warnx("X509_req_set_issuer_name");
		goto out;
	}

	/*
	 * Now add the SAN extensions.
	 * This was lifted more or less directly from demos/x509/mkreq.c
	 * of the OpenSSL source code.
	 * (The zeroth altname is the domain name.)
	 * TODO: is this the best way of doing this?
	 */

	nid = NID_subject_alt_name;
	if ((exts = sk_X509_EXTENSION_new_null()) == NULL) {
		warnx("sk_X509_EXTENSION_new_null");
		goto out;
	}
	/* Initialise to empty string. */
	if ((sans = strdup("")) == NULL) {
		warn("strdup");
		goto out;
	}
	sansz = strlen(sans) + 1;

	/*
	 * For each SAN entry, append it to the string.
	 * We need a single SAN entry for all of the SAN
	 * domains: NOT an entry per domain!
	 */

	for (i = 0; i < altsz; i++) {
		cc = asprintf(&san, "%sDNS:%s",
		    i ? "," : "", alts[i]);
		if (cc == -1) {
			warn("asprintf");
			goto out;
		}
		pp = recallocarray(sans, sansz, sansz + strlen(san), 1);
		if (pp == NULL) {
			warn("recallocarray");
			goto out;
		}
		sans = pp;
		sansz += strlen(san);
		strlcat(sans, san, sansz);
		free(san);
		san = NULL;
	}

	if (!add_ext(exts, nid, sans)) {
		warnx("add_ext");
		goto out;
	} else if (!X509_REQ_add_extensions(x, exts)) {
		warnx("X509_REQ_add_extensions");
		goto out;
	}
	sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);

	/* Sign the X509 request using SHA256. */

	if (!X509_REQ_sign(x, pkey, EVP_sha256())) {
		warnx("X509_sign");
		goto out;
	}

	/* Now, serialise to DER, then base64. */

	if ((len = i2d_X509_REQ(x, NULL)) < 0) {
		warnx("i2d_X509_REQ");
		goto out;
	} else if ((der = dercp = malloc(len)) == NULL) {
		warn("malloc");
		goto out;
	} else if (len != i2d_X509_REQ(x, (u_char **)&dercp)) {
		warnx("i2d_X509_REQ");
		goto out;
	} else if ((der64 = base64buf_url(der, len)) == NULL) {
		warnx("base64buf_url");
		goto out;
	}

	/*
	 * Write that we're ready, then write.
	 * We ignore reader-closed failure, as we're just going to roll
	 * into the exit case anyway.
	 */

	if (writeop(netsock, COMM_KEY_STAT, KEY_READY) < 0)
		goto out;
	if (writestr(netsock, COMM_CERT, der64) < 0)
		goto out;

	rc = 1;
out:
	close(netsock);
	if (f != NULL)
		fclose(f);
	free(der);
	free(der64);
	free(sans);
	free(san);
	X509_REQ_free(x);
	X509_NAME_free(name);
	EVP_PKEY_free(pkey);
	ERR_print_errors_fp(stderr);
	ERR_free_strings();
	return rc;
}
