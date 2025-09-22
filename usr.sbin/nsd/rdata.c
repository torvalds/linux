/*
 * rdata.c -- RDATA conversion functions.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include "rdata.h"
#include "zonec.h"

/* Taken from RFC 4398, section 2.1.  */
lookup_table_type dns_certificate_types[] = {
/*	0		Reserved */
	{ 1, "PKIX" },	/* X.509 as per PKIX */
	{ 2, "SPKI" },	/* SPKI cert */
	{ 3, "PGP" },	/* OpenPGP packet */
	{ 4, "IPKIX" },	/* The URL of an X.509 data object */
	{ 5, "ISPKI" },	/* The URL of an SPKI certificate */
	{ 6, "IPGP" },	/* The fingerprint and URL of an OpenPGP packet */
	{ 7, "ACPKIX" },	/* Attribute Certificate */
	{ 8, "IACPKIX" },	/* The URL of an Attribute Certificate */
	{ 253, "URI" },	/* URI private */
	{ 254, "OID" },	/* OID private */
/*	255 		Reserved */
/* 	256-65279	Available for IANA assignment */
/*	65280-65534	Experimental */
/*	65535		Reserved */
	{ 0, NULL }
};

/* Taken from RFC 2535, section 7.  */
lookup_table_type dns_algorithms[] = {
	{ 1, "RSAMD5" },	/* RFC 2537 */
	{ 2, "DH" },		/* RFC 2539 */
	{ 3, "DSA" },		/* RFC 2536 */
	{ 4, "ECC" },
	{ 5, "RSASHA1" },	/* RFC 3110 */
	{ 6, "DSA-NSEC3-SHA1" },	/* RFC 5155 */
	{ 7, "RSASHA1-NSEC3-SHA1" },	/* RFC 5155 */
	{ 8, "RSASHA256" },		/* RFC 5702 */
	{ 10, "RSASHA512" },		/* RFC 5702 */
	{ 12, "ECC-GOST" },		/* RFC 5933 */
	{ 13, "ECDSAP256SHA256" },	/* RFC 6605 */
	{ 14, "ECDSAP384SHA384" },	/* RFC 6605 */
	{ 15, "ED25519" },		/* RFC 8080 */
	{ 16, "ED448" },		/* RFC 8080 */
	{ 252, "INDIRECT" },
	{ 253, "PRIVATEDNS" },
	{ 254, "PRIVATEOID" },
	{ 0, NULL }
};

const char *svcparamkey_strs[] = {
		"mandatory", "alpn", "no-default-alpn", "port",
		"ipv4hint", "ech", "ipv6hint", "dohpath", "ohttp",
		"tls-supported-groups"
	};

typedef int (*rdata_to_string_type)(buffer_type *output,
				    rdata_atom_type rdata,
				    rr_type *rr);

static int
rdata_dname_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	buffer_printf(output,
		      "%s",
		      dname_to_string(domain_dname(rdata_atom_domain(rdata)),
				      NULL));
	return 1;
}

static int
rdata_dns_name_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	const uint8_t *data = rdata_atom_data(rdata);
	size_t offset = 0;
	uint8_t length = data[offset];
	size_t i;

	while (length > 0)
	{
		if (offset) /* concat label */
			buffer_printf(output, ".");

		for (i = 1; i <= length; ++i) {
			uint8_t ch = data[i+offset];

			if (ch=='.' || ch==';' || ch=='(' || ch==')' || ch=='\\') {
				buffer_printf(output, "\\%c", (char) ch);
			} else if (!isgraph((unsigned char) ch)) {
				buffer_printf(output, "\\%03u", (unsigned int) ch);
			} else if (isprint((unsigned char) ch)) {
				buffer_printf(output, "%c", (char) ch);
			} else {
				buffer_printf(output, "\\%03u", (unsigned int) ch);
			}
		}
		/* next label */
		offset = offset+length+1;
		length = data[offset];
	}

	/* root label */
	buffer_printf(output, ".");
	return 1;
}

static int
rdata_text_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	const uint8_t *data = rdata_atom_data(rdata);
	uint8_t length = data[0];
	size_t i;

	buffer_printf(output, "\"");
	for (i = 1; i <= length; ++i) {
		char ch = (char) data[i];
		if (isprint((unsigned char)ch)) {
			if (ch == '"' || ch == '\\') {
				buffer_printf(output, "\\");
			}
			buffer_printf(output, "%c", ch);
		} else {
			buffer_printf(output, "\\%03u", (unsigned) data[i]);
		}
	}
	buffer_printf(output, "\"");
	return 1;
}

static int
rdata_texts_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t pos = 0;
	const uint8_t *data = rdata_atom_data(rdata);
	uint16_t length = rdata_atom_size(rdata);
	size_t i;

	while (pos < length && pos + data[pos] < length) {
		buffer_printf(output, "\"");
		for (i = 1; i <= data[pos]; ++i) {
			char ch = (char) data[pos + i];
			if (isprint((unsigned char)ch)) {
				if (ch == '"' || ch == '\\') {
					buffer_printf(output, "\\");
				}
				buffer_printf(output, "%c", ch);
			} else {
				buffer_printf(output, "\\%03u", (unsigned) data[pos+i]);
			}
		}
		pos += data[pos]+1;
		buffer_printf(output, pos < length?"\" ":"\"");
	}
	return 1;
}

static int
rdata_long_text_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	const uint8_t *data = rdata_atom_data(rdata);
	uint16_t length = rdata_atom_size(rdata);
	size_t i;

	buffer_printf(output, "\"");
	for (i = 0; i < length; ++i) {
		char ch = (char) data[i];
		if (isprint((unsigned char)ch)) {
			if (ch == '"' || ch == '\\') {
				buffer_printf(output, "\\");
			}
			buffer_printf(output, "%c", ch);
		} else {
			buffer_printf(output, "\\%03u", (unsigned) data[i]);
		}
	}
	buffer_printf(output, "\"");
	return 1;
}

static int
rdata_unquoted_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	const uint8_t *data = rdata_atom_data(rdata);
	uint8_t length = data[0];
	size_t i;

	for (i = 1; i <= length; ++i) {
		char ch = (char) data[i];
		if (isprint((unsigned char)ch)) {
			if (ch == '"' || ch == '\\' || ch == '(' || ch == ')'
			  || ch == '\'' || isspace((unsigned char)ch)) {
				buffer_printf(output, "\\");
			}
			buffer_printf(output, "%c", ch);
		} else {
			buffer_printf(output, "\\%03u", (unsigned) data[i]);
		}
	}
	return 1;
}

static int
rdata_unquoteds_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t pos = 0;
	const uint8_t *data = rdata_atom_data(rdata);
	uint16_t length = rdata_atom_size(rdata);
	size_t i;

	while (pos < length && pos + data[pos] < length) {
		for (i = 1; i <= data[pos]; ++i) {
			char ch = (char) data[pos + i];
			if (isprint((unsigned char)ch)) {
				if (ch == '"' || ch == '\\'
				||  isspace((unsigned char)ch)) {
					buffer_printf(output, "\\");
				}
				buffer_printf(output, "%c", ch);
			} else {
				buffer_printf(output, "\\%03u", (unsigned) data[pos+i]);
			}
		}
		pos += data[pos]+1;
		buffer_printf(output, pos < length?" ":"");
	}
	return 1;
}

static int
rdata_tag_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	const uint8_t *data = rdata_atom_data(rdata);
	uint8_t length = data[0];
	size_t i;
	for (i = 1; i <= length; ++i) {
		char ch = (char) data[i];
		if (isdigit((unsigned char)ch) || islower((unsigned char)ch))
			buffer_printf(output, "%c", ch);
		else	return 0;
	}
	return 1;
}

static int
rdata_byte_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint8_t data = *rdata_atom_data(rdata);
	buffer_printf(output, "%lu", (unsigned long) data);
	return 1;
}

static int
rdata_short_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t data = read_uint16(rdata_atom_data(rdata));
	buffer_printf(output, "%lu", (unsigned long) data);
	return 1;
}

static int
rdata_long_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint32_t data = read_uint32(rdata_atom_data(rdata));
	buffer_printf(output, "%lu", (unsigned long) data);
	return 1;
}

static int
rdata_longlong_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint64_t data = read_uint64(rdata_atom_data(rdata));
	buffer_printf(output, "%llu", (unsigned long long) data);
	return 1;
}

static int
rdata_a_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	int result = 0;
	char str[200];
	if (inet_ntop(AF_INET, rdata_atom_data(rdata), str, sizeof(str))) {
		buffer_printf(output, "%s", str);
		result = 1;
	}
	return result;
}

static int
rdata_aaaa_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	int result = 0;
	char str[200];
	if (inet_ntop(AF_INET6, rdata_atom_data(rdata), str, sizeof(str))) {
		buffer_printf(output, "%s", str);
		result = 1;
	}
	return result;
}

static int
rdata_ilnp64_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint8_t* data = rdata_atom_data(rdata);
	uint16_t a1 = read_uint16(data);
	uint16_t a2 = read_uint16(data+2);
	uint16_t a3 = read_uint16(data+4);
	uint16_t a4 = read_uint16(data+6);

	buffer_printf(output, "%.4x:%.4x:%.4x:%.4x", a1, a2, a3, a4);
	return 1;
}

static int
rdata_eui48_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint8_t* data = rdata_atom_data(rdata);
	uint8_t a1 = data[0];
	uint8_t a2 = data[1];
	uint8_t a3 = data[2];
	uint8_t a4 = data[3];
	uint8_t a5 = data[4];
	uint8_t a6 = data[5];

	buffer_printf(output, "%.2x-%.2x-%.2x-%.2x-%.2x-%.2x",
		a1, a2, a3, a4, a5, a6);
	return 1;
}

static int
rdata_eui64_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint8_t* data = rdata_atom_data(rdata);
	uint8_t a1 = data[0];
	uint8_t a2 = data[1];
	uint8_t a3 = data[2];
	uint8_t a4 = data[3];
	uint8_t a5 = data[4];
	uint8_t a6 = data[5];
	uint8_t a7 = data[6];
	uint8_t a8 = data[7];

	buffer_printf(output, "%.2x-%.2x-%.2x-%.2x-%.2x-%.2x-%.2x-%.2x",
		a1, a2, a3, a4, a5, a6, a7, a8);
	return 1;
}

static int
rdata_rrtype_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t type = read_uint16(rdata_atom_data(rdata));
	buffer_printf(output, "%s", rrtype_to_string(type));
	return 1;
}

static int
rdata_algorithm_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint8_t id = *rdata_atom_data(rdata);
	buffer_printf(output, "%u", (unsigned) id);
	return 1;
}

static int
rdata_certificate_type_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t id = read_uint16(rdata_atom_data(rdata));
	lookup_table_type *type
		= lookup_by_id(dns_certificate_types, id);
	if (type) {
		buffer_printf(output, "%s", type->name);
	} else {
		buffer_printf(output, "%u", (unsigned) id);
	}
	return 1;
}

static int
rdata_period_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint32_t period = read_uint32(rdata_atom_data(rdata));
	buffer_printf(output, "%lu", (unsigned long) period);
	return 1;
}

static int
rdata_time_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	int result = 0;
	time_t time = (time_t) read_uint32(rdata_atom_data(rdata));
	struct tm *tm = gmtime(&time);
	char buf[15];
	if (strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", tm)) {
		buffer_printf(output, "%s", buf);
		result = 1;
	}
	return result;
}

static int
rdata_base32_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	int length;
	size_t size = rdata_atom_size(rdata);
	if(size == 0) {
		buffer_write(output, "-", 1);
		return 1;
	}
	size -= 1; /* remove length byte from count */
	buffer_reserve(output, size * 2 + 1);
	length = b32_ntop(rdata_atom_data(rdata)+1, size,
			  (char *) buffer_current(output), size * 2);
	if (length > 0) {
		buffer_skip(output, length);
	}
	return length != -1;
}

static int
rdata_base64_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* rr)
{
	int length;
	size_t size = rdata_atom_size(rdata);
	if(size == 0) {
		/* single zero represents empty buffer */
		buffer_write(output, (rr->type == TYPE_DOA ? "-" : "0"), 1);
		return 1;
	}
	buffer_reserve(output, size * 2 + 1);
	length = __b64_ntop(rdata_atom_data(rdata), size,
			  (char *) buffer_current(output), size * 2);
	if (length > 0) {
		buffer_skip(output, length);
	}
	return length != -1;
}

static void
hex_to_string(buffer_type *output, const uint8_t *data, size_t size)
{
	static const char hexdigits[] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	size_t i;

	buffer_reserve(output, size * 2);
	for (i = 0; i < size; ++i) {
		uint8_t octet = *data++;
		buffer_write_u8(output, hexdigits[octet >> 4]);
		buffer_write_u8(output, hexdigits[octet & 0x0f]);
	}
}

static int
rdata_hex_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	if(rdata_atom_size(rdata) == 0) {
		/* single zero represents empty buffer, such as CDS deletes */
		buffer_printf(output, "0");
	} else {
		hex_to_string(output, rdata_atom_data(rdata), rdata_atom_size(rdata));
	}
	return 1;
}

static int
rdata_hexlen_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	if(rdata_atom_size(rdata) <= 1) {
		/* NSEC3 salt hex can be empty */
		buffer_printf(output, "-");
		return 1;
	}
	hex_to_string(output, rdata_atom_data(rdata)+1, rdata_atom_size(rdata)-1);
	return 1;
}

static int
rdata_nsap_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	buffer_printf(output, "0x");
	hex_to_string(output, rdata_atom_data(rdata), rdata_atom_size(rdata));
	return 1;
}

static int
rdata_apl_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	int result = 0;
	buffer_type packet;

	buffer_create_from(
		&packet, rdata_atom_data(rdata), rdata_atom_size(rdata));

	if (buffer_available(&packet, 4)) {
		uint16_t address_family = buffer_read_u16(&packet);
		uint8_t prefix = buffer_read_u8(&packet);
		uint8_t length = buffer_read_u8(&packet);
		int negated = length & APL_NEGATION_MASK;
		int af = -1;

		length &= APL_LENGTH_MASK;
		switch (address_family) {
		case 1: af = AF_INET; break;
		case 2: af = AF_INET6; break;
		}
		if (af != -1 && buffer_available(&packet, length)) {
			char text_address[1000];
			uint8_t address[128];
			memset(address, 0, sizeof(address));
			buffer_read(&packet, address, length);
			if (inet_ntop(af, address, text_address, sizeof(text_address))) {
				buffer_printf(output, "%s%d:%s/%d",
					      negated ? "!" : "",
					      (int) address_family,
					      text_address,
					      (int) prefix);
				result = 1;
			}
		}
	}
	return result;
}

/*
 * Print protocol and service numbers rather than names for Well-Know Services
 * (WKS) RRs. WKS RRs are deprecated, though not technically, and should not
 * be used. The parser supports tcp/udp for protocols and a small subset of
 * services because getprotobyname and/or getservbyname are marked MT-Unsafe
 * and locale. getprotobyname_r and getservbyname_r exist on some platforms,
 * but are still marked locale (meaning the locale object is used without
 * synchonization, which is a problem for a library). Failure to load a zone
 * on a primary server because of an unknown protocol or service name is
 * acceptable as the operator can opt to use the numeric value. Failure to
 * load a zone on a secondary server is problematic because "unsupported"
 * protocols and services might be written. Print the numeric value for
 * maximum compatibility.
 *
 * (see simdzone/generic/wks.h for details).
 */
static int
rdata_services_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	int result = 0;
	buffer_type packet;

	buffer_create_from(
		&packet, rdata_atom_data(rdata), rdata_atom_size(rdata));

	if (buffer_available(&packet, 1)) {
		uint8_t protocol_number = buffer_read_u8(&packet);
		ssize_t bitmap_size = buffer_remaining(&packet);
		uint8_t *bitmap = buffer_current(&packet);

		buffer_printf(output, "%" PRIu8, protocol_number);

		for (int i = 0; i < bitmap_size * 8; ++i) {
			if (get_bit(bitmap, i)) {
				buffer_printf(output, " %d", i);
			}
		}
		buffer_skip(&packet, bitmap_size);
		result = 1;
	}
	return result;
}

static int
rdata_ipsecgateway_to_string(buffer_type *output, rdata_atom_type rdata, rr_type* rr)
{
	switch(rdata_atom_data(rr->rdatas[1])[0]) {
	case IPSECKEY_NOGATEWAY:
		buffer_printf(output, ".");
		break;
	case IPSECKEY_IP4:
		return rdata_a_to_string(output, rdata, rr);
	case IPSECKEY_IP6:
		return rdata_aaaa_to_string(output, rdata, rr);
	case IPSECKEY_DNAME:
		return rdata_dns_name_to_string(output, rdata, rr);
	default:
		return 0;
	}
	return 1;
}

static int
rdata_nxt_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	size_t i;
	uint8_t *bitmap = rdata_atom_data(rdata);
	size_t bitmap_size = rdata_atom_size(rdata);

	for (i = 0; i < bitmap_size * 8; ++i) {
		if (get_bit(bitmap, i)) {
			buffer_printf(output, "%s ", rrtype_to_string(i));
		}
	}

	buffer_skip(output, -1);

	return 1;
}

static int
rdata_nsec_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	size_t saved_position = buffer_position(output);
	buffer_type packet;
	int insert_space = 0;

	buffer_create_from(
		&packet, rdata_atom_data(rdata), rdata_atom_size(rdata));

	while (buffer_available(&packet, 2)) {
		uint8_t window = buffer_read_u8(&packet);
		uint8_t bitmap_size = buffer_read_u8(&packet);
		uint8_t *bitmap = buffer_current(&packet);
		int i;

		if (!buffer_available(&packet, bitmap_size)) {
			buffer_set_position(output, saved_position);
			return 0;
		}

		for (i = 0; i < bitmap_size * 8; ++i) {
			if (get_bit(bitmap, i)) {
				buffer_printf(output,
					      "%s%s",
					      insert_space ? " " : "",
					      rrtype_to_string(
						      window * 256 + i));
				insert_space = 1;
			}
		}
		buffer_skip(&packet, bitmap_size);
	}

	return 1;
}

static int
rdata_loc_to_string(buffer_type *ATTR_UNUSED(output),
		    rdata_atom_type ATTR_UNUSED(rdata),
		    rr_type* ATTR_UNUSED(rr))
{
	/*
	 * Returning 0 forces the record to be printed in unknown
	 * format.
	 */
	return 0;
}

static void
buffer_print_svcparamkey(buffer_type *output, uint16_t svcparamkey)
{
	if (svcparamkey < SVCPARAMKEY_COUNT)
		buffer_printf(output, "%s", svcparamkey_strs[svcparamkey]);
	else
		buffer_printf(output, "key%d", (int)svcparamkey);
}

static int
rdata_svcparam_port_to_string(buffer_type *output, uint16_t val_len,
	uint16_t *data)
{
	if (val_len != 2)
		return 0; /* wireformat error, a short is 2 bytes */
	buffer_printf(output, "=%d", (int)ntohs(data[0]));
	return 1;
}

static int
rdata_svcparam_ipv4hint_to_string(buffer_type *output, uint16_t val_len,
	uint16_t *data)
{
	char ip_str[INET_ADDRSTRLEN + 1];
	
	assert(val_len > 0); /* Guaranteed by rdata_svcparam_to_string */

	if ((val_len % IP4ADDRLEN) == 0) {
		if (inet_ntop(AF_INET, data, ip_str, sizeof(ip_str)) == NULL)
			return 0; /* wireformat error, incorrect size or inet family */

		buffer_printf(output, "=%s", ip_str);
		data += IP4ADDRLEN / sizeof(uint16_t);

		while ((val_len -= IP4ADDRLEN) > 0) {
			if (inet_ntop(AF_INET, data, ip_str, sizeof(ip_str)) == NULL)
				return 0; /* wireformat error, incorrect size or inet family */

			buffer_printf(output, ",%s", ip_str);
			data += IP4ADDRLEN / sizeof(uint16_t);
		}
		return 1;
	} else
		return 0;
}

static int
rdata_svcparam_ipv6hint_to_string(buffer_type *output, uint16_t val_len,
	uint16_t *data)
{
	char ip_str[INET6_ADDRSTRLEN + 1];
	
	assert(val_len > 0); /* Guaranteed by rdata_svcparam_to_string */

	if ((val_len % IP6ADDRLEN) == 0) {
		if (inet_ntop(AF_INET6, data, ip_str, sizeof(ip_str)) == NULL)
			return 0; /* wireformat error, incorrect size or inet family */

		buffer_printf(output, "=%s", ip_str);
		data += IP6ADDRLEN / sizeof(uint16_t);

		while ((val_len -= IP6ADDRLEN) > 0) {
			if (inet_ntop(AF_INET6, data, ip_str, sizeof(ip_str)) == NULL)
				return 0; /* wireformat error, incorrect size or inet family */

			buffer_printf(output, ",%s", ip_str);
			data += IP6ADDRLEN / sizeof(uint16_t);
		}
		return 1;
	} else
		return 0;
}

static int
rdata_svcparam_mandatory_to_string(buffer_type *output, uint16_t val_len,
	uint16_t *data)
{
	assert(val_len > 0); /* Guaranteed by rdata_svcparam_to_string */

	if (val_len % sizeof(uint16_t))
		return 0; /* wireformat error, val_len must be multiple of shorts */
	buffer_write_u8(output, '=');
	buffer_print_svcparamkey(output, ntohs(*data));
	data += 1;

	while ((val_len -= sizeof(uint16_t))) {
		buffer_write_u8(output, ',');
		buffer_print_svcparamkey(output, ntohs(*data));
		data += 1;
	}

	return 1;
}

static int
rdata_svcparam_ech_to_string(buffer_type *output, uint16_t val_len,
	uint16_t *data)
{
	int length;

	assert(val_len > 0); /* Guaranteed by rdata_svcparam_to_string */

	buffer_write_u8(output, '=');

	buffer_reserve(output, val_len * 2 + 1);
	length = __b64_ntop((uint8_t*) data, val_len,
			  (char *) buffer_current(output), val_len * 2);
	if (length > 0) {
		buffer_skip(output, length);
	}

	return length != -1;
}

static int
rdata_svcparam_alpn_to_string(buffer_type *output, uint16_t val_len,
	uint16_t *data)
{
	uint8_t *dp = (void *)data;

	assert(val_len > 0); /* Guaranteed by rdata_svcparam_to_string */

	buffer_write_u8(output, '=');
	buffer_write_u8(output, '"');
	while (val_len) {
		uint8_t i, str_len = *dp++;

		if (str_len > --val_len)
			return 0;

		for (i = 0; i < str_len; i++) {
			if (dp[i] == '"' || dp[i] == '\\')
				buffer_printf(output, "\\\\\\%c", dp[i]);

			else if (dp[i] == ',')
				buffer_printf(output, "\\\\%c", dp[i]);

			else if (!isprint(dp[i]))
				buffer_printf(output, "\\%03u", (unsigned) dp[i]);

			else
				buffer_write_u8(output, dp[i]);
		}
		dp += str_len;
		if ((val_len -= str_len))
			buffer_write_u8(output, ',');
	}
	buffer_write_u8(output, '"');
	return 1;
}

static int
rdata_svcparam_tls_supported_groups_to_string(buffer_type *output,
		uint16_t val_len, uint16_t *data)
{
	assert(val_len > 0); /* Guaranteed by rdata_svcparam_to_string */

	if ((val_len % sizeof(uint16_t)) == 1)
		return 0; /* A series of uint16_t is an even number of bytes */

	buffer_printf(output, "=%d", (int)ntohs(*data++));
	while ((val_len -= sizeof(uint16_t)) > 0) 
		buffer_printf(output, ",%d", (int)ntohs(*data++));
	return 1;
}

static int
rdata_svcparam_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t  size = rdata_atom_size(rdata);
	uint16_t* data = (uint16_t *)rdata_atom_data(rdata);
	uint16_t  svcparamkey, val_len;
	uint8_t*  dp; 
	size_t i;

	if (size < 4)
		return 0;
	svcparamkey = ntohs(data[0]);

	buffer_print_svcparamkey(output, svcparamkey);
	val_len = ntohs(data[1]);
	if (size != val_len + 4)
		return 0; /* wireformat error */
	if (!val_len) {
		/* Some SvcParams MUST have values */
		switch (svcparamkey) {
		case SVCB_KEY_ALPN:
		case SVCB_KEY_PORT:
		case SVCB_KEY_IPV4HINT:
		case SVCB_KEY_IPV6HINT:
		case SVCB_KEY_MANDATORY:
		case SVCB_KEY_DOHPATH:
		case SVCB_KEY_TLS_SUPPORTED_GROUPS:
			return 0;
		default:
			return 1;
		}
	}
	switch (svcparamkey) {
	case SVCB_KEY_PORT:
		return rdata_svcparam_port_to_string(output, val_len, data+2);
	case SVCB_KEY_IPV4HINT:
		return rdata_svcparam_ipv4hint_to_string(output, val_len, data+2);
	case SVCB_KEY_IPV6HINT:
		return rdata_svcparam_ipv6hint_to_string(output, val_len, data+2);
	case SVCB_KEY_MANDATORY:
		return rdata_svcparam_mandatory_to_string(output, val_len, data+2);
	case SVCB_KEY_NO_DEFAULT_ALPN:
		return 0; /* wireformat error, should not have a value */
	case SVCB_KEY_ALPN:
		return rdata_svcparam_alpn_to_string(output, val_len, data+2);
	case SVCB_KEY_ECH:
		return rdata_svcparam_ech_to_string(output, val_len, data+2);
	case SVCB_KEY_OHTTP:
		return 0; /* wireformat error, should not have a value */
	case SVCB_KEY_TLS_SUPPORTED_GROUPS:
		return rdata_svcparam_tls_supported_groups_to_string(output, val_len, data+2);
	case SVCB_KEY_DOHPATH:
		/* fallthrough */
	default:
		buffer_write(output, "=\"", 2);
		dp = (void*) (data + 2);

		for (i = 0; i < val_len; i++) {
			if (dp[i] == '"' || dp[i] == '\\')
				buffer_printf(output, "\\%c", dp[i]);

			else if (!isprint(dp[i]))
				buffer_printf(output, "\\%03u", (unsigned) dp[i]);

			else
				buffer_write_u8(output, dp[i]);
		}
		buffer_write_u8(output, '"');
		break;
	}
	return 1;
}

static int
rdata_hip_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
 	uint16_t size = rdata_atom_size(rdata);
	uint8_t hit_length;
	uint16_t pk_length;
	int length = 0;

	if(size < 4)
		return 0;
	hit_length = rdata_atom_data(rdata)[0];
	pk_length  = read_uint16(rdata_atom_data(rdata) + 2);
	length     = 4 + hit_length + pk_length;
	if(hit_length == 0 || pk_length == 0 || size < length)
		return 0;
	buffer_printf(output, "%u ", (unsigned)rdata_atom_data(rdata)[1]);
	hex_to_string(output, rdata_atom_data(rdata) + 4, hit_length);
	buffer_printf(output, " ");
	buffer_reserve(output, pk_length * 2 + 1);
	length = __b64_ntop(rdata_atom_data(rdata) + 4 + hit_length, pk_length,
			  (char *) buffer_current(output), pk_length * 2);
	if (length > 0) {
		buffer_skip(output, length);
	}
	return length != -1;
}

static int
rdata_atma_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint16_t size = rdata_atom_size(rdata), i;

	if(size < 2 || rdata_atom_data(rdata)[0] > 1)
		return 0;
	if(!rdata_atom_data(rdata)[0]) {
		hex_to_string(output, rdata_atom_data(rdata) + 1, size - 1);
		return 1;
	}
	for(i = 1; i < size; i++) {
		if(!isdigit(rdata_atom_data(rdata)[i]))
			return 0;
	}
	buffer_write_u8(output, '+');
	buffer_write(output, rdata_atom_data(rdata) + 1, size - 1);
	return 1;
}

static int
rdata_amtrelay_d_type_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
	uint8_t data = *rdata_atom_data(rdata);
	buffer_printf(output , "%c %lu", (data & 0x80 ? '1' : '0'),
			((unsigned long)data & 0x7f));
	return 1;
}

static int
rdata_amtrelay_relay_to_string(buffer_type *output, rdata_atom_type rdata,
		rr_type* rr)
{
	switch(rdata_atom_data(rr->rdatas[1])[0] & 0x7f) {
	case AMTRELAY_NOGATEWAY:
		break;
	case AMTRELAY_IP4:
		return rdata_a_to_string(output, rdata, rr);
	case AMTRELAY_IP6:
		return rdata_aaaa_to_string(output, rdata, rr);
	case AMTRELAY_DNAME:
		return rdata_dns_name_to_string(output, rdata, rr);
	default:
		return 0;
	}
	return 1;
}

static int
rdata_unknown_to_string(buffer_type *output, rdata_atom_type rdata,
	rr_type* ATTR_UNUSED(rr))
{
 	uint16_t size = rdata_atom_size(rdata);
 	buffer_printf(output, "\\# %lu ", (unsigned long) size);
	hex_to_string(output, rdata_atom_data(rdata), size);
	return 1;
}

static rdata_to_string_type rdata_to_string_table[RDATA_ZF_UNKNOWN + 1] = {
	rdata_dname_to_string,
	rdata_dns_name_to_string,
	rdata_text_to_string,
	rdata_texts_to_string,
	rdata_byte_to_string,
	rdata_short_to_string,
	rdata_long_to_string,
	rdata_longlong_to_string,
	rdata_a_to_string,
	rdata_aaaa_to_string,
	rdata_rrtype_to_string,
	rdata_algorithm_to_string,
	rdata_certificate_type_to_string,
	rdata_period_to_string,
	rdata_time_to_string,
	rdata_base64_to_string,
	rdata_base32_to_string,
	rdata_hex_to_string,
	rdata_hexlen_to_string,
	rdata_nsap_to_string,
	rdata_apl_to_string,
	rdata_ipsecgateway_to_string,
	rdata_services_to_string,
	rdata_nxt_to_string,
	rdata_nsec_to_string,
	rdata_loc_to_string,
	rdata_ilnp64_to_string,
	rdata_eui48_to_string,
	rdata_eui64_to_string,
	rdata_long_text_to_string,
	rdata_unquoted_to_string,
	rdata_unquoteds_to_string,
	rdata_tag_to_string,
	rdata_svcparam_to_string,
	rdata_hip_to_string,
	rdata_atma_to_string,
	rdata_amtrelay_d_type_to_string,
	rdata_amtrelay_relay_to_string,
	rdata_unknown_to_string
};

int
rdata_atom_to_string(buffer_type *output, rdata_zoneformat_type type,
		     rdata_atom_type rdata, rr_type* record)
{
	return rdata_to_string_table[type](output, rdata, record);
}

ssize_t
rdata_wireformat_to_rdata_atoms(region_type *region,
				domain_table_type *owners,
				uint16_t rrtype,
				uint16_t data_size,
				buffer_type *packet,
				rdata_atom_type **rdatas)
{
	size_t end = buffer_position(packet) + data_size;
	size_t i;
	rdata_atom_type temp_rdatas[MAXRDATALEN];
	rrtype_descriptor_type *descriptor = rrtype_descriptor_by_type(rrtype);
	region_type *temp_region;

	assert(descriptor->maximum <= MAXRDATALEN);

	if (!buffer_available(packet, data_size)) {
		return -1;
	}

	temp_region = region_create(xalloc, free);

	for (i = 0; i < descriptor->maximum; ++i) {
		int is_domain = 0;
		int is_normalized = 0;
		int is_wirestore = 0;
		size_t length = 0;
		int required = i < descriptor->minimum;

		switch (rdata_atom_wireformat_type(rrtype, i)) {
		case RDATA_WF_COMPRESSED_DNAME:
		case RDATA_WF_UNCOMPRESSED_DNAME:
			is_domain = 1;
			is_normalized = 1;
			break;
		case RDATA_WF_LITERAL_DNAME:
			is_domain = 1;
			is_wirestore = 1;
			break;
		case RDATA_WF_BYTE:
			length = sizeof(uint8_t);
			break;
		case RDATA_WF_SHORT:
			length = sizeof(uint16_t);
			break;
		case RDATA_WF_LONG:
			length = sizeof(uint32_t);
			break;
		case RDATA_WF_LONGLONG:
			length = sizeof(uint64_t);
			break;
		case RDATA_WF_TEXTS:
		case RDATA_WF_LONG_TEXT:
			length = end - buffer_position(packet);
			break;
		case RDATA_WF_TEXT:
		case RDATA_WF_BINARYWITHLENGTH:
			/* Length is stored in the first byte.  */
			length = 1;
			if (buffer_position(packet) + length <= end) {
				length += buffer_current(packet)[length - 1];
			}
			break;
		case RDATA_WF_A:
			length = sizeof(in_addr_t);
			break;
		case RDATA_WF_AAAA:
			length = IP6ADDRLEN;
			break;
		case RDATA_WF_ILNP64:
			length = IP6ADDRLEN/2;
			break;
		case RDATA_WF_EUI48:
			length = EUI48ADDRLEN;
			break;
		case RDATA_WF_EUI64:
			length = EUI64ADDRLEN;
			break;
		case RDATA_WF_BINARY:
			/* Remaining RDATA is binary.  */
			length = end - buffer_position(packet);
			break;
		case RDATA_WF_APL:
			length = (sizeof(uint16_t)    /* address family */
				  + sizeof(uint8_t)   /* prefix */
				  + sizeof(uint8_t)); /* length */
			if (buffer_position(packet) + length <= end) {
				/* Mask out negation bit.  */
				length += (buffer_current(packet)[length - 1]
					   & APL_LENGTH_MASK);
			}
			break;
		case RDATA_WF_IPSECGATEWAY:
			assert(i>1); /* we are past the gateway type */
			switch(rdata_atom_data(temp_rdatas[1])[0]) /* gateway type */ {
			default:
			case IPSECKEY_NOGATEWAY:
				length = 0;
				break;
			case IPSECKEY_IP4:
				length = IP4ADDRLEN;
				break;
			case IPSECKEY_IP6:
				length = IP6ADDRLEN;
				break;
			case IPSECKEY_DNAME:
				is_domain = 1;
				is_normalized = 1;
				is_wirestore = 1;
				break;
			}
			break;
		case RDATA_WF_SVCPARAM:
			length = 4;
			if (buffer_position(packet) + 4 <= end) {
				length +=
				    read_uint16(buffer_current(packet) + 2);
			}
			break;
		case RDATA_WF_HIP:
			/* Length is stored in the first byte (HIT length)
			 * plus the third and fourth byte (PK length) */
			length = 4;
			if (buffer_position(packet) + length <= end) {
				length += buffer_current(packet)[0];
				length += read_uint16(buffer_current(packet) + 2);
			}
			break;
		case RDATA_WF_AMTRELAY_RELAY:
			assert(i>1);
			switch(rdata_atom_data(temp_rdatas[1])[0] & 0x7f) /* relay type */ {
			default:
			case AMTRELAY_NOGATEWAY:
				length = 0;
				break;
			case AMTRELAY_IP4:
				length = IP4ADDRLEN;
				break;
			case AMTRELAY_IP6:
				length = IP6ADDRLEN;
				break;
			case AMTRELAY_DNAME:
				is_domain = 1;
				is_normalized = 1;
				is_wirestore = 1;
				break;
			}
			break;
		}
		if (is_domain) {
			const dname_type *dname;

			if (!required && buffer_position(packet) == end) {
				break;
			}

			dname = dname_make_from_packet(
				temp_region, packet, 1, is_normalized);
			if (!dname || buffer_position(packet) > end) {
				/* Error in domain name.  */
				region_destroy(temp_region);
				return -1;
			}
			if(is_wirestore) {
				temp_rdatas[i].data = (uint16_t *) region_alloc(
                                	region, sizeof(uint16_t) + ((size_t)dname->name_size));
				temp_rdatas[i].data[0] = dname->name_size;
				memcpy(temp_rdatas[i].data+1, dname_name(dname),
					dname->name_size);
			} else {
				temp_rdatas[i].domain
					= domain_table_insert(owners, dname);
				temp_rdatas[i].domain->usage ++;
			}
		} else {
			if (buffer_position(packet) + length > end) {
				if (required) {
					/* Truncated RDATA.  */
					region_destroy(temp_region);
					return -1;
				} else {
					break;
				}
			}
			if (!required && buffer_position(packet) == end) {
				break;
			}

			temp_rdatas[i].data = (uint16_t *) region_alloc(
				region, sizeof(uint16_t) + length);
			temp_rdatas[i].data[0] = length;
			buffer_read(packet, temp_rdatas[i].data + 1, length);
		}
	}

	if (buffer_position(packet) < end) {
		/* Trailing garbage.  */
		region_destroy(temp_region);
		return -1;
	}

	*rdatas = (rdata_atom_type *) region_alloc_array_init(
		region, temp_rdatas, i, sizeof(rdata_atom_type));
	region_destroy(temp_region);
	return (ssize_t)i;
}

size_t
rdata_maximum_wireformat_size(rrtype_descriptor_type *descriptor,
			      size_t rdata_count,
			      rdata_atom_type *rdatas)
{
	size_t result = 0;
	size_t i;
	for (i = 0; i < rdata_count; ++i) {
		if (rdata_atom_is_domain(descriptor->type, i)) {
			result += domain_dname(rdata_atom_domain(rdatas[i]))->name_size;
		} else {
			result += rdata_atom_size(rdatas[i]);
		}
	}
	return result;
}

int
rdata_atoms_to_unknown_string(buffer_type *output,
			      rrtype_descriptor_type *descriptor,
			      size_t rdata_count,
			      rdata_atom_type *rdatas)
{
	size_t i;
	size_t size =
		rdata_maximum_wireformat_size(descriptor, rdata_count, rdatas);
	buffer_printf(output, " \\# %lu ", (unsigned long) size);
	for (i = 0; i < rdata_count; ++i) {
		if (rdata_atom_is_domain(descriptor->type, i)) {
			const dname_type *dname =
				domain_dname(rdata_atom_domain(rdatas[i]));
			hex_to_string(
				output, dname_name(dname), dname->name_size);
		} else {
			hex_to_string(output, rdata_atom_data(rdatas[i]),
				rdata_atom_size(rdatas[i]));
		}
	}
	return 1;
}

int
print_rdata(buffer_type *output, rrtype_descriptor_type *descriptor,
	    rr_type *record)
{
	size_t i;
	size_t saved_position = buffer_position(output);

	for (i = 0; i < record->rdata_count; ++i) {
		if (i == 0) {
			buffer_printf(output, "\t");
		} else if (descriptor->type == TYPE_SOA && i == 2) {
			buffer_printf(output, " (\n\t\t");
		} else {
			buffer_printf(output, " ");
		}
		if (!rdata_atom_to_string(
			    output,
			    (rdata_zoneformat_type) descriptor->zoneformat[i],
			    record->rdatas[i], record))
		{
			buffer_set_position(output, saved_position);
			return 0;
		}
	}
	if (descriptor->type == TYPE_SOA) {
		buffer_printf(output, " )");
	}

	return 1;
}

