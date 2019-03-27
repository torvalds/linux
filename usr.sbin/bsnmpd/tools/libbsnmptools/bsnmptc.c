/*-
 * Copyright (c) 2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Shteryana Shopova <syrinx@FreeBSD.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Textual conventions for OctetStrings
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <bsnmp/asn1.h>
#include <bsnmp/snmp.h>
#include "bsnmptc.h"
#include "bsnmptools.h"

/* OctetString, DisplayString */
static char *snmp_oct2str(uint32_t, char *, char *);
static char *snmp_str2asn_oid(char *, struct asn_oid *);
static int parse_octetstring(struct snmp_value *, char *);

/* DateAndTime */
static char *snmp_octstr2date(uint32_t, char *, char *);
static char *snmp_date2asn_oid(char * , struct asn_oid *);
static int parse_dateandtime(struct snmp_value *, char *);

/* PhysAddress */
static char *snmp_oct2physAddr(uint32_t, char *, char *);
static char *snmp_addr2asn_oid(char *, struct asn_oid *);
static int parse_physaddress(struct snmp_value *, char *);

/* NTPTimeStamp */
static char *snmp_oct2ntp_ts(uint32_t, char *, char *);
static char *snmp_ntp_ts2asn_oid(char *, struct asn_oid *);
static int parse_ntp_ts(struct snmp_value *, char *);

/* BridgeId */
static char *snmp_oct2bridgeid(uint32_t, char *, char *);
static char *snmp_bridgeid2oct(char *, struct asn_oid *);
static int parse_bridge_id(struct snmp_value *, char *);

/* BridgePortId */
static char *snmp_oct2bport_id(uint32_t, char *, char *);
static char *snmp_bport_id2oct(char *, struct asn_oid *);
static int parse_bport_id(struct snmp_value *, char *);

/* InetAddress */
static char *snmp_oct2inetaddr(uint32_t len, char *octets, char *buf);
static char *snmp_inetaddr2oct(char *str, struct asn_oid *oid);
static int32_t parse_inetaddr(struct snmp_value *value, char *string);

static char *snmp_oct2bits(uint32_t len, char *octets, char *buf);
static char *snmp_bits2oct(char *str, struct asn_oid *oid);
static int32_t parse_bits(struct snmp_value *value, char *string);

static struct snmp_text_conv {
	enum snmp_tc	tc;
	const char	*tc_str;
	int32_t		len;
	snmp_oct2tc_f	oct2tc;
	snmp_tc2oid_f	tc2oid;
	snmp_tc2oct_f	tc2oct;
} text_convs[] = {
	{ SNMP_STRING, "OctetString", SNMP_VAR_STRSZ,
	  snmp_oct2str, snmp_str2asn_oid, parse_octetstring },

	{ SNMP_DISPLAYSTRING, "DisplayString" , SNMP_VAR_STRSZ,
	  snmp_oct2str, snmp_str2asn_oid, parse_octetstring },

	{ SNMP_DATEANDTIME, "DateAndTime", SNMP_DATETIME_STRSZ,
	  snmp_octstr2date, snmp_date2asn_oid, parse_dateandtime },

	{ SNMP_PHYSADDR, "PhysAddress", SNMP_PHYSADDR_STRSZ,
	  snmp_oct2physAddr, snmp_addr2asn_oid, parse_physaddress },

	{ SNMP_ATMESI, "AtmESI", SNMP_PHYSADDR_STRSZ,
	  snmp_oct2physAddr, snmp_addr2asn_oid, parse_physaddress },

	{ SNMP_NTP_TIMESTAMP, "NTPTimeStamp", SNMP_NTP_TS_STRSZ,
	  snmp_oct2ntp_ts, snmp_ntp_ts2asn_oid, parse_ntp_ts },

	{ SNMP_MACADDRESS, "MacAddress", SNMP_PHYSADDR_STRSZ,
	  snmp_oct2physAddr, snmp_addr2asn_oid, parse_physaddress },

	{ SNMP_BRIDGE_ID, "BridgeId", SNMP_BRIDGEID_STRSZ,
	  snmp_oct2bridgeid, snmp_bridgeid2oct, parse_bridge_id },

	{ SNMP_BPORT_ID, "BridgePortId", SNMP_BPORT_STRSZ,
	  snmp_oct2bport_id, snmp_bport_id2oct, parse_bport_id },

	{ SNMP_INETADDRESS, "InetAddress", SNMP_INADDRS_STRSZ,
	  snmp_oct2inetaddr, snmp_inetaddr2oct, parse_inetaddr },

	{ SNMP_TC_OWN, "BITS", SNMP_VAR_STRSZ,
	  snmp_oct2bits, snmp_bits2oct, parse_bits },

	{ SNMP_UNKNOWN, "Unknown", SNMP_VAR_STRSZ, snmp_oct2str,
	  snmp_str2asn_oid, parse_octetstring }	/* keep last */
};

/* Common API */
enum snmp_tc
snmp_get_tc(char *str)
{
	int i;
	for (i = 0; i < SNMP_UNKNOWN; i++) {
		if (!strncmp(text_convs[i].tc_str, str,
		    strlen(text_convs[i].tc_str)))
			return (text_convs[i].tc);
	}

	return (SNMP_STRING);
}

char *
snmp_oct2tc(enum snmp_tc tc, uint32_t len, char *octets)
{
	uint32_t tc_len;
	char * buf;

	if (tc > SNMP_UNKNOWN)
		tc = SNMP_UNKNOWN;

	if (text_convs[tc].len > 0)
		tc_len = text_convs[tc].len;
	else
		tc_len = 2 * len + 3;

	if ((buf = malloc(tc_len)) == NULL ) {
		syslog(LOG_ERR, "malloc failed - %s", strerror(errno));
		return (NULL);
	}

	memset(buf, 0, tc_len);
	if (text_convs[tc].oct2tc(len, octets, buf) == NULL) {
		free(buf);
		return (NULL);
	}

	return (buf);
}

char *
snmp_tc2oid(enum snmp_tc tc, char *str, struct asn_oid *oid)
{
	if (tc > SNMP_UNKNOWN)
		tc = SNMP_UNKNOWN;

	return (text_convs[tc].tc2oid(str, oid));
}

int32_t
snmp_tc2oct(enum snmp_tc tc, struct snmp_value *value, char *string)
{
	if (tc > SNMP_UNKNOWN)
		tc = SNMP_UNKNOWN;

	return (text_convs[tc].tc2oct(value, string));
}

/*****************************************************
* Basic OctetString type.
*/
static char *
snmp_oct2str(uint32_t len, char *octets, char *buf)
{
	uint8_t binary = 0;
	uint32_t i;
	char *ptr;

	if (len > MAX_OCTSTRING_LEN || octets == NULL || buf == NULL)
		return (NULL);

	for (ptr = buf, i = 0; i < len; i++)
		if (!isprint(octets[i])) {
			binary = 1;
			buf += sprintf(buf, "0x");
			break;
		}

	for (ptr = buf, i = 0; i < len; i++)
		if (!binary)
			ptr += sprintf(ptr, "%c", octets[i]);
		else
			ptr += sprintf(ptr, "%2.2x", (u_char)octets[i]);

	return (buf);
}

static char *
snmp_str2asn_oid(char *str, struct asn_oid *oid)
{
	uint32_t i, len = 0;

	/*
	 * OctetStrings are allowed max length of ASN_MAXOCTETSTRING,
	 * but trying to index an entry with such a long OctetString
	 * will fail anyway.
	 */
	for (len = 0; len < ASN_MAXOIDLEN; len++) {
		if (strchr(",]", *(str + len)) != NULL)
			break;
	}

	if (len >= ASN_MAXOIDLEN)
		return (NULL);

	if (snmp_suboid_append(oid, (asn_subid_t) len) < 0)
		return (NULL);

	for (i = 0; i < len; i++)
		if (snmp_suboid_append(oid, (asn_subid_t) *(str + i)) < 0)
			return (NULL);

	return (str + len);
}

static int32_t
parse_octetstring(struct snmp_value *value, char *val)
{
	size_t len;

	if ((len = strlen(val)) >= MAX_OCTSTRING_LEN) {
		warnx("Octetstring too long - %d is max allowed",
		    MAX_OCTSTRING_LEN - 1);
		return (-1);
	}

	if ((value->v.octetstring.octets = malloc(len)) == NULL) {
		value->v.octetstring.len = 0;
		syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
		return (-1);
	}

	value->v.octetstring.len = len;
	memcpy(value->v.octetstring.octets, val, len);
	value->syntax = SNMP_SYNTAX_OCTETSTRING;

	return (0);
}

/*************************************************************
 * DateAndTime
 *************************************************************
 * rfc 2579 specification:
 * DateAndTime ::= TEXTUAL-CONVENTION
 *   DISPLAY-HINT "2d-1d-1d,1d:1d:1d.1d,1a1d:1d"
 *   STATUS	  current
 *   DESCRIPTION
 *	"A date-time specification.
 *
 *	field	octets	contents		range
 *	-----	------	--------		-----
 *	1	1-2	year*			0..65536
 *	2	3	month			1..12
 *	3	4	day			1..31
 *	4	5	hour			0..23
 *	5	6	minutes			0..59
 *	6	7	seconds			0..60
 *			(use 60 for leap-second)
 *	7	8	deci-seconds		0..9
 *	8	9	direction from UTC	'+' / '-'
 *	9	10	hours from UTC*		0..13
 *	10	11	minutes from UTC	0..59
 *
 *	* Notes:
 *	    - the value of year is in network-byte order
 *	    - daylight saving time in New Zealand is +13
 *
 *	For example, Tuesday May 26, 1992 at 1:30:15 PM EDT would be
 *	displayed as:
 *
 *		1992-5-26,13:30:15.0,-4:0
 */
static char *
snmp_octstr2date(uint32_t len, char *octets, char *buf)
{
	int year;
	char *ptr;

	if (len != SNMP_DATETIME_OCTETS || octets == NULL || buf == NULL)
		return (NULL);

	buf[0]= '\0';
	year = (octets[0] << 8);
	year += (octets[1]);

	ptr = buf;
	ptr += sprintf(ptr, "%4.4d-%.2d-%.2d, ", year, octets[2],octets[3]);
	ptr += sprintf(ptr, "%2.2d:%2.2d:%2.2d.%.2d, ", octets[4],octets[5],
	    octets[6],octets[7]);
	ptr += sprintf(ptr, "%c%.2d:%.2d", octets[8],octets[9],octets[10]);

	return (buf);
}

static char *
snmp_date2asn_oid(char *str, struct asn_oid *oid)
{
	char *endptr, *ptr;
	static const char UTC[3] = "UTC";
	int32_t saved_errno;
	uint32_t v;

	if (snmp_suboid_append(oid, (asn_subid_t) SNMP_DATETIME_OCTETS) < 0)
		return (NULL);

	/* Read 'YYYY-' and write it in two subs. */
	ptr = str;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (v > 0xffff)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != '-')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) ((v & 0xff00) >> 8)) < 0)
		return (NULL);
	if (snmp_suboid_append(oid, (asn_subid_t) (v & 0xff)) < 0)
		return (NULL);

	/* 'MM-' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != '-')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'DD,' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != '-')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'HH:' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != ':')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'MM:' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != ':')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'SS.' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != '.')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'M(mseconds),' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != ',')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'UTC' - optional */
	ptr = endptr + 1;
	if (strncmp(ptr, UTC, sizeof(UTC)) == 0)
		ptr += sizeof(UTC);

	/* '+/-' */
	if (*ptr == '-' || *ptr == '+') {
		if (snmp_suboid_append(oid, (asn_subid_t) (*ptr)) < 0)
			return (NULL);
	} else
		goto error1;

	/* 'HH:' */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (*endptr != ':')
		goto error1;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	/* 'MM' - last one - ignore endptr here. */
	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0)
		goto error;
	else
		errno = saved_errno;
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	return (endptr);

  error:
	errno = saved_errno;
  error1:
	warnx("Date value %s not supported", str);
	return (NULL);
}

/* Read a DateAndTime string eg. 1992-5-26,13:30:15.0,-4:0. */
static int32_t
parse_dateandtime(struct snmp_value *sv, char *val)
{
	char *endptr;
	uint32_t v;
	uint8_t	date[SNMP_DATETIME_OCTETS];

	/* 'YYYY-' */
	v = strtoul(val, &endptr, 10);
	if (v > 0xffff || *endptr != '-')
		goto error;
	date[0] = ((v & 0xff00) >> 8);
	date[1] = (v & 0xff);
	val = endptr + 1;

	/* 'MM-' */
	v = strtoul(val, &endptr, 10);
	if (v == 0 || v > 12 || *endptr != '-')
		goto error;
	date[2] = v;
	val = endptr + 1;

	/* 'DD,' */
	v = strtoul(val, &endptr, 10);
	if (v == 0 || v > 31 || *endptr != ',')
		goto error;
	date[3] = v;
	val = endptr + 1;

	/* 'HH:' */
	v = strtoul(val, &endptr, 10);
	if (v > 23 || *endptr != ':')
		goto error;
	date[4] = v;
	val = endptr + 1;

	/* 'MM:' */
	v = strtoul(val, &endptr, 10);
	if (v > 59 || *endptr != ':')
		goto error;
	date[5] = v;
	val = endptr + 1;

	/* 'SS.' */
	v = strtoul(val, &endptr, 10);
	if (v > 60 || *endptr != '.')
		goto error;
	date[6] = v;
	val = endptr + 1;

	/* '(deci-)s,' */
	v = strtoul(val, &endptr, 10);
	if (v > 9 || *endptr != ',')
		goto error;
	date[7] = v;
	val = endptr + 1;

	/* offset - '+/-' */
	if (*val != '-' && *val != '+')
		goto error;
	date[8] = (uint8_t) *val;
	val = endptr + 1;

	/* 'HH:' - offset from UTC */
	v = strtoul(val, &endptr, 10);
	if (v > 13 || *endptr != ':')
		goto error;
	date[9] = v;
	val = endptr + 1;

	/* 'MM'\0''  offset from UTC */
	v = strtoul(val, &endptr, 10);
	if (v > 59 || *endptr != '\0')
		goto error;
	date[10] = v;

	if ((sv->v.octetstring.octets = malloc(SNMP_DATETIME_OCTETS)) == NULL) {
		warn("malloc() failed");
		return (-1);
	}

	sv->v.octetstring.len = SNMP_DATETIME_OCTETS;
	memcpy(sv->v.octetstring.octets, date, SNMP_DATETIME_OCTETS);
	sv->syntax = SNMP_SYNTAX_OCTETSTRING;
	return (1);

  error:
	warnx("Date value %s not supported", val);
	return (-1);
}

/**************************************************************
 * PhysAddress
 */
static char *
snmp_oct2physAddr(uint32_t len, char *octets, char *buf)
{
	char *ptr;
	uint32_t i;

	if (len != SNMP_PHYSADDR_OCTETS || octets == NULL || buf == NULL)
		return (NULL);

	buf[0]= '\0';

	ptr = buf;
	ptr += sprintf(ptr, "%2.2x", octets[0]);
	for (i = 1; i < 6; i++)
		ptr += sprintf(ptr, ":%2.2x", octets[i]);

	return (buf);
}

static char *
snmp_addr2asn_oid(char *str, struct asn_oid *oid)
{
	char *endptr, *ptr;
	uint32_t v, i;
	int saved_errno;

	if (snmp_suboid_append(oid, (asn_subid_t) SNMP_PHYSADDR_OCTETS) < 0)
		return (NULL);

	ptr = str;
	for (i = 0; i < 5; i++) {
		saved_errno = errno;
		v = strtoul(ptr, &endptr, 16);
		errno = saved_errno;
		if (v > 0xff) {
			warnx("Integer value %s not supported", str);
			return (NULL);
		}
		if (*endptr != ':') {
			warnx("Failed adding oid - %s", str);
			return (NULL);
		}
		if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
			return (NULL);
		ptr = endptr + 1;
	}

	/* The last one - don't check the ending char here. */
	saved_errno = errno;
	v = strtoul(ptr, &endptr, 16);
	errno = saved_errno;
	if (v > 0xff) {
		warnx("Integer value %s not supported", str);
		return (NULL);
	}
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	return (endptr);
}

static int32_t
parse_physaddress(struct snmp_value *sv, char *val)
{
	char *endptr;
	int32_t i;
	uint32_t v;
	uint8_t	phys_addr[SNMP_PHYSADDR_OCTETS];

	for (i = 0; i < 5; i++) {
		v = strtoul(val, &endptr, 16);
		if (v > 0xff) {
			warnx("Integer value %s not supported", val);
			return (-1);
		}
		if(*endptr != ':') {
			warnx("Failed reading octet - %s", val);
			return (-1);
		}
		phys_addr[i] = v;
		val = endptr + 1;
	}

	/* The last one - don't check the ending char here. */
	v = strtoul(val, &endptr, 16);
	if (v > 0xff) {
		warnx("Integer value %s not supported", val);
		return (-1);
	}
	phys_addr[5] = v;

	if ((sv->v.octetstring.octets = malloc(SNMP_PHYSADDR_OCTETS)) == NULL) {
		syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
		return (-1);
	}

	sv->v.octetstring.len = SNMP_PHYSADDR_OCTETS;
	memcpy(sv->v.octetstring.octets, phys_addr, SNMP_PHYSADDR_OCTETS);
	sv->syntax = SNMP_SYNTAX_OCTETSTRING;
	return (1);
}

/**************************************************************
 * NTPTimeStamp
 **************************************************************
 * NTP MIB, Revision 0.2, 7/25/97:
 * NTPTimeStamp ::= TEXTUAL-CONVENTION
 *    DISPLAY-HINT "4x.4x"
 *    STATUS	current
 *    DESCRIPTION
 *	""
 *    SYNTAX	OCTET STRING (SIZE(8))
 */
static char *
snmp_oct2ntp_ts(uint32_t len, char *octets, char *buf)
{
	char *ptr;
	uint32_t i;

	if (len != SNMP_NTP_TS_OCTETS || octets == NULL || buf == NULL)
		return (NULL);

	buf[0]= '\0';

	ptr = buf;
	i = octets[0] * 1000 + octets[1] * 100 + octets[2] * 10 + octets[3];
	ptr += sprintf(ptr, "%4.4d", i);
	i = octets[4] * 1000 + octets[5] * 100 + octets[6] * 10 + octets[7];
	ptr += sprintf(ptr, ".%4.4d", i);

	return (buf);
}

static char *
snmp_ntp_ts2asn_oid(char *str, struct asn_oid *oid)
{
	char *endptr, *ptr;
	uint32_t v, i, d;
	struct asn_oid suboid;
	int saved_errno;

	if (snmp_suboid_append(oid, (asn_subid_t) SNMP_NTP_TS_OCTETS) < 0)
		return (NULL);

	ptr = str;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0 || (v / 1000) > 9) {
		warnx("Integer value %s not supported", str);
		errno = saved_errno;
		return (NULL);
	} else
		errno = saved_errno;

	if (*endptr != '.') {
		warnx("Failed adding oid - %s", str);
		return (NULL);
	}

	memset(&suboid, 0, sizeof(struct asn_oid));
	suboid.len = SNMP_NTP_TS_OCTETS;

	for (i = 0, d = 1000; i < 4; i++) {
		suboid.subs[i] = v / d;
		v = v % d;
		d = d / 10;
	}

	ptr = endptr + 1;
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);
	if (errno != 0 || (v / 1000) > 9) {
		warnx("Integer value %s not supported", str);
		errno = saved_errno;
		return (NULL);
	} else
		errno = saved_errno;

	for (i = 0, d = 1000; i < 4; i++) {
		suboid.subs[i + 4] = v / d;
		v = v % d;
		d = d / 10;
	}

	asn_append_oid(oid, &suboid);
	return (endptr);
}

static int32_t
parse_ntp_ts(struct snmp_value *sv, char *val)
{
	char *endptr;
	int32_t i, d, saved_errno;
	uint32_t v;
	uint8_t	ntp_ts[SNMP_NTP_TS_OCTETS];

	saved_errno = errno;
	errno = 0;
	v = strtoul(val, &endptr, 10);
	if (errno != 0 || (v / 1000) > 9) {
		errno = saved_errno;
		warnx("Integer value %s not supported", val);
		return (-1);
	} else
		errno = saved_errno;

	if (*endptr != '.') {
		warnx("Failed reading octet - %s", val);
		return (-1);
	}

	for (i = 0, d = 1000; i < 4; i++) {
		ntp_ts[i] = v / d;
		v = v % d;
		d = d / 10;
	}
	val = endptr + 1;

	saved_errno = errno;
	errno = 0;
	v = strtoul(val, &endptr, 10);
	if (errno != 0 || (v / 1000) > 9) {
		errno = saved_errno;
		warnx("Integer value %s not supported", val);
		return (-1);
	} else
		errno = saved_errno;

	for (i = 0, d = 1000; i < 4; i++) {
		ntp_ts[i + 4] = v / d;
		v = v % d;
		d = d / 10;
	}

	if ((sv->v.octetstring.octets = malloc(SNMP_NTP_TS_OCTETS)) == NULL) {
		syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
		return (-1);
	}

	sv->v.octetstring.len = SNMP_NTP_TS_OCTETS;
	memcpy(sv->v.octetstring.octets, ntp_ts, SNMP_NTP_TS_OCTETS);
	sv->syntax = SNMP_SYNTAX_OCTETSTRING;
	return (1);
}

/**************************************************************
 * BridgeId
 **************************************************************
 * BRIDGE-MIB, REVISION		"200509190000Z"
 * BridgeId ::= TEXTUAL-CONVENTION
 *    STATUS	current
 *    DESCRIPTION
 *	"The Bridge-Identifier, as used in the Spanning Tree
 *	Protocol, to uniquely identify a bridge.  Its first two
 *	octets (in network byte order) contain a priority value,
 *	and its last 6 octets contain the MAC address used to
 *	refer to a bridge in a unique fashion (typically, the
 *	numerically smallest MAC address of all ports on the
 *	bridge)."
 *    SYNTAX	OCTET STRING (SIZE (8))
 */
static char *
snmp_oct2bridgeid(uint32_t len, char *octets, char *buf)
{
	char *ptr;
	uint32_t i, priority;

	if (len != SNMP_BRIDGEID_OCTETS || octets == NULL || buf == NULL)
		return (NULL);

	buf[0]= '\0';
	ptr = buf;

	priority = octets[0] << 8;
	priority += octets[1];
	if (priority > SNMP_MAX_BRIDGE_PRIORITY) {
		warnx("Invalid bridge priority %d", priority);
		return (NULL);
	} else
		ptr += sprintf(ptr, "%d.", octets[0]);

	ptr += sprintf(ptr, "%2.2x", octets[2]);

	for (i = 1; i < 6; i++)
		ptr += sprintf(ptr, ":%2.2x", octets[i + 2]);

	return (buf);
}

static char *
snmp_bridgeid2oct(char *str, struct asn_oid *oid)
{
	char *endptr, *ptr;
	uint32_t v, i;
	int32_t saved_errno;

	if (snmp_suboid_append(oid, (asn_subid_t) SNMP_BRIDGEID_OCTETS) < 0)
		return (NULL);

	ptr = str;
	/* Read the priority. */
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);

	if (v > SNMP_MAX_BRIDGE_PRIORITY || errno != 0 || *endptr != '.') {
		errno = saved_errno;
		warnx("Bad bridge priority value %d", v);
		return (NULL);
	}

	if (snmp_suboid_append(oid, (asn_subid_t) (v & 0xff00)) < 0)
		return (NULL);

	if (snmp_suboid_append(oid, (asn_subid_t) (v & 0xff)) < 0)
		return (NULL);

	ptr = endptr + 1;
	for (i = 0; i < 5; i++) {
		saved_errno = errno;
		errno = 0;
		v = strtoul(ptr, &endptr, 16);
		errno = saved_errno;
		if (v > 0xff) {
			warnx("Integer value %s not supported", str);
			return (NULL);
		}
		if (*endptr != ':') {
			warnx("Failed adding oid - %s",str);
			return (NULL);
		}
		if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
			return (NULL);
		ptr = endptr + 1;
	}

	/* The last one - don't check the ending char here. */
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 16);
	errno = saved_errno;
	if (v > 0xff) {
		warnx("Integer value %s not supported", str);
		return (NULL);
	}
	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	return (endptr);
}

static int32_t
parse_bridge_id(struct snmp_value *sv, char *string)
{
	char *endptr;
	int32_t i, saved_errno;
	uint32_t v;
	uint8_t	bridge_id[SNMP_BRIDGEID_OCTETS];

	/* Read the priority. */
	saved_errno = errno;
	errno = 0;
	v = strtoul(string, &endptr, 10);

	if (v > SNMP_MAX_BRIDGE_PRIORITY || errno != 0 || *endptr != '.') {
		errno = saved_errno;
		warnx("Bad bridge priority value %d", v);
		return (-1);
	}

	bridge_id[0] = (v & 0xff00);
	bridge_id[1] = (v & 0xff);

	string = endptr + 1;

	for (i = 0; i < 5; i++) {
		v = strtoul(string, &endptr, 16);
		if (v > 0xff) {
			warnx("Integer value %s not supported", string);
			return (-1);
		}
		if(*endptr != ':') {
			warnx("Failed reading octet - %s", string);
			return (-1);
		}
		bridge_id[i + 2] = v;
		string = endptr + 1;
	}

	/* The last one - don't check the ending char here. */
	v = strtoul(string, &endptr, 16);
	if (v > 0xff) {
		warnx("Integer value %s not supported", string);
		return (-1);
	}
	bridge_id[7] = v;

	if ((sv->v.octetstring.octets = malloc(SNMP_BRIDGEID_OCTETS)) == NULL) {
		syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
		return (-1);
	}

	sv->v.octetstring.len = SNMP_BRIDGEID_OCTETS;
	memcpy(sv->v.octetstring.octets, bridge_id, SNMP_BRIDGEID_OCTETS);
	sv->syntax = SNMP_SYNTAX_OCTETSTRING;
	return (1);
}

/**************************************************************
 * BridgePortId
 **************************************************************
 * BEGEMOT-BRIDGE-MIB, LAST-UPDATED "200608100000Z"
 * BridgePortId ::= TEXTUAL-CONVENTION
 *    DISPLAY-HINT "1x.1x"
 *    STATUS	current
 *    DESCRIPTION
 *	"A port identifier that contains a bridge port's STP priority
 *	in the first octet and the port number in the second octet."
 *    SYNTAX	OCTET STRING (SIZE(2))
 */
static char *
snmp_oct2bport_id(uint32_t len, char *octets, char *buf)
{
	char *ptr;

	if (len != SNMP_BPORT_OCTETS || octets == NULL || buf == NULL)
		return (NULL);

	buf[0]= '\0';
	ptr = buf;

	ptr += sprintf(ptr, "%d.", octets[0]);
	ptr += sprintf(ptr, "%d", octets[1]);

	return (buf);
}

static char *
snmp_bport_id2oct(char *str, struct asn_oid *oid)
{
	char *endptr, *ptr;
	uint32_t v;
	int saved_errno;

	if (snmp_suboid_append(oid, (asn_subid_t) SNMP_BPORT_OCTETS) < 0)
		return (NULL);

	ptr = str;
	/* Read the priority. */
	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 10);

	if (v > SNMP_MAX_BPORT_PRIORITY || errno != 0 || *endptr != '.') {
		errno = saved_errno;
		warnx("Bad bridge port priority value %d", v);
		return (NULL);
	}

	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	saved_errno = errno;
	errno = 0;
	v = strtoul(ptr, &endptr, 16);
	errno = saved_errno;

	if (v > 0xff) {
		warnx("Bad port number - %d", v);
		return (NULL);
	}

	if (snmp_suboid_append(oid, (asn_subid_t) v) < 0)
		return (NULL);

	return (endptr);
}

static int32_t
parse_bport_id(struct snmp_value *value, char *string)
{
	char *endptr;
	int saved_errno;
	uint32_t v;
	uint8_t	bport_id[SNMP_BPORT_OCTETS];

	/* Read the priority. */
	saved_errno = errno;
	errno = 0;
	v = strtoul(string, &endptr, 10);

	if (v > SNMP_MAX_BPORT_PRIORITY || errno != 0 || *endptr != '.') {
		errno = saved_errno;
		warnx("Bad bridge port priority value %d", v);
		return (-1);
	}

	bport_id[0] = v;

	string = endptr + 1;
	v = strtoul(string, &endptr, 16);
	if (v > 0xff) {
		warnx("Bad port number - %d", v);
		return (-1);
	}

	bport_id[1] = v;

	if ((value->v.octetstring.octets = malloc(SNMP_BPORT_OCTETS)) == NULL) {
		syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
		return (-1);
	}

	value->v.octetstring.len = SNMP_BPORT_OCTETS;
	memcpy(value->v.octetstring.octets, bport_id, SNMP_BPORT_OCTETS);
	value->syntax = SNMP_SYNTAX_OCTETSTRING;
	return (1);
}
/**************************************************************
 * InetAddress
 **************************************************************
 * INET-ADDRESS-MIB, REVISION     "200502040000Z"
 * InetAddress ::= TEXTUAL-CONVENTION
 *   STATUS      current
 *   DESCRIPTION
 *       "Denotes a generic Internet address.
 *
 *        An InetAddress value is always interpreted within the context
 *        of an InetAddressType value.  Every usage of the InetAddress
 *        textual convention is required to specify the InetAddressType
 *        object that provides the context.  It is suggested that the
 *        InetAddressType object be logically registered before the
 *        object(s) that use the InetAddress textual convention, if
 *        they appear in the same logical row.
 *
 *        The value of an InetAddress object must always be
 *        consistent with the value of the associated InetAddressType
 *        object.  Attempts to set an InetAddress object to a value
 *        inconsistent with the associated InetAddressType
 *        must fail with an inconsistentValue error.
 *
 *        When this textual convention is used as the syntax of an
 *        index object, there may be issues with the limit of 128
 *        sub-identifiers specified in SMIv2, STD 58.  In this case,
 *        the object definition MUST include a 'SIZE' clause to
 *        limit the number of potential instance sub-identifiers;
 *        otherwise the applicable constraints MUST be stated in
 *        the appropriate conceptual row DESCRIPTION clauses, or
 *        in the surrounding documentation if there is no single
 *        DESCRIPTION clause that is appropriate."
 *   SYNTAX       OCTET STRING (SIZE (0..255))
 **************************************************************
 * TODO: FIXME!!! syrinx: Since we do not support checking the
 * consistency of a varbinding based on the value of a previous
 * one, try to guess the type of address based on the
 * OctetString SIZE - 4 for IPv4, 16 for IPv6, others currently
 * not supported.
 */
static char *
snmp_oct2inetaddr(uint32_t len, char *octets, char *buf)
{
	int af;
	void *ip;
	struct in_addr	ipv4;
	struct in6_addr	ipv6;

	if (len > MAX_OCTSTRING_LEN || octets == NULL || buf == NULL)
		return (NULL);

	switch (len) {
		/* XXX: FIXME - IPv4*/
		case 4:
			memcpy(&ipv4.s_addr, octets, sizeof(ipv4.s_addr));
			af = AF_INET;
			ip = &ipv4;
			break;

		/* XXX: FIXME - IPv4*/
		case 16:
			memcpy(ipv6.s6_addr, octets, sizeof(ipv6.s6_addr));
			af = AF_INET6;
			ip = &ipv6;
			break;

		default:
			return (NULL);
	}

	if (inet_ntop(af, ip, buf, SNMP_INADDRS_STRSZ) == NULL) {
		warn("inet_ntop failed");
		return (NULL);
	}

	return (buf);
}

static char *
snmp_inetaddr2oct(char *str __unused, struct asn_oid *oid __unused)
{
	return (NULL);
}

static int32_t
parse_inetaddr(struct snmp_value *value __unused, char *string __unused)
{
	return (-1);
}

/**************************************************************
 * SNMP BITS type - XXX: FIXME
 **************************************************************/
static char *
snmp_oct2bits(uint32_t len, char *octets, char *buf)
{
	int i, bits;
	uint64_t value;

	if (len > sizeof(value) || octets == NULL || buf == NULL)
		return (NULL);

	for (i = len, value = 0, bits = 0; i > 0; i--, bits += 8)
		value += octets[i] << bits;

	buf[0]= '\0';
	sprintf(buf, "0x%llx.",(long long unsigned) value);

	return (buf);
}

static char *
snmp_bits2oct(char *str, struct asn_oid *oid)
{
	char *endptr;
	int i, size, bits, saved_errno;
	uint64_t v, mask = 0xFF00000000000000;

	saved_errno = errno;
	errno = 0;

	v = strtoull(str, &endptr, 16);
	if (errno != 0) {
		warn("Bad BITS value %s", str);
		errno = saved_errno;
		return (NULL);
	}

	bits = 8;
	/* Determine length - up to 8 octets supported so far. */
	for (size = sizeof(v); size > 0; size--) {
		if ((v & mask) != 0)
			break;
		mask = mask >> bits;
	}

	if (size == 0)
		size = 1;

	if (snmp_suboid_append(oid, (asn_subid_t) size) < 0)
		return (NULL);

	for (i = 0, bits = 0; i < size; i++, bits += 8)
		if (snmp_suboid_append(oid,
		    (asn_subid_t)((v & mask) >> bits)) < 0)
			return (NULL);

	return (endptr);
}

static int32_t
parse_bits(struct snmp_value *value, char *string)
{
	char *endptr;
	int i, size, bits, saved_errno;
	uint64_t v, mask = 0xFF00000000000000;

	saved_errno = errno;
	errno = 0;

	v = strtoull(string, &endptr, 16);

	if (errno != 0) {
		warn("Bad BITS value %s", string);
		errno = saved_errno;
		return (-1);
	}

	bits = 8;
	/* Determine length - up to 8 octets supported so far. */
	for (size = sizeof(v); size > 0; size--) {
		if ((v & mask) != 0)
			break;
		mask = mask >> bits;
	}

	if (size == 0)
		size = 1;

	if ((value->v.octetstring.octets = malloc(size)) == NULL) {
		syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
		return (-1);
	}

	value->v.octetstring.len = size;
	for (i = 0, bits = 0; i < size; i++, bits += 8)
		value->v.octetstring.octets[i] = (v & mask) >> bits;
	value->syntax = SNMP_SYNTAX_OCTETSTRING;
	return (1);
}
