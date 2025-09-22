/*	$Id: revokeproc.c,v 1.26 2025/09/18 13:22:36 florian Exp $ */
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

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

#include "extern.h"

/*
 * Convert the X509's notAfter time into a time_t value.
 */
static time_t
X509notafter(X509 *x)
{
	ASN1_TIME	*atim;
	struct tm	 t;

	if ((atim = X509_getm_notAfter(x)) == NULL)
		return -1;

	memset(&t, 0, sizeof(t));

	if (!ASN1_TIME_to_tm(atim, &t))
		return -1;

	return timegm(&t);
}

/*
 * Convert the X509's notBefore time into a time_t value.
 */
static time_t
X509notbefore(X509 *x)
{
	ASN1_TIME	*atim;
	struct tm	 t;

	if ((atim = X509_getm_notBefore(x)) == NULL)
		return -1;

	memset(&t, 0, sizeof(t));

	if (!ASN1_TIME_to_tm(atim, &t))
		return -1;

	return timegm(&t);
}

int
revokeproc(int fd, const char *certfile, int force,
    int revocate, const char *const *alts, size_t altsz)
{
	GENERAL_NAMES			*sans = NULL;
	char				*der = NULL, *dercp, *der64 = NULL;
	int				 rc = 0, cc, i, len;
	size_t				*found = NULL;
	FILE				*f = NULL;
	X509				*x = NULL;
	long				 lval;
	enum revokeop			 op, rop;
	time_t				 notafter, notbefore, cert_validity;
	time_t				 remaining_validity, renew_allow;
	size_t				 j;

	/*
	 * First try to open the certificate before we drop privileges
	 * and jail ourselves.
	 * We allow "f" to be NULL IFF the cert doesn't exist yet.
	 */

	if ((f = fopen(certfile, "r")) == NULL && errno != ENOENT) {
		warn("%s", certfile);
		goto out;
	}

	/* File-system and sandbox jailing. */

	ERR_load_crypto_strings();

	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	/*
	 * If we couldn't open the certificate, it doesn't exist so we
	 * haven't submitted it yet, so obviously we can mark that it
	 * has expired and we should renew it.
	 * If we're revoking, however, then that's an error!
	 * Ignore if the reader isn't reading in either case.
	 */

	if (f == NULL && revocate) {
		warnx("%s: no certificate found", certfile);
		(void)writeop(fd, COMM_REVOKE_RESP, REVOKE_OK);
		goto out;
	} else if (f == NULL && !revocate) {
		if (writeop(fd, COMM_REVOKE_RESP, REVOKE_EXP) >= 0)
			rc = 1;
		goto out;
	}

	if ((x = PEM_read_X509(f, NULL, NULL, NULL)) == NULL) {
		warnx("PEM_read_X509");
		goto out;
	}

	/* Cache and sanity check X509v3 extensions. */

	if (X509_check_purpose(x, -1, -1) <= 0) {
		warnx("%s: invalid X509v3 extensions", certfile);
		goto out;
	}

	/* Read out the expiration date. */

	if ((notafter = X509notafter(x)) == -1) {
		warnx("X509notafter");
		goto out;
	}

	if ((notbefore = X509notbefore(x)) == -1) {
		warnx("X509notbefore");
		goto out;
	}

	/* Extract list of SAN entries from the certificate. */

	sans = X509_get_ext_d2i(x, NID_subject_alt_name, NULL, NULL);
	if (sans == NULL) {
		warnx("%s: does not have a SAN entry", certfile);
		if (revocate)
			goto out;
		force = 2;
	}

	/* An array of buckets: the number of entries found. */

	if ((found = calloc(altsz, sizeof(size_t))) == NULL) {
		warn("calloc");
		goto out;
	}

	/*
	 * Ensure the certificate's SAN entries fully cover those from the
	 * configuration file and that all domains are represented only once.
	 */

	for (i = 0; i < sk_GENERAL_NAME_num(sans); i++) {
		GENERAL_NAME		*gen_name;
		const ASN1_IA5STRING	*name;
		const unsigned char	*name_buf;
		int			 name_len;
		int			 name_type;

		gen_name = sk_GENERAL_NAME_value(sans, i);
		assert(gen_name != NULL);

		name = GENERAL_NAME_get0_value(gen_name, &name_type);
		if (name_type != GEN_DNS)
			continue;

		/* name_buf isn't a C string and could contain embedded NULs. */
		name_buf = ASN1_STRING_get0_data(name);
		name_len = ASN1_STRING_length(name);

		for (j = 0; j < altsz; j++) {
			if ((size_t)name_len != strlen(alts[j]))
				continue;
			if (memcmp(name_buf, alts[j], name_len) == 0)
				break;
		}
		if (j == altsz) {
			if (revocate) {
				char *visbuf;

				visbuf = calloc(4, name_len + 1);
				if (visbuf == NULL) {
					warn("%s: unexpected SAN", certfile);
					goto out;
				}
				strvisx(visbuf, name_buf, name_len, VIS_SAFE);
				warnx("%s: unexpected SAN entry: %s",
				    certfile, visbuf);
				free(visbuf);
				goto out;
			}
			force = 2;
			continue;
		}
		if (found[j]++) {
			if (revocate) {
				warnx("%s: duplicate SAN entry: %.*s",
				    certfile, name_len, name_buf);
				goto out;
			}
			force = 2;
		}
	}

	for (j = 0; j < altsz; j++) {
		if (found[j])
			continue;
		if (revocate) {
			warnx("%s: domain not listed: %s", certfile, alts[j]);
			goto out;
		}
		force = 2;
	}

	/*
	 * If we're going to revoke, write the certificate to the
	 * netproc in DER and base64-encoded format.
	 * Then exit: we have nothing left to do.
	 */

	if (revocate) {
		dodbg("%s: revocation", certfile);

		/*
		 * First, tell netproc we're online.
		 * If they're down, then just exit without warning.
		 */

		cc = writeop(fd, COMM_REVOKE_RESP, REVOKE_EXP);
		if (cc == 0)
			rc = 1;
		if (cc <= 0)
			goto out;

		if ((len = i2d_X509(x, NULL)) < 0) {
			warnx("i2d_X509");
			goto out;
		} else if ((der = dercp = malloc(len)) == NULL) {
			warn("malloc");
			goto out;
		} else if (len != i2d_X509(x, (u_char **)&dercp)) {
			warnx("i2d_X509");
			goto out;
		} else if ((der64 = base64buf_url(der, len)) == NULL) {
			warnx("base64buf_url");
			goto out;
		} else if (writestr(fd, COMM_CSR, der64) >= 0)
			rc = 1;

		goto out;
	}

	cert_validity = notafter - notbefore;

	if (cert_validity < 0) {
		warnx("Invalid cert, expire time before inception time");
		rc = -1;
		goto out;
	}
	if (cert_validity > 10 * 24 * 60 * 60)
		renew_allow = cert_validity / 3;
	else
		renew_allow = cert_validity / 2;

	/* We suggest to run renewals daily. Make sure we have 2 chances. */
	if (renew_allow < 3 * 24 * 60 * 60)
		renew_allow = 3 * 24 * 60 * 60;

	remaining_validity = notafter - time(NULL);

	if (remaining_validity < renew_allow)
		rop = REVOKE_EXP;
	else
		rop = REVOKE_OK;

	if (rop == REVOKE_EXP)
		dodbg("%s: certificate renewable: %lld days left",
		    certfile, (long long)(remaining_validity / 24 / 60 / 60));
	else
		dodbg("%s: certificate valid: %lld days left",
		    certfile, (long long)(remaining_validity / 24 / 60 / 60));

	if (rop == REVOKE_OK && force) {
		warnx("%s: %sforcing renewal", certfile,
		    force == 2 ? "domain list changed, " : "");
		rop = REVOKE_EXP;
	}

	/*
	 * We can re-submit it given RENEW_ALLOW time before.
	 * If netproc is down, just exit.
	 */

	if ((cc = writeop(fd, COMM_REVOKE_RESP, rop)) == 0)
		rc = 1;
	if (cc <= 0)
		goto out;

	op = REVOKE__MAX;
	if ((lval = readop(fd, COMM_REVOKE_OP)) == 0)
		op = REVOKE_STOP;
	else if (lval == REVOKE_CHECK)
		op = lval;

	if (op == REVOKE__MAX) {
		warnx("unknown operation from netproc");
		goto out;
	} else if (op == REVOKE_STOP) {
		rc = 1;
		goto out;
	}

	rc = 1;
out:
	close(fd);
	if (f != NULL)
		fclose(f);
	X509_free(x);
	GENERAL_NAMES_free(sans);
	free(der);
	free(found);
	free(der64);
	ERR_print_errors_fp(stderr);
	ERR_free_strings();
	return rc;
}
