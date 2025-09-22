/* $OpenBSD: x509attribute.c,v 1.3 2021/11/01 08:28:31 tb Exp $ */
/*
 * Copyright (c) 2020 Ingo Schwarze <schwarze@openbsd.org>
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

#include <openssl/err.h>
#include <openssl/x509.h>

void	 fail_head(const char *);
void	 fail_tail(void);
void	 fail_str(const char *, const char *);
void	 fail_int(const char *, int);

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
	unsigned long	 errnum;

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

int
main(void)
{
	X509_ATTRIBUTE	*attrib;
	ASN1_TYPE	*any;
	ASN1_OBJECT	*coid;
	int		 num;

	testname = "preparation";
	if ((coid = OBJ_nid2obj(NID_pkcs7_data)) == NULL) {
		fail_str("OBJ_nid2obj", "NULL");
		return 1;
	}

	testname = "valid_args";
	if ((attrib = X509_ATTRIBUTE_create(NID_pkcs9_contentType,
	    V_ASN1_OBJECT, coid)) == NULL)
		fail_str("X509_ATTRIBUTE_create", "NULL");
	else if (X509_ATTRIBUTE_get0_object(attrib) == NULL)
		fail_str("X509_ATTRIBUTE_get0_object", "NULL");
	else if ((num = X509_ATTRIBUTE_count(attrib)) != 1)
		fail_int("X509_ATTRIBUTE_count", num);
	else if ((any = X509_ATTRIBUTE_get0_type(attrib, 0)) == NULL)
		fail_str("X509_ATTRIBUTE_get0_type", "NULL");
	else if (any->type != V_ASN1_OBJECT)
		fail_int("any->type", any->type);
	else if (any->value.object != coid)
		fail_str("value", "wrong pointer");
	X509_ATTRIBUTE_free(attrib);

	testname = "bad_nid";
	if ((attrib = X509_ATTRIBUTE_create(-1,
	    V_ASN1_OBJECT, coid)) != NULL)
		fail_str("X509_ATTRIBUTE_create", "not NULL");
	X509_ATTRIBUTE_free(attrib);

	return errcount != 0;
}
