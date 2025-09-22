/* $OpenBSD: ocsp_ht.c,v 1.28 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/ocsp.h>
#include <openssl/buffer.h>

#include "err_local.h"

/* Stateful OCSP request code, supporting non-blocking I/O */

/* Opaque OCSP request status structure */

struct ocsp_req_ctx_st {
	int state;		/* Current I/O state */
	unsigned char *iobuf;	/* Line buffer */
	int iobuflen;		/* Line buffer length */
	BIO *io;		/* BIO to perform I/O with */
	BIO *mem;		/* Memory BIO response is built into */
	unsigned long asn1_len;	/* ASN1 length of response */
};

#define OCSP_MAX_REQUEST_LENGTH	(100 * 1024)
#define OCSP_MAX_LINE_LEN	4096;

/* OCSP states */

/* If set no reading should be performed */
#define OHS_NOREAD		0x1000
/* Error condition */
#define OHS_ERROR		(0 | OHS_NOREAD)
/* First line being read */
#define OHS_FIRSTLINE		1
/* MIME headers being read */
#define OHS_HEADERS		2
/* OCSP initial header (tag + length) being read */
#define OHS_ASN1_HEADER		3
/* OCSP content octets being read */
#define OHS_ASN1_CONTENT	4
/* Request being sent */
#define OHS_ASN1_WRITE		(6 | OHS_NOREAD)
/* Request being flushed */
#define OHS_ASN1_FLUSH		(7 | OHS_NOREAD)
/* Completed */
#define OHS_DONE		(8 | OHS_NOREAD)


static int parse_http_line1(char *line);

void
OCSP_REQ_CTX_free(OCSP_REQ_CTX *rctx)
{
	if (rctx == NULL)
		return;

	BIO_free(rctx->mem);
	free(rctx->iobuf);
	free(rctx);
}
LCRYPTO_ALIAS(OCSP_REQ_CTX_free);

int
OCSP_REQ_CTX_set1_req(OCSP_REQ_CTX *rctx, OCSP_REQUEST *req)
{
	if (BIO_printf(rctx->mem, "Content-Type: application/ocsp-request\r\n"
	    "Content-Length: %d\r\n\r\n", i2d_OCSP_REQUEST(req, NULL)) <= 0)
		return 0;
	if (i2d_OCSP_REQUEST_bio(rctx->mem, req) <= 0)
		return 0;
	rctx->state = OHS_ASN1_WRITE;
	rctx->asn1_len = BIO_get_mem_data(rctx->mem, NULL);
	return 1;
}
LCRYPTO_ALIAS(OCSP_REQ_CTX_set1_req);

int
OCSP_REQ_CTX_add1_header(OCSP_REQ_CTX *rctx, const char *name,
    const char *value)
{
	if (!name)
		return 0;
	if (BIO_puts(rctx->mem, name) <= 0)
		return 0;
	if (value) {
		if (BIO_write(rctx->mem, ": ", 2) != 2)
			return 0;
		if (BIO_puts(rctx->mem, value) <= 0)
			return 0;
	}
	if (BIO_write(rctx->mem, "\r\n", 2) != 2)
		return 0;
	return 1;
}
LCRYPTO_ALIAS(OCSP_REQ_CTX_add1_header);

OCSP_REQ_CTX *
OCSP_sendreq_new(BIO *io, const char *path, OCSP_REQUEST *req, int maxline)
{
	OCSP_REQ_CTX *rctx;

	rctx = malloc(sizeof(OCSP_REQ_CTX));
	if (rctx == NULL)
		return NULL;
	rctx->state = OHS_ERROR;
	if ((rctx->mem = BIO_new(BIO_s_mem())) == NULL) {
		free(rctx);
		return NULL;
	}
	rctx->io = io;
	rctx->asn1_len = 0;
	if (maxline > 0)
		rctx->iobuflen = maxline;
	else
		rctx->iobuflen = OCSP_MAX_LINE_LEN;
	rctx->iobuf = malloc(rctx->iobuflen);
	if (!rctx->iobuf) {
		BIO_free(rctx->mem);
		free(rctx);
		return NULL;
	}
	if (!path)
		path = "/";

	if (BIO_printf(rctx->mem, "POST %s HTTP/1.0\r\n", path) <= 0) {
		free(rctx->iobuf);
		BIO_free(rctx->mem);
		free(rctx);
		return NULL;
	}

	if (req && !OCSP_REQ_CTX_set1_req(rctx, req)) {
		free(rctx->iobuf);
		BIO_free(rctx->mem);
		free(rctx);
		return NULL;
	}

	return rctx;
}
LCRYPTO_ALIAS(OCSP_sendreq_new);

/* Parse the HTTP response. This will look like this:
 * "HTTP/1.0 200 OK". We need to obtain the numeric code and
 * (optional) informational message.
 */
static int
parse_http_line1(char *line)
{
	int retcode;
	char *p, *q, *r;

	/* Skip to first white space (passed protocol info) */
	for (p = line; *p && !isspace((unsigned char)*p); p++)
		continue;
	if (!*p) {
		OCSPerror(OCSP_R_SERVER_RESPONSE_PARSE_ERROR);
		return 0;
	}

	/* Skip past white space to start of response code */
	while (*p && isspace((unsigned char)*p))
		p++;
	if (!*p) {
		OCSPerror(OCSP_R_SERVER_RESPONSE_PARSE_ERROR);
		return 0;
	}

	/* Find end of response code: first whitespace after start of code */
	for (q = p; *q && !isspace((unsigned char)*q); q++)
		continue;
	if (!*q) {
		OCSPerror(OCSP_R_SERVER_RESPONSE_PARSE_ERROR);
		return 0;
	}

	/* Set end of response code and start of message */
	*q++ = 0;

	/* Attempt to parse numeric code */
	retcode = strtoul(p, &r, 10);

	if (*r)
		return 0;

	/* Skip over any leading white space in message */
	while (*q && isspace((unsigned char)*q))
		q++;
	if (*q) {
		/* Finally zap any trailing white space in message (include
		 * CRLF) */

		/* We know q has a non white space character so this is OK */
		for (r = q + strlen(q) - 1; isspace((unsigned char)*r); r--)
			*r = 0;
	}
	if (retcode != 200) {
		OCSPerror(OCSP_R_SERVER_RESPONSE_ERROR);
		if (!*q)
			ERR_asprintf_error_data("Code=%s", p);
		else
			ERR_asprintf_error_data("Code=%s,Reason=%s", p, q);
		return 0;
	}

	return 1;
}

int
OCSP_sendreq_nbio(OCSP_RESPONSE **presp, OCSP_REQ_CTX *rctx)
{
	int i, n;
	const unsigned char *p;

next_io:
	if (!(rctx->state & OHS_NOREAD)) {
		n = BIO_read(rctx->io, rctx->iobuf, rctx->iobuflen);

		if (n <= 0) {
			if (BIO_should_retry(rctx->io))
				return -1;
			return 0;
		}

		/* Write data to memory BIO */
		if (BIO_write(rctx->mem, rctx->iobuf, n) != n)
			return 0;
	}

	switch (rctx->state) {
	case OHS_ASN1_WRITE:
		n = BIO_get_mem_data(rctx->mem, &p);
		i = BIO_write(rctx->io,
		    p + (n - rctx->asn1_len), rctx->asn1_len);
		if (i <= 0) {
			if (BIO_should_retry(rctx->io))
				return -1;
			rctx->state = OHS_ERROR;
			return 0;
		}

		rctx->asn1_len -= i;
		if (rctx->asn1_len > 0)
			goto next_io;

		rctx->state = OHS_ASN1_FLUSH;

		(void)BIO_reset(rctx->mem);
		/* FALLTHROUGH */

	case OHS_ASN1_FLUSH:
		i = BIO_flush(rctx->io);
		if (i > 0) {
			rctx->state = OHS_FIRSTLINE;
			goto next_io;
		}

		if (BIO_should_retry(rctx->io))
			return -1;

		rctx->state = OHS_ERROR;
		return 0;

	case OHS_ERROR:
		return 0;

	case OHS_FIRSTLINE:
	case OHS_HEADERS:
		/* Attempt to read a line in */
next_line:
		/* Due to &%^*$" memory BIO behaviour with BIO_gets we
		 * have to check there's a complete line in there before
		 * calling BIO_gets or we'll just get a partial read.
		 */
		n = BIO_get_mem_data(rctx->mem, &p);
		if ((n <= 0) || !memchr(p, '\n', n)) {
			if (n >= rctx->iobuflen) {
				rctx->state = OHS_ERROR;
				return 0;
			}
			goto next_io;
		}
		n = BIO_gets(rctx->mem, (char *)rctx->iobuf, rctx->iobuflen);
		if (n <= 0) {
			if (BIO_should_retry(rctx->mem))
				goto next_io;
			rctx->state = OHS_ERROR;
			return 0;
		}

		/* Don't allow excessive lines */
		if (n == rctx->iobuflen) {
			rctx->state = OHS_ERROR;
			return 0;
		}

		/* First line */
		if (rctx->state == OHS_FIRSTLINE) {
			if (parse_http_line1((char *)rctx->iobuf)) {
				rctx->state = OHS_HEADERS;
				goto next_line;
			} else {
				rctx->state = OHS_ERROR;
				return 0;
			}
		} else {
			/* Look for blank line: end of headers */
			for (p = rctx->iobuf; *p; p++) {
				if ((*p != '\r') && (*p != '\n'))
					break;
			}
			if (*p)
				goto next_line;

			rctx->state = OHS_ASN1_HEADER;
		}
		/* FALLTHROUGH */

	case OHS_ASN1_HEADER:
		/* Now reading ASN1 header: can read at least 2 bytes which
		 * is enough for ASN1 SEQUENCE header and either length field
		 * or at least the length of the length field.
		 */
		n = BIO_get_mem_data(rctx->mem, &p);
		if (n < 2)
			goto next_io;

		/* Check it is an ASN1 SEQUENCE */
		if (*p++ != (V_ASN1_SEQUENCE|V_ASN1_CONSTRUCTED)) {
			rctx->state = OHS_ERROR;
			return 0;
		}

		/* Check out length field */
		if (*p & 0x80) {
			/* If MSB set on initial length octet we can now
			 * always read 6 octets: make sure we have them.
			 */
			if (n < 6)
				goto next_io;
			n = *p & 0x7F;
			/* Not NDEF or excessive length */
			if (!n || (n > 4)) {
				rctx->state = OHS_ERROR;
				return 0;
			}
			p++;
			rctx->asn1_len = 0;
			for (i = 0; i < n; i++) {
				rctx->asn1_len <<= 8;
				rctx->asn1_len |= *p++;
			}

			if (rctx->asn1_len > OCSP_MAX_REQUEST_LENGTH) {
				rctx->state = OHS_ERROR;
				return 0;
			}

			rctx->asn1_len += n + 2;
		} else
			rctx->asn1_len = *p + 2;

		rctx->state = OHS_ASN1_CONTENT;

		/* FALLTHROUGH */

	case OHS_ASN1_CONTENT:
		n = BIO_get_mem_data(rctx->mem, &p);
		if (n < (int)rctx->asn1_len)
			goto next_io;

		*presp = d2i_OCSP_RESPONSE(NULL, &p, rctx->asn1_len);
		if (*presp) {
			rctx->state = OHS_DONE;
			return 1;
		}

		rctx->state = OHS_ERROR;
		return 0;

	case OHS_DONE:
		return 1;
	}

	return 0;
}
LCRYPTO_ALIAS(OCSP_sendreq_nbio);

/* Blocking OCSP request handler: now a special case of non-blocking I/O */
OCSP_RESPONSE *
OCSP_sendreq_bio(BIO *b, const char *path, OCSP_REQUEST *req)
{
	OCSP_RESPONSE *resp = NULL;
	OCSP_REQ_CTX *ctx;
	int rv;

	ctx = OCSP_sendreq_new(b, path, req, -1);
	if (ctx == NULL)
		return NULL;

	do {
		rv = OCSP_sendreq_nbio(&resp, ctx);
	} while ((rv == -1) && BIO_should_retry(b));

	OCSP_REQ_CTX_free(ctx);

	if (rv)
		return resp;

	return NULL;
}
LCRYPTO_ALIAS(OCSP_sendreq_bio);
