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
 * Textual conventions for snmp
 *
 * $FreeBSD$
 */

#ifndef	_BSNMP_TEXT_CONV_H_
#define	_BSNMP_TEXT_CONV_H_

/* Variable display length string. */
#define	SNMP_VAR_STRSZ		-1

/*
 * 11 bytes - octets that represent DateAndTime Textual convention
 * and the size of string used to diplay that.
 */
#define	SNMP_DATETIME_OCTETS	11
#define	SNMP_DATETIME_STRSZ	32

/*
 * 6 bytes - octets that represent PhysAddress Textual convention
 * and the size of string used to diplay that.
 */
#define	SNMP_PHYSADDR_OCTETS	6
#define	SNMP_PHYSADDR_STRSZ	19

/* NTPTimeStamp. */
#define	SNMP_NTP_TS_OCTETS	8
#define	SNMP_NTP_TS_STRSZ	10

/* BridgeId. */
#define	SNMP_BRIDGEID_OCTETS		8
#define	SNMP_BRIDGEID_STRSZ		25
#define	SNMP_MAX_BRIDGE_PRIORITY	65535

/* BridgePortId. */
#define	SNMP_BPORT_OCTETS	2
#define	SNMP_BPORT_STRSZ	7
#define	SNMP_MAX_BPORT_PRIORITY	255

/* InetAddress. */
#define	SNMP_INADDRS_STRSZ	INET6_ADDRSTRLEN

enum snmp_tc {
	SNMP_STRING = 0,
	SNMP_DISPLAYSTRING = 1,
	SNMP_DATEANDTIME = 2,
	SNMP_PHYSADDR = 3,
	SNMP_ATMESI = 4,
	SNMP_NTP_TIMESTAMP = 5,
	SNMP_MACADDRESS = 6,
	SNMP_BRIDGE_ID = 7,
	SNMP_BPORT_ID = 8,
	SNMP_INETADDRESS = 9,
	SNMP_TC_OWN = 10,
	SNMP_UNKNOWN, /* keep last */
};

typedef char * (*snmp_oct2tc_f) (uint32_t len, char *octs, char *buf);
typedef char * (*snmp_tc2oid_f) (char *str, struct asn_oid *oid);
typedef int32_t (*snmp_tc2oct_f) (struct snmp_value *value, char *string);

enum snmp_tc snmp_get_tc(char *str);
char *snmp_oct2tc(enum snmp_tc tc, uint32_t len, char *octets);
char *snmp_tc2oid(enum snmp_tc tc, char *str, struct asn_oid *oid);
int32_t snmp_tc2oct(enum snmp_tc tc, struct snmp_value *value, char *string);

#endif /* _BSNMP_TEXT_CONV_H_ */
