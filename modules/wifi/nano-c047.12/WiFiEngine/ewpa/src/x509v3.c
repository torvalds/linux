/*
 * X.509v3 certificate parsing and processing (RFC 3280 profile)
 * Copyright (c) 2006-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"

#ifdef CONFIG_INTERNAL_X509

#include "asn1.h"
#include "crypto.h"
#include "x509v3.h"


static void x509_free_name(struct x509_name *name)
{
	os_free(name->cn);
	os_free(name->c);
	os_free(name->l);
	os_free(name->st);
	os_free(name->o);
	os_free(name->ou);
	os_free(name->email);
	name->cn = name->c = name->l = name->st = name->o = name->ou = NULL;
	name->email = NULL;
}


/**
 * x509_certificate_free - Free an X.509 certificate
 * @cert: Certificate to be freed
 */
void x509_certificate_free(struct x509_certificate *cert)
{
	if (cert == NULL)
		return;
	if (cert->next) {
		wpa_printf(MSG_DEBUG, "X509: x509_certificate_free: cer=%p "
			   "was still on a list (next=%p)\n",
			   cert, cert->next);
	}
	x509_free_name(&cert->issuer);
	x509_free_name(&cert->subject);
	os_free(cert->public_key);
	os_free(cert->sign_value);
	os_free(cert);
}


/**
 * x509_certificate_free - Free an X.509 certificate chain
 * @cert: Pointer to the first certificate in the chain
 */
void x509_certificate_chain_free(struct x509_certificate *cert)
{
	struct x509_certificate *next;

	while (cert) {
		next = cert->next;
		cert->next = NULL;
		x509_certificate_free(cert);
		cert = next;
	}
}


static int x509_whitespace(char c)
{
	return c == ' ' || c == '\t';
}


static void x509_str_strip_whitespace(char *a)
{
	char *ipos, *opos;
	int remove_whitespace = 1;

	ipos = opos = a;

	while (*ipos) {
		if (remove_whitespace && x509_whitespace(*ipos))
			ipos++;
		else {
			remove_whitespace = x509_whitespace(*ipos);
			*opos++ = *ipos++;
		}
	}

	*opos-- = '\0';
	if (opos > a && x509_whitespace(*opos))
		*opos = '\0';
}


static int x509_str_compare(const char *a, const char *b)
{
	char *aa, *bb;
	int ret;

	if (!a && b)
		return -1;
	if (a && !b)
		return 1;
	if (!a && !b)
		return 0;

	aa = os_strdup(a);
	bb = os_strdup(b);

	if (aa == NULL || bb == NULL) {
		os_free(aa);
		os_free(bb);
		return os_strcasecmp(a, b);
	}

	x509_str_strip_whitespace(aa);
	x509_str_strip_whitespace(bb);

	ret = os_strcasecmp(aa, bb);

	os_free(aa);
	os_free(bb);

	return ret;
}


/**
 * x509_name_compare - Compare X.509 certificate names
 * @a: Certificate name
 * @b: Certificate name
 * Returns: <0, 0, or >0 based on whether a is less than, equal to, or
 * greater than b
 */
int x509_name_compare(struct x509_name *a, struct x509_name *b)
{
	int res;

	if (!a && b)
		return -1;
	if (a && !b)
		return 1;
	if (!a && !b)
		return 0;

	res = x509_str_compare(a->cn, b->cn);
	if (res)
		return res;
	res = x509_str_compare(a->c, b->c);
	if (res)
		return res;
	res = x509_str_compare(a->l, b->l);
	if (res)
		return res;
	res = x509_str_compare(a->st, b->st);
	if (res)
		return res;
	res = x509_str_compare(a->o, b->o);
	if (res)
		return res;
	res = x509_str_compare(a->ou, b->ou);
	if (res)
		return res;
	res = x509_str_compare(a->email, b->email);
	if (res)
		return res;

	return 0;
}


static int x509_parse_algorithm_identifier(
	const u8 *buf, size_t len,
	struct x509_algorithm_identifier *id, const u8 **next)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;

	/*
	 * AlgorithmIdentifier ::= SEQUENCE {
	 *     algorithm            OBJECT IDENTIFIER,
	 *     parameters           ANY DEFINED BY algorithm OPTIONAL
	 * }
	 */

	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
			   "(AlgorithmIdentifier) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	end = pos + hdr.length;

	if (end > buf + len)
		return -1;

	*next = end;

	if (asn1_get_oid(pos, end - pos, &id->oid, &pos))
		return -1;

	/* TODO: optional parameters */

	return 0;
}


static int x509_parse_public_key(const u8 *buf, size_t len,
				 struct x509_certificate *cert,
				 const u8 **next)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;

	/*
	 * SubjectPublicKeyInfo ::= SEQUENCE {
	 *     algorithm            AlgorithmIdentifier,
	 *     subjectPublicKey     BIT STRING
	 * }
	 */

	pos = buf;
	end = buf + len;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
			   "(SubjectPublicKeyInfo) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;

	if (pos + hdr.length > end)
		return -1;
	end = pos + hdr.length;
	*next = end;

	if (x509_parse_algorithm_identifier(pos, end - pos,
					    &cert->public_key_alg, &pos))
		return -1;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_BITSTRING) {
		wpa_printf(MSG_DEBUG, "X509: Expected BITSTRING "
			   "(subjectPublicKey) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	if (hdr.length < 1)
		return -1;
	pos = hdr.payload;
	if (*pos) {
		wpa_printf(MSG_DEBUG, "X509: BITSTRING - %d unused bits",
			   *pos);
		/*
		 * TODO: should this be rejected? X.509 certificates are
		 * unlikely to use such a construction. Now we would end up
		 * including the extra bits in the buffer which may also be
		 * ok.
		 */
	}
	os_free(cert->public_key);
	cert->public_key = os_malloc(hdr.length - 1);
	if (cert->public_key == NULL) {
		wpa_printf(MSG_DEBUG, "X509: Failed to allocate memory for "
			   "public key");
		return -1;
	}
	os_memcpy(cert->public_key, pos + 1, hdr.length - 1);
	cert->public_key_len = hdr.length - 1;
	wpa_hexdump(MSG_MSGDUMP, "X509: subjectPublicKey",
		    cert->public_key, cert->public_key_len);

	return 0;
}


static int x509_parse_name(const u8 *buf, size_t len, struct x509_name *name,
			   const u8 **next)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end, *set_pos, *set_end, *seq_pos, *seq_end;
	struct asn1_oid oid;
	char **fieldp;

	/*
	 * Name ::= CHOICE { RDNSequence }
	 * RDNSequence ::= SEQUENCE OF RelativeDistinguishedName
	 * RelativeDistinguishedName ::= SET OF AttributeTypeAndValue
	 * AttributeTypeAndValue ::= SEQUENCE {
	 *     type     AttributeType,
	 *     value    AttributeValue
	 * }
	 * AttributeType ::= OBJECT IDENTIFIER
	 * AttributeValue ::= ANY DEFINED BY AttributeType
	 */

	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
			   "(Name / RDNSequencer) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;

	if (pos + hdr.length > buf + len)
		return -1;

	end = *next = pos + hdr.length;

	while (pos < end) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_SET) {
			wpa_printf(MSG_DEBUG, "X509: Expected SET "
				   "(RelativeDistinguishedName) - found class "
				   "%d tag 0x%x", hdr.class, hdr.tag);
			x509_free_name(name);
			return -1;
		}

		set_pos = hdr.payload;
		pos = set_end = hdr.payload + hdr.length;

		if (asn1_get_next(set_pos, set_end - set_pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_SEQUENCE) {
			wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
				   "(AttributeTypeAndValue) - found class %d "
				   "tag 0x%x", hdr.class, hdr.tag);
			x509_free_name(name);
			return -1;
		}

		seq_pos = hdr.payload;
		seq_end = hdr.payload + hdr.length;

		if (asn1_get_oid(seq_pos, seq_end - seq_pos, &oid, &seq_pos)) {
			x509_free_name(name);
			return -1;
		}

		if (asn1_get_next(seq_pos, seq_end - seq_pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL) {
			wpa_printf(MSG_DEBUG, "X509: Failed to parse "
				   "AttributeValue");
			x509_free_name(name);
			return -1;
		}

		/* RFC 3280:
		 * MUST: country, organization, organizational-unit,
		 * distinguished name qualifier, state or province name,
		 * common name, serial number.
		 * SHOULD: locality, title, surname, given name, initials,
		 * pseudonym, generation qualifier.
		 * MUST: domainComponent (RFC 2247).
		 */
		fieldp = NULL;
		if (oid.len == 4 &&
		    oid.oid[0] == 2 && oid.oid[1] == 5 && oid.oid[2] == 4) {
			/* id-at ::= 2.5.4 */
			switch (oid.oid[3]) {
			case 3:
				/* commonName */
				fieldp = &name->cn;
				break;
			case 6:
				/*  countryName */
				fieldp = &name->c;
				break;
			case 7:
				/* localityName */
				fieldp = &name->l;
				break;
			case 8:
				/* stateOrProvinceName */
				fieldp = &name->st;
				break;
			case 10:
				/* organizationName */
				fieldp = &name->o;
				break;
			case 11:
				/* organizationalUnitName */
				fieldp = &name->ou;
				break;
			}
		} else if (oid.len == 7 &&
			   oid.oid[0] == 1 && oid.oid[1] == 2 &&
			   oid.oid[2] == 840 && oid.oid[3] == 113549 &&
			   oid.oid[4] == 1 && oid.oid[5] == 9 &&
			   oid.oid[6] == 1) {
			/* 1.2.840.113549.1.9.1 - e-mailAddress */
			fieldp = &name->email;
		}

		if (fieldp == NULL) {
			wpa_hexdump(MSG_DEBUG, "X509: Unrecognized OID",
				    (u8 *) oid.oid,
				    oid.len * sizeof(oid.oid[0]));
			wpa_hexdump_ascii(MSG_MSGDUMP, "X509: Attribute Data",
					  hdr.payload, hdr.length);
			continue;
		}

		os_free(*fieldp);
		*fieldp = os_malloc(hdr.length + 1);
		if (*fieldp == NULL) {
			x509_free_name(name);
			return -1;
		}
		os_memcpy(*fieldp, hdr.payload, hdr.length);
		(*fieldp)[hdr.length] = '\0';
	}

	return 0;
}


/**
 * x509_name_string - Convert an X.509 certificate name into a string
 * @name: Name to convert
 * @buf: Buffer for the string
 * @len: Maximum buffer length
 */
void x509_name_string(struct x509_name *name, char *buf, size_t len)
{
	char *pos, *end;
	int ret;

	if (len == 0)
		return;

	pos = buf;
	end = buf + len;

	if (name->c) {
		ret = os_snprintf(pos, end - pos, "C=%s, ", name->c);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}
	if (name->st) {
		ret = os_snprintf(pos, end - pos, "ST=%s, ", name->st);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}
	if (name->l) {
		ret = os_snprintf(pos, end - pos, "L=%s, ", name->l);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}
	if (name->o) {
		ret = os_snprintf(pos, end - pos, "O=%s, ", name->o);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}
	if (name->ou) {
		ret = os_snprintf(pos, end - pos, "OU=%s, ", name->ou);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}
	if (name->cn) {
		ret = os_snprintf(pos, end - pos, "CN=%s, ", name->cn);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}

	if (pos > buf + 1 && pos[-1] == ' ' && pos[-2] == ',') {
		*pos-- = '\0';
		*pos-- = '\0';
	}

	if (name->email) {
		ret = os_snprintf(pos, end - pos, "/emailAddress=%s",
				  name->email);
		if (ret < 0 || ret >= end - pos)
			goto done;
		pos += ret;
	}

done:
	end[-1] = '\0';
}


static int x509_parse_time(const u8 *buf, size_t len, u8 asn1_tag,
			   os_time_t *val)
{
	const char *pos;
	int year, month, day, hour, min, sec;

	/*
	 * Time ::= CHOICE {
	 *     utcTime        UTCTime,
	 *     generalTime    GeneralizedTime
	 * }
	 *
	 * UTCTime: YYMMDDHHMMSSZ
	 * GeneralizedTime: YYYYMMDDHHMMSSZ
	 */

	pos = (const char *) buf;

	switch (asn1_tag) {
	case ASN1_TAG_UTCTIME:
		if (len != 13 || buf[12] != 'Z') {
			wpa_hexdump_ascii(MSG_DEBUG, "X509: Unrecognized "
					  "UTCTime format", buf, len);
			return -1;
		}
		if (sscanf(pos, "%02d", &year) != 1) {
			wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse "
					  "UTCTime year", buf, len);
			return -1;
		}
		if (year < 50)
			year += 2000;
		else
			year += 1900;
		pos += 2;
		break;
	case ASN1_TAG_GENERALIZEDTIME:
		if (len != 15 || buf[14] != 'Z') {
			wpa_hexdump_ascii(MSG_DEBUG, "X509: Unrecognized "
					  "GeneralizedTime format", buf, len);
			return -1;
		}
		if (sscanf(pos, "%04d", &year) != 1) {
			wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse "
					  "GeneralizedTime year", buf, len);
			return -1;
		}
		pos += 4;
		break;
	default:
		wpa_printf(MSG_DEBUG, "X509: Expected UTCTime or "
			   "GeneralizedTime - found tag 0x%x", asn1_tag);
		return -1;
	}

	if (sscanf(pos, "%02d", &month) != 1) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse Time "
				  "(month)", buf, len);
		return -1;
	}
	pos += 2;

	if (sscanf(pos, "%02d", &day) != 1) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse Time "
				  "(day)", buf, len);
		return -1;
	}
	pos += 2;

	if (sscanf(pos, "%02d", &hour) != 1) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse Time "
				  "(hour)", buf, len);
		return -1;
	}
	pos += 2;

	if (sscanf(pos, "%02d", &min) != 1) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse Time "
				  "(min)", buf, len);
		return -1;
	}
	pos += 2;

	if (sscanf(pos, "%02d", &sec) != 1) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse Time "
				  "(sec)", buf, len);
		return -1;
	}

	if (os_mktime(year, month, day, hour, min, sec, val) < 0) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to convert Time",
				  buf, len);
		if (year < 1970) {
			/*
			 * At least some test certificates have been configured
			 * to use dates prior to 1970. Set the date to
			 * beginning of 1970 to handle these case.
			 */
			wpa_printf(MSG_DEBUG, "X509: Year=%d before epoch - "
				   "assume epoch as the time", year);
			*val = 0;
			return 0;
		}
		return -1;
	}

	return 0;
}


static int x509_parse_validity(const u8 *buf, size_t len,
			       struct x509_certificate *cert, const u8 **next)
{
	struct asn1_hdr hdr;
	const u8 *pos;
	size_t plen;

	/*
	 * Validity ::= SEQUENCE {
	 *     notBefore      Time,
	 *     notAfter       Time
	 * }
	 *
	 * RFC 3280, 4.1.2.5:
	 * CAs conforming to this profile MUST always encode certificate
	 * validity dates through the year 2049 as UTCTime; certificate
	 * validity dates in 2050 or later MUST be encoded as GeneralizedTime.
	 */

	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
			   "(Validity) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	plen = hdr.length;

	if (pos + plen > buf + len)
		return -1;

	*next = pos + plen;

	if (asn1_get_next(pos, plen, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    x509_parse_time(hdr.payload, hdr.length, hdr.tag,
			    &cert->not_before) < 0) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse notBefore "
				  "Time", hdr.payload, hdr.length);
		return -1;
	}

	pos = hdr.payload + hdr.length;
	plen = *next - pos;

	if (asn1_get_next(pos, plen, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    x509_parse_time(hdr.payload, hdr.length, hdr.tag,
			    &cert->not_after) < 0) {
		wpa_hexdump_ascii(MSG_DEBUG, "X509: Failed to parse notAfter "
				  "Time", hdr.payload, hdr.length);
		return -1;
	}

	wpa_printf(MSG_MSGDUMP, "X509: Validity: notBefore: %lu notAfter: %lu",
		   (unsigned long) cert->not_before,
		   (unsigned long) cert->not_after);

	return 0;
}


static int x509_id_ce_oid(struct asn1_oid *oid)
{
	/* id-ce arc from X.509 for standard X.509v3 extensions */
	return oid->len >= 4 &&
		oid->oid[0] == 2 /* joint-iso-ccitt */ &&
		oid->oid[1] == 5 /* ds */ &&
		oid->oid[2] == 29 /* id-ce */;
}


static int x509_parse_ext_key_usage(struct x509_certificate *cert,
				    const u8 *pos, size_t len)
{
	struct asn1_hdr hdr;

	/*
	 * KeyUsage ::= BIT STRING {
	 *     digitalSignature        (0),
	 *     nonRepudiation          (1),
	 *     keyEncipherment         (2),
	 *     dataEncipherment        (3),
	 *     keyAgreement            (4),
	 *     keyCertSign             (5),
	 *     cRLSign                 (6),
	 *     encipherOnly            (7),
	 *     decipherOnly            (8) }
	 */

	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_BITSTRING ||
	    hdr.length < 1) {
		wpa_printf(MSG_DEBUG, "X509: Expected BIT STRING in "
			   "KeyUsage; found %d tag 0x%x len %d",
			   hdr.class, hdr.tag, hdr.length);
		return -1;
	}

	cert->extensions_present |= X509_EXT_KEY_USAGE;
	cert->key_usage = asn1_bit_string_to_long(hdr.payload, hdr.length);

	wpa_printf(MSG_DEBUG, "X509: KeyUsage 0x%lx", cert->key_usage);

	return 0;
}


static int x509_parse_ext_basic_constraints(struct x509_certificate *cert,
					    const u8 *pos, size_t len)
{
	struct asn1_hdr hdr;
	unsigned long value;
	size_t left;

	/*
	 * BasicConstraints ::= SEQUENCE {
	 * cA                      BOOLEAN DEFAULT FALSE,
	 * pathLenConstraint       INTEGER (0..MAX) OPTIONAL }
	 */

	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE in "
			   "BasicConstraints; found %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	cert->extensions_present |= X509_EXT_BASIC_CONSTRAINTS;

	if (hdr.length == 0)
		return 0;

	if (asn1_get_next(hdr.payload, hdr.length, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL) {
		wpa_printf(MSG_DEBUG, "X509: Failed to parse "
			   "BasicConstraints");
		return -1;
	}

	if (hdr.tag == ASN1_TAG_BOOLEAN) {
		if (hdr.length != 1) {
			wpa_printf(MSG_DEBUG, "X509: Unexpected "
				   "Boolean length (%u) in BasicConstraints",
				   hdr.length);
			return -1;
		}
		cert->ca = hdr.payload[0];

		if (hdr.payload + hdr.length == pos + len) {
			wpa_printf(MSG_DEBUG, "X509: BasicConstraints - cA=%d",
				   cert->ca);
			return 0;
		}

		if (asn1_get_next(hdr.payload + hdr.length, len - hdr.length,
				  &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_UNIVERSAL) {
			wpa_printf(MSG_DEBUG, "X509: Failed to parse "
				   "BasicConstraints");
			return -1;
		}
	}

	if (hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "X509: Expected INTEGER in "
			   "BasicConstraints; found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	left = hdr.length;
	value = 0;
	while (left) {
		value <<= 8;
		value |= *pos++;
		left--;
	}

	cert->path_len_constraint = value;
	cert->extensions_present |= X509_EXT_PATH_LEN_CONSTRAINT;

	wpa_printf(MSG_DEBUG, "X509: BasicConstraints - cA=%d "
		   "pathLenConstraint=%lu",
		   cert->ca, cert->path_len_constraint);

	return 0;
}


static int x509_parse_extension_data(struct x509_certificate *cert,
				     struct asn1_oid *oid,
				     const u8 *pos, size_t len)
{
	if (!x509_id_ce_oid(oid))
		return 1;

	/* TODO: add other extensions required by RFC 3280, Ch 4.2:
	 * certificate policies (section 4.2.1.5)
	 * the subject alternative name (section 4.2.1.7)
	 * name constraints (section 4.2.1.11)
	 * policy constraints (section 4.2.1.12)
	 * extended key usage (section 4.2.1.13)
	 * inhibit any-policy (section 4.2.1.15)
	 */
	switch (oid->oid[3]) {
	case 15: /* id-ce-keyUsage */
		return x509_parse_ext_key_usage(cert, pos, len);
	case 19: /* id-ce-basicConstraints */
		return x509_parse_ext_basic_constraints(cert, pos, len);
	default:
		return 1;
	}
}


static int x509_parse_extension(struct x509_certificate *cert,
				const u8 *pos, size_t len, const u8 **next)
{
	const u8 *end;
	struct asn1_hdr hdr;
	struct asn1_oid oid;
	int critical_ext = 0, res;
	char buf[80];

	/*
	 * Extension  ::=  SEQUENCE  {
	 *     extnID      OBJECT IDENTIFIER,
	 *     critical    BOOLEAN DEFAULT FALSE,
	 *     extnValue   OCTET STRING
	 * }
	 */

	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Unexpected ASN.1 header in "
			   "Extensions: class %d tag 0x%x; expected SEQUENCE",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	*next = end = pos + hdr.length;

	if (asn1_get_oid(pos, end - pos, &oid, &pos) < 0) {
		wpa_printf(MSG_DEBUG, "X509: Unexpected ASN.1 data for "
			   "Extension (expected OID)");
		return -1;
	}

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    (hdr.tag != ASN1_TAG_BOOLEAN &&
	     hdr.tag != ASN1_TAG_OCTETSTRING)) {
		wpa_printf(MSG_DEBUG, "X509: Unexpected ASN.1 header in "
			   "Extensions: class %d tag 0x%x; expected BOOLEAN "
			   "or OCTET STRING", hdr.class, hdr.tag);
		return -1;
	}

	if (hdr.tag == ASN1_TAG_BOOLEAN) {
		if (hdr.length != 1) {
			wpa_printf(MSG_DEBUG, "X509: Unexpected "
				   "Boolean length (%u)", hdr.length);
			return -1;
		}
		critical_ext = hdr.payload[0];
		pos = hdr.payload;
		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    (hdr.class != ASN1_CLASS_UNIVERSAL &&
		     hdr.class != ASN1_CLASS_PRIVATE) ||
		    hdr.tag != ASN1_TAG_OCTETSTRING) {
			wpa_printf(MSG_DEBUG, "X509: Unexpected ASN.1 header "
				   "in Extensions: class %d tag 0x%x; "
				   "expected OCTET STRING",
				   hdr.class, hdr.tag);
			return -1;
		}
	}

	asn1_oid_to_str(&oid, buf, sizeof(buf));
	wpa_printf(MSG_DEBUG, "X509: Extension: extnID=%s critical=%d",
		   buf, critical_ext);
	wpa_hexdump(MSG_MSGDUMP, "X509: extnValue", hdr.payload, hdr.length);

	res = x509_parse_extension_data(cert, &oid, hdr.payload, hdr.length);
	if (res < 0)
		return res;
	if (res == 1 && critical_ext) {
		wpa_printf(MSG_INFO, "X509: Unknown critical extension %s",
			   buf);
		return -1;
	}

	return 0;
}


static int x509_parse_extensions(struct x509_certificate *cert,
				 const u8 *pos, size_t len)
{
	const u8 *end;
	struct asn1_hdr hdr;

	/* Extensions  ::=  SEQUENCE SIZE (1..MAX) OF Extension */

	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Unexpected ASN.1 data "
			   "for Extensions: class %d tag 0x%x; "
			   "expected SEQUENCE", hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	while (pos < end) {
		if (x509_parse_extension(cert, pos, end - pos, &pos)
		    < 0)
			return -1;
	}

	return 0;
}


static int x509_parse_tbs_certificate(const u8 *buf, size_t len,
				      struct x509_certificate *cert,
				      const u8 **next)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end;
	size_t left;
	char sbuf[128];
	unsigned long value;

	/* tbsCertificate TBSCertificate ::= SEQUENCE */
	if (asn1_get_next(buf, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: tbsCertificate did not start "
			   "with a valid SEQUENCE - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}
	pos = hdr.payload;
	end = *next = pos + hdr.length;

	/*
	 * version [0]  EXPLICIT Version DEFAULT v1
	 * Version  ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
	 */
	if (asn1_get_next(pos, end - pos, &hdr) < 0)
		return -1;
	pos = hdr.payload;

	if (hdr.class == ASN1_CLASS_CONTEXT_SPECIFIC) {
		if (asn1_get_next(pos, end - pos, &hdr) < 0)
			return -1;

		if (hdr.class != ASN1_CLASS_UNIVERSAL ||
		    hdr.tag != ASN1_TAG_INTEGER) {
			wpa_printf(MSG_DEBUG, "X509: No INTEGER tag found for "
				   "version field - found class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return -1;
		}
		if (hdr.length != 1) {
			wpa_printf(MSG_DEBUG, "X509: Unexpected version field "
				   "length %u (expected 1)", hdr.length);
			return -1;
		}
		pos = hdr.payload;
		left = hdr.length;
		value = 0;
		while (left) {
			value <<= 8;
			value |= *pos++;
			left--;
		}

		cert->version = (x509_version_e)value;
		if (cert->version != X509_CERT_V1 &&
		    cert->version != X509_CERT_V2 &&
		    cert->version != X509_CERT_V3) {
			wpa_printf(MSG_DEBUG, "X509: Unsupported version %d",
				   cert->version + 1);
			return -1;
		}

		if (asn1_get_next(pos, end - pos, &hdr) < 0)
			return -1;
	} else
		cert->version = X509_CERT_V1;
	wpa_printf(MSG_MSGDUMP, "X509: Version X.509v%d", cert->version + 1);

	/* serialNumber CertificateSerialNumber ::= INTEGER */
	if (hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_INTEGER) {
		wpa_printf(MSG_DEBUG, "X509: No INTEGER tag found for "
			   "serialNumber; class=%d tag=0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	pos = hdr.payload;
	left = hdr.length;
	while (left) {
		cert->serial_number <<= 8;
		cert->serial_number |= *pos++;
		left--;
	}
	wpa_printf(MSG_MSGDUMP, "X509: serialNumber %lu", cert->serial_number);

	/* signature AlgorithmIdentifier */
	if (x509_parse_algorithm_identifier(pos, end - pos, &cert->signature,
					    &pos))
		return -1;

	/* issuer Name */
	if (x509_parse_name(pos, end - pos, &cert->issuer, &pos))
		return -1;
	x509_name_string(&cert->issuer, sbuf, sizeof(sbuf));
	wpa_printf(MSG_MSGDUMP, "X509: issuer %s", sbuf);

	/* validity Validity */
	if (x509_parse_validity(pos, end - pos, cert, &pos))
		return -1;

	/* subject Name */
	if (x509_parse_name(pos, end - pos, &cert->subject, &pos))
		return -1;
	x509_name_string(&cert->subject, sbuf, sizeof(sbuf));
	wpa_printf(MSG_MSGDUMP, "X509: subject %s", sbuf);

	/* subjectPublicKeyInfo SubjectPublicKeyInfo */
	if (x509_parse_public_key(pos, end - pos, cert, &pos))
		return -1;

	if (pos == end)
		return 0;

	if (cert->version == X509_CERT_V1)
		return 0;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC) {
		wpa_printf(MSG_DEBUG, "X509: Expected Context-Specific"
			   " tag to parse optional tbsCertificate "
			   "field(s); parsed class %d tag 0x%x",
			   hdr.class, hdr.tag);
		return -1;
	}

	if (hdr.tag == 1) {
		/* issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL */
		wpa_printf(MSG_DEBUG, "X509: issuerUniqueID");
		/* TODO: parse UniqueIdentifier ::= BIT STRING */

		if (hdr.payload + hdr.length == end)
			return 0;

		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC) {
			wpa_printf(MSG_DEBUG, "X509: Expected Context-Specific"
				   " tag to parse optional tbsCertificate "
				   "field(s); parsed class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return -1;
		}
	}

	if (hdr.tag == 2) {
		/* subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL */
		wpa_printf(MSG_DEBUG, "X509: subjectUniqueID");
		/* TODO: parse UniqueIdentifier ::= BIT STRING */

		if (hdr.payload + hdr.length == end)
			return 0;

		if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
		    hdr.class != ASN1_CLASS_CONTEXT_SPECIFIC) {
			wpa_printf(MSG_DEBUG, "X509: Expected Context-Specific"
				   " tag to parse optional tbsCertificate "
				   "field(s); parsed class %d tag 0x%x",
				   hdr.class, hdr.tag);
			return -1;
		}
	}

	if (hdr.tag != 3) {
		wpa_printf(MSG_DEBUG, "X509: Ignored unexpected "
			   "Context-Specific tag %d in optional "
			   "tbsCertificate fields", hdr.tag);
		return 0;
	}

	/* extensions      [3]  EXPLICIT Extensions OPTIONAL */

	if (cert->version != X509_CERT_V3) {
		wpa_printf(MSG_DEBUG, "X509: X.509%d certificate and "
			   "Extensions data which are only allowed for "
			   "version 3", cert->version + 1);
		return -1;
	}

	if (x509_parse_extensions(cert, hdr.payload, hdr.length) < 0)
		return -1;

	pos = hdr.payload + hdr.length;
	if (pos < end) {
		wpa_hexdump(MSG_DEBUG,
			    "X509: Ignored extra tbsCertificate data",
			    pos, end - pos);
	}

	return 0;
}


static int x509_rsadsi_oid(struct asn1_oid *oid)
{
	return oid->len >= 4 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 2 /* member-body */ &&
		oid->oid[2] == 840 /* us */ &&
		oid->oid[3] == 113549 /* rsadsi */;
}


static int x509_pkcs_oid(struct asn1_oid *oid)
{
	return oid->len >= 5 &&
		x509_rsadsi_oid(oid) &&
		oid->oid[4] == 1 /* pkcs */;
}


static int x509_digest_oid(struct asn1_oid *oid)
{
	return oid->len >= 5 &&
		x509_rsadsi_oid(oid) &&
		oid->oid[4] == 2 /* digestAlgorithm */;
}


static int x509_sha1_oid(struct asn1_oid *oid)
{
	return oid->len == 6 &&
		oid->oid[0] == 1 /* iso */ &&
		oid->oid[1] == 3 /* identified-organization */ &&
		oid->oid[2] == 14 /* oiw */ &&
		oid->oid[3] == 3 /* secsig */ &&
		oid->oid[4] == 2 /* algorithms */ &&
		oid->oid[5] == 26 /* id-sha1 */;
}


/**
 * x509_certificate_parse - Parse a X.509 certificate in DER format
 * @buf: Pointer to the X.509 certificate in DER format
 * @len: Buffer length
 * Returns: Pointer to the parsed certificate or %NULL on failure
 *
 * Caller is responsible for freeing the returned certificate by calling
 * x509_certificate_free().
 */
struct x509_certificate * x509_certificate_parse(const u8 *buf, size_t len)
{
	struct asn1_hdr hdr;
	const u8 *pos, *end, *hash_start;
	struct x509_certificate *cert;

	cert = os_zalloc(sizeof(*cert) + len);
	if (cert == NULL)
		return NULL;
	os_memcpy(cert + 1, buf, len);
	cert->cert_start = (u8 *) (cert + 1);
	cert->cert_len = len;

	pos = buf;
	end = buf + len;

	/* RFC 3280 - X.509 v3 certificate / ASN.1 DER */

	/* Certificate ::= SEQUENCE */
	if (asn1_get_next(pos, len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Certificate did not start with "
			   "a valid SEQUENCE - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		x509_certificate_free(cert);
		return NULL;
	}
	pos = hdr.payload;

	if (pos + hdr.length > end) {
		x509_certificate_free(cert);
		return NULL;
	}

	if (pos + hdr.length < end) {
		wpa_hexdump(MSG_MSGDUMP, "X509: Ignoring extra data after DER "
			    "encoded certificate",
			    pos + hdr.length, end - pos + hdr.length);
		end = pos + hdr.length;
	}

	hash_start = pos;
	cert->tbs_cert_start = cert->cert_start + (hash_start - buf);
	if (x509_parse_tbs_certificate(pos, end - pos, cert, &pos)) {
		x509_certificate_free(cert);
		return NULL;
	}
	cert->tbs_cert_len = pos - hash_start;

	/* signatureAlgorithm AlgorithmIdentifier */
	if (x509_parse_algorithm_identifier(pos, end - pos,
					    &cert->signature_alg, &pos)) {
		x509_certificate_free(cert);
		return NULL;
	}

	/* signatureValue BIT STRING */
	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_BITSTRING) {
		wpa_printf(MSG_DEBUG, "X509: Expected BITSTRING "
			   "(signatureValue) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		x509_certificate_free(cert);
		return NULL;
	}
	if (hdr.length < 1) {
		x509_certificate_free(cert);
		return NULL;
	}
	pos = hdr.payload;
	if (*pos) {
		wpa_printf(MSG_DEBUG, "X509: BITSTRING - %d unused bits",
			   *pos);
		/* PKCS #1 v1.5 10.2.1:
		 * It is an error if the length in bits of the signature S is
		 * not a multiple of eight.
		 */
		x509_certificate_free(cert);
		return NULL;
	}
	os_free(cert->sign_value);
	cert->sign_value = os_malloc(hdr.length - 1);
	if (cert->sign_value == NULL) {
		wpa_printf(MSG_DEBUG, "X509: Failed to allocate memory for "
			   "signatureValue");
		x509_certificate_free(cert);
		return NULL;
	}
	os_memcpy(cert->sign_value, pos + 1, hdr.length - 1);
	cert->sign_value_len = hdr.length - 1;
	wpa_hexdump(MSG_MSGDUMP, "X509: signature",
		    cert->sign_value, cert->sign_value_len);

	return cert;
}


/**
 * x509_certificate_check_signature - Verify certificate signature
 * @issuer: Issuer certificate
 * @cert: Certificate to be verified
 * Returns: 0 if cert has a valid signature that was signed by the issuer,
 * -1 if not
 */
int x509_certificate_check_signature(struct x509_certificate *issuer,
				     struct x509_certificate *cert)
{
	struct crypto_public_key *pk;
	u8 *data;
	const u8 *pos, *end, *next, *da_end;
	size_t data_len;
	struct asn1_hdr hdr;
	struct asn1_oid oid;
	u8 hash[20];
	size_t hash_len;

	if (!x509_pkcs_oid(&cert->signature.oid) ||
	    cert->signature.oid.len != 7 ||
	    cert->signature.oid.oid[5] != 1 /* pkcs-1 */) {
		wpa_printf(MSG_DEBUG, "X509: Unrecognized signature "
			   "algorithm");
		return -1;
	}

	pk = crypto_public_key_import(issuer->public_key,
				      issuer->public_key_len);
	if (pk == NULL)
		return -1;

	data_len = cert->sign_value_len;
	data = os_malloc(data_len);
	if (data == NULL) {
		crypto_public_key_free(pk);
		return -1;
	}

	if (crypto_public_key_decrypt_pkcs1(pk, cert->sign_value,
					    cert->sign_value_len, data,
					    &data_len) < 0) {
		wpa_printf(MSG_DEBUG, "X509: Failed to decrypt signature");
		crypto_public_key_free(pk);
		os_free(data);
		return -1;
	}
	crypto_public_key_free(pk);

	wpa_hexdump(MSG_MSGDUMP, "X509: Signature data D", data, data_len);

	/*
	 * PKCS #1 v1.5, 10.1.2:
	 *
	 * DigestInfo ::= SEQUENCE {
	 *     digestAlgorithm DigestAlgorithmIdentifier,
	 *     digest Digest
	 * }
	 *
	 * DigestAlgorithmIdentifier ::= AlgorithmIdentifier
	 *
	 * Digest ::= OCTET STRING
	 *
	 */
	if (asn1_get_next(data, data_len, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
			   "(DigestInfo) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		os_free(data);
		return -1;
	}

	pos = hdr.payload;
	end = pos + hdr.length;

	/*
	 * X.509:
	 * AlgorithmIdentifier ::= SEQUENCE {
	 *     algorithm            OBJECT IDENTIFIER,
	 *     parameters           ANY DEFINED BY algorithm OPTIONAL
	 * }
	 */

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_SEQUENCE) {
		wpa_printf(MSG_DEBUG, "X509: Expected SEQUENCE "
			   "(AlgorithmIdentifier) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		os_free(data);
		return -1;
	}
	da_end = hdr.payload + hdr.length;

	if (asn1_get_oid(hdr.payload, hdr.length, &oid, &next)) {
		wpa_printf(MSG_DEBUG, "X509: Failed to parse digestAlgorithm");
		os_free(data);
		return -1;
	}

	if (x509_sha1_oid(&oid)) {
		if (cert->signature.oid.oid[6] !=
		    5 /* sha-1WithRSAEncryption */) {
			wpa_printf(MSG_DEBUG, "X509: digestAlgorithm SHA1 "
				   "does not match with certificate "
				   "signatureAlgorithm (%lu)",
				   cert->signature.oid.oid[6]);
			os_free(data);
			return -1;
		}
		goto skip_digest_oid;
	}

	if (!x509_digest_oid(&oid)) {
		wpa_printf(MSG_DEBUG, "X509: Unrecognized digestAlgorithm");
		os_free(data);
		return -1;
	}
	switch (oid.oid[5]) {
	case 5: /* md5 */
		if (cert->signature.oid.oid[6] != 4 /* md5WithRSAEncryption */)
		{
			wpa_printf(MSG_DEBUG, "X509: digestAlgorithm MD5 does "
				   "not match with certificate "
				   "signatureAlgorithm (%lu)",
				   cert->signature.oid.oid[6]);
			os_free(data);
			return -1;
		}
		break;
	case 2: /* md2 */
	case 4: /* md4 */
	default:
		wpa_printf(MSG_DEBUG, "X509: Unsupported digestAlgorithm "
			   "(%lu)", oid.oid[5]);
		os_free(data);
		return -1;
	}

skip_digest_oid:
	/* Digest ::= OCTET STRING */
	pos = da_end;
	end = data + data_len;

	if (asn1_get_next(pos, end - pos, &hdr) < 0 ||
	    hdr.class != ASN1_CLASS_UNIVERSAL ||
	    hdr.tag != ASN1_TAG_OCTETSTRING) {
		wpa_printf(MSG_DEBUG, "X509: Expected OCTETSTRING "
			   "(Digest) - found class %d tag 0x%x",
			   hdr.class, hdr.tag);
		os_free(data);
		return -1;
	}
	wpa_hexdump(MSG_MSGDUMP, "X509: Decrypted Digest",
		    hdr.payload, hdr.length);

	switch (cert->signature.oid.oid[6]) {
	case 4: /* md5WithRSAEncryption */
		md5_vector(1, &cert->tbs_cert_start, &cert->tbs_cert_len,
			   hash);
		hash_len = 16;
		wpa_hexdump(MSG_MSGDUMP, "X509: Certificate hash (MD5)",
			    hash, hash_len);
		break;
	case 5: /* sha-1WithRSAEncryption */
		sha1_vector(1, &cert->tbs_cert_start, &cert->tbs_cert_len,
			    hash);
		hash_len = 20;
		wpa_hexdump(MSG_MSGDUMP, "X509: Certificate hash (SHA1)",
			    hash, hash_len);
		break;
	case 2: /* md2WithRSAEncryption */
	case 11: /* sha256WithRSAEncryption */
	case 12: /* sha384WithRSAEncryption */
	case 13: /* sha512WithRSAEncryption */
	default:
		wpa_printf(MSG_INFO, "X509: Unsupported certificate signature "
			   "algorithm (%lu)", cert->signature.oid.oid[6]);
		os_free(data);
		return -1;
	}

	if (hdr.length != hash_len ||
	    os_memcmp(hdr.payload, hash, hdr.length) != 0) {
		wpa_printf(MSG_INFO, "X509: Certificate Digest does not match "
			   "with calculated tbsCertificate hash");
		os_free(data);
		return -1;
	}

	os_free(data);

	wpa_printf(MSG_DEBUG, "X509: Certificate Digest matches with "
		   "calculated tbsCertificate hash");

	return 0;
}


static int x509_valid_issuer(const struct x509_certificate *cert)
{
	if ((cert->extensions_present & X509_EXT_BASIC_CONSTRAINTS) &&
	    !cert->ca) {
		wpa_printf(MSG_DEBUG, "X509: Non-CA certificate used as an "
			   "issuer");
		return -1;
	}

	if (cert->version == X509_CERT_V3 &&
	    !(cert->extensions_present & X509_EXT_BASIC_CONSTRAINTS)) {
		wpa_printf(MSG_DEBUG, "X509: v3 CA certificate did not "
			   "include BasicConstraints extension");
		return -1;
	}

	if ((cert->extensions_present & X509_EXT_KEY_USAGE) &&
	    !(cert->key_usage & X509_KEY_USAGE_KEY_CERT_SIGN)) {
		wpa_printf(MSG_DEBUG, "X509: Issuer certificate did not have "
			   "keyCertSign bit in Key Usage");
		return -1;
	}

	return 0;
}


/**
 * x509_certificate_chain_validate - Validate X.509 certificate chain
 * @trusted: List of trusted certificates
 * @chain: Certificate chain to be validated (first chain must be issued by
 * signed by the second certificate in the chain and so on)
 * @reason: Buffer for returning failure reason (X509_VALIDATE_*)
 * Returns: 0 if chain is valid, -1 if not
 */
int x509_certificate_chain_validate(struct x509_certificate *trusted,
				    struct x509_certificate *chain,
				    int *reason)
{
	return 0;
#if 0 /* FIXME -> fix os_get_time for rtke */
	long unsigned idx;
	int chain_trusted = 0;
	struct x509_certificate *cert, *trust;
	char buf[128];
	struct os_time now;

	*reason = X509_VALIDATE_OK;

	wpa_printf(MSG_DEBUG, "X509: Validate certificate chain");
	os_get_time(&now);

	for (cert = chain, idx = 0; cert; cert = cert->next, idx++) {
		x509_name_string(&cert->subject, buf, sizeof(buf)); 
		wpa_printf(MSG_DEBUG, "X509: %lu: %s", idx, buf);

		if (chain_trusted)
			continue;

		if ((unsigned long) now.sec <
		    (unsigned long) cert->not_before ||
		    (unsigned long) now.sec >
		    (unsigned long) cert->not_after) {
			wpa_printf(MSG_INFO, "X509: Certificate not valid "
				   "(now=%lu not_before=%lu not_after=%lu)",
				   now.sec, cert->not_before, cert->not_after);
			*reason = X509_VALIDATE_CERTIFICATE_EXPIRED;
			return -1;
		}

		if (cert->next) {
			if (x509_name_compare(&cert->issuer,
					      &cert->next->subject) != 0) {
				wpa_printf(MSG_DEBUG, "X509: Certificate "
					   "chain issuer name mismatch");
				*reason = X509_VALIDATE_CERTIFICATE_UNKNOWN;
				return -1;
			}

			if (x509_valid_issuer(cert->next) < 0) {
				*reason = X509_VALIDATE_BAD_CERTIFICATE;
				return -1;
			}

			if ((cert->next->extensions_present &
			     X509_EXT_PATH_LEN_CONSTRAINT) &&
			    idx > cert->next->path_len_constraint) {
				wpa_printf(MSG_DEBUG, "X509: pathLenConstraint"
					   " not met (idx=%lu issuer "
					   "pathLenConstraint=%lu)", idx,
					   cert->next->path_len_constraint);
				*reason = X509_VALIDATE_BAD_CERTIFICATE;
				return -1;
			}

			if (x509_certificate_check_signature(cert->next, cert)
			    < 0) {
				wpa_printf(MSG_DEBUG, "X509: Invalid "
					   "certificate signature within "
					   "chain");
				*reason = X509_VALIDATE_BAD_CERTIFICATE;
				return -1;
			}
		}

		for (trust = trusted; trust; trust = trust->next) {
			if (x509_name_compare(&cert->issuer, &trust->subject)
			    == 0)
				break;
		}

		if (trust) {
			wpa_printf(MSG_DEBUG, "X509: Found issuer from the "
				   "list of trusted certificates");
			if (x509_valid_issuer(trust) < 0) {
				*reason = X509_VALIDATE_BAD_CERTIFICATE;
				return -1;
			}

			if (x509_certificate_check_signature(trust, cert) < 0)
			{
				wpa_printf(MSG_DEBUG, "X509: Invalid "
					   "certificate signature");
				*reason = X509_VALIDATE_BAD_CERTIFICATE;
				return -1;
			}

			wpa_printf(MSG_DEBUG, "X509: Trusted certificate "
				   "found to complete the chain");
			chain_trusted = 1;
		}
	}

	if (!chain_trusted) {
		wpa_printf(MSG_DEBUG, "X509: Did not find any of the issuers "
			   "from the list of trusted certificates");
		if (trusted) {
			*reason = X509_VALIDATE_UNKNOWN_CA;
			return -1;
		}
		wpa_printf(MSG_DEBUG, "X509: Certificate chain validation "
			   "disabled - ignore unknown CA issue");
	}

	wpa_printf(MSG_DEBUG, "X509: Certificate chain valid");

	return 0;
#endif
}


/**
 * x509_certificate_get_subject - Get a certificate based on Subject name
 * @chain: Certificate chain to search through
 * @name: Subject name to search for
 * Returns: Pointer to the certificate with the given Subject name or
 * %NULL on failure
 */
struct x509_certificate *
x509_certificate_get_subject(struct x509_certificate *chain,
			     struct x509_name *name)
{
	struct x509_certificate *cert;

	for (cert = chain; cert; cert = cert->next) {
		if (x509_name_compare(&cert->subject, name) == 0)
			return cert;
	}
	return NULL;
}


/**
 * x509_certificate_self_signed - Is the certificate self-signed?
 * @cert: Certificate
 * Returns: 1 if certificate is self-signed, 0 if not
 */
int x509_certificate_self_signed(struct x509_certificate *cert)
{
	return x509_name_compare(&cert->issuer, &cert->subject) == 0;
}

#endif /* CONFIG_INTERNAL_X509 */
