/*	$OpenBSD: x509_addr.c,v 1.94 2025/05/10 05:54:39 tb Exp $ */
/*
 * Contributed to the OpenSSL Project by the American Registry for
 * Internet Numbers ("ARIN").
 */
/* ====================================================================
 * Copyright (c) 2006-2016 The OpenSSL Project.  All rights reserved.
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
 */

/*
 * Implementation of RFC 3779 section 2.2.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/buffer.h>
#include <openssl/conf.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "bytestring.h"
#include "err_local.h"
#include "x509_local.h"

#ifndef OPENSSL_NO_RFC3779

/*
 * OpenSSL ASN.1 template translation of RFC 3779 2.2.3.
 */

static const ASN1_TEMPLATE IPAddressRange_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressRange, min),
		.field_name = "min",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressRange, max),
		.field_name = "max",
		.item = &ASN1_BIT_STRING_it,
	},
};

const ASN1_ITEM IPAddressRange_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = IPAddressRange_seq_tt,
	.tcount = sizeof(IPAddressRange_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(IPAddressRange),
	.sname = "IPAddressRange",
};
LCRYPTO_ALIAS(IPAddressRange_it);

static const ASN1_TEMPLATE IPAddressOrRange_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressOrRange, u.addressPrefix),
		.field_name = "u.addressPrefix",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressOrRange, u.addressRange),
		.field_name = "u.addressRange",
		.item = &IPAddressRange_it,
	},
};

const ASN1_ITEM IPAddressOrRange_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(IPAddressOrRange, type),
	.templates = IPAddressOrRange_ch_tt,
	.tcount = sizeof(IPAddressOrRange_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(IPAddressOrRange),
	.sname = "IPAddressOrRange",
};
LCRYPTO_ALIAS(IPAddressOrRange_it);

static const ASN1_TEMPLATE IPAddressChoice_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressChoice, u.inherit),
		.field_name = "u.inherit",
		.item = &ASN1_NULL_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(IPAddressChoice, u.addressesOrRanges),
		.field_name = "u.addressesOrRanges",
		.item = &IPAddressOrRange_it,
	},
};

const ASN1_ITEM IPAddressChoice_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(IPAddressChoice, type),
	.templates = IPAddressChoice_ch_tt,
	.tcount = sizeof(IPAddressChoice_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(IPAddressChoice),
	.sname = "IPAddressChoice",
};
LCRYPTO_ALIAS(IPAddressChoice_it);

static const ASN1_TEMPLATE IPAddressFamily_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressFamily, addressFamily),
		.field_name = "addressFamily",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(IPAddressFamily, ipAddressChoice),
		.field_name = "ipAddressChoice",
		.item = &IPAddressChoice_it,
	},
};

const ASN1_ITEM IPAddressFamily_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = IPAddressFamily_seq_tt,
	.tcount = sizeof(IPAddressFamily_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(IPAddressFamily),
	.sname = "IPAddressFamily",
};
LCRYPTO_ALIAS(IPAddressFamily_it);

static const ASN1_TEMPLATE IPAddrBlocks_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "IPAddrBlocks",
	.item = &IPAddressFamily_it,
};

static const ASN1_ITEM IPAddrBlocks_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &IPAddrBlocks_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "IPAddrBlocks",
};

IPAddressRange *
d2i_IPAddressRange(IPAddressRange **a, const unsigned char **in, long len)
{
	return (IPAddressRange *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &IPAddressRange_it);
}
LCRYPTO_ALIAS(d2i_IPAddressRange);

int
i2d_IPAddressRange(IPAddressRange *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &IPAddressRange_it);
}
LCRYPTO_ALIAS(i2d_IPAddressRange);

IPAddressRange *
IPAddressRange_new(void)
{
	return (IPAddressRange *)ASN1_item_new(&IPAddressRange_it);
}
LCRYPTO_ALIAS(IPAddressRange_new);

void
IPAddressRange_free(IPAddressRange *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &IPAddressRange_it);
}
LCRYPTO_ALIAS(IPAddressRange_free);

IPAddressOrRange *
d2i_IPAddressOrRange(IPAddressOrRange **a, const unsigned char **in, long len)
{
	return (IPAddressOrRange *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &IPAddressOrRange_it);
}
LCRYPTO_ALIAS(d2i_IPAddressOrRange);

int
i2d_IPAddressOrRange(IPAddressOrRange *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &IPAddressOrRange_it);
}
LCRYPTO_ALIAS(i2d_IPAddressOrRange);

IPAddressOrRange *
IPAddressOrRange_new(void)
{
	return (IPAddressOrRange *)ASN1_item_new(&IPAddressOrRange_it);
}
LCRYPTO_ALIAS(IPAddressOrRange_new);

void
IPAddressOrRange_free(IPAddressOrRange *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &IPAddressOrRange_it);
}
LCRYPTO_ALIAS(IPAddressOrRange_free);

IPAddressChoice *
d2i_IPAddressChoice(IPAddressChoice **a, const unsigned char **in, long len)
{
	return (IPAddressChoice *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &IPAddressChoice_it);
}
LCRYPTO_ALIAS(d2i_IPAddressChoice);

int
i2d_IPAddressChoice(IPAddressChoice *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &IPAddressChoice_it);
}
LCRYPTO_ALIAS(i2d_IPAddressChoice);

IPAddressChoice *
IPAddressChoice_new(void)
{
	return (IPAddressChoice *)ASN1_item_new(&IPAddressChoice_it);
}
LCRYPTO_ALIAS(IPAddressChoice_new);

void
IPAddressChoice_free(IPAddressChoice *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &IPAddressChoice_it);
}
LCRYPTO_ALIAS(IPAddressChoice_free);

IPAddressFamily *
d2i_IPAddressFamily(IPAddressFamily **a, const unsigned char **in, long len)
{
	return (IPAddressFamily *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &IPAddressFamily_it);
}
LCRYPTO_ALIAS(d2i_IPAddressFamily);

int
i2d_IPAddressFamily(IPAddressFamily *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &IPAddressFamily_it);
}
LCRYPTO_ALIAS(i2d_IPAddressFamily);

IPAddressFamily *
IPAddressFamily_new(void)
{
	return (IPAddressFamily *)ASN1_item_new(&IPAddressFamily_it);
}
LCRYPTO_ALIAS(IPAddressFamily_new);

void
IPAddressFamily_free(IPAddressFamily *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &IPAddressFamily_it);
}
LCRYPTO_ALIAS(IPAddressFamily_free);

/*
 * Convenience accessors for IPAddressFamily.
 */

static int
IPAddressFamily_type(IPAddressFamily *af)
{
	/* XXX - can af->ipAddressChoice == NULL actually happen? */
	if (af == NULL || af->ipAddressChoice == NULL)
		return -1;

	switch (af->ipAddressChoice->type) {
	case IPAddressChoice_inherit:
	case IPAddressChoice_addressesOrRanges:
		return af->ipAddressChoice->type;
	default:
		return -1;
	}
}

static IPAddressOrRanges *
IPAddressFamily_addressesOrRanges(IPAddressFamily *af)
{
	if (IPAddressFamily_type(af) == IPAddressChoice_addressesOrRanges)
		return af->ipAddressChoice->u.addressesOrRanges;

	return NULL;
}

static ASN1_NULL *
IPAddressFamily_inheritance(IPAddressFamily *af)
{
	if (IPAddressFamily_type(af) == IPAddressChoice_inherit)
		return af->ipAddressChoice->u.inherit;

	return NULL;
}

static int
IPAddressFamily_set_inheritance(IPAddressFamily *af)
{
	if (IPAddressFamily_addressesOrRanges(af) != NULL)
		return 0;

	if (IPAddressFamily_inheritance(af) != NULL)
		return 1;

	if ((af->ipAddressChoice->u.inherit = ASN1_NULL_new()) == NULL)
		return 0;
	af->ipAddressChoice->type = IPAddressChoice_inherit;

	return 1;
}

/*
 * How much buffer space do we need for a raw address?
 */
#define ADDR_RAW_BUF_LEN        16

/*
 * What's the address length associated with this AFI?
 */
static int
length_from_afi(const unsigned afi, int *length)
{
	switch (afi) {
	case IANA_AFI_IPV4:
		*length = 4;
		return 1;
	case IANA_AFI_IPV6:
		*length = 16;
		return 1;
	default:
		*length = 0;
		return 0;
	}
}

/*
 * Get AFI and optional SAFI from an IPAddressFamily. All three out arguments
 * are optional; if |out_safi| is non-NULL, |safi_is_set| must be non-NULL.
 */
static int
IPAddressFamily_afi_safi(const IPAddressFamily *af, uint16_t *out_afi,
    uint8_t *out_safi, int *safi_is_set)
{
	CBS cbs;
	uint16_t afi;
	uint8_t safi = 0;
	int got_safi = 0;

	if (out_afi != NULL)
		*out_afi = 0;
	if (out_safi != NULL) {
		*out_safi = 0;
		*safi_is_set = 0;
	}

	CBS_init(&cbs, af->addressFamily->data, af->addressFamily->length);

	if (!CBS_get_u16(&cbs, &afi))
		return 0;

	if (afi != IANA_AFI_IPV4 && afi != IANA_AFI_IPV6)
		return 0;

	/* Fetch the optional SAFI. */
	if (CBS_len(&cbs) != 0) {
		if (!CBS_get_u8(&cbs, &safi))
			return 0;
		got_safi = 1;
	}

	/* If there's anything left, it's garbage. */
	if (CBS_len(&cbs) != 0)
		return 0;

	/* XXX - error on reserved AFI/SAFI? */

	if (out_afi != NULL)
		*out_afi = afi;

	if (out_safi != NULL) {
		*out_safi = safi;
		*safi_is_set = got_safi;
	}

	return 1;
}

static int
IPAddressFamily_afi(const IPAddressFamily *af, uint16_t *out_afi)
{
	return IPAddressFamily_afi_safi(af, out_afi, NULL, NULL);
}

static int
IPAddressFamily_afi_is_valid(const IPAddressFamily *af)
{
	return IPAddressFamily_afi_safi(af, NULL, NULL, NULL);
}

static int
IPAddressFamily_afi_length(const IPAddressFamily *af, int *out_length)
{
	uint16_t afi;

	*out_length = 0;

	if (!IPAddressFamily_afi(af, &afi))
		return 0;

	return length_from_afi(afi, out_length);
}

#define MINIMUM(a, b) (((a) < (b)) ? (a) : (b))

/*
 * Sort comparison function for a sequence of IPAddressFamily.
 *
 * The last paragraph of RFC 3779 2.2.3.3 is slightly ambiguous about
 * the ordering: I can read it as meaning that IPv6 without a SAFI
 * comes before IPv4 with a SAFI, which seems pretty weird.  The
 * examples in appendix B suggest that the author intended the
 * null-SAFI rule to apply only within a single AFI, which is what I
 * would have expected and is what the following code implements.
 */
static int
IPAddressFamily_cmp(const IPAddressFamily *const *a_,
    const IPAddressFamily *const *b_)
{
	const ASN1_OCTET_STRING *a = (*a_)->addressFamily;
	const ASN1_OCTET_STRING *b = (*b_)->addressFamily;
	int len, cmp;

	len = MINIMUM(a->length, b->length);

	if ((cmp = memcmp(a->data, b->data, len)) != 0)
		return cmp;

	return a->length - b->length;
}

static IPAddressFamily *
IPAddressFamily_find_in_parent(IPAddrBlocks *parent, IPAddressFamily *child_af)
{
	int index;

	(void)sk_IPAddressFamily_set_cmp_func(parent, IPAddressFamily_cmp);

	if ((index = sk_IPAddressFamily_find(parent, child_af)) < 0)
		return NULL;

	return sk_IPAddressFamily_value(parent, index);
}

/*
 * Extract the AFI from an IPAddressFamily.
 *
 * This is public API. It uses the reserved AFI 0 as an in-band error
 * while it doesn't care about the reserved AFI 65535...
 */
unsigned int
X509v3_addr_get_afi(const IPAddressFamily *af)
{
	uint16_t afi;

	/*
	 * XXX are these NULL checks really sensible? If af is non-NULL, it
	 * should have both addressFamily and ipAddressChoice...
	 */
	if (af == NULL || af->addressFamily == NULL ||
	    af->addressFamily->data == NULL)
		return 0;

	if (!IPAddressFamily_afi(af, &afi))
		return 0;

	return afi;
}
LCRYPTO_ALIAS(X509v3_addr_get_afi);

/*
 * Expand the bitstring form (RFC 3779, section 2.1.2) of an address into
 * a raw byte array.  At the moment this is coded for simplicity, not speed.
 *
 * Unused bits in the last octet of |bs| and all bits in subsequent bytes
 * of |addr| are set to 0 or 1 depending on whether |fill| is 0 or not.
 */
static int
addr_expand(unsigned char *addr, const ASN1_BIT_STRING *bs, const int length,
    uint8_t fill)
{
	if (bs->length < 0 || bs->length > length)
		return 0;

	if (fill != 0)
		fill = 0xff;

	if (bs->length > 0) {
		/* XXX - shouldn't this check ASN1_STRING_FLAG_BITS_LEFT? */
		uint8_t unused_bits = bs->flags & 7;
		uint8_t mask = (1 << unused_bits) - 1;

		memcpy(addr, bs->data, bs->length);

		if (fill == 0)
			addr[bs->length - 1] &= ~mask;
		else
			addr[bs->length - 1] |= mask;
	}

	memset(addr + bs->length, fill, length - bs->length);

	return 1;
}

/*
 * Extract the prefix length from a bitstring: 8 * length - unused bits.
 */
#define addr_prefix_len(bs) ((int) ((bs)->length * 8 - ((bs)->flags & 7)))

/*
 * i2r handler for one address bitstring.
 */
static int
i2r_address(BIO *out, const unsigned afi, const unsigned char fill,
    const ASN1_BIT_STRING *bs)
{
	unsigned char addr[ADDR_RAW_BUF_LEN];
	int i, n;

	if (bs->length < 0)
		return 0;
	switch (afi) {
	case IANA_AFI_IPV4:
		if (!addr_expand(addr, bs, 4, fill))
			return 0;
		BIO_printf(out, "%d.%d.%d.%d", addr[0], addr[1], addr[2],
		    addr[3]);
		break;
	case IANA_AFI_IPV6:
		if (!addr_expand(addr, bs, 16, fill))
			return 0;
		for (n = 16;
		    n > 1 && addr[n - 1] == 0x00 && addr[n - 2] == 0x00; n -= 2)
			continue;
		for (i = 0; i < n; i += 2)
			BIO_printf(out, "%x%s", (addr[i] << 8) | addr[i + 1],
			    (i < 14 ? ":" : ""));
		if (i < 16)
			BIO_puts(out, ":");
		if (i == 0)
			BIO_puts(out, ":");
		break;
	default:
		for (i = 0; i < bs->length; i++)
			BIO_printf(out, "%s%02x", (i > 0 ? ":" : ""),
			    bs->data[i]);
		BIO_printf(out, "[%d]", (int)(bs->flags & 7));
		break;
	}
	return 1;
}

/*
 * i2r handler for a sequence of addresses and ranges.
 */
static int
i2r_IPAddressOrRanges(BIO *out, const int indent,
    const IPAddressOrRanges *aors, const unsigned afi)
{
	const IPAddressOrRange *aor;
	const ASN1_BIT_STRING *prefix;
	const IPAddressRange *range;
	int i;

	for (i = 0; i < sk_IPAddressOrRange_num(aors); i++) {
		aor = sk_IPAddressOrRange_value(aors, i);

		BIO_printf(out, "%*s", indent, "");

		switch (aor->type) {
		case IPAddressOrRange_addressPrefix:
			prefix = aor->u.addressPrefix;

			if (!i2r_address(out, afi, 0x00, prefix))
				return 0;
			BIO_printf(out, "/%d\n", addr_prefix_len(prefix));
			continue;
		case IPAddressOrRange_addressRange:
			range = aor->u.addressRange;

			if (!i2r_address(out, afi, 0x00, range->min))
				return 0;
			BIO_puts(out, "-");
			if (!i2r_address(out, afi, 0xff, range->max))
				return 0;
			BIO_puts(out, "\n");
			continue;
		}
	}

	return 1;
}

/*
 * i2r handler for an IPAddrBlocks extension.
 */
static int
i2r_IPAddrBlocks(const X509V3_EXT_METHOD *method, void *ext, BIO *out,
    int indent)
{
	const IPAddrBlocks *addr = ext;
	IPAddressFamily *af;
	uint16_t afi;
	uint8_t safi;
	int i, safi_is_set;

	for (i = 0; i < sk_IPAddressFamily_num(addr); i++) {
		af = sk_IPAddressFamily_value(addr, i);

		if (!IPAddressFamily_afi_safi(af, &afi, &safi, &safi_is_set))
			goto print_addresses;

		switch (afi) {
		case IANA_AFI_IPV4:
			BIO_printf(out, "%*sIPv4", indent, "");
			break;
		case IANA_AFI_IPV6:
			BIO_printf(out, "%*sIPv6", indent, "");
			break;
		default:
			BIO_printf(out, "%*sUnknown AFI %u", indent, "", afi);
			break;
		}
		if (safi_is_set) {
			switch (safi) {
			case 1:
				BIO_puts(out, " (Unicast)");
				break;
			case 2:
				BIO_puts(out, " (Multicast)");
				break;
			case 3:
				BIO_puts(out, " (Unicast/Multicast)");
				break;
			case 4:
				BIO_puts(out, " (MPLS)");
				break;
			case 64:
				BIO_puts(out, " (Tunnel)");
				break;
			case 65:
				BIO_puts(out, " (VPLS)");
				break;
			case 66:
				BIO_puts(out, " (BGP MDT)");
				break;
			case 128:
				BIO_puts(out, " (MPLS-labeled VPN)");
				break;
			default:
				BIO_printf(out, " (Unknown SAFI %u)", safi);
				break;
			}
		}

 print_addresses:
		switch (IPAddressFamily_type(af)) {
		case IPAddressChoice_inherit:
			BIO_puts(out, ": inherit\n");
			break;
		case IPAddressChoice_addressesOrRanges:
			BIO_puts(out, ":\n");
			if (!i2r_IPAddressOrRanges(out, indent + 2,
			    IPAddressFamily_addressesOrRanges(af), afi))
				return 0;
			break;
		/* XXX - how should we handle -1 here? */
		}
	}
	return 1;
}

/*
 * Sort comparison function for a sequence of IPAddressOrRange
 * elements.
 *
 * There's no sane answer we can give if addr_expand() fails, and an
 * assertion failure on externally supplied data is seriously uncool,
 * so we just arbitrarily declare that if given invalid inputs this
 * function returns -1.  If this messes up your preferred sort order
 * for garbage input, tough noogies.
 */
static int
IPAddressOrRange_cmp(const IPAddressOrRange *a, const IPAddressOrRange *b,
    const int length)
{
	unsigned char addr_a[ADDR_RAW_BUF_LEN], addr_b[ADDR_RAW_BUF_LEN];
	int prefix_len_a = 0, prefix_len_b = 0;
	int r;

	switch (a->type) {
	case IPAddressOrRange_addressPrefix:
		if (!addr_expand(addr_a, a->u.addressPrefix, length, 0x00))
			return -1;
		prefix_len_a = addr_prefix_len(a->u.addressPrefix);
		break;
	case IPAddressOrRange_addressRange:
		if (!addr_expand(addr_a, a->u.addressRange->min, length, 0x00))
			return -1;
		prefix_len_a = length * 8;
		break;
	}

	switch (b->type) {
	case IPAddressOrRange_addressPrefix:
		if (!addr_expand(addr_b, b->u.addressPrefix, length, 0x00))
			return -1;
		prefix_len_b = addr_prefix_len(b->u.addressPrefix);
		break;
	case IPAddressOrRange_addressRange:
		if (!addr_expand(addr_b, b->u.addressRange->min, length, 0x00))
			return -1;
		prefix_len_b = length * 8;
		break;
	}

	if ((r = memcmp(addr_a, addr_b, length)) != 0)
		return r;
	else
		return prefix_len_a - prefix_len_b;
}

/*
 * IPv4-specific closure over IPAddressOrRange_cmp, since sk_sort()
 * comparison routines are only allowed two arguments.
 */
static int
v4IPAddressOrRange_cmp(const IPAddressOrRange *const *a,
    const IPAddressOrRange *const *b)
{
	return IPAddressOrRange_cmp(*a, *b, 4);
}

/*
 * IPv6-specific closure over IPAddressOrRange_cmp, since sk_sort()
 * comparison routines are only allowed two arguments.
 */
static int
v6IPAddressOrRange_cmp(const IPAddressOrRange *const *a,
    const IPAddressOrRange *const *b)
{
	return IPAddressOrRange_cmp(*a, *b, 16);
}

/*
 * Calculate whether a range collapses to a prefix.
 * See last paragraph of RFC 3779 2.2.3.7.
 *
 * It's the caller's responsibility to ensure that min <= max.
 */
static int
range_should_be_prefix(const unsigned char *min, const unsigned char *max,
    const int length)
{
	unsigned char mask;
	int i, j;

	for (i = 0; i < length && min[i] == max[i]; i++)
		continue;
	for (j = length - 1; j >= 0 && min[j] == 0x00 && max[j] == 0xff; j--)
		continue;
	if (i < j)
		return -1;
	if (i > j)
		return i * 8;
	mask = min[i] ^ max[i];
	switch (mask) {
	case 0x01:
		j = 7;
		break;
	case 0x03:
		j = 6;
		break;
	case 0x07:
		j = 5;
		break;
	case 0x0f:
		j = 4;
		break;
	case 0x1f:
		j = 3;
		break;
	case 0x3f:
		j = 2;
		break;
	case 0x7f:
		j = 1;
		break;
	default:
		return -1;
	}
	if ((min[i] & mask) != 0 || (max[i] & mask) != mask)
		return -1;
	else
		return i * 8 + j;
}

/*
 * Fill IPAddressOrRange with bit string encoding of a prefix - RFC 3779, 2.1.1.
 */
static int
make_addressPrefix(IPAddressOrRange **out_aor, uint8_t *addr, uint32_t afi,
    int prefix_len)
{
	IPAddressOrRange *aor = NULL;
	int afi_len, num_bits, num_octets;
	uint8_t unused_bits;

	if (prefix_len < 0)
		goto err;

	if (!length_from_afi(afi, &afi_len))
		goto err;
	if (prefix_len > 8 * afi_len)
		goto err;

	num_octets = (prefix_len + 7) / 8;
	num_bits = prefix_len % 8;

	unused_bits = 0;
	if (num_bits > 0)
		unused_bits = 8 - num_bits;

	if ((aor = IPAddressOrRange_new()) == NULL)
		goto err;

	aor->type = IPAddressOrRange_addressPrefix;

	if ((aor->u.addressPrefix = ASN1_BIT_STRING_new()) == NULL)
		goto err;
	if (!ASN1_BIT_STRING_set(aor->u.addressPrefix, addr, num_octets))
		goto err;
	if (!asn1_abs_set_unused_bits(aor->u.addressPrefix, unused_bits))
		goto err;

	*out_aor = aor;
	return 1;

 err:
	IPAddressOrRange_free(aor);
	return 0;
}

static uint8_t
count_trailing_zeroes(uint8_t octet)
{
	uint8_t count = 0;

	if (octet == 0)
		return 8;

	while ((octet & (1 << count)) == 0)
		count++;

	return count;
}

static int
trim_end_u8(CBS *cbs, uint8_t trim)
{
	uint8_t octet;

	while (CBS_len(cbs) > 0) {
		if (!CBS_peek_last_u8(cbs, &octet))
			return 0;
		if (octet != trim)
			return 1;
		if (!CBS_get_last_u8(cbs, &octet))
			return 0;
	}

	return 1;
}

/*
 * Populate IPAddressOrRange with bit string encoding of a range, see
 * RFC 3779, 2.1.2.
 */
static int
make_addressRange(IPAddressOrRange **out_aor, uint8_t *min, uint8_t *max,
    uint32_t afi, int length)
{
	IPAddressOrRange *aor = NULL;
	IPAddressRange *range;
	int prefix_len;
	CBS cbs;
	size_t max_len, min_len;
	uint8_t unused_bits_min, unused_bits_max;
	uint8_t octet;

	if (memcmp(min, max, length) > 0)
		goto err;

	/*
	 * RFC 3779, 2.2.3.6 - a range that can be expressed as a prefix
	 * must be encoded as a prefix.
	 */

	if ((prefix_len = range_should_be_prefix(min, max, length)) >= 0)
		return make_addressPrefix(out_aor, min, afi, prefix_len);

	/*
	 * The bit string representing min is formed by removing all its
	 * trailing zero bits, so remove all trailing zero octets and count
	 * the trailing zero bits of the last octet.
	 */

	CBS_init(&cbs, min, length);

	if (!trim_end_u8(&cbs, 0x00))
		goto err;

	unused_bits_min = 0;
	if ((min_len = CBS_len(&cbs)) > 0) {
		if (!CBS_peek_last_u8(&cbs, &octet))
			goto err;

		unused_bits_min = count_trailing_zeroes(octet);
	}

	/*
	 * The bit string representing max is formed by removing all its
	 * trailing one bits, so remove all trailing 0xff octets and count
	 * the trailing ones of the last octet.
	 */

	CBS_init(&cbs, max, length);

	if (!trim_end_u8(&cbs, 0xff))
		goto err;

	unused_bits_max = 0;
	if ((max_len = CBS_len(&cbs)) > 0) {
		if (!CBS_peek_last_u8(&cbs, &octet))
			goto err;

		unused_bits_max = count_trailing_zeroes(octet + 1);
	}

	/*
	 * Populate IPAddressOrRange.
	 */

	if ((aor = IPAddressOrRange_new()) == NULL)
		goto err;

	aor->type = IPAddressOrRange_addressRange;

	if ((range = aor->u.addressRange = IPAddressRange_new()) == NULL)
		goto err;

	if (!ASN1_BIT_STRING_set(range->min, min, min_len))
		goto err;
	if (!asn1_abs_set_unused_bits(range->min, unused_bits_min))
		goto err;

	if (!ASN1_BIT_STRING_set(range->max, max, max_len))
		goto err;
	if (!asn1_abs_set_unused_bits(range->max, unused_bits_max))
		goto err;

	*out_aor = aor;

	return 1;

 err:
	IPAddressOrRange_free(aor);
	return 0;
}

/*
 * Construct a new address family or find an existing one.
 */
static IPAddressFamily *
make_IPAddressFamily(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi)
{
	IPAddressFamily *af = NULL;
	CBB cbb;
	CBS cbs;
	uint8_t *key = NULL;
	size_t keylen;
	int i;

	if (!CBB_init(&cbb, 0))
		goto err;

	if (afi != IANA_AFI_IPV4 && afi != IANA_AFI_IPV6)
		goto err;
	if (!CBB_add_u16(&cbb, afi))
		goto err;

	if (safi != NULL) {
		if (*safi > 255)
			goto err;
		if (!CBB_add_u8(&cbb, *safi))
			goto err;
	}

	if (!CBB_finish(&cbb, &key, &keylen))
		goto err;

	for (i = 0; i < sk_IPAddressFamily_num(addr); i++) {
		af = sk_IPAddressFamily_value(addr, i);

		CBS_init(&cbs, af->addressFamily->data,
		    af->addressFamily->length);
		if (CBS_mem_equal(&cbs, key, keylen))
			goto done;
	}

	if ((af = IPAddressFamily_new()) == NULL)
		goto err;
	if (!ASN1_OCTET_STRING_set(af->addressFamily, key, keylen))
		goto err;
	if (!sk_IPAddressFamily_push(addr, af))
		goto err;

 done:
	free(key);

	return af;

 err:
	CBB_cleanup(&cbb);
	free(key);
	IPAddressFamily_free(af);

	return NULL;
}

/*
 * Add an inheritance element.
 */
int
X509v3_addr_add_inherit(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi)
{
	IPAddressFamily *af;

	if ((af = make_IPAddressFamily(addr, afi, safi)) == NULL)
		return 0;

	return IPAddressFamily_set_inheritance(af);
}
LCRYPTO_ALIAS(X509v3_addr_add_inherit);

/*
 * Construct an IPAddressOrRange sequence, or return an existing one.
 */
static IPAddressOrRanges *
make_prefix_or_range(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi)
{
	IPAddressFamily *af;
	IPAddressOrRanges *aors = NULL;

	if ((af = make_IPAddressFamily(addr, afi, safi)) == NULL)
		return NULL;

	if (IPAddressFamily_inheritance(af) != NULL)
		return NULL;

	if ((aors = IPAddressFamily_addressesOrRanges(af)) != NULL)
		return aors;

	if ((aors = sk_IPAddressOrRange_new_null()) == NULL)
		return NULL;

	switch (afi) {
	case IANA_AFI_IPV4:
		(void)sk_IPAddressOrRange_set_cmp_func(aors,
		    v4IPAddressOrRange_cmp);
		break;
	case IANA_AFI_IPV6:
		(void)sk_IPAddressOrRange_set_cmp_func(aors,
		    v6IPAddressOrRange_cmp);
		break;
	}

	af->ipAddressChoice->type = IPAddressChoice_addressesOrRanges;
	af->ipAddressChoice->u.addressesOrRanges = aors;

	return aors;
}

/*
 * Add a prefix.
 */
int
X509v3_addr_add_prefix(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi, unsigned char *a, const int prefix_len)
{
	IPAddressOrRanges *aors;
	IPAddressOrRange *aor;

	if ((aors = make_prefix_or_range(addr, afi, safi)) == NULL)
		return 0;

	if (!make_addressPrefix(&aor, a, afi, prefix_len))
		return 0;

	if (sk_IPAddressOrRange_push(aors, aor) <= 0) {
		IPAddressOrRange_free(aor);
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(X509v3_addr_add_prefix);

/*
 * Add a range.
 */
int
X509v3_addr_add_range(IPAddrBlocks *addr, const unsigned afi,
    const unsigned *safi, unsigned char *min, unsigned char *max)
{
	IPAddressOrRanges *aors;
	IPAddressOrRange *aor;
	int length;

	if ((aors = make_prefix_or_range(addr, afi, safi)) == NULL)
		return 0;

	if (!length_from_afi(afi, &length))
		return 0;

	if (!make_addressRange(&aor, min, max, afi, length))
		return 0;

	if (sk_IPAddressOrRange_push(aors, aor) <= 0) {
		IPAddressOrRange_free(aor);
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(X509v3_addr_add_range);

static int
extract_min_max_bitstr(IPAddressOrRange *aor, ASN1_BIT_STRING **out_min,
    ASN1_BIT_STRING **out_max)
{
	switch (aor->type) {
	case IPAddressOrRange_addressPrefix:
		*out_min = *out_max = aor->u.addressPrefix;
		return 1;
	case IPAddressOrRange_addressRange:
		*out_min = aor->u.addressRange->min;
		*out_max = aor->u.addressRange->max;
		return 1;
	default:
		return 0;
	}
}

/*
 * Extract min and max values from an IPAddressOrRange.
 */
static int
extract_min_max(IPAddressOrRange *aor, unsigned char *min, unsigned char *max,
    int length)
{
	ASN1_BIT_STRING *min_bitstr, *max_bitstr;

	if (aor == NULL || min == NULL || max == NULL)
		return 0;

	if (!extract_min_max_bitstr(aor, &min_bitstr, &max_bitstr))
		return 0;

	if (!addr_expand(min, min_bitstr, length, 0))
		return 0;

	return addr_expand(max, max_bitstr, length, 1);
}

/*
 * Public wrapper for extract_min_max().
 */
int
X509v3_addr_get_range(IPAddressOrRange *aor, const unsigned afi,
    unsigned char *min, unsigned char *max, const int length)
{
	int afi_len;

	if (!length_from_afi(afi, &afi_len))
		return 0;

	if (length < afi_len)
		return 0;

	if (!extract_min_max(aor, min, max, afi_len))
		return 0;

	return afi_len;
}
LCRYPTO_ALIAS(X509v3_addr_get_range);

/*
 * Check whether an IPAddrBLocks is in canonical form.
 */
int
X509v3_addr_is_canonical(IPAddrBlocks *addr)
{
	unsigned char a_min[ADDR_RAW_BUF_LEN], a_max[ADDR_RAW_BUF_LEN];
	unsigned char b_min[ADDR_RAW_BUF_LEN], b_max[ADDR_RAW_BUF_LEN];
	IPAddressFamily *af;
	IPAddressOrRanges *aors;
	IPAddressOrRange *aor, *aor_a, *aor_b;
	int i, j, k, length;

	/*
	 * Empty extension is canonical.
	 */
	if (addr == NULL)
		return 1;

	/*
	 * Check whether the top-level list is in order.
	 */
	for (i = 0; i < sk_IPAddressFamily_num(addr) - 1; i++) {
		const IPAddressFamily *a = sk_IPAddressFamily_value(addr, i);
		const IPAddressFamily *b = sk_IPAddressFamily_value(addr, i + 1);

		/* Check that both have valid AFIs before comparing them. */
		if (!IPAddressFamily_afi_is_valid(a))
			return 0;
		if (!IPAddressFamily_afi_is_valid(b))
			return 0;

		if (IPAddressFamily_cmp(&a, &b) >= 0)
			return 0;
	}

	/*
	 * Top level's ok, now check each address family.
	 */
	for (i = 0; i < sk_IPAddressFamily_num(addr); i++) {
		af = sk_IPAddressFamily_value(addr, i);

		if (!IPAddressFamily_afi_length(af, &length))
			return 0;

		/*
		 * If this family has an inheritance element, it is canonical.
		 */
		if (IPAddressFamily_inheritance(af) != NULL)
			continue;

		/*
		 * If this family has neither an inheritance element nor an
		 * addressesOrRanges, we don't know what this is.
		 */
		if ((aors = IPAddressFamily_addressesOrRanges(af)) == NULL)
			return 0;

		if (sk_IPAddressOrRange_num(aors) == 0)
			return 0;

		for (j = 0; j < sk_IPAddressOrRange_num(aors) - 1; j++) {
			aor_a = sk_IPAddressOrRange_value(aors, j);
			aor_b = sk_IPAddressOrRange_value(aors, j + 1);

			if (!extract_min_max(aor_a, a_min, a_max, length) ||
			    !extract_min_max(aor_b, b_min, b_max, length))
				return 0;

			/*
			 * Punt misordered list, overlapping start, or inverted
			 * range.
			 */
			if (memcmp(a_min, b_min, length) >= 0 ||
			    memcmp(a_min, a_max, length) > 0 ||
			    memcmp(b_min, b_max, length) > 0)
				return 0;

			/*
			 * Punt if adjacent or overlapping.  Check for adjacency
			 * by subtracting one from b_min first.
			 */
			for (k = length - 1; k >= 0 && b_min[k]-- == 0x00; k--)
				continue;
			if (memcmp(a_max, b_min, length) >= 0)
				return 0;

			/*
			 * Check for range that should be expressed as a prefix.
			 */
			if (aor_a->type == IPAddressOrRange_addressPrefix)
				continue;

			if (range_should_be_prefix(a_min, a_max, length) >= 0)
				return 0;
		}

		/*
		 * Check final range to see if it's inverted or should be a
		 * prefix.
		 */
		aor = sk_IPAddressOrRange_value(aors, j);
		if (aor->type == IPAddressOrRange_addressRange) {
			if (!extract_min_max(aor, a_min, a_max, length))
				return 0;
			if (memcmp(a_min, a_max, length) > 0)
				return 0;
			if (range_should_be_prefix(a_min, a_max, length) >= 0)
				return 0;
		}
	}

	/*
	 * If we made it through all that, we're happy.
	 */
	return 1;
}
LCRYPTO_ALIAS(X509v3_addr_is_canonical);

/*
 * Whack an IPAddressOrRanges into canonical form.
 */
static int
IPAddressOrRanges_canonize(IPAddressOrRanges *aors, const unsigned afi)
{
	IPAddressOrRange *a, *b, *merged;
	unsigned char a_min[ADDR_RAW_BUF_LEN], a_max[ADDR_RAW_BUF_LEN];
	unsigned char b_min[ADDR_RAW_BUF_LEN], b_max[ADDR_RAW_BUF_LEN];
	int i, j, length;

	if (!length_from_afi(afi, &length))
		return 0;

	/*
	 * Sort the IPAddressOrRanges sequence.
	 */
	sk_IPAddressOrRange_sort(aors);

	/*
	 * Clean up representation issues, punt on duplicates or overlaps.
	 */
	for (i = 0; i < sk_IPAddressOrRange_num(aors) - 1; i++) {
		a = sk_IPAddressOrRange_value(aors, i);
		b = sk_IPAddressOrRange_value(aors, i + 1);

		if (!extract_min_max(a, a_min, a_max, length) ||
		    !extract_min_max(b, b_min, b_max, length))
			return 0;

		/*
		 * Punt inverted ranges.
		 */
		if (memcmp(a_min, a_max, length) > 0 ||
		    memcmp(b_min, b_max, length) > 0)
			return 0;

		/*
		 * Punt overlaps.
		 */
		if (memcmp(a_max, b_min, length) >= 0)
			return 0;

		/*
		 * Merge if a and b are adjacent.  We check for
		 * adjacency by subtracting one from b_min first.
		 */
		for (j = length - 1; j >= 0 && b_min[j]-- == 0x00; j--)
			continue;

		if (memcmp(a_max, b_min, length) != 0)
			continue;

		if (!make_addressRange(&merged, a_min, b_max, afi, length))
			return 0;
		sk_IPAddressOrRange_set(aors, i, merged);
		(void)sk_IPAddressOrRange_delete(aors, i + 1);
		IPAddressOrRange_free(a);
		IPAddressOrRange_free(b);
		i--;
	}

	/*
	 * Check for inverted final range.
	 */
	a = sk_IPAddressOrRange_value(aors, i);
	if (a != NULL && a->type == IPAddressOrRange_addressRange) {
		if (!extract_min_max(a, a_min, a_max, length))
			return 0;
		if (memcmp(a_min, a_max, length) > 0)
			return 0;
	}

	return 1;
}

/*
 * Whack an IPAddrBlocks extension into canonical form.
 */
int
X509v3_addr_canonize(IPAddrBlocks *addr)
{
	IPAddressFamily *af;
	IPAddressOrRanges *aors;
	uint16_t afi;
	int i;

	for (i = 0; i < sk_IPAddressFamily_num(addr); i++) {
		af = sk_IPAddressFamily_value(addr, i);

		/* Check AFI/SAFI here - IPAddressFamily_cmp() can't error. */
		if (!IPAddressFamily_afi(af, &afi))
			return 0;

		if ((aors = IPAddressFamily_addressesOrRanges(af)) == NULL)
			continue;

		if (!IPAddressOrRanges_canonize(aors, afi))
			return 0;
	}

	(void)sk_IPAddressFamily_set_cmp_func(addr, IPAddressFamily_cmp);
	sk_IPAddressFamily_sort(addr);

	return X509v3_addr_is_canonical(addr);
}
LCRYPTO_ALIAS(X509v3_addr_canonize);

/*
 * v2i handler for the IPAddrBlocks extension.
 */
static void *
v2i_IPAddrBlocks(const struct v3_ext_method *method, struct v3_ext_ctx *ctx,
    STACK_OF(CONF_VALUE)*values)
{
	static const char v4addr_chars[] = "0123456789.";
	static const char v6addr_chars[] = "0123456789.:abcdefABCDEF";
	IPAddrBlocks *addr = NULL;
	char *s = NULL, *t;
	int i;

	if ((addr = sk_IPAddressFamily_new(IPAddressFamily_cmp)) == NULL) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	for (i = 0; i < sk_CONF_VALUE_num(values); i++) {
		CONF_VALUE *val = sk_CONF_VALUE_value(values, i);
		unsigned char min[ADDR_RAW_BUF_LEN], max[ADDR_RAW_BUF_LEN];
		unsigned afi, *safi = NULL, safi_;
		const char *addr_chars = NULL;
		const char *errstr;
		int prefix_len, i1, i2, delim, length;

		if (!name_cmp(val->name, "IPv4")) {
			afi = IANA_AFI_IPV4;
		} else if (!name_cmp(val->name, "IPv6")) {
			afi = IANA_AFI_IPV6;
		} else if (!name_cmp(val->name, "IPv4-SAFI")) {
			afi = IANA_AFI_IPV4;
			safi = &safi_;
		} else if (!name_cmp(val->name, "IPv6-SAFI")) {
			afi = IANA_AFI_IPV6;
			safi = &safi_;
		} else {
			X509V3error(X509V3_R_EXTENSION_NAME_ERROR);
			X509V3_conf_err(val);
			goto err;
		}

		switch (afi) {
		case IANA_AFI_IPV4:
			addr_chars = v4addr_chars;
			break;
		case IANA_AFI_IPV6:
			addr_chars = v6addr_chars;
			break;
		}

		if (!length_from_afi(afi, &length))
			goto err;

		/*
		 * Handle SAFI, if any, and strdup() so we can null-terminate
		 * the other input values.
		 */
		if (safi != NULL) {
			unsigned long parsed_safi;
			int saved_errno = errno;

			errno = 0;
			parsed_safi = strtoul(val->value, &t, 0);

			/* Value must be present, then a tab, space or colon. */
			if (val->value[0] == '\0' ||
			    (*t != '\t' && *t != ' ' && *t != ':')) {
				X509V3error(X509V3_R_INVALID_SAFI);
				X509V3_conf_err(val);
				goto err;
			}
			/* Range and overflow check. */
			if ((errno == ERANGE && parsed_safi == ULONG_MAX) ||
			    parsed_safi > 0xff) {
				X509V3error(X509V3_R_INVALID_SAFI);
				X509V3_conf_err(val);
				goto err;
			}
			errno = saved_errno;

			*safi = parsed_safi;

			/* Check possible whitespace is followed by a colon. */
			t += strspn(t, " \t");
			if (*t != ':') {
				X509V3error(X509V3_R_INVALID_SAFI);
				X509V3_conf_err(val);
				goto err;
			}

			/* Skip over colon. */
			t++;

			/* Then over any trailing whitespace. */
			t += strspn(t, " \t");

			s = strdup(t);
		} else {
			s = strdup(val->value);
		}
		if (s == NULL) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}

		/*
		 * Check for inheritance. Not worth additional complexity to
		 * optimize this (seldom-used) case.
		 */
		if (strcmp(s, "inherit") == 0) {
			if (!X509v3_addr_add_inherit(addr, afi, safi)) {
				X509V3error(X509V3_R_INVALID_INHERITANCE);
				X509V3_conf_err(val);
				goto err;
			}
			free(s);
			s = NULL;
			continue;
		}

		i1 = strspn(s, addr_chars);
		i2 = i1 + strspn(s + i1, " \t");
		delim = s[i2++];
		s[i1] = '\0';

		if (a2i_ipadd(min, s) != length) {
			X509V3error(X509V3_R_INVALID_IPADDRESS);
			X509V3_conf_err(val);
			goto err;
		}

		switch (delim) {
		case '/':
			/* length contains the size of the address in bytes. */
			if (length != 4 && length != 16)
				goto err;
			prefix_len = strtonum(s + i2, 0, 8 * length, &errstr);
			if (errstr != NULL) {
				X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
				X509V3_conf_err(val);
				goto err;
			}
			if (!X509v3_addr_add_prefix(addr, afi, safi, min,
			    prefix_len)) {
				X509V3error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			break;
		case '-':
			i1 = i2 + strspn(s + i2, " \t");
			i2 = i1 + strspn(s + i1, addr_chars);
			if (i1 == i2 || s[i2] != '\0') {
				X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
				X509V3_conf_err(val);
				goto err;
			}
			if (a2i_ipadd(max, s + i1) != length) {
				X509V3error(X509V3_R_INVALID_IPADDRESS);
				X509V3_conf_err(val);
				goto err;
			}
			if (memcmp(min, max, length) > 0) {
				X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
				X509V3_conf_err(val);
				goto err;
			}
			if (!X509v3_addr_add_range(addr, afi, safi, min, max)) {
				X509V3error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			break;
		case '\0':
			if (!X509v3_addr_add_prefix(addr, afi, safi, min,
			    length * 8)) {
				X509V3error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			break;
		default:
			X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
			X509V3_conf_err(val);
			goto err;
		}

		free(s);
		s = NULL;
	}

	/*
	 * Canonize the result, then we're done.
	 */
	if (!X509v3_addr_canonize(addr))
		goto err;
	return addr;

 err:
	free(s);
	sk_IPAddressFamily_pop_free(addr, IPAddressFamily_free);
	return NULL;
}

/*
 * OpenSSL dispatch
 */
static const X509V3_EXT_METHOD x509v3_ext_sbgp_ipAddrBlock = {
	.ext_nid = NID_sbgp_ipAddrBlock,
	.ext_flags = 0,
	.it = &IPAddrBlocks_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = v2i_IPAddrBlocks,
	.i2r = i2r_IPAddrBlocks,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_sbgp_ipAddrBlock(void)
{
	return &x509v3_ext_sbgp_ipAddrBlock;
}

/*
 * Figure out whether extension uses inheritance.
 */
int
X509v3_addr_inherits(IPAddrBlocks *addr)
{
	IPAddressFamily *af;
	int i;

	if (addr == NULL)
		return 0;

	for (i = 0; i < sk_IPAddressFamily_num(addr); i++) {
		af = sk_IPAddressFamily_value(addr, i);

		if (IPAddressFamily_inheritance(af) != NULL)
			return 1;
	}

	return 0;
}
LCRYPTO_ALIAS(X509v3_addr_inherits);

/*
 * Figure out whether parent contains child.
 *
 * This only works correctly if both parent and child are in canonical form.
 */
static int
addr_contains(IPAddressOrRanges *parent, IPAddressOrRanges *child, int length)
{
	IPAddressOrRange *child_aor, *parent_aor;
	uint8_t parent_min[ADDR_RAW_BUF_LEN], parent_max[ADDR_RAW_BUF_LEN];
	uint8_t child_min[ADDR_RAW_BUF_LEN], child_max[ADDR_RAW_BUF_LEN];
	int p, c;

	if (child == NULL || parent == child)
		return 1;
	if (parent == NULL)
		return 0;

	p = 0;
	for (c = 0; c < sk_IPAddressOrRange_num(child); c++) {
		child_aor = sk_IPAddressOrRange_value(child, c);

		if (!extract_min_max(child_aor, child_min, child_max, length))
			return 0;

		for (;; p++) {
			if (p >= sk_IPAddressOrRange_num(parent))
				return 0;

			parent_aor = sk_IPAddressOrRange_value(parent, p);

			if (!extract_min_max(parent_aor, parent_min, parent_max,
			    length))
				return 0;

			if (memcmp(parent_max, child_max, length) < 0)
				continue;
			if (memcmp(parent_min, child_min, length) > 0)
				return 0;
			break;
		}
	}

	return 1;
}

/*
 * Test whether |child| is a subset of |parent|.
 */
int
X509v3_addr_subset(IPAddrBlocks *child, IPAddrBlocks *parent)
{
	IPAddressFamily *child_af, *parent_af;
	IPAddressOrRanges *child_aor, *parent_aor;
	int i, length;

	if (child == NULL || child == parent)
		return 1;
	if (parent == NULL)
		return 0;

	if (X509v3_addr_inherits(child) || X509v3_addr_inherits(parent))
		return 0;

	for (i = 0; i < sk_IPAddressFamily_num(child); i++) {
		child_af = sk_IPAddressFamily_value(child, i);

		parent_af = IPAddressFamily_find_in_parent(parent, child_af);
		if (parent_af == NULL)
			return 0;

		if (!IPAddressFamily_afi_length(parent_af, &length))
			return 0;

		child_aor = IPAddressFamily_addressesOrRanges(child_af);
		parent_aor = IPAddressFamily_addressesOrRanges(parent_af);

		if (!addr_contains(parent_aor, child_aor, length))
			return 0;
	}
	return 1;
}
LCRYPTO_ALIAS(X509v3_addr_subset);

static int
verify_error(X509_STORE_CTX *ctx, X509 *cert, int error, int depth)
{
	if (ctx == NULL)
		return 0;

	ctx->current_cert = cert;
	ctx->error = error;
	ctx->error_depth = depth;

	return ctx->verify_cb(0, ctx);
}

/*
 * Core code for RFC 3779 2.3 path validation.
 *
 * Returns 1 for success, 0 on error.
 *
 * When returning 0, ctx->error MUST be set to an appropriate value other than
 * X509_V_OK.
 */
static int
addr_validate_path_internal(X509_STORE_CTX *ctx, STACK_OF(X509) *chain,
    IPAddrBlocks *ext)
{
	IPAddrBlocks *child = NULL, *parent = NULL;
	IPAddressFamily *child_af, *parent_af;
	IPAddressOrRanges *child_aor, *parent_aor;
	X509 *cert = NULL;
	int depth = -1;
	int i;
	unsigned int length;
	int ret = 1;

	/* We need a non-empty chain to test against. */
	if (sk_X509_num(chain) <= 0)
		goto err;
	/* We need either a store ctx or an extension to work with. */
	if (ctx == NULL && ext == NULL)
		goto err;
	/* If there is a store ctx, it needs a verify_cb. */
	if (ctx != NULL && ctx->verify_cb == NULL)
		goto err;

	/*
	 * Figure out where to start. If we don't have an extension to check,
	 * (either extracted from the leaf or passed by the caller), we're done.
	 * Otherwise, check canonical form and set up for walking up the chain.
	 */
	if (ext == NULL) {
		depth = 0;
		cert = sk_X509_value(chain, depth);
		if ((X509_get_extension_flags(cert) & EXFLAG_INVALID) != 0) {
			if ((ret = verify_error(ctx, cert,
			    X509_V_ERR_INVALID_EXTENSION, depth)) == 0)
				goto done;
		}
		if ((ext = cert->rfc3779_addr) == NULL)
			goto done;
	} else if (!X509v3_addr_is_canonical(ext)) {
		if ((ret = verify_error(ctx, cert,
		    X509_V_ERR_INVALID_EXTENSION, depth)) == 0)
			goto done;
	}

	(void)sk_IPAddressFamily_set_cmp_func(ext, IPAddressFamily_cmp);
	if ((child = sk_IPAddressFamily_dup(ext)) == NULL) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		if (ctx != NULL)
			ctx->error = X509_V_ERR_OUT_OF_MEM;
		ret = 0;
		goto done;
	}

	/*
	 * Now walk up the chain. No cert may list resources that its parent
	 * doesn't list.
	 */
	for (depth++; depth < sk_X509_num(chain); depth++) {
		cert = sk_X509_value(chain, depth);

		if ((X509_get_extension_flags(cert) & EXFLAG_INVALID) != 0) {
			if ((ret = verify_error(ctx, cert,
			    X509_V_ERR_INVALID_EXTENSION, depth)) == 0)
				goto done;
		}

		if ((parent = cert->rfc3779_addr) == NULL) {
			for (i = 0; i < sk_IPAddressFamily_num(child); i++) {
				child_af = sk_IPAddressFamily_value(child, i);

				if (IPAddressFamily_inheritance(child_af) !=
				    NULL)
					continue;

				if ((ret = verify_error(ctx, cert,
				    X509_V_ERR_UNNESTED_RESOURCE, depth)) == 0)
					goto done;
				break;
			}
			continue;
		}

		/*
		 * Check that the child's resources are covered by the parent.
		 * Each covered resource is replaced with the parent's resource
		 * covering it, so the next iteration will check that the
		 * parent's resources are covered by the grandparent.
		 */
		for (i = 0; i < sk_IPAddressFamily_num(child); i++) {
			child_af = sk_IPAddressFamily_value(child, i);

			if ((parent_af = IPAddressFamily_find_in_parent(parent,
			    child_af)) == NULL) {
				/*
				 * If we have no match in the parent and the
				 * child inherits, that's fine.
				 */
				if (IPAddressFamily_inheritance(child_af) !=
				    NULL)
					continue;

				/* Otherwise the child isn't covered. */
				if ((ret = verify_error(ctx, cert,
				    X509_V_ERR_UNNESTED_RESOURCE, depth)) == 0)
					goto done;
				break;
			}

			/* Parent inherits, nothing to do. */
			if (IPAddressFamily_inheritance(parent_af) != NULL)
				continue;

			/* Child inherits. Use parent's address family. */
			if (IPAddressFamily_inheritance(child_af) != NULL) {
				sk_IPAddressFamily_set(child, i, parent_af);
				continue;
			}

			child_aor = IPAddressFamily_addressesOrRanges(child_af);
			parent_aor =
			    IPAddressFamily_addressesOrRanges(parent_af);

			/*
			 * Child and parent are canonical and neither inherits.
			 * If either addressesOrRanges is NULL, something's
			 * very wrong.
			 */
			if (child_aor == NULL || parent_aor == NULL)
				goto err;

			if (!IPAddressFamily_afi_length(child_af, &length))
				goto err;

			/* Now check containment and replace or error. */
			if (addr_contains(parent_aor, child_aor, length)) {
				sk_IPAddressFamily_set(child, i, parent_af);
				continue;
			}

			if ((ret = verify_error(ctx, cert,
			    X509_V_ERR_UNNESTED_RESOURCE, depth)) == 0)
				goto done;
		}
	}

	/*
	 * Trust anchor can't inherit.
	 */
	if ((parent = cert->rfc3779_addr) != NULL) {
		for (i = 0; i < sk_IPAddressFamily_num(parent); i++) {
			parent_af = sk_IPAddressFamily_value(parent, i);

			if (IPAddressFamily_inheritance(parent_af) == NULL)
				continue;

			if ((ret = verify_error(ctx, cert,
			    X509_V_ERR_UNNESTED_RESOURCE, depth)) == 0)
				goto done;
		}
	}

 done:
	sk_IPAddressFamily_free(child);
	return ret;

 err:
	sk_IPAddressFamily_free(child);

	if (ctx != NULL)
		ctx->error = X509_V_ERR_UNSPECIFIED;

	return 0;
}

/*
 * RFC 3779 2.3 path validation -- called from X509_verify_cert().
 */
int
X509v3_addr_validate_path(X509_STORE_CTX *ctx)
{
	if (sk_X509_num(ctx->chain) <= 0 || ctx->verify_cb == NULL) {
		ctx->error = X509_V_ERR_UNSPECIFIED;
		return 0;
	}
	return addr_validate_path_internal(ctx, ctx->chain, NULL);
}
LCRYPTO_ALIAS(X509v3_addr_validate_path);

/*
 * RFC 3779 2.3 path validation of an extension.
 * Test whether chain covers extension.
 */
int
X509v3_addr_validate_resource_set(STACK_OF(X509) *chain, IPAddrBlocks *ext,
    int allow_inheritance)
{
	if (ext == NULL)
		return 1;
	if (sk_X509_num(chain) <= 0)
		return 0;
	if (!allow_inheritance && X509v3_addr_inherits(ext))
		return 0;
	return addr_validate_path_internal(NULL, chain, ext);
}
LCRYPTO_ALIAS(X509v3_addr_validate_resource_set);

#endif /* OPENSSL_NO_RFC3779 */
