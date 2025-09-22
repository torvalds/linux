/*	$Id: certproc.c,v 1.13 2020/09/14 15:58:50 florian Exp $ */
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

#include "extern.h"

#define BEGIN_MARKER "-----BEGIN CERTIFICATE-----"
#define END_MARKER "-----END CERTIFICATE-----"

int
certproc(int netsock, int filesock)
{
	char		*csr = NULL, *chain = NULL, *url = NULL;
	char		*chaincp;
	size_t		 csrsz, chainsz;
	int		 rc = 0, cc;
	enum certop	 op;
	long		 lval;

	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	/* Read what the netproc wants us to do. */

	op = CERT__MAX;
	if ((lval = readop(netsock, COMM_CSR_OP)) == 0)
		op = CERT_STOP;
	else if (lval == CERT_REVOKE || lval == CERT_UPDATE)
		op = lval;

	if (CERT_STOP == op) {
		rc = 1;
		goto out;
	} else if (CERT__MAX == op) {
		warnx("unknown operation from netproc");
		goto out;
	}

	/*
	 * Pass revocation right through to fileproc.
	 * If the reader is terminated, ignore it.
	 */

	if (CERT_REVOKE == op) {
		if (writeop(filesock, COMM_CHAIN_OP, FILE_REMOVE) >= 0)
			rc = 1;
		goto out;
	}

	/*
	 * Wait until we receive the DER encoded (signed) certificate
	 * from the network process.
	 * Then convert the DER encoding into an X509 certificate.
	 */

	if ((csr = readbuf(netsock, COMM_CSR, &csrsz)) == NULL)
		goto out;

	if (csrsz < strlen(END_MARKER)) {
		warnx("invalid cert");
		goto out;
	}

	chaincp = strstr(csr, END_MARKER);

	if (chaincp == NULL) {
		warnx("invalid cert");
		goto out;
	}

	chaincp += strlen(END_MARKER);

	if ((chaincp = strstr(chaincp, BEGIN_MARKER)) == NULL) {
		warnx("invalid certificate chain");
		goto out;
	}

	if ((chain = strdup(chaincp)) == NULL) {
		warn("strdup");
		goto out;
	}

	*chaincp = '\0';
	chainsz = strlen(chain);
	csrsz = strlen(csr);

	/* Allow reader termination to just push us out. */

	if ((cc = writeop(filesock, COMM_CHAIN_OP, FILE_CREATE)) == 0)
		rc = 1;
	if (cc <= 0)
		goto out;
	if ((cc = writebuf(filesock, COMM_CHAIN, chain, chainsz)) == 0)
		rc = 1;
	if (cc <= 0)
		goto out;

	if (writebuf(filesock, COMM_CSR, csr, csrsz) < 0)
		goto out;

	rc = 1;
out:
	close(netsock);
	close(filesock);
	free(csr);
	free(url);
	free(chain);
	return rc;
}
