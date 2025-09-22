/* $OpenBSD: ber_test_int_i.c,v 1.6 2019/10/24 12:39:26 tb Exp $
*/
/*
 * Copyright (c) Rob Pierce <rob@openbsd.org>
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

#include <sys/types.h>

#include <ber.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SUCCEED	0
#define FAIL	1

struct test_vector {
	int		 fail;		/* 1 means test is expected to fail */
	char		 title[128];
	size_t		 input_length;
	long long	 value;
	unsigned char	 input[1024];
	size_t		 expect_length;
	unsigned char	 expect[1024];
};

struct test_vector test_vectors[] = {
	{
		SUCCEED,
		"integer (-9223372036854775807)",
		10,
		-9223372036854775807,
		{
			0x02, 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x01
		},
		10,
		{
			0x02, 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x01
		},
	},
	{
		SUCCEED,
		"integer (-9223372036854775806)",
		10,
		-9223372036854775806,
		{
			0x02, 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x02
		},
		10,
		{
			0x02, 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x02
		},
	},
	{
		FAIL,
		"integer (-2147483650) (expected failure)",
		10,
		-2147483650,
		{
			0x02, 0x08, 0xff, 0xff, 0xff, 0xff, 0x7f, 0xff,
			0xff, 0xfe
		},
		7,
		{
			0x02, 0x05, 0xff, 0x7f, 0xff, 0xff, 0xfe
		},
	},
	{
		SUCCEED,
		"integer (-2147483650)",
		7,
		-2147483650,
		{
			0x02, 0x05, 0xff, 0x7f, 0xff, 0xff, 0xfe
		},
		7,
		{
			0x02, 0x05, 0xff, 0x7f, 0xff, 0xff, 0xfe
		},
	},
	{
		SUCCEED,
		"integer (-2147483649)",
		7,
		-2147483649,
		{
			0x02, 0x05, 0xff, 0x7f, 0xff, 0xff, 0xff
		},
		7,
		{
			0x02, 0x05, 0xff, 0x7f, 0xff, 0xff, 0xff
		},
	},
	{
		FAIL,
		"integer (-2147483648) (expected failure)",
		8,
		-2147483648,
		{
			0x02, 0x06, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00
		},
		6,
		{
			0x02, 0x04, 0x80, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-2147483648)",
		6,
		-2147483648,
		{
			0x02, 0x04, 0x80, 0x00, 0x00, 0x00
		},
		6,
		{
			0x02, 0x04, 0x80, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-1073741824)",
		6,
		-1073741824,
		{
			0x02, 0x04, 0xc0, 0x00, 0x00, 0x00
		},
		6,
		{
			0x02, 0x04, 0xc0, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-536870912)",
		6,
		-536870912,
		{
			0x02, 0x04, 0xe0, 0x00, 0x00, 0x00
		},
		6,
		{
			0x02, 0x04, 0xe0, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-268435456)",
		6,
		-268435456,
		{
			0x02, 0x04, 0xf0, 0x00, 0x00, 0x00
		},
		6,
		{
			0x02, 0x04, 0xf0, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-1342177728)",
		6,
		-1342177728,
		{
			0x02, 0x04, 0xaf, 0xff, 0xfe, 0x40
		},
		6,
		{
			0x02, 0x04, 0xaf, 0xff, 0xfe, 0x40
		},
	},
	{
		SUCCEED,
		"integer (-67108865)",
		6,
		-67108865,
		{
			0x02, 0x04, 0xfb, 0xff, 0xff, 0xff
		},
		6,
		{
			0x02, 0x04, 0xfb, 0xff, 0xff, 0xff
		},
	},
	{
		SUCCEED,
		"integer (-67108864)",
		6,
		-67108864,
		{
			0x02, 0x04, 0xfc, 0x00, 0x00, 0x00
		},
		6,
		{
			0x02, 0x04, 0xfc, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-67108863)",
		6,
		-67108863,
		{
			0x02, 0x04, 0xfc, 0x00, 0x00, 0x01
		},
		6,
		{
			0x02, 0x04, 0xfc, 0x00, 0x00, 0x01
		},
	},
	{
		SUCCEED,
		"integer (-3554432)",
		5,
		-3554432,
		{
			0x02, 0x03, 0xc9, 0xc3, 0x80
		},
		5,
		{
			0x02, 0x03, 0xc9, 0xc3, 0x80
		},
	},
	{
		SUCCEED,
		"integer (-65535)",
		5,
		-65535,
		{
			0x02, 0x03, 0xff, 0x00, 0x01,
		},
		5,
		{
			0x02, 0x03, 0xff, 0x00, 0x01,
		},
	},
	{
		SUCCEED,
		"integer (-32769)",
		5,
		-32769,
		{
			0x02, 0x03, 0xff, 0x7f, 0xff
		},
		5,
		{
			0x02, 0x03, 0xff, 0x7f, 0xff
		},
	},
	{
		SUCCEED,
		"integer (-32768)",
		4,
		-32768,
		{
			0x02, 0x02, 0x80, 0x00
		},
		4,
		{
			0x02, 0x02, 0x80, 0x00
		},
	},
	{
		SUCCEED,
		"integer (-128)",
		3,
		-128,
		{
			0x02, 0x01, 0x80
		},
		3,
		{
			0x02, 0x01, 0x80
		},
	},
	{
		SUCCEED,
		"integer (-1)",
		3,
		-1,
		{
			0x02, 0x01, 0xff
		},
		3,
		{
			0x02, 0x01, 0xff
		},
	},
	{
		SUCCEED,
		"integer (0)",
		3,
		0,
		{
			0x02, 0x01, 0x00
		},
		3,
		{
			0x02, 0x01, 0x00
		},
	},
	{
		SUCCEED,
		"integer (127)",
		3,
		127,
		{
			0x02, 0x01, 0x7f
		},
		3,
		{
			0x02, 0x01, 0x7f
		},
	},
	{
		SUCCEED,
		"integer (128)",
		4,
		128,
		{
			0x02, 0x02, 0x00, 0x80
		},
		4,
		{
			0x02, 0x02, 0x00, 0x80
		},
	},
	{
		SUCCEED,
		"integer (32767)",
		4,
		32767,
		{
			0x02, 0x02, 0x7f, 0xff
		},
		4,
		{
			0x02, 0x02, 0x7f, 0xff
		},
	},
	{
		SUCCEED,
		"integer (32768)",
		5,
		32768,
		{
			0x02, 0x03, 0x00, 0x80, 0x00
		},
		5,
		{
			0x02, 0x03, 0x00, 0x80, 0x00
		},
	},
	{
		SUCCEED,
		"integer (65535)",
		5,
		65535,
		{
			0x02, 0x03, 0x00, 0xff, 0xff
		},
		5,
		{
			0x02, 0x03, 0x00, 0xff, 0xff
		},
	},
	{
		SUCCEED,
		"integer (65536)",
		5,
		65536,
		{
			0x02, 0x03, 0x01, 0x00, 0x00
		},
		5,
		{
			0x02, 0x03, 0x01, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (2147483647)",
		6,
		2147483647,
		{
			0x02, 0x04, 0x7f, 0xff, 0xff, 0xff
		},
		6,
		{
			0x02, 0x04, 0x7f, 0xff, 0xff, 0xff
		},
	},
	{
		SUCCEED,
		"integer (2147483648)",
		7,
		2147483648,
		{
			0x02, 0x05, 0x00, 0x80, 0x00, 0x00, 0x00
		},
		7,
		{
			0x02, 0x05, 0x00, 0x80, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (2147483649)",
		7,
		2147483649,
		{
			0x02, 0x05, 0x00, 0x80, 0x00, 0x00, 0x01
		},
		7,
		{
			0x02, 0x05, 0x00, 0x80, 0x00, 0x00, 0x01
		},
	},
	{
		SUCCEED,
		"integer (4294967295)",
		7,
		4294967295,
		{
			0x02, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff
		},
		7,
		{
			0x02, 0x05, 0x00, 0xff, 0xff, 0xff, 0xff
		},
	},
	{
		SUCCEED,
		"integer (4294967296)",
		7,
		4294967296,
		{
			0x02, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00
		},
		7,
		{
			0x02, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"integer (4294967297)",
		7,
		4294967297,
		{
			0x02, 0x05, 0x01, 0x00, 0x00, 0x00, 0x01
		},
		7,
		{
			0x02, 0x05, 0x01, 0x00, 0x00, 0x00, 0x01
		},
	},
	{
		SUCCEED,
		"integer (9223372036854775806)",
		10,
		9223372036854775806,
		{
			0x02, 0x08, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xfe
		},
		10,
		{
			0x02, 0x08, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xfe
		},
	},
	{
		SUCCEED,
		"integer (9223372036854775807)",
		10,
		9223372036854775807,
		{
			0x02, 0x08, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff
		},
		10,
		{
			0x02, 0x08, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff
		},
	},
};

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t	i;

	for (i = 1; i < len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "": "\n");

	fprintf(stderr, " 0x%02hhx", buf[i - 1]);
	fprintf(stderr, "\n");
}

static int
test_read_elements(int i)
{
	int			 pos, b;
	char			*string;
	void			*p = NULL;
	ssize_t			 len = 0;
	struct ber_element	*elm = NULL, *ptr = NULL;
	struct ber		 ber;
	long long		 val;
	void			*bstring = NULL;
	struct ber_oid		 oid;
	struct ber_octetstring	 ostring;

	bzero(&ber, sizeof(ber));
	ober_set_readbuf(&ber, test_vectors[i].input,
	    test_vectors[i].input_length);

	elm = ober_read_elements(&ber, elm);
	if (elm != NULL && test_vectors[i].fail == FAIL) {
		printf("unexpectedly passed ober_read_elements\n");
		return 1;
	} else if (elm == NULL && test_vectors[i].fail != FAIL) {
		printf("unexpectedly failed ober_read_elements\n");
		return 1;
	} else if (elm == NULL && test_vectors[i].fail == FAIL)
		return 0;

	pos = ober_getpos(elm);
	if (pos != 2) {
		printf("unexpected element position within "
		    "byte stream (%d)\n", pos);
		return 1;
	}

	switch (elm->be_encoding) {
	case BER_TYPE_INTEGER:
		if (ober_get_integer(elm, &val) == -1) {
			printf("failed (int) encoding check\n");
			return 1;
		}
		if (val != test_vectors[i].value) {
			printf("(ober_get_integer) got %lld, expected %lld\n",
			    val, test_vectors[i].value);
			return 1;
		}
		if (ober_scanf_elements(elm, "i", &val) == -1) {
			printf("(ober_scanf_elements) failed (int)"
			    " ober_scanf_elements (i)\n");
			return 1;
		}
		if (val != test_vectors[i].value) {
			printf("got %lld, expected %lld\n", val,
			    test_vectors[i].value);
			return 1;
		}
		break;
	default:
		printf("failed with unexpected encoding (%ud)\n",
		    elm->be_encoding);
		return 1;
	}

	len = ober_calc_len(elm);
	if (len != test_vectors[i].expect_length) {
		printf("failed to calculate length\n");
		printf("was %zd want %zu\n", len,
		    test_vectors[i].expect_length);
		return 1;
	}

	ber.br_wbuf = NULL;
	len = ober_write_elements(&ber, elm);
	if (len != test_vectors[i].expect_length) {
		printf("failed length check (was %zd want "
		    "%zd)\n", len, test_vectors[i].expect_length);
		return 1;
	}

	if (memcmp(ber.br_wbuf, test_vectors[i].expect,
	    test_vectors[i].expect_length) != 0) {
		printf("failed byte stream compare\n");
		printf("Got:\n");
		hexdump(ber.br_wbuf, len);
		printf("Expected:\n");
		hexdump(test_vectors[i].expect, test_vectors[i].expect_length);
		return 1;
	}
	ober_free(&ber);

	ober_free_elements(elm);

	return 0;
}

static int
test_printf_elements(int i)
{
	int			 pos, b;
	char			*string;
	void			*p = NULL;
	ssize_t			 len = 0;
	struct ber_element	*elm = NULL, *ptr = NULL;
	struct ber		 ber;
	long long		 val;
	void			*bstring = NULL;
	struct ber_oid		 oid;
	struct ber_octetstring	 ostring;

	bzero(&ber, sizeof(ber));
	ober_set_readbuf(&ber, test_vectors[i].input,
	    test_vectors[i].input_length);

	elm = ober_printf_elements(elm, "i", test_vectors[i].value);
	if (elm == NULL) {
		printf("unexpectedly failed ober_printf_elements\n");
		return 1;
	}

	switch (elm->be_encoding) {
	case BER_TYPE_INTEGER:
		if (ober_get_integer(elm, &val) == -1) {
			printf("failed (int) encoding check\n");
			return 1;
		}
		if (val != test_vectors[i].value) {
			printf("(ober_get_integer) got %lld, expected %lld\n",
			    val, test_vectors[i].value);
			return 1;
		}
		if (ober_scanf_elements(elm, "i", &val) == -1) {
			printf("(ober_scanf_elements) failed (int)"
			    " ober_scanf_elements (i)\n");
			return 1;
		}
		if (val != test_vectors[i].value) {
			printf("got %lld, expected %lld\n", val,
			    test_vectors[i].value);
			return 1;
		}
		break;
	default:
		printf("failed with unexpected encoding (%ud)\n",
		    elm->be_encoding);
		return 1;
	}

	len = ober_calc_len(elm);
	if (len != test_vectors[i].expect_length) {
		printf("failed to calculate length\n");
		printf("was %zd want %zu\n", len,
		    test_vectors[i].expect_length);
		return 1;
	}

	ber.br_wbuf = NULL;
	len = ober_write_elements(&ber, elm);
	if (len != test_vectors[i].expect_length) {
		printf("failed length check (was %zd want "
		    "%zd)\n", len, test_vectors[i].expect_length);
		return 1;
	}

	if (memcmp(ber.br_wbuf, test_vectors[i].expect,
	    test_vectors[i].expect_length) != 0) {
		printf("failed byte stream compare\n");
		printf("Got:\n");
		hexdump(ber.br_wbuf, len);
		printf("Expected:\n");
		hexdump(test_vectors[i].expect, test_vectors[i].expect_length);
		return 1;
	}
	ober_free(&ber);

	ober_free_elements(elm);

	return 0;
}

int
main(void)
{
	extern char *__progname;

	ssize_t		len = 0;
	int		i, ret = 0;

	/*
	 * drive test vectors for ber byte stream input validation, etc.
	 */
	printf("= = = = = test_read_elements\n");
	for (i = 0; i < sizeof(test_vectors) / sizeof(test_vectors[0]); i++) {
		if (test_read_elements(i) != 0) {
			printf("FAILED: %s\n", test_vectors[i].title);
			ret = 1;
		} else
			printf("SUCCESS: %s\n", test_vectors[i].title);
	}

	printf("= = = = = test_printf_elements\n");
	for (i = 0; i < sizeof(test_vectors) / sizeof(test_vectors[0]); i++) {
		/*
		 * skip test cases known to fail under ober_read_elements as
		 * they are not applicable to ober_printf_elements
		 */
		if (test_vectors[i].fail)
			continue;
		if (test_printf_elements(i) != 0) {
			printf("FAILED: %s\n", test_vectors[i].title);
			ret = 1;
		} else
			printf("SUCCESS: %s\n", test_vectors[i].title);
	}

	if (ret != 0) {
		printf("FAILED: %s\n", __progname);
		return 1;
	}

	return 0;
}
