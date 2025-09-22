/*	$OpenBSD: rfc3779.c,v 1.10 2023/12/13 07:19:37 tb Exp $ */
/*
 * Copyright (c) 2021 Theo Buehler <tb@openbsd.org>
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

#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>

#define RAW_ADDRESS_SIZE	16

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stderr, "\n");
}

static void
report_hexdump(const char *func, const char *description, const char *msg,
    const unsigned char *want, size_t want_len,
    const unsigned char *got, size_t got_len)
{
	fprintf(stderr, "%s: \"%s\" %s\nwant:\n", func, description, msg);
	hexdump(want, want_len);
	fprintf(stderr, "got:\n");
	hexdump(got, got_len);
}

static int
afi_size(int afi)
{
	switch (afi) {
	case IANA_AFI_IPV4:
		return 4;
	case IANA_AFI_IPV6:
		return 16;
	}
	return 0;
}

struct IPAddressOrRange_test {
	const char	*description;
	const uint8_t	 der[32];
	size_t		 der_len;
	unsigned	 afi;
	const uint8_t	 min[RAW_ADDRESS_SIZE];
	const uint8_t	 max[RAW_ADDRESS_SIZE];
};

const struct IPAddressOrRange_test IPAddressOrRange_test_data[] = {
	/* Examples from RFC 3779, section 2.1.1 */
	{
		.description = "address 10.5.0.4",
		.der = {
			0x03, 0x05, 0x00, 0x0a, 0x05, 0x00, 0x04,
		},
		.der_len = 7,
		.afi = IANA_AFI_IPV4,
		.min = {
			0x0a, 0x05, 0x00, 0x04,
		},
		.max = {
			0x0a, 0x05, 0x00, 0x04,
		}
	},
	{
		.description = "prefix 10.5.0/23",
		.der = {
			0x03, 0x04, 0x01, 0x0a, 0x05, 0x00,
		},
		.der_len = 6,
		.afi = IANA_AFI_IPV4,
		.min = {
			0x0a, 0x05, 0x00, 0x00,
		},
		.max = {
			0x0a, 0x05, 0x01, 0xff,
		}
	},
	{
		.description = "address 2001:0:200:3::1",
		.der = {
			0x03, 0x11, 0x00, 0x20, 0x01, 0x00, 0x00, 0x02,
			0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x01,
		},
		.der_len = 19,
		.afi = IANA_AFI_IPV6,
		.min = {
			0x20, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		},
		.max = {
			0x20, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		},
	},
	{
		.description = "prefix 2001:0:200/39",
		.der = {
			0x03, 0x06, 0x01, 0x20, 0x01, 0x00, 0x00, 0x02,
		},
		.der_len = 8,
		.afi = IANA_AFI_IPV6,
		.min = {
			0x20, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		},
		.max = {
			0x20, 0x01, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
	},

	/* Examples from RFC 3779, Section 2.1.2 */
	{
		.description = "prefix 10.5.0/23 as a range",
		.der = {
			/* Sequence */
			0x30, 0x0b,
			/* 10.5.0.0 */
			0x03, 0x03, 0x00, 0x0a, 0x05,
			/* 10.5.1.255 */
			0x03, 0x04, 0x01, 0x0a, 0x05, 0x00,
		},
		.der_len = 13,
		.afi = IANA_AFI_IPV4,
		.min = {
			0x0a, 0x05, 0x00, 0x00,
		},
		.max = {
			0x0a, 0x05, 0x01, 0xff,
		}
	},
	{
		.description = "prefix 2001:0:200/39 as a range",
		.der = {
			/* Sequence */
			0x30, 0x10,
			/* 2001:0:200:: */
			0x03, 0x06, 0x01, 0x20, 0x01, 0x00, 0x00, 0x02,
			/* 2001:0:3ff:ffff:ffff:ffff:ffff:ffff */
			0x03, 0x06, 0x02, 0x20, 0x01, 0x00, 0x00, 0x00,
		},
		.der_len = 18,
		.afi = IANA_AFI_IPV6,
		.min = {
			0x20, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		},
		.max = {
			0x20, 0x01, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		}
	},
	{
		.description = "prefix 0/0",
		.der = {
			0x03, 0x01, 0x00,
		},
		.der_len = 3,
		.afi = IANA_AFI_IPV4,
		.min = {
			0x00, 0x00, 0x00, 0x00,
		},
		.max = {
			0xff, 0xff, 0xff, 0xff,
		}
	},
	{
		.description = "prefix 10.64/12",
		.der = {
			0x03, 0x03, 0x04, 0x0a, 0x40,
		},
		.der_len = 5,
		.afi = IANA_AFI_IPV4,
		.min = {
			0x0a, 0x40, 0x00, 0x00,
		},
		.max = {
			0x0a, 0x4f, 0xff, 0xff,
		},
	},
	{
		.description = "prefix 10.64/20",
		.der = {
			0x03, 0x04, 0x04, 0x0a, 0x40, 0x00,
		},
		.der_len = 6,
		.afi = IANA_AFI_IPV4,
		.min = {
			0x0a, 0x40, 0x00, 0x00,
		},
		.max = {
			0x0a, 0x40, 0x0f, 0xff,
		},
	},
};

const size_t N_IPADDRESSORRANGE_TESTS =
    sizeof(IPAddressOrRange_test_data) / sizeof(IPAddressOrRange_test_data[0]);

static int
test_IPAddressOrRange(const struct IPAddressOrRange_test *test)
{
	IPAddressOrRange *aor;
	const unsigned char *p;
	unsigned char min[RAW_ADDRESS_SIZE] = {0}, max[RAW_ADDRESS_SIZE] = {0};
	unsigned char *out = NULL;
	int out_len;
	int afi_len;
	int memcmp_failed = 0;
	int failed = 1;

	/*
	 * First, decode DER from the test case.
	 */

	p = &test->der[0];
	if ((aor = d2i_IPAddressOrRange(NULL, &p, test->der_len)) == NULL) {
		fprintf(stderr, "%s: \"%s\" d2i_IPAddressOrRange failed\n",
		    __func__, test->description);
		goto err;
	}

	/*
	 * Now extract minimum and maximum from the parsed range.
	 */

	afi_len = afi_size(test->afi);

	if (X509v3_addr_get_range(aor, test->afi, min, max, sizeof min) !=
	    afi_len) {
		fprintf(stderr, "%s: \"%s\" X509v3_addr_get_range failed\n",
		    __func__, test->description);
		goto err;
	}

	/*
	 * Check that min and max match expectations.
	 */

	if (memcmp(min, test->min, afi_len) != 0) {
		memcmp_failed |= 1;
		report_hexdump(__func__, test->description, "memcmp min failed",
		    test->min, afi_len, min, afi_len);
	}
	if (memcmp(max, test->max, afi_len) != 0) {
		memcmp_failed |= 1;
		report_hexdump(__func__, test->description, "memcmp max failed",
		    test->max, afi_len, max, afi_len);
	}
	if (memcmp_failed)
		goto err;

	/*
	 * Now turn the parsed IPAddressOrRange back into DER and check that
	 * it matches the DER in the test case.
	 */

	out = NULL;
	if ((out_len = i2d_IPAddressOrRange(aor, &out)) <= 0) {
		fprintf(stderr, "%s: \"%s\" i2d_IPAddressOrRange failed\n",
		    __func__, test->description);
		goto err;
	}

	memcmp_failed = (size_t)out_len != test->der_len;
	if (!memcmp_failed)
		memcmp_failed = memcmp(test->der, out, out_len);

	if (memcmp_failed) {
		report_hexdump(__func__, test->description, "memcmp DER failed",
		    test->der, test->der_len, out, out_len);
		goto err;
	}

	failed = 0;
 err:
	IPAddressOrRange_free(aor);
	free(out);

	return failed;
}

static int
run_IPAddressOrRange_tests(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_IPADDRESSORRANGE_TESTS; i++)
		failed |=
		    test_IPAddressOrRange(&IPAddressOrRange_test_data[i]);

	return failed;
}

/*
 * XXX: These should really be part of the public API...
 */
static IPAddrBlocks *IPAddrBlocks_new(void);
static void IPAddrBlocks_free(IPAddrBlocks *addr);
static IPAddrBlocks *d2i_IPAddrBlocks(IPAddrBlocks **addrs,
    const unsigned char **in, long len);
static int i2d_IPAddrBlocks(IPAddrBlocks *addrs, unsigned char **out);

static IPAddrBlocks *
IPAddrBlocks_new(void)
{
	IPAddrBlocks *addrs;

	/*
	 * XXX The comparison function IPAddressFamily_cmp() isn't public.
	 * Start with the default and exploit a side effect of the lovely API
	 * which helpfully sets the correct function in a few places. Let's
	 * use the cheapest and easiest to reach one.
	 */
	if ((addrs = sk_IPAddressFamily_new_null()) == NULL)
		return NULL;
	if (!X509v3_addr_canonize(addrs)) {
		IPAddrBlocks_free(addrs);
		return NULL;
	}

	return addrs;
}

static void
IPAddrBlocks_free(IPAddrBlocks *addr)
{
	sk_IPAddressFamily_pop_free(addr, IPAddressFamily_free);
}

/*
 * We want {d2i,i2d}_IPAddrBlocks() to play with the DER of the extension.
 * These don't exist, so we have to implement them ourselves.  IPAddrBlocks_it
 * isn't public, so we need to fetch it from the library.  We cache it in a
 * static variable to avoid the cost of a binary search through all supported
 * extensions on each call.
 */

static ASN1_ITEM_EXP *
get_IPAddrBlocks_it(void)
{
	static ASN1_ITEM_EXP *my_IPAddrBlocks_it;
	const X509V3_EXT_METHOD *v3_addr;

	if (my_IPAddrBlocks_it != NULL)
		return my_IPAddrBlocks_it;

	if ((v3_addr = X509V3_EXT_get_nid(NID_sbgp_ipAddrBlock)) == NULL) {
		fprintf(stderr, "could not get v3_addr\n");
		return NULL;
	}

	my_IPAddrBlocks_it = v3_addr->it;

	return my_IPAddrBlocks_it;
}

static IPAddrBlocks *
d2i_IPAddrBlocks(IPAddrBlocks **addrs, const unsigned char **in, long len)
{
	ASN1_ITEM_EXP *my_IPAddrBlocks_it;

	if ((my_IPAddrBlocks_it = get_IPAddrBlocks_it()) == NULL)
		return NULL;

	return (IPAddrBlocks *)ASN1_item_d2i((ASN1_VALUE **)addrs, in, len,
	    my_IPAddrBlocks_it);
}

static int
i2d_IPAddrBlocks(IPAddrBlocks *addrs, unsigned char **out)
{
	ASN1_ITEM_EXP *my_IPAddrBlocks_it;

	if ((my_IPAddrBlocks_it = get_IPAddrBlocks_it()) == NULL)
		return -1;

	return ASN1_item_i2d((ASN1_VALUE *)addrs, out, my_IPAddrBlocks_it);
}

struct ipv4_prefix {
	unsigned char			addr[4];
	size_t				addr_len;
	size_t				prefix_len;
};

struct ipv4_range {
	unsigned char			min[4];
	unsigned char			max[4];
};

union ipv4_choice {
	struct ipv4_prefix	prefix;
	struct ipv4_range	range;
};

struct ipv6_prefix {
	unsigned char		addr[16];
	size_t			addr_len;
	size_t			prefix_len;
};

struct ipv6_range {
	unsigned char		min[16];
	unsigned char		max[16];
};

union ipv6_choice {
	struct ipv6_prefix	prefix;
	struct ipv6_range	range;
};

enum choice_type {
	choice_prefix,
	choice_range,
	choice_inherit,
	choice_last,
};

union ip {
	union ipv4_choice	ipv4;
	union ipv6_choice	ipv6;
};

enum safi {
	safi_none,
	safi_unicast,
	safi_multicast,
};

struct ip_addr_block {
	unsigned int		afi;
	enum safi		safi;
	enum choice_type	type;
	union ip		addr;
};

struct build_addr_block_test_data {
	char			*description;
	struct ip_addr_block	 addrs[16];
	char			 der[128];
	size_t			 der_len;
	int			 is_canonical;
	int			 inherits;
	unsigned int		 afis[4];
	int			 afi_len;
};

const struct build_addr_block_test_data build_addr_block_tests[] = {
	{
		.description = "RFC 3779, Appendix B, example 1",
		.addrs = {
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 0, 32,
					},
					.addr_len = 3,
					.prefix_len = 20,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 0, 64,
					},
					.addr_len = 3,
					.prefix_len = 24,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 1,
					},
					.addr_len = 2,
					.prefix_len = 16,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 2, 48,
					},
					.addr_len = 3,
					.prefix_len = 20,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 2, 64,
					},
					.addr_len = 3,
					.prefix_len = 24,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 3,
					},
					.addr_len = 2,
					.prefix_len = 16,
				},
			},
			{
				.afi = IANA_AFI_IPV6,
				.safi = safi_none,
				.type = choice_inherit,
			},
			{
				.type = choice_last,
			},
		},
		.der = {
			0x30, 0x35, 0x30, 0x2b, 0x04, 0x03, 0x00, 0x01,
			0x01, 0x30, 0x24, 0x03, 0x04, 0x04, 0x0a, 0x00,
			0x20, 0x03, 0x04, 0x00, 0x0a, 0x00, 0x40, 0x03,
			0x03, 0x00, 0x0a, 0x01, 0x30, 0x0c, 0x03, 0x04,
			0x04, 0x0a, 0x02, 0x30, 0x03, 0x04, 0x00, 0x0a,
			0x02, 0x40, 0x03, 0x03, 0x00, 0x0a, 0x03, 0x30,
			0x06, 0x04, 0x02, 0x00, 0x02, 0x05, 0x00,
		},
		.der_len = 55,
		.is_canonical = 0,
		.inherits = 1,
		.afis = {
			IANA_AFI_IPV4, IANA_AFI_IPV6,
		},
		.afi_len = 2,
	},
	{
		.description = "RFC 3779, Appendix B, example 1 canonical",
		.addrs = {
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 0, 32,
					},
					.addr_len = 3,
					.prefix_len = 20,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 0, 64,
					},
					.addr_len = 3,
					.prefix_len = 24,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 1,
					},
					.addr_len = 2,
					.prefix_len = 16,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_range,
				.addr.ipv4.range = {
					.min = {
						10, 2, 48, 00,
					},
					.max = {
						10, 2, 64, 255,
					},
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10, 3,
					},
					.addr_len = 2,
					.prefix_len = 16,
				},
			},
			{
				.afi = IANA_AFI_IPV6,
				.safi = safi_none,
				.type = choice_inherit,
			},
			{
				.type = choice_last,
			},
		},
		.der = {
			0x30, 0x35, 0x30, 0x2b, 0x04, 0x03, 0x00, 0x01,
			0x01, 0x30, 0x24, 0x03, 0x04, 0x04, 0x0a, 0x00,
			0x20, 0x03, 0x04, 0x00, 0x0a, 0x00, 0x40, 0x03,
			0x03, 0x00, 0x0a, 0x01, 0x30, 0x0c, 0x03, 0x04,
			0x04, 0x0a, 0x02, 0x30, 0x03, 0x04, 0x00, 0x0a,
			0x02, 0x40, 0x03, 0x03, 0x00, 0x0a, 0x03, 0x30,
			0x06, 0x04, 0x02, 0x00, 0x02, 0x05, 0x00,
		},
		.der_len = 55,
		.is_canonical = 1,
		.inherits = 1,
		.afis = {
			IANA_AFI_IPV4, IANA_AFI_IPV6,
		},
		.afi_len = 2,
	},
	{
		.description = "RFC 3779, Appendix B, example 2",
		.addrs = {
			{
				.afi = IANA_AFI_IPV6,
				.safi = safi_none,
				.type = choice_prefix,
				.addr.ipv6.prefix = {
					.addr = {
						0x20, 0x01, 0x00, 0x00,
						0x00, 0x02,
					},
					.addr_len = 6,
					.prefix_len = 48,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						10,
					},
					.addr_len = 1,
					.prefix_len = 8,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_unicast,
				.type = choice_prefix,
				.addr.ipv4.prefix = {
					.addr = {
						172, 16,
					},
					.addr_len = 2,
					.prefix_len = 12,
				},
			},
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_multicast,
				.type = choice_inherit,
			},
			{
				.type = choice_last,
			},
		},
		.der = {
			0x30, 0x2c, 0x30, 0x10, 0x04, 0x03, 0x00, 0x01,
			0x01, 0x30, 0x09, 0x03, 0x02, 0x00, 0x0a, 0x03,
			0x03, 0x04, 0xac, 0x10, 0x30, 0x07, 0x04, 0x03,
			0x00, 0x01, 0x02, 0x05, 0x00, 0x30, 0x0f, 0x04,
			0x02, 0x00, 0x02, 0x30, 0x09, 0x03, 0x07, 0x00,
			0x20, 0x01, 0x00, 0x00, 0x00, 0x02,
		},
		.der_len = 46,
		.is_canonical = 0,
		.inherits = 1,
		.afis = {
			IANA_AFI_IPV4, IANA_AFI_IPV4,
		},
		.afi_len = 2,
	},
	{
		.description = "Range should be prefix 127/8",
		.addrs = {
			{
				.afi = IANA_AFI_IPV4,
				.safi = safi_none,
				.type = choice_range,
				.addr.ipv4.range = {
					.min = {
						127, 0, 0, 0,
					},
					.max = {
						127, 255, 255, 255,
					},
				},
			},
			{
				.type = choice_last,
			},
		},
		.der = {
			0x30, 0x0c, 0x30, 0x0a, 0x04, 0x02, 0x00, 0x01,
			0x30, 0x04, 0x03, 0x02, 0x00, 0x7f,
		},
		.der_len = 14,
		.is_canonical = 1,
		.inherits = 0,
		.afis = {
			IANA_AFI_IPV4,
		},
		.afi_len = 1,
	},
};

const size_t N_BUILD_ADDR_BLOCK_TESTS =
    sizeof(build_addr_block_tests) / sizeof(build_addr_block_tests[0]);

static unsigned int *
addr_block_get_safi(const struct ip_addr_block *addr)
{
	static unsigned int safi;

	switch (addr->safi) {
	case safi_none:
		return NULL;
	case safi_unicast:
		safi = 1;
		break;
	case safi_multicast:
		safi = 2;
		break;
	}

	return &safi;
}

static int
addr_block_add_ipv4_addr(IPAddrBlocks *block, enum choice_type type,
    const union ipv4_choice *ipv4, unsigned int *safi)
{
	unsigned char addr[RAW_ADDRESS_SIZE] = {0};
	unsigned char min[RAW_ADDRESS_SIZE];
	unsigned char max[RAW_ADDRESS_SIZE];

	switch (type) {
	case choice_prefix:
		memcpy(addr, ipv4->prefix.addr, ipv4->prefix.addr_len);
		return X509v3_addr_add_prefix(block, IANA_AFI_IPV4, safi,
		    addr, ipv4->prefix.prefix_len);
	case choice_range:
		memcpy(min, ipv4->range.min, sizeof(ipv4->range.min));
		memcpy(max, ipv4->range.max, sizeof(ipv4->range.max));
		return X509v3_addr_add_range(block, IANA_AFI_IPV4, safi,
		    min, max);
	case choice_inherit:
		return X509v3_addr_add_inherit(block, IANA_AFI_IPV4, safi);
	case choice_last:
	default:
		return 0;
	}
}

static int
addr_block_add_ipv6_addr(IPAddrBlocks *block, enum choice_type type,
    const union ipv6_choice *ipv6, unsigned int *safi)
{
	unsigned char addr[RAW_ADDRESS_SIZE] = {0};
	unsigned char min[RAW_ADDRESS_SIZE];
	unsigned char max[RAW_ADDRESS_SIZE];

	switch (type) {
	case choice_prefix:
		memcpy(addr, ipv6->prefix.addr, ipv6->prefix.addr_len);
		return X509v3_addr_add_prefix(block, IANA_AFI_IPV6, safi,
		    addr, ipv6->prefix.prefix_len);
	case choice_range:
		memcpy(min, ipv6->range.min, sizeof(ipv6->range.min));
		memcpy(max, ipv6->range.max, sizeof(ipv6->range.max));
		return X509v3_addr_add_range(block, IANA_AFI_IPV6, safi,
		    min, max);
	case choice_inherit:
		return X509v3_addr_add_inherit(block, IANA_AFI_IPV6, safi);
	case choice_last:
	default:
		return 0;
	}
}

static int
addr_block_add_addrs(IPAddrBlocks *block, const struct ip_addr_block addrs[])
{
	const struct ip_addr_block	*addr;
	unsigned int			*safi;

	for (addr = &addrs[0]; addr->type != choice_last; addr++) {
		safi = addr_block_get_safi(addr);
		switch (addr->afi) {
		case IANA_AFI_IPV4:
			if (!addr_block_add_ipv4_addr(block, addr->type,
			    &addr->addr.ipv4, safi))
				return 0;
			break;
		case IANA_AFI_IPV6:
			if (!addr_block_add_ipv6_addr(block, addr->type,
			    &addr->addr.ipv6, safi))
				return 0;
			break;
		default:
			fprintf(stderr, "%s: corrupt test data", __func__);
			exit(1);
		}
	}

	return 1;
}

static int
build_addr_block_test(const struct build_addr_block_test_data *test)
{
	IPAddrBlocks *addrs = NULL, *parsed = NULL;
	const unsigned char *p;
	unsigned char *out = NULL;
	int out_len;
	int i;
	int memcmp_failed = 1;
	int failed = 1;

	if ((addrs = IPAddrBlocks_new()) == NULL)
		goto err;

	if (!addr_block_add_addrs(addrs, test->addrs))
		goto err;

	if (X509v3_addr_is_canonical(addrs) != test->is_canonical) {
		fprintf(stderr, "%s: \"%s\" X509v3_addr_is_canonical not %d\n",
		    __func__, test->description, test->is_canonical);
		goto err;
	}

	if (!X509v3_addr_canonize(addrs)) {
		fprintf(stderr, "%s: \"%s\" failed to canonize\n",
		    __func__, test->description);
		goto err;
	}

	if (!X509v3_addr_is_canonical(addrs)) {
		fprintf(stderr, "%s: \"%s\" canonization wasn't canonical\n",
		    __func__, test->description);
		goto err;
	}

	if ((out_len = i2d_IPAddrBlocks(addrs, &out)) <= 0) {
		fprintf(stderr, "%s: \"%s\" i2d_IPAddrBlocks failed\n",
		    __func__, test->description);
		goto err;
	}

	memcmp_failed = (size_t)out_len != test->der_len;
	if (!memcmp_failed)
		memcmp_failed = memcmp(out, test->der, test->der_len);
	if (memcmp_failed) {
		report_hexdump(__func__, test->description, "memcmp DER failed",
		    test->der, test->der_len, out, out_len);
		goto err;
	}

	if (X509v3_addr_inherits(addrs) != test->inherits) {
		fprintf(stderr, "%s: \"%s\" X509v3_addr_inherits not %d\n",
		    __func__, test->description, test->inherits);
		goto err;
	}

	for (i = 0; i < sk_IPAddressFamily_num(addrs) && i < test->afi_len; i++) {
		IPAddressFamily *family;
		unsigned int afi;

		family = sk_IPAddressFamily_value(addrs, i);

		if ((afi = X509v3_addr_get_afi(family)) == 0) {
			fprintf(stderr, "%s: \"%s\" X509v3_addr_get_afi"
			    " failed\n", __func__, test->description);
			goto err;
		}
		if (test->afis[i] != afi){
			fprintf(stderr, "%s: \"%s\" afi[%d] mismatch. "
			    "want: %u, got: %u\n", __func__,
			    test->description, i, test->afis[i], afi);
			goto err;
		}
	}
	if (i != test->afi_len) {
		fprintf(stderr, "%s: \"%s\" checked %d afis, expected %d\n",
		    __func__, test->description, i, test->afi_len);
		goto err;
	}

	p = test->der;
	if ((parsed = d2i_IPAddrBlocks(NULL, &p, test->der_len)) == NULL) {
		fprintf(stderr, "%s: \"%s\" d2i_IPAddrBlocks failed\n",
		    __func__, test->description);
		goto err;
	}
	if (!X509v3_addr_is_canonical(parsed)) {
		fprintf(stderr, "%s: \"%s\" parsed AddrBlocks isn't canonical\n",
		    __func__, test->description);
		goto err;
	}
	/* Can't compare IPAddrBlocks with inheritance. */
	if (!X509v3_addr_inherits(addrs) && !X509v3_addr_inherits(parsed)) {
		if (!X509v3_addr_subset(addrs, parsed)) {
			fprintf(stderr, "%s: \"%s\" addrs not subset of parsed\n",
			    __func__, test->description);
		}
		if (!X509v3_addr_subset(parsed, addrs)) {
			fprintf(stderr, "%s: \"%s\" parsed not subset of addrs\n",
			    __func__, test->description);
		}
	}

	failed = 0;

 err:
	IPAddrBlocks_free(addrs);
	IPAddrBlocks_free(parsed);
	free(out);

	return failed;
}

static int
run_IPAddrBlock_tests(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_BUILD_ADDR_BLOCK_TESTS; i++)
		failed |= build_addr_block_test(&build_addr_block_tests[i]);

	return failed;
}

struct asid_or_range {
	int			 type;
	int			 inherit;
	const unsigned char	*min;
	const unsigned char	*max;
};

struct ASIdentifiers_build_test {
	const char		*description;
	int			 should_build;
	int			 inherits;
	int			 canonical;
	int			 should_canonize;
	struct asid_or_range	 delegations[8];
	const unsigned char	 der[128];
	size_t			 der_len;
};

/* Sentinel value used for marking the end of the delegations table. */
#define V3_ASID_END -1

const struct ASIdentifiers_build_test ASIdentifiers_build_data[] = {
	{
		.description = "RFC 3779, Appendix C",
		.should_build = 1,
		.inherits = 1,
		.canonical = 1,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "135",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3000",
				.max = "3999",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "5001",
				.max = NULL,
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
				.min = NULL,
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.der = {
			0x30, 0x1a, 0xa0, 0x14, 0x30, 0x12, 0x02, 0x02,
			0x00, 0x87, 0x30, 0x08, 0x02, 0x02, 0x0b, 0xb8,
			0x02, 0x02, 0x0f, 0x9f, 0x02, 0x02, 0x13, 0x89,
			0xa1, 0x02, 0x05, 0x00,
		},
		.der_len = 28,
	},
	{
		.description = "RFC 3779, Appendix C without rdi",
		.should_build = 1,
		.inherits = 0,
		.canonical = 1,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "135",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3000",
				.max = "3999",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "5001",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.der = {
			0x30, 0x16, 0xa0, 0x14, 0x30, 0x12, 0x02, 0x02,
			0x00, 0x87, 0x30, 0x08, 0x02, 0x02, 0x0b, 0xb8,
			0x02, 0x02, 0x0f, 0x9f, 0x02, 0x02, 0x13, 0x89,
		},
		.der_len = 24,
	},
	{
		.description = "RFC 3779, Appendix C variant",
		.should_build = 1,
		.inherits = 0,
		.canonical = 1,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "135",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3000",
				.max = "3999",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "5001",
				.max = NULL,
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "135",
				.max = NULL,
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "3000",
				.max = "3999",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "5001",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.der = {
			0x30, 0x2c, 0xa0, 0x14, 0x30, 0x12, 0x02, 0x02,
			0x00, 0x87, 0x30, 0x08, 0x02, 0x02, 0x0b, 0xb8,
			0x02, 0x02, 0x0f, 0x9f, 0x02, 0x02, 0x13, 0x89,
			0xa1, 0x14, 0x30, 0x12, 0x02, 0x02, 0x00, 0x87,
			0x30, 0x08, 0x02, 0x02, 0x0b, 0xb8, 0x02, 0x02,
			0x0f, 0x9f, 0x02, 0x02, 0x13, 0x89,
		},
		.der_len = 46,
	},
	{
		.description = "inherit only",
		.should_build = 1,
		.inherits = 1,
		.canonical = 1,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 1,
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.der = {
			0x30, 0x08, 0xa0, 0x02, 0x05, 0x00, 0xa1, 0x02,
			0x05, 0x00,
		},
		.der_len = 10,
	},
	{
		.description = "adjacent unsorted ranges are merged",
		.should_build = 1,
		.inherits = 0,
		.canonical = 0,
		.should_canonize = 1,
		.delegations = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "27",
				.max = NULL,
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "28",
				.max = "57",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "66",
				.max = "68",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "58",
				.max = "63",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "64",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.der = {
			0x30, 0x14, 0xa1, 0x12, 0x30, 0x10, 0x30, 0x06,
			0x02, 0x01, 0x1b, 0x02, 0x01, 0x40, 0x30, 0x06,
			0x02, 0x01, 0x42, 0x02, 0x01, 0x44,
		},
		.der_len = 22,
	},
	{
		.description = "range of length 0",
		.should_build = 1,
		.inherits = 1,
		.canonical = 1,
		.should_canonize = 1,
		.delegations = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "27",
				.max = "27",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.der = {
			0x30, 0x10, 0xa0, 0x02, 0x05, 0x00, 0xa1, 0x0a,
			0x30, 0x08, 0x30, 0x06, 0x02, 0x01, 0x1b, 0x02,
			0x01, 0x1b,
		},
		.der_len = 18,
	},
	{
		.description = "reversed range doesn't canonize",
		.should_build = 1,
		.inherits = 0,
		.canonical = 0,
		.should_canonize = 0,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "57",
				.max = "42",
			},
			{
				.type = V3_ASID_END,
			},
		},
	},
	{
		.description = "overlapping ranges don't canonize",
		.should_build = 1,
		.inherits = 0,
		.canonical = 0,
		.should_canonize = 0,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "42",
				.max = "57",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "57",
				.max = "60",
			},
			{
				.type = V3_ASID_END,
			},
		},
	},
	{
		.description = "reversed interior range doesn't canonize",
		.should_build = 1,
		.inherits = 0,
		.canonical = 0,
		.should_canonize = 0,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "2",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "57",
				.max = "42",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "65523",
				.max = "65535",
			},
			{
				.type = V3_ASID_END,
			},
		},
	},
	{
		.description = "can't inherit and add AS ids",
		.should_build = 0,
		.inherits = 0,
		.canonical = 0,
		.should_canonize = 0,
		.delegations = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "2",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
	},
	{
		.description = "can't inherit and add rdis",
		.should_build = 0,
		.inherits = 0,
		.canonical = 0,
		.should_canonize = 0,
		.delegations = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "2",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
	},
};

const size_t N_ASIDENTIFIERS_BUILD_TESTS =
    sizeof(ASIdentifiers_build_data) / sizeof(ASIdentifiers_build_data[0]);

static int
add_as_delegation(ASIdentifiers *asid, const struct asid_or_range *delegation)
{
	ASN1_INTEGER	*min = NULL, *max = NULL;
	int		 ret = 0;

	if (delegation->inherit)
		return X509v3_asid_add_inherit(asid, delegation->type);

	if ((min = s2i_ASN1_INTEGER(NULL, delegation->min)) == NULL)
		goto err;

	if (delegation->max != NULL) {
		if ((max = s2i_ASN1_INTEGER(NULL, delegation->max)) == NULL)
			goto err;
	}

	if (!X509v3_asid_add_id_or_range(asid, delegation->type, min, max))
		goto err;
	min = NULL;
	max = NULL;

	ret = 1;

 err:
	ASN1_INTEGER_free(min);
	ASN1_INTEGER_free(max);

	return ret;
}

static ASIdentifiers *
build_asid(const struct asid_or_range delegations[])
{
	ASIdentifiers			*asid = NULL;
	const struct asid_or_range	*delegation;

	if ((asid = ASIdentifiers_new()) == NULL)
		goto err;

	for (delegation = &delegations[0]; delegation->type != V3_ASID_END;
	    delegation++) {
		if (!add_as_delegation(asid, delegation))
			goto err;
	}

	return asid;

 err:
	ASIdentifiers_free(asid);
	return NULL;
}

static int
build_asid_test(const struct ASIdentifiers_build_test *test)
{
	ASIdentifiers	*asid = NULL;
	unsigned char	*out = NULL;
	int		 out_len;
	int		 memcmp_failed = 1;
	int		 failed = 1;

	if ((asid = build_asid(test->delegations)) == NULL) {
		if (!test->should_build) {
			failed = 0;
			return failed;
		}
		fprintf(stderr, "%s: \"%s\" failed to build\n", __func__,
		    test->description);
		return failed;
	}

	if (!test->canonical) {
		if (X509v3_asid_is_canonical(asid)) {
			fprintf(stderr, "%s: \"%s\" shouldn't be canonical\n",
			    __func__, test->description);
			goto err;
		}
		if (X509v3_asid_canonize(asid) != test->should_canonize) {
			fprintf(stderr, "%s: \"%s\" failed to canonize\n",
			    __func__, test->description);
			goto err;
		}
		if (!test->should_canonize) {
			failed = 0;
			goto err;
		}
	}

	/*
	 * Verify that asid is in canonical form before converting it to DER.
	 */
	if (!X509v3_asid_is_canonical(asid)) {
		fprintf(stderr, "%s: asid is not canonical\n", __func__);
		goto err;
	}

	/*
	 * Convert asid to DER and check that it matches expectations
	 */
	out = NULL;
	if ((out_len = i2d_ASIdentifiers(asid, &out)) <= 0) {
		fprintf(stderr, "%s: \"%s\" i2d_ASIdentifiers failed\n",
		    __func__, test->description);
		goto err;
	}


	memcmp_failed = (size_t)out_len != test->der_len;
	if (!memcmp_failed)
		memcmp_failed = memcmp(out, test->der, test->der_len);
	if (memcmp_failed) {
		report_hexdump(__func__, test->description, "memcmp DER failed",
		    test->der, test->der_len, out, out_len);
		goto err;
	}

	/*
	 * Verify that asid inherits as expected
	 */
	if (X509v3_asid_inherits(asid) != test->inherits) {
		fprintf(stderr, "%s: \"%s\" unexpected asid inherit %d\n",
		    __func__, test->description, test->inherits);
		goto err;
	}

	failed = 0;

 err:
	free(out);
	ASIdentifiers_free(asid);

	return failed;
}

static int
run_ASIdentifiers_build_test(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_ASIDENTIFIERS_BUILD_TESTS; i++)
		failed |= build_asid_test(&ASIdentifiers_build_data[i]);

	return failed;
}

struct ASIdentifiers_subset_test {
	const char		*description;
	struct asid_or_range	 delegationsA[8];
	struct asid_or_range	 delegationsB[8];
	int			 is_subset;
	int			 is_subset_if_canonized;
};

const struct ASIdentifiers_subset_test ASIdentifiers_subset_data[] = {
	{
		.description = "simple subset relation",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = "4",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 1,
		.is_subset_if_canonized = 1,
	},
	{
		.description = "only asnums",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = "4",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 1,
		.is_subset_if_canonized = 1,
	},
	{
		.description = "only rdis",
		.delegationsA = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 1,
		.is_subset_if_canonized = 1,
	},
	{
		.description = "child only has asnums, parent only has rdis",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = "4",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 0,
		.is_subset_if_canonized = 0,
	},
	{
		.description = "child only has rdis, parent only has asnums",
		.delegationsA = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "2",
				.max = "4",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 0,
		.is_subset_if_canonized = 0,
	},
	{
		.description = "child only has rdis, parent has both",
		.delegationsA = {
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "2",
				.max = "4",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 1,
		.is_subset_if_canonized = 1,
	},
	{
		.description = "subset relation only after canonization",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3",
				.max = "4",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "3",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "4",
				.max = "5",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 0,
		.is_subset_if_canonized = 1,
	},
	{
		.description = "no subset if A inherits",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3",
				.max = "4",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "3",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "4",
				.max = "5",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "1",
				.max = "5",
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 0,
		.is_subset_if_canonized = 0,
	},
	{
		.description = "no subset if B inherits",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3",
				.max = "4",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 0,
				.min = "5",
				.max = NULL,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "3",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "4",
				.max = "5",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 0,
		.is_subset_if_canonized = 0,
	},
	{
		.description = "no subset if both inherit",
		.delegationsA = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "2",
				.max = NULL,
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "3",
				.max = "4",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.delegationsB = {
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "1",
				.max = "3",
			},
			{
				.type = V3_ASID_ASNUM,
				.inherit = 0,
				.min = "4",
				.max = "5",
			},
			{
				.type = V3_ASID_RDI,
				.inherit = 1,
			},
			{
				.type = V3_ASID_END,
			},
		},
		.is_subset = 0,
		.is_subset_if_canonized = 0,
	},
};

const size_t N_ASIDENTIFIERS_SUBSET_TESTS =
    sizeof(ASIdentifiers_subset_data) / sizeof(ASIdentifiers_subset_data[0]);

static int
asid_subset_test(const struct ASIdentifiers_subset_test *test)
{
	ASIdentifiers	*asidA = NULL, *asidB = NULL;
	int		 failed = 0;

	if ((asidA = build_asid(test->delegationsA)) == NULL)
		goto err;
	if ((asidB = build_asid(test->delegationsB)) == NULL)
		goto err;

	if (X509v3_asid_subset(asidA, asidB) != test->is_subset) {
		fprintf(stderr, "%s: \"%s\" X509v3_asid_subset failed\n",
		    __func__, test->description);
		failed = 1;
	}

	if (!test->is_subset) {
		if (!X509v3_asid_canonize(asidA))
			goto err;
		if (!X509v3_asid_canonize(asidB))
			goto err;
		if (X509v3_asid_subset(asidA, asidB) !=
		    test->is_subset_if_canonized) {
			fprintf(stderr, "%s: \"%s\" canonized subset failed\n",
			    __func__, test->description);
			failed = 1;
		}
	}

 err:
	ASIdentifiers_free(asidA);
	ASIdentifiers_free(asidB);

	return failed;
}

static int
run_ASIdentifiers_subset_test(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_ASIDENTIFIERS_SUBSET_TESTS; i++)
		failed |= asid_subset_test(&ASIdentifiers_subset_data[i]);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= run_IPAddressOrRange_tests();
	failed |= run_IPAddrBlock_tests();
	failed |= run_ASIdentifiers_build_test();
	failed |= run_ASIdentifiers_subset_test();

	return failed;
}
