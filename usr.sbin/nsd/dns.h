/*
 * dns.h -- DNS definitions.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef DNS_H
#define DNS_H

enum rr_section {
	QUESTION_SECTION,
	ANSWER_SECTION,
	AUTHORITY_SECTION,
	/*
	 * Use a split authority section to ensure that optional
	 * NS RRsets in the response can be omitted.
	 */
	OPTIONAL_AUTHORITY_SECTION,
	ADDITIONAL_SECTION,
	/*
	 * Use a split additional section to ensure A records appear
	 * before any AAAA records (this is recommended practice to
	 * avoid truncating the additional section for IPv4 clients
	 * that do not specify EDNS0), and AAAA records before other
	 * types of additional records (such as X25 and ISDN).
	 * Encode_answer sets the ARCOUNT field of the response packet
	 * correctly.
	 */
	ADDITIONAL_A_SECTION = ADDITIONAL_SECTION,
	ADDITIONAL_AAAA_SECTION,
	ADDITIONAL_OTHER_SECTION,

	RR_SECTION_COUNT
};
typedef enum rr_section rr_section_type;

/* Possible OPCODE values */
#define OPCODE_QUERY		0 	/* a standard query (QUERY) */
#define OPCODE_IQUERY		1 	/* an inverse query (IQUERY) */
#define OPCODE_STATUS		2 	/* a server status request (STATUS) */
#define OPCODE_NOTIFY		4 	/* NOTIFY */
#define OPCODE_UPDATE		5 	/* Dynamic update */

/* Possible RCODE values */
#define RCODE_OK		0 	/* No error condition */
#define RCODE_FORMAT		1 	/* Format error */
#define RCODE_SERVFAIL		2 	/* Server failure */
#define RCODE_NXDOMAIN		3 	/* Name Error */
#define RCODE_IMPL		4 	/* Not implemented */
#define RCODE_REFUSE		5 	/* Refused */
#define RCODE_YXDOMAIN		6	/* name should not exist */
#define RCODE_YXRRSET		7	/* rrset should not exist */
#define RCODE_NXRRSET		8	/* rrset does not exist */
#define RCODE_NOTAUTH		9	/* server not authoritative */
#define RCODE_NOTZONE		10	/* name not inside zone */

/* Standardized NSD return code.  Partially maps to DNS RCODE values.  */
enum nsd_rc
{
	/* Discard the client request.  */
	NSD_RC_DISCARD  = -1,
	/* OK, continue normal processing.  */
	NSD_RC_OK       = RCODE_OK,
	/* Return the appropriate error code to the client.  */
	NSD_RC_FORMAT   = RCODE_FORMAT,
	NSD_RC_SERVFAIL = RCODE_SERVFAIL,
	NSD_RC_NXDOMAIN = RCODE_NXDOMAIN,
	NSD_RC_IMPL     = RCODE_IMPL,
	NSD_RC_REFUSE   = RCODE_REFUSE,
	NSD_RC_NOTAUTH  = RCODE_NOTAUTH
};
typedef enum nsd_rc nsd_rc_type;

/* RFC1035 */
#define CLASS_IN	1	/* Class IN */
#define CLASS_CS	2	/* Class CS */
#define CLASS_CH	3	/* Class CHAOS */
#define CLASS_HS	4	/* Class HS */
#define CLASS_NONE	254	/* Class NONE rfc2136 */
#define CLASS_ANY	255	/* Class ANY */

#define TYPE_A		1	/* a host address */
#define TYPE_NS		2	/* an authoritative name server */
#define TYPE_MD		3	/* a mail destination (Obsolete - use MX) */
#define TYPE_MF		4	/* a mail forwarder (Obsolete - use MX) */
#define TYPE_CNAME	5	/* the canonical name for an alias */
#define TYPE_SOA	6	/* marks the start of a zone of authority */
#define TYPE_MB		7	/* a mailbox domain name (EXPERIMENTAL) */
#define TYPE_MG		8	/* a mail group member (EXPERIMENTAL) */
#define TYPE_MR		9	/* a mail rename domain name (EXPERIMENTAL) */
#define TYPE_NULL	10	/* a null RR (EXPERIMENTAL) */
#define TYPE_WKS	11	/* a well known service description */
#define TYPE_PTR	12	/* a domain name pointer */
#define TYPE_HINFO	13	/* host information */
#define TYPE_MINFO	14	/* mailbox or mail list information */
#define TYPE_MX		15	/* mail exchange */
#define TYPE_TXT	16	/* text strings */
#define TYPE_RP		17	/* RFC1183 */
#define TYPE_AFSDB	18	/* RFC1183 */
#define TYPE_X25	19	/* RFC1183 */
#define TYPE_ISDN	20	/* RFC1183 */
#define TYPE_RT		21	/* RFC1183 */
#define TYPE_NSAP	22	/* RFC1706 (deprecated by RFC9121) */
#define TYPE_NSAP_PTR	23	/* RFC1348 (deprecated by RFC9121) */
#define TYPE_SIG	24	/* 2535typecode */
#define TYPE_KEY	25	/* 2535typecode */
#define TYPE_PX		26	/* RFC2163 */
#define TYPE_GPOS	27	/* RFC1712 */
#define TYPE_AAAA	28	/* ipv6 address */
#define TYPE_LOC	29	/* LOC record  RFC1876 */
#define TYPE_NXT	30	/* 2535typecode */
#define TYPE_EID	31	/* draft-ietf-nimrod-dns-01 */
#define TYPE_NIMLOC	32	/* draft-ietf-nimrod-dns-01 */
#define TYPE_SRV	33	/* SRV record RFC2782 */
#define TYPE_ATMA	34	/* ATM Address */
#define TYPE_NAPTR	35	/* RFC2915 */
#define TYPE_KX		36	/* RFC2230 Key Exchange Delegation Record */
#define TYPE_CERT	37	/* RFC2538 */
#define TYPE_A6		38	/* RFC2874 */
#define TYPE_DNAME	39	/* RFC2672 */
#define TYPE_SINK	40	/* draft-eastlake-kitchen-sink */
#define TYPE_OPT	41	/* Pseudo OPT record... */
#define TYPE_APL	42	/* RFC3123 */
#define TYPE_DS		43	/* RFC 4033, 4034, and 4035 */
#define TYPE_SSHFP	44	/* SSH Key Fingerprint */
#define TYPE_IPSECKEY	45	/* public key for ipsec use. RFC 4025 */
#define TYPE_RRSIG	46	/* RFC 4033, 4034, and 4035 */
#define TYPE_NSEC	47	/* RFC 4033, 4034, and 4035 */
#define TYPE_DNSKEY	48	/* RFC 4033, 4034, and 4035 */
#define TYPE_DHCID	49	/* RFC4701 DHCP information */
#define TYPE_NSEC3	50	/* NSEC3, secure denial, prevents zonewalking */
#define TYPE_NSEC3PARAM 51	/* NSEC3PARAM at zone apex nsec3 parameters */
#define TYPE_TLSA	52	/* RFC 6698 */
#define TYPE_SMIMEA	53	/* RFC 8162 */
#define TYPE_HIP	55	/* RFC 8005 */
#define TYPE_NINFO	56	/* NINFO/ninfo-completed-template */
#define TYPE_RKEY	57	/* RKEY/rkey-completed-template */
#define TYPE_TALINK	58	/* draft-iet5f-dnsop-dnssec-trust-history */
#define TYPE_CDS	59	/* RFC 7344 */
#define TYPE_CDNSKEY	60	/* RFC 7344 */
#define TYPE_OPENPGPKEY 61	/* RFC 7929 */
#define TYPE_CSYNC	62	/* RFC 7477 */
#define TYPE_ZONEMD	63	/* RFC 8976 */
#define TYPE_SVCB	64	/* RFC 9460 */
#define TYPE_HTTPS	65	/* RFC 9460 */
#define TYPE_DSYNC	66	/* draft-ietf-dnsop-generalized-notify */

#define TYPE_SPF        99      /* RFC 4408 */

#define TYPE_NID        104     /* RFC 6742 */
#define TYPE_L32        105     /* RFC 6742 */
#define TYPE_L64        106     /* RFC 6742 */
#define TYPE_LP         107     /* RFC 6742 */
#define TYPE_EUI48      108     /* RFC 7043 */
#define TYPE_EUI64      109     /* RFC 7043 */

#define TYPE_TSIG	250
#define TYPE_IXFR	251
#define TYPE_AXFR	252
#define TYPE_MAILB	253	/* A request for mailbox-related records (MB, MG or MR) */
#define TYPE_MAILA	254	/* A request for mail agent RRs (Obsolete - see MX) */
#define TYPE_ANY	255	/* any type (wildcard) */
#define TYPE_URI	256	/* RFC 7553 */
#define TYPE_CAA	257	/* RFC 6844 */
#define TYPE_AVC	258	/* AVC/avc-completed-template */
#define TYPE_DOA	259	/* draft-durand-doa-over-dns */
#define TYPE_AMTRELAY	260	/* RFC 8777 */
#define TYPE_RESINFO	261	/* RFC 9606 */
#define TYPE_WALLET	262	/* WALLET/wallet-completed-template */
#define TYPE_CLA	263	/* CLA/cla-completed-template */
#define TYPE_IPN	264	/* IPN/ipn-completed-template */

#define TYPE_TA		32768	/* http://www.watson.org/~weiler/INI1999-19.pdf */
#define TYPE_DLV	32769	/* RFC 4431 */
#define PSEUDO_TYPE_TA	RRTYPE_DESCRIPTORS_LENGTH
#define PSEUDO_TYPE_DLV	(RRTYPE_DESCRIPTORS_LENGTH + 1)

#define SVCB_KEY_MANDATORY		0
#define SVCB_KEY_ALPN			1
#define SVCB_KEY_NO_DEFAULT_ALPN	2
#define SVCB_KEY_PORT			3
#define SVCB_KEY_IPV4HINT		4
#define SVCB_KEY_ECH			5
#define SVCB_KEY_IPV6HINT		6
#define SVCB_KEY_DOHPATH		7
#define SVCB_KEY_OHTTP			8
#define SVCB_KEY_TLS_SUPPORTED_GROUPS	9
#define SVCPARAMKEY_COUNT 10

#define MAXLABELLEN	63
#define MAXDOMAINLEN	255

#define MAXRDATALEN	64      /* This is more than enough, think multiple TXT. */
#define MAX_RDLENGTH	65535

/* Maximum size of a single RR.  */
#define MAX_RR_SIZE \
	(MAXDOMAINLEN + sizeof(uint32_t) + 4*sizeof(uint16_t) + MAX_RDLENGTH)

#define IP4ADDRLEN	(32/8)
#define IP6ADDRLEN	(128/8)
#define EUI48ADDRLEN	(48/8)
#define EUI64ADDRLEN	(64/8)

#define NSEC3_HASH_LEN 20

/*
 * The different types of RDATA wireformat data.
 */
enum rdata_wireformat
{
	RDATA_WF_COMPRESSED_DNAME,   /* Possibly compressed domain name.  */
	RDATA_WF_UNCOMPRESSED_DNAME, /* Uncompressed domain name.  */
	RDATA_WF_LITERAL_DNAME,      /* Literal (not downcased) dname.  */
	RDATA_WF_BYTE,               /* 8-bit integer.  */
	RDATA_WF_SHORT,              /* 16-bit integer.  */
	RDATA_WF_LONG,               /* 32-bit integer.  */
	RDATA_WF_LONGLONG,           /* 64-bit integer.  */
	RDATA_WF_TEXT,               /* Text string.  */
	RDATA_WF_TEXTS,              /* Text string sequence.  */
	RDATA_WF_A,                  /* 32-bit IPv4 address.  */
	RDATA_WF_AAAA,               /* 128-bit IPv6 address.  */
	RDATA_WF_BINARY,             /* Binary data (unknown length).  */
	RDATA_WF_BINARYWITHLENGTH,   /* Binary data preceded by 1 byte length */
	RDATA_WF_APL,                /* APL data.  */
	RDATA_WF_IPSECGATEWAY,       /* IPSECKEY gateway ip4, ip6 or dname. */
	RDATA_WF_ILNP64,             /* 64-bit uncompressed IPv6 address.  */
	RDATA_WF_EUI48,              /* 48-bit address.  */
	RDATA_WF_EUI64,              /* 64-bit address.  */
	RDATA_WF_LONG_TEXT,          /* Long (>255) text string. */
	RDATA_WF_SVCPARAM,           /* SvcParam <key>[=<value>] */
	RDATA_WF_HIP,                /* HIP rdata up to the Rendezvous Servers */
	RDATA_WF_AMTRELAY_RELAY      /* ip4, ip6, dname or nothing */
};
typedef enum rdata_wireformat rdata_wireformat_type;

/*
 * The different types of RDATA that can appear in the zone file.
 */
enum rdata_zoneformat
{
	RDATA_ZF_DNAME,		/* Domain name.  */
	RDATA_ZF_LITERAL_DNAME,	/* DNS name (not lowercased domain name).  */
	RDATA_ZF_TEXT,		/* Text string.  */
	RDATA_ZF_TEXTS,		/* Text string sequence.  */
	RDATA_ZF_BYTE,		/* 8-bit integer.  */
	RDATA_ZF_SHORT,		/* 16-bit integer.  */
	RDATA_ZF_LONG,		/* 32-bit integer.  */
	RDATA_ZF_LONGLONG,	/* 64-bit integer.  */
	RDATA_ZF_A,		/* 32-bit IPv4 address.  */
	RDATA_ZF_AAAA,		/* 128-bit IPv6 address.  */
	RDATA_ZF_RRTYPE,	/* RR type.  */
	RDATA_ZF_ALGORITHM,	/* Cryptographic algorithm.  */
	RDATA_ZF_CERTIFICATE_TYPE,
	RDATA_ZF_PERIOD,	/* Time period.  */
	RDATA_ZF_TIME,
	RDATA_ZF_BASE64,	/* Base-64 binary data.  */
	RDATA_ZF_BASE32,	/* Base-32 binary data.  */
	RDATA_ZF_HEX,		/* Hexadecimal binary data.  */
	RDATA_ZF_HEX_LEN,	/* Hexadecimal binary data. Skip initial length byte. */
	RDATA_ZF_NSAP,		/* NSAP.  */
	RDATA_ZF_APL,		/* APL.  */
	RDATA_ZF_IPSECGATEWAY,	/* IPSECKEY gateway ip4, ip6 or dname. */
	RDATA_ZF_SERVICES,	/* Protocol and port number bitmap.  */
	RDATA_ZF_NXT,		/* NXT type bitmap.  */
	RDATA_ZF_NSEC,		/* NSEC type bitmap.  */
	RDATA_ZF_LOC,		/* Location data.  */
	RDATA_ZF_ILNP64,	/* 64-bit uncompressed IPv6 address.  */
	RDATA_ZF_EUI48,		/* EUI48 address.  */
	RDATA_ZF_EUI64,		/* EUI64 address.  */
	RDATA_ZF_LONG_TEXT,	/* Long (>255) text string. */
	RDATA_ZF_UNQUOTED,	/* Unquoted text string. */
	RDATA_ZF_UNQUOTEDS,	/* A sequence of unquoted text strings. */
	RDATA_ZF_TAG,		/* A sequence of letters and numbers. */
	RDATA_ZF_SVCPARAM,	/* SvcParam <key>[=<value>] */
	RDATA_ZF_HIP,		/* HIP rdata up to the Rendezvous Servers */
	RDATA_ZF_ATMA,		/* ATM Address */
	RDATA_ZF_AMTRELAY_D_TYPE,/* Discovery Optional and Type */
	RDATA_ZF_AMTRELAY_RELAY,/* ip4, ip6, dname or nothing */
	RDATA_ZF_UNKNOWN	/* Unknown data.  */
};
typedef enum rdata_zoneformat rdata_zoneformat_type;

struct rrtype_descriptor
{
	uint16_t    type;	/* RR type */
	const char *name;	/* Textual name.  */
	uint32_t    minimum;	/* Minimum number of RDATAs.  */
	uint32_t    maximum;	/* Maximum number of RDATAs.  */
	uint8_t     wireformat[MAXRDATALEN]; /* rdata_wireformat_type */
	uint8_t     zoneformat[MAXRDATALEN]; /* rdata_zoneformat_type  */
};
typedef struct rrtype_descriptor rrtype_descriptor_type;

/*
 * Indexed by type.  The special type "0" can be used to get a
 * descriptor for unknown types (with one binary rdata).
 *
 * CLA + 1
 */
#define RRTYPE_DESCRIPTORS_LENGTH  (TYPE_IPN + 1)
rrtype_descriptor_type *rrtype_descriptor_by_name(const char *name);
rrtype_descriptor_type *rrtype_descriptor_by_type(uint16_t type);

const char *rrtype_to_string(uint16_t rrtype);

/*
 * Lookup the type in the ztypes lookup table.  If not found, check if
 * the type uses the "TYPExxx" notation for unknown types.
 *
 * Return 0 if no type matches.
 */
uint16_t rrtype_from_string(const char *name);

const char *rrclass_to_string(uint16_t rrclass);
uint16_t rrclass_from_string(const char *name);

#endif /* DNS_H */
