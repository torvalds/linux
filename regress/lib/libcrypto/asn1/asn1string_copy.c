/* $OpenBSD: asn1string_copy.c,v 1.1 2021/11/13 20:50:14 schwarze Exp $ */
/*
 * Copyright (c) 2021 Ingo Schwarze <schwarze@openbsd.org>
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

#include <err.h>
#include <string.h>

#include <openssl/asn1.h>

int
main(void)
{
	const unsigned char	*data = "hello world";
	const unsigned char	*str;
	ASN1_STRING		*src, *dst;
	int			 irc;

	/* Set up the source string. */

	if ((src = ASN1_IA5STRING_new()) == NULL)
		err(1, "FAIL: ASN1_IA5STRING_new() returned NULL");
	if (ASN1_STRING_set(src, data, -1) == 0)
		err(1, "FAIL: ASN1_STRING_set(src) failed");
	if ((str = ASN1_STRING_get0_data(src)) == NULL)
		errx(1, "FAIL: 1st ASN1_STRING_get0_data(src) returned NULL");
	if (strcmp(str, data))
		errx(1, "FAIL: 1st ASN1_STRING_get0_data(src) "
		    "returned wrong data: \"%s\" (expected \"%s\")",
		    str, data);
	if ((irc = ASN1_STRING_length(src)) != (int)strlen(data))
		errx(1, "FAIL: 1st ASN1_STRING_length(src) "
		    "returned a wrong length: %d (expected %zu)",
		    irc, strlen(data));
	if ((irc = ASN1_STRING_type(src)) != V_ASN1_IA5STRING)
		errx(1, "FAIL: 1st ASN1_STRING_type(src) "
		    "returned a wrong type: %d (expected %d)",
		    irc, V_ASN1_IA5STRING);

	/* Set up the destination string. */

	if ((dst = ASN1_STRING_new()) == NULL)
		err(1, "FAIL: ASN1_STRING_new() returned NULL");
	if ((str = ASN1_STRING_get0_data(dst)) != NULL)
		errx(1, "FAIL: 1st ASN1_STRING_get0_data(dst) "
		    "returned \"%s\" (expected NULL)", str);
	if ((irc = ASN1_STRING_length(dst)) != 0)
		errx(1, "FAIL: 1st ASN1_STRING_length(dst) "
		    "returned a wrong length: %d (expected 0)", irc);
	if ((irc = ASN1_STRING_type(dst)) != V_ASN1_OCTET_STRING)
		errx(1, "FAIL: 1st ASN1_STRING_type(dst) "
		    "returned a wrong type: %d (expected %d)",
		    irc, V_ASN1_OCTET_STRING);
	ASN1_STRING_length_set(dst, -1);
	if ((str = ASN1_STRING_get0_data(dst)) != NULL)
		errx(1, "FAIL: 2nd ASN1_STRING_get0_data(dst) "
		    "returned \"%s\" (expected NULL)", str);
	if ((irc = ASN1_STRING_length(dst)) != -1)
		errx(1, "FAIL: 2nd ASN1_STRING_length(dst) "
		    "returned a wrong length: %d (expected -1)", irc);
	if ((irc = ASN1_STRING_type(dst)) != V_ASN1_OCTET_STRING)
		errx(1, "FAIL: 2nd ASN1_STRING_type(dst) "
		    "returned a wrong type: %d (expected %d)",
		    irc, V_ASN1_OCTET_STRING);

	/* Attempt to copy in the wrong direction. */

	if (ASN1_STRING_copy(src, dst) != 0)
		errx(1, "FAIL: ASN1_STRING_copy unexpectedly succeeded");
	if ((str = ASN1_STRING_get0_data(src)) == NULL)
		errx(1, "FAIL: 2nd ASN1_STRING_get0_data(src) returned NULL");
	if (strcmp(str, data))
		errx(1, "FAIL: 2nd ASN1_STRING_get0_data(src) "
		    "returned wrong data: \"%s\" (expected \"%s\")",
		    str, data);
	if ((irc = ASN1_STRING_length(src)) != (int)strlen(data))
		errx(1, "FAIL: 2nd ASN1_STRING_length(src) "
		    "returned a wrong length: %d (expected %zu)",
		    irc, strlen(data));
	if ((irc = ASN1_STRING_type(src)) != V_ASN1_IA5STRING)
		errx(1, "FAIL: 2nd ASN1_STRING_type(src) "
		    "returned a wrong type: %d (expected %d)",
		    irc, V_ASN1_IA5STRING);

	/* Copy in the right direction. */

	if (ASN1_STRING_copy(dst, src) != 1)
		err(1, "FAIL: ASN1_STRING_copy unexpectedly failed");
	if ((str = ASN1_STRING_get0_data(dst)) == NULL)
		errx(1, "FAIL: 3rd ASN1_STRING_get0_data(dst) returned NULL");
	if (strcmp(str, data))
		errx(1, "FAIL: 3rd ASN1_STRING_get0_data(dst) "
		    "returned wrong data: \"%s\" (expected \"%s\")",
		    str, data);
	if ((irc = ASN1_STRING_length(dst)) != (int)strlen(data))
		errx(1, "FAIL: 3rd ASN1_STRING_length(dst) "
		    "returned a wrong length: %d (expected %zu)",
		    irc, strlen(data));
	if ((irc = ASN1_STRING_type(dst)) != V_ASN1_IA5STRING)
		errx(1, "FAIL: 3rd ASN1_STRING_type(dst) "
		    "returned a wrong type: %d (expected %d)",
		    irc, V_ASN1_IA5STRING);

	ASN1_STRING_free(src);
	ASN1_STRING_free(dst);
	return 0;
}
