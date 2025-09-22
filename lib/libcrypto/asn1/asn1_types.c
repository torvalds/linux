/* $OpenBSD: asn1_types.c,v 1.3 2023/07/05 21:23:36 beck Exp $ */
/*
 * Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
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

#include <stddef.h>

#include <openssl/asn1.h>

#define ASN1_ENCODING_CONSTRUCTED_ONLY	1
#define ASN1_ENCODING_PRIMITIVE_ONLY	2

struct asn1_type {
	const char *name;
	uint32_t bit_value;
	int char_width;
	int encoding;
};

/*
 * Universal class tag types - ITU X.680.
 */
static const struct asn1_type asn1_types[31] = {
	[0] = {
		/* Tag 0 (0x00) - Reserved for use by encoding rules */
		.name = "EOC",
		.bit_value = 0,
		.char_width = -1,
	},
	[1] = {
		/* Tag 1 (0x01) - Boolean */
		.name = "BOOLEAN",
		.bit_value = 0,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[2] = {
		/* Tag 2 (0x02) - Integer */
		.name = "INTEGER",
		.bit_value = 0,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[3] = {
		/* Tag 3 (0x03) - BitString */
		.name = "BIT STRING",
		.bit_value = B_ASN1_BIT_STRING,
		.char_width = -1,
	},
	[4] = {
		/* Tag 4 (0x04) - OctetString */
		.name = "OCTET STRING",
		.bit_value = B_ASN1_OCTET_STRING,
		.char_width = -1,
	},
	[5] = {
		/* Tag 5 (0x05) - Null */
		.name = "NULL",
		.bit_value = 0,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[6] = {
		/* Tag 6 (0x06) - Object Identifier */
		.name = "OBJECT",
		.bit_value = 0,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[7] = {
		/* Tag 7 (0x07) - Object Descriptor */
		.name = "OBJECT DESCRIPTOR",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
	},
	[8] = {
		/* Tag 8 (0x08) - External */
		.name = "EXTERNAL",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
	},
	[9] = {
		/* Tag 9 (0x09) - Real */
		.name = "REAL",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[10] = {
		/* Tag 10 (0x0a) - Enumerated */
		.name = "ENUMERATED",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[11] = {
		/* Tag 11 (0x0b) - Embedded PDV */
		.name = "<ASN1 11 EMBEDDED PDV>",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
	},
	[12] = {
		/* Tag 12 (0x0c) - UTF8String */
		.name = "UTF8STRING",
		.bit_value = B_ASN1_UTF8STRING,
		.char_width = 0,
	},
	[13] = {
		/* Tag 13 (0x0d) - Relative Object Identifier */
		.name = "<ASN1 13 RELATIVE OID>",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[14] = {
		/* Tag 14 (0x0e) - Time */
		.name = "<ASN1 14 TIME>",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
		.encoding = ASN1_ENCODING_PRIMITIVE_ONLY,
	},
	[15] = {
		/* Tag 15 (0x0f) - Reserved */
		.name = "<ASN1 15 RESERVED>",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
	},
	[16] = {
		/* Tag 16 (0x10)- Sequence */
		.name = "SEQUENCE",
		.bit_value = B_ASN1_SEQUENCE,
		.char_width = -1,
		.encoding = ASN1_ENCODING_CONSTRUCTED_ONLY,
	},
	[17] = {
		/* Tag 17 (0x11) - Set */
		.name = "SET",
		.bit_value = 0,
		.char_width = -1,
		.encoding = ASN1_ENCODING_CONSTRUCTED_ONLY,
	},
	[18] = {
		/* Tag 18 (0x12) - NumericString */
		.name = "NUMERICSTRING",
		.bit_value = B_ASN1_NUMERICSTRING,
		.char_width = -1,
	},
	[19] = {
		/* Tag 19 (0x13) - PrintableString */
		.name = "PRINTABLESTRING",
		.bit_value = B_ASN1_PRINTABLESTRING,
		.char_width = 1,
	},
	[20] = {
		/* Tag 20 (0x14) - TeletexString (T61String) */
		.name = "T61STRING",
		.bit_value = B_ASN1_T61STRING,
		.char_width = 1,
	},
	[21] = {
		/* Tag 21 (0x15) - VideotexString */
		.name = "VIDEOTEXSTRING",
		.bit_value = B_ASN1_VIDEOTEXSTRING,
		.char_width = -1,
	},
	[22] = {
		/* Tag 22 (0x16) - IA5String */
		.name = "IA5STRING",
		.bit_value = B_ASN1_IA5STRING,
		.char_width = 1,
	},
	[23] = {
		/* Tag 23 (0x17) - UTCTime */
		.name = "UTCTIME",
		.bit_value = B_ASN1_UTCTIME,
		.char_width = 1,
	},
	[24] = {
		/* Tag 24 (0x18) - GeneralizedTime */
		.name = "GENERALIZEDTIME",
		.bit_value = B_ASN1_GENERALIZEDTIME,
		.char_width = 1,
	},
	[25] = {
		/* Tag 25 (0x19) - GraphicString */
		.name = "GRAPHICSTRING",
		.bit_value = B_ASN1_GRAPHICSTRING,
		.char_width = -1,
	},
	[26] = {
		/* Tag 26 (0x1a) - VisibleString (ISO646String) */
		.name = "VISIBLESTRING",
		.bit_value = B_ASN1_ISO64STRING,
		.char_width = 1,
	},
	[27] = {
		/* Tag 27 (0x1b) - GeneralString */
		.name = "GENERALSTRING",
		.bit_value = B_ASN1_GENERALSTRING,
		.char_width = -1,
	},
	[28] = {
		/* Tag 28 (0x1c) - UniversalString */
		.name = "UNIVERSALSTRING",
		.bit_value = B_ASN1_UNIVERSALSTRING,
		.char_width = 4,
	},
	[29] = {
		/* Tag 29 (0x1d) - Unallocated */
		.name = "<ASN1 29>",
		.bit_value = B_ASN1_UNKNOWN,
		.char_width = -1,
	},
	[30] = {
		/* Tag 30 (0x1e) - BMPString */
		.name = "BMPSTRING",
		.bit_value = B_ASN1_BMPSTRING,
		.char_width = 2,
	},
};

static const struct asn1_type *
asn1_type_by_tag(int tag)
{
	if (tag < 0 || tag > 30)
		return NULL;

	return &asn1_types[tag];
}

int
asn1_must_be_constructed(int tag)
{
	const struct asn1_type *at;

	if (tag == V_ASN1_NEG_INTEGER || tag == V_ASN1_NEG_ENUMERATED)
		tag &= ~V_ASN1_NEG;
	if ((at = asn1_type_by_tag(tag)) != NULL)
		return at->encoding == ASN1_ENCODING_CONSTRUCTED_ONLY;

	return 0;
}

int
asn1_must_be_primitive(int tag)
{
	const struct asn1_type *at;

	if (tag == V_ASN1_NEG_INTEGER || tag == V_ASN1_NEG_ENUMERATED)
		tag &= ~V_ASN1_NEG;
	if ((at = asn1_type_by_tag(tag)) != NULL)
		return at->encoding == ASN1_ENCODING_PRIMITIVE_ONLY;

	return 0;
}

int
asn1_tag2charwidth(int tag)
{
	const struct asn1_type *at;

	if ((at = asn1_type_by_tag(tag)) != NULL)
		return at->char_width;

	return -1;
}

unsigned long
ASN1_tag2bit(int tag)
{
	const struct asn1_type *at;

	if ((at = asn1_type_by_tag(tag)) != NULL)
		return (unsigned long)at->bit_value;

	return 0;
}
LCRYPTO_ALIAS(ASN1_tag2bit);

const char *
ASN1_tag2str(int tag)
{
	const struct asn1_type *at;

	if (tag == V_ASN1_NEG_INTEGER || tag == V_ASN1_NEG_ENUMERATED)
		tag &= ~V_ASN1_NEG;

	if ((at = asn1_type_by_tag(tag)) != NULL)
		return at->name;

	return "(unknown)";
}
LCRYPTO_ALIAS(ASN1_tag2str);
