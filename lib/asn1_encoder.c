// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple encoder primitives for ASN.1 BER/DER/CER
 *
 * Copyright (C) 2019 James.Bottomley@HansenPartnership.com
 */

#include <linux/asn1_encoder.h>
#include <linux/bug.h>
#include <linux/string.h>
#include <linux/module.h>

/**
 * asn1_encode_integer() - encode positive integer to ASN.1
 * @data:	pointer to the pointer to the data
 * @end_data:	end of data pointer, points one beyond last usable byte in @data
 * @integer:	integer to be encoded
 *
 * This is a simplified encoder: it only currently does
 * positive integers, but it should be simple enough to add the
 * negative case if a use comes along.
 */
unsigned char *
asn1_encode_integer(unsigned char *data, const unsigned char *end_data,
		    s64 integer)
{
	int data_len = end_data - data;
	unsigned char *d = &data[2];
	bool found = false;
	int i;

	if (WARN(integer < 0,
		 "BUG: integer encode only supports positive integers"))
		return ERR_PTR(-EINVAL);

	if (IS_ERR(data))
		return data;

	/* need at least 3 bytes for tag, length and integer encoding */
	if (data_len < 3)
		return ERR_PTR(-EINVAL);

	/* remaining length where at d (the start of the integer encoding) */
	data_len -= 2;

	data[0] = _tag(UNIV, PRIM, INT);
	if (integer == 0) {
		*d++ = 0;
		goto out;
	}

	for (i = sizeof(integer); i > 0 ; i--) {
		int byte = integer >> (8 * (i - 1));

		if (!found && byte == 0)
			continue;

		/*
		 * for a positive number the first byte must have bit
		 * 7 clear in two's complement (otherwise it's a
		 * negative number) so prepend a leading zero if
		 * that's not the case
		 */
		if (!found && (byte & 0x80)) {
			/*
			 * no check needed here, we already know we
			 * have len >= 1
			 */
			*d++ = 0;
			data_len--;
		}

		found = true;
		if (data_len == 0)
			return ERR_PTR(-EINVAL);

		*d++ = byte;
		data_len--;
	}

 out:
	data[1] = d - data - 2;

	return d;
}
EXPORT_SYMBOL_GPL(asn1_encode_integer);

/* calculate the base 128 digit values setting the top bit of the first octet */
static int asn1_encode_oid_digit(unsigned char **_data, int *data_len, u32 oid)
{
	unsigned char *data = *_data;
	int start = 7 + 7 + 7 + 7;
	int ret = 0;

	if (*data_len < 1)
		return -EINVAL;

	/* quick case */
	if (oid == 0) {
		*data++ = 0x80;
		(*data_len)--;
		goto out;
	}

	while (oid >> start == 0)
		start -= 7;

	while (start > 0 && *data_len > 0) {
		u8 byte;

		byte = oid >> start;
		oid = oid - (byte << start);
		start -= 7;
		byte |= 0x80;
		*data++ = byte;
		(*data_len)--;
	}

	if (*data_len > 0) {
		*data++ = oid;
		(*data_len)--;
	} else {
		ret = -EINVAL;
	}

 out:
	*_data = data;
	return ret;
}

/**
 * asn1_encode_oid() - encode an oid to ASN.1
 * @data:	position to begin encoding at
 * @end_data:	end of data pointer, points one beyond last usable byte in @data
 * @oid:	array of oids
 * @oid_len:	length of oid array
 *
 * this encodes an OID up to ASN.1 when presented as an array of OID values
 */
unsigned char *
asn1_encode_oid(unsigned char *data, const unsigned char *end_data,
		u32 oid[], int oid_len)
{
	int data_len = end_data - data;
	unsigned char *d = data + 2;
	int i, ret;

	if (WARN(oid_len < 2, "OID must have at least two elements"))
		return ERR_PTR(-EINVAL);

	if (WARN(oid_len > 32, "OID is too large"))
		return ERR_PTR(-EINVAL);

	if (IS_ERR(data))
		return data;


	/* need at least 3 bytes for tag, length and OID encoding */
	if (data_len < 3)
		return ERR_PTR(-EINVAL);

	data[0] = _tag(UNIV, PRIM, OID);
	*d++ = oid[0] * 40 + oid[1];

	data_len -= 3;

	for (i = 2; i < oid_len; i++) {
		ret = asn1_encode_oid_digit(&d, &data_len, oid[i]);
		if (ret < 0)
			return ERR_PTR(ret);
	}

	data[1] = d - data - 2;

	return d;
}
EXPORT_SYMBOL_GPL(asn1_encode_oid);

/**
 * asn1_encode_length() - encode a length to follow an ASN.1 tag
 * @data: pointer to encode at
 * @data_len: pointer to remaining length (adjusted by routine)
 * @len: length to encode
 *
 * This routine can encode lengths up to 65535 using the ASN.1 rules.
 * It will accept a negative length and place a zero length tag
 * instead (to keep the ASN.1 valid).  This convention allows other
 * encoder primitives to accept negative lengths as singalling the
 * sequence will be re-encoded when the length is known.
 */
static int asn1_encode_length(unsigned char **data, int *data_len, int len)
{
	if (*data_len < 1)
		return -EINVAL;

	if (len < 0) {
		*((*data)++) = 0;
		(*data_len)--;
		return 0;
	}

	if (len <= 0x7f) {
		*((*data)++) = len;
		(*data_len)--;
		return 0;
	}

	if (*data_len < 2)
		return -EINVAL;

	if (len <= 0xff) {
		*((*data)++) = 0x81;
		*((*data)++) = len & 0xff;
		*data_len -= 2;
		return 0;
	}

	if (*data_len < 3)
		return -EINVAL;

	if (len <= 0xffff) {
		*((*data)++) = 0x82;
		*((*data)++) = (len >> 8) & 0xff;
		*((*data)++) = len & 0xff;
		*data_len -= 3;
		return 0;
	}

	if (WARN(len > 0xffffff, "ASN.1 length can't be > 0xffffff"))
		return -EINVAL;

	if (*data_len < 4)
		return -EINVAL;
	*((*data)++) = 0x83;
	*((*data)++) = (len >> 16) & 0xff;
	*((*data)++) = (len >> 8) & 0xff;
	*((*data)++) = len & 0xff;
	*data_len -= 4;

	return 0;
}

/**
 * asn1_encode_tag() - add a tag for optional or explicit value
 * @data:	pointer to place tag at
 * @end_data:	end of data pointer, points one beyond last usable byte in @data
 * @tag:	tag to be placed
 * @string:	the data to be tagged
 * @len:	the length of the data to be tagged
 *
 * Note this currently only handles short form tags < 31.
 *
 * Standard usage is to pass in a @tag, @string and @length and the
 * @string will be ASN.1 encoded with @tag and placed into @data.  If
 * the encoding would put data past @end_data then an error is
 * returned, otherwise a pointer to a position one beyond the encoding
 * is returned.
 *
 * To encode in place pass a NULL @string and -1 for @len and the
 * maximum allowable beginning and end of the data; all this will do
 * is add the current maximum length and update the data pointer to
 * the place where the tag contents should be placed is returned.  The
 * data should be copied in by the calling routine which should then
 * repeat the prior statement but now with the known length.  In order
 * to avoid having to keep both before and after pointers, the repeat
 * expects to be called with @data pointing to where the first encode
 * returned it and still NULL for @string but the real length in @len.
 */
unsigned char *
asn1_encode_tag(unsigned char *data, const unsigned char *end_data,
		u32 tag, const unsigned char *string, int len)
{
	int data_len = end_data - data;
	int ret;

	if (WARN(tag > 30, "ASN.1 tag can't be > 30"))
		return ERR_PTR(-EINVAL);

	if (!string && WARN(len > 127,
			    "BUG: recode tag is too big (>127)"))
		return ERR_PTR(-EINVAL);

	if (IS_ERR(data))
		return data;

	if (!string && len > 0) {
		/*
		 * we're recoding, so move back to the start of the
		 * tag and install a dummy length because the real
		 * data_len should be NULL
		 */
		data -= 2;
		data_len = 2;
	}

	if (data_len < 2)
		return ERR_PTR(-EINVAL);

	*(data++) = _tagn(CONT, CONS, tag);
	data_len--;
	ret = asn1_encode_length(&data, &data_len, len);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!string)
		return data;

	if (data_len < len)
		return ERR_PTR(-EINVAL);

	memcpy(data, string, len);
	data += len;

	return data;
}
EXPORT_SYMBOL_GPL(asn1_encode_tag);

/**
 * asn1_encode_octet_string() - encode an ASN.1 OCTET STRING
 * @data:	pointer to encode at
 * @end_data:	end of data pointer, points one beyond last usable byte in @data
 * @string:	string to be encoded
 * @len:	length of string
 *
 * Note ASN.1 octet strings may contain zeros, so the length is obligatory.
 */
unsigned char *
asn1_encode_octet_string(unsigned char *data,
			 const unsigned char *end_data,
			 const unsigned char *string, u32 len)
{
	int data_len = end_data - data;
	int ret;

	if (IS_ERR(data))
		return data;

	/* need minimum of 2 bytes for tag and length of zero length string */
	if (data_len < 2)
		return ERR_PTR(-EINVAL);

	*(data++) = _tag(UNIV, PRIM, OTS);
	data_len--;

	ret = asn1_encode_length(&data, &data_len, len);
	if (ret)
		return ERR_PTR(ret);

	if (data_len < len)
		return ERR_PTR(-EINVAL);

	memcpy(data, string, len);
	data += len;

	return data;
}
EXPORT_SYMBOL_GPL(asn1_encode_octet_string);

/**
 * asn1_encode_sequence() - wrap a byte stream in an ASN.1 SEQUENCE
 * @data:	pointer to encode at
 * @end_data:	end of data pointer, points one beyond last usable byte in @data
 * @seq:	data to be encoded as a sequence
 * @len:	length of the data to be encoded as a sequence
 *
 * Fill in a sequence.  To encode in place, pass NULL for @seq and -1
 * for @len; then call again once the length is known (still with NULL
 * for @seq). In order to avoid having to keep both before and after
 * pointers, the repeat expects to be called with @data pointing to
 * where the first encode placed it.
 */
unsigned char *
asn1_encode_sequence(unsigned char *data, const unsigned char *end_data,
		     const unsigned char *seq, int len)
{
	int data_len = end_data - data;
	int ret;

	if (!seq && WARN(len > 127,
			 "BUG: recode sequence is too big (>127)"))
		return ERR_PTR(-EINVAL);

	if (IS_ERR(data))
		return data;

	if (!seq && len >= 0) {
		/*
		 * we're recoding, so move back to the start of the
		 * sequence and install a dummy length because the
		 * real length should be NULL
		 */
		data -= 2;
		data_len = 2;
	}

	if (data_len < 2)
		return ERR_PTR(-EINVAL);

	*(data++) = _tag(UNIV, CONS, SEQ);
	data_len--;

	ret = asn1_encode_length(&data, &data_len, len);
	if (ret)
		return ERR_PTR(ret);

	if (!seq)
		return data;

	if (data_len < len)
		return ERR_PTR(-EINVAL);

	memcpy(data, seq, len);
	data += len;

	return data;
}
EXPORT_SYMBOL_GPL(asn1_encode_sequence);

/**
 * asn1_encode_boolean() - encode a boolean value to ASN.1
 * @data:	pointer to encode at
 * @end_data:	end of data pointer, points one beyond last usable byte in @data
 * @val:	the boolean true/false value
 */
unsigned char *
asn1_encode_boolean(unsigned char *data, const unsigned char *end_data,
		    bool val)
{
	int data_len = end_data - data;

	if (IS_ERR(data))
		return data;

	/* booleans are 3 bytes: tag, length == 1 and value == 0 or 1 */
	if (data_len < 3)
		return ERR_PTR(-EINVAL);

	*(data++) = _tag(UNIV, PRIM, BOOL);
	data_len--;

	asn1_encode_length(&data, &data_len, 1);

	if (val)
		*(data++) = 1;
	else
		*(data++) = 0;

	return data;
}
EXPORT_SYMBOL_GPL(asn1_encode_boolean);

MODULE_LICENSE("GPL");
