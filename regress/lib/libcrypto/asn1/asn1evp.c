/* $OpenBSD: asn1evp.c,v 1.5 2022/09/05 21:06:31 tb Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>

#define TEST_NUM 0x7fffffffL

unsigned char asn1_atios[] = {
	0x30, 0x10, 0x02, 0x04, 0x7f, 0xff, 0xff, 0xff,
	0x04, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07,
};

unsigned char test_octetstring[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
compare_data(const char *label, const unsigned char *d1, size_t d1_len,
    const unsigned char *d2, size_t d2_len)
{
	if (d1_len != d2_len) {
		fprintf(stderr, "FAIL: got %s with length %zu, want %zu\n",
		    label, d1_len, d2_len);
		return -1;
	}
	if (memcmp(d1, d2, d1_len) != 0) {
		fprintf(stderr, "FAIL: %s differs\n", label);
		fprintf(stderr, "got:\n");
		hexdump(d1, d1_len);
		fprintf(stderr, "want:\n");
		hexdump(d2, d2_len);
		return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	unsigned char data[16];
	long num = TEST_NUM;
	ASN1_TYPE *at = NULL;
	int failed = 1;
	int len;

	if ((at = ASN1_TYPE_new()) == NULL) {
		fprintf(stderr, "FAIL: ASN1_TYPE_new returned NULL\n");
		goto done;
	}

	if (!ASN1_TYPE_set_int_octetstring(at, num, test_octetstring,
	    sizeof(test_octetstring))) {
		fprintf(stderr, "FAIL: ASN1_TYPE_set_int_octetstring failed\n");
		goto done;
	}
	if (at->type != V_ASN1_SEQUENCE) {
		fprintf(stderr, "FAIL: not a V_ASN1_SEQUENCE (%d != %d)\n",
		    at->type, V_ASN1_SEQUENCE);
		goto done;
	}
	if (at->value.sequence->type != V_ASN1_OCTET_STRING) {
		fprintf(stderr, "FAIL: not a V_ASN1_OCTET_STRING (%d != %d)\n",
		    at->type, V_ASN1_OCTET_STRING);
		goto done;
	}
	if (compare_data("sequence", at->value.sequence->data,
	    at->value.sequence->length, asn1_atios, sizeof(asn1_atios)) == -1)
		goto done;

	memset(&data, 0, sizeof(data));
	num = 0;

	if ((len = ASN1_TYPE_get_int_octetstring(at, &num, data,
	    sizeof(data))) < 0) {
		fprintf(stderr, "FAIL: ASN1_TYPE_get_int_octetstring failed\n");
		goto done;
	}
	if (num != TEST_NUM) {
		fprintf(stderr, "FAIL: got num %ld, want %ld\n", num, TEST_NUM);
		goto done;
	}
	if (compare_data("octet string", data, len,
	    test_octetstring, sizeof(test_octetstring)) == -1)
		goto done;
	if (data[len] != 0) {
		fprintf(stderr, "FAIL: octet string overflowed buffer\n");
		goto done;
	}

	memset(&data, 0, sizeof(data));
	num = 0;

	/* With a limit buffer, the output should be truncated... */
	if ((len = ASN1_TYPE_get_int_octetstring(at, &num, data, 4)) < 0) {
		fprintf(stderr, "FAIL: ASN1_TYPE_get_int_octetstring failed\n");
		goto done;
	}
	if (num != TEST_NUM) {
		fprintf(stderr, "FAIL: got num %ld, want %ld\n", num, TEST_NUM);
		goto done;
	}
	if (len != sizeof(test_octetstring)) {
		fprintf(stderr, "FAIL: got length mismatch (%d != %zu)\n",
		    len, sizeof(test_octetstring));
		goto done;
	}
	if (compare_data("octet string", data, 4, test_octetstring, 4) == -1)
		goto done;
	if (data[4] != 0) {
		fprintf(stderr, "FAIL: octet string overflowed buffer\n");
		goto done;
	}

	failed = 0;

 done:
	ASN1_TYPE_free(at);

	return failed;
}
