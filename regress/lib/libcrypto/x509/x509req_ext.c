/* $OpenBSD: x509req_ext.c,v 1.1 2021/11/03 13:08:57 schwarze Exp $ */
/*
 * Copyright (c) 2020, 2021 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/err.h>
#include <openssl/x509.h>

void	 fail_head(const char *);
void	 fail_tail(void);
void	 fail_str(const char *, const char *);
void	 fail_int(const char *, int);
void	 fail_ptr(const char *, const void *);

static const char	*testname;
static int		 errcount;

void
fail_head(const char *stepname)
{
	fprintf(stderr, "failure#%d testname=%s stepname=%s ",
	    ++errcount, testname, stepname);
}

void
fail_tail(void)
{
	unsigned long    errnum;

	if ((errnum = ERR_get_error()))
		fprintf(stderr, "OpenSSL says: %s\n",
		    ERR_error_string(errnum, NULL));
	if (errno)
		fprintf(stderr, "libc says: %s\n", strerror(errno));
}

void
fail_str(const char *stepname, const char *result)
{
	fail_head(stepname);
	fprintf(stderr, "wrong result=%s\n", result);
	fail_tail();
}

void
fail_int(const char *stepname, int result)
{
	fail_head(stepname);
	fprintf(stderr, "wrong result=%d\n", result);
	fail_tail();
}

void
fail_ptr(const char *stepname, const void *result)
{
	fail_head(stepname);
	fprintf(stderr, "wrong result=%p\n", result);
	fail_tail();
}

int
main(void)
{
	X509_REQ	*req;
	X509_EXTENSIONS	*exts;
	X509_ATTRIBUTE	*attr;
	ASN1_TYPE	*aval;
	int 		 irc;

	testname = "exts=NULL";
	if ((req = X509_REQ_new()) == NULL) {
		fail_str("X509_REQ_new", "NULL");
		return 1;
	}
	if ((irc = X509_REQ_add_extensions(req, NULL)) != 0)
		fail_int("X509_REQ_add_extensions", irc);
	if ((irc = X509_REQ_get_attr_count(req)) != 0)
		fail_int("X509_REQ_get_attr_count", irc);
	if ((attr = X509_REQ_get_attr(req, 0)) != NULL)
		fail_ptr("X509_REQ_get_attr", attr);
	X509_REQ_free(req);

	testname = "nid=-1";
	if ((req = X509_REQ_new()) == NULL) {
		fail_str("X509_REQ_new", "NULL");
		return 1;
	}
	if ((exts = sk_X509_EXTENSION_new_null()) == NULL) {
		fail_str("sk_X509_EXTENSION_new_null", "NULL");
		return 1;
	}
	if ((irc = X509_REQ_add_extensions_nid(req, exts, -1)) != 0)
		fail_int("X509_REQ_add_extensions", irc);
	if ((irc = X509_REQ_get_attr_count(req)) != 0)
		fail_int("X509_REQ_get_attr_count", irc);
	if ((attr = X509_REQ_get_attr(req, 0)) != NULL)
		fail_ptr("X509_REQ_get_attr", attr);
	X509_REQ_free(req);

	testname = "valid";
	if ((req = X509_REQ_new()) == NULL) {
		fail_str("X509_REQ_new", "NULL");
		return 1;
	}
	if ((irc = X509_REQ_add_extensions(req, exts)) != 1)
		fail_int("X509_REQ_add_extensions", irc);
	sk_X509_EXTENSION_free(exts);
	if ((irc = X509_REQ_get_attr_count(req)) != 1)
		fail_int("X509_REQ_get_attr_count", irc);
	if ((attr = X509_REQ_get_attr(req, 0)) == NULL) {
		fail_str("X509_REQ_get_attr", "NULL");
		goto end_valid;
	}
	if ((irc = X509_ATTRIBUTE_count(attr)) != 1)
		fail_int("X509_ATTRIBUTE_count", irc);
	if ((aval = X509_ATTRIBUTE_get0_type(attr, 0)) == NULL) {
		fail_str("X509_ATTRIBUTE_get0_type", "NULL");
		goto end_valid;
	}
	if ((irc = ASN1_TYPE_get(aval)) != V_ASN1_SEQUENCE)
		fail_int("ASN1_TYPE_get", irc);
	exts = ASN1_item_unpack(aval->value.sequence, &X509_EXTENSIONS_it);
	if (exts == NULL) {
		fail_str("ASN1_item_unpack", "NULL");
		goto end_valid;
	}
	if ((irc = sk_X509_EXTENSION_num(exts)) != 0)
		fail_int("sk_X509_EXTENSION_num", irc);
	sk_X509_EXTENSION_free(exts);

end_valid:
	testname = "getext";
	if ((exts = X509_REQ_get_extensions(req)) == NULL) {
		fail_str("X509_REQ_get_extensions", "NULL");
		goto end_getext;
	}
	if ((irc = sk_X509_EXTENSION_num(exts)) != 0)
		fail_int("sk_X509_EXTENSION_num", irc);
	sk_X509_EXTENSION_free(exts);

end_getext:
	X509_REQ_free(req);
	return errcount != 0;
}
